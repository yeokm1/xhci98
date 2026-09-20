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
| `IMOD.BAT` | Windows 2000 and later (needs `reg.exe`, so XP and later in practice) | Sets, shows or clears `XhciImodExperiment`, the interrupt-moderation interval roadmap task 23.2 sweeps |
| `IMOD98.BAT` | Windows 98 SE and Windows ME | The same, through `regedit /s` and a generated `REGEDIT4` file |

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

`IMOD.BAT` and `IMOD98.BAT` only mean anything to an **experimental build**,
one built with `XHCI_EXTRA_DEFINES=-DXHCI_IMOD_EXPERIMENT`. A shipping binary
does not read the value and never will: the read and the IMOD write are both
behind that define, and `src/sources` turns any nonempty `XHCI_EXTRA_DEFINES`
into `XHCI_DIAGNOSTIC_BUILD`, the marker `make-package.ps1` refuses to package.
A value set against a shipping `.sys` correctly does nothing.

Neither file restarts the controller, and neither pretends to. The value is
read once per start, so nothing is in force until the machine is restarted -
and on **both** bench targets the restart is a reboot rather than a Device
Manager disable, for two different measured reasons. On Windows 98 disabling
any USB host controller devnode bugchecks the machine before the teardown
completes (`build-and-test.md`). On the E460 under 32-bit Windows 7 the first
Disable of this controller never finished (`runs/run-22.md`, task 22.9).

Confirm what the driver actually did with `XHCISNAP -verbosity 2` rather than
with either script's own output: `imod.exp.written` and `imod.exp.readback` are
what make a throughput figure attributable to an interval.

The procedure these serve is `docs/contributing/runs/run-23.md`, "23.2 - the
moderation experiment".
