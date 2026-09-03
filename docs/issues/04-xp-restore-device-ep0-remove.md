# Issue 4 - A device Windows XP's hub re-creates mid-enumeration is failed by this driver: the REMOVE of the superseded EP0 handle unbinds the live one

Status: fix in the tree, the closing run owed. Observed once, on the
Windows XP guest, 2026-09-03 (roadmap task 19.3). The mechanism below is a
static reading of this driver's own code against the callback order the run
recorded, and the host vector of section 4 reproduces it from that order:
on the REMOVE path as it was, the vector failed exactly as the run did. The
owner's decision that afternoon was no driver code change in release
`1.0.0.2`; reversed the same evening, the fix in section 4 is roadmap task
19.7 and landed that night. This page moves to fixed when the XP reading
section 5 asks for is taken. Until then the workaround is to unplug the
device and plug it back in, which enumerated cleanly in the same run.

Targets affected: Windows XP, the best-effort secondary target. Not seen on
Windows 98 SE, Windows ME or Windows 2000 in any run of this project: the
hub sequence that triggers it, a port reset and a second device handle for
a device that is still enumerating, has not been observed from those hubs.
Whether their hubs ever run it is not known.

The short version: XP's hub reset the port of a freshly attached
mass-storage device after the device descriptor had been read, re-created
the device through a **new** usbport device handle (a new EP0 endpoint
extension, addressed as device 3 while the old handle, addressed as 2, was
still open), read the configuration descriptor through the new handle, and
only then removed the old handle's EP0. This driver's REMOVE path for the
default control pipe does not ask which extension is being removed: it
clears the record's "EP0 open" flag and its endpoint pointer whatever
extension arrived, so the removal of the superseded handle unbound the live
one. Every submit through the live handle was then refused for retry, the
progress detector failed the record about five seconds later, and XP gave
up on the device: a banged generic "USB Device" in Device Manager.

---

## 1. The symptom

Run `p194`, second boot, the XP guest on the `p3=0` launcher
(`build-and-test.md`, "Windows XP target VM"), the driver loaded, a
`usb-mouse` bound. `device_add usb-storage,id=s1,bus=xhci.0,drive=xpd,
removable=on` on the monitor. `info usb` listed the disk as device 2 on
port 2 at 480 Mb/s. Device Manager showed "USB Device" with a yellow bang
under Universal Serial Bus controllers, no "USB Mass Storage Device", no
disk. The debug console (runtime, this driver's own counters):

```
devices addressed=00000003          (the mouse, then the disk twice)
SET_ADDRESS interceptions=00000003
endpoints opened=00000001           (the mouse's interrupt pipe only)
slots reset to Default=00000001
devices reopened=00000003
descriptor configs committed=00000003
descriptor configs partial=00000004
transfers refused for retry=0000017F   (climbing; transfers submitted frozen at 0xEC)
slot: record refused without progressing, failing it, state << 8 | pending op=00000400
slot: device failed, reason << 8 | completion code=00000001
records failed - no progress=00000001
slot: port disowned a device, hub port << 8 | slot=00000202
```

`0x0400` is `XHCI_DEV_STATE_ADDRESSED` with no operation pending: the
record was healthy and idle, and it was being refused anyway. Every
refusal and failure counter other than these stayed at zero. The livelock
signature, refusals climbing against a frozen submit count, is the one
`lessons.md` ("Batch 6-V") and `failure-diagnosis.md` name.

Unplugged (`device_del s1`), fifteen seconds, plugged again with a fresh
backend: port 3, addressed as device 4, `endpoints opened` 2 and 3 (the
bulk pair), "USB Mass Storage Device" and a Storage volumes node, no reset
to Default, nothing refused; the owner formatted it as `F:`, wrote a text
file and read it back. The owner's reading, "you unplug and replug too
quickly", is the same mechanism seen from the other side: the failure needs
the hub to re-create a device it is still enumerating, and a replug after
a pause gives it no reason to.

**The same run, the same shape, a second device.** A `usb-audio` composite
(Full Speed, port 4) hot-plugged afterwards took the identical path: slot
3 enabled and addressed (device 5), the full configuration descriptor read
and committed (`descriptor configs committed` 6, one isochronous endpoint
declared), a port reset from XP, `usb_xhci_slot_reset slotid 3`, a second
address (device 6), a partial configuration read, one more
`CloseEndpoint`, then `records failed - no progress` 2 with the
`record refused` print already out of budget, and XP disabling the port
(`devices disowned by a port disable` 2). Unplugged, fifteen seconds,
plugged again: port 2, addressed as device 7, the isochronous endpoint
opened with its interval derived from the descriptor (`endpoints opened`
4), "USB Composite Device" and "USB Audio Device" clean, no reset, nothing
refused. Two devices, two classes, both failed on
the first attach and both bound on a replug; the HID mouse, hot-plugged
first on every boot, never saw a second reset.

## 2. What the run recorded

The QEMU xHCI trace (`usb_xhci_slot_*` and `usb_xhci_queue_event`) for
slot 2, the disk, in order:

```
usb_xhci_slot_enable slotid 2
usb_xhci_slot_address slotid 2, port 2        (BSR = 1, the driver's chain)
ER_TRANSFER CC_SHORT_PACKET ... c 0x02018001  (the device descriptor read)
ER_PORT_STATUS_CHANGE                         (XP resets the port)
usb_xhci_slot_address slotid 2, port 2        (BSR = 0: SET_ADDRESS 2 emulated)
ER_TRANSFER x8 on slot 2                      (device and configuration descriptors)
ER_PORT_STATUS_CHANGE                         (XP resets the port again)
usb_xhci_slot_reset slotid 2                  (this driver's Reset Device: "slots reset to Default")
ER_TRANSFER CC_SHORT_PACKET on slot 2         (the 8-byte descriptor read at address 0)
ER_PORT_STATUS_CHANGE
usb_xhci_slot_address slotid 2, port 2        (BSR = 0: SET_ADDRESS 3 emulated)
ER_TRANSFER x2 on slot 2                      (the configuration descriptor, partial)
                                              - no further slot-2 event, ever
```

The debug console's callback log in the same window, with the per-site
print budget already spent on the earlier opens (`log records suppressed`
0x3A), kept one line between the third Address Device and the failure:

```
cb CloseEndpoint irql=02 a=81F749DC b=8203BE28 c=00000000
```

`8203BE28` is the endpoint extension usbport opened for the disk's EP0 at
its **first** open (`cb OpenEndpoint ... c=8203BE28`, before the disk was
addressed as 2). It is not the extension the device was re-created
through after the second reset: that open carried
`probe.ep props+00: 00000003 ...` (`DeviceAddress` 3 in the properties
block) and was counted as a reopen against the same record. `CloseEndpoint`
is a stub in this driver (batch 6-0: nothing acts on it); the work is done
by the `SetEndpointState(REMOVE)` usbport sends first, whose print was
among the suppressed.

## 3. The mechanism, read statically

usbport, XP lineage, as ReactOS documents the interface
(`external/reactos/usbport/device.c`, `USBPORT_RestoreDevice`, reached from
the hub through `USBHI_RestoreUsbDevice` in `iface.c`): when the hub
re-creates a device it takes an **old** and a **new** device handle. The
new handle already exists, with its own EP0 pipe opened through
`MiniportOpenEndpoint` at the port reset and addressed since. The restore
moves every pipe *but* EP0 from the old handle to the new one
(`ReopenEndpoint` with the new address), and the old handle's own EP0 pipe
is closed afterwards, which is the late `SetEndpointState(REMOVE)` plus
`CloseEndpoint` on the first extension above. So for one physical device
two EP0 extensions are live at once, both pointing at the same record of
this driver, and the older one is removed last.

This driver's side (`src/xhci_slot.c`):

- `xhciSlotOpenControl`: an open at `DeviceAddress` 0 on a port that has
  just reset resolves through `xhciDevOpenOnRootPort` to the record already
  bound to that port (batch 6-B's "re-enumeration without a disconnect"),
  re-enters it at Default (`xhciDevReenterAtDefault`, the Reset Device
  command), then sets `dev->EndpointExtension = endpoint` and
  `XHCI_DEV_FLAG_EP0_OPEN`. The extension that used to be there is neither
  remembered nor marked.
- `XhciSlotSetEndpointState`, the REMOVE branch, `if (endpoint->Dci <= 1)`:
  clears `XHCI_DEV_FLAG_EP0_OPEN`, sets `dev->EndpointExtension = NULL`,
  drops the owed EP0 invalidate, arms a drain of the EP0 queue and cancels
  any pending SET_ADDRESS. It does not compare `endpoint` with
  `dev->EndpointExtension`, so the REMOVE of the superseded extension
  unbinds the live one.
- `XhciSlotSubmitTransfer`, the default-pipe path: `if ((dev->Flags &
  XHCI_DEV_FLAG_EP0_OPEN) == 0)` refuses for retry. Every later submit
  through the live extension takes this exit. usbport re-offers it on each
  poll, which is the climbing counter.
- The progress detector (`xhciDevPollProgress`, task 7b-A.0): a record refused
  for `XHCI_DEV_STALL_MS` without placing anything is failed
  (`records failed - no progress`), which is the honest answer to a
  livelock and the wrong answer here.

On the 9x targets and Windows 2000 the reopen after SET_ADDRESS arrives as
REMOVE, Close, Open **on the same extension** (the mouse in this very run:
`81FEB3C8` removed and reopened), which the identity-free REMOVE handles by
construction. Nothing in the record says whether their hubs ever restore a
device through a second handle; the Windows 2000 SP4 image never showed
it, and the batch 6-B vector that models re-enumeration opens the second
time through the same static extension.

Why XP reset the disk and the audio device a second time is not
established. The mouse on the same boot got no second reset; each of the
other two got one after its configuration descriptor had been read and
committed, on its first attach only, and neither got one on the replug.
That pattern fits a reset asked for during the first driver installation
of the device instance (a class driver's start path requesting a port
reset through the hub, which is what the two-handle restore serves) better
than a hub distrusting the enumeration, but nothing in this run's evidence
decides it.

## 4. The fix, roadmap task 19.7

One identity check in the REMOVE branch: when `endpoint->Dci <= 1` and
`dev->EndpointExtension` is neither NULL nor `endpoint`, the REMOVE names a
superseded extension. Clear that extension's own `XHCI_ENDPOINT_FLAG_OPEN`,
count it, and leave the record's binding, its owed invalidate, its queue
and its pending SET_ADDRESS alone, since they belong to the live handle.
The same-extension reopen that both primary targets perform is unchanged
by it, and so is a REMOVE arriving with the binding already gone.

Landed 2026-09-03 evening, in this order. The host vector first
(`test_slot_ep0_remove_superseded_handle`, `test\test_init.c`): a device
addressed at 2 through one extension, its port reset again, EP0 opened at
address 0 through a second static extension (the existing
`xhciDevOpenOnRootPort` path re-entering the record), SET_ADDRESS 3
submitted through the second and still in flight, then the REMOVE of the
first. Against the REMOVE path as it was, the vector failed at every one of
the properties it asserts: the record's open flag and pointer gone, the
pending SET_ADDRESS completed as cancelled (`0x10000`), the descriptor read
through the live handle refused for retry, which is the run's livelock in
miniature. Then the change in `src\xhci_slot.c`, `XhciSlotSetEndpointState`,
with the counter `Ep0RemovesSuperseded` ("EP0 removes on a superseded
handle" in the counter dump; expected zero on Windows 98 and Windows 2000,
where a nonzero reading would say the two-handle restore is not XP's
alone). A second vector removes the old handle late, in the `p194` order,
with the new handle addressed and a descriptor read queued on it. The suite
passes (12,209 checks), `build-driver.cmd all` and every gate are green, and
the first vector's final REMOVE re-checks that the same-extension path is
what it was.

It was not taken on the afternoon of 2026-09-03 because Phase 19 was an
INF-and-documents release with the 9x targets' install routes still to
re-read from the asset (roadmap tasks 19.8 and 19.9), and a change to the
slot code is not free on either primary target; the owner's instruction
then was "let's not change the driver code, but write this up as a
potential issue." That evening the owner scheduled it as task 19.7, ahead
of those two, in the order the task states: the confirming run section 5
asks for first, the host vector, the change, the XP reading, then both
primary targets on the same binary with the reading that nothing changed.

## 5. What is still open

- A run that confirms the mechanism, now with the fix in place: the
  counter `Ep0RemovesSuperseded` moving while a mass-storage device and a
  composite bind on their first attach. The confirming run the task asked
  for first was taken 2026-09-03 evening (tag `i4`: a cold boot of the
  `p194` install, `usb-storage` the first device on port 1 with the
  `SetEndpointState` print budget intact, `usb-audio` second on port 2) and
  did not reproduce it: both devices bound on their first attach through
  one same-extension EP0 reopen each, no second port reset, `slots reset to
  Default` 0, `records failed - no progress` 0. What the three `p194` first
  attaches had that these two lacked is that each was also the first-ever
  installation of its class driver on that XP install (`usbstor.sys`,
  `usbaudio.sys` and their companions placed from the SP3 cab during the
  attach); every attach with the class driver already present, the `p194`
  replugs included, has enumerated cleanly. A correlation on five attaches,
  not a mechanism. The reading that settles both this and the fix is the
  clean snapshot (`winxp-clean-install`), the 1.0.0.2 package reinstalled
  from `vm\xferxp` (the owner at the Have Disk wizard), then `usb-storage`
  and `usb-audio` each attached once: the counter at 1 and 2, the devices
  bound, and the failure counters at zero.
- Whether the Windows 2000 hub ever takes the restore path against this
  driver. If it does, this is not an XP-only issue.
- Why XP's hub re-created the disk and the audio device. A second attach
  of each on the same boot did not repeat it, and neither did a first
  attach on a later boot with the class drivers already installed (`i4`);
  the first-ever installation of the class driver, during the attach, is
  the candidate condition.

## Sources

- `docs/contributing/build-and-test.md`, "Windows XP target VM": the run
  and the recipe.
- `docs/contributing/roadmap.md`, Phase 19, task 19.3.
- `src/xhci_slot.c`: `xhciSlotOpenControl`, `xhciDevOpenOnRootPort`,
  `XhciSlotSetEndpointState` (the REMOVE branch), `XhciSlotSubmitTransfer`
  (the EP0 gate), the progress detector.
- `external/reactos/usbport/device.c` (`USBPORT_RestoreDevice`),
  `external/reactos/usbport/iface.c` (`USBHI_RestoreUsbDevice`): interface
  documentation for the two-handle restore, read statically; nothing was
  taken from them but the shape.
- `docs/contributing/lessons.md`, "Batch 6-V": the livelock signature.
