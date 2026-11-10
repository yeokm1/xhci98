# Issue 30 - Windows 2000's USB audio played nothing on `3.4.2.0`: its streaming SELECT_INTERFACE was refused for a URB header length usbport never reads

Status: **fixed for `3.4.3.0`** (`72937554`, merged at `b76b4adb`; the
select-path follow-up merged at `89715976`; the rule made log-only for
every function merged at `358db5b7`, section 4), reviewed and built;
the fix read in the emulator, **not yet on the reporter's machine**, whose
counters show the same refusal (section 2). `3.4.1.0` and earlier are not
affected.

**Scope.** Windows 2000 SP4 with Windows' own USB audio driver
(`usbaudio.sys` 5.00.2150.1) on any controller `xhci98.sys` serves, under
`3.4.2.0` only. Reported on GitHub (issue 32, 2026-11-09) on a fresh
Windows 2000 SP4 under QEMU (i440fx and q35) with a USB Audio Class 1.0
device behind a Renesas uPD720201.
Only Windows 2000's select was seen refused; the reporter's 98 and XP
played on the same setup.

## 1. What was seen

The controller, the composite parent and the USB Audio Device all read
"This device is working properly", and Windows 2000 installed its WDM
audio drivers, but nothing played. DxDiag reported "Problem getting extra
sound info, result code = 0x88780032", and its DirectSound test failed
with the same HRESULT (`DSERR_INVALIDCALL`), at step 3
(`DirectSoundCreate`) on the reporter's DirectX and at step 10 (secondary
buffer) on Windows 2000's own DirectX 7.0. Sound Recorder refused every
file: "Your audio hardware cannot play files like the current file."

The project's own legs had seen the same refusal on 2000 in Phase 47 and
Phase 48 (`run-47.md` section 9, `run-48.md`'s 48.33 2000 leg) and put it
down to QEMU's emulated `usb-audio`.

## 2. Reproduced, and bracketed

On the q35 Windows 2000 base (one processor, QEMU's `qemu-xhci` and
`usb-audio`, `-audiodev none`), 2026-11-10:

| Stack | Sound Recorder, `tada.wav` | DxDiag's DirectSound test |
|---|---|---|
| Windows' own UHCI driver (`ich9-usb-uhci1`), no `xhci98.sys` | played to the end | played its first format ("Did you hear the sound play?") |
| `3.4.2.0` installed by `INSTALL.BAT`, before and after a restart | "cannot play files like the current file" | step 10, `0x88780032` |
| `3.4.1.0`'s `xhci98.sys` over that install | played to the end | - |
| `3.3.0.0`'s debug `xhci98.sys` over that install | played to the end | - |
| `3.4.3.0` at `c4c26525`, the `qemu` flavour | refused, as `3.4.2.0` | - |
| `3.4.3.0` with the fix, the `qemu` flavour | played to the end | "All tests were successful." |

So the device, the emulator and Windows' driver were not the cause: only
`xhci98.sys` from `3.4.2.0` on refused.

**Read on the reporter's hardware.** A real Renesas uPD720201
(`1912:0014`) PCIe card passed through to a Windows 2000 guest on Proxmox
9.0.3, a C-Media "Generic USB Audio Device" (`0D8C:000E`, composite, two
isochronous endpoints), `3.4.2.0` with its default settings: the XHCITR
report shows `UrbsMalformed` 62 beside `SelectEndpointsRequested` 3 and no
problem code - the refusal counted here, on metal under a hypervisor.

## 3. Why

The `qemu` flavour's trace showed one refusal, at every play:

```
xhci98: hcd: URB refused, function/length=00010000
xhci98: URBs refused - malformed=00000009
```

Function 1 is `URB_FUNCTION_SELECT_INTERFACE`, and its `UrbHeader.Length`
was 0. `3.4.2.0` added a rule (an audit fix, `hcdUrbMinLength` in
`src/hcd_urb.c`): a URB whose header length stops short of the fields the
bus reads for its function is refused before any of them is read. Every
`usbd.sys` builder and every DDK macro states the structure's size, so
the rule was taken to cost nothing. Windows 2000's `usbaudio.sys` builds
its streaming select itself and leaves the header length at 0. usbport
does not read that field for a select; it rewrites it, and bounds the
request by the interface information's own `Length`
(`external/reactos/usbport/device.c`, `USBPORT_HandleSelectInterface`).
The select failed with `USBD_STATUS_INVALID_PARAMETER`, `usbaudio.sys`
failed the pin's open, and every client above it - WinMM and DirectSound
alike - failed in turn.

The same field had cost the same symptom once before: in October the
first HCD read `UrbHeader.Length` in its select and every 2000 alternate-1
select was refused (`runs/run-26.md`, c15); `hcdCfgSelectInterface`'s
comment in `src/hcd_cfg.c` records it. The `3.4.2.0` rule sits ahead of
that function and brought the refusal back.

## 4. The fix

`SELECT_INTERFACE` is no longer held to its header length.
`hcdCfgSelectInterface` already bounds a select by the
interface information's `Length` (refused when shorter than the fixed
fields, `USBD_STATUS_BUFFER_TOO_SMALL` when shorter than the alternate's
pipes) and never takes an end from the header. Host vectors in
`test/test_pipe.c` (`test_urb_length`) hold the select row as the guest
sent it. With the fix the trace shows each select served with its header
at 0 - `select interface, URB header short, header/length=00000010` for
alternate 0 and `=00000024` for alternate 1 - the streaming endpoint
configured and isochronous URBs sent, and `URBs refused - malformed`
stays 0.

**The select path's own reads.** With the header no bound, a select's
interface information may end anywhere its own `Length` says, as early as
that two-byte field. `hcdCfgSelectInterface` reads the `Length` alone
first (`XhciPipeSelectIfaceName`, `src/xhci_pipe.c`) and refuses one
shorter than the fixed fields with a trace of the `Length` only; the
interface number and alternate are read, and traced, only past that test,
and the controller thread reads the number after the call only when the
select succeeded (it read it on every answer before). The
`SELECT_CONFIGURATION` trace of the first entry is written only for a
configuration, since an unconfigure's URB need not reach that entry.
`test_select_iface_name` in `test/test_pipe.c` places a two-byte
interface information at the end of a committed page whose next page is
never committed and holds the refusal there. Windows 2000's selects, with
a header of 0 and an interface `Length` of `0x10` or `0x24`, are served
as before.

**The rule made log-only for every function (owner, 2026-11-10).**
For `3.4.3.0` no URB is refused for its header length: a header short of
the fields read for its function is counted (`UrbsHeaderShort`, the log
note `urb.header.short` at each function's first, the `qemu` flavour's
"URBs served - short header") and served, as usbport and `3.4.1.0`
serve it (`XhciPipeUrbHeaderCheck`, `src/xhci_pipe.c`); `UrbsMalformed`
counts refusals only. Every walk the header bounded keeps a bound of its
own: a select's interface information by its own `Length`, and a
`SELECT_CONFIGURATION`'s list - at the device, a composite function, the
composite parent and a departed device's ghost - with its first entry
bounded by that entry's `Length`, as usbport reads it, and later entries
by the header (`XhciPipeSelectListBytes`), an entry shorter than its
fixed part refused. The isochronous URB's packet table,
`GET_CURRENT_FRAME_NUMBER`, the parent's descriptor request and the
static-streams URBs keep their own length checks, as in `3.4.1.0`.

## 5. Open

- **The reporter's machine.** The fix was read in the emulator only;
  the reporter's controller is a passed-through Renesas uPD720201 and the
  device a real UAC 1.0 one, whose `3.4.2.0` counters show this refusal
  (section 2).
- **Other functions.** Only the select was refused in the traces read
  (98 SE, 2000 and XP class drivers state their other URBs' sizes). Since
  the rule became log-only (section 4), a driver of a device's maker that
  leaves another URB's header at 0 is served and counted in
  `UrbsHeaderShort`.

## 6. Rules kept

- **A field usbport does not read is not a bound.** A check on a URB is
  measured against what Windows' own stack checks, not against what its
  builders write.
- **A refusal the matrix puts down to the emulator is checked against
  Windows' own stack on the same emulator.** Two phases' legs blamed
  QEMU's audio device; a UHCI control run would have shown otherwise.

## 7. How it reached a release, and what changed

Four things lined up (owner, 2026-11-10):

1. **A hardening rule was taken from the builders, not from Windows.**
   The rule of `3.4.2.0` (an audit fix of 2026-11-06) refused a URB whose
   header length stops short of the fields read for its function, on the
   reasoning that every `usbd.sys` builder and DDK macro states the
   structure's size. Whether Windows' own stack refuses such a URB was
   not checked; it does not, for most functions (section 3). The same
   pattern is in `lessons.md`, "A contract derived from two usbport
   builds was enforced against a third".
2. **The field had broken the same driver before.** In October the first
   HCD read `UrbHeader.Length` in its select and every 2000 alternate-1
   select was refused (`runs/run-26.md`, c15); the comment saying so sat
   in `hcdCfgSelectInterface`, one function after the new rule. The
   audit's review read the rule for safety (no read past a buffer), not
   for the callers it would refuse, and did not search the run records
   for the field.
3. **The legs saw it and put it down to the emulator.** Phase 47's and
   Phase 48's 2000 legs met the same refusal (`runs/run-47.md` section 9;
   `runs/run-48.md`, the 48.33 2000 leg) and blamed QEMU's `usb-audio`;
   no leg played the file through Windows' own UHCI driver, the
   comparison that settled it (section 2), and none failed on a non-zero
   `UrbsMalformed`.
4. **No release leg played audio on 2000.** The release matrix installed,
   checked the driver and copied to a stick; `3.4.2.0` was cut two days
   after the audit.

What changed (all 2026-11-10):

- **The rule is log-only** for every function (section 4), as
  `3.4.1.0` and usbport serve such a URB; the walks it guarded are
  bounded by their own lengths.
- **`AGENTS.md`, "What NOT to Do"**: no new refusal of what a client
  sends without evidence that Windows' own USB stack refuses the same
  input (count-and-log first); a hardening change is reviewed for the
  callers it would refuse and against earlier incidents on the same
  field.
- **`lessons.md`**: a failure on one OS with an emulated device is
  checked against Windows' own driver for another controller before the
  emulator is blamed.
- **The release legs** play a WAV on 2000 through QEMU's `usb-audio`
  (`WAVCHK`) and fail on any refused malformed URB; the check fails on
  `3.4.2.0` and passes on `3.4.3.0` (`build-and-test.md`, "The playback
  check (2000; GitHub issue 32)").
- **The acceptance test** gains clause 7.22: on real hardware, Sound
  Recorder and DxDiag's DirectSound test on 2000 with a USB Audio Class
  1 device.

## Sources

- GitHub issue 32 (sngy844, 2026-11-09).
- The legs: `runs/run-48.md`, "Issue 32: Windows 2000 USB audio on
  `3.4.2.0`" (screenshots and the `qemu` traces off-tree).
- `src/hcd_urb.c` (`hcdSubmitUrb`, `hcdUrbMinLength`), `src/hcd_cfg.c`
  (`hcdCfgSelectInterface`), `src/xhci_pipe.c`
  (`XhciPipeUrbHeaderCheck`, `XhciPipeSelectListBytes`,
  `XhciPipeSelectIfaceName`); `runs/run-26.md` c15;
  `external/reactos/usbport/device.c`, recorded in `external/README.md`.
