# Design Record 26 - The Windowed Installer

Status: draft 26, 2026-10-22 (the owner's simplification of that day: Welcome, Components, Summary, Progress, Steps, Finish, the settings pages replaced by one Settings dialog, room for roadmap 43.48's rows, section 5.0); draft 25, 2026-10-22 (roadmap-usb4 task 43.29, the owner: on 8 and 8.1 `xhciuas.inf` staged, never forced, section 5.3.1 case 3a); draft 24, 2026-10-22 (roadmap-usb4 task 43.42, the owner: no
preselection of XhciSilentInstall, section 6.5.12 item 2; record 23
revision 29); draft 23, 2026-10-22 (roadmap-usb4 task 43.43, the owner's
answers of that day: every proposal of section 5.3.1 accepted but the
refusal of a package without the connection manager on 8 and 8.1, which
becomes case 5's warning; record 23 revision 28); draft 22, 2026-10-22 (roadmap-usb4 task 43.42, the owner's
option A of that day: with the storage row on 98 SE the Bulk-Only
entries added to SilentClasses and XhciSilentInstall preselected,
section 6.5.12); draft 20, 2026-10-21 (roadmap-usb4 task 43.43, the owner's
decisions of that day: every Components box shown and tickable on every
system, the defaults as they are, a box ticked where it does not apply
warned in one message box on Next and the run going on, section 5.3.1;
sections 5.3, 6.5.2, 6.5.9, 6.5.10 and 12; record 23 revision 26; the
proposals in 5.3.1 are the owner's to confirm); draft 19, 2026-10-21 (roadmap-usb4 task 43.42, the owner's
decision of 2026-10-20, NUSB parity for the first plug: the pack's two
INFs placed in `%windir%\INF` by this installer, never over a file of
that name, recorded and removed by the uninstall and withdrawn with the
pack, section 6.5.11; with it two texts of the 98 SE and ME steps,
section 7; record 23 revision 25, record 27 draft 11); draft 18, 2026-10-20 (the owner's answers to draft 17's open
questions: the uninstall adopts the storage devices bound from this
installer's copy after the run, section 6.5.8, and the rest accepted as
designed, section 14); draft 17, 2026-10-20 (roadmap-usb4 task 43.4, the owner's
decisions of that day: the USB storage drivers on Windows 98 SE, NUSB
3.3's seven files from the download's `NONFREE\`, a Components row of
their own that never copies over a file already present and an
uninstall that removes what it placed, section 6.5; record 23 revision
24, record 27 draft 10); draft 16, 2026-10-19 (the fix round of that night, the owner's
decisions: on 98 SE and ME the bundled Stor98 is updated as a file
through `WININIT.INI`, never by reinstalling a device it runs, with one
Summary box "Please disconnect all external NVMe drives before
proceeding" in place of one per drive, section 6.3; a helper that did
not finish is an unfinished install with steps, section 7; the
Thunderbolt 3 / USB4 box "(experimental)", never ticked unasked, its
note saying whether a controller was found, and Stor98's box ticked with
it, section 5.3; no Stor98 install record without a Stor98 change, and
the uninstall removes the record also when Stor98 stays, sections 6.3
and 9.3; the `XhciCmMode` 0 line on the Summary, section 5.7; record 23
revision 23, record 27 draft 9); draft 15, 2026-10-19 (task 42-A.31, the owner's request of that
day: `SETUP.EXE` at the package root, the program a person double-clicks,
and `INSTALL.BAT` kept for the command line; sections 2, 5.9, 10, 12, 13
and 15, and record 23 revision 22); draft 14, 2026-10-19 (section 9.3 step 4: the setup engine's
`OEM Name` record of each deleted INF copy removed, record 23 revision
20); draft 13, 2026-10-19 (task 42-A.30: the bundled Stor98 at the
package version, one signature, sections 5.6 and 6.3, record 23 revision
19); draft 12, 2026-10-19 (task 42-A.26, the owner's decision of that
day: the bundled Stor98 on drives behind a Thunderbolt or USB4 tunnel
only, and the uninstall's adoption of tunnel drives Windows bound after
the run; sections 5.3, 6.3, 6.4 and 9.3, and record 23 revision 18);
draft 11, 2026-10-19 (task 42-A.22), the owner's (a wider gap
and a line between settings rows, XHCITR's icon, a row of another
system shown greyed, Stor98's box a child of the Thunderbolt one, the
UAS driver a box of its own under the USB drivers') and the matrix
legs' (each Progress row's outcome in place, the Summary's headline
from what it lists), record 23 revision 16; draft 10, 2026-10-19 (task
42-A.29, the owner: `XhciFastPollFsLs` among the USB page's
"Advanced..." rows; sections 5.4 and 11, record 23 revision 15); draft
9, 2026-10-19 (task 42-A.27, section 7: the settle after a root hub's
or hub's job within a pass, record 23 revision 14); draft 8, 2026-10-19
(task 42-A.21, the owner's request of that
day: the connection manager alone, and Windows 8 and 8.1 for it; sections
1.2, 5.1, 5.3, 9.1, 9.3, 10, 12 and 13, and record 23 revision 13);
draft 7, 2026-10-18, reviewed and revised (an INF copy made by
an update recorded without the device); draft 6, 2026-10-18, reviewed
and revised (the INF inventory
around every settle as well); draft 5, 2026-10-18, reviewed and revised
(the 98 SE and ME INF
copies inventoried before and after, the shutdown line's unfinished
form, the USB4 reconnection asked of every such update); draft 4,
2026-10-18, reviewed and revised (Stor98's
explicit bindings recorded on their own evidence, the 98 SE and ME copy
record, the update precautions per system, the USB4 reconnection
outcome, the shutdown line); draft 3, 2026-10-18, reviewed and revised
(Stor98's
ownership by first binding, its staged copies kept across updates, its
update precautions, its 98 SE and ME binding, the staging from the copy,
an incomplete uninstall keeping its entry point); draft 2, 2026-10-18,
reviewed and revised the same day (the
uninstall without a package, Stor98's ownership record, the plan run
again on a component change, the mixed value state, Stor98's settings
read back, the exit precedence, the machine-wide values the uninstall
removes, the multithreaded runtime); draft 1, 2026-10-18, before code.
Written for `roadmap-usb4.md`
Phase 42 task 42-0.1 (planned as `INSTUI.1` until the owner opened the
phase on 2026-10-18), for release `3.1.0.0`. It follows the owner's
decisions of 2026-10-18 recorded in that file's decisions table, "Phase
42, the open choices": the installer is a window, a wizard that walks the
user through the key options and ends by asking for the restart. Section
13 lists the decisions taken in writing it, each the orchestrator's for the
owner to confirm unless it names the owner; section 14 what is open. The
code is task 42-A.2; the uninstall it carries is task 42-A.3; the Windows
98 SE and ME device install it calls is design record 27 (task 42-0.2,
code 42-A.1); the legs are 42-V.1.

Design record 23 stays the authority for everything the installer does to
a system: the refusals, the NT route, the 98 SE and ME route, the registry
values, the install directory, the uninstall's order and its gates, the
exit codes and the log. This record adds a second front end over the same
engine and changes record 23 only where it says so (section 12).

## 1. What it is for, and what it is not

### 1.1 The problem

`INSTALL.BAT` of `3.0.0.0` runs a console program (record 23). It does the
right thing on every target, and the owner's run on a real machine made
its text fit a 79-column console, one choice a line. It is still a console:
a user who has never typed at one meets a black window, a plan in text, a
question with "Enter for [1]", and a last line that asks for a restart the
tool itself never does. The owner's instruction (2026-10-18): the installer
has to be a window that walks the user through the key options and then
asks for the restart.

### 1.2 Scope

- One engine, two front ends. The console tool `XHCIINST.EXE` and
  `XHCIIN64.EXE` stay, for scripts, the F6 floppy's companion steps, the
  legs and unattended installs (`/yes`, `/set`, `/list`, `/log`). The window
  is a second program over the same code (section 3).
- The window shows a wizard: welcome, what was found, the components, the
  USB settings, the Thunderbolt settings and Stor98's settings when their
  components are ticked, the summary, the progress, and the finish with
  "Restart now" and "Restart later" (section 5). The values record 23
  never asks sit behind an "Advanced..." button on each settings page.
- It installs `xhci98.sys`, `xhciuas.sys`, `xhcicm.sys` and, only when the
  user ticks it, the bundled Stor98 (section 6), and on Windows 98 SE,
  when ticked, NUSB 3.3's seven USB storage files from the download
  (section 6.5, draft 17); it puts the xHCI98 tools in
  the install directory (record 23 section 9.6); on 98 SE and ME it drives
  design record 27's helper for each device and shows the Update Driver
  steps for a device the helper could not install (section 7).
- It registers the package under Add/Remove Programs and is the program
  that entry runs (section 9, task 42-A.3).
- Every target of the package: Windows 98 SE, ME, 2000, XP, XP x64, Vista
  and 7 in both architectures, one 32-bit program that starts everywhere and
  under WOW64 starts the x64 one itself (section 4); and from draft 8
  Windows 8 and 8.1, in both architectures, for the Thunderbolt 3 / USB4
  connection manager alone (record 23 section 4.5).

### 1.3 Not in scope

- A Windows Setup path (text-mode or F6): `txtsetup.oem`'s, as before.
- A device bound to another vendor's driver: listed and left (record 23
  decision D3).
- The boot configuration, test-signing mode included: read and reported,
  never changed (record 23 decision D6).
- `XHCICLN`: never run by the installer (record 23 section 5.11).
- A Thunderbolt control panel, an approval, a security level: record 24's
  rule holds for the installer's Thunderbolt page too.
- A localised text: English, as every tool of the package.

## 2. Where it lives in the package

```
<package>\SETUP.EXE                     the entry point for people (draft 15)
<package>\INSTALL.BAT                   the command line's entry point, kept
<package>\tools\xhciinst\XHCIWIZ.EXE    the window, 32-bit (W1)
<package>\tools\xhciinst\XHCIWZ64.EXE   the window, x64
<package>\tools\xhciinst\XHCIINST.EXE   the console tool, 32-bit, kept
<package>\tools\xhciinst\XHCIIN64.EXE   the console tool, x64, kept
<package>\tools\xhciinst\XHCIDI.EXE     the 98 SE and ME helper (record 27)
<package>\tools\xhciinst\readme.txt, NOTICE.TXT
```

The names are 8.3 (`XHCIWZ64` keeps the x64 marker the other pairs use)
because the directory is read by Windows 98's shell and typed at its
console. An install or an update finds the package root two levels up
from the executable's own location, as record 23 section 4.2 requires,
and refuses to run from anywhere else. The uninstall and the device modes
do not: they read the system, never the package, and run from the install
directory's copy or from the temporary copy of section 9.2, with the 98 SE
and ME helper found beside the executable (W12; record 23 section 4.2 is
revised, section 12). `/devices` and `/eject` already run without the
package check today and keep doing so.

**`INSTALL.BAT` stays the launcher** (owner, 2026-10-18). It keeps its
choice of architecture and flavour directory and gains one rule (W2): with
no argument, or with arguments the window accepts (section 10), it starts
the window; with `/console` as its first argument it drops that word and
starts the console tool with the rest. So `INSTALL` double-clicked opens
the wizard; `INSTALL /console /yes /set XhciInterruptMode=2` is the
unattended run the release notes and the legs use today, unchanged but for
the first word. Under `COMMAND.COM` the `/console` form costs one of the
nine arguments, so the batch file's limit there is seven after it, said in
its refusal text as today's eight are. The release notes' examples are
rewritten with the word.

**`SETUP.EXE` is the entry point for people** (owner, 2026-10-19; draft
15, task 42-A.31; W17). It supersedes the paragraph above for a person:
the readme's first instruction is "To install or update, double-click
SETUP.EXE; to uninstall, use Remove in Add/Remove Programs (or SETUP
/uninstall)", and `INSTALL.BAT` is for the command line and
scripts, its behaviour unchanged (with no argument it still opens the
window, and waits for it). Double-clicked, a batch file keeps a console
window open behind the window for the whole run, an MS-DOS box on 98 SE
and ME; a windows-subsystem program opens none. `SETUP.EXE`
(`xhciinst\setup.c`) is one 32-bit image for every target:

- It finds its own directory (`GetModuleFileNameA`), the package root, and
  starts `tools\xhciinst\XHCIWZ64.EXE` on 64-bit Windows
  (`IsWow64Process`, looked up at run time, absent meaning 32-bit) and
  `tools\xhciinst\XHCIWIZ.EXE` otherwise, 98 SE and ME included.
- It starts the window with `ShellExecuteExA`, verb `open`, the working
  directory the package root, and its own command line after its name
  passed on unchanged (`SETUP /uninstall`, `/debug`), then exits at
  once. `ShellExecuteEx` is what raises the window's own
  `requireAdministrator` prompt on Vista and 7; `CreateProcess` refuses
  such an image with `ERROR_ELEVATION_REQUIRED`.
- Its manifest asks for `asInvoker` (no Common Controls: it draws only a
  message box). Without it, Vista's and 7's installer detection would
  elevate a program named `SETUP.EXE` before it starts, a second prompt;
  with it there is exactly one, the window's. It also declares the
  supported systems Vista to 10, as `XHCITR`'s does: without that,
  Windows 7's Program Compatibility Assistant, which takes a program
  named `SETUP.EXE` for an installer, asked "This program might not have
  installed correctly" once the window closed (read in a Windows 7
  guest, 2026-10-19).
- An empty popup of no size, made the foreground window while
  `SETUP.EXE` still has the right Explorer's start gave it, owns the
  elevation prompt (`SHELLEXECUTEINFO.hwnd`) and the message box: a
  declined prompt hands the foreground back to its owner, and with none
  it went to Explorer, so the message opened behind Explorer's window,
  even topmost (read in the same guest).
- A missing window, a path it cannot read, or a failed start is a message
  box naming the problem; the elevation declined (`ERROR_CANCELLED`) has
  its own text. It returns 0 once the window is started and 1 otherwise:
  it does not wait, so the window's exit code is not its own (section
  10). Scripts use `INSTALL /console`.
- Built by `xhciinst\build.cmd` with MSVC 6.0, the C runtime static,
  `/SUBSYSTEM:WINDOWS,4.0`, with the xHCI98 USB Tree's icon
  (`xhcitr\xhcitr.ico`) and the version resource from
  `src\xhci_version.h`; it imports KERNEL32, USER32 and SHELL32 alone, and
  `check-imports.ps1`'s `setup` profile also holds the image to the
  windows subsystem and an embedded `asInvoker` manifest. Its
  `/selftest` (alone on the command line) checks the command-line tail
  and the paths it builds; `selftest.cmd` runs it.
- `make-release.ps1` stages it at the version root beside `INSTALL.BAT`,
  under the install tool's staleness rule; `test-package.ps1` holds the
  staging and the readme's first instruction.
- `README.md` and the release readme offer two ways to install, update
  and uninstall: `SETUP.EXE` (and Remove in Add/Remove Programs), or
  Device Manager by hand (the owner, 2026-10-19). `INSTALL.BAT`, the
  console and its switches are documented in `tools\xhciinst\readme.txt`
  alone, the command-line and scripting reference, which the two point
  at in one line.

Not taken: `INSTALL.BAT` starting the window detached and ending at once
(on 98 SE and ME an MS-DOS box started from Explorer stays open on
"Finished" unless a PIF or a last `CLS` closes it, and on NT the console
still flashes); a renamed copy of `XHCIWIZ.EXE` at the root (the window
finds its package two levels up from itself, section 2).

A double-click on `XHCIWIZ.EXE` itself is the same as `INSTALL` with no
argument: the window reads its own location and picks the `release-`
directory of its architecture, with `/debug` for the other (section 10).

## 3. One engine, two front ends

### 3.1 The split

`xhciinst\xhciinst.c` is today one file: the system half of record 23 (the
scans, the plan, the installs, the staging, the copies, the uninstall, the
tray tool's maintenance, the device modes) with the console's output,
questions and pauses written through it (`out`, `read_line`, `confirm`,
`ask_values`, `choose_dir`, `open_device_manager`, `warn_uninstall`, the
eject's questions). The design moves the system half into
`xhciinst\inst_eng.c`, the engine, and leaves `xhciinst.c` as the console
front end. The engine:

- never writes to a console, never reads one, never calls `exit`, never
  shows a dialog of its own (Windows' own dialogs, the signature question
  and the file-copy prompts, are the routines' and are owned by the window
  handle the front end gives it);
- reports everything through one table of callbacks the front end fills
  (`INST_FRONT`, section 3.2): text for the report and the log, a question
  with its choices, a progress step, a page-worth of structured results;
- keeps every rule of record 23 where it is: the order of operations, the
  settle, the barriers, the retry rule, the gates of the uninstall, the
  final-line rule and the exit code, all of which are the pure half's
  (`inst_core.c`) or the engine's, and none of which a front end can skip;
- exposes the run as a small state machine the window drives page by page
  (section 3.3) and the console drives in one call, so the two front ends
  cannot ask in a different order or forget a question.

The pure half, `inst_core.c`, is unchanged in role: it gains the page
texts, the component rules and the settings table's new columns (section
11), and the host suite covers them row by row as it covers today's.

### 3.2 The front-end table

```
typedef struct _INST_FRONT {
    void *Ctx;
    void (*Line)(void *ctx, const char *text);         /* report text, wrapped by the console, listed by the window */
    void (*Log)(void *ctx, const char *text);          /* log-only text */
    int  (*Ask)(void *ctx, const INST_ASK *q, char *answer, size_t cap);
    void (*Progress)(void *ctx, const INST_STEP *step); /* a step began, ended, or waits */
    int  (*Waiting)(void *ctx, const INST_WAIT *w);    /* a bounded wait is under way; 0 cancels it */
    HWND  Owner;                                       /* NULL on the console */
} INST_FRONT;
```

`INST_ASK` is one question of record 23 rendered as data rather than text:
its kind (a value with choices, yes or no, a directory, the confirmation,
the uninstall's word), the value's row in the settings table, the devices
it applies to, the value now, the default, the choices with their meanings,
the caution. The console renders it with `inst_value_question`,
`inst_yesno_question` and `inst_dir_question` exactly as today, so its
screens do not change; the window keeps the answers on its pages and feeds
them back when the engine asks (section 3.3). `INST_STEP` names a step of
record 23's order (staging, a device's install, a value's write, a removal)
with its device and its result; `INST_WAIT` is the settle or a wizard wait
with its elapsed and bounded times, so the window can show "Windows is
installing a device it found; answer its window" where the console prints
it once.

A front end that returns "cancelled" from `Ask` or `Waiting` gets what the
console's end-of-input gets today: the run stops where record 23 says it
stops, nothing half-done, exit 1 before a change and the stop's own code
after one.

### 3.3 The run as the window drives it

The engine's run is split at the points where record 23 already stops to
ask:

| Phase | What the engine does | Record 23 |
|---|---|---|
| `INST_PH_READ` | the refusals, the rights, the routines, the first reading of the devices, the store trap, test-signing mode | 4, 5.1 steps 1 and 2, 5.10 |
| `INST_PH_PLAN` | the plan for one set of component choices (Thunderbolt support in or out, Stor98 in or out, the tray tool, `/force`, the store trap's replacement), with its warnings, refusals and the questions that would be asked, each with its current value; nothing written; run again over the data `READ` holds, and a fresh reading of the store trap, whenever the choices change | 5.1 step 3, 6.3, 8.5 |
| `INST_PH_ASK` | the answers to the frozen plan's questions (the registry values, the install directory) and the confirmation; nothing written | 5.1 step 4, 8, 9.6 |
| `INST_PH_RUN` | the pending-install check, the second reading and the plan comparison, the copy, the staging, the installs, the barriers, the values, the results | 5.1 steps 5 to 11, 6.2, 9 |
| `INST_PH_END` | the final line and the exit code | 7.4 |

The console calls them in order from `main`, as today: `READ`, then its
Thunderbolt question (record 23 section 8.5) and, where the package
carries Stor98, the Stor98 question (section 6.4), then `PLAN` with those
answers, then `ASK`, `RUN` and `END`, so its order of questions and
writes is unchanged. The window calls `READ` and `PLAN` with the default
component choices while the welcome page shows "Please wait", builds the
Found page from that plan, and runs `PLAN` again whenever the Components
page changes a choice, so the Found page, the settings pages and their
questions follow the choices as they stand (a Back to the Found page shows
the plan for the current choices). The plan the Summary shows is frozen
there for `ASK` and `RUN`, which the Install button starts; the engine's
own second reading before any change (record 23 section 5.1 step 5)
compares against that frozen plan, with "the plan changed; go back to see
it" in place of the console's "run INSTALL.BAT again". Back and Next
move between the window's pages, which hold data, not between the
engine's phases: nothing is written until Install.

The worker thread exists only in the window (W3): the engine is single
threaded and the window posts no second call into it while `RUN` runs. The
window's message loop keeps its page alive, its Cancel button asks the
engine to stop at the next step through `Waiting`'s return (a step that has
started finishes; a device half-installed is never left by a cancel, only
by a failure, as today), and the elevation, the signature and the
file-copy dialogs Windows shows are owned by the window's handle so they
appear in front of it.

## 4. Toolchain, builds and the one program that starts everywhere

As XHCITR (design record 25 section 9), and record 23 section 3 for what
the two share:

- `XHCIWIZ.EXE`: the in-repo MSVC 6.0, `/W3 /WX /O2`, the C runtime
  static and multithreaded (`/MT`, `LIBCMT`: the engine runs on a worker
  thread, section 3.3, made with `_beginthreadex`, and the single-threaded
  `LIBC` the console tool keeps is not safe under two threads),
  `/SUBSYSTEM:WINDOWS,4.0`, so it starts on 98 SE, ME, 2000, XP and
  32-bit Vista and 7; the import table is checked as before, since the
  runtime is static either way. `XHCIWZ64.EXE`: the same sources with WDK 7.1's amd64
  compiler, `/MT`, `/SUBSYSTEM:WINDOWS,5.02`, for XP x64, Vista x64 and
  7 x64. `xhciinst\build.cmd` builds the four executables (and record 27's
  helper when Open Watcom is found, section 7); `build.cmd x86` the 32-bit
  pair and, where Open Watcom is found, the helper.
- Static imports: `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `COMCTL32`
  (the property-sheet wizard), `COMDLG32` (the Save As of the steps page)
  and `SHELL32` (the restart, section 5.9) - every one present on 98 SE.
  Every SetupAPI, `newdev.dll`, configuration-manager and `version.dll`
  routine is looked up at run time, as the engine does today, and so is any
  routine of those seven DLLs that 98 SE's own build lacks (`check-imports.ps1`
  allows exactly the seven for the window and keeps the console tool to its
  two).
- The manifest: `requireAdministrator` (every run elevates on Vista and 7,
  as the console tool's does) and the Common Controls 6 dependency (XP and
  later draw themed pages; 98 SE to 2000 ignore the manifest). On 2000 and
  XP the engine's own rights check stands (record 23 section 7.2), shown
  on the welcome page as a refusal with "run it as an administrator".
- The version resource from `src\xhci_version.h`; `make-release.ps1`'s
  freshness and version checks cover the new pair as they cover the old.
- The icon: XHCITR's own, `xhcitr\xhcitr.ico` (record 25), which
  `xhciwiz.rc` names in place, so the two windows show one icon (owner,
  2026-10-19).
- **The wizard** is `PropertySheet` with `PSH_WIZARD`, the form every one of
  the targets' `comctl32.dll` builds draw (the 97-style header needs the
  IE4-era control library, which 98 SE has and 2000 has, but gains nothing
  the pages need; not used). The page templates are dialog resources; the
  fonts and sizes are the dialog's, so 98 SE's and 7's shells each draw
  their own. No owner-drawn control: the pages are static text, list views,
  check boxes, combo boxes, edit boxes and a progress bar.

**One 32-bit program that starts on every target** (owner, 2026-10-18).
`XHCIWIZ.EXE` starts on every target because it imports nothing a 98 SE
machine lacks. Under WOW64 (`IsWow64Process`, looked up at run time, absent
meaning not WOW64) it does not refuse as the console tool does: it starts
`XHCIWZ64.EXE` from its own directory with its own arguments
(`CreateProcessA`, the x64 image's path from `GetModuleFileNameA` with the
name swapped), waits for it, and exits with its exit code, so a
double-click on the 32-bit program on 64-bit Windows runs the right one
(W4). The x64 program refuses to start on 32-bit Windows by itself (not a
valid application), which nothing invites. The 32-bit program never
installs a driver under WOW64: the engine's refusal stays and is the
fallback when the x64 image is missing ("the package is incomplete:
`tools\xhciinst\XHCIWZ64.EXE` is missing; unzip the whole package").
`INSTALL.BAT` keeps choosing the right one first, so the self-start is for
the user who finds the executable by hand and for the Add/Remove Programs
entry on x64, which names the x64 program directly (section 9).

## 5. The pages

The owner's list (2026-10-18), in order. Every page has Back, Next (or
Install, or Finish), Cancel and a Help button that opens the package's
`tools\xhciinst\readme.txt` in Notepad (`ShellExecuteA` on 98 SE and ME,
`CreateProcessA notepad.exe` where that is simpler; looked up at run time).
Every text a page shows comes from the pure half (section 11), so the host
suite renders every page once at its longest and the self-test renders them
through the built binary, as record 23 section 10 does for the console's
screens. The window's text follows the console's rules (record 23 sections
7.6 and 8.3): practical guidance, no virtual machines, no emulators, no
word on what has or has not been tested.

### 5.0 The simplified flow (draft 26; the owner's decisions of 2026-10-22)

The owner's simplification of 2026-10-22 replaces the pages of sections
5.1 to 5.7 where this section differs; everything not named here (the
engine, its texts, the refusals, the uninstall of section 9, a row "left
as it is" writing nothing, section 5.4) stands. Proposals of this
section's own, for the owner to confirm, are marked so. Not built yet:
the owner reads this design before its code.

**The pages.** Welcome -> Components -> Summary -> Progress -> (Steps) ->
Finish. The Welcome page takes in section 5.2's "What was found": below
the paragraph of section 5.1 it shows the device list (each device, what
runs it now, what will be done) and the plan's warnings (the input
warning, the test-signing note, the "newer release installed" refusal of
a group, the empty reading's advice of section 7), with "Please wait"
while `READ` and `PLAN` run; a device Windows is installing by itself is
waited for there as section 5.2 waits for it. The USB settings,
Thunderbolt settings and Stor98 settings pages (`P_USB`, `P_TBS`,
`P_S98`) and the What was found page are removed; their rows move to the
Settings dialog below. A refusal of section 5.1 still ends on the Welcome
page with Next disabled. The Steps page of section 7 (98 SE and ME) and
the uninstall's pages (section 9) are unchanged.

**The Components page**, one box a line, each with its sentence:

```
[x] xHCI98 USB 3.x driver
      [x] UAS disk driver
[ ] Thunderbolt 3 / USB4 support (experimental)
      [x] Stor98, the NVMe driver (bundled)
[x] USB storage drivers for Windows 98 SE (NUSB 3.3's)    98 SE only
[x] xHCI98 USB Tree, started at login
[ ] Replace the stored build                 Vista and 7, the trap found
Tools folder: C:\XHCI98
            [Settings...]   [Change folder...]
```

- **The USB drivers' box and the UAS box** as section 5.3 has them, the
  defaults of section 5.3.1 unchanged (ticked, but unticked on 8 and
  8.1): the UAS box a separate box indented under the USB drivers',
  ticked by default, greyed and unticked while the USB drivers are
  unticked, its own state back when they are ticked again. On 8 and 8.1
  a ticked UAS box stages `xhciuas.inf` and forces it on no disk
  (section 5.3.1 case 3a, draft 25), and a disk the user already put on
  `xhciuas` in Device Manager is kept current (owner, 2026-10-22: decided,
  case 3a as it stands).
- **The Thunderbolt box and Stor98's** (owner, 2026-10-22): the
  Thunderbolt box unticked by default as now, but ticked and locked
  whenever the USB drivers' box is unticked (section 5.3.1's dependency,
  which stands and overrides that default: on 8 and 8.1, where the USB
  drivers are unticked by default, the Thunderbolt box therefore opens
  ticked and locked, as `xhciwiz.c` does today, and Stor98 under it takes
  the table's default; ticking the USB drivers there gives the Thunderbolt
  box its own state, unticked unless the user ticked it); Stor98's box a separate box
  indented under it, ticked by default only while the Thunderbolt box is
  ticked; unticking the Thunderbolt box unticks and greys it; ticking it
  again ticks Stor98 again unless the user had unticked Stor98 while it
  was open. What Stor98 takes when the Thunderbolt box is ticked comes
  from one table, one row per system - 98 SE, ME, 2000, XP, Vista, 7, 8,
  8.1, 10 - (`instS98Default` in the pure half, host-tested), ticked on
  every system for now, 8, 8.1 and 10 included, so a change for one
  system is one line. The rule for changing it later: on 8.1 and 10 the
  default becomes unticked only if Windows' own NVMe driver (`stornvme`)
  is shown to handle a hot-plug behind this package's tunnel (roadmap
  43.24 for 8.1, 43.33 and 43.37 for 10); on Windows 8, which has no
  in-box NVMe driver (to be confirmed in 43.24's image), it stays ticked.
  Section 5.3.1's case 3b is amended so: Stor98 on 8 and 8.1 is ticked
  with the Thunderbolt box, its "Stor98 has not been tested on Windows 8
  or 8.1" warning paragraph (`INST_CW_S98_8`, the console's counterpart
  and the host check of its text) and any "untested" mark on its row
  removed, and the console's Stor98 question takes Enter for [Y] there as
  elsewhere (`inst_s98_decide`'s `is8` dropped); Stor98 on 8 and 8.1 is
  still to be read by roadmap 43.24, which revisits the default and the
  wording. The paragraph's second sentence is not about testing: "If a
  drive behind a Thunderbolt or USB4 tunnel uses Windows' own NVMe driver,
  this choice replaces it." (case 3b's replacement, which stands).
  Proposal, the owner's call: that sentence kept, on 8, 8.1 and 10 only,
  as the second sentence of Stor98's row under its box and in the
  Summary's Stor98 line, and in the console as a line of Stor98's
  description before its question (printed under `/yes` and
  `/defaults` too, as the rest of that description is), never in
  the warning box; the alternative is to drop it with the paragraph. The USB drivers' warning on 8 and 8.1 (case
  3a) stays.
- **The USB storage drivers' row on Windows 98 SE only** (owner,
  2026-10-22), in place of draft 20's "every system": on ME and 2000 to
  8.1 the row is not shown, `inst_eng_stor_state` offers it only on 98
  SE (today it shows it on every system through `storTools`, and
  `comp_layout`'s comment is brought back to "98 SE only"), and section
  5.3.1's case 1 (the pack's files copied to the tools folder) is
  withdrawn from the window; the console's `/storage` keeps case 1 on
  every system but 98 SE: the seven files copied to the tools folder, with
  case 1's warning (owner, 2026-10-22: decided). The row reads the
  download's `NUSB33\W98SE` (owner, 2026-10-22: NUSB 3.3's files a
  top-level folder of their own beside `NONFREE\`, no longer inside it;
  section 6.5.1 follows when that change lands). When NUSB 3.3 or 3.6 is
  found installed (its `Uninstall\USB20U` entry, section 6.5.3) the row
  is unticked by default (owner, 2026-10-22; built with the pack's new
  folder, not by this section) and its sentence says so: "NUSB is
  installed on this computer. It already provides the USB storage
  drivers, so you can leave this unticked." in place of the detection's
  line of section 6.5.3 for that case.
- **The USB Tree box** as now.
- **"Replace the stored build"**: the box of section 5.2 (the window's
  `/restage`) moves here, shown only where section 5.2 shows it (Vista
  and 7, the store trap found), unticked by default, `/restage`
  preselecting it, its precondition and journal as record 23 section 5.7
  has them.
- **[Settings...]** opens the Settings dialog; **[Change folder...]**
  opens a small dialog with the tools folder's edit box, Browse, and the
  checks of section 5.3 (record 23 section 9.6), OK staying in it until
  the folder passes; the page itself shows the folder chosen as one line
  of text. The 98 SE and ME line about the drivers going there too and the
  8.3 rule moves into that dialog.
- **The warnings** of section 5.3.1 on Next as now (one OK/Cancel box,
  Cancel staying on the page); on 8 and 8.1 the USB drivers ticked and a
  package without the connection manager are warned, never refused
  (cases 3a and 5); the Summary repeats them (below).

**Room for the bundled driver rows (roadmap 43.48, not designed here).**
The second and third downloads carry the usb-wdm-free drivers and the
usb-wdm-nonfree pack in folders beside the installer (NUSB 3.3's 98 SE
storage files are a folder of their own beside them, `NUSB33\W98SE`,
owner 2026-10-22, read by the storage row above and in neither pack).
The owner, 2026-10-22, for 43.48: the installer copies those folders and
installs neither. Each pack has a row, unticked by default, that copies
it, and one sub-box under it, also unticked: "Let Windows use these
automatically when a device is plugged in", which stages the pack's
INFs (the INF folder on 98 SE and ME, the driver store on 2000 and
later) and forces nothing onto a device. The non-free row's sentence
and its sub-box carry a firmer warning ("not tested by this project;
the vendors' drivers as released"): the pack's vendors' own setups are
the user's to run, this package carrying the pack and not being its
installer. Three rules bound the staging: only an INF-based driver is
staged, a driver that comes with a setup program being copied only,
with "run its own setup" in the text; and where two of a pack's drivers
match the same hardware id (FTDI, FT2232D and CH341; the two Xbox 360
controller drivers), one only is staged, chosen by a written list
shipped with the pack (its manifest), never guessed at run time. The
copies go to one fixed folder under the install directory (for example
`<tools folder>\Drivers\...`), which the readme and the Finish page
name for the Found New Hardware wizard; the uninstall removes the
copied folders and whatever was staged. Each row is shown only when its
folder is there and holds what its record names, and the rows carry the
free drivers' beta notice and the two project addresses
(`https://github.com/yeokm1/usb-wdm-free`,
`https://github.com/yeokm1/usb-wdm-nonfree`) beside them and on the
Finish page. This design leaves that room and settles nothing more: the
page's rows are built from a row table (the box, its parent box, its
sentence, shown or not, ticked or not, greyed with its parent), not from
fixed positions, so a row with one sub-box, as the UAS and Stor98 boxes
are, is two entries more; the rows above the buttons may exceed a 640
by 480 page once 43.48's are added, and then the list scrolls in a
frame (proposal) with the buttons and the folder line kept below it;
one line of text under the list is kept for the addresses; the Finish
page keeps a line for the copied folder's path; the Settings dialog
gains no tab for them; the Summary lists them as it lists every
component, the copy and the staging as separate lines; the same warning
box takes their warnings; the uninstall's order (section 9.3) gains a
step for the copied folders and the staged INFs. Which drivers, one row
per pack or per driver, their systems, the folder's name, the
manifest's form and the staging's details are 43.48's.

**The Settings dialog.** One dialog, three tabs - USB, Thunderbolt,
Stor98 - each tab shown only while its component is ticked (proposal: a
tab of an unticked component shown greyed instead), each with its main
rows and an "Advanced..." button that opens that tab's advanced rows,
the rows built and read as sections 5.4 to 5.6 build and read them (the
combo boxes, "now", the machine-wide location, the greyed row of another
system, the mixed state, each row's "Asked when"; a row left as it is
writes nothing):

| Tab | Main | Advanced |
|---|---|---|
| USB | `XhciIntelPortSwitch` (live only on a controller `XhciPswGate` admits; greyed otherwise, as now), `XhciSilentInstall` (98 SE and ME, as its "Asked when" has it; default off, no preselection, section 6.5.12) | `XhciInterruptMode`, `XhciForceBulkOnly`, `XhciPollFineTimer`, `XhciSelectiveSuspend`, `XhciFastPollFsLs` |
| Thunderbolt | `XhciCmMode`, `XhciCmApprove`, `XhciCmSafeEject` | the ten of section 5.5's Advanced, `XhciCmNoIrqStart` (98 SE and ME) and the mark service's `Restart` (2000 to 8.1, where the mark service runs) |
| Stor98 | `StorWriteThrough`, `StorPollMode` | the six of section 5.6's Advanced |

`XhciCmNoIrqStart` and `Restart` are designed in section 5.5 and not in
the engine's value table yet (`instValues`); this design builds them.
`XhciCmNoIrqStart` is an ordinary row: `REG_DWORD`, default 1, 98 SE and
ME only, read machine-wide first and then per driver key, written
machine-wide when changed, and removed by the uninstall's machine-wide
loop as the other `XhciCm*` values are (record 28 section 7, N5).
**`Restart` is not**: it lives in the mark service's own key,
`HKLM\System\CurrentControlSet\Services\xhcimark\Parameters` (record 21
section 7.4; record 23 decision D16), not under `Services\USB`, so its
row carries a location of its own in the value table (a flag the
writer, the reader of "now" and the uninstall's loop test). `REG_DWORD`,
default 1 (0: the service keeps writing its safe-removal marks but
never restarts a unit's stack, so a mark takes effect at that stack's
next rebuild), on the systems the mark service runs on, 2000 to 7 and 8 and 8.1
(`inst_mk_system`; record 23 decision D19), shown greyed elsewhere as
another system's row, and shown open only when the mark service is in
the run or already registered (the Thunderbolt component ticked, record
23 D16's conditions); "now" is read from that key, a missing value
reading as the default. The service reads `Restart` once, at its start
(`xhcimark.c`, `mk_prepare` and `mk_params`), so a change is written in
the mark service's step of the run (`mk_install`) after the service is
registered or its configuration updated and before it is started: the
`Parameters` key created and the chosen value written there, then the
start. On an update that finds the service already current and running,
a changed value is written and the service stopped and started again
(`mk_stop`, then the start), so the value is in force when the run ends.
A value that cannot be written is said ("Restart could not be set:
<error>"), the service is still started with what the key holds, and
the run counts it failed (exit 2), as a service that does not start is;
a service that cannot be registered writes nothing. And the uninstall's machine-wide
loop over `Services\USB` skips it, so no `Restart` value is ever written
to or deleted from `Services\USB`: the value goes with the service's key
when the uninstall deletes the service (record 23 section 9). Record 23
D16's "the Advanced dialog does not yet offer the service's `Restart`"
is amended when this is built.
Each tab's rows are laid out by roadmap task 43.10's font- and
size-driven layout (`xhciinst\wiz_lay.c`, branch `p43-10`: the combo's
closed and open widths, a description box of at least six lines, at most
three rows a part, `test_wizlay.c`), reused per tab rather than written
again: `3.2.0.0` carries it (43.10, this record's draft 21, sections
5.4, "The layout", 5.6, 11 and 15), and the code of this section is
built on `3.2.0.0` with it. **When the dialog opens**, the Components
page first hands its boxes to the engine as Next does today
(`comp_leave`'s choices, then `replan`), so the tabs and their rows are
those of the components ticked at that moment (a Thunderbolt box ticked
a moment before opens the Thunderbolt tab); a value chosen at an earlier
opening is kept across that replan for every row still in the set, and
a row dropped by an untick keeps its choice unwritten until its
component is ticked again. OK keeps the dialog's choices for the run
(nothing is written before Install); Cancel restores the choices as they
stood when this opening began, those of earlier openings kept. Next on
the Components page replans again as now, so a box changed after the
dialog closed is still taken. The new install note of section 5.5 (`XhciCmMode` and
`XhciCmApprove` with the INF's defaults until the restart) stands on the
Thunderbolt tab.

**The console** asks the main rows only: `XhciInterruptMode` and
`XhciForceBulkOnly` are no longer asked (they are Advanced now); `/set`
still sets them, as it sets every value.

**The Summary** repeats, after the headline, each component warning of
section 5.3.1 that applies, each warning's first sentence. On 98 SE and
ME the Stor98 file update's checkbox ("Please disconnect all external
NVMe drives before proceeding", section 6.3) becomes a message box, OK
and Cancel, shown on Install only when Stor98's file would be replaced
(`inst_eng_s98_file_update`); Cancel stays on the Summary and nothing is
written.

**The silent install covers `xhciuas.inf` too** (owner, 2026-10-22, from
the 43.5 legs: a UAS disk's first plug on 98 SE raises the wizard and an
"Insert Disk" prompt for `C:\XHCI98`, and a replug at another port the
wizard again): with `XhciSilentInstall` on, a UAS device is to install
with no wizard and no prompt. Two things stand in the way and are this
task's design points, read first: the driver's silent decision bars a
device it presents as UAS (design record 13 section 6.11,
`XHCI_SILENT_BARRED`, decision O2), which a change of the bus lifts only
with record 13 amended; and the "Insert Disk" prompt for `C:\XHCI98`,
which is the observation. Its cause is not read: the hypothesis is that
Windows' device installer at that plug takes `xhciuas.inf` from a place
other than the install directory (an INF copy whose source path no
longer holds `xhciuas.sys`), and the legs read what establishes or
refutes it (the bound devnode's `InfPath`, the `OEM Name` and source
path records, the prompt's path and the file it asks for) before any
design rests on it. The design of that part is a separate, later
step of this task (proposal), after the Components page and the Settings
dialog.

**Host vectors**: the Stor98 default table on every system; the
Thunderbolt and Stor98 boxes' rule (ticked with the parent, unticked and
greyed without it, the user's own untick kept); the tab and row sets per
system and component; the console asking the main rows only; the
storage row on 98 SE alone, and its NUSB-found sentence; the Summary's
repeated warnings; the Thunderbolt box locked on while the USB drivers
are unticked (8 and 8.1 by default); `Restart`'s key (written to and
read from `Services\xhcimark\Parameters` alone, never to or from
`Services\USB`, and skipped by the uninstall's machine-wide loop); the
choices kept across a Settings opening's replan and restored by its
Cancel. **Legs**:
the page at 640 by 480 on 98 SE, ME, XP and 7; the Settings dialog's
three tabs; Stor98 on 8 with 43.24's VMs.

### 5.1 Welcome

The package's name and version, the system found (Windows 98 SE, ME, 2000,
XP, XP x64, Vista, 7, with the architecture), one paragraph saying what the
installer will do (install or update the xHCI98 USB 3.x drivers, put the
xHCI98 tools in a folder, and ask for a restart at the end), the log's
path, and "Please wait" while `READ` and `PLAN` run. On Windows 8 and 8.1
(draft 8) the paragraph says instead that the installer puts the xHCI98
Thunderbolt 3 / USB4 connection manager and the xHCI98 tools on this
computer, that Windows' own USB 3 driver keeps running the USB
controllers, and that it asks for a restart at the end; the system line
names 8 or 8.1 by the version `RtlGetVersion` reads (record 23 section
4.3). A refusal of record 23
section 4 (the ZIP folder, a mixed architecture, a wrong flavour, a stale
`.sys`, a wrong Windows, rights) ends here: the page shows the refusal's
text and Next is disabled; Cancel closes with exit 1. Under `/uninstall`
(section 9) the welcome page is the uninstall's, with the warning of record
23 section 9.1 on it.

### 5.2 What was found

The plan of record 23 section 5.4 (NT) or 6.3 (98 SE and ME) as a list:
each device of the package with its name, what runs it now (this release,
an older one, nothing, another driver) and what will be done (install,
update, nothing, left). Below it the warnings the console prints before its
confirmation: the input warning when a controller changes (record 23
section 5.8), the test-signing note on Vista x64 and 7 x64 (5.10), the
"newer release installed" refusal of a group (5.4), the Vista and 7 store
trap with its `/restage` way out (5.7; the window offers a check box
"Replace the stored build" in place of the switch, with the same
precondition and journal), and on 98 SE and ME the entry "recorded, but not
running now" (6.3). A device Windows is installing by itself is shown as
such and the Next button waits for it as the console's settle does, with
"answer the window Windows opened".

### 5.3 The components

Check boxes, one a line, each with one sentence:

| Component | Ticked | Can be unticked | What it is |
|---|---|---|---|
| xHCI98 USB 3.x host controller driver (`xhci98.sys`; until 2026-10-19 with `xhciuas.sys` in the same box) | yes, but on Windows 8 and 8.1 (draft 8) | when the package carries the connection manager (record 23 section 4.5; `/cmonly` preselects it unticked): until draft 20 on 2000 to 7 alone, shown ticked and disabled on 98 SE and ME and unticked and disabled on 8 and 8.1; from draft 20 on every system, with section 5.3.1's warning where it does not apply (unticked on 98 SE and ME, ticked on 8 and 8.1) | the driver for the controller, its hubs and devices, and Bulk-Only disks (UAS disks: the box under it); on 8 and 8.1 the sentence says Windows' own USB 3 driver runs the controllers there; unticked on 2000 to 7, the sentence under the box says the run leaves an installed `xhci98.sys` as it is and that the uninstall removes the whole package |
| UAS disk driver (`xhciuas.sys`) (owner, 2026-10-19) | yes | yes, while the USB drivers are ticked: a child of their box, indented under it, open only while it is ticked and unticked and greyed with it (the connection manager alone); `/nouas` preselects it unticked | "untick only if you use another UAS driver; Bulk-Only disks are not affected, and `XhciForceBulkOnly` is the other way to avoid UAS". Unticked, `xhciuas.inf` is neither staged nor installed (on 98 SE and ME not copied to `DevicePath` either), and an `xhciuas` already installed stays as it is: unticking is "not now", not an uninstall, as for every component; `XhciForceBulkOnly` is still asked, being `xhci98.sys`'s (`inst_uas_decide`) |
| Thunderbolt 3 / USB4 support (experimental) (`xhcicm.sys`) | no, whether or not a controller is found (draft 16; owner, 2026-10-19, replacing "ticked when a controller is found"): the user ticks it on purpose; `/thunderbolt` preselects it; ticked when it is locked on (the USB drivers unticked, and 8 and 8.1) | yes | the connection manager; the note under the box says whether a controller was found - "A Thunderbolt 3 / USB4 controller was found: <name>." (the name cut to 36 characters, `inst_tb_found_text`) with the sentence that a device plugged into its port can reach the computer's memory, see `XhciCmApprove`; or "No Thunderbolt 3 / USB4 controller was found (some laptops show it only with a device plugged in or a BIOS setting). Tick it only if this computer has such a port." Unticked on an update leaves an installed `xhcicm` as it is (record 23 section 8.5's N) |
| Stor98, the NVMe driver (bundled) | with the Thunderbolt box (draft 16; owner, 2026-10-19, replacing "never unasked" of 2026-10-18): ticking the Thunderbolt box ticks it, unticking it unticks it; `/nostor98` preselects it unticked; on 8 and 8.1 (draft 20) shown, and never ticked but by the user (section 5.3.1) | yes | a child of the Thunderbolt 3 / USB4 box, indented under it (owner, 2026-10-19): open only while that box is ticked (also when the USB drivers' box is unticked and the Thunderbolt box is locked on), unticked and greyed when it is unticked or the package lacks the support (both shown greyed then; `inst_s98_box_open`); shown only when the flavour directory carries it; the sentence "For NVMe drives behind a Thunderbolt or USB4 tunnel; the installer selects tunnel drives only." (section 6.3; draft 12: it no longer offers an internal drive), and the engine keeps Stor98 out of a run without the Thunderbolt support by the same rule (`s98_set`) |
| USB storage drivers for Windows 98 SE (NUSB 3.3's) (task 43.4, draft 17; owner, 2026-10-20) | an earlier run's recorded choice; otherwise when none of the seven is found (section 6.5.3); `/storage` and `/nostorage` preselect it | yes, whatever was found; greyed when the download lacks the pack or its files are not this release's (section 6.5.1) | shown on Windows 98 SE alone until draft 20, and from draft 20 on every system, unticked by default but on 98 SE, ticked elsewhere with section 5.3.1's warning and its files then only copied to the tools folder; a box of its own under the UAS box; the sentence and the detection's line of section 6.5.2 and 6.5.3; ticked it never copies over a file already present (section 6.5.4) |
| xHCI98 USB Tree, started at login | yes | yes (the `/notray` of record 23 section 9.5) | the tray tool |
| The tools folder | - | - | an edit box with Browse, `C:\XHCI98` by default or the directory an earlier install recorded (record 23 section 9.6's default and checks; the failure text under the box, in the place of the 98 SE and ME line while it shows, so the page with the UAS box still fits a 640 by 480 screen; Next checks it and stays on the page until it passes); on 98 SE and ME the line that the package's drivers go there too and the 8.3 rule |

The component page is not shown under `/uninstall`. Unticking Thunderbolt
support does what record 23 section 8.5's N does; unticking the tray tool
what `/notray` does. Unticking the USB drivers does what `/cmonly` does:
the Thunderbolt box is then ticked and disabled (it is all the run
installs), the USB settings page is not shown, and the plan is made again;
ticking them again gives the Thunderbolt box back its own state.

#### 5.3.1 Every box on every system (task 43.43; draft 20)

The owner, 2026-10-21: "for the installer first page, all options should
be available to all OS. Defaults can remain", and the same day, in place
of "greyed with a reason": every box shown and tickable on every system,
none refused; a box ticked where it does not apply gets a warning that
says so, and the run goes on. Until draft 20 the page gated four boxes by
the system: the USB storage drivers' row shown on 98 SE alone
(section 6.5.2); the USB drivers' box locked ticked on 98 SE and ME and
locked unticked on 8 and 8.1 (`inst_usb_decide`, record 23 section 4.5);
Stor98's box hidden on 8 and 8.1 (record 23 decision D18); and the
Thunderbolt and UAS boxes, which follow the USB drivers' box.

**What stays.** The defaults (section 5.3's table, unchanged on every
system), and the boxes' dependencies on one another, which are not a
system's gate: the UAS box a child of the USB drivers' box, the
Thunderbolt box ticked and locked while the USB drivers are unticked (it
is then all the run installs), Stor98's box a child of the Thunderbolt
box, and a box greyed because the package or the download lacks its files
(the Thunderbolt support, Stor98, the storage pack, section 6.5.1; the
USB drivers' box can be unticked only where the package carries the
connection manager, since without it a run would install nothing - but
on 8 and 8.1, where unticked is their default, case 5 below). The
tray box follows the package (`XHCITR` for this architecture), as
before. A box shown greyed for a missing file is not one the system
gates, and stays as it is (proposal for the owner).

**The cases** (the owner's, 2026-10-21; the pure half's
`inst_comp_warnings`, host-tested on every system and every combination
of the four boxes, a bit per warning, and `inst_comp_warn_text`, the
words):

| Case | When | What the run does | The warning's words (proposal for the owner) |
|---|---|---|---|
| 1 | the USB storage drivers ticked on ME, or on 2000 to 8.1 | the pack's seven files copied to `<the install directory>\NUSB33\` and nothing else: nothing in `%windir%`, the INF directory, `INF\OTHER` or `DevicePath`, no device job, no `Storage` record but the copies' own (below). A file of the seven's names already in that folder - the same bytes, other bytes or unreadable - is left as it is and never recorded; each absent one is written as `<name>.TMP` with `CREATE_NEW`, compared byte for byte with the download's, its `Tools` value written, then moved to its name with `MoveFileA`, which never replaces; a step that fails deletes the `.TMP` and the value and leaves the rest copied. The run says how many of the seven are in the folder and names any left out (a note, not a step); a later ticked run copies only those still absent. **Acceptance condition (ME):** Windows ME must not find the INFs there - the install directory is on ME's `DevicePath`, and whether ME's device search reads its subfolders is unread; if case 1's ME leg shows ME offering the pack's INF from `NUSB33` (or binding a stick from it), this placement is withdrawn before the code is released and the owner chooses another place or form for ME | "The USB storage drivers (NUSB 3.3's) are for Windows 98 SE only. On this Windows they are not installed: their seven files are copied to <dir>\NUSB33 for you to use by hand, and nothing installs them." |
| 2 | the USB drivers unticked on 98 SE or ME | the connection manager installed alone, as on 2000 to 7 (record 23 section 4.5, amended): `xhci98.inf` and `xhciuas.inf` out of the run, the Thunderbolt support in it; on these two systems through the helper (record 27 section 6 step 5), and the local copy on `DevicePath` made without `xhci98.inf`, `xhci98.sys`, `xhciuas.inf` and `xhciuas.sys`, as `/nouas` already leaves the UAS pair out of it, so no hardware wizard can offer them; an `xhci98.sys` already installed stays as it is (record 23 section 4.5) | "Without the xHCI98 USB drivers, Windows 98 SE and ME have no driver of their own for a USB 3 controller: an xHCI controller with no other driver installed, a dock's included, stays without one, and the USB devices on it do not work. Only the Thunderbolt 3 / USB4 connection manager is installed; xHCI98 USB drivers already installed stay as they are. This has not been tested on Windows 98 SE or ME." |
| 3a | the USB drivers ticked on 8 or 8.1 | `xhci98.sys` installed through the NT route of record 23 section 5 (`Xhci.Dev6`), taking roadmap task 43.29's row, and, with its box, `xhciuas.inf` staged but forced onto no device (owner, 2026-10-22, draft 25; record 13 section 6.13.9 on the `p43-29` branch: the bus answers Windows 8's own client contract, so Windows' own `uaspstor.sys` stays the default for a UAS disk): a UAS disk with no driver is left for Windows to bind (`INST_ACT_WINDOWS`, "left for Windows' own UAS driver", `inst_uas_nt6_action`), one on another driver stays foreign, and one already on `xhciuas` (the user's own pick in Device Manager) is kept current as on 2000 to 7; the staged INF is what Device Manager then offers; and, the one narrow exception to record 23 decision D3 (amended, revision 26), a controller whose service is Windows' own `USBXHCI` is not "another driver" here: with the box ticked it is planned as an install and installed with `INSTALLFLAG_FORCE` (Windows' signed in-box driver otherwise outranks this package's INF), as the user chose; every other vendor's driver on a controller stays foreign and left, as before; such a controller is this package's from then on, and the uninstall removes its devnode and rescans (record 23 section 9.2), after which Windows is expected to bind `usbxhci.sys` again (43.29's "the controller given back", unread until 43.31) | "The xHCI98 USB drivers have not been tested on Windows 8 or 8.1. They replace Windows' own USB 3 driver on every xHCI controller, the one your keyboard and mouse may use included. The uninstall is meant to give the controllers back to Windows' own driver; that has not been tested either. There the UAS disk driver, when ticked, is only staged: Windows' own UAS driver stays the default, and xhciuas.sys can be chosen for a disk in Device Manager." |
| 3b | Stor98 ticked on 8 or 8.1 | the bundled Stor98 on tunnel drives as on 2000 to 7 (section 6.3), taking roadmap task 43.23's installer row; the same narrow exception as 3a for Windows' own NVMe driver: on 8.1 a tunnel drive whose service is `stornvme` is installed over with `INSTALLFLAG_FORCE` when the box is ticked, and becomes Stor98's by section 6.3's record (its first binding by this installer); the replacement goes through section 6.3's reconnection rules as an update of a drive Stor98 runs does - the tunnel may be torn down and brought back when the drive's driver changes (the connection manager's child filter sees the removal whatever storage driver ran), so a drive gone after its job is "installed, awaiting reconnection", its read-back deferred to the reconnection (`s98_reconnect_nt`, which then admits a baseline of `stornvme` beside Stor98's own), and its ownership recorded when it is read back running Stor98; because that read-back may come only in a later run (a skipped or timed-out reconnection wait, a run cut short), the drive's id is written under the `Stor98` record's `Pending` subkey with the data `attempt` before its job starts, rewritten `done` once the install call returns success for it, and deleted when the call fails or is cancelled; every later install run and the uninstall resolve each pending id first: a `done` drive read back running Stor98 moves to the record's devices as this installer's (its first binding, evidenced by the call's success); a `done` drive not present stays pending, and the uninstall takes it when its `Enum` entry names Stor98; an `attempt` drive (a run cut short during the call, its outcome unknown) is never taken: read back running Stor98, its value is kept as `unknown`, a durable exclusion that the uninstall's adoption of tunnel drives (section 6.3) honours too, so that Stor98 stays as anyone's; the `attempt` value must be written before the call is made, and a write that fails leaves the drive's group unchanged; a `done` value moves to the devices only once the devices' value is written; a drive read back running another driver, or gone with no `Enum` entry, has its pending value deleted; an internal drive, and a drive another vendor's driver runs, are left as before; the uninstall removes Stor98 from it and rescans, Windows then expected to bind `stornvme` again, unread until 43.24, whose legs gain the `stornvme` replacement, its reconnection, a replug, and the wait skipped, timed out and cut short with the next run and the uninstall resolving the pending drive | "Stor98 has not been tested on Windows 8 or 8.1. If a drive behind a Thunderbolt or USB4 tunnel uses Windows' own NVMe driver, this choice replaces it." |
| 5 | a package without the connection manager on 8 or 8.1 (owner, 2026-10-22, in place of draft 20's refusal) | the run goes on with what the package has: the USB drivers' box, tickable and unticked by default (case 3a's warning when ticked), and the tools; Stor98, a child of the Thunderbolt support, is not in the run; with nothing ticked the run installs the tools alone | "This package carries no Thunderbolt 3 / USB4 driver. On Windows 8 and 8.1 it installs only what is ticked: the xHCI98 tools, and the xHCI98 USB drivers if their box is ticked." |

Case 3's defaults: on 8 and 8.1 both boxes are shown unticked, as the
USB drivers' box was; Stor98's box, which elsewhere is ticked with the
Thunderbolt box (draft 16), is unticked there whatever the Thunderbolt
box does, and only a tick of the user's own takes it. Each 8 and 8.1 row
says "untested" until roadmap tasks 43.31 (the USB drivers) and 43.24
(Stor98 with the connection manager) pass; record 23 decision D18 is
amended to "admitted for the connection manager, with the USB drivers
and Stor98 opt-in and untested".

Case 3a's and 3b's last sentences state what 43.29 and 43.23 ask of the
uninstall (the controller given back to `usbxhci.sys`, a drive to
`stornvme`) as intended and untested, which the legs read.

**The window** (case 4, the owner's): on Next from the Components page,
after the folder's check, when `inst_comp_warnings` is not zero for the
boxes as ticked, one message box (`MessageBoxA`, `MB_OKCANCEL |
MB_ICONWARNING`, the window's title) shows every warning that applies,
one paragraph each in the table's order, then "OK goes on; Cancel stays
on this page to change the choice." OK goes on to the next page; Cancel
stays on the Components page with the boxes as they were. It is shown
once per Next: a Back to the page and a Next again with the same boxes
shows it again (a proposal; the alternative is once per run). The
Summary repeats each warning's first sentence in its list (proposal).

**The console** prints the same words, each under a line "WARNING:",
before its go-ahead question (record 23 section 7.4), and asks nothing
more: the go-ahead is the answer. Under `/yes`, `/defaults` and `/list`
(the unattended runs; the window takes none of them, `inst_window_refusal`)
the words are printed and logged and nothing is asked, and the run goes
on (proposal; the owner's "logged and printed, never shown"). The
console's choices:

- case 1: `/storage` accepted on every system (it was refused off 98 SE),
  ticking the row; on ME and 2000 to 8.1 the row's question is not asked
  and its default is unticked, so only `/storage` takes it there;
- case 2: `/cmonly` accepted on 98 SE and ME (it was refused there), with
  the warning; a package without the connection manager still refuses
  `/cmonly` as on 2000 to 7;
- case 3a: `/usb` (new, proposal) puts the USB drivers in the run on 8
  and 8.1, where they are otherwise left out; elsewhere it changes
  nothing (they are in the run unless `/cmonly`); `/usb` with `/cmonly`
  is refused as a contradiction;
- case 3b: the Stor98 question is asked on 8 and 8.1 with Enter for N
  (Y elsewhere, draft 16), `/stor98` ticks it and `/yes` takes the N.

**What the uninstall removes** (proposals for the owner):

- case 1: the seven in `<the install directory>\NUSB33\`, each by the
  record `HKLM\Software\xhci98\Storage\Tools` (one value per copy this
  installer made, its path and SHA-256, written before its move, as
  `Files` is; a file it found there is never recorded), deleted when its
  bytes are still the recorded ones, then the folder if it is empty; a
  changed file is kept, said, and its value goes. This runs on both
  uninstall routes (NT's record 23 section 9.2 and 98 SE's and ME's
  section 9.3), before the installer's own files and the records go; a
  deletion that fails keeps its value, sets the restart and steps flags
  and leaves the uninstall incomplete with its Add/Remove Programs entry,
  as section 6.5.8 step 4 does; the `Tools` subkey is deleted with the
  `Storage` key by the run that finishes (`stor_forget` gains it, so a
  non-recursive key deletion on NT does not fail on it). On ME and 2000
  to 8.1 nothing else of section 6.5.8 runs (no `Files`, `Infs`,
  `Placed` or `Devices` can exist there). A later run with the row
  unticked leaves the copies (unticking is "not now"); a later run with
  it ticked copies only what is absent.
- case 2: what the connection manager's install placed, as record 23
  section 9 and record 26 section 9.3 remove it on 98 SE and ME (its
  devnodes through the helper, its INF copies, its file, the mark
  nothing: the mark service is NT's), and the Add/Remove Programs entry,
  whose name follows what the machine has (section 9.1); nothing of the
  USB drivers, which the run did not install. An `xhci98.sys` already
  installed before the run is the whole package's and goes with it, as
  on 2000 to 7.
- case 3: as on Vista and 7 (section 9.3, record 23 section 9.2): the
  controllers' devnodes removed and rescanned, so Windows binds its own
  driver again (43.29's "the controller given back", unread); Stor98 by
  its record (section 6.3).

**The 640 by 480 page.** With every row shown on every system the page
carries the storage row everywhere, so 98 SE's layout (section 6.5.2,
`comp_layout`) becomes the only one: the USB drivers' sentence under its
box gives way to the storage row as on 98 SE today, and the host suite
renders the page at its longest on each system.

**The texts under the boxes.** The storage row's line under its sentence
on ME and 2000 to 8.1 is "For Windows 98 SE only: ticked here, the files
are copied to the tools folder, nothing installs them" (in place of the
detection's line, which only 98 SE reads). The USB drivers' sentence is
the one the storage row's layout hides on every system now (the 98 SE
layout above), so its 8 and 8.1 form ("Windows' own USB 3 driver runs
the controllers here; ticked, this replaces it (untested here)", in
place of "Not offered on Windows 8 and 8.1") is kept for the template
alone, and the untested state of the USB drivers and Stor98 there is
said by the warning, which their ticks raise.

**The legs** (roadmap 43.43): the page at 640 by 480 on 98 SE with every
row shown; case 1 on ME and XP (the folder's seven, nothing in `%windir%\INF`,
an ordinary stick on ME still bound by ME's own `USBSTOR.INF` and the
driver key's `InfPath` and version read, whether ME's search reaches
`<dir>\NUSB33` read; the uninstall); case 2 on 98 SE and ME (the
connection manager alone with the helper, the emulator's host interface
bound, the warning, the uninstall); case 3 on 8 with 43.24's VMs; the
console's warnings and `/yes` on each.

### 5.4 The USB settings

The values of record 23 section 8.1 whose "Asked when" holds for this
machine (`XhciIntelPortSwitch` on a controller `XhciPswGate` admits,
`XhciInterruptMode`, `XhciForceBulkOnly`, and `XhciSilentInstall` on 98 SE
and ME), one row each: the value's name, one sentence of meaning, a combo
box with every allowed value and its meaning, the default marked, and the
value now where one is set. The caution of 8.3 stands under its row. A
value the user changes is written as 8.4 says; a value left as it is is not
written (the INF's don't-overwrite flag stays the rule).

**A row of another system is shown greyed** (owner, 2026-10-19: "grey out
if system does not support that option"): where a value's controller is
here and its system is not - `XhciSilentInstall` on 2000 to 7,
`XhciPollFineTimer` on 98 SE and ME, `XhciCmBridgeNumbering` on 2000 to 7,
and `XhciIntelPortSwitch` on a controller `XhciPswGate` does not admit -
the row is shown where its own system shows it (on the page or behind
Advanced), its label and its choice disabled, its "now" replaced by the
systems it is for ("(Windows 98 SE and ME only)", "(Windows 2000 to 7
only)", "(Intel 7-9 series, C610 only)"), and it never writes: the page
does not take it and the engine refuses it (`inst_value_greyed`,
`inst_value_only`, host-tested). A value with no controller for it at all
stays off the pages. Rows are 38 dialog units apart with an etched line
between them, so a label is not read as the title of the choice above it
(owner, 2026-10-19).

**Where it is written** (task 42-A.17, the owner's decision of 2026-10-18,
"The machine-wide registry key"): the settings pages write the machine-wide
value under `HKLM\System\CurrentControlSet\Services\USB`, which the drivers
read first in `3.1.0.0`, and show both locations where they differ: "set
machine-wide: 1; the controller's own value: 0 (the machine-wide one is
used)". A per-controller value is left as it is. The console tool's
questions do the same from `3.1.0.0`, so the two front ends agree; the
engine's write is one routine with the location as a parameter, and record
23 section 8.4 is revised to say so (section 12).

**An update keeps a value the user set** (owner, 2026-10-18): the combo box
is preselected with the value in force (machine-wide first, else the
controller's, else the default), and Next with nothing touched writes
nothing. That is record 23's "Enter keeps" as a page. Where two
controllers carry different values and no machine-wide value is set, the
row shows a mixed state, "each controller keeps its own", with each
controller's value listed under it, preselected and writing nothing;
choosing a value replaces the mixed state and writes the machine-wide
value, which then rules both (the console's "Enter for [each keeps its
own]", record 23 section 8.3, as a page).

**"Advanced..."** opens a dialog with the values record 23 never asks
(`XhciPollFineTimer`, `XhciSelectiveSuspend`, `XhciFastPollFsLs` (owner,
2026-10-19: 0, 2, 3 and 4, the gap at 1 kept), and from the owner's
decision the rest the console sets only with `/set`), each with its
default, its range or list, the systems it applies to, and its warning
(section 11's table carries all three), every one preselected with the
value in force and written only when changed. The dialog says at its top
that these are for the release notes' advice and a tester's use. The
fourteen Linux quirk values (record 20), the log values and the poll
periods stay out, as record 23 section 8.1 keeps them out: a value nobody
should set from an installer is not offered by one.

### 5.5 The Thunderbolt settings

Shown when Thunderbolt 3 / USB4 support is ticked and `xhcicm.inf` matches
a present device, or the box is ticked with none present (the staged-only
case of record 23 section 8.5, where the page says the values take effect
when the controller appears and are written to the machine-wide key, which
needs no device). The rows: `XhciCmMode`, `XhciCmApprove`, `XhciCmSafeEject`
as 8.1 asks them, with 8.4's new-install note for `XhciCmMode` and
`XhciCmApprove` (the connection manager runs with the INF's defaults until
the restart; disconnect the dock until then when the answer is 0).
"Advanced...": `XhciCmReenum`, `XhciCmUsb3Tunnel`, `XhciCmLaneBonding`,
`XhciCmPreOsCleanup`, and from the release notes' table `XhciCmBridgeNumbering`
(98 SE and ME only), `XhciCmPollIdleMs`, `XhciCmCmdTimeoutMs`,
`XhciCmApproveTimeoutMs`, `XhciCmLogVerbosity` and `XhciCmLogDebugView`,
each with its default, range and warning (`XhciCmBridgeNumbering` shown
greyed on 2000 to 7, as section 5.4 says); design record 28's
`XhciCmNoIrqStart` (98 SE and ME only) and the mark service's `Restart`
(2000 to 7 only; record 21 section 7.4, revision 3) join these rows with
their systems, written machine-wide when changed. `XhciCmTunnelBridge` (a string
for a machine whose ports the driver does not find) is not offered: it is
a diagnostic the release notes describe for a hand edit.

### 5.6 Stor98's settings

Shown when Stor98 is ticked. Stor98's settings (its `readme.txt`,
"Settings") are optional, its INFs write none, and an update keeps a
value the user set, so the page reads what is set now and shows each row
either as "not set (the built-in default is <value>)" or as the explicit
value found, preselected; a row left alone writes nothing, and choosing
the default's value where none was set writes it explicitly, which the
row says. The rows asked: `StorWriteThrough` (`auto`, `off`, `cacheoff`,
`fua`, each with Stor98's one-line meaning; a string on every system) and
`StorPollMode` (0 or 1). "Advanced...": `StorMaxTransferKB`,
`StorPollBusyUs`, `StorPollIdleMs`, `StorPollSpinUs`, and the two that
readme marks not settable on 98 SE and ME, `gone` and `idle`, each with
Stor98's default, unit and range; on 98 SE and ME those two are shown
greyed, "(Windows 2000 to 7 only)", and never written
(`inst_s98_greyed`, owner, 2026-10-19). Where they are
written is Stor98's rule, not this package's, and the table carries it
per value: on 98 SE and ME a value of the row's name under
`HKLM\System\CurrentControlSet\Services\stor98`, a `REG_SZ` for
`StorWriteThrough` and a `REG_DWORD` for the numeric ones (the readme
admits a decimal string too; the installer reads either and writes the
DWORD); on 2000 to 7 the one `DriverParameter` string (`REG_SZ`) under
`Services\stor98\Parameters\Device`, lower-case `key=value` pairs
separated by semicolons (`writethrough=`, `pollmode=`, `maxtransferkb=`,
`pollbusyus=`, `pollidlems=`, `pollspinus=`, `gone=`, `idle=`), which the
engine reads, edits one key at a time and writes back whole, keeping a
pair it does not know and the order it found. Its rows are 48 dialog units apart with an etched line between them, as
section 5.4's are (owner, 2026-10-19). The page names Stor98's
readme as the authority, from which the table's meanings, defaults,
ranges, types and key names are copied at the version the package bundles
(since `3.1.0.0` the package's own version: Stor98 is built from
`src\stor98` with the drivers, roadmap-usb4 task 42-A.30), and the host
suite checks every row against that readme's table
(`test\stor98-settings.txt`; `make-release.ps1` holds
`src\stor98\docs\package-readme.txt`'s table to it), so a Stor98 change
that renames or retypes a value fails the build rather than the user. Every Stor98 value the installer writes is
recorded by name under the `Stor98` key of section 6.3, for the uninstall.
The page is not shown when Stor98 is unticked.

### 5.7 The summary

What will happen, in the console's confirmation words (record 23 section
7.4: install and stage, write settings only, or check only), then the
list. The headline is decided from what the list under it shows
(`inst_summary_head`, matrix N1, 2026-10-19): a device run gives the
install's line; otherwise a setting the pages chose (the drivers', only
for a component in the run, or Stor98's) gives "Write the changed
settings", any other listed change (the Thunderbolt support added,
Stor98 ticked, the tools installed for the first time or moved, the USB
Tree's start at login turned on or off) "Make the changes listed
below", both together say both, and "Every device is current and no
setting changes" stands only when nothing changes. Draft 16 (owner,
2026-10-19): when the run will write `XhciCmMode` 0, the list opens with
"*** Thunderbolt 3 and USB4 will be turned off (XhciCmMode 0). ***"
(`inst_cm_mode_off`; the console prints it too, record 23 section 8.3). The list: each device and its action, each component, each setting that will
be written with its location, the tools folder, and on 98 SE and ME the
sentence that Windows' own wizards may open for the devices the controller
shows after its restart. The button is Install (Remove under `/uninstall`,
section 9). Nothing is written before it.

### 5.8 The progress

A list of record 23's steps in order as `INST_STEP`s arrive (the copy, the
staging, each device, each barrier's wait, each value, the tray tool),
each with its state (waiting, running, done, failed, skipped, left for the
user): a step's outcome takes the place of its "running" row, so no row
stays "running" once its step has ended (matrix N2, 2026-10-19; the
engine ends every step it begins, a failure included), and an outcome
with no step open, a value written, is a row of its own; a progress bar
over the steps, and the log's path. A wait shows
what it waits for and its bound (`INST_WAIT`): "Windows is installing a
device it found (2 of 10 minutes); answer the window it opened". Cancel
during the progress is record 23's stop at a step boundary; the page says
so when it is pressed and the Finish page then reports what was done, as
the console's "Changes were made, but not everything is done" does.

### 5.9 The finish

The final line of record 23 section 7.4 (one of its three), the devices'
results of 5.9 (each one line), the steps left for the user where there
are any (the trap, the shared id, test-signing mode, a wizard to finish;
each as the console prints it), and the restart:

- "Restart now" (the default) and "Restart later" as radio buttons when
  the run changed something; Finish with the first restarts Windows:
  `ExitWindowsEx(EWX_REBOOT, 0)` on 98 SE and ME, and on NT the same call
  after `SE_SHUTDOWN_NAME` is enabled on the process token
  (`OpenProcessToken`, `LookupPrivilegeValueA`, `AdjustTokenPrivileges`,
  looked up at run time, as record 23's security routines are). A refusal
  of the restart is shown and the window closes. On 98 SE and ME, when a
  Stor98 device was updated in this run, the choice reads "Shut down
  now" (`EWX_SHUTDOWN`) and "Shut down later", because Stor98's readme
  asks for a shutdown and a fresh start rather than a warm restart after
  its update on those two systems (section 6.3); the final line of
  record 23 section 7.4 then reads "Changes were made. Please shut down
  and start Windows again." in both front ends, and its unfinished form
  "Changes were made, but not everything is done: do what is said above
  first, then shut down and start Windows again.", the exit codes (3,
  and 2 or 4 by the failed and steps flags) unchanged: the one amendment
  of that section, in the pure half's final-line rule (section 12). On
  2000 to 7 a Stor98 update keeps the ordinary restart. The console tool
  never restarts or shuts the machine down (record 23 section 5.9): that
  is the window's one addition to what the engine may do (W5).
- Where the run changed nothing, Finish closes.
- Where steps are left, the second button ("Restart later", or "Shut
  down later" in the case above) is preselected and the page says to do
  them first, as the console's unfinished line does.

The exit code is record 23's, so `INSTALL.BAT` passes it on unchanged
whichever front end ran. `SETUP.EXE` does not wait for the window and
returns 0 once it is started (section 2, draft 15): the result a person
reads is this page, and the log.

## 6. What it installs

### 6.1 The drivers of the package

`xhci98.sys`, `xhciuas.sys`, `xhcicm.sys` and the tunnel-port filter on
NT, exactly as record 23 sections 5 and 6 install them, the Thunderbolt
pair under the component's box (section 5.3) in place of the question. On
98 SE and ME the device install is record 27's (section 7); the local copy,
`DevicePath` and the install directory are record 23's.

### 6.2 The xHCI98 tools

The USB tree tool as record 23 section 9.5 and record 25 section 8 say,
under the component's box; and, new, the installer's own executables
(section 9): the window, the console tool and on 98 SE and ME the helper,
copied into the install directory so that the Add/Remove Programs entry
has a program to run after the package directory is gone. They are the
installer's files outside every `XHCIINST.LST`, as the tray tool's are,
replaced by the same `.TMP` route and never left pending for a restart
(record 23 section 9.5's rules, applied to three more files), and the
uninstall removes them last (section 9.4).

On Windows 2000 to 7, when the Thunderbolt 3 / USB4 component is
installed, the safe-removal mark service of record 21 section 7.4,
revision 3 (`XHCIMARK.EXE`, `XHCIMK64.EXE` on x64) is installed as well -
but **not** into the install directory: a program the service controller
starts as the system must sit where only administrators can replace it,
so its copy goes to `%SystemRoot%\system32` and is registered from
there, stopped before it is replaced or removed, and removed with the
marks it wrote (section 9.3); the install directory's move does not
concern it. Never on 98 SE or ME.

### 6.3 Stor98, only when ticked

Record 23 section 4.4 keeps the bundled Stor98 out of the local copy so
that no wizard installs it when it was not chosen. Since draft 16 it is
chosen with the Thunderbolt 3 / USB4 support (section 5.3; owner,
2026-10-19, replacing "never unasked"), which is itself never chosen
unasked. With the box ticked:

- **Before anything**, the run records what Stor98 the machine already
  has: whether a `stor98` service or driver key exists, and every devnode
  bound to it, present or not (NT: `Service` = `stor98`; 98 SE and ME: the
  driver key naming Stor98's files, as its INF binds them). Those are
  pre-existing, and the uninstall never counts them as this installer's.
  On 98 SE and ME `xhcicm.sys` makes `Services\stor98` itself for
  Stor98's link-state locator (its value `LinkState`), Stor98 installed
  or not, so there that key counts only with a subkey or another value
  (draft 12; the 98 SE legs of 2026-10-19 read a pristine machine as
  "Stor98 there before" without it, and the uninstall then kept the
  Stor98 the installer brought).
- **NT**: the local copy is made first, every `stor98*` file included
  when the box is ticked (record 23 section 4.4 and 5.6's order: the copy,
  then the staging from it, so a device found later takes its files from
  the copy and not from a medium that may be gone), and `stor98.inf` is
  staged from that copy (`SetupCopyOEMInf`), so an NVMe drive behind a
  tunnel installs by itself; then it is installed now
  (`UpdateDriverForPlugAndPlayDevices`, unforced) on every present device
  `stor98.inf`'s models match that has **no** driver (`Service` absent, or
  problem code 28) **and sits behind a Thunderbolt or USB4 tunnel** (draft
  12; record 23 section 4.4's gate and place reading), never on one another
  driver runs (draft 20: but for `stornvme` on a tunnel drive on 8.1, section
  5.3.1 case 3b) (record 23 decision D3 applies to Stor98's devices as to
  this package's: an internal NVMe drive under a vendor's driver is listed
  and left), and never on an internal drive or one whose place cannot be
  read, which is left with its reason on screen and in the log. A tunnel
  drive Stor98 already runs at an older release is updated as record 23
  section 5.4 updates this package's own devices, with the same version
  rule and the precautions below, whether this installer or the user
  bound it; the update never makes the device the installer's (the
  record, below). An internal drive Stor98 already runs (installed by
  hand) is never updated or reconfigured, and record 23 section 4.4's
  shared-file rule holds the file and the settings for it.
- **98 SE and ME**: the `stor98*` files go into the install directory with
  the package's drivers (the flat copy's list names them), where the
  wizards find `stor98.inf` (none when record 23 section 4.4 holds
  Stor98); record 27's helper installs it now on every present NVMe
  device with no driver behind a tunnel, by the same route and rules as
  the package's own devices (record 27 section 6 step 6), with Stor98's
  own read-back after it (section 8). **A device Stor98 runs is never
  installed again** (draft 16): Windows' device installer replacing the
  running `stor98.mpd` stopped 98 SE with a fatal exception 0E, every
  time. An older Stor98 is updated as a file instead (record 23 section
  4.4, revision 23): `STOR98.TMP` beside it and a `WININIT.INI`
  `[rename]` entry, the `INF` or `INF\OTHER` copy each Stor98 driver key
  names made the bundled `stor98.inf`, with or without a drive present,
  then a shut down and a start; a drive first plugged after it binds to
  the new file. The printed steps never send the user to Update Driver
  on a drive Stor98 runs, and a Stor98 drive no helper job touches may
  be unplugged between the plan and Install, as the confirmation asks,
  without the run refusing a changed plan (the plan made again reads
  the gate again).
- **An update of a Stor98 device** follows Stor98's readme ("Updating the
  driver"), whose two precautions are each for its own systems. On 98 SE
  and ME: nothing may be using the drive's volume, and the machine is
  shut down and started again afterwards rather than restarted with the
  drive attached (the readme records a volume coming back "not ready",
  and ME stopping with a VCACHE error, after a warm restart there). So on
  those two systems, when the run replaces Stor98's file, the Summary
  page carries one check box, "Please disconnect all external NVMe
  drives before proceeding" (draft 16, owner 2026-10-19; until then one
  box per drive, "nothing is using this drive", that Install required):
  ticked, the file update goes ahead; unticked, Install still runs and
  Stor98 is left for a later run, out of this run whole (no copy of its
  files, no job, no setting, no record; a step, exit 4). The finish page offers
  "Shut down now" in place of "Restart now" (section 5.9); the console
  tool asks the same sentence as one yes/no question (`INST_Q_S98_UNUSED`,
  Enter and `/yes` give N) and ends with the shutdown line. On 2000 to 7 the
  ordinary restart flow stands, and one thing is added: on a USB4 host
  under this package's own connection manager, a driver change on a
  tunnelled drive stops the device, which the connection manager takes as
  an eject and tears the tunnel down, and that teardown is not
  synchronised with the call's return (record 24 section 5.2): a
  read-back right after the call may still see the device. So every
  successful update of a Stor98 device behind a tunnel under that
  connection manager (the device's unit a verified tunnel port's, record
  24 section 4) is "updated, awaiting reconnection" whatever the first
  read-back shows: the Progress page asks to unplug the drive and plug
  it in again and waits, bounded at two minutes and skippable, until the
  device has been seen gone and present again, and only then reads it
  back as any updated device; a device not reconnected is a step left
  (exit 4), never a success and never a failure, whether it is present or
  gone at the time, and the next run verifies it. Record 23 section 5.9's
  "gone after its install is a failure" is amended for this case alone
  (section 12). The console tool prints the same request and waits the
  same way.
- **The record** (owner, 2026-10-18: the uninstall removes the bundled
  Stor98 only when this package's installer installed it). A key
  `HKLM\Software\xhci98\Stor98` with: `PreExisting` (`REG_DWORD` 1 when
  the baseline of the first bullet found any Stor98, a service, a driver
  key or a bound devnode; written once, never cleared, and since draft 16
  written with the run's first Stor98 change - with the record's first
  entry (a staged copy, a device, a setting) on NT, and on 98 SE and ME
  also once a new local copy that carries Stor98 is made, before
  `DevicePath` names it, and before a helper job or the file update -
  never by a run that makes none, such as one that leaves a
  hand-installed Stor98 alone);
  `Version` (the bundled release last installed: written only by a run
  that installed or updated it, a device the call or the helper bound or
  updated, or the 98 SE and ME file update; record 28 section 4 item 4
  admits devices to the no-interrupt start by it); under `Staged` one value per INF copy
  the setup engine made for this installer's installs, on NT the
  `oemNN.inf` name `SetupCopyOEMInf` returned when it made a new copy,
  never one it answered with an INF already in the store (that one was
  there before and is left), and on 98 SE and ME an `INF\OTHER` copy
  the engine created during one of this installer's installs or during
  a settle of this run: the engine's `INF` and `INF\OTHER` directories
  are listed (names, sizes and SHA-256s) at the baseline, before and
  after each helper install, and before and after each settle (record
  27 section 6), and a file that was not there before is recorded when
  the driver key of the device the helper installed or updated in that
  step names it afterwards, whether or not that device is the
  installer's under `Devices` (an update of a hand-installed device can
  make a copy without making the device the installer's), or, after a
  settle, when a device newly recorded under `Devices` by the rule below
  names it; the INF's ownership and the device's are separate records,
  and the reference check before any deletion stands; a file that was there before, referenced
  or not (a hand install's copy whose device was removed), is never
  recorded and never deleted by this installer, and a file that appeared
  without being named is left and said in the log (the flat copy in the
  install directory is the current source, one directory replaced on an
  update under record 23 section 4.4's lists, not a copy the engine
  keeps, and is not recorded here);
  each value the Stor98 version, kept across updates, so that two bundled
  versions in succession leave two entries where the engine made two
  copies, which the leg reads; under `Devices` one value per device
  instance id the installer first bound to Stor98, by two rules with
  their own evidence: a device the installer's own call or the helper
  bound, whose reading before the call showed no driver, or no Stor98,
  and whose reading after shows Stor98, is recorded on that evidence
  alone, whatever `PreExisting` says and whichever INF the engine chose;
  a device Windows bound by itself from the staged INF during a settle is
  recorded when the comparison of that reading with the one before shows
  it newly bound and its driver key's `InfPath` names an entry under
  `Staged` (NT), or `PreExisting` is clear (98 SE and ME, where the copy's
  origin is not recorded), and otherwise is left unrecorded and said in
  the log; a device that ran Stor98 at the baseline, or that an update
  changed, is never added.
  Under `Settings` one value per Stor98 setting the installer wrote
  (section 5.6). A run with the box unticked never touches an installed
  Stor98 and writes none of it. Staging and installing are told apart: a
  staged INF with no device bound is a record of the staging alone.
- **The uninstall** (section 9.3) removes, each step gated on the one
  before: the devices under `Devices`, present or not, through record 23
  section 9.2's devnode removal on NT and record 27 section 9's on 98 SE
  and ME; then each INF under `Staged` (`SetupUninstallOEMInf` without
  force, which refuses an INF a devnode still references, and the
  engine's own reference check through every devnode's `InfPath` as
  record 23 section 9.2 step 4 reads them; on 98 SE and ME the copy's
  files and the `INF\OTHER` copies by the same check), so an INF a
  pre-existing or unlisted device still names stays; then the `stor98`
  service, its files and the recorded settings only when no devnode,
  present or not, is bound to Stor98 any more and `PreExisting` is clear
  (on 98 SE and ME a file update this installer queued and the next
  start has not done yet goes with the file: its `[rename]` entry and
  `STOR98.TMP`, draft 16);
  otherwise they stay, and the finish page says that a Stor98 this
  installer did not install was left. A device the user installed and
  this installer only updated keeps its binding, and two devices sharing
  one INF, service and binary, one installed by hand and one by this
  installer, lose only the installer's device; a staged copy that device
  still names (an update of the hand-installed device from the bundled
  release points its driver key at the newest copy) stays with it, by
  the reference check, and the finish page says so.
- **Tunnel drives Windows bound after the run** (draft 12; the owner's
  coordinator, 2026-10-19, from the Windows ME matrix leg of that day).
  The install stages Stor98 and records no device when no tunnel was up
  during the run; after the restart the enclosure appears, and Windows'
  own New Hardware wizard binds its NVMe to Stor98 from the staged INF
  (or the install directory), making an `INF\OTHER` copy on 98 SE and
  ME. That is the usual flow, so the uninstall's first Stor98 step adopts
  such devices when `PreExisting` is clear: every devnode bound to Stor98,
  present or not, that the record does not name (on 98 SE and ME every
  `Enum\PCI` entry whose driver key names `stor98.mpd`) is added under
  `Devices`, with the INF copy its driver key's `InfPath` names (on 98 SE
  and ME the `INF\OTHER` copy whose long or 8.3 name it is; exactly one
  candidate, or none is taken, and an unreadable directory stops the
  uninstall) under `Staged` when that copy's active text names Stor98's
  driver file as a word (`inst_inf_names_file`), the copy recorded
  before the device and a record that cannot be written stopping the
  uninstall - provided every such
  device sits behind a tunnel by record 23 section 4.4's reading, or is
  not present (gone with its tunnel) (`inst_s98_adoptable`). One that is
  internal, or whose place cannot be read, keeps Stor98 for all of them:
  nothing is adopted, it is named with its reason, and the service step
  below leaves Stor98 as before. The adopted devices and copies then go
  by the steps above; Stor98's CriticalDeviceDatabase entries go by
  record 23 section 9.2 step 6a only once its service is gone, so an
  adoption refused for an internal drive leaves them with the service. And at the install, on 98 SE and ME, a copy that
  appeared outside a Stor98 step (a wizard of Windows' own racing the
  helper) is recorded under `Staged` when its text names Stor98 and
  `PreExisting` is clear (`inst_s98_stray_inf_ours`); otherwise it is
  left and logged, as before.
- The x64 `stor98.sys` is signed in the package's one signing step, by
  the same certificate as this package's drivers, from `3.1.0.0` (Stor98
  `0.4.0.0` and earlier carried Stor98's own test signature and
  `stor98-test.cer`, which the package no longer ships); the test-signing
  note of record 23 section 5.10 names it beside this package's drivers
  when the box is ticked, and no certificate is imported.
- From `3.1.0.0` the bundled Stor98 carries the package's version (its
  `stor98.inf` `DriverVer` and its file versions), read from `stor98.inf`
  as before; a Stor98 `0.4.0.0` or `0.3.0.x` an earlier package installed
  is older than it by the four-number comparison, so its devices are
  updated, and `PreExisting` alone, never a version, decides whose Stor98
  it is.

### 6.4 The console tool

Gains the same: `/stor98` installs the bundled Stor98 without asking (the
pair of `/thunderbolt`, and refused without it or `/cmonly`: Stor98 is part of the
Thunderbolt 3 / USB4 support, owner, 2026-10-19), the question "Install
Stor98, the bundled NVMe driver?" with Enter for Y (draft 16; N until
then) where the package
carries it and the Thunderbolt support is in the run (left out, and said,
otherwise; `inst_s98_decide`); `/yes` gives Y there, and `/nostor98`
leaves it out unasked (record 23 section 7.1, revision 23: the console
follows the window's box, ticked with its parent), and the Stor98
rows in its value questions (asked only when Stor98 is installed by the
run). Record 23 sections 4.4, 7.1 and 8.1 are revised (section 12).
Draft 12: the question's text says "For NVMe drives behind a Thunderbolt
or USB4 tunnel; the installer selects tunnel drives only", and that an
internal drive is left for a hand install; with the Thunderbolt
component out of the run (a No, or `/yes` with no controller) it is not
asked and Stor98 is left out, as draft 11 has it (the console refuses
`/stor98` without `/thunderbolt` or `/cmonly`). The value questions are not asked when record 23 section
4.4's shared-file rule holds the settings.

### 6.5 The USB storage drivers on Windows 98 SE (task 43.4; draft 17)

Windows 98 SE has no USB mass-storage driver: a Bulk-Only stick is
"Unknown Device" at Code 28, and a UAS disk under `xhciuas.sys` stays at
Code 2, because `xhciuas.inf` names `USBNTMAP.SYS` as an upper filter and
NTKERN fails a devnode whose named filter is missing (`runs/run-28.md`,
"Windows 98 SE with no USB 2.0 stack, and with SweetLow's alone"). From `3.2.0.0` the release
download carries NUSB 3.3's seven storage files at its root as
`NONFREE\` (task 43.3; `legal-provenance.md` section 5, "NUSB 3.3's
Windows 98 SE storage files"), and the installer offers them. The owner's
decisions of 2026-10-20: selectable, so a user who already has NUSB need
not install them; on 98 SE only, ME and later using their own drivers;
installed unless NUSB is installed; no file already present overwritten,
even with the row ticked; and the uninstall removes what it placed.

**The seven, and where their INFs send them** (static, the INFs read as
text; nothing here edits them):

| File | Goes to | Sent there by | Binds |
|---|---|---|---|
| `USBSTOR.SYS`, `USBNTMAP.SYS`, `USBAUTH.SYS`, `USBU2A.SYS` | `%windir%\SYSTEM32\DRIVERS` | `USBSTOR.INF`'s `[DestinationDirs]`, dirid 10 `system32\drivers` | a Bulk-Only (and CBI, CB) device: `NTMPDriver` `USBSTOR.SYS`, `upperfilters` `USBNTMAP.SYS` (`[USBSTOR.HW.AddReg]`); `USBAUTH.SYS` and `USBU2A.SYS` lower filters of some vendors' lines |
| `USBMPHLP.PDR` | `%windir%\SYSTEM\IOSUBSYS` | `USBNTMAP.INF`'s `defaultdestdir=12` | a disk, CD-ROM or floppy unit (`USBSTOR\GenDisk`, `USBSTOR\GenCdRom`, `USBSTOR\GenSfloppy`): "USB Disk", `DevLoader` `*IOS`, `PortDriver` `USBMPHLP.PDR` |
| `USBSTOR.INF`, `USBNTMAP.INF` | the package directory (record 23 section 9.6), and from draft 19 `%windir%\INF` under their own names (section 6.5.11) | this installer, which `DevicePath` names (record 23 section 6.1); the setup engine then makes its own copy in `INF\OTHER` when it installs from the package directory; and this installer's own placement in `%windir%\INF`, where NUSB's installer puts them (section 6.5.11) | - |

#### 6.5.1 Where the files come from

From the download's `NONFREE\STORAGE\NUSB33\W98SE\` beside the version
root, the package root of record 23 section 4.2 (owner, 2026-10-20; task
43.3), read by those seven names alone and each held to its SHA-256 in a
table the installer carries (`instStorFile`, `inst_core.c`: the seven
names, their destinations and the hashes of `legal-provenance.md`
section 5's table, which `scripts\package\test-package.ps1` holds equal
to `package-common.ps1`'s allowlist). The pack's `SHA256.TXT` is not the
check: the installer's own table is. Where the folder is absent (a cut
with `-NoDriverPack`, or a package of `make-package.ps1`'s, which never
carries the pack), the row is shown on 98 SE greyed, its note saying that
this download does not carry the USB storage drivers; where a file is
missing or its bytes differ, greyed with "the storage files in this
download are not the ones this release carries", the log naming each.
Nothing in `NONFREE\` but the seven is read.

#### 6.5.2 The row

A Components row (section 5.3) shown on Windows 98 SE (4.10.2222) alone:
never on ME, 2000 or later, not even greyed, and the engine keeps it out
of a run on any other system (`stor_set`). (Draft 20, section 5.3.1:
shown on every system; ticked on any but 98 SE it only copies the
seven to the tools folder, and nothing else of section 6.5 runs there.) Its label, "USB storage
drivers for Windows 98 SE (NUSB 3.3's)", and its sentence: "Windows 98 SE
needs these files for every USB disk: Bulk-Only disks, and UAS disks
under xhciuas.sys too (its INF names USBNTMAP.SYS as a filter; without
it a UAS disk stays at Code 2). Install them unless NUSB is installed."
Under it the detection's line (section 6.5.3) and, when NUSB's uninstall
entry is there, a second line saying so. The default is section 6.5.3's;
the box can be ticked or unticked whatever was found, and ticked it never
copies over a file already present (section 6.5.4).

#### 6.5.3 Detection, and the default

Each of the seven is looked for where it installs: the four `.SYS` files
in `%windir%\SYSTEM32\DRIVERS` (`USBSTOR.INF`'s `DestinationDirs`, dirid
10 `system32\drivers`), `USBMPHLP.PDR` in the system directory's
`IOSUBSYS` (`USBNTMAP.INF`'s `defaultdestdir=12`), and each INF in
`%windir%\INF` or `INF\OTHER` under its own name or a renamed one: a file
of its own name, or any device INF (one with a `[Manufacturer]` section)
whose active text names that INF's key file as a word (`USBSTOR.SYS` for
`USBSTOR.INF`, `USBMPHLP.PDR` for `USBNTMAP.INF`; `inst_inf_names_file`),
which finds the setup engine's provider-prefixed copy in `INF\OTHER` and
NUSB 3.6's own INFs alike. Each file reads one of four states
(`inst_stor_state`):

- **absent**;
- **this installer's**: named by the install record's `Files`, `Infs`
  or `Placed` (section 6.5.6) with its bytes still the recorded ones;
- **the same**: present, not recorded, and byte for byte this download's
  file (for an INF, every candidate found is);
- **other**: present with other bytes, or unreadable, or for an INF any
  candidate that is not the same.

Whether an earlier run of this installer put the seven in the package
directory is recorded apart from the files (the record's `Source`,
section 6.5.6): it tells this installer's own deployment from another
installation when the files alone cannot, as when the setup engine has
since made its own copies of the INFs at a plug, or when a file present
before that run was the download's own bytes.

NUSB's own uninstall entry, `HKLM\Software\Microsoft\Windows\
CurrentVersion\Uninstall\USB20U` (static: NUSB 3.3's `_NUSB.INF` line
129 and 3.6's line 145 write it; NUSB's `_USB2UN.INF` deletes it with
the USB 2.0 stack's three files and leaves the storage files), is read
and recorded apart from the files: it is said under the row and in the
log, it decides neither the default nor what is copied, and while it is
present the uninstall deletes none of the seven (section 6.5.8).

**The default** (`inst_stor_default`): an earlier run's recorded choice
when there is one (section 6.5.7); otherwise ticked when none of the
seven is found (every file absent or this installer's), and unticked
when any is.

**The text** (`inst_stor_found_text`, host-tested), on the row, the
Found page and the Summary, and in the console's question: none found -
"Not installed: USB disks do not work on Windows 98 SE without them.";
all seven found - "Found installed: <the names>. They are left as they
are."; some found - "Found: <the names>. Only some of the seven: USB
storage is not known to be ready."; all present and this installer's -
"Installed by this installer earlier." It never says storage is ready
where only some are found.

#### 6.5.4 Ticked: no overwrite through the whole Setup operation

The pack's INFs are unmodified and their `CopyFiles` carry no
no-overwrite flag, and the helper leaves replacement to the setup
engine's version rules (record 27 section 5.3: never `DI_NOFILECOPY` or
`DI_FORCECOPY`). So any device install that uses them - one the helper
runs, or one Windows starts at the user's first plug from the package
directory on `DevicePath` - copies every file of the chosen section from
that directory, and may replace a file already present. The design does
not try to stop such a copy, which nothing documented would let it do
without editing the INFs. It keeps the pack's INFs away from Setup
unless every copy Setup could make writes the bytes already there: **the
same-bytes route**. Setup's copies come from the package directory,
which holds the download's seven; a file of the seven already present
survives such a copy byte for byte when it is the same as the download's,
or this installer's own recorded copy, which is the same bytes. And the
installer's own copies never replace anything (below). The run's storage
case (`inst_stor_case`, host-tested on every combination of states):

| Case | When | A ticked run |
|---|---|---|
| none | every file absent | the same-bytes route: the five binaries copied, the seven in the package directory, the devices of section 6.5.5 |
| complete | all seven present, at least one not this installer's, and no `Source` recorded | nothing: no file copied, the pack not in the package directory, no device job; Setup binds with what is there. The text says the files were found installed and are left as they are |
| partial, the same | some present, every present one the same or this installer's (the binaries without their INFs, when the binaries are the same, are this row) | the same-bytes route: only the absent binaries copied; the present ones untouched by the installer, and any copy Setup makes over them the same bytes |
| partial, other | some present and one of them other: another release, a patched file, a file that cannot be read, an INF of another text under a renamed name; and with `Source` recorded, any file other however many are present (an installed file changed since this installer's deployment: the withdrawal below) | no route that keeps the bytes is established: nothing copied, the pack not in the package directory (so no first plug can take it either), no device job; the run reports the storage install incomplete with the files found and their states, a step left (exit 4), and the text that storage is not known to be ready |
| this installer's | every present file this installer's, or `Source` recorded and every present file this installer's or the same (an update over its own install, section 6.5.7) | the same-bytes route: an absent one (deleted since) copied again; nothing present replaced; the package directory's seven refreshed |

**The installer's own copies** (`stor_place`), only on the same-bytes
route and only for the binaries (`inst_stor_copy_set`, host-tested); until
draft 19 the two INFs were never copied into `INF` or `INF\OTHER` by the
installer - the setup engine makes its `INF\OTHER` copies (record 23
section 6.1), and from draft 19 the installer places them in `INF`
itself after the binaries (section 6.5.11). Each absent
binary is written beside its destination as `<name>.TMP`, compared byte
for byte with the download's, its record value written (section 6.5.6),
and only then renamed to its name with `MoveFileA`, which fails when the
name exists and so never replaces a file that appeared meanwhile; a move
that fails deletes the `.TMP` and the record value and leaves the file
that is there. (Draft 19: the two INFs are then placed in `%windir%\INF`
by the same steps, section 6.5.11; never in `INF\OTHER`.) The states are read again immediately before the first
copy; a case that differs from the one the Summary showed stops the
storage part of the run with nothing copied ("the files changed while
the installer waited"), a step left. No `WININIT.INI` entry is ever made
for these files.

**What the guarantee covers.** Every one of the seven present when the
run starts, through every copy the run's own jobs make and every copy a
later device install makes from the package directory while that file
is still the one the run found. The 43.4 line itself sends the pack's
INFs to Setup through the package directory on `DevicePath`, and Setup
copies from them over whatever is at the destination when a device is
later installed: a file another package lays down at a destination
after this run, with other bytes, could be replaced by a later first
plug that picks the pack's INF. Nothing documented closes that without
editing the pack's INFs or keeping them off `DevicePath`, which the line
excludes, so the design does not claim it: the next run of this
installer reads such a file as other and withdraws the pack (below), the
uninstall never deletes it (section 6.5.8), and whether the owner
accepts this residual or wants the pack withdrawn from `DevicePath`
once the setup engine has made its own copies is section 14's open
decision. The release notes state the residual.

**Withdrawal.** Whenever a run's package copy will not carry the seven
while `Source` is recorded (an earlier run deployed them), or while the
record's `Placed` names a copy (draft 19) - the row
unticked, the download without the pack, the case partial-other, the
case changed at the second reading, a copy or a move that failed, a
binary found not in place - the install directory is taken off
`DevicePath` before the package's copy is attempted, so before any of
its steps that can fail (`devicepath_update` without it); in the same
step (draft 19) every INF this installer placed in `%windir%\INF`
(the record's `Placed`, section 6.5.11) is deleted with its `.PNF` by
section 6.5.8 step 3's protections: only while its bytes are still the
recorded ones, NUSB's `USB20U` is absent and no driver key, present or
not, names it by its `InfPath`. One whose bytes changed, or with
`USB20U` present (NUSB lays the same INF there, so it may be NUSB's
now), is left and relinquished: its `Placed` value goes, and it is
never this installer's again. One a driver key names is left and stays
recorded, said in the log, for the uninstall to remove after its
devices (step 3 again); it stays published, which is the residual
above for the copies the setup engine makes, unchanged. A reading
that fails, or a placed copy that cannot be deleted, stops the run
there, as a `DevicePath` that cannot be written does; only the
package's copy made without the seven puts
the directory back on `DevicePath`; a copy that fails
leaves the directory off `DevicePath`, the run saying to run the
installer again, so the pack is never left published over a file it
could replace. `Source` is cleared only once that copy is made, so a run
cut short leaves it for the next run to withdraw again.

**Order in the run** (record 23 section 6.2 and record 27 section 6),
each step failing closed: the baseline (section 6.5.6); the withdrawal
above where it applies; the states read again and compared with the Summary's
case; `PreExisting`; the storage copies; and only when every copy of the
set is in place and the case still admits the route, `Source`, the two
INFs placed in `%windir%\INF` (draft 19, section 6.5.11), then the
package's copy made with the seven in it and `DevicePath` naming it
(record 23 section 4.4). A changed case, a copy that fails or a `.TMP` that differs leaves
the run's storage part there: the package's copy is made without the
seven (so nothing of the pack reaches `DevicePath`), no storage job runs,
and the binaries already moved into place in this run stay recorded for
the uninstall; a step is left (exit 4). So the pack's INFs are never
published before its files, `USBNTMAP.SYS` is in place before
`xhciuas.sys` binds a UAS disk, and `USBSTOR.SYS` before a Bulk-Only
device's install.

#### 6.5.5 The devices, present now and later

The helper installs a device only when it is present (record 27 section
5.1: `not-present` otherwise; section 2: no driver-index insertion), so
in the run it installs the storage devices present, in record 27 section
6's order with two steps added (record 27 draft 10):

- **Bulk-Only devices** (`inst_stor_kind`: an id `USB\Class_08&SubClass_02&Prot_50`,
  `SubClass_05` or `SubClass_06`, the generic lines of `USBSTOR.INF`),
  after the root hub, hub and composite passes and before the UAS step:
  each present one no INF of this package matches with no driver
  ("Unknown Device": no driver key, or problem 28 and neither
  `NTMPDriver` nor `PortDriver`) is installed by the package directory's
  `USBSTOR.INF`; one with any driver is left.
- **UAS devices**, by `xhciuas.inf` as before, after the Bulk-Only step:
  the filter is in place when `xhciuas.sys` binds. A UAS devnode already
  at Code 2 before this run's copies (bound while `USBNTMAP.SYS` was
  missing) is not reinstalled: NTKERN started such a devnode only after a
  fresh start on 98 SE (`runs/run-28.md`: "after the next cold boot"),
  so it is listed as starting at the next start, and the run's final
  line takes section 5.9's shutdown form ("shut down and start Windows
  again", `inst_eng_shutdown`).
- **Units, the settle and the rebind**: after the UAS step a settle
  (record 23 section 5.8's, as each pass has), and then each present
  unit (`USBSTOR\GenDisk`, `USBSTOR\GenCdRom` or `USBSTOR\GenSfloppy`
  among its ids) not bound to `USBMPHLP.PDR` that has no driver or
  Windows' own generic binding is installed by the package directory's
  `USBNTMAP.INF`. Windows binds a first disk to its generic "Disk drive"
  before it has seen `USBNTMAP.INF` (the release notes and
  `runs/run-28.md`: no drive letter until Update Driver moves it to "USB
  Disk", then a replug), so that binding is the case this step exists
  for. It is read, every value readable, as the driver key of class
  `DiskDrive` with no `PortDriver` and no `NTMPDriver`, its `InfPath`
  exactly `DISKDRV.INF` and that file in `%windir%\INF` itself (98 SE's
  own disk-drive INF; `inst_stor_unit_takes`). That INF's name is
  expected, not yet read; 43.5's legs read it, and until they do a unit
  bound any other way - another INF, another loader, a value that cannot
  be read - is left, so a wrong guess leaves the disk at "Disk drive"
  (the release notes' hand step) rather than rebinding another driver's
  device. The helper's job moves it to "USB Disk"; then a settle, and the
  unit is read back: `PortDriver` `USBMPHLP.PDR`, started, no problem
  code. A unit read back bound but not started gets one step, "unplug
  the disk and plug it in again" (the replug of run-28's reading), exit
  4. Units appear only once their parent's driver has started, so this
  step follows the Bulk-Only and UAS steps and their settle; a unit that
  appears in a later settle of the run is taken in the same way before
  the run ends.
- **CBI and CB devices** (floppy drives, `Prot_00` and `Prot_01`) are not
  installed by the helper: they bind at their plug from `DevicePath`.

**Absent devices** bind at their first plug through Windows' own
detection. Until draft 19 that was from the package directory on
`DevicePath`: its New Hardware wizard (read on 43.5's first leg,
2026-10-20: every new stick raised the wizard, which found
`C:\XHCI98\USBSTOR.INF` without a prompt), or the silent install where
`XhciSilentInstall` is on (record 23 section 8.1). From draft 19 the
two INFs are in `%windir%\INF` as well, where NUSB's installer puts
them, so that Windows finds the driver as it does under NUSB, with no
wizard (section 6.5.11; that it does is the reading of 43.5's first
leg taken again). The files are already in place, so the engine's copy
writes the same bytes. Whether a first disk then
lands on "USB Disk" or on "Disk drive" is a reading of 43.5's legs; when
no unit of this run was bound to `USBMPHLP.PDR` (so the setup engine has
not yet installed `USBNTMAP.INF` from the package directory), the Finish
page and the console's end say once that if the first USB disk plugged
in later shows as "Disk drive" with no drive letter, running the
installer again moves it to "USB Disk" (the unit step above, on an
update over this install), or Update Driver on it pointed at the install
directory does, then a replug. This is a note, not a step.

#### 6.5.6 The record

`HKLM\Software\xhci98\Storage` (on Stor98's pattern, section 6.3), on
98 SE only:

- `Choice` (`REG_DWORD`, 1 ticked, 0 unticked): written by every install
  run on 98 SE that offered the row, once its confirmation is given;
- `PreExisting` (`REG_DWORD`): 1 when the baseline found any of the seven
  that was not this installer's; written once, never cleared, with the
  run's first storage change (before the first storage copy), never by
  a run that makes none;
- `Before` and `BeforeOk` (draft 18): every copy of the pack's own INF
  bytes in `INF` or `INF\OTHER` when the first deployment begins, one value
  per copy by its path, and `BeforeOk` 1 once that list is whole; written
  once, before that run's first copy (section 6.5.8 step 1);
- `Published` (`REG_DWORD`, draft 18): 1 once a run's package copy with
  the seven was made and `DevicePath` named it, or (draft 19) once a run
  placed one of the INFs in `%windir%\INF` (section 6.5.11); never cleared (a later
  withdrawal included), deleted only with the record. The uninstall's
  adoption rests on it (section 6.5.8 step 1);
- `Source` (`REG_DWORD`): 1 once a run has made the package's copy with
  the seven in it, written just before that copy; cleared by a run that
  makes the copy without them (the withdrawal of section 6.5.4, or the
  row unticked). It is what tells this installer's own deployment from
  another installation on an update (section 6.5.3);
- `Files`: one value per binary the installer copied, its name the full
  path it was copied to and its data the SHA-256 of its bytes, written
  before the file is moved into place (section 6.5.4);
- `Infs`: one value per INF copy the setup engine made for this
  installer, its name the exact name of that copy's value under SetupX's
  `OEM Name` key (`HKLM\Software\Microsoft\Windows\CurrentVersion\Setup\
  SetupX\INF\OEM Name`, record 23 section 6.1; the value's name is the
  copy's path, long or short as the engine wrote it) and its data the
  copy's SHA-256. A copy is recorded when the baseline's listing of `INF`
  and `INF\OTHER` (names and SHA-256s) did not hold it, its bytes are one
  of the pack's two INFs', an `OEM Name` value names it (found by its
  path or its 8.3 alias, as `inst_oem_name_ours` matches), and either a
  device recorded under `Devices` names it by its driver key's `InfPath`
  (`s98_inf_names`'s rule, the 8.3 alias included) or `PreExisting` is
  clear. A copy that was there before, one whose `OEM Name` value cannot
  be found, or one of other bytes is never recorded, and is said in the
  log. Nothing else of the driver index is read or recorded;
- `Placed` (draft 19): one value per INF this installer placed in
  `%windir%\INF` (section 6.5.11), its name the full path it was placed
  at and its data the SHA-256 of its bytes, written before the file is
  moved into place and deleted when the copy is deleted (the
  withdrawal, the uninstall) or found gone. A copy `Placed` names with
  its bytes unchanged is this installer's in every reading `Infs`
  serves: the detection (section 6.5.3), a device's `InfPath` copy
  (`stor_infpath_copy`'s `recorded`), and the adoption (section 6.5.8
  step 1);
- `Silent` (draft 22): one value per `SilentClasses` entry this installer
  added (section 6.5.12), its name the entry's value name and its data the
  entry's four bytes in hex, written before the entry; the uninstall
  removes exactly those;
- `Devices`: one value per devnode the installer first bound to the
  storage drivers (data `bulk` or `unit`). Ownership goes by first
  binding, as section 6.3 has it for Stor98: a device the baseline read
  as present with no driver ("Unknown Device"), or a unit at Windows'
  generic "Disk drive", or not present at all, that the helper's job
  then bound to `USBSTOR.SYS` or `USBMPHLP.PDR` is recorded on that
  evidence; one Windows bound during a settle of the run is recorded
  when the reading after the settle shows it newly bound and its
  `InfPath` names a copy recorded under `Infs` or `Placed`, or
  `PreExisting` is clear; one already bound to the storage drivers at the baseline, or
  bound by another package (an `InfPath` naming a copy of other bytes)
  is never recorded.

The readings are taken where Stor98's are (`s98_record_devices`): after
every settle and helper job, at every exit of the helper's passes, and at
the end of the run; the baseline before the run's first change.

#### 6.5.7 An update over its own install

The record's `Choice` is the row's default (an update keeps the choice;
the console's question takes it as its Enter). Ticked, the files the
record names with their bytes unchanged are this installer's, and with
`Source` recorded a file of the download's bytes (one the setup engine
copied at a later plug, or one that was there before the first run and
that run kept) counts with them, so the case is "this installer's" and
nothing present is replaced: the
installed system files stay as they are, and only the package
directory's copy of the seven is refreshed by the package's copy (record
23 section 4.4's lists), where the wizards and the helper take them from.
A recorded file whose bytes have changed is other, and the run copies
nothing (the partial, other case). Unticked, the package's copy is made
without the seven and nothing else changes: the system files, the
devices and the record stay (unticking is "not now", as for every
component, section 5.3), and the uninstall removes them later; with
`Source` recorded the withdrawal of section 6.5.4 runs first. A download
without the pack keeps the record and the system files as they are; the
row is greyed (section 6.5.1) and the package's copy has no seven, so
devices first plugged afterwards find the setup engine's own copies in
`INF\OTHER` where an earlier install made them, and otherwise a wizard
asks.

#### 6.5.8 The uninstall

On Stor98's ownership model (section 6.3): it removes what it placed, and
nothing it does not own. In section 9.3 step 4's order:

1. **Adoption** (the owner's decision of 2026-10-20, draft 18): a
   storage device whose driver came from the pack's INF through this
   installer's `DevicePath` copy is this installer's, a stick first
   plugged after the install (the usual case) included, so the usual
   flow - the install, the first stick, the uninstall - leaves none of
   the seven. It runs after the gate of record 27 section 6 (the tree
   settled, no wizard open), before any removal, and only where this
   installer published the seven (the record's `Published` is 1, or
   (draft 19) a copy `Placed` names is in `%windir%\INF` with its
   recorded bytes, the evidence of a placement whose `Published` write
   did not happen; a recorded choice, or a `Source` written before a
   package copy that then failed, is no evidence) and NUSB's `USB20U` is
   absent.
   Every devnode, present or not, whose driver key names `USBSTOR.SYS`
   (`NTMPDriver`) or `USBMPHLP.PDR` (`PortDriver`) and that `Devices`
   does not name is adopted when its `InfPath` names exactly one copy in
   `INF` or `INF\OTHER` of the pack's own INF bytes (`stor_infpath_copy`,
   the 8.3 alias included) and that copy is one `Infs` or (draft 19)
   `Placed` holds - a stick bound with no wizard from the INF this
   installer placed in `%windir%\INF` names it by its bare name - or is not
   one of the copies of those bytes that were already there before the
   first deployment (the record's `Before`, listed by path just before the
   first deployment's copies, and trusted only with `BeforeOk` 1;
   `inst_stor_adoptable`, host-tested, a partial install's first stick
   included). The copy is read again and must still hold the pack's bytes;
   an unrecorded one is recorded first, by its `OEM Name` value whose data
   names that INF (none found: the device is not adopted), then the device
   under `Devices` (`bulk` or `unit`). A device
   bound from a hand install of other bytes, from a copy that cannot be
   read or is named twice, or with `USB20U` present, is left with its
   copy and every file it uses (step 4's reference check). The bytes
   cannot tell a hand install of the very same seven files from this
   installer's copy; the owner accepted that (section 14). `/list` says
   what would be adopted.
2. **The recorded devices**, present or not, children first: units, then
   Bulk-Only devices, through the helper's `remove` job (record 27
   section 9), before the package's own UAS devices and everything above
   them. An entry gone with its parent is not an error; an entry left
   after its removal stops the destructive steps (section 9.3's
   incomplete uninstall, the record kept).
3. **The recorded INF copies**, after every devnode of the package is
   gone and the package's own INF copies are deleted: each deleted with
   its `.PNF` and its one `OEM Name` value, the exact name the record
   holds, only when its bytes are still the recorded ones, `USB20U` is
   absent and no driver key, present or not, names it by its `InfPath`
   (the reference check of record 23 section 9.2 step 4); one still named,
   or changed, or kept while `USB20U` is present, is left with its `OEM
   Name` value and said, and its record value goes. The INFs this
   installer placed in `%windir%\INF` (draft 19, the record's `Placed`)
   follow the same rules in the same step - its bytes still the recorded
   ones, `USB20U` absent, no driver key naming it - each deleted with its
   `.PNF`; they have no `OEM Name` value, so none is touched for them.
   NUSB's own uninstall deletes an INF of its from `%windir%\INF` the same
   way, by name (static: `_USB2UN.INF`'s `DelFiles` of `USB2.INF` from
   dirid 17); what the setup engine's index holds after such a deletion
   is the engine's own and is not read here.
4. **The recorded files**, last: each deleted only when its bytes are
   still the recorded hash, `USB20U` is absent (NUSB lays the same bytes
   over them, so the hash cannot tell whose they are; `inst_stor_remove`,
   host-tested), and no devnode present or not uses it - a driver key's
   `NTMPDriver` or `PortDriver`, or an `Enum` key's `UpperFilters` or
   `LowerFilters`, naming it. A file kept for one of those reasons is said
   with the reason and its record value goes, since nothing this
   uninstall owns uses it. **A deletion that fails** (a file still in
   use) keeps its record value and sets the restart and steps flags: the
   uninstall is incomplete, its Add/Remove Programs entry stays "(incomplete:
   run again)" (section 9.3), and the uninstall run after the restart
   finishes it, starting from what the record still names. No
   `WININIT.INI` entry is made.
4a. (Draft 21) The `SilentClasses` entries the record's `Silent` names,
   each deleted while it holds the recorded bytes (section 6.5.12),
   whether or not NUSB is installed.
5. The `Storage` key, its `Choice` and `PreExisting` and its subkeys,
   deleted whole with the records of section 9.3 step 6 by the run that
   finishes, as Stor98's is (`forget_dir`); an incomplete uninstall keeps
   it whole for the next run.

Never deleted: a file the installer did not copy, one whose hash has
changed, a devnode or INF copy it does not own (one already bound to the
storage drivers before the install, or bound or made later by another
package), and none of the files or INF copies while `USB20U` is present.
`/list` lists
each with what would be done.

#### 6.5.9 The console

`/storage` ticks the row and `/nostorage` unticks it, both unasked; the
question "Install the USB storage drivers for Windows 98 SE (NUSB
3.3's)?" with the row's sentence and the detection's text, Y and N on
lines of their own and Enter for the default of section 6.5.3, is asked
after the Stor98 question on 98 SE when the download carries the pack;
`/yes` and `/defaults` take the default. `/storage` is refused on any
other system ("the USB storage drivers are for Windows 98 SE only") and
in an uninstall (draft 20, section 5.3.1: accepted on every system, the
copy to the tools folder with its warning, and still refused in an
uninstall and without the pack); `/nostorage` is accepted everywhere (on any other system
it changes nothing), so one script serves every target. `/list` says
what the case is and what a run would copy. The window takes both as
preselections (section 10).

#### 6.5.10 Not done

The setup engine's driver index is never written (record 27 section 2):
until draft 19 no INF was placed in `INF` or `INF\OTHER` by the
installer; from draft 19 the pack's two INFs are placed in `INF` itself
(never `INF\OTHER`) by a file copy, as NUSB's installer places them,
and what the engine then makes of them is its own (section 6.5.11). The pack's
INFs are never edited, and no `WININIT.INI` entry is made for the seven.
NUSB's other files (`IOS.VXD`, `DISKTSD.VXD`, `NTMAP.SYS`) are not part
of the pack: whether a stock 98 SE needs any of them is 43.5's reading,
and the row's text makes no claim that it does not. On ME, 2000 and
later nothing of this section runs (draft 20: but section 5.3.1's copy
to the tools folder and its record, when the row is ticked there).

#### 6.5.11 The silent first plug: the INFs in `%windir%\INF` (task 43.42; draft 19)

**What was found.** 43.5's first leg (2026-10-20, the 43.4 cut on a
fresh 98 SE): with the row installed, every new Bulk-Only stick raised
the Add New Hardware Wizard for "USB Mass Storage Device", which found
`C:\XHCI98\USBSTOR.INF` through `DevicePath` with no prompt and gave
the drive letter after Finish; a second stick raised it again, after
the setup engine had made its `INF\OTHER` copy of the INF at the first.
43.5 asks for no wizard. The owner's decision (2026-10-20): NUSB parity
- the pack's two INFs in `%windir%\INF` as NUSB's own installer puts
them, so a new stick installs with no wizard; the copies owned,
recorded and removed by the uninstall, and never over an INF already
there.

**What NUSB's installer does** (static: `nusb33e.exe` of
`legal-provenance.md` section 5, its resources read from the file and
its INFs read as text; nothing executed; `legal-provenance.md`
section 4's row "NUSB 3.3's installer: how it places its INFs"):

- `nusb33e.exe` is an IExpress package. Its `RCDATA` resources:
  `RUNPROGRAM` `rundll32 advpack.dll,LaunchINFSection _nusb.inf`,
  `POSTRUNPROGRAM`, `ADMQCMD` and `USRQCMD` `<None>`, `REBOOT` four
  zero bytes, and `CABINET` the 38 files it unpacks.
- `_NUSB.INF`'s `[DefaultInstall]` is `CopyFiles` and `AddReg` alone. Its
  `W98Upd.Copy.inf` list (`1394.INF`, `ntmap.inf`, `usbstor.inf`,
  `usbntmap.inf`, `NODRIVER.INF`, `USB2.INF`, `_USB2UN.INF`) goes to
  dirid 17, the INF directory itself (`[DestinationDirs]`
  `W98Upd.Copy.inf = 17`); a second list, `Win98.Copy.cab`, puts the
  binaries and the INFs again in `10,Options\Cabs`. It has no
  `DelFiles`, no `RunPreSetupCommands` or `RunPostSetupCommands`, and
  nothing naming the index files `DRVIDX.BIN` or `DRVDATA.BIN`; its
  `AddReg` writes the Active Setup and update records, `SysTray.Exe`
  under `Run`, and the `USB20U` uninstall entry.
- The section runs through `ADVPACK.DLL` and the thunk pair
  `W95INF32.DLL` (which exports `GenInstall32@20`) and `W95INF16.DLL`
  (which imports `SETUPX`): the setup engine's own INF install, copying
  the files with its copy engine. `CSETUP.EXE` in the cabinet is a
  catalog registration program (its strings: "CAT File registration
  program", `MSCAT32.DLL`, `CryptCATAdminAddCatalog`) that neither the
  run command nor the INF names.

So no explicit index-rebuild step was found in NUSB's installer: it
places the INFs in the INF directory through the setup engine's own INF
install, and nothing of it names the index. What ADVPACK and the setup
engine do inside that install beyond copying the files, and how the
engine's index comes to hold the INFs, are not established by this
reading. That the engine takes an INF placed in that directory into its
index by itself is the inference NUSB's silent first plug suggests, and
it is read here (the legs below). "The driver database rebuilt as
NUSB's own installer does" is therefore taken as the same placement,
the engine's own update of its index, and a restart at the end of the
run.

**What this installer does.** On the same-bytes route only (section
6.5.4), after every binary of the seven is read in place and `Source`
is written, and before the package's copy: each of the two INFs with no
file of its own name in `%windir%\INF` is placed there under that name
(`inst_stor_inf_set`, host-tested: the route admitted, and the name
free). The steps are `stor_place`'s: `<name>.TMP` beside it made with
`CREATE_NEW` from the download's file and compared byte for byte, its
`Placed` value written (section 6.5.6), then `MoveFileA` to the name,
which fails when the name exists; a step that fails deletes the `.TMP`
and the `Placed` value and leaves what is there. A placement is a
publication of the pack, so the first one also writes `Published` 1
(section 6.5.6), the adoption's evidence, whatever happens to the
package's copy afterwards; and because a run can stop between the move
and that write (or the write can fail), the adoption also takes a
`Placed` copy that is still in place with its recorded bytes as that
evidence, with `Published` absent (section 6.5.8 step 1). **After the placements** the states of the
seven are read again (section 6.5.3): if the same-bytes route no longer
holds - a file of other bytes appeared under an INF's name between the
reading and the move, or anywhere else of the seven - the run takes
the storage part as the partial-other case: `storCopyOk` is cleared, so
the withdrawal of section 6.5.4 runs before the package's copy (the
placed INFs deleted under its protections, the directory off
`DevicePath`), and a step is left (exit 4). While the route still
holds, a placement that failed is a note: the run says that the first
plug of a new USB disk may still open Windows' wizard, which finds the
driver in the install directory (not a step: the route through
`DevicePath` stands as in draft 18). A
file of the INF's name already in `%windir%\INF` is never written over,
and on this route it is the download's bytes or this installer's (any
other bytes make the case partial-other, section 6.5.3), so it is
already where NUSB's installer would have put it and nothing is copied.
Nothing goes to `INF\OTHER`, no `OEM Name` value is written, no `.PNF`
is made by the installer, the index files are never touched, no setup
engine routine is called (`DiBuildDriverIndex` and
`DiAddSingleInfToDrvIdx` stay unused, record 27 section 2), and nothing
is written to `Options\Cabs` or to the Windows source path.

Why a file copy and not the setup engine's `GenInstall`, as NUSB's
does: `GenInstall`'s `CopyFiles` replaces a file already present by the
engine's version rules, so the no-overwrite rule would rest on a flag
of an INF of this package's own rather than on `MoveFileA`; it would
need a new helper job (record 27); and both leave the same bytes under
the same name in the same directory. Whether the engine takes an INF
that arrived by a file copy into its index as it takes one its own copy
engine placed is the reading below.

**The restart, and the plug.** The run that places them changed the
system, so on 98 SE it ends with section 5.9's restart (NUSB's package
leaves its restart to IExpress's own handling, its `REBOOT` resource
zero; what that asks for is not read here). The package directory keeps the seven and stays on
`DevicePath` (section 6.5.4), and the present devices are installed by
the helper from it as in draft 18 (section 6.5.5): the placement changes
only what Windows finds at a later first plug. An INF in `%windir%\INF`
has no `OEM Name` record naming where its files came from; the files are
at their destinations with the same bytes. Whether the engine still asks
for a file at that plug, and which path its prompt offers, is a reading;
the Finish page and the console's end say once, on 98 SE when an INF
was placed in the run, "If Windows asks for a file such as USBSTOR.SYS
when a USB disk is first plugged in, type <the install directory>."

**An update over its own install** reads a placed INF whose bytes are
the recorded ones as this installer's (section 6.5.3) and places
nothing again; one deleted since is placed again on the route. The
complete case (section 6.5.4: all seven present, not this installer's,
no `Source`; NUSB installed is that case) places nothing. **The
withdrawal** deletes the placed INFs (section 6.5.4) and **the
uninstall** deletes them in section 6.5.8 step 3, each with its `.PNF`,
under the rules of the recorded copies there. The residual of section
6.5.4 is the same for a placed INF as for the package directory on
`DevicePath`, and the withdrawal covers both.

**What the legs read** (43.5's first leg taken again on this design's
cut; the agent of those legs runs them, record 27 section 12's kit):

1. after the install and its restart, a new Bulk-Only stick installs
   with no wizard; its driver key's `InfPath` (`USBSTOR.INF` expected,
   the placed copy) and the unit's (`USBNTMAP.INF` expected, "USB Disk"
   at the first plug rather than "Disk drive");
2. whether any file prompt appears at that plug, and the path it offers;
3. a second new stick: no wizard either;
4. a stick plugged in the same boot as the install, before the restart:
   recorded as found (wizard or not), not a requirement;
5. the uninstall leaves neither INF nor its `.PNF` in `%windir%\INF`;
6. NUSB installed first (the complete case): nothing placed.

If the first does not hold - the engine does not take a copied INF
into its index, or not until something else happens - the code stops
there and the owner chooses; the candidates are the helper running the
setup engine's `GenInstall` over a section of an INF of this package's
own that copies the two INFs to dirid 17 as `_NUSB.INF` does, and
accepting the wizard with 43.5 amended.

#### 6.5.12 The silent install with the storage row (task 43.42, option A; draft 22)

**What the readings showed.** 43.5's first leg taken again on draft 19's
cut (2026-10-22) still raised the Add New Hardware wizard for a new stick,
and so did a 98 SE with NUSB 3.3 installed in full: with `USBSTOR.INF` and
`USBNTMAP.INF` in `%windir%\INF`, a new Bulk-Only devnode behind this bus
still brings the wizard, which finds `C:\WINDOWS\INF\USBSTOR.INF` with no
file prompt. So the placement alone does not give a silent first plug.
The route this package documents for installing a new devnode of a class
with drivers with no wizard is the driver's own silent install:
`XhciSilentInstall` 1 and the device's interface in
`HKLM\System\CurrentControlSet\Services\USB\SilentClasses` (design record
13 section 6.11; the release notes, "The silent install"); that it gives
the storage row a silent first plug is expected, and is the legs'
reading. The owner's decision (2026-10-22): option A.

**What the run does**, on 98 SE when the storage row's same-bytes route is
taken (section 6.5.4: `storCopyOk`), after the INFs' placement and its
reading (section 6.5.11):

1. **The list.** The three Bulk-Only entries of `USBSTOR.INF`'s generic
   lines (the ids `inst_stor_kind` reads, section 6.5.5), each a 4-byte
   `REG_BINARY` of record 13 section 6.11's form: `c3 08 02 50`,
   `c3 08 05 50` and `c3 08 06 50` (control `0xC3`: a device the bus does
   not split and a composite function, subclass and protocol compared;
   the last is `src\silent.reg`'s own storage entry). Each is added to
   `SilentClasses` only when no value there already holds those four bytes,
   under a name of its own (`XHCI98S1`, `XHCI98S2`, ... the first not in
   use; the driver ignores the names), never over a value of that name.
   Each value added is first recorded under the `Storage` record's
   `Silent` subkey (its name, and its four bytes as hex), then written; a
   write that fails deletes the record's value and is a note. The key is
   created when it is not there. The driver keeps 32 entries
   (`XHCI_SILENT_MAX`); a list already full keeps its own first 32, and
   the run says so.
2. **The gate is the user's** (owner, 2026-10-22, in place of draft 22's
   preselection): `XhciSilentInstall` is not turned on by the row and
   stays as it is unless the user picks it - on the USB settings page,
   by the console's question or by `/set` - and an explicit 1 still
   merges the whole `SILENT.REG` list as record 23 section 8.4 says.
   With the gate off, the entries the row added change nothing and a
   new stick raises the wizard as before; with it on, they are what
   lets a Bulk-Only device install with no wizard (the legs' reading).
   When the run took the route and the gate is not on afterwards, the
   Finish page and the console's end say once: "USB storage: Windows'
   wizard still opens for each new USB disk unless the silent install
   (XhciSilentInstall) is turned on in the USB settings." (a note, not a
   step).
3. **The INF placement of section 6.5.11 stays**: a silent install is
   expected to take the class's driver from the index with no wizard, so
   an INF the index holds is expected to let it find `USBSTOR.INF` with no
   prompt; whether the copied INFs are indexed, and which copy is chosen,
   are the legs' readings.

**The uninstall** (section 6.5.8): each value the `Silent` record names
is deleted from `SilentClasses` when it still holds the recorded bytes,
and its record value goes then; a value found absent, or read and found
changed (left and said), has its record value go too; a record or a list
that cannot be read, or a deletion that fails, keeps the record value,
stops the uninstall there and keeps its Add/Remove Programs entry for the
next run (section 9.3's incomplete uninstall). The key and every value it
did not add stay. This runs whether or not NUSB is
installed (the entries are this installer's own). `XhciSilentInstall`
machine-wide goes with the other machine-wide values the uninstall
removes (section 9.3). The `Silent` subkey goes with the `Storage` key.

**Not done.** The other classes of `src\silent.reg` (hubs, boot keyboards
and mice) are not added: they are the user's choice through
`silent-on.bat`. The storage units (`USBSTOR\GenDisk`, the "USB Disk"
binding of `USBNTMAP.INF`) are not USB devnodes and no `SilentClasses`
entry covers them; whether the first disk's unit installs with no wizard,
and lands on "USB Disk" rather than "Disk drive", is the legs' reading.

**Interactions.** Roadmap task 43.6 drops `XhciSilentInstall`'s
caution ("turn it on only once each listed class's drivers have been
installed at least once"); the storage row is meant to supply that
condition for the storage class itself (the files in place, the INFs
placed for the index; whether they are indexed, and the first plug, are
the legs' readings), and until 43.6 is done the caution is still printed
when the user turns the gate on. Task 43.43's case 1 (the row elsewhere)
adds no entries.

**The legs** (43.5's first leg taken again on this draft's cut): with the
gate left as it is (the default; the window untouched, the console's
`/yes` and `/defaults`), the entries are added, a new stick still raises
the wizard and the Finish note says why; with the gate turned on by the
user (the USB settings page, or `/set XhciSilentInstall=1`) and Windows
restarted, a new Bulk-Only stick installs with no wizard, on a SuperSpeed
and a High Speed port, and a second stick too; a subclass 02 or 05 device
where one is at hand; the unit's binding and whether it raises a wizard;
the uninstall removes exactly the entries the `Silent` record names and
leaves every other value (the whole list an explicit 1 merged stays); and,
on a run that never turned the gate on, the uninstall leaves
`SilentClasses` as it found it.

## 7. Windows 98 SE and ME: the helper, and the steps page

On 98 SE and ME the engine runs record 23 section 6's route (the copy, the
`DevicePath` entry, the install directory) and then, in place of the
printed steps, calls record 27's helper for each device of the plan whose
action is install, update or a forced reinstall, in record 27's order (the
controller; after the settle, the root hub and the hubs and composite
entries that still have no driver; on 98 SE with the storage drivers
ticked, Bulk-Only disks with no driver; UAS disks; on 98 SE, after a
settle, the storage units of section 6.5.5; the NHI; Stor98's devices
when ticked), one device per call, reading each verdict from the helper's
result file (record 27 section 5). The Progress page shows each as a step.

**Windows' own wizards.** After the controller's install starts the
driver, Windows finds the root hub and, behind it, the hubs and devices,
and opens its own hardware wizard for each it cannot install silently (the
silent install of `XhciSilentInstall`, off by default, is the user's
choice on the USB settings page, with its caution). The installer never
races those wizards (record 23 decision D13 holds here too): it waits the
settle of record 23 section 5.8 on the 98 SE and ME tree (the same state
machine, its reading of the tree through `cfgmgr32.dll`), shows "Windows
is installing the devices the controller shows; answer its windows: Next,
Next, Finish, and type `C:\XHCI98` where it asks", and only then reads the
tree again and installs, through the helper, what still has no driver (a
wizard the user cancelled) or an older one (an update's root hub). Within
a pass the same settle follows each root hub's, hub's or composite
entry's job before the next one, and a device such a job hid for a moment
is waited for and tried once more before it is a step (record 23 section
5.8, revision 14). How
the wizards and the helper meet is a reading of the 42-V.1 legs (record 27
section 12), and the design holds to this order until they are read.

**The steps page** (owner, 2026-10-18). A device the helper could not
install (its result file says so, with the engine's error), or whose own
wizard the user cancelled and the helper's retry failed too, gets a page
after the progress and before the finish: the device's name, what went
wrong in one line, and that device's Update Driver steps as record 23
section 6.4 prints them (the numbered list with the folder to type), with
a "Save as text file..." button (`GetSaveFileNameA`, the default name
`XHCI98-STEPS.TXT` in the install directory) so the steps survive the
restart, and "Open Device Manager" (record 23 section 6.4's
`control.exe sysdm.cpl,,1`). The rest of the install completes: the
other devices, the values, the tools, the record. The exit code follows
record 23 section 7.4's precedence: a wizard the user cancelled, a helper
dialog the user cancelled or a device the helper found no match for sets
the steps flag (exit 4); so, since draft 16, does a helper that did not
finish (`inst_di_unfinished`): its install is unfinished, its steps say
to shut down, start Windows and run the installer again, and never
"nothing to do" or "this release runs it", whatever the device reads
afterwards (the 98 SE update of 2026-10-19 reported a helper cut short
that way, exit 2); a helper that failed, or a
device read back wrong after it (section 8), sets the failed flag (exit 2)
with the steps page shown as well; the finish page says to restart after
the steps in either case, and which it was.

**Two texts of the steps** (draft 19, from 43.5's first leg,
2026-10-20). The 98 SE sentence that any disk, UAS included, needs
NUSB's mass-storage component was printed after the printed steps
whatever the storage row had done; it is printed only where the storage
drivers are not taken care of on this machine (`stor_covered`: this
run put the seven in place, or found all of them installed), on 98 SE
alone, and names both ways: "the installer's row for them, or NUSB's
mass-storage component", as the steps' other storage sentence does.
And where the reading finds no xHCI controller at all - no devnode in
the tree and no `Enum\PCI` entry with no driver (record 23 section 6.3;
on that leg the log listed neither, after the boot's New Hardware wizard
for the controller had been cancelled) - "No device of this package is
present" is followed, on 98 SE and ME, by what to do: "No USB 3
controller is listed by Windows now. If its New Hardware wizard was
cancelled at this start, restart: the wizard comes back; finish it
(Next until Finish; where it asks for the xHCI98 driver disk, type <the
install directory>, and where it asks for a Windows file, the Windows CD
or the folder of its setup files), then run the installer again." The plan's own empty line ("none
present") is followed by the same advice in its before-the-run form
("If its New Hardware wizard was cancelled at this start, go on: after
the restart the wizard comes back; finish it ..." with the same two
answers, the install directory named as "the folder the tools go to"),
so the console's plan and the window's Found page, whose detail text is
the plan, say it before anything is done.

The keyboard-and-mouse warning of record 23 section 5.8 stands on the
Found page unconditionally when a controller changes, and on 98 SE and ME
the summary repeats it, because the wizards after the controller's restart
need a keyboard and mouse that do not go through this driver.

## 8. The engine's reading of a device after the helper

Record 23 section 5.9's result check is applied on 98 SE and ME as well,
from `3.1.0.0`: each device the helper installed is read again (the driver
key's `NTMPDriver` naming the group's `.sys`, `DriverVersion` or the
binary's version at the package's, the devnode present with no problem
code), and a device gone after its install, or left with a problem code,
is a failure as on NT, with record 23's exception for a device that
needs the restart (`DI_NEEDRESTART` or `DI_NEEDREBOOT` in the helper's
result, record 27 section 5: "installed, restart pending") and, on 2000
to 7 only, section 6.3's for a tunnelled Stor98 device awaiting
reconnection. A cancelled
wizard's entry of section 6.3 (recorded, not running now) is a step as
before.

Stor98's devices are read by Stor98's own binding, not this package's:
on 98 SE and ME its INF binds a device through `DevLoader` `*IOS` and
`PortDriver` `stor98.mpd` in the driver key, the port driver living
under `SYSTEM\IOSUBSYS`, so the check reads those two values and the
`.mpd`'s version there (the device viewer of record 24 already reads
`PortDriver` and `DevLoader` on 98 SE and ME to show what runs such a
unit, `xhciinst.c`, so the read exists; the check makes it a rule); on NT the `stor98` service and
`stor98.sys` as for this package's groups. The baseline and the readings
of section 6.3 use the same rule, and the uninstall's file step removes
the files Stor98's INF copies where its INF puts them, by the same rule.

## 9. The uninstall from Add/Remove Programs (task 42-A.3)

### 9.1 The entry

Written by every install and update, on every target, once the drivers'
part of the run has finished (after the results on NT, after the helper's
installs on 98 SE and ME) and the installer's own executables are in the
install directory (section 6.2):

```
HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\xhci98
  DisplayName      = "xHCI98 USB 3.x drivers 3.1.0.0"
  DisplayVersion   = "3.1.0.0"
  Publisher        = <the INFs' Provider string>
  InstallLocation  = <the install directory>
  UninstallString  = "<install directory>\XHCIWIZ.EXE" /uninstall
                     (XHCIWZ64.EXE on x64)
  NoModify = 1, NoRepair = 1 (DWORD; NT reads them, 98 SE ignores them)
```

One entry whatever was installed (owner, 2026-10-18): Thunderbolt support
and Stor98 are not entries of their own. 98 SE's and ME's Add/Remove
Programs list reads `DisplayName` and `UninstallString`, as NUSB's entry
shows (`tools\nusb36-extracted\_nusb.inf`: a display name and a
`RUNDLL32 ... LaunchINFSection` command); 2000 to 7 read the rest. The key
is deleted by the uninstall, last but for the directory.

From draft 8 the display name
follows what the machine has when the entry is written: with the `xhci98`
service read as not registered and either the `xhcicm` service registered
or this run one without the USB drivers (the connection manager alone,
record 23 section 4.5; the second for a run that staged the connection
manager with no controller present, which registers no service yet),
"xHCI98 Thunderbolt 3 / USB4 connection manager 3.1.0.0"; otherwise the
name above (`inst_arp_cm_only`, host-tested). The key, the
program it runs and the uninstall are the same either way, so moving
between the two rewrites the one entry. The owner's rule (2026-10-19,
task 42-A.21): "let user update existing driver by installing over it.
Should not have multiple entries in Add remove programs in Control
Panel." So every run, whatever components it installs over whatever is
there, is an update in place of the one key above, never a second key;
on x64 the entry is written by the 64-bit program into the native view
(the 32-bit window starts the x64 one under WOW64 and never writes the
registry there, section 4), so no `Wow6432Node` entry is made.

### 9.2 Running from the install directory

The entry names the copy in the install directory, which is in use while
it runs and must be removed by the run. So `XHCIWIZ.EXE /uninstall`
started from the install directory first copies itself (and on 98 SE and
ME the helper) to the temporary directory (`GetTempPathA`, a directory of
its own under it), starts that copy with `/uninstall /from <install
directory>` and exits (W6). The copy does the uninstall; on NT it
schedules its own directory for deletion at the restart
(`MoveFileExA`, delay until reboot, the files then the directory); on 98
SE and ME, where nothing is ever deferred and `WININIT.INI` is never
edited (record 23 section 9.5), the temporary copy is left and the finish
page says where it is (a few hundred kilobytes under `TEMP`, deleted by
hand or by a disk cleanup). The console tool's `/uninstall`, run from the
package as today, needs none of this: it is not in the install directory.
The engine refuses an uninstall started from a directory it is about to
remove without the `/from` relaunch, so a user who starts
`C:\XHCI98\XHCIINST.EXE /uninstall` by hand gets the relaunch too.

The uninstall needs no package (W12): it reads the system alone, the
devnodes, the INF copies by provider, the services, the files and the
records under `Software\xhci98`, so neither the install directory's copy
nor the temporary one checks a package layout, and the 98 SE and ME helper
it needs is the one copied beside it. Record 23 section 4.2's refusal
stays for an install and an update.

### 9.3 What it removes, in order

Record 23 section 9 as revised for 42-A.3, one step after another, each
gated on the one before, shown on the Progress page:

1. The warning (record 23 section 9.1) on the uninstall's welcome page,
   and the window's equivalent of the typed word: a check box "I have a
   keyboard and mouse that do not go through this driver, or I accept
   that the USB ones stop working" that enables the Remove button (W7).
   The console keeps `REMOVE`. Where the `xhci98` service is not
   registered and `xhcicm`'s is (draft 8: the connection manager alone,
   record 23 section 9.1's rule), the page's title, warning and
   box are the connection manager's: "Remove the xHCI98 Thunderbolt 3 /
   USB4 connection manager", record 23 section 9.1's second text, and "I
   have closed every program that uses a device connected through a
   Thunderbolt or USB4 port".
2. The pending-install check, the reading of the devices.
3. On NT: first the safe-removal mark service stopped and every mark it
   wrote removed from the units' `LowerFilters`, present or not (record 21
   section 7.4, revision 3: a removal that fails keeps `xhcicm`'s service
   and file and ends with steps left, since a value naming a filter no
   longer installed stops the device); then the filter hosts, the devnodes
   children first, the check, the
   INFs, the services, the files (record 23 section 9.2, unchanged but for
   that first step); the
   bundled Stor98's recorded devices (with the tunnel drives Windows bound
   after the run, adopted first by section 6.3's draft 12 rule), its
   staged INF and, when nothing else depends on them, its service and
   files first, as section 6.3's record says (children before the NVMe
   device's parent port, which is a filter host and stays).
4. On 98 SE and ME: record 27's removal for each devnode of the package,
   present or not, children first (on 98 SE the storage devices section
   6.5.8 records, units then Bulk-Only devices; then UAS
   devices, Stor98's recorded
   devices, the NHI, hubs and composite entries, root hubs, controllers
   last), through the helper, an entry left after its removal stopping
   the destructive steps below (record 27 section 9); then the engine's own part of record 23 section 9.4 (the
   `DevicePath` entry, the copy's files, the directories), then the INF
   copies the engine kept (`INF` and `INF\OTHER`, the rule of 9.2 step 4,
   now deleted rather than listed, each only after no devnode of the
   package is left, and with each the `OEM Name` value SETUPX recorded
   for it, record 23 section 6.1), then the driver files in `SYSTEM32\DRIVERS`, each
   deleted where it is not in use and otherwise left with a step and the
   restart flag (a running `xhci98.sys` whose last device was removed is
   unloaded by Windows; one still held is said); then on 98 SE the
   storage drivers' recorded INF copies and files, by section 6.5.8's
   rules. A removal the helper
   refuses or fails stops the destructive steps after it, as on NT, with
   "restart and run the uninstall again", which resumes from what is
   left.
5. The xHCI98 tools: the tray tool (record 23 section 9.5), the
   installer's own executables in the install directory and the helper
   (section 9.2's relaunch makes that possible), the Run value.
6. The records: the machine-wide values the installer wrote under
   `Services\USB` (every `Xhci*` and `XhciCm*` value the settings table
   names, which only this package's drivers, its helpers and the installer
   write, each deleted by name; the key itself, its `SilentClasses`
   subkey, `DisableSelectiveSuspend` and any value the table does not name
   are left, so a later install of this package starts from the INFs'
   defaults and nothing else on the machine is touched); the `Stor98` key
   when Stor98 was removed, and its `Devices` and `Settings` entries that
   were removed when it was not, and since draft 16 the whole `Stor98`
   key also when Stor98 itself stays (nothing of the package is left for
   it to describe; the 98 SE matrix leg of 2026-10-19 left
   `Software\xhci98` behind for it); `InstallDir`, `MoveFrom`, the
   `Software\xhci98` key when empty, the Uninstall entry.
7. The install directory and `%windir%\XHCI98` when empty.

Steps 5 to 7 run only when steps 1 to 4 left nothing for a later run:
an uninstall that stopped at a gate, or left a file in use for the next
run (a tray tool that did not close on 98 SE or ME, a driver file still
held), keeps the installer's executables, the helper, the records under
`Software\xhci98` and the Add/Remove Programs entry, whose display name
then ends "(incomplete: run again)", so the next run has its entry
point after the package is gone; the tray tool's Run value goes at once,
as today. Only a run that finds nothing left to do removes them.

**What it leaves** (record 23 section 9.3, and the owner's decisions of
2026-10-18): Windows' own `usbd.sys` and `usbui.dll`; other drivers'
devnodes; a filter host's devnode; a Stor98 the installer did not install;
and nothing of another USB stack present before the install is restored
(NUSB's, say): Windows finds the controller again at the next start and
installs what it has for it, which on an xHCI-only machine is nothing, as
before the install. The finish page says so in one sentence, and that the
USB keyboard and mouse on an xHCI-only machine stay dead until another
driver is installed.

### 9.4 The order of the last steps

The uninstall removes the installer's own executables after everything
else they are needed for: the helper after the last removal, the window
and console copies at the very end through the temporary copy, then the
directory. On NT a file still in use goes by the delayed deletion; on 98 SE
and ME a file still in use (a tray tool that did not close) is left and
reported with the next uninstall deleting it, as record 23 section 9.5
says today, and in that case the entry point stays for that next
uninstall (section 9.3).

## 10. Switches, exit codes, the log

The window takes record 23's switches where they mean something to a
window and refuses the rest with the console's refusal text on the welcome
page:

| Switch | The window |
|---|---|
| `/uninstall` | the uninstall wizard (section 9) |
| `/from <dir>` | the relaunch of section 9.2; refused from a package directory |
| `/to <path>` | preselects the tools folder; the page still shows it |
| `/thunderbolt`, `/stor98`, `/notray`, `/nouas` | preselect the components; the page still shows them (`/stor98` needs `/thunderbolt` or `/cmonly`, section 6.4) |
| `/cmonly` | (draft 8) preselects the USB drivers unticked on 2000 to 7; refused on 98 SE and ME with the console's text; on 8 and 8.1 changes nothing |
| `/storage`, `/nostorage` | (draft 17) preselect the USB storage drivers' box on 98 SE; `/storage` refused on any other system with the console's text, `/nostorage` changing nothing there (section 6.5.9) |
| `/set Name=Value` | preselects a setting (and makes it "changed", so it is written); the page still shows it |
| `/defaults` | as record 23 section 7.1: every current value kept and no value question asked, which in the window means the settings pages are shown with the values in force and nothing marked changed (a page left alone writes nothing) |
| `/force`, `/restage` | as record 23; the Found page shows what they add |
| `/debug` | `INSTALL.BAT`'s flavour choice, and the window's own when started by hand |
| `/log <path>` | the log's path |
| `/yes`, `/list`, `/nopause`, `/devices`, `/eject`, `/dir`, `/console` | console only: refused with "use INSTALL /console" |

The exit codes are record 23 section 7.4's, unchanged, so `INSTALL.BAT`
and the legs read them as today; the log is 7.5's, appended by the window
as by the console, with every page's answers recorded as the console's
answers are. A window closed by the user before Install is exit 1 with
nothing changed; closed during the progress, the cancel of section 5.8. Started by
`SETUP.EXE`, the window's code reaches no one (section 2, draft 15): a
script runs `INSTALL /console`, or `INSTALL` with the window's switches,
which waits.

## 11. The pure half and the host suite

`inst_core.c` gains, each host-tested in `test\test_inst.c`:

- the settings table's new columns: for every value of record 23 section
  8.1 and the `/set`-only values, whether it is asked or advanced, its
  default, its range or list with meanings (a list names values 0 to 4
  and may have a gap: `XhciFastPollFsLs` offers 0, 2, 3 and 4), the
  systems it applies to, its warning, and its location rule (machine-wide for the drivers' values,
  Stor98's two forms for Stor98's); the suite checks every asked and
  advanced row against the INFs of `src\` and `src\cm\` as today, which
  rows are shown greyed on which system (section 5.4; `inst_value_greyed`,
  `inst_value_only`, `inst_s98_greyed`, `inst_s98_only`), the component
  boxes' dependencies (`inst_s98_box_open`, `inst_uas_decide`, Stor98
  with the Thunderbolt support in `inst_s98_decide`), the Summary's
  headline (`inst_summary_head`), and every
  Stor98 row against the bundled Stor98's readme table (the row names and
  ranges, as `test\stor98-settings.txt` holds them and the cut holds the
  bundled readme, `src\stor98\docs\package-readme.txt`, to them, so a
  renamed value fails the build);
- the `DriverParameter` string's edit (parse the pairs, set one, write them
  back in order, keep unknown pairs);
- the component rules: which boxes are shown, their defaults (record 23
  section 8.5's decision for Thunderbolt, never-ticked for Stor98), and
  what each unticked box removes from the plan;
- the page texts and the steps-page text, rendered at their longest for
  the width of the dialogs (a page is laid out for 98 SE's 640 by 480 at
  the dialog's font; a text longer than its control scrolls, never
  truncates, and the suite checks the longest rendering against the
  control's capacity);
- the Uninstall entry's values and the relaunch's argument rule;
- `INST_FRONT`'s question records: that every question record 23 asks is
  asked exactly once per run by both front ends, in record 23's order,
  checked by a fake front end that records the sequence against the
  console's;
- (draft 17) the USB storage drivers of section 6.5: the seven's table
  (names, destinations, SHA-256s) against `legal-provenance.md` section
  5's; `inst_stor_state` for each state; `inst_stor_default` (none found
  ticked, any found unticked, a recorded choice kept either way);
  `inst_stor_found_text` for none, some (the "not known to be ready"
  sentence, and never "ready"), all and this installer's;
  `inst_stor_case` over every combination; `inst_stor_copy_set` (which
  binaries are copied in each case, none in complete and partial-other,
  never a present one); `inst_stor_kind` and `inst_stor_unit_takes` on
  the ids and driver keys of section 6.5.5 (a foreign `DiskDrive` binding
  left); the case with `Source` recorded (a partial-same install, then
  every file present, then an update: the route, not complete); and
  `inst_stor_remove` (which files are removed: a recorded file with its
  hash, no user and no `USB20U`, and each reason one is kept); the row's
  text at its longest within its control; (draft 19) `inst_stor_inf_set`
  (which INFs are placed in `%windir%\INF`: on the route only, and only
  where no file of the name is there) and `inst_stor_adoptable` with a
  copy held by `Placed`; (draft 20) `inst_comp_warnings` on every system
  and every combination of the four boxes, `inst_comp_warn_text` at its
  longest, `inst_usb_decide` with its refusals lifted, and the Components
  page rendered at its longest on each system.

`xhciinst\selftest.cmd` runs the four executables' `/selftest`: the
window's renders every page with made-up devices through its own dialog
templates off screen (created with `CreateDialogParamA` and never shown,
so the self-test runs on a build host with no desktop interaction) and
checks each control holds its text whole; the import tables of all four;
the switch refusals, the window's included (`/yes` refused, `/console`
refused).

## 12. What this record changes in record 23

Record 23 is revised in the same change as the code (task 42-A.2), and
this list is the whole of it:

- Section 2: the package layout of section 2 here; `INSTALL.BAT`'s
  `/console` rule; (draft 15, record 23 revision 22) `SETUP.EXE` at the
  root for people, `INSTALL.BAT` for the command line, and D1 revised.
- Section 4.2: the package-layout refusal applies to an install and an
  update; the uninstall and the device modes run without a package
  (section 9.2).
- Section 3: the two new executables and record 27's helper in the build.
- Section 4.4: the bundled Stor98 is copied and installed when the user
  ticks it, with the record of section 6.3; "not copied" becomes "not
  copied unless asked".
- Section 6.2 to 6.5: the helper in place of the printed steps for the
  devices it reaches; the steps kept for the device it cannot (section 7).
- Section 5.9: a tunnelled Stor98 device under the USB4 connection
  manager is "updated, awaiting reconnection" after its update, never
  a failure by its disappearance (section 6.3).
- Section 7.1: `/stor98`, `/from`, `/console`; 7.4: the final line's
  shutdown forms, finished and unfinished, on 98 SE and ME after a Stor98
  update, with the exit codes unchanged.
- Section 8.1: the Stor98 rows and the asked-or-advanced column; 8.3: the
  mixed state of two controllers kept; 8.4: the machine-wide location
  (42-A.17) and Stor98's locations; `/defaults` unchanged.
- Section 9: the Add/Remove Programs entry, the relaunch, the 98 SE and ME
  removal through the helper, the Stor98 removal when recorded, the
  machine-wide values removed, the last steps' order (section 9 here),
  and 9.4's "best effort, then the manual steps" rewritten around the
  helper.
- Sections 3, 5.1, 9.2 and 9.5: the safe-removal mark service (record 21
  section 7.4, revision 3) built, installed into `system32`, started,
  updated, stopped and removed by the installer; the marks removed as a
  gated step 0 before the filter hosts, with the inventory read again
  before `xhcicm`'s service and file go; the invariant that a mark
  exists only while the `xhcicm` service is registered, kept at the end
  of every run (tasks 42-A.3 and 42-A.5).
- Section 10 and 11: the engine file, the front-end table, the new suite
  cases, the self-test of the window.
- Section 13: D2 (the 98 SE and ME route) gains the helper; D10, D11 and
  D14 unchanged.

Draft 17 (task 43.4) changes record 23 by its revision 24, made with the
code: section 4.4 (the seven in the 98 SE package copy when the row is
ticked and the case allows it, read from the download's `NONFREE\`),
6.2 and 6.4 (the storage copies, the storage devices' jobs and the unit
rebind; the UAS Code 2 step taken by the copy when the row is ticked,
and the advice kept where it is not), 7.1 (`/storage`, `/nostorage`),
7.4 (the shutdown form for a UAS devnode at Code 2 before the copies),
9 (the storage removal of section 6.5.8) and 13 (D2 gains the row).

Draft 19 (task 43.42) changes record 23 by its revision 25, made with the
code: section 6.1 (the `INF\OTHER` bullet: the pack's two INFs placed in
`%windir%\INF` itself on 98 SE, section 6.5.11 here, the one INF this
installer ever writes there, never into `INF\OTHER` and never through
the engine's index), 6.4 (the storage sentence only where storage is not
taken care of, and the empty reading's steps, section 7 here) and 9 (the
placed INFs removed with the recorded copies).

Draft 20 (task 43.43) changes record 23 by its revision 26, made with the
code: section 4.5 (the table: the connection manager alone on 98 SE and
ME with a warning, the USB drivers opt-in on 8 and 8.1), 7.1 (`/usb`;
`/cmonly` on 98 SE and ME, `/storage` everywhere and `/stor98` on 8 and
8.1, each with its warning) and decision D18 amended.

Draft 11 (task 42-A.22) changes record 23 by its revision 16: section
4.4 (Stor98 copied only with the Thunderbolt support; `xhciuas` left out
of the 98 SE and ME copy under `/nouas`), 7.1 (`/stor98` needs
`/thunderbolt` or `/cmonly`; `/nouas`), 8.1 (`XhciSilentInstall` shown
greyed by the window on 2000 to 7).

Draft 8 (task 42-A.21) changes record 23 by its revision 13: section 4.5
(the connection manager alone), 4.3 (Windows 8 and 8.1, `RtlGetVersion`),
5.10, 7.1 (`/cmonly`), 8.5, 9.1, 9.5, 10 and 13 (D17 to D19).

## 13. Decisions taken in writing this (the orchestrator's, owner to confirm)

- **W1** The window is a second pair of executables, `XHCIWIZ.EXE` and
  `XHCIWZ64.EXE`, in `tools\xhciinst\` beside the console pair, over one
  engine file (section 2, 3). Not taken: one executable with a `/gui`
  switch (a console program that opens a window keeps a console window
  behind it on every target; a windows-subsystem program that prints to a
  console it has to attach to loses the console's redirection rules record
  23 section 7.6 reads).
- **W2** `INSTALL.BAT` starts the window unless its first argument is
  `/console` (section 2); for people, superseded by W17 (draft 15). Not taken: a second batch file (record 23
  decision D1's reason stands), or choosing by the presence of any switch
  (the Add/Remove entry's `/uninstall` and a user's `/to` want the window).
- **W3** The engine is single threaded; the window runs `RUN` on one
  worker thread, drives nothing else into the engine meanwhile, and is
  built on the multithreaded static runtime (sections 3.3 and 4).
- **W4** Under WOW64 the 32-bit window starts the x64 one and returns its
  exit code; the console tool keeps its refusal (section 4).
- **W5** The window restarts Windows on "Restart now" and shuts it down
  on "Shut down now"; the console tool never does either (section 5.9).
- **W6** The uninstall from the install directory relaunches a temporary
  copy of itself; on 98 SE and ME that copy is left in `TEMP` and said
  (section 9.2). Not taken: `WININIT.INI` (record 23 forbids it), a
  deletion by `INSTALL.BAT` (the package may be gone), or leaving the
  executables in the install directory (the entry promised to leave the
  machine as it was).
- **W7** The window's equivalent of the typed word `REMOVE` is a check box
  that enables the Remove button (section 9.3).
- **W8** Stor98 ticked installs on NVMe devices with no driver only, and
  updates a Stor98 of an older release, on every target; a device under
  another driver is left; what the installer installed is recorded per
  device, with the pre-existing Stor98 noted, and the uninstall removes
  only that and the shared parts only when nothing else needs them
  (section 6.3).
- **W9** The settings pages write the machine-wide key and show both
  locations; the console tool follows (section 5.4; the owner's 42-A.17
  decision applied to the installer).
- **W10** Stor98's settings asked: `StorWriteThrough` and `StorPollMode`;
  the rest advanced; written in Stor98's own two forms (section 5.6).
- **W11** The wizard is `PSH_WIZARD` with dialog-template pages and no
  owner-drawn control (section 4).
- **W13** Stor98's ownership is by first binding: a device the installer
  (or Windows, from its staged INF) first bound to Stor98 is recorded;
  one it only updated never is; its staged copies are recorded one by
  one; on 98 SE and ME an update of a Stor98 device needs the
  user's word that nothing uses the drive and ends with a shutdown, and on
  2000 to 7 under the USB4 connection manager it waits for the drive's
  reconnection, as Stor98's readme asks per system (section 6.3).
- **W14** An incomplete uninstall keeps its entry point, records and
  helper for the next run (section 9.3).
- **W15** (draft 8) The USB drivers' box can be unticked on 2000 to 7
  when the package carries the connection manager, and then ticks and
  locks the Thunderbolt box; on 98 SE and ME it stays ticked and locked,
  on 8 and 8.1 unticked and locked (section 5.3; record 23 D17).
- **W16** (draft 8) One Add/Remove Programs entry, its display name by
  what is registered when it is written, and the uninstall's warning and
  agreement by the same reading (sections 9.1 and 9.3).
- **W12** The uninstall and the device modes need no package: they run
  from the install directory's copy or the temporary one and read the
  system alone; the package-layout refusal stays for an install and an
  update (sections 2 and 9.2).
- **W17** (draft 15; the owner, 2026-10-19, "setup.exe by tonight")
  `SETUP.EXE` at the package root is what a person double-clicks: a
  32-bit `asInvoker` windows program that starts the window of the
  system's architecture through `ShellExecuteEx` with its own arguments
  and exits, 0 once started; `INSTALL.BAT` stays, unchanged, for the
  command line and scripts. It supersedes record 23 decision D1's one
  entry point (section 2).
- **W18** (draft 17, task 43.4) The row's no-overwrite rule is the
  same-bytes route: the pack's INFs reach Setup only when every one of
  the seven already present is byte for byte the download's or this
  installer's own recorded copy, so a copy Setup makes over a present
  file writes the bytes already there; with any other file present
  nothing is copied and the install is reported incomplete; with all
  seven present nothing is done (section 6.5.4). Not taken: the
  installer's own copies alone with the INFs kept off `DevicePath`
  (every device the user plugs later would ask for its driver), the
  helper's `DI_NOFILECOPY` (it reaches only the installer's own jobs,
  never a first plug), and the INFs placed in `%windir%\INF` (the driver
  index, record 27 section 2).
- **W19** (draft 17) The record's `Choice` is the row's default on an
  update, the detection's only on a first offer (section 6.5.7).
- **W20** (draft 18; the owner, 2026-10-20) The uninstall adopts the
  storage devices whose driver came from the pack's INF through this
  installer's copy, first plugged after the install included, by the INF
  copy's bytes and `OEM Name` value (section 6.5.8); devices of a hand
  install of other bytes are left.
- **W21** (draft 17) A storage file whose deletion fails keeps its
  record and the uninstall's entry point; the uninstall run after the
  restart finishes it. No `WININIT.INI` entry (section 6.5.8).
- **W22** (draft 17) A UAS devnode at Code 2 before this run's copies is
  not reinstalled; the run ends with the shutdown form, from run-28's
  reading that it started after the next cold boot (section 6.5.5).
- **W23** (draft 17) `/storage` and `/nostorage`, the first refused off
  98 SE, the second accepted everywhere (section 6.5.9).

## 14. Open, and the risks

- How Windows' own hardware wizards on 98 SE and ME and the helper's
  installs meet after the controller's restart is designed as a wait and
  a retry (section 7) and read on the 42-V.1 legs; record 27 section 12
  lists what those legs settle.
- `ExitWindowsEx` on 98 SE with a wizard still open, and on NT with a
  pending file deletion, is read on the legs; the window never forces the
  restart (`EWX_FORCE` is not used).
- The off-screen rendering of the window's self-test on a build host with
  no interactive desktop (a service session) is to be read when the gate
  first runs there; the fallback is the pure half's text check alone, said
  in the self-test's output.
- The Stor98 rows depend on the bundled Stor98's readme table; a change
  to its forms fails the build by design, and the fix is the table's
  (`test\stor98-settings.txt` and the installer's rows) in the same change.
- The 98 SE and ME uninstall's removal of the INF copies and driver files
  through the engine rather than by hand, and whether the controller comes
  back as an unknown device after the restart, is read on the legs
  (record 23 section 9.4's open point, carried here).
- (draft 17) The USB storage drivers (section 6.5) rest on readings 43.5
  takes on a fresh 98 SE image: whether Setup copies a file over one of
  the same bytes, and what it does with a file in use; where the engine
  copies `USBSTOR.INF` and `USBNTMAP.INF` from the package directory and
  under which `OEM Name` value; which INF Windows' generic "Disk drive"
  binding names; whether a first plug after the install lands on "USB
  Disk" or "Disk drive", with and without `XhciSilentInstall`; whether the
  unit rebind starts the unit or needs the replug; that a UAS devnode at
  Code 2 starts after the shut down; and whether a stock 98 SE needs any
  of NUSB's other files. Until each is read the design takes the cautious
  reading named where it is used.
- (draft 18) The owner's decisions of 2026-10-20 on the questions draft
  17 left open. A later plug may install from the pack's INFs on
  `DevicePath` over a file another package laid down after the run
  (section 6.5.4): accepted as designed, the next run withdrawing the
  pack. Storage devices Windows bound from this installer's copy after
  the run are adopted by the uninstall (section 6.5.8): changed, so the
  usual flow leaves none of the seven; a hand install of the very same
  bytes cannot be told apart, which is accepted. Also accepted as
  designed: a copy of the same bytes over a present file counts as no
  overwrite; a failed deletion finished by the uninstall run after the
  restart; the shutdown line for a UAS disk at Code 2; `/storage` and
  `/nostorage`; the page's fit at 640 by 480 read by tasks 43.10 and
  43.11; `DISKDRV.INF` read on 43.5's legs.

## 15. Files

| File | What |
|---|---|
| `xhciinst\xhciwiz.c` | the window: the pages, the worker thread, the WOW64 start, the restart |
| `xhciinst\xhciwiz.rc`, `xhciwiz.manifest`, `xhciwiz_res.h` | the dialog templates, the manifest, the icon (`xhcitr\xhcitr.ico`) |
| `xhciinst\inst_eng.c`, `inst_eng.h` | the engine: record 23's system half behind `INST_FRONT` |
| `xhciinst\xhciinst.c` | the console front end |
| `xhciinst\inst_core.c`, `inst_core.h` | the pure half (section 11) |
| `xhciinst\build.cmd`, `selftest.cmd`, `check-imports.ps1` | the four builds and the helper's; the self-tests; the import allowlists per executable |
| `xhciinst\INSTALL.BAT` | the launcher with `/console` |
| `xhciinst\setup.c`, `setup.rc`, `setup.manifest` | `SETUP.EXE`, the package root's starter of the window (draft 15) |
| `xhciinst\README.TXT` | staged as `tools\xhciinst\readme.txt`; the wizard first, the console second |
| `test\test_inst.c` | the host suite |
| `scripts\package\test-package.ps1` | (draft 17) the installer's table of the seven held to `package-common.ps1`'s allowlist |
| `scripts\package\make-release.ps1`, `test-package.ps1`, `scripts\build-driver.cmd` | staging, the mutation cases, the gates |
| `docs\contributing\design\23-install-upgrade-tool.md` | revised as section 12 says |
| `docs\using\release-notes.md`, `README.md` | "Installing" rewritten around the window; `/console` in every scripted example |
