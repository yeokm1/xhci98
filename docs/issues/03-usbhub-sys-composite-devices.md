# Issue 3 - Composite USB devices on Windows 98 need `usbhub.sys`, and an xHCI-only machine never has it

Status: solved, shipped in `0.0.0.4`.

Targets affected: Windows 98 SE on any machine whose only USB controller is
xHCI, which is every machine this driver exists for.

The short version: a multi-interface ("composite") device such as a keyboard
with media keys, a headset with buttons, or a sound card with a volume knob
appeared as `USB Composite Device` with Code 2 and did nothing, on a root port
and behind a hub alike. For two weeks the cause was written down as "NUSB
ships no composite parent driver". The cause was one file. Windows 98's own
`usbhub.sys` is copied by Setup only when it finds a USB controller it
recognises, so on an xHCI-only machine it is never there, and NUSB does not
carry it either because NUSB assumes a machine that already has a USB 1.1
stack. The package now installs it. It was found by a laptop that had no role
in the test plan.

---

## 1. The problem

On Windows 98 the `USB.INF` binding for a composite device's parent devnode is
the hub driver, `usbhub.sys`, the same binary that drives USB 1.1 hubs. It is
not a separate "generic parent" file as on Windows 2000 (`usbccgp.sys`).
`USB.INF` itself is always present on a Windows 98 install; only the driver
files it names are conditional, and Setup copies them when it enumerates a
UHCI or OHCI controller during installation.

A machine whose only USB controller is xHCI gives Setup nothing to recognise.
It installs no USB stack, and `usbd.sys`, `uhcd.sys`, `openhci.sys` and
`usbhub.sys` never leave the CAB. The NUSB 3.3 back-port, which is what makes
`usbport.sys` and therefore this driver possible on Windows 98, ships
`USBPORT.SYS`, `USBHUB20.SYS`, `USBEHCI.SYS`, `USBSTOR.SYS`, `USBAUTH.SYS`,
`USBNTMAP.SYS`, `USBU2A.SYS`, `NTMAP.SYS` and a 1394 set. It carries no
`usbd.sys` and no `usbhub.sys`, because every machine NUSB was written for
already had them from Setup.

So a composite device's devnode is created, `USB.INF` matches it to
`usbhub.sys`, and the loader cannot find the file. Device Manager reports
Code 2 ("matched and unloadable") and names the loader, not the file.

One distinction worth keeping: NUSB's `usbhub20.sys` is a hub and parent
driver, just not the one `USB.INF` binds a Windows 98 composite to. "NUSB
ships no parent driver" is the wrong sentence.

## 2. How it was discovered

### 2.1 The shape of the problem was seen first on a different file

The Windows 2000 VM's first boot with an EHCI attached bugchecked
(`STOP c000026c`, "`usbhub20.sys` could not be loaded, status `0xc0000034`")
with `usbhub20.sys` visibly present and the right size. `dumpbin /imports`
showed the missing object was an import, `USBD.SYS`, and SP4's `usb.inf` put
`usbd.sys` in a `CopyFiles` section referenced only by the USB 1.1 controller
installs. An xHCI-only machine never gets it. The same day, `dir usb*.sys` on
the Windows 98 VM listed only the NUSB set, no `USBD.SYS`, and the LESSONS
entry noted in passing that "the absence of `uhcd.sys`, `openhci.sys` and
`usbhub.sys` alongside it identifies the cause."

`usbhub.sys` was named a month before anyone read that sentence as a
prediction. The per-target `usbd.sys` carry (distinct media names
`usbd98.sys` / `usbd2k.sys`, `COPYFLG_NO_OVERWRITE`, SHA-256 manifest in
`scripts/package/usbd-sources.expected`) was built then, and it is the
mechanism the `usbhub.sys` fix later reused verbatim.

### 2.2 The symptom on metal, with the wrong diagnosis

The first bare-metal run on the E460 recorded a multi-interface HID device
coming up as `USB Composite Device`, Code 2, root port and behind a hub alike.
The task filed for it ("the composite-device gap", eventually `13-E.1`) said:
"multi-interface (composite) USB devices cannot bind under the NUSB stack ...
NUSB 3.3 ships no composite generic-parent driver", and proposed deriving
whether a Windows 2000 `usbccgp.sys` could load under Windows 98's NTKERN.
Every clause of that turned out wrong, and the derivation was never needed.

Two things then muddied the water for two weeks.

A Code 2 on the wrong devnode. The E460 showed Code 2 on the controller,
because the machine had been restored to base Windows 98 with no NUSB, so
`usbport.sys` was absent and the driver's imports failed. That is the Phase 3
load-time gate, not a composite finding, and `run-13e.md` now warns: "A
`Code 2` on the controller devnode is not a composite-device finding."

A VM that worked, for the wrong reason. The Windows 98 QEMU guest binds
QEMU's `usb-audio` composite fine, but that guest is not a plain NUSB
install; the SE CD's native USB stack had been installed into it earlier to
satisfy `uhcd.sys`. The whole stack went in at once, so the one file was
never isolated, and the control looked like a pass.

### 2.3 The bench inventory that held the answer

For batch 13-E the E460 was reinstalled from scratch. Stage E1.0's
pre-install check ran four `dir` commands and recorded:

```text
E1.0.1 pre-state:    usbd.sys ABSENT   usbhub.sys ABSENT   uhcd.sys ABSENT
```

It was filed as "baseline is clean". Stage E2 then tested four devices, three
vendors, three speeds:

| Device | Kind | Root port | Behind hub |
|---|---|---|---|
| C-Media `0D8C:0014` | Full-Speed composite | `USB Composite Device`, Code 2 | same |
| MS Wired Keyboard 600 `045E:0750` | Low-Speed, 2 HID interfaces | Code 2, "and it does not type" | - |
| SB Play! 2 `041E:323D` | Full-Speed composite | `Unknown Device`, Code 22 (a different defect, EP0 max packet size; see the README) | same |
| SB X4 `041E:3278` | High-Speed, 7 interfaces | HID child Code 10 | - |

Conclusion at the time: "Windows 98 on plain NUSB does not load a composite
parent driver for composite devices, whatever they contain." True, and still
not the cause.

## 3. How it was troubleshot: a control that held everything else constant

The break came from a ThinkPad X61 (ICH8M: UHCI + EHCI, no xHCI) running
the same Windows 98 SE and the same NUSB 3.3. It owned no clause in any phase
and was not in the plan. Both composite units worked on it.

The diagnostic that named the file was the X61's `USB Composite Device`
devnode: Device Manager -> Driver -> Driver File Details reads `usbhub.sys`.

| | X61 (UHCI/EHCI) | E460 (xHCI only) |
|---|---|---|
| `USB.INF` | present | present |
| `usbhub.sys` | present | absent |
| result | composites bind | devnode named, Code 2 |

One difference. Before copying anything, the safety question was checked
host-side: `usbhub.sys` imports `HAL.DLL`, `NTOSKRNL.EXE`, `USBD.SYS` and
`WMILIB.SYS`, the identical set `usbhub20.sys` imports, and that driver was
already loading on the E460. `tools\win98se-extracted\usbhub.sys` (35,680
bytes, SHA-256 `E898B75F...FC31`) went into `C:\WINDOWS\SYSTEM32\DRIVERS\`,
the devnode was removed, cold start: "the keyboard types, the Play! 2
works." The commit that recorded it: "The composite gap was usbhub.sys not
being on the machine, and an X61 that owns no clause in this phase is what
broke it open."

The planned stage E5 (install the SE CD's entire native USB stack on the
E460, one-way) was struck without running. The variable was one file.

### Verified by removal, not by symptom match

Renaming `USBHUB.SYS` to `.BAK` on the Windows 98 VM turned QEMU's own
`usb-audio` composite into Code 2. That composite had been measured binding
there in Phase 9 for an unrelated reason, so it was a pre-existing control.
The bench defect reproduced in a VM for the first time. Reinstalling the
package restored it:

```text
USBHUB20 SYS    50,032   01-16-04    NUSB's 2.0 hub
USBHUB   SYS    35,680   08-23-26    placed by this package's INF
USBHUB   BAK    35,680   04-23-99    the renamed original, untouched
```

The 1999-versus-today timestamps show the install genuinely wrote it, and
`COPYFLG_NO_OVERWRITE` placed ours only because the destination was absent.

## 4. How it was solved

The fix commit's summary: "The package carries Windows 98's composite parent,
because an xHCI-only machine never gets it from setup and every composite
device on one is dead without it."

The INF. `src/xhci98.inf` gained a Windows 98-only copy section, reached from
both the device-install and right-click-Install paths:

```text
[Xhci.CopyW98]
usbd.sys,usbd98.sys,,16
usbhub.sys,usbhub98.sys,,16
```

`[Xhci.CopyW2K]` carries `usbd.sys` only. The asymmetry is intended and the
INF's comment block explains it: Windows 2000's composite parent is
`usbccgp.sys`, which Setup places from `driver.cab` unconditionally. That is
a structural argument the project cannot observe, because Windows 2000 Setup
bugchecks on both xHCI-only machines it has (see the README).

The source. Windows 98 SE's own file, `usbhub.sys` 4.10.2222, extracted from
`BASE5.CAB` (the CAB that `layout.inf` in `PRECOPY1.CAB` names:
`usbhub.sys=5,,35680`) and hash-pinned in
`scripts/package/usbd-sources.expected`. Older notes say `BASE4.CAB`; the
hash-verified derivation from the media's own `layout.inf` is `BASE5.CAB`.

The gate. `scripts/inf-gate/check-inf.ps1` enforces the asymmetry in both
directions with seven rules (`W98-MISSING`, `W98-ONWIN2K`, `W98-DUP`,
`W98-MEDIANAME`, `W98-FLAGS`, `W98-TARGET`, `W98-DEST`), each with a
self-test in `test-inf-checks.ps1` that watches it fire. The manifest
parser's "at most one source file per target" rule was dropped; it had only
ever been the same sentence as "one per destination" while `usbd.sys` was the
only per-target file.

The release. `0.0.0.4` ships three Microsoft files with the driver:
`usbd98.sys`, `usbd2k.sys` and `usbhub98.sys`. They travel in the GitHub
release download only and are never tracked in git;
[legal-provenance.md](../contributing/legal-provenance.md) section 5 records
that redistribution permission is not established. The published limitation
that had blamed NUSB was struck rather than edited, because the diagnosis in
it was wrong, not merely incomplete.

The `0.0.0.4` release history entry, for users:

> A device that is more than one thing at once ... stopped at `USB Composite
> Device` with `Code 2` and did nothing. Windows 98 needs `usbhub.sys` for
> these, and it only installs that file when Setup finds a USB controller it
> recognises. On an xHCI-only machine it never does, so the file was never
> there. This package now installs it, and leaves any copy you already have
> alone.

## 5. Second-order effects

Audio got worse before it got better. With the composite parent bound,
`USBAUDIO.VXD` on the E460 bugchecked (`fatal exception 00 ... in VXD
USBAUDIO(01) + 00002ED4`), so a user's experience changed from "the device
never appears" to "the device appears and then Windows falls over". At bench
session 3 a UAC 1.0 device then played clean on the E460, root port and
behind a multi-TT hub (Finding X), refuting that published limitation too.

Later in the same bench session (Finding D) the composite bound at the first
hot-plug stage on its own, closing the carve-out stage E2 had opened.

Documentation debt. Four documents kept naming NUSB as the cause for two
more days, the release readme template printed every carried file as "...'s
own USBD.SYS" (which shipped that way in the `0.0.0.4` readme), and a
hand-written "four files" count was wrong the moment the list grew to five.
The release generator now derives every count.

## 6. What is still open

- The reverse rename on the E460 itself (take `usbhub.sys` away on the bench
  machine and watch Code 2 return) is owed as a nicety, not a hole; the VM
  rename supplies that direction on a second vehicle.
- The Windows 2000 equivalent is structural and unobserved, and published as
  a limitation. Every Windows 2000 vehicle the project has carries an EHCI,
  the same confound that hid the Windows 98 gap.
- `WMILIB.SYS`, which both hub drivers import, has no row in the SE CD's
  `layout.inf`. Where it comes from on a working machine is recorded rather
  than solved.

## 7. Lessons the record kept

From lessons.md:

- On an xHCI-only machine, assume no USB file Windows ships is present.
  Windows placed none of them, because it never saw a controller. `usbd.sys`
  taught this earlier and it was read as a fact about `usbd.sys` rather than
  as a class of problem. It is a class of problem.
- A control that holds the OS and the stack constant is worth more than any
  amount of reasoning about the OS and the stack. The X61 settled in one
  reading what two days of inference had not.
- Code 2 means matched-and-unloadable and should be read as "which file is
  missing?", not "which driver is wrong?" The dialog names the loader, not
  the file, so the file has to come from a working machine's Driver File
  Details.
- A pre-install inventory is evidence, not ceremony. Stage E1.0's four `dir`
  commands contained the answer and were filed as "baseline is clean".

## Sources

`docs/contributing/lessons.md`'s `usbhub20.sys`/`USBD.SYS` and
composite-parent entries ("Windows 98's composite parent is
`usbhub.sys`..."); `docs/contributing/runs/run-13e.md` (Finding 1, stages
E1.0/E2/E5, the P4 hash record, session 2 Finding D);
`docs/contributing/build-and-test.md` ("What NUSB 3.3 does not ship");
`src/xhci98.inf` comment block above `[Xhci.CopyW98]`;
`scripts/inf-gate/check-inf.ps1`; `scripts/package/usbd-sources.expected`;
`docs/contributing/legal-provenance.md` section 5; `releases/history.md`
`0.0.0.4`.

### 2026-09-02: the carry is retired, the diagnosis stands

Release 1.0.0.1 stops carrying the file. `src/xhci98.inf` names
`LayoutFile=layout.inf` and the Windows setup engine copies `usbhub.sys` (and
`usbd.sys`) from the operating system's own install source, the CABs on the
hard disk or the Windows 98 CD, still with `COPYFLG_NO_OVERWRITE` and still
on the Windows 98 path only. The gate rules became the `OS-*` family
(`OS-MISSING` / `OS-ONWIN2K` keep the two directions above) and
`scripts/package/usbd-sources.expected` is gone. `legal-provenance.md`
section 5 and `build-and-test.md`, "The files the OS supplies", have the
record; nothing above about the cause or the symptom changes.

### 2026-09-03: the Windows 2000 asymmetry was wrong

The argument above that Windows 2000 needs no `usbhub.sys` from this INF
rested on "Setup places it from `driver.cab` unconditionally". It does not.
Both NT targets' `layout.inf` give `usbhub.sys` the text-mode disposition
that does not copy it at Setup, the same as `usbport.sys` and `usbd.sys`,
and a Windows XP guest installed with no USB controller had none of the
three (roadmap Phase 19; `build-and-test.md`, "The files the OS supplies",
has the disposition table). The composite-parent half of the argument
stands: on the NT targets that role is `usbccgp.sys`'s, and this issue's
symptom is Windows 98's. Since 1.0.1.0 `[Xhci.CopyNT]` copies `usbhub.sys`
too, with the same flag, and `OS-ONWIN2K` is retired; the rule that
survives it is `OS-ONWIN98`, for `usbport.sys`, which the Windows 98
`layout.inf` cannot resolve.
