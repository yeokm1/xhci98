# Run 45 - Release `3.4.0.0`

Phase 45 of `roadmap-usb4.md`, for release `3.4.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 45.23 a UAS drive behind a VIA VL805 that fails writes after standby (2026-10-29)

**Verdict: the report's symptom was reproduced on q35 XP through the
driver's resume re-initialization, in the report's own words; the owner
decided the same day to keep devices across it, and that is built (design
record 20 sections 4.3.1 and 4.3.2). A UAS drive with a serial number on a
root port and idle at the sleep was restored in place and kept its open
file; one with URBs pending across the sleep is still dropped.** The
reporter's system, Windows version, xHCI98 version and log were not had,
and hibernation stands in for standby, so the reporter's cause is not
confirmed; the reading is the code's and the emulator's. The real VL805
is 45-E.12.

### The reading of the code

On a VIA controller `XhciResumeReinit` (design record 20 section 4.3)
skips the suspend's save, so every resume re-initializes the controller
(`XhciResumeController`), as it does on any controller without a usable
save, QEMU's included. The re-initialization raises
`XhciSlotInvalidateAll`; the controller thread's next pass ran
`hcdInvalidate`, whose `hcdDropAll` drained every device, reported each PDO
missing and freed each record, and the bus then enumerated every device
again as a new PDO. `xhciuas.sys` passes power IRPs down and keeps nothing
across them. So a storage stack saw a surprise removal at every such
resume: requests in flight failed, cached writes were lost, and a handle
opened before the sleep - a virtual machine's disk image - failed for good.

### The vehicle

Development host A, QEMU under TCG (the q35 bases' emulator), the q35 XP
base (`launch-q35.ps1 -Os xp`, 2 processors), a fresh overlay per run,
`-vga cirrus -global ICH9-LPC.disable_s3=0 -global
ICH9-LPC.disable_s4=0`, a `usb-uas` stick (`serial=W45UAS01`, one
`scsi-hd` LUN on an overlay of the FAT test disk) on `qemu-xhci` port 3,
`XhciResumeReinit` 2 in the machine-wide key. Standby cannot be made on XP
here: with the standard VGA XP offers no sleep state ("System does not
support hibernation", standby "Not Supported"), and with Cirrus it offers
standby but the display driver vetoes it ("System Standby Failed ... the
device driver for the 'Cirrus Logic 5446 Compatible Graphics Adapter'
device is preventing the machine from entering standby"), as `lessons.md`
records for 2000. Hibernation works with Cirrus, and its resume takes the
same re-initialization, so the legs hibernate (`powercfg /hibernate on`,
`rundll32 powrprof.dll,SetSuspendState 1,1,0`; the resume by starting the
overlay again). Load: `W.BAT D:` (a 16 MiB file copied, compared and
deleted in a loop, a COM1 line a pass) and a file held open across the
sleep and written every second (`cmd /k (for /l %i in (1,1,100000) do
@echo %i & ping -n 2 127.0.0.1 >nul) > D:\LONG.TXT`).

### Before the fix

| Build | Transport | Result |
|---|---|---|
| `3.3.0.0` release | UAS, copy loop | 8 copies `COPYFAIL` over 4 s after the resume, then `OK` on the drive enumerated again |
| this tree at `4fe3d9b1`, release (`xhci98.sys` `134c7f02...`) | UAS, copy loop and held file | 9 copies `COPYFAIL`, then `OK`; "Windows - Delayed Write Failed: Windows was unable to save all the data for the file D:\LONG.TXT. The data has been lost."; the held file: "The volume for a file has been externally altered so that the opened file is no longer valid" and "Windows - Wrong Volume ... insert volume ORICO"; XP's balloon "Delayed Write Failed" - the report's symptom |
| the same | Bulk-Only (`usb-storage`), copy loop and held file | 1 `CMPFAIL`, 22 `COPYFAIL`, then `OK`; "Delayed Write Failed ... D:\LONG2.TXT" |

QEMU's `usb-uas` offers UAS alone (one alternate setting), so
`XhciForceBulkOnly` cannot put it on Bulk-Only (re-plugged with the value
at 1 it bound "xHCI98 USB Attached SCSI Storage" again); the Bulk-Only
reading is QEMU's `usb-storage`. A harness note: a `usb-uas` hot-plugged
before its `scsi-hd` attaches at 12 Mbit/s and is never addressed (`info
usb`: "Device 0.0 ... 12 Mb/s"); plugged with `attached=false`, its LUN
added, then `qom-set attached=true`, it attaches at 5 Gbit/s and
enumerates. Evidence: `E:\xhci98-vm\p45\w45-legs\xp23\`
(`delayed-write-failed-uas.png`, `held-handle-invalid-uas.png`,
`balloon-delayed-write.png`, `delayed-write-failed-bot.png`) and
`t330xp\`.

### The fix and its legs

The owner chose option (a) of record 20 section 4.3.1 on 2026-10-29. Built
as section 4.3.2 says (`59ef97c9` to `87d3d489`, reviewed and revised): a
device on a root port that `XhciEnumResumeKeep` keeps is restored in place
on a new slot with its PDOs, record and pipe handles, the D0 waiting for
that before the function drivers above are powered; anything else is
dropped as before; `XhciResumeKeep` 0 turns it off. Host vectors:
`test_enum` (the decision), `test_membuf` (the pool's claim and rehome).

Read on fresh XP overlays (`xpk\`) with the `qemu` flavour of the tree
copied over the installed `xhci98.sys` for its port-0xE9 trace (the
package `xhci98-3.3.0.0.zip` of the gated x86 build of `06642a74`, release
`xhci98.sys` `d7f8ab2c...`, then the `qemu` builds named below):

| Driver | Load at the sleep | Result |
|---|---|---|
| `06642a74`, `qemu` (`8b294fec...`) | copy loop and held file | the tablet kept ("resume keep, restored, port/slot=00000601"); the UAS drive not kept, its note `resume.keep.port=0000030A` (busy: a URB outstanding on its PDO); dropped and found again, "Delayed Write Failed" as before |
| `7e1b1934` (the D0 waits for the keep), `qemu` (`8fc7337b...`) | held file alone | the UAS drive kept: "resume keep, port/flags/why=0300E500" (root, started, storage, UAS, serial; kept), "endpoints replayed, mask=000001A4" (its four bulk endpoints), "restored, port/slot=00000301"; no popup, the held window shows no error, and `D:\LONG4.TXT` grew from 858 to 990 bytes after the resume |
| the same | copy loop and held file | "a PDO's URBs pending=00000003" (three URBs pending on the UAS PDO when the pass looked, the count alone: which they were was not read), `why` 0x0A: dropped, "Delayed Write Failed ... D:\LONG4.TXT" |

So the fix keeps the files of a drive the decision keeps and the restore
succeeds on - here one idle when the machine slept; a drive with URBs
pending across the sleep is dropped, as record 20's K4 rules for a
storage interface busy at the reset. Whether
`xhciuas.sys` should wait out its commands before it passes a power-down
on, so that a busy drive is idle by the sleep, is left open (the record's
section 4.3.2). The wait was revised after these legs (`87d3d489` and
`0e9a6151`: armed only with the switch on, released only once the
invalidation is taken, on the direct-transition route too, and settled by
a stop or a fresh start); FINAL-LEG

## 45.27 the legs the fixes of 2026-10-29 owe, and what that reading left (2026-10-29)

**Verdict: the small items are done (`4fe3d9b1`; the developer paths
`4f9c775d` and `984fcc36`, owner 2026-10-29); the installer's DevicePath
limit read on 98 SE; the update over `3.3.0.0` read on 98 SE and ME, with
a first-boot hang on each in the first run that the repeats did not show;
98 SE's and ME's event-wait paths read under load and replug; the rest
owed, each named below.**

### The small items

- The temporary-file field of `xhciuas-amd64.inf` and `xhcicm-amd64.inf`
  dropped, as their x86 INFs and `xhcicmf-amd64.inf` have it; UAS-TMPNAME
  and CM-TMPNAME hold every INF of their gate, the amd64 UAS-MSFILE
  mutation's anchor follows and the new mutations are in the self-tests
  (51 and 144 mutations pass).
- `s98_wininit_cancel` reads WININIT.INI from the file (`vsd_entry_r`), as
  `wininit_pending` does; only this installer's line goes, read again
  until gone; STOR98.TMP is deleted; and when a queued update may still
  put `stor98.mpd` back (another program's line, an unread file, a .TMP
  left) the uninstall keeps the driver and Stor98's record for a later
  run.
- `hcdHubPowerWork`: one work item per hub is enough beside the
  controller's D0 pair, since a hub's D0 completes in its completion
  routine (`hcd.h`, a comment; no code change).
- Design record 21 section 8: `XhciCmMode` 2 named for an unlisted Intel id
  of a class other than `0x0C0340`, as `TbtHwMatch` orders it.
- The `ObfDereferenceObject` rationale: every target for `xhci98.sys`, with
  its call sites; NT only for `xhcicm.sys`, named.
- `tuning.md`: `StorInterrupt`'s reasons, "INTx would not enable" among
  them; Stor98's README: "forced by storpollmode", as the code writes it.
- The developer paths in tracked documents, scripts and Stor98's records
  neutralised (owner, 2026-10-29); `run-usb4-guest.ps1` takes `-EmuRoot`,
  then `USB4EMU_ROOT`, then the `usb4-emu` checkout beside the main one;
  the dated run sheets stay as written.

### The installer's DevicePath on 98 SE

Fresh q35 98 SE overlay (`w98dp\`), `DevicePath` set to 270 characters
(`C:\WINDOWS\INF` and eleven `C:\DRV\...` entries) by `REGEDIT /S`, the
test cut of `4fe3d9b1` installed with `XHCIINST /dir release-x86 /yes
/nopause`: `C:\XHCI98` added, 280 characters exactly, accepted (the limit
is inclusive). The same run again with `/force /to C:\XHCI98B`: "DevicePath
would be 281 characters with C:\XHCI98B on it, past the 280 with which
Windows 98 SE's hardware wizard finds a driver: C:\XHCI98B is left off it;
point the wizard at it instead.", exit 4. PASS.

### The update over `3.3.0.0` on 98 SE and ME, and its WININIT reads

q35 98 SE and ME overlays with the ASM4242 model and the ORICO enclosure
at power-on: `3.3.0.0` installed (`/thunderbolt /stor98`, exit 3), a cold
start, then the test cut installed over it (`/force /thunderbolt
/stor98`, exit 3), a shutdown and a cold start. WININIT.BAK after the
start held `[rename]
C:\WINDOWS\System32\Drivers\xhci98.sys=C:\WINDOWS\System32\Drivers\xhci98.tmp`
on both: the setup engine's replacement of the loaded `xhci98.sys`. Stor98
was not loaded in these guests (the installer's log: "0 connection
managers" answered, the NVMe drive "install (no driver)"), so the
`stor98.mpd` half of the leg was not reached; it stays owed.

| Run | 98 SE | ME |
|---|---|---|
| `w98up`, `meup`: `3.3.0.0`, then the test cut | first start after the update hung (black text screen, the processor halted in the VMM's idle, 30 min; `first-boot-hang.png`); the second cold start reached the desktop | the same |
| `w98ctl`, `mectl`: `3.3.0.0`, then `3.3.0.0` `/force` (control) | desktop | desktop |
| `w98up2`: the same as `w98up`, the `qemu` `xhci98.sys` put in place of the queued `xhci98.tmp` | desktop; the 0xE9 trace runs to the root hub's start | - |
| `w98up3`, `meup3`: the same as the first row, release | desktop | desktop |

The update leg itself ran three times on 98 SE (the first run, which
hung; `w98up3`, release; `w98up2`, the `qemu` driver in place of the
queued one) and twice on ME (the first run, which hung; `meup3`); only the
first run hung on each. The control installs `3.3.0.0` over itself and
says nothing of the test cut's update. The hang is not explained and
stays open (evidence `w98up\`, `meup\`).

### The event waits on 98 SE and ME

The waits that a Windows 98 unsatisfied `KeWaitForSingleObject` return no
longer ends (`HcdWaitEvent`, `UasWaitEvent`): the code cannot force the
0xFFFFFFFF return, so the leg runs the paths that wait - the remove
events, the forward-and-wait of PnP IRPs, the UAS driver's rundowns, the
door's gate - many times under load and reads that none of them hangs
and that no gate wait was abandoned. Fresh q35 98 SE and ME overlays
(`w98ev\`, `meev\`), the test cut of `06642a74` installed (98 SE with
`/storage`, NUSB 3.3's files), a cold start, a Bulk-Only stick
(`usb-storage`, port 1) and a UAS stick (`usb-uas`, port 2, plugged
detached and then attached) hot-plugged and installed, `W9.BAT E:` (an
8 MiB random file copied and compared in a loop); then four Bulk-Only
and two UAS unplug-and-replug cycles, 15 s out and 25 to 30 s back, during
the loop; then `XHCITR /dump`. On both: each removal during a write gave
9x's own "Disk Write Error ... Unable to write to disk in drive E:";
once dismissed, both sticks were addressed again (`info usb`), the loop ran
on to its end, the snapshot was written through the door, and the
driver's notes hold no `gate.power.missed` or `gate.door.missed` (the
waits' abandonment record). What this proves is the paths' completion
under load and replug, not the unsatisfied return's handling, which only
a machine that produces it can show. PASS.

### A D0 overlapping a system power IRP

XP: every hibernation resume above (seven) completed with the devices
back and no hang, but whether a D0 and the system IRP's completion were
ever deferred together - the case the fix is for - was not observed, so
the leg is owed on XP as on 2000 and 7.

### Owed

- The power gate's wait under a standby on 98 SE or ME (ME's standby
  works in QEMU); owed.
- A D0 overlapping a system power IRP on 2000, XP and 7; owed.
- A UAS STOP and START with a LUN gone, a short REPORT LUNS answer, and
  ATA pass-through and SMART from a tool: owed (no such tool on the
  guests).
- Stor98's INTx fallback and a BUSY that keeps its length: QEMU's NVMe
  takes the INTx enable, and no BUSY was provoked; owed to hardware.
- The connection manager's eject or PCIe loss with an unplug during it and
  a resume with a lane adapter gone (`usb4-emu`'s `x-cable` and
  `x-pcie-down` are in the bases' emulator), its no-IRQ clock and retry
  wait on 98 SE, a failed D0 during a re-enumeration on 98 SE: owed.
- The WININIT read of the update over a loaded Stor98 `.MPD` (above).
- The SuperSpeed hub's reset with a stale change bit, the ring's RX index
  and cancelled waits: hardware readings, owed to the bench.
