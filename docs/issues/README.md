# Issues - the problems that shaped this driver

Long-form write-ups of the most instructive problems met during development:
what the symptom was, how it was found, how it was chased (including the wrong
turns), how it was fixed, and what rule the project kept from it. Each one is
a narrative distilled from the evidence documents. Where a narrative and the
evidence disagree, the evidence wins; the sources are listed at the foot of
each page.

Dates are 2026 unless stated. Task ids are the roadmap's.

Issues 1 to 3, 5 and 6 were fixed before `1.0.0.0`, so none of them is a
limitation of the release; those pages are here for the mechanism and for
how it was found (issue 5's fix is a registry value the release notes list
as a known limitation because it is machine-wide). Issue 4 was observed on the Windows XP guest on 2026-09-03 and
fixed the same day as roadmap task 19.7 in release `1.0.1.0`: a host vector
reproduces the mechanism, and the closing run on a clean install (`i4b`)
saw the restore recur on both devices and the fix carry them. The reading on
both primary targets that nothing changed there was taken the same night: the
device matrix on the Windows 98 SE and Windows 2000 guests plus the Windows 98
door sequence, with the counter at zero throughout. Nothing is owed.

| # | Issue | Status |
|---|---|---|
| 1 | [Getting a kernel log off a Windows 98 machine](01-windows-98-log-capture.md) - DebugView bugchecks real hardware, the driver owns no door of its own, and the way out was reading the driver's ring through usbport's own vendor IOCTL (`XHCISNAP`) | Fixed |
| 2 | [The bare-metal wedge, the PORTSC watchdog that "fixed" it, and what was actually wrong](02-bare-metal-wedge-and-portsc-watchdog.md) - five hot-plugs kill the controller on two Intel generations and never in QEMU; a polled sweep recovers it for the wrong reason; the cause is a recovery step nobody ever sends | Fixed |
| 3 | [Composite devices need `usbhub.sys`, and an xHCI-only machine never has it](03-usbhub-sys-composite-devices.md) - Code 2 on every multi-function device, blamed on NUSB for two weeks, settled by one file and a laptop that was not in the plan | Fixed |
| 4 | [A device Windows XP's hub re-creates mid-enumeration is failed by this driver](04-xp-restore-device-ep0-remove.md) - XP re-created a mass-storage device through a second device handle and removed the first one's EP0 last; the driver's REMOVE path unbinds whichever EP0 extension arrives, the live handle is refused for retry, and the progress detector fails the device. Replugging works | Fixed in `1.0.1.0` (task 19.7, closing run `i4b` 2026-09-03: the counter moved to 2 while both devices bound on their first attach; the same night the device matrix on both primary targets and the Windows 98 door sequence read unchanged on the same binary with the counter at 0) |
| 5 | [A device plugged into an idle Windows 98 controller is seen by nothing, and why the package writes `DisableSelectiveSuspend`](05-idle-suspend-and-disableselectivesuspend.md) - usbport idle-suspends the controller half a second after the bus goes quiet, a halted xHC cannot raise a port event, EHCI's re-armed interrupt has no xHCI equivalent, and the fix is usbport's own registry switch, machine-wide and measured on both Windows 98 stacks (present = 1 stops the idle; absent or 0 does not) | Fixed (task 11-V.6 on the Windows 98 path; `1.0.1.0` on the NT path) |
| 6 | [A Full-Speed device on a root port bugchecks both targets, and why every root port is reported as High Speed](06-full-speed-root-port-bugcheck.md) - usbport applies the EHCI model and looks up a transaction translator for any non-High-Speed root-port device; `USBPORT_GetTt` turns the root hub's empty TT list into a garbage pointer and the kernel faults on the first insertion, on both shipping builds; the one lever is the USB2 flag, so the driver reports every root port as High Speed and keeps the true speed for its own contexts, at the cost of 1/2/4 ms interrupt bands for Full and Low Speed devices on a root port | Fixed (Phase 5 task 7) |

## Other issues worth a page

These are recorded in [lessons.md](../contributing/lessons.md) and
[run-13e.md](../contributing/runs/run-13e.md) and would each carry a write-up
of the same shape. Listed roughly in order of how much they would teach a
reader.

- EP0's initial max packet size of 8 is babble on usbport. Two
  Sound Blasters read nothing (Code 22, no wizard). A field census of
  `bMaxPacketSize0` across the equipment showed the failing units shared only
  "not 8". usbport issues its first `GET_DESCRIPTOR` with `wLength=64` and the
  driver cannot change that, so a device answering in 16- or 64-byte packets
  on an endpoint declared as 8 dies on the first read. That is why Linux
  starts Full-Speed EP0 at 64. One constant changed.
- The multi-TRB short packet. A passed-through ASIX Ethernet
  adapter enumerated, bound, and never passed traffic: its 16 KB receive was a
  multi-TRB TD, the short packet landed on the first TRB, and QEMU's xHC
  emitted one Transfer Event where the specification (4.10.1.1.2) mandates
  two. Interrupt endpoints never go short, bulk OUT is exact, mass storage's
  short CSW is single-TRB; only this NIC could produce the case. The first fix
  was wrong and the test suite said so.
- Four thresholds in the recovery ladder, each wrong differently. A
  poll-counted deadline in somebody else's units; a retry budget spent by
  success that turned into an expiry date (3-for-3 then a dead port on the
  fourth); a backstop that skipped to the end of the ladder; a counter that
  recorded outcomes but not arrivals. The ladder around issue 2.
- Windows 98 offers a driver no way to write a file. `\??\` does
  not resolve under NTKERN, and opening `\DosDevices\C:\XHCI.LOG` for write
  never returns and hangs the boot inside `StartController`. The probe that
  established it was itself confounded once by asking for the wrong access
  mask. This is why the file sink in issue 1 died.
- Why a `0.0.0.4`-era debug binary did not load on real silicon (defect 2b,
  still open). That binary carried one import the release build did not,
  `HAL.dll!WRITE_PORT_UCHAR`, the port-`0xE9` writer, and the E460 gave it
  Code 2. Either the import did not resolve or something on that chipset
  decodes `0xE9`; the P6 binaries of `runs/run-13e.md` were built to separate
  the two and the cause has never been read. What is NOT open is the shipped
  article: the three-flavour split of task 13-L.1 moved every `XHCI_DBG_*`
  site and the `0xE9` mirror into the never-published `qemu` flavour, so the
  published `debug` flavour no longer carries the differing import at all.
  The split exists so the question can stay open without shipping it.
- The hub-churn wedge and the false green. 150 hub add/remove
  pairs were written up as clean on the strength of a screenshot and a single
  `info irq` sample; the guest was silently wedged (IDE IRQ frozen, clock
  stopped, desktop painted). The control two months later showed the churn
  wedges Windows 98 only under this driver, and that "cursor still tracking"
  had never been a symptom: the cursor being watched was the host's. Mechanism
  still unknown.
- Two driver defects no virtual machine could ever show. A static
  audit found a re-enumeration re-entering Address Device with `BSR=1` from a
  slot state the spec does not allow (the code comment cited a valid-state
  list that is not in the specification), and an isochronous Stop Endpoint
  doing single-TD arithmetic over a multi-TD group. QEMU's leniency selected
  for both.
- `usbhub20.sys` bugchecks Windows 2000 about a file that is present
 . `STATUS_OBJECT_NAME_NOT_FOUND` on a file that is there; the
  missing object was its import, `usbd.sys`, which SP4's `usb.inf` only copies
  for USB 1.1 controllers. The earlier, Windows 2000-side twin of issue 3, and
  the origin of the per-target `usbd.sys` carry.
- Windows 2000 Setup bugchecks on both real machines. Neither bugcheck code
  was captured; no cause is written into the record, and the project refuses
  to write one. Every Windows 2000 result in this repository is therefore a
  virtual-machine result, published as a limitation.
- Building a test bed for a 1999 OS in 2026: the QEMU local-APIC clock storm
  that hangs Windows 2000 Setup under TCG and not WHPX, diagnosed by sampling
  EIP from the monitor ("an interrupt storm and a dead machine look identical
  on the screen and are opposite in the registers"); the `-apic` workaround
  being unavailable to the very SMP VM that needs the second CPU; Windows 98
  needing `setup /p j` or PCI never enumerates.
- QEMU's emulated USB devices never fail. About 19,000 transfer events, zero
  error codes; every error path this driver has exercised came from `usb-host`
  passthrough of real hardware.
