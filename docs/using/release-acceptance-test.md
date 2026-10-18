# Release Acceptance Test

What someone does, in order, to a machine this project has never seen, with a
release it has never run, and what they must see at each step.

This is a fixed procedure. It is re-run unchanged against every release, by
someone who was not there when the driver was written, on hardware nobody here
has characterised. It was written for `1.0.0.0` and adapted for the host
controller driver at `2.0.0.0` (roadmap-hcd task 32.2); it applies to every
release from `2.0.0.0` on. The `1.x` miniport's version of this document is on
the `1.2.0.0` branch, and a run against a `1.x` release follows that one. The
release under test is named once, at the head of whatever record the tester
keeps; everything version-specific below is a citation of that release's own
files, `readme.txt` and `releases/history.md`, rather than a copy of them, so
a new version needs no edit here.

It is not a second install guide. `releases/<version>/readme.txt` is what a
user follows, and two copies of an install procedure is how one of them goes
stale. It is not an investigation either: when a reading does not appear, this
test records it and stops that clause, it does not chase it. The chase belongs
in an issue (`.github/ISSUE_TEMPLATE/bug_report.yml`) or in a run sheet written
for it.

The readings stay with the tester. Nothing from a run is filed back into this
repository, so keep the readings and the artefacts together in one place,
one run per machine and per OS, and send back only what this project can act
on: a defect, as an issue, and a correction to this document when the procedure
itself was what was wrong. A SKIP with a reason is a result; a blank is not,
and so is a verdict with no reading beside it.

A clause marked "record only" is an expectation this project has not
observed on the `2.0.0.0` driver: write down what is seen, and do not fail
the step on it.

One caution for Windows 98 SE, carried from the `1.x` releases. Do not
update to `2.0.0.0` in place over a running `1.x` driver under NUSB: NUSB's
stack blue-screens as it stops the `1.x` driver. Follow the upgrade route of
`readme.txt` section 4 (rename the old file and cold-boot first). The `1.x`
freeze on cycling a device rapidly in and out of a port was not seen under
`2.0.0.0`.

---

## Equipment

Properties, not models. A tester holding different hardware has to be able to
tell whether theirs will do. `docs/contributing/test-equipment.md` is the
characterisation record for the hardware this project holds, and
`scripts/hub-characterise.ps1` is how any of it was read.

### Cannot start without

| # | What | The property that matters |
|---|---|---|
| 1 | An xHCI machine | PCI class code `0C0330` and a memory window below 4 GB. Step 3 confirms them. Since `2.3.0.0` an interrupt pin is not required: a controller with none is polled, or takes a message-signalled interrupt on Vista and 7, and `XHCIQUAL` says so (`QUALIFIED WITH CAVEATS`) |
| 2 | One target OS, already installed and working | Windows 98 SE (4.10.2222) or Windows 2000 SP4; for the VM-only rows, Windows ME (4.90.3000) (4.5, 7.7, 7.8 and 7.21), 32-bit Windows XP SP3 (4.6, 7.9-7.12), Windows XP Professional x64 SP2 (4.8, 7.13-7.16), 32-bit Windows Vista SP2 or Windows 7 SP1 (4.9, 7.17-7.19), or Windows Vista x64 SP2 or Windows 7 x64 SP1 (4.10, 7.17-7.20). **On Windows 98 SE, NUSB 3.3 or 3.6 installed** if the storage clauses (5.2, 5.7, 5.8) are to be taken: storage there needs NUSB's mass-storage component, and without it those clauses are a SKIP with that reason (4.1). Do not install NUSB on any other target. The two 64-bit rows take a different package: separate 64-bit builds in their own directories (step 2); and on Vista x64 and Windows 7 x64 the drivers, test-signed with this project's own certificate since `2.3.0.0` (not Microsoft-signed), load only in test-signing mode or, for one boot at a time, on a boot with driver signature enforcement disabled (F8) (4.10). One OS per run: a dual-boot machine is two runs and two records |
| 3 | A PS/2 or built-in keyboard and pointing device | A USB keyboard on the controller under test is unusable during the DOS pass and can stop responding mid-run. On a laptop the built-in keyboard is normally i8042-attached, but that is per machine; confirm it rather than assuming (`docs/contributing/build-and-test.md`, "Bootstrapping xHCI-only machines") |
| 4 | A real-DOS boot medium, and a way to get a file off it | MS-DOS or FreeDOS on floppy, CD or USB key, booted without EMM386, a V86 monitor or a paging memory manager, but with `HIMEM.SYS` available, which is not one of those and which the qualifier may need (step 3). Not a DOS box inside Windows: the qualifier needs memory it can address one-to-one. Step 3 leaves its probe log (`mDDhhmmS.LOG` from `2.4.0.0`, `PROBE.LOG` before it) on it, and that file is the run's first artefact |
| 5 | A way to put the package on a machine whose USB does not work yet | Pull the disk and stage from another machine, burn a CD, or use a network share. On an xHCI-only machine there is no USB until this driver works (`docs/contributing/build-and-test.md`, "Bootstrapping xHCI-only machines"). Pre-stage generously: every forgotten file is another disk swap |
| 6 | A way back | A recovery rung that survives a driver which loads and then fails. Windows 98: the Startup Menu and Safe Mode. The NT targets: F8, and the Recovery Console pre-installed with `winnt32 /cmdcons` while USB still works. Test-boot the rung once before the first install, not after |

### What an individual clause needs

A clause whose device is absent is a SKIP with a reason, which is a result.

| Clause | Device | The property that matters |
|---|---|---|
| 5.1 | A Low-Speed HID | 1.5 Mbps signalling: an older mouse or keyboard. `scripts/hub-characterise.ps1` reads the negotiated speed as `Low` |
| 5.2 | A High-Speed flash drive | 480 Mbps, Bulk-Only. A USB 2.0 stick: a USB 3.x stick on a USB 3.x connector now runs at SuperSpeed, which is 5.8's reading, not this one |
| 5.3 | A hub, plus a device to put behind it | Any USB 2.0 hub. Record whether it is self- or bus-powered and, if the tester can read it, its TT class: `bDeviceProtocol` 1 = single-TT, 2 = multi-TT, 0 = a Full-Speed USB 1.1 hub |
| 5.4, optional | A USB Ethernet adapter | With a driver for the target OS. Without one it enumerates and does nothing, which tests the stack above this driver rather than this driver |
| 5.5, optional | A USB Audio device | UAC 1.0, with the target OS's own driver. On Windows 98 SE, plug it in only once the machine has settled for a couple of minutes after a cold boot (the release notes' "Known limitations", the audio wedge) |
| 5.7 | A UAS storage device | One that offers the UAS interface (`bInterfaceProtocol` `0x62`). A USB-to-SATA bridge or an enclosure is the usual form. Read at SuperSpeed on a USB 3.x connector; behind a USB 2.0 hub it runs at High Speed, which is a second reading |
| 5.8 | A SuperSpeed device and a USB 3.x connector | Any USB 3.x storage device. Its SuperSpeed is witnessed by the driver's snapshot (step 8), not by Device Manager |
| 7.3 | A composite device | One physical unit that is more than one thing at once: a headset with buttons, a keyboard with media keys |
| Step 8 | Nothing extra | The log channel is the xHCI98 USB Tree out of this release (`XHCISNAP.EXE` up to `3.0.0.0`), one setting it writes for you, and a restart of the machine |

### Suggested devices

The properties above are what a clause needs. These are units this project has
actually measured, offered as a shopping list for a tester who would rather buy
or borrow something known than characterise what is in the drawer. Nothing here
is required, and a device that satisfies the property is as good.
`docs/contributing/test-equipment.md` carries the full readings.

| Clause | Suggested unit | Why this one |
|---|---|---|
| 5.1 | Logitech USB Optical Mouse `046D:C077`, or any pre-2005 USB mouse or keyboard | Verified Low Speed: HID boot mouse, one interrupt IN at `bInterval=10`. Most modern HIDs enumerate at Full Speed, so a new mouse off the shelf usually does not satisfy this clause |
| 5.2 | SanDisk U3 Titanium `0781:5408`, or any USB 2.0 stick | High Speed wherever it is plugged, and Bulk-Only |
| 5.3 | Terminus 4-port `1A40:0101` (single-TT) or 7-port `1A40:0201` (multi-TT) | Plain USB 2.0 hubs, no SuperSpeed half, one chip and one tier. Adjacent product IDs from one vendor, so the pair changes only the TT class |
| 5.3 | Avoid a hub enclosure containing cascaded chips, such as Genesys `05E3:0608` | Its sockets are not equivalent: three sit at tier 1 and four at tier 2, with different route strings and a different TT, so a reading depends on which socket was used |
| 5.4 | An ASIX AX88772-based adapter, such as `0B95:7720` | ASIX parts have Windows 98 and Windows 2000 drivers. Read the chipset, not the packaging: the RTL8153 in most modern USB-C dongles has no driver for either target |
| 5.5, 7.3 | C-Media `0D8C:0014`, or a Creative Sound Blaster Play! 2 `041E:323D` | UAC 1.0, Full Speed, and composite: audio interfaces with an HID beside them and no IAD, so one unit serves both the audio clause and the composite clause |
| 5.5 | Not a UAC 2.0 unit such as the Sound Blaster X4 `041E:3278` | These systems ship UAC 1.0 audio drivers, and that unit offers no UAC 1.0 fallback |
| 5.7 | MSSU10-128GSR flash drive `090C:2320`, or the ASMedia USB-to-SATA bridge `174C:5106` | Both offer UAS at SuperSpeed. The ASMedia bridge offers both transports at both speeds, so it also serves the forced-Bulk-Only reading; the MSSU10 offers only Bulk-Only behind a USB 2.0 hub |
| 5.8 | SanDisk 3.2Gen1 flash drive `0781:55AB` | SuperSpeed, Bulk-Only, so it reads SuperSpeed through Windows' own `usbstor.sys` with no UAS involved |

---

## The steps

Nine steps, in order. Each gives what to do, the expected reading, what to do
when that reading does not appear, and where the expectation was observed. An
expectation that has never been observed anywhere is marked "record only" and
is not a pass criterion.

The longer steps open with a checklist table of numbered substeps: what to do,
on which device where that matters, and the reading that counts. The table is
what a tester works through; the prose under it carries the reasoning, the
branches for when a reading does not appear, and the provenance. Cite substeps
by their id in the record and in any report, because "step 5 failed" and "5.3
failed" are different findings.

### Step 1. Record the machine, its BIOS and the OS build

| # | Do | Expected reading |
|---|---|---|
| 1.1 | Fill every field of "What to record for each machine" in `xhciqual/hardware-testing.md`: model, chipset, BIOS version and date, the DOS version and boot medium, and whether PS/2 or built-in input is available | Every field filled, or explicitly `n/a` with the reason |
| 1.2 | Read every USB-related BIOS setting and write each one down, before anything else is done to the machine | Each setting with its value. A BIOS that offers no USB option at all is a result; write `NOT PRESENT` |
| 1.3 | Record the target OS and its build, its architecture, on Windows 98 SE whether NUSB (and which version) or SweetLow's stack is installed, and whether any previous version of this package was ever installed here | The OS and build, the architecture (it decides which package step 4 installs, and on Vista x64 and 7 x64 that test-signing mode is needed, 4.10), the USB stack, and `none` or the version. A machine that had one produces an upgrade result, which is a different measurement and is not what this test measures |

1.2 comes before anything else because on Intel 7-, 8- and 9-series chipsets
and C610/X99 a BIOS setting decides which controller owns the USB ports at
boot: `XUSB2PR` routes the USB 2.0 ports between EHCI and xHCI, so the same
machine presents a working xHCI or one with nothing on it depending on a
setting nobody recorded. Since `2.1.1.0` the driver can route the switchable
ports to xHCI itself on those chipsets (Intel device ids 1E31, 8C31, 9C31,
8CB1, 9CB1 and 8D31), since `2.3.0.0` only when `XhciIntelPortSwitch` turns
it on (1, or 2; the install writes 0, and an update keeps an earlier 1), so
on such a machine
also record that value, and keep the 8.4 report's `psw.` records with the
result: the setting still decides what the machine looks like before the
driver starts and which connectors are outside the switchable set.

If a field cannot be answered: write what was looked at and why it could not be
answered. A blank is not a result.

Observed: the record format is `xhciqual/hardware-testing.md`'s, with
`xhciqual/results/e460-2026-08-22/README.md` as a worked example. `XUSB2PR`'s
behaviour is derived from the Intel datasheet and from Linux, and has been
read on one machine: a Lenovo B490 (1E31) under Windows 98 SE, whose
firmware on Auto and on Smart Auto left the switchable ports on EHCI and
whose ports the driver then routed (roadmap-hcd task 34.3,
`docs/contributing/runs/run-34.md` 34.3-H2); see
`docs/usb-xhci-info/xhci-programming.md`, "Firmware Handoff, and the
Controller Deviations This Driver Acts On".

### Step 2. Check the download

| # | Do | Expected reading |
|---|---|---|
| 2.1 | Unzip the asset and list the top level, then `tools\` | No top-level directory: `readme.txt`, `LICENSE`, from `3.0.0.0` `INSTALL.BAT`, the flavour directories (`release-x86\`, `debug-x86\`, `release-x64\`, `debug-x64\`) and `tools\` come out directly, and nothing else; `tools\` holds `xhciqual\`, `xhcisnap\` (up to `3.0.0.0`) and `portsw\` (roadmap-hcd task 38.9), from `2.4.0.0` also `silent\` and `xhcicln\` (tasks 38.2 and 38.8), and from `3.0.0.0` also `xhciinst\` and `xhcitr\` (design records 23 and 25). An asset before `2.4.0.0` has no `tools\` and carries `xhciqual\`, `xhcisnap\` and, from `2.3.1.0` (roadmap-hcd task 37.5), `portsw\` at the top level instead |
| 2.2 | From `3.1.0.0`: check that `tools\xhcitr\` is one of them and `tools\xhcisnap\` is not; up to `3.0.0.0`, that `tools\xhcisnap\` is one of them | From `3.1.0.0`: `tools\xhcitr\` holds `XHCITR.EXE` and `XHCITR64.EXE`, and no `XHCISNAP.EXE`, `SNAPON.BAT`, `SNAPGET.BAT` or `SNAPOFF.BAT` is anywhere in the asset (roadmap-usb4 task 42-A.4). Up to `3.0.0.0`: `tools\xhcisnap\` holds `XHCISNAP.EXE`, with `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT` beside it from `2.4.0.0` (roadmap-hcd task 38.1), and a download without it is a cut made with `-SkipSnapTool`. It is what step 8 needs, so report a download without it rather than skipping the step |
| 2.3 | List each flavour directory against the file list in `readme.txt` section 8 | Each holds every file that section names, and nothing else. Read the list off that file rather than from memory. For `3.0.0.0` it is `xhci98.inf`, `xhci98.sys`, `xhciuas.inf`, `xhciuas.sys`, `xhcicm.inf`, `xhcicm.sys`, `txtsetup.oem` and `nt\`, which holds `xhcicmf.inf` and a copy of `xhcicm.sys`; with `xhcihlpr.sys` in the two `-x86` directories and `xhci98-test.cer` in the two `-x64` ones; and in the download `release-x86\` and `release-x64\` also hold the bundled Stor98's files, each name starting `stor98`, as the same section says. `2.0.0.0`'s list was the first four |
| 2.4 | Look for a version directory nested inside another | None |
| 2.5 | On a 64-bit target only: check that the cut carries a 64-bit pair at all | `release-x64\` and `debug-x64\` present, and `readme.txt` naming a 64-bit system among what it supports |
| 2.6 | List `tools\portsw\`, then look for `portsw.vbs` in each flavour directory | `tools\portsw\` holds `portsw.vbs`, `portsw-on.bat` and `portsw-off.bat` and nothing else, and no flavour directory holds any of them (roadmap-hcd tasks 37.5 and 38.9). The `2.3.1.0` asset has them in `portsw\` at the top level; the `2.3.0.0` asset instead has the three in each flavour directory and no `portsw\` |
| 2.7 | From `2.4.0.0`: list `tools\xhciqual\` (and up to `3.0.0.0` `tools\xhcisnap\`), then look for `QUALREP.BAT`, `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT` anywhere else in the asset | `QUALREP.BAT` beside `XHCIQUAL.EXE`, and up to `3.0.0.0` the three `SNAP*.BAT` beside `XHCISNAP.EXE`, and none of the four anywhere else (roadmap-hcd task 38.1); from `3.1.0.0` no `SNAP*.BAT` at all. An asset before `2.4.0.0` has none of them |
| 2.8 | From `2.4.0.0`: list `tools\xhcicln\` | `XHCICLN.EXE`, `XHCICL64.EXE`, `readme.txt` and `NOTICE.TXT`, and nothing else (roadmap-hcd task 38.8). An asset before `2.4.0.0` has no such directory. A `2.4.0.0` or later download without it is a cut made with `-SkipCleanTool`: the `XHCICLN` parts of 4.11 and 4.12 cannot be run against it. Report that rather than skipping them |
| 2.9 | From `3.0.0.0`: list `tools\xhciinst\`, and look for `INSTALL.BAT` at the top level | `XHCIINST.EXE`, `XHCIIN64.EXE`, `readme.txt` and `NOTICE.TXT`, and from `3.1.0.0` also `XHCIWIZ.EXE`, `XHCIWZ64.EXE` and `XHCIDI.EXE`, and nothing else; `INSTALL.BAT` beside `readme.txt` (roadmap-usb4 tasks 40.6 and 42-A.2, design records 23 and 26). Then `INSTALL /list` (from `3.1.0.0` `INSTALL /console /list`) on the target: it names the package, the devices and what it would do, and changes nothing; from `3.1.0.0` `INSTALL` opens the installer's window. An asset before `3.0.0.0` has neither |
| 2.10 | From `3.0.0.0`: list `tools\xhcitr\` | `XHCITR.EXE`, `XHCITR64.EXE`, `readme.txt` and `NOTICE.TXT`, and nothing else (roadmap-usb4 Phase 41, design record 25). An asset before `3.0.0.0` has no such directory |

The `release` directory for the machine's architecture is the one to install.
The `debug` directory beside it is the same drivers built for diagnosis, for a
machine that has already gone wrong. Pointing a machine at the other
architecture's directory is safe in both directions: the wrong directory
offers no driver at all rather than installing one that cannot load. Record it
as a wrong pick and point at the other one.

A flavour directory holds the drivers and their INFs (from `3.0.0.0`
`xhci98.sys`, `xhciuas.sys` and `xhcicm.sys`, with `xhcihlpr.sys` in the
x86 pair) and, in the download's `release-` directories, the bundled
Stor98, and no Microsoft file. What the install needs beyond them, `usbd.sys` and `usbui.dll`, Windows
supplies from its own installation source at step 4 (`readme.txt` section 3).
On Windows 98 SE the storage clauses also need NUSB's mass-storage component,
which this package does not carry.

If one directory nests another copy of the version inside itself (2.4): stop,
and report the asset rather than the driver. That is a packaging defect and it
has happened once, under `1.x`.

Observed: the layout and the assertion that protects it are
`scripts/package/make-release.ps1`, `New-UploadSet`; the directory names, and
why a wrong pick is safe, are `releases/README.md`;
`docs/contributing/legal-provenance.md` section 5 records why the asset
carries no Microsoft file. `2.0.0.0`'s four-file directory (two drivers,
two INFs) was staged for the `release` legs of its integration build and
read again from its asset; each later release's directory list is its own
`readme.txt` section 8 and `releases/<version>/`.

### Step 3. The DOS pass

| # | Do | Expected reading |
|---|---|---|
| 3.1 | Boot real DOS with `XHCIQUAL.EXE` from the download's `TOOLS\XHCIQUAL\` directory (`XHCIQUAL\` at the top of an asset before `2.4.0.0`), keeping `XHCIQUAL.MAP` beside it. No EMM386, no V86 monitor or paging memory manager, and not a DOS box inside Windows | A prompt on a machine whose memory the qualifier can address one-to-one |
| 3.2 | `XHCIQUAL` | One of four verdicts: `LOOKS QUALIFIED`, `LOOKS QUALIFIED WITH CAVEATS` (no line interrupt: the driver polls the controller), `DISQUALIFIED` or `CANNOT SAY`. The first two continue |
| 3.3 | `QUALREP`, from `TOOLS\XHCIQUAL\` (before `2.4.0.0`, `XHCIQUAL --probe-only --no-page --log PROBE.LOG`) | Read-only, writes nothing to the machine, and leaves a new log in the current directory named from the DOS date and time, `mDDhhmmS.LOG`, whose name it prints: the run's first artefact. Run it again: the second log takes the next letter and the first is unchanged |
| 3.4 | Read the controller `FACT` line out of that log | `hciver`, `ports`, `usb2ports` (the split between USB 2.0 and USB 3.x protocol ports; the `2.0.0.0` driver manages both), `pin`, `bar` and `irq`, each written down |

This step is the same on every target. `XHCIQUAL` reads the machine rather
than the operating system installed on it.

If the tool will not run at all (3.1): add `HIMEM.SYS` before concluding
anything about the machine. The line is `DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V`
in `CONFIG.SYS`, with the path pointed at wherever `HIMEM.SYS` actually is. A
program that never started has produced no verdict, which is a different
record from `DISQUALIFIED`; say which one this was.
(`xhciqual/hardware-testing.md`, "Safety and preparation" step 1.)

`Interrupt Pin = 0` is no longer a stop: since `2.3.0.0` the quick scan
reads `LOOKS QUALIFIED WITH CAVEATS` and the full run `QUALIFIED WITH
CAVEATS` (exit code 3), because the driver polls such a controller
(`XhciInterruptMode` `0` or `2`), or takes a message-signalled interrupt on
Vista and 7 where the controller offers one. Record the caveat and the
line saying which applies, and continue to step 4; at step 8, check in
the driver's snapshot (`XHCISNAP`'s report up to `3.0.0.0`) that the
delivery is the one the line named (polling, or MSI). Up to `2.2.0.0` it was
`DISQUALIFIED` and a stop. `DISQUALIFIED` on "no USB 2.0 ports" is the qualifier's criterion from
the `1.x` releases; record it and continue to step 4 (the `2.0.0.0` driver
accepts such a controller, built from the specification and untested).

`CANNOT SAY` sends the tester to the BIOS, not to the driver. Change the
setting, cold-boot, and re-run.

Observed: the verdicts and the command line are `readme.txt` section 1.
Real bare-metal passes of this shape are in `xhciqual/results/`.

### Step 4. Install, from the release directory

Do: follow `readme.txt` section 4 for the target, out of the `release`
directory for the machine's architecture (step 2). Point at a directory, never
at a loose `.sys`.

The rows are grouped by phase rather than by number: the seven install rows
first, one per target, then the readings taken once the install is done. The
ids are kept from the `1.x` version of this test, because the roadmap and the
run sheets cite them.

| # | Target | Do | Expected reading |
|---|---|---|---|
| 4.1 | Windows 98 SE | NUSB 3.3 or 3.6 first if the storage clauses are to be taken (none is needed for the controller, hubs, HID or audio), then Device Manager, the unclaimed xHCI controller, Properties -> Driver -> Update Driver -> Specify a location -> `RELEASE-X86\` | On an xHCI-only machine, "Insert Disk" asks for the Windows 98 Second Edition CD-ROM, then reports that `usbd.sys` cannot be found and asks where to copy from; give it the CD's `WIN98` folder. Then a request to restart. A machine that already has the file is not asked. Record which it was |
| 4.2 | Windows 2000 SP4 | Device Manager, the controller (under "Other devices" while it has no driver), Properties -> Driver -> Update Driver -> Search for a suitable driver -> Specify a location -> `RELEASE-X86\`. Over an installed xhci98 the list route also works: Display a list of the known drivers -> Have Disk -> `RELEASE-X86\` | Completes with no prompt and, from `2.4.0.0`, no request to restart, although the INF's `Xhci.Dev.NTx86` path also copies the helper `xhcihlpr.sys` to `System32\Drivers` and adds its boot-start service `xhcihlpr`; record both, and that the controller runs at once (4.3). On a controller with no driver yet the list route fails: it asks for a hardware type first, Windows 2000 does not offer USB controllers there, and Have Disk then answers "The specified location does not contain information about your hardware", since the INF names only the USB class (read on a clean 2000 SP4 guest, 2026-10-13, `docs/contributing/runs/run-37.md`, "37.final") |
| 4.5 | Windows ME | No USB 2.0 stack is needed (a stock ME was read on `2.3.0.0`; SweetLow's stack, if present, also works); the Windows 98 SE route of 4.1 | Completes, with a request to restart. In the virtual machines no CD was asked for. This target is supported in virtual machines only |
| 4.6 | Windows XP | Device Manager, the controller, Properties -> Driver -> Update Driver -> Have Disk -> `RELEASE-X86\`; Continue Anyway at the unsigned-driver warning | Completes with no file prompt and, from `2.4.0.0`, no request to restart, with `xhcihlpr.sys` and its service `xhcihlpr` installed as on 4.2. This target is supported in virtual machines only |
| 4.8 | Windows XP x64 | The Windows 2000 route of 4.2, pointed at `RELEASE-X64\`; Continue Anyway at the unsigned-driver warning | Completes with Windows' signature and Logo prompts only, no file prompt and no restart. `RELEASE-X86\` offers this machine no driver at all, which is what a wrong pick looks like. This target is supported in virtual machines; of real machines it has run on one, the owner's Dell Latitude E7250, at the `2.3.0.0` bench |
| 4.9 | Windows Vista, Windows 7 (32-bit) | Device Manager, the unrecognised xHCI device, Update Driver Software -> Browse my computer for driver software -> `RELEASE-X86\`. Use Device Manager rather than right-clicking an INF. If Windows warns that the driver is not signed or its publisher cannot be verified, choose to install it anyway, and write down the prompt's wording | Completes with no file prompt: the INF's `Xhci.Dev6` path copies `xhci98.sys` and the helper, `xhcihlpr.sys` from `2.4.0.0` (`xhcimsi.sys`, the MSI helper, in `2.3.0.0` and `2.3.1.0`). Over `2.3.0.0` or `2.3.1.0` it also marks the `xhcimsi` service for deletion, gone after the restart, and deletes `xhcimsi.sys` from `System32\Drivers` at once, before the restart, with no `PendingFileRenameOperations` entry (read in the Phase 38 VM matrix); record both. Since `2.3.0.0` it then asks for a restart, and until the restart the controller shows Code 28 (Windows 7) or Code 1 (Vista); 4.3 is read after it. 32-bit Vista and Windows 7 do not enforce kernel-mode signing, so the driver starts on the ordinary boot. This target is supported in virtual machines only |
| 4.10 | Windows Vista x64, Windows 7 x64 | First turn test-signing mode on, once, as `readme.txt` section 4 says: suspend BitLocker on the system drive if it is on, run `bcdedit /set testsigning on` at an elevated Command Prompt, restart, and check that "Test Mode" shows on the desktop. Then the route of 4.9, pointed at `RELEASE-X64\`; at the publisher warning, install anyway. Where test-signing mode cannot be turned on, restart with driver signature enforcement disabled (F8, the Advanced Boot Options menu) instead; that lasts one boot | Completes with no file prompt; the Driver tab then says "Not digitally signed". In test-signing mode the drivers start at every boot. On a boot with neither test-signing mode nor enforcement disabled, Device Manager shows Code 39 (Vista) or Code 52 (Windows 7) on the controller and nothing on it works. The x64 drivers are test-signed with this project's own certificate since `2.3.0.0` (`xhci98-test.cer` beside them; importing it is not needed); that is not Microsoft signing. Record which of the two modes the run used. This target is supported in virtual machines; Windows 7 x64 has also run on one real machine, the owner's Dell Latitude E7250, at the `2.3.0.0` bench |
| 4.3 | All seven | Look at Device Manager when the install is done | The two nodes below, and neither carries a warning mark. On Vista x64 and Windows 7 x64, on the boot of 4.10 |
| 4.4 | Windows 98 SE | Record only: open the controller's Driver File Details and the Driver tab | Driver File Details lists `xhci98.tmp` (the `release` legs, under NUSB and under SweetLow's stack). Under `1.x`, `xhci98.tmp` was left in `System32\Drivers` and listed there (cosmetic), and the Driver tab showed a date but no version; the `2.0.0.0` INF keeps the same temporary-copy line |
| 4.11 | Every target, from `2.4.0.0` | The root hub of 4.3: on 2000 to 7 its Details tab, Device Instance Id; on 98 SE and ME, `regedit`, `HKEY_LOCAL_MACHINE\Enum\USB`. Then `XHCICLN` (`XHCICL64` on a 64-bit target) from `tools\xhcicln\` at a prompt (on Vista and 7 it asks for elevation) | The root hub's id begins `USB\XHCI98_ROOT_HUB\`, and on 98 SE and ME its key is under `Enum\USB\XHCI98_ROOT_HUB` (roadmap-hcd task 38.8). On a machine that never ran a release before `2.4.0.0`, `XHCICLN` prints "No devnode is left under XHCI98\: nothing to do." and exits 0 |
| 4.12 | Every target, over an installed `2.3.x`, from `2.4.0.0` | Update the controller as `readme.txt` section 4 says for an update from `2.3.x`, with a keyboard and mouse that do not go through the driver on XP, XP x64 and 98 SE; restart where asked; then `XHCICLN`, and on 2000 to 7 `XHCICLN -remove` (`XHCICL64` on a 64-bit target), answering `Y` | The new root hub, each hub and each composite device are found and installed once, with the prompts section 4 lists; Device Manager's default view shows one root hub per controller and no warning mark. `XHCICLN` lists each old `XHCI98\` entry as "not present"; on 2000 to 7 it ends "Every check passed", and `-remove` removes them (exit 0, or 3 with a restart pending) and a second run prints "nothing to do"; on 98 SE and ME it lists only and exits 0. Record each prompt and the tool's last lines |
| 4.7 | Every target | The root hub of 4.3, Properties; then the controller's Properties | A **Power** tab on the root hub and an **Advanced** tab on the controller. Record what each shows. These are Windows' own pages, drawn with `usbui.dll` (on Windows 98 SE and ME through `sysclass.dll`), which the install has Windows copy; their absence is a missing file rather than a driver fault, and is recorded |

The two nodes of 4.3, as Device Manager shows them:

```
xHCI98 USB 3.x eXtensible Host Controller
    xHCI98 USB 3.x Root Hub
```

Both are the INFs' strings, and both architectures' INFs carry the same ones,
so they do not say which package was installed; what says that is which
directory was pointed at. An external hub plugged in later appears as
"xHCI98 USB Hub" beneath the port it is plugged into, with the devices
behind it beneath it; a USB 3 hub appears twice, the second entry "xHCI98
USB 3.x Hub" for its SuperSpeed half. The driver still runs every hub
itself (since `2.1.0.0`; the release notes, "Devices and hubs in Device
Manager"). On Windows XP each newly plugged hub brings the Found New
Hardware wizard and the unsigned-driver warning.

If a USB device's own driver fails to load after the install and Windows 98
SE asked for the CD at 4.1: `usbd.sys` is missing, which means the Insert Disk
prompt was cancelled or answered with the wrong disk. Put the CD in and
install the driver again.

If the controller reports `Code 10` (the NT targets): the driver loaded and
then failed while bringing the controller up. Record it together with the
whole of step 1's machine record.

Observed: the device strings are `src/xhci98.inf`'s `XhciDesc` and
`RootHubDesc`. 4.1's prompts, word for word, and the restart were read on
Windows 98 SE in virtual machines under NUSB 3.3, under SweetLow's stack and
on a stock install (roadmap-hcd 26-V.0 and 26-V.1, and the stock and
SweetLow-only readings of Phase 28). 4.2 is 26-V.0's and 26-V.2's. 4.8 is
28-A.2's, on Windows XP x64 in a virtual machine. 4.6, 4.9 and 4.10 are the
Phase 28 guest legs on the integration build, read again on its `release`
flavour, and read once more from the release asset after the cut. 4.7's two tabs were read on Windows 98 SE under both
stacks and on Windows 2000 (26-A.8).

### Step 5. Devices, one at a time, then a hub

Do: with the machine running, take each device in turn on a root port. Plug it
in, confirm it enumerates, use it, unplug it, plug it back in. Then assemble a
hub at a root port with children behind it, and repeat for the children. One
device at a time, and do not cycle a device rapidly.

| # | Device | Do | Expected reading |
|---|---|---|---|
| 5.1 | A Low-Speed HID | On a root port: plug in, use it, unplug, plug it back in | It appears with the right identity and no warning mark, the pointer moves or the keys type, unplugging removes the node, and replugging brings it back with no Refresh and no prompting. On Windows 98 SE the first HID device may ask for the CD for `hidclass.sys`, once |
| 5.2 | A High-Speed flash drive | The same, and round-trip a file: write one to it and read it back | A drive letter appears and the file reads back with matching contents, and the node comes and goes with the device. On Windows 98 SE without NUSB's mass-storage component: "Unknown Device", Code 28, which is the missing prerequisite, not this driver; record it and SKIP the rest of the clause |
| 5.3 | A hub at a root port, with children behind it | Assemble it, then take 5.1 and 5.2 again on the children | The hub's own "xHCI98 USB Hub" entry (4.3's note), and every child named and working beneath it. Record whether the hub is self- or bus-powered and, if it can be read, its TT class |
| 5.4 | A USB Ethernet adapter (optional) | On a root port, then behind the hub: bring it up and pass traffic | It takes an address and passes traffic |
| 5.5 | A USB Audio device (optional) | On a root port, then behind the hub: play something through it | It plays and is heard |
| 5.6 | Every device above | Record the negotiated speed wherever the OS will show it | `Low`, `Full` or `High`, the device's true speed, on a root port and behind a hub alike. Device Manager on these systems shows `High` at most, so a SuperSpeed device is 5.8's reading |
| 5.7 | A UAS storage device | On a USB 3.x connector: plug in; on its first arrival a New Hardware wizard asks for its driver - point it at the same `release` directory. Vista's wizard names the device by its own product string, and it becomes "xHCI98 USB Attached SCSI Storage" once installed. Windows 7 shows no wizard: it searches Windows Update and reports the device was not installed; install it from Device Manager on the device's entry (Update Driver Software, Browse my computer for driver software, the same directory). Round-trip a file, unplug, plug it back in | It installs with no restart, a drive letter appears, and the file reads back with matching contents. On Windows 98 SE without NUSB's mass-storage component it sits at Code 2; that is the missing prerequisite (`readme.txt` section 3), so record it and SKIP. If the device offers both transports, also record which driver Device Manager shows; `XhciForceBulkOnly` is `readme.txt` section 9 and is not this clause |
| 5.8 | A SuperSpeed device | On a USB 3.x connector: plug in, round-trip a file. Keep it plugged in for step 8 | It works, with matching contents. Its SuperSpeed is read at 8.8 from the driver's snapshot, not from Device Manager, which shows `High` at most |

On Windows 98 SE, the first storage device installed from NUSB's five
mass-storage files on their own, rather than from an NUSB install, takes one
step more: Windows binds the new disk to its generic "Disk drive" and no drive
letter appears until that entry is updated to "USB Disk" and the device
replugged (`readme.txt` section 3). That is a reading of the prerequisite and
not a failure of 5.2 or 5.7; record that it was needed.

If a device is only noticed after pressing Refresh in Device Manager: record
it, take step 7's idle hot-plug check now, and say in the record that it was
reached this way.

If a device does not enumerate at all: record its VID/PID, its speed if
anything reports one, and whether it behaves the same on a root port and behind
the hub. That pair is the discriminating reading, and it should be read for
what it excludes and no more. The same fault in both places rules out a
hub-specific defect (Route String, TT, topology) and leaves everything the two
paths share: this driver's common code, the class driver above it, and the
device itself. Different behaviour points at the hub path without proving it.

Observed: under `2.0.0.0`, in virtual machines: HID, storage with a verified
file compare, hubs with devices behind them and unplug and replug on both
primary targets and the Phase 28 guests (roadmap-hcd 26-V.1, 26-V.2, 27-V.1
and 28-V.1's legs on the integration build); UAS at SuperSpeed and at High
Speed with a round trip and no restart (31-V.1, and the Windows 98 SE storage
readings of Phase 31); SuperSpeed storage (29-V.1). The Windows 98 SE storage
prerequisite and the "Disk drive" step are the SweetLow-only reading of
Phase 28. On real hardware, every clause of this step was observed on
Windows 98 SE on a ThinkPad E460 and a ThinkPad P14s Gen 1, at the bench
session before the release (roadmap-hcd 28-E.1, 29-E.1, 30-E.1 and 31-E.1):
High-Speed hubs, single- and multi-TT, with Low- and Full-Speed devices
behind them and a Full-Speed hub behind a High-Speed one, a Low-Speed mouse
polled every 8 ms at a root port and behind a hub, audio played and heard at
a root port and behind a hub, SuperSpeed storage, a SuperSpeed hub, and UAS
at SuperSpeed and High Speed with forced Bulk-Only. The Low-Speed clause
(5.1) has no virtual-machine vehicle and rests on that bench reading.

### Step 6. Reboot with devices attached

Do: leave the devices plugged in and restart the machine normally.

Expected reading: the machine boots, and every device comes back with no
prompting: no wizard, no Refresh, no replug.

If a device does not come back: try one replug and record whether that
recovers it. A device that returns on a replug but not on a boot is a different
result from one that returns on neither, and the record should say which.

Observed: record only for `2.0.0.0`; the guest legs restarted with devices
attached but did not read this step as a clause. Under `1.x` this was batch
11-V's, on both primary targets.

### Step 7. The target-specific clauses

Take the block for the target under test. The other blocks' clauses are
`SKIP - other target`.

Windows 98 SE

| # | Do | Expected reading |
|---|---|---|
| 7.1 | Disable the controller in Device Manager, enable it, then remove it and refresh | It goes and comes back each time, with no crash. Under `1.x` with NUSB this blue-screened the machine at `0028:C00312EE`, in NUSB's `usbport.sys`, which `2.0.0.0` does not use |
| 7.2 | Unplug everything from the machine's USB ports, leave it a full minute, then plug in a mouse | It enumerates on its own, with no Refresh in Device Manager |
| 7.3 | Plug in one composite device, something that is more than one thing at once | It enumerates as one "xHCI98 USB Composite Device" entry beneath its port, with each of its functions loaded beneath that entry (since `2.3.0.0`; before it the functions had no parent entry), rather than one entry with a warning mark |

7.1 was read under `2.0.0.0` in Windows 98 SE virtual machines under NUSB 3.3
and under SweetLow's stack: disable, enable, remove and rescan, with a mouse
attached (roadmap-hcd Phase 26, "the Windows 98 door sequence"). It has not
been read on real hardware.

7.2 checks that nothing idles the controller out from under a plug. The driver
initiates no selective suspend at all (the release notes' "Known
limitations"), so an empty bus is the one condition left to try. **Empty the
bus properly**: any attached device keeps the bus busy, so a machine with an
internal USB device cannot show this either way.

7.3 is the driver's own composite split: Windows' own composite parent
driver is not used on any target, and the composite entry is the driver's
(the release notes, "Devices and hubs in Device Manager"). Read in virtual machines on both primary
targets with the C-Media `0D8C:0014`, which has no IAD (roadmap-hcd 26-A.7).

Windows 2000 SP4

| # | Do | Expected reading |
|---|---|---|
| 7.4 | Unplug everything from the machine's USB ports, leave it a full minute, then plug in a mouse | It enumerates on its own, with no Refresh |
| 7.5 | Record only: look at the Driver tab | Write down the date and version shown. Under `1.x` the version was present and the date read `Not available` |
| 7.6 | Disable the controller in Device Manager, then re-enable it once | It goes and comes back, with no crash |

7.6 was read under `2.0.0.0` in the Windows 2000 virtual machine, under Driver
Verifier, with the remove and rescan beside it (roadmap-hcd 26-V.2). It is also
the mechanism step 8 needs.

Windows ME (virtual machines only)

| # | Do | Expected reading |
|---|---|---|
| 7.7 | Record the USB stack installed, if any | None (stock ME) or SweetLow's USB 2.0 stack; both have been read |
| 7.8 | Plug in one composite device, as 7.3 | As 7.3; the functions were read on the integration build with QEMU's `usb-audio` at a root port and behind a hub |
| 7.21 | With a USB mouse attached, disable the controller in Device Manager, then re-enable it | It goes and comes back with no hang, and the mouse moves. Against `2.0.0.0` do not run it: re-enabling with a mouse or keyboard attached hangs Windows ME there, a known limitation fixed in `2.1.0.0` |

Windows XP (virtual machines only)

| # | Do | Expected reading |
|---|---|---|
| 7.9 | Look in `HKLM\System\CurrentControlSet\Services\USB` for a DWORD `DisableSelectiveSuspend` | Absent: no `2.0.0.0` INF writes it. If it is there, a `1.0.2.0` or earlier package ran on this machine; note it |
| 7.10 | Leave the machine idle for two minutes after boot with nothing plugged in, then plug in a Low-Speed HID | It enumerates and works with no Refresh |
| 7.11 | Plug in a flash drive, then a composite device, each for the first time on this installation | Each binds on its first attach, with no replug needed |
| 7.12 | Disable the controller in Device Manager, re-enable it, then uninstall it and Scan for hardware changes | It goes and comes back each time, with no crash; the Found New Hardware wizard returns on the rescan |

Windows XP x64 (virtual machines only)

| # | Do | Expected reading |
|---|---|---|
| 7.13 | Look in `HKLM\System\CurrentControlSet\Services\USB` for a DWORD `DisableSelectiveSuspend` | Absent, as 7.9 |
| 7.14 | Leave the machine idle for two minutes after boot with nothing plugged in, then plug in a Low-Speed HID | It enumerates and works with no Refresh |
| 7.15 | Plug in a flash drive, then a composite device, each for the first time on this installation | Each binds on its first attach, with no replug needed |
| 7.16 | Disable the controller in Device Manager, re-enable it, then uninstall it and Scan for hardware changes | It goes and comes back each time, with no crash, and the rescan reinstalls with no media prompt |

7.12 and 7.16 were read under `2.0.0.0` on the XP guests in virtual machines
(roadmap-hcd 28-A.2 on XP x64, and the Phase 28 guest legs). 7.10 and 7.14's
Low-Speed device has no virtual-machine vehicle; a Full-Speed one is what the
virtual machines read.

Windows Vista and Windows 7, 32-bit and x64 (virtual machines only)

| # | Do | Expected reading |
|---|---|---|
| 7.17 | Look in `HKLM\System\CurrentControlSet\Services\USB` for a DWORD `DisableSelectiveSuspend` | Absent, as 7.9 |
| 7.18 | Leave the machine idle for two minutes after boot with nothing plugged in, then plug in a Low-Speed HID | It enumerates and works with no Refresh |
| 7.19 | Disable the controller in Device Manager and re-enable it, five times, then uninstall it and Scan for hardware changes | It comes back each time, with no crash and no hang; the rescan reinstalls with no media prompt and, on x64, with no second boot needed. Record how long each enable took |
| 7.20 | x64 only: in test-signing mode (4.10), restart normally. Then turn it off (`bcdedit /set testsigning off`, restart), restart once more with driver signature enforcement disabled (F8), and turn test-signing mode back on | In test-signing mode the two nodes of 4.3 are back on the ordinary boot and the mouse of step 5 works. With it off, the ordinary boot shows Code 39 (Vista) or Code 52 (Windows 7) on the controller and nothing on it works, and the F8 boot brings the nodes and the mouse back. Record each. A run that used F8 alone at 4.10 takes the last two readings only. `SKIP - other target` on 32-bit Vista and Windows 7 |

7.19 was read under `2.0.0.0` in virtual machines: five disable and enable
cycles each on Windows 7 x86 and x64 with no hang (roadmap-hcd 28.3). The
controller disable that hung 32-bit Windows 7 on a ThinkPad E460 under `1.x`
belonged to the miniport under Microsoft's `usbport.sys` and did not occur
under `2.0.0.0`. Windows 7 has not been run on real hardware with `2.0.0.0`.

### Step 8. Produce the log channel, and read the SuperSpeed witness

The point of a log channel is that a stranger can produce one. This step tests
the instruction a user is given, not the driver. That instruction is
`docs/using/release-notes.md`, "The log, and how to send one", and
`readme.txt` sections 6 and 9 and `tools\xhcitr\readme.txt` (up to
`3.0.0.0`, `tools\xhcisnap\readme.txt`), which say the same thing in the
user's words.
Read them for the target under test and do what they say. The table below
carries the commands and what to read back, and nothing else those two
documents own.

It is the same procedure on every target. From `3.1.0.0` everything here
runs from the xHCI98 USB Tree the install put in place, or out of this
release's `tools\xhcitr\` directory: `XHCITR.EXE` on 32-bit Windows,
`XHCITR64.EXE` on 64-bit. Up to `3.0.0.0` it ran out of the release's
`tools\xhcisnap\` directory (`xhcisnap\` at the top of a release before
`2.4.0.0`), the one 32-bit `XHCISNAP.EXE` under WOW64 on the 64-bit
targets; `XHCISNAP -probe` was read that way on Windows XP x64
(roadmap-hcd 28-A.2).

| # | Do | Expected reading |
|---|---|---|
| 8.1 | From `3.1.0.0`: the xHCI98 USB Tree's View > Logging level... (up to `3.0.0.0`, `SNAPON.BAT` from `tools\xhcisnap\`, on Vista and 7 Run as administrator; before `2.4.0.0`, `XHCISNAP -verbosity 2`) | The dialog lists each xHCI controller `xhci98.sys` drives with the level its driver key holds, `2` on a fresh install. If it reads otherwise, set `2` (on 2000 to 7 as a standard user through "Set as administrator..."); the tool writes it to every controller, reads it back and offers the restart. Then, at a prompt: `XHCITR /snapshot -verbosity 2` and `XHCITR /dump -on` are refused, each saying which switches it takes and "Nothing was written.", and write nothing; `XHCITR /setlevel xhci98=5` is refused before anything is read. Up to `3.0.0.0`, `SNAPON.BAT` runs `XHCISNAP -on` (level 2), names each driver key it wrote to and prints the level that key held before; `SNAPON.BAT 0`, `SNAPON.BAT 5` and `SNAPON.BAT "A&B"` are refused by `XHCISNAP` and write no key |
| 8.2 | Restart the machine | This is what makes it take: the driver reads the value when it starts |
| 8.3 | Use a device, or reproduce whatever is wrong; leave 5.8's SuperSpeed device plugged in | Nothing to read yet. An empty note ring at 8.5 is a reading rather than a failure |
| 8.4 | From `3.1.0.0`: File > Save Report... in the xHCI98 USB Tree, then `XHCITR /snapshot` (`XHCITR64` on 64-bit Windows) at a prompt in a directory you can write to (up to `3.0.0.0`, `SNAPGET.BAT`; before `2.4.0.0`, `XHCISNAP -o MYDUMP`) | The report ends with a "Snapshot" part: the device report (the `.DEV` text) once, then for each xHCI controller `xhci98.sys` drives the dump's text (the `.TXT`), slots and enumeration regions included; at level 4 the dump is withheld and the report says so. `XHCITR /snapshot` writes `mDDhhmmS.DEV`, `.TXT`, `.BIN` and `.PSC` in the current directory, not under `C:\`, the name made from the local date and time, and its text (a message box, or the file its output is redirected to) ends "Attach: mDDhhmmS.DEV mDDhhmmS.TXT (the .BIN and .PSC only if asked)" and names the directory. On 98 SE and ME, with the controller disabled in Device Manager (Code 22), the dump is still taken and the `.DEV` and the text say "dump taken from a controller Device Manager reports as disabled (Code 22)". Run it again: the second set takes the next letter and the first is unchanged. Then `XHCITR /snapshot T84`: it writes `T84.DEV`, `.TXT`, `.BIN` and `.PSC`; again, and it is refused (exit 6), says a file of that name is there already, and leaves the four `T84` files as they were; `XHCITR /snapshot A.B` is refused for its dot and `XHCITR /snapshot CON` for the name rule (exit 2), each writing nothing. File > Save Snapshot... into a folder you pick writes a set there and shows the same text |
| 8.5 | Read the `.TXT` file's header | The tool's version and build stamp, the driver's counters, and at level 2 the driver's own note ring below them. A report whose version is not this release's is a report from the wrong build |
| 8.6 | From `3.1.0.0`: View > Logging level... again (up to `3.0.0.0`, `SNAPOFF.BAT`; before `2.4.0.0`, `XHCISNAP -disable`) | `xhci98` reads `2`, the default; if 8.1 or a maintainer set another level, set `2` again. Up to `3.0.0.0`, `SNAPOFF.BAT` runs `XHCISNAP -disable` and reports the channel off again. While the level is above 0, anyone using the machine can read the driver's diagnostic state, and at level 4 that includes internal addresses |
| 8.7 | Record only: look at the driver's own key for `XhciLogVerbosity`, `XhciLogDebugView`, `XhciImodInterval250ns`, `XhciForceBulkOnly`, `XhciFastPollFsLs`, `XhciFirstEnumWaitMs`, `XhciFirstEnumPortMs`, `XhciIntelPortSwitch`, `XhciIntelIrqRoute`, `XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`, `XhciMaxTransferKB`, `XhciInterruptMode`, `XhciPollIdleMs`, `XhciPollActiveMs`, `XhciPollFineTimer` (from `2.4.0.0`), `XhciWideMap`, `XhciSelectiveSuspend`, `XhciSilentInstall` (from `2.4.0.0`), `XhciUsb2HubSpeedFallback`, `XhciAccept096` (from `2.4.0.0`) and the fourteen values of Linux's controller quirks (`XhciMsiAvoid`, `XhciStreamsOff`, `XhciResumeReinit`, `XhciIntelShutdownToEhci`, `XhciShutdownReset`, `XhciSuspendHaltDelay`, `XhciSuspendHaltTimeout`, `XhciAsmediaFlowControl`, `XhciTiIntervalCap`, `XhciStuckLinkReset`, `XhciStreamRingPad`, `XhciControlLinkNoOp`, `XhciIsochSpuriousSuccess`, `XhciAmdSuspendBitClear`; new in `2.4.0.0`), then read the 8.4 report's "registry values" block and its "Linux quirks" block | Write down whether each value is there and what its data is. On a fresh install the INF writes all thirty-six (nineteen before `2.4.0.0`): `XhciImodInterval250ns` `160`, `XhciFirstEnumWaitMs` `5000`, `XhciFirstEnumPortMs` `2000`, `XhciMaxTransferKB` `1024`, `XhciPollIdleMs` `50`, `XhciIntelIrqRoute`, `XhciTolerance`, `XhciIntervalCap`, `XhciPollActiveMs`, `XhciUsb2HubSpeedFallback`, `XhciAccept096` and every quirk value but `XhciShutdownReset` `1`, `XhciLogVerbosity` `2` (from `3.0.0.0`; `0` before it), and the other eleven `0`, `XhciPollFineTimer` and `XhciSilentInstall` among them (an update keeps a value already there, such as the `1` an earlier install wrote for `XhciIntelPortSwitch`, and adds the values an older install never wrote), and the report shows the interval read and in force. Write down the report's lines for the value read and the interval in force, and from the "Linux quirks" block whether the controller is listed for any quirk |
| 8.8 | The SuperSpeed witness: in the 8.4 report, find 5.8's device | The speed the driver decoded from its port, SuperSpeed, and the speed it programmed into the device's slot, SuperSpeed, agreeing. The slot's speed is the report's slot table: `PSIV` is the Speed field of the controller's own Slot Context for the device, and `speed` is that value decoded, `SuperSpeed, 5 Gbit/s, Gen 1x1` for a 5 Gbit/s link. A SuperSpeed device that reads High Speed here ran on the USB 2.0 port of its connector; record which connector it was in |
| 8.9 | From `2.4.0.0`: read the 8.4 `.DEV` file | The controller's Device Manager status and problem code (a started controller, problem `0`), the resources it was given (memory window and interrupt, written down beside 3.4's `bar` and `irq`), and a version and SHA-256 for `xhci98.sys`, `xhciuas.sys`, `xhcihlpr.sys`, `usbd.sys` and `usbui.dll`, each file this target has installed, and for `xhcimsi.sys` where it is still on disk (on 32-bit Vista and 7 it should not be, after 4.9's restart). The `xhci98.sys` hash is the one `readme.txt` gives for the directory installed. Where this target cannot report an item, the file says so rather than leaving it out |

If nothing comes back (8.4): run `XHCITR /probe` (up to `3.0.0.0`,
`XHCISNAP -probe`), which answers whether the route to a driver exists at
all separately from whether this driver answered on it. The ordinary cause
of an empty report is a skipped 8.2. `docs/contributing/snapshot-reader.md`
decodes what the probe prints.

8.8 is the SuperSpeed clause's witness because nothing else on these systems
can be: Device Manager and the other Windows tools read an interface that
predates SuperSpeed and report High Speed at most (design record 13, section
8.4). The port speed and the slot speed are two readings of one fact taken at
two places, so their agreement is part of the reading.

On Windows 98 real hardware, do not run DebugView while capturing; that route
crashed the `1.x` driver's machines and has not been read on `2.0.0.0` there.
The xHCI98 USB Tree (`XHCISNAP` up to `3.0.0.0`) is the route.

Observed: under `2.0.0.0`, `XHCISNAP -probe` answered "the channel is live"
with the channel on and a full `-o` report was read on Windows 98 SE under NUSB
3.3 and under SweetLow's stack and on Windows 2000 (roadmap-hcd 26-A.8, in
virtual machines). In 29-V.1 the decoded port speed and the slot speed were
read from the driver's trace on both primary targets, in virtual machines;
the report's slot table, added for this witness, read a SuperSpeed drive as
SuperSpeed on real hardware under Windows 98 SE at the bench (29-E.1).

### Step 9. Shut down with devices attached

Do: leave devices plugged in, ideally with one of them transferring, and shut
the machine down normally.

Expected reading: the shutdown completes and nothing hangs. Then start the
machine again and confirm the devices come back, as at step 6.

Windows 98, about the test rather than the driver: that system raises a modal
"You must quit this program before you quit Windows" box for every running DOS
program, so a shutdown cannot be measured against a DOS-based load generator
on it at all. Use an Explorer file copy for the live traffic.

If the shutdown hangs: record what was attached, what was transferring, and
how far the shutdown got. Cut the power only after writing that down.

Observed: a clean shutdown with devices attached closed every guest leg of
roadmap-hcd Phases 26 to 28 in virtual machines; with traffic moving at the
shutdown, record only.

---

## When something fails

Report it; do not diagnose it. File an issue with
`.github/ISSUE_TEMPLATE/bug_report.yml`, attach step 3's probe log (the
`mDDhhmmS.LOG` `QUALREP` wrote; `PROBE.LOG` before `2.4.0.0`), and, where
Windows still runs, the report File > Save Report... wrote at 8.4 (up to
`3.0.0.0`, the `.DEV` and `.TXT` files `SNAPGET.BAT` wrote),
with the readings from the run, and say which substep and which reading.
`docs/using/release-notes.md`'s "Known limitations" is worth reading first,
since several of the things a tester will meet are measured and published, but
report it anyway if it is not there.

Two things make a report worth far more than the failure alone, and both are
free at the time:

- The same device on a root port and behind a hub. Identical behaviour in both
  places rules out a hub-specific defect and leaves this driver's common code,
  the class driver above it and the device; different behaviour points at the
  hub path.
- The step 1 machine record. Nearly every hard question this project has had
  to answer about a bare-metal result turned out to be a question about the
  machine's BIOS.
