# Issue 20 - On Windows 2000 a create of the controller's taken HCD name kept xhci98.sys loaded, and the next start failed with Code 31

Status: **fixed for `3.3.0.0`** (roadmap-usb4 task 44.81; design record 13
section 8.8). Read on Windows 2000 in QEMU with the built package, and the helper legs read 98 SE, ME, XP SP3 and 7 SP1 x86 on the same kit (`runs/run-44.md`, "44.81"). The fix takes effect from the first start of `3.3.0.0`: an update from `3.2.0.0` or earlier on a 2000 machine with two controllers on this driver, or with Windows' EHCI started after it, may still end at Code 31 and ask for the restart, once.

**Scope.** Windows 2000, with a controller on `xhci98.sys` and anything
that later tries to create the same `\DosDevices\HCD<n>` name: Windows'
own EHCI stack starting after it, a second controller of this driver
(before `22654b48`), or a program. Then a restart, disable, update or
uninstall of the controller fails until Windows restarts. XP SP3 and 7
SP1, both x86, were read with a `3.3.0.0` test build from before the fix
(the same `HCD<n>` names and collisions) and showed no Code 31. Not read
for the leak: Vista, XP x64, 8, 8.1 and 10. Windows 98 SE and ME are not affected by
the change and keep `HCD<n>`.

Machine: QEMU `q35`, TCG, one `pentium3`, 512 MB, Windows 2000
Professional SP4 on the Standard PC HAL (the q35 base `w2k`). The
controllers: QEMU's `qemu-xhci` behind a root port, a second `qemu-xhci`
on the root bus, and QEMU's ICH9 EHCI functions on Windows' `usbehci.sys`.

## 1. What was seen

Found by the pre-cut matrix (`runs/run-44.md`, "44.76"): on the
emulated ASM4242 board the update over `3.2.0.0` left the board's own
xHCI at Code 31 ("did not end as installed"), and the run asked for a
restart. The board layout restarts that controller as the driver's last
one (`runs/run-44.md`, "44.81", part 1 of the cause).

Then, on plain q35:

- two controllers on this driver: the second disabled, the first
  restarted - **Code 31**, restart needed;
- one controller beside Windows' EHCI driver: the controller restarted -
  **Code 31**;
- one controller alone: disable and enable - fine.

After each failure `\Driver\xhci98` was gone and `xhci98.sys` was still
in the module list.

## 2. The captures

Guest memory was dumped through QEMU's monitor (`dump-guest-memory`) and
the symbolic-link objects were read out of their pool blocks offline.

- After the failure beside the EHCI (dump `r2`): the driver object alive
  with no device object and its unload invoked; the controller's FDO
  deleted but alive with one pointer reference; and one symbolic link
  holding it. That link was this driver's own `HCD0`, after its
  `IoDeleteSymbolicLink`: no name, flags `22h` (no longer permanent), one
  pointer reference, no handle, its `LinkTargetObject` the FDO. Windows'
  EHCI, `\Device\USBFDO-0`, had no `HCD` name at all.
- Every `\??` link to a device object in the dumps carries that device in
  `LinkTargetObject`. A link whose target names another link carries
  none.

## 3. Why

On Windows 2000 a create of a `\DosDevices` name that already exists
fails, and it adds a pointer reference to the link that holds the name.
Nothing releases it. That link holds its device, so the holder's FDO and
image stay after the remove, and the I/O manager will not start the
driver again until Windows restarts. The collider's own failed create
leaves nothing.

Read on 2000 (`runs/run-44.md`, "44.81", "The proper fix"):

- boot u: with nothing else running, a program's
  `NtCreateSymbolicLinkObject` of `\??\HCD0` failed with `C0000035`,
  and the next restart of the controller was Code 31;
- boot n: when this driver was the collider (a program held `HCD0`) it
  started and unloaded normally.

Who collides. Windows 2000's `usbport.sys` takes the lowest free
`\Device\USBFDO-<i>` and then creates `\DosDevices\HCD<i>` once, with no
retry [static]. With this driver holding `HCD0` or `HCD1`, a usbport
controller that started later at that index collided (boot w: the second
EHCI got no `HCD` name). XP SP3 and 7 SP1 x86 show that missing name too,
but no Code 31 (the helper leg, `E:\xhci98-vm\p44\p44-c31x\`).

## 4. The fix

First, `22654b48`: a driver-wide record of the numbers this driver's
controllers hold, so a second controller never tries the first one's
`HCD<n>`. That closed the two-controller case and not the EHCI one.

Then `bf7595c7` (`src\xhci_dname.c`, `hcd_door.c`; host suite
`test_dname`):

- 98 SE and ME: `\DosDevices\HCD<n>` at the first free `n`, as before.
- NT: the door is `\DosDevices\XHCI98HC<k>`, a name only this driver
  makes, from 0 up.
- 2000, XP and XP x64: also `\DosDevices\HCD<m>`, whose target is the
  string `\DosDevices\XHCI98HC<k>`, from `HCD9` down. `usbui.dll`'s
  whole-machine walk (the control-panel walk, and the notification
  popups where they appear) opens
  `\\.\HCD0` to `\\.\HCD9` and passes over a gap [static], so it still
  finds the controller. From the top the alias meets usbport only when
  more than ten USB host controllers run at once.
- On NT each name is probed first (`IoGetDeviceObjectPointer`) and passed
  over when anything answers. A failed probe or create takes the next
  number, within 64 tries.
- Each name is deleted as made, once, and its number given back only
  when the delete succeeded or the name was already gone.

Why the alias pins nothing, read on 2000 with the change built in (`w2i`,
boot c): the alias's `LinkTargetObject` was NULL (dump `c0`). A program's
create of `\??\HCD0` collided (`C0000035`), and the controller's restart
then started, with `xhci98.sys` unloaded and loaded at another base. In
dump `c3` the orphan was the same object as the alias before the
collision (body `81FF4AD0h`), with no name, one pointer reference and no
`LinkTargetObject`.

XHCITR follows: it scans `XHCI98HC0` to `63` beside `HCD0` to `63`, and
`/c N` opens `XHCI98HC<N>` on NT (then `HCD<N>` for an older driver) and
`HCD<N>` on 98 SE and ME.

Read on 2000 with the built package (the gated build of `c260abd1`,
`runs/run-44.md`, "44.81", "The legs on the built package"):

- a program's create of the alias `\??\HCD9`, then a restart: started,
  `xhci98.sys` loaded at another base;
- Windows' EHCI beside it: the EHCI kept `HCD0`, and with the EHCI
  disabled, the controller restarted and the EHCI enabled, each started
  and the EHCI took `HCD0` again;
- two controllers: the second disabled and the first restarted, started;
- the board layout's forced update over itself: the board's own xHCI
  started as the driver's last controller;
- `XHCITR /snapshot /c 0` on `\\.\XHCI98HC0`, the Advanced and Power
  tabs, and Safely Remove.

## 5. What was tried and not taken

- Reading the failed create as the culprit. The first dump fitted both
  readings; boot n (this driver the collider, no leak) and boot u (a
  program the collider, the holder pinned) settled it, and dump `c3`
  showed the identity.
- High `HCD<n>` numbers alone on NT. usbport still meets them once it has
  that many controllers, and the 2000 and XP walk stops at 9.
- No `\DosDevices` name on NT, the interface only. The 2000 and XP pages
  would have needed an interface path in `SymbolicName`, never read, and
  their control-panel walk would lose the controller.
- `ObMakeTemporaryObject` on the orphan. It removes a name, not a pointer
  reference.

## 6. Limitations

- The probe opens what a name names. A link whose target is gone reads
  "not found" and is created over. Such a link names no live device, so
  the collision keeps nothing that its own link did not already keep.
- A program that creates `\??\XHCI98HC<k>` while this driver holds it
  would still pin the FDO. Nothing but a deliberate test does that.
- XHCITR `3.2.0.0` opens `\\.\HCD<N>`. On 2000, XP and XP x64 it reaches
  this driver through the alias (`/c m` for `HCD<m>`, usually 9); on
  Vista and later it cannot take a snapshot from this driver.
- On Vista and later a tool that scans `\\.\HCD<n>` alone (an old
  USBView) no longer finds this driver's controllers. On 2000, XP and XP
  x64 it finds them through the alias.
- The fix takes effect from the first start of `3.3.0.0`. An update from
  `3.2.0.0` or earlier on a 2000 machine with two controllers on this
  driver, or with Windows' EHCI started after it, may still end at Code
  31 and ask for the restart, once: the old image was kept at that boot,
  before the update ran (`runs/run-44.md`, "44.81", boot a).
- Read on QEMU only. No real machine has run Windows 2000 with this
  package.

## 7. Evidence

- `runs/run-44.md`, "44.81": every boot, the dumps' readings, the legs.
- `E:\xhci98-vm\p44\p44-c31\` (held on the development host): `STATE.md`,
  `pids.txt`, `w2b\` and `w2i\` with each boot's serial log and command
  line, `shots\`, `tools\`.
- `E:\xhci98-scratch\p44-c31\`: the dumps `m3.elf`, `r2.elf` and `c3.elf`,
  the pool reader `an2\syms.py`, and the disassembly listings under `ui\`.
- The XP and 7 helper leg: `E:\xhci98-vm\p44\p44-c31x\`.

## Sources

- Windows 2000 SP4 and XP SP3 `usbui.dll`, 2000 SP4 `usbport.sys`: static
  readings, `legal-provenance.md` section 4, the 44.81 row.
- Design record 13 sections 8.1, 8.2 and 8.8.
