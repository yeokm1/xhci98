# Issues - the problems that shaped this driver

Long-form write-ups of the most instructive problems met during development:
what the symptom was, how it was found, how it was chased (including the wrong
turns), how it was fixed, and what rule the project kept from it. Each one is
a narrative distilled from the evidence documents. Where a narrative and the
evidence disagree, the evidence wins; the sources are listed at the foot of
each page.

Dates are 2026 unless stated. Task ids are the roadmap's.

**Issues 7 and 8 were found and fixed before `1.1.0.0` was cut (2026-09-18),
and neither is a limitation of a published release.** Issue 7 was seen only on
Windows Vista and Windows 7, which no earlier release supported, and issue 8's
fix reaches the 32-bit tier for the first time in the `1.1.0.0` binary. Read any "at the
cut" wording on those two pages as "before the cut".

Issue 8 is a bugcheck on the Windows XP x64 guest at four vCPUs, found on
2026-09-13 while taking issue 7's fix across the NT 5.x legs. Its cause was
read and a fix run on 2026-09-14: this driver delivered completions without the
transfer's own endpoint lock, which usbport's completion service leaves to its
caller. The fix was amd64-only for one day. A static read on 2026-09-15 of
every 32-bit `usbport.sys` this project targets (XP SP3, NUSB 3.3 = 3.6,
SweetLow, Windows 2000 SP4) found the same unlocked mover and locked reader, so
the guard was lifted onto the whole 32-bit tier that night, and the 32-bit legs
have since passed: XP SP3 x86 at four vCPUs under WHPX on 2026-09-15, then
Windows 98, ME and Windows 2000 single-core under TCG.

Issue 7 was fixed in source on 2026-09-13 and the legs re-run: all four NT 6.x
guests the same day, then the NT 5.x tier as issue 8's fix reached each
architecture. Two things the page records are worth carrying here. The reading
made a prediction - that a single-processor guest would not show the arrest -
and **that run was taken and the prediction held, five consecutive cycles**, so
the mechanism is no longer unconfirmed by experiment. And the arrest **also
reproduced on Windows 7 x64**, so the issue is not x86-only and not a property
of the 32-bit binary, though the file keeps its `x86` name for link stability.

What stays open on issue 7 is not the defect but two things around it: what
`usbport.sys` is holding when the enumeration thread parks - unread, so the
page continues to refuse the claim that the defect is not this driver's - and
how much of that to say in the release notes, which is roadmap task 22.6 and
the owner's call.

Issues 1 to 3, 5 and 6 were fixed before `1.0.0.0`, so none of them is a
limitation of the release; those pages are here for the mechanism and for
how it was found (issue 5's fix was a machine-wide registry value until
`1.0.2.0`, which the release notes of those versions list as a known
limitation; from `1.1.0.0` it is the miniport flag, read on all ten targets
on 2026-09-17, section 5.5). Issue 4 was observed on the Windows XP guest on 2026-09-03 and
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
| 5 | [A device plugged into an idle Windows 98 controller is seen by nothing, and how the package stops the idle](05-idle-suspend-and-disableselectivesuspend.md) - usbport idle-suspends the controller half a second after the bus goes quiet, a halted xHC cannot raise a port event, EHCI's re-armed interrupt has no xHCI equivalent, and the fix is to stop usbport asking. It was usbport's own machine-wide registry switch until `1.0.2.0`, measured on both Windows 98 stacks; since `1.1.0.0` it is the miniport flag `USB_MINIPORT_FLAGS_DISABLE_SS` (0x20), which reaches the same state inside usbport without touching the registry and which no OS path can rewrite - the per-controller value can be, by Vista's power plan | Fixed (task 11-V.6 on the Windows 98 path; `1.0.1.0` on the NT path; mechanism replaced in `1.1.0.0` and read at run time on all ten targets on 2026-09-17, section 5.5) |
| 6 | [A Full-Speed device on a root port bugchecks both targets, and why every root port is reported as High Speed](06-full-speed-root-port-bugcheck.md) - usbport applies the EHCI model and looks up a transaction translator for any non-High-Speed root-port device; `USBPORT_GetTt` turns the root hub's empty TT list into a garbage pointer and the kernel faults on the first insertion, on both shipping builds; the one lever is the USB2 flag, so the driver reports every root port as High Speed and keeps the true speed for its own contexts, at the cost of 1/2/4 ms interrupt bands for Full and Low Speed devices on a root port. Measured 2026-09-19 (section 7), the same report costs more on the newer stacks: a USB 1.1 hub on a root port bugchecks every Vista and Windows 7 build once a slower device behind it is configured (a NULL transaction translator in usbport's USB 2.0 budgeter), and a Full-Speed USB audio device on a root port plays nothing from Windows XP on (usbport schedules it as High Speed; behind a hub it plays) | Fixed (Phase 5 task 7) on Windows 98 and 2000; the two costs from XP on are **known limitations of `1.1.0.0`** (section 7) |
| 7 | [An enable on Windows 7 x86 intermittently loses one device, and the completion it is waiting for is dropped inside usbport's own DPC state machine](07-win7-x86-enable-arrest-usbport-done-dpc.md) - a disable/enable cycle intermittently comes back without the audio device; the driver completes its configuration descriptor with 252 bytes and usbport accepts the completion, then never drains it, because `USBPORT_Xdpc_iSignal` calls `KeInsertQueueDpc` before storing the queued state and the DPC that won that race left the done list marked queued for ever. The enumeration thread is parked in a NULL-timeout, non-alertable wait, which is why a restart recovers and a rescan does not | **Fixed 2026-09-13 and re-run the same day on all four NT 6.x guests (Vista and Windows 7, x86 and x64), every clause passing on each through five disable/enable cycles, with one Windows 7 x86 disable refused by Windows itself for a reason not recorded (issue section 7.5), then on the first NT 5.x leg, Windows XP x64, which is
the `200` arm on amd64 and read the 0 that tier must read - though it ran on
one vCPU, which cannot discriminate a fixed binary from an unfixed one for
this race (issue section 7.6); raised to four vCPUs the same evening it
**BUGCHECKED** on the second live cycle - `D1` at `USBPORT+0x1d1a7`, reading a
pointer with its low 32 bits zeroed, which did NOT reproduce over six further
cycles and was traced to this driver's delivery on that tier (issue 8); **the
four-vCPU XP x64 leg then PASSED on 2026-09-14 on the build carrying issue 8's
fix** - five live cycles, remove and rescan, no bugcheck (issue section 7.8);
**XP 32-bit at four vCPUs, 2026-09-14/15, bugchecked `FC` on the first device
twice - not SMP but a `CloseEndpoint` parameter-count regression from
2026-09-12, fixed with one callee per tier - and then on the fixed build
livelocked before its first disable on issue 8's mechanism, which x86 did not
then carry the fix for; on the build taking issue 8's fix on the 32-bit tier
the leg PASSED on 2026-09-15 under WHPX - five live cycles, remove and rescan,
one run - after two TCG runs stalled in PnP and were set aside by the owner as
not representative (issue section 7.9); Windows 98, ME and 2000 then passed
single-core under TCG, Windows 98 without controller stops (NUSB cannot take
one) and Windows 2000 with USB audio unplugged before each disable, since
Windows refused a live disable with it attached (issue section 7.10)** - the lost wakeup is real and it is reachable only because this driver delivered completions from contexts usbport never expected (the r5 one from `RH_GetPortStatus`, which NT 6.x usbport calls at PASSIVE with no lock); `USBPORTSVC_CompleteTransfer` assumes its caller holds usbport's EpList lock, and Microsoft's usbehci completes from `PollEndpoint` alone. Fixed by delivering at DISPATCH always and, on the Version 300 tier, only from `PollEndpoint`, `AbortTransfer` and `SetEndpointState`, with a forced path for the lifecycle drains and a 1 s poll fallback (issue section 7). The arrest **also reproduced on Windows 7 x64**, so the title is narrower than the issue |
| 8 | [Windows XP x64 at four vCPUs rarely bugchecks in usbport, on a list head whose low 32 bits were overwritten](08-xp64-smp-usbport-list-head-low-dword.md) - four `D1` bugchecks inside `usbport.sys` on one guest, on enables, on an idle machine and on a first enumeration; three dumps show a `LIST_ENTRY` head in usbport's device extension with exactly the low half of its `Flink` zeroed. A kernel debugger's data breakpoint missed the write under TCG; a QEMU gdbstub watchpoint caught it: usbport's own `mov dword ptr [rcx+28h],edx` through a transfer walk that had followed a transfer moved, on another CPU, onto the done list, and took that list's head for a transfer. The move was this driver's `UsbPortCompleteTransfer` from a context without the transfer's endpoint lock, which XP x64's completion service leaves to its caller | **Fixed 2026-09-14 on the amd64 Version 200 tier only** (completions delivered only from a usbport callback for their own endpoint); one run, 10 live four-vCPU cycles, 0 bugchecks, 0 fallbacks (issue section 4c), then issue 7's full XP x64 four-vCPU leg on the committed build, 0 bugchecks (issue 7 section 7.8). **On 2026-09-15 XP SP3 x86 at four vCPUs showed the same mechanism as a livelock** - usbport's active-list walker holding a transfer moved to the done list under it, spinning with the endpoint lock held (issue 7 section 7.9) - **and a static read of every 32-bit usbport (XP SP3, NUSB 3.3 = 3.6, SweetLow, Windows 2000 SP4) found the same unlocked mover and locked reader (issue section 4d), so the fix was taken on the whole 32-bit tier the same night; its first 32-bit run, XP SP3 x86 at four vCPUs, passed issue 7's leg under WHPX on 2026-09-15 (one run); Windows 98, ME and 2000 then passed single-core under TCG (issue 7 section 7.10)** |

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
  decodes `0xE9`; the P6 binaries of `../contributing/runs/run-13e.md` were built to separate
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
