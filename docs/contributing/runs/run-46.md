# Run 46 - Release `3.4.1.0`

Phase 46 of `roadmap-usb4.md`, for release `3.4.1.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 46.18 the installer's texts at 640 by 480 and 120 DPI: the legs (2026-11-02 and 03, partial)

Branch `w46-inst` from `3eae1bfe` (the tip of `3.4.1.0` with both parts
of the owner's wording, record 26 drafts 61 and 62), development host A.
Record 26 section 5.10, *The legs*: every page's Help opened and kept for
the owner on 98 SE at 640 by 480, XP and 7 x86; the window self-test on
each; on one system at 120 DPI the self-test and the Components page's
Help at its smallest screen; and each page itself looked at for clipped
or overflowing text, since lines grew.

### Builds and packages

- Gated x86 `release` of `3eae1bfe`: BUILD + GATES PASSED, `test_inst`
  8017 checks 0 failures, the XHCIINST self-test passed. Log
  `E:\xhci98-scratch\p46-w46-inst\gated-x86-3eae1bfe.log`.
- The legs' package: `make-release.ps1 -Arch x86 -TestLabel w46inst
  -SkipQualtool -SkipMarkTool` with both driver sets (`usb-wdm-free`
  `v0.1.7`, `usb-wdm-nonfree` `v1.2.0`) into a scratch folder, exit 0;
  the full download (`upload-3.4.0.0-nonfree`; the version is raised with
  46.final) on the transfer disk. Hashes
  `E:\xhci98-vm\p46\w46-inst\hashes-3eae1bfe.txt` (`SETUP.EXE`
  `f9759945...`, `XHCIWIZ.EXE` `8f91a8de...`).
- After the fix below (`4ac614db`): gated x86 `release` PASSED again
  (`test_inst` 8018 checks, 0 failures; log `gated-x86.log` there), the
  same test cut, hashes `hashes-4ac614db.txt` (`XHCIWIZ.EXE`
  `65848af2...`).

### The legs

Fresh overlays of the q35 bases through `launch-q35.ps1 -Usb4 asm4242
-EncAtBoot`, driven from the keyboard (F1 opens the same window as the
Help button, record 26 section 5.10, *F1*), QMP 49300 to 49302, every
start and stop in `E:\xhci98-vm\p46\w46-inst\pids.txt`. Each Help window
was read whole: a text taller than its edit (Components, and the
Thunderbolt and USB4 page) was shot again scrolled to its end.

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480, 96 DPI (`h98`) | Help and the page on Welcome, Components, the Settings USB, Thunderbolt / USB4 and Stor98 tabs, the Advanced dialog and Stor98's Advanced, the Thunderbolt and USB4 page, Free and Non-free USB drivers, Summary, Progress (the install running), Finish (the restart words); the uninstall's first page and Summary (then Cancel) | every window inside the screen above the taskbar, every text whole, nothing clipped; install exit 3; after a cold start the window self-test 860 checks, 0 failures |
| XP, 1024 by 768, 96 DPI (`hxp`) | the same install pages and dialogs, and the uninstall's first page, Summary and Finish (Remove run) | every text whole; the window self-test 860 checks, 0 failures |
| 7 x86, 800 by 600, 96 DPI (`h7`) | the same install pages and dialogs, Progress left open to the run's end (the help window closed and Finish showed, as section 5.10 holds) and Finish | every text whole; the window self-test (elevated) 860 checks, 0 failures |
| 7 x86, 120 DPI (`LogPixels` 120, then a restart), 800 by 600, its smallest | the window self-test; a second run of Setup: Welcome, Components and its Help (to its end), the three Settings tabs, both Advanced dialogs, the Thunderbolt and USB4 page, Free and Non-free USB drivers, Summary, each with its Help | the self-test 860 checks, 0 failures; every window inside 800 by 552, every text whole; Components' Help and the Thunderbolt and USB4 page's scroll, the others fit |

640 by 480 at 120 DPI was not read: 7's smallest size is 800 by 600,
which section 5.10 allows ("else at its smallest size").

Then, on the package of `4ac614db` (below), the pages the first legs did
not reach, each with its Help:

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480 (`h98b`, a fresh overlay) | the uninstall on a system with nothing installed: its first page, Summary and Finish (the restart words); then an install with the Thunderbolt box ticked; then Setup again with every device current, which ended on **Steps left for you** and **Finish with "No changes were made."**; then 3.2.0.0's `stor98.mpd` (`b3305de4...`) copied over the installed one and Setup again: the message box "Please disconnect all external NVMe drives before proceeding", Steps, and **Finish with the shut-down words** | every text whole; Finish shut Windows down |
| XP (`hxp`, after the uninstall above) | an install, its Finish (the restart words) | whole |
| 7 x86, 120 DPI, 800 by 600 (`h7`) | Setup over the installed package: Welcome with the driver store's note, Summary, Finish ("Not everything is done: ..."), each Help; then the uninstall: its first page, Summary and Finish | whole |

So every page and Help variant of section 5.10 was read through the
window on the owner's wording on 98 SE: Welcome, Components, Thunderbolt
and USB4, Free and Non-free USB drivers, the three Settings tabs, both
Advanced dialogs, Summary, Progress, Steps, Finish with the restart
words, the shut-down words and none, and the uninstall's first page,
Summary and Finish; and on XP and 7 x86 all of them but Steps and the
shut-down and none Finish variants, which those systems show only in
narrower cases.

**Left: the Steps page's Help on XP and 7 x86 through the window.** On
2000 and later the page is shown only when a file step is refused or
cancelled inside it (`XHCIPFW.SYS`, `S98VSD.VXD`, Stor98's file); a run
on XP cancelled while Windows installed the controllers ended on Finish
with no Steps page ("Not everything is done: follow the steps above
first, then restart Windows.", `xp-page-finish-cancelled.png`), the
cancel landing before any file step. The Steps topic's routing and its
text in the help window are in the window self-test, which passed on
both (860 checks). On the first install of 98 SE (`h98b`)
the guest restarted while Progress waited on Windows' own windows: a
key sent for Cancel answered Windows' restart question behind the page
(the log ends with the devices installed and "restart pending"); the
next start went on as above.

The owner's screenshots and their index (135 shots, those of both
packages): `E:\xhci98-vm\p46\w46-inst\shots-4618\index.txt`.

### Found, and fixed

- **Stor98's Advanced said `StorMaxTransferKB` "Allowed: 64 to 512 KiB"
  on 98 SE and ME**, while its default there is 2048 KiB and the engine
  accepts up to that default (`inst_s98_hi`). The window carried its own
  copy of the top, left from before the 2048 KiB default. `4ac614db`
  exports `inst_s98_hi` and the dialog takes the top from it; a host
  vector holds 2048 on 98 SE and ME, 256 on 2000 to 10, and the check at
  both. Reviewed and revised. Read on 98 SE at 640 by 480 on the new
  build: "Default: 2048 KiB. Allowed: 64 to 2048 KiB, in steps of 4."
  (`98se-page-adv-s98-4ac614db.png`); the window self-test there 860
  checks, 0 failures.

### Seen, for the owner (wording; not changed here)

- The Non-free USB drivers page on 2000 and later says twice that ticked
  drivers are staged: "Tick the checkboxes below if you want the drivers
  to be staged so Windows will automatically install them when it sees
  the appropriate device." under the box, and "Stage these drivers so
  Windows installs them automatically." over the list.
- The Help window of the Settings sheet's second tab is titled "Help:
  Thunderbolt settings"; the tab is "Thunderbolt / USB4".
- The uninstall's Help starts "Remove removes all of this package's
  drivers, tools and records."
- The uninstall's Finish text (XP) shows lines the engine wraps at 79
  columns for the console, which the window wraps again: "xhcicm was
  removed from / CriticalDeviceDatabase\...'s UpperFilters (a copy /
  Windows made / when a device was installed)."
- The Welcome page on 98 SE shows "Windows 98 SE (4.10.2222  A ),
  32-bit": the system's own " A " in its version string.
- The Progress page's bar stays empty while the package is copied and
  staged on 7 (roadmap task 46.8).
- Steps left for you lists, after a run over a current install, each
  controller with "Nothing to do: this version already runs it."; 7's
  Finish then says "Not everything is done: follow the steps above
  first" with no Steps page shown (the driver store held a build of the
  same version with other files, Welcome's note).
- The Summary says "Thunderbolt / USB4 support, experimental: not
  installed" where the box is unticked and the connection manager is
  already installed and kept.
- The message box "Please disconnect all external NVMe drives before
  proceeding" has no full stop.

## 46.14 The registry writes of `xhcicm` and Stor98 on 98 SE and ME (2026-11-02)

The owner's bench of 2026-11-02 (issue 21 section 8): 98 SE's Registry
Checker restored an older registry at the next start twice in one day,
each after a clean shutdown with no hang, and after the first restore a
page fault in VMM on plugging the tunnelled drive in. Before any other
change in those paths, every registry access the two drivers make on
98 SE and ME was read for the context it runs in. Reading of the sources
at `3eae1bfe` on development host A; no guest for the audit itself.

### What the system's registry services allow, as far as this project knows

The registry on 98 SE and ME is the VMM's. NTKERN's `ZwSetValueKey`,
`RtlQueryRegistryValues` and `ZwClose` on a handle from
`IoOpenDeviceRegistryKey` end in it, and `xhcicm` and Stor98 also call
its services directly (`_RegOpenKey` 148h to `_RegCreateDynKey` 176h,
`legal-provenance.md` section 4). What this project holds about their
rules, and how sure:

- They may block (the registry is paged from disk), so they are for a
  thread or a configuration callback, never an interrupt, a DPC, a timer
  callback or a miniport's timer [inf: the services are synchronous and
  return only when done; record 29 section 7.4 already keeps Stor98's
  counters out of the registry for that reason].
- Two threads may call them at once: ring-3 programs do so through
  every `RegSetValueEx`, so the VMM serialises its own callers [inf].
- They are there from `Device_Init` on (record 32 section 5.1, where
  XHCIPFW writes its result).
- What a write does once the shutdown has begun is not known. A 9x
  shutdown sends each WDM device a system power IRP; `xhcicm` passes it
  on without waiting for its own thread (`cm_power.c`, `CM_PWR_DETACH`,
  task 44.47), so the thread's next pass runs at a time the system no
  longer orders against anything, the system's last save of the registry
  included [inf]. That is the one context below with no known rule
  behind it.

### Every access, and where it runs

`xhcicm`, 98 SE and ME (`src\cm\`):

| Value (key) | Read / written by | Context | When |
|---|---|---|---|
| `XhciCmStatus`, REG_BINARY (the NHI's hardware key, `HKLM\Enum\...`) | `cm_statos.c` `cmStPublish` | the PnP start's dispatch (the first record), then the NHI's thread, PASSIVE_LEVEL, no lock | at the start, then only when the record changed; at a stop |
| `XhciCmBridgeDiag`, `XhciCmNoIrqDiag`, `XhciCmBridgeReqDiag` (same key) | `cmStBrDiag`, `cmStNiDiag`, `cmStBqDiag` | the thread, after the record, the copy taken under `BrDiagLock` and written after it is released | when the worker posted a newer attempt |
| `XhciCmEjectReply`, `XhciCmEjectResult` (same key) | `CmStatusEjectWrite`, from `cm_ejectos.c` `CmEjectAsk` and `CmEjectService` | the thread (the ask's pass, or `CmReenumService` / `CmNvlinkStep`) | at each step of a tool's eject |
| `XhciCmQueryName`, REG_SZ (same key) | `CmStatusQueryName`, from `cm_qryos.c` | the PnP start's and stop's dispatch | once each way |
| `XhciCmStatusAsk`, `XhciCmEjectAsk` (the driver key; read only) | `cmStReadAsk`, `cmStReadEject` | the thread | at most every 500 ms |
| the keys themselves | `CmStatusOpen` (`IoOpenDeviceRegistryKey`), `cmStClose` (`ZwClose`) | opened in the PnP start's dispatch; closed by `CmStatusStopped` on the thread at a stop, or by `CmStatusClose` in the removal's dispatch | once each |
| the settings (the NHI's driver key; read only) | `cm_reg.c` `CmReadValues` (`IoOpenDeviceRegistryKey`, `RtlQueryRegistryValues`, `ZwClose`) | the PnP start's dispatch, before the thread's start work | once a start |
| `Version` (`HKLM\Software\xhci98\Stor98`; read only) | `cm_cfgmg.c` `CmCfgmgStor98Version` (`_RegOpenKey`, `_RegQueryValueEx`, `_RegCloseKey`), from `cm_cfgwk.c` | the CONFIGMG worker thread | per re-enumeration that checks Stor98's record |
| `LinkState`, REG_BINARY (`HKLM\System\CurrentControlSet\Services\stor98`); the proof key `HKEY_DYN_DATA\Stor98LinkState` | `cm_nvlinkos.c` `cmNvWriteRecord` (`_RegCreateKey`, `_RegSetValueEx`, `_RegOpenKey`, `_RegCreateDynKey`, `_RegCloseKey`) | the thread, at the boot's first tunnel action, never while the NHI's start is pending | once per boot |
| the proof key's values (answered, not stored) | `cmNvProofQuery` | the registry reader's context (Stor98's `HwFindAdapter`) | at each read; touches only this image's memory |
| the devnode's own values (read only) | `cm_cfgmg.c` `Read_Registry_Value` | the CONFIGMG worker | per re-enumeration |

Stor98, 98 SE and ME (`src\stor98\src\`):

| Value (key) | Written by | Context | When |
|---|---|---|---|
| `StorFind`, REG_SZ (`HKLM\System\CurrentControlSet\Services\stor98`) | `platform.c` `PlatFindReport` | `HwFindAdapter` | each find, whatever its outcome |
| `StorInterrupt <bus>.<dev>.<fn>`, REG_SZ (same key) | `platform.c` `PlatIrqReport` | `HwFindAdapter`, after the DMA memory | each found adapter |
| `AcctBoot`, REG_DWORD, then `AcctRef`, REG_BINARY (`HKLM\Software\stor98\Pool`) | `pool9x.c` `P9AcctGet` | `HwFindAdapter` (the 9x pool's find) | once per boot, by the first adapter |
| `StorWriteThrough`, `StorPollMode`, `StorMaxTransferKB`, the tunables (read only) | `platform.c` | `HwFindAdapter` | each find |
| the proof's values (`HKEY_DYN_DATA\Stor98LinkState`) and `LinkState` (read only) | `pool9x.c` `P9ReadProof`, `P9Rendezvous` | `HwFindAdapter` | each find |
| test builds only (`STOR98_DBGPORT`, never shipped): the accounting damaged on purpose | `pool9x.c` `P9TestCorruptAcct` (its modes 2 to 4 write or delete `AcctRef` and `AcctBoot`), from `P9Finish` | wherever `P9Finish` runs, the timer-driven isolation among them | when the test option is set |

`HwFindAdapter` on 98 SE and ME runs inside IOS's start of the
adapter: during the boot's device initialisation, or for a drive
plugged in after the start inside the configuration manager's start of
its devnode (for a tunnelled drive, the `xhcicm` CONFIGMG worker's
re-enumeration). Neither is a sleep or a shutdown. In the shipped
builds Stor98's timer, ISR and request paths touch no registry service;
the test-only `STOR98_DBGPORT` path above is the one exception.

### Findings

1. **Every access is at PASSIVE_LEVEL on a thread or in a PnP or
   configuration dispatch.** None runs in an interrupt, DPC or timer
   callback, none holds a spin lock across the call, and none is made
   while the NHI's own start is pending in the configuration manager
   except the first record and the query name, which every WDM driver's
   start makes the same way (`xhci98.sys`'s door among them).
2. **`xhcicm`'s thread went on reading and writing the registry after a
   9x shutdown's system IRP had passed (the fault).** On 98 SE and ME
   `cmSystemDone` publishes the system state, queues the quiescence to
   the thread and lets the IRP go on (task 44.47); the thread then runs
   `CmNhiQuiesce`, whose changed state the same pass writes as a new
   `XhciCmStatus` (and any diagnostic posted meanwhile), and its next
   passes read the two asks. Nothing orders those accesses against the
   rest of the shutdown. It is the one access found with no rule behind
   it, and a candidate for the restores [inf: not shown to cause them];
   the same holds for a standby.
3. **Stor98's writes run only in `HwFindAdapter`**, a context in which
   the configuration manager itself writes the registry; none at a
   sleep or a shutdown. `StorFind` is new in `3.4.1.0` (task 46.1) and is
   written at every find. What a 9x resume does is unread: if the port
   called `HwFindAdapter` again there, as NT's does (Stor98's PLAN, G7),
   `StorFind` and `StorInterrupt` would be written from the resume. No
   change (the day's sequence had no standby); noted for the board.
4. **Nothing found explains the VMM page fault on the plug-in.** It
   came after the first restore, which left the registry older than the
   drivers in place (the router's driver binding gone, reinstalled by
   hand); the restored registry's devnode records for the hot-added
   bridges and the drive are as old as that backup. Left to the board's
   reading with the fix in place.

### The fix (finding 2)

A registry admission on 98 SE and ME (`src\cm\cm_thr.c`, `CmRegEnter`,
`CmRegLeave`, `CmRegCloseWait`, `CmRegSync`; the fields in `cm.h`):

- every registry access of the connection manager's thread - each ask
  read, the record, each diagnostic, each eject reply and result, the
  query name, and the link-state record of `cm_nvlinkos.c` - is made
  between `CmRegEnter` and `CmRegLeave`, each one admitted on its own;
- the power dispatch of a system IRP away from S0 (`CmNhiPower`, at
  PASSIVE_LEVEL, before the IRP goes down) closes admission and then
  waits for an access already admitted to leave, for at most 1 s. The
  bound is the residual: an access still running after it goes on
  beside what follows the IRP, counted and logged
  (`cm.reg.close-timeout`, and `cm.reg.close-timeouts` at the stop). A
  wait with no bound was not taken: were the access held back by the
  registry itself, the system busy with it while it sends the IRP [inf],
  the shutdown would hang, the worse failure on this system;
- the keys and the start's reads too: a stop refused admission keeps
  its two handles for the next start's open or the removal's close, and
  the removal closes them whatever admission says (98 SE and ME send no
  removal at a shutdown); a start refused admission reads no setting
  (the defaults stand, and such a start goes no further than inert) and
  opens no key;
- the completion (`cmSystemDone`), once the state is published, reopens
  admission only for S0, so a sleep IRP that comes back failed reopens it
  too;
- what was refused is kept: the record dirty, the query name as last
  asked for (its own copy, under `BrDiagLock`, since the PnP dispatch
  sets it and the thread may write it), each written at the first pass
  back in S0 or by the stop before the keys close; an eject reply or
  result reports the write failed, as any failed write does; the
  link-state record is tried again at the next exposure, that one
  publishing no `UP`. The refusals are counted (`cm.status.deferred`,
  the first logged with the system state as `cm.status.deferred.first`).

NT: always admitted, nothing counted, no wait. Stor98: no change; no
Stor98 file touched.

### The legs (2026-11-02 and 03)

98 SE in QEMU on development host A: overlays of the q35 98 SE base, the
`usb4-emu` ASM4242 model with the board's padding (the bases' emulator
build of 2026-10-27 with the board's interrupt pins; root port
`bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref32-reserve=4M,
hotplug=off`, `fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, tunnel
ports without a power controller), the ORICO enclosure model plugged and
unplugged over QMP, one vCPU, 512 MB. Two installed images: the
published `3.4.0.0` (with NUSB 3.3) after its first plug-in's wizards,
and the same with this branch's `qemu` flavour `xhcicm.sys` (`2e4955dc`,
SHA-256 `3d9e00f7...`) copied over it. Each case is a fresh overlay: a
start, the trigger, a shutdown from the Start menu (or a hard stop), and
the next cold start watched for the Registry Checker's screen (the
VGA text and screenshots). Evidence `E:\xhci98-vm\p46\w46-cm\rc\`.

| Case | Trigger before the shutdown | `3.4.0.0` | This branch |
|---|---|---|---|
| a, 5 s | the drive plugged in, shutdown 5 s later, its records still being written | no restore | no restore |
| a, 60 s | the drive plugged in and working, shutdown | no restore | no restore (twice) |
| b | an eject (`XHCIINST /eject 1`, exit 0), shutdown 5 s later | no restore | no restore (twice) |
| c | a copy loop onto the drive, interrupted, shutdown | no restore | no restore |
| d | a copy loop onto the drive, QEMU stopped mid-copy (a hard stop) | no restore (ScanDisk at the next start) | no restore |

**The board's restore was not reproduced** by any trigger, on either
build. Two incidents on this branch's image, neither repeated on a
second run of the same case: at one first start "A fatal exception 0E has
occurred at 0197:BFF72170" (the address of 45.36 and ME0E.1, seen on
`3.4.0.0`'s images before, and the board's own fault at a start), and one
second session whose shutdown did not finish within two minutes. The
`qemu` flavour's debug-port log shows no record of a system power IRP at
any of these shutdowns, so in the emulator the thread's registry
accesses may never meet a shutdown at all; whether the board's 98 SE
sends one is for its log (the fix logs `cm.status.deferred.first`).

So 46.14's fix stands on the audit's reasoning, not on a reproduction.
Left to the board: the day's sequence with this build and clean
shutdowns.

## 46.16 The USB stick with no drive letter, in the emulator (2026-11-02)

The owner's reading: on the board a USB flash drive on the board's own
xHCI host got no drive letter on 98 SE once the tunnelled drive was
attached, listed as a USB Mass Storage Device with no problem code; the
owner suspected the registry growing with each connect. Read in the
emulator as 46.14's legs (the published `3.4.0.0`, NUSB 3.3's storage
files, the ORICO model), the stick a QEMU `usb-storage` on the board's
own `qemu-xhci`. Evidence `E:\xhci98-vm\p46\w46-cm\r16\`, `r16c\`,
`v16\`.

| Step | Drive letters |
|---|---|
| Control: a fresh install, the stick alone | the stick E: |
| 20 cycles of the drive plugged (50 s, E: checked each time) and unplugged | E: each time |
| then the stick, the drive out | the stick E: |
| the drive plugged with the stick in | E: and F: |
| the stick unplugged and plugged again with the drive in | E: and F: |
| a clean shutdown, a cold start, 15 cycles of the drive unplugged 8 s after its plug (the 46.13 loop's shape), then the drive and the stick | E: and F: |
| three cycles of the drive unplugged without an eject, plugged back, then the stick | E: and F: each time |

`SYSTEM.DAT` was 2,383,904 bytes before the 20 cycles and the same after
them, and 2,392,096 bytes after the stick's first install and the 15
quick cycles: the same devnodes are taken again at each connect, and the
registry does not grow with connects. The coordinator's reading of the
board's own files agrees (no key near a limit, growth from a software
install). **Not reproduced**: in the emulator the stick gets its letter
after every sequence tried. Not run: an eject before each replug and a
drive taken out in use, then the stick (the scripted eject took a wrong
form in that run). Left to the owner's board, as the roadmap line says.

## 46.12 The platform's USB4 handover on 98 SE and ME (2026-11-02 and 03)

Design record 32 section 12.8, four drafts, each reviewed and revised
(the firmware recognised by the hashes of the five methods' code, not by
names or constants; no processor register read; every physical read a
byte or a 32-bit field the firmware's own code reads, in its order; the
three named SSDTs walked whole, never searched; the topology taken whole
before anything is written; no power register written). Code `603f6329`
(reviewed, no finding left): `src\cm\cm_osc.c` and `cm_osc.h` (pure),
`src\cm\cm_oscos.c` (x86 alone), the value `XhciCmPlatformHandover`
(1; 98 SE and ME), called from the USB4 start before the platform's
steps.

### What was read off the tree

The board's own ACPI tables (the owner's two captures of 2026-11-02, LAN
on and off; kept outside the tree) through the pure half, built for the
host: all three named SSDTs walked whole, `\CPNV` and its two pointer
fields placed where the firmware's AML places them, the five methods
found once each, and their hashes identical in both captures; those
hashes are the one build in the release flavour's list. No address, bus
number or table byte of the board is in the tree.

### Host vectors

`test\test_cm_osc.c`, 82 checks: the root pointer (checksums, revisions,
alignment), root tables, MCFG and the ECAM arithmetic, the RAM bounds,
the AML walk (scopes and devices, a field in another scope not taken, a
method or buffer holding a region's bytes skipped, an unknown term
ending the table, a name as a region argument ending it, duplicate and
ambiguous declarations, every field-list element), the hashes and the
known build, the match's refusals. The gated builds of `603f6329` (x86,
both flavours, and amd64 release) passed, the host suites with them
(`E:\xhci98-scratch\p46-w46-cm\gated-x86-all.log`,
`gated-amd64-release.log`).

### The emulator

`usb4-emu` `ad29e11` (branch `w46-cm`): `usb4-asm-usp fw-handover=on`
answers the mailbox as the board did, `fw-data=<address>` maps the
firmware data, `usb4-host fw-handover-gate=on` keeps the router silent
until the handover, and `scripts/asm-fw-ssdt.py` builds the three SSDTs
(this model's own method bodies, recognised by the `qemu` flavour's
list alone). Read on the host: the SSDTs disassemble cleanly and the
pure half places and recognises them.

**The guest leg** (2026-11-03). 98 SE on the q35 base, with the three
SSDTs given to QEMU, stopped at its start: "While initializing device
CONFIGMG: Windows protection error" (twice, the field's lock flag on and
off); without them it started. The base's 98 SE runs ACPI (it was
installed with `setup /p j`), where the owner's 98 SE runs none, so the
leg was moved to a 98 SE without ACPI, as the board's.

- **98 SE without ACPI.** On an overlay of the q35 base,
  `C:\WIN98\SETUP.EXE` run again over the installed system from the
  command prompt with `/p i` (ACPI off) and a batch file giving the
  install's own answers (`/is /ie`; from inside Windows Setup stops with
  SU0013). Setup's detection made a Standard PC: no ACPI BIOS devnode,
  "Advanced Power Management support", the PCI bus enumerated, COM1, LPT1,
  the keyboard, the PS/2 mouse. The tables stay in memory (SeaBIOS's,
  with QEMU's `MCFG` and the three SSDTs) and 98 SE does not read them:
  it started with the SSDTs given. Then the ASM4242 topology's wizards,
  the published `3.4.0.0` installed (`XHCIINST /dir 98-me /yes /nopause
  /thunderbolt /stor98`, exit 3), and its `xhcicm.sys` replaced by this
  tree's `qemu` build of `603f6329` (`16497eed...`; `src\cm` is the same
  at `bf5eb8a6`). Images (development host A, kept outside the tree):
  `E:\xhci98-vm\p46\w46-cm2\na\disk.qcow2` (`9c9daa87...`) and
  `lg\disk.qcow2` (`647594d4...`), read-only.
- **The emulator.** The first start with the mailbox model refused at
  the last check before the exchange, `cm.osc.refused=00000104`: the
  upstream port has no power management capability, so its power state
  cannot be read. Every PCI Express function has one (PCIe Base 5.0
  section 7.5.2); QEMU's XIO3130 upstream port, which the model extends,
  does not. `usb4-emu` `befc800`, `6493907` and `e427aac` (branch
  `w46-cm2`, reviewed and revised): `usb4-asm-usp pm-cap=on`, a PM
  capability (PCI PM 1.2) whose PowerState stays D0. The h1 to n2 legs
  below ran on `befc800`'s binary (`35f3f091...`), whose capability read
  D0 the same way; h2 and h3 repeated the positive start on `6493907`'s
  (`19aef3fa...`) and `e427aac`'s (`97cb422e...`).
- **Every leg**: q35, TCG, one vCPU, 512 MB, `fw-handover=on`,
  `fw-data=0x7FF00000`, `fw-handover-gate=on` (the router silent until
  the handover), `pm-cap=on`, the three SSDTs of `scripts/asm-fw-ssdt.py`
  (`97a30a65...`, `8ac80772...`, `94c70e13...`), the ORICO model
  hot-plugged at the desktop; the `qemu` flavour's log on the debug
  console, the model's mailbox writes in QEMU's output.

| Leg | What | Result |
|---|---|---|
| h1 | the handover | the five methods' hashes the `qemu` list's (`E5BC260E`, `F8435882`, `E6BC27A1`, `ABE3DC80`, `1F4395E7`); bytes `10h` 2, `4Dh` 1, `4Bh` 0; the root port found; `cm.osc.mb.before=0`; `cm.osc.handover=00000100`, `cm.osc.query=000F0100`; the model saw the two commands once each (`0102h`, `0180h`); the router answered (`u4.host.id=174C2428`), the USB4 start completed |
| h1 | a tunnelled drive | the ORICO hot-plugged: the enclosure's bridges' and Stor98's wizards (the image's first plug), then "Orico (E:)"; a file copied to E: and compared (`fc /b`: no differences) |
| h2 | the same start on `6493907`'s binary | `cm.osc.handover=00000100`, `cm.osc.query=000F0100`, the router answering |
| h3 | the same start on `e427aac`'s binary | the same; the model saw the two commands once each |
| n0 | `XhciCmPlatformHandover` 0 (machine-wide key, `regedit /s`), cold start | `cm.osc.off`; no mailbox command; the router silent (`u4.failed=0000000C`) |
| n1 | one byte of `\M049`'s code changed (`Return (Zero)` to `Return (One)`, the table's checksum mended) | that method's hash differs; `cm.osc.refused=0000000D` (not a known build); no mailbox command; the router silent |
| n2 | the firmware data naming another root port (`fw-data-root-dev=5`) | `cm.osc.root=00000500`, `cm.osc.refused=0000000B`; no mailbox command; the router silent |

Not run: a disable and enable of the NHI (sending nothing again), the
mailbox busy, a reply never given, and the release flavour declining the
emulator's tables (section 12.8.8's other legs). Evidence:
`E:\xhci98-vm\p46\w46-cm2\h1\` (`debugcon-b.log`, `qemu-b.out`,
`s1.png` to `s12.png`), `h2\`, `h3\`, `n0\`, `n1\`, `n2\`, `lg\`
(`debugcon-b-refused104.log`), `pids.txt`.

Left: the legs not run above; then the board, LAN on and off.

## 46.3 Stor98's INF header comments and where each INF goes (2026-11-02)

Each of `src\stor98\inf\stor98-9x.inf`, `stor98-x86.inf` and
`stor98-x64.inf` now names, in the same three lines, the name it is
staged under in the flat package (`stor98w9.inf` for the 9x INF,
`stor98.inf` for the NT two; `scripts\package\package-common.ps1`), the
path the release publishes it at (`98-me\stor98\`, `nt-x86\stor98\`,
`nt-x64\stor98\`; design record 36), and Stor98's own standalone layout
(`x86\` and `x64\` of `releases\0.3.0.1` and `0.4.0.0`, where the 9x
install was part of `x86\stor98.inf`; whether a standalone release would
ship the 9x INF is left to Stor98). The INFs name each other by source
name; the x64 header says the catalog goes with the xhci98 package only.
`scripts\inf-gate\check-stor98-inf.ps1`'s description says it holds three
INFs and where the NT two go. Comments only: no section, id, file list or
`DriverVer` changed. The INF gate and its self-test (81 cases) and
`scripts\check-text.ps1` pass. Reviewed and revised (one round's finding:
the standalone releases carried no catalog). Commit `a32b83d9`.

## 46.1 the emulator legs: 98 SE and ME with an NVMe disk the BIOS serves (2026-11-02 and 03)

The code is `211a17e6` to `e806f378` (Stor98 PLAN 3.1); these are the
emulator legs of the task's steps 1 and 3. Development host A, QEMU TCG.

### The vehicle

- Emulator: the q35 bases' pinned usb4-emu build
  (`asm4242-20261025-2230`, `qemu-system-x86_64.exe` SHA-256
  `da60139f...0ce93`), copied into the leg's folder and run from there;
  SeaBIOS rel-1.17.0 from usb4-emu's share folder. The ASM4242 layout
  (`launch-q35.ps1 -Usb4 asm4242 -EncAtBoot`: the firmware model tunnels
  the ORICO model's NVMe drive at power-on).
- Two switches added to the leg's copy of the launcher: `-SysNvme` puts
  the system disk on a QEMU `nvme` at `00:04.0` with `bootindex=0`, which
  SeaBIOS boots through INT 13h; `-Nvme2` adds a second internal `nvme`
  at `00:05.0` (an overlay of the FAT test disk), and `-Nvme2Boot` gives
  it `bootindex=1`, so SeaBIOS sets it up and serves it too.
- 98 SE: a fresh overlay of the q35 98 SE base (`ab1d4d65...`), started
  from the NVMe system disk, with the second internal NVMe and the
  tunnelled drive at power-on. The package: the gated x86 `release` build
  of this branch at `8f77995b` ("BUILD + GATES PASSED (release)";
  `stor98.mpd` SHA-256 `903c79da...c55f`), laid out by
  `ConvertTo-FamilyLayout`, installed with `XHCIINST.EXE /dir 98-me /yes
  /nopause /thunderbolt /stor98` (exit 3).
- ME: the q35 ME base (`42fea19a...`) halts at the start when its system
  disk is on the NVMe ("Cannot find or load required file KRNL386.EXE.
  Access to the file was denied. System halted."), as it does on the q35
  board's AHCI controller (the bases' `STATE.md`): ME does not start from
  a disk only INT 13h serves, so there ME keeps its IDE system disk and
  the BIOS-served drive is the second NVMe (`-Nvme2Boot`).

### Readings

| Step | 98 SE | ME |
|---|---|---|
| The start from the NVMe system disk, SeaBIOS's INT 13h | the desktop; C: "MS-DOS compatibility mode" | not possible (above) |
| Install with the tunnelled drive at power-on | the tunnelled drive gets Stor98; both internal NVMe functions left ("an internal drive, not on a Thunderbolt / USB4 port") | the same; `00:05` left |
| Cold start after the install, everything at power-on | the desktop; `StorFind` "port 5.0.0 ... found 5.0.0; cmd 0107 cc 00000000 csts 00000000 taken; result 1" (the tunnelled drive, which SeaBIOS had not set up); no find for the internal functions | the same `StorFind` |
| Stor98 bound by hand (`XHCIDI` job, as a user accepting Windows' offer) to `00:04` (98 SE's system disk) and `00:05`; cold start | the desktop. `00:04`, which SeaBIOS had enabled: Code 10, C: still through the BIOS (compatibility mode), no `StorInterrupt 0.4.0`; `00:05`, which SeaBIOS had left disabled: taken (`StorInterrupt 0.5.0` "... interrupt"), its volume read and written (`fc /b` clean); the tunnelled drive the same | `00:05` (SeaBIOS-served, `bootindex=1`): left, no `StorInterrupt 0.5.0`; drives C: and D: in compatibility mode, D: the BIOS's |
| Step 1's "before": the `3.4.0.0` `stor98.mpd` swapped in, cold start | **no hang**: the desktop, and C: no longer in compatibility mode - the old scan from bus 0 took the system disk SeaBIOS was serving (Device Manager > Performance lists only A: and D:) | - |
| The tunnelled drive unplugged during a copy and plugged back, four times, then Ctrl+Alt+Del twice (98 SE, a test image, below) | the drive back each time under Stor98; Close Program shown, no fault | - |

So the emulator does not reproduce the board's stop at "Initing stor98.mpd":
there the old image's takeover of the BIOS's system disk worked, and C:
moved to Stor98. The board's hang came after that takeover, from the
port's `HwAdapterState(FALSE)` during the start (PLAN 3.1), which QEMU's
guest did not call [inference: nothing in the emulator's log shows the
call; the release image logs nothing]. The new code leaves a function the
BIOS has set up to it, in the emulator as on the board.

Evidence: `E:\xhci98-vm\p46\w46-stor\` (`s98a\`, `meb\`: `serial-*.log`,
`cmdline-*.txt`, the shots named above; `pids.txt`), the kits `xf1\`
(install), `xf2\` and `xfme\` (the `XHCIDI` jobs, the old and new
`stor98.mpd`), `xf1-sums.txt`.

Not done here: the board (46-E.1), and a hot plug of the drive after a
cold start with nothing attached (the four unplug and replug cycles above
cover a plug after the start).

## 46.9 related: the 98 SE faults inside VMM, and the shared IRQ (2026-11-03)

The board (owner, 2026-11-02 and 03; board data not here): "fatal
exception 0E" inside VMM during CrystalDiskMark on the tunnelled drive on
98 SE in interrupt mode, once with two driverless AHCI controllers sharing
the drive's IRQ and once without; later faults inside VMM at a plug-in and
at Ctrl+Alt+Del. Read and tried in the emulator; **no reproduction and no
cause found in Stor98 so far**.

- **`RequestTimerCall` with 0 on 98 SE's port.** Stor98 cancels its timer
  with a 0 request (as NT's port documents). A test image
  (`STOR98_TEST_T0PROBE`, debug-port build, not shipped) hands the port a
  timer routine of its own: a 0 request with nothing pending never called
  it (more than 5 s, both adapters); a 1 s request followed by a 0 request
  never called it either (more than 60 s). So in the emulator 98 SE's port
  takes 0 as a cancel and arms nothing (stage `15E`: `201`, `205`, `203`,
  no `202` or `204`). The same image logs any call of the timer routine
  after a stop (stage `15F`): none in four unplug and replug cycles and a
  Ctrl+Alt+Del. `s98b\debugcon-f.log`, `debugcon-g.log`.
- **Load.** The release image, the tunnelled drive in interrupt mode, 12
  passes of XHCIPERF (sequential 1 MiB at one and two threads, random
  4 KiB at one and four threads, read and write, 48 MiB): 288 results, no
  fault (`s98a\serial-i.log`). TCG speeds, far below the board's.
- **The interrupt path, read.** `EngInterrupt` returns at once in every
  state but running, resetting, gone and quarantined; to find its work it
  reads no register, only the completion queue's memory, writes a
  completion doorbell only for entries it consumed, and claims a call only
  when it consumed one of its own completions. What the completions it
  consumes lead to runs at interrupt time on 9x too: the port's
  `RequestComplete`, `NextRequest` and `NextLuRequest`; the dispatch of
  waiting requests (`ScsiPortGetPhysicalAddress`, the walk of the IOR's
  scatter/gather list, bounce copies to and from the caller's linear
  addresses, submission doorbells); `Get_System_Time`; on an interrupt
  storm, a timer request (`LcStormMask`); and a completed SHUTDOWN's Flush
  (`EngShutdownFlushed`, which ME sends) reads CC, writes INTMS and CC and
  asks for a timer, as does the state change before it. Whether those
  timer requests are safe at interrupt time on 9x is not established
  (Stor98 PLAN, "Missed-interrupt watchdog"). Whether the 98 port keeps `HwInterrupt` out of
  `HwStartIo` and the timer routine is inferred, not measured (Stor98's
  `docs/gates/G9.md`); if it does not, the issue path is open to a race
  (the child's doorbell is written before its parent's `Outstanding` is
  counted, `EngIssueRwChild`). A test image (`STOR98_TEST_REENTRY`) counts
  `HwInterrupt` entries while either runs (stage `15D`): under XHCIPERF
  load on the tunnelled drive, 714,414 `HwStartIo` calls and as many
  completions, and not one entry (`15D` never logged;
  `E:\xhci98-scratch\p46-w46-stor\reentry-debugcon-l.log`). So in the
  emulator the 98 port does keep `HwInterrupt` out of both, and that race
  does not occur there; real hardware's timing is not covered by it.
- **Change with 46.9:** no timer request from 98's interrupt storm
  masking (Stor98 PLAN, "Missed-interrupt watchdog").

## 46.9 Stor98's missed-interrupt watchdog: design, code and legs (2026-11-03)

Stor98 PLAN, "Missed-interrupt watchdog" (the design reviewed and revised
in two rounds, the code in two): an adapter that started interrupt-driven
and then finds completions at its timer three times within 2 s with no
interrupt claimed between them latches to the polling mode for the rest of
the start; `StorIntWatchdog` / `storintwatchdog=`, 1 by default, written
by the INFs at its default (no overwrite), the INF gate's rows and
self-tests, the readme's SETTINGS table and the installer's Stor98 Advanced
row. Host vectors: `poll_ut.c` (the run, a claim, the window, the
timer-before-interrupt interleaving, disarming, saturation, the setting)
and `test_inst.c`. Commit `f303b42c`; the gated x86 `release` build at
`f303b42c` passed (`E:\xhci98-scratch\p46-w46-stor\gated-x86-release-2.txt`;
`stor98.mpd` `c21d0ce4...`, x86 `stor98.sys` `2928f267...`, XHCIPERF
`e85bcb30...`).

**The vehicle.** The q35 bases on the pinned emulator (above), the ASM4242
layout with the ORICO model's drive tunnelled at power-on, a fresh overlay
with this build installed (`INSTALL /console /yes /nopause /thunderbolt
/stor98`). The interrupt is suppressed after the start from QEMU's monitor
(`intx.sh`: the guest stopped, CF8h kept, the drive function's command word
read and written back with bit 10, Interrupt Disable, set, by 16-bit
accesses at CFCh, CF8h restored, the guest resumed; the command word read
back `0507` from `0107`). Each reading is one XHCIPERF run on the drive's
volume (32 MiB, sequential 1 MiB and random 4 KiB, read and write, two
passes of 5 s), its Stor98 page sent to COM1.

**XP** (the q35 XP base `64ada103...`, `ntxp`):

| Step | Stor98 page's mode | Seq 1M write / read | Rnd 4K write / read |
|---|---|---|---|
| interrupts, nothing suppressed | `interrupts` | 112.3 / 344.5 MB/s | 809 / 835 IOPS |
| INTx suppressed, first run | `polling after missed interrupts (3 missed)` | 89.4 / 274.6 MB/s | 837 / 914 IOPS |
| INTx still suppressed, second run | the same, 3 missed | 89.4 / 250.9 MB/s | 817 / 898 IOPS |
| INTx enabled again | the same, 3 missed: the latch holds for the start | 89.1 / 273.8 MB/s | 834 / 857 IOPS |
| `storintwatchdog=0` (regedit, cold start), INTx suppressed | `interrupts`: no latch | 9.6 / 9.6 MB/s | 9 / 9 IOPS (about 109 ms each: the watchdog tick) |

No request failed in any run ("failed: 0" on every page). The first four
runs did not compare their data (XHCIPERF's `/verify` was not given); the
last did (`verify=1`) and passed.

**98 SE** (`s98b`: the 46.1 overlay with this build's `stor98.mpd` copied
in; the run's data compared, `verify=1`). XHCIPERF shows no Stor98 page
there: the guest has two Stor98 adapters of the same model (the tunnelled
drive and the internal one at `00:05`), which its page cannot tell apart,
so the rates are the reading (a dead line without the latch completes about
ten requests a second, as the last row shows):

| Step | Seq 1M write / read | Rnd 4K write / read |
|---|---|---|
| interrupts, nothing suppressed | 103.3 / 292.4 MB/s | 1523 / 1837 IOPS |
| INTx suppressed, first run | 73.9 / 155.0 MB/s | 385 / 410 IOPS (2.3-2.4 ms: the 1 ms polling period) |
| INTx still suppressed, second run | 75.5 / 151.7 MB/s | 396 / 417 IOPS |
| INTx enabled again | 72.7 / 142.6 MB/s | 404 / 403 IOPS: still polling |
| `StorIntWatchdog` 0 (regedit, cold start), INTx suppressed | 10.3 / 9.8 MB/s | 10 / 9 IOPS (about 100 ms each) |

**ME** (`meb`: the 46.1 ME overlay, this build's `stor98.mpd` copied in;
one Stor98 adapter running, so the page is there; data compared):

| Step | Stor98 page's mode | Seq 1M write / read | Rnd 4K write / read |
|---|---|---|---|
| interrupts | `interrupts` | 86.8 / 251.4 MB/s | 1140 / 2149 IOPS |
| INTx suppressed, first run | `polling after missed interrupts (3 missed)` | 67.3 / 128.1 MB/s | 205 / 396 IOPS |
| INTx still suppressed, second run | the same | 66.1 / 121.8 MB/s | 203 / 399 IOPS |
| INTx enabled again | the same: the latch holds | 66.1 / 121.1 MB/s | 205 / 393 IOPS |
| `StorIntWatchdog` 0, INTx suppressed | `interrupts`: no latch | 10.1 / 9.9 MB/s | 10 / 10 IOPS |

No request failed on any page. The latch came at three misses everywhere,
as designed; the interrupt-driven runs show no miss (XP and ME's pages).

Evidence: `E:\xhci98-vm\p46\w46-stor\` (`ntxp\serial-b.log`,
`serial-c.log`, `s98b\serial-w8.log`, `serial-w9.log`, `meb\serial-m8.log`,
`serial-m9.log`; `wd9e.sh`, `wd9me.sh`, `wdnt.sh`, `wdntoff.sh`, `intx.sh`;
the kits `xf4\`, `xf4me\`; `pids.txt`).

**Not read here:** 2000 and 7 (the 7 x86 install of this build stopped
at Windows' unsigned-driver prompts, which took no keys from the monitor;
2000 not started); the shared line with QEMU's `edu` (Stor98 PLAN's leg).
All three were read later: "46.9 the owed legs" below.

## 46.5 the installer on 98 SE and ME finds a USB4 host whose router is not listed (2026-11-03, partial)

Branch `w46-inst`. Commits: `fa4c1806` (record 26 draft 63, section
5.3.6: the design), `ba743174` (the code, the host vectors and the
window's self-test); each reviewed and revised.

### The reading (q35, before the design)

The emulated ASM4242 (`launch-q35.ps1 -Usb4 asm4242`'s command line)
with the `usb4-host` behind the switch's fourth downstream port left
out: the switch present, its router missing. Fresh overlays of the 98
SE base; `HKLM\Enum\PCI` and `HKEY_DYN_DATA\Config Manager\Enum`
exported with the registry editor and sent to COM1
(`E:\xhci98-vm\p46\w46-inst\r98\dump1.txt`, `r98n\dump.txt`).

| Layout | What 98 SE lists |
|---|---|
| the root port padded as on the board (`-AsmReserve`: its prefetchable window above 4 GB) | the upstream port `1B21:2421` at problem 12, nothing below it: no `1B21:2423`, no xHCI `1B21:2426` |
| no padding | the upstream port and the four downstream ports problem 0, the xHCI problem 28, no router |

`HKLM\Enum\PCI` keeps an entry for every function 98 SE has seen before,
present or not (the base's own `2423`, `2425` and `2426` entries were
there with the router absent). A bridge's `LogConfig` holds `PCI.VXD`'s
requirement list in 32-bit ranges, and the problem 12 bridge's
`Allocation` is empty: nothing there records the firmware's window
above 4 GB, so the NT `pfw_seen` note has no 9x reading to stand on
(record 26 section 5.3.6).

### Checks and builds

- `test_inst` 8042 checks, 0 failures (the decision with
  `INST_TB_ROUTER_UNLISTED`, the ids against `cm_ports.c`'s families).
- Gated x86 `release` of `ba743174`: BUILD + GATES PASSED. Log
  `E:\xhci98-scratch\p46-w46-inst\gated-x86-ba743174.log`. Test cut
  `make-release.ps1 -Arch x86 -TestLabel w46inst` with both driver sets,
  exit 0; the base download on the transfer disk; hashes
  `E:\xhci98-vm\p46\w46-inst\hashes-ba743174.txt` (`SETUP.EXE`
  `169ec09c...`, `XHCIWIZ.EXE` `4df44cc7...`).

### The legs (the package of `ba743174`)

Fresh overlays, the ASM4242 as above with no router, QMP 49306 to
49309, every start and stop in `E:\xhci98-vm\p46\w46-inst\pids.txt`;
screens in `E:\xhci98-vm\p46\w46-inst\shots-465\`.

| System, layout | Read | Result |
|---|---|---|
| 98 SE, padded (`p98a`) | the Components page | "Thunderbolt / USB4 controller found; Windows has not listed its router yet.", the box ticked |
| 98 SE, padded | the Settings sheet's Thunderbolt / USB4 tab | the note ends "Router not listed yet: these take effect once it is." |
| 98 SE, padded | the Thunderbolt and USB4 page, the Summary, Install, Finish (restart) | `XHCIPFW.VXD` ticked; the Summary "Find controllers and devices above 4 GB (XHCIPFW.VXD): installed, loaded at next start"; the run ended "Done. Please restart Windows now." |
| 98 SE, padded, after a cold start | `Services\VxD\XHCIPFW` and the configuration manager's tree | `XhciPfwResult` 1, `XhciPfwClosed` 7, "XHCIPFW.VXD 3.4.0.0"; the upstream port and the four downstream ports problem 0 (the upstream port was at problem 12 before) |
| ME, padded (`pmea`) | the same: Components, Summary, install, cold start | the same sentence and ticked box; after the start `XhciPfwClosed` 7 and the switch's five ports problem 0 |
| 98 SE, no padding (`p98b`) | the Components page; `XHCIINST.EXE /list` | the same sentence and box; the console "  Thunderbolt / USB4 controller found; Windows has not listed its router yet." and "Enter would give [Y]." |
| 98 SE, the router present (`p98c`, control) | the Components page | "Thunderbolt / USB4 controller found.", the box unticked (record 23 revision 23, unchanged) |

### Left

- Of record 26 section 5.3.6's legs: the install and cold start with the
  switch's ports started (no padding) on 98 SE and ME; on ME the page
  with them started and with the router present.
- **The board's own case on the emulator**: a downstream port of the
  switch listed but held at problem 12 by its interrupt, the router
  behind it. The two layouts above reach the detection through the same
  rule (a present devnode of `1B21:2421` or `1B21:2423`, whatever its
  problem), but the emulator has no way to hold one downstream port back
  that way. What is needed, in `usb4-emu` (not changed here): a property
  of `usb4-asm-dsp` (or `usb4-asm-tunnel-dsp`) that makes 98 SE leave that
  one port at problem 12 through its interrupt, for example an interrupt
  pin the q35 firmware's routing does not serve, with the `usb4-host`
  behind it; record 32 section 12.4 asks the same emulator reading for
  task 46.11.
- The board: 46-E.1's plug-in half.

## 46.7 the Settings dialog split by page (2026-11-03, partial)

Branch `w46-inst`. Commits: `5fc03eeb` (record 26 draft 64, section
5.4.2: the design), `82fa341b` (the code, the Help texts, the host pin
and the window's self-test); each reviewed and revised.

### What changed

- The Components page's "Settings..." opens the sheet with the USB tab
  alone (its main rows and "Advanced..."). It is greyed where that tab
  has nothing to show; it stays open while the last plan left the USB
  drivers out or a change of the boxes is still owed to the plan (Back
  keeps the boxes without planning), and its press plans again.
- The Thunderbolt and USB4 page has its own "Settings..." at its foot,
  the rows stacked above it. It opens the Thunderbolt / USB4 tab and,
  while the page's Stor98 box is ticked, the Stor98 tab; the page's
  boxes and approval choice are taken first, as its Next takes them.
- Help: Components "Settings...: the USB settings."; the Thunderbolt and
  USB4 topic's last paragraph "Settings...: the Thunderbolt / USB4 and
  Stor98 settings.".

### Checks and builds

- `test_inst` 8042 checks, 0 failures (the Thunderbolt topic's eight
  paragraphs).
- The window self-test: the tabs each page offers (`set_tabs`), the
  Components button open and greyed in its cases, the Thunderbolt page's
  button at its foot with every row above it.
- Gated x86 `release` of `c949e2fb` (this and 46.4): BUILD + GATES
  PASSED, the window self-test 876 checks, 0 failures. Log
  `E:\xhci98-scratch\p46-w46-inst\gated-x86-c949e2fb.log`. Test cut with
  both driver sets, exit 0; hashes
  `E:\xhci98-vm\p46\w46-inst\hashes-c949e2fb.txt` (`XHCIWIZ.EXE`
  `cb509b41...`).

### The legs (the package of `c949e2fb`)

Fresh overlays, `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot`, QMP 49310 to
49312, starts and stops in `E:\xhci98-vm\p46\w46-inst\pids.txt`;
screens in `E:\xhci98-vm\p46\w46-inst\shots-467\`.

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480 | Components' Settings: the sheet with the USB tab alone, its Help (F1), Advanced; the Thunderbolt and USB4 page: its Settings button below the networking row; the sheet with the Thunderbolt / USB4 and Stor98 tabs, each tab's Help and Advanced (with its Help); `XhciCmMode` and `XhciCmSafeEject` set to 0 and OK; Stor98 unticked on the page, then Settings: the Thunderbolt tab alone, the two values kept; the Summary "Settings saved for all controllers: XhciCmMode = 0, XhciCmSafeEject = 0"; install, Finish | every window inside the screen, nothing clipped; the Thunderbolt topic's Help scrolled to its new last paragraph |
| XP, 96 DPI and 120 DPI (Large fonts) | Components' Settings (USB tab alone), the Thunderbolt and USB4 page and its Settings (the two tabs), Help on each | whole at both sizes |
| 7 x86, 96 DPI and 120 DPI, 800 by 600 | the same, and the Thunderbolt topic's Help to its end at 120 DPI | whole at both sizes |

### Left

Of section 5.4.2's legs: a USB and a Stor98 value changed and seen in
the Summary (98 SE read a Thunderbolt one); on XP and 7 Advanced on
each tab, a value changed on each, and the sheet with Stor98 unticked.

## 46.4 XHCITR's Save Snapshot without its window (2026-11-03)

Branch `w46-inst`. Commits: `f90f0359` (record 25 revision 25, section
15 item 8: the design), `c949e2fb` (the code and host vectors); each
reviewed and revised.

### What changed

File > Save Snapshot... asks only for the folder ("Save the snapshot
in:"); `/snapshot` writes its four files there under its own names from
the date and time; the window reads the name from the head of the
child's output and says in a short message box where the files went and
what the exit proves ("Snapshot saved in <folder>: <name>.DEV, .TXT,
.BIN and .PSC.", and five other messages). Until this change the message
box held the engine's whole text, taller than a 640 by 480 screen.
`XHCITR /snapshot` and Save Report are unchanged.

### Checks

`test_tr` 1598 checks, 0 failures (every exit, the name's two forms, a
missing or too long name, a folder of `MAX_PATH` characters, the
output's bound). The gated build as in 46.7 (`XHCITR.EXE` `cf115ab3...`,
`XHCITR64.EXE` `9bffc122...`).

### The legs

Screens in `E:\xhci98-vm\p46\w46-inst\shots-464\`. Each run took the
snapshot of the xhci98 controllers then running.

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480 (after this package's install) | File > Save Snapshot..., the folder C:\ | the folder dialog, then "Snapshot saved in C:\: B030240A.DEV, .TXT, .BIN and .PSC." in a one-line box |
| XP, 96 DPI (after a console install of the package) | the same | "Snapshot saved in C:\: B030238A.DEV, ..."; `dir` lists the four files |
| XP, 120 DPI (Large fonts) | the same | "Snapshot saved in C:\: B030309A.DEV, ..." |
| 7 x86, 96 and 120 DPI | the same, the user's folder | "Snapshot saved in C:\Users\test: 9201245A.DEV, ..." and, at 120 DPI, "9201249A.DEV ..." on two lines, whole |
| 7 x64 (3.2.0.0's x64 package installed, test-signed, in test mode; this tip's `XHCITR64.EXE` run from the transfer disk) | the same | "Snapshot saved in C:\Users\test: 9201252A.DEV, ..." |

The 7 x64 leg ran the new `XHCITR64.EXE` against an older driver: the
tool is not a driver and loads nothing, and the snapshot's exchange with
the driver is the same in both releases; no package of this tip's x64
drivers was signed for it.

## 46.8 the Progress page's bar by the work left (2026-11-03, partial)

Branch `w46-inst`. Commits: `7c2e0838` (record 26 draft 65, section
5.8.1: the design), `d5533a56` (the code, the design's three
amendments, the host vectors); each reviewed and revised.

### What changed

The bar's range grew with each step begun, so it showed only the steps
seen so far and sat still through the device installs and Windows' own
wizards, which report no step, and through each 98 SE helper job. Now
the engine keeps one pool of items weighted by their kind (a device
install 6, a wait 4, the copy 4, a file step 2, an other-driver set 3,
the run's end 3, anything else 1), the space left shared by the weight
to come plus a reserve that grows with the work found late. The open
item creeps by its wait's bound or by elapsed / (elapsed + 30 s); the
gap between items is an item of its own; the window reads the position
on a 500 ms timer and once more at the run's end. Each item's begin and
end is in the log as "progress: <pos>/1000".

### Checks and builds

- `test_inst` 8057 checks, 0 failures (`test_prog`: an XP-like install,
  twenty late devices still moving the bar, a bounded and an unbounded
  creep, the clock's wrap, a wait's bound changing inside one item, an
  empty run, a dropped item, the end only at done).
- Gated x86 `release` of `d5533a56`: BUILD + GATES PASSED, the window
  self-test 876 checks, 0 failures. Log
  `E:\xhci98-scratch\p46-w46-inst\gated-x86-d5533a56.log`. Test cut,
  exit 0; hashes `E:\xhci98-vm\p46\w46-inst\hashes-d5533a56.txt`
  (`XHCIWIZ.EXE` `129bc661...`).

### The legs (the package of `d5533a56`)

Fresh overlays, `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot`, QMP 49314 to
49316; a first install with the Thunderbolt support. The log's
progress lines and screenshots:
`E:\xhci98-vm\p46\w46-inst\shots-468\` (`p8a-progress.txt`,
`p8x-progress.txt`, `p87-progress.txt`).

| System | The log's positions | Result |
|---|---|---|
| 98 SE | 0 at the start; the first settle 30 to 127; the copy 150 to 242; the two controllers' helper jobs 263 to 393 and 413 to 535; nine settles and a third helper job up to 856; `S98VSD.VXD` and `XHCIPFW.VXD` 859 to 869; the run's end 870 to 876; 1000 at done | never back; each item's begin above the previous end (the gap creeps); the bar moved during the helper jobs (`98se-bar-helper.png`) |
| 7 x86 | 0; the copy 78 to 129; the controllers 141 to 298; thirteen waits, the third controller, the tunnel ports' filters and Stor98's device up to 755; `XHCIPFW.SYS` 758 to 764; the run's end 767 to 777; 1000 at done | the same; "Done. Please restart Windows now." |
| XP | 0 to 350 through the copy, the two controllers and a wait, then the run's end 360 to 381 and 1000 | the run was stopped there by a keystroke meant for Windows' signing prompt that reached the page's Cancel (exit 4): the bar went to the end with the run's end |

### Found

- **The end still jumps**: from 876 on 98 SE and 777 on 7 to 1000 at
  the run's end. The jump is the reserve, which section 5.8.1 never
  spends and grows with each item found late; on these runs the waits
  are found late (thirteen on 7), so the reserve is large by the end. A
  refinement for the next round: the run's end, which is always the last
  item, takes the reserve and what is left as its share, so the bar
  creeps to the end through it; or the plan counts a settle per planned
  device. Not made here.

### Left

The final jump above; the XP leg again with the run not stopped.

## 46.11 Step 2: a downstream port whose line no routing serves, in the emulator (2026-11-03)

Record 32 section 12.9. The 98 SE without ACPI of "46.12" above (an
overlay of `E:\xhci98-vm\p46\w46-cm2\lg\disk.qcow2`: the published
`3.4.0.0` with this tree's `qemu` `xhcicm.sys` of `603f6329`), the
ASM4242 model with the handover mailbox and `pm-cap=on` (`usb4-emu`
`e427aac`), q35, TCG, one vCPU. QEMU's `info pci`: the root port 00:03.0,
the switch's upstream port 01:00.0, the downstream ports 02:00.0 to
02:03.0, the xHCI 63:00.0 behind 02:02.0, the NHI 64:00.0 behind
02:03.0, every port and both functions Interrupt Pin A, SeaBIOS's lines
11 and 10. Device Manager, PCI bus, IRQ Steering: "IRQ Steering
Disabled. IRQ Table has some errors." A test-only program from
`AUTOEXEC.BAT` (four port writes per line through `0CF8h`/`0CFCh`, made
for this leg and kept outside the tree) wrote the Interrupt Line of
02:03.0 and of 64:00.0 before Windows; each case a cold start.

| Case | The lines | 02:03.0 | 64:00.0 | `xhcicm` |
|---|---|---|---|---|
| r11 a | SeaBIOS's (11) | working | "xHCI98 USB4 Router", IRQ 11 | the handover, the router |
| r11 c | `FFh` | working; its line read `FFh` after the start | started, no IRQ resource (`cm.irq.resource=00000000`); line `FFh` | `cm.osc.handover=00000100`, `u4.host.id=174C2428` |
| r11 d | 4 (COM1's) | Code 12, "This device cannot find any free Interrupt Request (IRQ) resources to use"; nothing below it | not listed | not loaded (no record) |

Evidence: `E:\xhci98-vm\p46\w46-cm2\r11\` (`debugcon-c.log`,
`s1.png` to `s14.png`: s11 the tree with the port's problem, s13 its
Code 12, s14 the IRQ Steering page), `pids.txt`.

Left: the step's code waits for the owner's Q1 (record 32 section 12.6;
recommendation A, always, with this reading); steering enabled and ME
are not read; the board's `SETLN3.COM` reading stays the owner's.

## 46.6 A device holding the controller's line, named at the start (2026-11-03)

Design record 15 section 4.4 (revision 9); code `40fb2fd9`. Builds of
`577f5825`: x86 `all` and amd64 `release` gated, passed, the host
suites with them (`E:\xhci98-scratch\p46-w46-cm2\gated-x86-all.log`,
`gated-amd64-release.log`). The vehicle is 45.33's ("45.33" in
`runs/run-45.md`): q35 98 SE (the ACPI base), `qemu-xhci` at 01:00.0
with a `pci-serial` at 01:00.1 on the same INTA (`multifunction=on`),
the serial function armed from `AUTOEXEC.BAT` (`DEBUG`, its Interrupt
Enable register 02h) so it holds IRQ 10 with no driver. The published
`3.4.0.0` installed (`XHCIINST /dir 98-me /yes /nopause`), then this
build's `qemu` `xhci98.sys` (`36dce3e1...`) and `XHCITR.EXE`
(`d86c7d37...`) copied over it (the install's own copy pending at the
restart put `3.4.0.0`'s back once; copied again after it), and
`XhciInterruptMode` 2 (polling, so the held line stays masked).
`XHCITR /dump`, its text to COM1:

| Leg | The holder | "line held by another" | Counters | Log |
|---|---|---|---|---|
| s6 | armed | `likely: 01:00.1 1B36:0002 seen asserting IRQ 10 at every read (8 functions read)` | `HeldScan` 2, `HeldFunctions` 8, `HeldBdf` 257, `HeldId` 00021B36h, `HeldIrq` 10; 264 words | `intr.held.ms=0000003C`, `intr.held=00010102`, `intr.held.id=00021B36` |
| s6n | not armed | `no: IRQ 10, 8 functions read, 0 not confirmed` | `HeldScan` 1, `HeldFunctions` 8, `HeldIrq` 10 | `intr.held.ms=00000104`, `intr.held=00000001` |

The start otherwise as before (the delivery POLLING, reason "forced
polling (XhciInterruptMode 2)"). The walk cost 60 ms with the holder
(found at bus 1) and 260 ms without (every bus read), on TCG. Not read:
mode 0 with the holder (45.33's storm), ME, 2000 or later ("not looked
for"), and the walk on a board. Evidence:
`E:\xhci98-vm\p46\w46-cm2\s6\` and `s6n\` (`serial-c.log`, `pids.txt`).

## 46.13 The connect and drop loop's log points (2026-11-03)

Design record 21 section 8.2 (draft 5); code `577f5825`, built with
46.6's. The non-ACPI 98 SE of "46.12" (an overlay of
`lg\disk.qcow2`) with this build's `qemu` `xhcicm.sys`
(`c3142fa6...`) and `XHCITR.EXE`, the ORICO model hot-plugged at the
desktop, unplugged and plugged back after 1 s, then after 8 s. The debug
console (`E:\xhci98-vm\p46\w46-cm2\t13\debugcon-b.log`) and
`XHCITR /report` (its text in `serial-b.log`):

- `cm.ms` stamps before the records (65 in the session), `u4.connect`
  at each connection (`00010001`, `00010002`, ...);
- the 9x numbering's `cm.brnum.9x.link`: the tunnel port 02:00.0
  `2011h` (Data Link Layer Link Active), the enclosure's bridges
  03:00.0 `1011h` and 04:00.0 `3023h`;
- at each DOWN, `cm.vanish.sweep.gen=20000001` (a DOWN, member 0,
  generation 1) and `.now`; each reading's `.at`, `.end`, `.tag`
  (`00010001`: reading 1 for generation 1) and `.gen`; the removals'
  `cm.vanish.cr`, `cm.vanish.cr.from=00010001` and
  `cm.vanish.cr.at=85000000` (a hot-added function's path form);
- the report's log beginning with the head (`cm.ms`, `u4.connect`,
  ...) and `cm.log.head=00000FED` (4077 bytes); the `qemu` flavour
  hands the ring to the debug console as it goes, so no ring records
  followed.

The case the task names was not constructed: at both gaps the DOWN's
sweep read, removed and retired its children before the next
`u4.connect`. The guard (record 21 section 8.2, "What would prove the
rule") stays owed to a log from the board.

## 46.16 The variants, in the emulator (2026-11-03, not completed)

The published `3.4.0.0` on w46-cm's `golden340.qcow2` (98 SE, ACPI),
the ORICO model and a stick, a script typing into a DOS prompt
(`E:\xhci98-vm\p46\w46-cm2\v1\`). The run is not a reading: the
drive's AutoPlay opens an Explorer window at each plug, which took the
keys meant for the prompt, so most screenshots show the prompt as it
was before. Seen once, before the variants proper (after a surprise
removal and a plug back): the drive listed as E: and F:, the same
volume serial. Not run as designed: a proper eject then the stick, the
drive pulled while Explorer has it open, the stick while the drive is
out, a pull after a large copy. Left as the roadmap line says.

## The UNMAP probe: does Windows 8 to 10 send UNMAP to a SCSIport disk? (2026-11-03)

The question first: whether Windows 8, 8.1 and 10, given a disk on Stor98
(a SCSIport miniport) that reports logical block provisioning, read the
provisioning pages and send SCSI UNMAP at all. **They do not, by any route
tried, on all three. The owner's decision of 2026-11-03 for that outcome:
no UNMAP, no advertisement, the task and its design removed, nothing
planned.**

**The image.** A test-only debug-port build of Stor98 (x86 and amd64),
made from this branch's tree with a reporting-only change that was never
committed and has been removed: at the bring-up's Identify it offered
logical block provisioning on any NT version when the controller reports
Dataset Management (Identify Controller ONCS bit 2): INQUIRY VPD 00h
listing B0h and B2h (00h 80h 83h B0h B2h C9h), VPD B0h (Block Limits:
MAXIMUM UNMAP LBA COUNT 3FFFFFh, MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT 256),
VPD B2h (LBPU, LBPRZ from DLFEAT 001b) and READ CAPACITY(16) byte 14
(LBPME, LBPRZ). An UNMAP would have been checked, logged and answered GOOD
with no NVMe command. Every CDB other than READ, WRITE and TEST UNIT READY
was logged on the debug console (its first three bytes and transfer
length). x86 `stor98.sys` SHA-256 `021e3d9c...71760`, amd64 `b3ea119c...c5304d`.

**The vehicle.** The q35 bases on the pinned emulator (`qemu-system-x86_64.exe`
`da60139f...0ce93`), the ASM4242 layout with the ORICO model's drive (QEMU's
`nvme`) tunnelled at power-on, its backing drive with `discard=unmap` and
`detect-zeroes=off`, `usb4-debug-port`. 8 x86 (`b8e091bd...`): the `f303b42c`
package installed (`INSTALL /console /yes /nopause /thunderbolt /stor98`,
exit 3, Stor98 started on the tunnelled drive), then the test image copied
over `stor98.sys`, a cold start. 8.1 x64 (`27776857...`) and 10 x64
(`915dd350...`): Stor98 alone, the test image and a catalog signed inside
each guest with a throwaway certificate made and trusted there (Root and
TrustedPublisher; test signing on); the INF's copy in the guest matched the
QEMU drive's hardware ID, since the inbox `stornvme` outranks the
Authenticode-signed compatible-ID match (setupapi: Stor98 rank `0x00ff3006`,
`stornvme` `0x00ff2006`, selected), then a restart. In each the volume was
formatted NTFS (128 MB, 4 KiB clusters); then, in a new folder, a copy of
41 files (about 3 MB; 8 x86 only: the 8.1 and 10 kit had no such folder,
"0 File(s) copied") and a file of 624 KiB (four copies of XHCIPERF
joined), `fsutil sparse setflag` and `setrange` over that file's first MiB
and `fsutil file setzerodata` over the first MiB of a 312 KiB one (8 x86's
second run; its first had neither), the folder deleted (`rd /s /q`), and
`defrag <drive> /L /U /V` (the retrim that Optimize Drives runs).

| | 8 x86 (6.2.9200) | 8.1 x64 (6.3.9600) | 10 x64 (10.0.19045) |
|---|---|---|---|
| The offer (debug-port stage `162h`) | `34095d0b`: offered, ONCS `5Dh`, DLFEAT `09h`, NSFEAT `34h` | the same | the same |
| INQUIRY VPD 00h, 80h, 83h | read once each | read once each | read once each |
| VPD B0h, B2h | never read | never read | never read |
| READ CAPACITY(16) | never sent (READ CAPACITY(10), 8 to 11 times) | never sent | never sent |
| UNMAP | never sent | never sent | never sent |
| Other CDBs | MODE SENSE(6), SYNCHRONIZE CACHE | the same | the same |
| `fsutil behavior query DisableDeleteNotify` | 0 | 0 | NTFS 0, ReFS 0 |
| `fsutil fsinfo sectorinfo` | "Trim Not Supported" | "Trim Not Supported" | "Trim Not Supported"; `ntfsinfo`: "Max Device Trim Extent Count : 0", "Max Device Trim Byte Count : 0" |
| `defrag /L` | "The operation requested is not supported by the hardware backing the volume. (0x8900002A)" | the same | the same |

So the class driver of these three systems asks a SCSIport disk for none of
the provisioning data, whatever the disk reports, and file system deletion,
the sparse and zeroing calls and the retrim send nothing. The host's
`qemu-img map` was not read: with no UNMAP issued there is nothing to see.

Evidence: `E:\xhci98-vm\p46\w46-stor2\` (`w8p\debugcon-b.log`,
`debugcon-c.log`, `serial-b.log`, `serial-c.log`; `w81p\` and `w10x\`
`debugcon-b.log`, `serial-a.log`, `serial-b.log`; `cmdline-*.txt`; the kits
`xp1\` and `xp64\` with `LB.BAT`, `SETUP.BAT`, `P2.BAT`; `pids.txt`), the
images in `E:\xhci98-scratch\p46-w46-stor2\img\`.

## 46.9 the owed legs: 2000, 7 x86 and the shared line (2026-11-03)

The same build and vehicle as "46.9" above: the gated x86 `release` build
of `f303b42c` (`stor98.mpd` `c21d0ce4...`, x86 `stor98.sys` `2928f267...`,
XHCIPERF `e85bcb30...`; Stor98's source is unchanged since), the q35
bases on the pinned emulator, the ASM4242 layout with the ORICO model's
drive tunnelled at power-on, fresh overlays, `INSTALL /console /yes
/nopause /thunderbolt /stor98` (exit 3 on both), each reading one
XHCIPERF run with `/verify` (32 MiB, sequential 1 MiB and random 4 KiB,
read and write, two passes of 5 s) with its Stor98 page sent to COM1, the
interrupt suppressed with `intx.sh` as above.

- **2000** (`7fc9714e...`): Stor98 was left at the install ("its location
  could not be read") and installed at the next start through Windows'
  own wizard, as the installer says. On 7 x86 (`95893177...`) the
  unsigned-driver prompts took no keys, so each "Install this driver
  software anyway" was clicked with QEMU's PS/2 mouse (relative moves
  from the screen's corner); Stor98 "started" in the install's report.

| Step | 2000: page's mode | 2000: seq 1M write / read; rnd 4K write / read | 7 x86: page's mode | 7 x86: seq 1M write / read; rnd 4K write / read |
|---|---|---|---|---|
| interrupts, nothing suppressed | `interrupts` | 101.3 / 516.9 MB/s; 3005 / 4334 IOPS | `interrupts` | 90.3 / 544.5 MB/s; 2548 / 2917 IOPS |
| INTx suppressed, first run | `polling after missed interrupts (3 missed)` | 83.3 / 445.5 MB/s; 2756 / 4356 IOPS | the same, 3 missed | 93.5 / 628.7 MB/s; 3711 / 4712 IOPS |
| INTx still suppressed, second run | the same | 84.5 / 454.6 MB/s; 3406 / 4348 IOPS | the same | 93.8 / 506.7 MB/s; 3566 / 4610 IOPS |
| INTx enabled again | the same: the latch holds | 81.4 / 483.9 MB/s; 3346 / 4982 IOPS | the same | 105.1 / 666.1 MB/s; 3847 / 5119 IOPS |
| `storintwatchdog=0` (regedit, cold start), INTx suppressed | `interrupts`: no latch | 10.5 / 10.5 MB/s; 10 / 10 IOPS (about 100 ms each) | `interrupts`: no latch | 9.6 / 9.6 MB/s; 9 / 9 IOPS (about 109 ms each) |

Every run's data compared (`verified`) and no request failed ("failed: 0"
on every page). The command word read back `0507` from `0107` (2000) and
`0506` from `0106` (7) while suppressed.

**The shared line, 98 SE** (`ab1d4d65...`, a fresh overlay, this build
installed, `XHCIINST.EXE /dir 98-me /yes /nopause /thunderbolt /stor98`
exit 3; then the debug-port build of the same source, `stor98.mpd`
`3c7f931a...`, copied over the installed one so the stages are logged;
the first two rows in one start, each later row from a cold start). QEMU's `edu` at `00:07.0` with no driver:
98 SE gave it IRQ 11, and the tunnelled drive (bus 5, behind the root
port at `00:03.0`) IRQ 11 too. `edu.py` (a client of QEMU's gdbstub in physical-memory mode)
raised `edu`'s interrupt (1 to BAR0+60h), held it 1 ms with the guest
running, acknowledged it (1 to BAR0+64h), and waited 30 ms: about 30
cycles a second, bounded, while XHCIPERF ran on the drive. The guest is
stopped only for each register write; keys typed while the client runs
are lost, so XHCIPERF was started first. QEMU's monitor must not stop the
guest while the client is attached (one such mix left the guest paused
about ten minutes; that run is not counted), so in the fourth and last
rows below the client itself set the drive's Interrupt Disable bit with a 16-bit write
to its command register through the q35 ECAM window (`B0500004h`,
`0107` to `0507`), and cleared it at the end.

| Leg | Stages (debug port) | Page's mode | Seq 1M write / read; rnd 4K write / read |
|---|---|---|---|
| no `edu`, interrupts | none of `DBh`, `157h`, `50h` | `interrupts` | 67.7 / 184.6 MB/s; 780 / 777 IOPS |
| `edu` cycling, the drive's interrupt working | `DBh` action 1 (the vector masked, polling) once, action 3 (the unmask trial) once, action 4 (the storm went on masked) 16 times; no `157h`, no `50h` | `interrupts` | 59.9 / 155.4 MB/s; 611 / 527 IOPS |
| the drive's INTx suppressed, then `edu` cycling (it began about 30 s later) | `157h` `02050003`: the latch, 3 misses, already logged when `edu`'s cycles began; then no `DBh` (once latched an empty call does no storm accounting) | `polling after missed interrupts (3 missed)` | 63.0 / 114.7 MB/s; 322 / 362 IOPS |
| `edu` cycling first, the drive's INTx suppressed 5 s in | `DBh` action 1 once, action 4 29 times; no `157h`: the storm's mask came first, and with the vector masked the watchdog is not armed | `interrupts` | 61.0 / 37.9 MB/s; 39 / 38 IOPS (about 26 ms each) |
| `edu` with the shortest hold (raised and acknowledged at once: only the client's own round trip), every 100 ms, no suppression (the control) | none of `DBh`, `157h`, `50h` | `interrupts` | 70.2 / 166.9 MB/s; 730 / 770 IOPS |
| the same `edu` cycling first, the drive's INTx suppressed 5.7 s in | `157h` `02690006`: the latch at **6** misses; no `DBh` | `polling after missed interrupts (6 missed)` | 63.6 / 112.2 MB/s; 308 / 331 IOPS |

Every run's data compared, no request failed, and no controller reset
followed the empty interrupts (no stage `50h` or `87h` in any leg). Read
against the PLAN's shared-line paragraph: in this emulator a line held for
1 ms is called far more than once (a level line re-delivered), so `edu`'s
cycles made an interrupt storm rather than single shared calls, and the
storm's mask, not a claim, is what kept the watchdog from latching in the
fourth row. With the shortest hold (the last two rows, each from a cold
start) there was no storm: alone, `edu`'s calls changed nothing visible
(no stage, `interrupts`); with the drive's line suppressed the watchdog
latched at 6 misses, where every other suppression in this phase latched
at 3. That does not show claims on `edu`'s calls holding the latch off:
the miss count is the start's, so misses on the working line before the
suppression, each run set back by the drive's own interrupt, give the same
record, and the TCG clock does not place the latch against the
suppression. **The shared-line claim reading is inconclusive.** Settling
it needs a test image that logs the watchdog's run, claim count and misses
at each counted timer callback around the suppression.

Evidence: `E:\xhci98-vm\p46\w46-stor2\` (`n2k\` and `n7\`
`serial-b.log`, `serial-c.log`, `wdnt.txt`, `wdoff.txt`; `s98\`
`serial-c.log` to `serial-f.log`, `debugcon-c.log` to `debugcon-f.log`,
`serial-g.log`, `serial-h.log`, `debugcon-g.log`, `debugcon-h.log`,
`edu-e1.log`, `edu-e2.log`, `edu-e4.log` to `edu-e6.log`, `e2-intx.txt`; the
scripts
`edu.py`, `rel.py`, `intx.sh`, `wdnt.sh`, `wdntoff.sh`, `wd2koff.sh`,
`start9.sh`, `wdsum.py`; the kit `xw\`; `pids.txt`).

## 46.8 the run's end takes the reserve (2026-11-03, w46-inst2)

Branch `w46-inst2`. Commits: `f686a4ca` (record 26 draft 66, section
5.8.1: the owner's decision of 2026-11-03, the run's last item takes the
reserve), `f0fade2a` (the code and the host vectors); each reviewed and
revised.

### What changed

`inst_prog_last` begins the run's last item, "the run's end", with all
the space left as its share; the install begins it as before, and the
uninstall now has one too. A step begun inside it (the mark service's
install at the run's end) is part of it: the log says "part" at its
begin and end and the bar's item stays the run's end, whose creep its
waits move. Its end brings the bar to 999 and done to 1000. Found while
writing the vectors: `inst_prog_begin` read the space left before ending
an item still open, so that item's share was counted twice; it now reads
it after (the engine always ended the open item itself first, so no run
was affected).

### Checks and builds

- The merged tip `d82917e4` first: gated x86 `release` PASSED, `test_inst`
  8067 checks, the window self-test 876 checks, 0 failures
  (`E:\xhci98-scratch\p46-w46-inst2\gated-x86-d82917e4.log`).
- `test_inst` 8085 checks, 0 failures (the run's end after no late items
  and after twenty, a wait inside it, an item after it finding no space,
  done after it; the Stor98 range vectors of `c59742f7` included).
- Gated x86 `release` of `f0fade2a` and of `c59742f7` (with the Stor98
  range fix of "46.7" below): both PASSED, the window self-test 876
  checks, 0 failures (`gated-x86-f0fade2a.log`, `gated-x86-c59742f7.log`
  there). Test cut with both driver sets, exit 0; hashes
  `E:\xhci98-vm\p46\w46-inst2\hashes-c59742f7.txt` (`SETUP.EXE`
  `903ecbc5...`, `XHCIWIZ.EXE` `d34684c0...`).

### The legs (the package of `c59742f7`)

Fresh overlays of the q35 bases, `launch-q35.ps1 -Usb4 asm4242
-EncAtBoot` (the 46.5 layouts for the two "no padding" rows), QMP 49300
to 49305, every start and stop in
`E:\xhci98-vm\p46\w46-inst2\pids.txt`; a first install with the
Thunderbolt support. The log's progress lines:
`E:\xhci98-vm\p46\w46-inst2\shots\` (`l98\progress.txt`,
`lxp\progress-run12.txt` and `progress-run3.txt`, `l7\progress.txt`).

| System | The log's positions | The run's end |
|---|---|---|
| 98 SE, 640 by 480 (a USB and a Stor98 value set, "46.7" below) | 0; the copy 150 to 242; the two controllers' helper jobs 263 to 393 and 413 to 535; nine settles and the third helper job up to 856; `S98VSD.VXD` 859 to 863; `XHCIPFW.VXD` 865 to 869 | 870 to 999, then 1000 at done; exit 3 |
| 98 SE, no padding ("46.5" below) | the same kinds up to `XHCIPFW.VXD` 878 to 882 | 884 to 999, 1000; exit 3 |
| ME, no padding ("46.5" below) | the same kinds up to `XHCIPFW.VXD` 779 to 787 | 791 to 999, 1000; exit 3 |
| XP, first install | 0 to 350 through the copy, the two controllers and a wait that ran its 10 minutes (a Found New Hardware window hidden behind the screen saver) | 360 to 999, 1000; exit 4, "Stopped before all devices were done" |
| XP, Setup again to its end (a file of `XHCIPFW.SYS`'s name planted, "46.18" below) | 0; the copy 85 to 140; nine waits, the third controller and two tunnel-port filters up to 647; `XHCIPFW.SYS` 652 to 663 (skipped: a file with its name exists) | 668 to 999, 1000; exit 4 |
| 7 x86 at 120 DPI, 800 by 600 (a value on each tab, "46.7" below) | 0; the copy 78 to 129; the controllers 141 to 298; ten waits, the third controller, the filters and Stor98's device up to 755; `XHCIPFW.SYS` 758 to 764 | 767 to 999, 1000; exit 3 |

Every position at or above the one before it; every item's begin above
the previous end. No "part" line appeared: in these runs no step began
inside the run's end.

### Found

- **The jump is still there, now at the run's end's own end**: 870 to
  999 on 98 SE, 791 to 999 on ME, 767 to 999 on 7, 668 to 999 on XP
  (360 to 999 on the run that stopped). The run's end lasts a second or
  two, so its creep does not show, and the reserve it takes is as large
  as before: it grows by every item found late, and the waits are all
  found late (ten on 98 SE and XP, eleven on 7). Done itself is now one
  thousandth. Whether that is the "normal step" the owner meant is the
  owner's to say.
- The XP first install stopped after its 10-minute wait: Windows' Found
  New Hardware window for a root hub was hidden by XP's screen saver,
  which the keystroke driving had let start; Setup said so and exited 4,
  as designed. Setup run again, as it asked, went to its end.

## 46.5 the legs left (2026-11-03, w46-inst2)

The package of `c59742f7` ("46.8" above); the layouts of "46.5" above
(`tools\lnr.ps1` from `E:\xhci98-vm\p46\w46-inst\tools\` copied);
screens in `E:\xhci98-vm\p46\w46-inst2\shots\` (`n98\`, `nme\`, `meh\`,
`r12\`), the registry readings sent to COM1 (`N98\serial-*.log`,
`NME\serial-*.log`, `R12\serial-a.log`).

| System, layout | Read | Result |
|---|---|---|
| 98 SE, no padding (`N98`) | the Components page; the install; Finish with Restart later, Shut Down, a cold start | "Thunderbolt / USB4 controller found; Windows has not listed its router yet.", the box ticked; the Summary with `XHCIPFW.VXD` "installed, loaded at next start"; after the start the switch's five ports problem 0, both xHCI controllers started with their root hubs |
| ME, no padding (`NME`) | the same | the same sentence and box (`nme\comp.png`); after the first cold start the switch's ports problem 0 and both xHCI controllers at problem 2; after a second cold start both started with their root hubs |
| ME, the router present (`MEH`) | the Components page | "Thunderbolt / USB4 controller found.", the box unticked |
| 98 SE, the board's case (`R12`) | the Components page | below |

**The board's case on the emulator.** "46.11" above found the way: a
test-only program from `AUTOEXEC.BAT` writes Interrupt Line 4 to the
ASM4242's fourth downstream port (02:03.0, the router behind it) before
Windows, and 98 SE leaves that port at Code 12 with nothing below it.
On an overlay of that leg's disk (`E:\xhci98-vm\p46\w46-cm2\r11\`: the
non-ACPI 98 SE with the published `3.4.0.0`; its emulator build copied)
the configuration manager listed the upstream port and three downstream
ports at problem 0 and `1B21:2423` at `18001800` at problem 12, the
router absent; this package's Setup then said "Thunderbolt / USB4
controller found; Windows has not listed its router yet." with the box
ticked (`r12\comp.png`; the guest already had `xhcicm` from `3.4.0.0`,
so the box's state is not a first install's). Nothing was installed.

**`XHCIPFW.VXD`'s log, not written** (outside this task's code). After
the cold starts above `Services\VxD\XHCIPFW` held `StaticVxD` and
`Start` alone, on 98 SE and ME, with and without the padding, although
the padded layout's upstream port came up at problem 0 (so the VxD's
scan ran and closed the window). The image this worktree's build made
(`d3c67c14...`; the sources identical to `ba743174`'s) has on its page 6
thirteen fixups, the value names `PfwLog` passes, with 32-bit targets
below the object's start (`FFFFF0xxh`) where the image of `ba743174`
(`90cf7fa5...`) has 16-bit targets 2884 to 3092. Read on one 98 SE
guest: the older image copied over this one, a cold start, the values
there (`XhciPfwResult` 1, `XhciPfwClosed` 7, the version); this build's
image back, `XhciPfwResult` and `XhciPfwVersion` deleted, a cold start,
neither written again. Rebuilding `src\pfw` in this worktree gives the
same image every time (also after touching its sources); the same files
copied to four other paths give the good one; of the other worktrees'
builds `w46-cm3`'s is bad as well and those of `w46-inst`, `w46-cm`,
`w46-cm2`, `w46-cm4`, `w46-stor` and `w46-mx86` good; the published
`3.4.0.0` image is good. The objects differ only in their source path
and timestamps, and the image passed its gates.

Left: the board (46-E.1's plug-in half).

## 46.7 the per-tab checks left (2026-11-03, w46-inst2)

The package of `c59742f7`; screens in
`E:\xhci98-vm\p46\w46-inst2\shots\` (`l98\`, `lxp\`, `l7\`).

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480 | the USB tab's `XhciSilentInstall` set to 1, the Stor98 tab's `StorPollMode` set to 1; Advanced on both; the Summary | "Settings saved for all controllers: - XhciSilentInstall = 1" and "Stor98 settings: - StorPollMode = 1" |
| XP, 96 DPI | Advanced on the USB tab (`XhciLogVerbosity` 3), on the Thunderbolt / USB4 tab (`XhciCmPollIdleMs` 500) and on the Stor98 tab (`stormaxtransferkb` 128); Stor98 unticked on the page, then Settings: the Thunderbolt / USB4 tab alone; ticked again; the Summary | the three values under "Settings saved for all controllers" and "Stor98 settings", the Stor98 value kept through the untick |
| 7 x86, 120 DPI, 800 by 600 | the same three Advanced lists and values; the Thunderbolt tab alone with Stor98 unticked, and its Help; the Summary | the same; every window inside the screen |

Seen: Stor98's Advanced read "Allowed: 0 to 1 ." for `StorIntWatchdog`;
`c59742f7` corrects it (a value with no unit has no space before the
full stop), read on this package as "Allowed: 0 to 1." on 98 SE, XP and
7. In the Settings sheet Enter presses "Advanced...", the sheet's
default button, not OK (98 SE, XP).

## 46.18 the Steps page on 2000 and later (2026-11-03, w46-inst2)

The Steps page is shown when the engine's steps text is not empty
(`inst_eng_steps_text`), and only `steps_9x`, the 98 SE and ME hand
steps of record 26 section 7, writes that text; on 2000 and later what
is left for the user (a file step refused, a device not reached) is in
the Finish page's text under its "Not everything is done" line. So the
page and its Help are 98 SE and ME's alone, as section 7 says, and
"46.18" above read them there. Read on XP to confirm it: a file of
`XHCIPFW.SYS`'s names planted in the drivers folder before Setup
(`xhcipfw.sys` and `xhcipff.sys`, copies of `null.sys`); the run's
`XHCIPFW.SYS` step was skipped ("a file with its name exists"), and the
window went from Progress to Finish ("Not everything is done: follow
the steps above first, then restart Windows.") with no Steps page
(`lxp\q.png`). Also read: the Progress page on 7 x86 at 120 DPI at 800
by 600, whole (`l7\progress-120dpi.png`), the one page "46.18" above had
not read there.

## 46.20 The 98 SE faults inside VMM (2026-11-03, partial)

The board's faults are in "46.9 related" above (w46-stor's readings: the
port's `RequestTimerCall` of 0, no `HwInterrupt` inside `HwStartIo` or the
timer routine, 12 load passes and four replugs without a fault). This pass
went after the leads left there, in the emulator only. Development host A,
QEMU TCG; **no reproduction, and no defect found** in what was read and
measured. Method: STOP screens, object and offset, register and stack data
only; no system binary was read.

**The vehicle.** A fresh overlay of the non-ACPI 98 SE image of the 46.12
legs (`lg\disk.qcow2`, `647594d4...`: 98 SE set up without ACPI, the
3.4.0.0 package with `/thunderbolt /stor98`, so `S98VSD.VXD` is in
`IOSUBSYS`), on that leg's emulator (`97cb422e...`), the ASM4242 layout
with the ORICO model's drive tunnelled at power-on by the firmware model,
one vCPU, 512 MiB, `usb4-debug-port`. Stor98 in interrupt mode (the
drive's page: "interrupts"). Load: XHCIPERF (`e85bcb30...`), 32 MiB
unbuffered write-through, sequential 1 MiB at one thread, sequential
8 MiB at two threads (over Stor98's 2 MiB maximum, so IOS cuts them),
random 4 KiB at four threads: 74.4 / 184.9, 40.2 / 58.1 MB/s and
773 / 939 IOPS (write / read), no request failed. The board ran at about
1 GB/s; these are TCG rates.

**WHPX.** The host offers it (`-accel whpx` accepted), but 98 SE does not
start under it: "While initializing device IOS: Error: An I/O subsystem
driver failed to load", with `-cpu pentium3` and `-cpu host`, and the same
on the plain non-ACPI image with none of this project's drivers. So the
board's rates are not reachable this way.

**A test image of the 9x path** (`STOR98_TEST_VMMPROBE`, debug-port 9x
build only, never staged; `src/stor98/src/vmmprobe.h`, stages
`0x180`-`0x188`): it counts, for every adapter together, each entry of
`HwStartIo`, the timer routine and `HwInterrupt` made while another (or the
same) is in progress, reads EFLAGS.IF at each entry, the stack headroom
below ESP (to the first page not present, from VMM's `_CopyPageTable`,
32 KiB looked at), the arena of every READ/WRITE buffer and
scatter/gather fragment, any bounce copy to or from memory outside the
system arena made while `HwInterrupt` or the timer runs, and from which
callback each of its `RequestTimerCall`s is made. Image `d58811ac...` for the
readings below (`e46194df...` the same code with the two options below
added); 18 boots, the load above run three times.

| Reading | Result |
|---|---|
| A callback entered while another is in progress | never (no `0x180` in any boot) |
| EFLAGS.IF at entry | set for all three, `HwInterrupt` included: the port runs them with interrupts enabled, and still never nested them |
| Stack headroom at entry, least | 15.5 KiB under the load (`HwStartIo`, `HwInterrupt`, the timer alike); 11.4 KiB in one boot; 7.3 KiB at a shutdown (`HwStartIo`, `HwInterrupt`) |
| Buffer arena | every buffer and every fragment in the system arena (at or above `C0000000h`): IOS hands the miniport global addresses for XHCIPERF's buffers, which are `VirtualAlloc` memory, so no copy or translation depends on the memory context at interrupt time |
| Bounce copies at interrupt or timer time outside the system arena | none |
| `RequestTimerCall` (those of `EngTimerEnsure`; the image read did not count `EngStop`'s cancel, which the reviewed version does) | from `HwStartIo` and the timer routine during the load; from `HwInterrupt` only at a shutdown (the completed SHUTDOWN's Flush, `EngShutdownFlushed`: a cancel and a request) |

**Where two VMM services fault** (`STOR98_TEST_VMMMAP`, the same image
class: at the 3000th `HwStartIo` one service is called with a pointer into
a system-arena page whose page-table entry is 0): `_CopyPageTable` storing
there, "An exception 0E has occurred at 0028:C000855C"; `Cancel_Time_Out`
on such a handle, "0028:C0003E8C"; both "called from 0028:C003BC15";
`_HeapFree` of such a block returned without a fault. These two faulting
instructions are not at the board's VMM(01)+139F4 and +13B2E
(`C00149F4`, `C0014B2E`) or in VMM(06). That rules out only these two
instructions, not the services: other paths of theirs (and the board's
VMM build) may differ, so both stay possible leads.

**The refused start** (`STOR98_TEST_FINDNONE`: the port's range moved
16 KiB past BAR0, as the board's `StorFind` read it; image `b6869322...`;
the reviewed version also forces the refusal whatever the shifted range
matches): with the drive at
power-on the find refused (`STG_FIND_RESULT` 0, NOT_FOUND), and three
Ctrl+Alt+Del gave Close Program each time, no fault. A hot plug into a
running guest did not bring the drive in this kit (the connection manager
logged `u4.failed=0000000C`, no tunnel), so the refused **plug-in** is not
covered.

**Also seen**, once: "A fatal exception 0E has occurred at
017F:BFF86EBD" (a 32-bit component) at the start after QEMU had been
stopped at a hanging shutdown ("Windows is shutting down", known on 98 SE
with `xhcicm`); the next start came up in safe mode, the ones after it
normally. The board's earlier `01CF:BFF72170` is in the same 32-bit
range; here it followed an unclean stop, not a load.

What this leaves for the board (46-E.1): the faults came at the board's
rates, which the emulator cannot reach on 98 SE. Not read here: xhci98's
and `xhcicm`'s 9x paths under the same load (their timers and waits), and
the refused plug-in with the registry a Registry Checker restore leaves.

Evidence: `E:\xhci98-vm\p46\w46-vmm\` (`v1\debugcon-*.log`,
`serial-t7.log`, `m1c-c.png`, `m2b-c.png`, `m1-a.png`, `n2-cad*.png`,
`b1.png` (WHPX), `pids.txt`; `vp.py` decodes the stages; the images
`xf\PM.MPD`, `PMAP1-3.MPD`, `PMN.MPD`).

## 46.6 The ME leg (2026-11-03)

The build and vehicle of "46.6" above, on ME: the q35 ME base (system
disk on a `piix3-ide`), `qemu-xhci` at 01:00.0 with a `pci-serial` at
01:00.1 on the same INTA (`multifunction=on`), the published `3.4.0.0`
installed (`XHCIINST /dir 98-me /yes /nopause`, exit 3), then the same
`qemu` `xhci98.sys` (`36dce3e1...`) and `XHCITR.EXE` (`d86c7d37...`)
copied over it (copied again after the first restart, as on 98 SE) and
`XhciInterruptMode` 2. ME does not run the programs of `AUTOEXEC.BAT`,
so `DEBUG` there never armed the holder (the first boot read the line
not held); the serial function's Interrupt Enable register was written
from the QEMU monitor instead (`o /b 0xc001 0x02`, its I/O BAR at C000h)
as soon as the firmware had assigned the BAR, before Windows started,
and IRQ 10 then read pending at the slave PIC (`irr=04`) through the
boot. A write made late, after an IIR read had cleared the pending
transmit interrupt, armed nothing in time (the start read the line not
held). `XHCITR /dump` (a message box on ME; its text file to COM1):

| Leg | The holder | "line held by another" | Counters | Log |
|---|---|---|---|---|
| m6 e | armed | `likely: 01:00.1 1B36:0002 seen asserting IRQ 10 at every read (9 functions read)` | `HeldScan` 2, `HeldFunctions` 9, `HeldBdf` 257, `HeldId` 00021B36h, `HeldIrq` 10 | `intr.held.ms=0000003C`, `intr.held=00010102`, `intr.held.id=00021B36` |
| m6 c | not armed | `no: IRQ 10, 9 functions read, 0 not confirmed` | `HeldScan` 1, `HeldFunctions` 9, `HeldIrq` 10 | `intr.held.ms=000000D2`, `intr.held=00000001` |

ME's machine has one function more than 98 SE's (its `piix3-ide`).
The delivery POLLING, reason
"forced polling (XhciInterruptMode 2)", as on 98 SE. Evidence:
`E:\xhci98-vm\p46\w46-cm4\m6\` (`serial-c.log`, `serial-e.log`,
`m6d-serial-late-arm.txt`, `cmdline-*.txt`, `pids.txt`).

## 46.13 The stale DOWN in the emulator, and its guard (2026-11-03)

Design record 21 section 8.3 (draft 2). The case "46.13" above could not
build was built with the model's own controls, no change to `usb4-emu`:
the ORICO model's cable (`usb4-device` `x-cable`) pulled and put back
about a millisecond later over one QMP session (`flap.py`), and the
tunnel ports' settle (`usb4-asm-tunnel-dsp` `settle-ms`, 2,000 ms by
default) for whether the new endpoint answers at once. The vehicle is
"46.13"'s: the non-ACPI 98 SE of "46.12" with the ASM4242 model, as an
overlay of that leg's disk (Stor98 and the enclosure's bridges already
installed), the ORICO model hot-plugged on USB4 port 1 at the desktop,
`E:` read before each step. Times are the log's `cm.ms`.

**Before the guard** (`577f5825`, `xhcicm.sys` `c3142fa6...`):

- F1, the settle at 2,000 ms: `u4.linkdown=01000101` and
  `u4.connect=00010002` in one pass (`cm.ms` 32224h); the DOWN of
  generation 1 reached the seam 1,029 ms later with
  `cm.vanish.sweep.gen=20000001`, `cm.vanish.sweep.now=00000002`: the
  DOWN acted after a newer connection began. Its reading (tag
  `00010001`, `cm.vanish.reading.gen=00000002`, at 32637h) found the
  child vanished (the new endpoint not answering yet), the query and the
  removal succeeded (`cm.vanish.cr=02000900`, `02000D00`,
  `cm.vanish.cr.from=00010001`, `cm.vanish.cr.at=85000000`) and the
  child retired at 32713h - before the UP of generation 2, whose poll,
  numbering (03:00.0, 04:00.0) and re-enumeration followed at 329A6h to
  32E87h. `E:` came back. Twice more, the pulls timed from the log (F2,
  F3), the same order.
- E1, the settle at 0: the same pull. The DOWN of generation 1, again
  after generation 2's connection, read the child answering (the new
  hardware, the same vendor and device) and removed nothing
  (`cm.vanish.reading=02008000`); the UP's numbering was held
  (`cm.brnum.9x.held=00020001`: the bridge's devnode still had a child)
  and its re-enumeration ran over the reset bridge: Add New Hardware for
  two "PCI standard PCI-to-PCI bridge" and a "PCI standard host CPU
  bridge" (the reset bridge's secondary bus 0 read as bus 0). The drive
  did not come back as it was. Not a reading of the board's loop, but a
  failure of its own: a DOWN that runs after its port's next connection
  has the new endpoint answering cannot tell the old child by vendor and
  device.

So the removal F1 made is necessary, and the guard "46.13" above
proposed (no removal once the generation moved) would have turned F1
into E1; section 8.3 replaces it. The board's order (the worker's late
numbering published the new connection's bridges before the old DOWN's
reading) was not built: it needs the thread held inside the new
connection's poll while the worker's late call publishes.

Evidence: `E:\xhci98-vm\p46\w46-cm4\b2\` (`debugcon-a.log` F1-F3,
`debugcon-b.log` E1, `s1.png`-`s13.png`, `cmdline-*.txt`, `pids.txt`),
the excerpts `E:\xhci98-scratch\p46-w46-cm4\f1-excerpt.log` and
`e1-excerpt.log`.

**The guard** (`a8d2e027`, record 21 section 8.3): gated builds x86
`all` and amd64 `release`, passed, the host suites with them
(`E:\xhci98-scratch\p46-w46-cm4\gated-x86-all.log`,
`gated-amd64-release.log`; `test_cm_eject` 708 checks, the guard's
vectors among them). The `qemu` `xhcicm.sys` (`a85ca196...`) copied over
fresh overlays of the same disk, the same `XHCITR.EXE`:

| Leg | Settle | The cable | Guard (`cm.vanish.guard`, `.gen`, `.evt`, `.pub`) | What followed | `E:` |
|---|---|---|---|---|---|
| L3a | 2,000 ms | out 1 s | 1 (as before): generation 1, port on 1 | the child read vanished, removed, retired; the replug's numbering and re-enumeration as before | back |
| L3b | 2,000 ms | out 8 s | 1: generation 2, port on 2 | the same | back |
| L1 | 2,000 ms | out about 1 ms | 2 (every child): generation 3, port on 4; link-down event 17h, the last fresh call 13h, the last end 14h | the child removed (`cm.vanish.cr.from=00070003`) and retired before the new connection's numbering (03:00.0, 04:00.0), then its fresh call | back |
| L2 (twice) | 0 | out about 1 ms | 2: generation 1, port on 2; link-down 5, last fresh call 1, end 2 | the child, answering again, removed (`cm.vanish.cr.from=00010001`); the new connection's numbering not held (03:00.0, 04:00.0 numbered); no Add New Hardware | back |
| L3c | 0 | out 1 s | 1: generation 3, port on 3 | as before | back |

Before each, `E:` read on the first plug. The `cm.reenum.9x.call` records
show every call of the member numbered (admission, end, the fresh
publications marked), and `cm.vanish.down` with its event at each link
down whose port's member was known.

Rule 1 (superseded) was not built. Three pulls timed to land inside the
next connection's UP (`flap2.py`, a second pull 0.8, 1.35 and 2.4 s
after the first, the device router's control reads delayed 40 ms) all
reached the thread only after the worker had made both members' calls
(the thread was still in the UP's own waits, `cm.nvlink.up.waited`,
while the worker ran):
rule 2 each time, the drive back. With the `qemu` flavour's failed
re-enumeration (`XhciCmQemuFailReenum` 1, its retry 5 s later) and the
reads delayed 100 ms, the next connection failed in its router
enumeration (`tbt.ring.ctl-no-answer`, `u4.router.enumeration=FFFFFFFF`),
as the board's first did, and the member's retried call - a fresh one -
was admitted after the link-down and before the DOWN reached the seam:
rule 1's order. That DOWN was not bound (the port's member was not
learned yet, so `cm.vanish.down` was not logged) and found nothing to
remove (the member had no child). A pull 300 ms long then brought a
connection that worked (`E:` back). The emulator's enclosure does not
drop by itself, so the loop does not continue there. Rule 1 rests on the
argument of section 8.3 and its log points, for the board.

Evidence: `E:\xhci98-vm\p46\w46-cm4\b3\` (`debugcon-b.log` L3a, L3b, L1
and the three timed pulls; `debugcon-c.log` the failed connection;
`mark-*.txt` the log lines before each step; `s*.png`) and `b4\`
(`debugcon-b.log` L2, L3c), `cmdline-*.txt`, `pids.txt`; the scripts
`flap.py`, `flap2.py`, `trig2.py`, `mk.py` beside them.

## 46.16 The variants by hand, in the emulator (2026-11-03, w46-cm3)

The published `3.4.0.0` on an overlay of w46-cm's `golden340.qcow2` (98
SE, ACPI), q35, TCG, one vCPU, the ORICO model hot-plugged on the
ASM4242's first tunnel port and a QEMU `usb-storage` stick on the board's
own `qemu-xhci`, launched with w46-cm2's `v1` command line (QMP 49340).
AutoPlay turned off first (`NoDriveTypeAutoRun` `FFh` under
`HKCU\...\Policies\Explorer`, imported with the registry editor, then a
shutdown and a cold start), so no Explorer window took the keys. Each
step typed by hand and read on a screenshot before the next; the
letters read with `dir <d>:\ | find "Vol"` from one MS-DOS prompt.
Evidence `E:\xhci98-vm\p46\w46-cm3\v16\` (`a0.png` to `e2.png`,
`v16.log`, `pids.txt`).

**The stick and the drive look alike.** Both disks are overlays of the
same `nvme-fat.qcow2`: the same label (`ORICO`), the same volume serial
(`406E-1A0F`) and the same file. A second letter with that serial is the
stick (unplugging it removes that letter, step a6). The stick was
relabelled `STICK` (`label`) before the variants; its serial stays the
drive's.

| Step | Letters |
|---|---|
| a0: the cold start, nothing plugged | none |
| a1: the drive plugged | E: ORICO |
| a2-a3: `XHCIINST.EXE /eject 1`, `y` ("You can now unplug the device.", exit code 0) | none |
| a4: unplugged, plugged again | E: ORICO |
| a5: the stick plugged | E: ORICO, F: the stick |
| a6: the stick out | E: ORICO |
| b1-b2: Explorer open on E:, the drive pulled (the balloon "ASMedia 246x was disconnected.") | Explorer's window still shows E: |
| b4: Explorer closed, the drive plugged again | E: ORICO |
| b5: the stick plugged | E: ORICO, F: STICK; Device Manager, Disk drives: "QEMU NVMe Ctrl" and "QEMU QEMU HARDDISK" (the stick's disk drive) |
| c1: the stick out, the drive out, then the stick in | E: STICK |
| c2: the drive back | E: STICK, F: ORICO |
| d1-d3: a large copy to F: (`copy /b c:\windows\system\*.dll f:\` until the root directory was full), the drive pulled at once, plugged again | E: STICK, F: ORICO |
| d4-d5: the stick out, then in | F: ORICO; then E: STICK, F: ORICO |
| e1, e2: the stick out; two surprise removals, each plugged back 8 s later | E: ORICO each time |

**Not reproduced**: the stick got its letter and its disk-drive child in
every variant, and no volume was listed twice. w46-cm2's sighting of the
drive as E: and F: with one serial (`E:\xhci98-vm\p46\w46-cm2\v1\
v-a-1.png`, `v-b-1.png`) fits the two look-alike disks: the stick's
volume has the drive's label and serial in those kits; that run's
screens are also out of order (its `v-b-1.png` shows `dir e:`, `dir h:`,
`dir f:`, the keystrokes interleaved). No layer was found keeping a stale
volume. Left: the owner's board.

## 46.12 The second tier, the installer's question and the log (2026-11-03, w46-cm3)

The owner's answers to Q13 and Q14 (record 32 section 12.8.9). Design:
record 32 section 12.8 draft 5 (12.8.10 the canonical form, 12.8.11 the
tier reported and the installer's question, 12.8.12 the log), `9cdb4d82`
and `3da5148b`, each reviewed and revised (three rounds for the design,
three for the code). Code: `67076729`, `3da5148b` (with 46.11's in the
same commit, below); `usb4-emu` `856b10b` (`asm-fw-ssdt.py --build`).

### Off the tree: the board's own tables

The pure half built for the host over the board's three SSDTs (both
captures, identical): every table walked, `\CPNV` placed, the five
methods' raw hashes the known build's, and each method parsed whole into
its canonical form (no term outside the set; four undeclared names, each
one of the listed exemptions); the five forms' FNV-1a and CRC-32 are the
release flavour's second list. A Python prototype of the parse
(`E:\xhci98-scratch\p46-w46-cm3\proto\`) read the same structure first.

### The emulator's builds (`usb4-emu` `856b10b`)

| Build | Raw hashes | Forms | qemu flavour | release flavour |
|---|---|---|---|---|
| trivial (no `--build`) | the old qemu entry | not listed | known | declined |
| 1 | the new qemu entry | the qemu form entry | known (tier 2) | declined |
| 2 (build 1 by the rewrites alone) | others | equal to build 1's | same code (tier 3) | declined |
| 3 (one condition changed in `_OSC`) | `_OSC`'s differs | `_OSC`'s differs | declined (tier 1) | declined |

### Host vectors and the build

`test_cm_osc` 144 checks (the lookups, each rewrite giving the same form,
the near misses giving another or a refusal: a debug call dropped or its
argument changed, a spelling, a mask, an operand order, a field's width
or access; undeclared names, a body's own names, an Alias, terms outside
the set, nesting, two argument counts, the lists), `test_inst` 8108
(the record's tier, the question held and asked, `/set`), `test_pfw` 421.
The gated build of `4b55b47e` (x86, release and qemu): BUILD + GATES
PASSED, `E:\xhci98-scratch\p46-w46-cm3\gated-x86-all-2.log` (the first
run, of `67076729`, stopped at `XHCIPFW.SYS`'s link: fixed by
`4b55b47e`, the line pass in a file of its own). `qemu` `xhcicm.sys`
`951b7a8b...`, release `4198579f...`.

### The legs (98 SE without ACPI)

Base: an overlay of w46-cm2's `lg\disk.qcow2` with this build's `qemu`
`xhcicm.sys` and `XHCIPFW.VXD` (`bf30a038...`) copied in and AutoPlay
off (`E:\xhci98-vm\p46\w46-cm3\lb\`); each leg a fresh overlay of it,
q35, TCG, one vCPU, w46-cm2's QEMU (`pm-cap=on`, `fw-handover=on`,
`fw-data=0x7FF00000`, `fw-handover-gate=on`), the leg's three SSDTs; a
cold start each.

| Leg | What | Result |
|---|---|---|
| k1 | build 1 | the three SSDTs' ids logged (`cm.osc.ssdt.*`); `cm.osc.tier=2`, the contract notes; `cm.osc.handover=00000100`, `cm.osc.query=000F0100`; the model saw `0102h` and `0180h` once each; the router answered (`u4.host.id=174C2428`) |
| k1 | the NHI disabled and enabled in Device Manager | the second start: `cm.osc.tier=2`, `cm.osc.already=00000100`, no third mailbox command, the router answering |
| s1 | build 2, `XhciCmHandoverStructural` absent (0) | the five forms logged, equal to build 1's; `cm.osc.tier=3`, `cm.osc.refused=0000000E`; no mailbox command; the router silent (`u4.failed=0000000C`) |
| s1i | the same, then `XHCIINST.EXE /dir 98-me /list /thunderbolt /log` (this build's release kit) | the log: "thunderbolt: ASM4242 ...: firmware not known to be a known build"; the question "XhciCmHandoverStructural - send the USB4 handover to ASM4242 firmware that is not a known build but has the same handover code", allowed 0 and 1, default 0 |
| s2 | build 2, `XhciCmHandoverStructural=1` (machine-wide, `regedit`), cold start | `cm.osc.tier=3`, `cm.osc.handover=00000100`; both commands once; the router answered |
| m1 | build 3 | `_OSC`'s raw hash and form differ; `cm.osc.tier=1`, `cm.osc.refused=0000000D`; no command; the router silent |
| r1 | build 1, the release `xhcicm.sys` | no mailbox command; the status record's flags `00010000` (tier 1, nothing written), stage failed |

Not run: the mailbox busy and a reply never given (the model has no
property for either yet); the board.

### The coordinator's question on NT (`_OSC` and `_OSI`)

From the board's decompiled tables: the handover is sent from the
platform-wide `_OSC` branch only when `\M046` is at least `0Bh`, which
`\M045` sets from `_OSI ("Windows 2021")` (Windows 11) or later; no
`_OSC` capability bit takes part. By Microsoft's published `_OSI` table
2000 to 10 never answer that string, so the firmware sends nothing to
them: record 32 section 12.8.2's NT statement is wrong for this board.
The owner extended 46.12 to NT (2026-11-03); its design is to follow
(record 32 section 12.8.13).

## 46.12 on NT: the design and the model's tables (2026-11-03, w46-nt)

The owner's decisions of 2026-11-03 on record 32 section 12.8.13 draft
6's open points ("safe routes only": the mailbox through the upstream
port's own stack and never ECAM, a physical read only where the system's
documented memory map proves firmware memory, the system's own firmware
table interface where it gives what is needed; once per boot and again
after S4, not after S3; the tiers and the opt-in of 98 SE and ME).
Design: record 32 section 12.8.13, drafts 7 to 9, reviewed and revised
(four rounds). Work on development host A; nothing of the driver
changed, no build.

### What the design found

- No NT system documents a route the rules admit to the three named
  SSDTs and the firmware's data: the documented kernel table route
  (`AuxKlibGetSystemFirmwareTable`, Vista on, a library of the Windows 8
  and later kits, which this tree does not have) gives the first table
  of a signature alone, and no NT system documents a typed firmware
  memory map to a driver (`MmGetPhysicalMemoryRanges` is reserved for
  system use; `MmGetPhysicalMemoryRangesEx2`, from 10 version 2004, is
  untyped). Under the rules the handover is declined on every NT system:
  the owner's question Q15 (record 32 section 12.8.13.3).
- A hybrid sleep's two outcomes (a wake from S3, a resume from the
  hibernation file after a power loss) come back with the same documented
  power request fields, and a bridge's filter is not documented to see
  the NHI's system power requests: "again after S4, not after S3" cannot
  be told apart there, nor seen for an NHI disabled across the
  hibernation: the owner's question Q16.
- The rest (the mailbox port's role and install, the order of the
  mailbox reads, the boot marker as a permanent named callback object,
  the S4 obligation) is designed for options B and C, to be reviewed
  again with the owner's answer.

### The model's tables on the NT bases

`usb4-emu` `scripts/asm-fw-ssdt.py` gains `--osi` (with `--build`): the
board firmware's own condition on the commands its `_OSC` sends
(`\M045` from `_OSI ("Windows 2021")`, `\M046` at least `0Bh`), which the
model lacked, so its `_OSC` sent to any ACPI system that evaluates it.
The output without `--osi` is unchanged (builds 1 to 3 compared byte for
byte). QEMU `E:\xhci98-scratch\p46-w46-cm2\qemu-bin` (w46-cm2's build of
the model with the mailbox), q35 bases, `-Usb4 asm4242 -Enc orico`,
`pm-cap=on`, `fw-handover=on`, `fw-handover-gate=on`, `fw-data` as below,
TCG, two virtual processors unless said; the leg's three SSDTs by
`-acpitable`; a fresh overlay each start; no package installed.

| Start | Tables | Result |
|---|---|---|
| 7 x86, `fw-data=0x7FF00000` (2 GB of RAM) | build 1, region at `7FF00000h` | bug check `A5` (`00001000`, 0, `7FF00000`, `00000100`): the data region in the guest's RAM |
| 7 x86, `0x9FF00000` | build 1 | `A5` (`00000011`, `00000008`, ...) |
| 7 x86 | the data table alone; the data and USB4 tables; the three trivial bodies | each booted to the desktop |
| 7 x86 | build 1 `--osi`; build 2 `--osi` | the desktop (one start each), no mailbox command |
| 10 x64 | build 1 `--osi` | the desktop, no mailbox command |
| XP | build 1 `--osi` | `A5` (`11h`, 8) |
| XP | the data table; data and USB4; trivial | each booted |
| XP | build 1 `--osi`, no `External` anywhere (test only); build 1 | each booted (one start each) |
| XP | build 1 `--osi` without the `External`s of `\M045` and `\M046` (test only) | `A5` (`11h`, 8); 7 with it: "Windows failed to start" |
| 7 x86 | build 1 `--osi` again (twice with two processors, twice with one) | `A5` (`11h`, 8) each time |
| XP | build 1 again (two processors, then one) | `A5` (`11h`, 8) each time |
| 7 x86, XP | the model, no tables | the desktop |

So the region must lie in a hole of q35's map (`9FF00000h` with 2 to 4
GB), and whether the NT bases load the model's tables at every start is
**not settled**: the same tables booted on one start and stopped on
another. No mailbox command reached the model on any start (the model
logs each). Under option A nothing rests on it; B or C needs it settled
before its legs.

Evidence: `E:\xhci98-vm\p46\w46-nt\` (`pids.txt`, `STATE.md`,
`osc-nt.ps1`; the screenshots of the starts above in their folders), the
tables in `E:\xhci98-scratch\p46-w46-nt\emu\`, the design's drafts and
reviews in `E:\xhci98-scratch\p46-w46-nt\`.

### Left

The owner's Q15 and Q16; with B or C, the code, its host vectors, the
gated x86 and amd64 builds and the NT legs (record 32 section 12.8.13.8),
the model's tables on the NT bases settled first; with A, the board's 10
x64 reading without the handover.

## 46.11 The NHI's and its port's line FFh (2026-11-03, w46-cm3)

The owner's answer to Q1 (record 32 section 12.6). Design: record 32
section 12.10 (draft 3), `9cdb4d82`, reviewed and revised (three rounds).
Code: `67076729` and `4b55b47e` (`src\pfw\pfw_line.c`, `pfw_ids.c`,
`pfw.c`'s switch read at `Sys_Critical_Init` and log, `PfwCfgWrite8`,
the installer's `XhciPfwNhiLine`), reviewed and revised (three rounds).
`test_pfw` 421 checks (the switch's decision, the NHI by id or class,
the port by topology, a second device or function, two ports, a tunnel
port, decode off, pin 0, a line that does not read back, the root bus,
the ids against `xhcicm.inf`). Gated build as 46.12's above.

| Leg | What | Result |
|---|---|---|
| every leg's first start (the switch absent) | SeaBIOS's lines (`0Bh`), steering disabled | `XhciPfwLines`: 64:00.0 (NHI) and 02:03.0 (its port) written `0Bh` to `FFh`; `XhciPfwLineSwitch` open 0, query 2 (not found), setting 1; `XhciPfwResult` `81h`; the NHI started, `xhcicm` ran (k1, s2) |
| n1 | both lines put on 4 (COM1's) before Windows (`SETL4.COM` in `AUTOEXEC.BAT`, test only) | both written `04h` to `FFh`; the NHI started with no IRQ (`cm.irq.resource=0`), the handover, the router answering |
| n0 | the same lines, `XhciPfwNhiLine=0` | the switch read (type 4, raw 0, setting 0); both lines left at `04h` (outcome 6); `xhcicm` never started (the port held, as without the step) |

Not run: steering enabled (98 SE with ACPI), ME, a tunnel port's
drive's line after the step, the QEMU configuration trace; the board's
reading is the owner's. Evidence for both tasks:
`E:\xhci98-vm\p46\w46-cm3\` (`k1\`, `s1\`, `s1i\`, `s2\`, `m1\`, `r1\`,
`n1\`, `n0\`: `debugcon-*.log`, `qemu-*.out`, screenshots), `pids.txt`.

## XHCIPFW.VXD's log strings: a path-dependent bad build (2026-11-03)

Found by the 46.5 legs above ("`XHCIPFW.VXD`'s log, not written"): the
image built in one worktree (`d3c67c14...`) wrote no `Device_Init` value
under `Services\VxD\XHCIPFW` on 98 SE and ME while its scan ran; the same
sources elsewhere gave `90cf7fa5...`, which writes them. Work done on
development host A, file-only but for the one leg below.

### The cause

- **Only the objects matter.** The bad and a good build's objects differ
  in the `THEADR` record (Open Watcom writes the source's full path) and
  the dependency comments (each header's path and time stamp) alone; the
  maps differ in their time stamp alone. The bad image's 13 differing LE
  fixups are `PfwLog`'s value names on page 7 (`-zc` strings in `_TEXT`),
  targets `FFFFF000h` to `FFFFF0D0h`: each string's offset in its
  segment less `1000h`, where the good image has `0B44h` to `0C14h`.
  Relinking the copied objects in a scratch folder reproduced both
  images, so nothing of the worktree, `PATH`, `TEMP` or the environment
  is in it.
- **The record.** The bad object's records spliced one at a time into
  the good one: `THEADR` alone gives the bad image; the four dependency
  comments change nothing. Names of 1 to 140 characters in `THEADR`:
  18, 36, 54, 72, 90, 108 and 126 mislink, every other length links
  right, and two different names of one length behave alike. The bad
  worktree's `E:\xhci98-wt\w46-inst2\src\pfw\pfw.c` is 36 characters.
- **Which fixups.** Only those whose target is the object's own segment
  (a `SEGDEF` target): `pfw.obj` has 13, `vsd_list.obj` one, the other
  objects none; a 36-character name on `pfw_scan.obj`, `pfw_io.obj` or
  `pfw_data.obj` gives the same image byte for byte. `S98VSD.VXD` fails
  the same way: a 36-character name on `vsd_list.obj` puts its one fixup
  at `FFFFF000h`. Fresh checkouts of the unchanged sources at
  `E:\xhci98-scratch\p46-w46-pfwbld\t54abcd` (`pfw.c` 54 characters) and
  `...\v5` (`vsd_list.c` 54) built both images bad, and every check of
  the time passed them.
- Why 18 is not read (design record 30 section 6.1, [hyp]).

### The fix and the gates (`5d46ca99`)

Design record 30 section 6.1 and 6.4, record 32 section 8:
`omfpub.py` sets each object's `THEADR` to the source's base name and
both builds compile with `wcc386 -zld`; `omfcheck.py` refuses a `THEADR`
with a path or of 18n characters; `callcheck.py` holds every
segment-relative fixup of every object (the data objects now passed too)
to the image's LE fixup table, with a `--self-test` each build runs;
`check-pfw.ps1` and `check-vsd.ps1` refuse an LE fixup that is not an
internal 32-bit offset inside its target object, or a page-crossing one
not listed on both pages, with a `-SelfTest` on synthetic tables each
build runs (the packager runs the gates again); `make-release.ps1` holds
`XHCIPFW.VXD` newer than `src\vsd`'s guards. Reviewed and revised.

| Check | Result |
|---|---|
| the fixed sources built at three paths (the worktree, `t54efgh` and `v6`, the last two the lengths that mislinked) | the same objects; `XHCIPFW.VXD` `90cf7fa5...` (the known good image), qemu `b37d3298...`; `S98VSD.VXD` `2ae06831...`, qemu `0278a8a0...`, each the same from all three |
| `callcheck.py` on the bad images | `XHCIPFW.VXD`: 38 segment-relative fixups, 13 mismatching; `S98VSD.VXD`: 33, 1 mismatching |
| `check-pfw.ps1` / `check-vsd.ps1` on the bad images (`d3c67c14...`, the two 54-character builds) | refused: each fixup "a fixup to object 1 offset 0xFFFFF0xx, past its ... bytes" |
| the published images' LE fixups, read with the gates' rule (target object and offset in range): `S98VSD.VXD` of `3.2.0.0`, `3.3.0.0` and `3.4.0.0`, `XHCIPFW.VXD` of `3.3.0.0` (release and debug) and `3.4.0.0` | every fixup inside its object; the whole gates of `5d46ca99` (their stamp `3.4.0.0`) pass `3.4.0.0`'s two images |
| the 18 images then in `E:\xhci98-wt\*\src\pfw\obj` and `src\vsd\obj` (release and qemu) | only `w46-inst2`'s `XHCIPFW.VXD` bad; `w46-cm3`'s differs from the others because its sources do (46.11, 46.12), and its fixups are right |
| gated x86 release build of `5d46ca99` | passed (`E:\xhci98-scratch\p46-w46-pfwbld\gated-x86-5d46ca99.log`); a test cut of it (`make-release.ps1 -TestLabel`) passed, "XHCIPFW.VXD built from src\pfw ... newer than its sources" |

So `3.4.0.0`'s published `XHCIPFW.VXD` is not affected; of the worktrees
only `w46-inst2`'s build was bad at the time of the scan, and a package
staged from it (its kits of this phase) carries the bad image.

### The leg (98 SE)

q35 98 SE overlay with the ASM4242 switch and the root port's padding
(prefetchable windows above 4 GB), no host router, 1 vCPU; the test
cut's package (`XHCIPFW.VXD` `90cf7fa5...`) from the transfer disk,
`INSTALL /console /yes /thunderbolt`: exit 3, "Done. Please restart
Windows now."; `Services\VxD\XHCIPFW` then held `StaticVxD` and `Start`
alone. Shut Down, a cold start: the key holds `XhciPfwResult` 1,
`XhciPfwClosed` 7, `XhciPfwRefused` 0, `XhciPfwFunctions` 0Eh, the
`XhciPfwBridges` records and `XhciPfwVersion` "XHCIPFW.VXD 3.4.0.0".
**Pass.**

Evidence: `E:\xhci98-vm\p46\w46-pfwbld\` (`S98\serial-a.log`,
`serial-b.log`, `a1.png` to `a5.png`, `b1.png`, `cmdline-a.txt`,
`cmdline-b.txt`; `pids.txt`; `xf\D.BAT`); `E:\xhci98-scratch\p46-w46-pfwbld\`
(`rl\`, `rv\` the relinks; `py\` the readers; `bad-inst2-d3c67c14.vxd`,
`bad-p54-head.vxd`, `bad-v54-head.vxd` the bad images, untracked).

## 46.20 xhci98's and xhcicm's 9x paths, and the UAS driver's unload (2026-11-03, w46-vmm2, partial)

The leads the first pass left (above): xhci98.sys's and xhcicm.sys's
98 SE paths under the same load, read and run. Development host A, QEMU
TCG; method as above (STOP screens, object and offset, register and
stack data; no system binary read). **The board's VMM faults are not
reproduced and not explained. One defect was found, in xhciuas.sys, of
the kind the board's and the x86 matrix's faults suggest, and fixed for
98 SE and ME.**

### What was read, and what it rules out

Every kernel object and every service call of the two drivers that can
run on 98 SE and ME outside a PnP dispatch (sources at `057e985a`):

- **xhci98.sys, timers and DPCs.** The slot timers, the MFINDEX sampler
  and the tolerance clock (`hcd_svc.c`) are initialised once, at
  AddDevice (`HcdControllerInitObjects`), never while armed; the drain
  (`HcdTimersDrain`) closes the service, cancels, and waits for any DPC
  already fired to clear its flag or slot under `TimerLock`, then takes
  the lock once more; nothing re-arms after the close. The poller's timer
  (`hcd_poll.c`) closes the same way (`HcdPollClose`). The interrupt DPC
  is counted from its queuing (`DpcsInFlight`) and closed with an
  interlocked exchange before the count is waited out (`hcdRelease`). A
  device PDO's refusal timer and DPC (`hcd_io.c`) hold a count while
  armed or running, and the PDO is deleted only at 0 (`hcdDeletePdo`).
  The DMA DPCs are drained at the release (`HcdDmaMapDrain`). The KTIMERs
  with no DPC: the root links' recovery bound (cancelled when the
  controller thread leaves), the SuperSpeed hub's re-arm
  (`hcdHubRearmStop` at every forget and free), and every stack deadline
  (`hcd_enum.c`, `hcd_hub.c`, `hcd_hubfdo.c`, `hcd_rh.c`, `hcd_susp.c`):
  each cancelled on every return path before its frame goes. No KTIMER
  or KDPC of these is freed, reused or re-initialised while queued.
- **xhci98.sys, interrupt time.** The line ISR (`hcdIsr`, `XhciIsr`)
  reads and writes the controller's registers and queues the DPC; no
  other service. The finer timer's thread and `ExSetTimerResolution`
  exist only on NT (`HcdTmrCreate`). Pool and MDLs at lifetime sites
  and, in the transfer path, one `IoAllocateMdl` /
  `MmBuildMdlForNonPagedPool` for a client buffer (`hcd_io.c`), at
  DISPATCH_LEVEL at most, never in the ISR.
- **xhcicm.sys on 9x** connects no interrupt (the ring interrupt's value
  defaults to off, and a line is refused on 9x, `CmRirqService`), queues
  no DPC and arms no KTIMER (the networking timer runs only with the
  Thunderbolt network driver open). Its work runs on threads: the
  connection-manager thread (timed waits on its event), the CONFIGMG
  worker and the per-member call workers, which alone call CONFIGMG,
  PCI.VXD's two configuration services and VMM's three registry services
  through the INT 20h thunks (`cm_cfgmg.c`), each route its own pool
  block. The Stor98 link-state table is never freed (`stor98cm.h`). The
  registry admission of 46.14 holds. Nothing of the CM runs at interrupt
  or DPC time on 9x.
- **Waits.** The HCD's gates go through `HcdWaitEvent`, which handles
  98's early `0xFFFFFFFF` return. Several of the CM's waits and the
  thread joins of both drivers do not (`CmThrRunPnp`, `CmThrStop`,
  `CmPowerDrain`, the configuration and identity IRPs' stack events,
  `hcdThreadStop`); per `win98-wdm.md` that return comes when another
  thread terminates while the wait is blocked, any thread, not only
  these drivers' (theirs stay for the boot). Where the IRP or the work
  does not pend the wait is not entered; where it is, an early return
  would let the caller go on while the completion or the thread still
  owes it (`cmConfigOnce` would free an IRP still below it). Whether that
  happens in the board's sessions is not known: left unresolved, a
  possible corruption path, not an established lead.

### The defect: xhciuas.sys unloads under its own workers on 98 SE and ME

xhciuas.sys registered `DriverUnload` on every system. On 98 SE and ME
NTKERN calls it from the `IoDeleteDevice` of the driver's last device
object (`win98-wdm.md`), and the image goes. The recovery worker and the
wake worker are `ExQueueWorkItem` items (9x has no `IoQueueWorkItem`);
each ends with `uasDeref`, whose `KeSetEvent` releases the removal's
rundown wait (`UasEngineStop`), and only then returns through the image
(`uas_xport.c`, `uasRecoveryWorker`, `uasWakeWorker`). An unplug during
I/O fails transfers, which queues the recovery worker; the removal that
follows can delete the FDO and unload the image before that worker has
returned, and the worker then executes freed memory: a fault whose CR2
equals its EIP, as the x86 matrix's 98 SE leg read at 0028:FF058D80
(w46-mx86; which image owned that address is not established). The same
tail follows the transfer and telemetry completions and the timer DPC,
which on one processor the removal's thread cannot overtake.

Fix (`edcddc2b`): no `DriverUnload` on 98 SE and ME, so the image stays
resident for the boot, as xhcicm.sys already is there (`cm_entry.c`).
Re-plug creates fresh per-device state (`uasAddDevice`); the installer
places xhciuas.sys by the same route as the resident xhcicm.sys
(`sys_9x`, `own_put_as`), so residency adds no update case. Reviewed and
revised.

Left open, recorded rather than fixed here:
- **NT** keeps `uasUnload`, and the same worker tail is open there (on
  more than one processor the removal can overtake the worker). Closing
  it needs the `Io*WorkItem` family, which the one x86 binary that 98
  loads cannot import (the import gate refuses it).
- **xhci98.sys on 98** registers `DriverUnload` too; the same kind of
  tail (the controller thread's `PsTerminateSystemThread` after
  `ThreadExited`, the power and WAIT_WAKE workers, the suspend and idle
  callback threads' grace delay) matters only when its last controller
  is removed, which the board's chronology does not contain.

### Where more VMM services fault

`STOR98_TEST_VMMMAP` 4 to 8 (`8c2adbab`, test-only, never staged;
images `xf\PMAP4-8.MPD`), at the 3000th `HwStartIo`, with a pointer into
a never-committed system page:

| Service | Result |
|---|---|
| `Cancel_Global_Event` | returned, no fault |
| `Cancel_Priority_VM_Event` | "An exception 0E has occurred at 0028:C00034E8" |
| `Signal_Semaphore` | 0028:C000A488 |
| `List_Remove` | 0028:C0002591 |
| `_HeapFree` of a block of its own whose 16 bytes before it were overwritten | returned; the benchmark ended; the system later went to a black text screen |

Each fault was "called from 0028:C003BC15". With the first pass's
`_CopyPageTable` (C000855C) and `Cancel_Time_Out` (C0003E8C), none of
these faulting instructions is near the board's VMM(01)+139F4 or +13B2E
(C00149F4, C0014B2E). As before, that rules out these instructions only.

### The legs

Vehicle: the first pass's (w46-cm2's `lg\disk.qcow2`, emulator
`97cb422e...`, the ASM4242 layout with the ORICO model tunnelled at
power-on, one vCPU, 512 MiB, `usb4-debug-port`), the 3.4.0.0 package as
installed there; Stor98 "pin 1 line 11 ... interrupt"; XHCIPERF
`e85bcb30...`.

- **L2, both drivers busy.** A USB stick (`usb-storage`, a FAT image) on
  the ASM4242's xHCI copying files in a loop while XHCIPERF ran on the
  tunnelled drive (sequential 1, 2 and 8 MiB, random 4 KiB at four and
  eight threads, three passes): 50.5 / 82.7 MB/s sequential 1 MiB
  (write / read), 1588 / 1462 IOPS random 4 KiB at four threads, no
  request failed. A second run with the enclosure's cable pulled under
  load (`x-cable` false for 40 s, then true): "Disk Write Error" on the
  tunnelled drive, the stick kept copying; three Ctrl+Alt+Del gave Close
  Program each time. No fault. In this kit the connection manager is
  inert (`u4.failed=0000000C`) and the drive does not come back after the
  re-plug. The ASM4242's xHCI was on IRQ 10, the drive and the NHI on 11.
- **L3, xhci98 on the drive's line.** The same with QEMU's xHCI moved to
  slot 6, so on IRQ 11 with the drive and the NHI (Device Manager), and
  the stick on it: 42.5 / 98.0 MB/s sequential 1 MiB, 1216 / 1346 IOPS
  random 4 KiB; the cable pulled and replugged under load; three
  Ctrl+Alt+Del. No fault.
- **The UAS unplug.** A UAS drive (`usb-uas` with one `scsi-hd` LUN, a FAT
  image) on QEMU's xHCI, unplugged (`device_del`) while a copy wrote to it,
  then attached again (its LUN first). With the 3.4.0.0 xhciuas.sys, four
  unplugs, three with writes in flight; with the fixed image
  (`edcddc2b`'s xhciuas.sys, `06260fc3...`, copied over
  `SYSTEM32\DRIVERS\XHCIUAS.SYS` and loaded at a cold start), six, each
  with writes in flight. Each unplug gave "Disk Write Error" or "The
  volume that was removed had open files on it"; the drive came back as
  F: each time; a Ctrl+Alt+Del after gave Close Program. No fault with
  either image: the race did not reproduce here, so this leg shows the
  fixed image working through removals and re-plugs, not that the fix
  removed a fault. (A first set of cycles attached the device again
  without its LUN and is not counted.)

Also seen: the stick on the ASM4242's xHCI had two drive letters (E: and
G:, independent contents), the double listing 46.16 records.

What this leaves for the board (46-E.1): its faults at its full rate,
each one's full screen text; whether a UAS device was unplugged in the
sessions that faulted; the refused plug-in after a Registry Checker
restore.

Evidence: `E:\xhci98-vm\p46\w46-vmm2\` (`v1\` L2: `serial-L2.log`,
`L2-*.png`, `dm-irq*.png`; `v3\` L3; `p4\` to `p8\` the probes:
`debugcon-b.log`, `stop.png`; `u1\` the UAS leg; `pids.txt`;
`STATE.md`); builds `E:\xhci98-scratch\p46-w46-vmm2\gated-x86.log` and
`gated-amd64.log` (both "BUILD + GATES PASSED (release)").

## 46.8 the shrinking reserve, and Enter in the Settings sheet (2026-11-03, w46-inst2)

The owner's decisions of 2026-11-03: 46.8 option B (shrink the reserve as
the known work completes, so the run's end's step is about an ordinary
item's while late items keep room), and Enter in the Settings sheet
presses OK, not "Advanced...". Branch `w46-inst2` from the merge
`6ea228b1`. Commits: `cf6838d1` (record 26 draft 67: the design),
`c986e287` (the code), `7551cfa5` (a tab's first focus), `ef6aa7a6` (the
planned devices counted again at each item's end), `2e2347e3` (the
reserve at most twice the known work left), `6f7f639f` (no device weight
left planned that the run will not install; the pool in the log); each
reviewed and revised.

### What changed

- A share counts the reserve scaled by the square root of the share of
  the known work still to do, and at most twice that work
  (`inst_prog_reserve_milli`); the uninstall plans its run's end.
- The device installs planned are counted again at each item's end,
  dropping only: one install call reaches every device of its hardware
  id, and their weight stayed planned to the run's end. A device on the
  steps list is not counted, and when the run on 2000 and later stops
  before all devices are done, the planned installs are dropped.
- The log gives after each item's begin "progress: weight left <w>,
  devices planned <n>, reserve <r>, done <d>".
- Each Settings tab makes the sheet's OK the default button and takes the
  default style off "Advanced..." and "More >"; Enter on either without
  its focus presses OK; a tab whose first focus the sheet put on one of
  them moves it to its first row that can be changed, else to OK.

### Checks and builds

- `test_inst` 8093 checks, 0 failures (the run's end's step no larger
  than the median item's on a 98 SE- and an XP-like sequence, the
  reserve whole at the start and never 0, at most twice the known work,
  forty removals with their gaps each with a share).
- Gated x86 `release` of `c986e287`, `7551cfa5`, `ef6aa7a6`, `2e2347e3`
  and `6f7f639f`: each PASSED, the window self-test 888 checks, 0
  failures (with the check that neither tab button keeps the default
  style); logs `E:\xhci98-scratch\p46-w46-inst2\gated-x86-<commit>.log`.
  Test cuts exit 0; hashes `E:\xhci98-vm\p46\w46-inst2\hashes-<commit>.txt`
  (`6f7f639f`: `SETUP.EXE` `097f8779...`, `XHCIWIZ.EXE` `2dde26b4...`).

### The legs

Fresh overlays, `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot`, a first
install with the Thunderbolt support, QMP 49307 to 49315, starts and
stops in `E:\xhci98-vm\p46\w46-inst2\pids.txt`; progress lines in
`shots\<guest>\progress.txt`. The step is from one item's end to the
next's (the gap included); the median over the run's items.

| Package | System | The run's end | Its step | The items' median step |
|---|---|---|---|---|
| `c986e287` | 98 SE | 971 to 999 | 28 | 38 |
| `c986e287` | XP | 852 to 999 | 147 | the four ASM4242 ports' filters, one call, still planned |
| `c986e287` | 7 x86 | 906 to 999 | 93 | the same |
| `ef6aa7a6` | 7 x86 | 927 to 999 | 72 | the reserve, three times the known work |
| `ef6aa7a6` | XP | 879 to 999 | 120 | a device still planned |
| `6f7f639f` | 98 SE | 980 to 999 | 21 | 38 |
| `6f7f639f` | 7 x86 | 947 to 999 | 57 | 47 |
| `6f7f639f` | XP | 913 to 999 | 94 | 55 (items 24 to 88) |

On `6f7f639f` every position at or above the one before; "devices
planned 0" at the run's end on each; the reserve there 66 to 78 weights
against 3 known; each run exit 3, "Done. Please restart Windows now."

**The Settings sheet** (`ef6aa7a6`'s package, `7551cfa5`'s code): on 98 SE
the USB tab opened with the focus on `XhciSilentInstall`'s row and OK
the default; Enter closed the sheet with OK; a value changed and Enter
kept it (seen on reopening); Alt+A opened "Advanced...". On XP the
Thunderbolt / USB4 tab opened with the focus on `XhciCmMode`'s row,
Ctrl+Tab to the Stor98 tab left the focus there, and Enter closed the
sheet with OK. On `c986e287` (before `7551cfa5`) Enter from a row
already pressed OK, but the first focus was "Advanced...".

### Found and left

- The run's end's step is now about an item's on 98 SE (21 against 38)
  and 7 (57 against 47); on XP it is 94, the largest step of that run,
  above its other items' 24 to 88. Its cause is in the pool at the last
  items: the waits are all found late (eleven or twelve on XP and 7), so
  the reserve stands at some 65 to 80 weights while 5 are known, and
  the cap of twice the known work still keeps a fifth of what is left
  per item. A lower cap would bring XP's step down but let a run that
  keeps finding work late stand near the end sooner (with the cap at the
  known work, twenty late items in a row left no room in millionths).
  The owner's to choose; 46.8 is not ticked.

## 46.final pre-cut matrix, x64 (2026-11-03)

The regression matrix of the `3.4.1.0` tip on the 64-bit targets before
the cut, to catch anything the Phase 46 merges broke. Development host A,
QEMU TCG, every leg on a fresh overlay of its q35 base
(`E:\xhci98-vm\q35-bases\`), `-smp 2`, the pinned emulator of
`launch-q35.ps1` except where named.

**The build.** `e78c8e1b` (every round-1 merge, the docs, and `707a52fa`):
one gated `release -amd64` build, passed
(`E:\xhci98-scratch\p46-w46-mx64\gated-amd64.log`): `xhci98.sys`
`7a92b6dd...`, `xhciuas.sys` `ede4ac28...`, `xhcicm.sys` `71b7be9e...`,
`stor98.sys` `69eeaa29...`. The tools from their own `build.cmd`
(`XHCIIN64.EXE` `e86dc776...`, `XHCIWZ64.EXE` `b75c344d...`, `SETUP.EXE`
`d0115dee...`, `XHCITR64.EXE` `165f636d...`, `XHCIMK64.EXE` `07c19dff...`).
`make-package.ps1 -Arch amd64` (unsigned, no key on this clone), the
catalogs made unsigned with `package-common.ps1`'s
`New-PackageCatalogFile`, `ConvertTo-FamilyLayout -Arch amd64` into an
`nt-x64\` kit with `tools\`, `SETUP.EXE` and `INSTALL.BAT` at its root.
The tip still carries the version `3.4.0.0` (the bump is the cut's), so
the installer names every driver `3.4.0.0`; which build ran is read by
comparing the loaded image with the package (`fc /b`), below.

**The legs.** In each guest `S.BAT` copies the kit to `C:\PKG`, makes a
throwaway certificate (`MakeCert -b 01/01/2026 -e 01/01/2030`), adds it to
Root and TrustedPublisher first, signs every `.sys` and `.cat` of
`nt-x64\` with it (`SignTool sign`, `verify /pa` passed on each), and
turns test signing on (the guests' dates stay pinned); a restart; then
`INSTALL /console /yes /nopause` (the default ticks; on 8, 8.1 and 10
`/usb`, without which the USB drivers are not installed there); a full
shut down and a cold start; then readings (`R.BAT`: devices with their
codes and services, services, the image in `system32\drivers` against
`C:\PKG`), copies (`C.BAT`: a 16 MiB file to each test volume, `fc /b`
there and back), the controller's disable, enable, remove and rescan
(`DC64.EXE`: SetupAPI's property change and `DIF_REMOVE` as Device
Manager sends them, then `CM_Reenumerate_DevNode` on the root; readings
and copies after each), the uninstall, a cold start and readings. The
bus: a Bulk-Only stick (`usb-storage`, volume `STICKMX`) on port 1, the
tablet on port 2, a Full-Speed hub on port 3 with a HID mouse and QEMU's
composite USB audio behind it, a UAS disk (`usb-uas`, volume `UASMX`) on
port 4's SuperSpeed half. The two disks are copies of the FAT test disk
with their own MBR signatures (two disks with one signature put one
offline on 6.x).

| System | Install | After the cold start | Copies (stick, UAS) | Disable / enable | Remove / rescan | Uninstall |
|---|---|---|---|---|---|---|
| 7 SP1 x64 | exit 3, the controller, root hub, hub, composite device and UAS disk "started" | every device Code 0: `xhci98` controller, root hub, hub and composite entry, `xhciuas`, `USBSTOR`, `usbaudio`, both mice under `mouhid`; `xhci98.sys` and `xhciuas.sys` the package's | both passed | Code 22, then everything back, copies passed | completed; everything back under `xhci98`, copies passed | exit 3; after the cold start the controller at Code 28, nothing of xHCI98 left |
| Vista SP2 x64 | exit 3 | the same, all Code 0 | both passed | passed | the remove "restart required" (`DI_NEEDREBOOT`); after the rescan the devices still under `xhci98`, copies passed | exit 4: the UAS disk's removal "restart pending", "run again"; after a cold start the second run exit 3 and nothing left |
| 8 x64 | exit 3 | the same, all Code 0 | both passed | passed | completed; Windows then gave the controller to its own `USBXHCI` | exit 3 |
| 8.1 x64 | exit 3 | the same, all Code 0 | both passed | passed | as 8 | exit 3 |
| 10 x64 22H2 | exit 3 | the same, all Code 0 | both passed | passed | as 8 | exit 3 |
| XP x64 SP2 | exit 3 (the second run; see below) | the same, all Code 0 | both passed | passed | the controller waits on a Found New Hardware wizard after the rescan | exit 4 "Windows is still installing something" |

On 8, 8.1 and 10, after the rescan Windows ranks its own signed driver
above the staged test-signed one, and the UAS disk is then at Code 10
under `UASPStor` (QEMU's device fails Windows' own driver, as the release
notes record); the stick, hub, mice and audio work there under Windows'
own drivers. The controller's disable / enable / remove / rescan
sequence, the remove-then-rescan shape on Vista and the uninstall's stop
were read on the published `3.4.0.0` in the same kit (below).

**Thunderbolt / USB4: 7 SP1 x64, the ASM4242 layout.** `-Usb4 asm4242
-EncAtBoot` (the ORICO model with the tunnelled NVMe drive at power-on), on
a copy of `w46-cm2`'s emulator (usb4-emu `e427aac`, `97cb422e...`);
`INSTALL /console /yes /nopause /thunderbolt /stor98`: exit 3, "xHCI98
USB4 Router (ASMedia ASM4242): xhcicm", the four "xHCI98 USB4 PCI Bridge
(ASMedia ASM4242)", "xHCI98 USB 3 Controller (ASMedia ASM4242)" and
"Stor98 Storage Controller for NVMe SSDs: stor98" started, `xhcimark`
installed and running. After the cold start: every device Code 0, the
drive "QEMU NVMe Ctrl SCSI Disk Device" under Stor98 as `G:` (volume
`ORICO`), `xhci98.sys`, `xhciuas.sys`, `xhcicm.sys` and `stor98.sys` the
package's; the copies to the tunnelled drive, the stick and the UAS disk
passed, and again after the QEMU controller's disable / enable and after
its remove / rescan (the tunnel's devices stayed up). Uninstall exit 3;
after the cold start the router (`1B21:2425`), the ASMedia xHCI (`2426`)
and the NVMe function at Code 28, the switch's bridges at Code 0 under
Windows' own `pci`.

**The update over `3.4.0.0`: 7 SP1 x64.** The published `3.4.0.0`
(`releases\3.4.0.0`, its certificate trusted with `certutil`, test
signing on): exit 3, every device Code 0, the files `3.4.0.0`'s, copies
passed. Then the tip's package, signed in the guest as above:
`INSTALL /console /yes /nopause` and with `/force` both exit 4: "The
driver store has a DIFFERENT build of xhci98.inf with the same INF ...
the same DriverVer: Windows would install the stored files, not this
package's. Its devices are skipped; run INSTALL.BAT /restage" (and the
same for `xhciuas.inf`), the `3.4.0.0` files kept: the designed answer,
since the tip carries the same version. With `/restage` on a second fresh
overlay: exit 3, "the stored copy is replaced, and every device of
xhci98.inf reinstalled", the loaded images the tip's after the cold start;
then the copies, the sequence and the uninstall as in the 7 row, exit 3,
nothing left. In the first overlay the tip's uninstaller removed the
`3.4.0.0` install, exit 3. The real update (a higher version over
`3.4.0.0`) can be read only on the cut.

**XP x64.** The first two tip runs stopped at exit 2 ("Installing USB
Attached SCSI HBA ... FAILED (error 0x800B0109)", then the same for the
root hub). The second run's `setupapi.log` shows the cause: "#E368 ...
blocked (Policy=Warn, user said no)": the kit's blind key loop (Shift+Tab
and Space every 15 s) had pressed STOP Installation on Windows' Logo
testing question. Not the driver. With a key loop that presses only when
the question is on the screen (its warning icon found in a screendump),
and the Found New Hardware pages answered the same way, the tip installed
with exit 3 (its first run exit 4 behind a base wizard: the documented
guard). With no answers at all the installer waited its 10 minutes for the
root hub's wizard and stopped with exit 4, as designed.

**The `3.4.0.0` controls** (the published package, the same kit, fresh
overlays): Vista x64 the same shape as the tip, the remove "restart
required" and the uninstall exit 4 on the UAS disk's pending removal; 10
x64 (`/usb`) the remove "restart required" and the uninstall exit 4 on the
same pending removal (where the tip's run completed both); XP x64 install
exit 3, then after the rescan the controller waiting on its wizard and
the uninstall exit 4, as the tip. No difference that points at a Phase 46
change.

**Not read here:** the window installer (the Summary page's wording of
`707a52fa`: these legs use the console); Thunderbolt networking; sleep;
the USB4 leg on 8, 8.1 or 10.

**Result.** No regression on the 64-bit targets. Evidence:
`E:\xhci98-vm\p46\w46-mx64\` (`w7\`, `v64\`, `w8\`, `w81\`, `w10\`,
`xp64f\`, `w7tb\`, `upd\`, `upd2\`, the controls `v64c\`, `w10c\`,
`xp64c\`, and the runs given up `w7-x1\`, `w10-x1\`, `xp64-x1\`, `xp64b\`,
`xp64d\`, `xp64e\`, `upd-x1\`: each `serial-*.log`, `leg.log`, `pids.txt`;
`xp64b\setupapi-s2.txt`; `shots\`; `scripts\` (the kit, `an.py` the
summaries, `xpans.py`), `xfer\`, `xfer2\`, `STATE.md`).

## 46.12 on NT: the board's namespace audited (2026-11-03, w46-acpi)

The owner agreed (2026-11-03) to an audit before Q15 (record 32 section
12.8.13.3) is answered: does the board's ACPI namespace offer a route
through the documented interfaces (`IOCTL_ACPI_EVAL_METHOD` or `_EX` on a
device's own stack, ACPI-WMI) that sends the handover without the
`_OSI` gate, or returns what it needs, so that no firmware memory is read
on NT? Off the tree, on development host A: the board's decompiled tables
(the DSDT and fourteen SSDTs of the 2026-11-02 captures; no table loaded
at run time) read whole, with a call graph over every method name.
Nothing of the driver changed; no build, no VM. The result is record 32
section 12.8.13.10, reviewed and revised.

### What was read

- **The path's devices** (root port, the switch's upstream port, its four
  downstream ports, the xHCI, the NHI): wake and sleep states, `_STA`,
  `_INI`, `_REG`, `_DSW`, `_PS0` and `_PS3`, a bookkeeping power
  resource, `_DSD` properties; no `_DEP`, no `_OSC`. One `_DSM`, the
  NHI's, under an unpublished UUID: sleep-entry notifications only, and no
  function 0 buffer.
- **WMI**: four `PNP0C14` devices; no method of any reaches the sender
  method or the firmware's data. One block of the board maker's
  interface accesses raw hardware, the configuration space of any
  function through ECAM included: it could address the mailbox, but it
  gives no firmware data, is ECAM access and has no published contract.
- **The sender method** `\_SB.M625` holds no `\M046` test; the test is in
  its `_OSC` callers. Its other callers (each function's `_PS0` and
  `_PS3`, the NHI's `_DSM`) send other, unpublished commands, ungated.
- **The data**: the command's data byte, the grant byte and the root
  port's location are returned by no method.
- **The interfaces** (Microsoft Learn): `IOCTL_ACPI_EVAL_METHOD` from 2000
  (a method that is an immediate child of the device's node), `_EX` from
  Vista (the device or a descendant), IRQL below `DISPATCH_LEVEL`, the
  device's own PDO; nothing outside the device's namespace, no write of a
  namespace object; an ACPI filter only on a described, permanently
  connected device, its presence not to be assumed.

### Verdict

(a) No route sends the handover; (b) no route returns its data. Q15's
options and recommendation stand. For B or C: the `_PS0` and `_PS3` of
the path's functions write the same mailbox on any ACPI system that
evaluates them (the firmware's own lock, no `_OSI` test), so a driver's
exchange must keep away from those transitions or treat a collision as a
failed exchange (record 32 section 12.8.13.10). Whether 2000 to 10
evaluate them on the board, and send those commands at boot, is not
observed.

Evidence: `E:\xhci98-scratch\p46-w46-acpi\AUDIT.md` (with the tables'
method names and line numbers; board data, not tracked), `tools\` there
(the call graph), the review in that folder.

### Left

Q15 and Q16, as before.

  The owner's to choose; 46.8 is not ticked. (The owner, 2026-11-03:
  the cap stays at twice the known work; XP's last step, about 9% of
  the bar, accepted, and 46.8 ticked.)

## A resident xhciuas.sys replaced on 98 SE asks for the restart (2026-11-03, w46-inst2)

Since `edcddc2b` (task 46.20) `xhciuas.sys` registers no `DriverUnload`
on 98 SE and ME and stays resident for the boot, as `xhcicm.sys` does
there. A package of the same `DriverVer` with other bytes reinstalls no
device, and the run's placement in `SYSTEM32\DRIVERS` (record 26 section
5.0, draft 38) put the new file over the old and asked for no restart,
so the disks plugged in afterwards kept running the old image. Branch
`w46-inst2` from `01af7ae5`; commit `78dd6236` (record 26 draft 68, the
code and the host vectors `test_resident`), reviewed and revised. A file
in place with other bytes, `xhciuas.sys` or `xhcicm.sys`, is now always
replaced at the next start (`<name>.IN$` beside it and a `WININIT.INI`
`[rename]` line), the restart flagged in the run, and said in the
Summary and the console's plan; a later run finding the queued copy keeps
it if it is that package's and takes it back otherwise.

Gated x86 `release` of `78dd6236`: PASSED, `test_inst` 8141 checks, the
window self-test 888 checks, 0 failures
(`E:\xhci98-scratch\p46-w46-inst2\gated-x86-78dd6236.log`). Test cut
exit 0; hashes `E:\xhci98-vm\p46\w46-inst2\hashes-78dd6236.txt`
(`xhciuas.sys` `18bde20c...`; the published `3.4.0.0`'s `14200854...`,
the same version).

The leg: a fresh 98 SE overlay of the q35 base (no USB4 model), QMP
49316, starts and stops in `E:\xhci98-vm\p46\w46-inst2\pids.txt`, screens
in `shots\u98\`, the guest's log in `shots\u98\serial-c.log` and
`serial-d.log`.

| Step | Read |
|---|---|
| The published `3.4.0.0` (the download with the driver packs) installed, Restart later, a cold start | `xhciuas.sys` placed, 40,900 bytes |
| A UAS disk plugged (`usb-uas` and `scsi-hd` on `qemu-xhci`), Windows' wizard through | the disk as E: (`uas-mounted.png`); then unplugged |
| Setup of `78dd6236`, same boot | the Summary "- xhciuas.sys (UAS disk driver): replaced, loaded at next start" (`n-sum.png`); no helper job ran; the log "placed9x: WININIT.INI [rename] C:\WINDOWS\SYSTEM32\DRIVERS\XHCIUAS.SYS=C:\WINDOWS\SYSTEM32\DRIVERS\XHCIUAS.IN$" and "Setup will replace C:\WINDOWS\SYSTEM32\DRIVERS\xhciuas.sys at the next start. Restart Windows to load it."; Finish with the restart words, "Restart Windows later" preselected because a stale controller entry left a step (`n-fin.png`); exit 4; `XHCIUAS.IN$` 40,935 bytes beside the old file |
| Shut down, a cold start | `XHCIUAS.SYS` 40,935 bytes, `fc /b` against the package's: no differences; `WININIT.INI` consumed |
| The UAS disk plugged again | E: mounted (`uas-mounted-after.png`): the image loaded is the one on disk since the start, the package's |

### The uninstall and a second installer (draft 69, `71506436`)

The integration review of `78dd6236` found that an uninstall before the
restart stopped at its pending-rename guard on the queued line and left
the `.IN$` and the line, and that two installers could meet on the
shared `.IN$`. Commit `71506436` (record 26 draft 69), reviewed and
revised: the 98 SE and ME uninstall holds the placement lock
`XhciinstPlaced9x` throughout and withdraws this installer's queued
replacement (the line, then the copy) before the guard, stopping for
another's line naming either file; the install inspects, takes back,
stages (`<name>.IS$`, exclusive creation, compared), publishes
(`<name>.IN$`, a rename that never overwrites) and writes the line under
that lock. Host vectors for the decision (`inst_resident_queued`);
`test_inst` 8148 checks, 0 failures. Gated x86 `release` of `71506436`:
PASSED, the window self-test 888 checks
(`E:\xhci98-scratch\p46-w46-inst2\gated-x86-71506436.log`); test cut exit
0, hashes `E:\xhci98-vm\p46\w46-inst2\hashes-71506436.txt`.

The leg, a fresh 98 SE overlay, QMP 49317, screens and the guest's log
in `E:\xhci98-vm\p46\w46-inst2\shots\v98\`:

| Step | Read |
|---|---|
| The published `3.4.0.0` installed, a cold start, a UAS disk plugged (E:) and unplugged | as in the leg above |
| Setup of `71506436`, same boot | the Summary "replaced, loaded at next start"; `WININIT.INI` `[rename]` `...\XHCIUAS.SYS=...\XHCIUAS.IN$`; `XHCIUAS.IN$` 40,935 bytes beside the old 40,900; no `.IS$` left; Finish with the restart words |
| The uninstall (`XHCIWIZ.EXE /uninstall`), same boot | "Withdrew the queued replacement of C:\WINDOWS\SYSTEM32\DRIVERS\xhciuas.sys." first; the devices removed; "Deleted C:\WINDOWS\SYSTEM32\DRIVERS\xhciuas.sys (placed there by Setup)."; "Done. Please restart Windows now.", exit 3; `[rename]` empty, no `XHCI*` file in `SYSTEM32\DRIVERS` |
| Shut down, a cold start | still no `XHCI*` file in `SYSTEM32\DRIVERS`: nothing came back |

A later merge review found the lock released after the hash was read but
before its `Placed9x` record was written; the fix that follows this
section holds it through the record's write and `RegCloseKey`, released
on every path (reviewed and revised; ordering only, no leg rerun).

## 46.21 XHCITR saves the firmware tables (2026-11-03, w46-acpidump)

Design: record 25 section 21 (revision 26) and record 32 section 12.8.14,
`45089975`, reviewed and revised (the first answer tied to
the query's epoch, the message listing only the tables saved, names
unique per stem, the root pointer kept at its whole checked length, the
DSDT sentence; the part bound held to 32 KiB in all). Code `4b0ef2c9`,
reviewed and revised (the NT list taken whole or not at all,
`/acpi` reading nothing but the interface on NT, the root pointer's
length checked, a capture with no byte kept said as no memory, the
index's line ends, the message buffers, NULs inside an id).

### What changed

- `xhcicm.sys` (98 SE and ME): the handover's discovery (`cm_oscos.c`)
  copies what it already reads - the root pointer, the root table, each
  entry's header in the `MCFG` pass, `MCFG` and the three named SSDTs
  whole - from the buffers it read them into; no read added, nothing it
  decides, logs or sends changed (the 46.12 start below: the same notes,
  the handover sent, the router answering). The copies become the body
  of `CM_QA_IOCTL` (`0x00222910`), a fifth request of the query device,
  answered a part at a time, header and part at most 32 KiB
  (`cm_qacpi.c`, `cm_qryos.c`).
- `XHCITR`: File > Save Firmware Tables... and `XHCITR /acpi <folder>`;
  on NT `EnumSystemFirmwareTables` / `GetSystemFirmwareTable` looked up at
  run time (the import list unchanged), on 98 SE and ME the request
  above; one file per table saved whole (`<signature><n>.DAT`) and
  `TABLES.TXT`, nothing written where a name exists (`tr_acpi.c`,
  `xhcitr.c`).

### Checks

`test_tracpi` 1058 checks, 0 failures, over made-up tables (`TESTOE`,
`TESTTBL1` ...): the body read back through parts of five sizes (one byte to the whole), a part
past the body or of another epoch, bad shapes, the checksums (the root
pointer of revision 0 and 2, `FACS`), the names (`ASF!` and `ASF_`
apart), the index within 79 columns, every message. Gated builds of
`4b0ef2c9`: x86 release and qemu, BUILD + GATES PASSED
(`E:\xhci98-scratch\p46-w46-acpidump\gated-x86-all.log`), amd64 release
PASSED (`gated-amd64-release.log`). Binaries: `xhcicm.sys` qemu x86
`b2f25f14...`, release x86 `627eb4d4...`, release amd64 `c330c318...`;
`XHCITR.EXE` `3a11a8ae...`, `XHCITR64.EXE` `10be6d22...`.

### The legs

Development host A, TCG. Evidence `E:\xhci98-vm\p46\w46-acpidump\`
(`l98\`, `x1\`, `s7\`, `t10\`: screens and `serial-*.log`), `pids.txt`.

| System | Read | Result |
|---|---|---|
| 98 SE without ACPI, 640 by 480 (an overlay of w46-cm2's `lg\disk.qcow2`, this build's `qemu` `xhcicm.sys` and `XHCITR.EXE` copied in; the emulator's ASM4242 with the handover mailbox and w46-cm3's build 1 SSDTs, as in 46.12) | the start | the three SSDTs' ids, tier 2, `cm.osc.handover=00000100`, `cm.osc.query=000F0100`, the router answering (`u4.host.id=174C2428`): as before |
| the same | File > Save Firmware Tables..., C:\ | "This computer's firmware (ACPI) tables were saved in C:\: RSDP, RSDT, MCFG, SSDT (3), and TABLES.TXT listing them. Windows 98 SE / ME give no access to these tables; these are the ones xhcicm.sys read for the USB4 handover. 4 more are listed in TABLES.TXT, not saved." in a box that fits the screen |
| the same | `XHCITR /acpi c:\t1` | the same message; `RSDP1.DAT` (20), `RSDT1.DAT` (68), `MCFG1.DAT` (60), `SSDT1.DAT` to `SSDT3.DAT` and `TABLES.TXT` (FACP, APIC, HPET, WAET "not saved", headers alone, checksum "-"; every table saved "ok"; state 1, outcome 0, tier 2); each SSDT `fc /b` identical to the file QEMU was given |
| 32-bit XP (no package installed; the tool from the transfer disk) | `/acpi`, then the item | "No tables were saved: this Windows gives programs no way to read the firmware (ACPI) tables. XP x64, Vista and later do." (exit 1), the same in the box |
| 7 x86, 96 DPI, a standard user's prompt | `/acpi c:\t7` | APIC, FACP, WAET, HPET, MCFG and DSDT saved, every checksum "ok"; DSDT not in Windows' list and given by name |
| the same | the item: C:\ | "Saving stopped at APIC1.DAT in C:\: 0 of 7 files were written." (C:\ not writable for the user) |
| the same | the item: the user's folder, twice | saved; then "Nothing was saved: C:\Users\test already has TABLES.TXT. Pick an empty folder." |
| 7 x86, 120 DPI | `/acpi`, the item | the message and the folder dialog whole |
| 10 x64, 96 and 120 DPI (`XHCITR64.EXE`; QEMU given two made-up SSDTs, `TESTTBL1` and `TESTTBL2`) | `/acpi`, the item | MCFG, FACP, APIC, WAET, HPET, SSDT and DSDT saved; "Windows gives one table of each name: of SSDT (2), the first was saved."; `SSDT1.DAT` the 36-byte `TESTTBL1` |

The emulator's 46.12 SSDTs, given to 10 x64, stop it at boot with
`ACPI_BIOS_ERROR` (their methods are the model's, written for 98 SE
without ACPI); the 10 leg used two empty made-up SSDTs instead.

Not run: ME, XP x64 and Vista; an `xhcicm.sys` older than this
answering the request (the message is host-tested).

### Left

The wording, read by the owner (the strings are in this branch's
report).

## 46.21 option B: the DSDT and every SSDT on 98 SE and ME, and the 2000 and XP registry (2026-11-03, w46-acpidump)

The owner chose option B (2026-11-03): on 98 SE and ME the save holds the
DSDT and every SSDT whole. The owner then moved the reader to
`xhci98.sys`, which every machine with this package's controller driver
runs, and added the copies 2000 and 32-bit XP keep in the registry.
Design: record 13 section 8.13, record 32 section 12.8.16, record 25
section 21 item 9, `3305bb51` (it supersedes `0add9281`, the first draft
with `xhcicm.sys` as the reader), reviewed and revised. Code `b9281036`,
`331baa13`, reviewed and revised.

### What changed

- `xhci98.sys` (`src\hcd_acpi.c`, over the pure walk `src\xhci_acpi.c`):
  `IOCTL_XHCI98_ACPI` (`0x00222924`, answer `XHCA` version 1) reads, on
  98 SE and ME and only when asked, the root pointer, the root table, a
  bootstrap pass of every entry's header to find `MCFG` (every entry
  classified, else refused), `MCFG`'s windows of every segment, then
  FACP, every SSDT and the DSDT the FADT names, whole, each within its
  cap and the 4 MB budget, each checked (signature, length, checksum)
  before it is kept whole; held until its last part is fetched, a
  release, or the controller's stop or removal; NT answers "not served
  on this Windows". `XhciAcpiTables` (1, 0 refuses it) is written by
  both halves of `xhci98.inf` and by `xhci98-amd64.inf`, with its gate
  rows and its `tuning.md` row. `xhcicm.sys`'s start-time copies are
  unchanged.
- XHCITR: on 98 SE and ME the set from `xhci98.sys` and `xhcicm.sys`'s
  copies merged (an identical copy noted, a different one saved as
  `<SIG><n>C.DAT`); on 2000 and 32-bit XP the registry's copies
  (`HKLM\HARDWARE\ACPI`, `KEY_READ`), each checked, the RSDT's other
  entries listed by address, counted; on NT with the interface,
  `XSDT` then `RSDT` asked for by name, the root table's entries listed
  and the SSDTs Windows withheld counted. `TABLES.TXT` gains a "From"
  column and each table's reason when not saved.

### What was read first, read-only

- **32-bit XP SP3** (the q35 base, development host A):
  `HKLM\HARDWARE\ACPI\{DSDT,FADT,RSDT}\<OEM ID>\<OEM table ID>\<revision>`
  value `00000000` (`REG_BINARY`, the whole table), and `FACS`'s own
  value; no SSDT, `APIC`, `MCFG` or `WAET` key, even with two SSDTs given
  to QEMU (the RSDT listing seven entries). An administrator and a
  standard user (Users alone, through `runas`) read them.
- **2000 SP4** (the ACPI base): the same keys and layout.

### Checks

`test_tracpi` 2124 checks, 0 failures: the reading over a modelled
memory (a table crossing a page, a 100 KB DSDT read in pieces whose
mappings stay within 64 KB, a table moved since its header, a bad
checksum, a cap, a FADT of 43 bytes, `MCFG` windows of another segment,
an entry not classified, two `MCFG`s, `MCFG` changed between the
bootstrap and its reread, no root pointer, the time bound in the
bootstrap and at the DSDT), its answer in parts and its generation, the
merge, the registry walk over a modelled key tree (good values, a
wrong signature, a length off, a bad checksum, a string, too many
subkeys), the NT root table's entries and the withheld count.

### The legs

| System | Read | Result |
|---|---|---|
| 32-bit XP, administrator and a standard user (`/acpi`, redirected) | the registry route | DSDT, FACP, RSDT, FACS saved, the RSDT's five entries (the FACP one of them) listed by address; the message as drafted; the DSDT (8072 bytes) and RSDT (56) byte for byte as the guest's memory at the addresses its root pointer gives (dumped from QEMU's monitor; SHA-256 `8c5b9857...`, `0ede219e...`) |
| 2000 SP4 (the ACPI base), administrator and a standard user | the same | the same four tables (its FADT the base's own 116 bytes); the DSDT (7962) and RSDT (56) byte for byte as the guest's memory (`7d094a2b...`, `fa288331...`) |
| 10 x64, two made-up SSDTs given to QEMU, `XHCITR.EXE` and `XHCITR64.EXE` | the interface | MCFG, FACP, APIC, WAET, HPET, the first SSDT, DSDT and the RSDT (asked for by name) saved; the RSDT's seven entries listed; "Windows gives one table of each name: of SSDT (2), the first was saved. 1 SSDTs were not saved." |
| 98 SE without ACPI, no `xhcicm.sys` running (no USB4 model; the three SSDTs given to QEMU), this build's `qemu` `xhci98.sys` and `XHCITR.EXE` | `XHCITR /acpi c:\t1` | RSDP, RSDT, FACP, MCFG, three SSDTs and the DSDT saved, every checksum "ok", flags 10h (whole); each SSDT `fc /b` identical to the file QEMU was given; the DSDT (8018 bytes) byte for byte as the guest's memory at the address the FADT gives (dumped from QEMU's monitor, `d018fff0...`) |
| 98 SE without ACPI, the ASM4242 model, this build's `qemu` `xhci98.sys`, `xhcicm.sys` and `XHCITR.EXE` | the start, then `/acpi` | 46.12 as before (`cm.osc.tier=2`, `cm.osc.handover=00000100`, `cm.osc.query=000F0100`, `u4.host.id=174C2428`); the same eight tables from `xhci98.sys`, each of `xhcicm.sys`'s copies (RSDP, RSDT, MCFG, the three SSDTs) "the same as read by xhcicm.sys at its start", so none written twice; the SSDTs identical to QEMU's files, the DSDT (8724 bytes) to the guest's memory (`c92cc38a...`) |

A start nobody asks reads nothing: `xhci98.sys` reads `XhciAcpiTables` with
its other values and adds no log record; its reader runs only inside the
request. Not run: ME, a DSDT near 1 MB, a refused mapping.

### Builds

The gated builds of `2d76bccf` (`331baa13` with the singular-form fix,
reviewed and revised): x86 release and qemu, BUILD + GATES PASSED
(`E:\xhci98-scratch\p46-w46-acpidump\gated-x86-all-6.log`; the run of
`b9281036` had failed only on the INF footprint files, regenerated in
`331baa13`), amd64 release (`gated-amd64-release-2.log`). Binaries:
`xhci98.sys` qemu x86 `1761afc8...`, release x86 `f1fe328b...`;
`xhcicm.sys` qemu x86 `d15d361f...`; amd64 release `xhci98.sys`
`57ceafed...`, `xhcicm.sys` `68b43dd0...`; `XHCITR.EXE` `bcb76511...`,
`XHCITR64.EXE` `9ccfaf0f...`. The XP, 2000 and 10 legs above ran on the
`XHCITR` built from `331baa13` (`7ec4d3ed...`, `1ecd2d6b...`), which
differs from `2d76bccf`'s only in the two singular forms.

The registry route's index then listed the RSDT's five entries as rows "not
kept by this Windows", the FACP's among them; the next commit lists them by
address, counted ("The RSDT lists 5 tables, at these addresses; the registry
keeps 1 of them, so 4 were not saved:"), since the registry gives no address
with a table. Built with `xhcitruild.cmd` (`XHCITR.EXE` `14aa41ed...`,
`XHCITR64.EXE` `d46070fa...`), its self-test and the host tests passed; the
XP administrator leg read again on it gave that list and the four tables, and
the message without "more are listed".
Then each signature counted once (two registry copies of one table):
`XHCITR.EXE` `99c91fd8...`, `XHCITR64.EXE` `e679af44...`, its self-test and
`test_tracpi` passed; with one FACP, as on these guests, the list reads the
same.

### Left

The wording, read by the owner (`STRINGS-FOR-OWNER.md` items 54 to 64).

## 46.12 on NT, B and C (2026-11-03, w46-nthand)

The owner's answers of 2026-11-03 to record 32 section 12.8.13's Q15
(B, then C, then D for 2000 and 32-bit XP; B on x86 too) and Q16 (a);
the design in record 32 section 12.8.15 (draft 11), not converged; no
product code, by the coordinator's word, until the bench has read the
test image of section 12.8.15.9. Work on development host A.

### The interface, read

A test program of this run's own (not tracked) calling
`EnumSystemFirmwareTables` and `GetSystemFirmwareTable` for the `ACPI`,
`FIRM` and `RSMB` providers, run from the transfer disk on two overlays
of the 10 x64 q35 bases, no package installed, its output to COM1:

| Boot | `ACPI` listed | asked by name | `FIRM` |
|---|---|---|---|
| legacy BIOS (`w10x64`) | `MCFG`, `FACP`, `APIC`, `WAET`, `HPET` | `RSDT` 56 bytes; `XSDT` and `RSD ` error 1168 | `000C0000` and `000E0000`, 131072 bytes each; the root pointer at `F52C0h` (revision 0, its RSDT at `7FFE21F5h`) |
| UEFI (`w10x64-uefi`) | the same and `BGRT` | `XSDT` 84 bytes; `RSDT` and `RSD ` error 1168 | none (error 1) |

`MCFG` and `FACP` were given by name on both; the guests had no SSDT.
`RSMB` gave the SMBIOS tables (type 0: vendor, version, date) on both.
With 46.21's readings (one SSDT of each signature, the first): the root
table is given by name though not listed, the three named SSDTs are not.
Evidence: `E:\xhci98-vm\p46\w46-nthand\p10\probe-bios.log`,
`p10u\probe-uefi.log`, `pids.txt`.

### The test image on the emulator

The test image of record 32 section 12.8.15.9 (scratch branch, never
merged; gated amd64 release build passed; the unsigned image's SHA-256
`9c884b49f324006f60ad06f5ffa4bb385c002ad55b3facfd293fa1f9f7a736a5`), on
an overlay of the 10 x64 q35 base (build 19045) with the ASM4242 model
and its handover mailbox (`fw-handover=on`, `fw-handover-gate=on`, no
SSDTs), the enclosure at power-on: `3.4.0.0` installed, the image signed
in the guest with a throwaway certificate and put in place (the
installed file in use: renamed first), cold start.

| Leg | Result |
|---|---|
| ASM4242 | the q35 host found, ECAM `B0000000h`, data 0, the mailbox clean; `cm.osc.handover=00000100`, `cm.osc.query=000F0100`; the router answering (`u4.host.id=174C2428`), both tunnel ports active, the enclosure's PCIe switch behind the tunnel; problem 0 |
| an Intel USB4 host (`-Usb4 adl`), the same image | no `cm.osc` line; the router answering (`u4.host.id=8087463E`), inert reason 0 |

A warm reset (QMP) of these guests left a black screen with this model,
with or without the tables; every start here was cold. Two cold starts
with build 1 `--osi` tables reached the desktop. Evidence:
`E:\xhci98-vm\p46\w46-nthand\s1\` (`t10-report-e.log`,
`t10-report-f-adl.log`), `e1a\`, `e1b\`, `pids.txt`.

### The test image on the owner's bench (2026-11-04)

The owner's board, Windows 10 x64 22H2 (19045), CSM, Above 4G Decoding
on, the image signed by the owner and put in place of `3.4.0.0`'s
`xhcicm.sys`. Before (`3.4.0.0`): inert reason 12, `tbt.ring.tx-stuck`.
After: `cm.osc.test.ecam=E0000000`, `cm.osc.mb.before=00400094` (the
firmware's own `_PS0` command of the NHI, accepted just before ours),
`cm.osc.handover=00000100` (accepted within 1 ms), `cm.osc.query=000F0100`,
`u4.host.id=174C2428`, the enclosure's PCIe switch behind the two tunnel
ports, its drive found, two replugs survived. Evidence: the owner's
three XHCITR reports (before, after1, after2), kept on the
maintainer's disk.

### Left

The bench read the test image (above);
the design's review to approval (the owner answered Q17 to Q20, 2026-11-04), the code,
host vectors, the builds and the NT legs.

The three handover values were renamed on 2026-11-04 (owner), before any release carried them: `XhciCmPlatformHandover` is now `XhciCmAsm4242Handover`, `XhciCmHandoverStructural` `XhciCmAsm4242Structural`, and the new reads value `XhciCmAsm4242Reads`; the sections above keep the names they were written with.

## 46.22 the Thunderbolt box the user's, and the tools alone (2026-11-03 and 2026-11-04, w46-inst3)

Design: record 26 drafts 70 and 71, section 5.3.7, record 23 revision 52
(section 8.5), reviewed and revised (the USB box's click restoring the
Thunderbolt box on 8 to 10, `XHCIPFW.SYS`'s removal and the end-of-run
upkeep in a run of the tools alone, another driver set's staging, the
Summary's "drivers stored for later", `/notbnet`'s write and its device
refresh). Code `82040a1e`, `3fa7fc28`, `d2ebfcae`, `cd18b92d`,
`d52196ad`, each reviewed and revised.

### What changed

- The Components page: the USB drivers' box locks the Thunderbolt box on
  no system (owner, 2026-11-03 for 8, 8.1 and 10; 2026-11-04 for every
  system). The line under the USB drivers' box follows both boxes
  (`inst_drivers_line`), with a text of its own for both unticked.
- A run with no driver of the package (`tools_only_run`): on NT no local
  copy, nothing staged, no device read or touched, none of the end-of-run
  driver upkeep; on 98 SE and ME (`run_tools_only_9x`) no package copy and
  no `DevicePath` change, the helper, the devices and the driver files
  left as they are, a folder move refused. The Summary, the console's
  go-ahead and the plan say so; the end is "Done. No restart is needed.",
  exit 0, unless a step flagged a restart (`inst_final_tools`).
- The console on 8, 8.1 and 10 asks the Thunderbolt question, Enter for
  Y (`inst_tb_alone`); no switch added (owner, 2026-11-04).
- Stor98's line under its box on Windows 10 (`inst_s98_line`), one line:
  the first gated build's window self-test found two lines pushed the
  Thunderbolt and USB4 page's Settings... off its foot.

### Checks

- Host: `test_inst` 8221 checks, 0 failures (`test_tb_lock_4622` and the
  98 SE and ME warning's vectors).
- Gated x86 release build at `cd18b92d`, every step passed, the window's
  self-test included (development host A). Gated amd64 release build at
  `d52196ad`, every step passed (the window's self-test on the rebuilt
  installer).
- `scripts\check-text.ps1`: OK.

### Legs (q35 bases, overlays, development host A; the emulated ASM4242 USB4 host present)

Kit: the x86 family layout of the `cd18b92d`/`d52196ad` build (window
`XHCIWIZ.EXE` sha256 `dc48a12e...`, `xhci98.sys` `7381665f...`,
`xhcicm.sys` `c11f01d6...`, unchanged drivers), `SETUP.EXE` at the root.

| System | Steps | Result |
|---|---|---|
| 8.1 x86, fresh overlay | Components: USB drivers unticked, the Thunderbolt box ticked and open; untick it | both unticked, the box open; the Summary "Install/update only the xHCI98 tools now (no driver changes).", "- none changed (no driver in this run).", "not installed (xHCI98 tools only)" |
| | Install | "Done. No restart is needed.", exit 0, no restart buttons; log "tools only: no local copy, nothing staged, no device touched"; no `%windir%\XHCI98`, no `xhcicm` or `xhci98` service; `C:\XHCI98` the three tools |
| | again, the Thunderbolt box ticked alone | as before: the CM, its filter and Stor98 staged (oem1 to oem3), the tunnel-port filters added, `XHCIPFW.SYS` installed, "Done. Please restart Windows now." |
| 8 x86, fresh overlay | the Thunderbolt box ticked alone | as before (the page's 8.1 Stor98 line, the filters, the restart asked) |
| | again, both unticked | "No changes were made.", exit 0: the tools up to date, `xhcicm` running and untouched |
| 7 x86, fresh overlay | both unticked (the USB box's line "Only the xHCI98 tools will be installed. ..."), Install | the Summary as on 8.1; "Done. No restart is needed.", exit 0; no `%windir%\XHCI98`, no service |
| 98 SE, fresh overlay | both unticked: the warning's tools form; Install | "Done. No restart is needed.", exit 0; `C:\XHCI98` holds `XHCITR.EXE`, `XHCIWIZ.EXE`, `XHCIINST.EXE`, `XHCIDI.EXE` alone; log "tools only: no package copy, DevicePath and devices left as they are" |
| | cold start, again both unticked | the USB Tree's icon at login; the Summary without the Add New Hardware paragraph (`d52196ad`); "Done. No restart is needed." |
| | `XHCIWIZ.EXE /uninstall` | "DevicePath needs no change.", the tools, the Add/Remove Programs entry and `C:\XHCI98` removed |
| 10 x86, fresh overlay | the Thunderbolt and USB4 page, the Thunderbolt box ticked | Stor98 unticked, its line "Optional on Windows 10: stops drives cleanly when unplugged." (8.1's and 8's: "For NVMe drives in Thunderbolt / USB4 enclosures.") |
| | Back, both unticked, Install | the Summary as on 8.1; "Done. No restart is needed.", exit 0 |
| 10 x64 | the x64 kit | not run: a package staged without the test key carries no catalog, and Setup refuses it ("this 64-bit package carries the Thunderbolt driver without its catalog, xhcicm.cat"); owed on a signed package |

On the first build (`3fa7fc28`) the 8.1 run of the tools alone ended
"Done. Please restart Windows now.", exit 3; `d2ebfcae` gave it its own
end. On `cd18b92d` the 98 SE Summary still carried the Add New Hardware
paragraph and the plan the copy's sentence; `d52196ad` removed both.

Evidence: `E:\xhci98-vm\p46\w46-inst3\` (`w81\`, `w8\`, `w7\`, `w98\`,
`w10x86\`, `w10\` the refused x64 kit; screenshots by step;
`old\w81-run1\` the first build's 8.1 run).

### Left

The 10 x64 leg on a signed package; the new and changed texts, read by
the owner (the branch's report lists them).

### Copied-only rows and the Thunderbolt box over an installed part (2026-11-04, w46-inst5)

The owner's decisions of 2026-11-04 (record 26 draft 74, sections 5.3.8
and 6.7.15); code `198ee639`, `9282757c`, `d1096384`, `f880e609`,
reviewed and revised.

#### What changed

- The Free and Non-free USB drivers pages list, under each heading after
  the stageable packages, every package this Windows copies only (no
  `STAGE` line, a `COPY` line of an INF with a `[Manufacturer]` section,
  read in ASCII, UTF-8 or UTF-16) as a greyed box and its words in a
  disabled read-only edit control, which breaks a long folder path: "<the
  package's description>: copied only on this Windows. Install it by hand
  from <folder>." The console prints them under "  Copied only on this
  Windows:". 98 SE's and ME's "Copied only (no automatic install on this
  Windows):" block is gone; the sentence for the rest stays.
- Over an installed Thunderbolt part (`xhcicm` a devnode's service or
  upper filter) the box starts ticked and the console's question defaults
  to Y, so `/yes` updates it ("  /yes: Y (the installed Thunderbolt / USB4
  support is updated)."), but over Intel's driver and where Secure Boot
  would refuse the connection manager; `xhciinst\README.TXT` says so.

#### Checks

Host: `test_inst` 8583 checks, 0 failures, with `test_copy_rows_4622`
(a fixture `STAGE.TXT` of made-up packages: staged here no row, a copied
driver INF a row, a co-installer alone none, two INFs in one folder one
folder, "A or B", four folders and the `STAGE.TXT` ending, UTF-16 and
commented headers) and the decision's vectors (installed: asked Y, `/yes`
and `/list` Y, not over Intel's driver, unchanged when not installed).
The window self-test (969 checks, 0 failures in the gated x86 build of
`87ade048`): the box ticked over an installed part and not with Secure
Boot refusing, over Intel's driver or with no driver; every package's
copied-only row with the packs' longest folder whole in the list's pane.

#### The legs (q35, fresh overlays, the test cut of `87ade048` with both driver sets)

`make-release.ps1 -Arch x86 -TestLabel inst5` with usb-wdm-free and
usb-wdm-nonfree (`XHCIWIZ.EXE` `c725846d...`; hashes
`E:\xhci98-vm\p46\w46-inst5\hashes-xf86-87ade048.txt`).

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480 (`w98`) | Welcome; Components (the Thunderbolt box unticked: nothing installed); Free USB drivers, the list scrolled through; Non-free USB drivers, scrolled through | every page inside the screen, no scroll bar on a page; Free: "USB Ethernet (rtl815x): Realtek RTL8152, 8153, 8156, 8157 and 8159 adapters: copied only on this Windows. Install it by hand from C:\XHCI98\USBDRV\FREE\net\rtl815x\w98se-me." and under Other "No driver (noaud): leaves a webcam microphone driverless on 98 SE: copied only on this Windows. Install it by hand from C:\XHCI98\USBDRV\FREE\audio\noaud\w98se.", both greyed, whole; Non-free: none (every package copied there has a `STAGE` line) |
| 7 x86, 120 DPI (`LogPixels` 120, then a restart), 800 by 600 (`w7`) | Welcome; Components; Free and Non-free USB drivers; Summary | every window inside the screen, every text whole; Free: usbmidi (`audio\usbmidi\wv-8-x86`), cdcnet (`net\cdcnet\wv-7-x86`) and rtl815x (`net\rtl815x\wv-7-x86`) rows, greyed, whole, the folder broken at the line's end ("...\FREE\net" / "\rtl815x\wv-7-x86."); the whole Free list in its pane with no scroll bar; Non-free: none |

Evidence: `E:\xhci98-vm\p46\w46-inst5\w98\` (`a*.png`, `f*.png`,
`n2.png`), `w7\` (`b*.png`, `n2.png`), `pids.txt` in each.

#### Seen, not changed

- A path broken mid-word by the edit control (7 at 120 DPI) reads
  awkwardly; the owner may prefer the folder on a line of its own.
- The console's rows were not read in a guest (7's console needs an
  elevated prompt the driving did not get); the console prints the same
  engine text the window shows (host vectors).
- The Thunderbolt box over an installed part was not read in a guest
  (host and window self-test only).

#### The update over the USB drivers on 8 to 10, and the box by what is found (`2b25c1c0`, `bb541997`)

Found by the 3.4.1.0 update legs (10 x64 over 3.4.0.0 installed with
`/usb`, w46-upd's `w10x64-b`): the Components page opened with the USB
drivers unticked and the Thunderbolt box ticked with "No Thunderbolt /
USB4 controller found", so a click-through left `xhci98` and `xhciuas`
at the old version and added the tunnel-port filter. The cause:
`inst_usb_decide` on 8, 8.1 and 10 took the USB drivers only by `/usb`,
whatever ran the controllers, and with them out the connection manager
alone defaulted to yes (record 26 section 5.3.9). `2b25c1c0`:
`inst_usb_opt` takes them in over controllers `xhci98` runs, `/cmonly`
leaving them out. `bb541997`, the owner's decisions of 2026-11-04: with
the USB drivers out on 8 to 10 the box opens ticked only where an NHI or
a known host's switch or tunnel port is found (QEMU's own bridges not
counted), the console's Enter, `/yes` and `/list` alike, `/cmonly` there
asked as without it; elsewhere `/cmonly` as before. Both reviewed and
revised; host vectors (`test_inst` 8616 checks, 0 failures) and window
self-test cases (971 checks, 0 failures in the gated build below). The
98 SE and XP paths never took the unticked USB default (their USB drivers
are in the run by default; the 98 SE leg above shows it). Not read in a
guest on this branch: the update page on 10 x64 (w46-upd's kit). Gated
x86 `release` and amd64 `release` of `bb541997`: BUILD + GATES PASSED
(`E:\xhci98-scratch\p46-w46-inst5\gated-x86-8.log`,
`gated-amd64-2.log`).

## 46.final pre-cut matrix, x86 (2026-11-03)

The `3.4.1.0` tip `e78c8e1b` on the 32-bit systems, before the cut, in
QEMU 11.1.2 (the q35 bases' pinned emulator, `asm4242-20261025-2230`,
TCG) on development host A. Kit, scripts and evidence:
`E:\xhci98-vm\p46\w46-mx86\` (`STATE.md` holds the running notes).

**Build.** One gated x86 release build of `e78c8e1b` (gate slot 2):
"BUILD + GATES PASSED (release)"; `xhciinst` (with `di`), `xhcitr`,
`xhcimark`, `xhciperf` built; `make-package.ps1 -Flavor release -Arch x86`
exit 0. Log `E:\xhci98-scratch\p46-w46-mx86\gated-x86.log`. The tree
still stamps `3.4.0.0` (`src\xhci_version.h`): the version moves at the
cut, so every "update from 3.4.0.0" below is a same-version update.

**Kits.** `kit\PKG`: the family layout (`ConvertTo-FamilyLayout`, 98-me
and nt-x86) with `tools\` and `SETUP.EXE`/`INSTALL.BAT` at the top, 40
files (`kit-sums.txt`): `xhci98.sys` (98-me = nt-x86) `22213d61...`,
`xhciuas.sys` `c78a6241...`, `cm\xhcicm.sys` `e897d0b0...`,
`98-me\stor98\stor98.mpd` `d2a700c9...`, `nt-x86\stor98\stor98.sys`
`e769753c...`, `SETUP.EXE` `63b303e6...`, `XHCIINST.EXE` `d8736531...`.
That package carries no `USBDRV\` (no NUSB storage files for 98 SE, no
driver packs), so a test cut (`make-release -TestLabel w46mx86`, the two
driver packs) gave the downloads (`xhci98-3.4.0.0-nonfree.zip`
`00b6e32a...`); its x86 binaries equal the kit's by SHA-256, and its
unpacked `-nonfree` tree (`kitnf\PKG`) is the 98 SE and non-free legs'
package.

**A harness slip found and fixed (`9ed7f787`).** 46.18 (`308f5615`)
renamed the `/list` action of a driver at the package's version from
"current (this release)" to "Reinstall (same version)"; the release
matrix's `LEGNT.CMD` and `LEG9X.BAT` still searched the old words, so
list-current failed on every leg and list-removed passed whatever the
list said. Read by hand on the first run (two "Reinstall (same
version)" rows after each install, none after each uninstall) and fixed
for the later legs. Reviewed.

### The release matrix (`release-matrix.ps1`, the install, the stick, the uninstall)

| Leg | Result | Run | Note |
|---|---|---|---|
| w98se-console | PASS | `20261103-175455` | the first run's leg (`20261103-095247`) stalled at the first start after the install (below) |
| w98se-window | PASS | `20261103-172156` | with the `-nonfree` tree; on `kit\PKG` the stick was an Unknown Device (no NUSB storage files in that package) |
| me-console | PASS (list read by hand) | `20261103-095247` | |
| me-window | PASS | `20261103-172156` | the first run: SETUP.EXE's window not seen in 3 minutes (page key changed nothing); not seen again |
| w2k-console, w2k-window | PASS (list read by hand) | `20261103-095247` | |
| xp-console | PASS (list read by hand) | `20261103-095247` | |
| xp-window | PASS | `20261103-095247` | |
| vista-console, vista-window | PASS | `20261103-095247` | |
| w7-console, w7-window | PASS | `20261103-095247` | |
| me-nonfree, w2k-nonfree | PASS | `20261103-175455` | w2k-nonfree's first try (`172156`): the uninstall found a Found New Hardware wizard pending, exit 4; not again |
| w7-nonfree | PASS | `20261103-172156` | |
| xp-nonfree | PASS | `20261103-200934` | the earlier tries could not start (port taken) |

Run folders under `E:\xhci98-vm\p46\w46-mx86\release-matrix\runs\`.
Ports 49408 and 49409 were taken by an outbound connection of the host
during the day (they sit in Windows' dynamic range), so three legs failed
to start and ran again on other ports.

98 SE stall (`095247` w98se-console, `b2-stalled.png`): at the first
start after the console install the desktop and the poller's window
appeared, then the guest clock stopped at 9:56 and COM1 stayed silent
900 s. Not seen again in the later 98 SE first starts after an install
of the tip today (more than ten: the matrix's reruns, the device, repro
and fixed-build legs below).

### The device legs (mouse, BOT stick, UAS drive, composite audio, hub, Device Manager)

Each on a fresh overlay: the console install with the default ticks
(`INSTALL /console /yes`), a cold start, the installed images compared
with the kit's (`fc /b`), then plugged at once on `qemu-xhci` (four
ports): a `usb-hub` on port 1 with a `usb-mouse` behind it, `usb-audio`
on port 2 (behind the hub on 8 to 10, where port 2 holds the tablet), a
`usb-storage` stick on port 3 and a `usb-uas` with one `scsi-hd` on port
4 (the UAS disk is the same FAT image with its own MBR signature: Windows
7 and later take an identical twin offline). Then each device's state,
both drives read and written (`TESTDATA.BIN` copied off and back, `fc
/b`), the controller disabled, enabled, removed and found again
(Windows 2000 to 10: `devcon`; 98 SE: Device Manager by keys, Refresh
for the rescan), the devices and drives again after each, the unplug,
the uninstall, and a check that the driver is gone. Guest scripts
`kitsrc\XHLEG.CMD` (NT) and `kitsrc9x\*.BAT` (9x); host `tools\dleg.py`,
`tools\seq98.sh`, `tools\dmfull.sh`.

| System | Result | Evidence |
|---|---|---|
| 98 SE | install exit 3; `XHCI98.SYS` = kit; every devnode problem 0 (`HKEY_DYN_DATA`): xHCI98 hub, composite parent and audio interface, BOT (`USBSTOR`), UAS (`XHCIUAS`), HID mouse; both drives read and written before, after Disable/Enable and after Remove/Refresh; then **FAIL** at the unplug (MATRIX FAIL 1 below) | `dev\w98se-dev\` |
| XP | install exit 3; `xhci98.sys` = kit; all devices running (`USB\XHCI98_HUB&...`, `XHCI98_COMPOSITE`, `USBSTOR`, `XHCIUAS`, HID); drives read/written at each step; disable, enable, remove, rescan all PASS; uninstall exit 3, `xhci98.sys` gone | `dev\xp-dev4\` |
| Vista | the same, every step PASS (one extra start first to set the SuppressNewHWUI policy, as the release matrix's setup does) | `dev\vista-dev2\` |
| 7 | install, devices, drives, disable/enable PASS; after `devcon remove` and `devcon rescan` the controller comes back with problem code 1 and no device returns (MATRIX FINDING 3); uninstall PASS | `dev\w7-dev3\`, `dev\w7-dev2\` |
| 10 | the default install is the connection manager only (`xhcicm.sys` = kit, Windows' own USB drivers kept); BOT, audio, hub, mouse running, the stick read and written at each step; the UAS drive under Windows' own UAS driver at problem code 10 throughout; uninstall PASS | `dev\w10-dev\` |
| 2000 | not run: WDK 7.1's `devcon.exe` (subsystem 5.01) does not start on 2000 ("not a valid Win32 application"); the release matrix's 2000 legs cover the install, the stick and the uninstall | - |
| ME, 8, 8.1 | not run (time) | - |

**MATRIX FAIL 1 - 98 SE: fatal exception 0E at 0028:FF058D80 after the
UAS drive's removal.** After the device set, Device Manager's Disable,
Enable, Remove and Refresh (every check passing), unplugging the devices
gave "A fatal exception 0E has occurred at 0028:FF058D80", CR2 =
FF058D80 (the instruction address itself unmapped: a call into a freed
image).

| Run | Build | Sequence | Unplug | Result |
|---|---|---|---|---|
| `w98se-dev` | tip | full | all, 4 s apart | fault |
| `r3` | tip | no Device Manager steps | all, 4 s apart | no fault |
| `r4` | tip | full | all, 4 s apart | fault; the screen first in the shot 4 s after the audio unplug (8 s after the UAS one) |
| `r5` | tip | full | **the UAS device alone**, 60 s | **fault** |
| `c1` | published 3.4.0.0 | full | one at a time, 30 s apart | no fault |
| `c3` | published 3.4.0.0 | no Device Manager steps | all, 4 s apart | no fault |
| `c4` | published 3.4.0.0 | full | all, 4 s apart | no fault |
| `f2` | tip + `edcddc2b` (`xhciuas.sys` resident on 9x) | full | the UAS device alone, then the rest | no fault |
| `f6` | tip + `edcddc2b` | full | the UAS device alone, then the rest 60 s apart | no fault |
| `f6`, again | tip + `edcddc2b` | a cold start, then the hub, mouse, stick and UAS drive (no audio device) | the UAS device unplugged, plugged again (both drives read and written), unplugged again | no fault |
| `f7` | tip + `edcddc2b` | full | all, 4 s apart | **fault**: "fatal exception 0E at 0028:FF05808A", CR2 = FF05808A, first in the shot after the mouse's unplug (13 s after the UAS one) |
| `f8` | tip + `edcddc2b` | full | one at a time, 60 s apart, **the BOT stick first** | **fault**: "fatal exception 0E at 0028:FF058DA0", CR2 = FF058DA0, in the shot 60 s after the stick's unplug alone (nothing else unplugged) |

The first reading, made before any image base was known (superseded in
part by the 46.20 follow-up below, whose instrumented runs place the
second `xhciuas.sys` below the freed range and show pages going away
minutes before an unplug): with the tip, the faulting image was taken
to be the one freed at the UAS removal, `xhciuas.sys`,
which on 9x was unloaded from the last device object's deletion while
its own recovery or wake worker was still returning through it
(`edcddc2b`, task 46.20). The source between the published `3.4.0.0`
and the tip has no change in `src\uas`; that 3.4.0.0 did not fault in
its two full-sequence runs is a small sample of a race, not a sign that
it is free of it. No image base was read (the release build logs none;
no memory dump of the guest was taken). With `xhciuas.sys` kept resident
the UAS removal alone no longer faults (two of two), but `f7` shows a
second call into freed memory, at another offset of the same page,
after the rapid removal of all five devices, and `f8` the same class at
FF058DA0 from the Bulk-Only stick's removal alone: a second image
unloaded under a caller on the BOT stick's removal path (its storage
class drivers or the bus's path to them), not yet named. Evidence:
`dev\w98se-dev\u0.png`, `dev\w98se-r4\final.png` and `un-*.png`,
`dev\w98se-r5\un-ua1.png`, `bsod-regs*.txt`, `seq.log` in each folder.
A related trap: Windows 98's own `USBAUDIO.VXD` faults with QEMU's audio
device when that device returns with a stored assignment ("fatal
exception 00 ... in VXD USBAUDIO(01) + 00002ED4", `dev\w98se-r1\x1.png`),
the documented class (lessons.md, "plug the audio device once per
guest"); the fault above is not that one.

### 46.20 follow-up: an unplug after the controller's Remove and Refresh on 98 SE (a Windows 98 SE limitation)

After Device Manager's Remove of the controller and its Refresh, a
later unplug can stop 98 SE with "A fatal exception 0E has occurred at
0028:FF058DA0" (CR2 = EIP). This is `f8` above, and the BOT stick's
unplug alone is enough. A change to `xhci98.sys` is not needed for it:
it faulted with 3.4.0.0's `xhci98.sys` (`h2`), and with the whole
3.4.0.0 package when only `edcddc2b`'s `xhciuas.sys` was swapped in
(`u6`). The late page loss below also happens with the unmodified
3.4.0.0 package, which did not fault in seven runs. So whether this
release makes the fault more likely (a changed file moving what loads
where) is open, and the regression status stays provisional. In the
traced runs the fault came only when Windows had loaded `hidusb.sys` next
to the memory of the unloaded first controller driver. What takes the
pages away and what calls FF058DA0 are not established. The owner's
decision (2026-11-04), after the control on Windows 98 SE's own stack
below: a known Windows 98 SE and ME limitation, not fixed, and not a
blocker for 3.4.1.0; users restart Windows after removing the USB
controller in Device Manager.

The evidence comes from our own images only: test images that write to
port E9h (never merged; worktree `E:\xhci98-wt\w46-mx86-t`), page tables
from the emulator's monitor, and STOP screens. No other driver's code
was read.

- **The first instance goes at the Remove.** `xhci98-t46-base.sys`
  prints its DriverEntry address at each load: the boot instance at base
  FF03EAC0 is unloaded at the Remove, and the Refresh loads a second
  instance higher up (base FF148C60). Its range FF058000-FF085000, the
  interior of its nonpaged `.text`, then has no page mapped. Our image has
  no pageable section: `.text` and `.data` are NOT_PAGED, and only `INIT`,
  `.rsrc` and `.reloc` (from FF0860C0) are discardable.
- **The faulting address is not ours.** FF058DA0 falls in four
  different layouts of our image (3.4.0.0's among them) and in a
  different function in each. No bus-interface client calls in: the
  `xhci98-t46-busif.sys` image counts no QUERY_INTERFACE, Reference or
  Dereference on 98 SE, and its unload list is empty.
- **What lands there.** `xhci98-t46-trace.sys` logs the driver objects
  in the stack above each of its device PDOs (name, DriverInit and
  dispatch addresses) and the completion routines callers hand those
  PDOs. After the Refresh, NUSB 3.3's storage drivers and the second
  `xhciuas.sys` load into the low part of the old range, below the hole,
  in every run. Windows' `hidusb.sys` (the mouse) lands either at
  FF0E7340 or, in some runs, with DriverInit FF0578A0, next to the hole.
  The trace runs faulted exactly when it landed there (`w98se-tT3`,
  `w98se-tT6`, both FF058DA0 after the stick's unplug). The completion
  routine its stack then hands our mouse PDO with its URBs
  (IOCTL_INTERNAL_USB_SUBMIT_URB) is FF05808A, the address of `f7`'s
  fault, and FF058DA0 is DriverInit + 1500h.
- **The pages go away later.** Page tables read every 2 s
  (`tools\memwatch*.sh`) show the pages hidusb.sys was given at its load
  (up to FF05A000) become not present 7 to 10 minutes later, with no
  Device Manager step or other user action in progress and nothing new
  in our log. The 3.4.0.0 package shows the same late loss in the same
  range (`w98se-c16`, `w98se-c17`), without a fault. `w98se-tT8`
  (DriverInit FF0567C0) lost its pages the same way and did not fault.
- **The fatal part.** Our DPC, at DISPATCH_LEVEL, calls the completion
  routines that callers hand our PDOs, as completion is meant to work;
  FF05808A is one of them (`f7`). A page that is not present, met at
  DISPATCH_LEVEL, is fatal, so the fatal part is code at DISPATCH_LEVEL
  meeting pages that are no longer present, not our completion call as
  such. The caller of FF058DA0 itself was not named.

| Build (full sequence, the stick first, 60 s apart; runs where the first instance was unloaded) | Faulted |
|---|---|
| published 3.4.0.0 | 0 of 7 (`c7`-`c9`, `c14`-`c17`); a small sample, not proof it cannot |
| 3.4.0.0 with only `edcddc2b`'s `xhciuas.sys` (`x9xu`) | 1 of 5 (`u6`) |
| 3.4.0.0's `xhci98.sys` with the tip's other files (`h1`, `h2`) | 1 of 2 |
| tip test images (base print, 46.6 out, no XHCITR, polling, bus interface) | 8 of 12 |
| `xhci98-t46-trace.sys` | 2 of 11: the two runs with hidusb.sys at FF0578A0 |
| `xhci98-t46-trace.sys` with `[386Enh] Paging=Off` | 0 of 4; hidusb.sys loaded at FF0E9860 or FF0E8900, outside the old range, so these runs say nothing either way |

`edcddc2b` on this path: with no `DriverUnload` on 98 SE, the boot
`xhciuas.sys` still goes at the controller's Remove. Its pages are not
present after the Refresh (`w98se-tT1`, `-tT2`, `-tT3`, `-tT9`, `-c16`,
`-u1`), and the Refresh loads a fresh image (DriverInit FF041246, against
FF095D46 at the boot). So "resident for the boot" holds for a device's
unplug, which its own legs cover, but not for a Remove of the whole
controller.

Two test images tried keeping the first `xhci98.sys` instance through
the Remove (`xhci98-t46-res.sys`, `-res2.sys`, never merged). With no
`DriverUnload` on 98 SE (`w98se-tR1`, `-tR2`) the image still goes, and
the Refresh loads a second instance, as for `xhciuas.sys`. With, in
addition, one unnamed device object made at DriverEntry and never
deleted (`w98se-tS2`, `-tS3`), the image stays: the Refresh brings no
second DriverEntry, the new controller and its hubs start on the first
image (K d: 14 devnodes, none with a problem, both drives read and
written), and its range stays mapped. But `hidusb.sys` then reloads at
DriverInit FF09BEC0, in the range its own boot instance (FF09BAA0) left
at the Remove. Its pages FF09C000-FF09E000 go not present about 12
minutes after the Remove, and both runs stopped with "0E at
0028:FF09D3C0" after the stick's unplug, again DriverInit + 1500h. So
with `xhci98.sys` kept loaded, the same loss was seen in the reused
`hidusb.sys` range, and keeping our image loaded did not prevent the
fault. What takes the pages away stays unresolved.

A third test image refused IRP_MN_QUERY_REMOVE_DEVICE at the controller
on 98 SE (`xhci98-t46-qr.sys`, never merged; `w98se-tQR1` to `-tQR4`).
Windows 98 SE sends QUERY_REMOVE and REMOVE to every device below the
controller first, and their drivers unload, before it asks the
controller; after the refusal Device Manager shows "System Settings
Change": "To finish removing your hardware, you must restart your
computer. Do you want to restart your computer now?" (Yes, No). With No,
the Refresh brought the devices back on the same `xhci98.sys` instance,
and none of the four runs faulted, but `hidusb.sys` never reloaded at an
address that had faulted before, so the count says nothing either way.
Neither keeping `xhci98.sys` loaded nor refusing the controller's
query-remove was adopted: both leave the devices below the controller
unloaded and reloaded. Refusing QUERY_REMOVE at every device of the bus
as well would keep the subtree loaded, at the cost of a restart for every
Device Manager Remove of a USB device on 98 SE and ME; not built.

**A similar fault on Windows 98 SE's own USB stack.** The same sequence
was run with no xHCI98 driver loaded: the package installed, then the
guest started with no xHCI controller and an Intel UHCI controller
(Windows' "Standard Universal PCI to USB Host Controller") carrying the
hub, the mouse, the NUSB stick and, in the second set, the USB audio
device; Disable, Enable, Remove of the UHCI controller, Refresh, 15
minutes, then the stick unplugged first (`tools\seqctl*.sh`,
`launch-q35u.ps1`). Without the audio device: no fault in six runs
(`w98se-ctl3` to `-ctl8`). With it, all four runs that got past the
Enable stopped 8 to 10 minutes after the Remove, before any unplug: "A
fatal exception 0E has occurred at 0028:FF0421E0 in VXD USBD(01) +
00000440" (`w98se-ctla4`, `-ctla7`) and "... at 0028:FF04B2E8"
(`-ctla8`, `-ctla9`); two more stopped responding at the Enable
(`-ctla5`, `-ctla6`). The stack data words at those screens (the
owner allowed reading stack data at the fault screen on 2026-11-04; no
code was read) have the same layout as at our stack's faults at
FF058DA0 and FF09D3C0: an empty ring-0 stack above the entry and the
same words under the faulting call, among them C0001460, an address in
the VMM range. So a crash of the same shape happens after a Remove and
Refresh of the USB controller with no part of this package loaded. That
the two share one cause is the working reading, not established (the
control faulted in USBD before any unplug, ours in hidusb.sys's range
after the stick's); the audio device's part is not separated either.

**MATRIX FINDING 3 - 7: the controller at problem code 1 after `devcon
remove` and `devcon rescan`.** Two fresh overlays of the tip and one of
the published `3.4.0.0` (`dev\w7-c340\`) alike: after the rescan the
controller is "xHCI98 USB 3 Controller" (its FriendlyName gone) with
problem 01 for 180 s and none of its devices back; Windows' balloon says
the driver software installed. Not a Phase 46 regression. XP and Vista
pass the same steps. Device Manager's own Uninstall and Scan were not
tried.

### Thunderbolt / USB4 (asm4242 layout, the ORICO model with the NVMe disk at power-on)

| System | Result | Evidence |
|---|---|---|
| 98 SE | `I.BAT /thunderbolt /stor98` exit 3; at the cold start Windows' wizards for the enclosure's bridges and "Stor98 Storage Controller for NVMe SSDs" (a restart, taken: a 98 SE restart ends QEMU), then a cold start: the NVMe (`PCI\VEN_1B36&DEV_0010`) and its `SCSI\QEMU____NVME_CTRL` disk problem 0, E: read and written (`fc /b`) | `dev\w98se-tb2\` (the first try's wizards were escaped by the script: `dev\w98se-tb\`) |
| XP | not completed: the install ends with exit 4, "Windows is still installing something" - a Found New Hardware wizard for the layout's devices left pending; the script's handling did not clear it in three tries | `dev\xp-tb*\` |
| 7 | install exit 3; "Stor98 Storage Controller for NVMe SSDs" running on the tunnelled NVMe, `stor98.sys` and `xhcicm.sys` = kit, D: read and written; uninstall PASS | `dev\w7-tb\` |

### The update from the published 3.4.0.0

98 SE and XP, each on a fresh overlay: the published `3.4.0.0` (the x86
cut's `-nonfree` tree, `xhci98.sys` `6648b52e...`) installed by its
console (`/yes`), a cold start, then the tip's console install over it
(`/yes`), a cold start, the checks.

| System | Result | Evidence |
|---|---|---|
| XP | 3.4.0.0: exit 3; the tip over it: exit 3, both rows "Reinstall (same version)"; after the cold start `xhci98.sys` **still 3.4.0.0's** (differs from the kit); the device set, the drives and the Device Manager sequence then all PASS on it; uninstall PASS | `dev\xp-upd2\` |
| 98 SE | 3.4.0.0: exit 3; the tip over it: exit 4, "Nothing to do: this version already runs it"; `XHCI98.SYS` still 3.4.0.0's | `dev\w98se-upd\` |

Both as designed for two builds stamped with the same version: the tip
still says `3.4.0.0`, so the installer sees nothing newer. The update
leg owes a reading on the cut, once the version is `3.4.1.0`.

### Left

- Device legs on 2000 (no `devcon` that starts there), ME, 8 and 8.1;
  Thunderbolt legs on XP (the pending wizards), 2000, Vista, 8, 8.1 and
  10; the update legs on the cut's binaries.
- MATRIX FAIL 1's second freed image (`f7`).

## 46.final pre-cut matrix, x86: the legs owed (a22cdc71) (2026-11-04, w46-legs)

The legs the x86 pre-cut matrix of `e78c8e1b` left (its Left list): the
device legs on 2000, ME, 8 and 8.1 and the Thunderbolt / USB4 legs on XP,
2000, Vista, 8, 8.1 and 10, run once each on a build of `a22cdc71` (the
tip of `3.4.1.0` with 46.21 and the placement lock of `f0ce3df9`). The
tree still stamps `3.4.0.0`, so the update legs over the published
`3.4.0.0` are not here: they are read on the cut's binaries. Development
host A, QEMU 11.1.2 (the q35 bases' pinned emulator,
`asm4242-20261025-2230`, TCG), every leg on a fresh overlay of its q35
base. Kits, scripts and evidence: `E:\xhci98-vm\p46\w46-legs\`
(`STATE.md` holds the running notes).

**Build.** One gated x86 release build of `a22cdc71` (gate slot 2), after
`src\pfw`, `xhciinst\di` and `xhciinst` were built: "BUILD + GATES PASSED
(release)"; `xhcitr`, `xhcimark` and `xhciperf` built; `make-package.ps1
-Flavor release -Arch x86` exit 0. Log
`E:\xhci98-scratch\p46-w46-legs\gated-x86.log`.

**Kit.** `kit\PKG`: `ConvertTo-FamilyLayout` (98-me and nt-x86) with
`tools\` and `SETUP.EXE` / `INSTALL.BAT` at its root, 40 files
(`kit-sums.txt`): `xhci98.sys` (98-me = nt-x86) `fac5a1c7...`,
`xhciuas.sys` `da0e8919...`, `cm\xhcicm.sys` `ffd0f57d...`,
`98-me\stor98\stor98.mpd` `ba663696...`, `nt-x86\stor98\stor98.sys`
`1c9f85e9...`, `SETUP.EXE` `a8cc2fb6...`, `XHCIINST.EXE` `040320ba...`.
The transfer disks: `xnt\` (the NT device legs), `xnt8\` (the same with
`/usb`, for 8 and 8.1), `xtb\` (the Thunderbolt legs, `/thunderbolt
/stor98`), `x9x\` (ME).

**The harness.** w46-mx86's (`tools\dleg.py` on the host, `XHLEG.CMD` in
the guest, its starting points made with `make-start.ps1` for 2000 and
XP, the Vista to 10 guests started by keys), with these changes:

- **2000 has a device leg now.** WDK 7.1's `devcon.exe` does not start on
  2000, so `XHLEG.CMD` runs `DC2K.EXE` there (`3c3717fd...`, built with
  the in-repo MSVC 6.0 from `E:\xhci98-scratch\p46-w46-legs\dc\dc2k.c`):
  the verbs and words of `devcon` the leg reads (`find`, `status`,
  `driverfiles`, `disable` and `enable` as SetupAPI's property change,
  `remove` as `DIF_REMOVE`, `rescan` as `CM_Reenumerate_DevNode` on the
  root).
- **The Found New Hardware wizard that stays behind (XP, the TB leg's
  first failure).** In w46-mx86's `xp-tb2` the wizard of the layout's
  next device opened behind the poller's console, which kept the focus:
  the host's Escape went to the console, the wizard stayed, and the
  installer stopped with exit 4 ("Windows is still installing something
  ... usually a Found New Hardware window"), as designed. Read again on
  this tip (`dev\xp-tb-void1\`, the same shape: 11 to 47 points of the
  wizard's panel showing beside the console). Now `XHLEG.CMD` starts
  `XHFRONT.VBS` on 2000 and XP, which brings a window titled "Found New
  Hardware Wizard" to the front every 4 s (`WScript.Shell.AppActivate`),
  and sets `ForegroundLockTimeout` 0 for the next logons; the host also
  presses Alt+Tab when only a strip of the panel shows. On 2000, whose
  inner wizard pages the host cannot tell apart, `XHFRONT.VBS` itself
  presses Enter on the wizard (its default button, Next or Finish) and
  Yes on a "Digital Signature Not Found" question.
- **The wizards that open one after another (XP, the TB layout).** XP
  raises the next device's wizard only after the last one closed, and
  with the layout's four driverless functions one opened during the
  installer's minute of waiting (`dev\xp-tb-void2\`: exit 4 again). The
  host now escapes wizards until 190 s have passed and none has been seen
  for 45 s (at most 330 s), and `XHLEG.CMD` does what the installer's
  message says: on exit 4 with "still installing something" it says `XM
  RETRY install` on COM1, waits 120 s while the host escapes what is
  open, and runs the installer again (at most four more runs, each run's
  log kept).
- **8 and 8.1 with `/usb`** (`XMUSB.ON` on `xnt8\`): without it those
  systems get the connection manager alone, and the device leg would read
  Windows' own USB drivers (w46-mx86's 10 leg did that).
- A Thunderbolt leg waits for the tunnelled NVMe drive's driver to run
  (up to 420 s) before it reads it, since on 2000 and XP Stor98 arrives
  through Windows' wizard after the cold start.

### The device legs (mouse, BOT stick, UAS drive, composite audio, hub, the controller's sequence)

The device set and the sequence of the x86 section's device legs: plugged
at once on `qemu-xhci` (a `usb-hub` on port 1 with a `usb-mouse` behind
it, `usb-audio` on port 2, or behind the hub on 8 and 8.1 where port 2
holds the tablet, a `usb-storage` stick on port 3, a `usb-uas` with one
`scsi-hd` on port 4), each device's state, both drives read and written
(`TESTDATA.BIN` copied off and back, `fc /b`), the controller disabled,
enabled, removed and found again (NT: `devcon` or `DC2K.EXE`; ME: Device
Manager by keys, Refresh for the rescan), the devices and drives after
each step, the unplug, the uninstall, a cold start and a check that the
driver is gone.

| System | Result | Evidence |
|---|---|---|
| 2000 SP4 | **PASS.** Install exit 3; after the cold start `xhci98.sys` = kit, the controller "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)" running. Plugged: the stick (`USBSTOR`), "xHCI98 USB Attached SCSI Storage" (after Windows' wizard for it, walked), the audio interface, "xHCI98 USB Hub", the HID mouse, "xHCI98 USB Composite Device" all running; E: (stick) and F: (UAS) read and written. Disable (the controller "disabled"), enable: everything back, both drives read and written. Remove, rescan: Windows' wizard for the controller (found `oem0.inf`, walked), the controller running 10 s after the rescan and every device back under xHCI98, both drives read and written. Unplug: all gone in 30 s. Uninstall exit 3; after the cold start no `xhci98.sys` | `dev\w2k-dev\` |
| ME | **PASS.** Install exit 3 ("restart pending"), Shut Down, cold start. Plugged: Windows' wizard for "USB Audio Device" (Enter: automatic search), the rest installed with no question; Explorer opened E: and F:. `HKEY_DYN_DATA`: 17 devnodes, every one of xHCI98's at problem 0 (controller, root hub, hub, mouse, composite parent and audio interface, BOT `USBSTOR`, UAS `XHCIUAS`, their disks); one other at problem 10, the piix's `MF\CHILD0000` (an IDE channel of the board, not this project's). E: and F: read and written. Device Manager: Disable in this hardware profile (the children red X, no restart asked), Enable (Code 22 cleared, "working properly"), both drives read and written; Remove (the USB group gone), Refresh: every device found and installed again with no question, both drives read and written, the same 17 devnodes. Unplug one at a time, 60 s apart (stick, UAS, audio, mouse, hub): ME's own "Unsafe Removal of Device" notices, no fault, the desktop live. Uninstall exit 3; after the cold start Windows' wizard for "PCI Universal Serial Bus" (the controller with no driver, as before the install) and no `XHCI98.SYS` | `dev\me-dev\` |
| 8 | **Passed, except the UAS drive** (the note below: one drive read and written, not two). `/usb`: install exit 3; `xhci98.sys` = kit (`oem1.inf`, `xhci98.sys` and `xhcihlpr.sys`). Plugged: the stick, hub ("xHCI98 USB Hub"), composite device, audio, mice running under xHCI98; the UAS device under Windows' "USB Attached SCSI (UAS) Mass Storage Device" at problem 10. E: (stick) read and written at each step; disable, enable as on 2000. Remove, rescan: Windows gave the controller to its own driver ("Standard USB 3.0 eXtensible Host Controller", "USB Root Hub (xHCI)"), the stick read and written there. Uninstall exit 3 ("RESTART Windows to finish the removal"); no `xhci98.sys` after the cold start | `dev\w8-dev\` |
| 8.1 | The same shape as 8 in every step, the UAS drive at problem 10 included | `dev\w81-dev\` |

**The UAS drive on 8 and 8.1 (32-bit).** Under xHCI98 the UAS device
was given Windows' own UAS driver, which fails QEMU's device (problem 10,
as the release notes say of that driver), not `xhciuas`: the x86 package
is not signed, and Windows ranks its own signed driver above it. The
64-bit matrix (`46.final pre-cut matrix, x64`) signed its kit with a
throwaway certificate, and there `xhciuas` was chosen. Not a Phase 46
change, but `xhciuas` on 8 and 8.1 (32-bit) is not covered by these legs,
and after the rescan the controller ran under Windows' own driver, so the
stick's last copy there read Windows' stack, not xHCI98's. Also seen: with `/usb` the 8 and 8.1 installs placed no
`xhcicm.sys` (no USB4 host in these machines; the default install there,
w46-mx86's 8.1 leg, places it).

### Thunderbolt / USB4 (the asm4242 layout, the ORICO model with the NVMe disk at power-on)

`-Usb4 asm4242 -EncAtBoot`; `INSTALL /console /yes /nopause /thunderbolt
/stor98`; after the cold start the tunnelled NVMe drive's driver, its
volume read and written, then the uninstall and a cold start.

| System | Result | Evidence |
|---|---|---|
| XP SP3 | **PASS.** The first run exit 4 on a pending wizard (above), the harness's run again after 120 s exit 3. After the cold start `xhci98.sys`, `xhcicm.sys` and `stor98.sys` = kit; "xHCI98 USB4 Router (ASMedia ASM4242)", the four "xHCI98 USB4 PCI Bridge (ASMedia ASM4242)", "xHCI98 PCI Bridge" on the root ports; "Stor98 Storage Controller for NVMe SSDs" running on the tunnelled NVMe (`oem4.inf`, `stor98.sys`) with "QEMU NVMe Ctrl SCSI Disk Device"; D: read and written. Uninstall exit 3; no `xhci98.sys` after the cold start | `dev\xp-tb\` |
| 2000 SP4 | **PASS.** Install exit 3 (Stor98 left to the mark service: no connection manager answered yet); after the cold start the files = kit, the router running, Stor98 (service `stor98`) running on the NVMe, E: read and written. Uninstall exit 3; no `xhci98.sys` | `dev\w2k-tb\` |
| Vista SP2 | **PASS.** One start first for the `SuppressNewHWUI` policy; install exit 3; the files = kit, router, bridges and Stor98 (`oem7.inf`) running, D: read and written; uninstall exit 3; no `xhci98.sys` | `dev\vista-tb\` |
| 8 | **PASS.** Install exit 3 (the connection manager and Stor98; Windows' USB drivers kept, so no `xhci98.sys`); `xhcicm.sys` and `stor98.sys` = kit; router, bridges and Stor98 (`oem3.inf`) running, E: read and written; uninstall exit 3 | `dev\w8-tb\` |
| 8.1 | **PASS**, as 8 (Stor98 `oem3.inf`, E: read and written; uninstall exit 3, no `xhci98.sys`) | `dev\w81-tb\` |
| 10 22H2 | **PASS.** Install exit 3 (the connection manager and Stor98); `xhcicm.sys` and `stor98.sys` = kit; router, bridges and "Stor98 Storage Controller for NVMe SSDs" (`oem4.inf`) running on the tunnelled NVMe, E: read and written; uninstall exit 3; no `xhci98.sys`. At each of the three starts the harness's key sequence that opens the elevated prompt took its third try to start the leg (at the first start a consent prompt left open by the first try was answered by hand, Alt+Y) | `dev\w10-tb\` |

### The runs given up (harness)

- `dev\xp-tb-void1\`: the wizard behind the console (above), install exit
  4; stopped.
- `dev\xp-tb-void2\`: the poller's first call of `XHLEG.CMD` ended at
  once ("The system cannot find the path specified", the transfer disk's
  `XHLEG.CMD` there and readable a minute later); phase 1 started by
  hand from the same console; install exit 4 on a wizard raised during
  the installer's minute; stopped. Not seen in the other runs.
- `dev\w2k-dev-x1\`: before `XHFRONT.VBS` walked the wizards; the UAS
  device's wizard and, after the rescan, the controller's were walked by
  hand, and the leg ran to its end with the UAS device at problem 1 until
  then. Run again as `dev\w2k-dev\`.
- `dev\me-dev-void1\`: stopped at the desktop to stay within the vCPU
  allotment; run again from a fresh overlay.

**Harness limits seen in the readings.** The device readings' and the
Thunderbolt step's `for %%p in (...)` loops (from w46-mx86's
`XHLEG.CMD`) skip every pattern with a `*` (`for` takes them as file
names): the per-device queries (stick, UAS, audio, hub, mouse; the
`1B21` functions and the NVMe), so those readings list the controller's
query and the class queries alone. Each device's state above therefore
rests on the class queries and on the leg's wait lines (`XM RUNNING
<device>`, a `devcon status` of that pattern outside any loop), which are
not affected. `DC2K.EXE`'s class query on 2000 matched fewer devices than
the class holds (the disk class none), so the 2000 rows rest on the wait
lines, the Stor98 and router lines, the file checks and the drives'
copies.

**Revised after the legs** (reviewed; not exercised by these legs, whose
transfer disks keep the scripts as run): the loops above query the
patterns with a `*` one by one; `DC2K.EXE` (`5deb2b0d...`) matches a
class by its GUIDs; the host's escaping after `XM RETRY install <n>` now
stops at the guest's `XM RERUN install <n>` (said just before the
installer runs again; the host looks for it before each key), so a
late-read retry does not keep escaping into the next run, and the
escaping before the first run ends at that run's exit at the latest (it
overlaps the installer's minute of waiting, by design); the host presses Alt+Tab once more when its first did not bring
the wizard up (`XHFRONT.VBS` may have raised it just before); and `XHFRONT.VBS` presses its key on 2000
only when the window activates again right before it.

**Result.** Every leg owed ran to its end on `a22cdc71` with no fault: 2000
and ME read every device under xHCI98 through the whole sequence, and the
Thunderbolt legs read Stor98 on the tunnelled drive on every system. On 8
and 8.1 (32-bit) the UAS drive was not covered (Windows' own UAS driver
took it, above). No Phase 46 regression seen. The update legs over
`3.4.0.0` stay owed to the cut.

## 46.23 AMD's integrated USB4 hosts named by id (2026-11-03, w46-amdids)

The owner's task of 2026-11-03: `xhcicm.sys` names AMD's integrated USB4
hosts by id, where only Pink Sardine was named and the others took the
class line's plain `xHCI98 USB4 Router`. Commits: `f078720b` (the code, the INFs, the gate, the tests) and
this record's, reviewed and revised.

### The sources

`pci.ids` version `2026.10.31` (SHA-256
`3af2de60a0cb598912d4ede5314fcb36a59593d08fba4f7f26ca4d4e53a7b9ea`),
vendor `1022`, cross-checked against the pinned Linux `drivers/thunderbolt`
and FreeBSD `sys/dev/thunderbolt` mirrors (facts only; record 21 appendix
A, A.21; `legal-provenance.md` section 4, the fourth reading).

| Processor | NHIs (`pci.ids` name) | Its PCIe USB4 bridge |
|---|---|---|
| Rembrandt | `162E`, `162F` (USB4/Thunderbolt NHI controller #1, #2) | `14CD` Rembrandt PCIe USB4 Bridge |
| Phoenix | `15C4`, `15C5` (the same names) | `14EF` Phoenix/Phoenix2 PCIe USB4 Bridge |
| Pink Sardine | `1668`, `1669` (the same names; FreeBSD `nhi_reg.h` `DEVICE_PINK_SARDINE_0`, `_1`) | none named for it |
| Strix | `151C`, `151D` (the same names) | `150A` Strix/Strix Halo PCIe USB4 Bridge |
| Strix Halo | `158D`, `158E` (USB4 Host Router) | `150A` |
| Krackan | `1120`, `1121` (USB4/Thunderbolt NHI controller #1, #2) | `1125` Krackan/Krackan2 PCIe USB4 Bridge |
| Krackan2 | `113B`, `113C` (the same names) | `1125` |

Linux names no AMD NHI id (`pci.c` matches class `0C0340`), only the
router ids `0438:0208` to `020B` of "Yellow Carp and Pink Sardine
platforms" (`quirks.c`), and pairs a tunnel port with its NHI by the
port's ACPI `usb4-host-interface` reference (`acpi.c`
`tb_acpi_add_link`). FreeBSD names Pink Sardine alone.

Pink Sardine and Phoenix: no source at hand ties Pink Sardine to one
processor. `pci.ids` lists Phoenix's NHIs (`15C4`, `15C5`) apart from
Pink Sardine's, and Hawk Point has no USB4 id of its own there (it shares
Phoenix's NPU id `1502`), so a Hawk Point machine is expected to show
Phoenix's NHI ids; nothing here confirms it. Pink Sardine keeps its name
and the three bridge ids it had since task 40-0.2.

`151C` and `151D` were "USB4 Router" in `2026.10.14` and so on the CM
gate's `CM-NOROUTER` list (task 40-0.2); `2026.10.31` names them Strix's
NHIs, and the list drops them.

### The Strix Halo reading

A Strix Halo machine, Windows 11, read only (`Get-PnpDevice`,
`Get-PnpDeviceProperty`; no driver of this project installed, nothing
changed):

| Device | Read |
|---|---|
| `1022:158D`, `1022:158E` | class `0C0340`, Windows' own USB4 host router driver, each with a `USB4\ROOT_DEVICE_ROUTER&VID_1022&PID_158D` / `158E` child: host interfaces. Both behind an internal GPP bridge (`1022:150C`), not on the root bus |
| `1022:150A`, two | class `060400`, "PCI Express Root Port" (`pci.sys`), on the root bus, nothing behind either; each lists one NHI's `USB4\VIRTUAL_POWER_PDO` child as a dependency provider, the first port `158D`'s, the second `158E`'s |

The owner's Linux capture of the same machine (2026-11-03) agrees:
Linux's `thunderbolt` driver bound `158D` and `158E` as two domains and
found both host routers (generation 4); its "created link" lines pair
the first `150A` port with `158D` and the second with `158E`.

So on this processor `150A` is the tunnel root port, one per NHI, and
the pairing is the platform's (ACPI), as Linux's `acpi.c` reads it.
`tbt_topo.c`'s root-port source takes a listed root port only beside an
NHI on the root bus; these NHIs are behind a bridge, so it takes
neither, and the set comes from the host router's PCIe mapping (record 22
section 7.6) or `XhciCmTunnelBridge`. The filter has matched `150A` since
task 40-0.2 and acts on no port the NHI does not assign.

### What changed

- `xhcicm.inf`, `xhcicm-amd64.inf`: twelve id lines in every models
  section (98 SE and ME, 2000 and XP, Vista to 10; amd64 both), one string
  per processor, after Pink Sardine's: `U4RbDesc` "xHCI98 USB4 Router
  (AMD Rembrandt)", `U4PhDesc` "(AMD Phoenix)", `U4SxDesc` "(AMD Strix)",
  `U4ShDesc` "(AMD Strix Halo)", `U4KrDesc` "(AMD Krackan)", `U4K2Desc`
  "(AMD Krackan2)" (each within 98 SE's 46 characters).
- `cm_vals.c` `cmAmdNhi`: a `CM_PROFILE_USB4` row per id; `tbt_hw.c`:
  a row per id with its listed name (`test_tbt_hw` holds the two tables
  equal); `xhcitr\tr_core.c` `tr_profile_of_nhi`: the same ids.
- `cm_ports.c`: four families, `amd-rembrandt` (`14CD`), `amd-phoenix`
  (`14EF`), `amd-strix` (Strix and Strix Halo, `150A`), `amd-krackan`
  (Krackan and Krackan2, `1125`); `amd-usb4` (Pink Sardine) unchanged.
- `xhcicmf.inf`, `xhcicmf-amd64.inf`: `1022:1125&CC_0604` in both
  sections; `xhciinst\inst_core.c` `instTbPortIds` the same, in the
  INF's order.
- `xhciinst\inst_core.c` `instNhiNames`: the twelve names (the
  installer's and XHCITR's, which `test_inst` holds to both NHI INFs).
- `src\pfw\pfw_ids.c`: the twelve ids (`test_pfw` holds it to the 98 SE
  models section).
- `check-cm-inf.ps1`: `CM-DESC`'s six strings; `151C`, `151D` off
  `CM-NOROUTER`; two self-test mutations (a Krackan line dropped, a Strix
  Halo line with Strix's string).

Not added: no port id beyond `1125`, and no Phoenix2 NHI (the listing
names none). The emulator leg was not run: `usb4-emu`'s `usb4-host`
gives its generic host's `1B36:00F4` on every `host-model`, `amd`
included, with no property for another id, so showing an AMD id would
need a change to the emulator.

### Builds and tests

Gated builds of `e92a248d` (this task's ids, the ACPI-named source below
on top): x86 `all` (release and qemu) PASSED, amd64 release PASSED
(`E:\xhci98-scratch\p46-w46-amdids\gated-x86-all2.log`,
`gated-amd64-release.log`); an earlier x86 `all` of `0c840ca3`, the ids
alone, PASSED too (`gated-x86-all.log`). `xhcicm.sys`: x86 release
`f73be8c9...`, x86 qemu `1a7a3414...`, amd64 release `bf199416...`. Host
suite on the same tip: all suites passed (`test_cm_dsd` 44 checks,
`test_tbt_topo` 745, `test_cm_ports`, `test_cm`, `test_tbt_hw` 178,
`test_inst` 8390, `test_tr` 1602, `test_pfw` 445); the CM INF gate and its
self-tests passed inside the builds.

### AMD's tunnel root ports: the ACPI-named source (2026-11-04)

The reading above showed the gap: `tbt_topo.c` takes a listed root port
only beside an NHI on the root bus, and AMD's NHIs sit behind an internal
bridge, so on Strix Halo neither `150A` port is taken. Design record 22
section 7.6.1 (`ee4bfd0b`; the direction the coordinator's, on the
owner's delegation; six rounds, reviewed and revised): after the override
and the router-named source, an ACPI-named source on NT amd64 - the bridge
filter on an AMD family's root port reads its `_DSD` and the NHI matches
the `usb4-host-interface` reference to its own ACPI name; the
`usb4-port-number` is logged only; position rejected; learning by
elimination later; the 9x route deferred (with the owner's no-ACPI start
on the Strix Halo machine: the host routers answered, a device attached at
power-on came up as router `1-2` through the firmware's tunnel on the first
`150A` port, no hot-plug event after the start, no INTx route). Code
`e92a248d`, reviewed and revised (three rounds: an overfull claim set voided
as `MANY`, never an override; a late claim's retries replenished):
`cm_dsd.c` (the reader, host suite `test_cm_dsd`), `cm_dsdos.c` (amd64:
`IOCTL_ACPI_EVAL_METHOD` for `_DSD`, `IoGetDevicePropertyData` through
`MmGetSystemRoutineAddress`, allowlisted for `cm_dsdos.obj` alone),
`XhciTbtTopoAddAcpi` (`test_tbt_topo`), the claim, activity, late-claim and
recovery rules in `cm_reenum.c`. On the same machine,
`DEVPKEY_Device_BiosDeviceName` (`{540b947e-8b40-45bc-a8a2-6a0b894cbda2}
10`) reads `\_SB.PCI0.GPPC.NHI0` for `158D`, the name the first port's
`_DSD` references.

### The leg: Windows 10 x64 on the emulator's AMD built-in layout (2026-11-04)

Development host A, TCG, `-smp 2`, 4 GB. `usb4-emu` `main` `fafdef8`'s
AMD built-in layout (`docs/USB4-HOSTS.md` section 9; the device lines of
`scripts/run-linux.sh` `TOPOLOGY=amd-builtin`) on the emulator build
`c918493`: two `usb4-host` `host-model=amd` (`1022:158D` and `158E`, router
`0438:020D`, one USB4 port each) behind the internal bridge `00:08.3`
(`1022:150C`), their tunnel root ports `00:01.1` and `00:01.2`
(`1022:150A`, `usb4-port` 1 and 2), `acpi-sb-osc=off` with the two tables of
`scripts/asm-fw-ssdt.py --amd-builtin`, no device router; standard VGA moved
to `00:10.0`. A fresh overlay of the `w10x64` base; the `3.4.0.0` x64
package installed with `/thunderbolt` (exit 3, restart: `xhcicm.inf` on both
NHIs by the class line, the filter on both `150A` ports and on the q35
xHCI's root port), then this branch's amd64 `xhcicm.sys` (`bf199416...`)
signed in the guest with a throwaway certificate and put in place
(`3abc4637...` signed). Each case a cold start; the members read from
`XHCITR64 /report` (the NHI's log). Evidence
`E:\xhci98-vm\p46\w46-amdids\w10\` (`serial-*.log`, screens, `pids.txt`),
the kit in `kit\`.

| Case | `pcie-ds-mapping` | `_DSD` (`tp0` / `tp1`) | `158D` | `158E` |
|---|---|---|---|---|
| a: `3.4.0.0`'s `xhcicm.sys` (the baseline) | off | own / own | `u4.map.none=FFFFFFF5`; outcome `UNLISTED` (8), candidates `00:01.1`, `00:01.2`, `00:1C.0`; no member, `cm.reenum.no-route=1` | the same (not quoted) |
| R1 | on | own / own | `u4.named=01000901` (port 1, `00:01.1`, added); `cm.acpi.offer=00000902` (the same bridge, known); member `00:01.1` source router (4), flags exclusive and active | `u4.named=01000A01`, offer `00000A02`, member `00:01.2` router, active |
| R2 | off | own / own | `u4.map.none`; `cm.acpi.name=00000014` (20 characters); `cm.acpi.offer=00000901` (added); member `00:01.1` source ACPI (6), exclusive and active | offer `00000A01`, member `00:01.2` ACPI, active |
| other | off | own / other | offers `00000901` and `00000A01`: members `00:01.1` and `00:01.2`, both ACPI, both active | no claim names it: `UNLISTED`, no member |
| dangling | off | dangling / own | no claim names it: `UNLISTED`, no member | offer `00000A01`, member `00:01.2` ACPI, active |

R1 and R2 take the right root port for each NHI; the baseline takes neither,
as the gap predicted. With `acpi-host-ref=other` on `tp1` the firmware names
the first host for both ports, and `xhcicm.sys` follows it as Linux does
(`USB4-HOSTS.md` 9.3: both links to the first host): `158D` holds both ports
and `158E` none. Nothing in the source checks a firmware that names one NHI
for two ports; with operation `30h` answered as well, the router's naming
of `00:01.2` for `158E` would make that port claimed by both domains and
inactive for both (record 21's exclusive ownership; not run). With a
dangling reference the port is nobody's. The `Method` form of the `_DSD`
was not run. `XHCITR`'s report gave the package's version, `3.4.0.0`, for
the replaced image too (this branch still stamps `3.4.0.0`); the hash read
in the guest is the record of which image ran.

### One port per NHI, and a renewed claim (2026-11-04)

The coordinator's decision, on the owner's delegation (record 22 section
7.6.1): when two or more current claims name one NHI, none is taken for it,
not one picked (each AMD NHI has one tunnel root port); the router-named
source binds as before. `b69044b2`, reviewed and revised. Then a finding of
the merge's review: with operation `30h` unavailable, a root port whose
filter was restarted alone left its ACPI member inactive, the member's
claim serial being set only at discovery; a listed member now adopts the
new serial when its new claim names the same NHI and is the only one, not
while the host interface is lost or contained (`ccc6b5cd`, reviewed and
revised). Host suite: `test_cm_dsd` 52 checks (the one-port rule, the
renewal). XHCITR names the new source "named by the firmware (ACPI)"
(`tr_pci.c`; it printed "unknown source" in the cases above). Gated amd64
release builds of `b69044b2` (`xhcicm.sys` `904d33d2...`) and `ccc6b5cd`
(`6bdc8542...`) PASSED (`gated-amd64-r2.log`, `gated-amd64-r3.log`).

Further cases on the same overlay, each image signed in the guest as
before (`b69044b2`'s `1b79389b...`, `ccc6b5cd`'s `ad34b759...` signed):

| Case | Image | `pcie-ds-mapping` | `_DSD` | `158D` | `158E` |
|---|---|---|---|---|---|
| other, again | `b69044b2` | off | own / other | `cm.acpi.several=00000002`: no claim taken; `UNLISTED`, no member | no claim names it; no member |
| other, router on | `b69044b2` | on | own / other | `cm.acpi.several=00000002`; `u4.named=01000901`: member `00:01.1`, router, active | `u4.named=01000A01`: member `00:01.2`, router, active |
| restart | `ccc6b5cd` | off | own / own | member `00:01.1`, ACPI, active; `00:01.1` disabled (Status Error) and enabled (OK) with `Disable-PnpDevice` / `Enable-PnpDevice`: `cm.acpi.renewed=00000009`, XHCITR "Tunnel port 1: PCI 00:01.1, active" | member `00:01.2`, active, untouched |
| hot plug after the restart | the same | off | own / own | a device router with the NVMe disk added on `u4`'s port 1 over QMP: `u4.pcie.built=1`, the NVMe controller enumerated behind `00:01.1` as E: (`stornvme`), the enclosure approved | - |

### The ACPI-named source on x86 (2026-11-04, w46-amdx86)

The owner's decision (2026-11-04): the ACPI-named source goes into the x86
build too, through the routine route of record 32 section 12.8.15.15
(record 22 section 7.6.1, "The x86 build"; reviewed and revised). Code
`6110f51c`, reviewed and revised: `cm_dsdos.c` has one path for both
builds, the NHI finding `IoGetDevicePropertyData` with `CmRoutine` (on
x86 the helper's channel, minor 1; no x86 import row added), the bridge
filter reading `_DSD` on Vista and later (WDM 6.0); `cm_oscnt.c` no longer
keeps a helper lookup that failed for want of memory, for any caller, the
handover included; the amd64 allowlist's `MmGetSystemRoutineAddress` row
held to `cm_oscnt.obj`. The pure reader is unchanged, so no host vector was
added. Merged with 3.4.1.0 at `e1600232` (`99483e1d`; no overlap in
`cm_oscnt.c`).

Builds (development host A): gated x86 `all` of `6110f51c` PASSED
(`xhcicm.sys` release `075aea22...`) and of `99483e1d` PASSED (`4187f25e...`);
gated amd64 release of `6110f51c` PASSED (`ca50487b...`). Host suite PASSED
on `99483e1d` (`test_cm_dsd` 52 checks, `test_tbt_topo` 739).

The leg: Windows 10 x86 (19045) on the emulator's AMD built-in layout, as
the x64 leg above (the same device lines, emulator build `c918493`, whose
devices `usb4-emu` `main` `189d459` keeps; the two `--amd-builtin` tables,
the same bytes; `acpi-sb-osc=off`; standard VGA at `00:10.0`, no NIC),
TCG, `-smp 2`, 2 GB, a fresh overlay of the `w10x86` base. The kit is this
tree's x86 package. Each case is a cold start, its members read from
`XHCITR /report` (the NHI's log), the service states with `sc query`, the
loaded images hashed in the guest. Evidence
`E:\xhci98-vm\p46\w46-amdx86\w10\` (`serial-*.log`, screens, `pids.txt`).

| Case | Package | Helper | `pcie-ds-mapping` | `_DSD` (`tp0` / `tp1`) | `158D` | `158E` |
|---|---|---|---|---|---|---|
| helper absent | `6110f51c`, `/thunderbolt` (on 10 the connection manager alone: no `xhci98.sys`, so no helper) | `xhcihlpr` no service (1060) | off | own / own | `cm.acpi.name=00000000`; `u4.map.none=FFFFFFF5`; outcome `UNLISTED` (8), `cm.reenum.no-route=1`, no member | the same |
| R2 | `99483e1d`, then `/thunderbolt /usb /restage` (`xhci98.sys` on the q35 xHCI) | `xhcihlpr` running (`1e164322...`) | off | own / own | `cm.acpi.name=00000014`; `cm.acpi.offer=00000901`; member `00:01.1`, source ACPI (6), flags exclusive and active; XHCITR "Tunnel port, PCI 00:01.1 (named by the firmware (ACPI), active)" | offer `00000A01`; member `00:01.2`, ACPI, active |
| dangling | the same | running | off | dangling / own | `cm.acpi.name=00000014`, no claim names it: `UNLISTED`, no member | offer `00000A01`; member `00:01.2`, ACPI, active |

The x86 image reads its NHIs' ACPI names through the helper and takes each
NHI's root port as the amd64 image does; with no helper the source is
absent and nothing is bound, as designed (the router-named source was off
in every case). The helper-absent case ran `6110f51c`'s image; the merge
changed no line of `cm_dsdos.c` or of the lookup. Installing over the
first package needed `/restage` (the driver store held the earlier build
of `xhcicm.inf` at the same version). Not run on x86: operation `30h` on,
the `Method` form, `other`, 8 and 8.1.

### Left

The six descriptions' wording, read by the owner; the README's and the
release notes' rows (46.final; the text is in this branch's report); the
`_DSD` method form on the emulator (not run). No AMD host has run
`xhcicm.sys`.
## 46.12 on NT, E: the known-board route on the emulator (2026-11-04, w46-nthand2)

Route E of record 32 section 12.8.15.11 (the driver half `8221ab22`, the
deferred activation `0f47583e`, sections 12.8.15.14 and 12.8.15.16), on
the 10 x64 q35 base (build 19045), `usb4-emu`'s ASM4242 model (its
mailbox model `fw-handover=on`, `pm-cap=on`, the router silent until the
handover, `usb4-host.fw-handover-gate=on`), `usb4-host.acpi-sb-osc=off`,
no SSDT loaded, the ORICO enclosure at power-on, `-smbios
type=2,manufacturer=usb4-emu,product=asm4242-model` for the qemu flavour's
emulator profile. The emulator: the `emu-hw` build of 2026-11-04
(`qemu-system-x86_64.exe` SHA-256 `53a3811a...07814`). The guest: the
`3.4.0.0` package installed (exit 3, the restart), then the qemu flavour's
amd64 `xhcicm.sys` of the commit under test signed inside the guest with a
throwaway certificate and put in place, and `xhcicm` added to the
`UpperFilters` of the switch's upstream port (`1B21:2421`) and xHCI
(`1B21:2426`) by `reg add` as the administrator (the installer's entry of
section 12.8.15.12 is another task's). Evidence:
`E:\xhci98-vm\p46\w46-nthand\e1\` (`debugcon-*.log`, `serial-*.log`,
screenshots, `pids.txt`).

### The image of `8221ab22`

| Start | What the log says |
|---|---|
| tag c, no filter on the upstream port or the xHCI | `cm.osc.refused=102h`: no upstream port member; the router silent (`u4.inert-after-activation=0Ch`) |
| tag d, the filters on, the boot's start | `cm.osc.profile=2` (the emulator's), every identity field read (WMI's SMBIOS answer decoded), then `cm.osc.guard.write=1`: the marker set, its key's flush failed: `10Fh` |
| tag d, `pnputil /restart-device` of the NHI later in the boot | the flush succeeded, `cm.osc.members=6` (coverage whole), then `10Ah`: the upstream port's configuration interface not held |

So at the boot's start the hardware key cannot be flushed (record 32
section 12.8.15.16's reading), and the upstream port member's
`BUS_INTERFACE_STANDARD` query at its start had failed.

### The tables and A5

With `acpi-sb-osc=off` and build 1 `--osi`'s three tables (no driver of
this change involved): 7 x86 stopped with bug check `A5` (`11h`, `8`,
`8124F758h`, `1`) at both cold boots (`a57\a2.png`, `b1.png`); 10 x64
reached the desktop at both (`a5\a2.png`, `b3.png`). The cause is the
tables' top-level `External`s, not a driver: `usb4-emu`
`docs/USB4-HOSTS.md` section 9.6 (`--externals-ifzero`, `189d459`).

### The images of `0f47583e` and `fd35546a` (the start deferred)

The qemu flavour's amd64 `xhcicm.sys` (`0f47583e` SHA-256
`ea896930...b963b` unsigned; `fd35546a` `53857d18...f1a4e` unsigned,
`d6259163...6e802` as signed in the guest).

| Boot | Image | What the log says | Reading |
|---|---|---|---|
| g | `0f47583e` | `cm.osc.profile=2`; `cm.osc.guard.preflush=C000014Dh` (STATUS_REGISTRY_IO_FAILED), `cm.osc.defer`, `cm.inert=0Eh`; 15 tries one second apart, the flush then succeeding; `cm.osc.members=6`; `cm.osc.bus.start=00BB00BB`, `bus.again=00BB00BB`; `10Ah`; `cm.osc.defer.done=0`, the activation run, the router silent | the deferral works; the bus interface was refused by the FDO and the PDO: the GUID was the PCI bus interface's (`...8281`), fixed in `fd35546a` (`...8280`, `wdmguid.h`) |
| h | `0f47583e`, no `-smbios` | `cm.osc.guard=101h` (marker found, kind 1), `cm.osc.guard.deleted=1` | boot g armed the marker and refused under the hold: the next boot skips once, as 12.8.15.13 says |
| i | `0f47583e`, no `-smbios` | `cm.osc.smbios.refused=0Ch` (no type 2), `cm.osc.unknown=0`; XHCITR's report: "Handover not sent: this board is not recognised. To try it, set the Thunderbolt setting for unknown ASM4242 boards to yes." | an unknown board at `XhciCmAsm4242Reads` 0: PASS |
| k | `fd35546a` | `cm.osc.guard=101h`, the marker deleted | boot j (the signing boot, still `0f47583e`) armed and refused: one skip |
| l | `fd35546a` | 15 tries, `cm.osc.members=6`, `cm.osc.route=100h`, `cm.osc.mb.before=0`, `cm.osc.handover=100h`, `cm.osc.query=000F0100h`, `u4.host.id=174C2428`, `u4.start=0`, `cm.osc.guard.clear=100h`, both ports started; XHCITR: the host active, tunnel port 1 active, the enclosure's switch `1B21:2463` behind it | the known board's profile sent at the deferred activation, the router answering, the marker cleared: PASS |
| m | `fd35546a`, `XhciCmQemuOscNoClear` 1 | sent (`100h`, `000F0100h`, router `174C2428`), `cm.osc.qemu.noclear=1`; then QEMU quit | an unfinished boot after the send |
| n | the same | `cm.osc.guard=101h`, the marker deleted, nothing sent | the boot guard's skip: PASS |
| o | the same | Windows' recovery screen after two unclean ends, turned off from it | not the driver's |
| p | the same | sent again (`100h`, `000F0100h`, router `174C2428`) | the boot after the skip sends: PASS |

### Not run

Reads 1 on B and C and D on 2000 or 32-bit XP: the read path is not
built (`110h`); the x86 image refuses (`108h`, no routine route,
record 32 section 12.8.15.15); Windows 11: no q35 base; the S4 resend,
fast startup and an NHI disabled across S4; a member's power request
held past 1000 ms and a surprise removal during the exchange (no fault of
the mailbox model for them); the bench.
## 46.12 on NT, the installer's part (2026-11-04, w46-inst3)

From design record 32 section 12.8.15.12 (`fac635d8`, the w46-nthand
branch); record 26 draft 72, section 5.5. Code `439ac42f`, reviewed and
revised (the xHCI's devnodes kept out of the tunnel-port passes and the
connection manager's version group; the uninstall's early removal not
hiding the tunnel-port pass's restart; additions only over an image the
run admitted, put in place at this package's version, its service not
disabled).

### What changed

- `XhciCmAsm4242Reads`: an Advanced row of the Thunderbolt settings on
  2000 and later (greyed on 98 SE and ME), never asked, `/set` 0 or 1,
  default 0; the uninstall's value table deletes its machine-wide copy.
  It takes the driver's default (`NoInf`) until both `xhcicm` INFs' NT
  installs write it (the driver's change).
- The ASM4242 xHCI's filter entry: `xhcicm` appended to the
  `UpperFilters` of every present `PCI\VEN_1B21&DEV_2426` devnode
  (`inst_cm_xhci_target`), the order kept, at an NT install's end with
  the Thunderbolt support installed by the run; taken out of every
  devnode, present or not, when the support is gone, and by the
  uninstall as its first NT step; never on Windows 11; a change asks for
  the restart and is said and logged. These devnodes are no tunnel port
  and no member of the connection manager's group (`tunnel_filter`).

### Checks

- Host: `test_inst` 8237 checks, 0 failures (`test_cm_xhci_4612`).
- Gated x86 release build at `439ac42f`, every step passed (development
  host A).

### Leg (8.1 x86, q35, the emulated ASM4242 USB4 host, fresh overlay)

| Step | Result |
|---|---|
| Setup, the Thunderbolt box ticked alone, Install | the CM, its filter and Stor98 staged; "xhcicm: added to the UpperFilters of PCI\VEN_1B21&DEV_2426&SUBSYS_24211B21&REV_01\..." in the run's text and the log; the devnode's `UpperFilters` `xhcicm` (`reg query`) |
| `XHCIWIZ.EXE /uninstall` | the first line of the removal "xhcicm: taken out of the UpperFilters of PCI\VEN_1B21&DEV_2426&...", then the mark service, Stor98 and the tunnel ports as before; "Done. Please restart Windows now."; no `UpperFilters` left under `Enum\PCI` |

Evidence: `E:\xhci98-vm\p46\w46-inst3\w81f\` (`k01` to `k08`).

### Left

The `NoInf` flip once the INFs write the value; the identity
classification and the report's line (with the driver's
`CmOscProfileMatch`); the Summary's list of the devnodes a run will
change; legs on 2000, XP and 10 and an update over an older release; the
texts, read by the owner.

### The board's identity and the filter devnodes before the go-ahead (2026-11-04, w46-inst4)

The two items the section above left: design record 32 section
12.8.15.12, "As built in the installer"; record 26 draft 73, sections 5.5
and 5.7; the owner's decisions of 2026-11-04 (through the coordinator):
the class said in the report, the log, the reads row's note and the
Summary; on 2000 and 32-bit XP the SMBIOS tables by WMI, read at the
Thunderbolt and USB4 page's Next with a 10 s countdown and Skip, the
console waiting as long; nothing read with the box unticked or no ASM4242
host. Code `700ba334`, reviewed and revised (the read never by a plan,
whose Thunderbolt choice may be a default; the filter list's admission
the run's: the CM group verdict, the stored-package traps known before
staging, a staging-only run, an installed image of another version or a
disabled service); `433acb00` logs each NHI's class once.

#### What changed

- `xhciinst\inst_board.c` (new, pure): the class of the board under an
  ASM4242 NHI by the driver's own `cm_oscp.c` (`CmOscpSmbios`,
  `CmOscpMatch`): known, not recognised, not checked (a parent's id or the
  tables not read), and before the read "to be checked at Next". The
  installer compiles `cm_oscp.c` with `CM_OSCP_RELEASE`: the release
  driver's profile alone (the host suite `test_cm_oscp_rel` and the
  tool's `/selftest` hold it).
- The engine: the NHI's parents by `CM_Get_Parent`; the tables by
  `GetSystemFirmwareTable` `'RSMB'`, else WMI (`root\WMI`,
  `MSSmBios_RawSMBiosTables`) through COM loaded at run time on a thread
  of its own, its waiter capping it at 10 s, a late answer dropped; the
  class in the plan's report under the NHI and again at the run's start
  when it changed; the reasons in the log.
- The window: the reads row's note ends with the class; the Summary
  lists it; at the Thunderbolt and USB4 page's Next with WMI a modal box
  "Reading this board's identity... N" with Skip; Back and Next reuse the
  answer.
- The 1B21:2426 filter devnodes the run will change, listed before the
  go-ahead: the Summary (install and uninstall), the console's plan and
  its uninstall warning, by a pass of the same enumeration that writes
  nothing and the run's own decision (`inst_cm_xhci_mode`).
- `XhciCmAsm4242Structural`'s caution gains its NT sentence; its row
  keeps `INST_APPLY_9X_CM` here until both INFs write it on NT (the read
  path's change).

#### Checks

- Host: `test_inst` 8529 checks, 0 failures (`test_board_4612`);
  `test_cm_oscp` and `test_cm_oscp_rel` 91 each, 0 failures.
- Gated x86 release build at `700ba334` (after `src\pfw`,
  `xhciinst\di` and `xhciinst` built): BUILD + GATES PASSED, the
  installer's self-tests passed (development host A). Log
  `E:\xhci98-scratch\p46-w46-inst4\gated-x86.log`.
- `433acb00`: `xhciinst\build.cmd` built all five executables; host
  suites as above.

#### Legs (q35 bases, fresh overlays, the emulated ASM4242 USB4 host, the kit of `700ba334`)

The installer only, up to the Summary (nothing installed). Kit
`E:\xhci98-vm\p46\w46-inst4\kit86` (`XHCIWIZ.EXE` SHA-256 `01862cf6...`,
`XHCIINST.EXE` `a8bcf219...`); the known-board leg with a TEST-ONLY
installer built without `CM_OSCP_RELEASE` (`kit86t`, `XHCIWIZ.EXE`
`22f82a13...`, `XHCIINST.EXE` `2b442d07...`), never packaged.

| Leg | System, configuration | Result |
|---|---|---|
| L1 | 10 x86, no `-smbios` (no type 2 structure), the release kit | console `/list /thunderbolt`: "ASM4242 board: not recognised (see XhciCmAsm4242Reads)." under the NHI; "Setup will add the xhcicm filter to these USB controllers (restart):" and `PCI\VEN_1B21&DEV_2426&SUBSYS_24211B21&REV_01\6&...` before the end; log "SMBIOS tables read (GetSystemFirmwareTable, 409 bytes)", class reason 12 (no type 2, the driver's own refusal). Window: the reads row before Next "This board: Setup checks it when you press Next."; the Summary "- ASM4242 board: not recognised (see XhciCmAsm4242Reads)." and the filter list; after Next the note "This board: not recognised." PASS |
| L2 | 10 x86, `-smbios type=2,manufacturer=usb4-emu,product=asm4242-model`, the test kit | Summary "- ASM4242 board: known; the USB4 handover is sent." and the filter list; the note "This board: known; the handover is sent without this setting." PASS |
| L3 | XP SP3 (32-bit), the release kit | console: "Reading this board's identity...", log "SMBIOS tables asked of WMI", "SMBIOS tables by WMI: read (reason 0, 0x00000000, 409 bytes)", "not recognised" (reason 12); the filter list. PASS |
| L4 | XP, the window, the Thunderbolt box unticked (its default there) | Summary with no board line and no filter list; nothing asked of WMI until the box was ticked. PASS |
| L5 | XP, the window, the box ticked, Next on the Thunderbolt and USB4 page | the box "Reading this board's identity... 10" with Skip, closed at the answer; Summary "- ASM4242 board: not recognised ..."; Back and Next again: no box. PASS |
| L6 | XP, `winmgmt` disabled and stopped | the box, then Summary "- ASM4242 board: Setup could not check this board."; log "root\WMI not opened (reason 4, 0x80070422)". PASS (a failure at once; a timeout was not made: the window self-test covers the countdown's end and Skip) |
| L7 | 2000 SP4, the release kit | console: WMI answered (409 bytes), "not recognised" (reason 12); the filter list. PASS |

Evidence: `E:\xhci98-vm\p46\w46-inst4\u10\` (L1), `k10\` (L2), `xp\`
(L3 to L6), `w2k\` (L7): `L*.png`, `cmdline-*.txt`, `pids.txt`.

#### Found

- On 2000 WMI's `MSSmBios_RawSMBiosTables` answers (L7), while the
  driver's route E has no WMI there (12.8.15.17: the routines absent, an
  unknown board): on 2000 Setup can say "known" where the driver will
  not send by E. Decided (the coordinator, on the owner's delegation):
  on 2000 Setup reads nothing and says it could not check; 32-bit XP
  keeps its read, the x86 image of `45cf9e30` reading WMI there
  (record 32 section 12.8.15.12; the follow-up commit after this one).
- The x86 `xhcicm.sys` of this tree refuses on NT (`108h`) until the
  helper's channel (12.8.15.15) is merged: on 32-bit NT "known; the USB4
  handover is sent" holds only from then.
- The Summary's edit control breaks the long instance id at its first
  `\` (L1, L2): not changed.

#### Left

The texts, read by the owner; an install over a known board's devnode and
the uninstall's list on a guest (the run's step itself was read by the
section above); the structural row's `INST_APPLY_CM` with the read path's
INF change.

### The helper from xhcicm.inf: the handover on a Thunderbolt-only 10 x86 (2026-11-04, w46-inst5)

The owner's decision of 2026-11-04: the x86 NHI INF installs the helper
`xhcihlpr.sys` too, so a Thunderbolt-only install on 32-bit NT sends the
ASM4242 handover. Design record 18 section 15.15 (pointer in record 32
section 12.8.15.15); code `198ee639`, `87ade048`, reviewed and revised.

#### What changed

- `src\cm\xhcicm.inf`: `[Cm.Dev.NTx86]` and `[Cm.Dev6.NTx86]` copy
  `xhcihlpr.sys` and add the `xhcihlpr` service, the same in every
  directive as `xhci98.inf`'s; never the 98 SE and ME or right-click
  sections, no `Reboot`. Both INFs' helper copy lines carry
  `COPYFLG_NO_VERSION_DIALOG` (`0x20`): a newer helper is kept.
- Gates: the CM gate's `CM-HLPR` with 13 mutations; `HLPR-SVC` requires
  the flag on `xhci98.inf`; the footprint's two rows (`review`).
- Packaging: `nt-x86\cm\xhcihlpr.sys` beside `xhcicm.inf`, the same bytes
  as the root's (`PKG-FAMILY`; record 36 section 3.1); the release
  readme's contents line.
- The installer: `inst_board_sendable` counts an NHI the run installs; a
  run counts only where the staging does not hold the INF
  (`inst_board_run_installs`, `cm_inf_held`); a helper in place counts at
  3.4.1.0 or later (`inst_board_hlpr_cmp`), never the package's version,
  which the 3.4.0.0 minor-0 helper shares until the bump (the two points
  of the inst4b merge review); a missing or older helper reinstalls the
  NHI (`group_verdicts`); the uninstall removes `xhcihlpr` only when
  neither `xhci98` nor `xhcicm` is left; the helper-less line names
  `xhcihlpr`.

#### Checks and builds

- Host: `test_inst` 8583 checks, 0 failures. Gated x86 `all` of
  `87ade048` (after `src\pfw`, `xhciinst\di`, `xhciinst`): BUILD + GATES
  PASSED (release qemu), the window self-test 969 checks, 0 failures,
  the packager self-tests 1824 checks
  (`E:\xhci98-scratch\p46-w46-inst5\gated-x86-5.log`). Gated amd64
  `release` of `87ade048`: BUILD + GATES PASSED (`gated-amd64.log`).
  Four earlier gated runs failed and were fixed before this one: the
  copied-only rows' layout self-test (`9282757c`, `d1096384`,
  `f880e609`), a stale `HLPR-OLD` mutation and the packager's family
  stand-in (`87ade048`, `9282757c`).

#### The leg (10 x86 q35, fresh overlay, development host A)

`usb4-emu`'s ASM4242 model with its mailbox (`fw-handover=on`,
`pm-cap=on`, `usb4-host.fw-handover-gate=on`, `acpi-sb-osc=off`), the
ORICO enclosure at power-on, `-smbios
type=2,manufacturer=usb4-emu,product=asm4242-model`; the emulator of
w46-nthand's E route (`qemu-system-x86_64.exe` SHA-256 `53a3811a...07814`,
copied). The kit: the family layout of `87ade048` with a TEST-ONLY
`XHCIINST.EXE` built without `CM_OSCP_RELEASE` (`c0910bc3...`, never
packaged); after the install the qemu flavour's x86 `xhcicm.sys`
(`7f462d65...`) put in place for its debug log. Hashes
`E:\xhci98-vm\p46\w46-inst5\hashes-xt10-87ade048.txt`.

| Step | Result |
|---|---|
| `INSTALL.BAT /console /yes /nopause /cmonly` (no xHCI98 USB drivers; Windows' `usbxhci` keeps the controllers) | the report "ASM4242 board: known; the USB4 handover is sent." under the NHI; the filter list; "Installing Universal Serial Bus (USB) Controller ... done."; exit 3. `xhcihlpr`: `BOOT_START`, `\SystemRoot\System32\drivers\xhcihlpr.sys`, the display name as `xhci98.inf`'s, `STOPPED` until the restart; `xhcihlpr.sys` `f4380b7d...` (the package's); no `xhci98` service. PASS |
| the boot after (the qemu image) | `cm.osc.profile=2`, `cm.osc.members=6`, `cm.osc.route=100h` (the helper's routines found), `cm.osc.handover=100h`, `cm.osc.query=000F0100h`, `u4.host.id=174C2428`, `u4.start=0`, `cm.osc.guard.clear=100h`; `xhcihlpr` and `xhcicm` `RUNNING`. PASS |
| the NHI's device removed (`pnputil /remove-device`, as Device Manager's uninstall: the Thunderbolt part alone) | "Device removed successfully."; `xhcihlpr` `RUNNING`, `BOOT_START`, its file in place. PASS |
| `INSTALL.BAT /console /uninstall /yes /nopause` | the first run stopped while giving the fifth tunnel port back to `pci.sys` (below); after a restart the second run resumed: the NHI and its INFs removed, "Removing the service xhcicm (running) ... marked for deletion", "Removing the service xhcihlpr (running) ... marked for deletion", the files deleted, exit 3; after the restart neither service exists (1060) and no `xhci*.sys`. PASS |

Evidence: `E:\xhci98-vm\p46\w46-inst5\k10x\` (`serial-a..d.log`,
`debugcon-b.log`, `after-uninstall.txt`, `*.png`, `pids.txt`).

#### Found

- The first uninstall run ended with no last line while giving a tunnel
  port (`1B21:2423`, the downstream port of the ASM4242 xHCI) back to
  Windows' driver; both elevated consoles of the session closed, and an
  elevated `cmd` then failed with "The extended attributes are
  inconsistent" until the restart, after which the uninstall resumed and
  finished. The boot before had removed the NHI by `pnputil` and an
  `XHCITR /report` in it had not returned. Not this change's code (the
  tunnel-port pass is record 23 section 9.2's); not reproduced or
  investigated.
- Until the 3.4.1.0 bump a development build's own helper reads 3.4.0.0:
  a reading after the install (the barrier's re-plan) logs "the driver's
  routines need xhcihlpr, which is not in place after this run" while the
  report said "sent" (reporting only; the bump removes it).

#### Left

The bump to 3.4.1.0 (46.final), before which the version rules above
hold only for an install of this run; 2000 and 32-bit XP Thunderbolt-only
legs; the texts, read by the owner.

## 46.18 the owner's wording pass, applied (2026-11-04)

The owner's reading of the 3.4.1.0 wording sheet (2026-11-04, "agree with
all") applied in `08d8277c` and `b8acde9e` (Stor98's Windows 10 line, the
owner's choice of the one-line form, its table row widened from 260 to
272 units, the `IDC_C_S98_T` control's own width). Reviewed and revised.

### Applied

| Row | Text now |
|---|---|
| A1 | "Setup will install only the xHCI98 tools. xHCI98 USB drivers already installed are kept."; the connection manager's line alike, "Setup will install only the Thunderbolt / USB4 connection manager. xHCI98 USB drivers already installed are kept." |
| A2 | the 98 SE and ME warning ends "Setup will install only the xHCI98 tools. xHCI98 USB drivers already installed are kept." |
| A3 | "Windows 10 has its own NVMe driver; Stor98 stops drives cleanly." |
| A4 | the note "a known build always gets it; firmware Setup does not recognise never does. Restart Windows after changing it." |
| 21 | under the box "Setup will copy all drivers to <dir>\USBDRV\NONFREE." alone |
| 22 | the Help title "Thunderbolt / USB4 settings" |
| 23 | "Setup will remove all of this package's drivers, tools and records." |
| 24 | "Removed %s from CriticalDeviceDatabase\%s's %s (a copy Windows made at a device's install)." |
| 25 | the system line with `szCSDVersion`'s spaces trimmed and a blank one left out (`inst_os_line`) |
| 26 | the window's Finish where no Steps page was shown: "Not everything is done: see the steps in the log, then restart Windows." (`inst_final_text_shown`) |
| 27 | with the box unticked and a connection manager installed: "- Thunderbolt / USB4 support (experimental): not changed; the installed one is kept" |
| 28 | "Disconnect all external NVMe drives before Setup continues." |
| A14 | "A queued replacement of %s could not be taken back: %s is left as it is." (no fixed break) |
| A22 | "On Windows 98 SE and ME Setup will copy only the xHCI98 tools to the install folder; DevicePath and the devices are left as they are." |
| 14d | "Snapshot saved in %s as %s, but its .TXT is missing / incomplete. Send the files anyway." |

### Checks

- Host: `test_inst` 8486 checks, 0 failures; `test_tr` 1602, 0 failures.
- Gated x86 release build at `b8acde9e` (after `xhciinst`, `xhciinst\di`
  and `xhcitr` built): BUILD + GATES PASSED, the window self-test 964
  checks, 0 failures. Log
  `E:\xhci98-scratch\p46-w46-words\build-x86b.log`. `check-text.ps1`
  and `check-readme-text.ps1` pass.

### The screens (the package of `b8acde9e`, fresh overlays, the emulated ASM4242 USB4 host)

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480 | Welcome | "Windows: Windows 98 SE (4.10.2222 A), 32-bit" (`w98-25-welcome.png`) |
| 98 SE | both boxes unticked, Next | the warning ends "Setup will install only the xHCI98 tools. xHCI98 USB drivers already installed are kept." (`w98-A2-warning.png`) |
| 98 SE | the same, Back to Welcome | the plan "On Windows 98 SE and ME Setup will copy only the xHCI98 tools to the install folder; DevicePath and the devices are left as they are." (`w98-A22-plan.png`) |
| 10 x86, 1024 by 768 | the Thunderbolt and USB4 page | Stor98's line whole on one line, Settings... at the foot (`w10-A3-tbt-page.png`); Welcome "Windows 10 (10.0.19045), 32-bit" |
| 10 x86 | the page's Settings, Help | "Help: Thunderbolt / USB4 settings" (`w10-22-help-title.png`) |

Evidence: `E:\xhci98-vm\p46\w46-words\w98\` and `w10x86\`; hashes
`E:\xhci98-vm\p46\w46-words\hashes-b8acde9e.txt`.

### Seen, not changed

Fixed breaks inside path-length console lines next to the rows above:
"%s could not be removed from CriticalDeviceDatabase\%s's %s\n(error
%ld)." and "%s could not be copied to %s: %s plugged in later asks for
the\ndriver disk (type %s)." (`inst_eng.c`).

## 46.22, the 10 x64 leg on the signed package (20fe2c09) (2026-11-04, w46-owed1)

The leg 46.22 left owed: Windows 10 x64 on a package signed with the
project's test certificate. The owner signed an amd64 package of
`20fe2c09` (the tip of `3.4.1.0` before the owner's wording pass,
`b72ec20a`), so its texts are the drafts of `runs/run-46.md` "46.22", not
the owner's wording of "46.18 the owner's wording pass, applied". The kit
(`nt-x64\`, `tools\`, `SETUP.EXE`, `INSTALL.BAT`) was copied whole, its 34
files checked against the owner's list of SHA-256 sums (`SETUP.EXE`
`54a2c8a7...`, `XHCIWZ64.EXE` `bd2188da...`, `XHCIIN64.EXE` `39a6f555...`,
`xhci98.sys` `1fc899a8...`, `xhcicm.sys` `7c1991d9...`).

Development host A, a fresh overlay of the q35 base `w10x64` (22H2,
19045; test signing on in the base, `bcdedit` reading "testsigning Yes"),
`launch-q35.ps1 -Usb4 asm4242 -EncAtBoot -Tablet`, `-smp 2`, the guest's
date the host's. In the guest `xhci98-test.cer` was added to Root and to
TrustedPublisher (`certutil -addstore`; TrustedPublisher, absent on the
base, needed `-f`); nothing was signed in the guest. Before the leg: no
`xhci98`, `xhciuas`, `xhcicm`, `xhcicmf`, `stor98` or `xhcipfw` service,
two third-party INFs in the driver store.

| Case | Steps | Result |
|---|---|---|
| (a) both boxes unticked | `SETUP.EXE`; Welcome "Windows: Windows 10 (10.0.19045 ), x64" (this kit's system line, before row 25); Components: the USB drivers' box unticked (10's default), the Thunderbolt box ticked (a host found), unticked by hand | the line under the USB drivers' box stays "Tick to install xHCI98 drivers over existing Windows native drivers for xHCI controllers." on 8, 8.1 and 10 whatever the boxes (`inst_drivers_line`: the tools-alone line, "Only the xHCI98 tools will be installed. ..." in this kit and "Setup will install only the xHCI98 tools. ..." in the owner's wording, is 2000 to 7's and 98 SE and ME's); Next went straight to the Summary: "Install/update only the xHCI98 tools now (no driver changes).", "- none changed (no driver in this run).", "- xhci98.sys and xhciuas.sys: not installed (xHCI98 tools only)", "- Thunderbolt / USB4 support (experimental): not installed" |
| | Install | Finish "Done. No restart is needed.", "Exit code 0."; the log "tools only: no local copy, nothing staged, no device touched"; afterwards no service of the package, the driver store's two INFs alone, no `%windir%\XHCI98`, `C:\XHCI98` holding `XHCIIN64.EXE`, `XHCITR64.EXE`, `XHCIWZ64.EXE` |
| (c) the Thunderbolt and USB4 page (the Thunderbolt box ticked) | Next from Components | Stor98 unticked, its line "Optional on Windows 10: stops drives cleanly when unplugged." (this kit's; the owner's is "Windows 10 has its own NVMe driver; Stor98 stops drives cleanly."), on one line, Settings... at the page's foot |
| (b) `INSTALL.BAT /console` | over (a) | "Install Thunderbolt / USB4 support?" with "Enter for [Y]:" asked; Enter; Stor98 "Enter for [N]:", Enter; Thunderbolt networking "[N]", Enter; `XhciCmMode`, `XhciCmApprove`, `XhciCmSafeEject` at their defaults; the folder; "Install the drivers now?" Enter: `xhcicm.inf` staged as `oem2.inf`, `xhcicmf.inf` as `oem3.inf` (the owner's catalogs accepted, no publisher prompt), the router under `xhcicm` 3.4.0.0, the tunnel-port filters added, "Done. Please restart Windows now.", "Exit code 3." |
| | a restart | `xhcicm` running; the ORICO enclosure's volume as E: (Windows' own `stornvme`); four third-party INFs |
| (d) both boxes ticked | `SETUP.EXE`; Welcome lists the connection manager's devices "Reinstall (same version)"; Components: the USB drivers' box ticked (the UAS box with it), the Thunderbolt box ticked; Next: the 8, 8.1 and 10 message box ("On Windows 8, 8.1 and 10, the xHCI98 USB drivers replace Windows' own USB 3 driver ..."), OK; the Thunderbolt and USB4 page as in (c); Summary "Install the drivers now." with both controllers "Install" | Finish "Done. Please restart Windows now.", exit code 3 in the log, no step "FAILED" |
| | a restart | both controllers ("xHCI98 USB 3 Controller (Red Hat QEMU xHCI)", "(ASMedia ASM4242)") and their root hubs under `xhci98`, the router under `xhcicm`, the tablet's "HID-compliant mouse" under `mouhid`, every one Code 0; `xhci98` and `xhcicm` running; `fc /b` of `system32\drivers\xhci98.sys` against the kit's: no differences |
| the uninstall | `SETUP.EXE /uninstall`: the box ticked, Summary, Remove | the devices removed, the controllers and tunnel ports given back to Windows' drivers (Progress waited on Windows installing them, "answer the window it opened (0 of 1 minutes)", then went on); Finish "Done. Please restart Windows now." |
| | a restart | no service of the package, the driver store's two INFs alone, no `C:\XHCI98`; both controllers under `USBXHCI`, the enclosure's NVMe under `stornvme` (E: ORICO), the mouse under `mouhid`, all Code 0 |

Seen, not changed (this kit's console text): the run's end wraps
"This package has no tools\xhcimark\XHCIMK64.EXE: the safe-removal mark
service" with "is" alone on the next line; the kit has no `xhcimark\`
folder (a package, not a release download).

Evidence: `E:\xhci98-vm\p46\w46-owed1\x64\w10\` (`a*.png` the
certificate and the readings before, `b*.png` (a) and (c), `c*.png` (b),
`d*.png` and `e*.png` (d), `f*.png` and `g*.png` the uninstall), `pids.txt`;
the kit `E:\xhci98-vm\p46\w46-owed1\x64\kit\`.

## 46.18 the owner's wording at 640 by 480 and 120 DPI (2026-11-04, w46-owed1)

The legs of record 26 section 5.10 (*The legs*) read again on the owner's
final wording (`b72ec20a`, "46.18 the owner's wording pass, applied"):
every page, both Settings tabs of each page, both Advanced dialogs and
each Help, on 98 SE at 640 by 480 and on 7 x86 at 120 DPI at its smallest
size, 800 by 600 (7 offers no 640 by 480; section 5.10 allows its
smallest), each page looked at for clipped or overflowing text and for a
scroll bar where none belongs. Help windows whose text is taller than the
box scroll by design (section 5.10, since draft 58); the device and driver
lists and the Advanced lists are list controls.

### Builds and packages

- The tools of `b72ec20a` from their own `build.cmd` (`src\pfw`,
  `xhciinst\di`, `xhciinst`, `xhcitr`, `xhciperf`, each exit 0), then one
  gated x86 `release`: BUILD + GATES PASSED, `test_inst` 8486 checks, 0
  failures, the window self-test 964 checks, 0 failures
  (`E:\xhci98-scratch\p46-w46-owed1\gated-x86.log`).
- Kits: the family layout of `make-package.ps1 -Arch x86` with this tree's
  tools (`XHCIWIZ.EXE` `c7ed35dd...`, `SETUP.EXE` `9789b69d...`,
  `xhci98.sys` `20741de8...`; `E:\xhci98-vm\p46\w46-owed1\hashes-kit86-b72ec20a.txt`),
  and, for the Free and Non-free USB drivers pages, the test cut of
  `make-release.ps1 -Arch x86 -TestLabel owed1 -SkipQualtool
  -SkipMarkTool` with both driver sets (`upload-3.4.0.0-nonfree`; the same
  binaries).

### The legs (q35 bases, fresh overlays, the emulated ASM4242 USB4 host with the enclosure at power-on)

| System | Read | Result |
|---|---|---|
| 98 SE, 640 by 480, 96 DPI (`w98`, the package kit) | Welcome and its Help; Components (the USB drivers' box ticked, unticked, the Thunderbolt box alone, both) and Help; Settings (USB tab) and Help; Advanced and Help; the Thunderbolt and USB4 page and Help; its Settings, the Thunderbolt / USB4 tab and the Stor98 tab, each with Help; Stor98's Advanced and the Thunderbolt Advanced, each with Help; Summary and Help; Install: Progress and Help, Finish (the restart words) and Help | every window inside the screen above the taskbar, every text whole, no scroll bar on a page; Welcome "Windows: Windows 98 SE (4.10.2222 A), 32-bit"; "Help: Thunderbolt / USB4 settings"; Stor98's Advanced "Default: 2048 KiB. Allowed: 64 to 2048 KiB, in steps of 4." |
| 98 SE (`w98` again, cold start, the full download) | the window self-test (`XHCIWIZ.EXE /selftest`, from a DOS box, redirected); Setup over the install: Welcome, Components (the Thunderbolt box unticked, its default on 98 SE), Free USB drivers and Help, Non-free USB drivers and Help, Summary; Components with both boxes unticked, Next; the install folder dialog; the uninstall: its first page and Help, Summary and Help, Remove, Finish and Help | 964 checks, 0 failures; the Non-free line "Setup will copy all drivers to C:\XHCI98\USBDRV\NONFREE." whole; the Summary's "- Thunderbolt / USB4 support (experimental): not changed; the installed one is kept" on one line; the warning ending "Setup will install only the xHCI98 tools. xHCI98 USB drivers already installed are kept."; the uninstall's Help "Setup will remove all of this package's drivers, tools and records."; all whole |
| 7 x86, 120 DPI (`LogPixels` 120, then a restart), 800 by 600 (`w7`, the full download) | the window self-test (elevated, redirected); Welcome and Help; Components and Help, the boxes in each state; Settings (USB) and Help, Advanced and Help; the Thunderbolt and USB4 page and Help, its Settings (both tabs) and both Advanced dialogs, each with Help; Free and Non-free USB drivers, each with Help; Summary and Help; Install: Progress and Help, Finish (the restart words) and Help | 960 checks, 0 failures; every window inside 800 by 552 and every text whole; both unticked the line under the USB drivers' box "Setup will install only the xHCI98 tools. xHCI98 USB drivers already installed are kept." on one line; the Thunderbolt box alone "Setup will install only the Thunderbolt / USB4 connection manager. xHCI98 USB drivers already installed are kept." on two lines, the rows below moved down by one; but see below |

### Found, and fixed (`97920761`)

On 7 at 120 DPI, unticking the USB drivers' box with the Thunderbolt box
ticked made the line under it two lines while the page showed; the rows
below were moved (`comp_layout`, `SetWindowPos`) and the old pixels of the
moved labels stayed on the page, over the Thunderbolt box's line
("Thunderbolt / USB4 controller found." unreadable;
`w7\7-120dpi-comp-tbonly3.png`). Opening and closing Help did not clear
it (the dialog's saved bits); Next and then Back drew the page cleanly,
the layout right (`w7\7-120dpi-comp-tbonly-back.png`), so nothing was
clipped: the moved controls were not drawn again. `97920761` draws a page
again whole (`RedrawWindow`, erase, invalidate, all children) after the
Components page's, the Thunderbolt and USB4 page's and the Welcome page's
layout when the page shows. Reviewed and revised. The window self-test
does not see a page's pixels, so the fix was read on the guests.

- Gated x86 `release` of `97920761` (the tools rebuilt first): BUILD +
  GATES PASSED, `test_inst` 8486 checks, 0 failures, the window self-test
  964 checks, 0 failures (`E:\xhci98-scratch\p46-w46-owed1\gated-x86-2.log`).
  Its test cut (`XHCIWIZ.EXE` `dbea01b5...`, `SETUP.EXE` `004f1fba...`;
  `hashes-xf86b-97920761.txt`).
- 98 SE at 640 by 480, a fresh overlay (`w98c`): Welcome and Components,
  the boxes clicked through each state: whole and clean (the USB drivers'
  box has no line under it on 98 SE and ME; its warning stands for it).
- 7 x86 at 120 DPI, the same overlay (`w7`, a cold start on the new
  cut): Welcome; Components with the boxes clicked through each state,
  the line growing to two lines and back: every row in its place, no old
  pixels (`w7-120dpi-fix-*.png`).

### Seen, not changed

- XHCIWIZ.EXE's window self-test counts 964 checks on 98 SE and 960 on 7
  (the checks that apply to the system it runs on).
- On 98 SE the Thunderbolt box is unticked by default with an ASM4242
  host found, so a second run over an install with the connection manager
  shows "not changed; the installed one is kept" (row 27) unless the box
  is ticked again.

Evidence: `E:\xhci98-vm\p46\w46-owed1\w98\` (`98-*-page.png`,
`98-*-help.png`, `98b-*.png`), `w98c\`, `w7\` (`7-120dpi-*.png`, `r0*.png`
the defect), `pids.txt` in each.

## 46.12 on NT: the x86 route and the read path (2026-11-04, w46-nthand3)

Record 32 sections 12.8.15.15 (the helper's channel, minor 1) and
12.8.15.17 (the read path, B, C and D), each reviewed and revised:
`74dcf2ce` (the channel and its x86 caller), `1df5a3bf` (the read path,
`XhciCmAsm4242Structural` on every system, the qemu flavour's lists
gaining `usb4-emu`'s build 1 `--osi`), `4ad98288` (`CmRoutine`, the
lookup for any `xhcicm` caller). Host tests: `test_msi` 291 checks,
`test_cm_oscp` 132, `test_cm_osc` 175, the whole suite passed.

### The x86 route on 7 x86

The 7 x86 q35 base (build 7601), a fresh overlay; `usb4-emu`'s ASM4242
model as in "46.12 on NT, E" above (mailbox model, router silent until
the handover, `acpi-sb-osc=off`, no SSDT, the ORICO enclosure at
power-on, `-smbios type=2,manufacturer=usb4-emu,product=asm4242-model`).
The release package of `74dcf2ce` installed (`INSTALL /console /yes
/thunderbolt`, exit 4: the kit carried no `XHCIMARK.EXE`), so
`xhci98.sys` runs both controllers and `xhcihlpr.sys` is loaded; then the
qemu flavour's `xhcicm.sys` (SHA-1 `be40af29...328c`) put in place and
`xhcicm` added to the upstream port's and the xHCI's `UpperFilters` by a
script run as SYSTEM (the installer's entry is another task's). Evidence:
`E:\xhci98-vm\p46\w46-nthand\e7\` (`debugcon-c.log`, `report-c.log`).

| Boot | What the log says | Reading |
|---|---|---|
| c, cold | `cm.osc.nt.build=1DB1h`; the identity read through WMI; `cm.osc.profile=2`; `cm.osc.members=6`; `cm.osc.route=100h`; `cm.osc.mb.before=0`; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `u4.host.id=174C2428`; `u4.start=0`; `cm.osc.guard.clear=100h`; a second start `cm.osc.already=100h` | the routines found through the helper (no `cm.osc.nt.route`), the known profile sent, the router answering: PASS. XHCITR: the host active, both tunnel ports active, the enclosure's switch `1B21:2463` and its NVMe behind them; no `XhciCmOscPending` left |

Windows reinstalled `xhcicm` from its store once the two new filter
devnodes started (the release image back on disk after that boot, SHA-1
`ebbdb315...90d3`); the image the boot ran was the qemu one, which alone
writes these lines.

### The read path, B, on 10 x64

The 10 x64 q35 base (build 19045), a fresh overlay; the same model, with
`usb4-emu`'s build 1 tables made by `asm-fw-ssdt.py 0x9FF00000 <dir>
--build 1 --osi --externals-ifzero` (main `189d459`; `--osi` so that
Windows' own ACPI sends nothing from the model's `_OSC`, as on the
board), `acpi-sb-osc=off`, no `-smbios` profile (so E finds the board
unknown). `3.4.0.0` installed (exit 3), then `4ad98288`'s qemu flavour
amd64 `xhcicm.sys` (SHA-256 `987e1522...4b8d` unsigned, `6686e4cd...6a02`
as signed in the guest with a throwaway certificate) put in place, the two
`UpperFilters` entries as above, and `XhciCmAsm4242Reads` 1 in the NHI's
driver key. Evidence: `E:\xhci98-vm\p46\w46-nthand\e10r\debugcon-c.log`.

| Boot | What the log says | Reading |
|---|---|---|
| c, cold | `cm.osc.nt.build=4A61h`; `cm.osc.smbios.refused=0Ch` (no type 2: unknown); `cm.osc.read.route=1`; `cm.osc.read.ranges=4`; `cm.osc.read.uefi=C0000002h` (not implemented: a BIOS boot); `cm.osc.read.scan=0` (found); the three SSDTs' ids; the five methods' hashes (`_OSC` `2FF061F8h`, the `--osi` entry); `cm.osc.tier=2`; `cm.osc.dusb4.10=2`, `4d=1`, `4b=0`; `cm.osc.root=300h`; `cm.osc.read.refused.pages=0`; then `cm.osc.guard.preflush=C000014Dh`, `cm.osc.defer=10Fh` and 15 deferred tries, each reading the tables again; `cm.osc.members=6`; `cm.osc.route=100h`; `cm.osc.mb.before=0`; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `u4.host.id=174C2428`; `u4.start=0`; `cm.osc.guard.clear=100h` | B from its own root pointer, no page of the reads in the system's ranges, the known build's tier, sent at the deferred activation, the router answering: PASS |

Found: while the start is deferred (record 32 section 12.8.15.16) each
try runs the read path whole again, so the firmware's tables and data
were read 16 times in that boot (no page refused). A follow-up for the
coordinator: test the key's flushability before the match on a start's
try, so a deferred start reads them once.

### 98 SE, the x86 image of `4ad98288`

98 SE runs the board's handover path without ACPI, which the q35 `w98se`
base does not (it runs ACPI): the leg used an overlay of `w46-cm2`'s
non-ACPI 98 SE disk `lg\disk.qcow2` (`na` plus `3.4.0.0` `/thunderbolt`),
the one the reference legs of "46.12 The second tier" ran on, on the
coordinator's decision of 2026-11-04; that disk read-only, SHA-256
`647594d4...29fd2` before and after. The release package of `4ad98288`
installed over it (`INSTALL /console /yes /thunderbolt /force`, exit 3;
the router's `xhcicm` "date 11-4-2026, started"); the NHI's driver key
then holds `XhciCmAsm4242Handover=1` and `XhciCmAsm4242Structural=0`
(written by `[Cm.Values]` now). The qemu `xhcicm.sys` (SHA-256
`13bb6f1b...62ab2`) copied in; a cold start with build 1's tables at
`7FF00000h` (`w46-cm3`'s): `cm.osc.tier=2`, `cm.osc.handover=100h`,
`cm.osc.query=000F0100h`, `u4.host.id=174C2428`, as the reference leg k1:
PASS (`E:\xhci98-vm\p46\w46-nthand\e98\debugcon-b.log`).

### Not run

C (7 x64 or Vista): not reached. D: the release kit of `4ad98288`
installed on XP SP3 (exit 4, both controllers on `xhci98.sys`, the
router started), but the leg's own boot was not reached: Windows' Found
New Hardware wizard for the tunnelled NVMe took the focus from every
keystroke the leg sent (`E:\xhci98-vm\p46\w46-nthand\exp\`); owed, with
2000. Reads 0 on 10 x64 is "46.12 on NT, E"'s boot i, unchanged code
path. The S4 resend, fast startup, the fault legs (the mailbox model has
none for a member's held request or a surprise removal during the
exchange) and Windows 11 (no q35 base), and the bench.
## 46.12 The 9x fault legs: a disable and enable, the mailbox busy, a reply never given, the release flavour (2026-11-04, w46-flt)

The legs record 32 section 12.8.8 lists and "46.12" and "46.12 The second
tier" above left owed, on the mailbox faults `usb4-emu` gained for them
(`a67af95`, `46ec9ca`, `57dd977` on its branch `emu-flt`, reviewed and
revised; `docs/FAULTS.md` "The ASM4242 handover mailbox"): `x-mb-busy-ms`
(each command held busy that long, `4294967295` for good), `x-mb-busy-reset`
(busy from the reset too, before any command), `x-mb-no-reply` (taken,
never answered, no handover); four qtests in `usb4-asm4242-test`, each
reading the router's ring 0 answer under `fw-handover-gate=on`.

**The build.** The gated x86 build of `b72ec20a` (release and qemu: BUILD
+ GATES PASSED, `E:\xhci98-scratch\p46-w46-flt\gated-x86-all.log`); the
release kit by `make-package.ps1` and `ConvertTo-FamilyLayout` with this
tree's `XHCIINST.EXE` (`a49c3b49...`) and `XHCITR.EXE` (`a3a1d3fd...`).
`xhcicm.sys` qemu `3e2c4620...`, release `ae2a6d71...`; `XHCIPFW.VXD`
qemu `da2eb2d2...`, release `fcf71471...`.

**The vehicle.** 98 SE without ACPI: an overlay of w46-cm3's
`lb\disk.qcow2` ("46.12 The second tier"), this kit installed
(`XHCIINST /dir 98-me /yes /nopause /thunderbolt /stor98`, exit 3), the
restart, then the qemu `xhcicm.sys` and `XHCIPFW.VXD` copied over it and
checked (`fc /b`, at each leg's start too); frozen as
`E:\xhci98-vm\p46\w46-flt\fb\disk.qcow2` (`7d567903...`). (The install left
`XHCIPFW.VXD` as it was, "another installation's file", the image being
w46-cm3's test copy, so `XhciPfwNhiLine` is absent there and its default 1
stands.) Each leg a fresh overlay of it, q35, TCG, one vCPU, 512 MB, the
emulator `qemu-system-x86_64.exe` of `usb4-emu` `57dd977` (`e68ad1bc...`),
`pm-cap=on`, `fw-handover=on`, `fw-data=0x7FF00000`,
`fw-handover-gate=on`, build 1's three SSDTs of `asm-fw-ssdt.py --build 1`
(`81f55c57...`, `52fc6320...`, `3e757b2f...`, w46-cm3's), a cold start.
Read in each: the `qemu` flavour's log on the debug console, the model's
mailbox lines in QEMU's output, XHCITR's report (File, Save Report) and
the NHI's `XhciCmStatus` record (its flags word: bits 18:16 the tier, bit
19 the handover written this boot; record 32 section 12.8.11).

| Leg | Fault | The design says | Seen |
|---|---|---|---|
| a1 | none | sent once, the router answering (12.8.5) | `cm.osc.tier=2`, `cm.osc.mb.before=0`, `cm.osc.handover=100h`, `cm.osc.query=000F0100h`; the model took `0102h` and `0180h` once each; `u4.host.id=174C2428`; XHCITR "Stage: active"; flags `000A0000h` (tier 2, bit 19) |
| a1 | the NHI disabled ("This device is disabled (Code 22.)") and enabled in Device Manager | "a restart of the NHI (a disable and enable) sends nothing again" (12.8.2); bit 19 "survives a disable and enable" (12.8.11) | the second start: `cm.osc.tier=2`, `cm.osc.mb.before=00000280h`, `cm.osc.already=00000100h`, no third command; the router answering; the record of that start (uptime 1244 s): stage active, flags `000A0000h` |
| b1 | `x-mb-busy-ms=4294967295`, `x-mb-busy-reset=on` (busy before the driver writes) | "the mailbox clean: the command dword ... bits 11:8 not 1 ... else nothing is sent, nothing written" (12.8.4), `107h` the last hardware check (12.8.12); "a host whose router then does not answer fails its start as today (inert reason 12)" (12.8.5) | `cm.osc.mb.before=00000100h`, `cm.osc.refused=00000107h`; no command reached the model; `u4.failed=0000000Ch`; XHCITR "Stage: failed"; flags `00020000h` (tier 2, bit 19 clear) |
| b2 | `x-mb-busy-ms=4294967295` (the command never accepted) | "Accepted: ... within 1000 ms"; "a timeout ... ends it, logged with the values read" (12.8.5) | `cm.osc.not-accepted=00000102h`, `cm.osc.handover=FFFFFFFEh`, no query; the model held `0102h` busy; `u4.failed=0Ch`; "Stage: failed"; flags `000A0000h` (the write made) |
| b3 | `x-mb-busy-ms=300` | accepted within 1000 ms, then as a1 | `cm.osc.accepted-ms=106h` and `F4h` (the wait's count of 1 ms stalls against the model's 300 ms of virtual time), `100h`, `000F0100h`; the model "taken after 300 ms" twice; the router answering; "Stage: active"; flags `000A0000h` |
| c1 | `x-mb-no-reply=on` | "Answered: ... within 1000 ms"; "the reply register is written 0 only once a reply came" (12.8.5); "a reply never given (the start inert, no second attempt at the next start)" (12.8.8) | `cm.osc.accepted-ms=0`, `cm.osc.no-reply=00000000`, `cm.osc.handover=FFFFFFFDh`, no query; the model "taken, no reply" once; `u4.failed=0Ch`; "Stage: failed"; flags `000A0000h` |
| c1 | the NHI disabled and enabled | no second attempt | the second start: `cm.osc.mb.before=00000202h`, `cm.osc.already=00000100h`, no second command; `u4.failed=0Ch`; its record (uptime 1372 s): stage failed, flags `000A0000h` |
| d1 | none; the release `xhcicm.sys` put over the qemu one, a cold start | "The release flavour declines the emulator's tables (its list holds the board's build alone)" (12.8.8); tier 1 "the code not recognised" (12.8.11) | the driver's log in XHCITR's report: the five forms logged, `cm.osc.tier=1`, `cm.osc.refused=0Dh`, `u4.failed=0Ch`; `x-mb-state` "commands=0"; "Stage: failed"; flags `00010000h` (tier 1) |

So each ends as the design says. Two readings for the record's text: the
"inert reason 12" of section 12.8.5 is the U4 core's reason
(`TBT_INERT_HOST_ROUTER`, "host router unanswered") in the log's
`u4.failed`, while the start's stage is "failed" (XHCITR prints a reason
only for an inert stage, so its line is "Stage: failed"); and on 98 SE
and ME the record has no bit for an exchange that timed out (bit 21 is
NT's), so a1 and c1 differ in the record by their stage alone, and in the
log by `cm.osc.handover`.

Evidence: `E:\xhci98-vm\p46\w46-flt\` `a1\` to `d1\` (`debugcon-*.log`,
`qemu-*.out`, `serial-*.log`: the reports and the `Enum\PCI` exports,
`cmdline-*.txt`, `a1\dm*.png` and `c1\dm-*.png` the Device Manager steps,
`d1\mbstate-b.txt`), `pids.txt`; the record decoder
`E:\xhci98-scratch\p46-w46-flt\st.py`.

Not run: ME (its q35 base runs ACPI, and on the ACPI 98 SE the three
SSDTs stopped CONFIGMG, "46.12"); the busy hold past the 1000 ms wait but finite (the
model takes it late, after the driver gave up); the board, LAN on and off.

## 46.11 Steering enabled, ME, a tunnel port's drive and the configuration trace (2026-11-04, w46-flt)

The legs record 32 section 12.10 lists and "46.11 The NHI's and its
port's line FFh" left: steering enabled, ME, a drive behind a tunnel port
after the step, the QEMU configuration trace. The build, the kit and the
emulator as "46.12 The 9x fault legs" above (`b72ec20a`, `qemu`
`XHCIPFW.VXD` `da2eb2d2...` and `xhcicm.sys` `3e2c4620...`, `usb4-emu`
`57dd977`). Vehicles, each a fresh overlay, q35, TCG, one vCPU, 512 MB:

- **98 SE without ACPI** (steering disabled, "IRQ Steering Disabled. IRQ
  Table has some errors."): the `fb` image of "46.12 The 9x fault legs",
  the handover mailbox on.
- **98 SE with ACPI** (the q35 base, installed with `setup /p j`) and
  **ME** (the q35 base, ACPI); ACPI on, but whether a routing link (and
  not the line) serves the switch's functions there was not read: this kit installed (exit 3), the restart,
  the `qemu` files copied over and checked; the ASM4242 model as above but
  no handover mailbox, no SSDT (the router answers without the handover,
  `fw-handover-gate` off). On the 98 SE image the install had found the
  `qemu` `XHCIPFW.VXD` already there (copied too early by the preparation)
  and left it unregistered; the static-VxD key the installer writes
  (`StaticVxD`, `Start`) was then imported by `regedit` before the legs.
- Without the step: `XhciPfwNhiLine` 0 (`regedit`), a cold start.

### The step, with and without

| System | `XhciPfwNhiLine` | XHCIPFW's log (02:03.0, 64:00.0) | The NHI (`cm.irq.resource`) | QEMU's lines after the start (02:03.0, 64:00.0) |
|---|---|---|---|---|
| 98 SE, no ACPI (t1) | absent (default 1) | written `0Bh` to `FFh` (outcome 1), both | no IRQ (`0`) | 255, 255 |
| 98 SE, ACPI (s1) | 1 by default (absent) | written `0Bh` to `FFh`, both | no IRQ (`0`) | 255, 255 |
| 98 SE, ACPI (s0) | 0 | left (outcome 6, switch setting 0), both `0Bh` | an IRQ, shared (`00011001h`) | 11, 11 |
| ME, ACPI (m1) | 1 (written by the install) | written `0Bh` to `FFh`, both | no IRQ (`0`); XHCITR's report: no IRQ entry, its memory ranges listed | 255, 255 |
| ME, ACPI (m0) | 0 | left (outcome 6), both `0Bh` | an IRQ, shared (`00011001h`) | 11, 11 |

In every leg the router answered (`u4.host.id=174C2428`) and the start
went active; the tunnel ports' lines (02:00.0 `0Bh`, 02:01.0 `0Ah`) and
the ASM4242 xHCI's (63:00.0, `0Ah`) were the same with the step as
without. So with ACPI on the step does change something: the NHI and its
port start with no IRQ where without it they have IRQ 11, their line's
own value, which is also what a line used as the fallback (no routing
link serving them, record 28 section 12.1) gives; that a routing link was
overridden is not shown. Record 32 section 12.10 said that with steering
enabled the step "changes nothing there"; it is corrected to this
reading, and a case where a link is shown to serve the functions stays
owed. No driver uses that IRQ on 98 SE
and ME (`xhcicm` polls), and ME's drive below works either way (below).

### A drive behind a tunnel port after the step

- **ME** (m1, the step; m0, without): the ORICO model hot-plugged at the
  desktop, the hardware wizards answered (the bridges, then Stor98, no
  restart asked), then "ORICO" at E:, a file copied to it and compared
  (`fc /b`: no differences), in both. With the step, XHCITR's report:
  the tunnel port 02:00.0 IRQ 11, the enclosure's two bridges and the
  Stor98 controller IRQ 11 (level, shared), the second tunnel port IRQ
  10, the ASM4242 xHCI IRQ 10; the enclosure's functions' lines 11 in
  QEMU (the bridge numbering's, from the tunnel port).
- **98 SE** (t1 without ACPI, s1 with): the bridge numbering wrote the
  tunnel port's line to the enclosure's functions (`cm.brnum.9x.irq=
  00010B01h`; in the trace `03:00.0`, `04:00.0`, `05:00.0` written
  `0Bh`), but each of the enclosure's bridges' wizards asked for a restart
  (one more cold start and plug each), and after three cold starts the
  NVMe function's BAR was still not assigned and no E: was listed. The
  same `cm.reenum.9x.noirq.refused=03000203h` appears in w46-cm2's h1,
  whose image had had its enclosure's first plug; on these fresh images
  the first plug's wizards were not completed in the time given. Owed.

### The configuration trace

98 SE without ACPI, the step (`-trace pci_cfg_write -trace
pci_cfg_read`, `t1x\trace-b.log`, 79634 lines, the boot):

| Line | Access | Whose |
|---|---|---|
| 4745, 4755 | 02:03.0 and 64:00.0 `@0x3c <- 0xb` | SeaBIOS |
| 19527 | 64:00.0 `@0x3c -> 0x10b`, among `@0x0`, `@0x8`, `@0xc` of every function | XHCIPFW's flat scan (`Sys_Critical_Init`) |
| 29513 to 29517 | 64:00.0 `@0x3c -> 0x10b`, `@0x4`, `@0x3c <- 0xff`, `@0x3c -> 0x1ff` | the step: the dword read, the command register, the byte written, the read back |
| 37777 to 37781 | 02:03.0 the same (`-> 0x2010b`, `<- 0xff`, `-> 0x201ff`) | the step |
| 38886, 39031 | 02:03.0 and 64:00.0 `@0x3c -> 0xff` | `PCI.VXD`'s enumeration (byte reads, after its BAR sizing of the port) |
| 38128 to 40471 | `@0x3c` written back for 00:03.0, 01:00.0, 02:00.0 (`0Bh`), 02:01.0, 02:02.0, 63:00.0, 00:1C.0 (`0Ah`) and 69:00.0; not for 02:03.0 or 64:00.0 | `PCI.VXD` |

So the step's write comes before `PCI.VXD`'s first read of either
function's Interrupt Line, and nothing reads `3Ch` of them between
SeaBIOS's write and the step but XHCIPFW's own scan.

Evidence: `E:\xhci98-vm\p46\w46-flt\` `t1\`, `t1x\` (the trace), `s1\`,
`s0\`, `m1\`, `m0\` (`debugcon-*.log`, `serial-*.log`, `infopci-*.txt`,
screenshots), `pids.txt`; the images `fb\`, `sa\` (`aed97ae0...`), `ma\`
(`b739943e...`), read-only.

Not run: steering enabled with a routing link shown to serve the
switch's functions; a drive behind a tunnel port on 98 SE (above); the lines on
COM1's IRQ on ME (ME does not run `AUTOEXEC.BAT`'s programs); the board.

## 46.12 on NT: the board once a start, the mailbox's reach (2026-11-04, w46-nthand4)

Record 32 section 12.8.15.16, "The board decided once a start" and "The
alias test and the system gate", and 12.8.15.17's stored result
considered and rejected, each reviewed and revised: `6ae89b03` (the
decision kept a start, the key's flush test before the read path, the
mailbox probe before the read and the marker), `0978309c` (the record:
no cross-boot store, the guard's reach), `0af38a12` (no mailbox access
on Windows 2000, the alias test). Host tests: `test_cm_oscp` 433 checks,
the whole suite passed.

### 32-bit XP: the read path, then the probe

XP SP3 (the q35 `xp` base), an overlay with `4ad98288`'s release kit
(exit 4), then the qemu flavour's `xhcicm.sys` put in place, `xhcicm` in
the upstream port's and the xHCI's `UpperFilters` (a script as SYSTEM),
and `XhciCmAsm4242Reads` 1. The model as in "46.12 on NT: the x86 route
and the read path" above, build 1 `--osi` tables, `acpi-sb-osc=off`, no
`-smbios` profile, and no enclosure at power-on (so Windows asks for no
NVMe driver; the Found New Hardware wizard that stopped the earlier
attempt does not open). Evidence: `E:\xhci98-vm\p46\w46-nthand\exp\`.

| Boot | Image | What the log says | Reading |
|---|---|---|---|
| c, cold | `4ad98288` qemu | `cm.osc.wmi.open=C0000295h` (no SMBIOS block through WMI: unknown); `cm.osc.read.route=3`; `cm.osc.read.ranges=3`; `cm.osc.read.scan=0`; the three SSDTs and five hashes; `cm.osc.tier=2`; `cm.osc.root=300h`; `cm.osc.read.refused.pages=0`; `cm.osc.members=6`; `cm.osc.route=100h`; `cm.osc.refused=109h` | D read the tables from its own scan, no page refused; the send's first mailbox read failed: XP's bus interface does not reach `420h`. The marker had been armed before the hold |
| d | the same | `cm.osc.guard=101h`, `cm.osc.guard.deleted=3` | the marker left by boot c: this boot skipped, the marker deleted (12.8.15.13) |
| e, cold | `6ae89b03` qemu (SHA-256 `ee4df7ad...afbdc`) | `cm.osc.wmi.open=C0000295h`; `cm.osc.probe.mb=FFFFFFFFh`; `cm.osc.probe=109h`; `cm.osc.refused=109h` | the probe refused before the read path and before the marker: no table read, nothing armed. PASS |

### Windows 2000: the route aliases `420h`

2000 SP4 (the q35 `w2k` base, Standard PC), a fresh overlay; the release
kit of `6ae89b03` installed (`INSTALL /console /yes /thunderbolt`, exit 2,
the switch ports "Code 12 until the restart"; Digital Signature Not Found
answered Yes), a cold start, then the qemu `xhcicm.sys` (SHA-256
`ee4df7ad...afbdc`), the filters (WMI's `StdRegProv` as SYSTEM through
`at`: 2000 has no `reg.exe`) and `XhciCmAsm4242Reads` 1; the same model.
Evidence: `E:\xhci98-vm\p46\w46-nthand\e2k\`.

| Boot | What the log says | Reading |
|---|---|---|
| d, cold | `cm.osc.nt.build=0` (no `RtlGetVersion`); `cm.osc.wmi.none`; `cm.osc.probe.mb=FE70FA80h`; `cm.osc.probe=0`; `cm.osc.read.route=3`, tier 2, `cm.osc.read.refused.pages=0`; `cm.osc.members=6`; `cm.osc.route=100h`; `cm.osc.mb.before=FE70FA80h`; `cm.osc.not-accepted=100h`; `cm.osc.timeout=1`; `cm.osc.handover=FFFFFFFEh` | FAIL, a finding: `FE70FA80h` is the upstream port's dword `20h` (its memory base and limit): 2000's route drops the offset's high bits. The command was written there: QEMU's `info pci` after the boot shows the port's memory range `[0x01000000, 0x000fffff]`, closed (`infopci-d.txt`); everything behind the switch lost its window. Not retried (the stated no-retry held) |

`4ad98288` would have written the same (the send read `420h` and wrote
it). Fixed in `0af38a12` (record 32 12.8.15.16): no offset of `100h` or
more asked of a route on Windows 2000, and the alias test elsewhere.

The gated x86 all build of `0af38a12` passed, then the leg again on the
same overlay with its qemu `xhcicm.sys` (SHA-256 `a3d87802...98f0f`):

| Boot | What the log says | Reading |
|---|---|---|
| f, cold | `cm.osc.nt.build=0`; `cm.osc.probe.system=0`; `cm.osc.probe=112h`; `cm.osc.refused=112h` | the system gate: no read of the tables, no mailbox access; `info pci` shows the port's memory range `[0xfa800000, 0xfe7fffff]`, intact (`infopci-f.txt`). PASS |

### The x86 image of `0af38a12` on Vista, 8 and 8.1: C

The q35 `vx86` (6002), `w8x86` (9200) and `w81x86` (9600) bases, fresh
overlays with a usb-tablet (the publisher prompts clicked); the release
kit of `0af38a12` (x86 release `xhcicm.sys` SHA-256 `05179b16...092df`),
`INSTALL /console /yes /thunderbolt` (exit 3); then the qemu
`xhcicm.sys` (`a3d87802...98f0f`), the filters by a script as SYSTEM and
`XhciCmAsm4242Reads` 1; the same model and tables. On 8 and 8.1 Windows'
own USB 3 driver keeps the controllers without `/usb`, so no helper is
loaded and the first cold start logged `cm.osc.nt.route=1`,
`cm.osc.nt.route.status=C0000034h`, `cm.osc.refused=108h` (the x86
image's routines come through the helper alone, 12.8.15.15); the kit was
installed again with `/usb` (both controllers on `xhci98.sys`). Vista
makes a new devnode for the NHI at each start (its instance id changes),
so its driver key's `XhciCmAsm4242Reads` was new and 0 at each boot
(`cm.osc.unknown`): the value was set machine-wide (`Services\USB`) for
the leg. Evidence: `E:\xhci98-vm\p46\w46-nthand\ev\`, `e8\`, `e81\`.

| System, boot | What the log says | Reading |
|---|---|---|
| Vista, f | `cm.osc.wmi.open=C0000295h`; `cm.osc.alias.100=00020001h`, `cm.osc.alias.20=FE70FA80h`, `cm.osc.probe.mb=0`, `cm.osc.probe=0`; `cm.osc.read.route=2`; class 76: `XSDT` `C0000225h` (not found), `RSDT` and `MCFG` read; `cm.osc.tier=2`; `cm.osc.read.refused.pages=0`; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `cm.osc.guard.clear=100h`; a second start `cm.osc.already=100h` | C, sent, the router answering. PASS |
| 8, d | the same lines, `cm.osc.nt.build=23F0h`; `u4.host.id=174C2428` | PASS |
| 8.1, d | the same lines, `cm.osc.nt.build=2580h`; `u4.host.id=174C2428` | PASS |

### The amd64 image of `0af38a12`

The gated amd64 all build of `0af38a12` passed. Its x64 package is
unsigned without the key, and the installer refuses a 64-bit package
whose Thunderbolt driver has no catalog (7 x64, exit 1, nothing
changed), so the x64 legs installed `3.4.0.0` (the project's test-signed
release) and then put the qemu flavour's amd64 `xhcicm.sys` of
`0af38a12` (SHA-256 `ac017515...79b9` unsigned) in place, signed in the
guest with a throwaway certificate on 7 and 10, unsigned on XP x64; the
filters and `XhciCmAsm4242Reads` 1 as above.

| System, boot | What the log says | Reading |
|---|---|---|
| 10 x64, e (the B leg's overlay; the image signed `f7530af1...b874`) | `cm.osc.probe=0`; `cm.osc.guard.preflush=C000014Dh`, `cm.osc.defer=10Fh`; 15 tries; `cm.osc.smbios.refused`, `cm.osc.read.ranges` and the three SSDT ids once each; `cm.osc.read.route=1`; tier 2; `cm.osc.handover=100h`; `cm.osc.query=000F0100h` | the board decided once a start: the tables read once in a deferred start (16 times before). PASS |
| 10 x64, f (resumed after `shutdown /h`) | `cm.osc.resume=1`; the read path afresh (route 1, tier 2); `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `cm.osc.guard.clear=100h` | the S4 resend (12.8.15.8). PASS |
| 10 x64, g (fast startup: `HiberbootEnabled` 1, `shutdown /s /hybrid`) | the same lines as f | fast startup's return sends again. PASS |
| 7 x64, d (7601) | `cm.osc.probe=0`; `cm.osc.read.route=2`; class 76 as on Vista; tier 2; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; a second start `cm.osc.already=100h` | C on x64. PASS |
| XP x64, d (3790; 3.4.0.0 exit 4, one root hub left) | `cm.osc.wmi.open=C0000295h`; `cm.osc.probe.mb=FFFFFFFFh`; `cm.osc.probe=109h`; `cm.osc.refused=109h` | XP x64's route does not reach `420h` either: refused before the read and the marker. PASS (no send possible) |

### The mailbox's faults on 10 x64

`usb4-emu` main `8f200d5` (the mailbox faults), its binary
`E:\xhci98-scratch\p46-w46-flt\qemu-bin-18573d0` used read-only; the 10
x64 overlay above, cold starts.

| Boot | Model | What the log says | Reading |
|---|---|---|---|
| h | `x-mb-busy-ms=4294967295`, `x-mb-busy-reset=on` | `cm.osc.probe.mb=100h`, `cm.osc.probe=0` (the probe does not judge a busy dword); the read once; `cm.osc.mb.before=100h`; `cm.osc.refused=107h` | busy before the write: nothing written, as on 98 SE. PASS |
| i | `x-mb-no-reply=on` | `cm.osc.guard=101h`, `cm.osc.guard.deleted=1` | the marker boot h's refusal left (armed before the hold): this boot skipped (12.8.15.13) |
| j | the same | the read; `cm.osc.mb.before=0`; `cm.osc.timeout=2`; `cm.osc.timeout.reply=0`; `cm.osc.handover=FFFFFFFDh`, one such line in the boot; QEMU: "command 00000102 taken, no reply (1)" | the timeout logged, never retried (the stated risk). PASS |

### Not run

The bench; Windows 11 (no q35 base); a member's held request or a
surprise removal during the exchange (no model property); XHCITR's own
configuration reads on 2000 (they land in the first 256 bytes as the
driver's did; reads only).

## 46.final, the update over 3.4.0.0: where xhciuas.sys lands (test kit with the 3.4.1.0 bump) (2026-11-04, w46-upd)

The question: does an update replace `xhciuas.sys` when it runs with no
UAS drive connected? The kit is TEST-ONLY: `83931c0e` on branch
`w46-upd`, a merge of `3.4.1.0` (`6304fa35`) with the version bump branch
`w46-docs2` (`37349f91`), made only so the kit stamps 3.4.1.0 (every INF
`DriverVer=11/01/2026,3.4.1.0`, the same date as 3.4.0.0's); it is never
merged or pushed, and no release carries it. Gated `build-driver.cmd all`
(x86, after `src\pfw`, `xhciinst\di`, `xhciinst`, `xhcitr`, `xhcimark`)
and `release -amd64` both "BUILD + GATES PASSED", `make-package.ps1`
exit 0 for both; the amd64 catalogs made unsigned
(`New-PackageCatalogFile`) and the whole `nt-x64\` signed in each x64
guest with a throwaway certificate (MakeCert `-b 01/01/2026 -e
01/01/2030`, Root and TrustedPublisher, SignTool), its `.cer` as the
kit's `xhci98-test.cer`. Kits: `ConvertTo-FamilyLayout` plus `tools\`,
`SETUP.EXE`, `INSTALL.BAT`. `xhciuas.sys` SHA-256: 3.4.0.0 `14200854...`
(98-me and nt-x86, the same file) and `19abfc66...` (nt-x64, the owner's
test signature); the kit `3c221f33...` (98-me, nt-x86) and `edbb03f6...`
(nt-x64 before the guest's signature). The published 3.4.0.0 came from
`releases\3.4.0.0\` (NT) and, for 98 SE, from the cut's
`xhci98-3.4.0.0-nonfree.zip` (the same binaries plus `USBDRV\`: 98 SE needs
its NUSB storage drivers for any USB disk, UAS included; with the plain
package the UAS device stayed at Problem 2). The 98 SE update kit carried
the same `USBDRV\`.

Development host A, fresh overlays of the q35 bases `w98se`, `xp`,
`w10x86`, `w10x64` (`launch-q35.ps1`, `-smp 1` on 98 SE, 2 on NT; no
USB4), the transfer folder as a read-only disk, every reading on COM1.
The UAS drive: QEMU `usb-uas` (serial `W46UPDUAS1`, xHCI port 4,
SuperSpeed) with one `scsi-hd` LUN on an overlay of a 128 MiB FAT disk
holding `TESTDATA.BIN`; plugged detached, the LUN added, then attached;
unplugged with `device_del`; the same overlay each time, so the same
drive. "Read and write" is `TESTDATA.BIN` copied off the drive and
written back as a new file, both compared byte for byte. File identity is
`fc /b` against each kit's file (and on 10 `certutil -hashfile`); the
binding is the UAS devnode's driver key (`InfPath`, `DriverVersion`) and
`Win32_PnPSignedDriver`. 98 SE, XP and 10 x64 ran the update by
`SETUP.EXE` (the wizard), 10 x86 and the XP "with the drive" leg by
`INSTALL.BAT /console /yes` (`/usb` on 10); 3.4.0.0 went in the same way
with "the USB drivers" (98 SE, XP: the defaults; 10: `/usb`). Every
restart is a power off and a cold start.

### 98 SE (`legs\w98-b`, `legs\w98-e`)

| Step | Where `xhciuas.sys` is, and what was asked |
|---|---|
| (a) 3.4.0.0, a restart, the drive plugged | one Add New Hardware wizard "xHCI98 USB Attached SCSI Storage" (location `C:\XHCI98\XHCIUAS.INF`), Finish, E: (ORICO); `SYSTEM32\DRIVERS\XHCIUAS.SYS` SAME AS 3.4.0.0, the devnode Problem 0, E: read and write PASS; unplugged |
| (b) the update, no drive | Welcome: the controller and root hub "Update", and "PCI Universal Serial Bus" "none (listed, not ...)" "Install"; Summary "xhciuas.sys (UAS disk driver): replaced, loaded at next start"; Finish "Done. Please restart Windows now." (Restart later chosen). After: `XHCIUAS.SYS` still 3.4.0.0, `xhciuas.IN$` (40,933 bytes, the kit's) beside it, `WININIT.INI` `[rename]` `C:\WINDOWS\SYSTEM32\DRIVERS\XHCIUAS.SYS=C:\WINDOWS\SYSTEM32\DRIVERS\XHCIUAS.IN$` (and `xhci98.sys=xhci98.tmp`); `C:\XHCI98\XHCIUAS.SYS` and `.INF` the kit's; `INF\OTHER` both INFs (3.4.0.0, 3.4.1.0); the log "placed9x: WININIT.INI [rename] ...", "Setup will replace C:\WINDOWS\SYSTEM32\DRIVERS\xhciuas.sys at the next start." |
| (c) the restart | `WININIT.INI` gone, `WININIT.BAK` holding both lines; `XHCIUAS.SYS` SAME AS 3.4.1.0, no `.IN$` left |
| (d) the drive plugged | no wizard, no prompt; E: read and write PASS, the devnode Problem 0 on the 3.4.1.0 file; its driver key (`Class\USB\0002`) still names the 3.4.0.0 INF (`InfPath` `YEOKHE~2.INF`) |
| (e) the update with the drive (fresh overlay) | Summary the same "replaced, loaded at next start"; the update reinstalled the drive's devnode (`XHCIDI.EXE` job "install ... C:\XHCI98\xhciuas.inf", `CtlSetLddPath C:\XHCI98`) and Windows asked "Insert Disk: Please insert the disk labeled 'xhci98 xHCI Host Controller Disk'", then "The file 'xhciuas.sys' on xhci98 xHCI Host Controller Disk cannot be found", Copy files from `C:\WIN98`; `C:\XHCI98` typed, OK; "xHCI98 USB Attached SCSI Storage: xhciuas date 11- 4-2026, started"; Finish "Done. Please restart Windows now." After: `XHCIUAS.SYS` already 3.4.1.0 (copied by that install), no `.IN$`, `WININIT.INI` only `xhci98.sys`; E: read and write PASS. After the restart and the drive plugged: 3.4.1.0, PASS |

### XP SP3 x86 (`legs\xp-b`, `legs\xp-e`)

| Step | Where `xhciuas.sys` is, and what was asked |
|---|---|
| (a) 3.4.0.0, a restart, the drive plugged | the Found New Hardware wizard and the Logo-testing dialog (Continue Anyway); `xhciuas` running, `system32\drivers\xhciuas.sys` SAME AS 3.4.0.0 (version 3.4.0.0), the devnode `Enum\USB\VID_46F4&PID_0003\W46UPDUAS1` on `oem1.inf` / `DriverVersion` 3.4.0.0, D: read and write PASS; unplugged, `xhciuas` STOPPED |
| (b) the update, no drive | Welcome: only the controller and root hub ("Update"); Summary "- xhciuas.sys (UAS disk driver)" among the components, no UAS device; Logo dialogs (Continue Anyway); Finish "Done. Please restart Windows now.", "Staged xhciuas.inf as oem3.inf." The log: `UpdateDriverForPlugAndPlayDevices` for the controller and the root hub only. After: `xhciuas.sys` SAME AS 3.4.0.0 (`xhci98.sys` 3.4.1.0); `oem1.inf` 3.4.0.0 and `oem3.inf` 3.4.1.0 both in `%windir%\inf`; the devnode still `oem1.inf` 3.4.0.0 |
| (c) the restart | unchanged |
| (d) the drive plugged | no wizard, no prompt; `xhciuas` running from the 3.4.0.0 file, `Win32_PnPSignedDriver` `DriverVersion` 3.4.0.0, `InfName` `oem1.inf`; D: read and write PASS |
| (d') a second drive (serial `W46UPDUAS2`, a new devnode) | the wizard and the Logo dialog; installed from `oem3.inf` 3.4.1.0 (`Class\...\0003`), which copied `xhciuas.sys` 3.4.1.0 over the old one (the first drive unplugged, the service stopped); D: PASS. The first drive plugged again then ran the 3.4.1.0 file (SAME AS 3.4.1.0, version 3.4.1.0, PASS) while its devnode still read `oem1.inf` / 3.4.0.0 |
| (e) the update with the drive (fresh overlay, console) | exit 3, Logo dialogs; right after: `xhciuas.sys` 3.4.1.0, running, the devnode on `oem3.inf` 3.4.1.0, D: PASS; after the restart and the drive plugged the same, no prompt |

### 10 x86 (22H2, `legs\w10x86-b`, `legs\w10x86-e`)

The x86 package is unsigned. 3.4.0.0 `/usb`: "Windows can't verify the
publisher of this driver software" for each INF (Install this driver
software anyway); the console said so too: "Windows keeps its own UAS
driver unless this package's test certificate is trusted". The drive,
first plugged, went to Windows' `UASPStor` (`uaspstor.inf`
10.0.19041.1949), Code 10 (QEMU's known UAS fault under it); `xhciuas`
was never installed as a service and `system32\drivers\xhciuas.sys` never
existed, under 3.4.0.0 or after the update. The update (console `/usb`, no
drive): exit 3, the publisher dialog again; the driver store then held
`xhciuas.inf` 3.4.0.0 and 3.4.1.0 (`oem3`, `oem5`; `oem4` is `xhci98.inf`). After the
restart and the drive plugged: `UASPStor` Code 10 again, no prompt. The
same with the drive connected during the update (fresh overlay). The
readings right after (b) and after (c) without the drive were not taken
(a harness fault); the later readings show the same files.

### 10 x64 (22H2, `legs\w10x64-b`, `legs\w10x64-e`)

3.4.0.0's `xhci98-test.cer` added to Root and TrustedPublisher; test
signing on (the base). No publisher dialog for either kit.

| Step | Where `xhciuas.sys` is, and what was asked |
|---|---|
| (a) 3.4.0.0 `/usb`, a restart, the drive plugged | before the plug no `xhciuas.sys` in `system32\drivers` (staged only, `oem3.inf`); plugged: no prompt, `xhciuas` running, the file `19abfc66...` (3.4.0.0), the devnode on `oem3.inf` 3.4.0.0, E: read and write PASS; unplugged |
| (b) the update by `SETUP.EXE`, no drive | Welcome: "xHCI98 Thunderbolt / USB4 connection manager 3.4.1.0", only "PCI Express Root Port ... Add tunnel-port filter", "xHCI98 USB drivers already installed are kept"; Components: "xHCI98 USB 3 driver (xhci98.sys)" UNTICKED, the Thunderbolt box TICKED ("No Thunderbolt / USB4 controller found ..."); ticked the USB drivers, unticked Thunderbolt, OK on the 8, 8.1 and 10 message box; Summary the controller and root hub "Update", "- xhciuas.sys (UAS disk driver)"; Finish "Done. Please restart Windows now." After: `xhciuas.sys` `19abfc66...` (3.4.0.0, `xhci98.sys` the kit's); the driver store `xhciuas.inf` 3.4.0.0 as `oem3.inf` and 3.4.1.0 as `oem5.inf` (`oem4.inf` is `xhci98.inf` 3.4.1.0); the devnode still `oem3.inf` 3.4.0.0 |
| (c) the restart | unchanged |
| (d) the drive plugged | no prompt; `xhciuas` running from `19abfc66...` (3.4.0.0), the devnode `oem3.inf` 3.4.0.0, E: read and write PASS |
| (d'') `INSTALL.BAT /console /yes /usb` again, the drive plugged | "Updating xHCI98 USB Attached SCSI Storage ... done, restart needed.", "xHCI98 USB Attached SCSI Storage: xhciuas 3.4.1.0, started", exit 3; `xhciuas.sys` 3.4.1.0, the devnode `oem5.inf` 3.4.1.0, E: PASS |
| (e) the update with the drive (fresh overlay, console `/usb`) | exit 3; right after `xhciuas.sys` 3.4.1.0, running, `oem5.inf` 3.4.1.0, E: PASS; after the restart without it the same file; plugged: 3.4.1.0, no prompt, PASS |

### What this says

- 98 SE: as the code reads. With no drive the new file is queued
  (`xhciuas.IN$` plus the `WININIT.INI` line), the restart is asked and
  replaces it, and the drive then runs it with no question. With the drive
  connected Windows reinstalls the drive and asks for the "xhci98 xHCI Host
  Controller Disk" at `C:\WIN98`; answered with `C:\XHCI98` it copies the
  new file at once.
- XP and 10 x64: NOT as expected. The update with no drive stages the newer
  `xhciuas.inf` and leaves the installed drive's devnode on the old INF;
  when that drive is plugged again Windows does not rank the staged INF
  (no wizard, no prompt) and the old `xhciuas.sys` keeps running. The new
  file lands only when some UAS devnode is installed or reinstalled: a
  drive never seen before (XP), the update run again with the drive
  connected (10 x64), or the update run with it connected in the first
  place (XP, 10 x64). Since every drive shares the one service file, a
  new drive's install updates the file for the old drives too (XP), while
  their records keep reading 3.4.0.0.
- 10 x86: `xhciuas.sys` is not used at all (the unsigned package loses to
  `UASPStor`), before or after the update.
- 10's wizard opens an update over 3.4.0.0's USB drivers with the USB
  drivers unticked and the Thunderbolt box ticked; clicked through as
  offered it updates neither `xhci98.sys` nor `xhciuas.sys`.

Harness notes (not product): on 98 SE a 240 MB transfer disk hung Setup
at "reading this computer's devices" (twice, the clock stopped); a small
disk holding only the batches did not. Five 98 SE tries were voided
(the plain package's Problem 2, a boot hang, a registry error after a
forced power off, two stuck installs); one 10 x64 try was voided (keys
from a stopped script).

Evidence: `E:\xhci98-vm\p46\w46-upd\` `legs\` (`serial-*.log`, `leg.log`,
`pids.txt`, the screenshots `gui-*.png`, `*-plug-*.png`,
`xhciinst-log*.txt`), `kits\` (`k341-x86`, `k341-amd64` and their
`-sums.txt`), `scripts\` (`mkbat.py`: the guest batches); the builds'
logs `E:\xhci98-scratch\p46-w46-upd\gated-x86.log`, `gated-amd64.log`.

### 10 x64, the update page over 3.4.0.0 (kit of `c24d22f3`, the same bump)

A second TEST-ONLY kit: `0698fc10` on branch `w46-upd-t2`, `c24d22f3`
(3.4.1.0 with `w46-inst5`'s update-page fix, `e241549b`, and its merge
review) merged with `w46-docs2` (`37349f91`) so it stamps 3.4.1.0; never
merged or pushed. `xhciinst\di`, `xhciinst`, `xhcitr`, `xhcimark` rebuilt;
gated `release -amd64` "BUILD + GATES PASSED", `make-package.ps1` exit 0,
catalogs unsigned and the kit signed in each guest as above (`xhci98.sys`
`1b5886cb...` before the guest's signature; `SETUP.EXE` `a5d0d0eb...`,
`XHCIWZ64.EXE` `b90e2174...`, `XHCIIN64.EXE` `b69be9c0...`). Fresh `w10x64`
overlays, no USB4 host, 3.4.0.0 installed by `INSTALL.BAT /console /yes
/usb` (exit 3) and a restart first. `xhciuas` not judged here.

| Case | Steps | Result |
|---|---|---|
| (u) xhci98 3.4.0.0 running the controller (`legs\w10x64-u`) | `SETUP.EXE` | Welcome "xHCI98 Thunderbolt / USB4 connection manager 3.4.1.0" listing "xHCI98 USB 3 Controller (Red Hat ...)" and "xHCI98 USB 3 Root Hub", "xhci98 3.4.0.0", "Update"; Components: "xHCI98 USB 3 driver (xhci98.sys)" TICKED (the UAS box with it), the Thunderbolt box UNTICKED ("No Thunderbolt / USB4 controller found ..."); Next: the 8, 8.1 and 10 message box, OK; Summary "Install the drivers now.", both "Update", "- xhci98.sys (USB 3 driver)"; Install; Finish "Done. Please restart Windows now." |
| | readings | right after: `system32\drivers\xhci98.sys` SAME AS the kit's (signed in the guest); `INSTALL.BAT /console /yes` (no `/usb`) then printed "Setup updates the xHCI98 USB drivers already running here (/cmonly leaves them as they are).", both devices "Reinstall (same version)", "Exit code 0."; after a restart both devices on `oem4.inf` `DriverVersion` 3.4.1.0, the file the kit's |
| (t) the controller given back to Windows' driver (`legs\w10x64-t`) | `UpdateDriverForPlugAndPlayDevices(PCI\VEN_1B36&DEV_000D, usbxhci.inf, FORCE)` from PowerShell (what Device Manager's Update Driver does with that INF chosen), then a restart | "ok=True"; the controller "Standard USB 3.0 eXtensible Host Controller - 1.0 (Microsoft)" under `USBXHCI`, its root hub under `USBHUB3`, Code 0; `xhci98.sys` still on disk |
| | `SETUP.EXE` | Welcome "No supported device found." "drivers stored for later", "xHCI98 USB drivers already installed are kept"; Components: the USB box UNTICKED, the Thunderbolt box UNTICKED; Next went to the Summary "Install/update only the xHCI98 tools now (no driver changes).", "- xhci98.sys and xhciuas.sys: not installed (xHCI98 tools only)"; Install; Finish "Done. No restart is needed.", "Exit code 0." |
| | readings | the controller still under `USBXHCI`, the root hub under `USBHUB3`, Code 0: nothing replaced Windows' driver |

Evidence: `E:\xhci98-vm\p46\w46-upd\legs\w10x64-u\`, `legs\w10x64-t\`
(`gui-*.png`, `serial-*.log`, `leg.log`, `pids.txt`), `x64c\UX.PS1`, the
kit `kits\k342-amd64\` and its sums; the build's log
`E:\xhci98-scratch\p46-w46-upd\gated2-amd64.log`.

## 46.24 The UAS disk driver on update, and none on 8 to 10 (2026-11-04, w46-uas)

The owner's decisions of 2026-11-04, after 46.final's update legs found
that an update with no UAS disk plugged in left the known disk's devnode
on the earlier `xhciuas.inf` and, with no UAS disk present, the earlier
`xhciuas.sys` (XP x86 and 10 x64; w46-upd's legs): on 2000 to 7 an
update moves every `xhciuas` devnode, plugged in or not, to the package's
version; on 8, 8.1 and 10 the run neither installs nor stages
`xhciuas.inf` and leaves an earlier binding as it is (design record 13
section 6.13.10 question 1 answered). Design: record 23 revision 53,
section 5.5.1; record 26 draft 75 (section 5.3.1 case 3a, the UAS row of
section 5.3, section 5.0); record 27 draft 18 (section 6 steps 4 and 5).
Commits `642b6cd2`, `b73f66b4`, `1111f452`, `7d6203ee`, `5c55782b` and
`559cfc7d` on `w46-uas` (merge `43a132ee` of `3.4.1.0` `c24d22f3`
between), reviewed and revised.

### What changed

- **8, 8.1 and 10**: the UAS driver is out of the run (`inst_uas_here`;
  `usb_decide`, `inst_eng_uas_choose`), so `xhciuas.inf` is neither staged
  nor installed; the window's UAS box is unticked and greyed
  (`inst_eng_uas_offered`); the Components warning ends "UAS disks use
  Windows' own UAS driver." (approved by the coordinator).
- **2000 to 7**: after the per-INF passes, each devnode not present whose
  service is `xhciuas` and whose `DriverVersion` is older than the package
  (with `/force` equal or newer too) is updated by model selection from the
  staged INF (`inst_uas_absent_runs`, `uas_absent_install`: a compatible
  driver list over that INF, `DIF_SELECTBESTCOMPATDRV`,
  `DIF_INSTALLDEVICE` with `DI_DONOTCALLCONFIGMG`), and its driver key read
  again. Then `xhciuas.sys` still older is copied from the local copy's
  `xhciuas.inf` through a SetupAPI file queue (`SP_COPY_NOSKIP`, the
  version read back), not after an install that asked for a restart or
  one refused or cancelled. A Cancel is honoured before each absent
  devnode's install and before and after the copy.
- **98 SE and ME** (the coordinator's item, from w46-upd's 98 SE update
  with the drive plugged in, which asked for "xhci98 xHCI Host Controller
  Disk" and `xhciuas.sys` at `C:\WIN98`): the placement of `xhciuas.sys`
  and `xhcicm.sys` runs before the helper's jobs, and a job whose file it
  placed or queued passes `nocopy` (`inst_di_nocopy`). The same code was in
  `3.4.0.0`; the cause is record 27's second copying job of a run
  (task 45.38).
- New strings (the coordinator's OK, 2026-11-04): "  UAS disks not
  plugged in now, updated too:" with rows "    <instance id> <version> (not
  plugged in)"; "Updating <name> (not plugged in) ... " with the existing
  endings; "xhciuas.sys could not be replaced (error 0x%08lX)."

### Builds and tests

Host tests passed (`test_inst` 8637 checks; `test_uas_absent`, the
warning's vector, `inst_di_nocopy`). Gated builds: `642b6cd2`'s x86
`all` and amd64 `release` (BUILD EXIT 0 both), and the legs' test branch
`w46-uas-t` `7e8fc972` (`43a132ee` with w46-docs2's version bump
`37349f91`, never to be merged), x86 `all` and amd64 `release`, BUILD
EXIT 0 both; the 98 SE kit's tools rebuilt on `71ddc52d` (that branch
with `7d6203ee`). Packages by `make-package.ps1`, the x64 catalogs
unsigned and signed in the guest with a throwaway certificate.
`xhciuas.sys` 3.4.1.0: x86 `8b8ee85f...`, amd64 `1947a623...`;
`XHCIINST.EXE` `cb465571...` (`f3e9b814...` with `7d6203ee`),
`XHCIIN64.EXE` `98639d90...`.

### The legs (q35 bases, fresh overlays, QEMU `usb-uas` drives)

| System | Steps | Result |
|---|---|---|
| XP x86 | the published `3.4.0.0`; drives `W46UASA1` and `W46UASA2` each plugged (its wizard walked), read, unplugged; the test kit's console update with neither plugged; restart; each plugged | The plan listed both under "UAS disks not plugged in now, updated too:"; the run printed "Updating xHCI98 USB Attached SCSI Storage (not plugged in) ... done." twice; the log: `DIF_INSTALLDEVICE` with `DI_DONOTCALLCONFIGMG`, each driver key read back as `xhciuas`, `oem3.inf`, `Uas.Dev`, `3.4.1.0`. After the restart, before any plug, `xhciuas.sys` was 3.4.1.0; each drive plugged with no wizard and no prompt, read and written; WMI: Code 0, `DriverVersion` 3.4.1.0, `InfName` `oem3.inf`. PASS |
| XP x86, signature questions | the same update | six "has not passed Windows Logo testing" dialogs, against four in w46-upd's same update without this change (the two INFs staged, the controller, the root hub): one more per absent drive. `/yes` answers none of them; they are Windows' own and block until answered, as a present device's do |
| 7 x86 | the same steps | the same: both devnodes `oem5.inf` 3.4.1.0 at the update; after the restart the file 3.4.1.0, each drive plugged with no prompt, read and written. PASS |
| 10 x64 update | the published `3.4.0.0` `/usb` with its certificate trusted; the drive plugged, bound to `xhciuas` (`oem3.inf`, 3.4.0.0, as Windows ranks the trusted package above `uaspstor`); unplugged; the test kit `/usb`; restart; plugged | only `xhci98.inf` staged; the devnode `xhciuas`, `oem3.inf`, 3.4.0.0, and `xhciuas.sys` 3.4.0.0 after the update, the restart and the plug; read and written. PASS |
| 10 x64 fresh | the test kit `/usb` on a fresh overlay; restart; the drive plugged | only `xhci98.inf` staged, no `xhciuas.inf` in the store, no `xhciuas.sys`; the disk "USB Attached SCSI (UAS) Mass Storage Device" on `UASPStor`, Code 10, QEMU's device failing under Windows' own driver as it does without this package. PASS (the expected vehicle result) |
| 98 SE, drive plugged | the published `3.4.0.0` (console); the drive plugged, its wizard walked, read; the kit with `7d6203ee`'s tools, console, the drive still plugged; restart; plugged | no Insert Disk prompt: the log shows `xhciuas.sys`'s `.IN$` and `WININIT.INI` line queued first, then the UAS job's `FLAGS quiet nocopy`, `DIF_INSTALLDEVICE` 0; "updated; the new driver file replaces the running one at the next start" for the controller, root hub and disk, exit 3. After the restart, before any plug, `xhciuas.sys` and `xhci98.sys` were 3.4.1.0; the drive plugged mounted as E: with no wizard and no prompt, read and written. PASS |

Evidence: `E:\xhci98-vm\p46\w46-uas\legs\` (`xp`, `w7x86`, `w10u`,
`w10f`, `w98`: `serial-*.log` with the installer's console and log and
`R.BAT`'s readings, `xpans-*.out` and its shots, `*.png`, `pids.txt`).

### Seen, not changed

- 98 SE under the harness: a finished `COMMAND.COM` window kept COM1, so
  the next batch's first write failed ("Write fault error writing device
  COM1"); closing the window and Retry went on. The harness's, not the
  package's.
- The 98 SE test kit carries no `NUSB33\`, so `/storage` was refused
  ("this download does not have the storage drivers") and the run without
  it took the storage drivers' earlier staging off `DevicePath`
  (`USBNTMAP.INF` kept, a device using it); a release download carries the
  folder.

### Left

Windows 2000: tried and stopped (the q35 base's Found New Hardware
wizard held the published `3.4.0.0` install, exit 4, nothing changed,
and the harness's power-off batch needs `shutdown.exe`, which 2000 does
not have; `legs\w2k`). Vista and XP x64 not run (the coordinator: XP x86
and 7 cover NT 5.x and NT 6). A refused or cancelled signature question
on an absent drive and a Cancel during the pass (read only in review);
ME.

## 46.25 The ASM4242 handover through ECAM on 2000, 32-bit XP and XP x64 (2026-11-04 and 2026-11-05, w46-ecam)

Record 32 section 12.8.15.18, with the owner's answers to Q22 to Q25
(2026-11-04), reviewed and revised. `5facdcb6` is the route: ECAM on those
three systems alone, only with `XhciCmAsm4242Reads` 1, the base from
`MCFG`, one page mapped inside a hold of the members and checked against
the bus interface before any mailbox write, writes at `420h` and `430h` alone,
`113h` to `115h`, status bit 25 and XHCITR's line. `e06cc384` merges
`3.4.1.0` at `62b07a0f`. `157b493a` is XP x64's root table by the scan,
after the first XP x64 leg below. Host tests: `test_cm_oscp` 491 checks,
`test_cm_status` 138, the whole suite passed. The gated x86 `all` and
amd64 `all` builds of `157b493a` passed (`BUILD EXIT x86 0`,
`BUILD EXIT amd64 0`). The qemu flavour's `xhcicm.sys`: x86 SHA-256
`1e72d9ac...3f7baa`, amd64 `18087097...70ad68a`. The release flavour's:
x86 `b3521779...d3cbf3af`, amd64 `f6041f04...67eeee4b71b0`.

The legs used the emulator and tables of "46.12 on NT: the board once a
start, the mailbox's reach" above: q35 with its `MCFG` (base `B0000000h`,
buses 0 to `FFh`), the ASM4242 model, build 1 `--osi` tables,
`acpi-sb-osc=off`, no `-smbios` profile, so the board is unknown and the
read path decides. The overlays were copies of that section's own, with
the kits, filters and `XhciCmAsm4242Reads` 1 already in place, and the
qemu `xhcicm.sys` put in place. Evidence: `E:\xhci98-vm\p46\w46-ecam\`
(`xp\`, `w2k\`, `xp64\`, `w7x64\`, `pids.txt`).

### XP x64: class 76 gives `MCFG` and no root table

| Boot | Image | What the log says | Reading |
|---|---|---|---|
| f | `e06cc384` amd64 qemu (`57a6ab34...adce9ecf0`) | `cm.osc.ecam.system=502h`; class 76 `MCFG` read; `cm.osc.ecam.base=B0000000h`, `cm.osc.ecam.end=FFh`; `cm.osc.ecam.id=24211B21h`, `cm.osc.alias.100=00020001h`, `cm.osc.probe.mb=0`, `cm.osc.ecam.maps=0`, `cm.osc.probe=0`; `cm.osc.read.route=2`; class 76 `XSDT` and `RSDT` both `C0000225h`; `cm.osc.read=24h`; `cm.osc.refused=110h` | the ECAM page found and checked, and the probe passed through it. Then the read path's root table was absent. A finding: fixed in `157b493a` (12.8.15.18, "XP x64's root table") |
| h, cold | `157b493a` amd64 qemu | the same up to the probe; `cm.osc.read.rootscan=0`; class 76 `MCFG`; the three SSDTs, `cm.osc.tier=2`, `cm.osc.read.refused.pages=0`; `cm.osc.members=6`; `cm.osc.ecam.id=24211B21h`; `cm.osc.mb.before=0`; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `cm.osc.ecam.maps=0`; `u4.host.id=174C2428h`; `cm.osc.guard.clear=100h` | sent through ECAM, the router answering. QEMU: "command 00000102, reply 00000100", then "00000180, reply 000f0100". `info pci`: the upstream port's memory range `[0xfa800000, 0xfe7fffff]`, intact (`infopci-h.txt`). PASS |
| i (resumed after hibernation, `powercfg /hibernate on`, `SetSuspendState`) | the same | the base found afresh (class 76 `MCFG`, `B0000000h`); the read path afresh, tier 2; `cm.osc.resume=1`; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `cm.osc.ecam.maps=0`; `cm.osc.guard.clear=100h` | the S4 resend through ECAM. PASS |

### Windows 2000 and 32-bit XP: D and ECAM

| System, boot | What the log says | Reading |
|---|---|---|
| 2000, h, cold | `cm.osc.nt.build=0`; `cm.osc.ecam.system=0` (no `RtlGetVersion`); `cm.osc.wmi.none`; `cm.osc.ecam.base=B0000000h`; `cm.osc.ecam.id=24211B21h`, `cm.osc.alias.100=00020001h`; `cm.osc.probe=0`; `cm.osc.read.route=3`, `cm.osc.read.scan=0`, tier 2, `cm.osc.read.refused.pages=0`; `cm.osc.members=6`; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `cm.osc.ecam.maps=0`; `u4.host.id=174C2428h`; `cm.osc.guard.clear=100h` | sent through ECAM. `info pci`: the port's memory range `[0xfa800000, 0xfe7fffff]`, intact (`w2k\infopci-h.txt`; the aliased write of the earlier leg had closed it). PASS |
| XP, c, cold | `cm.osc.nt.build=A28h`; `cm.osc.ecam.system=501h`; the same lines as 2000's from the base on; `cm.osc.handover=100h`; `cm.osc.query=000F0100h`; `u4.host.id=174C2428h` | PASS; the port's window intact (`xp\infopci-c.txt`) |

### The faults, on 32-bit XP

| Boot | Set before it | What the log says | Reading |
|---|---|---|---|
| d | `XhciCmQemuEcamFault` 1 (the base 1 MB too high) | `cm.osc.qemu.ecam=1`; `cm.osc.ecam.base=B0100000h`; `cm.osc.ecam.id=24231B21h` (a downstream port's page); `cm.osc.ecam.check=1`; `cm.osc.ecam.maps=0`; `cm.osc.probe=115h`; `cm.osc.refused=115h` | the page not the port: refused before any mailbox write (the check's own reads only, among them the alias test's `100h` and `420h` of that page), no command in QEMU's output. PASS |
| e | `XhciCmQemuEcamFault` 2 (MCFG taken as absent) | `cm.osc.qemu.ecam=2`; `cm.osc.ecam=5`; `cm.osc.refused=114h` | no page: nothing mapped or written. PASS |
| f | the fault 0, `XhciCmAsm4242Reads` 0 | `cm.osc.ecam.system=501h`, `cm.osc.unknown=0`, no ECAM base or mapping logged | an unknown board at reads 0: nothing read, nothing mapped. PASS |
| g | reads 1; `x-mb-no-reply=on` (the fault build of "46.12 on NT: the board once a start", `qemu-bin-18573d0`) | the read and the probe as in c; `cm.osc.mb.before=0`; `cm.osc.timeout=2`; `cm.osc.timeout.reply=0`; `cm.osc.handover=FFFFFFFDh`; `cm.osc.ecam.maps=0`; QEMU: "command 00000102 taken, no reply (1)" | the timeout logged, the page unmapped, never retried. PASS |
| h | none | `cm.osc.guard=101h`; `cm.osc.guard.deleted=3` | the marker that g left: this boot skipped (12.8.15.13) |
| i | none | as c: `cm.osc.handover=100h`, `cm.osc.query=000F0100h` | the boot after the skip sends. PASS |

### The regression: 7 x64

The `w7x64` overlay of the C leg, `e06cc384`'s amd64 qemu `xhcicm.sys`
(`57a6ab34...adce9ecf0`), signed in the guest with a throwaway certificate
(`9048c779...afe9630c`), `XhciCmAsm4242Reads` 1. Boot f, cold:
`cm.osc.probe=0` through the bus interface (`cm.osc.alias.100=00020001h`,
`cm.osc.alias.20=FE70FA80h`); `cm.osc.read.route=2`; tier 2;
`cm.osc.handover=100h`; `cm.osc.query=000F0100h`; no `cm.osc.ecam` line
in the boot; the port's window intact. ECAM is not used there. PASS.
`157b493a` changes the read path on the ECAM systems alone.

### Not run

- 113h, the known board at reads 0: the emulator's 2000, XP and XP x64
  guests have no SMBIOS block through WMI (`cm.osc.wmi.open=C0000295h`,
  `cm.osc.wmi.none`), so E finds every board unknown there. Held by host
  vectors (`CmOscpBoardNextEx`, `test_cm_status`'s bit 25); XHCITR's
  line not read on a guest.
- S4 on 32-bit XP: the guest's `powercfg /q` reports hibernation "Not
  Supported".
- A deferral on these systems: no start deferred in these legs (the key
  flushable, every member started), so the try after a deferral was not
  seen. The order is held by host vectors.
- A surprise removal while the page is mapped: no model property.
- The bench.

## 46.9 the shared-line claims (2026-11-05, w46-legs2)

"46.9 the owed legs" left the shared-line claims inconclusive: whether
completions taken on another device's calls of `HwInterrupt` (claims)
hold the watchdog's latch off when the drive's own interrupt is dead.

**The test image.** Branch `w46-legs2-t` (`ab065258`, test only, never
merged): `STOR98_TEST_WATCHLOG`, built only with `STOR98_DBGPORT=1`, logs
at every armed timer callback and at every change of armed the debug-port
stages `159h` (the run after and before, found, armed, latched, the
start's misses) and `15Ah` (claims and `HwInterrupt` calls since the
previous logged callback). Its 9x image `580fb53d...` on 3.4.1.0's Stor98
source (`391aad0d`).

**The vehicle.** "46.9 the owed legs"' shared-line vehicle: a copy of
w46-stor2's `s98\disk.qcow2` (98 SE, the gated build of `f303b42c`
installed), the image copied over `STOR98.MPD`, the ASM4242 layout with
the ORICO model's drive tunnelled at power-on, QEMU's `edu` at `00:07.0`
with no driver, both on IRQ 11; each leg a cold start, one XHCIPERF run
with `/verify` (32 MiB, sequential 1 MiB and random 4 KiB, read and
write, two passes of 5 s) and `edu.py` (w46-stor2's, a `mark=` added
here: the debug console log's size at the suppression, so the stages
are placed against it) cycling `edu`'s interrupt, the drive's Interrupt
Disable bit set at cycle 50 (about 5 s in) through the q35 ECAM window
and cleared at the end. The stages' TSC is about 3 GHz (the clock's
`D3h` calibration; the counted callbacks are about 0.31 G apart at the
100 ms watchdog tick).

| Leg | `edu` | Read |
|---|---|---|
| c | raised and acknowledged at once, every ~100 ms | `edu`'s interrupts never reached Stor98's ISR (after the suppression no `HwInterrupt` call at all at the counted callbacks); the latch at 3 misses 1.3 s after the suppression (`157h` `02170003`); random 4 KiB 431 / 395 IOPS |
| d | held 1 ms, every ~100 ms (15 to 110 ISR calls each, no storm) | before the suppression: 169 counted callbacks, 5 to more than 20 claims each, one with a find (and 10 claims); after it: 182 counted callbacks, a find at 137, and all but 5 of those with a claim since the previous callback (130 one, 1 two, 1 nineteen), so the run stayed at 0; the latch only about 19 s (56.9 G) after the suppression, at a phase drift (`157h` `025F0005`, 5 misses); random 4 KiB writes 89 IOPS (11 ms each) |

Every run's data compared and no request failed; no stage `50h` or `87h`
(no controller reset). So completions taken on a shared call do hold the
latch off: with another device on the line interrupting about ten times
a second, the run is set back at nearly every callback, and I/O runs at
that device's rate for as long as that lasts (here 19 s; a steadier
interrupter could hold it off for the start).

**The fix** (`ed04e0ba`, Stor98 PLAN "Missed-interrupt watchdog" revised
first, reviewed and revised): a timer find at a callback with at most 2
completions claimed since the previous one (`IntClaimCqes`, counted from
the entries the claimed calls consumed) is a miss too, a *find*, and 8
finds within 2 s (a rolling window of the last 8) latch beside the run's
3. The finds are forgotten at a callback that is not armed, when the
newest is more than 2 s old, and after a gap the engine's clock did not
measure (a long-gap fold, a reading behind its base, an idle gap with no
TSC); nothing is forgotten when the timer stops idle (at queue depth 1
every command can end that way). Host vectors (`poll_ut.c`, NT and 9x):
the shared dead line's finds, the claimed-completions bound, the rolling
window, the forgetting, a working line under load and at light load with
races, depth 1 with idle gaps; all Stor98 unit tests pass.

The gated x86 (release and qemu) and amd64 release builds of `ed04e0ba`
passed (`E:\xhci98-scratch\w46-legs2\gated-fix.log`; `stor98.mpd`
`2ff431ed...`, x86 `stor98.sys` `bab621ca...`, amd64 `stor98.sys`
`f2647fe1...`).

**The legs again**, the fix's debug-port image (`9d656fad...`, `ed04e0ba`)
in place of the test image, the same vehicle:

| Leg | `edu`, suppression | Mode, misses | Seq 1M write / read; rnd 4K write / read |
|---|---|---|---|
| f | held 1 ms every ~100 ms, INTx suppressed at cycle 50 | `polling after missed interrupts (8 missed)`; the latch (`157h` `02DD0008`) 0.9 s (2.6 G) after the suppression | 87.1 / 198.5 MB/s; 362 / 389 IOPS |
| g | the same, no suppression (the control) | `interrupts`; no latch, no storm (no `157h`, no `DBh`) | 82.9 / 221.6 MB/s; 943 / 1039 IOPS |
| h | raised and acknowledged at once, INTx suppressed at cycle 50 (the dead line alone) | `polling after missed interrupts (6 missed)`; the latch (`157h` `02550006`) 0.7 s (2.0 G) after the suppression | 85.1 / 204.5 MB/s; 423 / 443 IOPS |

Every run's data compared, no request failed, no controller reset.
So with the fix the shared dead line latches in under a second, at the
eighth find, where it had taken 19 s, and its random 4 KiB rates are the
polling mode's (about 4 times leg d's writes); the dead line alone
latches as before, in under a second; and the working line shared with
the same interrupter stays interrupt-driven. ME, 2000, XP and 7 x86 with
the fix: below.

Evidence: `E:\xhci98-vm\p46\w46-legs2\` (`a1\` `debugcon-c.log` to
`debugcon-h.log`, `serial-c.log` to `serial-h.log`, `edu-c1.out`, `edu-c1.log`, `edu-d.out` to
`edu-h.out` and their `.log`, `wl-*.txt`; the scripts `leg.sh`, `edu.py`,
`wl.py`, `wdsum.py`, `start9.sh`; the kit `xw\`; `pids.txt`).

### The fix on ME, 2000, XP and 7 x86 (2026-11-05)

The interrupt suppressed after the start and the interrupt-driven case,
as "46.9" and "46.9 the owed legs" above, on the fix's gated release
images (`stor98.mpd` `2ff431ed...`, x86 `stor98.sys` `bab621ca...`),
each copied over the one installed on a copy of the earlier legs'
overlays (ME: w46-stor's `meb`; 2000: w46-stor2's `n2k`; XP: w46-stor's
`ntxp`; 7 x86: w46-stor2's `n7`), `StorIntWatchdog` / `storintwatchdog=`
1 written again (the overlays' last legs had left it 0), a cold start,
then one XHCIPERF run with `/verify` each: with interrupts, the drive's
INTx suppressed (twice), and with it enabled again (`intx.sh`). No
shared line here: the run's rule, unchanged, is what these read.

| System | Interrupts | INTx suppressed, first run | Second run | INTx enabled again |
|---|---|---|---|---|
| ME | `interrupts`; 87.6 / 251.8 MB/s; 1044 / 1933 IOPS | `polling after missed interrupts (3 missed)`; 67.0 / 123.3; 205 / 445 | the same; 59.4 / 113.9; 196 / 376 | the same, 3 missed: the latch holds; 64.6 / 114.6; 191 / 383 |
| 2000 | `interrupts`; 137.2 / 861.5 MB/s; 5109 / 6243 IOPS | the same mode as ME's, 3 missed; 119.7 / 799.1; 5888 / 8693 | the same; 118.2 / 708.9; 5583 / 7068 | the same, 3 missed; 135.7 / 842.3; 5470 / 7736 |
| XP | `interrupts`; 62.1 / 207.5 MB/s; 389 / 407 IOPS | 3 missed; 59.0 / 176.6; 594 / 675 | the same; 56.8 / 147.8; 541 / 667 | the same, 3 missed; 57.8 / 192.1; 584 / 526 |
| 7 x86 | `interrupts`; 122.1 / 690.8 MB/s; 3181 / 3865 IOPS | 3 missed; 88.8 / 486.9; 3227 / 4231 | the same; 99.9 / 506.8; 3426 / 4923 | the same, 3 missed; 101.7 / 521.1; 3658 / 4294 |

(Sequential 1 MiB write / read, then random 4 KiB write / read.) Every
run's data compared (`verified`) and no request failed ("failed: 0" on
every page); the command word read back with bit 10 set while
suppressed. So the revised watchdog latches at three misses on each
system with the line dead and leaves the interrupt-driven case as it
was.

Evidence: `E:\xhci98-vm\p46\w46-legs2\` `meb\` (`serial-n2.log`,
`me.out`), `n2k\` (`serial-b.log`), `ntxp\` (`serial-b.log`,
`wdnt-b.out`), `n7\` (`serial-c.log`, `wdnt-c.out`; a first cold start, `serial-b.log`, ran the same sequence
unelevated, where XHCIPERF reads no Stor98 page: "Reading its counters
needs administrator rights", its data compared too), the scripts `me.sh`, `wdnt.sh`,
`ntrun.sh`, `intx.sh`, `startn.sh`, `startx.sh`, `start7.sh`; the kits
`xw\` (`FIX.SYS`, `FIXN.BAT`) and `xme\`; `pids.txt`.

## 46.11 and 46.12 a tunnelled drive on 98 SE after the step, and the owner's board (2026-11-05, w46-legs2)

The leg "46.11 Steering enabled" left owed on 98 SE: a drive behind a
tunnel port after the step, whose fresh images had not had the
enclosure's first-plug wizards completed. The owner dropped the 2000 and
32-bit XP Thunderbolt-only legs of 46.11 and 46.12 on 2026-11-05; 98 SE
is the one system left for both.

**The build.** The gated x86 build of `391aad0d` (release and qemu: BUILD
+ GATES PASSED, `E:\xhci98-scratch\w46-legs2\gated-base.log`); the
release kit by `make-package.ps1` and `ConvertTo-FamilyLayout`, this
tree's `XHCIINST.EXE` (`0fe8ad45...`) and `XHCITR.EXE`; the release
`stor98.mpd` `2a8b98d1...`; the `qemu` `xhcicm.sys` `eed0f3c9...` and
`XHCIPFW.VXD` `da2eb2d2...` copied over the installed ones after the
restart and checked (`fc /b`).

**The vehicle.** 98 SE without ACPI (steering disabled, as the board's):
a copy of w46-cm2's `h1\disk.qcow2` (`7eb2cac0...`, an overlay of its
`lg\`), the image on which the enclosure's first plug had been made in
"46.12" (the bridges' and Stor98's wizards answered, "Orico (E:)"), and
its NVMe disk; a fresh overlay of each; this kit installed over it
(`XHCIINST /dir 98-me /yes /nopause /thunderbolt /stor98`, exit 3), the
restart, the `qemu` files copied in (the restart had put the install's
own pending copies over the first ones), then a cold start. q35, TCG, one
vCPU, 512 MB, the emulator of `usb4-emu` `57dd977` (`e68ad1bc...`) with
w46-flt's settings ("46.12 The 9x fault legs": `pm-cap=on`,
`fw-handover=on`, `fw-data=0x7FF00000`, `fw-handover-gate=on`, build 1's
three SSDTs), the enclosure absent at power-on. `XhciPfwNhiLine` absent
(its default, 1).

| Step | Seen |
|---|---|
| the start | XHCIPFW's log: the NHI's function (`64:00.0`) and its port (`02:03.0`) "line outcome/before/after" `1`, `0Bh`, `FFh`, both; `cm.irq.resource=0` (no IRQ); `cm.osc.tier=2`, `cm.osc.mb.before=0`, `cm.osc.handover=00000100`, `cm.osc.query=000F0100`; `u4.host.id=174C2428` |
| the ORICO model hot-plugged at the desktop (QMP `device_add`) | no hardware wizard; the bridge numbering wrote the tunnel port's line to the enclosure's functions (`cm.brnum.9x.irq=00010B01`), and the no-IRQ path refused them as before (`cm.reenum.9x.noirq.refused=03000203h`, `04000203h`, `05000203h`: they have a line) |
| `dir E:\` | volume "ORICO", its two files |
| `XHCICM.SYS` copied to E: and compared (`fc /b`) | no differences |
| E:'s 1 MiB `TESTDATA.BIN` copied to C: and back to E: as a second file, the two compared | no differences |
| XHCITR's report | "PCIe tunnel: up - QEMU NVMe Ctrl (E:)", "Unit behind a Thunderbolt port", "Stor98 Storage Controller for NVMe SSDs", the drive's IRQ 11 (level, shared) |

So a drive behind a tunnel port, hot-plugged after the step, is listed,
runs on Stor98 and its volume reads and writes, on 98 SE without ACPI.
The earlier failure ("46.11 Steering enabled") was the fresh images'
first plug, as recorded there: on this image, whose first plug had been
made, no wizard was asked.

Evidence: `E:\xhci98-vm\p46\w46-legs2\hb\` (`debugcon-c.log`,
`serial-c.log`: the listing, the compares, the XHCITR report and the
`Enum\PCI` export, `qemu-c.out`, `launch-c.cmd`, `c-cmd.png`,
`infopci-c-before.txt`; `serial-a.log`: the install), `pids.txt`.

**The owner's board** ("bench 2", 2026-11-04, 98 SE): the handover sent
reply `100h`, query `000F0100h`, the router answering
(`174C2428`), the tunnelled NVMe drive on Stor98 as D:, 1004/1060 MB/s
sequential, no fault (whether the LAN was on
or off is not in the summary, and record 32 section 12.8.9 asks for
both). Evidence: the owner's, kept on the maintainer's disk.

## 46.final, the pre-RC matrix (2026-11-05, w46-rcmx)

The install matrix of the `3.4.1.0` tip before the release candidate, so
that the cut tonight needs only a short re-run: a fresh install and an
update over the published `3.4.0.0` on every target system with a q35
base (8 and 8.1 in both architectures each), the window
installer once on 98 SE and once on 10 x64, 46.24's leg on 2000 (a UAS
disk known at `3.4.0.0`, the update with it unplugged) and 46.24's "no
`xhciuas` on 8 to 10" on every 8, 8.1 and 10 fresh install. Development
host A, QEMU 11.1.2 (the q35 bases' pinned emulator,
`asm4242-20261025-2230`, TCG), every leg on a fresh overlay of its q35
base (`E:\xhci98-vm\q35-bases\`), no USB4 host, `-smp 1` on 98 SE, ME and
2000, 2 on the others.

**The commit.** `c28bcf62` (3.4.1.0 with the release date in
`src\xhci_version.h`). The first build, of `3db97c74`, stopped in its
self-tests: `d5852858` had moved every INF's `DriverVer` to `11/05/2026`
and left `XHCI_DRIVERVER_DATE` at `11/01/2026`, so every INF gate refused
the INFs ("[BOTH-VERSION] xhci_version.h says the release date is
11/01/2026 but this INF's DriverVer says 11/05/2026"; the packager's
self-tests 20 of 1817 checks). Reported; `c28bcf62` is the fix.

**The builds.** Gated (slot protocol of `E:\xhci98-locks\`), after
`src\pfw`, `xhciinst\di`, `xhciinst`, `xhcitr`, `xhcimark`, `xhciperf`
(each exit 0): `build-driver.cmd all` (x86) "BUILD + GATES PASSED (release
qemu)", `BUILD EXIT x86 0`; `build-driver.cmd release -amd64` "BUILD +
GATES PASSED (release)", `BUILD EXIT amd64 0` (logs
`E:\xhci98-scratch\p46-w46-rcmx\gated.log`, `gated2.log`; the first amd64
run found the lock of the stopped `3db97c74` build and was run again).
`make-package.ps1 -Flavor release` exit 0 for x86 and amd64.

**The downloads.** A test cut of the x86 half, `make-release.ps1 -Arch x86
-TestLabel "pre-RC w46-rcmx"` with the two driver packs (exit 0; its
readme opens "TEST 3.4.1.0, pre-RC w46-rcmx."): `xhci98-3.4.1.0.zip`
`9d55cc1e...`, `-free.zip` `ad322480...`, `-nonfree.zip` `e56057ce...`
(`E:\xhci98-scratch\p46-w46-rcmx\up\`, `up-sha256.txt`). The x64 half
needs the owner's key, so the kit's `nt-x64\` is `out\pkg-release-amd64`
with its catalogs made unsigned (`package-common.ps1`'s
`New-PackageCatalogFile`) and laid out by `ConvertTo-FamilyLayout -Arch
amd64`; in each x64 guest a throwaway certificate (MakeCert `-b 01/01/2026
-e 01/01/2030`, Root and TrustedPublisher) signs every `.sys` and `.cat`
of it and stands in as its `xhci98-test.cer`, and test signing is on. The
kit `K341` is the base zip unpacked plus that `nt-x64\` (92 files,
`kits\k341-sums.txt`): `xhci98.sys` x86 (98-me = nt-x86) `1dabbe2b...`,
amd64 `c721af12...` (before the guest's signature); `xhciuas.sys` x86
`9023cbbe...`, amd64 `c108ad9c...`; `xhcicm.sys` x86 `e7827985...`, amd64
`4889e7b0...`; `SETUP.EXE` `55585bb8...`, `XHCIINST.EXE` `e10c3a2f...`,
`XHCIIN64.EXE` `06ecd786...`, `XHCITR.EXE` `96a516c0...`, `XHCITR64.EXE`
`aa5e1ed8...`. Each binary of the zip is the gated build's (`xhci98.sys`
and `xhciuas.sys` compared with `out\pkg-release-x86`). The published
`3.4.0.0` is the cut's own `xhci98-3.4.0.0.zip` (`0b96a2c8...`, as its
`SHA256SUMS.txt`, `E:\xhci98-scratch\cut340\release-3.4.0.0\`), its
`xhci98-test.cer` trusted on the x64 systems.

**The legs.** A guest-side leg of one step per start, each ending in a
power off, so every restart is a cold start (`scripts\mkx.py` in the
evidence folder; the poller and its start at logon as the release
matrix's, `scripts\vm-matrix\leg\XHPOLL.CMD`/`.BAT`): the setup (the kits
copied to `C:\P340` and `C:\K341` as a user has them, the x64 signing),
then the install (`INSTALL.BAT /console /yes /nopause /log ...`, `/usb` on
8, 8.1 and 10; the update legs first install `3.4.0.0` the same way,
restart, check, then install the tip), then the check after a cold start.
The check: each driver file compared with both kits (`fc /b`),
`INSTALL.BAT /console /list`, every device's status and service
(`DC2K.EXE`, SetupAPI and Config Manager, on 2000 to 10;
`HKEY_DYN_DATA\Config Manager\Enum` on 98 SE and ME),
`Win32_PnPSignedDriver` on XP and later; then a USB keyboard (`usb-kbd`,
port 1), a USB tablet (`usb-tablet`, port 2) and a Bulk-Only stick
(`usb-storage`, port 3, a FAT disk holding `TESTDATA.BIN`) plugged; the
stick's file read and written back (`fc /b` both ways); XHCITR's process
(started at logon), `XHCITR /probe` (2000 to 10), `/report <file>` and
`/snapshot`
(exit codes, the files' sizes); then the keyboard and tablet: both bound
to the display (`display=vga0`) so that only the host's `input-send-event`
to that display reaches them and the harness's own keys and pointer stay
on PS/2, and a full-screen window in the guest (`INPT.EXE`) records the
click at 300,200 and the typed `usbkbd7` and Enter. The installer's log is
read for its line stamps (every line starting `[hh:mm:ss.mmm] `). Windows'
own questions were answered by the harness: XP and XP x64's Logo-testing
question "Continue Anyway" (Alt+C, in the guest) and a Found New Hardware
wizard there cancelled; 2000's wizard walked (Enter) and "Digital
Signature Not Found" Yes; Vista and 7 x86's and 8 and 10 x86's "Install
this driver software anyway" clicked; 98 SE and ME's Add New Hardware
wizard for the first USB HID and disk walked (Enter).

**The readings.** "PASS" in the table below is every one of these, read in
that leg's final check after a cold start: the install's exit 3 ("Done.
Please restart Windows now."); `xhci98.sys` the kit's 3.4.1.0 file (98 SE
and ME `xhciuas.sys` too; on 2000 to 7 `xhciuas.sys` is staged, not
copied, with no UAS disk: absent); every device of the USB tree and every
xHCI98 device running (2000 to 10: the controller and root hub on
`xhci98`, the stick on `USBSTOR` and `disk`, the keyboard on `HidUsb` and
`kbdhid`, the tablet on `HidUsb` and `mouhid`, 8 devices; 98 SE and ME: 9
devnodes, every one problem 0); `INPT.EXE` read `keys=[usbkbd7] enter=1
click=1 at=300,200` with the USB keyboard and mouse devices present (so
the keys came through the USB keyboard, not PS/2); the stick's file read
and written back, both compares passing; XHCITR's process running from the
logon (its `Run` value `"C:\XHCI98\XHCITR.EXE" /tray`, `XHCITR64.EXE` on
x64), `/report` (about 53 KB) and `/snapshot` (four files) exit 0, and on
2000 to 10 `/probe` exit 0 (the 98 SE and ME check does not run `/probe`);
every line of the 3.4.1.0 install's log stamped (the line counts in the
table, none unstamped). On an update leg also: the `3.4.0.0` install exit
3 and its own check (the files 3.4.0.0's, the same readings), then after
the update and a cold start the files 3.4.1.0's and
`Win32_PnPSignedDriver` `DriverVersion` 3.4.1.0 for the controller and
root hub (XP and later; on 2000 `/list` "driver xhci98 3.4.1.0"; on 98 SE
and ME the files). On 8, 8.1 and 10 also 46.24's reading: on a fresh
install no `xhciuas.inf` in `DriverStore\FileRepository`, none among the
`oem*.inf`, no `xhciuas.sys`; on an update no 3.4.1.0 `xhciuas.inf`
staged, `3.4.0.0`'s staging left as it was (below), no `xhciuas.sys`.

| System | Fresh install | Update over 3.4.0.0 | Also |
|---|---|---|---|
| 98 SE | PASS (`r-w98se-F`, log 898 lines) | PASS (`r-w98se-U`, 1072) | the window installer PASS (`r-w98se-G`, below) |
| ME | PASS (`r-me-F`, 633) | PASS (`r-me-U2`, 787); `r-me-U` voided (below) | |
| 2000 SP4 | PASS (`r-w2k-F`, 153) | PASS (`r-w2k-U`, 150; `/list` 3.4.1.0) | 46.24's UAS leg PASS (`r-w2k-A`, below) |
| XP SP3 | PASS (`r-xp-F2`, 159); `r-xp-F` voided (below) | PASS (`r-xp-U`, 150) | |
| XP x64 SP2 | PASS (`r-xpx64-F`, 141) | PASS (`r-xpx64-U`, 132) | |
| Vista SP2 x86 | PASS (`r-vx86-F`, 154) | PASS (`r-vx86-U`, 153) | |
| Vista SP2 x64 | PASS (`r-vx64-F`, 138) | PASS (`r-vx64-U`, 135) | |
| 7 SP1 x86 | PASS (`r-w7x86-F`, 155) | PASS (`r-w7x86-U2`, 158); `r-w7x86-U` voided (below) | |
| 7 SP1 x64 | PASS (`r-w7x64-F`, 139) | PASS (`r-w7x64-U`, 140) | |
| 8 x86 | PASS (`r-w8x86-F`, 172; no `xhciuas`) | PASS (`r-w8x86-U`, 174), but its check on `3.4.0.0` before the update incomplete (below) | |
| 8 x64 | PASS (`r-w8x64-F`, 150; no `xhciuas`) | PASS (`r-w8x64-U`, 150) | |
| 8.1 x86 | PASS (`r-w81x86-F`, 172; no `xhciuas`) | PASS (`r-w81x86-U`, 174) | |
| 8.1 x64 | PASS (`r-w81x64-F`, 150; no `xhciuas`) | PASS (`r-w81x64-U`, 150) | |
| 10 22H2 x86 | PASS (`r-w10x86-F`, 167; no `xhciuas`) | PASS (`r-w10x86-U`, 169) | |
| 10 22H2 x64 | PASS (`r-w10x64-F`, 151; no `xhciuas`) | PASS (`r-w10x64-U2`, 151); `r-w10x64-U` voided (below) | the window installer PASS (`r-w10x64-G2`, below) |

On the 8, 8.1 and 10 update legs the store keeps `3.4.0.0`'s `xhciuas.inf`
alone (one `xhciuas.inf_*` folder in `FileRepository`; `pnputil -e` on 8
x86: `oem2.inf` "11/01/2026 3.4.0.0" beside `xhci98.inf` 3.4.0.0 and
3.4.1.0; the same on 8 x64, 8.1 x86 and x64, 10 x86 and x64): the update
neither installs nor stages the 3.4.1.0 one and leaves the earlier staging
as it is (46.24's rule); `xhciuas.sys` was never copied there. No system
with a q35 base was left out; the UEFI bases (`w81x64-uefi`,
`w10x64-uefi`) were not used.

**The window installer, 98 SE** (`r-w98se-G`, `shots\g01` to `g08`):
Welcome "xHCI98 USB 3 drivers 3.4.1.0", the controller rows "PCI Universal
Serial Bus ... Install"; Components the defaults (the USB 3 driver and the
UAS disk driver, "Storage drivers for Win 98 SE (from NUSB 3.3)", no
Thunderbolt / USB4, the USB Tree at login); Summary; Install; Progress;
Finish "Done. Please restart Windows now." (Restart later chosen: the
harness powers off). Then the check: PASS as above, the log
`C:\XHCIINST.LOG` 897 lines, all stamped.

**The window installer, 10 x64** (`r-w10x64-G2`, `shots\g01` to `g10`):
Welcome "xHCI98 Thunderbolt / USB4 connection manager 3.4.1.0", "No
supported device found." / "drivers stored for later"; Components: "xHCI98
USB 3 driver (xhci98.sys)" unticked, the UAS disk driver box unticked and
greyed (46.24), the Thunderbolt box unticked; the USB 3 driver ticked; the
"On Windows 8, 8.1 and 10, the xHCI98 USB drivers replace Windows' own USB
3 driver ..." box, OK; Summary "Standard USB 3.0 eXtensible Host
Controller - 1.0 (Microsoft): Install", "xhciuas.sys (UAS disk driver):
not installed; an installed one is kept"; Install; Progress "Staged
xhci98.inf as oem2.inf.", "Installing Standard USB 3.0 eXtensible Host
Controller ..."; Finish "Done. Please restart Windows now." (Restart
later). Then the check: PASS as above, with no `xhciuas` staged, the log
159 lines, all stamped.

**46.24 on 2000** (`r-w2k-A`): the published `3.4.0.0` (exit 3); a UAS
disk (`usb-uas`, serial `W46RCUAS1`, port 4, SuperSpeed) plugged, its
Found New Hardware wizard walked, the devnode on `xhciuas` running from
`xhciuas.sys` 3.4.0.0, the disk read and written; unplugged; then the
tip's console install with no UAS disk: the log "UAS disks not plugged in
now, updated too:" / "USB\VID_46F4&PID_0003\W46RCUAS1 3.4.0.0 (not plugged
in)", "Staged xhciuas.inf as oem3.inf.", "Updating xHCI98 USB Attached
SCSI Storage (not plugged in) ...", `DIF_INSTALLDEVICE` with
`DI_DONOTCALLCONFIGMG`, "now service xhciuas, InfPath oem3.inf, InfSection
Uas.Dev, DriverVersion 3.4.1.0"; exit 3. After a cold start, before any
plug, `xhciuas.sys` was 3.4.1.0's; the disk plugged again with the stick,
keyboard and tablet: no wizard and no question (six screens 15 s apart,
`shots\t4-uasplug-*`), the devnode's driver key `oem3.inf` `Uas.Dev`
`DriverVersion` 3.4.1.0, `xhciuas` running, both disks read and written;
the other readings PASS as above (9 devices, log 176 lines). The earlier
attempt's two obstacles were met by the harness: the base's Found New
Hardware wizard walked in the guest (Enter, and Yes on "Digital Signature
Not Found") with the installer run again on its exit 4 "still installing",
and the power off by WMI's `Win32Shutdown` (2000 has no `shutdown.exe`)
with QEMU stopped at the halt it ends in.

**Not product, and how each was settled.**

- XP SP3, the first fresh leg (`r-xp-F`): exit 2, "xhci98.inf could not be
  staged (error 0xE000022F)". `setupapi.log`, read off the overlay
  afterwards (`setupapi-d1.txt`): "#E369 An unsigned or incorrectly signed
  file ... xhci98.inf blocked (Policy=Warn, user said no)": the harness's
  own keys (the host's Shift+Tab and Space, and the guest's helper
  activating the dialog in between) answered STOP Installation, as
  w46-mx64 found on XP x64 ("46.final pre-cut matrix, x64"). The question
  is now answered in the guest alone, Alt+C (Continue Anyway) right after
  activating it; the rerun (`r-xp-F2`) and every XP and XP x64 leg after
  it passed.
- ME, the first update leg (`r-me-U`): after the published `3.4.0.0`'s
  console install (exit 3, no USB device plugged) the shut down did not
  start, and Start, Shut Down then froze on the dimmed desktop (the clock
  stopped, the processor in VMM at `C0013xxx`, `hang-regs.txt`); QEMU
  stopped after 10 minutes. The rerun (`r-me-U2`) of the same leg passed.
  Once more on ME (`r-me-F`, after its last step, every check passed) and
  once on 98 SE (`r-w98se-G`, after the check step, every check passed)
  the shut down stopped (ME at a black text screen, 98 SE at "Windows is
  shutting down") and QEMU was stopped; the next start of each was a cold
  start of the same overlay and the leg went on. Three of about 24 shut
  downs of 98 SE and ME in these legs; the 98 SE and ME hangs at shut down
  on q35 are older than this release (run-45, run-44), and none was chased
  here.
- 98 SE and ME: `RUNDLL32 SHExitWindowsEx` did not always end the session
  after an install; the harness then used Start, Shut Down (`leg.log`, "no
  power-off 150 s after POWEROFF").
- 7 x86, the first update leg (`r-w7x86-U`): the tip's install exit 2,
  "xhci98.inf could not be staged (error 0xE000023F)" (no Authenticode
  catalog: the publisher question answered "Don't install"). The harness
  answers that question by a click at the pointer, parked at the screen's
  centre where "Install this driver software anyway" lies; the update
  leg's earlier check had moved the pointer to 300,200 with the USB tablet
  (`r-w7x86-U\shots\t3-004.png`). The harness now puts the pointer back
  with the tablet before each click; the rerun (`r-w7x86-U2`) passed, and
  Vista x86's update leg, by then running, had its pointer put back by
  hand before its install and passed.
- 10 x64, the first update leg (`r-w10x64-U`): every reading passed except
  the keyboard and tablet one (`keys=[] click=0`): the slow guest had not
  yet shown `INPT.EXE`'s window when the host typed and clicked
  (`r-w10x64-U\shots\input-after.png`: the desktop). The harness now waits
  for the window; the rerun (`r-w10x64-U2`) passed.
- Three running legs (`r-vx86-U`, `r-w8x86-U`, `r-w10x64-U`) lost their
  host-side driver when the queue that had started them was stopped; their
  guests ran on and the driver was attached again (`leg.py --resume`,
  `leg.log` "RESUME"). Two `WANT plug-all` lines fell in the gap:
  `r-w10x64-U`'s last check (plugged by hand, `leg.log`) and `r-w8x86-U`'s
  check on `3.4.0.0` before the update: neither the stick nor the USB
  keyboard was plugged, so that check read no drive ("NOTSEEN") and its
  keyboard reading came through PS/2 (QEMU gives a display's keys to an
  unbound keyboard when no keyboard is bound to it); its install, the
  update and its final check after the update are complete. A second
  driver started on `r-w81x64-U` by the same mix-up was stopped before it
  did anything. `r-w10x64-G` was stopped at its first start (too many
  guests at once) and run again as `r-w10x64-G2`.
- 10 x64's window leg: the guest's wait for the installer's end matched
  Windows' own `OneDriveSetup.exe` (a substring match on `SETUP.EXE`); the
  two processes were ended in Task Manager by hand, after the installer's
  Finish (`leg.log`).
- 8 x86 (a debug leg on `3.4.0.0`, before the matrix): the harness's click
  for the publisher question opened the Start screen's Photos tile; the
  legs then show the desktop (Win+D) at each logon on 8 and 8.1.

**Seen, for the owner** (the window installer's texts, as shown):

- 98 SE, Summary: "Storage drivers for Win 98 SE: all seven files
  installed." and, on the next line, the component's own line "Not
  installed, USB disks will not work on Windows 98 SE without them."
  (`r-w98se-G\shots\g03.png`): read together they contradict each other.
- 10 x64, Welcome while it looks for devices: "Windows 8/8.1 should have
  its own USB 3 drivers but you still can install it if you tick them in
  the next page." on Windows 10 (`r-w10x64-G2\shots\g02.png`).
- 10 x64, Summary on a machine that never had xHCI98: "xhciuas.sys (UAS
  disk driver): not installed; an installed one is kept"
  (`r-w10x64-G2\shots\g07.png`): there is none installed.

**Not covered here.** The x64 binaries the owner signs tonight (these legs
sign with a throwaway certificate); the `-free` and `-nonfree` downloads
(the base download only); Thunderbolt / USB4, Stor98, the Device Manager
disable / enable / remove / rescan sequence, the uninstall, hubs, audio
and a UAS disk on systems other than 2000 (the earlier "46.final pre-cut
matrix" sections); sleep; real hardware.

Evidence: `E:\xhci98-vm\p46\w46-rcmx\`: `legs\r-<system>-<mode>\` (F
fresh, U update, G window installer, A 2000's UAS leg; each with
`leg.log`, `serial-t*.log` (the guest's readings and the installer's
console and log), `shots\`, `pids.txt`), the voided `r-xp-F`, `r-me-U` and
the harness's trial legs `dbg-*` (on `3.4.0.0` only); `scripts\` (`mkx.py`
the guest side, `leg.py` the host side, `verdict.py` the table's readings,
`an.py`); `kits\k341`, `kits\p340`; `xfer\`; `pids.txt`; `STATE.md`.
Builds and cut: `E:\xhci98-scratch\p46-w46-rcmx\` (`gated.log`,
`gated2.log`, `gated-af98bd86-FAILED.log`, `pkg-*.log`, `up\`,
`up-sha256.txt`).

## 46.final, the delta matrix of the tip (2026-11-05, w46-rcdelta)

A short re-run of "46.final, the pre-RC matrix" on the `3.4.1.0` tip
`3e7a1444`, for what changed since `c28bcf62` (the commit that matrix
ran, after the message-only history rewrite; the same tree): Stor98's
missed-interrupt watchdog on a shared line (46.9, `polltun.h`,
`lifecycle.c`), the 98 SE Summary's storage note
(`inst_eng_stor_summary_final`, `xhciwiz.c`), two installer texts
(`INST_PT_WELCOME_TOOLS` on 8 and later; the unticked UAS Summary line)
and comments. Development host A, QEMU 11.1.2 (`asm4242-20261025-2230`,
TCG), each leg on a fresh overlay of its q35 base, `-smp 1` on 98 SE, ME
and 2000, 2 on the others; at most five guests at once. The harness is
w46-rcmx's, copied (`scripts\`), and the PASS rules are that section's,
unchanged.

**The builds.** Gated (slot 1 of `E:\xhci98-locks\`), after `src\pfw`,
`xhciinst\di`, `xhciinst`, `xhcitr`, `xhcimark`, `xhciperf` (each exit
0): `build-driver.cmd release -amd64` "BUILD + GATES PASSED (release)",
`BUILD EXIT amd64 0`; `build-driver.cmd all` (x86) "BUILD + GATES PASSED
(release qemu)", `BUILD EXIT x86 0`; `make-package.ps1 -Flavor release`
exit 0 for x86 and amd64 (`E:\xhci98-scratch\p46-w46-rcdelta\gated.log`).

**The downloads.** `make-release.ps1 -Arch x86 -TestLabel "pre-RC delta
w46-rcdelta"` with the two driver packs, exit 0 (the readme's "TEST
3.4.1.0, pre-RC delta w46-rcdelta."): `xhci98-3.4.1.0.zip` `4f62470d...`,
`-free.zip` `bf0b4fb3...`, `-nonfree.zip` `7b2a48c9...`
(`E:\xhci98-scratch\p46-w46-rcdelta\up\`, `up-sha256.txt`). The kit
`K341` is the base zip unpacked plus an `nt-x64\` made as w46-rcmx's
(`out\pkg-release-amd64`, its catalogs unsigned, `ConvertTo-FamilyLayout
-Arch amd64`; in each x64 guest a throwaway certificate signs every `.sys`
and `.cat` and test signing is on; never the owner's certificate): 92
files, `kits\k341-sums.txt`. `xhci98.sys` x86 `e23926ab...`, amd64
`c93ade8e...`; `xhciuas.sys` x86 `0f55bb12...`, amd64 `2b37cda3...`;
`xhcicm.sys` x86 `6c6850d6...`, amd64 `5e8fb903...`; `stor98.mpd`
`579b78e9...`; `SETUP.EXE` `ba1f68f3...`, `XHCIINST.EXE` `620d30ef...`,
`XHCIIN64.EXE` `62e76327...`, `XHCIWIZ.EXE` `1de26128...`,
`XHCIWZ64.EXE` `3f8edda1...`, `XHCITR.EXE` `cca9bf9f...`,
`XHCITR64.EXE` `e7d66579...`. `xhci98.sys` and `xhciuas.sys` of the zip
are the gated build's (`out\pkg-release-x86`, `out\pkg-release-amd64`).
The published `3.4.0.0` is the cut's own zip, as before.

| System | Leg | Reading |
|---|---|---|
| 98 SE | the window installer, fresh (`r-w98se-G`) | PASS (log 897 lines, all stamped) |
| 98 SE | update over 3.4.0.0 (`r-w98se-U`) | PASS (3.4.0.0's check PASS; log 1072) |
| ME | fresh (`r-me-F2`); `r-me-F` voided (below) | PASS (log 633) |
| 2000 SP4 | update over 3.4.0.0 (`r-w2k-U2`); `r-w2k-U` voided (below) | PASS (3.4.0.0's check PASS; log 150; `/list` "driver xhci98 3.4.1.0") |
| XP x64 SP2 | update over 3.4.0.0 (`r-xpx64-U`) | PASS (3.4.0.0's check PASS; log 132; `DriverVersion` 3.4.1.0) |
| 10 22H2 x64 | the window installer, fresh (`r-w10x64-G`) | PASS (log 159; no `xhciuas`; `DriverVersion` 3.4.1.0) |
| 98 SE | a tunnelled NVMe drive on Stor98 (`tun\`) | PASS (below) |

Every PASS is all of w46-rcmx's readings in the leg's final check after a
cold start: the install's exit 3 (the window legs: Finish "Done. Please
restart Windows now."), the driver files the kit's, every device running
(2000 and XP x64: 8 devices; 98 SE and ME: 9 devnodes, problem 0), the
stick read and written back, `INPT.EXE` `keys=[usbkbd7] enter=1 click=1
at=300,200` through the USB keyboard and tablet, XHCITR running from the
logon with `/report` and `/snapshot` exit 0 (and `/probe` on 2000 to 10),
every installer log line stamped; and on an update leg the same readings
on `3.4.0.0` before the update.

**The window installer, 98 SE** (`r-w98se-G`, `shots\g01` to `g08`):
Welcome "xHCI98 USB 3 drivers 3.4.1.0", two rows "PCI Universal Serial
Bus ... Install"; Components the defaults (the USB 3 driver and the UAS
disk driver, "Storage drivers for Win 98 SE (from NUSB 3.3)" ticked with
its note "Not installed, USB disks will not work without them.", no
Thunderbolt / USB4, the USB Tree at login); Summary (`g04`), its
Components lines verbatim:

    - xhci98.sys (USB 3 driver)
    - xhciuas.sys (UAS disk driver)
    - Thunderbolt / USB4 support (experimental): not installed
    - Stor98 3.4.1.0 (NVMe driver): not installed
    - Storage drivers for Win 98 SE: all seven files installed.
    - xHCI98 USB Tree: installed, starts at login

the storage line no longer followed by the Components page's "Not
installed, USB disks will not work on Windows 98 SE without them." (the
pre-RC matrix's contradiction, gone); Install; Progress; Finish "Done.
Please restart Windows now." (Restart later chosen: the harness powers
off). Then the check: PASS.

**The window installer, 10 x64** (`r-w10x64-G`, `shots\g01` to `g09`):
Welcome "xHCI98 Thunderbolt / USB4 connection manager 3.4.1.0", "No
supported device found." / "drivers stored for later"; Components: the
USB 3 driver unticked, the UAS disk driver box unticked and greyed, the
Thunderbolt box unticked; the USB 3 driver ticked; the "On Windows 8, 8.1
and 10, the xHCI98 USB drivers replace Windows' own USB 3 driver ..."
box, OK; Summary (`g06`), its Components lines verbatim:

    - xhci98.sys (USB 3 driver)
    - xhciuas.sys (UAS disk driver): not installed (one installed earlier is kept)
    - Thunderbolt / USB4 support (experimental): not installed
    - Stor98 3.4.1.0 (NVMe driver): not installed
    - xHCI98 USB Tree: installed, starts at login

Install; Progress "Staged xhci98.inf as oem2.inf.", "Installing Standard
USB 3.0 eXtensible Host Controller - 1.0 (Microsoft) ... done."; Finish
"Done. Please restart Windows now." (Restart later); the guest's wait for
the installer's end let go at once (`XM EXIT gui`, no `OneDriveSetup.exe`
match). Then the check: PASS, with no `xhciuas` staged (46.24's reading),
the log 159 lines, all stamped.

**The Welcome's text while it looks for devices, 10 x64**
(`x-w10x64-W`, `shots\s08.png`): the Welcome's first screens were
not caught in `r-w10x64-G` (the harness's first screen came before the
window, the next after the search), so a fresh overlay of the 10 x64
base was started once more, the kit's `SETUP.EXE` run from the transfer
drive in an elevated prompt and the screen taken every 1.5 s, then the
installer cancelled at its Welcome (no change made) and the guest shut
down. While it looks for devices ("Looking for devices, please wait..."),
under "xHCI98 USB 3 drivers 3.4.1.0":

    Windows 8 and later should have their own USB 3 drivers but you still
    can install xHCI98's if you tick them in the next page.

(wrapped as the window wraps it); once the search found no
controller and no Thunderbolt host the page became the connection
manager's Welcome, as in `r-w10x64-G`. The pre-RC matrix's "Windows
8/8.1 should have its own USB 3 drivers ..." on Windows 10, gone.

**A tunnelled NVMe drive on Stor98, 98 SE** (`tun\`): w46-legs2's
"46.11 and 46.12 a tunnelled drive on 98 SE after the step" recipe, with
this kit: 98 SE without ACPI, an overlay of a copy of that leg's image
(`disk-h1copy.qcow2`, the enclosure's first plug made there), the
ASM4242 model with w46-flt's settings, usb4-emu `57dd977`'s emulator, the
enclosure absent at power-on, one vCPU, 512 MB. The install
(`XHCIINST /dir 98-me /yes /nopause /thunderbolt /stor98`): exit 4,
"/yes: Stor98's driver file is not updated ("Disconnect all external
NVMe drives before Setup continues." cannot be confirmed without asking;
run Setup again without /yes to update it)": that image's Stor98 is
older than this kit's (46.9 changed `stor98.mpd`), and `/yes` does not
answer that question, as designed. Run again without `/yes` (each
question's default, and Y on "Stor98 3.4.1.0 replaces the installed
stor98.mpd at the next start. Disconnect all external NVMe drives before
Setup continues."): exit 3. The `qemu` `xhcicm.sys` (`c215f683...`) and
`XHCIPFW.VXD` (`b7fb6ecf...`) of this build copied over the installed
ones (`fc /b` no differences), the restart, the installed `STOR98.MPD`
compared with the kit's (no differences), then a cold start. The start:
XHCIPFW's "line outcome/before/after" `1`, `0Bh`, `FFh` for the NHI's
function and its port; `cm.irq.resource=0`; `cm.osc.tier=2`,
`cm.osc.mb.before=0`, `cm.osc.handover=00000100`,
`cm.osc.query=000F0100`; `u4.host.id=174C2428`. The enclosure
hot-plugged at the desktop (QMP `device_add`): no hardware wizard, "Orico
(E:)" opened; `dir E:\` volume "ORICO", its two files; `XHCICM.SYS`
copied to E: and compared, no differences; E:'s 1 MiB `TESTDATA.BIN`
copied to C: and back to E: as a second file, both compares no
differences; XHCITR's report: "PCIe tunnel: up - QEMU NVMe Ctrl (E:)",
"Unit behind a Thunderbolt port", "Stor98 Storage Controller for NVMe
SSDs" on `stor98.mpd`, problem code 0, the drive's IRQ 11 (level,
shared); `cm.brnum.9x.irq=00010B01` and the no-IRQ refusals
(`cm.reenum.9x.noirq.refused=03000203`, `04000203`, `05000203`) as in
46.11. So the 46.9 watchdog's build runs a tunnelled drive on a shared
line with its interrupts delivered; the watchdog's own case (a line that
stops being delivered) is 46.9's legs, not read here.

**Voided tries and harness notes.**

- `r-me-F` and `r-w2k-U`: two queue runs drove them at once (a first
  queue thought stopped had gone on); the setup line was typed twice (ME's
  leg read the mode as "FD"). Both stopped by QMP after their first
  starts and run again as `r-me-F2` and `r-w2k-U2` with one driver each;
  the duplicate drivers were ended, no other leg had two (`pids.txt`).
- The 10 window leg's wait for the installer's end now matches the image
  name at the line's start ("PS SETUP.EXE"), so Windows' own
  `OneDriveSetup.exe` (the pre-RC matrix's note) no longer holds it; the
  leg ended by itself after Finish.
- The tunnel leg's install was typed in a second MS-DOS window while the
  first (`PREP.BAT`'s, finished) still held COM1: its echoes there failed
  ("Write fault error writing device COM1") until that window was
  closed; nothing of the install was in them.

**Not covered here.** The other systems of the pre-RC matrix (unchanged
since it for what this delta touches: the installer's two texts are
read above, the Summary's storage note is 98 SE's alone); Stor98's
watchdog change on 2000 to 10 (`stor98.sys`), which these legs do not
load (no `stor98.sys` on the 2000 and XP x64 legs): its evidence is "46.9
the shared-line claims", read on ME, 2000, XP and 7 x86 with that
section's limits; the owner-signed x64 binaries; the `-free` and
`-nonfree` downloads; real hardware.

Evidence: `E:\xhci98-vm\p46\w46-rcdelta\`: `legs\r-<system>-<mode>\`
(`leg.log`, `serial-t*.log`, `shots\`, `pids.txt`), the voided `r-me-F`
and `r-w2k-U`, `legs\x-w10x64-W\`, `tun\` (`serial-a.log` the install,
`serial-b.log`, `serial-c.log` the drive's readings and the report,
`debugcon-c.log`, `infopci-c-before.txt`, the screens), `xtun\` (its
batch files), `scripts\`, `kits\`, `pids.txt`, `STATE.md`. Builds and cut:
`E:\xhci98-scratch\p46-w46-rcdelta\` (`gated.log`, `pkg-*.log`,
`mr.log`, `up\`, `up-sha256.txt`).
