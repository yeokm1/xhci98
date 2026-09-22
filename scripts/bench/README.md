# scripts/bench/ - helpers that run on the target, not on the build host

Small batch files copied onto a bench machine or a guest and run there. They
configure or read something this driver uses and do not build anything. The
build wrapper does not call them and no gate depends on them.

`*.BAT` here is covered by the repository's two standing rules for batch files:
`.gitattributes` checks them out CRLF, and `xhciqual/test/check-bat-eol.ps1` -
which `build-driver.cmd` runs - refuses a bare LF anywhere in the tree, because
MS-DOS 7.1 COMMAND.COM can fail to find a `goto` label in an LF-only file and
skip its error paths silently. `scripts/check-source-charset.ps1` holds them to
ASCII.

## The files

| File | Target | What it does |
|---|---|---|
| `IMOD.BAT` | Windows 2000 and later (needs `reg.exe`, so XP and later in practice) | Sets, shows or clears `XhciImodInterval250ns`, the interrupt-moderation interval the driver reads at every start (roadmap task 23.4; task 23.3 swept it) |
| `IMOD98.BAT` | Windows 98 SE and Windows ME | The same, through `regedit /s` and a generated `REGEDIT4` file |
| `SWEEP98.BAT` | Windows 98 SE and Windows ME | One boot of the sweep: a timed `XHCISNAP` dump before a benchmark run (`SWEEP98 ARM A`) and one after (`SWEEP98 ARM B`) |

**Nothing in a 9x batch file may put `<`, `>` or `|` in a `REM` line.**
COMMAND.COM performs redirection on a comment: until 2026-09-22 the header of
`IMOD98.BAT` quoted examples of the very syntax it warned about, and every run
left files named after their targets in the current folder and printed `File
not found` for the input one. It was found on the P14s Gen 1 during task
23.3's sweep, and `LOAD98.BAT`, `STGF98.BAT` and three `xhciqual` DOS batch
files were fixed with it. cmd.exe ignores redirection on a `REM`, so
`IMOD.BAT`'s usage comment is safe as it stands.

### Why the moderation helper is two files

The same reason `scripts/vm-matrix/guest/` carries `LOAD.BAT` beside
`LOAD98.BAT`: Windows 98's shell is COMMAND.COM and the other targets' is
cmd.exe, and three of the differences are load-bearing here rather than
cosmetic.

- **`IF` has no `/I`.** Case-insensitive comparison is cmd.exe's, so the 9x
  file tests each keyword in the spellings a person actually types.
- **`^` is not an escape character.** An unescaped `<` or `>` in an `echo` is a
  redirection, so the 9x file's usage text contains no angle brackets at all.
- **There is no `for /f`, no `set /a` and no `reg.exe`.** The 9x file therefore
  cannot convert a decimal interval to the hex a `REGEDIT4` dword needs, and
  cannot search the registry for the devnode. So it takes the sweep's ladder
  steps by name from a fixed table, offers a `HEX` form for anything else, and
  has a separate `FIND` step that exports the class key for the operator to
  read the instance number out of once per machine.

### What they need, and what they do not do

Since `1.1.1.0` **every build reads the value** at every start and writes the
result to IR0's IMOD (roadmap task 23.4). The contract is the driver's: 10 to
4000 is used as given, and anything else - absent, 0, 5000 - is replaced by
4000, the hardware reset value, not clamped. An INF install writes 500. So
`CLEAR` is the control arm of a sweep and **not** what the package installs;
set 500 to put a machine back, and leave a bench machine at 500 when a session
ends or say in the run sheet that it was not.

*(Task 23.3 used an experimental build, `XHCI_EXTRA_DEFINES=-DXHCI_IMOD_EXPERIMENT`,
packaged through a `make-package.ps1 -ImodExperimentArtifact` exception. It
took 0 to 65535 and wrote nothing when the value was absent. The owner retired
both on 2026-09-22 when 23.4 folded the read into every build; the shipping
range still reaches down to 10.)*

Neither file restarts the controller, and neither pretends to. The value is
read once per start, so nothing is in force until the machine is restarted -
and on **both** bench targets the restart is a reboot rather than a Device
Manager disable, for two different measured reasons. On Windows 98 disabling
any USB host controller devnode bugchecks the machine before the teardown
completes (`build-and-test.md`). On the E460 under 32-bit Windows 7 the first
Disable of this controller never finished (`runs/run-22.md`, task 22.9).

Confirm what the driver actually did with an `XHCISNAP` dump rather than with
either script's own output: the `XhciImodInterval250ns` lines of its `.TXT`
give the value read, the interval in force and what the register read back,
and those are what make a throughput figure attributable to an interval. The
channel has to be engaged (`XHCISNAP -verbosity 1` or higher) at the start the
dump is taken in, so set it before the same reboot.

The procedures these serve are `docs/contributing/runs/run-23.md`, "23.3 - the
moderation experiment" and 23.5, the audio test.
