# Phase 24 Record - GitHub issue 4's remaining requests: polling rates and true speeds

The detail behind `docs/contributing/roadmap-phases-17-on.md`, "Phase 24 -
GitHub Issue 4's Remaining Requests: Polling Rates and True Speeds". The
roadmap entry carries the goal, the status, the task list and the checkpoint;
this file carries what each task did and what each reading said. Where the two
disagree about a clause, the roadmap wins.

**Written while the phase is open**, and kept as written rather than rewritten
in the past tense. A sentence saying something "is owed" describes the day it
was written; the roadmap's task list says how each task closed.

**On `out\...`, `vm\...` and `tools\...` paths in this file.** They say where
a reading was taken and what the file was called, on the host that ran it;
they are not files a clone has.

Opened 2026-09-24 by task 24.1, the first task of this phase to take a
reading.

---

## 24.1 - Low-Speed polling rates behind a hub

Started 2026-09-24, on branch `issue4`.

### The cause, read before any guest booted

The roadmap's candidate was `XhciIntervalFromPeriod` refusing a Low-Speed
`Period` under 8 "on the strength of the SP4 and NUSB floors alone". The
first thing read was whether the reporter's stack has that floor, since the
reporter runs SweetLow's Windows 9x rebuild of XP SP2's usbport and never
NUSB's. It does not, and that is the whole cause:

- **SweetLow's `USBPORT.SYS` 5.1.2600.2180 has no Low-Speed floor.** Its
  `USBPORT_OpenPipe` producer (`0x24A31`-`0x24A8F`, image base `0x10000`)
  tests the device speed against 2 for High Speed, computes
  `1 << min(bInterval - 1, 5)` on that arm, and on every other arm takes
  `mov al,byte ptr [edx+0Ah]` at `0x24A5B` - the descriptor's own `bInterval` -
  straight into the preload-32-and-shift-right rounding. No compare against 8
  sits anywhere between that load and the store at `0x24A83`. Static,
  `dumpbin /disasm` from `tools/MSVC600`, the listing already in
  `tools/sweetlow-extracted/usbport-disasm.txt`.
- **Windows XP SP3's 5.1.2600.5512 has the floor SP4 and NUSB have**, at
  `0x25A69`-`0x25A78` (`test edx,edx` for Low Speed, `cmp al,8`, `jae`,
  `mov byte ptr [esi+10Eh],8`). Static, a fresh `dumpbin /disasm` over
  `tools/winxpsp3-extracted/usbport.sys` into the session scratchpad. So the
  three Microsoft builds this project holds for NT 5.x floor, and the one
  rebuild does not; whether SweetLow removed it or XP SP2 lacked it is not
  read and does not matter to the driver.

The chain from there to the reporter's screen needs no guess. hidusbf's 9x
filter rewrites the interrupt endpoint's `bInterval` in the
`SELECT_CONFIGURATION` descriptor (Phase 20's static read of the filter,
`legal-provenance.md` section 4). At 250, 500 and 1000 Hz that is 4, 2 and 1.
Behind a hub the device is at its true speed, so SweetLow's usbport buckets it
as a Low-Speed `Period` of 4, 2 or 1. `XhciBuildEndpointParams` then returned
`XHCI_CTX_BAD_PARAM` from the Low-Speed refusal, `OpenEndpoint` released the
pool ring, counted `EndpointRefusalsParams` and answered
`MP_STATUS_NO_RESOURCES`, usbport failed the pipe open and with it the
configuration, and the HID driver could not start: **Code 10**. At 125 Hz
(`bInterval` 8) the same path delivers `Period` 8 and works, which is where
the reporter's "250 Hz and above" boundary comes from. Under NUSB the same
tool never crosses 8 and the mouse never fails - and never speeds up either.

Both readings are provenance rows (section 4) and the ABI document's
"Periodic scheduling" section carries them as a table under the SP4
derivation.

### What changed

- `src/xhci_ctx.c`, `XhciIntervalFromPeriod`: the Low-Speed arm no longer
  refuses `Period` 1, 2 or 4. It translates them as Full Speed's are
  translated (frames, shift 3), giving Interval 3, 4 and 5 - 1, 2 and 4 ms -
  all inside Table 6-12's Low-Speed range of 3 to 10. The power-of-two and
  1..32 contract is unchanged, so a `Period` no shipping usbport sends stays
  refused. `src/xhci.h`'s contract comment says the reachable Low-Speed range
  is now 3-8, and from which builds each end comes.
- `test/test_ctx.c`: the three sub-8 Low-Speed buckets are pinned as accepted
  values with their Intervals (they were pinned as refusals), `Period` 3 at Low
  Speed is pinned as still refused, and the endpoint-builder vector that
  carried "LS period 4 refused through the builder too" now carries the
  endpoint it becomes (Interval 5, no burst). The suite passed:
  `test_ctx: 330 checks, 0 failures`, every other suite unchanged and green.
- `src/xhci_slot.c`, `OpenEndpoint`: a third tier-2 ring record,
  `ep.open.ival`, beside `ep.open` and `ep.open.rate`:
  `speed << 16 | floored << 8 | Interval`, where `speed` is the class usbport
  bucketed with. Phase 20's reading had to fetch the programmed Interval back
  through the DCBAA over QEMU's monitor; on a bare-metal machine, or a guest
  read only through `XHCISNAP`, this record is the only way to see it.
- `docs/contributing/implementation-invariants.md`, "Root Hub Reporting": the
  sentence that said Low Speed is floored at 8 now says by which builds.

### What is owed, and what cannot be taken in QEMU

- **The reproduction and the Low-Speed reading need a Low-Speed device, and
  QEMU has none.** Every emulated HID device in this QEMU build enumerates at
  12 Mb/s, `usb_version=0` is refused, and the one in-QEMU route to
  `USB_SPEED_LOW` is `usb-host` passthrough of a real mouse rebound off the
  host's HID driver (Phase 5's measurement, `lessons.md`). So the roadmap's
  "QEMU `usb-hub` plus `usb-mouse` on the SweetLow guest" cannot exercise the
  arm this task changed: a `usb-mouse` behind a `usb-hub` is Full Speed there,
  and Full-Speed `Period` 1, 2 and 4 were accepted before this task (Phase 20's
  behind-hub rows). The checkpoint's Low-Speed clause - Interval read from the
  snapshot, no Code 10, at 250, 500 and 1000 Hz on Windows 98 SE under
  SweetLow's stack - is therefore a **bare-metal reading**: the E460, a
  Low-Speed mouse behind a USB 2.0 hub, SweetLow's stack installed on its
  Windows 98 SE, hidusbf's 9x filter, `XHCISNAP` for `ep.open.ival`. Or
  `usb-host` passthrough, which needs the owner's host mouse rebound to
  WinUSB and is not something to do to a working machine unasked. **The
  owner's call.**
- **The no-regression reading on every other guest is takeable in QEMU as
  written**, because it is a regression read rather than a Low-Speed one:
  hidusbf on each guest, the Full-Speed `usb-mouse,usb_version=1` behind a
  `usb-hub`, `bInterval` 4, 2 and 1, `ep.open.rate` and `ep.open.ival` from
  the ring. What it shows is that the Full-Speed path is untouched and what
  each usbport passes down. It cannot show the Low-Speed arm on any of them,
  and the record must not say it does.
- **hidusbf itself.** The Phase 20 copy was never kept (`out\` and the
  overlay were discarded). Fetching `hidusbf.zip` again from
  `github.com/LordOfMice/hidusbf` through this host's proxy returns a McAfee
  Web Gateway scanning page in place of the archive, by three routes (the
  `raw` path, `raw.githubusercontent.com`, and the contents API with the raw
  media type); the owner is downloading it by hand. It goes to
  `tools\hidusbf.zip`, extracted to `tools\hidusbf-extracted\`, both
  git-ignored under `tools/`; the README there records hash and version the
  way `tools/sweetlow-extracted/README.md` does.
- The device matrix on both primary targets against
  `runs/run-23-post-release/`, and the cut, are 24.5's.
