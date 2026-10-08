# Issue 12 - On the Dell Latitude E7250, the BIOS routes the xHCI's interrupt to a different IRQ from the one Windows connects, so USB devices crawl, hang or fail

Status: **open, root cause found (2026-10-08); fix planned** (roadmap
`roadmap-hcd.md` Phase 36, task 36.3 first; branch `2.2.1.0`). There is a
manual workaround (section 6).

**Machine:**
- Dell Latitude E7250.
- Intel Wildcat Point-LP xHCI `8086:9CB1` rev 03, subsystem `1028:062D`, at
  `00:14.0`.
- LPC bridge `8086:9CC3` at `00:1F.0`.
- EHCI at `00:1D.0`.

**Seen under:**
- Windows XP x64: a user's report against `2.2.0.0`, the first report of the
  HCD on real hardware under XP x64. Then the owner's own E7250.
- Windows 7 x64: the owner's E7250.

The same fault is expected under every target that uses the line interrupt.

## 1. Symptoms

- **A USB mouse is sluggish**, even with nothing else plugged in.
- **A hot-plugged mass-storage device never gets a drive letter.**
  - Device Manager lists the disk as working, but Disk Management hangs at
    "Connecting to Virtual Disk Service".
  - The user's UAS reader (Kingston `11B0:3307`) bound `xhciuas` and showed
    no disk. The owner's Bulk-Only SanDisk stick (`0781:55AB`) behaved the
    same under Microsoft's `usbstor.sys`.
- **A device present at boot** either:
  - fails enumeration, with the port marked Failed, cause "no slot", until
    it is replugged (XP x64); or
  - enumerates, but PnP then hangs: Device Manager will not open, Explorer
    hangs, and unplugging does not release it (Windows 7 x64).
- **With the EHCI disabled in Device Manager, nothing on the xHCI works.**
  The controller reports itself "working" with no devices.

## 2. Root cause

**The BIOS routes the xHCI's INTA, in the PCH, to a different IRQ from the
one its ACPI tables give the OS.**

| | Pin (`DnnIP`) | Route (`DnnIR`) | INTA goes to | What the OS is told (ACPI `_PRT`) |
|---|---|---|---|---|
| xHCI, device 20 | INTA | `D20IR` = `3210` | **PIRQA = GSI 16** | **GSI 21** |
| EHCI, device 29 | INTA | `D29IR` = `2035` | PIRQF = GSI 21 | GSI 21 |

The route registers are in the PCH's RCBA space: `D20IR` at RCBA+`3160`,
`D29IR` at RCBA+`3144`. A register's bits 2:0 route INTA to PIRQA-H (0-7),
which are GSI 16-23 in APIC mode. On this machine RCBA is `FED1C000`.

Windows connects `xhci98.sys`'s ISR on IRQ 21. The xHCI asserts its INTx on
IRQ 16, where nothing listens. What follows:
- **The ISR runs only when the EHCI interrupts**, since they share IRQ 21,
  and then finds the xHCI's events waiting. Everything else waits for the
  lost-interrupt backstop.
  - The backstop drains an event only after it has stood unchanged for 4
    tolerance ticks, about 0.4-0.5 s in practice.
  - Every transfer, command and stall recovery is slowed by that. A
    RESET_PIPE took 2-3 s.
  - So the HID is sluggish, and the mass-storage stack is too slow for the
    partition manager and mount to finish.
- **A device present at boot fails "no slot"** (the user's Finding 1).
  - The start's No Op self-test completes, but its completion is never
    delivered, so the command engine stays busy.
  - Each Enable Slot is refused `XHCI_CMD_BUSY`, and `hcdCommand` gives up
    at once.
  - Port enumeration runs on the controller thread, which also runs the
    backstop. So both attempts fail before the No Op is drained.
- **With the EHCI disabled**, no ISR call happens at all.
  - The controller thread blocks for up to 5 s in each command wait
    (`hcdCommandBody`), so the backstop cannot run either.
  - The commands time out and three controller recoveries follow. The
    recovery window refuses a fourth, and the controller latches failed.

Why it went unnoticed: Intel's own Windows 7 xHCI driver, and Microsoft's
from Windows 8, use MSI. MSI bypasses PIRQ routing, so a wrong `D20IR` never
affected them. `xhci98.sys` uses the line interrupt.

## 3. How it was found

### 3.1 The report and the reproduction

**The user's report**, against `2.2.0.0`, XP x64, at verbosity 2. The dumps
are in `temp/e7250/` (git-ignored).
- With the reader present at boot, Enable Slot "never completed" on both
  attempts. The only command issued in the whole boot was the start's No Op.
- With the reader hot-plugged, `xhciuas` bound but found no LUN: EP1 IN
  stalled four times.
- About half of all events came from the backstop. That was the first sign.

**The owner's reproduction**, on 2026-10-07: the owner's own E7250 under
Windows 7 x64, at `2.2.0.0`, with the SanDisk stick under `usbstor`. The
dumps are in `temp/e7520/`. It showed the same stalls, a sluggish mouse,
and 35-43% of DPCs from the backstop. Since `usbstor` showed it too, the
fault was in `xhci98.sys`, not `xhciuas`.

### 3.2 Leads that were dropped

- **An interim build, `2.2.0.1`**, was made on 2026-10-07 and withdrawn
  unsent. It held a Finding 1 fix (a drain at interrupt enable, and a BUSY
  wait-and-resubmit in `hcdCommand`) and diagnostics: `xhciuas`'s START
  report and six counters. Its code was reverted. Its tasks were roadmap
  36w.1 to 36w.3.
- **The stall-recovery order.** `hcdCfgReset` sends
  `CLEAR_FEATURE(ENDPOINT_HALT)` before Reset Endpoint and Set TR Dequeue.
  That reverses xHCI 4.6.8, and Codex and a Claude subagent each suspected
  it. But the dumps below showed every stall recovered. It is a spec fix to
  make on its own (task 36.12), not this hang.

### 3.3 The diagnostic build `2.2.0.2` (task 36.1)

`2.2.0.2` was built on `2.2.0.0`'s code with no change in behaviour. It
recorded:
- each pipe's oldest transfer and its Output Endpoint Context;
- a timeline of the slow requests (RESET_PIPE and others) and cancels;
- the interrupter's registers at each backstop drain.

It was **reverted on 2026-10-08 after its read.** The `2.2.0.2` dumps decode
only with its own `XHCISNAP.EXE` (`out\xhci98-2.2.0.2.zip`, or a build of
commit `3d667f1`).

The owner's runs on 2026-10-08, with the dumps in `temp/e7250-2202/`:

| Run | Windows 7 x64 | XP x64 (fresh install) |
|---|---|---|
| clean boot, nothing plugged in | `w7start` | `xpstart` |
| the mouse only | `w7mouse0` | `xpmouse0` |
| the stick hot-plugged, about 30 s later | `w7hot1` | `xphot1` |
| about 2 min later | `w7hot2` | `xphot2` |
| the stick and mouse present at boot | `w7boot` | `xpboot` |

What they showed:
- **Nothing was stuck.** Transfers completed, every Endpoint Context was
  Running, and every RESET_PIPE succeeded, slowly.
- **The controller raised its interrupt, and the ISR was not called for it.**
  At every backstop sample:
  - USBCMD.INTE, USBSTS.EINT, IMAN.IP = 1 and IMAN.IE = 1 were all set, and
    an event was waiting;
  - the ISR had last run 0.4-1.0 s before.

  The backstop never found an event with IP clear (`w7hot2`: 1402 looks).
  ISR claims tracked backstop drains: 203 vs 202 in `w7hot2`, 164 vs 164 in
  `xphot2`.
- **It failed from the first event.** In `xpstart` and `xpboot`, the first
  sample shows the start's No Op completion waiting, and "ISR entered
  never".
- **`xpboot` reproduced Finding 1** exactly: Enable Slot "never completed"
  twice, and port 13 Failed with cause "no slot".
- **The interrupt resource** came from the dump's `XHCI_EXTENSION` header:
  - Windows 7: vector `0xB2`, IRQL 11, LevelSensitive.
  - XP x64: vector `0x171`, IRQL 7, LevelSensitive. Device Manager showed
    IRQ 21, shared only with the EHCI.

### 3.4 The EHCI test (`xpnoehci`)

The owner disabled the EHCI in Device Manager and restarted with the mouse
and the stick plugged in:
- **The ISR was entered 0 times**, while IP, IE and INTE stayed set.
- Neither device came up. Commands timed out (`CommandsGivenUp` 4), three
  recoveries ran, and the controller latched failed.

So every earlier ISR call had been for the EHCI.

### 3.5 The PCI and chipset reads (RW-Everything, XP x64)

The screenshots are `pci-idle.jpg`, `pci-busy.jpg`, `lpc.jpg` and
`rcba-3100.jpg` in `temp/e7250-2202/`.

**The xHCI's configuration space:**
- Command `0006`: Interrupt Disable is 0.
- MSI Message Control `0086` (capability at `80`): MSI is off.
- PMCSR is D0. `3C`/`3D` = `15`/`01` (IRQ 21, INTA).
- **Status byte `06` read `98` while busy:** the Interrupt Status bit, so
  the controller does assert INTx.

So nothing masks the interrupt at the device, and it is not in MSI mode.

**The LPC bridge and RCBA:**
- LPC `F0` = `FED1C001`, so RCBA = `FED1C000`.
- `D20IP` = 1 and `D29IP` = 1 (both INTA).
- `D20IR` = `3210`, `D29IR` = `2035`. That gives the mismatch in section 2.

### 3.6 The live test (`xpfixed`)

With RW-Everything, the owner wrote `D20IR` = `3215` (INTA to PIRQF), a
16-bit write at `FED1F160`, on the running XP x64 system:
- **The mouse became smooth at once**, and the stick, hot-plugged, mounted
  with a drive letter.
- **`xpfixed`**, taken after the write in the same boot, shows:
  - no backstop drain after the write;
  - about 68,800 ISR claims;
  - 69,527 transfers submitted and 69,525 completed;
  - RESET_PIPE down from 2.5-3 s to under 16 ms.

A restart restores the BIOS value.

## 4. Considered and not chosen

- **MSI as the only fix.** It would bypass the routing on Vista and 7, but
  it cannot help Windows 98, 2000 or XP, which have no MSI. It is planned
  as well, not instead (section 5.3).
- **Polling only.** It works anywhere, but costs CPU time and latency
  everywhere. Polled event delivery is planned instead as a fallback
  beside the quirk (section 5.2).
- **A standalone tool** (a small driver that applies the write at boot). The
  owner chose to put the fix in `xhci98.sys` instead.

## 5. The fix (planned, roadmap Phase 36)

### 5.1 Detect the mismatch and route the interrupt to the IRQ Windows assigned (task 36.3)

This is a routing quirk behind a registry value, `XhciIntelIrqRoute`, a
REG_DWORD on the controller's software key. It is modelled on
`XhciIntelPortSwitch`: its strict DWORD reader is reused, but not its
meaning of 2. The policy was the owner's choice on 2026-10-08, after two
Codex reviews.

| Value | Meaning |
|---|---|
| **0** | No routing writes. What the driver finds is still reported. |
| **1** (default) | Only xHCI `8086:9CB1` with subsystem `1028:062D` and LPC `8086:9CC3`, the verified E7250. A missing, wrong-type or out-of-range value means 1, with a note logged. |
| **2** (opt-in; never the default) | Any reviewed pair below, whatever the subsystem. Every check is kept. |

Value 1 is keyed on the subsystem because the fault is this firmware's,
not the silicon's.

The reviewed xHCI/LPC pairs, all vendor `8086`. Where a PCH datasheet is in
`docs/references/`, it confirms the registers as follows:
- RCBA is at LPC `D31:F0` `F0h`: bits 31:14 hold the base, bit 0 the enable.
- `D20IP` is at RCBA+`3128h`; its bits 3:0 give the xHCI's pin (1 = INTA#).
- `D20IR` is at RCBA+`3160h`, 16-bit; its bits 2:0 route INTA (0-7 =
  PIRQA#-H#) and bit 3 is reserved.
- In APIC mode PIRQA# is IRQ16, up to PIRQH#, which is IRQ23.

| xHCI | Chipset | LPC device IDs | Register source |
|---|---|---|---|
| `1E31` | 7 series (Panther Point) | `1E40`-`1E5F` (Linux `pci_ids.h` range); Codex's list `1E44 1E46 1E47 1E48 1E49 1E4A 1E53 1E55 1E56 1E57 1E58 1E59 1E5B` | 326776-003: `D20IP` 10.1.25 p.406, `D20IR` 10.1.33 p.414, RCBA 13.1.40 p.493. Device IDs are in its specification update, not fetched |
| `8C31` | 8 series (Lynx Point, C220) | `8C44 8C46 8C49 8C4A 8C4B 8C4C 8C4E 8C4F 8C50 8C52 8C54 8C56 8C5C` (Table 1-2; `8C41`/`8C42` are engineering samples, not admitted) | 328904-003: `D20IP` 10.1.25 p.343, `D20IR` 10.1.34 p.350, RCBA 12.1.40 p.400 |
| `9C31` | 8 series LP (Lynx Point-LP) | `9C43 9C45` (`9C41` is an engineering sample, not admitted; `9C47` is from coreboot only, not in the datasheet, and not admitted) | 329003-003: `D20IP` 8.1.23 p.335, `D20IR` 8.1.31 p.342, RCBA 10.1.40 p.403 |
| `8CB1` | 9 series (Wildcat Point) | `8CC3 8CC4 8CC6` (`8CC1`/`8CC2` are engineering samples, not admitted) | 330550-002: `D20IP` 10.1.25 p.336, `D20IR` 10.1.34 p.343, RCBA 12.1.40 p.393 |
| `9CB1` | 9 series LP (Wildcat Point-LP) | `9CC3` | 330837-002 (Core M I/O, Wildcat Point-LP): `D20IP` 8.1.23 p.337, `D20IR` 8.1.31 p.344, RCBA 10.1.40 p.405; xHCI `9CB1` rev 03 in Table 1-4. Its LPC rows are the Y-line's; `9CC3` (U-line) was read on the E7250 itself. Also confirmed on hardware by the live write (section 3.6) |
| `8D31` | C610 / X99 (Wellsburg) | `8D44 8D47` | 330788-003: `D20IP` 10.1.34 p.288, `D20IR` 10.1.43 p.294, RCBA 12.1.40 p.350. Its summary Table 10-1 misplaces these as `3140h` and `31FEh`; the register pages agree with the other datasheets |

These are not admitted:
- whole ID ranges, and engineering samples;
- 100-series and later PCHs, which route through PCR/ITSS behind P2SB;
- non-Intel controllers.

Every pair is now confirmed by its datasheet. The fetching notes and hashes are in
`docs/references/README.md`, "Intel PCH datasheets".

**The driver writes only when all of these hold**, and otherwise reports
what it found:
1. The pair is admitted by the value, at the expected locations: xHCI
   `00:14.0`, LPC `00:1F.0`.
2. The LPC's RCBA is enabled and valid.
3. `D20IP` agrees with the xHCI's Interrupt Pin.
4. The interrupt is line-based in APIC mode, with a raw GSI of 16-23. The
   translated vector and IRQL are never used, and message-signalled or
   ambiguous resources are refused.
5. `D20IR`'s IAR differs from GSI-16.

**The write:**
- `(old & ~7) | (GSI-16)` as a 16-bit write, then a read-back.
- It goes through one noncached page mapped at RCBA+`3000`.
- Nothing else is touched: no other device's route, no LPC PIRQ selector,
  no IOAPIC entry, no ACPI link.

**Report-only, whatever the value**, in PIC mode (Windows 98 included) or
when the mode is uncertain. In PIC mode the route also passes through the
LPC PIRQ selectors, and GSI-16 means nothing there.

**When it runs:**
- at start, after IoConnectInterrupt and `XhciInitController`, before
  `XhciEnableInterrupts`. The start's No Op then completes with a delivered
  interrupt;
- on every return to D0, before `XhciResumeController`. Whether the BIOS
  restores the bad value on S3 resume is untested.

**It needs first:**
- the raw interrupt resource kept by `hcdParseResources`;
- a supported way to read the LPC's configuration space on every target.
  `HcdSvcConfigSpace` reaches only the xHCI's own PDO, and CF8/CFC is not
  acceptable.

**What it records:** what it found, decided, wrote and read back, in the
snapshot and the note ring, decoded by `XHCISNAP`.

### 5.2 Polling and the BUSY retry (tasks 36.4 to 36.10, 36.13, 36.14)

Polled event delivery (design record 15), moved into Phase 36 on
2026-10-08:
- `XhciInterruptMode` 0 automatic, 1 line, 2 polling, with an adaptive
  poll period (`XhciPollIdleMs` 50, `XhciPollActiveMs` 1).
- **For this issue**, automatic mode also moves to polling when a line
  interrupt is proven dead at run time: events waiting with `IP`, `IE` and
  `INTE` set, and the ISR claiming nothing (record 15 section 4.2).
  - That covers a machine the routing quirk does not, or cannot, repair.
  - The poller has its own timer DPC, so it keeps running while the
    controller thread is blocked in a command wait.
  - Mode 1 (the line, forced) never moves.
- **The BUSY retry (36.4).** `hcdCommand` on `XHCI_CMD_BUSY` waits a
  bounded time and retries under one deadline, instead of failing the port
  at once.

### 5.3 MSI on Vista and 7 (tasks 36.15, 36.16)

A message-signalled interrupt does not pass through PIRQ routing, so a
wrong `D20IR` cannot affect it. That is why Intel's own Windows 7 driver
works on the E7250. The plan covers x86 and amd64:
- **The INF** requests MSI on the NT6 paths.
- **The driver** accepts a message resource and connects it with
  `IoConnectInterruptEx`. On x86 that routine is found in the kernel's
  export table at run time, so the same binary still loads on 98 and ME.
- **The ISR** treats a message as always its own.

Windows 98, ME, 2000 and XP keep the line, with the routing quirk and
polling. Design record 15 section 4.3 and roadmap 36.15 carry the design
and its open questions.

### 5.4 Related changes

- **36.11:** XHCIQUAL reports the LPC, RCBA, `D20IP`/`D20IR` and the PIRQ
  routing, read-only. It does so only on the reviewed pairs of section 5.1,
  and elsewhere says "not applicable". These registers exist only on the
  RCBA chipsets:
  - 100 series and later, and the Apollo Lake-class Atom SoCs, use ITSS
    behind P2SB;
  - Bay Trail and Braswell use the iLB.
- **36.12:** RESET_PIPE in xHCI 4.6.8 order.
- **36.17:** the `2.2.1.0` builds, the QEMU regression, and the owner's check
  on the E7250 under XP x64 and Windows 7 with no manual write. The check
  covers:
  - the mouse;
  - the stick hot-plugged and present at boot;
  - the EHCI disabled;
  - S3.

## 6. Workaround until the fix: RW-Everything

**By hand**, after each start. A restart restores the BIOS value.
1. In RW-Everything, select PCI `Bus 00, Device 1F, Function 00`. Check that
   bytes `F0`-`F3` read `01 C0 D1 FE` (RCBA `FED1C000`).
2. Open Memory at `FED1F100` and switch to 16-bit view. Check that
   `FED1F144` reads `2035` and `FED1F160` reads `3210`. If either differs,
   stop.
3. Change `FED1F160` to `3215`.
4. Unplug and replug any USB device that was present at boot.

**As a batch file**, `e7250irq.bat`. It is kept with the evidence in
`temp/e7250-2202/` (git-ignored) and reproduced below.
- Put it next to `Rw.exe` and run it as an administrator after each start.
- It uses RW-Everything's command line, which the owner checked on XP x64:
  `/Min /Nologo /Stdout /Command="R16 ..."` and `"W16 ..."` both work.
- It writes nothing unless both registers hold the E7250's values, and it
  reads the result back.
- Its read and write commands were tested on the owner's machine. The whole
  script has not yet been run from a fresh start.
- A device present at boot still needs a replug.

```bat
@echo off
setlocal
cd /d "%~dp0"
if not exist Rw.exe goto norw
set OUT=%TEMP%\e7250irq.txt
Rw.exe /Min /Nologo /Stdout /Command="R16 0xFED1F160" > "%OUT%"
findstr /I /C:"0x3215" "%OUT%" >nul && goto already
Rw.exe /Min /Nologo /Stdout /Command="R16 0xFED1F144" > "%OUT%"
findstr /I /C:"0x2035" "%OUT%" >nul || goto unexpected
Rw.exe /Min /Nologo /Stdout /Command="R16 0xFED1F160" > "%OUT%"
findstr /I /C:"0x3210" "%OUT%" >nul || goto unexpected
Rw.exe /Min /Nologo /Stdout /Command="W16 0xFED1F160 0x3215" > "%OUT%"
Rw.exe /Min /Nologo /Stdout /Command="R16 0xFED1F160" > "%OUT%"
findstr /I /C:"0x3215" "%OUT%" >nul || goto failed
echo Done: the USB 3.0 interrupt now goes to IRQ 21.
echo Unplug and replug any USB device that was plugged in at startup.
goto end
:already
echo Already applied (the route register reads 3215). Nothing to do.
goto end
:unexpected
echo NOT APPLIED: this machine's registers are not as expected.
echo Please send these values to the developer:
Rw.exe /Min /Nologo /Stdout /Command="R16 0xFED1F144"
Rw.exe /Min /Nologo /Stdout /Command="R16 0xFED1F160"
goto end
:failed
echo FAILED: the write did not take effect. The register now reads:
type "%OUT%"
goto end
:norw
echo Rw.exe was not found. Put this file in the RW-Everything folder.
:end
pause
```

## 7. Evidence and sources

All the evidence is git-ignored:
- `temp/e7250/`: the user's `2.2.0.0` XP x64 dumps (`MYDUM`, `MYDUMP`,
  `BEFOREUSB`, `AFTERUSB`) and XHCIQUAL's `PROBE.LOG`.
- `temp/e7520/`: the owner's `2.2.0.0` Windows 7 dumps.
- `temp/e7250-2202/`: the `2.2.0.2` dumps (`w7*`, `xp*`, `xpnoehci`,
  `xpfixed`), the RW-Everything screenshots and `e7250irq.bat`.

Reading an amd64 `.BIN`:
- Use the `.TXT` where it suffices.
- Otherwise use a WDK 7.1 amd64 probe that includes `src/xhci.h` with
  `/DXHCI_HOST_TEST`. MSVC6 gives the wrong layout, and
  `scripts\local\offsets.txt` is 32-bit.
- The interrupt resource is at the start of `XHCI_EXTENSION`: vector +44,
  level +48, mode +52.

Sources:
- Intel PCH datasheets 326776-003 (7 series), 328904-003 (8 series),
  329003-003 (Lynx Point-LP I/O), 330550-002 (9 series), 330837-002
  (Core M I/O, Wildcat Point-LP) and 330788-003 (C610/X99), and Linux `pci_ids.h`. They are fetched into
  `docs/references/`, with the hashes and the ones still to fetch in its
  README.
- coreboot: `src/soc/intel/broadwell/include/soc/rcba.h`, and the
  `bd82x6x` and `lynxpoint` `pch.h` headers.
- The code: `src/hcd_ctl.c` (`hcdParseResources`, `hcdStart`, `hcdThread`,
  `hcdBackstop`), `src/hcd_enum.c` (`hcdCommand`, `hcdCommandBody`),
  `src/xhci_evt.c` (`XhciIsr`, `XhciEnableInterrupts`), `src/xhci_cmd.c`,
  `src/hcd_cfg.c` (`hcdCfgReset`), `src/hcd_power.c`, `src/hcd_svc.c`
  (`HcdSvcConfigSpace`), `src/xhci_psw.c`.
