# Handoff - 2026-09-18 (evening), branch `1.1.0.0`

For the next session picking up Phase 22. Read `AGENTS.md` and
`.claude/memory/MEMORY.md` first; the newest entry,
`cut-1-1-0-0-2026-09-18.md`, is this session's and carries the owner's
readme rules.

## Where things stand

**`1.1.0.0` is cut and committed.** Nothing has been uploaded, so a finding
can still re-cut it under the same number with `make-release.ps1 -Force`
(`releases/README.md`). HEAD is `d1c9bca`; the working tree is clean.

| Task | State |
|---|---|
| 22.8 prose | **Done**, `26162ae`. Release date `09/18/2026` in `xhci_version.h` and both INFs; history entry, release notes, README Install, issue forms. |
| 22.9 harness (XP x64 + Win7 x86 targets) | **Done, Codex-converged**: `fcbf9a1` and the round-1 fixes `2dba77a`; round 2 had no findings. Self-test 298 checks. |
| 22.10 the cut | **Done**: cut at 14:50, then re-cut four times the same day with `-Force` for the owner's readme changes (`02f4ba1`, `681b707`). `releases\1.1.0.0\` holds `release-x86`, `debug-x86`, `release-x64`, `debug-x64`, the two tools, `LICENSE` and `readme.txt`. The asset is `out\xhci98-1.1.0.0.zip`, **396,894 B**, git-ignored, with no Microsoft file. |
| 22.10 the nine install legs | **NEXT. None taken.** |
| 22.12 (a) | Read during 22.10's XP x64 and Vista x64 legs: `usbport services written=16`. |
| 22.12 (b) | Done (`16d5ad2`); the Vista x86 leg re-reads it on the `release` flavour. |
| 22.9 the run | After the install legs (owner's decision: cut first). |
| 22.12 (c) recovery refusal under CNR/HCRST, (d) iso counters | Open; both follow the cut. (d) comes off 22.9's audio rows. |
| Acceptance test + upload | Not a roadmap task: `docs/using/release-acceptance-test.md`, run by hand twice (a fresh VM and a physical machine), then the owner uploads. |

## Next: 22.10's nine install legs

`docs/contributing/runs/run-22.md`, section 22.10, lists each leg and what it
must read. Every leg installs from the **unzipped asset**
(`out\xhci98-1.1.0.0.zip`), not from `src\obj*` or `out\pkg-*`.

1-4. **x86 package (`RELEASE-X86\`), with the full device clauses:**
   - Windows 98 SE, on NUSB and on SweetLow's stack.
   - Windows ME.
   - Windows 2000 SP4.
   - 32-bit XP.

   For each: the three devices, and disable / enable / remove / rescan
   wherever that target can take them. On the NT pair, read `setupapi.log`
   for `Section: Xhci.Dev` and `[Xhci.Dev.NTx86]`.
5. **XP x64, `RELEASE-X64\`.** This is the first XP x64 install through
   `%Mfg%=XhciModels,NTamd64,NTamd64.6.0`. Read `setupapi.log` for
   `XhciModels.NTamd64` and `[Xhci.Dev.NTamd64]` (not `Xhci.Dev6`), and
   check the four OS files on disk. Also read 22.12 (a).
6-7. **Vista x64 and Windows 7 x64, `RELEASE-X64\`**, each off its
   clean-install snapshot with driver signature enforcement disabled.
   - Take 21.5's clauses.
   - Read `setupapi.log` for `XhciModels.NTamd64.6.0` and
     `[Xhci.Dev6.NTamd64]`.
   - Windows 7 x64: its first install through the committed INF.
   - Vista x64: 22.12 (a).
8-9. **Vista x86 and Windows 7 x86, `RELEASE-X86\`**, off
   `vista-clean-install` / `win7-clean-install`.
   - The first `release` flavour on either, and Windows 7 x86's first
     install through the committed INF.
   - Read `setupapi.log` for `XhciModels.NTx86.6.0` and `[Xhci.Dev6.NTx86]`.
   - **Write down what the unsigned-driver prompt says and which choice
     took it.**
   - Vista x86 also re-reads 22.12 (b).

Then check the asset's file list against what the packager staged, and record
each leg in `run-22.md` 22.10. **The `release` flavour writes no
port-`0xE9` trace.** Read it the way task 21.5 did (its entry in `run-21.md`):
Device Manager state, devices bound, `setupapi.log`, and the
monitor. Whether (a)'s line can be read at all on `release` needs checking
first. If it cannot, it comes off a `qemu` reading or the debug flavour.

## After that: 22.9 preparation, per target

`scripts\vm-matrix\README.md`, "The post-release run", is the procedure. Build
first:
- `build-driver.cmd qemu` and `build-driver.cmd qemu -amd64`
- `make-package.ps1 -Flavor qemu`, with and without `-Arch amd64`
- `gen-offsets.ps1`, with and without `-Arch amd64`

Then for each target (`2a-fresh`, `2b-fresh`, `xp64-fresh`, `win7-fresh`):
1. `prepare-image.ps1 -Target <id> -Clone -FreshCopy`
2. `-Boot -Xfer`, then install in the guest
3. `-Status`, and on Windows 98 `-Attach` per device class
4. A clean shutdown (`quit` at the monitor on the NT guests)
5. `-Stamp`

Then run
`run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -PostRelease`.

- `vm\fresh-2a.img` and `fresh-2b.img` are stamped `1.0.2.0`, so both need
  re-preparing. **`fresh-2b.img` was re-cloned, booted once and left
  half-prepared**; run `-Clone -FreshCopy` on it again.
- Not yet measured on the new guests:
  - the liveness probe (PIT IRQ0 on an MP HAL);
  - the boot/ready deadlines under TCG (600 s);
  - whether the keep-alive mouse binds before the ready poll.
- The `ExpectNoDriver` rows for uas, serial and braille on the two new targets
  are guesses. The first run corrects them.

## Owner's rules for the download readme (2026-09-18)

These apply to the `make-release.ps1` template and to the `history.md` entry
the readme embeds:
- The top is a point-form OS list.
- The log request asks for FULL.LOG, and for PROBE.LOG only if the full run
  did not finish.
- Reporting text goes in an unnumbered "ISSUE REPORTING" section; there is no
  "what the version number means" section.
- **Do not name Windows Server 2003.**
- **Do not name F8.** Say "driver signature enforcement must be disabled".

The release notes and README follow the F8 rule too (`9483496`); they still name
Server 2003 x64, which the owner has not asked to change. A readme change after the cut means a re-cut with `-Force`, and
the recorded asset size in the roadmap, `run-22.md` and this file must follow.
An editor tab left open on `releases\1.1.0.0\readme.txt` shows the old file
after a re-cut: the cut replaces the directory, so reopen the tab.

## Traps met this session

- Driving Codex: see `.claude/memory/codex-plugin-invocation-here.md`.
  **Launch it from the PowerShell tool directly**; through
  `bash -> powershell -Command` the job was lost.
- A PowerShell `ReadAllText`/`WriteAllText` edit must keep each file's
  BOM and CRLF:
  - `scripts\inf-gate\test-inf-checks.ps1` has a BOM.
  - A LF-only search string misses a CRLF file.
  - Normalise to LF, replace, then restore the line endings.
- The INF gate's self-test had the release date typed in (now a regex).
  If a gate self-test fails right after a date or version bump, look for a
  hard-coded literal first.

## Guest-driving notes (see the memories for detail)

- NT guests (XP x86 and x64, Vista, 7) use **US Dvorak**. `sendkey` names
  QWERTY positions: Win+R is `meta_l-o`, and Alt+C is physical `i`. Log in
  as `test` / `test`.
- XP and Vista apply pointer acceleration. Make relative moves in 3-5 unit
  steps (a ratio of about 0.74-0.8).
- `mouse_move` goes to the newest QEMU mouse. Never move the pointer while a
  USB mouse under test is suspended; drive by keyboard instead.
- UAC and the unsigned-driver prompts take a click (Vista/7) or the
  accelerator key.
- The 22.12 (b) harness is in `out\post-release\task22-12b\` (git-ignored):
  - launchers `xp-b.cmd` and `vista-b.cmd`;
  - helpers `mon.ps1`, `type.ps1`, `goto.ps1` and `rel.ps1`;
  - overlays in `vm\t2212\` (throw-away, about 7 GB, safe to delete).
