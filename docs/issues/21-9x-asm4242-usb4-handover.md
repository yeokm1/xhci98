# Issue 21 - On Windows 98 SE an on-board ASMedia ASM4242 USB4 host never answers the connection manager, because the firmware's USB4 handover runs only from ACPI

Status: **open; the product forms are being finished for `3.4.1.0`, and
on Windows 10 x64 the product form was read working on the owner's board
on 2026-11-04** (section 6a). The 98 SE bench of the product form is
owed. Before that: **cause found and a test image working on the owner's
board, 2026-11-02** (roadmap-usb4 tasks 46.11 and 46.12; design record 32
sections 12 and 12.7). With two board-specific test helpers in place, a
tunnelled NVMe drive hot-plugged at the desktop is enumerated, tunnelled
and started by Stor98 with a drive letter, and survived four further
unplug and replug cycles. Nothing of it is in a release: the IRQ step is
a DOS program run from `AUTOEXEC.BAT` and the handover a test build of
`xhcicm.sys` with the board's addresses compiled in. The product forms
are tasks 46.11 (the IRQ) and 46.12 (the handover). Problems found on
the way are tasks 46.13 to 46.16 (section 8).

**Scope.** Windows 98 SE (and by the same reasoning ME, unread) on a
machine whose USB4 host is an ASMedia ASM4242 and whose firmware hands
USB4 control to the operating system only through ACPI `_OSC` (the
firmware's own connection manager most likely holds the router until
then; section 5). **NT is affected too**: the board's firmware sends
the handover from its `_OSC` only to Windows 11 and later (its `_OSI`
test, design record 32 section 12.8.13), and on the owner's board
Windows 10 x64 22H2 (build 19045, CSM, Above 4G Decoding on) with
`3.4.0.0` bound the NHI (problem 0) and failed its USB4 start with inert
reason 12 (host router unanswered): ring 0's first control read was
submitted, its transmit reported stuck (`tbt.ring.tx-stuck`), and no
answer came, where on 98 SE before the handover the transmit completed
and nothing answered (owner's bench, 2026-11-03). A test image sending
the handover at the NHI's start on that system (design record 32
section 12.8.15.9) then made the router answer and the tunnelled drive
work, through two replugs (owner's bench, 2026-11-04): the cause is the
same on NT. 2000 to 10 are expected to behave
alike; Windows 2000 on the Standard PC computer type (issue 19) runs no
ACPI at all. The NT forms are task 46.12's NT part (record 32 section
12.8.15).
Thunderbolt 3 hosts such as issue 18's Alpine Ridge have no such
handover. Other boards with an ASM4242 are expected to need the same
command; whether their firmware's data byte (section 5) is the same is
unread.

Machine: the owner's desktop board with an on-board ASM4242 (its switch upstream port `1B21:2421`, downstream ports
`1B21:2423`, host interface `1B21:2425`), CSM boot, Windows 98 SE on an
internal NVMe drive the BIOS serves (C: in MS-DOS compatibility mode,
left to the BIOS by task 46.1), without ACPI. Device: an ORICO USB4 NVMe
enclosure built on the ASMedia ASM246x (device router `174C:2463`, its
PCIe switch `1B21:2463`), holding an SK hynix NVMe (`1C5C:174A`).

## 1. What was seen

The enclosure, attached at power-on, was served by the firmware's own
INT 13h driver (task 46.1 leaves a drive the BIOS has set up to the
BIOS). Plugged in after the start, nothing happened at all: no
connection-manager start, no tunnel, no drive. XHCITR said no
Thunderbolt or USB4 host was driven by `xhcicm.sys`.

## 2. The first layer: an IRQ Windows 98 SE could not give

Device Manager showed the router's downstream port at problem 12 and no
host interface behind it. The board's `$PIR` table has an entry for the
switch's upstream port only, so the downstream ports and the host
interface are routed through the upstream port's INTD swizzle, the link
the BIOS put on an IRQ that a PnP BIOS COM1 node holds, and that
Windows will not move ("This resource setting cannot be modified"), and
Windows 98 SE reports IRQ steering disabled because of errors in that
table.

A DOS program run before Windows (`SETLN2.COM`, test only) rewrites the
Interrupt Line of exactly two functions, the downstream port that leads
to the host interface and the host interface, to an IRQ Windows can
give. With it the
port started, the host interface was listed (as "PCI Universal Serial
Bus"), and `xhcicm.sys` installed by hand and started. The product form,
and why only those two functions, is design record 32 section 12
(task 46.11).

## 3. The second layer: a router that never answers

`xhcicm.sys` then failed its USB4 start (inert reason 12). The log
showed the first control read of router 0 sent on ring 0 - the transmit
descriptor's done bit written back, so DMA worked - and never answered:
the receive ring's producer stayed at 0, and the retry was never even
consumed. A dump of ring 0 added for this (task 46.11) showed it
programmed as Linux programs it.

Four test images, each one change, changed nothing:

| Image | Change | Result |
|---|---|---|
| T1 | Host Interface Reset before ring 0, held 10 ms | no answer |
| T2 | T1, plus interrupt status cleared and the no-auto-clear bit Linux sets | no answer |
| T3 | a one-dword read of router 0 first, as Linux does | no answer |
| T4 | 10-entry rings, as Linux uses | no answer |

## 4. Linux on the same board

A Linux 7.0 live disc on the same board was built for the comparison.
With `acpi=off` it did not reach a shell on this board. With ACPI it found
router 0 at once, with ring 0 programmed as ours. Its log has, before
the connection manager starts:

    ACPI: USB4 _OSC: OS controls USB3+ DisplayPort+ PCIe+ XDomain+

So the difference was ACPI, and specifically the USB4 handover.

## 5. What the firmware's handover does

The board's ACPI tables, copied from the live session and decompiled
(they stay out of this tree), show the handover. The platform-wide
`_OSC`, when the operating system asks for native USB4 control and is
not only querying, calls a firmware method that sends one command to a
mailbox in the USB4 host's switch upstream port, in its extended
configuration space:

- the command dword written at offset 0x420; while bits 11:8 read 1 the
  command is busy;
- the reply read at offset 0x430, ready when bits 11:8 are not 0, then
  written 0;
- the port put in D0 around the exchange, its previous power state put
  back after;
- the command 0x0102, with a byte from the firmware's own data area in
  bits 23:16. The USB4 `_OSC` sends a second command,
  0x0180, a capability query.

Windows 98 SE never runs `_OSC`, so most likely the firmware's own
connection manager keeps the host router (inferred from the firmware
source and from section 6, not read from the chip). Ring 0 accepts our request and gets no
answer from anything behind it.

## 6. The test image that works

Test image T5 (branch `p46-asmtest`, not merged) sends the same two
commands from `xhcicm.sys`'s start, before ring 0: the handover first,
then the query, the order Linux calls the two `_OSC`s. The two addresses
it needs - the ECAM page of the upstream port and the location of the
firmware's pointer to its data - come from an untracked header written
for the bench. It checks the port's identity before writing and sends
nothing if the firmware's data fails the checks the firmware itself
makes.

On the board:

- the firmware replied 0x100 to the handover and 0x000F0100 to the
  query;
- the router answered, and the connection manager reached active;
- the enclosure hot-plugged at the desktop was enumerated (USB4, lanes
  bonded), its PCIe tunnel built, its bridges numbered by issue 17's
  `XhciCmBridgeNumbering`, and the drive started by Stor98 as a
  removable unit with a drive letter;
- four further unplug and replug cycles were clean.

A first review of T5 caught a wrong address: the firmware's pointer had
been placed by summing two separate excerpts of a field list, skipping
the fields between them. The image was rebuilt before it reached the
board.

## 6a. The product forms (`3.4.1.0`, in progress)

- **The IRQ (46.11).** `XHCIPFW.VXD` writes "no IRQ" (`FFh`) to the
  Interrupt Line of every Thunderbolt or USB4 host interface and its lone
  parent bridge on 98 SE and ME (`XhciPfwNhiLine`, on by default), and
  `xhcicm.sys` polls the host interface there. The NVMe drive's own
  interrupt comes through the tunnel port and is untouched. This replaces
  the DOS program in `AUTOEXEC.BAT`.
- **The handover on 98 SE and ME (46.12).** `xhcicm.sys` reads the
  firmware's `_OSC` data from the ACPI tables itself and sends the two
  commands before ring 0 (design record 32 sections 12.7 and 12.8).
- **The handover on NT (46.12).** The board is identified from SMBIOS
  through WMI (route E); other boards only with the opt-in
  `XhciCmAsm4242Reads` (routes B, C and D). The mailbox is reached through
  Windows' PCI bus interface, never by mapping configuration space
  directly. That interface reaches offset `420h` on Vista, 7, 8, 8.1 and
  10, reads all ones there on 32-bit XP and XP x64, and on Windows 2000
  folds it onto the first 256 bytes: an emulator leg wrote the command
  into the switch's memory window. So every write is preceded by an alias
  test, and Windows 2000 is refused outright (design record 32 section
  12.8.15.16). For 2000, 32-bit XP and XP x64 the owner chose a narrow
  exception, the switch's own configuration page mapped for the two
  mailbox dwords only, behind the same opt-in (roadmap-usb4 46.25, in
  progress).

**Read on the owner's board, Windows 10 x64, 2026-11-04** (CSM, Above 4G
Decoding on, the test-signed kit of `3.4.1.0`'s tree at `62b07a0f`):

- Setup identified the board ("ASM4242 board: known; the USB4 handover is
  sent.") and added the filter to the switch's ports, the downstream
  port's needing a restart;
- after a cold start the handover was sent and answered (`100h`, the
  query `000F0100h`), and the router answered (`174C:2428`);
- the enclosure, replugged, reached a PCIe tunnel and its drive a letter
  on Windows' `stornvme`; after a restart the tunnel was "found at start"
  and the drive was there again.

The first report, after the cold start, showed no device router: the
enclosure was not attached at that start (owner); it was plugged in for
the second report.

## 7. Wrong turns

- **The ring was suspected first.** The causes were ranked inherited
  host-interface state, then ring start and timing, then the ACPI
  handover, then the packet. T1 to T4 tested the first two and the
  last; the third was the cause.
- **"Windows replaced some system files"** after a cold start was taken
  for a file replacement. It was the Registry Checker restoring an older
  registry, which had dropped the router's driver binding (task 46.14).

## 8. Found with it

Each is its own task; none is fixed.

- **A connect and drop loop (46.13).** In one session, after a first
  connection whose PCIe path failed, the enclosure connected and dropped
  17 times. Linux, with the enclosure left unauthorized (no PCIe
  tunnel), saw it drop 10.04 s after connecting; that the enclosure
  drops when its PCIe link has not come up in about ten seconds is
  inferred from that one reading. Our log suggests each reconnect ran
  beside the previous connection's teardown, whose sweep removed the
  new bridges. Inferred from a log without time stamps; to be proved.
- **The registry damaged and a VMM fault (46.14).** The Registry Checker
  restored the registry twice during the day with clean shutdowns; after
  the restore, plugging the enclosure in raised a page fault in VMM.
  Cause unread.
- **Speed (46.15).** CrystalDiskMark 2.2 read 505 MB/s and wrote 514 MB/s
  sequential, about half what the owner reads elsewhere; ATTO showed a
  fixed cost near 4.8 ms a request from 0.5 KiB to 2 MiB, and above
  2 MiB about 13 MB/s, which fits Windows 98's I/O supervisor cutting
  each request into 63 KiB pieces sent one at a time at that same cost
  (inferred from the numbers and the supervisor's code). Resolved 2026-11-03 (46.15, closed): Stor98 polled because
  `StorPollMode` 1 was still set from 46.1's readings (its
  `StorInterrupt` value read "polling (forced by StorPollMode)"); with
  0 it ran on the drive's own interrupt and read 1085/1062 MB/s
  sequential. The rates at 2 MiB and above were not followed up.
- **A USB flash drive with no letter (46.16)** once the enclosure is
  attached.

## 9. Rules kept

- **Compare with a system that works on the same board.** Four ring
  experiments said nothing; one Linux log line named the cause.
- **Read the firmware, not only the hardware.** The missing step lived
  in ACPI source the operating system never ran.
- **Board data stays out of the tree.** The test image takes its
  addresses from an untracked header; the product must find them itself.

## Sources

- The owner's capture folder under `temp\` (git-ignored, 2026-11-01 and
  2026-11-02): XHCITR reports `tr`, `tr2`, `tr3`, `tr4` to `tr11`, the
  Linux folders with their kernel logs and copied ACPI tables, the
  benchmark screenshots and the crash photos. Not tracked, and not
  quoted beyond the values above.
- The owner's Windows 10 x64 bench folder `temp\x870e-t11\` (2026-11-04):
  `XHCIINST.LOG` and XHCITR reports `a`, `b` and `c`.
- Design record 32 sections 12, 12.7 and 12.8; roadmap-usb4 tasks 46.11
  to 46.16 and 46.25.
- Issues 17 and 18 (the bridge numbering behind a tunnel on 98 SE).
