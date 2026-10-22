# Run 43 - Release `3.2.0.0`

Phase 43 of `roadmap-usb4.md`, for release `3.2.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

## 43.14 read first: whether a VID/PID INF binds a refused interface (2026-10-22)

The reading task 43.15 waited on (was NOX.1's reading; design record 13
section 10.7.1, whose plan of 2026-10-13 it follows except where said).
A storage interface with no transport the bus can run - a UAS-only device
at SuperSpeed on a controller that does not stream - is refused: its one
hardware id is `USB\XHCI98_NOXPORT&VID_v&PID_p&REV_r`, and it has no
compatible id, but until 43.15 its device id kept the `USB\VID_v&PID_p`
form, which task 38.2 found in the 98 SE devnode's `HardwareID` value.
The question: does an INF whose only line is that VID/PID form bind it?
Runtime, development host A.

### The vehicle

- QEMU 11.1.0 under TCG, headless; each guest 1 vCPU (`pentium3,-apic`),
  256 MB, `-action reboot=shutdown`. The controller
  `qemu-xhci,p2=1,p3=4,streams=off`: port 3 is a SuperSpeed port with no
  USB 2.0 companion, and the controller reports no streams.
- The device: `usb-uas` with a `scsi-hd` (a 128 MiB FAT16 image),
  hot-plugged on port 3 after the desktop, `attached` set, each plug with a
  serial never seen before (`NOXW98V1`, ...). VID/PID `46F4:0003`, REV
  `0000`. A `usb-tablet` on port 1 for the pointer.
- The driver: the published `3.1.0.0` x86 package
  (`releases\3.1.0.0`), installed from its INF through the controller's
  Found New Hardware wizard. Every release since `2.0.0.0` answers a refused
  interface's ids the same way, so no build of this branch was needed.
- Three guests, each on its own overlay:
  - **98 SE**: installed fresh from the 98 SE CD on 2026-10-20 for task
    43.5 (no NUSB, nothing added), then `3.1.0.0`.
  - **98 SE + NUSB 3.3**: the same, then NUSB 3.3 English (`nusb33e.exe`,
    its agreement accepted in the guest, owner) and a restart.
  - **ME**: stock Windows ME (`vm\t37me\winme-stock-base.qcow2`), then
    `3.1.0.0`.
- The probes, this project's own INFs written for the reading, never in the
  tree: `Class=USB`, an install section that only writes one value
  (`NoxProbe`) under the driver key, and `.NT` sections with
  `AddService=,2`. Probe V's one line is `USB\VID_46F4&PID_0003`; probe
  C's is `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000`.
- "Binds": the probe's line is chosen for the devnode - its `DeviceDesc`,
  `Mfg`, `Driver` key and Device Manager name come from the probe.

### Results

| Guest | First plug, probe V offered at the wizard | Second plug, V already installed, index deleted |
|---|---|---|
| 98 SE | Wizard for "USB Attached SCSI HBA"; at `D:\PV` it named "NOX probe V device id", `D:\PV\PROBEV.INF`, and installed it. Device Manager: "NOX probe V device id" under Universal Serial Bus controllers | No wizard, no prompt: bound to probe V |
| 98 SE + NUSB 3.3 | The same | The same; "Updating hardware information database" shown at the plug, then bound |
| ME | The same (ME's wizard: "Specify the location", the folder typed) | The same as NUSB's |

Every one of the six devnodes read the same (`regedit /e` of
`HKLM\Enum\USB\VID_46F4&PID_0003` and of `...\Services\Class\USB`, taken
off the disk after a clean shut down), for example 98 SE's second:

```
[HKEY_LOCAL_MACHINE\Enum\USB\VID_46F4&PID_0003\NOXW98V2]
"HardwareID"="USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\VID_46F4&PID_0003"
"DeviceDesc"="NOX probe V device id"
"Class"="USB"
"Driver"="USB\\0003"
"Mfg"="NOX probe"

[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\0003]
"NoxProbe"="V"
"InfPath"="NOXPRO~1.INF"
"InfSection"="Probe.Dev"
"MatchingDeviceId"="USB\\VID_46F4&PID_0003"
```

`MatchingDeviceId` is the device id, the second entry of the `HardwareID`
value; the PDO answers only the first as a hardware id. So Windows 98 SE,
with and without NUSB 3.3, and ME match INF lines against the device id the
configuration manager adds to that value, and an INF whose only line is a
refused device's `USB\VID_v&PID_p` binds it - silently, with no wizard,
once that INF is installed: the wizard had copied the probe into
`INF\OTHER` (`NOXPRO~1.INF`, "NOX probePROBEV.INF"), and with
`DRVIDX.BIN` and `DRVDATA.BIN` deleted before the second plug, the index
was rebuilt and the new device bound from it.

For the second plug the guests were shut down and relaunched cold (98 SE
excepted, which took it in the same session).

### Not read

- Probe C, the control: not offered. V bound, which answers the question C
  was there to qualify (C's line is the id the PDO answers as its hardware
  id).
- Windows 2000 SP4 and NT's matching in general (documented as hardware and
  compatible ids only): not read. Route 2 of the plan (a serial-number
  device's devnode, installed in its working form, reloading its driver in
  the refused form): not read. Neither can bind a refused device after
  43.15, whose device id is project-owned and differs from the working
  form's.
- The NUSB 3.3 `USBSTOR.INF`'s own VID/PID lines: none names `46F4:0003`,
  so they were not exercised; a listed device would match the same way.

### What it decides

By record 13 section 10.7.1's decision 1, the refused PDO's device id
moves to `USB\XHCI98_NOXPORT&VID_v&PID_p[&MI_nn]` on every target: task
43.15.

## 43.16 the legs: a refused interface under the new device id, 98 SE, ME and 2000 (2026-10-22)

NOX.1's legs (design record 13 section 10.7.1) for task 43.15's change,
on the three guests 43.14 read, each continued from the disk 43.14 left:
probe V (`USB\VID_46F4&PID_0003`) installed in `INF\OTHER` and bound to
the two refused devnodes 43.14 made; and on Windows 2000 (below).
Runtime, development host A.

### The vehicle

- As 43.14's: QEMU 11.1.0 under TCG, headless, 1 vCPU, 256 MB,
  `qemu-xhci,p2=1,p3=4,streams=off`, a `usb-uas` with a `scsi-hd` (a fresh
  overlay of the 128 MiB FAT16 image) hot-plugged on port 3 after the
  desktop, `attached` set, a serial never seen before (`NOXW98N1`,
  `NOXN98N1`, `NOXMEN1`).
- The driver: this branch's x86 release `xhci98.sys` (the gated build of
  `66840a9e`, 300,271 bytes, SHA-256
  `81E163EC145B5A99996E5E2F0456F6083A6A4247AFB0B3E786A4C309FF4E005D`),
  copied over `3.1.0.0`'s in `C:\WINDOWS\SYSTEM32\DRIVERS` from an MS-DOS
  Prompt; the INF and the rest of `3.1.0.0` unchanged. The guest shut
  down, the copy read back off the disk with the same SHA-256, then a cold
  boot.
- Guests: 98 SE, 98 SE + NUSB 3.3, ME, as in 43.14.

### Results

| Guest | The plug | The wizard pointed at `D:\PV` (probe V) | The new devnode |
|---|---|---|---|
| 98 SE | Add New Hardware Wizard for "USB Attached SCSI HBA" | "Windows was unable to locate a driver for this device"; Finish: "Unknown Device", no driver installed | `Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW98N1` |
| 98 SE + NUSB 3.3 | The same | The same | `...\NOXN98N1` |
| ME | The same (ME's wizard) | "Windows was unable to locate the software for this device"; Finish | `...\NOXMEN1` |

Every new devnode read the same (`regedit /e` of `HKLM\Enum\USB` and of
`...\Services\Class\USB`, taken off the disk after a clean shut down), for
example 98 SE's:

```
[HKEY_LOCAL_MACHINE\Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW98N1]
"HardwareID"="USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\XHCI98_NOXPORT&VID_46F4&PID_0003"
"DeviceDesc"="USB Attached SCSI HBA"
"Class"="Unknown"
"ConfigFlags"=hex:40,00,00,00
```

No `Driver` value and no new `Class\USB` key: probe V, already installed
and offered again at the wizard, did not bind, on any of the three. The
device id is the second `HardwareID` entry, as 43.14 read for the old form.
The old devnodes, `Enum\USB\VID_46F4&PID_0003\<serial>` (43.14's two per
guest), were left as they were - the same `HardwareID`, `Driver`
(`USB\0002`, `USB\0003`) and probe V's `Class\USB` keys as 43.14 read -
by decision 4; the device did not return to them.

### Windows 2000

The plan's NT target, on a fresh overlay of the project's clean Windows
2000 image (`vm\t26\win2k-clean.qcow2`), the same controller and
device:

1. `3.1.0.0` installed through the controller's Found New Hardware
   wizard from the package's INF, then a restart.
2. A refused `usb-uas` plugged under `3.1.0.0` (serial `NOXW2KO1`), and
   the wizard pointed at probe V's folder: "Windows was unable to locate a
   driver for this device"; "Skip driver installation" chosen.
3. The gated `xhci98.sys` of `66840a9e` copied over `3.1.0.0`'s in
   `C:\WINNT\system32\drivers`, the guest shut down, the copy read back off
   the disk with the same SHA-256, a cold boot.
4. The same device plugged again (the same serial): a new wizard, probe V
   offered again, "unable to locate a driver", skipped.

`regedit /e` of `HKLM\SYSTEM\CurrentControlSet\Enum\USB` and of the USB
class key, off the disk after a clean shut down:

- Step 2's devnode, `Enum\USB\VID_46F4&PID_0003\NOXW2KO1`, its
  `HardwareID` the one id the PDO answered,
  `USB\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000`: 2000 does not add the
  device id to that value as 98 SE and ME do, and probe V's
  `USB\VID_46F4&PID_0003` line did not bind the old form either.
- Step 4's devnode, `Enum\USB\XHCI98_NOXPORT&VID_46F4&PID_0003\NOXW2KO1`,
  its `HardwareID` the two NOXPORT ids, no `Driver`; the old devnode left
  beside it, not present (no `Control` key), by decision 4.
- No USB class key past the controller's and the root hub's.

### Not read

- A refused function of a composite device (`&MI_nn`): host vectors only.

## 43.22 Windows 8 and 8.1, a target of the connection manager: what NT 6.2 and 6.3 need (2026-10-22)

Branch `p43-22`, from `3.2.0.0` at `46e7d2af`. The roadmap entry lists what
an install on NT 6.2 and 6.3 needs that 42-A.21 did not read; each item
below says what was read and how. The install, update and uninstall legs,
the emulator's tunnel legs and the NVMe legs are 43.24's; this task read
the install alone, one per system, with the published `3.1.0.0` package
(the installer as 42-A.21 coded it; `release-x64\xhcicm.sys` SHA-256
`b643972abf8f940c2fd5f07ecb1b55871a8a5e5f78433a210f718e74f2444de0`, an
embedded test signature, security directory 1,496 bytes; `release-x86`'s
`d7202c39e01450e051b65496ad66d7e94e6a537c8a601729ae0971a4465f602b`,
unsigned).

### The vehicle

Development host A, QEMU, headless, TCG: the usb4-emu QEMU
(`vm\usb4\bin-2a893ea`, SHA-256 `6a1dfcc8...bd66`, the hash every kit
checks), `-machine pc` (SeaBIOS: no Secure Boot), `-cpu
qemu64,+3dnowprefetch`, smp 2, a `qemu-xhci` with a tablet, and the
emulator's `tb3-alpine-ridge` NHI (`8086:15BF`) with its tunnel bridge
(`1B36:0001`) and xHCI - the `p43-33` kit's topology. Overlays of the
read-only pristine images of the `win8-10` set: Windows 8 Pro x86
(6.2.9200), Windows 8 Pro x64 (6.2.9200) and Windows 8.1 Pro x64
(6.3.9600, with Update); test-signing mode on in each, as built. **No
Windows 8.1 x86 image exists** (no medium; owner, 2026-10-22: 8.1 32-bit
is skipped for `3.2.0.0`, recorded as untested ground, covered by 8 x86
and 8.1 x64). A guest script (`read.ps1`, kit `E:\xhci98-vm\p43-22`) read
`SystemStartOptions`, `sc qc`, the PCI devices' problem codes and
drivers, the `UpperFilters`, and `setupapi.dev.log` onto a second disk,
read after power-off with 7-Zip; the guest ran `INSTALL /console /cmonly
/yes`, elevated, from a VVFAT drive. The host was saturated (another
agent's builds and a dozen guests): nothing here is a timing. Kit ports
48620 to 48622.

### Results

| System | `INSTALL /console /cmonly /yes` in test-signing mode | On a start with signature enforcement disabled | After |
|---|---|---|---|
| 8 x86 | **installs**: two "Windows can't verify the publisher" prompts (`xhcicm.inf`, `xhcicmf.inf`), answered; `Cm.Dev6.NTx86` and `CmF.Dev6.NTx86`; NHI on `xhcicm`, started; bridge filtered, started; `xhcimark` running; exit 3 | - | `xhcicm` `BOOT_START` ("Updated service 'xhcicm' to be boot start"); with test-signing off and on again, each start reached the desktop with the NHI and the bridge at problem 0 and `xhcicm` running |
| 8 x64 | **refused**: "xhcicm.inf could not be staged (error 0xE000022F)"; `setupapi.dev.log`: "Driver package does not contain a catalog file, and Code Integrity is in Test Signing mode" / "The third-party INF does not contain digital signature information"; "No changes were made", exit 2 | installs as on 8 x86 (the two prompts; "Driver package does not contain a catalog file, but user wants to install anyway"); `Cm.Dev6.NTamd64`, `CmF.Dev6.NTamd64`; NHI and bridge started, exit 3 | `xhcicm` `BOOT_START`; the next start, test-signing mode off, stopped in "Attempting repairs" (Automatic Repair) and stayed there through two launches, over an hour |
| 8.1 x64 | **refused**, the same error and log lines; the local copy made, exit 2 | installs as on 8 x64, exit 3 | `xhcicm` `BOOT_START`; a plain start in test-signing mode (`SystemStartOptions` `TESTSIGNING NOEXECUTE=OPTIN`) reached the desktop with the NHI on `xhcicm`, started, and the bridge filtered: the test-signed SHA-1 binary loads in test-signing mode |

`SystemStartOptions` on a start with enforcement disabled read
`TESTSIGNING NOEXECUTE=OPTIN DISABLE_INTEGRITY_CHECKS` (8 x64, 8.1 x64);
in test-signing mode `TESTSIGNING NOEXECUTE=OPTIN`; with it off
`NOEXECUTE=OPTIN`. `HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot`
holds `AvailableUpdates` alone and `Confirm-SecureBootUEFI` answers "Cmdlet
not supported on this platform" on these SeaBIOS guests. The installer
named the systems "Windows 8" and "Windows 8.1" (`RtlGetVersion`), offered
the connection manager alone and left Stor98 out, as 42-A.21 coded; its
x64 note said "Test-signing mode is on: the test-signed x64 connection
manager loads." and the staging then failed. The console tool waits for
Enter at its end even with `/yes` and its output redirected; a script feeds
it `< NUL`.

### What the reading settles, item by item

- **The INFs' decorations**: right as they are. 8 and 8.1 take
  `NTx86.6.0` / `NTamd64.6.0` and the `Dev6` install sections, as the host
  vectors of `inst_models_section` say; `Signature="$CHICAGO$"` and
  `$Windows NT$` both accepted. No INF change.
- **The x64 test signature with Secure Boot off and test-signing on**: the
  binary loads (8.1 x64, a plain start; 8 x64 in the start with
  enforcement disabled), but the package does not install in test-signing
  mode: Windows 8 and 8.1 x64 refuse an INF with no catalog there. Record
  19's premise that a catalog governs only the prompt holds for Vista and
  7 and not for 8 and 8.1.
- **Boot start**: Windows 8 and 8.1 make `xhcicm` boot start through the
  bridge filter (32-bit too). On x64 that turns test-signing mode from
  "needed for the driver to load" into "needed for Windows to start". Read
  on 8 x64 only, with a hung Automatic Repair; the cause is inferred from
  the boot-start change and the test signature, not read from a boot log.
  64-bit 8.1 and 32-bit 8 read `BOOT_START` too.
- **32-bit 8 and 8.1 with Secure Boot on**: Microsoft's "Driver Signing
  Policy" page ("Signing requirements by version"; `ms.date` 2024-08-19,
  read 2026-10-22) lists "64-bit, 32-bit" for "Windows 8, Windows 8.1,
  Windows 10, versions 1507, 1511 with Secure Boot on", against "64-bit
  only, no signature required for 32-bit" for "Windows Vista, Windows 7;
  Windows 8+ with Secure Boot off". It does: so the note of record 23
  section 5.10 covers the x86 package there (owner, 2026-10-20). With
  Secure Boot off (SeaBIOS) 32-bit 8 loaded the unsigned driver with
  test-signing mode on and off. Secure Boot on is unread: no guest here
  has it.
- **The installer's 8 and 8.1 path as 42-A.21 coded it**: version, name,
  the connection manager alone, Stor98 left out: as designed. Wrong: on
  8 and 8.1 x64 it went on in test-signing mode into a staging Windows
  refuses, said nothing of why, and its note promised the driver would
  load; and it would install outside test-signing mode with a note, which
  on 8 and 8.1 x64 leaves a machine that does not start. Fixed, commit
  `6c9e49f1` and the commit that closes this task (record 23 section
  5.10, revision 30; numbered 30 because `p43-34` and `p43-27` take 27
  and `integ` and `p43-42` take 28 and 29): `inst_cm_boot_gate` refuses the connection manager on 8 and 8.1 x64 unless the package is
  test-signed and test-signing mode is confirmed, and warns when it goes
  on; `inst_stage_error_text` explains `0xE000022F` there and names the
  start without enforcement; on 32-bit 8 and 8.1 `inst_sig_note_kind`
  prints the Secure Boot note, and a Code 52 there is reported as that
  refusal. Host vectors in `test_inst.c`. The three decisions key on the
  kind `inst_os_kind` gives (`INST_OS_CMONLY`), not on 6.2 and 6.3, and
  their texts say "this Windows", so Windows 10 below build 22000, which
  `p43-34` (task 43.34) admits to that kind, takes them on the merge;
  whether 10 makes `xhcicm` boot start and refuses the catalog-less
  package in test-signing mode is 43.34's to read. The note that told 8
  and 8.1 x64 users that test-signing mode needs Secure Boot off, after
  the test-signing note, is gone: the gate's texts say it on those
  systems, and the branch is dead code otherwise (`p43-34` edits the same
  lines for 10: on the merge, take the removal).
- **8.1 with Intel's own Thunderbolt software**: not read in a guest (no
  image of it at hand; 43.24's leg, "where an image of it is at hand").
  Read from the code: a controller another driver holds is foreign and
  left (decision D3), so the NHI keeps Intel's driver; the filter still
  goes onto the bridges, where it does nothing without this driver on the
  NHI (record 21 section 3.2) - but on 8.1 x64 it makes `xhcicm` boot
  start all the same, so the gate applies. Intel's release notes for its
  Thunderbolt software 16.3.61.275 (as reposted on a vendor forum, not
  read on Intel's site) list Windows 7 32- and 64-bit, 8.1 64-bit and 10
  64-bit, Falcon Ridge and Alpine Ridge, and call it the last package for
  7 and 8.1; no 8 and no 8.1 32-bit.
- **README and the release notes**: moved from "never run" to what was
  read here (install alone), the legs still 43.24's; 8.1 32-bit in
  "Untested ground".

### Builds

| Build | Result |
|---|---|
| `xhciinst\build.cmd`, `6c9e49f1` | passed |
| host suites (`test\run-host-tests.cmd`), `6c9e49f1` | 0 failures (`test_inst` 1790 checks) |
| host suites, the closing commit (the gates keyed on the kind, Secure Boot's state) | 0 failures (`test_inst` 1798 checks) |
| `xhciinst\build.cmd`, `build-driver.cmd all`, `all -amd64`, `544cfcd4` | passed (`BUILD + GATES PASSED (debug release qemu)` for x86 and amd64; a first run failed the self-test's "every screen within 79 columns" on an 80-column line of the x86 Secure Boot note, rewrapped before the commit) |

### Not read

8.1 32-bit (no medium). Secure Boot on, either architecture (no UEFI
guest). The way back from Automatic Repair on 8 x64 by a start with
enforcement disabled: the guest stayed at "Attempting repairs" through
two launches (13:51 to 15:04 on the second), never reaching the menu, and
was closed; the warning's way back rests on Microsoft's documented
Startup Settings, not on a reading. Whether 7 x64 also makes `xhcicm`
boot start (the same filter; 42-A.21's 7 leg was 32-bit, and no x64 leg
has read `sc qc xhcicm`): owed, and if it does, 7 x64 has the same hazard
and the gate should cover it. Intel's own Thunderbolt software on 8.1 (no image). The update and
uninstall on 8 and 8.1, the
tunnels, eject and NVMe (43.24). The new installer in a guest: its
refusal, warning and notes rest on the host vectors and the self-test's
screens (a package with an x64 half needs the signing key, which this
session did not have).

### Secure Boot's state (owner, 2026-10-22)

The owner accepted `p43-34`'s design (record 26 section 5.3.2): the
installer reads `UEFISecureBootEnabled` (on, off, not known) and shows it
in the signature note on 8, 8.1 and 10, never changing it; with Secure
Boot on, test-signing mode never counts as confirmed and the user is told
to turn Secure Boot off first. Coded here (record 23 section 5.10,
revision 30): `inst_sb_state`, `inst_sb_applies`, `inst_sb_text`,
`inst_ts_confirmed`, keyed on the connection-manager-alone kind so 10
takes them; on 32-bit 8 and 8.1 a known state replaces the Secure Boot
note by its case. Host vectors for the three states on each system and
through the gate. The value is read on no guest here (SeaBIOS only).

The owner's answers the same day: Intel's own Thunderbolt driver on a
controller is another vendor's device and is left alone, on 8.1 and 10
alike (as the code does: the NHI is foreign and left); the per-system
"untested" warning is 43.37's.

### Owner questions

1. A signed catalog for the x64 package's INFs (record 19 decided against
   one): with a test-signed catalog, 8 and 8.1 x64 would install in
   test-signing mode with no start without enforcement. Recommendation:
   yes for `xhcicm.inf` and `xhcicmf.inf` at least, signed with the same
   test key the packaging scripts sign the x64 binaries with (no script
   here makes a catalog yet); Vista and 7 accept a catalog-less package
   now, so they lose nothing. It does not cure item 2 of record 23's
   revision 30: the boot driver still needs test-signing mode.
2. Whether 7 x64 also makes `xhcicm` boot start through the filter: if it
   does, 7 x64 on starts with enforcement disabled (F8 at every start) is
   the same hazard, and the gate should cover it. Recommendation: read it
   in 43.24's or 43.31's 7 x64 leg (`sc qc xhcicm` after the install) and
   widen the gate to Vista and 7 x64 if so.
3. On a controller another driver holds (Intel's software, left alone
   as the owner decided), the filter still goes onto its bridges, which
   run Windows' own `pci`, where it does nothing, yet on 8 and 8.1 x64 it
   makes the system depend on test-signing mode. Recommendation: leave
   the filter out too where no NHI in the run is this driver's (a small
   installer change, host-testable), as a later task.
