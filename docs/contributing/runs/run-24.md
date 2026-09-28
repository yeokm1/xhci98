# Phase 24 Record - GitHub issue 4's remaining requests: polling rates and true speeds

The detail behind `docs/contributing/roadmap-phases-17-on.md`, "Phase 24 -
GitHub Issue 4's Remaining Requests: Polling Rates and True Speeds". The
roadmap entry carries the goal, the status, the task list and the checkpoint;
this file carries what each task did and what each reading said. Where the two
disagree about a clause, the roadmap wins.

**Written while the phase is open**, and kept as written rather than rewritten
in the past tense. A sentence saying something "is owed" describes the day it
was written; the roadmap's task list says how each task closed.

**On `out\...`, `vm\...` and `tools\...` paths in this file.** They say where
a reading was taken and what the file was called, on the host that ran it;
they are not files a clone has.

Opened 2026-09-24 by task 24.1, the first task of this phase to take a
reading.

---

## 24.1 - Low-Speed polling rates behind a hub

Started 2026-09-24, on branch `issue4`.

### The cause, read before any guest booted

The roadmap's candidate was `XhciIntervalFromPeriod` refusing a Low-Speed
`Period` under 8 "on the strength of the SP4 and NUSB floors alone". The
first thing read was whether the reporter's stack has that floor, since the
reporter runs SweetLow's Windows 9x rebuild of XP SP2's usbport and never
NUSB's. It does not, and that is the whole cause:

- **SweetLow's `USBPORT.SYS` 5.1.2600.2180 has no Low-Speed floor.** Its
  `USBPORT_OpenPipe` producer (`0x24A31`-`0x24A8F`, image base `0x10000`)
  tests the device speed against 2 for High Speed, computes
  `1 << min(bInterval - 1, 5)` on that arm, and on every other arm takes
  `mov al,byte ptr [edx+0Ah]` at `0x24A5B` - the descriptor's own `bInterval` -
  straight into the preload-32-and-shift-right rounding. No compare against 8
  sits anywhere between that load and the store at `0x24A83`. Static,
  `dumpbin /disasm` from `tools/MSVC600`, the listing already in
  `tools/sweetlow-extracted/usbport-disasm.txt`.
- **Windows XP SP3's 5.1.2600.5512 has the floor SP4 and NUSB have**, at
  `0x25A69`-`0x25A78` (`test edx,edx` for Low Speed, `cmp al,8`, `jae`,
  `mov byte ptr [esi+10Eh],8`). Static, a fresh `dumpbin /disasm` over
  `tools/winxpsp3-extracted/usbport.sys` into the session scratchpad. So the
  three Microsoft builds this project holds for NT 5.x floor, and the one
  rebuild does not; whether SweetLow removed it or XP SP2 lacked it is not
  read and does not matter to the driver.

The chain from there to the reporter's screen needs no guess. hidusbf's 9x
filter rewrites the interrupt endpoint's `bInterval` in the
`SELECT_CONFIGURATION` descriptor (Phase 20's static read of the filter,
`legal-provenance.md` section 4). At 250, 500 and 1000 Hz that is 4, 2 and 1.
Behind a hub the device is at its true speed, so SweetLow's usbport buckets it
as a Low-Speed `Period` of 4, 2 or 1. `XhciBuildEndpointParams` then returned
`XHCI_CTX_BAD_PARAM` from the Low-Speed refusal, `OpenEndpoint` released the
pool ring, counted `EndpointRefusalsParams` and answered
`MP_STATUS_NO_RESOURCES`, usbport failed the pipe open and with it the
configuration, and the HID driver could not start: **Code 10**. At 125 Hz
(`bInterval` 8) the same path delivers `Period` 8 and works, which is where
the reporter's "250 Hz and above" boundary comes from. Under NUSB the same
tool never crosses 8 and the mouse never fails - and never speeds up either.

Both readings are provenance rows (section 4) and the ABI document's
"Periodic scheduling" section carries them as a table under the SP4
derivation.

### What changed

- `src/xhci_ctx.c`, `XhciIntervalFromPeriod`: the Low-Speed arm no longer
  refuses `Period` 1, 2 or 4. It translates them as Full Speed's are
  translated (frames, shift 3), giving Interval 3, 4 and 5 - 1, 2 and 4 ms -
  all inside Table 6-12's Low-Speed range of 3 to 10. The power-of-two and
  1..32 contract is unchanged, so a `Period` no shipping usbport sends stays
  refused. `src/xhci.h`'s contract comment says the reachable Low-Speed range
  is now 3-8, and from which builds each end comes.
- `test/test_ctx.c`: the three sub-8 Low-Speed buckets are pinned as accepted
  values with their Intervals (they were pinned as refusals), `Period` 3 at Low
  Speed is pinned as still refused, and the endpoint-builder vector that
  carried "LS period 4 refused through the builder too" now carries the
  endpoint it becomes (Interval 5, no burst). The suite passed:
  `test_ctx: 330 checks, 0 failures`, every other suite unchanged and green.
- `src/xhci_slot.c`, `OpenEndpoint`: a third tier-2 ring record,
  `ep.open.ival`, beside `ep.open` and `ep.open.rate`:
  `speed << 16 | floored << 8 | Interval`, where `speed` is the class usbport
  bucketed with. Phase 20's reading had to fetch the programmed Interval back
  through the DCBAA over QEMU's monitor; on a bare-metal machine, or a guest
  read only through `XHCISNAP`, this record is the only way to see it.
- `docs/contributing/implementation-invariants.md`, "Root Hub Reporting": the
  sentence that said Low Speed is floored at 8 now says by which builds.

### What is owed, and what cannot be taken in QEMU

- **The reproduction and the Low-Speed reading need a Low-Speed device, and
  QEMU has none.** Every emulated HID device in this QEMU build enumerates at
  12 Mb/s, `usb_version=0` is refused, and the one in-QEMU route to
  `USB_SPEED_LOW` is `usb-host` passthrough of a real mouse rebound off the
  host's HID driver (Phase 5's measurement, `lessons.md`). So the roadmap's
  "QEMU `usb-hub` plus `usb-mouse` on the SweetLow guest" cannot exercise the
  arm this task changed: a `usb-mouse` behind a `usb-hub` is Full Speed there,
  and Full-Speed `Period` 1, 2 and 4 were accepted before this task (Phase 20's
  behind-hub rows). The checkpoint's Low-Speed clause - Interval read from the
  snapshot, no Code 10, at 250, 500 and 1000 Hz on Windows 98 SE under
  SweetLow's stack - is therefore a **bare-metal reading**: the E460, a
  Low-Speed mouse behind a USB 2.0 hub, SweetLow's stack installed on its
  Windows 98 SE, hidusbf's 9x filter, `XHCISNAP` for `ep.open.ival`. Or
  `usb-host` passthrough, which needs the owner's host mouse rebound to
  WinUSB and is not something to do to a working machine unasked. **The
  owner's call.**
- **The no-regression reading on every other guest is takeable in QEMU as
  written**, because it is a regression read rather than a Low-Speed one:
  hidusbf on each guest, the Full-Speed `usb-mouse,usb_version=1` behind a
  `usb-hub`, `bInterval` 4, 2 and 1, `ep.open.rate` and `ep.open.ival` from
  the ring. What it shows is that the Full-Speed path is untouched and what
  each usbport passes down. It cannot show the Low-Speed arm on any of them,
  and the record must not say it does.
- **hidusbf itself.** The Phase 20 copy was never kept (`out\` and the
  overlay were discarded). Fetching `hidusbf.zip` again from
  `github.com/LordOfMice/hidusbf` through this host's proxy returns a McAfee
  Web Gateway scanning page in place of the archive, by three routes (the
  `raw` path, `raw.githubusercontent.com`, and the contents API with the raw
  media type); the owner is downloading it by hand. It goes to
  `tools\hidusbf.zip`, extracted to `tools\hidusbf-extracted\`, both
  git-ignored under `tools/`; the README there records hash and version the
  way `tools/sweetlow-extracted/README.md` does.
- The device matrix on both primary targets against
  `runs/run-23-post-release/`, and the cut, are 24.7's (24.5 until 2026-09-27, 24.6 until 2026-09-28).

### What arrived, and what it settled (later the same day)

**hidusbf is in hand.** The owner downloaded both archives by hand at 19:37.
`tools\hidusbf.zip`, 616,467 bytes, sha256
`bd8d1fb0545d8df88d9cef0c67682daef7d304561bc64acfee5c0d8c12d0f797` - the
**same hash Phase 20 recorded**, and `DRIVER\98ME\hidusbf.sys` still hashes
`b14d9d49...` at 3,648 bytes and version 1.2.0.10. So the tool Phase 20 read
statically and the tool this task will run are one file, and Phase 20's
account of the 9x filter carries over without re-reading it. A second archive
came with it, `tools\hidusbfn.zip`, 89,053 bytes, sha256 `19da9ed6...`: an
addendum for Intel's `IUSB3XHC.SYS` on Windows 7 and Microsoft's
`USBXHCI.SYS` on Windows 8 and later, neither of which this project targets.
Both are extracted beside their zips under the git-ignored `tools/`, and
`tools/hidusbf-extracted/README.md` carries the hashes, the version
resources, the directory layout and the traps, the way
`tools/sweetlow-extracted/README.md` does.

**The author's own manual corroborates the disassembly.** `README.ENG.TXT`
section 2, unprompted and predating every reading here, says the polling-rate
restriction is an NT-side one: "Windows 2000 doesn't have restrictions for
overclocking Low Speed USB devices in driver code for OHCI and UHCI
controllers... **Windows 98 and ME also don't have such restrictions, so
there is no patch version of driver for these OSes at all**", and he offers
"a special unofficial version of `USBPORT.SYS` for Windows 98 and ME without
any restrictions of polling rate". The archive's shape says the same thing
twice over: every NT build ships in a **patching** form, which edits
Microsoft's `usbport.sys` in memory to remove the floor, and a **NOPATCH**
form which cannot raise a Low-Speed rate at all - and there is no such pair
for 9x, because on 9x there is nothing to patch. That is the author's account
of exactly what the `0x24A5B` arm shows. It is independent of the
disassembly, it agrees with it, and it is still not a runtime reading.

**QEMU's lack of a Low-Speed device re-measured, not recalled.** On QEMU
11.1.0 (`v11.1.0-12130-ge470268ff4`), `usb-mouse` and `usb-kbd` expose
`usb_version` alone (1 Full, 2 High) and `usb-wacom-tablet` and `usb-braille`
expose no speed property at all. That is Phase 5's finding
(`build-and-test.md`) holding on the build actually installed.

**So the Low-Speed reading is `usb-host` passthrough, and the owner chose
it.** The roadmap lists "a metal reading of 24.1" under *Not a checkpoint*,
which leaves passthrough as the only route that answers the checkpoint as
written. `info usbhost` on this host - QEMU's own libusb enumeration, run
against a throwaway machine with no disk - reports a **`046d:c077` "USB
Optical Mouse" at Speed 1.5 Mb/s**: a genuine Low-Speed device, already
attached, and not the owner's working pointer (a `046d:c099` G502 X at
12 Mb/s, left alone). It is bound to WinUSB with `tools\zadig-2.9.exe`
because Windows' HID driver holds it exclusively and libusb cannot detach a
kernel driver on Windows.

### The vehicle

- **The binary.** The owner built all six flavours at 11:23-11:30; the
  `qemu` x86 one's `srcstamp` matches `HEAD` (`4e52964`), so the binary under
  test is the committed 24.1 source and not a stale object. The host suites
  were re-run against it: 16,782 + 234 + 2,027 checks, 0 failures.
- **The package.** `make-package.ps1 -Flavor qemu -Arch x86` staged
  `out\pkg-qemu-x86` (`xhci98.sys` 164,944 B, 1.1.1.0 - 24.1 does not bump
  the version, 24.7 does), copied to `vm\xfer98\XHCI98\` and verified
  byte-identical to `src\objchk_qemu\i386\xhci98.sys`.
- **The guest.** `vm\t24-sweetlow.img`, a copy of `vm\sweetlow-2a.img`
  reverted to its one snapshot, `sweetlow-stack-nodriver`: SweetLow's stack
  installed, no xhci98 driver of any version. A clean install rather than an
  upgrade, which also steps around the trap that 24.1 ships the same
  `DriverVer` as `1.1.1.0`.
- **The transfer drive.** `vm\xfer98` already carried, from Phase 20 and
  unchanged since, `HIDUSBF\` (98ME filter + INFs + Setup.exe), `USBD36\`
  (NUSB 3.6's Windows ME `usbd.sys`, which the filter's
  `USBD_ParseDescriptors` import needs), `XHCISNAP.EXE` and the `*.REG`
  files. All three staged copies were re-hashed against the freshly extracted
  archive and match.
- **The launcher.** `scripts\local\phase24\qemu-sweetlow-24-1.cmd`
  (per-host, git-ignored): `pc,smm=off`, `pentium3`, 256 MB,
  `qemu-xhci,p2=8,p3=0`, a `usb-hub` given at launch on root port 2, the
  passthrough mouse hot-plugged from the monitor onto `port=2.1`,
  isa-debugcon, a QEMU trace (`ls-trace-events.txt`, Phase 20's list plus
  `usb_host_*`), read-only VVFAT `vm\xfer98`, monitor 56890.

### The readings, 2026-09-24 evening

Taken on `vm\t24-sweetlow.img` through `qemu-sweetlow-24-1.cmd`, monitor
56890, four boots (`b1` the pre-stage, `b2` the driver and tooling install,
`b3` the hub, `b4` the rates). The owner drove the guest's GUI from `b3`
onward; the monitor, the ring and the counters are this side.

**The Low-Speed device is real and it is behind the hub.** `info usb` in the
guest:

```
Device 0.1, Port 2,   Speed 12 Mb/s,  Product QEMU USB Hub,      ID: hub1
Device 0.2, Port 2.1, Speed 1.5 Mb/s, Product USB Optical Mouse, ID: lsm
```

1.5 Mb/s is Low Speed, on a port behind a hub, which is what this task needed
and what no emulated QEMU peripheral can present. Device Manager, by
connection, shows the whole chain: `USB 2.0 eXtensible Host Controller
(xhci98)` -> `USB 2.0 Root Hub` -> `Generic USB Hub (EHCI)` -> `USB Human
Interface Device`. The hub's name is SweetLow's own string
(`USB\HubClass.DeviceDesc="Generic USB Hub (EHCI)"` in his `USB2.INF`), which
is how that screenshot also says the guest is on his stack and not
Microsoft's - his `usbhub20.sys` bound it.

**The guest.** `xhci98.sys` built `Sep 24 2026 11:23:51`,
`USBPORT_GetHciMn=10000001` (the XP-lineage value, so SweetLow's usbport and
not NUSB's), `MiniPortExtensionSize=000168A0` = 92320, matching
`offsets.txt`'s SIZEOF. `XhciLogVerbosity` 2, `Log.Enabled` 1.

**The tool.** hidusbf's own `Setup.exe`, driven by the owner rather than
Phase 20's hand-written registry keys - a better reading, because it is the
reporter's configuration rather than a reconstruction of it. It listed the
device as `Filter? Yes`, `bInterval 10`, `Controller Name: USB 2.0
eXtensible Host Controller (xhci98)`. `Filter? Yes` is also the proof that
Phase 20's Code 2 trap was paid: the 98 SE `usbd.sys` was replaced by NUSB
3.6's Windows ME build (22,928 bytes) before the filter would load.

#### The four readings

Each row is one `ep.open` / `ep.open.rate` / `ep.open.ival` triple out of the
ring, decoded by `scripts\local\phase24\decode-ival.py`. `speed` is the class
usbport bucketed the device with, `floored` is whether the Low-Speed floor
moved the value, and `Interval` is what went into the Endpoint Context.

| hidusbf setting | `bInterval` | `ep.open.rate` | Period | `ep.open.ival` | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|---|---|
| Default | 10 | `00080004` | 8 | `00010006` | Low | no | 6 | 8 | 125 |
| 250 | 4 | `00040004` | 4 | `00010005` | Low | no | 5 | 4 | 250 |
| 500 | 2 | `00020004` | 2 | `00010004` | Low | no | 4 | 2 | 500 |
| 1000 | 1 | `00010004` | 1 | `00010003` | Low | no | 3 | 1 | 1000 |

**This is the task's whole claim, measured.** `Period` 4, 2 and 1 at Low
Speed are the three values `XhciIntervalFromPeriod` refused before this task,
and that refusal is what returned `XHCI_CTX_BAD_PARAM` -> `OpenEndpoint`
`MP_STATUS_NO_RESOURCES` -> a failed pipe open -> the reporter's **Code 10 at
250 Hz and above**. All three now open. Their Intervals are 5, 4 and 3, all
inside Table 6-12's Low-Speed range of 3 to 10, and the mouse carries no
Code 10 - Device Manager shows no exclamation on it, in a screenshot where
the unrelated `PCI Ethernet Controller` does carry one, which is the control
that makes the absence mean something.

The stock row is worth as much as the other three. `bInterval` 10 buckets to
`Period` 8 with **no floor applied** (`floored` no, `EndpointIntervalsFloored`
0), which is SweetLow's usbport having no Low-Speed floor - read at runtime
rather than out of the disassembly. It is also the reporter's own boundary:
125 Hz was the one rate that always worked.

**usbport passes the true speed down behind a hub.** Every mouse row reads
`speed` Low, against the hub's own root-port row of High - so the miniport is
told Low Speed for the device and High for the hub, and the bucketing this
task turns on is genuinely the Low-Speed arm. The hub's row also puts issue
6's item 1 on the record in one line: a root-port device reported High Speed,
`ep.open.ival=00030005`, `speed` High, `Period` 32, Interval 5. That is the
known reporting limitation 24.3 is about, and `ep.open.ival` is now the
cheapest way to see it.

#### Counters at the end of the run

```
DevicesAddressed             8      OpensTotal                  24
OpensAccepted               24      BehindHubOpens               7
EndpointRefusalsParams       0      EndpointRefusalsPool         0
EndpointRefusalsType         0      EndpointRefusalsNotReady     0
EndpointIntervalsFloored     0      EndpointsNoResources         0
EndpointsNoBandwidth         0      EndpointSpeedMismatches      1
```

24 opens seen, 24 accepted, nothing refused for any reason and nothing
floored. `EndpointSpeedMismatches` 1 is the root-port hub and not the mouse.

#### What this run cost, and what the next one should not pay again

- **Phase 20's `readring.ps1` reads the wrong memory now.** It hardcoded the
  `1.0.1.0` extension layout - `Log.Enabled 75072`, `Head 75116`,
  `Used 75120`, `ErrorRecords 75160`, `Ring 75224`. On this build
  `Log.Enabled` is 75760 and `Ring` is 75912, so those constants are 688
  bytes short and would have reported unrelated memory as the ring.
  `readring24.ps1` now derives them from `scripts\vm-matrix\offsets.txt` -
  `Head = Log.Appends - 8`, `Used = Log.Appends - 4`,
  `Ring = Log.FlushFailures + 4 + 64` - because `gen-offsets.ps1` names only
  counters and those four fields are not counters. **Any other Phase 20
  reader reused later needs the same check.**
- **`readring.ps1` also carried a latent bug**: `$tmp -replace '\', '/'` is
  an invalid regular expression and throws on every invocation. Replaced with
  a literal `.Replace()`. It cannot have worked as written, so the Phase 20
  copy was evidently edited after its run.
- **hidusbf's `[Restart]` only half-cycles the device.** After each rate
  change the slot was re-addressed but no pipe was reopened, so the ring did
  not move and the reading looked like a refusal when the counters said
  nothing had been refused. `device_del lsm` + `device_add ...` from the
  monitor forces the full re-enumeration, and every row above was taken
  through it. Do not read a missing `ep.open` as a Code 10 without checking
  `EndpointRefusalsParams` first.
- **`info usbhost` is the cheap way to find a Low-Speed device on the host.**
  `qemu-system-x86_64 -machine pc -m 64 -display none -nodefaults -monitor
  stdio`, then `info usbhost`, prints every host device with its speed; feed
  the monitor a leading newline or it eats the first character. Windows
  exposes no USB speed through `Get-PnpDeviceProperty`, and the DDK's
  `usbview.exe` is GUI-only.
- **IDE will not hotplug a CD, but the `pc` machine already has one.**
  `device_add ide-cd,bus=ide.1` is refused ("Bus 'ide.1' does not support
  hotplugging"); `change ide1-cd0 <iso>` inserts media into the drive that is
  already there. The launcher now carries the CD from the start.
- **The INF wants the Windows CD, and that is safe on this guest.** The
  `LayoutFile` route fetches `usbd.sys`, `usbhub.sys` and `usbui.dll` from
  the OS's own source, and the `sweetlow-stack-nodriver` snapshot has none of
  them. It does **not** touch SweetLow's `usbport.sys`: `[Xhci.CopyW98]` does
  not list that file at all, and every line it does list carries flag 16,
  `COPYFLG_NO_OVERWRITE`. Answer the prompt with `e:\win98`.

## 24.1 - the no-regression reading on the other guests

The roadmap asks for the same hidusbf reading on every other guest the
project holds, "to show the change affects none of them and to record what
each system's usbport passes down". It is a **regression read, not a
Low-Speed one**: QEMU models no Low-Speed peripheral, so the mouse here is
`usb-mouse,usb_version=1` - Full Speed - behind a `usb-hub` on root port 2.
It cannot exercise the arm 24.1 changed and must not be recorded as doing so.

Vehicle: `scripts\local\phase24\run-guest.ps1`, one launcher for every guest,
each on a qcow2 **overlay** of the owner's matrix image so a reading cannot
alter it. `qemu-xhci,p2=8,p3=0` and a `usb-hub` at launch on every guest, so
the topology is the same one the SweetLow reading used. Artifacts in
`out\t24-1\`.

### Windows 98 SE under NUSB 3.3 - done

`vm\win98.img` overlay, monitor 56801. The driver on it was the
`Sep 3 2026` build; the 24.1 package was pre-staged with
`rundll32 setupx.dll,InstallHinfSection DefaultInstall 132 d:\xhci98\xhci98.inf`
and picked up on the next boot: `DriverEntry (built Sep 24 2026 11:23:51)`,
`USBPORT_GetHciMn=57324B30` - the Win2000-lineage value, against SweetLow's
`10000001`, which is the two stacks telling themselves apart in one line.

| hidusbf | `bInterval` | Period | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|
| Default | 10 | 8 | Full | no | 6 | 8 | 125 |
| 250 | 4 | 4 | Full | no | 5 | 4 | 250 |
| 500 | 2 | 2 | Full | no | 4 | 2 | 500 |
| 1000 | 1 | 1 | Full | no | 3 | 1 | 1000 |

`OpensTotal` 15, `OpensAccepted` 15, every `EndpointRefusals*` 0,
`EndpointIntervalsFloored` 0, `BehindHubOpens` 4,
`EndpointSpeedMismatches` 1 (the root-port hub). The same Interval ladder as
the SweetLow guest produced at Low Speed - 6, 5, 4, 3 - reached here at Full
Speed, which is the point: these Full-Speed values were accepted before 24.1
as well, so the path is untouched.

hidusbf's `Setup.exe` warned that `Controller Driver "xhci98.sys" is unknown,
some overclocking (to higher USB Speed Class) is probably impossible`. That
is the tool looking for a `usbport.sys` it knows how to patch and not finding
one; on 9x it patches nothing anyway, and the `bInterval` rewrite in the
`SELECT_CONFIGURATION` URB is all it needs. Answering Yes is correct.

### What the other guests cost, before any of them gave a reading

- **The NT guests are set to DVORAK, and `sendkey` names keys by their
  QWERTY POSITION.** Every keystroke arrives, mapped: `cmd` becomes `jme`,
  `d:\` becomes `eS\`. It reads exactly like a flaky emulator dropping
  characters - it was diagnosed that way first, and a longer key hold was
  added to chase it, which could never have helped because nothing was ever
  dropped. The owner named the layout.
  **`build-and-test.md` already said so** - "the guests are US Dvorak and
  the accelerators follow the layout", with the two UAC accelerators spelled
  out as physical keys - but it says it inside the x64
  signature-enforcement recipe, which is not where anyone looks before
  typing a path into a Run box. What this cost was not discovering the fact;
  it was not having read it first.
  `scripts/local/phase24/type24d.ps1` types through the inverse map; the 9x
  guests are QWERTY and keep `type24.ps1`.
- **The guest pointer cannot be driven by one computed delta.** QEMU's
  `mouse_move` is relative and Windows applies pointer acceleration on top:
  a 137-pixel move travelled 274 on Windows 98, a 250-pixel move travelled
  515 on XP, and small steps travel *less* than asked because "enhance
  pointer precision" shrinks them - a walk to 220,294 in steps of 4 arrived
  at 175,237. `scripts\local\phase24\clickat.ps1` homes the pointer into the
  top-left corner first, where the clamp is exact, then walks in small steps
  with a per-guest `-Scale`. Even that is only good to a few pixels over a
  long walk, so prefer a keyboard accelerator where the dialog has one.
- **A CD is not needed on a 9x guest that has its CABs.** The INF's
  `LayoutFile` route wants `usbd.sys`, `usbhub.sys` and `usbui.dll`; on the
  NUSB guest `C:\WINDOWS\OPTIONS\CABS` answered the prompt where the CD
  drive letter did not.
- **The transfer drive is `D:` on the 9x guests and `E:` on the NT ones**,
  because the NT guests have a CD-ROM ahead of it.

### Windows 2000 SP4 - done

`vm\win2k-xonly.img` overlay, monitor 56803. The 24.1 package was installed
through the Found New Hardware wizard from the transfer drive (`E:` here, not
`D:` - the NT guests have a CD-ROM ahead of it), and the binary that loaded is
`built Sep 24 2026 11:23:51` with `USBPORT_GetHciMn=57324B30`.

| hidusbf | `bInterval` | Period | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|
| Default | 10 | 8 | Full | no | 6 | 8 | 125 |
| 250 | 4 | 4 | Full | no | 5 | 4 | 250 |
| 500 | 2 | 2 | Full | no | 4 | 2 | 500 |
| 1000 | 1 | 1 | Full | no | 3 | 1 | 1000 |

`OpensTotal` 15, `OpensAccepted` 15, every `EndpointRefusals*` 0,
`EndpointIntervalsFloored` 0, `BehindHubOpens` 4.

### Windows XP SP3 - done

`vm\winxp.img` overlay, monitor 56804. `built Sep 24 2026 11:23:51`,
`USBPORT_GetHciMn=10000001` - the same value SweetLow's rebuild gives, which
is what it should be: his is a rebuild of XP's own usbport.

| hidusbf | `bInterval` | Period | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|
| Default | 10 | 8 | Full | no | 6 | 8 | 125 |
| 250 | 4 | 4 | Full | no | 5 | 4 | 250 |
| 500 | 2 | 2 | Full | no | 4 | 2 | 500 |
| 1000 | 1 | 1 | Full | no | 3 | 1 | 1000 |

`OpensTotal` 18, `OpensAccepted` 18, every `EndpointRefusals*` 0,
`EndpointIntervalsFloored` 0, `BehindHubOpens` 5. (18 rather than 15 because
the 250 Hz row was read twice - the first attempt to step the rate combo by
keyboard did not reach it and the replug repeated 250. The duplicate is in
the ring and is left there.)

### Windows ME - done

`vm\winme.img` overlay, monitor 56802. `built Sep 24 2026 11:23:51`,
`USBPORT_GetHciMn=10000001`. **That value is the XP lineage, not the Windows
2000 one**, and this guest's Documents list names `USB2.INF` under
`D:\T231\SWEETLOW`: the ME guest is running SweetLow's stack too. So of the
five guests read so far, three are on his usbport (98 SE under his stack, ME,
and XP's own) and two on NUSB's Win2000-lineage build (98 SE under NUSB 3.3,
Windows 2000).

| hidusbf | `bInterval` | Period | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|
| Default | 10 | 8 | Full | no | 6 | 8 | 125 |
| 250 | 4 | 4 | Full | no | 5 | 4 | 250 |
| 500 | 2 | 2 | Full | no | 4 | 2 | 500 |
| 1000 | 1 | 1 | Full | no | 3 | 1 | 1000 |

`OpensTotal` 15, `OpensAccepted` 15, every `EndpointRefusals*` 0,
`EndpointIntervalsFloored` 0, `BehindHubOpens` 4.

### What the five agree on

Every guest read so far produces the **same ladder** - `bInterval` 10, 4, 2, 1
-> `Period` 8, 4, 2, 1 -> Interval 6, 5, 4, 3 - and on every one of them the
mouse is bucketed **Full Speed** and nothing is floored. Two things follow.

The first is the answer the task wanted: the Full-Speed path is untouched by
24.1, on both usbport lineages and on four operating systems, with not one
refusal of any kind in 63 endpoint opens.

The second is worth more than it looks. The Interval ladder is identical to
the one the SweetLow guest produced at **Low** Speed, because
`XhciIntervalFromPeriod` now treats a Low-Speed `Period` exactly as it treats
a Full-Speed one. These readings are therefore the control for that change:
they show the values 24.1 newly accepts at Low Speed are the values every
shipping usbport has been sending at Full Speed all along, and that this
driver has always programmed them correctly.

### Also true of every guest, and not a reading

The root-port hub is reported **High Speed** on all five
(`ep.open.ival=00030005`), against the mouse behind it at its true speed.
That is issue 6's item 1, unchanged by this task and 24.3's subject.
`EndpointSpeedMismatches` is 1 on every guest and it is the hub, never the
mouse.

### The NT 6.x guests cannot take this reading behind a hub, and that is issue 6's fault, not 24.1's

**What happened.** Windows Vista SP2 x86, with the 24.1 build installed and
the Full-Speed `usb-mouse` hot-plugged behind the `usb-hub` on root port 2,
bugchecked the moment the mouse was configured, and went on bugchecking: five
`DriverEntry` lines in one QEMU run, each cycle ending on the same three
records -

```
xhci98: descriptor replies folded=00000005
xhci98: descriptor configs committed=00000004
xhci98: DriverEntry (built Sep 24 2026 11:23:51)      <- the restart
```

**It is `docs/issues/06-full-speed-root-port-bugcheck.md` section 6.2**, which
measured it on 2026-09-19 against `1.1.0.0`, five days before this task
existed: "A Full-Speed hub on a root port, and a Full or Low Speed device
with a periodic endpoint behind it - a mouse was enough - stops the machine
with `STOP 0x0000007E` in `USBPORT.SYS` the moment that device is
configured. All four NT 6.x builds do it; the same steps passed on Windows
2000 and XP x64." QEMU's `usb-hub` is a Full-Speed USB 1.1 hub with no
transaction translator, the driver reports it as High Speed because it is on
a root port, and usbport's USB 2.0 budgeter then charges a NULL TT at
`0xA04`. The stack in that section is usbport's alone; **no frame is in
`xhci98.sys`**.

**The control, taken rather than assumed.** A second overlay was cut from the
untouched `vm\vista.img`, which still carries the `1.1.1.0` release binary
(`built Sep 20 2026 10:33:35`), and the same mouse was hot-plugged behind the
same hub. It crashed at the **identical point**: the same
`descriptor configs committed=00000004` followed by a fresh `DriverEntry`,
at line 1615 against 1604. So the fault is unchanged by 24.1, and this run
reproduces a known limitation of the shipped release rather than finding a
new one. Both logs are kept: `out\t24-1\vista-24.1-crash-debugcon.log` and
`out\t24-1\vista-control-prev-build-debugcon.log`.

**So the sweep's topology was wrong for four of the nine guests**, and the
roadmap's "Each takes the mouse behind the hub" cannot be met on Vista or
Windows 7 in either architecture. That sentence was written for the 9x and
Windows 2000 guests, where behind-a-hub is the reporter's own configuration
and the only place the Low-Speed arm can be reached; on the NT 6.x guests it
names the one topology issue 6 says stops the machine. The reading there is
taken on a **root port** instead, and what it shows is narrower and must be
read as such: a root-port device is reported High Speed (issue 6's item 1),
so it exercises the High-Speed bucketing and not the behind-hub path.

It was the owner who named the cause, from the symptom alone, while the
control was still booting.

### Windows Vista SP2 x86 and Windows 7 SP1 x86 - done, on a ROOT PORT

Both carry `built Sep 24 2026 11:23:51`; both took the driver only through
**Have Disk**, because Windows refuses the identical `DriverVer` as an
upgrade and answers "the best driver software for your device is already
installed". The mouse is on **root port 1**, not behind the hub, for the
reason in the section above.

Identical readings on the two systems, to the record:

| hidusbf | `bInterval` | `ep.open.rate` | Period | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|---|
| Default | 10 -> 6 | `00200004` | 32 | High | no | 5 | 4 | 250 |
| 250 | 6 | `00200004` | 32 | High | no | 5 | 4 | 250 |
| 500 | 5 | `00100004` | 16 | High | no | 4 | 2 | 500 |
| 1000 | 4 | `00080004` | 8 | High | no | 3 | 1 | 1000 |

Vista `OpensTotal` 30 / `OpensAccepted` 30; Windows 7 27 / 27. Every
`EndpointRefusals*` 0 and `EndpointIntervalsFloored` 0 on both.

**Read this table differently from the other five.** The device is on a root
port, so the driver reports it **High Speed** (issue 6's item 1) and usbport
buckets it with the High-Speed arm, where `bInterval` is a log2 exponent
rather than a frame count. hidusbf writes the exponent accordingly - the
`bInterval` column in its own window reads **6** for 250 Hz, not 4 - and
`1 << min(bInterval - 1, 5)` gives the Periods above. So this is a reading of
the **High-Speed** bucketing, not of the behind-hub path the other five
exercise, and it cannot say anything about the Low-Speed arm 24.1 changed.

What it does do is reproduce `docs/issues/06-full-speed-root-port-bugcheck.md`
section 5's three bands on the 24.1 build: "`bInterval` 1 to 4 gives 1 ms, 5
gives 2 ms, 6 and above gives 4 ms. A stock mouse at `bInterval` 10 runs at
4 ms; nothing slower is reachable, nothing faster than 1 ms either, and two
values inside one band are indistinguishable." Stock and 250 Hz land on the
same `Period` 32 for exactly that reason - both are in the 4 ms band - and a
root-port device cannot reach the 8 ms that every behind-hub guest shows at
default. That is corroboration of a measured result, not a new one, and it
is data 24.2 will want.

`EndpointSpeedMismatches` is 12 and 11 here against 1 on the five behind-hub
guests. The difference is the topology and not a defect: on those five the
only mismatch is the root-port hub, while here every open of the mouse is one
too, because the mouse itself is the root-port device being reported High
Speed.

### Windows XP Professional x64 SP2 - done, behind the hub, and the first amd64 reading

`vm\winxp64.img` overlay, monitor 56807, `built Sep 24 2026 11:30:29` - the
**amd64** binary, seven minutes after the x86 one in the same build run.
`USBPORT_GetHciMn=10000001`, `MiniPortExtensionSize=00017548` = 95560, which
is `offsets-amd64.txt`'s SIZEOF against x86's 92320.

The mouse is **behind the hub** here, not on a root port. Issue 6 section
6.2 names XP x64 and Windows 2000 as the two targets that survive that
topology, and this run is that prediction holding: the hub and the mouse
behind it enumerated and stayed up through four rate changes and twelve
behind-hub opens.

| hidusbf | `bInterval` | `ep.open.rate` | Period | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|---|
| Default | 10 | `00080004` | 8 | Full | no | 6 | 8 | 125 |
| 250 | 4 | `00040004` | 4 | Full | no | 5 | 4 | 250 |
| 500 | 2 | `00020004` | 2 | Full | no | 4 | 2 | 500 |
| 1000 | 1 | `00010004` | 1 | Full | no | 3 | 1 | 1000 |

`OpensTotal` 37, `OpensAccepted` 37, every `EndpointRefusals*` 0,
`EndpointIntervalsFloored` 0, `BehindHubOpens` 12,
`EndpointSpeedMismatches` 1 - the root-port hub, as on every behind-hub
guest.

**The ladder is identical to the five 32-bit behind-hub guests**, which is
the point worth having: the two builds come from one source tree through two
toolchains, and this is the first evidence in this task that the amd64 one
buckets and programs intervals the same way. Nothing in `xhci_ctx.c` is
architecture-dependent, so that was expected; it had not been read.

**Reading an amd64 guest needs three things changed together**, and any one
alone fails: the offset table (`-Arch amd64`), the identity line (the
debugcon carries a high/low PAIR, `FFFFFADF` + `CE6A3DC8`, not one 32-bit
value), and the address width - `0xFFFFFADFCE6A3DC8` does not fit
`ToUInt32` and `{0:X8}` would print it truncated with no error at all.
`readring24.ps1` and `readctr.ps1` both take `-Arch` now; `readctr` also
picks `offsets-amd64.labels.txt`. The SIZEOF check is what caught it -
"guest SIZEOF 95560 != table 92320" - which is that check doing exactly the
job it was written for.

### Windows Vista SP2 x64 and Windows 7 SP1 x64 - done, on a root port

Both `built Sep 24 2026 11:30:29`, `MiniPortExtensionSize=00017548` = 95560.
Both took the driver through **Have Disk**, both need
`bcdedit /set {current} advancedoptions true` and *Disable Driver Signature
Enforcement* on every boot because the package is unsigned, and both are run
with **no `usb-hub` on the machine at all** (`run-guest.ps1 -NoHub`). The hub
alone is harmless - issue 6 section 6.2 says so - so the switch is not what
keeps these guests up; the mouse being on a root port is. It exists so the
fatal topology cannot be reached by a mistyped `port=`, which is how it was
reached the first time.

Identical on both, and identical to Vista and Windows 7 x86:

| hidusbf | `bInterval` | `ep.open.rate` | Period | speed | floored | Interval | ms | Hz |
|---|---|---|---|---|---|---|---|---|
| Default | 10 -> 6 | `00200004` | 32 | High | no | 5 | 4 | 250 |
| 250 | 6 | `00200004` | 32 | High | no | 5 | 4 | 250 |
| 500 | 5 | `00100004` | 16 | High | no | 4 | 2 | 500 |
| 1000 | 4 | `00080004` | 8 | High | no | 3 | 1 | 1000 |

Vista x64 `OpensTotal` 24 / `OpensAccepted` 24; Windows 7 x64 25 / 25. Every
`EndpointRefusals*` 0 and `EndpointIntervalsFloored` 0 on both.
`EndpointSpeedMismatches` 10 on each, all of them the root-port mouse.

**A step that is easy to skip and silently wastes a boot:** `XHCISNAP
-verbosity 2` is read once per driver start and never re-read, so it needs a
restart - and on these two that means the F8 menu again. Both were first read
with `Log.Enabled` 0, `Head` 0, `Used` 0, `Appends` 0, which is what the ring
looks like when the value was never applied rather than when nothing has
happened.

---

## 24.1 - what all ten guests say

| Guest | usbport | `GetHciMn` | Topology | Stock | 250 | 500 | 1000 | Opens |
|---|---|---|---|---|---|---|---|---|
| 98 SE, SweetLow | his XP rebuild | `10000001` | behind hub, **Low Speed** | 8 ms | **4 ms** | **2 ms** | **1 ms** | 24/24 |
| 98 SE, NUSB 3.3 | Win2000 | `57324B30` | behind hub, Full | 8 ms | 4 ms | 2 ms | 1 ms | 15/15 |
| Windows ME | SweetLow's | `10000001` | behind hub, Full | 8 ms | 4 ms | 2 ms | 1 ms | 15/15 |
| Windows 2000 SP4 | Win2000 | `57324B30` | behind hub, Full | 8 ms | 4 ms | 2 ms | 1 ms | 15/15 |
| Windows XP SP3 | XP's own | `10000001` | behind hub, Full | 8 ms | 4 ms | 2 ms | 1 ms | 18/18 |
| XP x64 SP2 | XP x64's | `10000001` | behind hub, Full | 8 ms | 4 ms | 2 ms | 1 ms | 37/37 |
| Vista SP2 x86 | Vista's | `10000001` | root port, High | 4 ms | 4 ms | 2 ms | 1 ms | 30/30 |
| Vista SP2 x64 | Vista x64's | `10000001` | root port, High | 4 ms | 4 ms | 2 ms | 1 ms | 24/24 |
| Windows 7 SP1 x86 | 7's own | `10000001` | root port, High | 4 ms | 4 ms | 2 ms | 1 ms | 27/27 |
| Windows 7 SP1 x64 | 7 x64's | `10000001` | root port, High | 4 ms | 4 ms | 2 ms | 1 ms | 25/25 |

**230 endpoint opens across ten guests, 230 accepted, not one refusal of any
kind and not one interval floored.** Both usbport lineages, both
architectures, both builds of this driver, and the one guest that matters
most - the reporter's own stack - reading the Low-Speed arm the task
changed.

The six behind-hub guests all walk 8, 4, 2 and 1 ms. The four root-port ones
walk 4, 4, 2 and 1, and the doubled first entry is not a fault: a root-port
device is reported High Speed, so stock and 250 Hz are two values inside one
band and 8 ms is not reachable at all. That is issue 6 section 5's bands,
reproduced here on the 24.1 build.

**What the task set out to show, and what it actually established.** The
checkpoint asked for the reporter's three rates on his own stack and no
regression anywhere else. Both are read. But the no-regression half turned
out to carry the stronger evidence for the change itself: every Full-Speed
guest was already being handed `Period` 4, 2 and 1 by its usbport and this
driver was already programming Intervals 5, 4 and 3 for them. The three
values 24.1 newly accepts at Low Speed are not new values - they are the
values the driver has always accepted one speed up. The refusal removed in
`XhciIntervalFromPeriod` was the only thing that made Low Speed different,
and nothing else in the driver treated it differently at all.

---

## 24.2 - Polling rates on a root port

Decided 2026-09-25, on branch `issue4`: **closed as owned by 24.3.** No
narrower change exists. No code changed and no guest was booted; the roadmap
asked for a boot only if a change was found. The owner chose this from two
options (the other is below, under "The one candidate").

### What the reporter asked, and what a root port already gives

GitHub issue 4's item 2 (2026-09-06) was that overriding an interrupt
endpoint's polling rate "is successfully reported back as applied but really
is not applied". Behind a hub that was 24.1's Code 10, and 24.1 fixed it. On
a root port the answer is issue 6 section 5's three bands. Phase 20 measured
them, and 24.1 read them again on the four NT 6.x guests above:

| hidusbf | `bInterval` it writes | `Period` from usbport | Interval | Service |
|---|---|---|---|---|
| 1000 Hz | 4 | 8 | 3 | 1 ms |
| 500 Hz | 5 | 16 | 4 | 2 ms |
| 250 Hz | 6 | 32 | 5 | 4 ms |
| stock, or anything slower | 6 and up | 32 | 5 | 4 ms |

The `bInterval` column is what hidusbf wrote on those guests. hidusbf sees a
root-port device reported as High Speed and uses High-Speed arithmetic for
it, and so **every rate hidusbf offers above the stock one lands exactly on
a root port today.** What cannot be reached is 125 Hz and anything slower,
the device's own stock rate when that is slower than 4 ms, and any
difference between two values in the same band. All of it runs faster than
asked, never slower.

### Why nothing narrower than 24.3 recovers the rest

- **The information is gone before the miniport sees it.** usbport buckets
  a device it believes is High Speed as `1 << min(bInterval - 1, 5)`
  microframes, so every `bInterval` from 6 up arrives as the same `Period`
  32. `USBPORT_ENDPOINT_PROPERTIES` carries no raw `bInterval`
  (`docs/usb-xhci-info/usbport-miniport-abi.md`, "Periodic scheduling: what
  `Period` actually carries"). This holds for every usbport build the
  project targets, SweetLow's included: his rebuild's High-Speed arm is the
  same `dec`, `min 5`, `shl` sequence.
- **The invariants forbid working it back out of `Period`**
  (`docs/contributing/implementation-invariants.md`, the root-port speed
  report). That would be a guess, and the guess is ambiguous by
  construction.
- **The one candidate: the descriptor snoop.** The driver already reads
  each device's `GET_DESCRIPTOR(Configuration)` reply on EP0 to get
  isochronous `bInterval` (`src/xhci_desc.h`), and it could record interrupt
  endpoints the same way. It was rejected for three reasons. First, that
  reply is read below hidusbf, so the snoop sees the **device's own**
  `bInterval` and never the override, which means it cannot deliver what the
  reporter asked for. Second, it would change the other thing: every
  stock Full- and Low-Speed device on a root port, on every target, would go
  from 4 ms to its declared rate (8 ms for a stock mouse). That is the
  slower direction, and nobody asked for it. Third, telling an override
  apart from a stock value would mean comparing the snoop with `Period`,
  and that comparison is the reconstruction the invariants forbid. It also
  fails whenever the override and the stock value share a band. And
  `xhci_desc.h` chose on purpose not to record interrupt intervals, because
  usbport already hands the miniport those.
- **Everything else is 24.3.** A root-port device reported at its true
  speed gets usbport's frame bucketing, which is what the six behind-hub
  guests in 24.1 show: 8, 4, 2 and 1 ms, every rate reachable. Doing that
  without usbport's missing-TT bugcheck (issue 6 sections 3 and 4) is the
  virtual hub proposal, `docs/contributing/design/12-virtual-hub-on-root-ports.md`,
  and that is 24.3's decision. A patched usbport is not this project's to
  ship (24.3).

### For the reporter

On a root port, 1000, 500 and 250 Hz work now, and 1.1.1.0 already behaved
this way. Slower rates and the device's own stock rate need a hub, or 24.3.
Behind a hub, every rate works, Low Speed included, from the 24.1 build on.
Replying on the issue is the owner's.

## 24.3 - a truthful root-port report on SweetLow's stack (an experiment)

Taken 2026-09-25 at the owner's request, before 24.3's decision and as
evidence for it, not as the decision. The question: does the reporter's own
stack survive a true-speed report on a root port, which is the one thing
issue 6 had never run? Issue 6 section 8 carried it as a static reading only
- his `USBPORT.SYS`'s single-TT branch at `0x2667A`-`0x26686` returns the
same `0xFFFFFFEC` for an empty list (ABI document section 8) - with "no
truthful-speed run has been made on that rebuild".

**Answer: it does not. Windows 98 SE under SweetLow's stack goes down exactly
as it does under NUSB.**

### The build, and why it is not in the tree

One change, in `XhciPortShadowReport` (`src/xhci_port.c`): the High-Speed
status bit only for a decoded High-Speed device, the Low-Speed bit for a
decoded Low-Speed one, neither for Full Speed - the Phase 5 override removed
and nothing else touched. The host suite then fails on exactly the eight
vectors that pin the override (five in `test_port.c`, three in
`test_init.c`) and on nothing else, which is the suite confirming the change
is confined to that one layer. Those eight were flipped to the truthful
values so the binary could go through every remaining gate:
`build-driver.cmd qemu`, host tests PASSED, the import gate PASSED. The
`qemu` flavour, `xhci98.sys` 164,944 bytes, SHA-256 `28217287...`, staged by
`make-package.ps1 -Flavor qemu -Arch x86 -OutDir out\t24-3\pkg`. The three
source files were then restored to `HEAD` and the `qemu` flavour rebuilt from
them, so no tree and no output directory holds the experiment except
`out\t24-3\`. **That binary must never be published.**

### The vehicle

- `vm\t24-3-sweetlow.img`: a copy of `vm\sweetlow-2a.img` reverted to
  `sweetlow-stack-nodriver` - SweetLow's stack, no xhci98 of any version.
- `scripts\local\phase24\qemu-sweetlow-24-3.cmd` (per-host, git-ignored):
  24.1's machine (`pc,smm=off`, `pentium3`, 256 MB, `qemu-xhci,p2=8,p3=0`,
  isa-debugcon) with **no hub**, because the topology under test is a device
  directly on a root port; transfer drive `vm\xfer24-3` (24.1's `vm\xfer98`
  with the experimental package in `XHCI98\`), monitor 56891.
- Install through Device Manager -> the controller's Driver tab -> Update
  Driver -> `D:\XHCI98`. The INF's `LayoutFile` route asked for the Windows
  98 SE CD; `C:\WINDOWS\OPTIONS\CABS` on this guest does not carry
  `usbd.sys`, so the CD was inserted (`change ide1-cd0`) and answered with
  `E:\WIN98`, as in 24.1.
- Cold start after the install (the guest hangs on a warm reboot).

### The reading

`xhci98: DriverEntry (built Sep 25 2026 12:43:51)`,
`USBPORT_GetHciMn=10000001` (the XP-lineage value: his usbport, not NUSB's),
`MiniPortExtensionSize=000168A0`, the No Op self-test matched.

| Step | Device | What the driver reported and saw | Result |
|---|---|---|---|
| Control | `usb-kbd` (High Speed, 480 Mb/s), root port 1 | `RH_GetPortStatus` `0x0503` (connected, enabled, powered, High Speed); `QueryEndpointRequirements` and `OpenEndpoint` for address 0, `SET_ADDRESS` answered, the configuration descriptor read | Enumerated; the HID wizard opened for it |
| Under test | `usb-mouse,usb_version=1` (Full Speed, 12 Mb/s), root port 2 | First decode `00010202` (hub port 2, Full Speed); usbhub reset the port (`RH_SetFeaturePortReset` port 2, reset completed, `RH_ClearFeaturePortResetChange` port 2); then **no** `QueryEndpointRequirements` and **no** `OpenEndpoint` for the new device's EP0 | **"A fatal exception 0E has occurred at 0028:C002F70E in VXD NTKERN(01) + 0000E32E"** |

The address is the one issue 6 section 2 records for Windows 98 under NUSB
in Phase 5, `0028:C002F70E` in `NTKERN`. The trace stops where issue 6's
mechanism says it should: after the reset that makes the device creatable,
before usbport opens its default pipe, which is where `USBPORT_CreateDevice`
runs the TT lookup and `OpenPipe` inserts at the garbage pointer. The port
events that followed were latched and announced with nobody servicing them
(`root hub invalidates owed=00000001`), the dead system's signature in this
driver's own counters. Screenshots and both debugcon logs are in
`out\t24-3\` (`b2-fs2.png` is the fatal exception).

What it does not show, stated so it is not read in:

- **The status word usbport read for port 2 is inferred, not read.** The
  per-line trace suppressed those records and the log ring was off (a fresh
  guest, `XhciLogVerbosity` never set), so no `RH_GetPortStatus` value for
  port 2 survives. That the report carried neither speed bit rests on the
  build's own host vectors, which assert exactly that for a Full-Speed port.
- **The branch itself was not observed.** No debugger was attached. The run
  is consistent with the static reading of `0x2667A`-`0x26686` and adds a
  runtime crash at the NUSB address; it does not show which instruction
  produced the pointer.
- **Low Speed was not run.** The lookup is gated on "not High Speed", so a
  Low-Speed device reaches the same branch; the passed-through mouse from
  24.1 would only repeat this.
- **Windows ME**, which runs the same usbport, was not run.

### What the reporter said about a fix

Nothing in the thread is a fix this project can take. His words on GitHub
issue 4: on 2026-09-07, that the High-Speed report "is only default
behaviour", because "USBPORT + USBHUB pair definitely supports Low- and
Full-Speed Devices on Root Hubs for UHCI and OHCI"; on 2026-09-19, "what I
meant when said 'EHCI with right USBPORT, of course'", where the remark it
quotes is in neither issue 4 nor issue 1. The first is true and is issue 6
section 4's other lever: UHCI and OHCI miniports never declare
`USB_MINIPORT_FLAGS_USB2`, so usbport never runs the TT lookup for them, and
dropping the flag costs High Speed on Windows 98 because the root hub then
binds the USB 1.1 hub driver. The second names no usbport and offers none.
The roadmap's 24.3 entry called "a patched usbport that guards the empty TT
list" the reporter's alternative; that reads more into the thread than it
says, and the entry was corrected the same day.

### What it settles for 24.3

A truthful report is now measured fatal on both usbport lineages Windows 98
runs, NUSB's in Phase 5 and SweetLow's here, and on Windows 2000. The report
cannot be made truthful at the root hub on any stack this project holds; the
virtual hub (`docs/contributing/design/12-virtual-hub-on-root-ports.md`,
then still a future-plans page), which keeps the root port reporting High Speed and gives usbport a TT above the slower
device, stays the only candidate.

## 24.3.1 - The design record

Done 2026-09-25, on branch `24.3`. The future-plans page moved to
`docs/contributing/design/12-virtual-hub-on-root-ports.md`, the next free
number (record 10's is not reused), and every link followed it; the
future-plans index keeps one sentence saying where it went. Its section 9
now records the owner's decisions of the same day - the shape, the targets,
the default, the switch's name, the ids and the string - as decisions, and
its header says what 24.3.2 to 24.3.5 build and read.

The one open point the record owned was whether each NT target's hub INF
binds by class, as the 9x ones were already known to. It does, on all of
them. `usb.inf` was taken from each install medium the project holds - the
Windows 2000 SP4 CD's and both XP media's compressed `USB.IN_`, and
`Windows\inf\usb.inf` out of each Vista and Windows 7 `install.wim` - and
kept git-ignored under `tools/<os>-extracted/`. Windows 2000 matches
`USB\HubClass` and `USB\CLASS_09&SUBCLASS_01` / `USB\CLASS_09`; XP in both
architectures, Vista and 7 match the class pair under
`[GenericHub.Section]` with its architecture decoration. None of the eleven
hub INFs on the project's targets names `VID_1209`, so the INF's test id
collides with nothing the targets carry. Method static, a text search
(`legal-provenance.md` section 4); design record 12 section 3.1 carries the
sizes and hashes.

## 24.3.2 - Host vectors

Done 2026-09-25, on branch `24.3`. No boot; host tests only.

**Scope, the owner's.** The roadmap called this batch `-0`, which the batch
table defines as static work producing no driver code, yet its list was
host vectors, and vectors need code to run against. The owner chose a pure
core with its own suite: `src/xhci_vhub.c` and `src/xhci_vhub.h`, DDK-free,
called by nothing, and kept out of `src/sources`, so no binary changes and
rule 2 holds by construction; and `test/test_vhub.c`, registered in
`test\run-host-tests.cmd`, linking `src/xhci_topo.c` for the graph cases.
The vectors that need the driver around the core moved to 24.3.3, whose
roadmap entry now lists them.

**What the core is.** The switch and the two id strings (parsed from
either encoding, with the encoding recorded for 24.3.4's first reading on
Windows 98 and ME); the request table as one function from a SETUP packet
to an answer or a verdict; one 32-byte record per root port holding value
1's decision, the hub's device state and the two views; and every root-hub
event as a function that returns what the caller must do (the existing
reset, disable, power, suspend and resume bodies, an announce, the pipe)
rather than doing it. Setup packets are record 02's measured ones; the
standard `GET_DESCRIPTOR(Device)` length usbport sends has no measurement
in the tree, so it is fed 8, 18, 64 and 255.

**Result.** `test_vhub` 1036 checks, 0 failures; every host suite green,
both amd64 legs included ("Host tests PASSED."); the charset gate clean.
Five mutations of the core - the value-1 root report keeping PORTSC's
enable and reset, a port-1 reset not held while a disable is owed, the
second enumeration reset not suppressed, no forced connect change, a vendor
id of `0000` accepted - failed 3, 12, 71, 3 and 6 checks.

**What writing it found.** Record 12 kept value 1's root report "today's,
bit for bit" but for the suspend pair. Port 1 is the same physical port, so
each port-1 reset - two per device enumeration behind the hub - would have
shown the root port in reset and latched an unsolicited `C_PORT_RESET`, and
each port-1 disable would have left the hub's upstream reading disabled.
Value 2 never had the problem, since its root report is the upstream
view's. A second opinion (Codex) agreed the gap was real and proposed the
bit ownership; the owner took it on 2026-09-25: at 1, on a port in
virtual-hub mode, the enable, reset and suspend groups come from the
upstream view, connection, power, over-current and the High-Speed bit stay
today's, over-current latches in both views, every root reset owns its own
completion by owner and generation, a timed-out re-decision on a hub-mode
port forces the connect change, and `SET_CONFIGURATION` sets the hub's
state. Record 12 sections 3.1 to 3.7, 4, 5 and 8 carry it.

## 24.3.3 - The driver

Done 2026-09-25, on branch `24.3`. No boot; host tests and the build gates
only. Design record 12 section 10 is what the wiring decided and why; this is
what was built and how it was checked.

**What was built.** The pure core joined `src/sources`. The extension gained
the per-port records (`Vhub`), what usbport has bound to each
(`VhubBind`), the switch and ids as read (`VhubConfig`) and thirteen
counters, all after `ImodReadback`; `XHCI_ENDPOINT` gained `VhubPort`. The
root-hub half (`src/xhci_rh.c`) carries out the core's verdicts under the
lock that decided them: the refresh routes a reset's end by its owner and
generation and lets the views absorb what it latched, `RH_GetPortStatus`
reports the upstream view, the six feature callbacks and the change clear
go through the core with the switch applied, the root hub's build stands
the hubs up once per start and keeps them across a resume or a recovery,
and a reset held for a disable starts when the disable is collected. To
carry a verdict out in one lock hold, `xhciRhPortOperation` and
`xhciRhStartOperation` were split into lock-held bodies with the callbacks
wrapped round them, the deferred work still run only where it ran before.
The device half (`src/xhci_slot.c`) takes a hub's endpoint opens before
either opener sees them, answers its default pipe from the request table,
holds its status-change transfer, and handles the abort and the REMOVE of
both; every answer is completed through the ordinary completion list. The
registry read is `xhciVhubRead` in `src/xhci_dispatch.c`, beside the
moderation read, with three start notes; the snapshot header is schema 5,
and `XHCISNAP` was rebuilt to print it. Both INFs write
`XhciVirtualHSHub` as DWORD 0 and the ids as the quoted strings `"1209"`
and `"0001"` on every install path; the INF gate's `VAL-*` rules require
them, with eight new self-test cases, and both footprints were regenerated.
The matrix's offset tables were regenerated with a print site for each new
counter: `SIZEOF` 104,644 on x86 and 111,968 on amd64 (92,320 and 95,560
before), and design record 04's size table follows.

**The vectors.** Six functions at the end of `test/test_init.c`, every
reset in them driven through `RH_SetFeaturePortReset` and the model's own
completion so a virtual hub owns it: rule 2 over the driver (the switch
absent and at 0 give today's report, claim and record, and never read the
ids; a value of 3, a missing vendor id and a five-digit product id each
leave the feature off and say why; a single-byte `0x` pair is accepted);
value 1 end to end on a Full-Speed device (the hub's open told apart from
the device's, the hub's traffic reaching neither snoop, the descriptor
bytes, a serial number stalled, port 1's resets physical with the root port
quiet, the device on today's root-port record, found again across a
repeated reset and a re-open, and a port-1 disable leaving the upstream
enabled); value 1 with a High-Speed device on today's path; value 2's
root-port reset with a transfer in flight on a port that will not drop PED
(the slot, the ring and the transfer kept until the confirmation, a port-1
reset held meanwhile and started when it is collected); the held
status-change transfer found by an abort, answered on a REMOVE, and the
hub dropped with its device at 1 with nothing of its views left latched on
the root port; and a TT pair naming a virtual hub counted as agreed. The
model gained one knob for them, `stuckPortPed`, a port that ignores a
disabling PED write. `test_init` now runs 20,028 checks.

**Mutations.** Eleven driver mutations, each run against the whole suite:
the hub's open never claimed (89 failures), the refresh ignoring a hub's
reset routing (68), the root report not the upstream view (5), value 2's
reset running no disable (7), a held reset never collected (1), the pipe
never completing (16), an abort not finding the held pipe (4), a TT pair
naming a virtual hub counted as a disagreement (3), the ids read with the
switch at 0 (3), the shadow's strip removed (1, after a vector was added
for it: the first run of that mutation passed, because the report masks
those bits while the hub stands, and nothing looked after it was dropped),
and a hub's answer delivered from inside `SubmitTransfer` (32). A first
form of the last - zeroing `SubmitDepth` - also passed, and was not a real
test: the completion list's second gate, the pass epoch, still holds such a
completion, so that mutation removed one of two protections.

**Result.** Host tests PASSED, the amd64 legs included; `build-driver.cmd
all` and `release -amd64` green through every gate with no new import
(the amd64 build's five warnings are `build.exe`'s own notice, as
`run-21.md` records); the INF gate on both files and its 596 self-test
checks; `XHCISNAP`'s self-test.

**Owed.** 24.3.4's readings, the first being which encoding a `REG_SZ`
arrives in on Windows 98 SE and ME (`XHCISNAP` now prints it). 24.7's cut
had to rewrite the download's readme sentence that names snapshot schema 4,
which `make-release.ps1` refuses as stale; the audit below took it.

### 24.3.3, audited (2026-09-25)

A code and document audit of the branch, with a second opinion from Codex,
after the wiring. What it found in the driver:

- **A direct port at 1 kept its decision across an unplug.** `XhciVhubAbsorb`
  returned before reading anything on a port with no hub, so a High-Speed
  device unplugged and a Full-Speed one plugged into the same port met a
  `DIRECT` decision at its first reset, and the core forced the connect
  change of 3.2 - one spurious re-enumeration per such swap, counted in
  `VhubForcedConnects`, where record 12 says "a disconnect forgets the
  decision". The integration vector had pinned the defect as intended. The
  core now forgets a direct port's decision on a disconnect, a lost supply,
  a connect change and usbport's disable or power-off of the port; the
  vectors pin the swap standing a hub up at the first reset with no forced
  change.
- **A refused resume left the view running while the port stayed in U3.**
  The suspend refusal was undone in the root-hub half; the resume refusal
  was not, so usbhub's retry of `CLEAR_PORT_FEATURE(PORT_SUSPEND)` would
  have met a view that already believed itself resumed and done nothing.
  Both views now go back to suspended on a refused resume.
- **`XHCI_VHUB_DO_ARM_DEVICE` was returned and never carried out.** The
  header names it a caller action; the root-hub half now spends the hub's
  arm at a port-1 reset's start as the core does, rather than only at the
  end the PRC path reaches.

And in the vectors: the snapshot header's twelve schema-5 fields read back
through `PassThru`; the disown vector continued through the next port-1
reset, re-open and `SET_ADDRESS` to the same record with its address back;
value 2's cancelled hub install, power-off and power-on through the
callbacks with the hub enumerated again; suspend and resume through the
callbacks for both views, the completed resume stripped from the shadow;
the two suspend orders the core suite had not pinned, remote wake with the
upstream alone and with neither, and `XhciVhubConfigIds` after a refused
switch; the request table's corners (a port request naming port 2 or 0,
the device feature requests, `GET_INTERFACE`, `SET_INTERFACE` on
interface 1, a hub descriptor asked for at `wLength` 0, a NULL reply
length or argument) and the id parser's (the encoding still recorded on a
refusal, "0x" and three digits, four digits with no terminator inside a
four-byte length); and `test_packet` pins the snapshot header at 38
`ULONG`s on both architectures, with a compile-time twin for the amd64
leg, since its layout rests on every field being one. `test_vhub` 1202
checks, `test_init` 20,196, `test_packet` 236.

**Codex's first round over those fixes** (fresh thread, 7 minutes) found an
edge on each of the three driver fixes and two defects beside them, every
one real on inspection:

- The forgotten decision came one reading too late: `xhciRhVhubRefreshed`
  routed a reset's end before absorbing the reading's connect change, so a
  device swapped *inside* a reset (CSC beside PRC, Full Speed decoded on a
  port decided direct) still met the old decision and forced a connect
  change the port already carried. A direct port now absorbs the reading
  first.
- The refused resume was put back and hidden: `XhciRhVhubPort1Feature`
  returned nothing, so the slot half completed `CLEAR_PORT_FEATURE(1,
  PORT_SUSPEND)` with success while port 1 stayed suspended, and usbhub
  would have waited on a `C_PORT_SUSPEND` nothing would send - the wait
  that, on a root port, ended in Vista's 60 s `0xFE` trap in
  `runs/run-22.md` 22.12 (b). A refused suspend or resume now stalls the
  request (record 12 section 3.3), as the root port's own callback returns
  the refusal.
- The hub's arm was spent only on the hub whose port 1 reset; an arm left
  standing on another port by an abandoned enumeration survived the reset
  and its timeout. Spent for whichever port held it, as `XhciSlotPortReset`
  spends it at the end.
- A port-1 disable, a value-2 root disable and a value-2 root reset asked
  for no disable body when PED already read clear - a hardware disable, a
  reset that timed out - so an addressed child kept its address. The body
  is asked for regardless; its software half is the disown.
- `XhciVhubReinit` ended a held reset at value 2 only, while the root hub's
  rebuild clears the shadows at either value, so a reset held at 1 across a
  recovery would have waited for ever. Ended at both.

And three vectors that could not tell: the address-reuse vector disowned a
record the re-open had already put back to address 0 (addressed again
first now); the snapshot vector's twelve fields all held accepted values (a
second window with the switch refused, `test_passthru_snapshot_vhub_refused`);
and a comment in `test_packet` said this host cannot run the amd64 leg,
which it can. New vectors: the swap inside a reset, the refused resume of
each view, the arm spent across ports through a timed-out reset, the
disable after a hardware disable. `test_vhub` 1208, `test_init` 20,381.

**Codex's second round** (the same thread, 10 minutes) found two defects
inside the first round's fixes and one claim too strong:

- The direct port absorbed the reading twice, and the second time forgot
  the decision the reset in between had just taken: a High-Speed device
  decided direct again under a connect change was left with no decision,
  so a later reset decoding Full Speed with no connect evidence would have
  stood a hub up with nothing to flip from and forced nothing, leaving
  usbhub's direct device in place. The reading is spent by the first call
  when the port had no hub.
- A port-1 reset still running when a value-2 root reset (synthetic)
  overtook it ended as port 1's: its PRC armed today's device claim and
  spent the hub's open the root reset had just armed, so the hub's next
  address-0 open would have been served as a device's. The overtaken reset
  is now owned by nobody (`XHCI_VHUB_OWNER_SUPERSEDED`): its end frees the
  port and does nothing else. At 1, where the root reset is physical, the
  same case had the root reset overwrite port 1's ownership before the port
  refused it as busy; the write is now asked for with nothing changing
  hands, and usbport gets the busy refusal as for a root port's second
  reset today.
- "usbhub asks again" after a stalled resume was more than the stall
  guarantees: Vista's `UsbhResumeSuspendedPort` reports the failure and
  signals its resume event with no retry of its own (static, over the local
  hub binary). The record, the comments and the vector now say a request
  that follows is served.

New vectors: the direct decision standing under a connect change and the
flip forced by the next speed-changing reset; the root reset over a running
port-1 reset at both values, the hub's open surviving the stale end at 2 and
the busy refusal at 1. `test_vhub` 1255, `test_init` 20,479.

**Codex's third round** (the same thread, 6 minutes) found three more,
two of them corners of the overtaken reset and one of recovery at 1:

- The overtaken reset's end was nobody's only while its deadline had not
  passed: the deadline cleared its ownership, and a PRC arriving after it -
  a reset nothing armed any more - took today's path, arming a device claim
  for a reset usbhub had given up on and spending the hub's open. A PRC
  nothing armed on a port carrying a hub is now nobody's (`vhub.prc.late`).
- The value-2 root reset's disable wrote PED into a port in reset, where PED
  is already 0 and a '1' clears nothing; when that reset finished before
  the confirmation read, the debt stood against a port that had enabled
  itself again, the poll waits only for a clear, and the next port-1 reset
  was held behind it for good. A reset that ends and leaves the port
  enabled with a disown owed has the disable written again
  (`vhub.redisable`).
- A recovery at 1 with a configured hub whose device had gone: HCRST took
  the disconnect's CSC, an empty port raises none, and the hub stayed with
  its bindings and held transfer while usbhub was never told. The lost
  bitmap now covers value 1 (a hub present is a device that was there), and
  the seed latches the root port's connect change for such a port itself
  (`vhub.lost`), so usbhub removes the hub as on any unplug and usbport's
  disable drops it; record 12 section 3.2 says so.

New vectors: the overtaken reset through its deadline and a late PRC, with
the disown the reset swallowed written again and the next port-1 reset not
held; the recovery at 1 with the device gone. `test_init` 20,535.

**Codex's fourth round** (the same thread, 6 minutes) found one: a second
reset asked for while the first still ran took the ownership before the
port refused it as busy, so the first reset's end - the one that decides,
at 1 - decided nothing, and a Full-Speed device's root reset ended as
today's: the device opened direct, which is issue 6's bugcheck. The same
for a second port-1 reset over a running one. The core now takes no reset
while one runs on the port (the request is asked for untaken, refused as
busy, and stalled for port 1 as a root port's second reset is refused
today), the refusal's own end is matched by generation so it changes
nothing, and the running reset keeps its owner. Vectors in the core and
through the driver, both views. `test_vhub` 1279, `test_init` 20,552.

**Codex's fifth round** (the same thread, 4 minutes) found one inside the
fourth's fix and nothing beside it: the stall for a port-1 reset the core
declined was decided after the carry, which also ends a reset the core
took and the port then refused (busy with a resume) - so that one, meant
to complete with port 1's reset change and the port disabled, stalled too.
Decided before the carry now; vector for the taken-and-refused shape.
`test_init` 20,561.

**Codex's sixth round** (2 minutes): no findings, over the commit and over
the whole of the loop's diff read as one body - the second clean
independent pass in a row, which is where the loop ends. Six rounds over
six commits; every finding of the first five was real on inspection, and
each of rounds two to five found a defect inside the round before it -
the shape every review loop in this repository has had, and the reason a
round without a MAJOR is not taken as convergence. What the loop leaves:
`test_vhub` 1279 checks, `test_init` 20,561, `test_packet` 236; the
three log notes `vhub.prc.late`, `vhub.redisable` and `vhub.lost` beside
the wiring's `vhub.create`, `vhub.drop`, `vhub.flip`, `vhub.open` and
`vhub.address`; and 24.3.4's readings still owed on every guest.

## 24.3.4 - The readings on every guest held

### The two vectors the audit owed (2026-09-27)

Before any guest: the two `test/test_init.c` vectors the audit of
2026-09-25 left under this task, since only the core suite pinned what
they read.

- `test_vhub_always_unplug_is_port1s_change` - hub-path removal at 2. A
  Full-Speed device behind port 1, addressed, the status-change transfer
  held; the unplug completes it with port 1's bit, latches port 1's
  connect change and nothing on the root port, which still reports its
  hub connected, enabled and High Speed; the device's record is torn down
  by the connect-change path and released once its slot is disabled; the
  hub keeps its address, bindings and configuration; and the next device
  on the port enumerates behind it in a slot of its own, at the address
  the first one gave back.
- `test_vhub_always_recovery_keeps_the_hub` - a recovery at 2 with that
  hub configured. HCRST leaves the port with no CSC; the held transfer is
  completed for the device the reinitialisation took, port 1 carries the
  connect change and reads disabled, an empty hub on another port gains
  none, the hub is kept whole and none is stood up or dropped, the root
  port latches nothing, and the device enumerates again behind the kept
  hub.

Writing the second found a seam in the model rather than the driver: its
event producer kept its enqueue index and cycle across the recovery's
re-initialisation of the event ring, so every event after a recovery was
written where the driver was not reading. The model now does what an
ERSTBA write does - "The xHC initializes its internal PCS flag to '1'"
and its enqueue pointer to the first segment (4.9.4, p.167) - and no
other vector's outcome moved. Five driver mutations were each caught by
the new vectors: the reinitialisation's port-1 connect change dropped, its
pipe verdict dropped, `lost[]` never set at 2, the pipe verdict not
carried at start, and the connect-change teardown skipped at 2; a sixth,
the absorb's strip from the root shadow dropped, is caught by existing
ones. `test_init` 20,700 checks; host tests, all three x86 flavours and
the amd64 build green, the offset tables unchanged (SIZEOF 104,644 x86,
111,968 amd64).

### The vehicle (2026-09-27, host `minis-w11p-ykm`)

The `qemu` flavour of `078a0dc` (`DriverEntry (built Sep 27 2026
11:12:13)`), staged by `make-package.ps1 -Flavor qemu` for both
architectures and installed on **fresh images only**, by the owner's word
that day: `win98.img` reverted to `post-nusb` (NUSB 3.3, no xhci98),
`sweetlow-2a.img` to `sweetlow-stack-nodriver`, `winme.img` to
`winme-sweetlow-nodriver`, `win2k-xonly.img` to `win2k-xonly-clean-install`,
and the XP, Vista and 7 guests as overlays on their `*-clean-autologon`
state. Every install was the INF's own device install (the Update Driver or
Found New Hardware wizard, or `pnputil -i -a` on Windows 7), so the three
values reached the registry through `AddReg` as a user's would. A first
attempt on `win98.img`'s current state, whose `Sep 3` build was replaced
with Update Driver in place, took a fatal exception 0E at `0028:C0031B0A`
as the running controller was restarted; that is the known Windows 98 limit
(`build-and-test.md`, "Do not disable the controller in Device Manager"),
not a reading of this build, and the overlay was discarded.

`qemu-xhci,p2=8,p3=0`, no QEMU hub unless a reading says so, devices
hot-plugged from the monitor: `usb-mouse,usb_version=1` (Full Speed),
`usb_version=2` (High Speed), `usb-audio` with the `wav` backend, and the
owner's Low-Speed `046d:c077` by `usb-host` passthrough. Every value was
read from its own cold boot, the switch set with a REGEDIT4 import into the
driver's class key (instance `0002` on the two 98 SE images, `0000`
elsewhere). The readings come from the note ring and counters through the
monitor, the probe's TT pair table, `XHCISNAP` dumps, and screenshots.
Churn is 25 cycles of `device_add`/`device_del` of a Full-Speed mouse on
one root port.

### The first reading: how a `REG_SZ` arrives through usbport's service

**UTF-16, on every stack read.** `XHCISNAP` at 1 printed
`XhciVirtualHSHubVid read, accepted, arrived as UTF-16, 1209` and the same
for `0001` on Windows 98 SE under NUSB 3.3, under SweetLow's stack, on
Windows ME, and on Windows 2000; `vhub.ids=12090001` in each ring. The
parser's single-byte arm is therefore not exercised by any target read,
and the question record 12 section 3.1 left open for 9x is answered: the
9x path through NTKERN hands usbport's `GetMiniportRegistryKeyValue` the
same wide string the NT path does. (runtime)

### Windows 98 SE, NUSB 3.3 (`USBPORT_GetHciMn=57324B30`)

| Value | Reading | Result |
|---|---|---|
| 0 | switch notes | `vhub.switch=0`, `applied=0`, `ids=0` (never consulted) |
| 0 | FS mouse, root port 1 | High Speed, Interval 5 (`ep.open.ival=00030005`), as 24.1; pointer moves |
| 1 | first hub install | Add New Hardware wizard, "Generic USB Hub", bound from NUSB's `USB2.INF` by class; no CD asked |
| 1 | FS mouse behind the hub | Full Speed, `Period` 8, Interval 6 (8 ms, `00020006`); pointer moves |
| 1 | TT record | pair `HubAddr 1, port 1` for the mouse (addresses 0 and 2); the hub's own opens `0xFFFF` |
| 1 | High Speed | direct, no hub, Interval 5 |
| 1 | churn | 26 hubs stood up and 26 dropped, 26 slots enabled and disabled, 156/156 opens, no refusal, stall, held reset or forced connect |
| 2 | start | 8 hubs, addresses 1-8, each configured (24 hub opens); one wizard per new port, no CD |
| 2 | FS mouse behind hub 1 | Interval 6, no wizard, pointer moves |
| 2 | HS mouse behind hub 2 | works; **`Period` 1, Interval 0** (see the findings) |
| 2 | churn | 25 slots in and out, 8 hubs throughout, 105/105 opens, 107 pipe completions |
| 2 | hub disabled and enabled in Device Manager | the held status-change transfer aborted; on enable the hub enumerated again (3 opens) with no controller restart |
| 2 | Power tab | "The hub is self powered", 500 mA per port, "1 port(s) available" |

### Windows 98 SE, SweetLow's stack (`USBPORT_GetHciMn=10000001`)

The same table, reading for reading, with the hub installed as "Generic USB
Hub (EHCI)" from SweetLow's INF. The line that matters on this stack: **a
Full-Speed mouse on a root port at 1 enumerates behind the virtual hub and
works**, where a truthful root-port report took the fatal exception at
`0028:C002F70E` (24.3's experiment, above). Churn at 1: 26/26 hubs, 26/26
slots, 156/156 opens; at 2: 26/26 slots, 8 hubs, 102/102. The TT table at 2
shows all eight hubs on their root ports at `0xFFFF` and the mouse under
`HubAddr 1`.

**Low Speed, by passthrough** (the owner attached the `046d:c077` that
afternoon): at 1 the port decoded Low (`port.connect=00000101`), the reset
stood a hub up (`vhub.create`), and the mouse opened behind it at Low
Speed, `Period` 8, Interval 6 (`ep.open.ival=00010006`), TT `HubAddr 1,
port 1`; at 2 the same behind hub 1. The HID class driver then failed its
`SET_IDLE` (`0x210A`): QEMU's `usb-host` got status `-1` from libusb, the
guest saw a Transaction Error on EP0, and the HID driver unconfigured the
mouse. On this host the mouse is bound to Windows' `HidUsb`/`mouhid`, not
to WinUSB as on 24.1's host, where the same request completed with status
0. So the pointer was not moved by it; the speed, interval and TT
readings stand, and a pointer reading wants the device bound to WinUSB
(Zadig) first.

### Windows ME, SweetLow's stack

The same readings; the hub installs silently on the first plug, and at 2
all eight at boot with no user action. 0: High, Interval 5. 1: UTF-16,
Interval 6, TT `HubAddr 1`, churn 26/26 hubs, 156/156 opens. 2: 8 hubs,
Interval 6, High-Speed behind a hub `Period` 1, churn 26/26 slots, 105/105.

### Windows 2000 SP4

The same again, the hub bound silently by SP4's own `usbhub.sys`: 0 High,
Interval 5; 1 UTF-16, Interval 6, TT `HubAddr 1`, churn 26/26, 156/156;
2 eight hubs at boot, Interval 6, High-Speed behind a hub `Period` 1, churn
25 slots in and out, 105/105.

**Hub tiers.** At 2, four QEMU hubs chained behind the virtual hub on root
port 3 with a mouse at the end (route `0x1111`) enumerated; a fifth hub
enumerated too, but a mouse behind it - tier 8 if the virtual hub counts -
never reached slot enable. At 0 the same chain enumerated that mouse
(route `0x12111`, Interval 6). **SP4's usbhub counts the virtual hub as a
tier**, so at 2 a chain of five real hubs loses its last level, the cost
record 12 section 5 named. Tearing the chain down released all six
records (`BehindHubGone` 5).

### Windows XP SP3 x86 (`USBPORT_GetHciMn=10000001`)

WHPX on this host boots XP to a black screen that never draws a desktop
(one run's QEMU exited); XP was read under TCG. 0: High, Interval 5. 1: the
hub is enumerated twice (addresses 1 then 2, XP's re-create), installs
silently ("Found New Hardware: Generic USB Hub"), mouse Interval 6, TT
`HubAddr 2`; churn at 1 gave 26 hubs but 24 slots at a 12-second pace -
two cycles unplugged before XP's double hub enumeration finished; five
more at 25 seconds all reached a slot (163/163 opens overall, no refusal).

**Full-Speed audio on a root port plays.** At 1, a `usb-audio` on root port
1 behind its virtual hub: the isochronous endpoint opened at Full Speed,
Interval 3, and a `sndrec32 /play` of "Windows XP Startup.wav" drove 82
submits, 820 packets, the `wav` oracle growing to 102,400 bytes. Issue 6
section 7's reading on a root port was 0 submits and 0 bytes. At 2, on one
vCPU: 568 submits, 5,680 packets, all answered, 2 ring underruns, the
`wav` file growing steadily. At 2 also: the High-Speed mouse behind hub 4
`Period` 1, Interval 0; churn 25 slots in and out, 8 hubs, 114/114 opens.

The first boot at 2 ran on four TCG vCPUs, the configuration the owner
ruled unrepresentative for 32-bit XP on 2026-09-15, and on it the
controller failed during playback: `interrupter re-arm: IE did not come
back up`, a re-arm escalation, usbport's `ResetController`, and three
recovery deliveries lost, leaving the controller down (244 transfers
failed "endpoint gone"). The same boot's first plug of the audio device
was reset four times and never opened; a replug enumerated it. Neither
recurred on one vCPU. They are recorded, not read as evidence either way
(see the findings).

### Windows XP Professional x64 SP2 (TCG, four vCPUs)

0: High, Interval 5, pointer moves. 1: the hub enumerated twice as on XP
x86, mouse Full Speed Interval 6 behind `HubAddr 2`, dropped on unplug; a
High-Speed mouse direct at Interval 5; **audio plays** (usb-audio on port 3
behind its hub: 482 submits, 4,820 packets, all answered, the `wav` oracle
+675,840 bytes). Churn at 1: 26 hubs stood up and dropped, 144/144 opens,
no refusal or controller reset, but **23 of 25 cycles reached a slot** and
6 of those were never addressed; a recheck at 30 seconds a cycle gave 4 of
5. In each miss the hub stood up, was addressed and opened its pipe, and
XP's hub driver then reset port 1 four times and never opened the device's
address 0 - no transfer error, no refusal; the next plug enumerated. XP
x86 showed the same shape twice (two churn cycles, and the first audio
plug at 2 on its four-vCPU boot), which reads less like a pace too fast
for XP and more like an intermittent enumeration failure behind an
on-demand hub on NT 5.x; not diagnosed. At 2: 8 hubs at boot; Full-Speed
mouse behind hub 1 at Interval 6; High-Speed behind hub 4 `Period` 1,
Interval 0; audio +712,704 bytes (482 submits, 4,820 packets); churn 25 of
25, 161/161 opens.

### Windows Vista and 7, both architectures: stopped

The four NT 6.x guests installed, and read correctly at 0 (Full-Speed mouse
on a root port, all opens accepted, pointer moves). At 1 and 2 they did not
get far, for three reasons, and the task stopped there for the owner.

1. **QEMU does not disable a port.** Vista's and 7's hub drivers clear
   `PORT_ENABLE` on port 1 after its first reset. The driver writes PED=1
   (`usb_xhci_port_write port 1 ... val 0x00000603`) and QEMU reads back
   `0x603`: its model has no PED write-to-disable (xHCI 1.2 Table 5-27;
   upstream `xhci_port_write` omits PED, per the review below). The disable
   is never confirmed, and every later port-1 reset is held (record 12
   section 3.3's gate, which has no deadline by the owner's decision of
   2026-09-27): "USB Device Not Recognized", Code 43. It stops every
   Full-Speed device behind a hub at 1 on all four, and at 2 a High-Speed
   one behind hub 4 on Windows 7 x86. Windows 98, ME, 2000 and XP never
   disable a connected port mid-enumeration and never met it.
2. **A virtual hub outlives its device on NT 6.x.** After the stuck mouse
   was unplugged, Windows 7's hub driver removed the hub (its held
   status-change transfer was aborted) but never called
   `RH_ClearFeaturePortEnable` for the root port - the one path that drops
   a hub at 1 (record 12 section 3.2). The record stayed with address 1;
   usbport gave address 1 to a High-Speed mouse on root port 2, whose EP0
   was then bound to the stale hub, and Windows installed a "Generic USB
   Hub" `USB\VID_1209&PID_0001` there instead of the mouse (Windows 7 x86
   and x64 alike). On 98, ME, 2000 and XP every unplug brought the disable.
3. **A deadlock, in 24.3.3's code.** Windows 7 x64 at 2 hung when a
   High-Speed mouse was plugged behind hub 4, and Vista x64 at 1 on an
   unplug: two CPUs spinning on a lock in usbport's FDO extension. The
   symbolised stack (amd64 `qemu` PDB) is `XhciSlotSubmitTransfer+0x15c` ->
   `XhciRootHubDeferredWork+0x81` -> `xhciRhAnnounce+0xba` -> usbport ->
   spin: the virtual hub's branch of `SubmitTransfer`
   (`src/xhci_slot.c`, the `XHCI_ENDPOINT_FLAG_VHUB` test), which usbport
   calls under its miniport spin lock, runs the root hub's deferred work,
   and that calls `USBPORT_InvalidateRootHub`, which on NT 6.x takes the
   lock again. NT 5.x's usbport evidently does not. This is not a QEMU
   artefact and would hang Vista and 7 at 1 or 2 on metal.

### Resume and recovery: not reachable here

Windows 98 SE under NUSB at 2, with a Full-Speed mouse behind hub 1 and a
High-Speed one behind hub 2, refused Stand by ("Your computer cannot go on
standby because a device driver or program won't allow it"). That is the
QEMU limit `lessons.md` already records - no guest here produces a
suspend/resume pair, because the display driver vetoes S3 - and nothing in
the guest can raise a host controller error to force a recovery in place.
The resume and recovery clauses at 1 and 2 (the hubs kept, the devices
re-enumerated behind them, a swapped device's forced connect change) rest
on the host vectors - 24.3.3's and the two taken above - and on 24.3.5's
E460.

### Findings the readings leave for the owner

In the order of severity on real hardware (the first two confirmed by a
Codex review of 2026-09-27, `.claude/codex-2434-heldreset-r2-result.txt`,
with static reads of Vista x64's and Windows 7's `usbport.sys` and
`usbhub.sys`):

A. **The deadlock** (Vista and 7 section, item 3). The chain is
   `SubmitTransfer` -> `xhciVhubSubmit` -> `XhciRhVhubPort1Feature` ->
   carry -> `XhciRootHubDeferredWork` -> `xhciRhAnnounce`; on Vista x64
   `USBPORTSVC_InvalidateRootHub` (`0x2990C`) reaches
   `USBPORT_AcquireEpListLock` (`0x7050`), the lock at FDO `+0x1160` that
   `SubmitTransfer` runs under - the captured address. Windows 7 x64 the
   same at FDO `+0xF88`. The repository's safety argument for calling the
   service there was ReactOS-derived (`usbport-miniport-abi.md`), not read
   against these binaries. (static; the stack runtime)
B. **The stale hub** (item 2). A disconnect clears the hub's upstream state
   and decision but keeps `Present` and `Address` (`src/xhci_vhub.c`
   `XhciVhubAbsorb`), and `XhciVhubFindAddress` tests only those; Windows
   7 x86 `usbhub!UsbhPortDisconnect` (`0x29967`) and Vista x64's
   (`0x2A5D8`) remove the device without a port disable, so an ordinary
   unplug of a working device reaches it on metal. (static; runtime)
C. Two orderings the review found by reading, not observed: a held port-1
   reset survives a later port-1 disable and can undo usbhub's decision to
   abandon the device; and a disable landing during a real asynchronous
   reset is taken as confirmed (PED 0 without PR excluded).
D. **The QEMU disable gap** (item 1), with the owner's no-deadline hold:
   an emulator artefact, but record 12 section 3.3's "revisited only if a
   24.3.4 reading shows a hold that never clears" is met.
E. **An intermittent enumeration miss behind an on-demand hub on NT 5.x**
   (XP x86 and x64, about one plug in ten at 1): usbhub resets port 1
   four times and never opens the device. Not diagnosed.

Then, from the earlier guests:

1. **A High-Speed interrupt endpoint behind a virtual hub gets `Period` 1,
   programmed as Interval 0 (125 us).** Seen at 2 on NUSB, SweetLow's,
   ME's, SP4's and XP's usbport; the same device directly on a root port at
   1 gets `Period` 32, Interval 5. Record 12 section 7 says High-Speed
   devices at 2 keep "the same interval". Likely mechanism, read in ReactOS
   only (static): usbport's USB 2.0 budgeter overwrites `Period` with the
   budget's `ActualPeriod` and carries the microframe choice in
   `InterruptScheduleMask`, which an EHCI miniport uses and this driver
   does not. If so, a High-Speed device behind any real USB 2.0 hub is
   polled at the same rate today. The devices work; nothing was changed.
2. **`XhciRearmInterrupter`'s read-back can lose to an ISR on another
   CPU.** The four-vCPU XP boot above: IE written, and cleared again by a
   concurrently claimed interrupt before the read-back, three times; the
   escalation ended in a controller the recovery could not bring back.
   Under TCG, on a configuration the owner has ruled out; whether real SMP
   hardware can reach it is not read.
3. **The passthrough Low-Speed mouse's pointer reading** needs the device
   on WinUSB on this host.

### The fix batch: findings A, B and C (2026-09-27, owner: "fix 1+2 now")

Driver changes, all in the virtual hub's paths; with the switch at 0 the
driver is unchanged (rule 2). Design record 12 section 11 carries the
rules; the static facts are in `legal-provenance.md` section 4.

- **A.** The virtual hub's `SubmitTransfer` branch runs
  `XhciRootHubDeferredArms` instead of `XhciRootHubDeferredWork`: owed port
  timers are armed at once and the announcement is left owed for the event
  DPC, the health poll, a root-hub callback or a port timer. It was the
  only route from an EpList-held callback to root-hub code. (The first
  draft justified the arm by the Ex service's own lock; Codex's review
  below showed the legacy service skips it.) A first cut that also deferred the arms failed 14 checks: a resume
  through port 1 is ended by its timer, so deferring that arm stretched it
  to the next health poll.
- **B.** At 1, `XhciVhubAbsorb` retires the hub on a reading with no
  connection or with a connect change, clearing it as usbport's disable
  did and carrying `DROP` (bindings, held transfer); a port-1 reset in
  flight stays recognised as nobody's, and its end is stripped from the
  root port's shadow. usbport's root disable and power-off at 1 retire the
  same way. The recovery seed samples `Present` before its
  refresh, which now retires the hub itself.
- **C.** A port-1 disable or power-off ends a held port-1 reset (port 1's
  `C_PORT_RESET`); and on a port carrying a hub the disable confirmation
  refuses PED 0 with PR 1, the redisable at the reset's end then leaving
  port 1 disabled and arming no claim.

Vectors: `test_vhub` 1279 -> 1381 (`testHeldResetEndsAtPort1Disable`,
`testUnplugRetiresV1`, and the unplug and held-reset rows rewritten - the
second had pinned finding C's first ordering as intended); `test_init`
20,700 -> 20,906 (`test_vhub_submit_never_announces`,
`test_vhub_on_demand_unplug_retires_the_hub` - address 1 reused by a
High-Speed mouse on another port, then a replacement hub and the old
hub's late transfer, abort and REMOVEs -
`test_vhub_disable_inside_a_port1_reset`,
`test_vhub_unplug_inside_a_port1_reset`, a model knob for a reset that
clears PED as the specification says, and a never-reset net:
`UsbPortInvalidateRootHub` never with `SubmitDepth` nonzero). Two older
vectors were corrected where they described the removal route as
usbport's disable. Nine mutations, one per rule, each fail at least one
check (the retire on a connect change alone, and the retire keeping a
running reset as nobody's, needed the last two vectors to be caught).
Host tests and `build-driver.cmd all`, `qemu -amd64` and `release -amd64`
passed with every gate.

**Codex, round 1** (`--fresh`, `.claude/codex-2434-fix-r1.txt`, result
beside it): A's announcement separation sound, with no other route from
the five EpList callbacks to an EpList-taking service (`InvalidateEndpoint`
is assertion-only on all four NT 6.x builds); B's ordinary lifetime and the
replacement-hub isolation sound; C's two port-1 cases sound; switch 0
unchanged. Four findings, all real:

1. **MAJOR, older than 24.3 - roadmap 24.4 by the owner's decision.** NT 6.x's legacy
   `UsbPortRequestAsyncCallback` calls `RequestAsyncCallbackEx` with its
   seventh argument 1, which skips the timer-list lock; the service assumes
   its caller holds it, as usbport does only around its root-hub feature
   callbacks and its timer DPC (which holds it across the miniport's
   callback). Every arm this driver makes elsewhere - the command watchdog
   from the submit pump, the DPC and the poll; the DPC's device-initiated
   resume; the health poll's arms; now the virtual hub's submit, as before
   the batch - races the DPC's unlink and free on another CPU. Re-read on
   Windows 7 x64 with `kd` (static). Switching to the locking Ex branch
   from `SubmitTransfer` would add EpList -> timer lock against the timer
   DPC's timer lock -> EpList (a callback that announces), so the fix is a
   design decision about where the driver may arm on NT 6.x, not a
   one-line change. Not changed.
2. **MAJOR, new.** A retired port-1 reset whose deadline passed before its
   PRC: the late PRC became a root reset and a device claim, spending
   another port's armed hub open. Fixed: `XHCI_VHUB.LateEnd` (a reserved
   byte) marks a port-1 or nobody's reset that timed out, survives the
   retirement, and makes the late PRC nobody's.
3. **MINOR, new.** The unplug and the port-1 reset's end in one reading
   leaked `C_PORT_RESET` to the root port and armed a claim. Fixed: the
   strip and the claim suppression moved to the end of the refresh, after
   the retirement.
4. **MINOR, older.** A root disable inside a port-1 reset at 1 cleared the
   record before the confirmation, so the PR rule did not apply. Fixed:
   the root disable and power-off retire through `xhciVhubRetire`, and the
   PR rule and the redisable also cover a record that still owns a reset
   or a late end.

Vectors added for all three fixes and for Codex's two coverage asks (the PR
rule at 2 through the driver; a retirement inside a SET_PORT_FEATURE with a
held status-change transfer): `test_init` 21,170. Sixteen mutations now,
each failing at least one check.

**Codex, round 2** (`--resume`, `.claude/codex-2434-fix-r2.txt`, result
beside it): the three round-1 fixes sound, finding 1's corrected text right
but for one word. Two more, both real:

1. **MAJOR.** A recovery or a reinitialising resume while a retired hub's
   port-1 reset was out left the absent record owning it:
   `XhciVhubReinit` returned early for a record with no hub. The next
   device's root reset was taken for the old one - no decision, its
   `C_PORT_RESET` stripped. Fixed: the reinitialisation lets go of every
   reset a hub-less record owns, and of `LateEnd` (which also closes the
   older case of a direct port's root reset surviving a recovery).
2. **MINOR.** The widened PR rule held a debt on a direct port whose own
   root reset a root disable landed in, and nothing would redisable it.
   Fixed: the rule covers a hub, a port-1 or nobody's reset, or a late end;
   a direct port's own reset is today's.

And two notes, taken: the legacy timer service does take one lock, the
I/O-count lock at FDO+0x200, so "no lock at all" became "neither the
timer-list lock nor EpList"; and the retirement-inside-a-request vector now
counts both completions and checks each status. New vectors for both
fixes and for a root power-off inside a port-1 reset followed by a new
device: `test_init` 21,291. Nineteen mutations, each caught.

**Codex, round 3** (`--resume`, `.claude/codex-2434-fix-r3.txt`, result
beside it): round 2's fixes sound, and an independent pass over the whole
diff found nothing further - except one defect inside round 2's own fix:

1. **MAJOR.** A successful Controller Restore State keeps PORTSC, so a
   retired hub's port-1 reset may already have ended, its PRC waiting for
   the seed, or may end afterwards; clearing `LateEnd` at every
   reinitialisation made that PRC a root reset and a claim. Fixed:
   `XhciVhubReinit` takes `restored` (from `XhciRootHubInit`'s
   `afterRestore`): after HCRST every reset and late end is forgotten; after
   a restore a port-1 or nobody's reset becomes a late end.

The vector for it exposed a **model seam**: the model's event producer went
back to ERST[0] on the ERSTBA write the restore makes before CRS, where a
conforming controller's CRS restores its saved internal enqueue - so every
event after a restore was lost in the model, and a check that the restored
controller ignored a PRC passed for the wrong reason. The model now saves
the cursor at CSS and restores it at a successful CRS; every existing
restore vector still passes. `test_init` 21,388; twenty-one mutations, each
caught.

**Codex, round 4** (`--resume`, `.claude/codex-2434-fix-r4.txt`, result
beside it): round 3's fix and the model correction sound - the
specification's event-ring note says CRS overwrites ERSTBA's initialisation
with the saved enqueue state - and the independent pass found nothing else.
One MINOR interaction with the older per-tenancy rule, which clears every
disown debt when a restore rebuilds the shadows (`XhciRootHubBuild`): a root
disable inside a port-1 reset, then a restore, and the reset's end left the
hub-less port enabled under usbport's disable. Fixed narrowly: on a port
whose hub has retired, a nobody's reset that ends with the port enabled is
disabled again whether or not the debt survived. Vector
`test_vhub_restore_between_disable_and_reset_end` (the PRC after the seed,
and pending at it); `test_init` 21,480; twenty-two mutations.

**Left for the owner, with the timer-lock finding above:** the same
per-tenancy clear also drops a port-1 disable's debt across a restore on a
port whose hub stays; the reset's late end then leaves the physical port
enabled while port 1 reads disabled, and the record behind it keeps its
slot until the port's next reset or connect change. It is the older rule
working as written for every port at every value, and reconciling it would
mean carrying a debt into the next tenancy, which that rule exists to
prevent. Codex also suggested a model vector for an event-ring wrap with a
saved PCS of 0 across a restore; not written.

**Codex, round 5** (`--resume`, `.claude/codex-2434-fix-r5.txt`, result
beside it): **clean** - no MAJOR or MINOR finding; the hub-less redisable
cannot reach a port a replacement device's accepted reset enabled (that end
is the root's own), a swapped device's reset is refused busy while the old
one is armed, and switch 2 and direct ports are untouched. Its one note, a
negative vector for exactly that replacement case, was taken
(`test_vhub_replacement_after_a_late_end_keeps_its_port`): `test_init`
21,523. The review loop over the fix batch converged at round 5, with the
two items above left for the owner.

Not yet read in a guest: the Vista and 7 readings are owed again on this
build, with the spot re-checks on 98 SE and 2000 (the handoff's list), and
finding D still stands in QEMU.

**The owner's decisions (2026-09-27), on the three items left open.**
Codex round 1's item 1, the NT 6.x timer-arm race, is recorded and taken
as its own task, roadmap 24.4 (24.5 until 2026-09-28), taken before the cut (now 24.7): a static
read of the locks usbport holds in each context that reaches this driver
first, then the choice of where the driver may arm on NT 6.x; no code
changes for 24.3.4. The disown debt a
restore drops on a port whose hub stays is the per-tenancy rule in
`XhciRootHubBuild` working as written, and stays: carrying the debt across
a restore would need proof that the same device and reset survived the
suspend, which the driver cannot have, and the rule exists so that a debt
is never confirmed against a live device of the new run. The cost it
leaves - the physical port enabled while port 1 reads disabled, and a
record holding its slot until the port's next reset or connect change -
needs a successful CSS/CRS inside a port-1 reset, so bare metal only.
Finding D, QEMU's missing port disable, is accepted as a gap: Full- and
Low-Speed devices behind a virtual hub on NT 6.x are read on the E460
under Windows 7 x86 (roadmap 24.3.5), and no emulator is patched.

### The readings on the fixed build (2026-09-27/28)

The `qemu` flavour of `1a2edc2` plus a comment-only change (`DriverEntry
(built Sep 27 2026 22:21:42)` x86, `22:27:02` amd64; `xhci98.sys` sha256
`1d8ec911...` and `a012b33c...`), staged by `make-package.ps1 -Flavor qemu`
and installed on **fresh images** again: `win98.img` reverted to
`post-nusb`, `win2k-xonly.img` to `win2k-xonly-clean-install`, and the four
Vista and 7 guests as new overlays on their `*-clean-autologon` state, the
driver installed with `pnputil -i -a` (x64 on an F8 boot with signature
enforcement disabled). Each value from its own cold boot, the switch
written with `VHUB98.BAT` / `VHUBNT.BAT`; readings from the note ring, the
counters and the TT table over the monitor, and screenshots. Recipe and
per-guest reports: `out\t24-3-4\recipe-2434-r2.md`,
`out\t24-3-4\r2-<guest>-report.md` (git-ignored).

**Windows 98 SE (NUSB 3.3) and Windows 2000 SP4, the spot re-checks: both
pass at 1 and at 2**, as in the first readings. At 1 a Full-Speed mouse on
root port 1 stands up a hub (`vhub.create`), sits behind it at Full Speed,
Interval 6 (`ep.open.ival=00020006`), with its TT pair naming the hub
(`HubAddr 0x0001 port 1`); the unplug retires the hub (`vhub.gone`,
`vhub.drop`); a High-Speed mouse on port 2 is direct at Interval 5; churn
of 25 cycles gives `VhubCreated` and `VhubDropped` +25 each, slots enabled
and disabled +25 each, every endpoint open accepted (98 SE 159/159, 2000
159/159), no refusal and no `ResetControllerCalls`. At 2 eight hubs stand
up at start (`VhubOpens` 24), a Full-Speed mouse works behind hub 1, and
churn of 25 on port 5 keeps `VhubCreated` at 8 with slots +25/+25 (98 SE
opens 167/167 on a clean rerun, 2000 105/105). 98 SE's first churn at 2
reached 17 slots of 25 because Windows 98's own "USB Human Interface
Device" install wizard, raised for the new device instance and waiting on
the CD for `hidclass.sys`, held enumeration for eight cycles; the driver's
counts stayed paired through it, and a five-cycle recheck and a second
25-cycle run took every slot. `VhubTransfersFailed` reads exactly 10 per
hub lifetime at 1 and 0 at 2 on both: transfers to a hub no longer there,
as the counter is defined (`src/xhci.h`), none with an error note. A
High-Speed mouse behind hub 4 at 2 on 2000 opens at `Period` 1, Interval 0
again (finding 1 of the first readings).

**Windows Vista SP2 and Windows 7 SP1, both architectures: both defects
are gone; nothing behind a virtual hub enumerates.** No hang, no bugcheck,
no `ctrl.failed.here`, no `ResetControllerCalls` in any boot of the four.

- Defect 1 (the deadlock): the two steps that hung in the first readings -
  Vista x64 at 1 on the unplug of the Full-Speed mouse from port 1, and
  Windows 7 x64 at 2 with a High-Speed mouse behind hub 4 - leave the
  guest answering (the Start menu opens and closes 30 s later), and so does
  every other plug and unplug on all four.
- Defect 2 (the stale hub): after the Full-Speed mouse's unplug at 1 every
  hub the driver stood up is retired (`VhubCreated` equals `VhubDropped`),
  and a High-Speed mouse on port 2 then opens as a mouse -
  `slot.addressed=00010103`, `ep.open.rate=00200004`,
  `ep.open.ival=00030005`, "HID-compliant mouse" in Device Manager, no
  `VID_1209` hub installed - on all four, twice each.
- At 2 eight hubs stand up at start with every open accepted (Windows 7
  x86 and x64 and Vista x86 `VhubOpens` 24; Vista x64 32, because Vista
  re-enumerated hubs 2 and 4 at boot and opened their pipes twice).
- Issue 6 section 6.2's topology - QEMU's Full-Speed hub on root port 2, a
  mouse at 2.1 - no longer bugchecks Vista or 7, at 1 or at 2, on any of
  the four. It is not a reading of a device behind a real hub, though: the
  QEMU hub itself sits behind a virtual one and never enumerates, for the
  reason below.
- **No device behind a virtual hub enumerated on any of the four**, at
  either value: the Full-Speed mouse, `usb-audio` (the `wav` file stays at
  its 44-byte header; every `Iso*` counter 0) and the QEMU hub - and at 2
  the High-Speed mouse too, since at 2 every device is behind a hub. This
  is finding D, QEMU's ignored port-disable write, which the owner accepted
  as a gap (above): the hub's port-1 resets are held behind a disable QEMU
  never confirms (`VhubResetsHeld` climbs). At 2 Windows then keeps
  re-enumerating the virtual hub itself at alternating addresses for as
  long as the device stays plugged, and a root reset begun on a port just
  unplugged times out (`RhResetTimeouts` +1 per unplug). The High-Speed
  interval behind a hub at 2 could therefore not be read on NT 6.x.
- **New with the fix batch, at 1 only: the virtual hub is created and
  dropped in a loop** while a Full- or Low-Speed device stays plugged -
  four rounds per plug on Vista, then Vista gives up; endless on Windows 7
  (36 in two minutes on x86). The first readings, on the build before the
  fix batch, showed no loop: the hub survived usbhub's recovery, was
  re-enumerated at the same address, and the device ended as Code 43. Each
  round in the ring: a root reset, `vhub.drop` with no `vhub.gone` (usbport's
  root disable retiring the hub), then a root reset that stands up a new
  hub, `vhub.create`, `vhub.redisable`, its default pipe opened, and the
  next drop. Codex's reading of it, and the disposition, follow.

Churn on NT 6.x was first cut to five cycles per value, since no device
behind a hub can enumerate here: at 1 `VhubCreated`/`VhubDropped` +5/+5
with the guest alive and `EndpointRefusalsNoDevice` 4 (a hub's pipe open
racing its unplug); at 2 `VhubCreated` stays 8. It was taken again at 25,
below.

**Codex on the loop** (`--fresh`, `.claude/codex-2434-loop-r1.txt`, result
beside it; the owner, away, asked for a second opinion and left the path to
this session). The mechanism is QEMU's ignored disable, one step more
precisely than the ring alone says:

1. usbhub's port-1 resets are held behind the port-1 disable QEMU never
   confirms (3.3), and so is its next root reset while the hub is present.
2. It abandons that with a root disable, which retires the hub
   (`XhciVhubRootDisable`, `src/xhci_vhub.c`) and writes PED, which QEMU
   ignores, so the disable's own debt stands (`src/xhci_rh.c`, the disable
   body).
3. The next root reset takes the hub-absent path, which does not hold for
   that debt; its end stands up a replacement hub, and the refresh's
   redisable - a debt standing, the port enabled, a hub present - writes PED
   again. That disables the physical port only; the virtual upstream stays
   enabled, and the hub's address-0 open succeeds.
4. The replacement's second enumeration reset is then held behind the same
   debt, and usbhub abandons it the same way - the loop.

The build before the fix batch already dropped a hub at 1 on usbport's root
disable, already let a hub-less root reset through, and already redisabled
under a hub present at a reset's end; the first readings do not show that
build meeting the same inputs, and the batch's new port-1 rule (a held
port-1 reset ended by a port-1 disable reports its `C_PORT_RESET`) gives
usbhub another status change, a plausible reason its recovery now runs
this way. On hardware that honours the write the same order is reachable
- a disable whose PED clears after the read-back, and a root reset asked
for before the health poll collects it - and there the redisable is what
collects the debt, after which the second reset proceeds. Excluding a hub
the same reset stood up from the redisable would leave the debt standing
with nothing left to confirm it: the enumeration deadlock, on hardware.
Discharging the debt at the root reset would release the old device's
buffers without evidence that the port stopped reading them. So: no driver
change, and one vector for the hardware order. And the churn on NT 6.x had
been cut to five cycles, which the roadmap's clause does not allow, so it
was taken again at 25.

**The vector**, `test_vhub_root_disable_confirmed_late_then_reset`: a
configured hub at 1; usbport's root disable with PED not yet clear at the
read-back (the model's `stuckPortPed`), the debt owed; PED clears with no
health poll; the next root reset is written, stands up a replacement, and
its redisable collects the debt with the hub and its upstream enable kept;
the replacement's second reset is not held and ends. `test_init` 21,576,
every host test passing. Mutated as the tempting fix - the redisable
excluding a hub the same reading stood up - four of its checks fail (the
debt uncollected, the second reset held) and nothing else does.

**The churn at 25 on NT 6.x** (2026-09-28; the same four disks and build,
`out\t24-3-4\recipe-2434-r3.md`, reports `r3-<guest>-report.md`): **25 of 25
cycles at 1 and at 2 on all four, the guest answering after each run, no
`ctrl.failed.here`, no `xfer.error`, no `ResetControllerCalls`.** At 1
(port 1, 20 s plugged, 8 s out) `VhubCreated` and `VhubDropped` reach 25
and 25 on every guest - one hub per plug, retired with its device - and
`VhubResetsHeld` stays 0: within a 20-second plug no guest reached the loop
above, Windows having not yet reset the hub's port 1. On Windows 7, both
architectures, usbhub had not opened the hub's status-change pipe either
when the unplug retired the hub; it asks for it afterwards, and the driver
refuses an open at an address no hub holds any longer
(`EndpointRefusalsNoDevice` 25, one per cycle; opens 75 seen, 50
accepted; the first such refusal in the debug output follows that cycle's
drop). Vista opens it at once: 75 of 75. `VhubTransfersFailed` 23 to 28 at
1, requests to a hub gone. At 2 (port 5) `VhubCreated` stays 8 and no hub
re-enumerates; every open is accepted; `VhubResetsHeld` climbs (38 to 75)
and each cycle's unplug leaves a root reset begun on the empty port that
never ends (`RhResetTimeouts` 25) - the gap again. Slots stay at 0 on both
values, nothing behind a hub having enumerated.

**What 24.3.4 reads, and what it leaves.** Taken: every guest held at 0, 1
and 2 (the first readings), and after the fix batch Windows 98 SE and 2000
again and all four NT 6.x guests again; the `REG_SZ` encoding (UTF-16 on
every stack); the TT record naming the hub; `Period` 8 programming
Interval 6; the first hub install on 98 and ME; a High-Speed device direct
at 1; eight hubs at start at 2; churn at 25 at 1 and at 2 on every guest;
Full-Speed audio playing from XP; SP4's usbhub counting the hub as a tier;
the Low-Speed passthrough on SweetLow's stack; issue 6 section 6.2's
topology no longer bugchecking Vista or 7; a hub disabled and enabled in
Device Manager at 2 on 98 SE, enumerating again with no controller
restart. Not taken in QEMU, and where each goes: any device behind a
virtual hub on NT 6.x, High Speed at 2 included (QEMU's ignored port
disable; the E460 under Windows 7 x86, 24.3.5); resume, and a recovery with
hubs kept (not reachable here, above; host vectors and the E460). **Not
taken at all yet, and owed before 24.3.4 is ticked:** the hidusbf rates on
a root port at 1 and 2; idle suspend; plug latency; at 2 a cancelled hub
install and a root-port power-off and on; and the device matrix at 2 with
its High-Speed rows (the cut, 24.7, reads the matrix in all three states
too). Finding 1 (a High-Speed interrupt endpoint behind a hub at `Period`
1, Interval 0) stands open for the owner (roadmap 24.5 since 2026-09-28,
taken before the cut). So 24.3.4 stays open: the loop
is disposed of, the fix batch is read on every guest it could be, and
what remains is the list above.

### The clauses never read: Windows 98 SE and 2000 (2026-09-28)

The owner's scope for the list above, taken the same day: the hidusbf rates
on every 32-bit guest held, at 1 and 2; idle suspend on the shipping build,
and again on a local build with `USB_MINIPORT_FLAGS_DISABLE_SS` cleared (a
control, never in the tree, on 98 SE and 2000 only); plug latency at 0, 1
and 2; the power-off probed once and then recorded; the cancelled hub
install on Windows 98 SE under SweetLow's stack at 2; and the device matrix
at 2 now rather than at the cut. Two guests at a time, on a second
development host (QEMU 11.0.92). The two primary targets were read first,
on the same disks and build as the readings above (`DriverEntry (built Sep
27 2026 22:21:42)`, `xhci98.sys` `1d8ec911...`), each value from its own
cold boot. Per-guest reports: `out\t24-3-4\r4-<guest>-report.md`
(git-ignored).

**Neither guest met a stop condition at any value**: no fatal exception or
hang, no `ctrl.failed.here` or `xfer.error`, `ResetControllerCalls` 0, no
`EndpointRefusals*` and nothing floored, every endpoint open accepted.

**The hidusbf rates on a root port: the ladder, exactly, at 1 and at 2.** A
Full-Speed mouse on root port 1, behind its virtual hub at both values:

| hidusbf | `bInterval` | `ep.open.rate` | Period | `ep.open.ival` | Interval | 98 SE | 2000 |
|---|---|---|---|---|---|---|---|
| none | 10 | `00080004` | 8 | `00020006` | 6 | 1, 2 | 1, 2 |
| 250 | 4 | `00040004` | 4 | `00020005` | 5 | 1, 2 | 1, 2 |
| 500 | 2 | `00020004` | 2 | `00020004` | 4 | 1, 2 | 1, 2 |
| 1000 | 1 | `00010004` | 1 | `00020003` | 3 | 1, 2 | 1, 2 |

The same ladder 24.1 read behind a real hub, now on a root port, which is
issue 4's request: 250, 500 and 1000 Hz arrive as asked. On 98 SE the filter
was installed Phase 20's way (the file, NUSB 3.6's Windows ME `usbd.sys`,
`LowerFilters` and `bInterval` in the registry), on 2000 through the
author's `Setup.exe`; each rate was read after a `device_del`/`device_add`.
On 2000 the tool's "Default" with its filter ticked handed the device
`bInterval` 4, not 10 - its own column showed 4 - so that row reads as 250
Hz; the stock 10 appeared with the filter unticked. That is the tool's
behaviour, and the driver programmed what it was handed.

**Idle suspend on the shipping build: the controller never idled, at 0, 1 or
2.** `USBCMD`/`USBSTS` read `0x5`/`0x0` at every read (98 SE and 2000 at 2
from about +1 to +5.5 minutes with nothing plugged; at 1 six minutes idle,
three with a Full-Speed mouse and its hub up, two after the unplug; three
reads at 0), no `SuspendController` but the shutdown's, and a keyboard
hot-plugged on port 6 afterwards was addressed within 10 s (2000 at 1: 3.0 s;
98 SE at 1: its pipe open at 4.35 s). Windows 2000 was read with
`DisableSelectiveSuspend = 0` written, since it never idles with the value
absent (issue 5 section 5.5). This is flag 0x20 doing what issue 5 says,
at every value; whether the virtual hubs would hold the idle off without
it is the control build's reading, still owed.

**Plug latency** (median of three timed plugs, milliseconds, device_add to
the device usable):

| | 0 | 1 | 2 |
|---|---|---|---|
| 98 SE, Full-Speed mouse | 640 | 2536 | 792 |
| 98 SE, `usb-audio` (enumeration settled) | 1212 | 2018 | 1823 |
| 2000, Full-Speed mouse | 388 | 2077 | 380 |
| 2000, `usb-audio` (first streaming endpoint enabled) | 328 | 1549 | 546 |

Record 12 section 4's cost, measured: at 1 a slower device takes about two
seconds more (the hub's enumeration and two extra resets; six opens for the
mouse where 0 and 2 take three), and at 2 about what 0 takes, the hub being
there already. Under TCG the first plug after a pause took 3.7 to 4 s at
every value, so the size of the gap is rough; its direction is not. The
timers: `usable` is the mouse's interrupt pipe - on 98 SE from the driver's
counters polled over the monitor, on 2000 from the first Configure
Endpoint of a non-default endpoint in QEMU's trace log, which agreed to the
millisecond with the debug console where both could be read. The debug
console alone cannot time a plug at 2: the line it would count is one of
the driver's value prints capped at 32 per load (`src\xhci_dbg.h`), and the
eight hubs spend 24 of them at boot.

**The root-port power-off: no operating system of the two asks for one.**
Device Manager's disable of "USB 2.0 Root Hub" at 2, on both: the hub
driver disabled all eight ports (`RH_ClearFeaturePortEnable`), aborting the
eight held status-change transfers (98 SE `TransfersAborted` 8), and never
cleared port power (`RhPortsUnpowered` 0, no `RH_ClearFeaturePortPower`);
the hubs left Device Manager and the controller kept running. The enable
powered all eight ports again (`RhPortsPowered` 8 -> 16) and the same eight
records enumerated again - `VhubCreated` stayed 8, `VhubOpens` 24 -> 48,
eight new `vhub.address` notes with addresses 1 to 8 - with no
`StopController`, `StartController` or reset. A Full-Speed mouse plugged
afterwards sat behind hub 1 (TT `HubAddr 0x0001 port 1`) and moved the
pointer. So the root-port disable and re-enable at 2 is read, ending with
the hubs enumerated again without a controller restart. The power-off half
is not reachable from either guest: statically, XP SP3's `usbhub.sys` sends
no `CLEAR_FEATURE(PORT_POWER)` at all (its one clear-port-feature helper,
`USBH_SyncClearPortStatus` at `0x190A6`, is called only with the four
change selectors 0x10, 0x12, 0x13 and 0x14), and none of the 24.3.4 debug
logs on any of the ten guests holds an `RH_ClearFeaturePortPower`. It rests
on the host vectors (`test_vhub`'s port-1 power cycle, the value-2
power-off and power-on of section 8's `-0` list).

Also seen, neither a finding: `VhubTransfersFailed` at 1 about 10 per hub
lifetime again (0 at 2), and new-hardware wizards on 98 SE for the audio
device's first plug at 2 and the keyboard's at 1, each answered before any
timed plug.

Still owed after these two: the idle control legs on 98 SE and 2000;
SweetLow's stack (with the cancelled hub install at 2), ME and XP x86 for
the hidusbf rates, idle and latency; the two matrix images prepared at 2
and the matrix run. By the owner's order of 2026-09-28 all but the idle
control come after roadmap 24.4 and 24.5, which change the driver, and are
read on fresh images of the build that carries both.

## 24.4 - The NT 6.x timer-arm race (2026-09-28)

Taken before the rest of 24.3.4, with 24.5, in one rebuild (owner,
2026-09-28). This section records the read, the decision and the host
vectors; the build and the guests are recorded with 24.5's.

### The read

All four NT 6.x `usbport.sys` builds were disassembled whole (`kd -z ... -y
srv*tools\symbols -c "u 11000 <end>; q"`, with the public PDBs; a range
starting below 0x11000 hangs on the unmapped page at 0x10400) and read by one
agent per build; Windows 7 x64's `MPf_CheckController`, `USBPORT_IsrDpc`,
`USBPORT_AsyncTimerDpc` and the Ex prologue were read first-hand. The facts,
per build with addresses, are in `legal-provenance.md` section 4; the ones
the choice turned on, all static:

- `CheckController` is called holding **no** usbport lock (the MP lock is
  taken for a flag test and released first); `InterruptDpcEx` under the
  ISR-DPC lock only; the timer callback from `USBPORT_AsyncTimerDpc` under
  the timer-list lock only; every endpoint callback under the MP-call lock
  and EpList; Start/Stop/PollController under nothing; Suspend/Resume under
  the suspend/resume semaphore only; the root-hub status queries under the
  MP lock from `RootHub_Endpoint1_Peek` or under nothing.
- No usbport function takes the MP, MP-call or ISR-DPC lock or EpList while
  holding the timer-list lock, none takes the timer-list lock under the MP
  or ISR-DPC lock, and EpList and the timer-list lock are never nested.
- `USBPORTSVC_RequestAsyncCallbackEx` takes seven arguments - the legacy
  five, an optional handle out-pointer, a skip-lock byte - takes the
  timer-list lock itself when the byte is 0, and answers an NTSTATUS
  (`0xC000009A` on its pool failure). The legacy slot is it with `(NULL, 1)`.
- **Two facts changed the draft design.** The root-hub feature callbacks
  arrive with the timer-list lock from `USBPORT_RootHub_PortRequest` and
  without it from the USB 2.0 `SetFeaturePortPower` detour (this driver sets
  `MiniPortFlags` 0x10) and the User* IOCTL paths; and on Windows 7 only
  `MPf_StopController` runs `USBPORT_CancelAllAsyncTimerCallbacks` before
  the miniport's StopController, which calls each pending timer callback
  synchronously with the lock not held. So no callback of this driver can
  tell whether it holds the lock.

### The decision

The roadmap's two options were arming only where the lock is held, and Ex
wherever the read shows it safe. The first has no context to stand on (none
is known to hold the lock), so the second, completed: on the Version 300
tier the legacy service is never called; an arm from a context usbport
reaches holding nothing, its MP lock or its ISR-DPC lock (`XHCI_ARM_UNLOCKED`:
the event DPC, `CheckController`, the root-hub status queries, the lifecycle
callbacks) is made at once through Ex with its lock; one from any other
context (`XHCI_ARM_DEFER`: every endpoint callback, every root-hub feature
callback, every timer callback) is owed to the next UNLOCKED context -
`ArmPending` for a port, the new `CommandArmOwed` latch for the command
watchdog - and a feature callback, a port timer or the recovery, which
already announce, announce when an arm is owed (the kick) so usbport's
root-hub peek makes it at once. The mode is an argument at every call, never
a field. The Version 200 tier arms at once through the legacy service in
both modes, as before. A refused Ex arm is counted (`AsyncArmsRefused`) and
treated as lost at once. Design record 05, "Where a timer may be armed", is
the record; the cost is a port-1 resume through a virtual hub up to one poll
interval late on NT 6.x at 1 or 2 (design record 12 section 11).

### The host vectors

`test_init` gained three never-reset nets (no legacy arm on a Version 300
start; no Ex arm from a timer delivery, a modelled root-hub feature or
endpoint callback, or inside `SubmitTransfer`; no Ex arm with the lock byte
set) and six vectors, `test_nt6_arm_*`, through a host-only seam that makes a
start present Version 300 (`XhciSetInterfaceVersionForTest`). Every existing
vector runs the 200 tier unchanged; `test_init` went from 21,576 checks to
21,952, every suite green.

Eighteen mutations were run, each flipping one context's mode or removing a
drain (`out\t24-4\mutations-24-4.txt`, git-ignored). The first pass killed 14
and left four alive: the power/disable/suspend feature callbacks, the resume
end of the port timer, the kick (the mutation removed only its first site)
and the locked endpoint drains; a vector was added for each and the rerun
killed all four. One survives by construction: the device half of the health
poll flipped to DEFER, because the root-hub half of the same poll has
already drained every owed arm through its own UNLOCKED slot pass.
