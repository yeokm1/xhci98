# Issue 7 - An enable on Windows 7 x86 intermittently loses one device, and the completion it is waiting for is dropped inside usbport's own DPC state machine

Status: **cause read and fixed in source on 2026-09-13, and the fix re-run
the same day on all four NT 6.x guests, every clause passing on each**
(section 7.5, including one Windows 7 x86 disable that Windows itself
refused and that is stated there, not smoothed over). **The NT 5.x legs
are still owed**, because the fix changes the delivery path there too.
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

## Sources

Evidence, all under `vm\`:

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
