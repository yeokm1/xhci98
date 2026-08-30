# The Phase 13 Test Equipment, As Measured

Every hub and device the project owner holds for the Phase 13 bench trips,
characterised on the modern Windows development host with
`scripts\hub-characterise.ps1` in batch 13-H, in the rig of
`build-and-test.md`, "The bench rig".

This file carries two records, kept apart. The first section, "Requirements
the Phase 13 clauses set, and how each was met", says what each Phase 13
clause needed and whether that need was met; it is the requirements list.
Everything after it is the characterisation record: what each physical unit
is. Where they overlap, the requirements table decides whether a clause could
be taken and the characterisation decides what the hardware measured.

The characterisation is not an inventory of what ought to exist. The
requirements table names the one item that does not (a Full-Speed USB 1.1
hub) and what its absence costs.

To re-take any of it: `powershell -ExecutionPolicy Bypass -File
scripts\hub-characterise.ps1`, with the tree assembled as the rig describes.
Every value below is a line of its output.

The `Speed` column, in every table below, is the signalling speed the devnode
negotiated, with its rate and the specification that introduced it:

| Reads | Rate | Introduced by |
|---|---|---|
| Low | 1.5 Mbps | USB 1.1 |
| Full | 12 Mbps | USB 1.1 |
| High | 480 Mbps | USB 2.0 |
| SuperSpeed | 5 Gbps (5000 Mbps) | USB 3.0 (the same rate USB 3.1 and 3.2 call Gen 1) |

That version is the one the speed belongs to, not the device's declared
`bcdUSB`. Three audio devices below run at a USB 1.1 speed while declaring
`bcdUSB=0200`, and the storage units declare 0300 or 0320 on a path whose rate
is USB 3.0's 5 Gbps. Where the declared version matters it has its own column.

---

## Requirements the Phase 13 clauses set, and how each was met

This is the shopping and verification list the bench trips were booked
against, not a packing checklist. It comes first because three of its rows
could not be checked at the bench: Windows' PnP layer will not say whether a
hub is multi-TT, and it will not say a device's `bInterval`. Those had to be
verified before travelling, or the trip could not close the task that needed
them. One bag served every bench batch.

The "Verify before the trip" column was batch 13-H's work. Every row was
verified in the rig it was used in (`build-and-test.md`, "The bench rig"),
because a child's negotiated speed is a property of the tree it sits in, and a
hub characterised with nothing behind it has been characterised in an
arrangement no session reproduces.

| # | Device | Needed by | Verify before the trip | Held, and how the row closed |
|---|---|---|---|---|
| 1 | Composite (multi-interface) USB device: any device exposing more than one interface | 13-E.1 | Nothing; it either binds or reads `Code 2`, which is the measurement | Yes, six specimens: `041E:323D` Sound Blaster Play! 2, `041E:30D3`, `041E:324D` Sound Blaster Play! 3, `041E:3249` Sound BlasterX G1, `0D8C:0014` C-Media USB Audio Device, and `041E:3278` Sound Blaster X4 (High Speed, UAC 2.0, seven interfaces, IAD-grouped). Every one is a composite carrying an HID: the five UAC 1.0 units are four interfaces each (AudioControl `01/01`, two AudioStreaming `01/02`, an HID `03/00`); the X4 is seven across two interface associations. So every unit is simultaneously row 1's specimen and a row 6 unit, which coupled tasks 13-E.1 and 13-E.3 (a non-composite audio device would have decoupled them), but not 13-E.3's clause 3, which runs on the X4 while clauses 1-2 run on a UAC 1.0 unit, so one `Code 2` could not take all three down. Six specimens also meant a `Code 2` at the bench could be checked against more than one unit |
| 2 | Windows 98 SE CD (not a USB device; it is what made row 1's cheap branch runnable) | 13-E.1 | That it is the SE CD and its USB components are on it | Confirmed by the project owner, and verified by hash on the host rather than at the bench: the disc's own `layout.inf` (out of `PRECOPY1.CAB`) lists the full USB stack, and its `usbd.sys` (18,912 bytes) hashes identically to `scripts\package\usbd-sources.expected`'s Win98 SE row, which also proves the disc is the media the package's `usbd98.sys` came from. The CD contents were copied onto the E460's own disk so stage E5 installs from hard disk; `run-13e.md` P4 has the recipe for checking a different disc |
| 3 | Multi-TT High-Speed hub, characterised | 13-E.2, and 13-E.3 (which needs only an HS hub, for the split-transaction audio path; this row or row 4) | `scripts\hub-characterise.ps1`. `TTT` does not discriminate; see row 4 | Yes: `1A40:0201`. `bcdUSB=0200`, a plain USB 2.0 hub with no SuperSpeed half, `bDeviceProtocol=2` with the second alternate setting (alt 1, protocol 2) present. One devnode reporting `Ports=7`, measured at both ends of the enclosure: all seven sockets sit at one tier behind one transaction translator, which is what makes H1-H4 comparable. Its socket numbering runs opposite to the physical order (one end is logical port 7, the other port 1), and it reads `TTT=0` |
| 4 | Single-TT High-Speed hub, characterised | 13-E.2 (the single-TT -> multi-TT replacement needs both hubs) | `scripts\hub-characterise.ps1`, same run as row 3 | Yes: `05E3:0608` (`bcdUSB=0200`, plain USB 2.0, `bDeviceProtocol=1`, alt 0 only), so no purchase was needed. `1A40:0101` is the better of the two and is the swap partner: plain USB 2.0, single-TT, 4 ports on one chip, no SuperSpeed half, same vendor and adjacent product ID to the multi-TT `1A40:0201`, so the replacement changes only the TT class. `05E3:0608` is the spare for two measured reasons: it is one enclosure containing two cascaded chips (chip 1 spends its port 1 on the internal link and offers ports 2-4 as sockets; chip 2 offers all four), so its seven sockets are not equivalent (three at tier 1 and four at tier 2, with different route strings and a different TT); and it reads `TTT=3`, the value the multi-TT `1A40:0201` reads `0` for. Two further specimens close the question: of the three `05E3:0610` units (the High-Speed halves of USB 3.0 hub units), one reads multi-TT and another single-TT while both report `TTT=3`, and all three report identical strings with no serial, so all three are indistinguishable in software and must be physically labelled |
| 5 | Full-Speed (USB 1.1) hub | 13-E.2: the FS-hub-behind-a-multi-TT-HS-hub case, where `MTT` has both of its independent causes | That it really is a 1.1 hub and not a High-Speed one | No. No purchase, and not in the release notes. None is held, and a genuine one is no longer reliably purchasable: most hardware sold as "USB 1.1" is a USB 2.0 hub, which answers nothing here. Batch 13-H characterised six hub units looking for one; every one reported High Speed. Task 13-E.2's FS-hub clause therefore ships as untested ground in `docs/using/release-notes.md`, naming the `test/test_init.c` host vectors and the batch 7b-V0 QEMU measurement as its only evidence. The clause reopens cheaply if a specimen appears; the likeliest source is a late-1990s keyboard or monitor with built-in USB ports, the check is `scripts\hub-characterise.ps1` and the reading is `bDeviceProtocol = 0`. See "What the missing Full-Speed hub costs" below |
| 6 | USB Audio class device declaring `bInterval > 1`, and not IAD-grouped | 13-E.3 | A descriptor read. Every device reachable in the VM vehicle reported `bInterval = 1`, so this is the one property that made the device worth carrying | Devices exist and no single one satisfies the row whole (the six specimens of row 1). On all five UAC 1.0 units every isochronous endpoint reads `bInterval = 1`; the not-IAD-grouped half is satisfied, since none has an interface association descriptor. All five are Full Speed and two declare `bcdUSB=0110`, which makes this structural rather than bad luck: a Full-Speed isochronous audio endpoint is serviced every frame, so `bInterval=1` is what the class of device means. The five are not five independent observations: four are Creative (`041E:323D`, `041E:30D3`, `041E:324D`, `041E:3249`) sharing one design lineage down to an identical four-interface shape and an identical `bInterval=32` HID endpoint on three of them; the fifth, `0D8C:0014` C-Media, is the independent one, the generic chip in a very large number of cheap USB audio adapters, whose HID endpoint reads `bInterval=2` rather than 32, so the isochronous 1 is not one vendor's firmware. The sixth device, `041E:3278` Sound Blaster X4, is High Speed, UAC 2.0 (`bInterfaceProtocol` 32 on its audio interfaces, where the five read 0), with isochronous endpoints carrying three distinct `bInterval` values (1, 3 and 4), which is Interval 0, 2 and 3 through this driver's High-Speed derivation, against the single value everything else in this project had ever produced. But descriptors are not an exercised derivation: an Endpoint Context is built only when a class driver selects an alt setting carrying the endpoint, both targets ship UAC 1.0 audio drivers, and the X4 measures `bNumConfigurations = 1`, so there is no UAC 1.0 fallback configuration. Whether a target binds it regardless was unmeasurable on the host, so the row became "carry it and find out", settled at the bench (`run-13e.md`, Finding Y: it does not bind). The X4 is IAD-grouped (seven interfaces across two associations), so it fails the row's other half: the row is carried on its `bInterval` half by the X4 and on its not-IAD half by the five. That blocked nothing, because the not-IAD requirement was a property of the VM passthrough rung (task 9-V.2), and on real silicon the device is simply plugged in. Being High Speed the X4 produces no split transactions behind a High-Speed hub, so clause 2 stayed with the Full-Speed units |
| 7 | Low-Speed keyboard or mouse (a USB 1.x HID) | the Low-Speed leg's free re-observation | Nothing; LS peripherals are almost always HID. Its behavioural half was already observed on the E460, so the row costs nothing to re-take while the machine is open | Yes, and verified Low Speed: `046D:C077` Logitech USB Optical Mouse, read at rig position H1 in the row 3/4 run. HID boot mouse (`03/01` proto 2), one interrupt IN endpoint at `bInterval=10` (10 ms) |
| 8 | Two or three ordinary High-/Full-Speed devices to hang off the hubs as children | 13-E.2: the hub replacement is only a test with children attached | Nothing | Yes, both characterised at rig position H2, one at a time: the USB 2.0 `0781:5408` SanDisk U3 Titanium (serial `000016A298742748`) and the USB 3.0 `090C:2320` MSSU10-128GSR (serial `AA000000000000003447`). They present identically (`speed=High`, Mass Storage `08/06` proto `80` (Bulk-Only), two bulk endpoints at 512), so "the 3.0 one is an ordinary High-Speed device behind a USB 2.0 hub" is measured rather than argued, and either serves H2. Keep the 2.0 one as the standing occupant anyway, because it fails safe: it is High Speed wherever it is plugged, where the 3.0 one silently becomes a different test (the unpowered-USB3-root-port fallback) in a root connector, a reading that is owed once, in position D. A third drive, `0781:55AB` SanDisk 3.2Gen1, presents the same way, so "two or three" is satisfied in full. A device's configuration descriptor is speed-dependent, which sharpens what "characterise it in the rig" means: on a SuperSpeed root port `090C:2320` presents two alternate settings (BOT plus UAS) with 1024-byte bulk endpoints, and behind a USB 2.0 hub the same unit presents one BOT-only alternate at 512. A descriptor read is a property of the device and the speed it enumerated at; see row 9, where that decides whether a clause is reachable |
| 9 | Physical BOT or UAS storage enclosure | 13-E.4: no vehicle in this project ever had one, and Phase 8's `+0` finding is a property of two QEMU device models that transfers to nothing on silicon | That it is a real enclosure rather than a flash drive; what matters is the bridge chip | Held: `174C:5106` ASMedia StoreJet Transcend (serial `NB202029162975`), a real USB-to-SATA bridge, reading `0210`/BOT+UAS/512 behind a USB 2.0 hub and `0300`/BOT+UAS/1024 on a SuperSpeed root port. It replaced an earlier enclosure that left the fleet and is not recorded in this repository; the departed unit is what E4.2's enumeration half was taken on, so the unit named here took the I/O half only, and the UAS finding is one specimen from one vendor. Expect BOT, but not because UAS is gated behind SuperSpeed: a bridge, the class of device that actually implements UAS, offers BOT and UAS (proto `0x62`, four bulk endpoints) at High Speed as well as SuperSpeed on the one unit this project holds, so at the descriptor level the "or UAS" branch is reachable through this driver. What is unavailable is a host class driver: UAS support arrived in Windows 8, and both targets ship BOT-only storage drivers (`usbstor.sys`; NUSB's on Windows 98), so alt 1 sits unselected and the enclosure runs BOT on both. That the speed is not what blocks it is observed rather than argued: at 480 Mbps behind a plain USB 2.0 multi-TT hub the modern host still binds `174C:5106` to `UASPStor` and selects alt 1, the same shape as the X4 in row 6, correct descriptors with no driver to use them. One consequence for Phase 8's open question: batch 8-A's backpressure latch never fired on the stated grounds that "no available class can fill a 62-TRB ring" because BOT is strictly serial, and UAS with four endpoints and command queueing is the class that could have; it is unreachable for host-driver reasons rather than anything about this driver, so that conclusion stands, now naming the real blocker. The unit's disk was already MBR with an active FAT32 first partition, the precondition the I/O half needed. The row is fully discharged: a drive letter plus a verified file round trip at position D on Windows 98 SE, shipping `0.0.0.5`. If it is bus-powered, it goes in rig position D and never behind a bus-powered hub |
| 10 | USB Ethernet adapter with a working driver on the target under test | 13-E.4's free re-observation (behavioural, on the E460); the counter reading it would have been paired with went with batch 13-T | That a driver for it exists on the target OS: `build-and-test.md` calls driver-absence "the single most common way a validation result gets misattributed". Read the chipset, not the packaging: ASIX parts have Win98 and Win2000 drivers, and the RTL8153 in most modern USB-C dongles does not | Yes: `0B95:7720` ASIX AX88772A (serial `000387`). High Speed, vendor class `FF/FF`, one interface, no IAD, an interrupt IN for link status (`bInterval=11`) plus bulk IN and bulk OUT at 512. It is the same chip task 8-V.2 validated Ethernet with on both targets, so the verify column was discharged by that record, and the bare-metal re-observation is a one-variable control |

Rows 3, 4 and 6 are the ones that turned a wasted trip into a productive one,
and rows 3 and 4 are one purchase in practice: a USB 3.0 hub unit's High-Speed
half is the usual multi-TT specimen, and most cheap 2.0 hubs are single-TT.

The "Held" column started as the project owner's list of what actually
existed, which covered rows 1, 5, 6, 7 and 8; rows 2, 3 and 4 were unknown
rather than present until batch 13-H read them. A blank was never a yes.

Rows 1 and 6 are one device for task 13-E.3's clauses 1 and 2, so tasks 13-E.1
and 13-E.3 were coupled there, and only there. Every USB Audio unit held is a
composite also exposing an HID, which is row 1's specimen. So the device
13-E.3 had to play through was the device 13-E.1 expected to read `Code 2`
on, since composite devices had not bound under the NUSB stack as first
measured on the E460. Had that gap been real on this machine, 13-E.3 would
have got a device that never binds, discharging neither of its first two
clauses.

The plan was therefore to run task 13-E.1's cheap branch first at the bench
(install the native USB stack from the SE CD, row 2, the one configuration a
composite had been seen to bind under) and only then attempt 13-E.3. That
made row 2 a prerequisite of the batch's audio work until it closed by hash.
Clause 3 sat outside the coupling because its device is the X4, a separate
unit whose own bind outcome is the clause's reading, so stage E6.3 was owed
whatever the other five did. How it went is in `run-13e.md`: Finding D found
the gap was one missing file, `usbhub.sys`, which removed the ordering rule's
premise.

What the missing Full-Speed hub costs. Row 5 is the
FS-hub-behind-a-multi-TT-HS-hub case, where `MTT` has both of its independent
causes at once and the marking must OR rather than assign. Without the hub
that case cannot be observed on metal at all, in any batch, and no other
device substitutes for it: the case needs a hub that has no transaction
translator sitting behind one that does. What survives without it: the host
vectors in `test\test_init.c` (an FS hub one tier down, and the split
transactions addressed to the HS hub's port rather than the leaf's), and the
batch 7b-V0 QEMU measurement of a 1.1 hub on a root port with a Full Speed
device behind it on both targets. So the shape is not unevidenced; it is
unevidenced on real silicon, which was Phase 13's whole subject.

Per the phase's closing rule this was a two-way decision due before the trip
was booked, and it was taken: publish the limitation, no purchase. The
alternative would have needed a genuine USB 1.1 hub, and much of what is sold
as one is a 2.0 hub, hence the "verify before the trip" column.
Nothing about the clause remains pending or may be carried into a later phase.

Row 7's mouse might not have been Low Speed, and that was checkable for free.
A USB HID mouse is usually Low Speed but plenty are Full Speed, and Windows'
PnP layer will not volunteer which. `scripts\hub-characterise.ps1` reports the
negotiated speed of every attached device, so the mouse was plugged into a hub
during the row 3/4 characterisation run and read there; one run answered rows
3, 4 and 7 together. Had it come back Full Speed the row would simply have
been unfilled, at small cost: the Low-Speed behavioural half was already
observed on the E460 with an LS keyboard (batch 7b-M, root port and behind a
High-Speed hub), and its remaining half is a trace line that needs a counter
channel regardless.

---

## Hubs

Four hub units, and neither VID:PID nor `TTT` predicts the
transaction-translator class.

| Unit | Bag? | VID:PID | `bcdUSB` | Speed | TT class | `TTT` | `wHubChar` | Sockets | Structure |
|---|---|---|---|---|---|---|---|---|---|
| Terminus 7-port | position T | `1A40:0201` | 0200 | High, 480 Mbps (USB 2.0) | multi-TT | 0 | `0x0088` | 7 | one devnode, barrel jack |
| Terminus 4-port | swap partner | `1A40:0101` | 0200 | High, 480 Mbps (USB 2.0) | single-TT | 3 | `0x00E0` | 4 | one devnode |
| Genesys 7-port | position H4 | `05E3:0608` | 0200 | High, 480 Mbps (USB 2.0) | single-TT | 3 | `0x00E0` | 7 | two cascaded chips |
| Genesys USB 3.0 | spare | `05E3:0610` + `05E3:0612` | 0210 / 0300 | High, 480 Mbps (USB 2.0) / SuperSpeed, 5 Gbps (USB 3.0) | multi-TT | 3 | `0x00E4` | 4 | USB 3.0 unit |

For a hub the `Speed` column follows from `bcdUSB` and the tree the devnode
sits on rather than from a separate reading: a hub carrying a transaction
translator is running at High Speed by definition, and the second devnode of a
USB 3.x unit is on the SuperSpeed tree by definition. The script itself cannot
report SuperSpeed as such; see the traps below.

The development host's two internal USB 3.0 hubs are not in this table. They
are part of the desktop (one on a root port, one behind it carrying the host's
keyboard and mouse) and they do not travel, so they are not test equipment.
They were read in the same run and two statements below rest on them: both
are `05E3:0610` on the High-Speed side with `05E3:0625` and `05E3:0626`
SuperSpeed halves, they report identical strings and no serial numbers, and
one is multi-TT while the other is single-TT. Counting the spare above, three
`05E3:0610` specimens sit on this host: two multi-TT, one single-TT.

`1A40:0201` <-> `1A40:0101` is task 13-E.2's replacement swap, and it changes
one variable: same vendor, adjacent product IDs, both plain USB 2.0 with no
SuperSpeed half, both one chip at one tier, and 4 ports is H1-H4.

`05E3:0608` is one enclosure containing two 4-port chips. Chip 1 (`7&64daed6`)
spends its port 1 on the internal link and offers ports 2-4; chip 2
(`8&29230d8d`) offers all four. Its seven sockets are therefore not
equivalent: three are tier 1 and four are tier 2, behind a different
translator. Socket map, counting from the cable end:

| Socket | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Chip | 1 | 1 | 1 | 2 | 2 | 2 | 2 |
| Port | **4** | 3 | 2 | **4** | 3 | 2 | **1** |

Bold entries are measured; the rest are monotonic interpolations between
measured endpoints of each group. The chip boundary itself is measured, not
inferred: chip 1 has exactly three external ports and socket 4 is on chip 2.

`1A40:0201`'s socket numbering runs opposite to its physical order: one end is
logical port 7, the other is port 1. H1-H4 are the first four sockets counting
from the port-1 end.

Both swap-partner maps were walked with `scripts\hub-characterise.ps1 -Walk`
and are in `build-and-test.md`, "Available Test Hardware".

What a trip packs from, in one line: both `1A40` units number backwards from
the cable end. On each of them `H1` is the socket furthest from the cable and
`H1` -> `H4` runs back towards it, so the same physical loading order gives
the same hub-port assignment on both, which is what makes the position-T swap
a one-variable test. The 7-port `1A40:0201` simply leaves its three cable-end
sockets (logical ports 7, 6, 5) unused. `05E3:0608`'s map is expressed the
same way. Label the sockets with their logical numbers before the bag travels.

---

## Devices

### Human interface

| Device | VID:PID | Speed | Class | Endpoints |
|---|---|---|---|---|
| Logitech USB Optical Mouse | `046D:C077` | Low, 1.5 Mbps (USB 1.1) | HID `03/01` proto 2 (boot mouse) | int IN, 4 B, `bInterval=10` (10 ms) |

Device-table row 7 asked whether it was Low Speed rather than Full. It is.

### Mass storage

All three flash drives are Bulk-Only over USB 2.0. A device's configuration
descriptor depends on the speed it enumerated at, so the two USB 3.0 drives
are recorded in both arrangements:

| Device | VID:PID | Serial | Speed | Behind a USB 2.0 hub | On a SuperSpeed root port |
|---|---|---|---|---|---|
| SanDisk U3 Titanium | `0781:5408` | `000016A298742748` | High, 480 Mbps (USB 2.0) only | 0200, BOT, 2 bulk @ 512 | (USB 2.0 device) |
| MSSU10-128GSR | `090C:2320` | `AA000000000000003447` | High, 480 Mbps (USB 2.0) / SuperSpeed, 5 Gbps (USB 3.0) | 0210, BOT only, 2 bulk @ 512 | 0320, BOT + UAS (proto `0x62`), 4 bulk @ 1024 |
| SanDisk 3.2Gen1 | `0781:55AB` | `01018f4d...` (128 hex chars) | High, 480 Mbps (USB 2.0) / SuperSpeed, 5 Gbps (USB 3.0) | 0210, BOT only, 2 bulk @ 512 | 0320, BOT only, 2 bulk @ 1024 |

| Device | VID:PID | Serial | Speed | Behind a USB 2.0 hub | On a SuperSpeed root port |
|---|---|---|---|---|---|
| StoreJet Transcend (USB-to-SATA bridge, ASMedia) | `174C:5106` | `NB202029162975` | High, 480 Mbps (USB 2.0) / SuperSpeed, 5 Gbps (USB 3.0) | 0210, BOT + UAS, 512 | 0300, BOT + UAS, 1024 |

The bridge row was read at the enclosure visit, not in batch 13-H like the
rest of the file, because the bridge that travels is not the unit that session
characterised: an earlier enclosure, held before the trip, left the fleet and
is not recorded here. Both cells above were measured on this host on that
date: the High-Speed one with the drive behind `1A40:0201` port 3, the
SuperSpeed one behind the host's internal `05E3:0625`.

One bench reading in `run-13e.md` was not taken on this unit: E4.2's
enumeration half, which the departed enclosure produced. Do not read it as
this one's. The I/O half is this one's, taken at position D on the E460 under
Windows 98 SE, shipping `0.0.0.5`, at the enclosure visit: a drive letter, and
a file written and read back with the contents matching (`run-13e.md`, the
enclosure-visit RUN box under stage E4).

Every claim in this section is read on `174C:5106` itself, so none of them
rests on a unit this file no longer records. What did change with the swap is
the count: this project holds one bridge chip, not two, and the UAS finding
below is a one-specimen reading accordingly.

This unit's disk is what task 13-E.4 needed. It is MBR, 64,023,257,088 bytes,
with partition 1 FAT32 and active (10.7 GB), the "FAT32 on MBR" the task named
as the precondition for its I/O half, which was then taken on it (`run-13e.md`,
stage E4's third RUN box). Partition 2 is NTFS (16 GB), which Windows 98 will
letter without being able to read: that is the trap the requirement exists to
avoid, so the FAT32 partition being first and active is what matters.

`bMaxPower` is not measured on this unit, because `hub-characterise.ps1` does
not report the field. "Bus-powered, therefore position D and never behind a
bus-powered hub" is an inference from the form factor, not a reading.

Two of two flash drives change `bcdUSB` and endpoint size with the path, and
so does the bridge; that part is a property of USB, not of a device.

UAS is not speed-gated. `090C:2320`, a flash drive, offers UAS only on the
SuperSpeed path, but the bridge this project holds offers BOT and UAS at High
Speed as well (ASMedia `174C:5106`, measured), which is what the class of
device that actually implements UAS does. That is one specimen from one
vendor, and it is enough because the claim it refutes is universal: a single
bridge offering UAS at 480 Mbps falsifies "UAS is gated behind SuperSpeed",
and the host-side half below rests on the driver binding rather than on how
many units were read. `0781:55AB` has no UAS at either speed.

What makes UAS unreachable on the targets is the host, not the bus, and since
the enclosure visit that is an observation rather than an argument. UAS
support arrived in Windows 8; Windows 98 and Windows 2000 SP4 ship BOT-only
storage drivers, so alt 1 sits in the descriptor unselected and this enclosure
runs BOT on both. The modern host supplies the other half directly: with
`174C:5106` sitting behind a plain USB 2.0 multi-TT hub at 480 Mbps, Windows
still binds it to `UASPStor` and selects alt 1. A host that has a UAS driver
therefore takes the UAS alternate setting at High Speed given the chance, so
what leaves it unselected on the targets is the missing driver and not the
speed. That is the same shape as the Sound Blaster X4 below: correct
descriptors, no driver to use them.

It also settles a hope worth naming: batch 8-A's backpressure latch has never
fired because "no available class can fill a 62-TRB ring" with
strictly-serial BOT, and UAS with four endpoints and command queueing is the
class that could have. It is out of reach for host-driver reasons rather than
anything about this driver.

`174C:5106` satisfies device-table row 9: a real bridge chip, and the first
one this driver has done data-verified I/O through on real silicon (E4.2's I/O
half). The three flash drives do not and never could: a flash controller is
the storage, where a bridge translates to SATA, with its own firmware,
queueing and error paths. Phase 8's storage results all came from QEMU device
models that never produced a single error completion code across ~19,000
transfer events.

### Networking

| Device | VID:PID | Serial | Speed | Class | Endpoints |
|---|---|---|---|---|---|
| ASIX AX88772A | `0B95:7720` | `000387` | High, 480 Mbps (USB 2.0) | vendor `FF/FF` | int IN 8 B `bInterval=11` (128 ms); bulk IN/OUT @ 512 |

This is the same chip task 8-V.2 validated USB Ethernet with on both targets,
so a working driver is known to exist for it on Windows 98 and Windows 2000,
which is the one property that decides whether an Ethernet adapter is usable
at the bench at all.

### USB Audio

Six specimens: five UAC 1.0 units and the UAC 2.0 Sound Blaster X4. The five
UAC 1.0 units are all Full Speed, all four-interface composites carrying an
HID alongside the audio function, none IAD-grouped, and every isochronous
endpoint on every one of them reads `bInterval = 1`. The X4 is the exception
on every one of those counts (High Speed, seven interfaces, IAD-grouped, isoch
`bInterval` 1, 3 and 4) and has its own section below.

| Device | VID:PID | Serial | `bcdUSB` | Speed | UAC | Isoch `bInterval` | HID `bInterval` |
|---|---|---|---|---|---|---|---|
| Sound Blaster Play! 2 | `041E:323D` | `000000000057` | 0200 | Full, 12 Mbps | 1.0 | 1 | 10 |
| (no product string) | `041E:30D3` | `1401060001BE` | 0110 | Full, 12 Mbps | 1.0 | 1 | 32 |
| Sound Blaster Play! 3 | `041E:324D` | `00000052` | 0200 | Full, 12 Mbps | 1.0 | 1 | 32 |
| Sound BlasterX G1 | `041E:3249` | `00000051` | 0200 | Full, 12 Mbps | 1.0 | 1 | 32 |
| C-Media USB Audio Device | `0D8C:0014` | (none) | 0110 | Full, 12 Mbps | 1.0 | 1 | 2 |
| Sound Blaster X4 | `041E:3278` | `29F7657FFDE2C119` | 0200 | High, 480 Mbps | 2.0 | 1, 3 and 4 | 6 |

This is the one table whose `Speed` column omits the version, because the
`bcdUSB` column sits next to it: five of these run at USB 1.1's 12 Mbps and only
two of them declare a USB 1.1 `bcdUSB`.

The `UAC` column is `bInterfaceProtocol` on the audio interfaces: 0 is UAC
1.0, 32 (`0x20`) is UAC 2.0. The first five are UAC 1.0 Full-Speed devices and
the sixth is not, so it is the only one with an isochronous
`bInterval` above 1.

The five UAC 1.0 specimens are not five independent observations. Four are
Creative and share one design lineage, down to an identical interface shape
and an identical `bInterval=32` HID endpoint on three of them. The C-Media
unit is what the finding rests on: an independent vendor, the generic chip in
a very large number of cheap USB audio adapters, and its HID endpoint reads
`bInterval=2` rather than 32, so the isochronous 1 is not one vendor's
firmware habit. A Full-Speed isochronous audio endpoint is serviced every
frame, and that is what `bInterval=1` means here.

`bMaxPacketSize0` is missing from the table above, and bench session 1 made
it matter. Two of the Creative units (`041E:323D` and `041E:324D`) did not
enumerate at all on this driver on real xHCI silicon (`Unknown Device`, Code
22, no wizard), while the C-Media `0D8C:0014`, at the same speed and in the
same class, enumerated normally. Speed, composite-ness and topology were each
tested and eliminated as the discriminator (see
`docs/contributing/runs/run-13e.md`, "Session record - bench session 1"),
which left a descriptor field these two share and the C-Media does not.

`bMaxPacketSize0` is the one field that varies only at Full Speed (8, 16, 32
or 64, unknowable until the descriptor is read), and it drives a correction
path no other speed exercises: the driver addresses a Full-Speed device at an
assumed size and then issues an Evaluate Context to correct it. A device
already reporting the assumed size never touches that path, and neither Low
Speed (always 8) nor High Speed (always 64, set up front) ever does.

Measured the same day on the modern host, with `scripts\hub-characterise.ps1`
extended to report the field:

| Device | Speed | `bcdUSB` | `bMaxPacketSize0` | On `xhci98.sys`, before the fix |
|---|---|---|---|---|
| C-Media `0D8C:0014` | Full | 0110 | 8 | enumerates |
| Sound Blaster Play! 3 `041E:324D` | Full | 0200 | 16 | does not enumerate |
| Sound Blaster Play! 2 `041E:323D` | Full | 0200 | 64 | does not enumerate |

The two failing units share "not 8" rather than a value: 16 and 64 have
nothing in common except that each obliged the driver, which then assumed 8,
to correct EP0 after the fact.

Fixed, by changing `XHCI_EP0_MPS_FULL_INITIAL` from 8 to 64: both Creative
units now enumerate on the E460 and are named by the wizard, and the C-Media
control is unchanged. The last column above is the pre-fix reading, kept
because it is what the field values were diagnosed from. `bcdUSB` was the
rival hypothesis and is refuted: the fix touched the max packet size
assumption and nothing about `bcdUSB`, and both failing units declare 0200 and
now work. `1209:4704` is therefore no longer needed as a tie-breaker, though
it remains a Full-Speed `mps0`=8 specimen. See
`docs/contributing/runs/run-13e.md`, "Finding 2 - FIXED".

Still unread: `041E:30D3`, `041E:3249` and the X4 `041E:3278` were not on the
host for this run. The X4 is High Speed, so its 64 is fixed and carries no
information; the other two are Full Speed and would each add a specimen.

### Devices held for the Finding 2 discrimination set

Read on the host and added to the bench bag for the next visit. None of them
belongs to a device-table row; they exist only to settle which field is
responsible.

| Device | VID:PID | Speed | `bcdUSB` | `mps0` | Interfaces | Why it travels |
|---|---|---|---|---|---|---|
| (unnamed) | `1209:4704` | Full | 0200 | 8 | `00/00` | the tie-breaker: the two hypotheses predict opposite outcomes for it |
| Logitech | `046D:C099` | Full | 0200 | 64 | `00/00` | the control both hypotheses agree fails, and a third vendor |
| Microsoft Wired Keyboard 600 | `045E:0750` | Low | 0110 | 8 | two HID interfaces (`03/01` boot keyboard + `03/00`), no IAD | a Low-Speed composite: tests composite-ness with the speed variable removed, from the opposite side to the High-Speed X4. Being a keyboard, whether it types is stage E2.3's reading |

### The Sound Blaster X4, and why its descriptors are not the whole answer

`041E:3278` is a High-Speed UAC 2.0 device, seven interfaces across two
interface associations (a CDC serial function, `02/02` + `0A/00`, and the
audio function, `01/01` and `01/02`, protocol 32) plus an HID interface. Its
isochronous endpoints carry three distinct `bInterval` values: 1, 3 (4
microframes, 0.5 ms) and 4 (8 microframes, 1 ms). Through the driver's
High-Speed derivation (`Interval = bInterval - 1`,
`XhciIsochIntervalFromBInterval`) that is Interval 0, 2 and 3, where every
other device in this project has only ever produced Interval 0.

Having the right descriptors is not the same as exercising the derivation:

- An Endpoint Context is built only when a class driver selects an alt setting
  carrying that endpoint. Windows 98's `USBAUDIO.VXD` and Windows 2000's
  `usbaudio.sys` are UAC 1.0 drivers.
- `bNumConfigurations = 1` (measured): the X4 offers no UAC 1.0 fallback
  configuration, so there is nothing on it a UAC 1.0 driver was built to
  understand.
- Whether a target binds it anyway was measured on Windows 98: it does not.
  One HID child at Code 10, no composite parent, no audio devnode
  (`run-13e.md`, Finding Y), so `usbaudio.inf` never got as far as the
  descriptors it cannot parse. Windows 2000 is unobserved and unreachable on
  this fleet.

So this device converted device-table row 6 from "buy or publish" into "carry
it and find out", and it was carried: the question is settled on Windows 98 in
the direction that leaves clause 3 unreadable there.

One earlier belief is worth stating because the bench refuted it. Phase 9 had
established, in QEMU, that Windows 98's `USBAUDIO.VXD` bugchecks the machine,
and a Windows 98 audio limitation was published on that basis. Stages E6.1 and
E6.2 then played a physical UAC 1.0 composite cleanly on real xHCI silicon
under Windows 98, at a root port and behind a multi-TT hub (`run-13e.md`,
Finding X). The Phase 9 bugcheck was only ever reproduced on one emulated
device in QEMU, so it is a vehicle artefact, and the published limitation was
corrected. A five-run QEMU result had been treated as settled enough that no
bench reading could disturb it, which is the assumption the bench batch
exists to test.

The X4's own outcome is unchanged by any of that: it did not bind at all
(Finding Y), so it never reached the question it was carried to answer. Stage
E6.3 in `docs/contributing/runs/run-13e.md` is a step of batch 13-E: it
records whether `usbaudio.inf` matches a UAC 2.0 device at all, and if it
binds, what is heard. The Windows 98 audible result is all E6 owns.

Four things needed a Windows 2000 vehicle: the same three clauses on Windows
2000 metal (physical playback, playback behind a High-Speed hub, a device
declaring `bInterval > 1`) and the isochronous counter block. No Windows 2000
vehicle exists (Setup bugchecks on both fleet machines), so all four are
published as limitations. The Windows 98 machine does have a counter channel,
the PassThru read route shipping in every flavour since `0.0.0.6` and read
with `XHCISNAP`; only the Windows-2000-specific half of the clause had no
vehicle.

It is an addition, not a replacement. Being High Speed, it produces no split
transactions behind a High-Speed hub, so clause 2 still belongs to the
Full-Speed units above and the `bcdUSB=0110` ones are the purest for it.

### Two properties read across all six - one shared, one that splits them

- Every one is a composite carrying an HID interface, so the 13-E.1 / 13-E.3
  coupling survives all six for task 13-E.3's clauses 1-2. A non-composite
  audio device would have decoupled those tasks and changed the bench run
  order; six chances, none taken. (Clause 3 is decoupled anyway, but by device
  rather than by class: it runs on the X4 as a second unit, so stage E6.3 does
  not wait on the others.) That is a measured property of this equipment
  rather than an accident of owning one device.
- The five UAC 1.0 units are not IAD-grouped; the X4 is (two associations).
  Row 6 asks for not-IAD-grouped, but only for the QEMU passthrough rung task
  9-V.2 measured shut. On real silicon the device is simply plugged in, so it
  does not disqualify the X4 here.

---

## Position D versus behind the hub: measured, and it makes no difference

The rig's two positions are a root port (D) and behind the hub under test (T,
children at H1-H4). The two devices that will actually be used on a root port
at the bench, the `041E:323D` audio device and the `0B95:7720` Ethernet
adapter, were read in both arrangements, position D being a measured root port
(`root_hub30 5&1a10f0e2` port 2).

Both are byte-identical between the two, once port number and device address
are normalised: same negotiated speed, same interface list, same alternate
settings, same endpoints, same `bInterval`. So tier does not change what these
devices present, and the tree readings above already cover them in both
arrangements.

Two things that does not license:

- Speed does change the descriptor (see the storage table), and speed is a
  property of the tree a device sits in. "Tier makes no difference" holds for
  devices that negotiate the same speed either way; it is not a general rule.
- The one position-D observation the rig actually wants cannot be taken on
  this host at all. A USB 3.x device falling back to its USB 2.0 companion
  path is a property of `xhci98.sys` leaving those root ports unpowered; Windows
  powers them normally, so the fallback never happens here. That reading exists
  only at the bench, and what this host supplies is the control for it; see
  the closing section.

## Reading the output: four traps this equipment set contains

- `TTT` is not a single-vs-multi-TT discriminator. The six units read in this
  run (the four above plus the host's two internal hubs) read 3 on single-TT
  hubs, 3 on multi-TT hubs, and 0 on a multi-TT hub.
- `hub-characterise.ps1` cannot report SuperSpeed as such.
  `USB_NODE_CONNECTION_INFORMATION_EX` returns `UsbHighSpeed` for a SuperSpeed
  device, and the V2 request that separates them is refused from this script's
  handle. Any device declaring `bcdUSB >= 3.00` that reads "High" is annotated;
  read its endpoint sizes. 1024-byte bulk is the SuperSpeed path, 512 is the
  USB 2.0 one.
- A failed enumeration is latched. `DeviceFailedEnumeration` stays on the
  port until a physical disconnect, so re-running any diagnostic re-reports the
  stale verdict. Reseat before recording it as a finding. A known-good drive
  did this.
- Self-powered versus bus-powered is not readable in software and must be
  recorded by hand. Connecting `1A40:0201`'s barrel-jack PSU changed nothing
  in the script's entire output (same `wHubCharacteristics`, same
  `bHubControlCurrent`, same address, no re-enumeration), which generalises the
  older finding that `HubIsBusPowered` reads zero by construction.
  `bHubControlCurrent` is what the hub's own controller draws, not the per-port
  budget, so it was never going to move.

  What self-power changes is the current
  available to children, and that is only visible as a brown-out under a
  hungry one; a brown-out changes a negotiated speed, which would poison the
  very reading the characterisation exists to take. The rig is characterised
  self-powered with the PSU connected, so the storage enclosure may
  sit behind the hub rather than being confined to position D, and why the PSU
  goes in the bag.

## What the SuperSpeed halves are for, and what they are not for

A USB 3.x hub unit enumerates as two devnodes at the same port number on the
two parallel trees. Under `xhci98.sys` the SuperSpeed half should be absent,
because this driver leaves USB 3.x root ports unpowered by design. The pair of
readings above is the control for that observation, and the difference between
them is the observation itself.

Do not plan to identify a hub by its SuperSpeed half. The three `05E3:0610`
units on this host (the spare above and the desktop's two internal hubs) carry
different SuperSpeed PIDs (`0612`, `0625`, `0626`), so on a modern host they do
sort the units, but on the target the SuperSpeed half never enumerates at all.
The one field that would tell them apart is guaranteed absent on the machine
where telling them apart matters. Label the hubs physically.
