# Issue 13 - On the VIA VL805 no Low- or Full-Speed device ever appears: the controller's USB 2.0 speed table names only its integrated hub's link, and the driver read it as the vocabulary of every speed below that hub

Status: **fixed in `2.3.0.0` without hardware; not yet observed on a
VL805.** Reported as GitHub issue 18 by SweetLow against `2.2.0.0`, and
investigated on 2026-10-10 from the logs; the fix is roadmap task 36.49 (section 6), built and tested on
host vectors and in QEMU only - no VL805, and no controller with a USB 2.0
speed table that omits Full and Low Speed, is held by this project. The
release notes carry it as untested ground, with the switch that turns it
off.

Machines affected: those with a VIA Labs VL805 (`1106:3483`), the reporter's
a PCIe add-in card at revision `01`. Any controller whose USB 2.0 Supported
Protocol capability publishes a non-empty speed table without Full and Low
Speed - as xHCI 1.2c allows an xHC with an integrated hub to do (section 4) -
would be affected the same way; none other is known. No controller the
project has read is: every USB 2.0 table read so far lists IDs 1 to 3 as
Full, Low and High Speed (issue 11 section 6), and QEMU's controller
publishes no table at all.

## 1. Symptom

On the reporter's machine (an AMD Phenom II on an nForce board, the VL805 on
a PCIe card, Windows 98 SE with SweetLow's USB 2.0 stack) under `2.2.0.0`'s
`debug` flavour, a Low-Speed or Full-Speed device on any of the card's four
connectors never appears: no device, no error, no yellow bang. High-Speed
and SuperSpeed devices work. Under `1.2.0.0` (the frozen miniport) the same
devices showed as "Unknown Device". Under Windows 7 with Microsoft's driver
the same card and devices work.

`XHCIQUAL` qualified the controller and identified the one device it
enumerates, the card's own hub (`2109:3431`, "USB2.0 Hub", High Speed, on
xHCI port 1), but it walks root ports only and never reaches a device behind
that hub.

## 2. What the reading shows

The reporter's logs, held off-tree in `temp/via/` (git-ignored, the
maintainer's copy; no serial numbers are transcribed here):

- **The controller.** `PROBE.LOG` 72-117: `1106:3483` rev `01`, xHCI 1.00,
  32-byte contexts, AC64, PPC, 32 slots, 4 interrupters, 5 ports, 31
  scratchpad buffers, `HCCPARAMS2` 0, INTx on IRQ 15.
- **The speed tables.** `PROBE.LOG` 87-90 and 100-104: the USB 2.0 protocol
  covers port 1 alone with PSIC 1 and the single PSI DWORD `01E00023` - ID 3,
  480 Mb/s, High Speed; the USB 3.0 protocol covers ports 2 to 5 with PSIC 1
  and `00050134` - ID 4, 5 Gb/s. The USB 2.0 capability's DW2 reads
  `10060101` (line 101): PSIC 1, one port at 1, and protocol-defined bits
  17 and 18 set - **HSO** (High-speed Only) and **IHI** (Integrated Hub
  Implemented), xHCI 1.2c Table 7-15, p.488.
- **The driver's own map.** `freshboot.TXT` 54-66: `hc.pci=34831106`, two
  groups, port 1 USB 2.0 only, `map.usb2companion=0`, ports 2 to 5 USB 3.
  The driver's companion pairing (`xhci_caps.c`, `xhciPairCompanions`)
  pairs a USB 3 port with a USB 2.0 port only by the default layout, which a
  USB 2.0 group of one port cannot give four USB 3 ports; it leaves them
  unpaired, which is the right reading of IHI = 1 ("the Root Hub to External
  xHC port mapping does not adhere to the default mapping", Table 7-15). No
  change is needed there.
- **A device behind the hub is never given a slot.** Five `XHCISNAP` dumps
  at verbosity 3 (`freshboot`, then a Low-Speed device plugged in,
  `lowspeedin`, `+5` s, `+30` s, and removed, `lowspeedout`): the slot table
  holds slot 1 alone, the hub, configured at ID 3 (`lowspeed30.TXT`
  108-109); `SlotsEnabled` stays 1 (`freshboot.TXT` 124 and every later
  dump); the only completion code that moves is Success, 15 at boot, 38
  with the device plugged, 41 after removal (`freshboot.TXT` 147,
  `lowspeedin.TXT` 147, `lowspeedout.TXT` 147) - the hub's own transfers.
- **The extension images say more.** Decoded from the `.BIN` files against
  an offset table for the `2.2.0.0` tree (2026-10-10; offsets 10668/10672
  and 79156): commands issued and completed
  4/4 at boot and 10/10 once the device is plugged, through to its removal;
  hub port resets requested 0, then 3. Three resets and six more commands
  fit three Enable Slot and Disable Slot pairs, one per enumeration attempt,
  and `SlotsEnabled` is counted only after a placement succeeds
  (`hcd_enum.c`, the Enable Slot action), so its staying at 1 is
  consistent with a slot enabled and handed back each time. It is not an
  opcode trace.

## 3. The cause

A device behind a hub has no PORTSC of its own; the hub reports its speed
as a class, and the driver has to write a Protocol Speed ID into its Slot
Context. `HcdHubPlace` (`src/hcd_hub.c`) asked `XhciPortPsivForSpeed`
(`src/xhci_caps.c`) for the ID of that class on the hub's root port, and
`XhciPortPsivForSpeed` inverts the root port's PSI table, refusing a class
the table does not list - "a refusal rather than a default", the rule of
`implementation-invariants.md`, "Port Speed Decoding", written so that a
controller which reorders its IDs is never misread. On the VL805 the table
lists only High Speed, so every Full- and Low-Speed device behind the hub
was refused, its slot given back before Address Device, and nothing logged
on a shipping build: the refusal was traced only through `XHCI_DBG_VALUE`,
which only the `qemu` flavour emits. `1.2.0.0` had the same refusal in its
own slot code, which is why it showed "Unknown Device".

The diagnosis is a static reading of the logs and the code (about 95 %
confident, a second opinion concurring); the refusal itself was not
observed, because
nothing on a shipping build recorded it. The residual alternative is a
failure elsewhere in placement (allocation, topology), which would also give
Enable/Disable pairs but is not speed-specific.

## 4. The table is legal; the reading was wrong

The investigation first read the VL805's table as incomplete, since a
non-empty table replaces the defaults (xHCI 1.2c Table 7-8 and footnote
120, p.481 and p.485; transcribed in `xhci-data-structures.md` section
10.1). A second opinion corrected it, and the specification bears it
out:

- xHCI 1.2c 7.2.1, the note under Table 7-10 (p.482-483): "An xHC
  implementation that employs an Integrated Hub to provide USB Full-speed
  and Low-speed support and only provided a USB 2.0 High-speed BI may
  define a USB2 xHCI Supported Protocol Capability data structure with a
  single PSI Dword (PSIC = 1)", defining "PSIV = 3, PLT = 0, PFD = 0, PSIE =
  2, and PSIM = 480" - exactly `01E00023`.
- Table 7-15 (p.488): HSO set means the root ports themselves are "High-speed
  only", and section 4.24.2.1 (p.328) describes an integrated hub "connected
  to a Root Hub port associated with a High-speed Bus Instance to provide
  Low-speed (LS), Full-speed (FS), and High-speed (HS) functionality on
  External Ports".
- Section 4.5.2 (p.84): a Slot Context's Speed is "Defined by downstream
  facing port attached to the device"; Table 7-13 (p.485) gives Full Speed
  ID 1 and Low Speed ID 2.

So the root port's table describes the root port's own link - the
integrated hub's upstream link, High Speed only - and says nothing about the
speeds of the devices below that hub. The driver's error was to read it as
the vocabulary for them. The specification does not state, in one sentence,
that a device behind such a hub takes the default IDs for every possible
table; IDs 1 and 2 for those devices are the supported reading and
established practice (section 5), not a guarantee, which is why the fix
keeps an explicit table entry winning and has an off switch.

The VL805 datasheet (VIA Labs, "VL805 4-Port USB 3.0 Host Controller",
Revision 0.93, Sep. 4, 2014; SHA-256
`be2d93a38e80331596da351dbb9a70c34ee61afa252fb4e0608d4cbf991bf305`; held by
the maintainer, not redistributed; `docs/references/README.md`, "VIA Labs
VL805 datasheet") confirms the architecture and says nothing about speed
tables: section 1 (p.4), "Four down-stream ports support SuperSpeed(SS),
High-Speed (HS), Full-Speed (FS), and Low-Speed (LS)"; four USB 2.0 pin
pairs, `USB2D1` to `USB2D4` (section 4, p.8); `HCSPARAMS1` default
`05000420h` (section 7, p.18), five ports - so all four connectors' USB 2.0
paths run through the integrated hub on xHCI port 1. `HCCPARAMS` default
`002841EBh` (p.20) gives MaxPSASize 4: streams are supported, so roadmap
task 29-A.5's send-back to USB 2.0 is not this controller's path. And PCI
configuration offset `50-53h` (section 6, p.14) is "XHCI MCU Firmware
Version" (`FW_VERSION`, read-only), the value the next risk (section 7)
depends on.

## 5. What other systems do

Interface facts only, cited by place:

- Linux writes the Slot Context speed from the USB core's speed with fixed
  values - `SLOT_SPEED_FS`, `SLOT_SPEED_LS`, `SLOT_SPEED_HS` - and never
  reads the USB 2.0 PSI table for it (`external/linux/xhci-mem.c` 1121-1132,
  in `xhci_setup_addressable_virt_dev`). Mainline has no VIA quirk matching
  this symptom: `XHCI_RESET_ON_RESUME` for every VIA xHC, and for the VL805
  `XHCI_LPM_SUPPORT` and `XHCI_TRB_OVERFETCH`.
- FreeBSD writes 2, 3 and 1 for Low, High and Full Speed
  (`external/freebsd/xhci.c` 2652-2672), and Haiku 2, 1, 3 and 4
  (`external/haiku/xhci.cpp` 1627-1636); neither reads the USB 2.0 table.
- The Raspberry Pi kernel, whose boards carry a VL805, adds VL805 quirks:
  `XHCI_EP_CTX_BROKEN_DCS`, `XHCI_AVOID_DQ_ON_LINK`,
  `XHCI_VLI_SS_BULK_OUT_BUG`, and `XHCI_VLI_HUB_TT_QUIRK` when the firmware
  version at configuration dword `50h` is below `0x0138C0` (Full-Speed
  non-periodic transfers behind the internal TT). None addresses
  enumeration; they are section 7's risks.
- Windows 7 with Microsoft's driver works on the reporter's card; how it
  chooses the IDs is not read.

## 6. The fix (roadmap 36.49)

The options considered (`out/via18/report.md`, held off-tree, and a second
opinion's two answers):

- **A**: append PSI DWORDs for IDs 1 and 2 (`000C0021`, `05DC0012`) to the
  USB 2.0 group's captured table, for `1106:3483` only. Workable, but it
  rewrites data the driver keeps verbatim for the preflight and post-reset
  port-map comparison, reports synthetic entries as "listed", and must never
  append to an empty table.
- **B**: a gated default-ID fallback in the decode and the inverse lookup,
  with provenance kept; the table untouched.
- **C**: an ungated generic rule, as Linux, FreeBSD and Haiku have in
  effect.
- **D**: the Raspberry Pi VL805 quirks - only if, with LS/FS addressed,
  transfers fail.

And four ways to gate B: on the PCI id; on the observed pattern (USB 2.0
group, HSO and IHI set, an HS-only table); on the PCI id and the pattern;
or generally, for any controller, restricted to devices behind a hub with a
collision check. **The last was taken** (orchestrator decision on the second
opinion's recommendation, owner to confirm; the owner asked for the fix to
be in the
`2.2.0.3` test build): HSO and IHI describe the root port, not the numeric
IDs of downstream speeds, and the actual topology - a device behind a hub -
is the stronger evidence. On by default, because without it every Full- and
Low-Speed device on such a controller fails; off with
`XhciUsb2HubSpeedFallback` `0`, because a fallback is not risk-free: it
reaches Address Device and Configure Endpoint, a command that never answers
requests a controller-wide reset, and a failed Disable Slot quarantines the
record.

As built (2026-10-10, `implementation-invariants.md`, "Port Speed
Decoding", the downstream exception):

- **Eligibility.** A Full- or Low-Speed device behind a hub, on a USB 2.0
  group with a table (PSIC > 0), takes the default ID - 1 Full, 2 Low - when
  the table has no entry decoding to that class **and** does not list that
  ID at all. A table that names ID 1 as High Speed keeps ID 1 High Speed and
  the Full-Speed device is refused as before (`XHCI_HUB_SPEED_CONFLICT`); a
  listed entry this driver cannot decode wins too. High Speed and
  SuperSpeed are never given a fallback. `XhciPortPsivForHubChild`
  (`src/xhci_caps.c`) decides, asked by `HcdHubPlace` from the graph's root
  port and Route String at placement.
- **Root ports unchanged.** PORTSC decoding follows today's rules exactly;
  task 35.1's USB 3 exception is untouched; the captured PSI dwords stay
  verbatim, so the port-map comparison is too.
- **Provenance, both directions.** The device record carries
  `SpeedFallback`; every later decode of that device's speed - endpoint
  policy, rate, the readdress check, the topology graph, the PDO's speed,
  the door's connection answers and `XHCISNAP`'s slot records - goes through
  `HcdDeviceSpeedClass` (`src/hcd_hub.c`), `XhciPortSpeedClassFor` and
  `XhciPortRateFor`, so a device addressed at ID 2 is read as Low Speed
  everywhere and never as unknown. The Slot Context receives the raw ID.
- **The switch.** `XhciUsb2HubSpeedFallback`, a REG_DWORD in the
  controller's driver key, written by every INF at 1 since roadmap-hcd 36.51
  (owner, 2026-10-11; by no INF before): only `0` turns it off; read at
  each start (`hub.speed.value`, `hub.speed.fallback`).
- **Diagnostics, ungated.** Counters appended at words 162 to 166 of the
  counter block: the switch as read and in effect, `TopoBehindHubNoSpeed`
  (every behind-hub placement refused for want of an ID, USB 2.0 or
  SuperSpeed hub), `Usb2HubSpeedFallbacks` (each placement that took the
  default ID), and `Usb2HubSpeedFallbackSlots` (the Slot IDs holding such a
  device). Bounded notes, 16 pairs a start: `hub.speed.place` (root port,
  hub slot, hub port, class, outcome) and `hub.speed.route` (ID given,
  Route String) for each refusal or fallback. `XHCISNAP` prints the policy,
  the counts and the slots. On this controller only, `pci.vl805.fw` notes
  configuration dword `50h` once at start, so a dump answers section 7's
  firmware question.
- **Host vectors** (`test/test_caps.c`): the VL805's capabilities word for
  word (Low and Full Speed placed at 2 and 1 and decoded back with their
  rates, High Speed by the table, the root port's 1 and 2 still unknown,
  the switch off refusing as before, the USB 3 group unchanged); a table
  mapping ID 1 to High Speed, one moving Full Speed to ID 5, one listing an
  undecodable 2, and PSIC 0; the notes' layouts and the firmware offset.
  `test/test_snap.c` pins words 162 to 166.

## 7. What is still open

- **The hardware reading.** The fix has never run on a VL805. In QEMU, which
  publishes no USB 2.0 table, it is a regression check only.
- **Full-Speed TT babble.** The Raspberry Pi kernel's `XHCI_VLI_HUB_TT_QUIRK`
  works around Full-Speed non-periodic transfers behind the VL805's internal
  TT on firmware below `0x0138C0`. If a Full-Speed device enumerates and its
  transfers then fail (Babble or Transaction Error), `pci.vl805.fw` says
  whether that firmware is in play.
- **Recovery and ring quirks.** `XHCI_EP_CTX_BROKEN_DCS`,
  `XHCI_AVOID_DQ_ON_LINK` and Linux's `XHCI_TRB_OVERFETCH` concern stopped
  endpoints, dequeue on a Link TRB and overfetch: risks after a stall or a
  cancellation, not of enumeration.

## 8. What the project keeps from it

- A root port's speed table describes that port's link. The speeds of the
  devices below an integrated hub are a different question, and the
  invariant now names the two directions apart.
- A refusal that only a debug trace records is invisible on the machines
  that hit it. Every behind-hub refusal is now counted and noted in every
  flavour.
- Issue 11's rule ("a listed entry wins") was kept intact a second time:
  the fallback applies only where the table is silent on both the speed and
  the ID.

## Sources

- GitHub issue 18 (SweetLow), and the logs held off-tree in `temp/via/`:
  `PROBE.LOG`, `LOWSPEED.LOG`, `freshboot`, `lowspeedin`, `lowspeed5`,
  `lowspeed30`, `lowspeedout` (`.TXT`, `.BIN`, `.PSC`).
- `out/via18/report.md` (held off-tree): the investigation and the options.
- `docs/references/spec-1.2c-dump.txt` (per-machine, git-ignored): 4.5.2
  p.84, 4.24.2.1 p.328, 7.2.1 p.482-483, Table 7-13 p.485, Table 7-15 p.488.
- The VL805 datasheet, Revision 0.93 (section 4 above; held by the
  maintainer).
- `docs/contributing/implementation-invariants.md`, "Port Speed Decoding";
  issue 11 (`11-sunrise-point-ssic-psi-table.md`), the USB 3 precedent.
- `external/linux/xhci-mem.c`, `external/freebsd/xhci.c`,
  `external/haiku/xhci.cpp`.
