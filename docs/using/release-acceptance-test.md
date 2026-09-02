# Release Acceptance Test

What someone does, in order, to a machine this project has never seen, with a
release it has never run, and what they must see at each step.

This is a fixed procedure. It is re-run unchanged against every release, by
someone who was not there when the driver was written, on hardware nobody here
has characterised. It was written for `1.0.0.0` and applies to every release
after it. The release under test is named once, at the head of whatever record
the tester keeps; everything version-specific below is a citation of that
release's own files, `readme.txt` and `releases/history.md`, rather than a copy
of them, so a new version needs no edit here.

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

Two things the test never does on Windows 98, and both bite before the step
that says so. Do not disable, remove or upgrade the driver in Device Manager:
under NUSB's stack, which is what this test installs, each of the three
blue-screens that system at `0028:C00312EE`, and 7.1 is where that is
recorded (under SweetLow's stack they complete; the release notes say so, and
this test does not cover that stack). Do not cycle one device rapidly in and out of a port: that
can freeze the machine and it is this driver's own defect (release notes,
"Known limitations").

---

## Equipment

Properties, not models. A tester holding different hardware has to be able to
tell whether theirs will do. `docs/contributing/test-equipment.md` is the
characterisation record for the hardware this project holds, and
`scripts/hub-characterise.ps1` is how any of it was read.

### Cannot start without

| # | What | The property that matters |
|---|---|---|
| 1 | An xHCI machine | PCI class code `0C0330`, at least one USB 2.0 port, a memory window below 4 GB, and a legacy interrupt pin. Step 3 confirms all four. A controller with no interrupt pin cannot be driven on either target, and there is no software workaround |
| 2 | One target OS, already installed and working | Windows 98 SE (4.10.2222) with NUSB 3.3 already installed, or Windows 2000 SP4. Do not install NUSB on Windows 2000. One OS per run: a dual-boot machine is two runs and two records |
| 3 | A PS/2 or built-in keyboard and pointing device | A USB keyboard on the controller under test is unusable during the DOS pass and can stop responding mid-run. On a laptop the built-in keyboard is normally i8042-attached, but that is per machine; confirm it rather than assuming (`docs/contributing/build-and-test.md`, "Bootstrapping xHCI-only machines") |
| 4 | A real-DOS boot medium, and a way to get a file off it | MS-DOS or FreeDOS on floppy, CD or USB key, booted without EMM386, a V86 monitor or a paging memory manager, but with `HIMEM.SYS` available, which is not one of those and which the qualifier may need (step 3). Not a DOS box inside Windows: the qualifier needs memory it can address one-to-one. Step 3 leaves `PROBE.LOG` on it, and that file is the run's first artefact |
| 5 | A way to put the package on a machine whose USB does not work yet | Pull the disk and stage from another machine, burn a CD, or use a network share. On an xHCI-only machine there is no USB until this driver works; that is the chicken-and-egg this driver exists inside (`docs/contributing/build-and-test.md`, "Bootstrapping xHCI-only machines"). Pre-stage generously: every forgotten file is another disk swap |
| 6 | A way back | A recovery rung that survives a driver which loads and then fails. Windows 98: the Startup Menu and Safe Mode, both of which run before this driver's BIOS handoff. Windows 2000: F8, and the Recovery Console pre-installed with `winnt32 /cmdcons` while USB still works. Test-boot the rung once before the first install, not after |

### What an individual clause needs

A clause whose device is absent is a SKIP with a reason, which is a result.

| Clause | Device | The property that matters |
|---|---|---|
| 5.1 | A Low-Speed HID | 1.5 Mbps signalling: an older mouse or keyboard. `scripts/hub-characterise.ps1` reads the negotiated speed as `Low` |
| 5.2 | A High-Speed flash drive | 480 Mbps. A USB 3.0 stick also serves: it falls back and enumerates at High Speed through this driver, which is itself worth recording |
| 5.3 | A hub, plus a device to put behind it | Any hub. Record whether it is self- or bus-powered and, if the tester can read it, its TT class: `bDeviceProtocol` 1 = single-TT, 2 = multi-TT, 0 = a Full-Speed USB 1.1 hub |
| 5.4, optional | A USB Ethernet adapter | With a driver for the target OS. Without one it enumerates and does nothing, which tests the stack above this driver rather than this driver. It is the cheapest sustained bulk-IN load a tester is likely to have |
| 5.5, optional | A USB Audio device | With a driver for the target OS. On Windows 98, read the USB Audio entry in the release notes' "Known limitations" before running this: one physical UAC 1.0 device has played clean on real hardware, and what QEMU's emulated device shows there (a CD prompt on a second arrival, and on an older guest a fault inside that system's own `USBAUDIO.VXD`) is that stack and not this driver. Keep the installation CD to hand |
| 7.3 | A composite device | One physical unit that is more than one thing at once: a headset with buttons, a keyboard with media keys. Windows 98 only; this is the clause the package's `usbhub98.sys` exists for |
| Step 8 | Nothing extra | The log channel is `XHCISNAP.EXE` out of this release, one setting it writes for you, and a restart of the machine |

### Suggested devices

The properties above are what a clause needs. These are units this project has
actually measured, offered as a shopping list for a tester who would rather buy
or borrow something known than characterise what is in the drawer. Nothing here
is required, and a device that satisfies the property is as good.
`docs/contributing/test-equipment.md` carries the full readings.

| Clause | Suggested unit | Why this one |
|---|---|---|
| 5.1 | Logitech USB Optical Mouse `046D:C077`, or any pre-2005 USB mouse or keyboard | Verified Low Speed: HID boot mouse, one interrupt IN at `bInterval=10`. Most modern HIDs enumerate at Full Speed, so a new mouse off the shelf usually does not satisfy this clause |
| 5.2 | SanDisk U3 Titanium `0781:5408`, or any USB 2.0 stick. A USB 3.0 stick such as `090C:2320` or `0781:55AB` also serves | The 2.0 unit fails safe: it is High Speed wherever it is plugged. A 3.0 unit in a USB 3.0 connector is quietly testing the fallback as well, which is worth recording but is a second reading, not this one |
| 5.2 | A USB-to-SATA enclosure, such as the ASMedia `174C:5106` | The only storage unit this project has round-tripped a file through on real silicon. Both targets ship BOT-only storage drivers, so a bridge that also offers UAS still runs BOT here |
| 5.3 | Terminus 4-port `1A40:0101` (single-TT) or 7-port `1A40:0201` (multi-TT) | Plain USB 2.0 hubs, no SuperSpeed half, one chip and one tier, so what is behind the hub is behind one transaction translator. Adjacent product IDs from one vendor, so the pair changes only the TT class |
| 5.3 | Avoid a hub enclosure containing cascaded chips, such as Genesys `05E3:0608` | Its sockets are not equivalent: three sit at tier 1 and four at tier 2, with different route strings and a different TT, so a reading depends on which socket was used |
| 5.4 | An ASIX AX88772-based adapter, such as `0B95:7720` | ASIX parts have Windows 98 and Windows 2000 drivers. Read the chipset, not the packaging: the RTL8153 in most modern USB-C dongles has no driver for either target, and driver absence is the commonest way a validation result gets misattributed |
| 5.5, 7.3 | C-Media `0D8C:0014`, the generic chip in a very large number of cheap USB audio adapters, or a Creative Sound Blaster Play! 2 `041E:323D` | UAC 1.0, Full Speed, and composite: four interfaces with an HID alongside the audio, so one unit serves both the audio clause and the composite clause |
| 5.5 | Not a UAC 2.0 unit such as the Sound Blaster X4 `041E:3278` | Measured on real silicon: it does not bind. Both targets ship UAC 1.0 audio drivers, and that unit offers no UAC 1.0 fallback configuration |

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
| 1.3 | Record the target OS and its build, and whether any previous version of this package was ever installed here | Windows 98 SE (4.10.2222) or Windows 2000 SP4, and `none` or the version. A machine that had one produces an upgrade result, which is a different measurement and is not what this test measures (the release notes' "Known limitations", the Windows 2000 upgrade entry) |

1.2 comes before anything else rather than as an afterthought because on Intel
7- and 8-series chipsets a BIOS setting decides which controller owns the USB
2.0 ports: `XUSB2PR` routes them between EHCI and xHCI, so the same machine
presents a working xHCI or one with nothing on it depending on a setting nobody
recorded. On an Intel 7/8-series machine this typically reads as "USB 3.0
Mode"; on another machine it will read as something else, or not exist at all.

If a field cannot be answered: write what was looked at and why it could not be
answered. A blank is not a result.

Observed: the record format is `xhciqual/hardware-testing.md`'s, with
`xhciqual/results/e460-2026-08-22/README.md` as a worked example. `XUSB2PR`'s
behaviour is derived from the Intel datasheet and from Linux, and has never
been measured on any machine this project has had; see
`docs/usb-xhci-info/xhci-programming.md`, "Firmware Handoff, and the
Controller Deviations This Driver Acts On".

### Step 2. Check the download

Do: unzip the release asset and look at what came out.

| # | Do | Expected reading |
|---|---|---|
| 2.1 | Unzip the asset and list the top level | No top-level directory: `readme.txt`, `LICENSE`, `release\`, `debug\`, `xhciqual\` and `xhcisnap\` come out directly |
| 2.2 | Check that `xhcisnap\` is one of them | Present. It is what step 8 needs, so a download without it is a cut made with `-SkipSnapTool` and step 8 cannot be run against it. Report that rather than skipping the step |
| 2.3 | List `release\` and `debug\` against the file list in `readme.txt` section 3 | Each holds every file that section names. Read the list off that file rather than from memory, because it can change between releases |
| 2.4 | Look for a version directory nested inside another | None |

`RELEASE\` is the one to install. `DEBUG\` is the same driver built for
diagnosis and is for a machine that has already gone wrong. It carries no
per-line trace either; that lives only in the never-published `qemu` flavour.

If a flavour directory holds only `xhci98.inf` and `xhci98.sys` (2.3): this is
a copy taken from the source repository, not the download. Either fetch the
release asset, or complete it per `readme.txt` section 3, "Completing a copy
taken from the repository". Do not install it as it stands. The missing files
are the ones nothing on an xHCI-only machine ever placed, and their absence
surfaces at step 4 as a fault that looks like this driver's.

If one directory nests another copy of the version inside itself (2.4): stop,
and report the asset rather than the driver. That is a packaging defect and it
has happened: an asset was built with the published tree nested one level
deeper than the Microsoft files, leaving no directory in the download holding a
complete install set.

Observed: the layout and the assertion that protects it are
`scripts/package/make-release.ps1`, `New-UploadSet`; the nesting defect and its
fix are recorded in that function's own comments, and the reason the asset
carries the three Microsoft files that the tracked tree does not is
`docs/contributing/legal-provenance.md` section 5.

### Step 3. The DOS pass

| # | Do | Expected reading |
|---|---|---|
| 3.1 | Boot real DOS with `XHCIQUAL.EXE` from the download's `XHCIQUAL\` directory, keeping `XHCIQUAL.MAP` beside it. No EMM386, no V86 monitor or paging memory manager, and not a DOS box inside Windows | A prompt on a machine whose memory the qualifier can address one-to-one |
| 3.2 | `XHCIQUAL` | One of three verdicts: `LOOKS QUALIFIED`, `DISQUALIFIED` or `CANNOT SAY`. `LOOKS QUALIFIED` is the one that continues |
| 3.3 | `XHCIQUAL --probe-only --no-page --log PROBE.LOG` | Read-only, writes nothing to the machine, and leaves `PROBE.LOG`, the run's first artefact |
| 3.4 | Read the controller `FACT` line out of `PROBE.LOG` | `hciver`, `ports`, `usb2ports` (the managed and unmanaged split: this driver drives the USB 2.0 ports and leaves the USB 3.0 ones alone), `pin`, `bar` and `irq`, each written down |

If the tool will not run at all (3.1): add `HIMEM.SYS` before concluding
anything about the machine. It is not what "no memory manager" excludes (the
exclusion is EMM386, V86 monitors and paging managers), and the tool runs
32-bit through an embedded DOS/32A extender, so it needs extended memory. The
line is `DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V` in `CONFIG.SYS`, with the path
pointed at wherever `HIMEM.SYS` actually is. A program that never started has
produced no verdict, which is a different record from `DISQUALIFIED`; say which
one this was. (`xhciqual/hardware-testing.md`, "Safety and preparation" step 1.)

`DISQUALIFIED` on the interrupt pin is a stop. A controller reporting `pin=0`
cannot be driven on either target: neither Windows 98 nor Windows 2000 has an
MSI path, and there is no software workaround. Record the verdict, complete the
record, and stop. That is a complete result about the machine.

`CANNOT SAY` sends the tester to the BIOS, not to the driver: something the
tool is not allowed to change is in the way, usually the controller powered
down or its memory access switched off. Change the setting, cold-boot, and
re-run.

Keep the logs. `PROBE.LOG` goes with everything else the run produces, and
`xhciqual/hardware-testing.md`, "What to record for each machine", is the shape
to follow for the machine's own details.

Observed: the verdicts and the command line are `readme.txt` section 1;
"Interrupt Pin 0 is an unconditional failure because neither target has an MSI
path" is the roadmap's Phase 0 checkpoint. Real bare-metal passes of this
shape are in `xhciqual/results/`.

### Step 4. Install, from `RELEASE\`

Do: follow `readme.txt` section 4 for the target. Point at a directory, never
at a loose `xhci98.sys`; nothing about a copied file says which flavour it is.

| # | Target | Do | Expected reading |
|---|---|---|---|
| 4.1 | Windows 98 SE | NUSB 3.3 first, then Device Manager, the unclaimed xHCI controller, Properties -> Driver -> Update Driver -> Specify a location -> `RELEASE\` | The install completes without asking for a file it cannot find |
| 4.2 | Windows 2000 SP4 | Device Manager, the controller, Properties -> Driver -> Update Driver -> Have Disk -> `RELEASE\` | The same |
| 4.3 | Both | Look at Device Manager when the install is done | The two nodes below, and neither carries a warning mark |
| 4.4 | Windows 98 SE | Look for the two cosmetic readings and note them | `xhci98.tmp` left in `System32\Drivers` and listed in Driver File Details (cosmetic; the loaded binary is the real one), and the Driver tab showing a date but no version (release notes, "Known limitations"). Neither is a failure and neither should be reported as one |

The two nodes of 4.3, as Device Manager shows them:

```
USB 2.0 eXtensible Host Controller (xhci98)
    USB Root Hub
```

The controller string is the INF's, as written; the root hub is the system's
own.

If the root hub fails with `0xc0000034` naming `usbhub20.sys`: the per-target
`usbd.sys` is missing. That is step 2's failure arriving late; the package
carries that file and a repository copy does not.

If the controller reports `Code 10` (Windows 2000): the driver loaded and then
failed while bringing the controller up. Record it together with the whole of
step 1's machine record; this is the one failure mode where the two supported
systems behave very differently.

If Windows 98 stops with `Windows protection error. You need to restart your
computer.`: the same fault, in that system's response to it. Recover through
Safe mode per the failed-start entry in the release notes' "Known
limitations", which carries the steps and
records that recovery is complete and loses nothing.

Observed: the device string is `src/xhci98.inf`'s `XhciDesc`. The `0xc0000034`
failure is `readme.txt` section 3 and `docs/contributing/lessons.md`, the
`USBD.SYS` lesson. `Code 10` versus the Windows 98 protection error was
measured in both virtual machines (roadmap task 12.3; release notes, "Known
limitations").

### Step 5. Devices, one at a time, then a hub

Do: with the machine running, take each device in turn on a root port. Plug it
in, confirm it enumerates, use it, unplug it, plug it back in. Then assemble a
hub at a root port with children behind it, and repeat for the children. One
device at a time, and do not cycle a device rapidly: on Windows 98 fast
repeated plug and unplug of the same device can freeze the machine, and that is
this driver's own defect (release notes, "Known limitations").

| # | Device | Do | Expected reading |
|---|---|---|---|
| 5.1 | A Low-Speed HID | On a root port: plug in, use it, unplug, plug it back in | It appears with the right identity and no warning mark, the pointer moves or the keys type, unplugging removes the node, and replugging brings it back with no Refresh and no prompting |
| 5.2 | A High-Speed flash drive | The same, and round-trip a file: write one to it and read it back | A drive letter appears and the file reads back with matching contents, and the node comes and goes with the device |
| 5.3 | A hub at a root port, with children behind it | Assemble it, then take 5.1 and 5.2 again on the children | Every child named and working, and the hub itself carrying no warning mark. Record whether the hub is self- or bus-powered and, if it can be read, its TT class |
| 5.4 | A USB Ethernet adapter (optional) | On a root port, then behind the hub: bring it up and pass traffic | It takes an address and passes traffic |
| 5.5 | A USB Audio device (optional) | On a root port: play something through it | It plays. On Windows 98 read the release notes' USB Audio entry before running this at all |
| 5.6 | Every device above | Record the negotiated speed wherever the OS will show it | `Low`, `Full` or `High`. A USB 3.0 device on a USB 3.0 connector is expected to fall back and run at High Speed: the SuperSpeed half of every connector is left switched off by design |

If a device is only noticed after pressing Refresh in Device Manager (Windows
98): that is the idle-sleep symptom, and it points at step 7's
`DisableSelectiveSuspend` check rather than at the device. Take step 7 now, and
say in the record that it was reached this way.

If a device does not enumerate at all: record its VID/PID, its speed if
anything reports one, and whether it behaves the same on a root port and behind
the hub. That pair is the discriminating reading, and it should be read for
what it excludes and no more. The same fault in both places rules out a
hub-specific defect (Route String, TT, topology) and leaves everything the two
paths share: this driver's common code, the usbport/class stack above it, and
the device itself. Different behaviour points at the hub path without proving
it. Bench session 1 is the reminder not to read "identical in both places" as
"above this driver": two devices failed identically in both places and the
cause was this driver's EP0 initial-MPS assumption (`test-equipment.md`,
"`bMaxPacketSize0`").

Observed: enumerate/work/unplug/replug on real xHCI silicon is
`docs/contributing/runs/run-13e.md` stages E3 and E4 (E460), across HID, a
two-tier hub tree and a USB Ethernet adapter, the last taking a DHCP lease and
five parallel ping streams. The 20-cycle unplug/replug per device class passed
on all three virtual vehicles in batch 11-V. The storage round-trip has been
measured once on real silicon: the `174C:5106` USB-SATA bridge at a root port
on the E460 took a drive letter, and a file written to it read back with
matching contents (stage E4.2's I/O half, `run-13e.md`). A tester's file copy
is therefore a repetition on another release and another machine; record it as
one more reading. The USB 2.0 fallback on a USB 3.0 connector is `readme.txt`
section 5 and release notes "What this is not".

### Step 6. Reboot with devices attached

Do: leave the devices plugged in and restart the machine normally.

Expected reading: the machine boots, and every device comes back with no
prompting: no wizard, no Refresh, no replug.

If a device does not come back: try one replug and record whether that
recovers it. A device that returns on a replug but not on a boot is a different
result from one that returns on neither, and the record should say which.

Observed: batch 11-V, on both systems. Restarting brought all three device
classes back with no prompting, on a bus that had been under live traffic at
the shutdown (roadmap batch 11-V).

### Step 7. The target-specific clauses

Take the block for the target under test. The other block's clauses are
`SKIP - other target`.

Windows 98 SE

| # | Do | Expected reading |
|---|---|---|
| 7.1 | Do not disable, remove or upgrade this driver in Device Manager. Disabling the USB Root Hub is fine | Nothing to see: the clause is a prohibition, and what the record says is that it was respected. Each of the three blue-screens the machine at `0028:C00312EE` under NUSB's stack, the one this test installs |
| 7.2 | Look in `HKLM\System\CurrentControlSet\Services\USB` for a DWORD `DisableSelectiveSuspend` | Present, value 1 |
| 7.3 | Plug in one composite device, something that is more than one thing at once | It enumerates and its functions load, rather than `USB Composite Device` with `Code 2` and nothing above it |

7.1 is not this driver: Microsoft's own `usbehci.sys` does the same on the same
machine. A tester who needs the driver gone uses the unload-first route in
`readme.txt` section 5. (Release notes, "Known limitations".)

7.2 without the value, the USB stack idle-suspends the controller within about
half a second of the last transfer, and a device plugged in afterwards is
noticed by nothing until Refresh, which is what step 5 would have shown.
(Measured on the Windows 98 virtual machine; release notes, "Known limitations";
`src/xhci98.inf`'s `[Xhci.AddReg.Global]` and the comment block below it.)

7.3 is what the package's `usbhub98.sys` is for: Windows 98 Setup places its
composite parent only when it finds a USB controller it recognises, so an
xHCI-only machine never got one. (Established by remedy on the E460, roadmap
task 13-E.1 and `run-13e.md` Finding D, and cross-checked on a non-xHCI machine
running the same OS and the same NUSB. The release carries the file, so it has
no limitation for it.)

Windows 2000 SP4

| # | Do | Expected reading |
|---|---|---|
| 7.4 | Look for `DisableSelectiveSuspend` after the install | Absent, by design: the NT install path omits it |
| 7.5 | Look at the Driver tab | The version is present and the date reads `Not available` |
| 7.6 | Disable the controller in Device Manager, then re-enable it once | It goes and comes back, with no crash |

7.4 is absent because that system's native `usbport` never idle-suspends this
controller. Finding the value means something else on the machine wrote it;
record that, it is not a failure of this package. (`src/xhci98.inf`:
`[Xhci.Dev.NTx86]` carries only `Xhci.AddReg.NT`, and the comment block states
the omission is intended.)

7.5 is expected and is not a failed install: the engine reads this package's
`DriverVer` and declines the date half specifically. (Roadmap task 12.4,
measured on a Windows 2000 guest; release notes, "Known limitations".)

7.6 Windows 2000 disables, re-enables and uninstalls without crashing; the blue
screen in the other block is that system's, not this driver's. This is also the
mechanism step 8 needs. Upgrade is a separate matter and is not tested here: on
Windows 2000 installing a newer package over an older one is refused rather
than crashing, and there is a manual step that works (release notes,
"Known limitations"). (Batch 11-V, measured on that target; `readme.txt` section 5.)

### Step 8. Produce the log channel

The point of a log channel is that a stranger can produce one. This step tests
the instruction a user is given, not the driver. That instruction is
`docs/using/release-notes.md`, "The log, and how to send one" and "DebugView", and
`readme.txt` sections 6 and 9, which say the same
thing in the user's words. Read them for the target under test and do what they
say. The table below carries the commands and what to read back, and nothing
else those two documents own.

It is the same procedure on both targets, which is itself worth confirming.
Everything here runs out of this release's `XHCISNAP` directory.

| # | Do | Expected reading |
|---|---|---|
| 8.1 | `XHCISNAP -verbosity 2` | It names each driver key it wrote to and prints the level that key held before, once per xHCI controller the machine has |
| 8.2 | Restart the machine | This is what makes it take: the driver reads the value only when it starts |
| 8.3 | Use a device, or reproduce whatever is wrong | Nothing to read yet. An empty note ring at 8.5 is a reading rather than a failure |
| 8.4 | `XHCISNAP -o C:\MYDUMP` | It writes `C:\MYDUMP.TXT` and prints the resolved absolute path it wrote it to |
| 8.5 | Read that file's header | The tool's version and build stamp, the driver's counters, and at level 2 the driver's own note ring below them. A report whose version is not this release's is a report from the wrong build, which is what the tool's version check exists to make visible |
| 8.6 | `XHCISNAP -disable` | It reports the channel off again. A machine left with the channel on is a machine whose diagnostic state anyone using it can read |
| 8.7 | Record only: look at the driver's own key for `XhciLogVerbosity` and `XhciLogDebugView` | Write down whether each is there and what its data is. There are two values and both are DWORDs. This is not a pass criterion; do not fail the step on it |

If nothing comes back (8.4): run `XHCISNAP -probe`, which answers whether the
route to a driver exists at all separately from whether this driver answered on
it. The ordinary cause of an empty report is a skipped 8.2. A machine whose own
USB stack already owns `HCD0` leaves this driver at `HCD1` or `HCD2`, so try
`-c 1` and `-c 2` before concluding anything. `xhcisnap/README.md` decodes the
four probe controls, including that the first is state-dependent and that `6`
is its shipping value.

An empty note ring is a reading, not a failure. The report says why it is
empty, and a machine still at level `0` (every fresh install) is the commonest
reason.

Windows 98 only, and a separate route from this step's: do not run DebugView on
real hardware while capturing. Plugging a device in while it captures crashes
the machine, measured across three device classes. That route is closed on that
target and `XHCISNAP` is what replaced it.

8.7 is record-only for a reason. A key still holding `XhciLogFile` or
`XhciLogSnapshot` is a leftover from something other than this package, which
places neither, and is worth writing down as one. That the
installer writes the two values has never been observed on either system; no
task before the release takes the reading. An acceptance run on a
fresh guest is the first thing that could, so the step asks.

Observed: both the release and the debug build handed their log
to `XHCISNAP` on the ThinkPad E460 under Windows 98 SE (task 13-L.3), and the
same route was exercised in the Windows 2000 SP4 guest against SP4's own
`usbport.sys` 6681 on the same day (`xhcisnap/README.md`, "What has actually
been executed, and what has not"). An earlier ring-0 file sink, since retired,
produced a 2,107-byte log at a user-chosen path on Windows 2000 (task 11-V.9);
it is named here only so that its absence is not read as a regression. The
DebugView bugchecks are `docs/contributing/build-and-test.md`, "Getting a
trace off a bare-metal machine", with the three E460 addresses in
`run-13e.md`'s DebugView-check session record.

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

Observed: batch 11-V, taken twice on each system with the bus moving traffic,
on Windows 98 with a write slowed on purpose so it could not finish before the
shutdown committed. Nothing was recorded as broken and the transfer books
closed across the teardown each time (roadmap batch 11-V).

---

## When something fails

Report it; do not diagnose it. File an issue with
`.github/ISSUE_TEMPLATE/bug_report.yml`, attach `PROBE.LOG` and the readings
from the run, and say which substep and which reading.
`docs/using/release-notes.md`'s "Known limitations" is worth reading first,
since several of the things a tester will meet are measured, published, and not
this driver, but report it anyway if it is not there.

Two things make a report worth far more than the failure alone, and both are
free at the time:

- The same device on a root port and behind a hub. Identical behaviour in both
  places rules out a hub-specific defect and leaves this driver's common code,
  the stack above it and the device; different behaviour points at the hub
  path. This is the control this project uses on itself, and bench session 1
  is the reminder that "identical in both places" was once this driver's own
  EP0 assumption, not something above it.
- The step 1 machine record. Nearly every hard question this project has had
  to answer about a bare-metal result turned out to be a question about the
  machine's BIOS.
