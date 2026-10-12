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

## 37.7 - An event ring check after the re-arm

**Verdict: PASS** (2026-10-12). Under QEMU's MSI-X the backstop no longer
serves the events the interrupter's IE toggle strands; under MSI and the line
the check queues nothing.

**The change.** `f808e22`: `hcdRearmPeek` (`src/hcd_ctl.c`) after an
interrupt-delivered drain, the `HCD_DRAIN_FROM_PEEK` marker so the drain it
queues does not peek again, `RearmPeekPending` and `RearmPeekDrains` before
`TrailingPad`; design record 17 section 4.1, "Amended for 37.7". Codex,
three rounds: round 1 found the first draft's re-peeking drain could chain
DPCs at DISPATCH_LEVEL under sustained completions (fixed by the marker),
round 2 a reversed race in the design text, round 3 no defects. Host tests
2027 checks, 0 failures; the qemu flavour built and gated, x86 and amd64.

**The vehicle.** A development host, QEMU 11.1.0 (installed that day from
the stable `qemu-w64-setup-20260811.exe`, SHA-512 checked; the rc2 build of
37.1 was replaced), TCG, `qemu64`, two processors, 2048 MB,
`qemu-xhci,p2=4,p3=4` with `msi`/`msix` set per leg. Bases
`vm\t28pre\win7-x64-base.qcow2` and `vista-x64-base.qcow2`, each leg on its
own overlay. Kit `out\p37-rrc\kit` (the Phase 36 matrix kit, retargeted);
readings in `out\p37-rrc\legs\<guest>\notes.txt`. Devices: the Bulk-Only
stick on port 1 at boot, a raw UAS disk on port 3 and `usb-audio` on port 4
hot-added. The new build is the qemu flavour of `f808e22`, unsigned, so each
boot took F8's "Disable Driver Signature Enforcement"; the control is the
`2.3.0.0` `release-x64`.

**How the counters were read.** `XHCISNAP` does not decode the two new
words, so they were read from its `.BIN`, the raw extension image: the two
words before `TrailingPad` and the trailing signature. A start zeroes them,
so a snapshot after a controller restart covers only that start.

**What was read** (load: TONE.WAV three times, the stick copied and compared,
the UAS disk written 64 MiB in 1 MiB blocks and verified four times):

| Guest | Build | Delivery | BackstopDrains | Peek pending / drains | TONE | UAS VERIFY | Result |
|---|---|---|---|---|---|---|---|
| 7 x64 | 37.7 | MSI-X | 0 | 487 / 226 | 20.26, 20.31 s (first play 59.5 s, cold); Ring Underrun 1-3 | 97.3-101.1 MB/s | PASS |
| 7 x64 | 2.3.0.0 | MSI-X | 172 | - | 24.87, 27.47, 26.51 s; Ring Underrun 9-42 | 3.6-4.1 MB/s | control |
| 7 x64 | 37.7 | MSI | 0 | 869 / 0 | 21.34, 20.27, 20.36 s | 109.0-115.3 MB/s | PASS |
| 7 x64 | 37.7 | line | 0 | 815 / 0 | 21.09, 20.26, 20.32 s | 109.7-115.8 MB/s | PASS |
| Vista x64 | 37.7 | MSI-X | 0 | 1681 / 204 | capture: 3 plays in 62.76 s, 12 gaps of 20.7 ms | 94.4-103.6 MB/s | PASS |
| Vista x64 | 2.3.0.0 | MSI-X | 263 | - | capture: 3 plays in 63.14 s, 70 gaps (one 990.7 ms) | 4.2-5.3 MB/s | control |
| Vista x64 | 37.7 | MSI | 0 | 1811 / 0 | capture: 7 gaps, two of them the pauses between plays | 110.3-113.5 MB/s | PASS |
| Vista x64 | 37.7 | line | 0 | 1562 / 0 | capture: 9 gaps, two of them the pauses between plays | 89.7-102.5 MB/s | PASS |

Every stick compare was clean and every VERIFY read 0 sectors bad.

**What it says.**

- QEMU 11.1.0 still drops the MSI-X message raised across the IE toggle:
  the `2.3.0.0` control counts 172 and 263 backstop drains, as Phase 36 read
  on 11.1.0-rc2 (`runs/run-36.md`, "MSI-X BackstopDrains (NT6)"), and with
  them its UAS reads run at about 4 MB/s and its audio gaps.
- With 37.7 the same load counts no backstop drain; the peek queued 226 and
  204 drains in their place, and the UAS reads and the tone match MSI's.
- Under MSI and the line the peek saw events at the dequeue but queued
  nothing: the interrupt had already queued the drain each time, which is
  the cost record 17 section 4.1 accepts for a delivering interrupter.

**Harness notes, not the driver.** Vista has no PowerShell, so its tones
were played by a WMPlayer.OCX script and read from QEMU's WAV capture (the
guest's clock runs slow under TCG). Disk numbers move between boots; one
XFERTEST aimed at the stick was refused (error 5) and was rerun on the UAS
disk. Vista's autoplay window on the stick vetoed one controller disable.
The repository's untracked `xhcisnap\XHCISNAP.EXE` was a stale `2.2.0.0`
build and failed the build's self-test gate until rebuilt.

**Not read:** a real MSI-X controller (37.14 keeps that bench item), and
Windows 7 and Vista x86, whose MSI-X goes through `xhcimsi.sys`.
