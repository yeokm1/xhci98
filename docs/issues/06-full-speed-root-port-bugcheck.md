# Issue 6 - A Full-Speed device on a root port bugchecks both targets, and why every root port is reported as High Speed

Status: fixed in roadmap Phase 5 task 7, before the first release. The
driver reports every connected root port to usbport as High Speed and keeps
the true speed for its own contexts. The cost, an interrupt interval usbport
buckets on the wrong speed, is a documented limitation, not an open defect.
**Two further costs were measured on 2026-09-19 and are known limitations of
`1.1.0.0` (section 7):** a USB 1.1 hub on a root port bugchecks every Vista
and Windows 7 build once a slower device behind it is configured, and a
Full-Speed USB audio device on a root port plays nothing from Windows XP on.

Targets affected: Windows 98 SE under the NUSB stack and Windows 2000 SP4,
both bugchecked in the virtual machines; the same usbport logic is in both
binaries. Real hardware never ran the truthful-speed build: the fix predates
every bare-metal batch, and Low-Speed and Full-Speed devices have since run
on the E460 under the override without incident. SweetLow's XP-lineage
rebuild of usbport was not read for this path.

The short version: usbport applies the EHCI model to a USB 2.0 miniport. On
EHCI a root port can only ever hold a High-Speed device, because Full and
Low Speed devices are handed to a companion controller, so when usbport is
told a root port holds a Full-Speed device it goes looking for the
transaction translator that model guarantees. There is none, the lookup
returns a garbage pointer instead of NULL, and the kernel faults on the
first list insertion. Reporting the true speed is therefore fatal on both
shipping usbport builds, and the driver lies at exactly one layer to avoid
it. This page is the story; the instruction-level chain is in
[usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md) section
8, "The transaction-translator lookup", and the reporting rule in
[implementation-invariants.md](../contributing/implementation-invariants.md),
"Root Hub Reporting". Where this page and those disagree, they win.

---

## 1. The problem

The Phase 5 root-hub checkpoint plugged Low, Full and High Speed devices
into the QEMU guests and watched port status, the asynchronous reset, the
decoded speed and the change bits. Everything the driver was responsible for
was right: connect, `C_PORT_CONNECTION` latched and cleared, reset completed
by PRC, `C_PORT_RESET` latched and cleared, a steady `0x0103` (connected,
enabled, powered, Full Speed) at the moment usbport took the device. Then
the machine died.

| Target | Device | Speed | Result |
|---|---|---|---|
| Windows 98 | `usb-audio` | Full | `Windows protection error`, or `0028:C002F70E` in `NTKERN` |
| Windows 98 | `usb-kbd` | High | healthy |
| Windows 2000 | `usb-audio` | Full | `STOP 0x0000000A (0xFFFFFFFC, 0xFF, 0x00000000, 0x804006B2)` |
| Windows 2000 | `usb-kbd,usb_version=1` | Full | the same STOP, the same four parameters |
| Windows 2000 | `usb-kbd` | High | healthy |

The first four runs confounded speed with class: every Full-Speed test was
an audio device and every High-Speed control a keyboard. One run with a
Full-Speed keyboard reproduced the Windows 2000 bugcheck with byte-identical
parameters and separated them. Speed was the variable, on both targets.

Twice in that session a complete, correct driver trace was read as a pass
and a screenshot then showed the bugcheck. The trace ends where the driver's
involvement ends, not where the system dies.

## 2. What the four parameters said (static, Windows 2000)

`0x804006B2` is `ntoskrnl.exe` RVA `0x6B2`: the third instruction of
`ExfInterlockedInsertTailList`, `mov eax,[ecx+4]`, reading `ListHead->Blink`.
`0xFFFFFFFC` is that read with `ecx = 0xFFFFFFF8`. The IRQL `0xFF` is not a
real IRQL; the routine's own `pushfd; cli` is why. Among the USB stack only
`usbport.sys` imports that routine, which pins the caller.

The producer is `USBPORT_GetTt`. `USBPORT_CreateDevice` calls it for any
device that is not High Speed when the miniport declared
`USB_MINIPORT_FLAGS_USB2`. It walks up the hub handles to the first
High-Speed one, and on its `TtCount <= 1` branch takes `CONTAINING_RECORD`
of the TT list head unconditionally. An empty list gives `0xFFFFFFEC`, not
NULL. `USBPORT_OpenPipe`'s null check passes it, adds `0xC`, and hands
`0xFFFFFFF8` to the kernel's list insertion. Two facts make it deterministic
rather than unlucky:

- `USBPORT_RootHubCreateDevice` marks the root hub's own handle High Speed
  exactly when the miniport declares the USB2 flag, so the upward walk stops
  at the root hub instead of running off the top to the safe NULL exit.
- usbport's root-hub device descriptor template hardcodes
  `bDeviceProtocol = 0`, a hub with no transaction translator, and only
  `bcdUSB` is patched. No hub driver ever initialises a TT for the root hub,
  so its list is empty for the life of the controller.

The multi-TT branch twenty instructions away does test for an empty list.
ReactOS's `USBPORT_GetTt` has early returns for both cases; neither shipping
binary has them. A ReactOS read is interface documentation, not evidence of
safety, and this is the second place in this project where it added a guard
the shipping code lacks.

On genuine EHCI the combination is unreachable: a Full or Low Speed device
on an EHCI root port is released to a companion controller, and usbport is
never asked to create a non-High-Speed child of the EHCI root hub. The
companion is UHCI or OHCI at the chipset vendor's choice (Intel and VIA
paired EHCI with UHCI, most others with OHCI), and usbport does not care
which: the port's ownership bit flips to the companion, whose own miniport
(`usbuhci.sys` or `usbohci.sys`) enumerates the device under its own root
hub. Only an xHCI miniport, which has no companion and owns every speed on
every port, can reach this code.

## 3. The remedy that was refuted before it was tried

The leading hypothesis named the right mechanism, the EHCI companion model,
and the wrong lever: declare `MiniPortVersion` as xHCI rather than EHCI.
ReactOS switches the root-hub descriptor type on that version. Both shipping
builds copy a fixed seven-byte hub descriptor template and contain no
version branch at all, so declaring the xHCI version changes nothing here.
`USB_MINIPORT_FLAGS_USB2` is the only lever, and it has two positions:

- **Drop the flag.** Structurally immune: without it `CreateDevice` never
  calls `GetTt` in any topology. But the root hub then enumerates as
  `USB\ROOT_HUB`, NUSB's `USB2.INF` claims only `USB\ROOT_HUB20`, and Windows
  98 binds its own USB 1.1 `usbhub.sys`, which extracts one speed bit from
  the port status and has no notion of High Speed. High Speed is lost on the
  primary target.
- **Keep the flag and never report a root port as anything but High Speed.**
  `CreateDevice` then skips the lookup for every root-port device, because
  the lookup is gated on the reported speed.

The second was chosen, and ran the same day on both targets: Full-Speed HID
and Full-Speed audio plugged, reset, decoded Full Speed, reached
`QueryEndpointRequirements` and `OpenEndpoint` (the very path that had run
off the null base), and unplugged, with no bugcheck and every failure
counter at zero. High Speed unchanged. The pre-fix binary was archived so
the comparison is reproducible rather than remembered.

## 4. How the fix is confined, and what it costs

The lie lives in one place, `XhciPortShadowReport` in `src/xhci_port.c`: a
connected managed root port sets the High-Speed status bit whatever the
decoded speed, and the Low-Speed bit is never set at all. Devices behind
an external High-Speed hub report their true speed; a genuine 2.0 hub
really has a TT.

usbport derives four things from the speed it believes, and the trade-off
is what happens to each:

- **The device's own programming: corrected.** The Slot Context speed,
  EP0's max packet size and every Endpoint Context are programmed from
  the decoded speed the driver keeps in the port shadow; usbport's
  `DeviceSpeed` is never used for them. This is what makes the devices
  work at all, and Low-Speed HID and Full-Speed audio have both run on
  real silicon under it.
- **The TT fields for devices behind a hub on a root port: corrected**, in
  Phase 7b, by deriving them from the driver's own topology graph (section
  5).
- **The interrupt interval: lost.** Below.
- **Periodic bandwidth accounting: unmeasured.** usbport budgets a
  believed-High-Speed device against the High-Speed bus budget rather
  than a frame budget. The xHC does its own admission check when an
  endpoint is configured, and no symptom has been seen, but no run has
  loaded a root port with enough Full-Speed periodic traffic to test it.

And one cosmetic effect: Device Manager and any tool that asks usbport
report every root-port device as High Speed, whatever it is. The true
speed is visible only in this driver's own trace and counters.

A stop-time review found that the first draft of the override destroyed the
checkpoint's own evidence: the decoded speed had only ever been visible in
the port-status trace, which now read High Speed for everything. The
replacement, a first-decode trace line and a monotone set of speed classes
seen since the last start, took four rounds to make quiet, and left a rule
about trace budgets that later pages lean on.

The cost is the interrupt interval, and it is irrecoverable through this
usbport. usbport turns a device's `bInterval` into the `Period` it hands the
miniport using the speed it believes: for a High-Speed device that is
`2^(bInterval-1)` microframes capped at 32, for a Full or Low Speed one the
frame count rounded down to a power of two. No raw `bInterval` reaches the
miniport. A Full or
Low Speed device on a root port is therefore bucketed on High-Speed rules,
and the driver then floors its interrupt endpoints at 1 ms because the xHCI
specification allows no less at those speeds (Table 6-12). The result is
three bands: `bInterval` 1 to 4 gives 1 ms, 5 gives 2 ms, 6 and above gives
4 ms. A stock mouse at `bInterval` 10 runs at 4 ms; nothing slower is
reachable, nothing faster than 1 ms either, and two values inside one band
are indistinguishable. Always the faster direction: latency only, never a
missed poll. An interval override tool that changes `bInterval` within a
band shows no effect for this reason, and one that crosses a band does.
Measured with such a tool, SweetLow's hidusbf and its Windows 9x lower
filter, on a Full-Speed mouse in a virtual machine: `bInterval` 1, 2, 5 and
8 on a root port arrived as `Period` 1, 2, 16 and 32 and were programmed as
Interval 3, 3, 4 and 5 (1, 1, 2 and 4 ms), the first two through the floor;
the same mouse behind a Full-Speed hub arrived at its true speed, with
`bInterval` 10, 1 and 4 bucketed in frames as `Period` 8, 1 and 4 and
programmed as Interval 6, 3 and 5 (8, 1 and 4 ms), nothing floored. The
prohibition on "reconstructing" `bInterval` from `Period` is in the
invariants: the information is gone before the miniport sees it.

## 5. The residual that was inferred, asserted, and measured false

The chosen fix left one topology apparently exposed: a hub with no TT (any
USB 1.1 hub, or a 2.0 hub with `bDeviceProtocol = 0`) on a root port, with
a Full or Low Speed device behind it. By the transcribed `GetTt` logic the
lookup would run one level down and fault the same way. One document filed
this as "inferred, nothing has run"; the invariants and a memory note said
"verified".

Batch 7b-V0 attached exactly that topology on both targets, a QEMU
`usb-hub` with a Full-Speed mouse behind it, and neither faulted. The TT
lookup returned the hub's own address, the documented "lookup succeeded"
reading, so a TT record existed for a hub that physically has none: the
override marks the hub High Speed, and a believed-High-Speed hub gets a TT
initialised for it. Why that holds is not confirmed against the binaries
(design record 02, open question 6). What the topology cost at the time
was that the device behind such a hub never enumerated, because usbport
handed the driver TT fields naming a translator that does not exist, and
the driver's refusal of those transfers was unbounded: a dead Windows 98
guest. Both halves were closed in Phase 7b: the refusal got a bound (task
7b-A.0), and the driver now derives the TT fields from its own topology
graph, which carries each hub's speed as the driver decoded it, so a
Full-Speed hub's children get no TT whatever usbport says (task 7b-A.3).
The disagreement is counted (`TtPairsDisagreed`) rather than resolved.

The lesson kept its own section in the lessons file: a disassembly says
what a function does with an empty list, not whether the list is empty.

## 6. What is still open

- **A true-speed report as an opt-in.** Under a usbport that guards the
  empty TT list, reporting the real speed would remove the interval bands
  as well, because usbport would then bucket `Period` in frames, which
  `XhciIntervalFromPeriod` already handles. It must never be enabled by
  build detection: under NUSB's and SP4's usbport it bugchecks the machine.
  SweetLow's XP-lineage rebuild does not guard it either: its single-TT
  branch at `0x2667A`-`0x26686` returns the same `0xFFFFFFEC` for an empty
  list (static, ABI document section 8). No truthful-speed run has been
  made on that rebuild; its lineage is not a basis for enabling an option.
- **The bandwidth accounting above** has no measurement either way.
- **Metal never ran the truthful build**, so the bugcheck itself is a VM
  observation. Nothing suggests real hardware differs: the fault is in
  usbport's own list handling, not in anything the controller does.
- **Why a believed-High-Speed 1.1 hub gets a TT record** is unconfirmed
  against the binaries. The driver no longer depends on the answer, and
  measures the disagreement instead.
- **Section 5's residual bugchecks every Vista and Windows 7 build**
  (2026-09-19): section 7.1.
- **The High-Speed report silences Full-Speed audio on a root port from XP
  on** (2026-09-19): section 7.2. Both are known limitations of `1.1.0.0`;
  section 7.3 has what would fix them and what is still unmeasured.

## 7. What the High-Speed report costs from Windows XP on (2026-09-19)

Roadmap task 22.9, the post-release run of `1.1.0.0`, was the first time a
hub was put on a Windows Vista or Windows 7 guest and the first time audio
was played, rather than only bound, on any target after Windows 2000. Both
met the fix this page describes. Neither is a defect in that fix on the
systems it was made for: Windows 98 and 2000 behave as before. They are what
the same report costs on the newer USB stacks, and both are known
limitations of `1.1.0.0` by the owner's decision of 2026-09-19
(`docs/using/release-notes.md`, "Known limitations"; the download readme,
section 7; `README.md`, "Known limitations"). The run record is
`docs/contributing/runs/run-22.md`, 22.9; every binary-derived fact below
has its row in `legal-provenance.md` section 4.

### 7.1 A USB 1.1 hub on a root port bugchecks Windows Vista and Windows 7

**What happens.** A Full-Speed hub on a root port, and a Full or Low Speed
device with a periodic endpoint behind it - a mouse was enough - stops the
machine with `STOP 0x0000007E` (an access violation) in `USBPORT.SYS` the
moment that device is configured. The hub alone enumerates and works. It is
section 5's residual topology, which batch 7b-V0 measured harmless on
Windows 98 and 2000; in the same 22.9 run the same steps passed on Windows
2000 and XP x64.

| Target | Stop parameters | Faulting instruction (public PDB) | Read of |
|---|---|---|---|
| Windows 7 SP1 x86, DateStamp `4CE79C15` | `0xC0000005, 0x8EAEED30, 0x8A6D749C, 0x8A6D7080` | `usbport!Allocate_time_for_endpoint+0x15d` | `0x00000A04` |
| Vista SP2 x86, `49E01FCF` | `0xC0000005, 0x8E1A9E9C, 0x881C750C, 0x881C7208` | `Allocate_time_for_endpoint+0x162` | `0x00000A04` |
| Windows 7 SP1 x64, `4CE7A670` | `0xFFFFFFFFC0000005, 0xFFFFF88002D96B4C, ...` | `Allocate_time_for_endpoint+0x19c` | `0x0000000000000C08` |
| Vista SP2 x64, `49E02D1B` | `0xFFFFFFFFC0000005, 0xFFFFFA6002A72398, ...` | `Allocate_time_for_endpoint+0x194` | `0x0000000000000C08` |

The stack walked from the Windows 7 x86 context record is usbport's alone,
from `USBPORT_Dispatch` through `USBPORT_ProcessURB`,
`USBPORT_SelectConfiguration`, `USBPORT_InternalOpenInterface`,
`USBPORT_OpenEndpoint`, `MPx_AllocateBandwidth`,
`USBPORT_AllocateBandwidthUSB20` and `USB2LIB_AllocUsb2BusTime` to the fault;
no frame is in `xhci98.sys`.

**Why (static, confirmed by the runtime values).** Opening the mouse's
interrupt endpoint runs usbport's USB 2.0 bus-time budgeter.
`USBPORT_AllocateBandwidthUSB20` passes it the endpoint's transaction
translator as `[ep+1Ch] ? [[ep+1Ch]+38h] : 0` - it sees the missing TT and
passes NULL on. `USB2LIB_AllocUsb2BusTime` takes the schedule to charge as
`TT + 4` for a Full or Low Speed endpoint (`bus + 414h` for High Speed), and
`Set_endpoint` stores it at `+0Ch` of the budget object; on x64 the fields are
pointer-sized, so it is `TT + 8`. `Allocate_time_for_endpoint` then reads
`[schedule + 0A00h]` (`0C00h` on x64): `0 + 4 + 0xA00 = 0xA04`,
`0 + 8 + 0xC00 = 0xC08`, the addresses in the table. The budget object
itself held the mouse's endpoint - maximum packet 4, period 8. The mouse has
no TT because the driver reports the hub, as a root-port device, as High
Speed; the hub's own descriptor offers no TT; and the mouse's speed comes
from the real hub, Full. So the stack looks for a translator on a hub it
believes is High Speed, finds none, and budgets against address 4.

**What it predicts, not measured.** Any Full or Low Speed device with an
interrupt or isochronous endpoint behind a USB 1.1 hub on a root port does
the same - a keyboard, a mouse, an audio device, a keyboard with a built-in
hub. A bulk-only device there may not reach the budgeter. A USB 2.0 hub has a
real TT, so devices behind it should budget correctly; QEMU cannot show it,
because its only hub, `usb-hub`, is a Full-Speed hub with no speed option (it
enumerates at 12 Mb/s).

**Workarounds** (in the release notes): plug Full and Low Speed devices into
a root port directly, where the driver's report covers them, or behind a USB
2.0 hub (unmeasured).

**How it was found and read.** The harness reported the churn row's guest as
"not executing": its debug console showed four `DriverEntry` lines in one
group, three with no teardown before them - Windows 7 restarting after each
bugcheck - and the harness went on reading the first boot's extension
address. A run through an untracked copy of the runner with
`-action reboot=shutdown` held the guest on the stop screen; the exception
and context records and the stack were read from the halted guest's memory
over QEMU's monitor, and the whole guest memory was saved
(`out\post-release\1.1.0.0-win7-hub-diag\`). The other three builds were
installed on fresh overlays and given only the hub and the mouse
(`out\post-release\1.1.0.0-t229-{vista,vista64,win764}\`).

### 7.2 A Full-Speed audio device on a root port plays nothing from Windows XP on

**What happens.** The device installs, is the default playback device, and
Windows shows it playing, but no sound reaches it: no isochronous transfer
reaches the driver at all. Measured with QEMU's `usb-audio` and its `wav`
backend as the oracle, the driver's counters read over the monitor while
the guest played:

| Target | Played with | Isochronous submits | Audio reaching the device |
|---|---|---|---|
| Windows 98 SE | the startup sound | 1 (10 packets, all answered), then the OS's own `USBAUDIO.VXD` faults | - (batch 9-V's known OS fault) |
| Windows 2000 SP4 | Sounds and Multimedia | 376 (3,760 packets, all answered) | 659,456 B |
| XP SP3 x86, on a root port | Sound Recorder, 1.93 s of 1.93 s | 0 | 0 B |
| XP SP3 x86, **behind a Full-Speed hub** | Sound Recorder, the same file | **196 (1,960 packets, all answered)** | **344,064 B** |
| XP x64 SP2 | Sounds tab | 0 | 0 B |
| Vista x86 and x64 | Speakers Properties, Test showing Stop | 0 | 0 B |
| Windows 7 x86 and x64 | Speakers Properties, Test showing Stop | 0 | 0 B |

Nothing is refused on the driver's side: where every counter was dumped
(Windows 7 x86, XP x64, Vista x86) every open usbport asked for was accepted
and no refusal counter moved. On Vista x86 and Windows 7 x64 an endpoint was
opened as playback began and nothing was ever submitted down it.

**Why.** 32-bit XP runs the same binary through the same NT 5.x registration
as Windows 2000, so the difference is the OS's own stack, and the hub row
isolates it: behind a hub the device is reported at its true Full speed and
plays; on a root port it is reported High Speed and does not. XP's
`USBPORT_IsochTransfer` takes a separate branch for a High-Speed endpoint -
`cmp dword ptr [eax+110h],2` - which allows up to 0x400 packets and counts
them as microframes (`packets x period >> 3`), so a Full-Speed stream of one
packet per frame is scheduled as a High-Speed one (static, the public PDB).
How that branch then loses the stream is not read, and neither is why
Windows 2000's usbport plays the same stream on a root port.

**Workarounds** (in the release notes): put the audio device behind a hub.
On XP that can be a USB 1.1 hub (measured on 32-bit XP only); on Vista and 7
it must be a USB 2.0 hub, because of 7.1 (unmeasured).

**Consequences for the record.** Every device-matrix audio row judged
arrival, not playback: XP x64's row passed because its audio stack opens an
endpoint on arrival, and Windows 7's read NODRIVER because it does not.
Before 2026-09-19 the NT 5.2 and NT 6.x tiers were accepted on "the audio
device bound"; this is a first measurement, not a regression. Roadmap task
22.12 (d), which wants isochronous counters moving on a QEMU audio row, can
only be read on Windows 2000 as things stand.

### 7.3 What would fix both, and why neither was attempted for `1.1.0.0`

Both follow from the one lever section 4 describes, and changing what the
driver reports on a root port is exactly what bugchecks Windows 98 and 2000
(sections 1 and 2) - the primary targets. The candidate that would remove
both without reopening that is the idea in
[`docs/future-plans/virtual-hub-per-root-port.md`](../future-plans/virtual-hub-per-root-port.md):
a virtual USB 2.0 hub behind every root port, so that a slower device is
reported at its true speed behind a hub that has a transaction translator.
It is an idea only - nothing of it is built, and it is off by default by its
own rules - and neither consequence above has been measured under it.

**Open:** a USB 2.0 hub on Vista or Windows 7 with a Full-Speed mouse and a
Full-Speed audio device behind it, on real hardware, which is what the
workarounds rest on; why XP's High-Speed isochronous branch drops the
stream; and whether a bulk-only device behind a USB 1.1 hub is safe on
Vista and 7.

## 8. Lessons the record kept

- A healthy trace is not a living guest. Screenshot before calling a VM run
  a pass; the driver can be entirely right and the machine still dead.
- Control for device class as well as the variable under test. Two devices
  that differ in speed usually differ in class too.
- A guard present on one branch is not a guard on the function. Grepping
  `GetTt` for "is there a null check" answers yes, twenty instructions from
  the branch that runs.
- Map an import slot by how the code uses it, not by counting the import
  listing. An off-by-one produced six plausible call sites of the wrong
  function; the fastcall argument shape identified the right one at once.
- ReactOS adds guards the shipping binaries lack. Twice now.
- When a change makes the driver stop telling someone the truth, ask what
  was reading that truth. The override silently deleted the checkpoint's
  evidence and silently broke the interval, and both were found by review
  rather than by a run.
- When one document hedges a claim and another asserts it, the hedge was
  the one thinking. Propagate the hedge, or measure it.
- A fix that holds on the stacks it was made for is a new experiment on
  every stack added after it. This one was made on Windows 98 and 2000 and
  carried to XP, Vista and 7 unchanged; the hub and the audio playback that
  found its cost there were simply never tried on them until section 7.
- "Bound" is not "working". An audio device that installs, is the default
  and shows as playing can move no data at all; the oracle is the stream
  itself (QEMU's `wav` backend), not the device list.

## Sources

- [lessons.md](../contributing/lessons.md), "A Full-Speed device on a root
  port bugchecks both targets, and a healthy trace is not a living guest"
  (the matrix, the proven-versus-inferred split, the task 7 closing run) and
  the static-pass entry before it (the four parameters resolved, the refuted
  `MiniPortVersion` hypothesis, the refuted 1.1-hub residual).
- [usbport-miniport-abi.md](../usb-xhci-info/usbport-miniport-abi.md)
  section 8, "The transaction-translator lookup, and why
  `USB_MINIPORT_FLAGS_USB2` must be set": the instruction-level chain with
  addresses in both builds (static).
- [implementation-invariants.md](../contributing/implementation-invariants.md),
  "Root Hub Reporting": what is programmed from the decoded speed, and the
  prohibition on reconstructing `bInterval`.
- `src/xhci_port.c`, `XhciPortShadowReport` (the override and its comment);
  `src/xhci_ctx.c`, `XhciIntervalFromPeriod` and `XhciIntervalForSpeed`
  (the bucketing contract and the floor).
- [roadmap.md](../contributing/roadmap.md), Phase 5 status and task 7.
- [design record 02](../contributing/design/02-hub-topology-route-string.md),
  open question 6 (why a believed-High-Speed 1.1 hub gets a TT).
- [legal-provenance.md](../contributing/legal-provenance.md) section 4: the
  static rows for `USBPORT_GetTt`, `USBPORT_CreateDevice`,
  `USBPORT_RootHubCreateDevice` and the descriptor templates.
