# Issue 8 - Windows XP x64 at four vCPUs rarely bugchecks in usbport, on a list head whose low 32 bits were overwritten

Status: **open, rare, cause not established.** Two bugchecks on one guest,
2026-09-13, both `D1` inside `usbport.sys`, both on a list head in usbport's
device extension whose 64-bit forward pointer had exactly its low 32 bits
replaced. **Which module wrote those 32 bits is not known.** usbport faulted;
that does not mean usbport wrote it, and this driver is a suspect, not
convicted. Nothing on this page is a fix, and nothing on it may be read as
exonerating this driver.

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

**Rate:** 0 in 7 uniprocessor loads; at four vCPUs, 2 bugchecks across about
ten live cycles plus idle time. Too rare to chase by repetition at one
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

## 5. What was ruled out, and what an instrument could not see

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
- `vm\winxp64.img` - **not reverted** after crash 2: it still holds both dumps
  and the Verifier setting. Snapshots `winxp64-clean-install`,
  `winxp64-clean-install-smp4` and `winxp64-smp4-installed-kerneldump`

Documents:

- [Issue 7](07-win7-x86-enable-arrest-usbport-done-dpc.md) sections 7.6 and
  7.7 - the leg this was found on
- [legal-provenance.md](../contributing/legal-provenance.md) section 4 - the
  provenance row for the readings above
