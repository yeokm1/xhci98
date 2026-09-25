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
  `runs/run-23-post-release/`, and the cut, are 24.5's.

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
  the version, 24.5 does), copied to `vm\xfer98\XHCI98\` and verified
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
  virtual hub proposal, `docs/future-plans/virtual-hub-per-root-port.md`,
  and that is 24.3's decision. A patched usbport is not this project's to
  ship (24.3).

### For the reporter

On a root port, 1000, 500 and 250 Hz work now, and 1.1.1.0 already behaved
this way. Slower rates and the device's own stock rate need a hub, or 24.3.
Behind a hub, every rate works, Low Speed included, from the 24.1 build on.
Replying on the issue is the owner's.
