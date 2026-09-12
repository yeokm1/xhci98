# Issue 7 - An enable on Windows 7 x86 intermittently loses one device, and the completion it is waiting for is dropped inside usbport's own DPC state machine

Status: **open**, and deliberately left open at the `1.1.0.0` cut on the
owner's ruling of 2026-09-12. Observed on the Windows 7 x86 guest across
four runs on 2026-09-12 (`p225win7x86r2` through `r5`, roadmap task 22.5's
guest leg). The mechanism below is read from usbport's own log ring,
captured live against a signature-checked miniport extension, and from a
static reading of `usbport.sys` 6.1.7601.17514 x86 against Microsoft's
public symbols. **The one experiment that would confirm or refute it - the
same guest with a single vCPU - HAS NOW BEEN RUN, on 2026-09-12, and it
agrees** (section 6 item 1). It was run on Windows 7 **x64** rather than
x86, and on that target the arrest vanished across five consecutive
disable/enable cycles. That lifts the mechanism below out of "a reading of
a binary" and into a prediction that held once, on one target. It does not
settle the rest: nothing in this driver is known to be wrong, and nothing
in this driver is known to be right either; section 5 says exactly what
the evidence licenses, and it is unchanged by the single-vCPU run.

Targets affected: Windows 7 x86 **and Windows 7 x64**, both in virtual
machines. Not seen on Windows 98 SE, Windows ME, Windows 2000 or Windows
XP in any run of this project. **The x64 half was added 2026-09-12**: on a
freshly reverted guest carrying one install of the same binary, an enable
came back with one device of three and held that way for a measured ten
minutes, with usbport's ring showing the same signature (the Windows 7 x64 block at the end of section 2).
**So this is not an x86 defect, and it is not a property of the 32-bit
binary** - it reproduced on a separate amd64 build from a separate
toolchain against a different `usbport.sys`. Whether it reaches real
hardware is still unknown; no run has looked.

**The file keeps its `07-win7-x86-...` name** so existing links stay
valid, and the name is now narrower than the issue. Vista x64 is **not**
listed above: what that guest showed on 2026-09-12 is a *remove/rescan*
wedge, a different clause from the enable, not tested under `-smp 1`, and
it is recorded in roadmap task 22.5 rather than here.

The short version: on a clean install with three devices on root ports and
no hub, install, first plug, disable and restart-recovery all pass, but a
**disable/enable cycle** intermittently comes back with the audio device
missing. The device is missing because its second `GET_DESCRIPTOR` -
the configuration descriptor - never completes to the caller.
This driver does complete that transfer, with 252 bytes, and usbport
accepts the completion; usbport then fails to deliver it, because the DPC
that drains its done list was left permanently marked "queued" by a
write-after-queue race in `USBPORT_Xdpc_iSignal`. The enumeration thread
is parked in a `KeWaitForSingleObject` with a NULL timeout and
`Alertable = FALSE`, so nothing - not the cancel that eventually arrives,
not a rescan - can release it. Only a driver reload does, which is why a
restart recovers and a rescan does not.

## 1. The symptom

The guest is QEMU under TCG with four vCPUs, a clean Windows 7 x86 install
from the `win7-clean-install` snapshot, this driver's `qemu` flavour
installed from a staged package, and exactly three devices plugged on root
ports 1, 2 and 3 - nothing else ever attached, and no hub anywhere. The
sequence is: boot, first plug, then Device Manager disable followed by
enable, repeated.

What passes, every time it has been tried:

- install
- the first plug, all three devices
- disable
- recovery by restarting the guest

What fails, intermittently: **the enable**. The Audio Device does not come
back. The other two devices do. The visible device tree is otherwise
identical to a healthy one.

**The rate is not a measurement, and this is the most important caveat on
the page.** Run `r2` saw it 3 times in 5; run `r4` saw it once in 11. Run
`r4`'s twelfth load showed the Audio Device absent and then **present again
with no intervention at all** (`DescIsoEntries` moved 1 -> 2 on its own).
So a cycle sitting in the failing signature can still recover, "arrested"
and "merely slow" are distinguished by nothing that was measured - only by
how long the observer waited - and the differing rates are plausibly an
artefact of waiting time rather than of anything about the configuration.
The longest wait recorded is run `r4`'s load 4: the failing signature held
with `InterruptCount` frozen for about six minutes and had not recovered
when the cycle was ended by a disable. That is a long wait. It is not
proof of permanence.

**A related trap this leg paid for twice: on a TCG guest, slow looks
exactly like wedged.** A disable and a shutdown were each called stuck and
each then completed by itself. Judge by a long fixed timeout, never by "it
is not moving now".

Two further symptoms, both of which the mechanism in section 4 explains:

- **A disable issued while an arrest is live can wedge.** No teardown
  reaches the driver at all - `HealthPolls` keeps climbing (this driver's
  own timer is independent), `RhPortStatusQueries` freezes, and all four
  vCPUs sit in `HLT`, which is a blocked wait rather than a spin. On run
  `r4` this blocked the guest's shutdown too and needed a monitor
  `system_reset`.
- **Shutting down from an arrest has twice failed to reach this driver's
  suspend path promptly**, with Windows sitting at "Shutting down..."
  while the driver's health-poll clock still advanced. Do not over-read
  it: an ordinary shutdown taken from a **healthy** state on the same run
  completed normally after several minutes, through
  `SuspendController` -> `save: declined` -> `DisableInterrupts` ->
  `StopController`.

## 2. What the runs recorded

The driver's own counters, read from the arrest through `XHCISNAP`:

| reading | failing | healthy |
|---|---|---|
| descriptor rounds folded | **7** | 9 |
| committed | **5** | 6 |
| partial | **2** | 3 |
| `DescIsoEntries` | **1** | 2 |
| `SlotsEnabled` | 3 | 3 |
| `DevicesAddressed` | 3 | 3 |
| `DevicesReopened` | **3** | 3 |
| `OpensTotal` / `OpensAccepted` | 11 / 11 | 11 / 11 |
| every refusal counter | 0 | 0 |

So exactly **one descriptor round is missing**: the healthy loads fetch the
audio device's configuration descriptor twice, the failing load once.

A second reading, taken live from the arrest of run `r5` before the guest
was shut down and confirmed stable across two reads:

| reading | value |
|---|---|
| `TransfersSubmitted` | **54** |
| `TransfersCompleted` | **54** |
| `TransfersRefused` / `RefusedRingFull` | 0 / 0 |
| `TransfersCancelled` / `Aborted` / `FailedGone` | 0 / 0 / 0 |
| `SubmitDepth` | **0** |
| `CompletionHead` / `CompletionTail` | **NULL / NULL** |
| `CompletionsOwed` | 0 |
| `SubmitUnderflows` | 0 |
| `HealthPolls` | climbing, 598 -> 670 |
| `InterruptCount` / `DpcCount` | frozen at 73 |

**Everything this driver was given, it completed. It is holding nothing,
it refused nothing, and it owes nothing.**

The decisive evidence is usbport's own log ring, a 2048-record circular
buffer in the FDO extension. Two were captured with the extension live and
signature-checked: the arrest of run `r5`, and a healthy first plug for
comparison. The healthy one holds 2016 work records, nine submits and
**zero** cancellations; both arrests hold exactly one cancellation each.

**Two traps in reading that ring, each of which cost a cycle:**

- **The dump is newest-first and the indices wrap.** The file starts at
  the write position and runs, for example, `817 -> 2047 -> 0 -> 816`, so
  **increasing index means older** and `tac` gives chronological order.
  Getting this backwards inverts the entire story.
- **The ring is on by default; capture is method, not luck.**
  `USBPORT_DebugLogEnable` is a `.data` variable at RVA `0x29440` shipping
  as `1`, with exactly one reference in `.text` (a read at
  `USBPORT_LogAlloc+0x31`) and no write anywhere in the module. An early
  observation that "logging is off on this boot" was a **stale pointer**,
  taken just after a disable had torn the driver down, so `+0x324` led to a
  dead FDO. Before concluding logging is off, take the extension from the
  current load and check its signature and its `'HFDO'` at `+0x324`.

### The Windows 7 x64 reproduction, 2026-09-12

Taken on `vm\win7-x64.img` reverted to `win7-x64-clean-install` and given
**one** install of the amd64 `qemu` binary built Sep 12 2026 14:25:43 - one
driver generation through one devnode. Install, all three devices and the
Device Manager disable all passed, `read-v300.ps1 -Expect nt6` reading
ALL PASS. The enable then came back short and **stayed short across a
threshold fixed at ten minutes before the run** (twenty samples at thirty
seconds, every value identical, the debug log untouched throughout):

| reading | failing enable | healthy |
|---|---|---|
| `SlotsEnabled` | **2** | 3 |
| `DevicesAddressed` | **1** | 3 |
| `DevicesReopened` | **1** | 3 |
| SET_ADDRESS interceptions | **1** | 3 |
| endpoints configured | **2** | 3 |
| `DescIsoEntries` | **0** | 2 |
| interface selections | **0** | 2 |
| `TransfersSubmitted` / `Completed` | 0xA4 / 0xA4 | equal |
| every refusal counter | 0 | 0 |

The counters are not the same set the x86 table above uses - `XHCISNAP` was
not run on this guest - but the conclusion is the same one: **submitted
equals completed, every refusal counter is zero, and this driver is holding
nothing.**

usbport's ring says the rest. The whole 1,024-record buffer - about the
last 105 seconds at the measured rate - is **its idle poll and nothing
else**: `Tmt2` 293, `Tmt0`/`chgZ`/`chg0`/`nes+`/`nes-` about 146 each,
**zero work records**. `Tmt2` names **exactly two distinct objects**, each
examined once per tick across 146 ticks, for ever. That is the same shape
as the Vista x64 stall of 2026-09-11, which recorded exactly two objects
per tick, one per blocked thread.

**Two measurements needed to read a ring on these guests, neither of which
carries across builds:** the usbport FDO extension is
`miniportExtension - 0x1500` on Windows 7 x64 against `- 0x16E0` on Vista
x64, and the ring header is shifted eight bytes with it (index at `+0x18`,
mask `+0x20`, base `+0x28` on Windows 7 x64). Find the offset by scanning
back from the signature-checked miniport extension for the `'HFDO'` dword
`0x4f444648` - six `x/256xg` over the 0x3000 below it answers in under a
minute and needs no disassembler.

**What the two objects are was not established.** Naming them is a static
read of Windows 7 x64's `usbport.sys`, and it has not been done, so the
boundary statement in section 5 is unchanged by this run.

## 3. The wrong turns

Eight, and every one of them was closed by a measurement rather than by
argument. They are here because each is a plausible thing to re-raise.

**1. This driver's `SET_ADDRESS` interception.** Exonerated by measurement
early in the leg: `DevicesAddressed` is 3 in both the failing and the
healthy case.

**2. "usbport never reopened EP0."** Run `r2` showed `DevicesReopened 2`
where a healthy load showed 3, and that looked like the family's mechanism.
Runs `r4` and `r5` **refute it**: the failure reproduces with
`DevicesReopened 3`. `USBPORT_PokeEndpoint` not being called for device 3
in `r2` was real, but it is not what the later arrests do.

**3. A leaked `SubmitDepth` pinning a completion for ever.** This is a real
mechanism in this driver - `src\xhci_slot.c:10632` deliberately holds
completions while `SubmitDepth != 0`, leaving delivery to the next event
DPC or `XhciSlotPoll` - and `CompletionsHeldBySubmit` reads 1 in the
arrest, which looks like a confirmation. **Refuted by the same live read
that raised it**: `SubmitDepth` is 0 and `CompletionHead` is NULL. That
counter is a historical latch, once per pass by design (Phase 7 review B5),
not a stuck state. Do not re-raise this without re-reading those two
fields.

**4. "The transfer never reached the miniport."** Refuted by the ring,
which logs a full submit path for the stuck transfer.

**5. "usbport logged a submit and never a completion, so the two sides
disagree."** This was written up as the central open question and it is
**false**. The stuck transfer does carry completion records -
`cmpT 000000FC`, `cmpU`, `cpt0`. The grep that missed `cmpT` missed it
because that record's three words are
`length | 0 | &transferParameters`, and the transfer parameters live at
`transfer + 0xCC`, so searching for the transfer's own address never finds
it. There is no disagreement between the two sides.

**6. "The stuck transfer's submit timeout of zero is the anomaly."** The
`subt` record for the stuck transfer carries `00000000` where two other
transfers in the ring carry `00001388` (5000 ms), and that contrast looked
diagnostic. It is not: the **three healthy transfers on the same IRP also
carry zero** and complete normally. A zero does mean usbport arms no
timeout for that transfer (`USBPORT_Core_InitTimeout+4a` logs `tmoZ` and
clears the deadline), so nothing in usbport will ever give up on it - but
it is the normal state for this class of transfer, not the defect.

**7. "usbport's log ring is a dead end - it only holds about 1.2 seconds
of history."** Recorded early in the leg and wrong in practice. A ring
dumped within about two minutes of the arrest carries real history; one
taken nine minutes later was all idle poll. The two-minute rule is the
usable form of that observation.

**8. "No disassembler is available and none is needed."** Recorded when
`link -dump -disasm` was found to fail on this image (`LNK4195`, missing
`msdis160.dll`; the repo carries only `msdis109`/`110`). **Retired on
2026-09-12**: `python -m pip install --target <scratch>/pylibs capstone`
installs in seconds and reads the `.text` bytes directly. None of section 4
would have been found by hand-decoding bytes, and the attempt to do so is
what made the first pass stop one function short of the cause. (Trap: do
not name the scratch script `dis.py` - Python's own `dis` module shadows it
and capstone fails with a bogus circular-import error.)

## 4. The mechanism

### 4.1 What the ring's submit and completion tags actually bracket

usbport writes about 80 four-character ASCII tags into its ring; all 144
seen across the two captured rings are now decoded in
`scripts\local\usbport-ring-tags-win7-x86.txt`, with the recipe. Four of
them matter here, and their names are misleading:

`USBPORT_Core_iSubmitTransferToMiniport` is at RVA `0x83e2`.

- `sub0` (+`0x3a`) and `sub1` (+`0x7b`) are **entry logging only**.
  `sub1`'s third word is `[endpoint+0x38]`, the endpoint state - `4` in
  every record of this ring.
- The miniport is reached only on endpoint state 4, through
  `iSetGlobalEndpointStateTx(fdo, ep, op = 0x10, transfer, &status, 0)` ->
  `MPx_SubmitTransfer` -> `MPf_SubmitTransfer` ->
  `call dword ptr [regPacket+0x6c]`. Endpoint states 2, 3, 7 and 0xb set
  status 5 and anything else sets status 1, both **without calling the
  miniport at all**.
- `subt` (+`0x2d7`) and `subx` (+`0x31b`) bracket
  **`USBPORT_Core_InitTimeout`, not the miniport call**. But they are
  reached only when the returned status is 0, and status 0 can only come
  from the miniport's own return - so **a `subt` record does prove this
  driver's `SubmitTransfer` was called and accepted**, just not for the
  reason the tag's position suggests.

On the completion side, `cmpT` and `cmpU` are
`USBPORTSVC_CompleteTransfer`, the service **this driver calls**, and
`cpt0` is `USBPORT_Core_iCompleteTransfer+0x96`.

### 4.2 Where the enumeration stops

Read chronologically, the audio device's enumeration in run `r5`:

| record | tag | site | what |
|---|---|---|---|
| `[1478]` | `SENc` | `USBPORT_SendCommand+56` | entered |
| `[1468]` | `sndC` | `+1ba` | `0x12 / 0x80 / 0x06` - GET_DESCRIPTOR, 18 bytes, the **device** descriptor |
| `[1376]` | `sWTt` | `+242` | enters the wait |
| `[1030]` | `sWTd` | `+281` | **the wait returns** |
| `[997]` | `iniD` | `USBPORT_InitializeDevice+302` | |
| `[887]` | `SENc` | `+56` | entered again |
| `[884]` | `sndC` | `+1ba` | `0xFF / 0x80 / 0x06` - the **configuration** descriptor, the second round |
| `[717]` | `sWTt` | `+242` | enters the wait |
| - | - | - | **no `sWTd` ever follows** |
| `[336]` `[330]` | `txCA` `txC1` | `USBPORT_TxCsqCompleteCanceledIrp` | the IRP is cancelled |

`USBPORT_SendCommand`'s wait is **infinite and non-alertable**. At
`+0x242` the tag is written, then
`push esi; push esi; push esi; push 5; lea eax,[ebp-1Ch]; push eax;
call [imp]` - arguments in reverse, so `Timeout = NULL`,
`Alertable = FALSE`, `WaitMode = KernelMode`. `sWTd` at `+0x281` is logged
immediately after that call returns, so the pair brackets the wait exactly.

This is not an argument from counts: about 700 records newer than `[717]`
survive in the ring, the cancellation among them, and none is an `sWTd`.
The wait had genuinely not returned when the dump was taken. **The cancel
cannot release it**, which is why the cancel is a consequence - something
above giving up on a request that never completes - and not the cause.

### 4.3 Why the completion never arrives

The stuck transfer `84566CB8` was submitted, accepted, and **completed by
this driver with 252 bytes**. Chronologically:

```
[ 987] sub0    entry
[ 986] sub1    endpoint state 4
[ 976] subt    timeout 0
[ 975] tmoZ    no timeout armed
[ 974] subx
[ 951] cmpT    0xFC = 252 bytes          <- this driver completes it
[ 950] cmpU
[ 948] cpt0    status 0                  <- usbport accepts the completion
```

A **healthy** transfer continues from there into
`Dne1 Dne2 Dne3 Dne5 Dne7 decE tRM2 x4 Dne8` - that is
`USBPORT_Core_UsbDoneDpc_Worker` draining the done list - and then
`Ioc1`, `CPX1 CPX4 CPX6 CPX9`
(`USBPORT_Core_iIrpCsqCompleteDoneTransfer`), `cptU`, `freT`, and the IRP
is completed.

The stuck transfer reaches **none of it**. Instead it keeps reappearing in
`gNX1`/`gNX3` (`iGetNextPriorityTransfer`) and `Map0`/`Map6`
(`UsbMapDpc_Worker`) rounds, and in `Tmt2`
(`Core_TimeoutAllTransfers`), for the rest of the ring - it was never
removed from the endpoint's priority list, because only the `Dne*` block
removes it.

**`USBPORT_Core_UsbDoneDpc_Worker` ran three times in the entire
2048-record ring and never after index 1223.** Four completions were
accepted after that point - `cpt0` at indices 948, 711, 640 and 582 - and
**not one of them was ever drained.** The stuck transfer is simply the one
that mattered.

### 4.4 The break, in six records

Each FDO carries four Xdpc objects, `0x128` apart. On run `r5` they were
`845C6720`, `845C6848` (**done**), `845C6970` (map) and `845C6A98` (ioc).
`xdw2`'s and `xSt0`'s last word is the object's state at `[obj+0x9c]`:
1 idle, 2 queued, 3 running.

| idx | record | what |
|---|---|---|
| 950 | `cmpU  .. 84566CB8 ..` | this driver calls `USBPORTSVC_CompleteTransfer` |
| 948 | `cpt0  00000000 85C47778 84566CB8` | `Core_iCompleteTransfer`, status 0 |
| 947 | `Xsi1  845C6848` | `Xdpc_iSignal` on the **done** DPC |
| 946 | `xdw0  845C6848` | the DPC **fires** |
| 945 | `xdw2  845C6848 .. 00000001` | it reads state **1 = idle** |
| 943 | `xdw8  845C6848 .. 00000001` | **the do-nothing branch** |
| 941 | `xSt0  845C6848 00000007 00000002` | `Xdpc_iSignal` now stores state **2** |

That `xdw8` is **the only one in all 2048 records.**

Read off the binary, this is a write-after-queue lost wakeup:

- **`USBPORT_Xdpc_iSignal`** (RVA `0x2fae`), on state 1, calls
  **`KeInsertQueueDpc` first** - `call dword ptr [0x381a0]`, confirmed
  against the import table - and only **then** calls `Xdpc_iSetState`,
  which is what actually stores `[obj+0x9c] = 2`. It takes **no lock**
  around either.
- **`USBPORT_Xdpc_Worker`** (RVA `0x185b`) takes
  `USBPORT_TxAcquireLock` and then reads `[obj+0x9c]`: **2 dispatches the
  worker, 6 re-arms, and anything else logs `xdw8` and returns having done
  nothing.**
- So if the queued DPC runs before the store - four vCPUs here - the
  worker sees the stale 1 and drops the work.
- **And it never recovers.** From then on the state is 2, and
  `Xdpc_iSignal`'s state-2 branch **re-writes 2 and queues nothing**. The
  ring shows exactly that: `Xsi1 845C6848` three more times, at indices
  710, 639 and 581, each followed only by `xSt0 -> 2`, and **no `xdw0` for
  that object ever again.**

Everything else keeps running, which is why the controller looks healthy
from outside: the map DPC and the Xdpc worker itself are alive and busy
throughout the remaining records.

### 4.5 What that explains

- The missing descriptor round, and so the missing devnode.
- The enumeration thread blocked for ever in a non-alertable wait.
- The wedged disable and the two blocked shutdowns: both are queued behind
  that thread.
- Why a restart clears it and a rescan does not - only a reload frees the
  thread and re-initialises the DPC objects.
- Why it is intermittent: it needs the DPC to win a window of about a
  dozen instructions.
- Why `InterruptCount` and `DpcCount` freeze while `HealthPolls` climbs -
  this driver's poll timer is independent of the path that is stuck.

## 5. What this leaves for this driver, stated carefully

**This is the part to read before quoting the page.**

What the evidence licenses:

- Everything this driver was given, it completed; it holds nothing,
  refused nothing and owes nothing, at the moment of the arrest.
- The transfer that the enumeration is waiting on was submitted to this
  driver, accepted by it, and completed by it with data.
- The completion was accepted by usbport and then dropped inside usbport,
  between `Core_iCompleteTransfer` and `UsbDoneDpc_Worker`.
- The code path that drops it is guarded by a lock
  (`USBPORT_TxAcquireLock`) that is internal to usbport and **not exposed
  to miniports**, and the signalling side does not take it.

What the evidence does **not** license, and must not be written down as
settled:

- **That the defect is outside this driver.** On the present reading there
  is nothing a miniport holds, sets or orders that closes that window -
  but that is a reading of one binary, and it has not been tested.
- **That a single vCPU makes it go away.** This is the diagnosis's central
  prediction and it is **unrun**.
- **That anything about this driver's traffic shape is irrelevant.** How
  often completions are delivered, from which context, and how many per
  drain pass, could all affect how often that window is entered. None of
  it was measured.
- **That other host controller drivers hit it.** `usbport.sys` is shared
  with EHCI and UHCI, so if this is real it is a latent Windows defect
  rather than an xHCI one - but no other driver was tested, and this is
  inference.
- **That an arrest is permanent.** Run `r4`'s load 12 recovered on its
  own. Nothing measured separates "arrested" from "slow".

## 6. What would confirm or refute it, cheapest first

1. ~~**The same guest with `-smp 1`.**~~ **TAKEN 2026-09-12, ON WINDOWS 7
   x64, AND THE PREDICTION HELD.** Same guest, same image, same binary,
   same three devices, same operator sequence, inside one hour: under
   `-smp 4` the enable arrested at two slots and one device and stayed
   there for a measured ten minutes; under `-smp 1` (`info cpus` listing
   only CPU #0) it passed **five consecutive disable/enable cycles**. All
   six loads of that run end at `SlotsEnabled 3` and `DescIsoEntries 2`,
   ALL PASS each time, `isr count == dpc count` throughout, and **not one
   non-zero refusal counter** - not even the `transfers refused for
   retry=1` that every four-vCPU run of this project carries. Removing the
   second processor removed the symptom, which is what a write-after-queue
   race needing two processors predicts, and section 4.4 survives it.

   **Three things it does not establish, and they matter.** It was run on
   **x64, not on the x86 guest this page is named for** - that leg has
   still not been re-run single-processor. **Five clean cycles is not proof
   of absence**: leg 4's own rate wandered from three-in-five to one-in-
   eleven, and this page's section 1 says why a rate measured without a
   fixed wait threshold is not a measurement at all. And it names nothing:
   **the two objects usbport holds are still unidentified**, so section 5's
   refusal to say "the defect is not ours" stands untouched.
2. **A run with QEMU's `usb_xhci_xfer_*` trace enabled.** It was off for
   both `r4` and `r5` (`scripts\local\xhci-trace-events.txt` carries only
   `port_*`, `slot_*`, `queue_event`, `irq_intx`). It says whether the TD
   was placed on a ring and rung, which is now a cross-check on section
   4.3 rather than the discovery it would once have been.
3. **Whether any miniport-visible action can un-strand a DPC already in
   state 2.** On the reading above, no - every later signal is a no-op -
   but this was not chased, and a workaround, if one exists at all, lives
   here.
4. **Whether the window's width depends on this driver's delivery
   pattern**, which would turn an unfixable usbport defect into something
   this driver could make rare.

The first of these is now done, so the honest statement has moved, but not
as far as it may look. The arrest was localised to a usbport code path the
miniport cannot influence, on one reading of the binary; that reading made
a prediction; the prediction held on one target, five times over. What is
still missing is the identity of what usbport is holding, and a
single-processor run of the **x86** guest this page is named for. Item 2
below is now the cheapest useful thing left.

## Sources

Evidence, all under `vm\`:

- `vm\ring-r5-ARREST-enable\` - the arrest's ring (`.bin`, decoded `.txt`,
  and a 2 MB window), copied to
  `vm\task225-evidence\win7-x86-r5-ARREST-enable-ring-decoded.txt`
- `vm\ring-r5-healthy-firstplug\`, copied to
  `vm\task225-evidence\win7-x86-r5-HEALTHY-firstplug-ring-decoded.txt`
- `vm\ring-r4-cycle3\` - the first ring taken against a live extension
- `vm\ring-win7x64-re1\` - the 2026-09-12 Windows 7 x64 material: the
  arrest's debug log (`...smp4-arrest.log`), its ring
  (`win7-x64-ring-enable.txt`), the twenty threshold samples
  (`enable-threshold-samples.txt`), the five clean single-vCPU cycles
  (`...smp1-five-cycles.log`), and the exact one-off invocation used
  (`win7-x64-smp1.cmd`, identical to the generated launcher but `-smp 1`)
- `vm\ring-vistax64-re1\` - the Vista x64 material of the same evening. It
  belongs to a **different clause** (remove/rescan, not enable) and is
  listed here only so it is not lost
- `vm\task225-evidence\win7-x86-r5-ARREST-counters-full.txt`,
  `win7-x86-r4-cycle3-counters.txt`
- the debug-console logs `win7-x86-p225win7x86r2-ENABLE-NO-AUDIO-...`
  through `...r5-debugcon.log`, and the device-tree screenshots beside
  them, including `win7-x86-r4-load12-LATE-RECOVERY-tree.png`

Tools and tables:

- `scripts\local\usbport-ring-tags-win7-x86.txt` - all 144 tags, the
  method that decodes them, the four Xdpc objects and the state numbers
- `scripts\local\decode-usbport-ring.py`, `scripts\local\read-reopen.ps1`
- `tools\win7-x86-extracted\usbport.sys` and
  `tools\symbols\usbport.pdb\5224A6DF50AF4F28A0A7A60F3EA734812\`
  (PDB age 12), read with `tools\WinDDK71\Debuggers\dbh.exe` and capstone.
  `dbh` wants **hex**, prints a **leading blank line**, and fails silently
  in bursts - re-running the same loop works.

Working notes, `.claude\memory\` (git-ignored, so they may not be present):
`leg4-usbport-done-dpc-stranded-2026-09-12`,
`leg4-mechanism-sendcommand-infinite-wait-2026-09-12`,
`leg4-driver-exonerated-at-the-boundary-2026-09-12` (**its filename
overstates and its central claim is refuted by section 3 item 5**),
`usbport-ring-tags-and-the-cancelled-irp-2026-09-12`,
`healthy-ring-has-no-cancel-2026-09-12`,
`win7-x86-leg4-r4-descriptor-round-2026-09-12`,
`win7-x86-enable-stall-repeat-2026-09-12`,
`win7-x64-enable-arrest-2026-09-12`,
`smp1-clears-the-enable-arrest-2026-09-12`,
`vista-x64-retake-and-remove-rescan-wedge-2026-09-12`,
`usbport-internals-win7-x86-2026-09-12`,
`setaddress-wedge-hypothesis-and-usbport-survey-2026-09-12`.
