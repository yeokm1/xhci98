# Issue 7 - An enable on Windows 7 x86 intermittently loses one device, and the completion it is waiting for is dropped inside usbport's own DPC state machine

Status: **cause read and fixed in source on 2026-09-13, and the fix re-run
the same day on all four NT 6.x guests, every clause passing on each**
(section 7.5, including one Windows 7 x86 disable that Windows itself
refused and that is stated there, not smoothed over). Of the NT 5.x legs,
**Windows XP x64 has passed at four vCPUs** on the build carrying issue 8's
fix (section 7.8, 2026-09-14, after a bugcheck on the earlier build in
section 7.7), and **Windows XP SP3 x86 has passed at four vCPUs under WHPX**
on the build carrying issue 8's fix on the 32-bit tier (section 7.9,
2026-09-15, one run, after two TCG runs on that build stalled in PnP and were
set aside by the owner's decision as not representative). **Windows 98, ME and
2000 then passed single-core under TCG** the same night (section 7.10), each
through the clauses it can take: Windows 98 under NUSB takes no controller
stop at all, and Windows 2000 refused a live disable while a USB audio device
was attached, a refusal not yet explained.
Section 7 has the cause, the fix, what the re-run had to show and what it
showed.
The lost wakeup in usbport described below is real, and it is reachable
only because this driver called usbport's completion service from contexts
usbport was never written to expect; Microsoft's own miniport never does.
So the sentence this page carried until 2026-09-13, "the loss is entirely
inside usbport, on a path guarded by a lock not exposed to miniports", is
withdrawn: the lock is not exposed, and usbport assumes the miniport is
already inside it. Everything from here to section 7 is the record as it
was written on 2026-09-12 and is kept as it stood, with the corrections
marked where they land.

Was: open, and deliberately left open at the `1.1.0.0` cut on the
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
restart recovers and a rescan does not. **Why the race is reachable at all
is section 7**: the completion
that stranded the DPC was delivered from `RH_GetPortStatus`, at PASSIVE
level with no usbport lock held, and usbport's service assumes its caller
holds the lock that makes the queue-then-store safe.

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

**Superseded by section 7 on 2026-09-13.** The bullets below were true as
readings of usbport and are kept; what they left open - "whether the defect
is outside this driver" - is now answered, and the answer is no.

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
   here. *(2026-09-13: still no, and none is needed - the fix keeps the
   DPC from being stranded in the first place, section 7.)*
4. **Whether the window's width depends on this driver's delivery
   pattern**, which would turn an unfixable usbport defect into something
   this driver could make rare. *(2026-09-13: it does, entirely - the
   window is open only from the contexts this driver was delivering from,
   section 7.)*

The first of these is now done, so the honest statement has moved, but not
as far as it may look. The arrest was localised to a usbport code path the
miniport cannot influence, on one reading of the binary; that reading made
a prediction; the prediction held on one target, five times over. What is
still missing is the identity of what usbport is holding, and a
single-processor run of the **x86** guest this page is named for. Item 2
below is now the cheapest useful thing left.

## 7. The cause, read 2026-09-13, and the fix

Host-side, no guest. `tools\win7-x86-extracted\usbport.sys` and
`tools\win7-x64-extracted\usbport.sys` and `usbehci.sys`, with Microsoft's
public PDBs, read with capstone (`scripts\local\usbport-disasm.py`); the
r5 arrest ring and the healthy ring beside it; this driver's own callback
log, which records the IRQL of every callback. All of the binary facts are
**static** (`legal-provenance.md` section 4).

### 7.1 usbport's contract for its completion service

- `USBPORT_Xdpc_Worker` takes `USBPORT_TxAcquireLock`, which is
  `USBPORT_AcquireEpListLock` - a per-FDO spin lock (x64: fdo+0xF88) that
  is neither the ISR DPC's lock (fdo+0x1028) nor `MiniportSpinLock`
  (fdo+0x868). It reads the DPC's state under that lock, and it calls the
  worker function with the lock still held.
- `USBPORT_Xdpc_iSignal` takes no lock and, on state 1, queues the DPC
  before storing state 2 - section 4.4. The `i` is usbport's convention
  for "the caller holds the lock"; `USBPORT_Xdpc_Signal` is the locked
  wrapper and is what `IsrDpc` and the timer DPC call.
- **`USBPORTSVC_CompleteTransfer` acquires nothing** before calling
  `USBPORT_Core_iCompleteTransfer`, which ends in `Xdpc_iSignal` (x64 RVA
  `154dc`: `AssertSig`, the `cmpT` record, `AssertSig`,
  `EndpointFromHandle`, `Core_iCompleteTransfer`, `WmiLogEvent`; x86 RVA
  `8d32` the same). So the service is safe only from a caller usbport has
  already placed under the EpList lock.
- usbport provides exactly that caller: `USBPORT_Core_UsbHcIntDpc_Worker`,
  signalled from `IsrDpc` on every pass, walks every active endpoint under
  the lock and reaches `MPf_PollEndpoint` through
  `iSetGlobalEndpointStateTx` op `0xE`, once per endpoint per pass.
  `SubmitTransfer`, `AbortTransfer` and `SetEndpointState` are reached the
  same way (`iSubmitTransferToMiniport`, `Core_iAbortEndpoint`,
  `USBPORT_SetGlobalEndpointState+7a` takes the lock first).
- **Microsoft's usbehci obeys it.** Every call through Windows 7 x64
  `usbehci.sys`'s `RegistrationPacket`: the CompleteTransfer slot (amd64
  `+0x1D0`, x86 `+0xFC`) is called only from `EHCI_ProcessDoneAsyncTd+20f`
  and `EHCI_sMode_PollEndpointSlot+51e/+57c`, both inside `PollEndpoint`.
  `EHCI_InterruptDpcEx` calls `InvalidateEndpoint` (`+0x1C8`) and one
  300-tier slot, never the completion service.
  `USBPORTSVC_InvalidateEndpoint` on Windows 7 is an `AssertSig`-only
  no-op; the per-pass poll of every active endpoint replaces it.
- NT 5.x usbport has no Xdpc state machine: the same service does an
  interlocked insert onto the done list and queues a plain DPC, so it is
  safe from any context. That is why 98, ME, 2000 and XP never see this.

### 7.2 What this driver did, and the r5 break with its cause

`XhciSlotDeferredWork` delivered completions from whichever context reached
it - the event DPC (inside usbport's `IsrDpc`, under its interrupt lock, not
the EpList lock), the health poll, the lifecycle paths, and
`XhciRhGetPortStatus`. This driver's `PollEndpoint` was a logging stub and
its `InterruptDpcEx` never reported bit 0, "because this driver completes
transfers from the drain itself".

The driver's own log records `cb RH_GetPortStatus irql=00` on Windows 7 x86
and x64 alike, and the binary says why: `USBPORT_RootHub_ClassCommand`
calls `RH_GetPortStatus` (x86 `+514`) and `RH_GetHubStatus` (`+540`)
directly, with no lock, at the URB dispatcher's IRQL; only the
Set/ClearFeature requests go through `USBPORT_RootHub_PortRequest` and its
spin lock. **Design record 05's row "root-hub status queries: DISPATCH,
`MiniportSpinLock`" was wrong for NT 6.x** and is corrected.

The r5 break re-read with that (newest-first indices, read down):

| idx | record | who |
|---|---|---|
| 964/963 | `iDP+` `iDlk` | CPU B: `IsrDpc` calls our InterruptDpc; the event DPC retires 84566CB8 and queues its completion; its own drain hands off |
| 955..952 | `quTR neo1 ctw1 rCCM` | CPU A: the hub driver's GET_PORT_STATUS to the root hub enters `RootHub_ClassCommand` - no `rSCM` (`PortRequest`) record follows, so this is the direct, unlocked `RH_GetPortStatus` |
| 951/950 | `cmpT 0xFC` `cmpU` | CPU A, PASSIVE, no usbport lock: `XhciRhGetPortStatus` -> `XhciSlotDeferredWork` delivers the completion CPU B parked |
| 948/947 | `cpt0` `Xsi1` | `Core_iCompleteTransfer`, `Xdpc_iSignal` on the Done DPC, state 1, `KeInsertQueueDpc` - **at PASSIVE the DPC interrupt is taken on CPU A at once** |
| 946/945 | `xdw0` `xdw2 1` | the worker runs on CPU A inside the window, takes the EpList lock (nobody holds it), reads 1 |
| 944 | `iDuk` | CPU B releases its interrupt lock: B's bracket 963..935 encloses A's records, so they are two CPUs |
| 943 | `xdw8` | the do-nothing branch |
| 941 | `xSt0 7 2` | CPU A stores 2. Stranded |

The control: every one of the eight completions in the healthy ring, and
six of the eight in the arrest ring, were delivered from inside
`iDlk..iDuk`, and for those the DPC fires only after `iDP-` - a DPC queued
at DISPATCH on the same CPU waits for the ISR DPC to return, and usbport
sets no target processor (`Xdpc_InitDpc` is `KeInitializeDpc` alone). The
only way the worker runs before the store is the signalling CPU being
below DISPATCH, and the root-hub query is that path.

Why the symptoms looked as they did: it needs the ISR DPC on one CPU to
retire a transfer while a root-hub query on another is inside the drain, so
the query delivers it - hence intermittent, hence rare but not impossible
under `-smp 1` (the interrupt must then land inside the query's own drain
window), hence heavier during enumeration, when the hub polls port status
most. And it needs NT 6.x usbport.

### 7.3 The fix

Three parts, all in this driver, host-tested (three new vectors and two
never-reset nets), built for both architectures, and **run on the four NT
6.x guests on 2026-09-13** (section 7.5):

1. **Every completion is handed over at DISPATCH_LEVEL**, on every tier and
   from every context, by holding a private spin lock (`xhciDeliveryLock`,
   `src\xhci_cmd.c`) across the `UsbPortCompleteTransfer` /
   `UsbPortCompleteIsoTransfer` call. The lock guards no state; it is the
   raise, because the import ceiling has spin locks and no `KeRaiseIrql`.
   This alone closes the same-CPU race above.
2. **On the Version 300 tier a completion is delivered only from a
   callback usbport made under its EpList lock** -
   `XHCI_EXTENSION.DeliverUnderUsbportLockOnly`, set in `StartController`
   from the interface version presented. `PollEndpoint` now drains, and
   the drains inside `AbortTransfer` and `SetEndpointState` are
   `XhciSlotDeferredWorkLocked` - the admission travels with the call, not
   with a per-controller counter another CPU could ride; the event DPC
   retires and reports
   `USBPORT_DPC_EX_TRANSFER_WORK`, as usbehci does, and every other context
   parks the completion and counts `completions held for PollEndpoint`.
   This is what closes the transient shape too - an unlocked signal racing
   `Xdpc_End`'s state-3 read - which the raise alone does not.
3. **Two overrides so nothing can wait for ever**: the lifecycle drains
   (suspend, stop, resume, recovery) force delivery, and the health poll
   delivers anything parked for more than `XHCI_COMPLETION_FALLBACK_MS`
   (1000 ms, two poll periods), counting `completion fallback polls`. A
   nonzero fallback count during ordinary traffic says usbport stopped
   polling an endpoint that owed work, and is the first thing to read.

The 200 tier keeps delivering from the drain, as it always has, and the
suite's existing nets (never under the controller lock, never inside a
submit) hold unchanged.

### 7.4 What the re-run must show, and what it does not settle

The four NT 6.x guests under `-smp 4`, `qemu` flavour: install, three
devices, disable, enable through five cycles each, remove and rescan, with
the counters read after every cycle - `completions delivered under
usbport's lock` carrying nearly everything, `completions delivered forced`
small, `completion fallback polls` zero. Then the NT 5.x legs (98, ME,
2000, XP32, XP x64), because the delivery lock and the `PollEndpoint`
drain touch them too. Five clean cycles per guest is what this page said
it is: not proof of absence. Vista x64's remove/rescan wedge is a different
clause and is not claimed by this fix until it is re-run.

### 7.5 The re-run, 2026-09-13: the four NT 6.x guests

Each guest was reverted to its clean-install snapshot (`qemu-img check`
clean before and after), installed **once** with the `qemu` flavour, run
under `-smp 4` on QEMU 11.1.0, and put through the section 7.4 sequence:
install, the three devices (HID mouse, mass storage, composite audio),
five watched disable/enable cycles, remove (Uninstall, package left in the
store) and a rescan. **The wait threshold was fixed at ten minutes before
every click and held**; a watcher sampled the debug log every 30 s from
before the click to the result. The binaries: amd64 sha256 `249badfe...`,
built `Sep 13 2026 00:44:14`; x86 sha256 `A74734C3...`, built `Sep 13 2026
00:40:04`. The stamp was read on every load.

| guest | tag | install + devices | enables | remove / rescan |
|---|---|---|---|---|
| Windows 7 x64 | `fix7win7x64` | ALL PASS; 3 slots / 3 reopened / iso 2 | 5 of 5, each within 30-31 s | remove complete within 30 s; rescan clean |
| Vista x64 | `fix7vistax64` | ALL PASS; 3 / 3 / iso 2 | 5 of 5, each within one 30 s sample | remove complete within the first 30 s sample; rescan clean |
| Windows 7 x86 | `fix7win7x86` | ALL PASS; 3 / 3 / iso 2 on all seven loads | 5 of 5, each within 30-60 s | remove complete within one sample; rescan clean |
| Vista x86 | `fix7vistax86` | ALL PASS; 3 / 3 / iso 2 on all seven loads | 5 of 5, each within one 30 s sample | remove complete within one sample; rescan clean |

Every tree after the rescan showed the controller, the root hub, the
composite and mass-storage devices, the HID mouse and the audio device,
with no bangs. Windows 7 x64 is the guest whose enable arrested on the
**first** attempt on 2026-09-12, and Windows 7 x86 is the guest this page is
named for; on 2026-09-12 its fourth leg had also come back with two devices
reopened and no iso endpoints, and on this run all seven loads read three
and two.

**The counters, and how far each can be trusted.** On the two x86 guests
they were read out of guest memory (`XHCI_EXTENSION`, 91,644 bytes,
signature-checked at the base) after every load: `transfers completed` is
`transfers submitted` less 2, `completions delivered under usbport's lock`
is `transfers completed` less 1 to 3, `completions held for PollEndpoint` is
in the tens of thousands, and **`completions delivered forced`, `completion
fallback polls` and `ResetController` are 0 on every load**. On the two
amd64 guests the extension is 95,528 bytes, the x86 offsets table does not
describe it, and the change-gated debug log is the only instrument. That
log caps each print site at 32 samples a load, so there `delivered under
usbport's lock` (31-32 against 31 completed) and `held for PollEndpoint`
are lower bounds taken at the cap. `forced 0`, `fallback 0` and `only under
usbport's lock 1` are exact there too: a counter that never changes prints
no change for the cap to hide. `forced` is 0 rather than the small number
section 7.4 expected because the lifecycle drains found nothing parked.

**One deviation, on Windows 7 x86, and its cause is not known.** The first
disable after the hot-plug was refused by Windows: Device Manager asked to
restart twice, the operator declined both times, the watcher saw nothing
reach the driver, and the device then offered Enable. No Explorer or
AutoPlay window was open. That disable was applied by restarting the guest
instead; the shutdown took the ordinary suspended-controller stop, so that
guest's first enable followed a restart rather than a live disable.
**Event Viewer was not read before the restart, so what vetoed it is not
recorded.** Disables 2 to 5 on that guest, and every disable on the other
three, applied live with no prompt, and the teardown finished within one
sample each time. On Vista x86, run afterwards with the same sequence, the
first disable applied live. A disable that asks to restart never reaches this
driver, so this is not evidence against the fix. It is still an unexplained
refusal, and the next guest that shows one should have Kernel-PnP event 225
read before anything else.

What this establishes: on each of the four NT 6.x guests, under four
virtual processors, the sequence that arrested before the fix completed
five times over, with the completion path running as section 7.3 designed
it and the fallback never needed. It does **not** establish absence: five
cycles a guest is what section 6 said it is. It does not reach real
hardware. **The NT 5.x legs have not been run against this binary**, and
the delivery lock and the `PollEndpoint` drain change them too; there the
200 tier must read `completions delivered only under usbport's lock` 0.
Vista x64's remove wedge of 2026-09-12 did not recur in one remove. That
suggests it shared this cause, but one remove does not show it, and the fix
was not written against it.

### 7.6 The first NT 5.x leg, 2026-09-13: Windows XP x64

Windows XP x64 SP2 is the `200` arm on amd64 - the only guest in this project
that takes the pre-6.00 three-argument registration on a 64-bit build - and it
is the first of the five NT 5.x legs section 7.4 owes. It was reverted to
`winxp64-clean-install` (`qemu-img check` clean before and after, 13.36%
allocated and image end offset 2,296,971,264 both times), installed **once**
from the `qemu`-flavour amd64 package staged in `vm\xferxp64` (sha256
`249badfe...`, built `Sep 13 2026 00:44:14` - the same binary section 7.5's
two 64-bit guests ran), and put through the section 7.4 sequence: the three
devices, five watched disable/enable cycles, remove and rescan. The wait
threshold was fixed at ten minutes before every click and held; a watcher
sampled the debug log every 30 s from before the click to the result. Seven
loads, the stamp read on every one, `read-v300.ps1 -Expect nt5` **ALL PASS**
on every one.

| stage | reading |
|---|---|
| install + three devices | ALL PASS; 3 slots / 5 addressed / 5 reopened / iso 2 |
| disables 1-5 | all five applied **live**, no restart prompt; teardown complete inside one 30 s sample each |
| enables 1-5 | 5 of 5 healthy inside one 30 s sample each; 3 slots / 3 reopened / iso 2 on every reload |
| remove | complete, in **two** 30 s samples |
| rescan | healthy in two samples; tree clean |

The install load's `5 addressed / 5 reopened / 5 SET_ADDRESS interceptions`
against 3 slots is not a property of the 200 tier: it is the first-install
sequence, where the three devices were hot-plugged one at a time into a stack
that had just bound. Every one of the six later loads enumerated all three in
one pass and read 3 / 3 / 3. The rescan's tree carried the controller, the
root hub, the composite and mass-storage devices, the HID mouse and the audio
device, with no bangs - the same list the task 21.5 leg of 2026-09-09 read on
this guest.

**The counters.** The tier is confirmed on every load: `wdm pre-6.00` 1,
interface version presented `000000C8`, **`nt6 services written` 0**, so the
300 tier's locked-callback path is not in play here at all. What section 7.4
asked of this tier is met: **`completions delivered only under usbport's
lock` reads 0**, and so do `delivered under usbport's lock`, `held for
PollEndpoint`, `delivered forced` and `completion fallback polls` - on all
seven loads. Those five are exact rather than capped, by section 7.5's own
reasoning: a counter that never changes prints no change for the print cap to
hide. `PollEndpoint callbacks` ran 31 to 3,703 a load, which is this usbport
polling hard and is the 200 tier behaving as designed. `isr count` equalled
`dpc count` on every load, `ResetController` was 0 throughout, and there are
no memory reads on amd64, so the transfer totals are lower bounds at the cap.

**This guest ran on ONE vCPU, and that bounds the leg hard.** Its launcher
carries no `-smp`, where all four of section 7.5's guests ran `-smp 4`. Under
`-smp 1` the *unfixed* binary also passed five consecutive clean cycles on
Windows 7 x64 (section 6's experiment, taken 2026-09-12), so **a uniprocessor
pass cannot discriminate a fixed binary from an unfixed one** for the race
this page is about. This leg is therefore evidence for what section 7.4 asked
the NT 5.x legs for - the delivery lock and the `PollEndpoint` drain do not
break the 200 tier, and the 200 tier reads the zero it must - and it is **not**
evidence that the fix holds under contention on NT 5.x. Nothing here is an SMP
result.

**Correction, made the same evening.** This section first said that raising the
guest would mean reinstalling it, the HAL being fixed at install time. **That
is wrong, and the rule it leaned on does not reach this guest.** The "HAL is
fixed at install time" lesson was written for the 32-bit targets, where the
HAL zoo and the `ntoskrnl`/`ntkrnlmp` split are real. XP x64 has **one** HAL,
and this guest has reported **`ACPI Multiprocessor x64-based PC`** since it was
installed on 2026-09-08. Adding `-smp 4` was all it took: Windows enumerated
four `QEMU Virtual CPU` processors, asked once for a restart to install them,
and then scheduled on all four (four Task Manager graphs; the dump of section
7.7 reads `MP (4 procs)`). No reinstall, no HAL switch. Section 7.7 is what
that produced.

**One deviation, stated rather than smoothed over.** The remove took two 30 s
samples where every guest in section 7.5 finished inside one. It was not a
stall: the intermediate sample had `AbortTransfer+2` and `disowned+3` already
in, with `DisableInterrupts`, `StopController` and `unpowered` arriving in the
next one, and the whole thing finished about 60 s into a ten-minute threshold.
It is recorded because it is a difference, not because it is a fault.

What this establishes: on the `200` arm on amd64, single-processor, the fix's
completion path runs as section 7.3 designed it, never forces a drain and
never falls back to the poll, across seven loads including five
disable/enable cycles, a remove and a rescan. **Four NT 5.x legs remain** (98,
ME, 2000, XP32), and none of the five reaches real hardware.

### 7.7 XP x64 raised to four vCPUs, 2026-09-13: a BUGCHECK, and the leg is UNFINISHED

Section 7.6 ran on one vCPU and said so. Raised to `-smp 4` the same evening -
`-accel tcg,thread=multi` to match section 7.5's four guests, from a new
`winxp64-clean-install-smp4` snapshot, one install - **the second live
disable/enable cycle bugchecked.** Seven uniprocessor loads had not touched
this. **Nothing in this section is a pass; the leg is unfinished.**

**The bugcheck.** `D1 DRIVER_IRQL_NOT_LESS_OR_EQUAL`,
`{fffffadf00000000, 2, 0, fffffadfc62201a7}` - a **read**, at **IRQL 2**, of
`fffffadf00000000`. usbport is loaded `fffffadf'c6203000-c623d000`, so the
faulting instruction is **`USBPORT+0x1d1a7`** and `kd` says "Probably caused
by : USBPORT.SYS". The loaded image's timestamp `45D69800` matches
`tools\winxp64-extracted\usbport.sys` exactly, so that file is the build that
crashed and the disassembly below is of the real code.

**What it was doing.** Function `usbport+1d140..1d1ee` walks a doubly-linked
list under usbport's own spinlock: extension from `[rcx+0x40]`, lock at
`+0x460`, list head at `+0x988`, `LIST_ENTRY` at offset `0x78` inside each
element (`lea rax,[rcx-0x78]`), comparing each element against a
caller-supplied object and doing `lock inc dword ptr [rbx+8]` on a match -
the shape of a handle validation and reference. It faulted at
`mov rcx,[rax+0x78]`, loading the **next `Flink`**.

**The observation that matters.** The address it read,
`fffffadf00000000`, carries **usbport's own high 32 bits with a zero low
dword**. Freed pool carries poison and a wild pointer is wild; this is
neither. It is **a 64-bit pointer whose low half was overwritten with zero, or
written 32 bits wide** - which is the M8 failure mode, on the one guest that
has already produced it once.

**It did not reproduce.** Same guest, auto-restart turned off and the dump
raised to a kernel dump, then one cold enable and **five live disable/enable
cycles, all clean**, `ALL PASS` throughout. With auto-restart off a bugcheck
would halt rather than reboot, so those results are trustworthy. **The fault
is timing-dependent, not deterministic**: 0 crashes in 7 uniprocessor loads,
1 in 2 live SMP cycles, 0 in 6 more.

**What is NOT established.** Which module corrupted the link. usbport faulted;
that does not mean usbport wrote it. Whether the fix of section 7.3 introduced
it - the task 21.5 leg of 2026-09-09 passed this sequence on this guest, but
at one vCPU, so it never tested this. And the last load carries an unresolved
`isr 232 / dpc 206` that the amd64 instrument cannot settle (no memory reads,
32-sample print cap), which per this project's standing trap is **not** to be
called a shortfall from the log.

**What the audit has established so far.** The list is **usbport-private**:
its head at `devext+0x988` is referenced from 7 sites in 6 functions, all
inside usbport, and the miniport is handed its own `MiniportExtension`, never
usbport's extension. So this driver cannot reach that list directly, and if it
is the writer the write is collateral - in one of two shapes needing different
instruments. A true **out-of-bounds** write into a neighbouring pool block is
what **Driver Verifier special pool** catches at the instant of the write. A
**wrong-offset write inside** a usbport allocation is what Verifier
**cannot** catch, the write being in bounds - M8's shape exactly - and only
reading the layouts finds it. Both are owed.

**Three instrument defects surfaced with it**, none of which the five clean
guests had exposed, because nothing had gone wrong on them:

- **`watch-threshold.ps1` reported the post-bugcheck REBOOT as a healthy
  re-enable.** It waits for one more boot in the log and cannot tell why the
  boot happened. Every green enable in every leg rests on that assumption.
  Turning off "Automatically restart" removes the ambiguity, which is why the
  re-run did so first.
- **It cannot start from a cold log** - exit 2, "NO DriverEntry line", which
  is exactly the state after a failed enable.
- A one-off launcher copied outside `scripts\local\` **silently loses the QEMU
  trace**, `-trace` being resolved against `%~dp0`. The crash run has no trace
  because of it; the re-run does.

**Continued as [issue 8](08-xp64-smp-usbport-list-head-low-dword.md).** A
second bugcheck on the same guest that evening, with a full kernel dump,
showed this list head damaged again in exactly its low 32 bits while every
element was intact, and it corrects one claim above: `fffffadf` is the high
half of every pool pointer on that guest, not usbport's own. The bugcheck is
tracked there from now on, as a clause of its own; this leg stays unfinished.
Issue 8 section 4c (2026-09-14) found the cause in this fix's territory: on
XP x64 the delivery gate was off, on the premise that the Version 200 service
synchronises itself, and it unlinks from the endpoint list without the
endpoint lock; the XP x64 tier now delivers per endpoint. **This leg has to be
taken again on that build.** It was, on 2026-09-14, and passed: section 7.8.

### 7.8 The XP x64 four-vCPU leg retaken on issue 8's fix, 2026-09-14: PASSED

Section 7.7's leg, taken again on the committed build that carries issue 8's
per-endpoint delivery (commit `20af60b`): the amd64 `qemu` flavour rebuilt from
the clean tree at `d1b4e71`, SHA-256 `0BD32770...8502F13E`, stamp
`DriverEntry (built Sep 14 2026 20:24:13)`. Same guest, same launcher (`-smp 4
-accel tcg,thread=multi`), started from snapshot `winxp64-smp4-issue8diag` with
the new binary copied over `System32\drivers\xhci98.sys` and one restart. The
three devices were hot-plugged from the monitor, and issue 8 section 4b's
gdbstub watch was armed on `devext+0x988` on every load it could reach. The
owner drove Device Manager; every stage was read before the next click.

| stage | reading |
|---|---|
| settled read | `read-v300.ps1 -Expect nt5` ALL PASS; 3 slots / 3 addressed / 3 reopened / iso 2 |
| disables 1-5 | all five applied live, no restart prompt; `DisableInterrupts`, `StopController` and `ports unpowered=4` inside the first read each |
| enables 1-5 | 5 of 5 back with 3 / 3 / 3 / iso 2, ALL PASS on each, `isr count` = `dpc count` (46 or 47) |
| remove, three devices attached | teardown complete inside the first read |
| rescan | reinstalled on the fixed build (see below), ALL PASS, 3 / 3 / 3 / iso 2, `isr` 47 = `dpc` 47; tree clean - the controller, root hub, composite and mass-storage devices, the HID mouse and the USB Audio device, no bangs |

**The counters.** `completions delivered per endpoint only` reads 1 on every
fixed-build load, and with it `completions delivered only under usbport's
lock` reads 1: that tier sets the gate now, so section 7.6's expected 0 no
longer applies to amd64 (`read-v300.ps1`'s verdict does not test that line,
which is why it still reads ALL PASS). **`completions delivered forced` and
`completion fallback polls` read 0 on every load**, exact by section 7.5's
reasoning. `ResetController` 0 throughout.

**The watch.** 17 records over 9 watched loads, **none with a zeroed low
half.** Every one is a shape issue 8 section 4c already names: the teardown
unlink on each disable and the remove (head going self-pointing, a full 64-bit
value); a freed extension reused at the same address, zeroed as a whole qword,
re-initialized and inserted into; one whole-qword zero with a foreign `Blink`
as the extension was freed at a guest restart; and one stop at QEMU's own
`quit` that was not a watch hit.

**The rescan first ran the wrong binary, and was taken again.** An uninstall
followed by a rescan reinstalls through the INF from the transfer drive's
root, not from the copy placed in `System32\drivers`, and that root still held
this section's predecessor, issue 7's build `249BADFE` without issue 8's fix.
The first rescan's load printed `DriverEntry (built Sep 13 2026 00:44:14)` and
no `per endpoint only` line, so it is **not** a reading of the fixed build and
is not counted above (it also printed `isr 167 / dpc 154` past the print cap,
which per this project's standing trap is not called a shortfall, and belongs
to the old build either way). The fixed binary was copied back and the guest
restarted (ALL PASS, 3 / 3 / 3 / iso 2); the guest was shut down, the
transfer root restaged with the fixed build while QEMU was stopped, and the
guest relaunched without a revert. That boot's remove ran with **no devices
attached and no watch armed** - the click came before the hot-plug - and the
devices were then attached to the driverless controller before the rescan,
whose load is the row above. The remove with all three devices attached is the
part-one row, on the fixed build.

What this establishes: on the `200` arm on amd64 at four vCPUs, the build
carrying issue 7's and issue 8's fixes passed every section 7.4 clause - the
settled read, five live disable/enable cycles, remove and rescan - with no
bugcheck, no damaged head, no forced drain and no fallback poll. Against the
unfixed tier's four bugchecks in about fifteen loads, that is a second clean
run after issue 8's `i8diag1`, on one guest, in a virtual machine; it is
support, not proof. **Four NT 5.x legs remain** (98, ME, 2000, XP32), and
issue 8 section 4c's note on the 32-bit Version 200 targets still stands.

### 7.9 The XP 32-bit four-vCPU leg, 2026-09-14/15: a BUGCHECK on the first device that was not SMP's, its fix, a livelock, and a PASS under WHPX

The 32-bit XP guest raised to four vCPUs for the same leg section 7.8 took on
XP x64. `vm\winxp.img` at snapshot `winxp-clean-install-smp4`: a clean XP
Professional SP3, on the `ACPI Multiprocessor PC` HAL (XP switched to it by
itself on the first `-smp` boot), four `Intel Core 2 Duo T7700` cores in one
socket, kernel memory dump, automatic restart off. The launcher
`vm\fix-issue7-xp-smp4\winxp-smp4.cmd` is the generated XP launcher plus
`-accel tcg,thread=multi`, `-cpu core2duo` and `-smp 4,sockets=1,cores=4`
(XP Professional licenses two sockets, so the four are cores; `pentium3` has
no multi-core CPUID). The build was the x86 `qemu` flavour of `363d52e`,
SHA-256 `F1BC64F5...`, stamp `DriverEntry (built Sep 14 2026 22:34:18)`, and
its install read ALL PASS with the stamp matching the transfer root.

**Two runs, one bugcheck, identical each time.** Run 1 (`i7xp32smp4`) had the
three devices hot-plugged together and bugchecked within a minute:
`STOP 0x000000FC (0xF8AF98E8, 0x02B20963, 0xF8AF9848, 0x00000001)`,
ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY at a kernel-stack address. Run 2
(`i7xp32smp4r2`), off a fresh revert, the mouse alone:
`0xFC (0xF8B058E8, 0x02B2B963, 0xF8B05848, 0x00000001)` - the same stack
offsets in a different stack, the same trap-frame registers, the same last
lines in the debug log. The log ends on the first device's EP0 close after
SET_ADDRESS every time: `SET_ADDRESS answered to usbport` (address 1), `cb
SetEndpointState ... c=00000004`, `cb CloseEndpoint`, its `probe.ep
03000101` line, and nothing after. One device is enough and the point never
moves, so this is not a race.

**The dump, read with `kd -z` and the public `usbport.pdb`.** The thread is
usbhub's device-setup worker at PASSIVE_LEVEL, inside
`USBPORT_InitializeDevice` -> `USBPORT_PokeEndpoint` -> `MP_CloseEndpoint`,
usbport's wrapper for the packet's `CloseEndpoint` slot. That wrapper pushes
**three** arguments - `cmp dword ptr [edi+114h],0` / `sete cl` / `push ecx`
(ReactOS's `IsDoDisablePeriodic`, exactly as declared), the endpoint
extension, the miniport extension - and ends `pop edi / pop esi / pop ebx /
pop ebp / ret 8` with no `mov esp,ebp`. This driver's callee had cleaned
eight bytes since commit `8a46d1b` (2026-09-12, "CloseEndpoint takes two
parameters, because that is what NT 6.x pushes"), so the third argument was
still on the stack under every pop, and the trap frame says so slot for
slot:

| register at the trap | value | what the shifted pop took |
|---|---|---|
| `edi` | `8210d301` | the leftover third argument: `ecx` with `cl` set to 1 |
| `esi` | `8210d0e0` | the wrapper's saved `edi`, usbport's device extension |
| `ebx` | `81f638f8` | the wrapper's saved `esi`, the endpoint |
| `ebp` | `80546abc` | the wrapper's saved `ebx`, `USBPORT_PokeEndpoint`'s `InterlockedDecrement` import |
| `eip` | `f8b058e8` | the wrapper's saved `ebp` - `USBPORT_PokeEndpoint`'s frame, a stack address |

`-cpu core2duo` carries the NX bit, so XP loaded its PAE kernel
(`ntkrpamp.exe`) and refused the execute with a legible `0xFC`; on the
generated launcher's `pentium3` the same defect would have executed stack
bytes. **Nothing here is SMP.** The 32-bit XP guest had last run on
2026-09-07, on the three-parameter callee that happened to match; this was
its first run on the two-parameter build, which would fail the same way on
one vCPU (an inference, not run). Section 7.8's "four NT 5.x legs remain" is
therefore still exactly the count: this leg is not taken by these two runs.

**The scope, read statically** (`legal-provenance.md` section 4, the XP SP3
`MP_CloseEndpoint` row). XP SP3 x86 has exactly one call through the slot and
it pushes three. Vista x86 and Windows 7 x86 push two (the 2026-09-11
reading, still right). Windows 2000 SP4, NUSB and SweetLow's XP-derived
5.1.2600.2180 - the Windows ME target's stack - have no call through the slot
at all, so the two shipping targets and ME never reached either shape.
amd64 is caller-cleaned, so XP x64 saw nothing either way.

**The fix.** One x86 stdcall callee cannot serve two callers that push
different counts, so there are now two: the packet is filled with the
two-parameter `xhciCloseEndpoint`, and `DriverEntry` stores a cast of the
three-parameter `xhciCloseEndpointNt5` on the NT 5.x arm it already selects
with `IoIsWdmVersionAvailable(6, 0)` - the same answer that picks the
registration arity, the resource mask and the interface version. The third
parameter is declared for the stack contract and not read. Both share one
body; `kd` on the new x86 `qemu` build shows `xhciCloseEndpointNt5` ending
`ret 0Ch` and `xhciCloseEndpoint` `ret 8`. `src\xhci_usbport.h` above
`PHCI_CLOSE_ENDPOINT` carries both readings, and the ABI document's
`CloseEndpoint` row no longer says NT 5.x never calls the slot - that
sentence was a census of the two shipping builds and had been generalised.
`lessons.md` has the rule that came out of it.

**The re-run on the fixed build (`i7xp32smp4r3`, 2026-09-15 00:00), and the
leg is UNFINISHED.** Off a fresh revert, the x86 `qemu` build of the fix,
SHA-256 `0BA0133C...B46B00`, stamp `DriverEntry (built Sep 14 2026
23:54:46)`, staged at the transfer root. The mouse alone first, the sequence
that had crashed run 2: `cb CloseEndpoint` came and went, and the device was
addressed, reopened and configured. Then the audio and mass-storage devices:

| stage | reading |
|---|---|
| settled read | `read-v300.ps1 -Expect nt5` ALL PASS; 3 slots / 5 addressed / 5 reopened / iso 2; `isr` 69 = `dpc` 69; `completions delivered only under usbport's lock` 0, which is what the x86 tier must read; `forced` 0, `fallback polls` 0; `cb CloseEndpoint` 4 |

The 5 against XP x64's 3 is XP re-enumerating two of the three devices as
their class drivers installed (addresses 1, 2, 3, then 2 again and 4, on the
same three slots): usbhub's port reset on a restarted PDO, not a driver event.

**Then, before the first disable, the guest froze** - while XP was still
installing the USB Audio class driver, about two minutes after the settled
read, with the debug log's last lines `short packets` climbing and
`transfers submitted 7D / completed 7B`. QEMU reported `running`; the screen
and the log stopped. Read live over the monitor (`vm\issue7-xp32-smp4\r3\
livelock-readings.md` has every value): CPUs 2 and 3 idle in `intelppm`;
CPU0 in `USBPORT_IsrDpc -> IsrDpcWorker -> DpcWorker -> CoreEndpointWorker`
spinning in HAL's `KfAcquireSpinLock` on the lock of usbport endpoint
`81eede40` (our slot 3, DCI 3: the mass-storage device's bulk IN); CPU1 in
`USBPORT_TransferFlushDpc -> FlushDoneTransferList ->
EndpointHasQueuedTransfers`, holding that lock and walking the endpoint's
**active** transfer list without end, 40 of 40 samples inside the walk at one
stack depth. The endpoint's active and pending lists read empty, and the
FDO's done list held one transfer, `8205d260`, whose list entry linked to the
**done** head. That is the walker's cycle: it holds a transfer that was moved
from the active list to the done list after the walker had taken its entry,
and from the done head it steps to a phantom transfer inside the FDO
extension whose `+0x38` is the done head again. The mover therefore ran
without the endpoint lock the walker holds.

**That is issue 8's mechanism, on x86** - issue 8 section 4c's mover,
`Core_iCompleteTransfer` taking a transfer active -> done under the done-list
lock only, reached from a `UsbPortCompleteTransfer` this driver delivered
outside `PollEndpoint` and its endpoint lock. On amd64 the same walk *wrote*
the done head's low dword and bugchecked `D1`; on x86 this walk only reads
and logs, so it spins with the endpoint lock held and CPU0 starves on it: a
livelock at DISPATCH on two CPUs, no bugcheck, no dump. Issue 8's fix, delivery
per endpoint from usbport's own callback, was compiled under `_WIN64` only,
exactly as section 7.8 and issue 8 section 4c left it, so this run is the
first reading of the 32-bit Version 200 tier's exposure and it says the tier
is exposed. *(The guard was lifted later the same night, after issue 8
section 4d read the other 32-bit builds as sharing the mover; the leg's next
run is on that build.)* 512 MB of guest physical memory was saved while frozen
(`guest-phys-512m.bin`); the guest was then reset and shut down cleanly.

What this establishes: the `CloseEndpoint` fix holds on XP SP3 x86 (the crash
point passed twice over, with one device and with three), and the XP 32-bit
four-vCPU leg is **not taken** - it stopped before its first disable on a
finding of its own. Whether the x86 Version 200 tier takes issue 8's fix is
the owner's decision, which issue 8 section 4c already names; until then the
leg cannot be run to its clauses. Windows 98, ME and 2000 at four vCPUs
remain unrun, and this reading says they share the exposure only if their
`usbport` moves the transfer the same way, which is not read. *(Both were
answered the same night: issue 8 section 4d read all three as moving it the
same way, and the owner took the fix on the whole 32-bit tier. The leg was
then taken on that build, below.)*

**The leg on the per-endpoint build, 2026-09-15: three runs, and it PASSED
under WHPX.** All three off a fresh revert of `winxp-clean-install-smp4`, on
the x86 `qemu` build carrying `413581c` (the `_WIN64` guard lifted), SHA-256
`B410BA07...5CAF47EE`, stamp `DriverEntry (built Sep 15 2026 08:30:44)`,
staged at the transfer root. Every load read `completions delivered per
endpoint only` 1 and `completions delivered only under usbport's lock` 1 -
the x86 tier's expected values from this build on, replacing the 0 the r3
row above carries - and the stamp matched the root. r1 to r4 ran on the
owner's other host; r5 and r6 on `fw-w11p-ykm` (i5-1240P). Every reading
below is **runtime**, from this driver's own log and counters.

| run | host, accelerator | reading |
|---|---|---|
| r4 (`i7xp32smp4r4`) | other host, `-accel tcg,thread=multi` (the section's launcher, unchanged) | settled read ALL PASS, 3 slots / 5 addressed / iso 2 / `forced` 0 / `fallback polls` 0; alive more than six minutes past where r3 livelocked, CPU samples in idle and HAL, never usbport. Cycles 1 and 2 clean. **Cycle 3's disable hung in PnP**: the log stops after usbhub's port-3 and port-2 disables, before the mouse endpoint's Stop Endpoint and aborts that cycle 2 showed at the same point; three CPUs halted in the idle driver, the guest clock running, IRQ 5 static, `USBSTS` 0; live counters read twice 20 s apart moved only on `CheckController`, health polls and frame samples - no `PollEndpoint`, no invalidate, no command, `transfers submitted` = `completed` |
| r5 (`i7xp32smp4r5`) | `fw-w11p-ykm`, `-accel whpx,kernel-irqchip=off`, otherwise r4's command line | settled read ALL PASS, 3 slots / 5 addressed / iso 2 / 0 / 0; **five disable/enable cycles, cycle 3 included**, every disable the full sequence (ports 3 and 2, Stop Endpoint on the mouse's DCI 3, two `AbortTransfer`, port 1, `DisableInterrupts`, `StopController`, 4 ports unpowered, halted), every enable ALL PASS with 3 slots / 3 addressed / iso 2 / `forced` 0 / `fallback polls` 0; **remove and rescan** clean, the reload's stamp and the binary's hash unchanged, ALL PASS, tree back; `system_powerdown` clean |
| r6 (`i7xp32smp4r6`) | `fw-w11p-ykm`, `-accel tcg,thread=multi` (r4's launcher) | laggy from boot; load and first plug ALL PASS, all three devices on the bus; then **Device Manager stalled at least nine minutes** on the "Disk drive" install during the settle, before any cycle, while the driver's counters kept moving (transfers, interrupts, DPCs, `PollEndpoint`, health polls; commands 17/17; `forced` 0) and the QEMU trace showed the mass-storage device's CBW/CSW polling pair about twice a second. Stopped. Physical memory saved, not analysed |

**The owner's decision, 2026-09-15**: multi-core 32-bit XP under TCG does not
work well enough to be a representative system, so this guest's SMP legs run
under WHPX, and r4's and r6's PnP stalls are **not driver evidence** - they
are recorded here, not chased. That does not withdraw what TCG found on this
guest: r1/r2's `0xFC` and r3's livelock were real defects, read to their
cause and fixed. The decision is about this guest; the other guests' four-vCPU
legs (sections 7.7 and 7.8 on XP x64) ran under TCG and stand as they are.

What this establishes, and no more: on XP SP3 x86 at four vCPUs, the build
carrying issue 7's fix, the `CloseEndpoint` fix and issue 8's per-endpoint
delivery passed every section 7.4 clause **once, under WHPX, on one host** -
the settled read, five live disable/enable cycles, remove and rescan - with
no bugcheck, no livelock, no forced drain and no fallback poll. r5 changed the
host **and** the accelerator against r4, and r6 held the host and put TCG
back and stalled earlier, in a different place, so the three do not isolate
which change removed r4's cycle-3 hang. What r4 read is consistent with a wait
above the miniport - the driver held no outstanding transfer by its own
counters and usbport was issuing it nothing - but whether a completion the
per-endpoint mode had parked was never handed back is not established: no
counter publishes the completions parked at a given moment. **Three legs
remain, and none of them is SMP**: Windows 98 under NUSB, ME under SweetLow's
stack and Windows 2000, each on one core under TCG, by the owner's decision of
2026-09-15. Windows 98 and ME are uniprocessor by construction, so a
four-vCPU leg would not be one there, and the owner did not take a
two-processor Windows 2000 leg (its Professional edition's ceiling). These
legs are regression readings of the per-endpoint delivery on those stacks,
not tests of issue 8's race. They were taken the same night: section 7.10.

### 7.10 Windows 98, ME and 2000 on one core, 2026-09-15: the per-endpoint build on the older stacks, PASSED with two recorded departures

Section 7.9's build (`B410BA07`, the x86 `qemu` flavour of `413581c`), from
`vm\xferxp`, on each guest's ordinary machine under TCG with one CPU, host
`minis-w11p-ykm`. Each guest ran from a local qcow2 converted out of a
snapshot, so no image under `vm\` was written: Windows 98 SE from
`win98.img` at `post-nusb` (NUSB 3.3, no driver), Windows 2000 SP4 from
`win2k-xonly.img` at `win2k-xonly-clean-install` (no driver), and Windows ME
from `winme.img` at `winme-sweetlow-driver` - SweetLow's stack with the
2026-09-02 build already on it, since no snapshot has the stack without a
driver. The owner drove Device Manager; the three devices were hot-plugged
from the monitor as before (mouse, then audio and mass storage). Every reading
is **runtime**, from this driver's log and its counters read live.

| guest | reading |
|---|---|
| Windows 98 SE, NUSB | install from the transfer root and a shutdown; the load read stamp `Sep 15 2026 08:30:44`, interface `0xC8`, `completions delivered per endpoint only` 1, `only under usbport's lock` 1. All three devices bound (an install stall of about a minute on the USB disk cleared by itself, the guest idle). Settled read ALL PASS, 3 slots / 3 addressed / 3 reopened / iso 2, `isr` 50 = `dpc` 50. **No disable, enable, remove or rescan**: on Windows 98 under NUSB any controller stop bugchecks inside NUSB's `usbport.sys` (release notes, known limitations). Taken instead: the mass-storage device and the mouse unplugged and replugged, both back and working; live, 5,824 transfers submitted / 5,820 completed, `forced` 0, `fallback polls` 0. Shutdown: `SuspendController`, `DisableInterrupts`, `StopController(TRUE)` |
| Windows ME, SweetLow | the new build on a cold boot, per-endpoint 1 and lock-only 1; three devices bound; settled read ALL PASS, 3 / 3 / 3 / iso 2; **five live disable/enable cycles**, every disable ending in `StopController(TRUE)`, 4 ports unpowered, halted, and every enable reading 3 slots / 3 addressed / 3 reopened with commands issued = completed, `forced` 0, `fallback polls` 0, `ResetController` 0 and both mode flags 1 read from memory; **remove** clean, **rescan** a fresh image load at the same stamp, ALL PASS, 3 / 3 / 3 / iso 2, tree complete |
| Windows 2000 SP4 | install through Have Disk with no restart and no prompt; per-endpoint 1 and lock-only 1; three devices bound; settled read ALL PASS, 3 / 3 / 3 / iso 2. **A live disable with the USB audio device attached was refused by Windows, twice** (see below). With audio unplugged before each disable and replugged after each enable: disables 2 to 5 applied live, each ending in `StopController(TRUE)`, 4 ports unpowered, halted; every enable a fresh image load reading 2 slots / 2 / 2 for mouse and disk, commands issued = completed, `forced` 0, `fallback polls` 0, both mode flags 1, and 3 slots with commands issued = completed once audio was back. **Uninstall** clean with no restart; **rescan** a fresh load at the same stamp, ALL PASS, `forced` 0, `fallback polls` 0, then audio back and the tree complete |

**The Windows 2000 refusal.** With all three devices attached, Device
Manager's disable asked for a restart, twice. On both attempts the driver saw
only the mouse endpoint's Stop Endpoint and two `AbortTransfer` - no port
disable, no `DisableInterrupts`, no `StopController` - and the tree stayed;
the mouse went on working, and `forced` and `fallback polls` stayed 0. The
first was applied through the owner's restart (the controller booted
disabled, and enable 1 was a clean fresh load: ALL PASS, 3 / 3 / 3 / iso 2).
Then the audio device alone was unplugged and the disable retried, and the
controller stopped live, its children leaving the tree; every later disable
with audio unplugged applied live. So the refusal needs the audio device
attached. **What held the stack was not read**, and whether it is Windows
2000's audio stack holding the device open or something this driver answers
is **not established**. No earlier Windows 2000 record here had an audio
device attached for a disable, so this is the first reading of the
combination, not a regression against one.

**It is not the per-endpoint change** (2026-09-16, `vm\i8tier-1cpu\win2k-base-r1\`).
The x86 `qemu` build of `bc16c6e` - the commit before `413581c`, identical
but for the guard, so both mode flags read 0 - was installed on a fresh copy
of the same clean image and given the same three devices. Its first disable
applied live, but it came a minute after the audio device was plugged and
before the USB Audio Device was shown bound; after an enable, with USB Audio
Device visibly bound, the next disable **asked for a restart**, and this
driver saw nothing of it at all - no endpoint stop, no abort, no port
disable. So the refusal needs a bound USB Audio Device and reproduces without
the per-endpoint delivery. What holds the stack is still not read.

**The Windows ME departures, all procedural.** Update Driver over the old
build stopped the controller and loaded the image again with no restart
prompt, but from the old file (its `DriverEntry` still read 2026-09-02); the new file was copied into
`SYSTEM32\DRIVERS` from an MS-DOS prompt. A disable and enable then restarted
the same loaded image (Windows ME does not unload it), and a monitor
`system_powerdown` **hibernated** the guest, whose relaunch resumed the old
build. Only a Start-menu shutdown and a cold boot loaded the new one, and the
leg above is that boot.

What this establishes: the per-endpoint delivery, on one CPU under TCG, runs
on NUSB's Windows 98 stack, SweetLow's stack on Windows ME and Windows 2000
SP4's own through every clause each target can take, with no forced drain, no
fallback poll and no `ResetController` in any read. It is one run per
guest, in virtual machines, and says nothing about SMP. Two things remain
open: Windows 98's cycles, which that stack cannot take at all, and Windows
2000's refusal with audio attached.

## Sources

Evidence, all under `vm\`:

- `vm\issue7-xp32-smp4\` - the 2026-09-14/15 XP 32-bit four-vCPU leg (section
  7.9): run 1's `bsod.png`, `MEMORY.DMP`, `Mini091426-01.dmp`, the debug logs
  and `kd-analyze.log` / `kd-dis.log` / `kd-stack.log`; `r2\` (the mouse-only
  repeat: `r2-mouse.png`, its `MEMORY.DMP`, `kd.log`, `kd2.log`, `kd3.log`,
  `kd4-symbols.log` with the `MP_CloseEndpoint` naming, `kd6-sweetlow.log`,
  `kd7-newbuild.log` showing the two callees' `ret`); `r3\` (the fixed-build
  run: `debugcon-settled.log`, `settled.png`, `hang.png`,
  `debugcon-frozen.log`, `livelock-readings.md`, `guest-phys-512m.bin`,
  `kd1-loop.log`, `kd2-callers.log`); `r4\hang-readings.md` (with
  `guest-phys-512m.bin`, the hang screenshots, register and counter reads and
  the build's `offsets.txt`); `r5\readings.md` (with `debugcon-full.log`, the
  counter reads, screenshots and `winxp-smp4-whpx.cmd`, the WHPX launcher);
  `r6\readings.md` (with `debugcon-full.log`, `qemu-trace.log`, the counter
  reads, screenshots and `guest-phys-512m-settle-stall.bin`); `static-tier\`
  (issue 8 section 4d). Launcher `vm\fix-issue7-xp-smp4\winxp-smp4.cmd`;
  snapshot `winxp-clean-install-smp4` on `vm\winxp.img`
- `vm\i8tier-1cpu\` - the 2026-09-15 single-core legs (section 7.10): the
  three launchers, and `win98-r1\`, `winme-r1\`, `win2k-r1\`, each with its
  `readings.md`, full debug log, QEMU trace, `read-v300` verdicts, live
  counter reads per stage and screenshots; `winme-r1\` also keeps the two
  old-build logs. The work images are under
  `C:\Users\yeokm1\xhci98-work\i8tier\` on `minis-w11p-ykm`
- `vm\issue8-kd\i7smp4b\` - the 2026-09-14 retake (section 7.8): `part1\`
  (launcher tag `i7smp4b`: the debug log of the swap boot, the five cycles,
  the remove, the wrong-binary rescan and the restart; `gdb-load1..8-*.log`;
  `boot-01.png`, `rescan-01.png`) and `part2\` (tag `i7smp4b2`: the remove and
  the fixed-build rescan's debug log, `gdb-load1-*.log`, `rescan-tree.png`),
  and `step.ps1`, the per-stage reader. QEMU traces
  `vm\winxp64-qemu-trace.i7smp4b.log` and `...i7smp4b2.log`

- `vm\fix-issue7-xp64-smp4\` - the 2026-09-13 `-smp 4` leg (section 7.7):
  `Mini091326-01.dmp` (the crash's minidump, lifted out of the qcow2 with
  7-Zip after shutdown), `post-bsod-state.png` (the error-reporting dialog,
  which is where the bugcheck arguments came from - the BSOD itself was lost
  to auto-restart), the probe screenshots showing
  `ACPI Multiprocessor x64-based PC` and four CPU graphs, both runs' debug
  logs, the re-run's QEMU trace, every watcher's samples, and
  `winxp64-smp4.cmd` (the generated launcher plus `-smp 4` and
  `-accel tcg,thread=multi`). Snapshots `winxp64-clean-install-smp4` and
  `winxp64-smp4-installed-kerneldump` (driver installed, kernel dumps armed,
  auto-restart off) are on the image; the 2026-09-08 `winxp64-clean-install`
  is untouched

- `vm\fix-issue7-xp64\` - the 2026-09-13 Windows XP x64 leg (section 7.6):
  the full debug log and QEMU trace, `read-01.txt` and `counters-01..07.txt`,
  every `disable1-5` / `enable1-5` / `remove` / `rescan` watcher's samples,
  and `rescan-tree.png`. Taken over monitor port **55700**, not the launcher's
  generated 55562, which the host had excluded that day
- `vm\fix-issue7-win7-x64\`, `vm\fix-issue7-vista-x64\`,
  `vm\fix-issue7-win7\` and `vm\fix-issue7-vista\` - the 2026-09-13 re-run
  (section 7.5): each guest's full debug log and QEMU trace, the
  `read-v300.ps1` reading per stage, every disable/enable/remove/rescan
  watcher's samples, the x86 guests' counter reads from memory, and the
  post-rescan Device Manager screenshots

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
- Section 7's tools, all git-ignored under `scripts\local\`:
  `usbport-disasm.py` (capstone over any of the three usbport builds with
  symbol-annotated calls and cross-references), `usbport-vista-tags.py`
  (Vista x64 builds its ring tags with byte stores, so no literal exists in
  the file), `pdbid.py`, `usbport-syms\` (dbh enumerations, unioned over
  many patterns because `enum *` silently drops symbols), and the tag
  tables `usbport-ring-tags-win7-x64.txt` / `usbport-ring-tags-vista-x64.txt`.

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
