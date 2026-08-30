# Issues - the problems that shaped this driver

Long-form write-ups of the most instructive problems met during development:
what the symptom was, how it was found, how it was chased (including the wrong
turns), how it was fixed, and what rule the project kept from it. Each one is
a narrative distilled from the evidence documents. Where a narrative and the
evidence disagree, the evidence wins; the sources are listed at the foot of
each page.

Dates are 2026 unless stated. Task ids are the roadmap's.

Each of these was fixed before `1.0.0.0`, so none of them is a limitation of
the release; the pages are here for the mechanism and for how it was found.

| # | Issue | Status |
|---|---|---|
| 1 | [Getting a kernel log off a Windows 98 machine](01-windows-98-log-capture.md) - DebugView bugchecks real hardware, the driver owns no door of its own, and the way out was reading the driver's ring through usbport's own vendor IOCTL (`XHCISNAP`) | Fixed |
| 2 | [The bare-metal wedge, the PORTSC watchdog that "fixed" it, and what was actually wrong](02-bare-metal-wedge-and-portsc-watchdog.md) - five hot-plugs kill the controller on two Intel generations and never in QEMU; a polled sweep recovers it for the wrong reason; the cause is a recovery step nobody ever sends | Fixed |
| 3 | [Composite devices need `usbhub.sys`, and an xHCI-only machine never has it](03-usbhub-sys-composite-devices.md) - Code 2 on every multi-function device, blamed on NUSB for two weeks, settled by one file and a laptop that was not in the plan | Fixed |

## Other issues worth a page

These are recorded in [lessons.md](../contributing/lessons.md) and
[run-13e.md](../contributing/runs/run-13e.md) and would each carry a write-up
of the same shape. Listed roughly in order of how much they would teach a
reader.

- The Windows 98 idle hot-plug defect. A device plugged after the
  controller idle-suspends is seen by nothing until a Device Manager Refresh.
  Microsoft's own `usbehci.sys` was disassembled to learn that it re-arms Port
  Change Detect after halting the controller, a trick that is a category error
  on xHCI, where an interrupt exists only as an Event TRB and a halted
  controller may not generate port events (spec Fig. 4-34 note). Timer polls
  and PME# were then eliminated by measurement. The whole investigation had
  answered "how does a driver wake a sleeping controller"; the owner asked
  "can the sleep be prevented?", and `strings` on the same binary found two
  registry values usbport reads (`HcDisableSelectiveSuspend` and the global
  `DisableSelectiveSuspend`, which must both be set). Fixed by one `AddReg`
  line, no driver code.
- EP0's initial max packet size of 8 is babble on usbport. Two
  Sound Blasters read nothing (Code 22, no wizard). A field census of
  `bMaxPacketSize0` across the equipment showed the failing units shared only
  "not 8". usbport issues its first `GET_DESCRIPTOR` with `wLength=64` and the
  driver cannot change that, so a device answering in 16- or 64-byte packets
  on an endpoint declared as 8 dies on the first read. That is why Linux
  starts Full-Speed EP0 at 64. One constant changed.
- A Full-Speed device on a root port bugchecks both targets. The
  root hub was reporting correctly; the fault was in usbport's own handling of
  a Full-Speed device that is a direct child of a 2.0 root hub, a situation no
  EHCI miniport can produce, so Microsoft's binary had never been exercised on
  it. Bugcheck forensics from raw parameters, a refuted hypothesis, and a fix
  with a documented blast radius.
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
- The published debug flavour does not load on real silicon (still open). One
  import differs from the release build, `HAL.dll!WRITE_PORT_UCHAR`, the
  port-`0xE9` writer, and the E460 gives it Code 2. Either the import does not
  resolve or something on that chipset decodes `0xE9`. The three-flavour split
  exists so the question can stay open.
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
