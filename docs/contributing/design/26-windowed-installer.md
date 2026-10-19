# Design Record 26 - The Windowed Installer

Status: draft 11, 2026-10-19 (task 42-A.26, the owner's decision of that
day: the bundled Stor98 on drives behind a Thunderbolt or USB4 tunnel
only, and the uninstall's adoption of tunnel drives Windows bound after
the run; sections 5.3, 6.3, 6.4 and 9.3, and record 23 revision 16);
draft 9, 2026-10-19 (task 42-A.27, section 7: the settle after a
root hub's or hub's job within a pass, record 23 revision 14); draft 8, 2026-10-19
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
  user ticks it, the bundled Stor98 (section 6); it puts the xHCI98 tools in
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
<package>\INSTALL.BAT                   the one entry point, kept
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
- The icon: `xhciinst\xhciwiz.ico`, drawn for this project in the 16 VGA
  colours as XHCITR's is.
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
| xHCI98 USB 3.x host controller driver (`xhci98.sys`) and UAS driver (`xhciuas.sys`) | yes, but on Windows 8 and 8.1 (draft 8) | on 2000 to 7, when the package carries the connection manager (record 23 section 4.5; `/cmonly` preselects it unticked); on 98 SE and ME no, shown ticked and disabled; on 8 and 8.1 shown unticked and disabled | the drivers for the controller, its hubs and devices, and UAS disks; on 8 and 8.1 the sentence says Windows' own USB 3 driver runs the controllers there; unticked on 2000 to 7, the sentence under the box says the run leaves an installed `xhci98.sys` as it is and that the uninstall removes the whole package |
| Thunderbolt 3 / USB4 support (`xhcicm.sys`) | as record 23 section 8.5 decides: with a controller present yes, with none no | yes | the connection manager; its warning sentence (a device plugged into that port can reach the computer's memory) stands under the box, with "see `XhciCmApprove` on the Thunderbolt settings page" |
| Stor98, the NVMe driver (bundled) | never unasked (record 23 section 4.4; owner, 2026-10-18) | yes, and it needs the Thunderbolt component: the engine keeps it out of a run without it (owner, 2026-10-19; the box's place under that component is the window's) | shown only when the flavour directory carries it; the sentence "For NVMe drives behind a Thunderbolt or USB4 tunnel; the installer selects tunnel drives only." (section 6.3; draft 11: it no longer offers an internal drive) |
| xHCI98 USB Tree, started at login | yes | yes (the `/notray` of record 23 section 9.5) | the tray tool |
| The tools folder | - | - | an edit box with Browse, `C:\XHCI98` by default or the directory an earlier install recorded (record 23 section 9.6's default and checks; the failure text under the box, Next disabled until it passes); on 98 SE and ME the line that the package's drivers go there too and the 8.3 rule |

The component page is not shown under `/uninstall`. Unticking Thunderbolt
support does what record 23 section 8.5's N does; unticking the tray tool
what `/notray` does. Unticking the USB drivers does what `/cmonly` does:
the Thunderbolt box is then ticked and disabled (it is all the run
installs), the USB settings page is not shown, and the plan is made again;
ticking them again gives the Thunderbolt box back its own state.

### 5.4 The USB settings

The values of record 23 section 8.1 whose "Asked when" holds for this
machine (`XhciIntelPortSwitch` on a controller `XhciPswGate` admits,
`XhciInterruptMode`, `XhciForceBulkOnly`, and `XhciSilentInstall` on 98 SE
and ME), one row each: the value's name, one sentence of meaning, a combo
box with every allowed value and its meaning, the default marked, and the
value now where one is set. The caution of 8.3 stands under its row. A
value the user changes is written as 8.4 says; a value left as it is is not
written (the INF's don't-overwrite flag stays the rule).

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
(`XhciPollFineTimer`, `XhciSelectiveSuspend`, and from the owner's
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
each with its default, range and warning; design record 28's
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
`StorPollBusyUs`, `StorPollIdleMs`, `StorPollSpinUs`, and on 2000 to 7
only the two that readme marks not settable on 98 SE and ME, `gone` and
`idle`, each with Stor98's default, unit and range. Where they are
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
pair it does not know and the order it found. The page names Stor98's
readme as the authority, from which the table's meanings, defaults,
ranges, types and key names are copied at the version the package bundles
(`-Stor98Version`), and the host suite checks every row against that
readme's table, so a Stor98 release that renames or retypes a value fails
the build rather than the user. Every Stor98 value the installer writes is
recorded by name under the `Stor98` key of section 6.3, for the uninstall.
The page is not shown when Stor98 is unticked.

### 5.7 The summary

What will happen, in the console's confirmation words (record 23 section
7.4: install and stage, write settings only, or check only), then the
list: each device and its action, each component, each setting that will
be written with its location, the tools folder, and on 98 SE and ME the
sentence that Windows' own wizards may open for the devices the controller
shows after its restart. The button is Install (Remove under `/uninstall`,
section 9). Nothing is written before it.

### 5.8 The progress

A list of record 23's steps in order as `INST_STEP`s arrive (the copy, the
staging, each device, each barrier's wait, each value, the tray tool),
each with its state (waiting, running, done, failed, skipped, left for the
user), a progress bar over the steps, and the log's path. A wait shows
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
whichever front end ran.

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
that no wizard installs it unasked; the owner's rule stands (never
unasked). With the box ticked:

- **Before anything**, the run records what Stor98 the machine already
  has: whether a `stor98` service or driver key exists, and every devnode
  bound to it, present or not (NT: `Service` = `stor98`; 98 SE and ME: the
  driver key naming Stor98's files, as its INF binds them). Those are
  pre-existing, and the uninstall never counts them as this installer's.
  On 98 SE and ME `xhcicm.sys` makes `Services\stor98` itself for
  Stor98's link-state locator (its value `LinkState`), Stor98 installed
  or not, so there that key counts only with a subkey or another value
  (draft 11; the 98 SE legs of 2026-10-19 read a pristine machine as
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
  11; record 23 section 4.4's gate and place reading), never on one another
  driver runs (record 23 decision D3 applies to Stor98's devices as to
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
  device with no driver behind a tunnel, and updates one such device
  Stor98 runs at an older release, by the same route and rules as the
  package's own
  devices (record 27 section 6 step 6), with Stor98's own read-back after
  it (section 8).
- **An update of a Stor98 device** follows Stor98's readme ("Updating the
  driver"), whose two precautions are each for its own systems. On 98 SE
  and ME: nothing may be using the drive's volume, and the machine is
  shut down and started again afterwards rather than restarted with the
  drive attached (the readme records a volume coming back "not ready",
  and ME stopping with a VCACHE error, after a warm restart there). So on
  those two systems the Summary page lists each Stor98 device the run
  will update with a check box "nothing is using this drive" that
  Install requires, and the finish page offers "Shut down now" in place
  of "Restart now" (section 5.9); the console tool asks the same before
  such an update and ends with the shutdown line. On 2000 to 7 the
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
  key or a bound devnode; written once, never cleared); `Version` (the
  bundled release last installed); under `Staged` one value per INF copy
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
  present or not, is bound to Stor98 any more and `PreExisting` is clear;
  otherwise they stay, and the finish page says that a Stor98 this
  installer did not install was left. A device the user installed and
  this installer only updated keeps its binding, and two devices sharing
  one INF, service and binary, one installed by hand and one by this
  installer, lose only the installer's device; a staged copy that device
  still names (an update of the hand-installed device from the bundled
  release points its driver key at the newest copy) stays with it, by
  the reference check, and the finish page says so.
- **Tunnel drives Windows bound after the run** (draft 11; the owner's
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
  by the steps above. And at the install, on 98 SE and ME, a copy that
  appeared outside a Stor98 step (a wizard of Windows' own racing the
  helper) is recorded under `Staged` when its text names Stor98 and
  `PreExisting` is clear (`inst_s98_stray_inf_ours`); otherwise it is
  left and logged, as before.
- The x64 `stor98.sys` keeps Stor98's own test signature; the test-signing
  note of record 23 section 5.10 names it beside this package's drivers
  when the box is ticked, and `stor98-test.cer` is never imported (the
  bundled-Stor98 rule of `build-and-test.md`).

### 6.4 The console tool

Gains the same: `/stor98` installs the bundled Stor98 without asking (the
pair of `/thunderbolt`), the question "Install Stor98, the bundled NVMe
driver?" with Enter for N where the package carries it, and the Stor98
rows in its value questions (asked only when Stor98 is installed by the
run). Record 23 sections 4.4, 7.1 and 8.1 are revised (section 12).
Draft 11: the question's text says "For NVMe drives behind a Thunderbolt
or USB4 tunnel; the installer selects tunnel drives only", and that an
internal drive is left for a hand install; with the Thunderbolt
component out of the run (a No, or `/yes` with no controller) it is not
asked and `/stor98` installs nothing: the console says Stor98 needs that
component. The value questions are not asked when record 23 section
4.4's shared-file rule holds the settings.

## 7. Windows 98 SE and ME: the helper, and the steps page

On 98 SE and ME the engine runs record 23 section 6's route (the copy, the
`DevicePath` entry, the install directory) and then, in place of the
printed steps, calls record 27's helper for each device of the plan whose
action is install, update or a forced reinstall, in record 27's order (the
controller; after the settle, the root hub and the hubs and composite
entries that still have no driver; UAS disks; the NHI; Stor98's devices
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
the steps flag (exit 4); a helper that failed or did not finish, or a
device read back wrong after it (section 8), sets the failed flag (exit 2)
with the steps page shown as well; the finish page says to restart after
the steps in either case, and which it was.

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
   after the run, adopted first by section 6.3's draft 11 rule), its
   staged INF and, when nothing else depends on them, its service and
   files first, as section 6.3's record says (children before the NVMe
   device's parent port, which is a filter host and stays).
4. On 98 SE and ME: record 27's removal for each devnode of the package,
   present or not, children first (UAS devices, Stor98's recorded
   devices, the NHI, hubs and composite entries, root hubs, controllers
   last), through the helper, an entry left after its removal stopping
   the destructive steps below (record 27 section 9); then the engine's own part of record 23 section 9.4 (the
   `DevicePath` entry, the copy's files, the directories), then the INF
   copies the engine kept (`INF` and `INF\OTHER`, the rule of 9.2 step 4,
   now deleted rather than listed, each only after no devnode of the
   package is left), then the driver files in `SYSTEM32\DRIVERS`, each
   deleted where it is not in use and otherwise left with a step and the
   restart flag (a running `xhci98.sys` whose last device was removed is
   unloaded by Windows; one still held is said). A removal the helper
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
   were removed when it was not; `InstallDir`, `MoveFrom`, the
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
| `/thunderbolt`, `/stor98`, `/notray` | preselect the components; the page still shows them |
| `/cmonly` | (draft 8) preselects the USB drivers unticked on 2000 to 7; refused on 98 SE and ME with the console's text; on 8 and 8.1 changes nothing |
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
nothing changed; closed during the progress, the cancel of section 5.8.

## 11. The pure half and the host suite

`inst_core.c` gains, each host-tested in `test\test_inst.c`:

- the settings table's new columns: for every value of record 23 section
  8.1 and the `/set`-only values, whether it is asked or advanced, its
  default, its range or list with meanings, the systems it applies to, its
  warning, and its location rule (machine-wide for the drivers' values,
  Stor98's two forms for Stor98's); the suite checks every asked and
  advanced row against the INFs of `src\` and `src\cm\` as today, and every
  Stor98 row against the bundled Stor98's readme table (the row names and
  ranges, read from `docs\` of the pinned Stor98 release folder at build
  time by the suite, so a renamed value fails the build);
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
  console's.

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
  `/console` rule.
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
  `/console` (section 2). Not taken: a second batch file (record 23
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
- The Stor98 rows depend on the bundled release's readme table; a release
  that changes its forms fails the build by design, and the fix is the
  table's, with the cut's `-Stor98Version` bump.
- The 98 SE and ME uninstall's removal of the INF copies and driver files
  through the engine rather than by hand, and whether the controller comes
  back as an unknown device after the restart, is read on the legs
  (record 23 section 9.4's open point, carried here).

## 15. Files

| File | What |
|---|---|
| `xhciinst\xhciwiz.c` | the window: the pages, the worker thread, the WOW64 start, the restart |
| `xhciinst\xhciwiz.rc`, `xhciwiz.manifest`, `xhciwiz.ico`, `xhciwiz_res.h` | the dialog templates, the manifest, the icon |
| `xhciinst\inst_eng.c`, `inst_eng.h` | the engine: record 23's system half behind `INST_FRONT` |
| `xhciinst\xhciinst.c` | the console front end |
| `xhciinst\inst_core.c`, `inst_core.h` | the pure half (section 11) |
| `xhciinst\build.cmd`, `selftest.cmd`, `check-imports.ps1` | the four builds and the helper's; the self-tests; the import allowlists per executable |
| `xhciinst\INSTALL.BAT` | the launcher with `/console` |
| `xhciinst\README.TXT` | staged as `tools\xhciinst\readme.txt`; the wizard first, the console second |
| `test\test_inst.c` | the host suite |
| `scripts\package\make-release.ps1`, `test-package.ps1`, `scripts\build-driver.cmd` | staging, the mutation cases, the gates |
| `docs\contributing\design\23-install-upgrade-tool.md` | revised as section 12 says |
| `docs\using\release-notes.md`, `README.md` | "Installing" rewritten around the window; `/console` in every scripted example |
