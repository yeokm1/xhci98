# Issue 29 - On an HP EliteBook 8470p a Realtek RTL8157 is not usable on the USB 3 ports: its SuperSpeed link keeps failing and the driver warm-resets it for ever

Status: **fixed** in `3.4.3.0` (merge `e840d1df`; `4066a71e`, `418d5c06`, `ab30f3c9`;
design record 13 section 21), read on the 8470p (section 4a).
The fix does not mend the link: it sends the device to the connector's
USB 2.0 side, where it runs at High Speed (480 Mbit/s).

**Cause: this adapter's SuperSpeed link on these connectors (hardware).** The deciding reading
is Linux on the same laptop (section 3a): neither the RTL8157 nor a
stick held SuperSpeed on the left connectors there either, so this driver
is not needed for the failure (provisional; section 3a). What the driver owned was the endless warm-reset loop and the
missing move to USB 2.0, which the fix supplies.

**Scope.** Seen on the owner's HP EliteBook 8470p (Intel 7 Series xHCI
`8086:1E31`, subsystem `103C:179B`, BIOS 68ICF F.74; the issue 27
machine) under Windows XP x64 SP2 with `3.4.2.0` and
`XhciIntelPortSwitch` 1, with a Realtek RTL8157 5 GbE adapter
(`0BDA:8157`) on either USB 3 connector of the left side. The rule the fix
changes is the driver's own and reaches every controller and Windows
version: any device whose SuperSpeed link fails again and again after it
trains, on a root port or behind a SuperSpeed hub.

## 1. What was seen

The adapter did not appear in Device Manager. XHCITR's tree showed "the
device failed enumeration" on USB 2.0 port 4 and nothing on the USB 3
ports. On the right side, a connector Windows' own EHCI driver serves,
the same adapter works with `rtl815x` at High Speed. The same adapter ran
at SuperSpeed on development host A, passed through to guests, under the
host's own USB stack (`usb-wdm-free`,
`docs/testing/2026-11-07-rtl8157-hardware.md`), so the adapter is not
faulty. It has not been tried on the 8470p under Windows 10 or Linux.

## 2. The snapshot

Taken 143 s after the controller's start (the owner's report of
2026-11-09). The notes, decoded (record 13 section 21.1 has the table):

- USB 2.0 port 4: a connection whose enumeration reset ended disconnected
  (`enum.port.fail=04010001`: the reset, no retry);
- USB 3 port 8: SS.Inactive (PLS 6, CCS 0), warm-reset, trained (CCS,
  PED, U0, WRC and PRC), SS.Inactive again - four times noted, 346 more
  not noted once the port's note budget was spent;
- USB 3 port 7, after a fresh plug: enumerated at SuperSpeed (5 Gbit/s,
  slot 1, `rtl815x` started, 20 bulk transfers), its link failed under the
  bound adapter (the link keep refused it: not a UAS drive), enumerated
  again (slot 2), failed again before its PDO started, then the port-8
  pattern, 178 more not noted;
- `SsWarmResets` 270 (272 at the dump), `SsLinksGivenUp` 0.

## 3. The cause

Two parts.

**The link (not this driver's).** After every warm reset the link
trained and then fell to SS.Inactive within 100 ms: the reset's end began
the attach debounce, and no enumeration reset followed it. SS.Inactive is
a link failure with the device attached (USB 3.2 7.5.2). Nothing this
driver writes in those 100 ms explains it. The connector's signal path,
the cable, the adapter's power draw (its bMaxPower is 544 mA) and the
pairing of this adapter with this controller all remain possible; the
snapshot cannot tell them apart. Linux's xHCI quirks for `8086:1E31`
concern endpoint limits, bandwidth checking and shutdown, not the link;
its compliance-mode timer polls for Compliance Mode (not SS.Inactive) on
four HP workstations, not this laptop. Linux switches the ports the same
way (USB3_PSSEN, then XUSB2PR, the same masks) and enables U1/U2 on Intel
controllers where this driver keeps them off; neither explains a link that
fails in U0 (record 13 section 21.2).

**The loop (this driver's).** 29-A.2's warm-reset budget (three) is
refilled by every reading of a trained link, and each of these warm
resets ends trained, so the budget never ran out: the port was
warm-reset about twice a second for as long as the adapter stayed
plugged in, and the device never reached its USB 2.0 path. A device in
SS.Inactive stays there until a warm reset or until the host withdraws
its receiver terminations, which the host does by disabling the port
(PED = 1 takes a USB3 port to SS.Disabled from any state, xHCI 1.2c
4.19.1.2.1).

**Port 4** reads as the same device: on USB 2.0 first (its first
SuperSpeed attempt having failed), sent back to SuperSpeed by the
enumeration's own USB 2.0 reset, as USB 3.2 10.18.1 requires of a
device whose eSS.Disabled count is below 3. That is an inference: the
notes give neither port 4's device nor its speed. The bench kit's USB 2.0
hub reading checks it.

## 3a. The deciding reading: Linux on the same laptop

The owner booted Kubuntu 26.04.1 (Linux 7.0.0-30) live on the 8470p and
read the left connectors with the bench kit's `R8157.SH` (2026-11-09;
connectors as the owner labelled them):

- the RTL8157 in the front connector: "usb usb3-port4: Cannot enable.
  Maybe the USB cable is bad?" every 4 s for the whole minute; never
  enumerated and never on USB 2.0;
- the RTL8157 in the back connector: enumerated at SuperSpeed (`cdc_ncm`),
  then "USB disconnect" after 36 s, and not back;
- a SanDisk 3.2 Gen1 stick (`0781:55AB`) in the front connector: during
  its capture, "device descriptor read/8, error -110" at SuperSpeed twice
  and Linux's "attempt power cycle" on SuperSpeed port 3, an unnamed High
  Speed connection on USB 2.0 port 3, then the stick at High Speed on USB
  2.0 port 4. The log names no device for the SuperSpeed failures.

So the RTL8157 does not hold SuperSpeed on either left connector under
Linux either: this driver is not needed for the failure, and the cause
points to the laptop's SuperSpeed path (provisional: the exact fault is
not read, and the stick's reading settles nothing alone). Linux retries the RTL8157 without end, as `3.4.2.0`
did.

Open: the front connector's port numbers differ between the two devices
(SuperSpeed port 4 for the RTL8157; SuperSpeed port 3, then USB 2.0 ports
3 and 4, for the stick). Either Panther Point's SuperSpeed-to-USB 2.0
pairing on this laptop is not the usual one, or the port-4 lines were not
the RTL8157's. The kit's XP x64 readings, each naming its connector, pin
it.

## 4. The fix

Record 13 section 21. On a root port, the warm resets an SS.Inactive or
Compliance Mode link is given are counted in a 60 s window. The first
three are made as before. The fourth failure is sent to the connector's
USB 2.0 side: the port is disabled (PED) and held under 29-A.5's hold, of
a new kind with no identity, released when the device leaves the USB 2.0
side. Where no hold can be had (no USB 2.0 companion, the table full), the
link is given up instead, until a new plug-in. A working device whose link
fails once in a while is not touched.

Behind a SuperSpeed hub (record 13 section 21.5) the same count applies;
the fourth failure takes the hub's port to SS.Disabled, so the device
connects on the hub's USB 2.0 half, when that half is there; otherwise
the link is given up until the port reads empty. If the hub refuses the
request to disable its port, it is retried on the hub's next re-arm tick,
three attempts in all, then the link is given up (merge `63128209`; not
read on hardware, which needs a SuperSpeed hub between the adapter and the
port).

`XhciSsFlapFallback` (default 1, written by the install; tuning.md) turns
the move to USB 2.0 off: with 0 the fourth failure is given up instead,
so the link is reset three times a minute at most and the device stays
off until it is plugged in again.

On the 8470p the adapter is then expected on USB 2.0 port 3 or 4 at High
Speed, as on the right side. Owed: the owner's XP x64 reading with the bench
kit (`E:\xhci98-bench\p48-8470p`, not tracked), now only to confirm the
move to USB 2.0 and the High Speed path.

## 4a. Read on the 8470p with the fix

The owner's bench, 2026-11-09, XP x64 SP2, the release x64 build of
`8fbd4b41` (the merged code), XHCITR reports S1 to S6:

| Connector | Device | Result |
|---|---|---|
| front | RTL8157 | three warm resets, then sent to USB 2.0: High Speed on USB 2.0 port 4, the Realtek driver started (`link.flap` 08010001 to 08020004) |
| back | RTL8157 | the same, High Speed on USB 2.0 port 3 (port 7's record) |
| front | SanDisk 3.2 Gen1 stick | SuperSpeed on USB 3 port 8, a volume, a large file copied both ways |
| back | SanDisk 3.2 Gen1 stick | SuperSpeed on USB 3 port 7, the same |
| front, through a USB 2.0 hub | RTL8157 | High Speed behind the hub |
| front, `XhciSsFlapFallback` 0 | RTL8157 | three warm resets, then given up once (`SsLinksGivenUp` 1): no device, no loop |

So the front connector is USB 3 port 8 with USB 2.0 port 4, and the back
one port 7 with port 3. The connectors hold SuperSpeed for a stick under
this driver; it is the RTL8157's SuperSpeed link that fails on them, under
Linux as well (section 3a), which makes the adapter on these connectors
the cause rather than the laptop's SuperSpeed path as a whole.

## 5. Workarounds with `3.4.2.0`

- Use the right-side connector (Windows' EHCI driver, High Speed).
- Or set `XhciIntelPortSwitch` 0 (`portsw-off.bat`) and restart: the left
  connectors stay on the USB 2.0 controller, as the firmware leaves them,
  and every device there runs at High Speed at most.
- A USB 2.0 hub or a USB 2.0-only cable between the adapter and a left
  connector forces High Speed (to be read with the kit).

## 6. Open questions

- Whether the left connectors' SuperSpeed path is a fault of this unit
  (a connector, a cable inside, a redriver) or of the model: another
  8470p would tell.

## Sources

- The owner's report, 2026-11-09 (REPORT.TXT, the snapshot `B091738A`,
  XHCISET.TXT; not tracked).
- Design record 13 section 21.
- xHCI 1.2c 4.19.1.2.1 (p.279-280) and PORTSC (p.372, p.374); USB 3.2
  7.5.1.2, 7.5.2 and 10.18 (p.163-164, p.457).
- Linux v6.12 `drivers/usb/core/hub.c`, `drivers/usb/host/xhci-pci.c`,
  `drivers/usb/host/xhci.c` (facts only).
- `runs/run-45.md`, "45.41": an earlier user's RTL8157 on a VIA VL805
  whose link fell to SS.Inactive the same way (one event, dropped).
