# Handoff - 2026-09-18, branch `1.1.0.0`

For the next session picking up Phase 22. Read `AGENTS.md` and
`.claude/memory/MEMORY.md` first (the last four entries are this session's).

## Where things stand

| Task | State |
|---|---|
| 22.12 (b) root-hub port suspend/resume | **Done**, commit `16d5ad2`. Read on Vista x86 (XP and Windows 7 cannot reach it under the 0x20 flag). Found and fixed a real defect: the resume timer abandoned a port still in U3, QEMU ignores a PLS=Resume write, and Vista's usbhub bugchecked 0xFE (8, 6, 1) after 60 s. Codex: 2 rounds, converged. `runs/run-22.md` 22.12 (b) is the record. |
| 22.9 harness extension (XP x64 + Win7 x86 as post-release targets) | **Committed with this file, NOT yet reviewed by Codex** - the launch printed "started" but no job or log was ever created. Owed: one Codex round on that commit (prompt draft below). |
| 22.9 the run itself | **Not run, and now ordered AFTER the cut** (owner, 2026-09-18). Roadmap, `run-22.md` and the cut checkpoint re-worded. |
| 22.8 prose (history.md, release date in `xhci_version.h` and both INFs, release notes, README Install, issue forms) | Open. |
| 22.10 the cut + nine install legs from the asset | Open. Next after 22.8. Gives 22.12 (a) (`usbport services written=16` on XP x64 and Vista x64). The Vista x86 leg also re-reads the (b) fix on the `release` flavour. |
| 22.12 (c) recovery under CNR/HCRST, (d) iso counters | Open; both may follow the cut. (d) comes off 22.9's audio rows. |

## Suggested order

1. Codex round on the harness commit (below). Fix and re-review until a round
   returns "No actionable findings."
2. 22.8, then 22.10 (the cut, both architectures, nine install legs).
3. 22.9 on the cut's tree: prepare all four fresh images, run
   `run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -PostRelease`.
4. 22.12 (c) whenever convenient.

## What the harness change did (for the review and for 22.9)

- Driver: `src\xhci_dispatch.c` `xhciStartController` prints
  `StartController extension VA high=` / `low=` under `#ifdef _WIN64`. Only the
  amd64 `qemu` build carries it (the trace channel is qemu-only); x86 and the
  shipping amd64 flavours are unaffected.
- `gen-offsets.ps1 -Arch amd64` -> `offsets-amd64.txt` (SIZEOF 95,544; x86 92,304).
- `lib\counters.ps1`: 64-bit guest addresses (`Format-GuestAddress`, no Double
  arithmetic), `Import-CounterTable -Arch`, identity from the high/low pair.
- `lib\fresh.ps1`: `Get-TargetFamily` (`win98`/`win2k` via `Like`/Id,
  `winxp64`/`win7` via `Family`), `Get-TargetArch`, never-boot list + `winxp64.img`, `win7.img`.
- `run-matrix.ps1` one table per arch; `prepare-image.ps1` per-arch package and
  table, `-accel`/`-smp` in prep boots, NT 5.2/6.1 wizard text.
- Targets `xp64-fresh` (port 56598, `Arch='amd64'`) and `win7-fresh` (56599) in
  `config.sample.psd1` (sample ports 55610/55611) and the git-ignored
  `matrix.config.psd1`; ExpectNoDriver guesses for uas/serial/braille.
- Self-test: 292 checks, all pass. `-PostRelease -ValidateOnly` on this host
  lists only the four expected prep gaps (2a/2b stamped 1.0.2.0; xp64/win7 not cloned).
- Not yet measured on the new guests: the liveness probe (PIT IRQ0 on an MP
  HAL), boot/ready deadlines under TCG (set to 600 s), whether the keep-alive
  mouse binds before the ready poll.

Codex prompt draft: the file `codex-229-r1.txt` from this session was in the
session scratchpad and is gone; re-describe the commit above and ask for the
same five areas: amd64 reads through the x86 table or a truncated address,
PowerShell 5.1 arithmetic/parsing, Phase 10 and 2d regressions, the driver
line's compile/IRQL rules, doc consistency. Drive Codex per
`.claude/memory/codex-plugin-invocation-here.md` **from the PowerShell tool
directly** - launching it through `bash -> powershell -Command` is what lost the job.

## 22.9 preparation, per target (after the cut)

`scripts\vm-matrix\README.md`, "The post-release run", is the procedure; per
target: `prepare-image.ps1 -Target <id> -Clone -FreshCopy`, `-Boot -Xfer`,
install in the guest, `-Status`, (`-Attach` per class on 98), clean shutdown
(`quit` at the monitor on the NT ones), `-Stamp`. Build first:
`build-driver.cmd qemu`, `build-driver.cmd qemu -amd64`, `make-package.ps1
-Flavor qemu` and `-Arch amd64`, `gen-offsets.ps1` and `-Arch amd64`.

**`vm\fresh-2b.img` was re-cloned today and booted once for prep, then quit
unfinished - it is unstamped and half-prepared.** `-Clone -FreshCopy` again.

## Guest-driving notes (see the memories for detail)

- NT guests (XP x64, Vista, 7) and this session's XP x86: keyboard **US
  Dvorak** - `sendkey` names QWERTY positions (Win+R is `meta_l-o`; Alt+C is
  physical `i`). Login `test` / `test`.
- XP and Vista apply pointer acceleration; relative moves in 3-5 unit steps
  (~0.74-0.8 ratio). `mouse_move` goes to the newest QEMU mouse - never move
  the pointer while a USB mouse under test is suspended; drive by keyboard.
- UAC and the unsigned-driver prompts take a click (Vista/7) or the accelerator.
- Harness for 22.12 (b): `out\post-release\task22-12b\` (git-ignored) - launchers
  `xp-b.cmd`, `vista-b.cmd`, helpers `mon.ps1`, `type.ps1`, `goto.ps1`, `rel.ps1`;
  overlays in `vm\t2212\` (throw-away, ~7 GB, safe to delete).
