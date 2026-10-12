# Phase 37 Record

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 37". The
roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 37.1 - Windows ME on its stock USB stack, no SweetLow

**Verdict: PASS** (2026-10-12). Windows ME needs no USB 2.0 stack under the
host controller driver.

**The vehicle.** A development host with QEMU 11.1.0-rc2, TCG, `pc`,
`pentium3`, one processor, 256 MB, `qemu-xhci,p2=4,p3=4`. The base is the
`winme-clean-install` snapshot of `vm\winme.img` (the stock ME install of
2026-09-02, from before SweetLow's stack was added), copied out read-only to
`vm\t37me\winme-stock-base.qcow2`; the leg ran on its own overlay. Kit
`out\p37-me\kit` (the Phase 36 matrix kit, retargeted); readings, shots and
captures in `out\p37-me\legs\me\` (`notes.txt` is the full list).

**The package** is the `2.3.0.0` asset, `xhci98-2.3.0.0.zip`, SHA-256
`025EA20C95C6D90097720C45B8D266C9B4019B454F422C0724E5BBDF1A1A1EE3` (the hash
recorded at the cut), `release-x86`.

**What was read:**

| Step | Result |
| --- | --- |
| Install | Found New Hardware "PCI Universal Serial Bus", Specify a location `D:\`, "xHCI98 USB 3.x eXtensible Host Controller"; no file asked for; restart requested and taken. The root hub installed silently |
| Controller | `XHCISNAP` (empty bus): driver `2.3.0.0` running, Windows ME 4.90.3000, 8 ports |
| HID mouse | `usb-mouse`, root port, High Speed: installed silently, the pointer moves through it |
| Bulk-Only stick | `usb-storage`, root port, SuperSpeed: ME's own "USB Mass Storage Device", a drive letter, `fc /b` of a 4 MB file clean |
| UAS drive | `usb-uas`, root port, SuperSpeed: the wizard once, `XHCIUAS.INF` from the package, no restart; a drive letter, `fc /b` clean |
| Hub | `usb-hub`: installed silently |
| Composite audio | `usb-audio` behind the hub: the wizard once, "USB Audio Device" with files from ME's own CABs. The sound device appears about a minute after Finish. `TONE.WAV` (20 s, 1 kHz) played whole twice, captured by QEMU's WAV backend: the tone is one unbroken 20.01 s block in both captures (the silent runs `audchk.ps1` lists fall between ME's own system sounds, not inside the tone) |
| Slots | five, all configured (`XHCISNAP`); four `xfer.error` notes, slot 5 DCI 2 completion code 14 (Ring Underrun on the audio's isochronous OUT at stream start and stop), as on the Phase 36 Vista, 7 and XP audio legs |
| Disable, enable | Device Manager, live: Code 22 and every child disabled, no restart asked; enabled, everything back |
| Remove, rescan | Remove: every USB, HID, sound and storage entry gone, no hang. Refresh: everything reinstalled with no question |
| After the sequence | both disks `fc /b` clean; five slots configured, containment none |
| Shut Down | with every device attached: off within 60 s |

**Not read:** an update from an earlier xhci98 on a stock ME, and a UAS
drive on a machine where no Bulk-Only stick was ever plugged (the README's
advice to plug an ordinary stick first stands unread either way).

**A tool note.** The kit's `PNPCTL.EXE` is an NT build (subsystem version
5.1), which ME refuses to start; Device Manager carried the lifecycle steps.
