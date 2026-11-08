# Issue 25 - On Windows 98 SE and ME, unplugging a USB audio device while Windows' own usbaudio.sys plays it stops Windows

Status: **ME fixed in `3.4.2.0`; 98 SE a known limitation of `3.4.2.0`**
(owner, 2026-11-07).
- **ME:** stopped at about one unplug in four while playing. Option B
  (a departed device's held isochronous requests released at its
  SELECT_CONFIGURATION, 98 SE and ME only; `459730a5`, merged into
  `3.4.2.0` as `c184c508`) ships for ME: 0 stops in 54 unplugs while
  playing (20 at a root port, 8 behind a hub, 16 in race checks, 10 on a
  later test build that runs as option B on ME).
- **98 SE:** no workaround in `xhci98.sys` was found. With a real UAC 1.0
  device, every valid unplug while playing has frozen 98 SE (`3.4.1.0`,
  option B) or ended in a fatal exception 0E at `0028:C001545A` (every
  test build that let the stream's requests go), but one: a test build
  that also answered the client's unconfigure with success (Q2) survived
  1 of 3. None of the test builds ships.
- **Known limitation, as worded for `3.4.2.0`:** "On Windows 98 SE, stop
  audio playback before unplugging a USB audio device." ME needs it no
  longer with `3.4.2.0`; on earlier releases it applies to ME too. An
  unplug with nothing playing has never stopped either system.

**Scope.** Windows 98 SE and Windows ME, with Windows' own `usbaudio.sys`
(USB Audio Class 1.0) playing a USB audio device, and the device unplugged
while it plays. Not seen with usb-wdm-free's `uac2` on 98 SE (1 of 1 on
`3.4.1.0`, 5 of 5 on option B) nor on Windows XP (5 of 5, read on option
B only; section 4); but `uac2` drives only UAC 2.0 devices (its INF binds
protocol `20` only, and its design leaves UAC 1 devices to Windows'
driver), and those `uac2` readings were with a UAC 2.0 device, a Sound
Blaster X4. A UAC 1.0 device on 98 SE always has Windows' `usbaudio.sys`. On ME the same act also stops Windows on its own
UHCI stack with no `xhci98.sys` loaded, though at another address
(section 3); that suggests, without showing, that the cause is not this
driver's alone. What this driver can change is the order and the way it
gives back the stream's requests. Found in the Phase 47 legs
(2026-11-06), on QEMU's emulated `usb-audio` (UAC 1.0, Full Speed) and on
a real Logi USB Headset (`046D:0A64`, UAC 1.0, Full Speed, a composite of
audio and HID) passed through to QEMU.

## 1. What was seen

During the 47.28 legs (2026-11-06) a 98 SE guest stopped once with "A
fatal exception 00 has occurred at 0028:FF09FED4 in VXD USBAUDIO(01) +
00002ED4" (98 SE's `usbaudio.sys` on QEMU's `usb-audio` under `3.4.1.0`)
around an unplug and a plug on other ports; it did not repeat on a fresh
cold start. Asked to reproduce it and compare with NUSB, a second round
the same day stopped both stacks: `xhci98.sys` with the same `USBAUDIO(01)
+ 00002ED4`, and NUSB 3.3 with Windows' UHCI driver with "A fatal exception
0E has occurred at 0028:FF06A848" (no module named). The step that faulted
was an unplug while Sound Recorder played; the idle unplugs never faulted.
That reading, "unplug while playing, on both stacks", is what started the
work, and half of it was wrong (section 2).

## 2. The first wrong turn: on 98 SE that fault is the play, not the unplug

The first round had never played anything (Sound Recorder: "a sound
device is not installed"), so its unplugs were idle; the second round was
the first play at all, and its screenshot came 27 s after the play started
and 15 s after the unplug. Read again with no unplug:

| Stack | Steps | Result |
|---|---|---|
| `xhci98.sys` `3.4.1.0` | a first plug, then play | fatal exception 00 at `0028:FF09FED4`, `USBAUDIO(01) + 00002ED4`, within 12 s of the play |
| NUSB 3.3, Windows' UHCI | cold start, plug, play | fatal exception 0E at `0028:FF06A848` within 7 s (CR2 `007004A9`, a data address) |

Both are the Phase 9 finding (`lessons.md`, "Windows 98 SE cannot play
USB audio here"): 98 SE's `usbaudio.sys` on QEMU's `usb-audio` stops 98 SE
once it streams, under either stack, with no unplug. So QEMU's device
cannot show what 98 SE does at an unplug while playing, and the NUSB
fault is not evidence that 98 SE's unplug crash is stack-independent. A
real device was needed for 98 SE (section 5). Which module holds
`FF06A848` was not named: the fault screen names none, and the address
lies in the region the WDM images share.

## 3. ME: an unplug while playing stops it, on Windows' own stack too

ME's `usbaudio.sys` plays QEMU's `usb-audio`. An unplug about 12 s into a
play:

| Stack and build | Valid unplugs | Stops |
|---|---|---|
| `xhci98.sys` `3.4.1.0`, QEMU `usb-audio` | 20 | 5: "An error has occurred ... NTKERN(01) + 0000EECA Error: 0E : 0028 : C002EFDA", CR2 `00310044` each time |
| ME's own UHCI stack, no `xhci98.sys`, QEMU `usb-audio` | 10 | 5: "Error: 0E : 0028 : 0B70001D", CR2 the same address |
| `xhci98.sys` `3.4.1.0`, the Logi headset | 20 | 4, the same NTKERN screen (and 1 in 2 in a run cut short by a power trip) |

A trial counted only when Sound Recorder's waveform was on screen at the
unplug. No idle unplug stopped ME on either stack.

**What the bus does at the removal**, from a test build with a bounded
trace of isochronous completions, PnP minors and the requests a departed
PDO receives (the `qemu` flavour, uncommitted):

1. At the unplug ME's `usbaudio.sys` aborts and resets its isochronous OUT
   pipe and resubmits; four requests of ten packets are on the ring.
2. About 2 s later the port's disconnect reaches the bus. Design record 13
   section 10.5's teardown holds the four on the PDO (`HcdIoPark`), as it
   holds every request of a departed device on 98 SE and ME until its
   client acts or its REMOVE comes.
3. About 0.3 s later, after the root hub's relations, ME's `usbaudio.sys`
   sends SELECT_CONFIGURATION to the departed PDO (refused `DEVICE_GONE` at
   the next tick), and ME sends the REMOVE with the four still pending.
4. The REMOVE completes the four `USBD_STATUS_CANCELED` /
   `STATUS_CANCELLED` one after another inside its dispatch. In the
   faulting trial the trace ends at the fourth completion's marker and ME
   is at the NTKERN screen; another trial with the same sequence survived.

On `xhci98.sys` that points at an order-sensitive failure above the
bus, involving the held requests' completion inside the REMOVE. The UHCI
control's fault is a different screen (`0028:0B70001D`, a call through a
bad pointer by its CR2) and its order of completions was not traced, so
whether it is the same failure is not known. It does not say what NTKERN
faults on: that is Microsoft's code, and it was not read. The requests'
fields were read and none contradicts the Windows 2000 DDK's `usbdi.h`; no
zero packet count, packet table or frame interval is returned.

## 4. ME's fix: option B

Three test builds were read on ME with QEMU's device (design record 13
section 18.3):

| Candidate | Valid unplugs | Stops | The REMOVE after the departure |
|---|---|---|---|
| none (`3.4.1.0`) | 20 | 5 | about 0.3 s |
| 1: an isochronous request not held, every target | 20 | 0 | not read |
| 2: held, then released `CANCELED` at a SELECT_CONFIGURATION on the departed PDO | 20 | 0 | 0.22 s |
| 3: candidate 1 for 98 SE and ME only, an abort's `CANCELED` kept | 26 | 0 | 2.0 s and 2.4 s |

Candidates 1 and 3 complete the stream `DEVICE_GONE` at once, and ME's
`usbaudio.sys` answers each failure with an abort, a reset and new
requests, each refused at the next tick, until its REMOVE: at least 111
aborts, 111 resets and 1,189 refused requests in 2.4 s (a lower bound, the
trace's budget ran out). Candidate 2 keeps the status and moves only the
time of the completions, before the REMOVE instead of inside it; so on ME
the time matters, not the status.

The owner chose **option B** on 2026-11-06 (design record 13 sections
18.5 and 18.7, commit `459730a5`, merged into `3.4.2.0` as `c184c508` on
2026-11-07): on 98 SE and ME only, any
SELECT_CONFIGURATION reaching a departed PDO first completes the PDO's
held isochronous requests `CANCELED`, inside the select's dispatch as an
ABORT_PIPE's completions run, and latches the PDO so an isochronous
request the teardown reaches later is completed `CANCELED` rather than
held into the REMOVE. Interrupt, bulk and control holds are unchanged (so
hidclass is untouched), as are a contained controller's and a ghost's
holds, and the NT targets, to which option B is not applied: on NT the
departure also precedes the SURPRISE_REMOVAL, and a client may select
while handling it before passing it down, an order not read there
(design record 13 section 18.5). The select itself is still refused `DEVICE_GONE`. It helps only a
client that selects before passing its REMOVE down, which ME's
`usbaudio.sys` does. The code was reviewed and revised, and its gated
build (x86 and amd64, `release` and `qemu`) passed.

On option B's gated `release` image:

| Leg | Result |
|---|---|
| ME, QEMU `usb-audio`, unplug 12 s into a play at a root port | 0 stops in 20 valid unplugs (`3.4.1.0`: 5 in 20) |
| ME, the device behind a hub, the hub unplugged while playing | 0 in 8 |
| ME, idle plug, play, close, unplug | 5 of 5 played, no stop |
| ME races: unplug 1 s into the play; the client's close and the unplug together; the close 0.3 s after the unplug; a replug 1 s after the unplug | 4 of 4 each, no stop |
| ME, a keyboard unplugged and replugged and a tablet unplugged while playing; a stick plugged, 3,000,000 bytes copied from it to C:, and unplugged | no stop |
| XP SP3, QEMU `usb-audio`, unplug while playing | 5 of 5 survived |
| 98 SE, usb-wdm-free `uac2` with a Sound Blaster X4, unplug while playing; and while recording | 5 of 5; 3 of 3 |
| 98 SE, `uvc98` with a simulated camera, unplug while streaming, then replug | as `3.4.1.0`: 4 of 4 streamed, 3 of 3 offered again |
| Windows 2000 SP4, one vCPU, Driver Verifier on `xhci98.sys` | 6 plugs and unplugs, no bugcheck; the guest could not play the device, so no unplug while streaming |

Not run: MIDI (no guest at hand; a bulk pipe, which option B does not
touch) and Windows 2000 with two vCPUs (no multiprocessor base).

## 5. 98 SE with a real device: a wait, not a fault

98 SE plays the Logi headset under `xhci98.sys` (98 SE's own stack, NUSB
3.3 with Windows' UHCI, leaves it in "Other devices" with no driver, so
there is no Windows control on 98 SE). An unplug about 12 s into a play:

- On `3.4.1.0` the first unplug **froze** the guest: its clock stopped at
  that minute, and every later screenshot is the same frame. A script had
  logged "0 faults in 20"; the 19 later trials never played, so that count
  is void. Only the first unplug in a boot was a real one.
- On the trace build the same freeze. At the departure three isochronous
  requests were held on the audio function's PDO. The HID function's two
  interrupt reads were cancelled by their client, hidclass sent
  ABORT_PIPE, and the HID PDO got its REMOVE, which completed. **Nothing
  ever reached the audio PDO again**: no SELECT_CONFIGURATION, no
  ABORT_PIPE, no cancel, no PnP IRP. Its three requests stayed held.
- On option B the same freeze: 98 SE's `usbaudio.sys` sends no
  SELECT_CONFIGURATION to the departed PDO before its REMOVE while playing
  (it did once at an idle unplug), so B's trigger never comes.

So 98 SE's `usbaudio.sys`, handling the removal above the bus, waits for
its outstanding stream requests before it passes anything down, and the
bus holds those requests until that client aborts, cancels, selects or
passes the REMOVE down. Each waits for the other. ME's `usbaudio.sys`
selects first and does not wait, which is why option B reaches ME and not
98 SE. On option B's image, sampled at the freeze, QEMU was busy, every
EIP sample was in the `C000A4D8` to `C000A881` range, and Ctrl+Alt+Del
listed "Msgsrv32 [Not responding]".

## 6. 98 SE: what did not help

Every change below let the held requests go; each turned the freeze into
a fatal exception, the same one each time: "A fatal exception 0E has
occurred at 0028:C001545A" (CR2 `00C90FA6`; the screen names no module).
The screens and port-0xE9 traces are kept together with the run's VM kit
(`ev\fault-0e\`, with a README).

- **Not held at all** (candidate 3, a test build): at the unplug, ME's
  retry pattern (abort, reset, three refused requests, again), then the
  fault.
- **Released `CANCELED` once PnP is told** (a test build on top of option
  B: when the root hub's relations answer first omits the gone PDO, its
  held isochronous requests complete `CANCELED` and the PDO is latched):
  3 faults in 3 (ME: 0 in 8). The trace: the three requests complete
  `CANCELED` at the drain; `usbaudio.sys` still answers with about 50
  rounds of abort, reset and resubmit, each refused `DEVICE_GONE`; the HID
  function is removed; then `usbaudio.sys` sends SELECT_CONFIGURATION to
  the audio PDO, and 98 SE stops before the REMOVE. With nothing playing,
  the same 98 SE sends the select and takes its REMOVE cleanly.
- **Completed early, with success, at the stream's pace** (test build P,
  98 SE only, on option B: a departed device's isochronous requests, held
  or new, complete `USBD_STATUS_SUCCESS` at their own frame time, as an
  OUT stream to an absent device would on the wire): the stream drains,
  but 98 SE faults the same way right after the bus refuses
  `usbaudio.sys`'s unconfigure (a SELECT_CONFIGURATION with no
  configuration descriptor) on the gone device.

So neither the time nor the status of the stream's completions saves
98 SE once a stream was running; what the last trace points at is the
refusal of that unconfigure. That 98 SE faults inside its own teardown is
what the screens and traces show; which code faults was not read.

## 7. 98 SE: answering the unconfigure (test builds Q and Q2)

- **Q**: P, and on 98 SE a departed PDO's SELECT_CONFIGURATION, RESET_PIPE
  and SYNC_RESET_PIPE answered `USBD_STATUS_SUCCESS` at the next tick
  instead of refused `DEVICE_GONE`. Its review found that a successful
  select with a configuration descriptor would hand the client unfilled
  handles and pipe information as a valid configuration. **Q was never
  run**: the trials first counted for it ran on a guest whose launch with
  the Q build had stopped on a port clash, and whose preparation carried
  on with the base's `3.4.1.0` `release` driver in place (its port-0xE9
  log is empty). Nor was that a reading of `3.4.1.0`: the guest never
  played. No audio driver was installed for the headset on that base, and
  the scripted check took the Add New Hardware wizard's screen changes for
  a play. A deliberate `3.4.1.0` control on the same base said "a sound
  device is not installed", 0 plays in 4 rounds.
- **Q2**: P, and only an unconfigure (no configuration descriptor)
  answered with success, its configuration handle cleared; a select of a
  configuration is still refused. Reviewed with no findings.

On freshly prepared 98 SE guests, the headset installed and a cold start
taken, each round's play read by eye and the binary proven by its
port-0xE9 trace:

| Build | Valid unplugs while playing | Result |
|---|---|---|
| option B (control) | 1 | frozen |
| P | 1 | fatal exception 0E at `0028:C001545A` |
| Q2 | 3 | 1 survived (the audio PDO got its REMOVE, and the replugged device was found again); 2 fatal exception 0E at `0028:C001545A`, before any REMOVE |
| Q2 on ME, QEMU `usb-audio` | 10 | 0 stops; option B's release fired in all 10, and none of Q2's 98 SE paths: ME runs as option B |

The traces of P and Q2 read alike: the early completions kept the stream
going (about 150 requests), the HID function's PDO got its REMOVE,
`usbaudio.sys` stopped resubmitting by itself and its last requests
completed, then it sent the unconfigure to the departed audio PDO. In P
that was refused and 98 SE stopped; in Q2 it was answered with success,
and 98 SE stopped at the same address twice in three.

So neither the requests' status (`CANCELED`, `DEVICE_GONE`, success), nor
their timing (at once, or at frame time until the client stops), nor the
answer to the select decides it. The fault follows `usbaudio.sys`'s
unconfigure in its teardown of a stream that was running, whatever the
bus answers; with nothing playing, the same teardown is clean. The builds
that hold the requests freeze instead. Q2 would trade a freeze for a fatal
exception at about two unplugs in three, for about 200 lines of timer and
completion code, so the owner took 98 SE as a known limitation; P, Q and
Q2 stay uncommitted test diffs.

## 8. Not the same as 36.41's audio-arrival wedge

Windows 98 SE has a documented wedge of its own when a USB audio device
**arrives** (roadmap-hcd 36.41, `runs/run-36.md`; read again in run 47 as
"wdm6's 98 SE stops after a cold start"): soon after a cold start and a
first plug of an audio device, the frame freezes before any play,
Ctrl+Alt+Del lists "Msgsrv32 [Not responding]", the EIP samples are in
VMM's address range (`C00036xx`, `C000A4D8` to `C000A933`), and it comes
on Windows 98 SE's own UHCI stack as often. A freeze seen in this work's
`uac2` guest (a Sound Blaster X4 and a tablet plugged together right after
the X4's install) was that wedge.

This issue's freeze is told apart by when it comes and by what the bus
shows, not by its look: its EIP samples fall in the same VMM range, QEMU
is as busy, and Msgsrv32 is listed as not responding the same way. It comes at
an unplug while a stream is running, never at an idle unplug and never at
the arrival; and the bus trace shows the audio PDO's isochronous requests
held and no IRP from its client after the departure. A guest that has
taken 36.41's wedge never reaches the play, so it cannot be mistaken for a
trial of this issue as long as a trial counts only when the play is seen
running at the unplug.

## 9. Open questions

- What 98 SE's and ME's faulting code is (the NTKERN screen on ME, the
  unnamed `0028:C001545A` on 98 SE). Microsoft's binaries were not read;
  everything here is from screens, register dumps and the bus's own trace.
- Whether ME's stop on its own UHCI stack (5 in 10, at another address)
  is the same failure as on `xhci98.sys`; whether Windows' UHCI driver gives back the stream's requests
  in the same order as the hold's release was not read (no filter above
  that stack).
- Real hardware: every reading here is in QEMU; the only real devices were
  the owner's Logi headset and Sound Blaster X4, passed through to the
  guests. No bench machine
  was read.
- Windows 2000: no unplug while streaming was readable there.

## 10. Rules kept

- **A first fault is a reading of the step that was running, not of the
  step the script meant.** The 98 SE screenshot came 15 s after the
  unplug and 27 s after the play; the play alone faulted. Re-read a fault
  with the suspected step taken away before building on it.
- **An emulated device that cannot do the thing cannot show what happens
  after it.** QEMU's `usb-audio` cannot stream on 98 SE, so 98 SE's
  unplug had to be read with a real device; the ME readings on QEMU's
  device did not carry over (98 SE waits where ME selects).
- **A count is only as good as the trial's own check.** "0 faults in 20"
  on 98 SE was one real trial and 19 that never played; a trial now counts
  only when the play is seen at the unplug.
- **Read which driver a guest is running, and that a play really
  played, before counting its result.** A test build's launch failed, its
  preparation went on, and a guest on `3.4.1.0` was counted for the test
  build; that guest had no audio driver at all, and a weak scripted check
  took a wizard's screen changes for a play. The `qemu` flavour's
  port-0xE9 trace (or the image's hash in the guest) says which binary
  ran; the screen, read by eye, says whether the play ran.
- **Read the client's order before choosing the release point.** Option B
  is right for a client that selects first and inert for one that waits;
  the trace of which requests reach the departed PDO, and when, decided
  each step.

## Sources

- `docs/contributing/runs/run-47.md`: "The 98 SE usbaudio fault at unplug
  while playing (2026-11-06)" (sections 1 to 11, the record of every
  reading here); the 47.28 section's note of the first sighting; "wdm6's
  98 SE stops after a cold start: Windows 98 SE's audio-load wedge, 36.41"
  for section 8.
- Design record 13 section 18 (`docs/contributing/design/13-superspeed-hcd.md`;
  `e0365409`, `29d9994f`, `459730a5`, merged as `c184c508`).
- usb-wdm-free: `uac2`'s INFs (`USB\Class_01&SubClass_0x&Prot_20` only)
  and `docs/design/uac2.md` ("Q5, UAC 1 devices. Not handled by this
  driver in v1"), on its branch `w47-rel019`.
- `docs/contributing/lessons.md`, "Windows 98 SE cannot play USB audio
  here, and the control is what says so"; roadmap-hcd 36.41 and
  `runs/run-36.md`.
- The guests' traces, screenshots and register dumps, the test builds'
  diffs and the trial logs, kept untracked with the run's VM kit
  (2026-11-06 and 07); the fatal exception 0E's screens, register dump
  and port-0xE9 traces for candidate 3, the option A test and test build
  P are gathered in its `ev\fault-0e\` folder, with a README; the test
  builds' diffs (`phantom-test.diff`, `phantomq-test.diff`,
  `phantomq2-test.diff`) are kept untracked with the worker's notes.
