# Issue 8 - Windows XP x64 at four vCPUs rarely bugchecks in usbport, on a list head whose low 32 bits were overwritten

Status: **fixed in this driver on 2026-09-14 (section 4c, commit `20af60b`),
and issue 7 section 7.8's XP x64 four-vCPU leg passed on the committed build
the same day** - two clean runs, not proof. **The same mechanism was then
read on Windows XP SP3 x86 at four vCPUs on 2026-09-15, where the fix did
not yet reach (it was compiled under `_WIN64` only): usbport's active-list
walker holding a transfer that had been moved to the done list under it,
spinning for ever with the endpoint lock held - a livelock rather than a
bugcheck, because the x86 walk only reads** (issue 7 section 7.9,
`vm\issue7-xp32-smp4\r3\livelock-readings.md`). Section 4d then read the
other three 32-bit builds as sharing the mover, and **on the owner's
decision of 2026-09-15 the guard was lifted: the 32-bit build sets the mode
on its Version 200 tier too. Its first run - the XP 32-bit four-vCPU leg -
is owed**, and the two shipping targets have only the reading, not a run. The cause is this driver's: on XP
x64 it handed completions to usbport from contexts that did not hold the
transfer's own endpoint lock, and XP x64's completion service needs that lock
(section 4c). The corruption itself is a store by usbport, caught by a
watchpoint (section 4b). Four bugchecks on one guest, 2026-09-13, all `D1`
inside `usbport.sys`, the first three dumps showing a list head in usbport's
device extension whose 64-bit forward pointer had exactly its low 32 bits
replaced. Sections 1 to 4a are the chase as it happened and say "not known"
where it was not yet known; read them with 4b and 4c.

Target affected: Windows XP Professional x64 SP2 (NT 5.2.3790) in QEMU, at
`-smp 4 -accel tcg,thread=multi`, on the amd64 `qemu` build carrying issue 7's
fix (`src\objchk_qemu\amd64\xhci98.sys`, SHA-256 `249BADFE...F3F873`). Not
seen at one vCPU (seven loads). Not looked for on any other guest at four
vCPUs beyond issue 7 section 7.5's four NT 6.x legs, which did not show it.
Never on real hardware.

It was found while taking issue 7's XP x64 leg at four vCPUs and is recorded
there too (issue 7 section 7.7). It is a separate page because it is a
different clause: issue 7 is a lost wakeup that leaves a device unenumerated;
this is memory corruption that stops the machine. Whether issue 7's fix
introduced it, exposed it, or has nothing to do with it is **not known**:
task 21.5 passed the same sequence on this guest on 2026-09-09, but at one
vCPU, so it never tested this.

The short version: something stores a 32-bit value over the low half of a
64-bit `LIST_ENTRY.Flink` inside usbport's device extension. The rest of the
list stays intact, so the damage sits silently until usbport next walks or
pops that list, and then it faults - minutes or cycles later, in whichever
usbport routine gets there first. That delay is why the two crashes are in
different functions and why the second one struck an idle machine.

Every binary-derived fact below is tagged **static** (read from the shipping
`usbport.sys` with `kd -z`; nothing executed) or **runtime** (read from this
guest's own crash dumps). `tools\winxp64-extracted\usbport.sys` carries
timestamp `45D69800`, which matches the image loaded in both crashes, so the
disassembly is of the code that ran.

---

## 1. The two bugchecks

| | Crash 1 | Crash 2 |
|---|---|---|
| When (guest time) | 2026-09-13 20:28:43 | 2026-09-13 about 21:41, uptime 0:28:10 |
| Run tag | `fix7xp64smp4` | `verifier1` |
| Doing | the second live Device Manager enable | nothing - idle, devices attached, two cycles after boot |
| Code | `D1 {fffffadf00000000, 2, 0, fffffadfc62201a7}` | `D1 {000000000001000c, 2, 1, fffffadfc61ea1c3}` |
| Access | **read** at IRQL 2 | **write** at IRQL 2 |
| Where | `USBPORT+0x1d1a7` | `USBPORT+0x101c3`, CPU 0, in usbport's DPC path (`+0x12485` calls it) |
| Dump | minidump only (auto-restart was on) | **full kernel dump** |

Between them, on the same guest: one cold enable and five live
disable/enable cycles with auto-restart off, **all clean**; then a revert to
`winxp64-smp4-installed-kerneldump`, Driver Verifier on `xhci98.sys`
(section 5), and two more live cycles, **both clean**, before crash 2.

A third, on the first enumeration after a clean boot with no Verifier, is
section 4a.

**Rate:** 0 in 7 uniprocessor loads; at four vCPUs, 2 bugchecks across about
ten live cycles plus idle time, then a third within a minute of a boot. Too rare to chase by repetition at one
operator click per cycle, and the second one did not need a cycle at all.

## 2. Crash 1: a handle-list walk reads a pointer with a zero low half

**static** - `usbport+0x1d140`, bounds from `.pdata`:

```
mov  rdi, [rcx+0x40]      ; usbport device extension
lea  rcx, [rdi+0x460]     ; lock, acquired
lea  r9,  [rdi+0x988]     ; LIST_ENTRY head
mov  rcx, [r9]            ; first Flink
1d1a0: lea rax, [rcx-0x78]      ; element, LIST_ENTRY at +0x78
       cmp rax, rbx             ; the caller's object?
       mov rcx, [rax+0x78]      ; <== fault: next Flink
       ...
1d1bc: lock inc dword ptr [rbx+8]   ; reference on a match
```

A walk under usbport's own lock, validating that a caller's object is on the
list and referencing it - the shape of a device-handle check. The address it
read was `fffffadf'00000000`.

## 3. Crash 2: the same list, in a full dump, damaged the same way

Crash 2 faulted on a **different** list (section 4), but its kernel dump holds
the whole device extension, and **crash 1's list head is damaged in it with
crash 1's exact value** - on a different boot, a different allocation, and
without having faulted yet.

**runtime** - the HCD FDO is `fffffadf'ce2f5050`, its device extension
`[FDO+0x40]` = `fffffadf'ce2f51a0`, so the head is `fffffadf'ce2f5b28`.
Walking it backwards by `Blink`:

| entry | Flink | Blink |
|---|---|---|
| head `ce2f5b28` | **`fffffadf'00000000`** | `cdb7e088` |
| `cdb7e088` | `ce2f5b28` (head) | `cdc09c08` |
| `cdc09c08` | `cdb7e088` | `ce393528` |
| `ce393528` | `cdc09c08` | `ce0385a0` |
| `ce0385a0` | `ce393528` | `ce2f5b28` (head) |

(all addresses `fffffadf'...`.) Four elements - plausibly the root hub and
the three attached devices - and **every link agrees except one**: the
head's `Flink` should be `fffffadf'ce0385a0` and reads `fffffadf'00000000`.
**Exactly the low 32 bits of one 64-bit field were set to zero.** An
ordinary list operation on a damaged element cannot produce that, because no
element is damaged. The empty list heads beside it at `+0x998`, `+0x9B8` and
`+0x9C8` point to themselves and are intact.

**Correction to issue 7 section 7.7:** it called the high half "usbport's own
high 32 bits". `fffffadf` is the high half of every non-paged pool pointer on
this guest, not something particular to usbport; the finding is the zero low
half, not the high one.

## 4. Crash 2's own list, briefly

The owner's steer on 2026-09-13 was to work crash 1 and not to chase crash 2
as a bug of its own. What its dump shows is recorded so it is not lost.

**static** - `usbport+0x100b0`: head at `devext+0x9A8`, lock at
`devext+0x400`, a remove-head (`rdx=[rsi]`, `rcx=[rdx]`, `[rsi]=rcx`,
`[rcx+8]=rsi` - the fault), element `LIST_ENTRY` at `+0x48`, a pointer at
`+0xA8` handed on with the device extension, and bit 8 of `[+4]` choosing
between `usbport+0x6570` and `usbport+0xf1f0`. The shape suggests a transfer
done list; that is an inference.

**runtime** - from the trap frame, the head's `Flink` was
`fffffadf'ce4280f8`, whose first quadword is `0x0000000000010004`; usbport
stored that into the head and then faulted writing `0x1000C`. The same
`fffffadf'ce4280..` page holds a regular array of `0x30`-byte records, each
with a small ordinal (`0x10001`, `0x10002`, ...) at `+8`, and the quadword at
`ce428100` is the head's own address. So this head's `Flink` too was wrong in
its **low half only**, high half intact - eight below a record that links
back to the head. The rest of that list was not reconciled.

**Two heads 0x20 apart in the same device extension, both damaged in the
low 32 bits of `Flink` only.**

## 4a. Crash 3: the same head, 57 seconds after a clean boot

Taken 2026-09-13 about 22:15 guest time, tag `fix7xp64smp4leg`, while
resuming issue 7 section 7.7's leg. The image had been reverted to
`winxp64-smp4-installed-kerneldump` (**no Driver Verifier**), booted at four
vCPUs, and the mouse, audio and storage devices hot-plugged from the
monitor. **No Device Manager action had been taken.** The driver's log had
reached `slots enabled=00000001` and was answering usbport's first control
transfers on the second device when the guest stopped.

**runtime** - `D1 {0000009b'00000040, 2, 0, fffffadf'c7969d95}`, a read at
IRQL 2, `USBPORT+0xbd95` (loaded image timestamp `45d69800` again), system
uptime 0:00:57, full kernel dump. The stack is usbhub's enumeration calling
into usbport (`usbhub+0x3c1e` ... `USBPORT+0x3948` ... `+0x127a4`,
`+0x10e17`, `+0xdc4a`, `+0xae9e`, `+0xa698`, `+0xbd95`).

**runtime** - FDO `fffffadf'ce680050`, device extension
`[FDO+0x40]` = `fffffadf'ce6801a0`. **The head at `devext+0x988` reads
`Flink` = `fffffadf'00000000`**, `Blink` = `fffffadf'ce1df588`, whose own
`Flink` points back at the head. The element that `Blink` belongs to starts
with the ASCII tag `DevH` at `-0x78`, which is consistent with section 2's
reading of this as a device-handle list. **Three boots, three dumps or
minidumps, the same head, the same value.** The empty heads at `+0x998`,
`+0x9B8` and `+0x9C8` again point to themselves.

**static and runtime** - the fault itself is on section 4's list. The walk at
`usbport+0xa3e5` follows `[element+0x48]`, stops when it equals a saved end
pointer (`r15`, which the trap frame does not preserve), and hands
`CONTAINING_RECORD(entry, +0x48)` to `usbport+0xbd60`, which removes that
entry and loads `[[rcx+0xA8]+0x18]+0x40`. It was handed
`rcx` = `devext+0x960`, which is the head `devext+0x9A8` taken as an element.
`[devext+0x960+0xA8]` is `devext+0xA08`, an empty self-pointing head, and
`+0x18` beyond that holds a small count (`0000009f'00000000` in the dump), so
`r9` = `0000009b'00000000` was that field's value at the fault and the load
missed. The one real element on the `+0x9A8` list, `fffffadf'ce5834c8`,
reads `Flink` = `Blink` = itself in the dump, which is what `+0xbd60`'s
remove writes when it unlinks the head from a one-element list, so this
list's state before the fault cannot be read back from it. **Why the walk
reached the head without meeting its end pointer is not established**; the
`+0x9A8` head's `Flink` low half being wrong, as in crash 2, would do it, but
the dump no longer shows the value it had.

What crash 3 changes:

- **It needs no disable/enable cycle.** The damage at `+0x988` is present
  within a minute of a cold boot, before the third device was addressed. So
  a write breakpoint armed from boot (section 6, item 1) is expected to fire
  in the first minute, and the per-enable re-arming is not needed to catch
  it once.
- **It happened without Driver Verifier**, so crash 2's Verifier
  configuration is not a factor.
- Rate on this guest at four vCPUs is now 3 bugchecks: one on a live enable,
  one idle, one on the first enumeration after boot.

## 4b. Crash 4: the write caught, and the writer is usbport

Taken 2026-09-13 about 23:31 guest time, tag `i8gdb1`, from
`winxp64-smp4-installed-kerneldump` (no Verifier, no `/debug`), at four
vCPUs, the three devices hot-plugged, on the **second** Device Manager enable
of the boot, about two seconds after `StartController`.

**The instrument.** A kernel debugger's data breakpoint was tried first and
was not trusted: with kd on a named-pipe COM1, `ba w4` on `devext+0x988`
stayed silent while the head was demonstrably written (it read self-pointing
when armed and held the root hub's handle when next read), although a `ba w4`
on this driver's own interrupt counter did fire. The kd link also froze the
guest when combined with the second instrument, so it was dropped. What
caught the write is **QEMU's gdbstub**, started from the monitor
(`gdbserver`), driven by a small remote-protocol client that sets a
`Z2` write watchpoint of four bytes on the head, records `rip`, the general
registers, the head, the bytes around `rip` and the stack on every hit, and
continues. A TCG watchpoint is enforced by the emulator on every vCPU and
does not depend on the guest's debug registers. Each load's head was
computed from this driver's own log, `cb StartController a=` minus `0x2A0`,
and the watch was moved to it within about a second of that line. It was
proved live on the busy `+0x9A8` list first (76 legitimate hits in about 20
seconds) and on `+0x988` by a legitimate teardown unlink in the first
disable.

**runtime** - the hit, with usbport loaded at `fffffadf'c6294000` (from the
bugcheck screen):

```
stop     watch:fffffadfcdc36b28  (devext+0x988)
rip      fffffadf'c629fd8e = usbport+0xbd8e
rbx      fffffadf'cdc361a0       usbport device extension
rcx=rsi=r11  fffffadf'cdc36b00   devext+0x960
rdx      0
r12      fffffadf'cdfb2010       endpoint (tag "hcEP")
r15      fffffadf'cdfb2070       endpoint+0x60
head     fffffadf'00000000  fffffadf'cdf694d8
```

**static** - the instruction that ends at `usbport+0xbd8e`, in the routine at
`usbport+0xbd60` already read for crash 3 (section 4a):

```
usbport+0xbd8b:  mov dword ptr [rcx+28h], edx
```

The routine takes a transfer (`LIST_ENTRY` at `+0x48`, endpoint at `+0xA8`)
and stores a 32-bit field at `+0x28`. It was handed `rcx` = `devext+0x960`,
which is the `+0x9A8` list head taken as an element, so its `+0x28` is
`devext+0x988` and **the `dword` zero lands on the low half of the
device-handle list's `Flink`.** The guest bugchecked seconds later at
`usbport+0xbd95` - `D1 {000002ce'00000040, 2, 0, fffffadf'c629fd95}`, the
same instruction as crash 3 - loading through the field `+0xA8` of the same
false element.

**This corrects section 5's first bullet.** usbport *does* store 32 bits at
`devext+0x988`, through `[rcx+28h]` on a wrong base, which a search for the
displacement `0x988` cannot find. It is a consequence, not the defect: the
defect is whatever makes usbport treat its `+0x9A8` head as a transfer.

**static and runtime** - how the head became an element. The caller is the
walk at `usbport+0xa3a1..0xa3e5`: `rax = [r12+60h]`, `r15 = r12+60h`, and
each step `rax = [transfer+48h]` until `rax == r15`. So the walk was over
**the endpoint's list at `endpoint+0x60`**, not the device extension's
`+0x9A8`. It left that list by following a transfer's `+0x48` link that
pointed at `devext+0x9A8`, then took that head for the next transfer. In the
kernel dump:

- the endpoint's `+0x60` list is empty (`fffffadf'cdfb2070` points to itself);
- the first transfer on `devext+0x9A8` is `fffffadf'cdf5f400`, and its `+0xA8`
  is `fffffadf'cdfb2010` - **a transfer of the same endpoint**, now on the
  device extension's list with `Blink` = that head;
- CPU 3 (the fault) is on usbhub's enumeration thread in the walk; **CPU 0**,
  at the same instant, is in usbport's DPC path `+0x12485` -> `+0x1026a` ->
  `+0xca44` with that transfer, that endpoint and `devext+0x9A8` among its
  arguments. That is section 4's list-processing routine's caller (crash 2's
  `+0x100b0` is reached from the same `+0x12485`).

The reading, which is **inference and not established**: a transfer was moved
from its endpoint's list to the device extension's `+0x9A8` list on one CPU
while another CPU was walking the endpoint's list, and the walk followed the
moved link. Crash 2's damaged `+0x9A8` head and crash 3's walk onto the head
fit the same race. Which path moved the transfer, and under which lock, is
the next thing to read. This driver's `UsbPortCompleteTransfer` call is one
candidate and not the only one: on the 200 tier it is made "from whichever
context got here" (`src/xhci_slot.c`, the completion drain), and whether
usbport 5.2 requires that call under a lock of its own is not established
here. **Section 4c establishes it.**

## 4c. The mover, the fix, and the run that tested it

**static** - XP x64's completion service, the function this driver calls as
`UsbPortCompleteTransfer` (packet slot `+0x1D0` = `usbport+0xc060`, stored at
`+0x2225c`/`+0x22263`), takes the transfer as `TransferParameters - 0x60`
(`+0xc107`), takes and releases a lock at `[rbx+58h]` around marking bit
`0xB` on the entries of a list at `rbx+0xB8` (`+0xc20d`..`+0xc2ce`), and
**then** unlinks the transfer from its endpoint's list with no lock held
(`+0xc2f1`/`+0xc2f4`) before inserting it on `devext+0x9A8` under
`devext+0x400` (`+0xc36b`..`+0xc37d`). The walk that crashed is reached with
**the endpoint's own lock**, `endpoint+0x160`, held (`+0xadf5`/`+0xae01`,
walk called at `+0xae99`), and CPU 0's done-list path was waiting on the same
lock (`+0xca32`/`+0xca3e`). So the unlink and the walk are ordered only if the
service's caller holds that endpoint's lock - which usbport's own callbacks
for that endpoint do (`PollEndpoint`, `SetEndpointState`, `AbortTransfer`),
and which this driver's event DPC, health poll and root-hub paths do not.
Bit `0xB` is also the flag the walk tests before calling `+0xbd60`
(`+0xa454`), which is how a moved transfer reaches the store of section 4b.

These offsets were first proposed by a second-opinion reading (another model,
read-only, 2026-09-13) and every one was re-read with `kd -z` before being
written here; the lock at `[rbx+58h]` is a refinement that reading did not
report.

**This driver's side** - on the Version 200 tier it delivered every
completion from whichever context drained the list (`src/xhci_slot.c`, the
completion drain): the event DPC, the health poll, root-hub callbacks,
`OpenEndpoint`, `SubmitTransfer` failures, and the lifecycle paths. The
issue 7 gate (`DeliverUnderUsbportLockOnly`) was off on that tier, on the
recorded premise that its service is self-synchronising, which is true of the
done-list insert and false of the unlink before it. The gate alone would not
have been enough either: usbport's lock here is **per endpoint**, and
`PollEndpoint` for one endpoint drained the controller-wide completion list,
so it could complete another endpoint's transfer without that endpoint's lock.

**The fix** (`XHCI_EXTENSION.DeliverPerEndpointOnly`, `src/xhci_slot.c`
`xhciSlotDeferredWorkEx`, `src/xhci_dispatch.c` `xhciStartController`), set
with the gate on the amd64 build's Version 200 tier only - XP x64 and Server
2003 x64:

- a completion is handed over only from `PollEndpoint`, `SetEndpointState` or
  `AbortTransfer` **for its own endpoint**; the oldest such completion is taken
  from wherever it sits on the list, so per-endpoint order is kept;
- any other pass leaves it parked and asks usbport to poll the endpoint that
  owes it (`UsbPortInvalidateEndpoint`, once a pass), which is what delivers
  it;
- the gate's 1 s fallback and the lifecycle paths' forced drains are
  unchanged, and both are counted;
- three counters: `completions delivered per endpoint only` (1 on this tier,
  0 everywhere else), `completions held for another endpoint's poll`, and
  `endpoint polls requested for a parked completion`.

A host test holds it (`test_slot_completion_per_endpoint_only`): with the mode
set, the event DPC parks and requests a poll of the owing endpoint,
`PollEndpoint` for EP0 does not deliver an interrupt pipe's completion, and
`PollEndpoint` for the pipe does. Every x86 path was unchanged at this point:
the mode was compiled only under `_WIN64`, and with it off the drain selected
the head as before. *That guard was lifted on 2026-09-15, after section 4d
read the whole 32-bit tier as sharing the mover; the x86 build now sets the
mode on its Version 200 tier too, and its first run is owed (issue 7 section
7.9's leg).*

**runtime** - 2026-09-14, tag `i8diag1`, snapshot `winxp64-smp4-issue8diag`
(snapshot 3 plus the new amd64 `qemu` build copied over
`System32\drivers\xhci98.sys`, SHA-256 `FDE30B38...4889343C`), `-smp 4`, the
three devices hot-plugged, the gdbstub watch of section 4b armed on
`devext+0x988` for every load:

| | |
|---|---|
| loads / live disable-enable cycles | 11 / 10, all three devices back on every enable |
| bugchecks | **0** |
| watch hits zeroing the low half | **0** of 16; the 16 are one teardown unlink per disable (the section 4b routine's `RemoveEntryList`, head going self-pointing) and two reuses of a freed extension being zeroed and re-initialized |
| `completions delivered forced` / `completion fallback polls` | **0 / 0** on every load - exact, a counter that never changes prints no change for the sample cap to hide |
| `completions delivered under usbport's lock` | tracked `transfers completed` (`0x3A` each on load 1, both capped lower bounds) |
| `isr count` / `dpc count` | equal on every load |

Against the unfixed tier the same evening - four bugchecks in roughly fifteen
loads, two of them within the first two enumerations of a boot - eleven clean
loads by chance is about one in fifteen. That supports the fix; it is one run
on one guest, not proof, and the build it ran differs from the committed one
only in comments and its build stamp.

**runtime** - 2026-09-14 evening, the committed build (SHA-256
`0BD32770...8502F13E`, rebuilt from `d1b4e71`) through issue 7's full leg at
`-smp 4` with the same watch: settled read, five live disable/enable cycles,
remove and rescan, **0 bugchecks, 0 of 17 watch records zeroing the low half,
forced and fallback 0 on every load**. Issue 7 section 7.8 is the record,
including a first rescan that reinstalled the older build from the transfer
drive and was taken again.

**What this does not cover:**

- **The 32-bit Version 200 targets** (Windows 98 SE, ME, 2000 SP4, XP SP3)
  still deliver from any context. Their services have not been read for this
  unlink; `src/xhci.h` already records, from design review A7, that SP4 and
  NUSB's completion path unlinks from the endpoint list "with no endpoint lock
  held", which would put Windows 2000's SMP environment in the same shape.
  Not observed there, and not established.
- **Vista and Windows 7** keep the issue 7 gate without per-endpoint matching.
  Issue 7 section 7.1 reads their lock as one per controller (the EpList
  lock), which would make cross-endpoint delivery safe there; whether every
  NT 6.x transfer-list walk and unlink runs under that lock is not yet read.
- Server 2003 x64 rests on being NT 5.2.3790, as the rest of that tier does.

## 4d. The 32-bit Version 200 tier, read statically, 2026-09-15

Section 4c left the 32-bit targets unread. Issue 7 section 7.9 then showed the
mechanism live on Windows XP SP3 x86 at four vCPUs - the walker holding the
endpoint lock, the mover not - and the owner asked for the remaining 32-bit
`usbport` builds to be read before deciding whether the tier takes the fix.
All four were read on the same evening (`legal-provenance.md` section 4 has
the row with every address; `vm\issue7-xp32-smp4\static-tier\README.md`
the table and the `kd` logs): XP SP3 x86 by name from its public PDB, and
NUSB 3.3 = 3.6 (one file, byte-identical), SweetLow's XP-derived build (the
Windows ME target's stack) and Windows 2000 SP4 without symbols, reached
from the exported `USBPORT_RegisterUSBPortDriver`'s sixteen service stores.

**The mover is the same function in all four.** The `CompleteTransfer`
service ends in a routine that takes the transfer's `Blink` and `Flink`,
splices them - `RemoveEntryList` with **no lock and no interlock** - stores
the status, then `ExfInterlockedInsertTailList` on the FDO's done list under
the done-list lock, then `KeInsertQueueDpc` on the flush DPC. Only the
offsets differ (done list `+0x6F8` / `+0x63C` / `+0x64C` / `+0x690`).

**The reader holds a per-endpoint lock in all four.** On XP it is
`EndpointHasQueuedTransfers` under `endpoint+0xC4`, from the flush loop. The
older builds have no such walk in their flush loop - they call
`InvalidateEndpoint` after every done transfer instead - and their reading of
the endpoint's lists lives in the endpoint worker (NUSB `0x9c80`, SweetLow
`0x7836`, Windows 2000 `0xa096`), which takes `endpoint+0xD4` first and, inside
it, calls the miniport's `PollEndpoint` under the FDO miniport lock
(`fdo+0x288`, Windows 2000 `+0x28C`).

So the contract section 4c read out of XP x64 is the contract of the whole
32-bit tier: `UsbPortCompleteTransfer` assumes its caller is inside a
callback usbport made for that endpoint, under that endpoint's lock; the
service itself locks nothing. A completion this driver delivers from any
other context - the interrupt DPC, a root-hub callback, the health poll -
races every endpoint-worker read of that list on every 32-bit build, exactly
as it raced the walker on XP x64 and, on 2026-09-15, on XP x86. What the
race does when it lands differs by build and is not read further: on XP x64
the walker wrote (bugcheck `D1`); on XP x86 the walker only read (a livelock,
issue 7 section 7.9); what the older workers do with a transfer that moved
under them is not established, and does not need to be for the decision.

What this does not say: nothing has been run at four vCPUs on Windows 98,
ME or 2000, so the exposure there is by reading, not by observation, and
the fix's cost on those targets has been measured only by analogy (amd64:
`forced` 0, `fallback polls` 0 across every load). The read says the tier is
one tier, and the owner took the fix on all of it the same night: the
`_WIN64` guard in `xhciStartController` is gone, `DeliverPerEndpointOnly` is
set on every Version 200 load, and `completions delivered per endpoint only`
must now read 1 on XP x86 as it does on XP x64. Lifting the guard hung the
host suite, which had never run the mode: it builds x86, so the mode had been
compiled out of everything it exercised except the four vectors that set the
flag by hand, and its `deliver_events()` - the event DPC - had been the
suite's deliverer of completions since Phase 7. The harness now carries the
half of usbport it lacked: `UsbPortInvalidateEndpoint` records the endpoint
and `usbport_worker()` polls it through the registered `PollEndpoint` after
every DPC, as usbport's worker does after `IsrDpc`. The hang itself was a
harness artefact - a global transfer record, parked once and reused by the
next vector, appended to the completion list twice and made it a one-node
cycle - and it cannot happen against usbport, which never resubmits a record
it is still waiting on; twenty count expectations moved to deltas or to
re-offer counts, and the suite reads 12,695 checks, 0 failures. Owed, in order: the XP 32-bit
four-vCPU leg on this build (issue 7 section 7.9's clauses), then Windows 98
under NUSB, ME under SweetLow's stack and Windows 2000 at four vCPUs, and the
single-vCPU install legs of the two shipping targets, since their binary
changed.

## 5. What was ruled out, and what an instrument could not see

**The first bullet below is wrong; section 4b corrects it.**


- **usbport does not store 32 bits at `devext+0x988`.** *static* - a byte
  search for the displacement `0x988` in the image finds nine hits, and each
  disassembles as a 64-bit use: the self-link initialization at `+0x13dc3`;
  an insert-tail of an element's `+0x78` entry at `+0x1d3ab` (`add rdx,988h`
  then `qword` stores); a load at `+0x2010`; and 64-bit walks under the
  `+0x460` lock at `+0x1d183`, `+0x1d2ec`, `+0x1f652`, `+0x1f6ed`,
  `+0x206ad` and `+0x20ff5`. None stores a `dword`. Writes that reach the
  head through an element pointer are 64-bit inline list operations on
  amd64.
- **This driver does not address it by displacement either.** *static* - the
  amd64 `xhci98.sys` contains no displacement `0x988`, and neither binary
  contains `-0x2A0`, which is `devext+0x988` expressed from this driver's
  extension. So whoever writes the zero reaches the address through a
  computed or stored pointer, which reading displacements cannot find.
- **This driver's extension lives in the same allocation.** *runtime* - the
  driver's own log on the crash-2 boot reads `cb StartController
  a=CE2F5DC8 b=CE2F5518`: `MiniportExtension` = `devext+0xC28`, 0x2A0 bytes
  past the damaged head, and the `USBPORT_RESOURCES` it is handed =
  `devext+0x378` (0x48 bytes, ending `+0x3C0`, not adjacent).
- **Driver Verifier on `xhci98.sys` alone guarded nothing that matters here.**
  *runtime* - `verifier /flags 0x0B /driver xhci98.sys`; after two cycles
  `verifier /query` read `loads: 3, unloads: 2` (so it does re-attach on each
  PnP reload) but `AllocationsAttempted: 0`. The driver allocates no pool by
  policy, so special pool had nothing of its to guard; only IRQL checking
  was live, and it raised nothing. Special pool cannot see a write inside a
  device extension in any configuration.
- The owner was typing `verifier /flags 0x0B /driver xhci98.sys usbport.sys`
  when crash 2 struck. That command never committed: after the reboot,
  `verifier /query` listed `xhci98.sys` only. Crash 2 ran under the same
  configuration as the two clean cycles before it.

## 6. What would name the writer, cheapest first

*Written before sections 4b and 4c and kept as the plan it was. Item 1 was
tried and did not work under TCG (section 4b); a QEMU gdbstub watchpoint did.
Items 2 and 3 were not needed.*

1. **A kernel debugger and a write breakpoint on `devext+0x988`** (`ba w4`,
   the low half). The guest already loads `kdcom`; it needs `/debug
   /debugport=com1 /baudrate=115200` in `boot.ini`, a named-pipe serial port
   on a copy of the `-smp 4` launcher, and the breakpoint re-armed on every
   enable, because the device extension is re-created each time. usbport
   writes this head only at initialization and when the first element
   changes, so the breakpoint fires rarely; the first write that zeroes the
   low half stops with the writer on the stack. Caveat: a debugger under TCG
   changes timing, and this is a race.
2. **The amd64 layout audit** - every structure this driver writes through
   compared with what usbport reads, M3-M8 style, looking for a 32-bit store
   through a pointer whose amd64 layout was guessed (M8's shape). Offline.
3. **An unfixed build at four vCPUs**, to learn whether issue 7's fix
   matters at all. Expensive at this rate.

Evidence is to be kept until one of these is done.

## Sources

Evidence, all under `vm\` (git-ignored):

- `vm\fix-issue7-xp64-smp4\` - crash 1: `Mini091326-01.dmp`,
  `post-bsod-state.png` (the bugcheck arguments), `winxp64-debugcon-CRASHRUN.log`,
  and the re-run's `r2-*` samples, debug log and QEMU trace; the one-off
  launcher `winxp64-smp4.cmd`
- `vm\fix-issue7-xp64-smp4\dump2\` - crash 2: `MEMORY.DMP` (kernel dump) and
  `Mini091326-02.dmp`, both lifted out of `vm\winxp64.img` with 7-Zip after
  shutdown, and the `kd-*.txt` readings; `v1-shot-01.png` and
  `v1-query2.png` / `v1-query3.png` (the Verifier queries), `v1-disable1/2`
  and `v1-enable1/2.png`, `v1-bsod-01/02.png`, `v1-afterreset.png`
- `vm\fix-issue7-xp64-smp4\dump2\v1-winxp64-debugcon.log` and
  `v1-winxp64-qemu-trace.log` - crash 2's debug log and QEMU trace, copied
  there before the image was reverted
- `vm\fix-issue7-xp64-smp4\leg\` - crash 3: `bsod-01.png`,
  `afterreset-02.png` (the error-report dialog), `crash3-debugcon-at-bsod.log`,
  `winxp64-debugcon.log` (with the post-reset boot),
  `winxp64-qemu-trace.fix7xp64smp4leg.log`, and `dump3\` holding `MEMORY.DMP`,
  `Mini091326-02.dmp` (this crash's; `Mini091326-01.dmp` beside it is crash
  1's, carried in the snapshot) and the `kd*.txt` scripts with their
  `kd-0*-out.txt` readings
- `vm\issue8-kd\` - the instruments and crash 4: `gdbwatch.ps1` (the gdbstub
  client), `autoarm.ps1`, the kd attempt (`winxp64-smp4-kd.cmd`,
  `kd-attach.cmd`, `init.txt`, `arm.txt`, `hit.txt`, `canary.txt`, `kd-*.log`),
  `gdb-hits*.log` and `gdb-load1/2-*.log` (the hit records; crash 4's write is
  `gdb-load2-fffffadfcdc36b28.log`), `gdb1-bsod.png`,
  `crash4-debugcon-at-bsod.log`, `winxp64-debugcon-i8gdb1.log`, and
  `dump4\` holding `MEMORY.DMP`, `Mini091326-02.dmp` and `kd-01-out.txt`
- `vm\issue8-kd\diag1\` - the fix's run (section 4c): `gdb-load1..11-*.log`
  (every watch hit), `winxp64-debugcon-i8diag1.log`, `diag1-01.png`; the
  staging boot's log is `vm\issue8-kd\winxp64-debugcon-i8diag0.log`, and the
  binary was staged as `vm\xferxp64\ISSUE8\XHCI98.SYS`
- `vm\issue8-kd\i7smp4b\` - the committed build's leg (issue 7 section 7.8),
  every watch record and both parts' debug logs
- `vm\winxp64.img` - snapshots `winxp64-clean-install`,
  `winxp64-clean-install-smp4`, `winxp64-smp4-installed-kerneldump` (the
  unfixed driver), `winxp64-smp4-installed-kd` (`/debug` on COM1, otherwise
  snapshot 3) and `winxp64-smp4-issue8diag` (snapshot 3 with the fix's
  build copied in)

Documents:

- [Issue 7](07-win7-x86-enable-arrest-usbport-done-dpc.md) sections 7.6 and
  7.7 - the leg this was found on; section 7.8 - that leg retaken on the fix
- [legal-provenance.md](../contributing/legal-provenance.md) section 4 - the
  provenance row for the readings above
