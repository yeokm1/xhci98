# Design Record 25 - XHCITR, the USB and Thunderbolt Tree with Eject and Tray Icon

Status: revision 57, 2026-11-20 (section 28 items 10 and 11, the
owner's decision of 2026-11-20, revised: the "can run
faster" list's lines, the USB drive notice's names, the console's and
the recovery details' wraps, the diagnostics' NHI heading, the 98 SE
and ME storage refusal and the hold notice's, the recovery record's
and the plain tooltip's names cut
at a whole character too; xhcitr searched again for a byte cut of a
device's name);
revision 56, 2026-11-20 (section 28 items 9 and 10, the
owner's decision of 2026-11-20, revised: the "can run
faster" notice's names, the red icon's set and its tooltip cut at a
whole character too, by the same test);
revision 55, 2026-11-20 (section 18 item 4 and section 28
items 7 and 9, the owner's decisions of 2026-11-20, revised: the
same-version line reads "3.3.0.0 running; the installed
file is the same."; the tree's own label, and every other place
`tr_unit_label` serves, cut at a whole character as the tray's is);
revision 54, 2026-11-20 (section 28 items 3, 5, 6 and 7,
reviewed and revised: the tray's cut never splits a character of a
double-byte code page; the depth past which a report node's lines
pass 79 columns stated);
revision 53, 2026-11-20 (section 28 items 4 to 8, the owner's
answers of 2026-11-20, reviewed and revised: Settings' heading a
borderless read-only box that scrolls; the tray menu's device label cut
sooner, in proportion to the screen's width; each report node fitted to
its own width and the details pane's default size kept; the task
renumbered 50.39);
revision 52, 2026-11-20 (section 28, task 50.39, reviewed
and revised: every line of the details pane and of Save Report's own
text within 79 columns, fitted between words with no word changed;
what the sweep found, what layout cannot fit, put to the owner);
revision 51, 2026-11-20 (section 27 item 4, decided by the
coordinator under the owner's leave of 2026-11-20 to take the
recommended decision, reviewed and revised: `XHR_F_LIFETIME` moves from
`0x08` to `0x10`, because design record 38's `XHR_F_RETRY_NEVER` took
`0x08` (its section 15.1) on task 50.31's branch, merged before this
one; nothing else changes, and no released driver or tool served or
read `0x08` as the lifetime flag);
revision 50, 2026-11-19 (section 27, the owner's decision of
that day on the answer after a restart Windows refuses in the session,
given through the coordinator, reviewed and revised: where Windows
accepted the request, no new start was seen and Windows asks for a
restart of the computer or has marked the controller, the answer ends
with H1, a hint naming a program using one of its drives as a
possibility and the two next steps; on 2000 and later only);
revision 49, 2026-11-19 (section 27, two more decisions of the
owner that day, given through the coordinator, reviewed and revised: on
98 SE and ME a floppy letter - A: or B:, a removable drive - that no
devnode claims no longer refuses the restart, R13 staying for every
other unclaimed local letter, with the risk the owner accepted stated;
and C10 stays dropped, the owner's decision rather than revision 48's
choice);
revision 48, 2026-11-19 (section 27, the owner's decision of
that day, given through the coordinator, reviewed and revised: on 98 SE
and ME the restart is refused whenever a local drive letter is
unclaimed (R13), so item 7 step 4's port readings and class table, and
the refusals and texts that followed from them, are gone and stage 2 is
settled; the drive rule checked before the storage rule; a USB storage
device stopped with Safely Remove but still plugged in no longer
allowed, so the removal record and C10 are gone and R12 and R18 ask for
it to be unplugged; after a failed removal the parent rescanned once;
the legs on a guest whose every letter is placed);
revision 47, 2026-11-19 (section 27, reviewed and revised:
stage 1's run given a digest that no longer matches refuses with R9, as
item 6 says, and every run repeats item 5's checks after the shared
mutex; stage 2's rules for a letter no devnode claims tightened and put
to the owner, all of them superseded by revision 48);
revision 46, 2026-11-18 (section 27 revised before code for the
owner's answer of that day - every uncertain case refuses - reviewed and
revised: stage 2's reading of the controller for a letter no devnode
claims, superseded by revision 48; the section split into stage 1, 2000
and later, which may ship alone, and stage 2, 98 SE and ME);
revision 45, 2026-11-18 (section 27 revised before code for the
owner's answers of that day: on 98 SE and ME a letter no devnode claims
allowed on a reading of the controller, superseded by revision 48; a
stopped controller there not offered the action; the legs, strings and
known-limitation line to match);
revision 44, 2026-11-18 (section 27 revised before code, reviewed
and revised: the 98 SE and ME drive letters read in guests - every
protected-mode letter claimed by one devnode, every MS-DOS
compatibility-mode letter, the system disk among them, by none - with the
rule's consequence put to the owner; a placed letter on the controller
refused whatever the storage rule found; a devnode-less storage device
allowed only on this tool's own recorded safe removal (dropped in
revision 48); a controller
stopped after repeated failures not offered the action on 98 SE and ME;
the 9x preparation failures, an earlier restart mark and the Run As run
that cannot report back given their answers);
revision 43, 2026-11-18 (section 27 revised before code for the
owner's further answers of that day, reviewed and revised: the start
identity's conclusions positive only - a changed identity with the
controller started - with the sampling history, the counted attempt, the
door gate's synchronization and the answer's buffer tiers beside the
isolation and retry words; the answer's tables total, with exit codes
that need an accepted request; on 98 SE and ME every drive letter placed
or the action refused, the network case, storage decided from fetched
descriptors and ids or refused, a stopped controller restarted only from
a model taken before it stopped, and every check repeated just before
the call; Run As on 2000 and XP; the legs widened again);
revision 42, 2026-11-18 (section 27 revised before code for the
owner's answers of that day and a review: the stop-and-restart request
not described as Disable and Enable; on 98 SE and ME the action refused
while USB storage is on the controller, where Windows or its swap file is
on one of its drives, or where a drive cannot be placed, the latched
controller there gated on its reading; the tolerance clock dropped as a
proof, a small addition to the driver's recovery-status answer proposed
to number its starts, with request, attempt, completion and state told
apart; the answer composed of four lines from their own readings, an
unread restart flag unknown and an earlier one kept; every structure's
size; the list from a complete fresh walk, scrolling, every devnode in
its digest; keyboards and mice; the legs widened, T.27 to T.34);
revision 41, 2026-11-18 (roadmap-usb4 task 50.32's design
before code, section 27: the calls on 2000 and later and on 98 SE and ME,
each checked against the kit headers both executables build with; the
98 SE and ME remove and rescan read in guests under emulation and found
to work on both, so offered there, with Windows' refusal of a drive in use not to be
relied on; a restart proved by the driver's tolerance clock, not by
Windows' answer; the restart of the computer Windows asks for said and
nothing more; the elevation, the answers and their exit codes, the
strings as drafts, and legs T.27 to T.32);
revision 40, 2026-11-17 (section 26 item 11: "Windows completed
the restart request" told from the driver's recovery by reading the
recovery-status request again with the devnode, with a defined answer
for a controller still stopped and for a status that cannot be read;
the release-note line's restart sentence conditional on the 98 SE and ME
action; T.21's latch taken from the right legs; revisions 38 to 40
and section 26 were revisions 37 to 39 and section 25 on their own
branch until it met revision 37 below, whose section 25 is task
50.29's);
revision 39, 2026-11-17 (section 26 revised before code for the
owner's answers of that day and a review: the recovery status read by a
request of its own so the query's version 3 stays as it is, the place
"involved" rather than a cause, the restart's flags not taken for a veto
and the state refreshed before anything is claimed, containment rechecked
in the elevated process, the 9x restart a remove and rescan offered only
after its guest reading; decision T30 revised);
revision 38, 2026-11-17 (roadmap-usb4 tasks 50.30 to 50.32, a
design before code: a controller stopped after repeated failures shown
with its cause and its next try, hold reason 4, and the controller's
restart by Windows' own device restart, section 26; decision T30);
revision 37, 2026-11-17 (roadmap-usb4 task 50.29: every value of
each USB device's driver key, and of the devnodes below the device to
four levels whatever their enumerator, by one walk for Save Report and
the device report, read only, section 25; the names whose data is
withheld, the owner's rule of 2026-11-17, section 25 item 3, closing
section 13's point);
revision 36, 2026-11-15 (roadmap-usb4 task 50.7: the holds read
through a request of their own at every logging level, the fallback note
where no hold is shown, the hold notice's re-send, section 24 items 1, 4
and 5);
revision 35, 2026-11-15 (roadmap-usb4 task 50.5: a note under
the storage transport line of a drive with no transport, section 19 item
3a);
revision 34, 2026-11-14 (roadmap-usb4 task 49.43's legs: the
fallback note at a root port alone, since the bus reports no failed
enumeration at a hub's port, and a held port at logging level 0 as it
reads, section 24 item 4);
revision 33, 2026-11-13 (roadmap-usb4 task 49.37: the
connection manager's state snapshot, `BASE.CT<n>` and `BASE.CB<n>`
beside the controllers' files and in Save Report, through a sixth
request of the query device, section 23, with sections 4.2 and 15);
revision 32, 2026-11-13 (roadmap-usb4 task 49.35: a snapshot of
every controller xhci98.sys drives, one set each, section 15 item 12);
revision 31, 2026-11-09 (roadmap-usb4 task 48.31's follow-up:
on 98 SE and ME a unit's details read "Marked removable." or "Not marked
removable.", the devnode's `DN_REMOVABLE` as Windows reports it, no
longer "Marked removable by the Thunderbolt driver.", record 24 section
4.3 revision 9, section 4.2);
revision 30, 2026-11-09 (roadmap-usb4 task 48.17, was
TRSTORNOTE.1: the optional USB drive notice, off by default, a tick box
in the Settings window, section 7.8, with sections 11 and 22; revised
the same day after its legs: a refresh at the poll's first tick, the
re-send held while the Settings window is open, and XP's
shell counted as one that answers for the drive balloon);
revision 29, 2026-11-07 (roadmap-usb4 task 47.35, the owner's
choices of that day: the Settings window and XHCITR /set, the
drivers' registry values from Setup's own table on three tabs, with
the restart they need, section 22, with sections 7.3 and 9);
revision 28, 2026-11-06 (roadmap-usb4 task 47.34, the owner's
of that day: where Windows' firmware-table interface gives no `DSDT`, the
registry's copies; `MSDM` and `SLIC` never saved, on every system;
section 21 item 10); revision 27, 2026-11-03 (roadmap-usb4 task 46.21, the owner's
option B and XP route: on 98 SE and ME the DSDT and every SSDT read by
`xhci98.sys` when the tool asks, on 32-bit XP and 2000 the registry's
copies, section 21 item 9); revision 26, 2026-11-03
(roadmap-usb4 task 46.21: File > Save
Firmware Tables... and `XHCITR /acpi <folder>` save the computer's ACPI
tables, on 98 SE and ME through a fifth request of `xhcicm.sys`'s query
device, section 21, with section 4.2); revision 25, 2026-11-03 (roadmap-usb4 task 46.4, was TRSNAP.1:
File > Save Snapshot... asks only for the folder and says where the files
went in a short message, section 15 item 8); revision 24, 2026-10-28
(revision 24, roadmap-usb4 task 44.80:
the "can run faster" notice held to what this computer and the path
give, and Windows' own ports as an option, section 7.7 items 10 and 11,
with sections 7.2.1, 10 and 12 and record 13 section 8.12's version 3);
revision 23, 2026-10-27 (revision 23, roadmap-usb4 task 44.62:
the PCI and bridge diagnostics node, section 20, with sections 3.1,
3.2, 10, 11 and 12);
revision 22, 2026-10-27 (roadmap-usb4 task 44.61, the owner's
decision of that day: the root hub's count and the protocol list say "USB
3", not "USB 3.x", section 7.4's root-hub line);
revision 21, 2026-10-25 (revision 21, roadmap-usb4 tasks 44.8,
44.9 and 44.35: what a link runs as, the running driver's version and
the storage transport, sections 17 to 19, with sections 3.3, 10, 11
and 12 and record 13 sections 8.12 and 10.7);
revision 20, 2026-10-23 (revision 20, 98 SE's tooltip after a
long hover and the balloon a click is on, roadmap-usb4 task 43.72:
sections 7.1, 7.7 item 6 and 16 item 11, with sections 10 and 11);
revision 19, 2026-10-22 (revision 19, the red tray icon,
roadmap-usb4 task 43.57: section 7.2.1, with sections 7.1, 9, 10, 11
and 12, and record 13 section 8.12's failed flag);
revision 18, 2026-10-22 (revision 18, the "can run faster"
notice, roadmap-usb4 tasks 43.27 and 43.30, with the owner's decisions
of 2026-10-22: section 7.7, with sections 8, 10 and 11; record 13 section
8.4.1 is its driver half);
revision 17, 2026-10-22 (revision 17, the 98 SE pop-up,
roadmap-usb4 task 43.55: section 16, with sections 7.2, 11 and 12);
revision 16, 2026-10-21 (revision 16, the unexpected-removal
notice, roadmap-usb4 task 43.21, below; revision 15, Windows 2000's USB disk
class and its stopped mark, roadmap-usb4 task 42-A.28: sections 6.5 and
14; revision 14, XHCISNAP's reading
moved here, below; revision 13, the drivers' logging level, below;
revision 12, PCI configuration and xhci98's query request, below).
Revision 11 adds the not-removable unit's stop (below).
Revision 10 adds a per-user "Start at login" choice
(section 7.4, the owner's request of 2026-10-16), with an addendum on the
tree's labels (end of section 7.4). Revision 9 adds the drivers' logs
(below). Revision 7 adds Safely Remove for plain USB
devices (section 6.5), by the owner's decision of 2026-10-16 that reverses
decision T6; sections 1.3, 3.1, 7.1, 7.3, 11, 12 and 14 change with it.
Revision 6 was reviewed and revised early on 2026-10-16
(`63cbfdbe`); the code is written against both. Revision 8, the same day, adds the install
directory (see below). Written for the owner's instructions of
2026-10-16: the command-line `INSTALL /devices` and `INSTALL /eject` of
design record 24 are not easy to use, so a separate graphical tool shows
every USB and Thunderbolt 3 / USB4 controller and device with its details,
ejects a tunnelled device as Windows' own Safely Remove Hardware does, and
sits in the notification area. It ships in `3.0.0.0`, which the owner held
for it, as roadmap Phase 41 (`roadmap-usb4.md`). Section 12 lists the
decisions taken in writing it, each an orchestrator decision for the owner
to confirm unless it names the owner; section 13 what is open.

Revision 8, 2026-10-16 (the owner, after running the installer on a real
machine): XHCIINST installs the tool in an install directory the user
names, `C:\XHCI98` on the system's drive by default, instead of
`%windir%\XHCI98\` (record 23 section 9.6); sections 2 and 8 follow. On
98 SE and ME the package's drivers share that directory with it, named
by XHCIINST's lists, which never name the tool. The tool names no
directory of its own, so nothing in it changes.

Revision 9, 2026-10-16 (the owner): the tool shows the drivers' logs, so
a user can send one from a release install with no debug build and no
DebugView. `xhcicm.sys`'s query device answers a second request with its
log ring and counters (section 4.5); `xhci98.sys`'s log and counters are
read as `XHCISNAP` reads them, with no change to that driver (section
4.6); each controller has a "Driver log" node, and Save Report and
`/report` carry both logs (section 3.5). Sections 1.2, 3.1, 4.2, 7.3, 10,
11 and 12 change with it.
Revision 11, 2026-10-16 (the owner, for `3.0.0.0`): a Thunderbolt unit
that Windows does not mark removable is stopped by record 24 section
7.2's query-and-remove route (its revision 5), and the tool offers the
administrator rights that route needs when it has none (section 6.6):
"Stop as administrator..." starts the tool elevated for that one stop,
and only then. Sections 3.3, 6.2, 6.4, 7.3 and 12 change with it, and
section 3.3 words a Thunderbolt 3 router's link behind a USB4 host as
Thunderbolt 3. The tray and the window never run elevated otherwise, and
the manifest stays `asInvoker`.

Revision 12, 2026-10-16 (the owner): the tool shows PCI details Windows
does not, read-only, through this package's drivers: a "PCI
configuration" node under each NHI and each xhci98 controller (section
3.6). `xhcicm.sys`'s query device answers a third request with the
configuration space of the NHI, of each tunnel port and, on 2000 and
later, of every function below a tunnel port (section 4.7). `xhci98.sys`
gains a query request of its own on `\\.\HCD<n>`, framed as `xhcicm.sys`'s
log request is, that serves its log ring, its counter block and its
controller's configuration space (section 4.8; the owner's
logging-consistency decision of 2026-10-16, which reopens decision T3),
and XHCITR reads xhci98's "Driver log" through it too. Both drivers'
logging level defaults to 2 from this revision (the owner, 2026-10-16;
design record 08 section 13.2, amended). Sections 1.2, 3.5, 4.6, 10, 11
and 12 change with it.

Revision 13, 2026-10-16 (the owner, for `3.0.0.0`): a "Logging level"
item shows and sets both drivers' log levels - `xhci98.sys`'s
`XhciLogVerbosity` and `xhcicm.sys`'s `XhciCmLogVerbosity` - and
replaces `XHCISNAP -verbosity` and `SNAPON.BAT` (XHCISNAP still ships in
`3.0.0.0`, unchanged, and is retired in the next release). It is the one
driver setting the tool writes (section 7.5). A standard user on 2000 to 7 sets
it through a second mode of section 6.6's elevated run, `/setlevel`.
Sections 1.2, 1.3, 7.3, 11 and 12 change with it.

Revision 14, 2026-10-19 (roadmap-usb4 42-A.4, the owner's 42-0.3
decision of 2026-10-18): XHCISNAP is retired in `3.1.0.0` and its
reading moves into the tool: `XHCITR /snapshot`, `/dump` and `/probe`,
File > Save Snapshot..., and a "Snapshot" part at the end of Save Report
and `/report` (section 15). XHCISNAP.EXE and its SNAPON.BAT, SNAPGET.BAT
and SNAPOFF.BAT leave the package. Sections 4.6, 4.8 and 12 change with
it.

Revision 16, 2026-10-21 (roadmap-usb4 task 43.21, which was UNPL.1 and
earlier 42-A.8; the owner's request behind decision T9, and the owner's
decision of 2026-10-18 that it is NT-only): the tray says when a
Thunderbolt or USB4 unit was removed unexpectedly - Windows surprise-removed
a running device of it - from a count the child filter keeps, published in
the query's answer as its version 2 (section 7.6). The notice says the
unit was removed unexpectedly, never that data was lost. 98 SE and ME
have no child filter and show such a unit as disconnected, as before.
Sections 4.3, 4.4, 7.2, 10, 11 and 12 change with it.

Task 43.66, 2026-10-23 (from 43.28's legs, `runs/run-43.md`): the "can run faster"
balloon the shell did not show is sent again once (section 7.7 item 9).

Like record 24 it is not a Thunderbolt control panel: it approves nothing,
changes no security level, updates no firmware, and shows no secret or
authentication material.

## 1. What it is for

### 1.1 The problem

- `INSTALL /devices` and `INSTALL /eject` (record 24) are console
  commands. They are the only eject on Windows 98 SE and ME, and the owner
  finds them hard to use.
- No target system shows, in one place, what is connected to an xHCI
  controller or behind a Thunderbolt or USB4 tunnel, at what speed, with
  which driver and in which state. The tools that do part of it
  (Microsoft's usbview) do not run on every target, know nothing of the
  tunnels, and do not ship with this package.
- On Windows 2000 to 7 the user needs no administrator rights to stop a
  removable device with Windows' own applet; record 24's tool needs them
  on NT, because it asks `xhcicm.sys` for a fresh status record by writing
  a value under `HKLM\SYSTEM\...\Control\Class` (record 24 section 3.3).

### 1.2 Scope

- **XHCITR.EXE** (x86, every target) and **XHCITR64.EXE** (x64: XP x64,
  Vista x64, 7 x64), a Win32 GUI program separate from XHCIINST (owner).
  XHCIINST keeps `/devices` and `/eject`.
- One window, laid out like Microsoft's usbview (owner): a tree on the
  left, the selected node's details as text on the right, a status bar,
  and View menu check boxes that show or hide each category (owner:
  "everything, with check boxes").
- The tree shows every USB host controller Windows reports (xhci98's and,
  where present, Windows' own EHCI/UHCI/OHCI ones), their root hubs,
  ports, hubs and devices, and every Thunderbolt 3 / USB4 host interface
  (NHI) with its routers, ports, tunnels and the Windows devices behind
  each tunnel.
- Per USB device: speed, port, VID:PID, class, driver, problem code, power,
  and, behind a check box, the decoded device, configuration, interface,
  endpoint, BOS and string descriptors (owner).
- Eject of a tunnelled unit from the window and from the tray icon, with
  record 24's safety rules (section 6).
- A notification-area (tray) icon: a left click lists the units to eject,
  a double click opens the window, balloons announce plug, approval,
  disconnection and the outcome of an eject where the shell has balloons,
  the tooltip carries the last event where it has not, and on 98 SE the
  tool draws a pop-up of its own in the balloon's place (section 16,
  revision 17); it starts at
  login (owner).
- Runs as the logged-on user, with no administrator rights and no UAC
  prompt at start (owner); an action that needs rights asks for them then.
- A read-only query device in `xhcicm.sys` (section 4), so the tool reads a
  current status without administrator rights and gets the details the
  registry record lacks (link rate and width, liveness). `xhci98.sys` is
  not changed (decision T3).
- Since revision 9, both drivers' logs and counters, read live, in the
  window and the report (sections 3.5, 4.5 and 4.6).
- Since revision 12, the PCI configuration of the NHI, its tunnel ports,
  the functions below them and each xhci98 controller, read-only, in the
  window and the report (sections 3.6, 4.7 and 4.8); `xhci98.sys` gains a
  read-only query request for it and for its log (decision T3).
- An application icon (owner): `xhcitr\xhcitr.ico`, drawn for this project
  (section 9).
- The two drivers' logging levels, shown and set (revision 13, section
  7.5).

### 1.3 Not in scope

- Approving, rejecting or authorising devices; security levels; firmware.
- Changing any driver setting from the tool but the two logging levels
  of section 7.5 (the other registry values stay `tuning.md`'s, set by
  hand or by XHCIINST).
- Forcing a removal Windows vetoes, ever, or stopping a Thunderbolt
  device that is not a unit (record 24 section 4.2).
- Stopping a USB hub, a root hub, a controller, an input device (HID) or a
  composite device with an input function, a radio, or a USB device that
  section 6.5 does not offer (one with no disk or volume on it). Until
  revision 7 plain USB devices got details only (decision T6); section 6.5
  is what replaced that.
- Writing `LowerFilters` to repair a missing child filter (roadmap-usb4
  42-0.4 and 42-A.5, was EJRB.1); the tool shows the condition (record 24 section 4.3) and says
  what to do.

## 2. Where it lives

- Source: `xhcitr\` (new): `xhcitr.c` (window, tray, OS calls),
  `tr_core.c` / `tr_core.h` (pure: query and record parsing, descriptor
  decoding, the event diff, keys, text), `xhcitr.rc`, `xhcitr.manifest`,
  `xhcitr.ico`, `build.cmd`, `selftest.cmd`, `check-imports.ps1`,
  `README.TXT`. It links `xhciinst\inst_core.c` for record 24's pure
  rules (unit selection, protection, eject answers and texts) and carries
  its own port of XHCIINST's devnode walk and post-eject assessment.
- Driver: `src\cm\cm_query.h` (the answer's layout), `cm_query.c` (pure
  builder), `cm_qryos.c` (the query device).
- Package: `tools\xhcitr\XHCITR.EXE`, `XHCITR64.EXE`, `readme.txt`.
- Installed copy: `<install directory>\XHCITR.EXE` (or `XHCITR64.EXE`;
  the install directory is record 23 section 9.6's, `C:\XHCI98` by
  default, `%windir%\XHCI98\` before revision 8),
  started at login from
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Run`, value
  `xHCI98 USB Tree`, data `"<path>" /tray` (section 8).
- Host tests: `test\test_tr.c`; the builder's in the CM suite.

## 3. The window

### 3.1 Layout

- Title "xHCI98 USB Tree" and the package version, as `xHCI98 USB Tree
  3.4.0.0`, built from `src\xhci_version.h` (`TR_MAIN_TITLE`, task 45.4),
  so a screenshot or a report says which build it came from; the message
  boxes keep the plain name. A resizable top-level window: tree view (left),
  read-only multi-line edit with both scroll bars (right), a splitter
  between them, a status bar ("N USB devices, M Thunderbolt/USB4
  devices"). Size, splitter position and the View check boxes persist
  per user under `HKCU\Software\xHCI98\XHCITR`.
- Menus:
  - **File**: Refresh (F5), Save Report... (the whole tree with every
    node's details as text, for a bug report, the driver logs of section
    3.5 included), Exit.
  - **Edit**: Copy, Select All (the details pane). Copy with nothing
    selected copies the whole pane (as built; section 20 relies on
    it).
  - **View** (check boxes): Thunderbolt and USB4; USB controllers;
    Windows' own USB controllers (not xhci98's); Empty ports;
    Descriptors; Devices behind each tunnel; Auto Refresh.
  - **Action**: Safely Remove... (enabled on a unit or a node inside
    one, and on a USB device section 6.5 offers), Properties (Device Manager's properties of the selected devnode
    through `DeviceProperties_RunDLL` on 2000 to 7; disabled on 98 SE and
    ME).
  - **Help**: About (icon, version, the release it belongs to).
- A right click on a node opens the Action menu. Keyboard: arrows, F5,
  Enter = Properties, Delete = Safely Remove.

### 3.2 The tree

```
My Computer
  Thunderbolt and USB4
    <NHI description>  [ICM | USB4 software CM], <stage>
      Port 1 (bridge 00:1C.0): active
        <vendor> <model>  (Thunderbolt 3 | USB4 v1), approved
          PCIe tunnel: up  - <unit devnode description>
            <child devnodes: NVMe, USB controller, ...>
          USB3 tunnel: up
          DisplayPort tunnel: none
      Port 2: no device
  USB
    <controller description>  (xhci98 | Windows)
      Root hub
        [Port 1] <device description>  SuperSpeed
        [Port 2] <hub description>     High Speed
          [Port 1] <device>
        [Port 3] (empty)
  PCI and bridge diagnostics
```

The last child of "My Computer", since revision 23, is the "PCI and
bridge diagnostics" node (section 20); no View check box hides it.

A dock's own xHCI controller sits behind a PCIe tunnel; it appears under
the tunnel (as a Windows device) and, with its USB devices, in the USB
branch, marked "(behind <router name>)".

### 3.3 The details pane

- **NHI**: description, PCI ID, driver and version, connection manager
  (ICM or USB4 software CM), profile, stage and inert reason, security
  level and firmware version (ICM), liveness state, tunnel ports with
  their bridges, Device Manager status and problem code, and whether the
  shown state is current (section 4.3).
- **Router / device**: kind, vendor and model names, IDs, generation,
  link rate and width where the connection manager read them (USB4:
  10, 20 or 40 Gbit/s per lane, x1 or x2; under the ICM "not reported by
  the firmware"), route, approval, each tunnel's state, the unit's
  devnodes, whether Windows lists it in Safely Remove (record 24 section
  4.3) and the advice when not. Since revision 11 a unit whose capabilities
  say neither Removable nor SurpriseRemovalOK reads "Not marked
  removable", with section 6.6's route; on 98 SE and ME (revision 31) the
  unit reads "Marked removable." or "Not marked removable.", its
  devnode's `DN_REMOVABLE` as Windows reports it and never who set it
  (record 24 section 4.3: 98 SE and ME set the bit on nearly every
  devnode); and a Thunderbolt 3 router behind
  a USB4 host (generation Thunderbolt 3, or USB4 Version major 0) has its
  lane-adapter rate worded "Thunderbolt 3, 20 Gbit/s per lane, x2 (40
  Gbit/s)", never as a USB4 generation (`tr_link_text_gen`). Since
  revision 21 a USB4 router whose link runs as Thunderbolt 3 says so
  (section 17).
- **USB controller**: description, PCI ID, driver, `HCD<n>` name, root
  port count, problem code; since revision 21 the running driver's
  version against the installed file's (section 18), as on the NHI.
- **Hub**: hub descriptor (ports, power switching, compound, over-current),
  self/bus power, speed.
- **USB device**: port, connection status, speed (Low, Full, High,
  SuperSpeed, SuperSpeedPlus, or unknown), address, VID:PID, class,
  product and manufacturer strings, driver and driver key, problem code,
  current configuration, MaxPower; since revision 21 on an xhci98
  controller the storage transport the bus chose and why (section 19);
  with Descriptors on, every descriptor
  decoded field by field, then in hex. The BOS's SuperSpeedPlus sublink
  speeds are labelled what the device supports; the negotiated lane rate
  and count are "not reported" (decision T3).
- Every node: the devnode instance ID, status flags and problem code.

### 3.4 Refresh

- F5, a device-change notification (`WM_DEVICECHANGE`,
  `DBT_DEVNODES_CHANGED`, coalesced 500 ms), or Auto Refresh (every 2 s
  for the Thunderbolt branch only) rebuilds the tree. The selection and
  expanded nodes are kept by key (section 7.2's router keys, instance
  IDs, controller + port path).
- A refresh reads only what the connection information carries (the
  device descriptor, speed, address, open pipes). xhci98 answers the
  device and first configuration descriptors from its cache (record 13
  section 8.11), but Windows' own stacks may send a configuration request
  to the device, so on every controller the configuration, string and BOS
  descriptors are read only for the selected node with Descriptors on,
  and for Save Report.

### 3.5 Driver logs (revision 9)

- **Where.** A "Driver log" node is the first child of each xhci98
  controller (label "Driver log (xhci98)") and of each NHI ("Driver log
  (xhcicm)"). No other controller has one. A refresh adds the node and
  reads nothing; selecting it reads the log then (sections 4.5 and 4.6),
  and so does Save Report and `/report`, once per node.
- **What it shows**, in this order: whether recording is on and at which
  `XhciLogVerbosity` (xhcicm: `XhciCmLogVerbosity`); when it is off, why and how to turn it on; the
  counters; the log, oldest record first, as the driver wrote it, one
  `label=XXXXXXXX` record a line.
- **Recording off.** Reading a log writes nothing; the levels are set in
  section 7.5's dialog (revision 13). The node's advice:
  - xhci98, level 0 (the driver does not answer) or 1 (counters only):
    "To record the log, set xhci98's level to 2 in View > Logging level,
    then restart Windows." On 2000 and later the text adds "(as an
    administrator, or with Set as administrator...)". Until revision 14
    (section 15) it named SNAPON.BAT in the package's tools\xhcisnap
    folder.
  - xhcicm, level below 2: "To record the log, set the DWORD value
    XhciCmLogVerbosity to 2 under HKEY_LOCAL_MACHINE\<the NHI's driver
    key> (as an administrator on 2000 and later), then restart
    Windows.", with the key the details pane already shows as "Driver
    key", spelled out as the full path (`...\Control\Class\...` on NT,
    `...\Services\Class\...` on 98 SE and ME).
  - A value the driver refused (out of range) is said as refused, with
    the same advice.
- **Addresses.** Below level 4 neither node shows an internal address:
  xhcicm's answer withholds its address records' values (section 4.5) and
  xhci98's ring holds none. At level 4, from `3.5.0.0` (the owner's
  decision of 2026-11-13, "Show it as it is"; decision T15 as revised),
  both nodes show the log as the driver serves it, kernel addresses
  included, under "Recording:  on (XhciLogVerbosity 4)" in the same form
  as levels 2 and 3 and with no sentence added; the report carries it and
  the snapshot's dump at level 4 too. Up to `3.4.3.0` the tool withheld
  xhci98's level 4 ring and said to use level 2, and the report left a
  level 4 dump out; that message stays for an xhci98.sys before `3.5.0.0`,
  which serves no log at level 4 (`XHQ_F_WITHHELD` without
  `XHQ_F_ADDRESSES`). The raw extension image is never shown or saved
  (section 4.6).
- **Text.** Every byte outside printable ASCII, CR and LF is shown as
  `.`. The details pane and the report hold the whole of both logs
  (16 KiB of ring each) with room to spare.

### 3.6 PCI configuration (revision 12)

- **Where.** A "PCI configuration" node follows the "Driver log" node
  under each NHI and each xhci98 controller. A refresh adds it and reads
  nothing; selecting it reads then (sections 4.7 and 4.8), and so does
  Save Report and `/report`, once per node. Kept apart from the Resources
  section (Windows' own allocations): different nodes, functions and
  files (`xhcitr\tr_pci.c`).
- **What it shows**, per function: vendor and device, revision,
  subsystem (a type 0 header's, or a bridge's subsystem capability),
  class, header type; Command and Status with their bits named; the
  interrupt pin and line ("Interrupt pin INTA, line 10"); the BARs as
  read, raw, with their encoding (I/O, 32- or 64-bit memory,
  prefetchable) - never sized, since sizing writes configuration space;
  for a type 1 header the buses ("primary 06, Bus 07-08") and the I/O,
  memory and prefetchable windows ("Window E0000000-E00FFFFF", or
  "none"); then the capability list: MSI with its enable state and vector
  count ("MSI: present, disabled (1 vector)"), MSI-X with its table size
  and enable state, PCI Express with its device or port type and the link
  ("PCIe link: Gen 3 x4 (capable Gen 3 x4)"), power management with its
  state, and any other capability by id; "not present" for MSI, MSI-X or
  PCI Express where the list has none.
- **The walk.** The capability list is walked only where the Status
  register's Capabilities List bit is set; each pointer is masked to a
  DWORD, below 40h ends the walk as invalid, a pointer seen before ends
  it as a loop, and it stops at 48 entries (the most the 192 bytes can
  hold); a capability whose registers lie past the 256 bytes read is
  named and not decoded. Each case is said in the text.
- **The NHI's node** lists the NHI, each tunnel port (with its source
  and whether it is active), and below each port the functions found
  there, indented by bridge depth; on 98 SE and ME "Not read on this
  system" stands for them (section 4.7) where the driver does not read
  below the port (`CM_QP_F_BELOW_OS`, with its reason), and since
  roadmap-usb4 task 43.63 a port the driver walked with nothing below it
  is "None found." there as on NT (a 98 SE or ME driver has read below
  the port since task 42-A.14): the answer says so by
  `CM_QP_F_UNWALKED_SAID`, under which a member whose own bus numbers the
  walk did not admit has its own entry ("Not walked: PCI.VXD does not
  read the port's bus numbers ..."); from an older driver that walks,
  an empty port is "None listed", and in a cut-short answer
  (`CM_QP_F_TRUNCATED`) too, on every system. **An xhci98 controller's node**
  shows the controller.
- **Text.** Every line within 79 columns; the host suite holds it on a
  worst case. The constants are `tr_pci.c`'s own, each cited to the PCI
  Local Bus Specification 3.0, the PCI-to-PCI Bridge Architecture
  Specification 1.2, the PCI Bus Power Management Interface
  Specification 1.2 or the PCI Express Base Specification 4.0, by
  section; nothing is taken from a Microsoft header.
- **Older drivers.** A driver from before revision 12 is said as "Not
  served by this xhcicm.sys" or "Not served by this xhci98.sys: it is
  older than this tool". A configuration read the driver could not make
  reads "Unread".

## 4. The query device in `xhcicm.sys`

### 4.1 Why

The registry record (record 24 section 3) is readable by every user, but
only a fresh one, confirmed by an echo of a value written under
`HKLM\...\Control\Class`, is trusted for an eject, and that write needs an
administrator on NT. It also lacks link rate and width and the liveness
state. A device the tool opens with no access rights and asks with a
buffered, any-access IOCTL answers from the connection manager's own
published state, and works the same on 98 SE and on 7. Whether an
unelevated user can open it on every target is read by leg T.1 (section
13).

### 4.2 The device

- **A fourth role.** `xhcicm.sys` already serves the NHI FDO, the bridge
  filter and the child filter from one dispatch table, and passes every
  unrelated IRP of a filter down. The query device is a control device
  object with its own discriminator in the common extension; the dispatch
  routes every major function by object kind first, so the three existing
  roles behave exactly as before. The query device has no lower stack:
  `IRP_MJ_CREATE` succeeds only with an empty file name (anything after
  the device name is refused), `CLEANUP` and `CLOSE` succeed,
  `DEVICE_CONTROL` answers `CM_Q_IOCTL` alone (and, since revision 9,
  `CM_QL_IOCTL`, section 4.5; since revision 12, `CM_QP_IOCTL`, section
  4.7; `CM_QM_IOCTL`, record 21 section 7.4; since revision 26,
  `CM_QA_IOCTL`, section 21; since revision 32, `CM_QS_IOCTL`, section
  23), everything else fails with
  `STATUS_INVALID_DEVICE_REQUEST`, and PnP and power never reach it.
- **Name.** `\Device\XhciCm<n>`, linked as `\DosDevices\XHCICM<n>`, `n`
  the first free of 0..15, created in the start IRP's system context (so
  the link is global on XP and later), with `FILE_DEVICE_SECURE_OPEN` on
  NT and the system's default security. The name is published as
  `XhciCmQueryName` (REG_SZ) in the NHI's hardware key after the link
  exists and written empty before the link goes (not deleted:
  `ZwDeleteValueKey` has no Windows 98 evidence, section 14). The tool
  reads an empty value as no query device, and trusts the mapping only
  when the answer's instance ID equals the devnode it read it from, so a
  stale or recycled name never stands for another NHI.
- **Lifetime.** Created after the NHI's start has succeeded (removed again
  if a later start step fails). On stop, surprise removal and remove the
  driver sets `CM_Q_F_STOPPING`, deletes the value and the link, marks the
  device disconnected under its lock, drops the NHI's pointer to it and
  deletes it; an open handle keeps the object alive harmlessly and gets
  `STATUS_DEVICE_REMOVED` (`STATUS_NO_SUCH_DEVICE` on 98 SE and ME) with no
  data. The device holds no pointer to the NHI: its answer lives in its
  own extension. A restart creates a new device with a new epoch.

### 4.3 `IOCTL_XHCICM_QUERY`

`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA40, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222900`, no input; the answer's layout is `src\cm\cm_query.h`
(Windows-type free, shared by the driver, the tool and the host tests):
a 64-byte header (magic `XCMQ`, version 1, size, the published-pass
counter, the pass's age in ms, the liveness state, flags inert /
truncated / stopping, the offsets, the start epoch, the NHI's bus,
device and function, the offset and length of the NHI's instance ID),
then the status record byte for byte as `CmStatusBuild` builds it (record
24 section 3.1), one 32-byte extension per router entry (link rate per
lane, width, the lane adapter's link state and speed code as read, the
IDs the connection manager holds for a router the record has none for,
flags), then the instance ID. Since revision 16 the answer is version 2:
the same header, then section 7.6's removal block, then the record, the
extensions and the instance ID as before, each found by its offset.

- **Publication.** After each pass the connection-manager thread builds
  the whole answer on its own thread and copies it into the query
  device's extension under a dedicated spin lock held for the copy only,
  then counts the pass and stamps its time from a running clock, all three
  under the lock. It keeps the answer's own content generation
  (`CM_Q_O_CONTENT_GEN`), bumped when anything but the pass counter, its
  age and the record's registry-only fields (generation, uptime, time,
  echo) changes: the record's generation is stamped only by the registry
  write, which skips an unchanged record. It does not depend on the
  registry write succeeding. The IOCTL copies out under
  the same lock and computes the age at answer time. Neither ever holds
  the lock across registry, hardware or a wait; the extension fields are
  read by the connection manager on its own path (the USB4 CM reads the
  lane speed and width when it sets up a USB3 tunnel's bandwidth,
  `tbt_u4cm.c`), never by the IOCTL.
- **Sizing.** Under 64 bytes: `STATUS_BUFFER_TOO_SMALL`, no data. Under
  the whole answer: success with the header alone and the size needed;
  the tool asks again with that size, at most 64 KiB. Otherwise the whole
  answer. The tool parses no more than the bytes returned. The largest
  version-1 answer is 64 + 128 + 24 x 160 + 200 bytes; the tool's first
  buffer is 8 KiB.
- **Current.** The window marks the Thunderbolt branch "not current" when
  the pass is 3 s old or more, the inert flag is set, or the liveness
  state is not ACTIVE. Age is for display only: an eject never relies on
  it (section 6.2).

### 4.4 What does not change

The registry record (`XhciCmStatus`, version 1), the ask and echo, and
XHCIINST's use of them stay exactly as record 24 has them. Revision 9
leaves the query's answer (`XCMQ`, version 1) as it is. Revision 16 makes
it version 2 (section 7.6); the registry record stays version 1 and
XHCIINST, which never reads the query, is untouched.

### 4.5 `IOCTL_XHCICM_LOG` (revision 9)

A second request on the same device, not a version 2 of `XCMQ`, so the
eject path's reader, its checks and XHCIINST are untouched, and a driver
from before revision 9 answers `STATUS_INVALID_DEVICE_REQUEST`, which the
tool shows as "this xhcicm.sys does not serve its log".

`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA41, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222904`, no input, the same device, the same access rules (opened
with no access rights; a standard user may read), the same removed-device
answer. The layout is `src\cm\cm_query.h`'s `CM_QL_*`: a 64-byte header
(magic `XCML`, version 1, size, flags, `XhciCmLogVerbosity` applied and as
read, the start epoch, the offset and length of the counters and of the
log, the records accepted since the start, the bytes the ring's wrap
overwrote, and the pass the counters belong to with its age), then the
counters as text, then the log as text. Both texts are ASCII lines
`label=XXXXXXXX` ending CR LF, the ring's own record format.

- **The log is copied, not drained.** The ring stays as it was, so the
  tray, the window and a second program read the same log, and the
  release flavour, which drains only under `XhciCmLogDebugView` 1, keeps
  the last 16 KiB. Where the thread hands the ring to DebugView after
  each pass - the `qemu` flavour always (and the `debug` one until task
  45.45 retired it), and any flavour
  with `XhciCmLogDebugView` 1 since the owner's decision of 2026-10-16
  (record 21 section 8) - the copy holds what has not been emitted yet;
  the answer's `CM_QL_F_EMITTED` flag says so. That emission withholds
  the same address values below level 4 (`CmQueryLogLine`).
- **Under the ring's own lock.** The IOCTL takes the query device's lock,
  and, while the device is connected, the NHI's `LogLock` inside it, and
  copies the ring, oldest byte first, straight into the system buffer;
  then releases both. No path takes them in the other order (`CmLogNote`
  holds `LogLock` alone; the thread's publication holds the device's lock
  alone). The copy is at most 16 KiB, so a producer on the thread or at
  `DISPATCH_LEVEL` waits microseconds. The device keeps the address of
  the NHI's log and its lock, set when it is created and cleared under its
  lock in `CmQueryClose`, which runs before the NHI's extension can go;
  this is the one exception to section 4.2's "no pointer to the NHI", and
  it is only ever followed with the device's lock held and `Connected`
  set.
- **The counters** are formatted by the connection-manager thread after
  each pass, into a buffer of its own, and published with the status
  answer under the device's lock (section 4.3), so they are the counters
  of pass `CM_QL_O_PASS`: the NHI's (`cm.*`), the ring's and the ICM's or
  the USB4 connection manager's, the re-enumeration's (`cm.reenum.*`) and
  the child filters' attached, skipped and refused (until now written only
  at stop), and the liveness state and refusals, in the stop dump's names;
  on 98 SE and ME the running CONFIGMG worker's counts (copied under its
  lock) are added to those its predecessors left, which the stop dump
  adds only at the stop. They are served at every verbosity: they hold no
  address and no user data. With no thread (its creation failed) the
  counters are formatted once when the device is created, pass 0; when
  the thread waits with no timeout the answer says so
  (`CM_QL_F_INERT`), and its pass and age say how old they are.
- **The ring's lifetime.** The ring lives in the NHI's extension from
  the device's addition to its removal and keeps its records across a
  stop and a start (the stop dump included), so the log can begin before
  the epoch it is served under; `CM_QL_O_APPENDS` counts from the
  addition.
- **No address.** `xhcicm.sys` never calls `XhciLogAppendAddress`, but
  three records carry an address as their value: `cm.live.bar-changed`
  (the BAR as read, a bus address) and, on 98 SE and ME,
  `cm.reenum.9x.gone.superseded` and `cm.reenum.9x.gone.id-changed` (a
  CONFIGMG devnode handle, a pointer). Below level 4 the builder writes
  their values as `--------` and sets `CM_QL_F_WITHHELD`; at level 4,
  from `3.5.0.0` (the owner, 2026-11-13; section 3.5), it serves them as
  recorded, as the DebugView emission always did, and a driver before it
  withheld them at 4 too. A record added later whose value
  is an address joins that list (`cm_query.c`), which the host suite
  holds.
- **Text.** A byte of the ring outside 0x20 to 0x7E, CR and LF is
  written `.`. When the oldest bytes can be the tail of a record - the
  wrap has overwritten bytes, or, in a checked flavour, a drain's chunk
  (`cm_log.c`, 128 bytes, not aligned to records) took its head - the
  text starts after the first line feed and `CM_QL_F_LOG_CUT` is set, so
  no value is ever shown without its label (and the withholding, which
  goes by the label, never misses one).
- **Bounds and sizing.** Counters at most 8 KiB (`CM_QL_CTR_MAX`, cut at a
  line with `CM_QL_F_CTR_CUT`), the log at most 16 KiB (the ring), the
  answer at most 64 + 8192 + 16384 bytes (`CM_QL_MAX_BYTES`). Sizing is
  `XCMQ`'s: under 64 bytes `STATUS_BUFFER_TOO_SMALL`; under the whole
  answer the header alone with the size needed; the tool's first buffer
  (32 KiB) holds the largest.
- **Matching.** The tool asks for the log only on a query device whose
  `XCMQ` answer names the NHI (section 4.2), and keeps the log only when
  its epoch equals that answer's.
- **Every target.** The same code on 98 SE, ME and NT; nothing in it is
  NT-only, and the 98 SE and ME open rule of section 14 applies.

### 4.6 `xhci98.sys`'s log, read as `XHCISNAP` reads it (revision 9)

Since revision 12 the tool reads xhci98's log through its query request
(section 4.8) and comes here only for a driver that refuses it.

No driver change (decision T3 holds). The tool opens the controller as it
already does (`\\.\HCD<n>`, or its interface path) and sends
`IOCTL_USB_USER_REQUEST` with `USBUSER_PASS_THRU` and the snapshot GUID,
schema 5 (design record 08, `docs\contributing\snapshot-reader.md`),
exactly as `XHCISNAP`'s `take_window` (now `xhcitr\tr_snap.c`'s): a
window of the extension region at offset 0 for the header (the ring's
offset), then one window at the ring's offset that must hold the whole ring,
and the HCD region (version 1) for the
counter block `XHCIHC_COUNTERS`. The ring is decoded with the header of
the window that carried it - the door fills header and payload together
under the controller lock (`hcd_door.c`) - and its verbosity, offset,
size, head and used count are checked there; the first window's header
only says where to ask. Every count and offset is checked against the
bytes returned, as `XHCISNAP` checks them; a schema or region version the
tool does not know is shown as such, not decoded.

- **Level 0**: the door answers status 6 and nothing else; the node says
  the log is off (or the controller is not an xhci98 one, which the tool
  already knows it is) and how to turn it on.
- **Level 1**: the counters; "Recording is off" and the advice.
- **Levels 2 and 3**: the counters and the ring.
- **Level 4**: from `3.5.0.0` as levels 2 and 3, the counters and the
  ring with the kernel addresses it holds (`start.bar`, `start.ok`,
  `xhci_log.h`; section 3.5). Up to `3.4.3.0` the ring was withheld here
  and the node said to use level 2 (`XHCITR /snapshot`, `XHCISNAP`'s
  maintainer report up to `3.0.0.0`, was the level-4 reading; section 15).
- The counters are named from `XHCIHC_COUNTERS`'s own fields
  (`xhcitr\tr_hcctr.h`, one line a field, which the host suite holds to
  `src\xhci_counters.h` field by field by offset); a word past the table is
  "counter N". Nonzero counters only, as `XHCISNAP` prints them.
- Nothing of the extension image but the ring is kept, shown or saved.

### 4.7 `IOCTL_XHCICM_PCI` (revision 12)

A third request on the same device, as section 4.5's was a second:
`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA42, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222908`, no input, the same access rules and removed-device answer,
and `XCMQ`'s sizing rules (under 64 bytes `STATUS_BUFFER_TOO_SMALL`; under
the whole answer the header alone with the size needed). A driver from
before revision 12 answers `STATUS_INVALID_DEVICE_REQUEST`. The layout is
`src\cm\cm_query.h`'s `CM_QP_*`: a 64-byte header (magic `XCMP`, version
1, size, flags, the start epoch, the entries' offset and shape, the NHI's
location, the members listed, the reads made and refused), then one
288-byte entry per function: a 32-byte head (location, role - NHI, tunnel
port, below a port - state - read, not read on this system, refused,
absent - the port it is below, the bridge it is behind, its depth, a
port's source and flags) and the function's configuration space 00h to
FFh as read. The tool decodes it; the driver interprets nothing but the
header type and bus numbers its walk needs.

- **Read at the request**, on the caller's thread at `PASSIVE_LEVEL`,
  through `HalGetBusData` from offset 0 - the reads `cm_reenum.c` and
  `tbt_topo.c` already make of the same functions on every target - and
  never written. The functions to read are what the thread published with
  its last answer (the NHI's location and its domain's tunnel-port set,
  copied under the device's lock in `CmQueryPublish`); the reads hold no
  lock and follow no pointer to the NHI, so section 4.2's rule holds for
  this request. One request at a time builds into a buffer in the
  device's extension (a synchronization event); the answer is copied out
  from it.
- **One DWORD at a time, and the NHI's location on 98.** Each function's
  256 bytes are assembled from prefixes of offset + 4 bytes, the reads the
  topology makes: read on 98 SE, its HAL answers those and not one
  256-byte read, which left the tunnel port reading as absent. A DWORD
  past the HAL's answer ends the assembly: the entry keeps how many bytes
  were read (`CM_QP_E_VALID`, the rest zero), the walk enters a bridge's
  bus only where its bus numbers were read, and the tool decodes nothing
  past the bytes read and says so ("Only the first N bytes ..."; a
  capability list going on past them, with no "not present" claimed; a
  port not walked). And 98 SE's PCI bus answers no bus number or address for
  the NHI's PDO, so the topology's own reading of the NHI's location
  (`CM_REENUM.Nhi`) stands in. Read in the emulator on 98 SE (run 41):
  while the tunnel bridge's devnode had no driver (its Add New Hardware
  wizard cancelled), the query's reads of it answered "no function"; once
  Windows had installed its standard PCI-to-PCI bridge driver, the bridge
  was read whole. A real machine installs that driver at setup.
- **The walk below a port** (2000 and later): the port's secondary bus,
  then the secondary bus of each bridge found there whose numbers lie
  inside the port's secondary-to-subordinate range and above its own bus,
  each bus once; 32 devices a bus, eight functions where function 0 says
  multi-function. Bounds: 32 functions below all ports, 16 buses a port,
  8 bridges deep, 4 ports; a bound reached sets `CM_QP_F_TRUNCATED`, so
  the largest answer is 64 + 37 x 288 = 10,720 bytes and the tool's first
  buffer (16 KiB) holds it. A port with no bus numbers has nothing below.
- **Windows 98 SE and ME** (roadmap-usb4 task 42-A.14, 2026-10-19): the
  NHI and the tunnel ports are read as above; the walk below them is
  handed to the 98 CONFIGMG worker (`cm_cfgwk.c`, `CmCfgwkPciAsk`) and
  waited for 3 s at most. The worker reads below each member through
  `PCI.VXD`'s guarded `_PCI_Read_Config` (`CmCfgmgPciRead` behind
  `CmCfgmgPciOpen`, record 21 section 6.1.1), between its other
  operations and never alongside a numbering, and enters a bridge's bus
  only where `PCI.VXD` reads that bridge with the same bus numbers itself
  (the HAL, which on 98 SE answers only functions `PCI.VXD` knows) or
  where its own numbering gave them; a member whose controller was lost
  (`CONTROLLER_UNAVAILABLE`'s removal or no-scan re-enumeration owed) is
  not read below. The query device reaches the worker through a pointer
  the thread publishes and withdraws before it stops the worker, under a
  rundown count that covers the posting alone; the request is a block of
  its own that the asker and the worker share, freed by whichever lets go
  last, so a worker inside a blocked CONFIGMG call answers late or never
  and touches nothing of the device. Where the walk is not made, each
  member keeps one `CM_QP_S_NOT_READ_OS` entry with its reason in
  `CM_QP_E_INFO` (`CM_QP_NR_*`: no worker, the worker busy, `PCI.VXD`'s
  guard refused, the controller lost, and since roadmap-usb4 task 43.63
  the member's own bus numbers not admitted by the rule above; 0 from a
  driver before it), and the tool says which. Where the walk is made and
  finds nothing, the port is "None found." as on NT; where the answer
  was cut short (`CM_QP_F_TRUNCATED`) a port with nothing listed is said
  as "None listed" rather than empty (task 43.63, on every system).
- **No kernel address.** The answer holds configuration space and the
  driver's own counts. The BARs and bridge windows are bus addresses,
  shown by the owner's request; they are the ranges Device Manager's
  Resources tab shows any user.
- **Why `HalGetBusData` and not `IRP_MN_READ_CONFIG`** from the bridge
  and child filters: the filters exist on NT only, a child filter only
  for a device plugged after the bridge became active (record 21 section
  7.4), and reaching one from the query device would need a reference on
  a filter's lifetime; `HalGetBusData` reaches every function the walk
  names on every target, with no stack.

### 4.8 `xhci98.sys`'s query request (revision 12)

The owner's logging-consistency decision of 2026-10-16: `xhci98.sys` is
read as `xhcicm.sys` is, through a request of its own, not the USBUSER
pass-through. `IOCTL_XHCI98_QUERY` =
`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA48, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222920`, no input, on the controller's own `\\.\HCD<n>`, answered by
the HCD's dispatch at `PASSIVE_LEVEL`. Record 13 section 8.12 specifies
it; the layout is `src\xhci_qry.h`'s `XHQ_*`: a 64-byte header (magic
`XHCQ`, version 1, size, flags, the level applied and as read, the
offsets and lengths of the counter block, the log and the configuration
space, the ring's appends and overwritten bytes, the configuration
read's state, the build flavour), then the parts. Sizing is `XCMQ`'s.

- **What it serves follows the level**, as `xhcicm.sys`'s log answer
  does, and it is not gated by the door's enable step: the counter block
  (`XHCIHC_COUNTERS`, little-endian words, the one word the door computes
  computed the same way) from level 1; the log ring as text, oldest first,
  from level 2 - at level 4, from `3.5.0.0`, with the kernel addresses
  it holds, `XHQ_F_ADDRESSES` set beside `XHQ_F_WITHHELD` (which a
  driver before `3.5.0.0` set alone, serving no log there; record 13
  section 8.12 says why the old flag stays set); and the
  controller's configuration space 00h to FFh at every level, 0 included,
  because it is not the log.
- **The configuration space** is read on demand, one DWORD at a time
  through `HcdSvcConfigSpace`'s `IRP_MN_READ_CONFIG` to the PCI PDO, at
  `PASSIVE_LEVEL` under the door gate, before the controller lock; never
  written, read in no interrupt, transfer or power path. A refused read is
  `XHQ_PCI_FAILED`, a first DWORD of all ones `XHQ_PCI_ABSENT`; the tool
  says "unread" for both.
- **The ring and the counters** are copied under the controller lock,
  the lock the door's snapshot copies them under, straight into the
  request's buffer.
- **No address below level 4, in either driver.** With level 2 the
  default, every log record site of both drivers was read for values
  that are addresses (physical, bus, DMA, MMIO, kernel virtual, a
  pointer or a handle). Four were found below level 4 and became address
  records, which the shared ring takes at level 4 alone: `start.common.pa`
  and `pirq.rcba` (`hcd_ctl.c`), `qemu.inj.bulk.lost` (`hcd_inj.c`, the
  `qemu` flavour), and in `xhcicm.sys` - which had no address-gated
  append - `cm.live.bar-changed` and the two 98 devnode-handle records,
  through a new `CmLogNoteAddress`. `start.bar` and `start.ok` already
  were. Section 4.5's label filter stays as a second guard.
- **Compatibility.** The USBUSER pass-through, its clause table and its
  regions are untouched, so `usbui.dll` and `XHCISNAP` read the driver as
  before. An older `xhci98.sys` answers the request with
  `STATUS_INVALID_DEVICE_REQUEST`: the tool reads its log through the
  door as section 4.6 has it, and says "not served by this xhci98.sys"
  for its configuration space. `XHCISNAP` stayed in `3.0.0.0` unchanged;
  it was retired in `3.1.0.0` (roadmap-usb4 tasks 42-0.3 and 42-A.4,
  were SNAP.1 to SNAP.3), its reading moved into this tool (section 15),
  and the pass-through stays: Windows' own property tabs use it, and the
  tool's snapshot reads through it.

## 5. The USB branch

- Controllers: `\\.\HCD0` to `\\.\HCD63` on every target (usbview's own
  route reaches only HCD0 to HCD9, and xhci98 can take a higher number,
  record 13 section 8.1), opened with no access rights (every IOCTL below
  is `FILE_ANY_ACCESS`); on 2000 to 7 also every interface of
  `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`, de-duplicated by driver key.
- `IOCTL_GET_HCD_DRIVERKEY_NAME` (the controller's devnode, so its
  description and whether xhci98 drives it), `IOCTL_USB_GET_ROOT_HUB_NAME`,
  then per hub `IOCTL_USB_GET_NODE_INFORMATION` and per port
  `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX`, falling back to the
  original `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION` where `_EX` is
  refused (older stacks; the legacy speed field cannot tell Full from
  High, so that is shown unknown), `_EX_V2` with its input initialised
  (the connection index, length and protocols, record 13 section 8.4) for
  the SuperSpeed and SuperSpeedPlus flags (xhci98 reports speed 2 in `_EX`
  on NT), `_DRIVERKEY_NAME`, `_NAME` for hubs (recursion goes by
  `DeviceIsHub` and the name, whichever form answered), and
  `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` for the configuration,
  strings and BOS.
- The structures the MSVC 6.0 headers lack (`_EX`, `_EX_V2`, the
  descriptor request) are declared in `tr_core.h` from their documented
  layouts, packed as the DDK packs them, each with a size check.
- A controller that is not xhci98's is shown with whatever its driver
  answers; an IOCTL it refuses leaves the field "not reported".

## 6. Eject

### 6.1 What can be ejected

Exactly record 24's units (section 4.2 there): a devnode directly behind
an active, verified tunnel port, not protected (NHIs, their ancestors, and
the USB controllers that share the NHI's grandparent), selected by its
instance ID within one NHI start (epoch), never by route. The rules are
`inst_core.c`'s (`inst_tb_units`, `inst_tb_protect`,
`inst_tb_unit_blocked`), fed from the query instead of the registry
record.

### 6.2 The transaction

1. Confirm with the user: a dialog like Windows' "Stop a Hardware
   device", the unit and the devnodes that will stop as the tree last
   showed them, OK / Cancel (from the tray menu the selection is the
   request and the dialog follows it), with record 24 section 5.4's
   reminder where it gives one (its revision 8, roadmap-usb4 task 43.8),
   as one paragraph the box wraps: the console's line break in the
   shared text is a space there (`tr_put_box`, task 43.63).
   Nothing below waits on the user.
2. Take the shared eject mutex (section 6.3). Query (Q1). An answer is
   eligible when the record's stage is active, the inert and stopping
   flags are clear and the liveness state is ACTIVE; refuse unless Q1 is.
3. Wait, polling every 100 ms for at most 3 s, until the published-pass
   counter has moved at least two past Q1's (wrap-safe) with the same
   epoch (Q2), and Q2 is eligible: one move could be a snapshot built
   before Q1 and published after it, two prove one built after the eject
   began. If none comes, refuse ("the controller is not answering"). This
   replaces record 24's ask and echo for this tool: like the echo, it
   proves a pass after the request rather than a recent one.
4. Walk the devnodes completely; query again (Q3). Q3 must be eligible
   and have Q2's epoch and content generation; otherwise walk once more,
   then refuse.
5. The selected unit must still be a unit, by instance ID, with the same
   devnodes under it as the user confirmed; otherwise refuse and show the
   new state. Immediately before the call, one more query must be
   eligible with Q3's epoch and content generation.
6. The call, record 24 section 7: `CM_Request_Device_EjectA` on NT
   (setupapi on 2000), with the not-removable fallback
   `CM_Query_And_Remove_SubTreeA`; on 98 SE and ME
   `CM_Query_Remove_SubTree` then, if accepted, always `CM_Remove_SubTree`,
   both `UI_NOT_OK` - since 2026-10-18 (record 24 section 7.1's revision;
   record 21 section 7.2, the 9x half; the code is 42-A.13) a unit's
   eject on 98 SE and ME is asked of the connection manager through
   record 24's ask and result values instead, the two calls staying for
   the plain USB devices of section 6.5 (as built in task 43.18 under Q3's
   epoch and content generation, which Q4 just confirmed; the port's
   details say "Blocked" while the status record's blocked bit for it is
   set, record 24 section 3.1). Never forced, never
   `DIF_REMOVE`, one unit at a
   time. Since revision 11 the not-removable fallback is
   `CM_Query_And_Remove_SubTreeW`, decided by `inst_eject_nt_step` as
   record 24 section 7.2 has it, and a unit not marked removable in a
   process without administrator rights goes to section 6.6 before this
   step.
7. The post-eject assessment ported from XHCIINST (the restarted-unit
   check, the USB4 tunnel watch, the unplug advice of record 24 section
   5.4 with every fact it needs), captured as text; on a veto, the vetoing
   device and the reason. The text is shown with its wrapped lines
   joined (`tr_join_lines`, paragraph breaks kept), so the advice laid
   out for the console ("Other devices behind the same Thunderbolt port
   ...", "The device has started again ...") is one paragraph in the box
   or balloon (held by a host vector, task 43.63). The tunnel watch (2026-10-22, from the 43.45
   legs; `tr_eject_watch_kind` in `tr_core.c`) depends on who confirms
   the eject to the connection manager (record 21 section 7.4). An
   administrator's run confirms it itself and watches 10 s, as XHCIINST
   does; so does the not-removable route, which nothing confirms. A run
   without administrator rights on XP and later, the mark service not
   read as stopped or absent, leaves the confirmation to the service's
   next pass: the service writes it under this mutex, so the tool
   releases the mutex here, before the watch, and watches 10 s plus
   twice the service's idle period (`mk_poll_ms(0)`, 10 s), 30 s. That
   is a budget, not a bound the service promises: its pass needs the NHI
   verifiable, the unit's departure pending and Windows holding it for
   eject (problem 47), and a walk has no time limit. If the tunnel is
   still up it says "The PCIe tunnel has not been seen closed yet. It
   closes if the xHCI98 mark service confirms the eject to the
   Thunderbolt driver, which the service does in the background; an
   eject made as an administrator is confirmed by this tool itself."
   The watch ends early at the installer's quit event (section 7.1), as
   30 s outlasts the installer's 15 s wait for the tool to end; no
   answer is shown then. Without administrator rights on 2000, or with
   the service stopped or absent, nothing confirms it: 10 s, then "The
   PCIe tunnel stays up until the device is unplugged, because the eject
   was not confirmed to the Thunderbolt driver; an eject made as an
   administrator is confirmed by this tool itself." Whatever the kind,
   when step 5's last query (with the unit present) showed its tunnel
   port, an active member, named by no entry beside a host port's router
   with its PCIe tunnel up and no tunnel port known, no read of the watch
   named the port, and the last read, at the step that found the bound
   passed, still shows it so (`inst_port_pcie`, `inst_pcie_unbound`: the
   connection manager has no USB4 port to close), the line at the end is
   record 24 section 5.2's for a port the driver does not know
   (`tr_eject_end_text`, Phase 48, 2026-11-10). That last read counts
   only when it is current (`tr_query_watch_current`): eject-ready by
   step 1's gate, younger than `CM_Q_CURRENT_MS` with the time from
   before the call that read it to the decision added (the driver
   samples the age inside the call), and published at least
   `CM_Q_NEW_PASSES` passes after step 5's answer, in the same start. The
   query device answers from the driver's cached snapshot, so an answer
   whose publication stalled would otherwise read unmapped through the
   whole watch; such an answer keeps the kind's line.
8. Release the mutex (if step 7 has not), then show the result: a message box from the
   window or a balloon (tooltip on 98 SE) from the tray.

### 6.3 The shared mutex

`Global\XhciinstTbAsk` on NT, `XhciinstTbAsk` on 98 SE and ME, the name
XHCIINST already uses. Both programs open it first with
`SYNCHRONIZE | MUTEX_MODIFY_STATE`; whichever creates it gives it a
security descriptor that grants those two rights to everyone and full
access to SYSTEM and Administrators, so an unelevated tray and an
elevated XHCIINST can both take it. An abandoned mutex counts as taken
(the transaction revalidates everything anyway). A wait longer than 10 s
refuses ("another program is removing a device"). It is held only for
one transaction, never while waiting for elevation, and not through a
tunnel watch that waits for the mark service (section 6.2 step 7).

### 6.4 Rights

On NT, `CM_Request_Device_EjectA` needs no administrator for a removable
device in the console session (Windows' applet runs as the user); a dock
can also need the undock right. If the call answers with a rights
failure, the tool says so and offers to run the eject elevated: it
releases the mutex and starts itself with `runas` (Vista and 7) and
`/eject id=<instance id>`, a one-shot mode (section 7.3) that runs the
whole transaction itself and exits. Since revision 11 a unit's elevated
run is section 6.6's, waited for and answered; a USB device's is as
before.

### 6.5 Plain USB devices (revision 7)

The owner's decision of 2026-10-16: the tool stops a plain USB device too,
on every target, as Windows' own Safely Remove does. Sections 6.1 to 6.4
stay the Thunderbolt units'; this section is the USB devices'. Nothing of
`xhci98.sys`, `xhciuas.sys` or `xhcicm.sys` changes for it.

**The device and what stops with it.** A USB device is the devnode a
port of section 5 names (`IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME`,
found in the walk by its driver key), on any controller the tool lists -
xhci98's or Windows' own, a dock's behind a tunnel included. Its **set**
is everything Windows stops with it:

- on 98 SE and ME, the device and every devnode below it in the walk (a
  composite device's functions, a storage device's disks): the removal of
  a subtree there is the subtree;
- on NT, the same, and then, until nothing new is added, the removal and
  ejection relations of every member (`CM_Get_Device_ID_ListA` with
  `CM_GETIDLIST_FILTER_REMOVALRELATIONS` and `_EJECTRELATIONS`, from
  `cfgmgr32.dll`, else `setupapi.dll`, looked up at run time) and every
  devnode below each: a disk's volumes are its removal relations, outside
  its subtree. A relation that cannot be read, or that names a devnode
  the walk does not hold, leaves the set unknown.

The port scan runs on every refresh, in the tray too, whatever the View
check boxes show, so the tray and `/eject` see the same devices as the
window.

**Listed.** A device is listed (in the tree with its state, in the tray's
menu, for `/eject`) when its set holds none of:

- a hub (the port says so, or the hardware or compatible IDs carry
  `USB\Class_09`), a root hub, or a host controller;
- an input device: a devnode of setup class `HIDClass`, `Keyboard` or
  `Mouse`, bound to `hidusb`, or with an instance, hardware or compatible
  ID beginning `USB\Class_03` or `HID\` - so a keyboard, a mouse, a
  tablet and a composite device with an input function are never listed;
- a wireless radio (device class `E0`, a Bluetooth adapter);

its devnode was found in the walk (a device with no driver has none on
98 SE and ME, and no disk anywhere), and its set holds a disk (setup
class `DiskDrive`), or, on NT, a volume (setup class `Volume`); on 98 SE
and ME the disks are found as section 14's drive labels already find
them. On NT the setup class is `CM_DRP_CLASS`; where that is empty, the
`Class` value of the class key its `CM_DRP_CLASSGUID` names (revision
15: Windows 2000 writes a USB disk's `ClassGUID` alone, so without it
the disk was no disk). Everything else connected gets details only, with the reason ("a
hub", "an input device", "a wireless radio", "no disk on it").

**Why not "removable" on NT.** The owner's instruction allowed a device
Windows reports removable. `xhci98.sys` reports `Removable` on every
device it presents (`src\hcd_pdo.c`), internal ports included: it reads
no ACPI `_UPC` or `_PLD`. The owner's ThinkPad P14s Gen 1 under 98 SE
has, on xhci98's root ports, a touchscreen, an integrated camera (a
composite device), a fingerprint reader and a Bluetooth radio, all
internal; under the capability rule the camera's parent is
`SurpriseRemovalOK` and drops out, but a fingerprint reader with a driver
or the radio would be offered, as Windows' own applet would list them.
So on every target a disk or a volume decides, and a USB device with no
storage (a camera, a network adapter, an audio device) is stopped by
unplugging it, as before. Windows' own applet still lists what it lists.

**Offered.** A listed device is offered when it is started and has no
problem code, its set is known, holds no more than 64 devnodes, and each
of them was read whole. One that is stopped is shown "[stopped - unplug
it]" (held for eject, `tr_held_for_eject`: Code 47 after an eject on XP
and later; on Windows 2000, which has no Code 47, Code 24, revision 15,
below); one with another problem code is shown with the code, and is not
offered.

On 2000 Code 24 is also a failed start, so it reads as held for eject
only for a devnode this process stopped (step 5's reading at Code 24, or
a unit's accepted stop), kept until a walk finds it started or a whole
walk no longer finds it, and, for the device at a USB port, only while
the port still reports a configuration (`tr_w2k_code24`): the stopped
device keeps its configuration until it is unplugged, and one plugged in
again starts unconfigured. A device stopped elsewhere (Windows' own
Unplug or Eject Hardware, or an elevated child of this tool) reads as
any Code 24 does. The tray and the window are one process, so a stop
from either marks both.

**What it is called.** Its disks' names and their drive letters, as units
are (section 14, "A unit named by its drive" in run 41): "USB Flash Disk
(F:)"; without a disk, the device's own name. The letters come from the
same code: on NT the storage device number of each disk's interface
against each `\\.\X:`, on 98 SE and ME the disk's
`CurrentDriveLetterAssignment`. On NT a disk is named by its friendly
name and found by its setup class, so on 2000 both depend on the class
read above (revision 15); the interface and device-number reads answer
on 2000 as on XP and 7 (`runs/run-42.md`, "XHCITR on Windows 2000").

**The transaction.** No query device, no epoch and no passes:

1. Confirm, as section 6.2 step 1 and with the same layout: "This stops
   <device> and everything on it:", each disk with its volumes, then every
   devnode of the set. Nothing below waits on the user. A thread of the
   tool's own, with a hidden top-level window and a message loop of its
   own, in every mode (`/eject` included), counts the device-change
   notifications Windows broadcasts (`WM_DEVICECHANGE`); it never waits on
   anything else, so no wait of the eject's thread - the question, the
   mutex, a walk - can make it miss one. The request carries the count read
   just before the model it was built from was read.
2. Take the shared mutex of section 6.3, so that no XHCIINST and no other
   XHCITR eject runs beside it.
3. Walk the devnodes again and find the device by its instance ID: its
   parent must be the devnode it was under when confirmed, its set (read
   again, relations included) exactly the devnodes confirmed, and it must
   still be offered by the rules above. Then, immediately before the call,
   read the count again: any notification since the request's count -
   while the question was open, the mutex awaited or the walk read -
   refuses, and the request's count is never moved forward. Otherwise
   nothing is called and the reason is shown ("The device list changed;
   nothing was stopped. Look at the tree again."). An instance ID alone
   cannot tell a device from an identical one plugged into the same port
   (a location ID, without a serial number); the count is what catches a
   re-plug. It narrows that case rather than closing it: a USB device has
   no attachment-lifetime token, and a re-plug faster than Windows'
   broadcast of it, between the last count and the call, is not seen.
4. The call on the device's devnode, never on a disk or a function below
   it:
   - NT: `CM_Request_Device_EjectA` (setupapi on 2000, as section 6.2),
     taking the veto type and name. No not-removable fallback: a refusal
     is shown as it is.
   - 98 SE and ME: `CM_Query_Remove_SubTree` with `UI_NOT_OK`, then, only
     if accepted, always `CM_Remove_SubTree`, as section 6.2 step 6 does
     for a unit.
5. Release the mutex, then show the result:
   - stopped: "Windows has stopped <name>.", then what a walk right
     after reads: the walk whole and the device gone, or held for eject
     (Code 47, or Code 24 on 2000, the addendum below) - "You can now
     unplug the device."; the device started
     again - "The device has started again, so it is not safe to
     unplug." (exit 4, as record 24 section 8.2 has it); anything else,
     the walk incomplete included - "Its state could not be read again:
     check that it is not running before you unplug it." On 98 SE and ME
     also "Until you unplug it, Device Manager's Refresh can start it
     again.";
   - refused: in plain words, with the drive letters where Windows'
     answer allows it. NT `PNP_VetoOutstandingOpen`: "Windows refused: a
     program is using F:. Close it and try again." (the device's name in
     place of the letters when it has none); `PNP_VetoWindowsApp` and
     `PNP_VetoWindowsService`: "<name> is using F:"; the other veto types
     and answers as record 24 section 7.2. 98 SE and ME
     `CR_REMOVE_VETOED`, which names no cause: "Windows refused: F: is in
     use. A program may have a file open on it, or a DOS box may be using
     it. Close it and try again." Nothing was changed by any refusal.
   - exit codes as record 24 section 8.2: stopped 0, refused or started
     again 4, the list changed or the call not offered 1, a removal that
     failed after an accepted query 2.
6. From the tray, a balloon with the outcome (a message box on 98 SE, and
   for an outcome longer than a balloon), as section 14 has it for units.

Never forced: no `CM_Remove_SubTree` without an accepted query, no veto
overridden, no `CM_Query_And_Remove_SubTreeA`, no `DIF_REMOVE`, one device
at a time.

Addendum (2026-10-17, branch `tr-w2kmsg`): Windows 2000 has no Code 47
(its DDK's `cfg.h` ends at `NUM_CM_PROB` 0x25), so a stopped USB device
left in its tree read "could not be read again" (the release matrix's 2000
stick). Step 5 now reads Code 24 on 2000, and on 2000 alone, as stopped:
"You can now unplug the device." (`tr_usb_after_kind`). Code 24 is run 41's
2000 bridge after `CM_Query_And_Remove_SubTreeW`; for a USB device after
`CM_Request_Device_EjectA` it is inferred. Host-tested only. Revision 15
read it: a Bulk-Only stick and a UAS disk after
`CM_Request_Device_EjectA` on 2000 read status `0x01806400`, Code 24,
and this reading now also records the stop that lets the tree and the
tray mark the device ("Offered", above).

**Rights.** On NT a removable device needs no administrator (Windows'
own applet runs as the user); a rights refusal is offered elevated on
Vista and 7 as section 6.4 says, with `/eject id=<instance id>`.

**Where it shows.**

- The window: the USB branch's label of an offered device ends "[can be
  stopped]"; its details say "Safely Remove: can be stopped here" or why
  not. Action > Safely Remove..., Delete and the right-click menu act on a
  USB port node whose device is offered, and on a node of the
  Thunderbolt branch that is that device's devnode; a USB device behind a
  dock's controller is stopped by itself, its dock only from the dock's
  unit.
- The tray's left-click menu has two groups, each under a grey heading
  and only when it has an entry: "USB devices" and "Thunderbolt and USB4
  devices", each item "Safely remove <name>" (grey with the reason when
  listed but not offered); "No device to stop" when both are empty.
- `/eject id=<instance id>` takes a USB device's instance ID as it takes
  a unit's; `/report` writes each device's "Safely Remove:" line.
- A one-shot answer written to a redirected output (`/eject`, an error
  of `/report`) is console text, and is wrapped at 79 columns.

### 6.6 A unit not marked removable (revision 11)

NT only. A unit whose capabilities say neither `Removable` nor
`SurpriseRemovalOK` (`inst_unit_not_marked`) can be stopped only by the
query-and-remove route of record 24 section 7.2, which needs an
administrator's rights. The owner's decision of 2026-10-16: the tool asks
for those rights for this stop only, when it lacks them.

1. **When.** `tr_elev_needed`: NT, the unit not marked removable, and
   this process no administrator - on Vista and 7 its token not elevated
   (`TokenElevation`), on 2000 and XP not a member of Administrators
   (`CheckTokenMembership`), both looked up at run time. An administrator
   stops it in place, as section 6.2 does.
2. **The question.** After the usual confirmation, a dialog with the unit
   and "Windows does not list this device for safe removal. Stopping it
   needs administrator rights.", and two buttons: "Stop as
   administrator..." (with the shield on Vista and 7) and Cancel.
3. **The elevated run.** `ShellExecuteEx` with the verb `runas` starts the
   same executable with `/eject id=<instance id> /result <name>
   /confirmed <digest>`: User Account Control on Vista and 7, the Run As
   dialog on 2000 and XP (read on both). It runs section 6.2 whole - the
   shared mutex, every re-check, the route and its vetoes - as a one-shot
   run, and never offers elevation itself.
4. **What the elevated run trusts.** Nothing it reads from a place a user
   can write. `/confirmed` is a digest of the devnode list the user
   confirmed (`tr_res_digest`: two 32-bit FNV-1a hashes over the count
   and the upper-cased instance IDs), on a command line nothing can change
   after the start; the run reads its own list and asks its own
   confirmation unless that list has the same digest, so the digest can
   spare a question and never add a devnode. Its answer goes into a named
   section the window created before the start (`Local\Xhci98TrAnswer-
   <process id>-<tick>`, refused if it exists already, writable by SYSTEM
   and the Administrators - who include another account under Run As);
   the run opens it by a name `tr_res_name_ok` checks, writes at most
   `TR_RES_BYTES` ("XHCITR ANSWER 1", the code, the text), and no file is
   written or read.
5. **The wait.** The window waits on the run's process for up to five
   minutes, answering its messages; another stop is refused meanwhile, and
   a quit (Exit, or an installer's) ends the wait at once and is posted
   again. Then `tr_elev_result`: the run's own text when its code is the
   process's exit code; a cancelled elevation (`ERROR_CANCELLED`) -
   "Nothing was stopped: administrator rights were not given. Try again
   and allow it, or unplug the device and plug it in again." (exit 1); a
   failure to start - the rights text of record 24 section 7.2 with the
   error; no end within the wait, or an end with no answer - the state
   not known, look before unplugging (exit 2). The answer is shown as any
   stop's, in a message box or the tray's balloon.
6. `/eject id=` started by a user with no administrator rights takes the
   same route for such a unit.

Since 2026-10-18 the mark itself survives a rebuild on Windows 2000 to 7
where the mark service of record 21 section 7.4, revision 3, runs (task
42-0.4; the code is 42-A.5): this section's route stays for a unit the
service has not reached (its restart vetoed, the service stopped, a value
not yet in effect), and the window shows the mark's state beside the
unit as that revision says. As built (42-A.5's tool part, 2026-10-19): on NT a unit's
details read "Safe-removal mark: listed by Windows", "mark written; takes
effect at the next start of the device", "mark written, but Windows does
not list it", "not marked", or "not read" with the reason (since task
45.62 the first and third read "the Thunderbolt driver has answered it as
removable" and "mark written; the Thunderbolt driver has not answered it
as removable", and the first is followed by "Windows has not recorded it
as removable." when the unit's recorded capabilities lack `Removable`:
record 21 section 7.7), from its NHI's
marks answer (`IOCTL_XHCICM_MARKS`, read at each refresh with the query and
held to its epoch) and the unit's `LowerFilters`; the NHI's details add
"Mark service: running", "stopped", "not installed" or "not read"
(`QueryServiceStatus`, looked up at run time). After the tool's own
successful `CM_Request_Device_EjectA` of a unit, and only when it runs as
an administrator (an elevated run on Vista and 7), it waits up to 3 s for
the unit's entry with a departure pending and writes
`XhciCmEjectConfirm` (`CM_QC_*`, 28 bytes) to the NHI's driver key while
the value is absent or empty, under the shared eject mutex it already
holds, and says whether the eject was confirmed; the query-and-remove
fallback confirms nothing (record 21 section 7.4). The pure half
(`tr_mark_state`, `tr_mark_find`, `tr_msz_has`, `tr_mark_member`) is in
`test\test_tr.c`; the marks answer is parsed by `xhcimark\mark_core.c`,
which XHCITR links.

XHCIINST needs nothing new: it runs as an administrator (record 24 section
7.2).

## 7. The tray icon

### 7.1 Behaviour

- `XHCITR /tray` starts with only the icon; `XHCITR` with no argument
  opens the window and also keeps the icon. One tray per user session (a
  named mutex `Local\Xhci98TrTray` on 2000 and later, `Xhci98TrTray` on
  98 SE and ME); a second plain start asks the first to show its window
  (a registered window message carrying no request but "show") and exits.
  The one-shot modes `/eject` and `/report` never hand over: they run
  and exit with a status. No window message can start an eject.
- **Left click**: a menu "Safely remove <unit>" per unit (grayed with the
  reason when it cannot be stopped now), and since revision 7 per listed
  USB device, in two groups (section 6.5), a separator, "Open xHCI98 USB
  Tree". **Double click**: the window. **Right click**: Open, Refresh,
  About, Exit.
- **Tooltip**: "xHCI98 USB Tree: N Thunderbolt/USB4 devices" and, where
  the shell has no balloons, the last event (at most 63 characters).
  While the icon is red (revision 19), what is wrong instead (section
  7.2.1), and the left-click menu and the double click lead to it.
  **98 SE after a long hover** (revision 20, task 43.72). Read in QEMU
  with a build that traced every `Shell_NotifyIcon` call and tray message
  to the debug port: `shell32.dll` 4.72 shows an icon's tooltip once per
  entry, and a pointer that rests on the icon until the tooltip times out
  and then leaves it with no move over the notification area beside it
  leaves that tooltip "popped" for good - it never shows again for the
  icon, while the clock's and every other tooltip still do, and the
  tool's own `NIM_MODIFY` calls (all accepted, the text intact) do not
  bring it back. QEMU's pointer jumps, so it does this every time; a real
  mouse moved off the icon fast can. `3.2.0.0`'s XHCITR did the same. A
  `NIF_TIP` with no text and then the text did not help; removing the
  icon and adding it again (a new tooltip of its own in the shell) did.
  So on 98 SE alone (`shell32.dll` below 5.0) the tray notes each
  `WM_MOUSEMOVE` the icon reports (`tr_hover_move`), and the 1 s poll,
  once the pointer is more than a small icon's width or height from the
  last move's point, or out of the notification area, and no tray menu
  is up, adds the icon again
  (`tray_remove`, `tray_add`: its colour and tooltip as `tray_red` and
  section 7.2 last chose) when that hover may have outlasted the
  tooltip: the shell shows it the initial delay after the last move and
  keeps it the autopop time, which comctl32 derives from the double-click
  time (1 and 10 times it), so 11 times `GetDoubleClickTime()`, less the
  poll's period (`tr_hover_poll`, host-tested). A hover shorter than
  that is left alone; one longer draws the icon again once, perhaps at
  another place among the area's icons.
- A hidden top-level window (class `Xhci98TrTray`) owns the icon, so the
  `TaskbarCreated` broadcast reaches it and the icon is re-added after an
  Explorer restart; `WM_CLOSE` to it ends the program cleanly.
- **Quit event** (for the installer, section 8): a manual-reset event
  `Global\Xhci98TrQuit` on NT, `Xhci98TrQuit` on 98 SE and ME, created
  only by XHCIINST, set, and closed when its run ends (so it exists only
  during an install or uninstall). The tray never creates it: once a
  second, with its query poll, it opens it with `SYNCHRONIZE` alone; when
  it exists and is set, the tray finishes an eject transaction in
  progress, removes the icon and exits. On NT XHCIINST gives it a security
  descriptor granting everyone `SYNCHRONIZE` and SYSTEM and
  Administrators full access. A local user who creates the name first
  could close other sessions' trays; that is the whole of the exposure,
  and XHCIINST's `WM_CLOSE` and its "run again" answer (section 8) still
  cover a tray that misses the event.

### 7.2 Events and keys

The tray polls the query of every NHI every 1 s (one IOCTL each, no
device traffic) and diffs the router entries (pure, `tr_core.c`). A
router's key is its NHI's epoch plus, for USB4 entries (kinds 2 and 3),
the route, and for ICM entries (kind 1, whose route is not known) the
link and depth, IDs and UUID suffix; two entries with one key have no
identity, and their events use generic wording ("a Thunderbolt device").
A new epoch is a new set of devices.

| Event | Text |
|---|---|
| a router entry appears | "<vendor> <model> connected (Thunderbolt 3 / USB4)." |
| an ICM device's approval becomes pending or unanswered | "<name> is waiting to be approved." |
| an ICM device's approval becomes failed | "<name> could not be approved." |
| a router entry leaves | "<name> was disconnected." |
| the liveness state goes from ACTIVE to a loss state, or the record's stage from active to failed | "The Thunderbolt/USB4 controller stopped answering." |
| the NHI's unexpected-removal count rises (revision 16, section 7.6; NT) | "A Thunderbolt or USB4 device was removed unexpectedly." |

A loss state (`tr_live_lost`) is suspect, absent, configuration lost,
recovering, recovery failed, contained or preparing. OFF ("not
monitored") is not one: the connection manager disarms the monitor at
every sleep and stop (`CmLiveDisarm`), so until 2026-10-22 the tray
raised this balloon after every sleep and hibernate with nothing failed
(the 43.45 legs, 7 x86). Revoked (a PnP stop or removal) and a value
the tool does not know are not loss states either. A resume that fails
(`cm_power.c`) leaves the liveness OFF and sets the stage failed, so the
stage's move from active to failed raises the same balloon, once for the
NHI however many of the two changes one sample shows.

The approval events read the ICM's approval states only: a USB4 entry's
approval word folds held, failed and unsupported together and an eject
sets failed (`cm_status.c`, `tbt_u4cm.c`), so USB4 entries announce
connection and disconnection alone.

These are descriptive. A 1 s sample cannot prove how a device left: the
ICM keeps a tunnel up after an eject until the unplug (record 24 section
5.2), the USB4 teardown is asynchronous and absent on 98 SE and ME and
without the child filter (record 21 section 7.4, record 24 section 5.2),
and another program can stop a unit and the user unplug it between two
samples. So the tray says "disconnected", never "unplugged while in use"
(decision T9). The outcome of an eject comes from the transaction
(section 6.2 step 7), not from this diff. The unexpected-removal notice of
revision 16 is no reading of this diff either: it comes from a count the
driver keeps of what Windows itself did to a running device (section 7.6).

**Balloons.** Where `shell32.dll` is version 5.0 or later (2000, ME, XP
and later) the tray uses the version 2 `NOTIFYICONDATAA` size with
`NIF_INFO` balloons and `NIM_SETVERSION` 3; on 98 SE (4.72) the version 1
size and the tooltip, and since revision 17 a pop-up the tool draws
itself for each notice that gets a balloon elsewhere (section 16). The structures are declared in `tr_core.h` for the
MSVC 6.0 build with size checks (88 and 488 bytes, ANSI, x86); the x64
build uses the WDK's header.

### 7.2.1 The red icon (revision 19, task 43.57)

Roadmap-usb4 43.57 (owner, 2026-10-22): the tray's icon turns red while
any of four conditions holds, and returns to its usual icon when none
does. **The rule** (the owner's, for these four and for every condition
added later): red means a condition that is still present and that the
user can put right - move the device to a USB 3 port, take the slow hub
away, approve the device, deal with the failed controller. The
unsafe-unplug warning (43.21) is not one: it has already happened, there
is nothing left to put right, and its balloon is enough; nor are a plug,
a disconnection or an eject.

1. **The four conditions**, in the order the tooltip ranks them (item 4):

   | Kind | Holds while | Read from |
   |---|---|---|
   | `TR_RED_HC` | a controller has stopped answering: an NHI whose liveness is a loss state or whose record's stage is failed; an `xhci98.sys` controller failed for good | the poll's snapshot (section 7.2); the controller's query answer (item 3) |
   | `TR_RED_WAIT` | a Thunderbolt or USB4 device is waiting to be approved | the poll's snapshot |
   | `TR_RED_SLOW3` | a USB 3 (SuperSpeed-capable) device runs at High, Full or Low Speed | 43.27's set (section 7.7), item 2 |
   | `TR_RED_SLOW2` | a High Speed-capable device runs at Full Speed because the hub it is behind is a Full Speed hub | the same, item 2 |

   **A controller.** For an NHI the test is the one the "stopped
   answering" balloon makes since 2026-10-22 (`tr_live_lost`, section
   7.2): suspect, absent, configuration lost, recovering, recovery
   failed, contained or preparing, or the record's stage failed (a resume
   that failed, `cm_power.c`; the connection manager's latched failure).
   OFF ("not monitored", which every sleep and stop sets,
   `CmLiveDisarm`) and revoked are not: a sleep never turns the icon red.
   For an `xhci98.sys` controller it is the new flag of item 3, set when
   the bus is latched failed (three recoveries failed in a row, or the
   recovery window refused a fourth) or contained (`XhciTolTerminal` 2
   to 5, record 13 section 8.12); a failure with a recovery still owed
   (`XhciTolTerminal` 1) is not, because the driver is still putting it
   right itself.
   **Waiting.** An ICM entry whose approval is pending or unanswered, the
   entries `TR_EV_WAITING` announces (section 7.2). A USB4 entry's
   approval word folds held, failed and unsupported together, so a USB4
   entry never counts. An entry whose approval failed or was rejected
   does not count either: nothing the user does in the tool approves it.

2. **Conditions 1 and 2 are 43.27's reading, consumed, not made again.**
   43.27's `tr_fast_check` (section 7.7 item 1) already decides which
   ports hold a device running slower than it declares; the red icon
   takes its set as it stands (`red_slow`, `xhcitr.c`, which reads the
   model's ports as `fast_scan` does and sends no request of its own: a
   device qualifier is the answer the scan settled). Its kinds map as
   follows: `TR_FW_PORT2`, `TR_FW_HUB2`, `TR_FW_HUB1` and `TR_FW_HUB3`
   are `TR_RED_SLOW3`, and since revision 24 `TR_FW_WIN2` (section 7.7
   item 11); `TR_FW_HUBFS`, 43.27's High Speed case (a High
   Speed-capable device at Full Speed behind a Full Speed hub, its
   device qualifier answered, section 7.7 item 1), is `TR_RED_SLOW2`;
   `TR_FW_LINK` (a SuperSpeedPlus device at 5 Gbit/s) is **not** red -
   it runs at USB 3 speed, which the owner's condition names, and
   section 7.7 item 8 shows it may have no faster port to go to - and it
   keeps 43.27's balloon, list and menu line (item 7). The red icon
   reads that set only while 43.27's notice is in force (section 7.7
   item 7): turned off, conditions 1 and 2 do not hold; turned on, the
   icon is decided again at once. The set is the model's, so it changes
   at a model refresh, after which `tray_red` runs (item 5).

3. **`xhci98.sys`'s failed flag.** The query answer (record 13 section
   8.12) gains one flag, `XHQ_F_FAILED` (`0x80`): set when
   `XhciTolTerminal` is 2 or more, read under the controller lock as the
   HCD region reads it. The layout, the version and every other field
   are unchanged; a driver from before this flag never sets it, so the
   icon never turns red for an older driver's controller. The tray asks
   every listed `xhci98.sys` controller (`UCTRL.Ours`) for the answer's
   header alone (64 bytes) every 5 s, from the 1 s poll - not every
   second, because each answer reads the controller's 256 bytes of
   configuration space - and once after each model refresh. A controller
   that cannot be opened, or refuses the request, is not counted failed:
   the icon says what it knows, not what it cannot read.

4. **The decision is one pure function** (`xhcitr\tr_red.c`,
   `tr_red_state`, host-tested in `test\test_trred.c`). Its inputs are
   the poll's snapshot (`TR_SNAP`: each NHI's liveness and stage, each
   router entry's kind and approval), the `xhci98.sys` controllers' last
   flags (`TR_REDHC`: failed, a name) and 43.27's handed-over set
   (`TR_REDDEV`: the kind, a name); its answer is how many conditions
   hold and which is first, by kind and index. **The first** is the first
   in the table's order - a controller, then a device waiting for
   approval, then the slow devices - because a failed controller makes
   every other reading of its devices doubtful and a device waiting for
   approval does not work at all, where a slow one does; within a kind,
   the order of the inputs. Each NHI and each controller counts once,
   however many of its clauses hold; each router entry and each device
   counts once.

5. **The one place that applies it** (`tray_red`, `xhcitr.c`), called at
   the end of every poll and after every model refresh. It calls
   `tr_red_state`, composes the tooltip (item 6), and when the colour or
   the tooltip differ from what the icon shows, sends one `NIM_MODIFY`
   with `NIF_ICON | NIF_TIP`; when neither changed it sends nothing.
   `tray_add`, which the start and the `TaskbarCreated` broadcast (an
   Explorer restart) both call, adds the icon in the colour and with the
   tooltip `tray_red` last chose, so a restarted Explorer shows the state
   at once. The icon is always shown while the tool runs; there is no
   option to hide it, and nothing but the colour of the glyph changes:
   no speed, count or text is drawn in it.

6. **The tooltip.** While the icon is red, the tooltip names what is wrong
   in place of the device count (and on 98 SE in place of the last event,
   which the 98 SE pop-up, section 16, still shows): the first condition,
   with " and N more" when N others hold (`tr_red_tip`, pure). Its room is
   the `szTip` field the shell takes - 64 bytes with `shell32.dll` below
   5.0 (98 SE), 128 from 5.0 - less the terminating zero; the prefix
   "xHCI98 USB Tree: " is written only in the 128-byte field. The texts:

   | Kind | Text |
   |---|---|
   | `TR_RED_HC`, an NHI | "Thunderbolt/USB4 controller stopped answering" |
   | `TR_RED_HC`, an `xhci98.sys` controller | "<controller> stopped answering" |
   | `TR_RED_WAIT` | "<device> is waiting to be approved" |
   | `TR_RED_SLOW3` | "<device> runs below USB 3 speed" |
   | `TR_RED_SLOW2` | "<device> is slowed by a USB 1.1 hub" |

   The name is the one the tree shows (for a router, its vendor and
   model; for a USB device, its label with its drive letters first; for
   a controller, its devnode's name). The fixed words and " and N more"
   are always kept; the name is cut to the room left, ending "...", and
   a room of fewer than 4 characters writes "A device" (or "A USB
   controller") in its place. When the icon is its usual colour the
   tooltip is exactly what it was before this revision (owner,
   2026-10-22).

7. **A click.** While the icon is red, the left-click menu (section 7.1)
   begins with one item, the tooltip's text without the prefix, followed
   by "...", and a separator; choosing it, or a double click on the icon,
   opens the window on the concerned item - the first condition's - as
   the tree's own keys find it (section 3.4: the USB port's node for
   conditions 1 and 2, the router's for a waiting device, the NHI's or
   the controller's for a controller), refreshing the tree once when the
   window's model does not hold it, and the window's top node when even
   the refreshed tree does not (a view setting may hide that branch).
   When the icon is its usual colour the menu and the double click are
   unchanged. A balloon's click keeps whatever its own notice gives it
   (43.27's opens its list; section 16 item 6). While the icon is red
   this item replaces 43.27's own first menu line, "<n> USB device(s)
   can run faster..." (section 7.7 item 6), so the menu carries one such
   line, and 43.27's tooltip while red (section 7.7 item 5) is this
   item's tooltip. Where the icon is not red but 43.27's set holds a
   device (`TR_FW_LINK` devices alone, item 2), 43.27's line stays, so
   its list is still reachable from the menu.

8. **The red icon** (`xhcitr-f.ico`, `IDI_RED`), drawn as `xhcitr.ico`
   is (section 9): the same glyph, the same sizes (16, 32 and 48 pixels)
   and the same depths (4 bits per pixel in the 16 VGA colours, and 32
   bits per pixel), with the green of the stack beside the controller -
   VGA colour 10, 00FF00, in each 4-bit image, and the same colour in
   each 32-bit one - painted red, VGA colour 9, FF0000 (the owner,
   2026-10-24: the blue box stays blue, the green turns red, which reads
   more plainly at 16 pixels), and every other pixel, the masks
   and the white highlight unchanged. VGA colour 9 is one of the 16
   colours every display mode of 98 SE draws without dithering, and red
   on the taskbar's grey (C0C0C0) and on XP's blue and 7's dark taskbar
   reads as clearly as the blue it replaces. It is loaded as the usual
   icon is, at the small-icon size (`SM_CXSMICON`), so the shell picks
   the depth the display has. It is the file 43.27 added (section 7.7
   item 5), byte for byte the icon this section drew: one red icon serves
   every condition, and 43.27's own red state (its icon handle and
   tooltip) gives way to `tray_red`.

9. **High contrast.** Notification-area icons are drawn with their own
   colours in every high-contrast scheme, so the icon stays red there;
   the scheme is not read and no third icon is drawn. The colour is never
   the only carrier of the state: the tooltip names it and the menu's
   first item names it, both in the system's text colours, and both are
   read by a screen reader as any tooltip and menu are. A user who cannot
   tell red from blue loses nothing the tooltip does not give.

10. **Host vectors** (`test\test_trred.c`, over `xhcitr\tr_red.c`): each
    condition alone, holding and then clearing (a liveness back to
    active, a stage back to active, a device approved, a controller's
    flag cleared, the set emptied); OFF, revoked and an unknown liveness
    never counting, so a sleep does not turn the icon red; a USB4 entry's
    held approval and an ICM entry's failed approval not counting; an NHI
    lost and failed at once counted once; the ranking across kinds and
    within one; the count with every kind at once; and the tooltip at 64
    and 128 bytes - each text, a name cut, the " and N more" suffix up to
    a four-digit N, a name too long for any room, an empty name - never
    longer than its room. `xhci98.sys`'s flag in `test\test_xhq.c`: each
    terminal value 0 to 5 and its flag, and the flag at every level.

11. **Not done.** A condition on a controller that is not this package's
    (Windows' own USB stacks answer none of these readings); the
    Container ID pairing of a USB 3 hub's halves (section 7.7 item 1);
    and a red icon for a device whose approval failed, which no action
    of the tool's can put right.

The legs are with 43.28 (section 11, T.14).

### 7.3 Command line

`XHCITR [/tray] [/eject id=<instance id>] [/setlevel xhci98=<n>
xhcicm=<n>] [/report <file>]`. `/report`
writes the Save Report text and exits (for scripts and bug reports).
Exit codes follow XHCIINST's (record 23 section 10). Since revision 7
`/eject id=` names a unit or a USB device (section 6.5); since Phase 48
(2026-11-10) the ID of a devnode inside a unit (the drive in an
enclosure, behind its switch ports) names that unit, as the window's
Safely Remove on any node under a unit does, and the confirmation lists
the whole unit (before, such an ID was refused as "No USB device, and
no device behind a Thunderbolt port, has the instance ID ..."). Since revision 9
the report carries each "Driver log" node's text (section 3.5). Since
revision 11 `/eject` also takes `/result <name>` and `/confirmed <digest>`, which
the tool passes to its own elevated run (section 6.6) and which nothing
else needs. Since revision 13 `/setlevel` (section 7.5) writes one or
both drivers' levels and exits; with `/result <name>` it is the dialog's
elevated run. Since revision 29 `/set Name=Value ... [/on <id>]` and `/set
/show` (section 22) set and list the Settings window's values;
`/set /request /result <name>` is its elevated run.

### 7.4 Start at login, per user (revision 10)

- The machine's start is the Run value of section 8, written by XHCIINST
  and removed by `INSTALL /notray`; that is machine-wide and needs an
  administrator, as before.
- Each user can turn it off without one: "Start at login", a checkable
  item in the View menu and in the icon's right-click menu, writes the
  DWORD `TrayAtLogin` under `HKCU\Software\xHCI98\XHCITR` - 0 off, 1 on.
  The item shows the user's choice.
- `XHCITR /tray`, the start the Run value makes, reads it first: a DWORD 0
  ends the run at once, with exit 0, before any window, icon or thread.
  An absent value, another type or any other number starts as before; a
  plain start (no `/tray`) always opens. The decision is
  `tr_tray_login_start`, host-tested.
- Without the machine's Run value nothing starts at login whatever the
  user chooses, so the item is grey and reads "Start at login (off for
  this computer)".
- 98 SE and ME read the same key, from the user's profile or the default
  user's.

Addendum to revision 10, the tree's labels (the owner, 2026-10-16, after
two controllers of one name each showed a "Root hub" on the ThinkPad P14s
Gen 1):

- A controller's label ends with its vendor and PCI id from its instance
  ID ("- Intel 8086:02ED"; "- vendor XXXX" for a vendor outside the
  tool's short table). A Thunderbolt chip's own xHCI reads "- Thunderbolt
  3 controller's USB (8086:15C1)" ("USB4" or "Thunderbolt" by its NHI's
  published connection manager): an NHI in the walk on the same bus and
  device (an integrated controller), or behind a bridge beside the
  controller's under one switch (a discrete one); never a guess, so a
  PCH xHCI on the root bus is not one.
- The root hub reads "Root hub - N ports (a USB 2.0, b USB 3)" ("USB 3.x"
  until revision 22) from
  each port's `_EX_V2` protocols, without the split when a port did not
  report them; each port label carries its protocol ("[Port 15, USB 3]").
  No pairing of a connector's USB 2 and USB 3 ports is shown: the tool
  reads none.
- The same labels are in `/report` and Save Report, a file whose tree
  lines can be wider than 79 columns at depth, as before.

A second addendum to revision 10, resources (the owner, 2026-10-16):
every devnode's details - controllers, hubs, USB devices, an NHI, a
tunnel port, a unit with its bridges and functions - end with its PCI
location (bus:device.function, from `CM_DRP_BUSNUMBER` and `_ADDRESS` on
NT, from the instance ID on 98 SE and ME for a function on a root bus)
and a "Resources" list: what the configuration manager has allocated
(`CM_Get_First_Log_Conf` with `ALLOC_LOG_CONF`, then each descriptor's
data) - memory and I/O ranges, an IRQ (level or edge, shared or
exclusive; "message-signalled" for the negative number Vista and 7 give
an MSI), a DMA channel, a bus-number range. With a problem code it also
lists what the device asked for: the boot configuration, then the
filtered requirement list or the first basic one, so a device that wants
an IRQ and has none shows it. The routines are looked up at run time
from `cfgmgr32.dll` and need no rights; the descriptor layouts are
`cfgmgr32.h`'s packed ones, the same in the Windows 98 DDK and in WDK
7.1, except the x64 IRQ descriptor's 8-byte affinity. The decoding is
`tr_res_text`, host-tested. Physical ranges only; no kernel address is
shown. On 98 SE and ME the configuration manager arbitrates memory, I/O,
DMA and IRQ only, so no bus-number range is listed there; a type that only
a private arbitrator knows (67 on a PCI bridge, with no data) is listed by
its number as "Resource type 67 (no data)".

The claimed IRQ of mode 4 (roadmap-usb4 task 50.18, the owner,
2026-11-15): an `xhci98.sys` controller started under `XhciInterruptMode`
`4` with an IRQ claimed for its message (`XhciMsiStrategy` `0`, design
record 15 section 4.5.2) takes its interrupt through that IRQ, and the
line Windows assigned, which Windows still lists, is left unconnected.
Its allocated IRQ then reads "IRQ           16 (level, shared), not in
use in XhciInterruptMode 4", and a line follows it, "IRQ           7,
claimed by xhci98 for its message interrupt" (at the end of the list
where Windows gave no IRQ). The claim is read from the controller's
counter block in its query answer, the one section 19 item 5 reads once
a refresh: `IntrMode` 4, `IntrReason` 21, `IntrDelivery` MSI or MSI-X,
`Msg4ClaimStatus` 0, `Msg4Irq` nonzero and `Msg4Lost` 0
(`tr_msg4_claimed_irq`, `tr_res_text_m4` and `tr_res_claimed_text`,
host-tested). Every other case reads as before: the other modes, the
line borrowed under `XhciMsiStrategy` `1` (it is the line in use), a
refused start, a message lost since, logging level 0 (no counters
served), a driver before mode 4, and every devnode that is not such a
controller. The details pane and Save Report's tree carry it; the
device report's lines (`.DEV`, section 15) are the snapshot engine's
and are not changed.

### 7.5 The drivers' logging levels (revision 13)

The owner's decision of 2026-10-16: the tool shows and sets the level of
each driver's log, in place of `XHCISNAP -verbosity` and `SNAPON.BAT`.

1. **Where.** "Logging level..." in the View menu, and at the end of the
   right-click menu of an `xhci98.sys` controller, of an NHI and of each
   "Driver log" node (section 3.5), which answers its parent's driver:
   `level_dialog(owner, driver)` is the one entry point, and
   `lv_driver_of_node` names a node's driver.
2. **What is read.** xhci98: the REG_DWORD `XhciLogVerbosity` in the
   driver (software) key of every devnode the walk finds with service
   `xhci98` and a `PCI\` instance ID - every controller, not a root hub
   or a hub. xhcicm: `XhciCmLogVerbosity` in every NHI's driver key
   (service `xhcicm`, `PCI\`), and no other name: `xhcicm.sys` has never
   been released, so no older value is there to honour (the owner,
   2026-10-16). No value reads as the default, 2; a DWORD outside 0 to 4 is
   shown as it is, "out of range"; a value of another type is shown as
   not a REG_DWORD. The decision is `tr_level_pick`, host-tested.
3. **The dialog.** A list of each device and its reading (the same block
   as the report's), then the two drivers side by side, each with five
   radio buttons, one line per level: 0 off; 1 counters only; 2 the log
   (default); 3 adds port status; 4 adds memory addresses (for
   developers). A driver's buttons start at the level every one of its
   devices reads, and none is checked when they differ or a reading is
   out of range. Set writes a driver's level to every one of its devices
   when the checked level is not what they all read, and nothing else.
4. **The write.** `REG_DWORD` 0 to 4 under the driver's one value name,
   read back. A driver key is written only when it has the configuration
   manager's form (`tr_driver_key_ok`), and a value out of range is never
   written: the command line refuses it before anything is read.
5. **Rights.** On 98 SE and ME, and for an administrator on NT (section
   6.6's test), the window writes directly. A standard user on 2000 to 7
   sees "Set as administrator..." (the shield on Vista and 7), and Set
   starts section 6.6's elevated run with `/setlevel xhci98=<n>
   xhcicm=<n> /result <name>` in place of `/eject`: the same `runas`, the
   same section for the answer, the same wait. The elevated run takes
   only its arguments - each `xhci98=<d>` or `xhcicm=<d>` exactly, one
   digit 0 to 4, each driver once (`tr_level_arg`, `tr_parse_args`) -
   walks the device tree itself, writes those values into the keys it
   found and nothing else, answers and exits. A cancelled elevation
   changes nothing (`tr_level_elev_result`).
6. **The restart.** Each driver reads its level only at start, so after a
   write the writer itself confirmed, the tool offers to restart Windows
   (`ExitWindowsEx`, with the shutdown right enabled on NT). A run that
   did not answer is not offered one.
7. **The report.** `/report` and Save Report carry a "Logging levels"
   block before the tree, and a controller's and an NHI's details carry
   its own line.

### 7.6 The unexpected-removal notice (revision 16)

Roadmap-usb4 task 43.21. Decision T9 left the owner's request for a
notice on an unsafe unplug open because a 1 s sample cannot prove one, and
named the source that can: the child filter, which on NT sees Windows
surprise-remove a unit's device (record 21 section 7.4). This section
publishes what it sees and has the tray say so. It is NT-only (the
owner, 2026-10-18): 98 SE and ME offer no documented way to watch a
removal from outside the device's stack, and a unit gone there without an
eject the tool saw proves nothing - an eject from Windows' own applet, a
re-enumeration and a power loss look the same - so there such a unit is
shown as disconnected, as before.

1. **What is counted.** An attachment of `xhcicm.sys` on a PDO (a child
   filter of either kind, or a bridge filter on a child that is a
   bridge) that receives `IRP_MN_SURPRISE_REMOVAL` counts one unexpected
   removal in its domain when:
   - the attachment is bound to a marks entry of a listed domain (record
     21 section 7.4, revision 3: a PDO an active member's bridge
     reported). Whenever the table brings a current entry up to date
     from the attachment list (`cmMarkAtt`), each attachment on its PDO
     takes the entry's serial, its domain and that domain's start count
     (`CM_REENUM.Starts`, which a stop and start moves on while the
     table and its serials begin again); the event finds that entry by
     serial in that domain, only while the domain is listed at the same
     start and the entry's PDO is the attachment's, current or a
     tombstone - a tombstone
     because the PnP manager surprise-removes a device after the bus
     answer that no longer lists it, and the member bridge's filter,
     reading that answer on its way back, has already made the entry a
     tombstone (`cmMarksSyncAll`). Never by the PDO's address: a
     tombstone keeps the address of a PDO that is gone, which a later PDO
     may reuse, and a member's stop and start makes a new entry with a
     new serial for the same PDO, which the attachment then follows. The
     entry's own surprise-removal count (`CM_QM_E_COUNTS1`) is found the
     same way; until revision 16 it was looked up among the current
     entries by address, so an unplug's surprise removal did not reach it;
   - that attachment saw the device started: its start succeeded below
     it and no stop or removal followed;
   - the entry has not been counted before. A PDO is counted once however
     many of this driver's attachments it carries; a surprise-removed PDO
     never starts again, and a re-plug is a new PDO with a new entry; and
   - the device's removal is not owed to a loss of the host interface.
     It is, and is counted apart (`CM_Q_RM_LOST`, no notice), when at the
     event the domain's liveness is not ACTIVE, a loss is being drained
     (`CM_REENUM.LossDrain`), or the device's member is one a recovery
     owes or holds a retirement for (`CM_NHI.RecDebtMask`, accumulated
     during the attempt, and `RecBarred`, the barrier it becomes at the
     commit, record 22 section 7.7 part 4). The liveness returns to
     ACTIVE at the commit, before the barrier is serviced, and a
     member's bit leaves `RecBarred` only once `CmReenumRetired` has seen
     its old devices' removal finish - so a lost device's surprise
     removal, which comes before that removal finishes, finds its member
     barred even after ACTIVE. The commit moves the debt into the barrier
     (`RecBarred |= RecDebtMask`, then `RecDebtMask = 0`) under the
     registration list's lock, which the event holds while it reads both,
     so a member moving from one to the other is never seen in neither;
     every other write to either is a single aligned word. A loss the
     liveness has not yet noticed when the removal comes is not told
     apart.

   Never counted: a removal after a query-remove (an eject from any tool,
   a disable, a driver update, an uninstall: Windows does not
   surprise-remove those); a device that never started (no driver of its
   own, or a start refused while its tunnel is torn down for an eject,
   `CmRegStartAllowed`); a function deeper than a member's direct
   children, which has no marks entry (the devices of a chain's second
   router); a device no attachment of this driver sits on - a unit
   present at boot that was never marked, because the mark service is not
   installed or not running, or because the unit was never plugged in
   while it ran (the bridge filter attaches to a child at a plug, the
   mark's `LowerFilters` at every start; read by leg T.11 on 2000 and XP,
   2026-10-22: such a unit's unplug gives Windows 2000's Unsafe Removal
   dialog and the "disconnected" balloon, and no count); anything on 98
   SE and ME, where there is no filter. Windows
   surprise-removes more than an unplugged unit: a device whose driver
   reports itself failed (`PNP_DEVICE_FAILED`), a function its tunnel lost
   to a link fault, a unit gone across a sleep. That is why the notice
   says "removed unexpectedly", never "unplugged", and nothing about data.
2. **Which unit.** The child filter knows the device's entry and its
   member - the tunnel port, by bus, device and function - and nothing
   of a router. Which router a device ran behind cannot be proved from
   the driver's side: neither connection manager gives a continuity
   token at the moment a bridge's answer lists the device (under the
   ICM a replacement can be announced and approved before the bus answer
   that drops the old device; under the USB4 profile the member's
   connection generation is refreshed only when the re-enumeration is
   serviced, after the answer it caused may already have come). Nor can
   the tray prove it from what it sees leave: a router is keyed by its
   route or place, which a replacement shares, and a clean departure on
   the same port can fall beside another device's removal. So the key the
   answer carries for the last removal is its tunnel port, and the
   notice names no unit; the unit's own "disconnected" balloon, which the
   tray shows as before, names it where it left the record (accepted by
   the owner, 2026-10-22, in place of the roadmap entry's "last router
   key"). The count,
   the lost count and the last member live beside the marks table, under
   the registration list's lock, and are emptied with it at the domain's
   start and stop, so they are per start, as the epoch is.
3. **The answer, version 2.** `CM_Q_VERSION` is 2. The 64-byte header is
   version 1's. At offset 64 follows the removal block, `CM_Q_RM_BYTES`
   = 32:

   | Offset | Field |
   |---|---|
   | +0 | `CM_Q_RM_FLAGS`: `CM_Q_RF_WATCHED` (this domain's removals are watched: an NT target whose domain is listed on the filter route) |
   | +4 | `CM_Q_RM_COUNT`: unexpected removals counted since this start; it wraps |
   | +8 | `CM_Q_RM_MEMBER`: the last one's member, bus 15:8, device 7:3, function 2:0; `CM_Q_UNKNOWN` before the first |
   | +12 | `CM_Q_RM_LOST`: surprise removals in a loss of the host interface (item 1), counted apart; it wraps |
   | +16 | `CM_Q_RM_DRIVER`: the running driver's version (section 18) |
   | +20 | `CM_Q_RM_IRQ`: the ring interrupt as in force, and why it is polled (record 21 section 4.5.17) |
   | +24 | reserved, 0, to +31 |

   The record follows at 96 (`CM_Q_O_REC_OFFSET` says so), then the
   extensions and the instance ID, each at its offset as before;
   `CM_Q_BUILD_MAX` grows by the block. The answer the device holds
   before the thread's first pass carries the block as the table stands
   (count 0 after a start). The content generation follows the block like
   any other byte. The driver's own stamp clearing of the embedded record
   (`CmQueryPublish`) finds the record at its offset, not at 64.

   Readers: XHCITR takes versions 1 and 2, reading version 1 as "not
   reported by this driver"; the mark service (`xhcimark`), which reads
   nothing of the block, takes both. A reader from before revision 16
   refuses version 2, as the header's rule says; the package replaces
   its drivers and tools together (section 8).
4. **The tray.** The snapshot keeps each NHI's block (flags, count,
   member). Between two snapshots, for an NHI both hold at the same
   epoch, each with `CM_Q_RF_WATCHED` and the liveness ACTIVE, a count
   that differs gives one event, however much it moved (one unplug can
   remove several devices, counted in one burst; where a burst straddles
   two polls a second notice can follow, which is accepted): "A
   Thunderbolt or USB4 device was removed unexpectedly.", under the
   tray's warning title. It is decided within that one diff, so nothing
   waits across polls: a domain whose liveness has left ACTIVE in either
   sample gives none (it already says it stopped answering). The
   departures are shown as before: where the unit's router left the
   record, its own "<name> was disconnected." comes too, in the same poll
   or another, and names it. Balloons are 2000 and later anyway, and 98
   SE and ME never set `CM_Q_RF_WATCHED`. Item 8's option can turn the
   warning off. Where the departure falls in the warning's own poll, the
   warning's balloon carries it as a second line ("A Thunderbolt or USB4
   device was removed unexpectedly." then "<name> was disconnected."), and
   so every other event of that poll, each on its own line, a line that
   does not fit left out (`tr_events_text`): leg T.11 read on XP, on
   2026-10-22, that a second balloon from the same icon replaces the first
   at once, so the departure shown after the warning hid it.
5. **The window and the report.** The NHI's details carry
   "Unexpected removals:" with the count since this start and, after
   the first, the last one's tunnel port, and the count in a loss when
   it is not 0; "not watched on
   this system" without `CM_Q_RF_WATCHED`; "not reported by this driver"
   for a version 1 answer. Save Report and `/report` carry the same line.
6. **Windows 2000** shows its own Unsafe Removal dialog after an unplug
   without an eject (record 21 section 7.4, read by leg 39-V.2). The
   tray's notice comes as well there (the owner, 2026-10-22: every NT
   system, 2000 included), and item 8's option is the way to have only
   Windows' own.
7. **Release notes.** "The tray's unexpected-removal notice is shown on
   Windows 2000 and later only; on Windows 98 SE and ME a Thunderbolt or
   USB4 unit removed without an eject is shown as disconnected", with
   item 8's option.
8. **The option** (the owner, 2026-10-22). A checked item, "Warn when a
   device is removed unexpectedly", in the tray's right-click menu and
   the window's View menu beside "Start at login", on by default, per
   user: the DWORD `WarnRemoved` under the tool's settings key
   (`HKCU\Software\xHCI98\XHCITR`, where "Start at login" keeps
   `TrayAtLogin`, section 7.4), no driver value. It reads as section
   7.4's value does: only a DWORD 0 turns the warning off; absent,
   another type or any other number is on (`tr_warn_removed`,
   host-tested). With it off the tray skips the warning's balloon alone:
   the count still rises in the driver and shows in the details, and the
   "disconnected" balloons are unchanged. It is read when a warning is
   due, so a change takes effect at the next poll in every running copy
   of the user's tray. On 98 SE and ME, where there is no warning, the
   item is not shown.

### 7.7 The "can run faster" notice (revision 18, task 43.27)

Roadmap-usb4 43.27 (the Windows 8 reporter's idea, owner, 2026-10-20): on
every target under `xhci98.sys`, when a device that declares SuperSpeed or
SuperSpeedPlus in its BOS runs slower than it can, a balloon names it and
the tray icon turns red until every such device reconnects at its speed
or is removed; clicking shows the device, its port and hub, the speed it
runs at and the speed it can, and what to try. Its input is the
`_EX_V2` capable flags, which `xhci98.sys` answers on every connection
from record 13 section 8.4.1 (the BOS read at High and Full Speed, built
with this task).

**The owner's decisions of 2026-10-22** (picking from record 13 section
6.12.11's options for 43.30, then widening them the same day): option 1,
the BOS read at High and Full Speed for `bcdUSB` 0x0210 or more (record 13
section 8.4.1); option 3, this notice on by default on 8, 8.1 and 10, and
then **on by default on every target** - 98 SE, ME, 2000, XP, XP x64,
Vista, 7, 8, 8.1 and 10 - since under `xhci98.sys` Windows' own notice
appears on none of them; option 4, the release notes saying so; option 5,
the notice widened to **a High Speed device slowed by a Full Speed hub**,
on every target too. Option 2 (`_EX` `Speed` 3 on NT 6.2 and later) is not
taken. Delivery is section 7.2's rule: a `NIF_INFO` balloon where
`shell32.dll` is 5.0 or later, the tooltip on 98 SE (4.72). The user's
choice to turn it off stays (item 7). These items are 43.30's "can perform
faster" part, done here; the UAS client-contract half of 43.30 is separate.

1. **Which devices count** (`tr_fast_check`, host-tested). A port the
   scan read (section 5) counts when all of these hold: its controller is
   `xhci98.sys`'s (`UCTRL.Ours`); it is connected; the device is not a
   hub; `_EX_V2` answered; and the speed the device declares is above the
   speed it runs at. Declared: bit 3 (`SuperSpeedPlusCapableOrHigher`)
   SuperSpeedPlus, else bit 1 SuperSpeed, else nothing. Running: bit 2
   SuperSpeedPlus, else bit 0 SuperSpeed, else the `_EX` `Speed` byte
   (Low, Full, High); a byte the tool does not know never counts. So a
   SuperSpeed or SuperSpeedPlus device at High or Full Speed counts, and
   a SuperSpeedPlus device at 5 Gbit/s; a High Speed-only device never
   does, nor a USB 3 device whose BOS read failed (no flag). Windows' own
   controllers are not read: their stacks answer no capable flag before
   Windows 8, and on 8 and later Windows has its own notice.
   **A hub never counts itself.** A USB 3 hub is two hubs on two ports
   (record 13 section 10.13), and its USB 2.0 half runs at High Speed by
   design beside its SuperSpeed half; the two share a Container ID (USB
   3.2 section 11.3), which the tool does not read, so it cannot tell
   that half from a USB 3 hub on a USB 2.0 port. A SuperSpeed device
   behind a hub that runs slow counts itself, and its text names the hub.
   **The High Speed case** (option 5): a device that declares no
   SuperSpeed, runs at Full Speed behind a hub that runs at Full Speed,
   declares `bcdUSB` 0x0200 or more, and answers GET_DESCRIPTOR(DEVICE_
   QUALIFIER) (type 6, WDK 7.1 `usb200.h`'s
   `USB_DEVICE_QUALIFIER_DESCRIPTOR`, 10 bytes) with a descriptor of
   bLength 10 and type 6, counts, its declared speed High. USB 2.0 section
   9.6.2 (from memory, not read here: no USB 2.0 text is in
   `docs/references`) gives a High Speed-capable device that descriptor
   and has a Full Speed-only device refuse it; USB 3.2 section 9.4.3
   (read) has a SuperSpeed device refuse it at Gen X speed only. The
   driver keeps no such reading, so the tool asks
   (`IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION`, which `xhci98.sys`'s
   door forwards to the device) **once per candidate**: only for a port
   `tr_fast_wants_qual` admits (the conditions above but the answer), and
   only when its key (item 4) has no settled answer kept from earlier
   refreshes (`TR_QUAL_CACHE`, room for one per port of the tool's model,
   1024, `TR_QUAL_MAX`; nothing in it is ever replaced, and a new key that
   finds no room is not asked, `tr_qual_room`; `tr_qual_get`, `tr_qual_put`, `tr_qual_begin`,
   `tr_qual_end`, host-tested). A short or malformed answer is "Full Speed
   only" and is settled at once. No answer at all is not: the hub request
   gives one result for a STALL (a Full Speed-only device's answer) and
   for a transient failure (a suspended device, a request cancelled, an
   allocation or open that failed), so it is asked again at the next
   refreshes and settled as "Full Speed only" after the third such
   failure (`TR_QUAL_TRIES`); until then it is not counted. An answer of
   10 bytes of type 6 settles "High Speed capable". Each refresh's scan
   drops the answers of the candidates it did not see that a device's
   answer settled, so such a device is asked again when it comes back;
   one that had a request go unanswered is kept, and settled as "Full
   Speed only" if it was not yet, so a device that went away after an
   unanswered request (perhaps with a controller recovery that request
   caused, if `xhci98.sys` could not withdraw it) is not asked again for
   as long as the tool runs. The scan marks every candidate present before
   it sends any request, so that dropping happens before a new key is
   added (43.27, reviewed and
   revised 2026-10-22).
   On a root port a High Speed device runs at High Speed (every USB 2.0
   root port of an xHCI controller is High Speed), so a root port is
   never asked. Nor is a device whose Full Speed hub is itself behind a
   hub at High Speed or faster, or behind one whose connection was not
   read (`UnderTt`): it is behind a TT, where `xhci98.sys` cannot withdraw
   a request that is never answered from EP0 (`hcdEp0Withdraw`, record 13
   section 8.4.1) and resets the controller instead, so the tool's own
   request could cost every device on it. Such a device is not counted;
   the cost is the notice for a High Speed device behind a Full Speed hub
   behind a High Speed hub. Only the refresh's scan asks; the details
   pane and item 6's list read the answers it settled, so drawing them
   neither sends a request nor uses up a try.
2. **Where it is, and what to try** (`TR_FW_*`, `tr_fast_where`,
   `tr_fast_try`):

   | Where | Read as | Try |
   |---|---|---|
   | `TR_FW_PORT2` | on a root port, below SuperSpeed (a root port at High or Full Speed is a USB 2.0 port: a USB 3 connector's SuperSpeed port carries nothing slower) | "Connect it to a USB 3 port of this computer (often blue, or marked SS). If it is already in one, it fell back to USB 2.0 there: try another cable or another port." |
   | `TR_FW_HUB2` | behind a hub at High Speed that declares no SuperSpeed | "Connect it to a USB 3 port of this computer, or through a USB 3 hub." |
   | `TR_FW_HUB1` | behind a hub at Full Speed that declares no SuperSpeed | the same; for the High Speed case (declared High Speed): "Connect it to a port of this computer, or through a USB 2.0 or USB 3 hub, rather than through this Full Speed hub." |
   | `TR_FW_HUB3` | behind a hub that declares SuperSpeed and runs below it | "Connect the hub to a USB 3 port of this computer with a USB 3 cable. If it already is, try another cable for the hub, then for the device." |
   | `TR_FW_LINK` | a SuperSpeedPlus device at 5 Gbit/s | "Connect it to a port rated 10 Gbit/s or more (USB 3.1 Gen 2 or USB 3.2), with a cable rated for that speed. A 5 Gbit/s port is the most it gets there." |
   | `TR_FW_WIN2` | on a USB 2.0 port Windows' own driver runs (item 11) | "Connect it to a USB 3 port of this computer." |

   A hub whose own connection was not read (another controller's, or
   `_EX_V2` refused) is taken as `TR_FW_HUB2` by its speed.
3. **When it is read.** At every model refresh (`refresh_model`): the
   start, F5 and the window's automatic refresh, and the tray's refresh
   500 ms after each `DBT_DEVNODES_CHANGED`. Nothing new is polled. No
   device is asked anything but item 1's High Speed candidate, until its
   answer is settled (three requests at most while it stays on its port),
   and item 4's product string of a counted device with no devnode (one
   pair of requests while it stays on its port); otherwise the scan's
   `_EX` and `_EX_V2` requests are all it reads.
4. **The set and its balloons.** Each counted device is keyed by its
   controller's driver key, its port path and its VID:PID (`tr_fast_key`);
   the tool keeps the last refresh's keys, 32 at most (`TR_FAST_MAX`; a
   33rd is counted, never keyed, and gets no balloon of its own). A key
   that was not in the last set is new (`tr_fast_new`), and the new keys
   of one refresh get one balloon, title "A USB device can run faster":
   for one, "<name> runs at High Speed (480 Mbit/s) on a USB 2.0 port;
   it can run at SuperSpeed (5 Gbit/s). Click here for what to try."
   (`tr_fast_balloon`; the "on a ..." part from section 2's "Where"); for
   several, "<n> USB devices run slower than they can: <name> and <n-1>
   more. Click here for what to try." The tray's first refresh treats
   every counted device as new, since the reporter's case is a stick left
   in at login. The name is the device's as the tree labels it (its drive
   letters first where it has some, section 6.5), the same function
   naming both (`fast_name`, `tr_fast_name`, host-tested): its devnode's
   name where Windows gave a specific one; else its product string
   (`iProduct`, spaces at either end taken off); else its devnode's name
   where that is one of Windows' class names; "USB device <VID:PID>" only
   when none is there (task 43.78). The class names (owner, 2026-10-24:
   they say what kind of device it is, not which) are
   `tr_name_generic`'s, host-tested, matched whole with case aside: "USB
   Input Device", "USB Human Interface Device", "Human Interface
   Device", "HID Keyboard Device", "HID-compliant mouse", "HID-compliant
   device", "HID-compliant consumer control device", "HID-compliant game
   controller", "HID-compliant system controller", "USB Composite
   Device", "USB Mass Storage Device", "USB Device", "Unknown Device",
   "USB Printing Support", "USB Audio Device", "USB Printer", and the hub
   names "Generic USB Hub", "USB Hub", "USB 2.0 Hub", "Standard USB Hub";
   and by prefix "Unknown USB Device" and a hardware ID ("USB\...")
   standing in for a name. A name with a disk's name or drive letters
   (section 6.5) is specific. A device just plugged in has no devnode for the tree while
   Windows installs its driver, since the driver key the tree matches it
   by is written then; the balloon comes at that refresh, so the 3.2.0.0
   matrix (2026-10-24, QEMU's usb-kbd and usb-mouse behind a Full Speed
   hub on 7 and XP) saw "USB device 0627:0001" where the device answers
   "QEMU USB Keyboard". For such a device, and for one whose devnode
   has only a class name, the scan asks the string (string
   descriptor 0 for its first language, 0x0409 if none, then `iProduct`,
   through the hub as item 1's request goes), once per key while it stays
   (kept by key, answered or not, for the keys of the last refresh, and
   kept while the key stays even once a devnode names the device), and
   never for one behind a TT (item 1's reason) or with `iProduct` 0. The
   details pane's strings, once read, serve as well (so the tree names
   a device it did not count by its product string only once that pane
   has read it). **The hold** (43.78):
   so that the balloon names the device as the list and the tooltip do
   (the matrix read a stick's balloon as "USB device 46F4:0001" and its
   list entry and tooltip as "QEMU QEMU HARDDISK USB Device (G:)"), a
   balloon whose first device has no devnode at that refresh is held:
   each second's tick (`TIMER_POLL`) reads the model of the last refresh
   and sends it, its name read again, once that device has a devnode and
   3 seconds have passed since the hold began (`FAST_SETTLE_MS`, room for
   its disk and letters), or after 15 seconds whatever it has
   (`FAST_HOLD_MS`). New devices of the refreshes while it is held join
   its count. While it is held, item 9's re-send of an earlier balloon
   is not made (`fastText` is the held one's), and one released while the
   icon is away goes at the first tick after the icon is back. A device
   that has its devnode at the refresh that finds it
   (the tray's first refresh, a device plugged in again) is not held.
   Read on 7 x86 in QEMU with usb-kbd and usb-mouse behind a Full Speed
   hub, before the class names were set aside: "2 USB devices run
   slower than they can: USB Input Device and 1 more." (the 3.2.0.0
   build: "USB device 0627:0001 runs at ..."). Where the shell has no
   balloons (98 SE) the red icon's tooltip says it (item 5; its 63
   characters hold the count, not the balloon's text), and the menu's
   first item opens the list (item 6). A balloon owed before the icon is
   added (the start's first refresh comes before it) is shown once it is.
5. **The icon.** Since revision 19 the icon, its tooltip while red and
   the menu's first line are section 7.2.1's, which takes this set as
   two of its conditions (a `TR_FW_LINK` device is not one, section 7.2.1
   item 2); what follows is this revision's own text. Red while the
   notice is on and the set holds a device:
   `xhcitr-f.ico` (since revision 19 loaded as `IDI_RED`), which is `xhcitr.ico` with the
   stack's green - VGA colour 10, 00FF00, in each 4-bit image, and
   the same colour in each 32-bit one - painted red FF0000, every other
   pixel unchanged. The usual icon again once the set is empty: each
   counted device reconnected at its speed (the refresh after the plug
   reads it so) or removed. The tooltip while red: "xHCI98 USB Tree: <n>
   USB device(s) can run faster", set again whenever a refresh changes the
   count while the icon stays red. An Explorer restart re-adds the icon in
   the state it was in.
6. **Clicking.** The balloon's click (`NIN_BALLOONUSERCLICK`, which the
   shell sends after `NIM_SETVERSION` 3; only for a balloon of this
   notice, the one on the screen (below); not on 2000, below) and, while the icon is red, a first item
   of the left-click menu, "<n> USB device(s) can run faster..." (since
   revision 19 shown only while the icon is not red and the set holds a
   device, `TR_FW_LINK` alone; while red the menu's first item is
   section 7.2.1 item 7's, which opens the tree on the device), open one
   window, "A USB device can run faster", listing every counted device
   (`tr_fast_entry`) in a read-only text box that scrolls, with a Close
   button (Escape and Enter close it too): 80 columns of its fixed font
   wide and two thirds of the work area high, never wider than the work
   area, so it fits a 640 by 480 screen, and resizable; one
   such window at a time, its text read again when it is asked for again.
   The text is built in a buffer that doubles from 16 KiB to 1 MiB (60
   KiB on 98 SE and ME, whose edit control holds 64 KiB) until it fits,
   each entry whole or not at all; past that it ends with "<n> more devices are not listed." (A
   message box was the first build; it neither scrolls nor fits several
   devices on a 640 by 480 screen.) Each entry: its name
   and VID:PID; "Port <path> of <controller>"; "Behind <hub> (<its
   speed>)" when it is behind a hub; "Runs at <speed>"; "Can run at
   <speed>"; "Try: <section 2's text>". The window's details of the
   device carry the same lines under "Can run faster", and so do Save
   Report and `/report`, whether the notice is on or off: they are a
   reading, not a notice.
   **Which balloon is clicked** (revision 20, task 43.72). The shell's
   messages name no balloon, and a balloon sent while another of the
   icon's is up may replace it at once (a `NIN_BALLOONSHOW` then follows)
   or wait until it goes, so the kind of the last balloon sent - what the
   click went by until this revision - is not always the kind on the
   screen: an eject's outcome sent while the notice's balloon was up
   made a click on the notice's open nothing, and the reverse could open
   the list for another notice. The tray keeps the kind each
   `NIN_BALLOONSHOW` saw sent (`tr_bkind_step`, `tr_bkind_fast`,
   host-tested), none after a hide, a timeout, a click or the icon's
   balloon taken away (item 9's re-send), and a click opens the list only
   when that kind is the notice's. The balloons sent and not yet shown
   are kept in order (the last sixteen): a show is the oldest's where the
   shell queues them and the newest's where it replaces them, so it is
   that kind when they are all of one kind and counts as another
   balloon's when they are not - a click on it then opens nothing, the
   icon's menu still leading to the list or the device, rather than the
   list for a notice that is not the one clicked. Each show takes the
   oldest away, and a hide, a timeout, a click or the icon's balloon
   taken away (item 9's re-send) ends the one shown and leaves those
   waiting, since a show or a click the shell posted before it may still
   come; one the shell dropped unshown keeps only the next show in doubt,
   more waiting than are kept (sixteen) are counted, and their shows,
   which come first, are in doubt; so is a show with none waiting. A shell not heard from in the session (and not Vista or
   later, item 9) is read by the last balloon sent, as before.
   **Windows 2000 (task 43.68, 2026-10-23).** Read on 2000 SP4 in QEMU
   with a build that logged every `WM_TRAYICON` message (`runs/run-43.md`,
   "43.68"): the notice's balloon was shown and a click on it closed it,
   and the tool received no message at all - no `NIN_BALLOONSHOW`, no
   `NIN_BALLOONUSERCLICK` - while the icon's own right click arrived as
   `WM_RBUTTONDOWN`, `WM_RBUTTONUP` and `WM_CONTEXTMENU` (so
   `NIM_SETVERSION` 3 had taken). 2000's shell does not report balloons,
   and nothing the tool can do makes the balloon's click open the list
   there. So on 2000 (NT 5.0, `tr_fast_click_balloon`) the balloon ends
   "Click the icon for what to try." instead of "Click here for what to
   try.": the icon's left click opens the menu, whose first line opens
   the tree on the device while the icon is red, or the list while only
   a `TR_FW_LINK` device is counted (item 6; 43.28 read the list on 2000
   before revision 19). Every other system keeps
   "Click here". (Item 9's watch learns nothing on 2000 either, and so
   never re-sends there.)

7. **On or off** (`inst_fast_default`, `inst_fast_on` in
   `xhciinst\inst_core.c`, which both tools link; host-tested).
   - The user's choice: "Warn when a device can run faster" ("Tell me
     when a device can run faster" until the owner's wording of
     2026-11-15, revision 36), a
     checkable item in the View menu and in the icon's right-click menu,
     writes the DWORD `FasterNotice` under `HKCU\Software\xHCI98\XHCITR`
     (0 off, 1 on), as "Start at login" writes `TrayAtLogin`.
   - The computer's: the DWORD `TrayFasterNotice` under
     `HKLM\Software\xhci98`, the installer's own key (record 23 section
     9.6), written by the installer only (its Components page and `/faster`
     and `/nofaster`, record 26 section 5.3; record 23 section 9.5).
   - In force: the user's DWORD 0 or 1, else the computer's DWORD 0 or 1,
     else this system's default. Another type or another number counts
     as absent.
   - The default (43.27's rule: on where Windows shows no notice of its
     own under this driver; the owner's decision of 2026-10-22 above):
     **on on every system**. Under `xhci98.sys` Windows' own notices
     appear on none, but for one case below: 7's High Speed balloon is
     raised by `usbhub.sys`,
     which this bus replaces (measured on 7, record 13 section 6.12.11),
     and 8's and 8.1's Explorer line needs a running `usbxhci`
     controller (static, the same section); 10's is assumed to need the
     same until 43.33 (b) reads it, and a reading that says otherwise
     goes back to the owner. `inst_fast_default` keeps the system as an
     argument for that. The case: Explorer's test accepts any working
     `usbxhci` controller, so on an 8, 8.1 or 10 computer where another
     controller runs Windows' own driver, its line may show for a stick on
     an `xhci98.sys` controller too (record 13 section 6.12.11, "A
     computer with both drivers"), beside this notice. The tool keeps its
     own on there (the orchestrator's choice, for the owner to confirm):
     reading the other controller's service to stay quiet would rest on
     a test of Windows' that is static and unread on 10, and a double
     notice tells the user nothing wrong. 43.28 reads such a computer.
   - Off: no balloon, no red icon, no menu line. Turned on, the icon
     turns red at once when the set holds a device, with no balloon;
     turned off, it goes back. (Since revision 19: off, this set turns
     nothing red; a condition of section 7.2.1 outside it, a controller
     or a device waiting for approval, still does. On, a `TR_FW_LINK`
     device alone does not.)
8. **Not done.** Windows' own controllers' devices, but for item 11's
   option; the pairing of a USB 3 hub's two halves (item 1). Until
   revision 24 the top rate of a port was not done either: no request
   the tool sent read it (the root hub answers no speed table), so a
   SuperSpeedPlus device on a computer whose every port runs at 5
   Gbit/s stayed counted (balloon, list) until the notice was turned
   off, and "Can run at" said SuperSpeedPlus for it at High Speed too
   (the report of 2026-10-28, item 10). Item 10 reads it now.

9. **A balloon the shell did not show** (task 43.66, 2026-10-23;
   `tr_bsend_step`, host-tested). Item 4 gives each new key one balloon,
   and 43.28's legs found 7 dropping that balloon on a stick's first
   plug: Windows' own "Your device is ready to use" balloon held the area
   for about 35 s and ours never followed (`runs/run-43.md`, "43.28", the
   missing first balloon), so on 7, whose new icon sits in the overflow,
   the user saw nothing. After `NIM_SETVERSION` 3 the shell answers a balloon with
   `NIN_BALLOONSHOW` (`WM_USER` + 2) when it shows it,
   `NIN_BALLOONTIMEOUT` (+ 4) or `NIN_BALLOONHIDE` (+ 3) when it goes,
   and `NIN_BALLOONUSERCLICK` (+ 5) for a click (Windows SDK
   `shellapi.h`, documentation; which of them 7 sends for the dropped
   balloon is read in 43.66's leg). The notice's balloon is watched from
   the moment it is sent:
   - shown (`NIN_BALLOONSHOW`): done; nothing is ever sent again for it;
   - gone without being shown (a timeout or a hide before any show):
     lost, and sent again, once, 40 s later (`TR_BS_WAIT_MS`), long
     enough for a balloon of Windows' own to have gone; a show in those
     40 s still ends it (the messages name no balloon, so the "gone" may
     have been an older balloon's, posted before this one was sent);
   - no answer at all within 60 s (`TR_BS_NOSHOW_MS`): sent again at
     once, but **only where the shell has sent one of the four messages
     in this session**, for any balloon of the tool's, **or the system
     is Vista or later** (NT 6.0 and up), whose shell was read answering
     and dropping a queued balloon without a word (below), so a shell that never sends
     them is never sent a balloon twice;
   - the re-send gone unshown or unanswered too: given up;
   - another balloon of the tool's sent meanwhile (a removal's, an
     eject's), the balloon clicked, the device it names no longer
     counted, or the notice turned off: no longer owed (nor, then, a
     balloon still waiting for the icon to be added).
   A re-send first takes away the icon's balloon, shown or queued, the
   documented way (`NIM_MODIFY` with `NIF_INFO` and an empty `szInfo`,
   Microsoft's `NOTIFYICONDATA` page), then sends it again, so a balloon
   the shell had only queued cannot be shown beside its re-send; that a
   new `NIF_INFO` alone replaces a queued one is not promised there. **Read on 7 x86 (QEMU,
   2026-10-23, `runs/run-43.md`, "43.66"; a build that logged each
   message):** a balloon sent while Windows' own "Your device is ready
   to use" holds the area gets no message at all - no show, no timeout
   - for as long as Windows' stays (it stays while the user is idle,
   minutes on end); with the user active, ours is shown about 10 s after
   it was sent and timed out 8 s later when Windows' replaced it, so a
   shown balloon is reported as shown. The silence is why the no-answer
   case exists, and why Vista and later count as answering from the
   start: the first balloon of a session (43.28's case) has no earlier
   answer to learn from. On 7 the re-send at 60 s then waited in the
   shell's queue and was shown once Windows' had gone, once, the
   original not shown beside it. One race is left as it is: a
   show still queued for another balloon of the tool's sent just before
   the notice's (both in one poll) is taken as the notice's, and the notice
   is then not sent again if it was in fact lost - the behaviour before
   this item, never a second notice (reviewed 2026-10-23). Where it was shown nothing changes:
   XP showed ours after its "Found New Hardware" balloon (43.28), and a
   shown balloon is never sent again. 98 SE has no balloon (section 16)
   and nothing changes there. The text sent again is the one first sent,
   naming the device it named; the 60 s and 40 s are judgements, not
   readings, to be read again against 7's timing in the leg.

10. **What the path can give** (revision 24, roadmap-usb4 44.80). A
    user's report (2026-10-28, XP, a VIA VL805, a USB 3 port of 5 Gbit/s
    at most): an RTL8157 (SuperSpeedPlus-capable) behind a hub plugged
    in by its USB 2.0 pins was listed "Can run at: SuperSpeedPlus (10
    Gbit/s or more)", and plugged in properly it runs at 5 Gbit/s, the
    best that computer gives, yet stayed counted (`TR_FW_LINK`). Item
    1's "declared" was the device's own top, never held to the path.
    Since this revision the speed it can run at is the least of:
    - what it declares (item 1);
    - the fastest this computer's xHCI98 USB 3 ports run
      (`tr_fast_top_add`, host-tested): each `xhci98.sys` controller's
      query answer, version 3's ports part (record 13 section 8.12), read
      once a refresh (`fast_top_read`, `xhcitr.c`; the whole answer the
      transport part already reads): SuperSpeedPlus when a port says so,
      else SuperSpeed when a port is USB 3; a controller whose ports
      cannot be read (a driver before 44.80, a refused or failed query)
      counts as SuperSpeed, never SuperSpeedPlus, since nothing says
      more; none when no controller has a USB 3 port. The computer's
      best rather than the device's own root port's: the advice of
      every kind but `TR_FW_HUB3` is to move the device to another port
      of this computer, and the root port a fallen-back path is on is a
      USB 2.0 port, whose USB 3 partner is paired by convention only
      (AGENTS.md, "Port Strategy");
    - SuperSpeed when a hub on the path holds it there
      (`tr_fast_hub_cap`, host-tested): a hub whose connection declares
      or runs at SuperSpeed without declaring SuperSpeedPlus, or whose
      connection or `_EX_V2` was not read. A hub that declares no
      SuperSpeed (a USB 2.0 hub) holds nothing, since the advice for it
      is to go around it.
    A device counts only when it runs below that, so on a computer whose
    USB 3 ports run at 5 Gbit/s a SuperSpeedPlus device at SuperSpeed is
    never counted, and the report's case still counts, "Can run at:
    SuperSpeed (5 Gbit/s)", `TR_FW_HUB3`. A SuperSpeed device is never
    raised, and the High Speed case (item 1) does not depend on it. With
    no xHCI98 USB 3 port at all nothing counts but that case. The
    details pane says what is not counted, for a device declaring
    SuperSpeedPlus held to SuperSpeed (`tr_fast_note`, host-tested): "Note:
    This device can run at SuperSpeedPlus." and "This computer's USB 3
    ports run at 5 Gbit/s at most." or "A hub it is behind runs at 5
    Gbit/s at most."; the tray says nothing of it. Not done: a port's
    own top among several of different rates on one controller (the
    computer's best is taken), and a controller that states
    SuperSpeedPlus in a PSI table whose connector or retimer cannot
    carry it (the capability is all that is read).
11. **Windows' own ports** (revision 24; the owner, 2026-10-28: an
    option, off by default). "Watch Windows' own USB ports too", a
    checkable item under "Warn when a device can run faster" in the
    View menu and the icon's right-click menu, greyed while the notice is
    off, writes the DWORD `FasterWindowsPorts` under
    `HKCU\Software\xHCI98\XHCITR` (1 on; absent, 0, another type or
    number off; there is no computer-wide value). On, and with the notice
    on, a port of a controller Windows' own driver runs counts when all
    of these hold (`tr_fast_wants_bos`, `tr_fast_check`, host-tested):
    the controller is EHCI, OHCI or UHCI by its PCI class (`0C0320`,
    `0C0310`, `0C0300`; an xHCI controller of Windows' own is never
    watched: Windows has its own notice where it drives one); a device
    is connected and is not a hub; its device descriptor's `bcdUSB` is
    0x0210 or later (Windows asks a BOS from 0x0201; a USB 3 device at
    High Speed reports 2.10, so `bcdUSB` alone says nothing of
    SuperSpeed); this computer has an xHCI98 USB 3 port (item 10); and
    its BOS, asked through Windows' hub
    (`IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION`, type 0Fh, its
    5-byte header and then the whole, `fast_win_bos`), declares
    SuperSpeed (capability 03h) or SuperSpeedPlus (0Ah) (`tr_bos_top`,
    host-tested). It is asked once per plug: the answer is kept by
    item 4's key with the device's address (`tr_wbos_*`, host-tested,
    64 keys; a new key with no room is not asked), dropped by the scan
    that no longer sees it, and any failure (the hub refusing type 0Fh,
    a stall, a malformed set) is kept as "unknown", with no notice. It
    runs at the `_EX` speed, or on an OHCI or UHCI controller Full Speed
    (Low Speed when the original request says so) when only the
    original request answered; on EHCI with no `_EX` it does not count.
    It can run at the least of what its BOS declares and item 10's
    computer's best. Its kind is `TR_FW_WIN2`, "on a USB 2.0 port",
    "Try: Connect it to a USB 3 port of this computer.", and it turns
    the icon red as `TR_RED_SLOW3` (section 7.2.1 item 2). Turned on or
    off, the model is read again at once; so is it when the notice is
    turned on with the option on, with no balloon (item 7: turning the
    notice on sends none). Off by default because the
    request goes to a device on a stack this package does not run; the
    request is the one Windows' own hub sends at enumeration for such a
    device, and is sent only for the devices the gate admits. Off, only
    xHCI98's ports are watched, as before. Read on XP (EHCI: one BOS pair
    per plug, none with the option off) and on 98 SE, where the tool's
    scan lists no port of Windows' own UHCI controller at all, so the
    option asks nothing and says nothing there (`runs/run-44.md`,
    "44.80").

The legs are roadmap-usb4 43.28 (section 11, T.13), and 43.66's for
item 9; 44.80's for items 10 and 11 (`runs/run-44.md`, "44.80").

### 7.8 The USB drive notice (revision 30, task 48.17)

Roadmap-usb4 48.17 (was TRSTORNOTE.1; the owner, 2026-11-07, into Phase
48 on 2026-11-09): an optional notice in the tray when a USB drive on a
controller `xhci98.sys` drives is connected or removed, off by default,
turned on by a tick box in the Settings window, per user; the same path
as the Thunderbolt and USB4 notices (section 7.2: a balloon on 2000, ME
and later; on 98 SE the tooltip's last event and the tool's pop-up of
section 16), naming the drive and its letter where known. Its main use is
98 SE without NUSB, where a USB drive has no storage driver, so no disk
and no letter, while `xhci98.sys` still enumerates it and the tool still
lists it. A draft of 2026-11-07 was reviewed and revised into this
section; its observation model, identity and delivery are replaced.

1. **The setting** (`tr_stor_on`, host-tested). The DWORD
   `StorageNotice` under `HKCU\Software\xHCI98\XHCITR` (section 7.4's
   key): only a `REG_DWORD` 1 is on; absent, 0, another type, size or
   number is off. No computer-wide value, no installer box, no driver
   value. The Settings window's tool tab (section 22 item 16) has the
   tick box "USB &drive notices" under "Watch Windows' own USB ports
   too", at the left margin; the quick-save hotkey's and its sound's rows
   move 14 DLU down (the dialog keeps its 330 by 262 DLU). A click writes
   the value at once, as the tab's other boxes do, and the box is then
   filled again from what reads back, so a write that failed shows as it
   is. No View-menu or icon-menu item: the owner named the Settings
   window. Turned on, the next scan only records the drives there
   (item 6), with no notice; turned off, the set, the queued notices and
   any watched re-send (item 10) are dropped, and nothing more is read
   for it.
2. **What counts** (`stor_scan` in `xhcitr.c`, `tr_stor_*` in
   `tr_core.c`). A port of the model (section 5) whose controller is
   `xhci98.sys`'s (`UCTRL.Ours`), connected, not a hub, and either:
   - holding a storage record in its controller's query answer (section
     19: version 2 or later, the record at the port's root port and
     route, `uport_xport`) - any record, UAS, Bulk-Only or refused, for
     the device or any interface of a composite one. The bus writes it
     when it makes the PDO, with no class driver above it, so a drive on
     98 SE without NUSB counts; or
   - where the answer is version 1 (a driver before 3.3.0.0), its device
     descriptor's class 08 (`Conn.Dev[4]`): a drive that declares its
     class per interface is not seen there.
   No request is sent to a device to classify it. The query is the one
   every refresh already makes of each `xhci98.sys` controller
   (`fast_top_read`, section 7.7 item 10).
3. **When.** At the end of every model refresh (`refresh_model`, after
   `fast_scan`), by whichever path: the tray's refresh 500 ms after each
   `DBT_DEVNODES_CHANGED`, F5, the window's automatic refresh and the
   poll's change. Nothing is polled anew. A plug makes the bus's PDO and
   so a devnode, with or without a driver, which is the devnode change
   the refresh follows; an unplug removes it. Except on 98 SE (read in
   this task's leg): there a stick with no driver brought no
   `DBT_DEVNODES_CHANGED` until its Add New Hardware wizard was dismissed,
   and its unplug none at all, so a drive came and went unseen until
   another refresh. So, while the notice is on, each second's tick also
   asks each `xhci98.sys` controller for its query answer's header alone
   (88 bytes, `stor_poll`; no device request), and a storage part whose
   size changed since the last tick asks for the same refresh a devnode
   change does (`TIMER_CHANGE`, 500 ms), on every system. The bus writes
   a record when it makes the PDO and drops it with the PDO, so a plug
   and an unplug change the size; a swap within one second that keeps it
   is not seen this way. The baseline is the last tick's own reading
   (`TR_STOR_POLL`, `tr_stor_poll_step`, host-tested), never the
   refresh's query, whose ports are read before its query is made and so
   can sit either side of an unplug; and a controller with no baseline -
   the first tick after the tray starts or the notice is turned on, or a
   controller new to the model - asks for one refresh too, so a drive
   gone between the refresh that counted it and that first tick (the tray
   started with a stick in, and the stick pulled at once) is seen. A
   failed read is kept as a baseline like any other, so a controller that
   keeps failing asks for no refresh after its first. The baseline is
   dropped whenever the tick box is clicked and whenever the set is
   (item 1), so turning the notice off and on again while the Settings
   window holds the poll still gives its first tick after the close no
   baseline. While the Settings window
   is open the model is held (`setOpen`) and no refresh runs; a refresh
   asked for meanwhile (`setMissed`, set where `refresh_model` returns
   for `setOpen`) is made when the window closes, whether or not an
   option changed, and so is one whenever the notice is on (`stor_poll`
   keeps its baseline while the window is open, but a drive gone and
   another come meanwhile can leave the size as it was); it announces
   what changed as any other. Only where
   the icon is (the tray, `hTray`).
4. **The key: one attachment** (`tr_stor_key`, host-tested). The
   controller's driver key (its device path where it has none), the port
   path and VID:PID, as section 7.7 item 4's key, and the record's Slot
   ID (0 for item 2's class-08 case): the controller gives a device a
   slot at each enumeration, so the same drive plugged in again between
   two refreshes is, where its slot differs, one removal and one
   arrival. Where the controller gives it the same slot (it may reuse the
   lowest free one) such a replug is not seen; nor is a swap of two drives
   of one VID:PID between two refreshes with the same slots. No serial
   number is read: a key must not change when a read fails.
5. **What the last refresh's drives are now** (`stor_resolve`). Each
   drive kept from the last refresh is, in this one:
   - **present**: its key is among this refresh's counted drives;
   - **unknown**: its controller is not in the model, is not
     `xhci98.sys`'s, or did not answer its query in this refresh; or its
     port is in the model, read, connected, with the same VID:PID and no
     record (the PDO not made yet, or being made again); or its port is
     not in the model and the nearest port on its path that is, is a
     connected hub (its ports not read), or no port on its path is (the
     root hub not read);
   - **gone**: its port is read and not connected, a hub, another
     VID:PID or a record with another Slot ID; or its port is not in the
     model and the nearest port on its path that is, is read and not
     connected or not a hub (a hub unplugged with drives behind it).
   An unknown drive is kept, with its name and whether it was announced,
   and gives no notice; a failed read is never a removal.
6. **Taken as they are, silently** (`tr_stor_step`'s quiet argument):
   the tray's first refresh; the first refresh after the notice is
   turned on; for one controller, the first refresh in which it answers
   its query after one in which it did not (a start, an enable, a driver
   update, a stop and start): its gone drives are dropped and its new
   ones kept, with no notice, and its queued arrivals and removals
   (item 8) are dropped; and every refresh from `PBT_APMSUSPEND` until
   15 s after the resume (`PBT_APMRESUMEAUTOMATIC`, `PBT_APMRESUMESUSPEND`
   or `PBT_APMRESUMECRITICAL`; `STOR_RESUME_MS`), since a resume may
   enumerate every drive again with new slots: at the suspend every
   queued group, held arrival and watched re-send (item 10) is dropped,
   and nothing is sent until the 15 s are up. Quiet drives count as
   announced, so a drive left in at login or across a sleep gets "was
   removed" when it goes. What it costs: a drive plugged or unplugged
   while asleep or while its controller was stopped is not announced.
7. **The set** (`TR_STOR_SET`, 64 drives, `TR_STOR_MAX`): each drive's
   key, controller key, port path, name and the name's rank (item 9),
   and whether its arrival was announced or taken quietly. A drive past
   64 is not kept and gets no notice. Removed drives leave the set when
   their removal is queued.
8. **The notices, queued and coalesced** (`TR_STOR_Q`, host-tested). Two
   groups, removals and arrivals, each with its count, the key,
   controller key and name of its first `TR_STOR_MAX` drives and the tick
   of its first event. A new
   drive (not quiet) joins the arrivals; a gone drive whose arrival is
   still queued leaves the arrivals and is not queued as removed (an
   attachment never announced is never announced as removed); any other
   gone drive joins the removals. A group is sent (`stor_try_send`, at
   each second's `TIMER_POLL`, never from inside a refresh, which may
   still be building the model) when all of these hold:
   - the icon is there (one owed before it is goes once it is) and the
     notice is on;
   - 1.5 s have passed since the group's first event (`STOR_GATHER_MS`),
     so a burst that straddles two refreshes is one balloon;
   - 5 s have passed since the tool last sent any other balloon (a
     Thunderbolt or USB4 event, the unexpected-removal warning, a quick
     save's end, the faster notice; `STOR_GAP_MS`): on XP and later a
     second balloon from the icon replaces the first at once (section
     7.6 item 4), and those outrank this notice;
   - the "can run faster" notice (section 7.7) is neither owed nor held
     (`fastPending`, `fastHold`), and its balloon's re-send watch
     (`fastSend` not idle) has not been open for less than 100 s
     (`TR_BS_NOSHOW_MS` and `TR_BS_WAIT_MS`, the longest that watch can
     owe a re-send) since that balloon was sent: that notice outranks
     this one, and the bound keeps a shell that never answers (2000,
     where the watch can stay open) from holding this queue. Both
     notices come for a drive that both count, the faster one first;
   - for the arrivals, the hold: every arrival in the group has its
     devnode (the port's `Dn`) and 3 s have passed since the group's first
     event (`FAST_SETTLE_MS`, room for the disk and its letter), or 10 s
     have (`STOR_HOLD_MS`); a drive with no storage driver (98 SE without
     NUSB) never has a devnode the tree matches (the driver key is written
     with a driver), so its notice comes at 10 s. Once every other
     condition holds, the model is read again (`refresh_window`, as
     `TIMER_CHANGE` does) immediately before the arrivals' text is made,
     unless a refresh ran in this same tick: a disk's letter can come
     after the last devnode change, and `disk_letters_nt` runs only in a
     refresh. The conditions are checked again after it (it may have
     found the drive gone), and only the arrivals counted in the model
     then are sent: one whose controller stopped answering stays queued,
     unknown (dropped silently if that controller answers again, item 6),
     and waits another hold if it is the only one. The Settings window
     being open defers the send to its close.
   The removals go first; the arrivals then wait the 5 s after them (a
   drive balloon counts as another for the other group). A
   group's names are read again from the model when it is sent (an
   arrival's, item 9), and the group is emptied once sent.
9. **The name** (`tr_stor_name`, host-tested): the tree's label for the
   device (`usb_label`: its disks' names and letters, "SanDisk Ultra
   (F:)"), where its devnode gives one that is not a class name of
   Windows' (`tr_name_generic`); else its product string; else the
   devnode's class name; else "USB drive <VID:PID>". The product string
   is the one section 7.7 item 4 asks for (string 0 for the first
   language, then `iProduct`, through the hub), asked once per key and
   only where the devnode gives no specific name, `iProduct` is not 0
   and the drive is not behind a TT (section 7.7 item 1's reason), and
   kept in the set answered or not, so a drive is asked two requests at
   most while it stays on its port. Ranks, best first: a label with a
   letter, a label, a product string, a class name, VID:PID. A present
   drive's name is read again at every refresh and kept unless the new
   one ranks lower, so a removal names the drive as it was best known
   (its letter included) and not as a teardown half-read it.
   `usb_label` lists disks only for a device the tree lists for Safely
   Remove (`usb_listed`, section 6.5); a composite device it does not
   list is named by its product string.
10. **The texts and the path** (the owner's drafts of 2026-11-07, the
    plural forms added):
    - one arrival: "USB drive connected", "<name> is connected.";
    - several: "USB drives connected", "<name> and <name> are
      connected." or "<name>, <name> and <n> more are connected.";
    - one removal: "USB drive removed", "<name> was removed.";
    - several: "USB drives removed", "<name> and <name> were removed."
      or "<name>, <name> and <n> more were removed.".
    The count is exact; a name longer than fits is cut so that the text
    stays within the balloon's 255 characters (`tr_stor_text`). The
    information icon, never the warning: section 7.6's warning stays
    Thunderbolt and USB4's, and a drive stopped with Safely Remove gets
    "was removed." when it is unplugged. The balloon is sent as every
    other one is (`tray_balloon_as`, the sound by the shell's rule; the
    quick save's `NIIF_NOSOUND` is its own), as a third kind of its own
    (`TRAY_BK_DRIVE`): on 98 SE `lastEvent` (the tooltip's "xHCI98 USB
    Tree: <n> TB/USB4 - <text>", 63 characters; the red icon's tooltip
    takes precedence, section 7.2.1) and the pop-up with its own id
    (`TRAY_POP_DRIVE`). A click on it opens nothing (the clicks of
    section 7.7 item 6 and of the quick save go by their own kinds).
    A balloon the shell did not show is sent again once by section 7.7
    item 9's rule, with its own state (`storSend`) and its own record of
    which balloon a show is on (`storKind`, as `fastKind`), and not when
    another balloon of the tool's was sent meanwhile; an arrivals'
    balloon is sent again only while each of its drives is counted in
    the model, and never once its controller was taken silently
    (item 6); the faster notice's watch
    is never open at the same time (item 8). Two differences from that
    rule (`tr_stor_answers`, `tr_stor_resend_due`, host-tested). XP and
    later (NT 5.1 up) count as shells that answer, for this balloon's
    unanswered case, from the first balloon of the session: XP's leg
    (`runs/run-48.md`, "48.17") lost the arrival of a stick's first plug
    while Windows' Found New Hardware balloons held the area, before the
    shell had answered anything, as 7 does behind its own; 2000, ME and
    98 SE keep section 7.7's rule. And while the Settings window holds
    the model (`setOpen`) the watch's tick waits, its state as it is: an
    arrival's drives are counted again in the model that the window's
    close reads (item 3), so a drive unplugged while the window was open
    is not announced again as connected; the shell's own messages still
    move the watch meanwhile.
11. **Not done.** Windows' own controllers' drives; a drive's speed or
    transport (section 19 has it); a removal of a drive on 98 SE and ME
    told apart from an eject (none is: the unplug is what is announced,
    on every system); serial numbers (item 4); a disk mounted later by a
    driver installed by hand (the arrival's name is the one it had when
    sent).
12. **The record.** The README's notices part says it in one paragraph;
    the release notes carry the owner's text. No line in Save Report: it
    is a notice, not a reading.
13. **Host vectors** (`test\test_tr.c`): `tr_stor_on` (absent, 0, 1, 2, a
    `REG_SZ`, a short value); `tr_stor_key` with and without a slot, a
    long path cut; `tr_stor_step`: the first refresh quiet, an arrival, a
    removal, a replug with another slot, the same slot unseen, unknown
    kept with no notice and later present or gone, a controller back
    after a failed query quiet, a hub gone with two drives (one group of
    two), 65 drives (the cap), an arrival gone before it was sent (nothing
    either way), a removal of a quiet drive; `TR_STOR_Q`: the gather, the
    gap, the hold (a devnode and 3 s, the 10 s), the faster notice's
    precedence (owed, held, its watch open and its 100 s bound with a
    shell that never answers), removals before arrivals, a controller's
    queued drives dropped at its quiet rebase, everything dropped at a
    suspend and nothing sent in the 15 s after the resume, the refresh
    before the arrivals' text when their send waited past the last
    refresh (a letter that came later), tick wrap; `tr_stor_name`'s ranks and the kept better name; `tr_stor_text`
    for one, two, three and 70 names, a 300-character name;
    `tr_stor_poll_step`: no baseline owes a refresh, a size change, a
    drive gone during the refresh it asked for, a controller new to the
    model once and its failed read then settled, a failed read answering,
    matching by key; `tr_stor_answers` per system and
    after an answer; `tr_stor_resend_due`: XP's unanswered balloon sent
    again at 60 s, 2000's never, a lost balloon's due tick held while
    the window is open and sent at the first tick after, a show while
    held.
14. **Legs** (T.20, section 11): 98 SE without NUSB (a QEMU USB stick
    plugged and unplugged: the pop-up and the tooltip, its product name,
    at 10 s), XP and 7 (the balloon with the letter, the removal by the
    letter; on 7 the balloon after Windows' own, its re-send where 7
    dropped it), and 2000 SP4 where the rig allows (a shell that reports
    no balloon), each with the notice on and off (off: nothing); a hub
    with two sticks unplugged (one balloon of two); a stick plugged while
    the Settings window is open in a tray with no window, closed with no
    change (announced at its close); the tray started with a stick in (no
    notice; its removal announced).

## 8. Install, upgrade and uninstall

- XHCIINST owns the tool's file (and its `.TMP`, `.BAK` and `.DEL`
  forms) and the Run value, outside the per-package `XHCIINST.LST` lists;
  its removal of the install directory (record 23 section 9.6) counts
  them as its own. Nothing is
  ever left pending for a restart on 98 SE or ME, and XHCIINST never
  edits `WININIT.INI`.
- (Revision 18) The computer's choice of the "can run faster" notice,
  `TrayFasterNotice` under `HKLM\Software\xhci98` (section 7.7), is
  XHCIINST's too: written only when the user changes the Components
  page's box or gives `/faster` or `/nofaster`, and deleted by the
  uninstall with the install directory's record. The users'
  `FasterNotice` values under `HKCU` are left, as `TrayAtLogin` is.
- **One at a time.** XHCIINST holds a machine-wide maintenance mutex
  (`Global\XhciinstMaint` on NT, `XhciinstMaint` on 98 SE and ME, with the
  shared mutex's security) for everything in this section. A second
  XHCIINST waits up to 60 s for it, then refuses; it reads every version
  again once it holds it.
- **Leftovers first.** A `.TMP` is deleted. A `.BAK` with no tool beside
  it is an interrupted replacement: it is renamed back, and if that fails
  the run stops before any version is compared ("steps left"). A `.BAK`
  beside a tool is deleted.
- **Copy.** On an install or upgrade run, the tool for the machine's
  architecture goes to `<install directory>\XHCITR.EXE` (`XHCITR64.EXE` on
  x64) when the file is missing or the package's file version is newer
  than the installed file's; otherwise it is left alone. The new file is
  first copied to `.TMP` and compared with the package's; then NT
  replaces the tool with `MoveFileEx` (replace existing), and 98 SE and ME
  rename the tool to `.BAK`, `.TMP` to the tool, and delete `.BAK`,
  renaming `.BAK` back on a failure. The Run value is written only after
  the file is in place. `/notray` writes no Run value and removes an
  existing one. There is no new question (owner: start at login).
- **Closing running copies.** Before replacing or deleting the file,
  XHCIINST sets the quit event (section 7.1), which every session's tray
  sees, and posts `WM_CLOSE` to the `Xhci98TrTray` window in its own
  session, then waits up to 15 s for the file to become replaceable. It
  never ends a process.
- **Still in use.** If the file still cannot be replaced, XHCIINST leaves
  the installed tool as it is, deletes its `.TMP`, says to close the
  xHCI98 USB Tree (or restart) and run it again, and ends with "steps
  left"; the drivers' part of the run is unaffected. No replacement is
  deferred to a restart, so no older copy can ever land on a newer one.
- **Uninstall.** Closes it as above, deletes the Run value and the file.
  On NT a file still in use is renamed to `.DEL` and deleted at the next
  restart (`MoveFileEx`, delay until reboot), and the next install or
  uninstall deletes a leftover `.DEL`. On 98 SE and ME a file still in use
  is left, reported, and deleted by the next uninstall; the Run value is
  gone, so it no longer starts. A failure to delete the Run value is
  reported and fails the run.
- The packager carries both executables under `tools\xhcitr\` with
  XHCIINST's freshness and version checks; `test-package.ps1` checks the
  layout.

## 9. Build, imports, manifest and icon

- x86: MSVC 6.0, `/SUBSYSTEM:WINDOWS,4.0`; x64: WDK 7.1,
  `/SUBSYSTEM:WINDOWS,5.02 /MACHINE:X64`. ANSI C89 as XHCIINST.
- Static imports: KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, COMCTL32,
  COMDLG32 (all present on 98 SE); setupapi, cfgmgr32 and version.dll
  resolved at run time as XHCIINST does, and any API absent from 98 SE's
  system DLLs likewise. `xhcitr\check-imports.ps1` allows exactly the
  seven, and since 3.4.2.0 holds every function each image imports to a
  row of `xhcitr\xhcitr-imports.allow` for its architecture (the x86 rows
  the 98 SE baseline, the amd64 rows XP x64's; an unlisted import and a
  row no longer imported both fail) and the subsystem version to the
  build's (4.0, 5.2).
- XHCITR.EXE on 64-bit Windows (WOW64) would read the 32-bit registry
  view and the x86 configuration manager, which refuses the resource
  calls, so since 3.4.2.0 it starts XHCITR64.EXE from its own folder with
  its command line in place of the window, the icon, `/report`, `/eject`,
  `/setlevel` and `/acpi` (waiting for and returning the exit code of a
  one-shot mode). When that is not there it no longer refuses (owner,
  2026-11-06, the audit's TR-3): it says so, with what WOW64 costs (it
  reads the 32-bit view of `HKLM\Software`, so what it shows from the
  start at login under `Run` and from Setup's records under
  `Software\xhci98` may be wrong; its own writes there are none, its
  preferences being the user's; no resources; Eject may fail), in a box
  for the window and the icon and on the console for a one-shot mode,
  never from an elevated `/result` child, and runs itself. The snapshot
  modes and `/selftest` stay in XHCITR.EXE; the
  snapshot names its bitness.
- Manifest: `asInvoker`, the Common Controls 6 dependency (XP and later
  draw themed controls; 98 SE to 2000 ignore the manifest).
- Icon `xhcitr.ico`, drawn for this project: a controller box with a
  tree line to two devices and an eject arrow, in the 16 VGA colours with
  no partial transparency, at 16, 32 and 48 pixels, 4 and 32 bits per
  pixel, so it reads the same on 98 SE's grey taskbar, XP's blue and 7's
  dark one. Since revision 19 its red twin `xhcitr-f.ico` (section 7.2.1
  item 8), the stack's green painted red, is the tray's icon while
  a condition holds.
- Version resource from `src\xhci_version.h`.

## 10. The pure half and its host tests

`tr_core.c` holds no Windows call: query and record parsing and checking
(every offset and count bounded by the bytes returned), the extension
decode, descriptor decoding to text (device, configuration, interface,
endpoint, SuperSpeed companion, BOS with USB 2.0 extension, SuperSpeed and
SuperSpeedPlus capabilities, strings), speed naming, router keys and the
tray event diff, the eject transaction's decisions, report formatting.
`test\test_tr.c` covers each with golden vectors, including truncated and
malformed descriptors, a query whose size disagrees with its counts, a
sizing answer, duplicate ICM keys and an epoch change. The driver's
builder (`cm_query.c`) is host-tested in the CM suite.

Revision 9: the log answer's builder (`cm_query.c`: the counter lines,
the ring's text with the cut, the withheld value and the replaced bytes,
the sizing answers) in the CM suite; the tool's readers of both logs
(`xhcitr\tr_log.c`, pure: the `XCML` parse, the snapshot window's header,
the ring by its head and used count, the HCD region's shape, the counter
names held to `src\xhci_counters.h`, the level-4 withholding - from
`3.5.0.0` level 4 shown as levels 2 and 3 are - the recording-off advice)
in `test\test_trlog.c`.

Revision 12: the PCI answer's builder (`src\cm\cm_qpci.c`, over modelled
buses: a dock's chain, a loop, a bus outside the port's range, a chain
deeper than the bound, more functions and buses than the answer holds, a
refused read, an absent function, 98's "not read", the sizing answers)
and the tool's reader and decoders (`xhcitr\tr_pci.c`: the answer's bad
shapes, pin and line, MSI, MSI-X, the link, BARs, windows, the capability
walk with a clear Status bit, a loop, a self loop, a bad and an unaligned
pointer, a cut and a capability past the 256 bytes, 79 columns on a worst
case) in `test\test_trpci.c`; xhci98's query answer (`src\xhci_qry.c`:
what each level serves, the ring's text and cut, the configuration read's
states, the sizing) and the tool's reader and texts of it in
`test\test_xhq.c`, with the default level.

Revision 16: the count (`cm_marks.c`: once per PDO, a tombstone found
by serial, a serial of another start or of another PDO refused, a device
never started, a loss counted apart, the block, the reset) in
`test\test_cm_marks.c`; the version 2 layout, the block, the record at
its offset and the content generation in `test\test_cm_query.c`; the
readers of both versions, the event (a rise, a wrap, a rise of more than
one, an epoch change, the liveness gate either side, not watched, a
version 1 answer, the "disconnected" beside it unchanged), the details
line and the option's reading (`tr_warn_removed`) in `test\test_tr.c`,
with the mark service's reader of version 2.

Revision 18: the "can run faster" decision (`tr_fast_check`: each speed
against each declared speed, a hub, another controller, no `_EX_V2`, an
unknown speed byte, 98's `_EX` bytes 3 and 4, each "Where" with a hub
read and not read, the High Speed case and its qualifier
(`tr_fast_wants_qual`, `tr_qual_ok`)), the key and the new-key diff (`tr_fast_key`,
`tr_fast_new`: a device moved, one gone and back, the 32 bound), the
balloon, the box's entry and its 79 columns (`tr_fast_balloon`,
`tr_fast_entry`) in `test\test_tr.c`; the default per system and the
value in force (`inst_fast_default`, `inst_fast_on`: on for every
target's version, 10.0 and 6.4 included, each source's 0, 1, another number and absent) in
`test\test_inst.c`, with the installer's switches.

Task 43.66: the balloon the shell did not show (`tr_bsend_step`: 7's
case, a timeout or a hide before any show, sent again once after the wait
and across `GetTickCount`'s wrap; XP's case, shown and never sent again;
no answer at all, sent again at the deadline only from a shell heard
from; the re-send lost or unanswered, given up; a new text, another
balloon, a click, the device gone and the notice off) in
`test\test_tr.c`.

Revision 19: the red icon's decision and tooltip (`tr_red_state`,
`tr_red_tip`, section 7.2.1 item 10) in `test\test_trred.c`, over
`xhcitr\tr_red.c`; `xhci98.sys`'s failed flag in `test\test_xhq.c`.

Revision 20: the balloon a click is on (`tr_bkind_step`,
`tr_bkind_fast`: another balloon queued behind the notice's and the
reverse, a replacement with its show, a show after two and three kinds
waiting and with an end between, one dropped unshown, more than the
bound, a show with none waiting, the re-send's removal before and after
an older show, a shell not heard from) and 98 SE's long hover (`tr_hover_move`, `tr_hover_poll`:
a short hover and a long one either side of 11 double-click times less
the poll, the last move counting, once per hover, resting on the icon,
another double-click time, across `GetTickCount`'s wrap) in
`test\test_tr.c`.

Revision 21: the generation line (`tr_gen_link_text`,
`tr_route_parent`, section 17 item 6), the version comparison
(`tr_ver_state`, `tr_pfro_find`, section 18 item 7) and the
storage transport (`tr_path_route`, the lines and the binding check,
section 19 item 8) in `test\test_tr.c`, `test\test_trlog.c`,
`test\test_xhq.c`, `test\test_snap.c` and `test\test_cm_query.c`.

Revision 23: the diagnostics' decoders (`xhcitr\tr_diag.c`, section 20
item 8) in `test\test_trdiag.c`.

Revision 24: the path's limit and Windows' own ports (section 7.7 items
10 and 11) in `test\test_tr.c`'s `test_fast_path`: the computer's top
controller by controller (5 Gbit/s, 10 Gbit/s, two in either order, a
controller not read, USB 2.0 ports alone, a stopped one, values not
known), what a hub holds, a SuperSpeedPlus device direct and behind a
SuperSpeed or SuperSpeedPlus hub on a 5 Gbit/s, a 10 Gbit/s and an
unread computer, at SuperSpeed and at High Speed (the report's case and
its entry text), a SuperSpeed device never raised, no USB 3 port at all
with the High Speed case still counted, the note; Windows' ports with
the option on and off, each clause of the gate (`bcdUSB` 2.00, 2.01,
2.10 and 3.00, a hub, nothing connected, an xHCI or unread class,
xHCI98's own port, no xHCI98 USB 3 port), the speed from `_EX` and from
the original request, the BOS parse (SuperSpeed, SuperSpeedPlus, the
USB 2.0 extension alone, short, wrong type, a capability of length 0,
past the end, not a device capability) and the once-per-plug cache
(asked, kept, dropped when gone, another address, full). Record 13
section 8.12's version 3 in `test\test_xhq.c` (`test_v3`: the part at
every level, its place and bound, the reader, a stopped controller, a
version 2 answer) and `XhciPortTop` in `test\test_caps.c`
(`test_port_top`: qemu-xhci's shape, 3.1 without a table, a 3.2 table,
a Gen 1 table, a rate above 5 Gbit/s with LP 0, the VL805's capture).

## 11. Legs

| Leg | Systems | What it reads |
|---|---|---|
| T.1 | 98 SE, ME, 2000, XP, XP x64, Vista x86/x64, 7 x86/x64 | the tool starts as a normal user (no UAC prompt), opens `\\.\HCD<n>` and `\\.\XHCICM<n>` with no rights, the window shows the USB tree with speeds and descriptors |
| T.2 | the same, with the TB3 and USB4 emulator devices | the Thunderbolt branch, the NVMe unit behind its tunnel, link details |
| T.3 | 98 SE, 2000, XP, 7 x64 | Safely Remove from the window and from the tray; the answer text; the unit gone; re-plug |
| T.4 | 2000, ME, XP, 7 | balloons on plug, approval, eject, disconnection; 98 SE tooltip |
| T.5 | 98 SE, 7 x64 | install puts the tool and the Run value; login starts the tray; upgrade with the tray running; uninstall closes it and removes both |
| T.6 | every target | the record 24 legs of XHCIINST still pass with the changed `xhcicm.sys` |
| T.7 | 98 SE, ME, XP, 7 x86 | revision 7, as a standard user: a Bulk-Only stick at a USB 3 and a USB 2 port and a UAS disk (98 SE, XP, 7) - the label with its letter, Safely Remove from the window, a right click, Delete and the tray, the result, the device gone or held, unplug and re-plug with the drive back, a refusal with a file open on it; a tablet, a keyboard, a hub and a device with no storage never offered |
| T.8 | 98 SE, 2000, 7 x86 (as a standard user) | revision 9: `XhciCmLogVerbosity` 2 on xhcicm's NHI and `SNAPON.BAT` for xhci98, restart; both "Driver log" nodes show the counters and the log live, after a plug and after an eject; Save Report holds both with no address; with recording off, the advice |
| T.9 | 98 SE (TB3 enclosure), 7 x86 as a standard user; xhci98's nodes on 98 SE, ME, 2000, XP, 7 x86 and 7 x64 | revision 12: the NHI's node shows the NHI and the tunnel port with pin and line and capabilities; below the port, the enclosure's functions on 7 and "not read on this system" on 98 SE; xhci98's PCI node and Driver log node read through the query request at the new default level 2; `/report` holds them; with the HCD regression (enumeration, a mass-storage copy, an unplug and re-plug) on each of the six |
| T.11 | 2000, XP, XP x64, Vista x86, 7 x86, 7 x64 (the TB3 and USB4 emulator devices); 98 SE | revision 16: an unplug with no eject gives "A Thunderbolt or USB4 device was removed unexpectedly." beside the unit's "disconnected"; a chain's first device unplugged gives one such notice where its removals fall in one sampled change of the count (another where they straddle two samples); an eject and then an unplug, a disable and enable of the unit's device, a driver update and a sleep and resume with the unit attached give none; the NHI's details count each; with "Warn when a device is removed unexpectedly" unchecked, an unplug with no eject gives no warning balloon and the count still rises in the details; on 98 SE the unplug reads "disconnected", the details "not watched on this system", and the item is not shown. Read 2026-10-22 (`runs/run-43.md`); the chain case is not readable on the emulator |
| T.10 | 98 SE, 7 x86 | revision 13, 7 x86 as a standard user: each driver's level set to 3 (on 7 through "Set as administrator..."), the restart, the registry and the dialog read 3 for each; then set back to 2 |
| T.12 | 98 SE; ME as the control | revision 17: the pop-up of section 16 for a plug of the Thunderbolt emulator's device and for the outcome of a Safely Remove from the tray, by screenshot; the focus left where it was; a click on the body and on the close box; the timeout; two notices in turn; on ME the shell's balloon and no pop-up |
| T.13 | 98 SE, ME, 2000, XP, 7 x86 and x64, 8 and 8.1 (roadmap-usb4 43.28) | revision 18: a SuperSpeed stick on a USB 2.0 root port and behind a High Speed hub (the balloon, 98 SE's tooltip, the red icon, the box), then moved to a USB 3 port (the icon clears); two such devices with one moved; a High Speed-only device never counted on a High Speed port or hub, and counted at Full Speed behind a Full Speed hub (item 1's High Speed case, its device_qualifier answered); the default per target; the setting turned off in the tool and by the installer |
| T.14 | 98 SE, ME, 2000, XP, 7 x86 and x64 (with 43.28) | revision 19, by screenshot: the icon red and its tooltip for a SuperSpeed stick on a USB 2.0 port, a High Speed stick behind a Full Speed hub, the Thunderbolt emulator's device left unapproved, and two at once ("and 1 more"; 98 SE's 63 characters); each put right and the icon back; a sleep and resume leaving it as it was; an Explorer restart while red; the menu's first item and the double click opening the window on the device; the notice turned off clearing conditions 1 and 2; 98 SE at 16 colours |
| T.15 | 98 SE; 2000 and XP | revision 20 (read 2026-10-23, QEMU, 43.28's installed guests, by screenshot): on 98 SE a pointer resting on the red icon until its tooltip timed out, then away, and back, twice - the tooltip shows again each time (with `3.2.0.0`'s and 43.57's XHCITR it never does again), and the "can run faster" pop-up at the tray's start; a stick that raised the Add New Hardware wizard, the pop-up when the wizard finished (`USBSTOR.INF` from NUSB 3.3 given to it); on 2000 the notice's balloon "Click the icon for what to try.", a click on it and on an eject's "Device stopped" opening nothing, the icon's first item opening the tree on the port; on XP a click on the notice's balloon opening the list and one on "Device stopped" opening nothing |
| T.16 | 98 SE and one NT target (the Thunderbolt and USB4 emulator) | revision 21, section 17: on the Alpine Ridge profile the USB4 device reads "USB4 v1.0 device, running as Thunderbolt 3" and the Thunderbolt 3 dock "Thunderbolt 3"; on the USB4 profile the USB4 device "USB4 v1.0" and the dock "Thunderbolt 3" |
| T.17 | 98 SE, 7 x86 | revision 21, section 18: an update to a higher version not restarted into - the status bar and the controller's and NHI's details say the running and installed versions differ; after the restart, the same |
| T.18 | 7 x86 (QEMU `usb-uas` with a serial, UAS alone); the transport line on 98 SE | revision 21, section 19 item 7: the transport line and counters for the emulated drive; a stand-in INF binding Windows' `usbstor` to its devnode, the tool's binding check, the binding kept across an unplug and replug; the stand-in removed from the store, the entry uninstalled and the drive plugged in again, on `xhciuas`. The natural case (a drive offering both) is 44-E.5 |
| T.19 | 98 SE (q35, `XHCIPFW.VXD` installed, the ASM4242 with its windows above 4 GB, the ORICO plugged after the start); 7 x86 or XP (q35, `xhcicm` with the emulator's NHI); 2000 on the Standard PC HAL once 44.53's code is merged | revision 23, section 20: the node's text in the window, Edit > Copy with nothing selected, and the same text in Save Report; on 98 SE `XHCIPFW`'s values and the connection manager's records decoded; on the NT system their NT wording and the slot of item 5 read through the log; on 2000 a hot plug's `cm.chain.irq` records in the slot |
| T.20 | 98 SE without NUSB, XP, 7 (roadmap-usb4 48.17) | revision 30, section 7.8 item 14: a stick plugged and unplugged with the notice on and off; the name with its letter (XP, 7) or its product string (98 SE, at 10 s, the pop-up and the tooltip); a hub with two sticks unplugged as one balloon; a plug while the Settings window is open; the tray started with a stick in |
| T.21 to T.25 | 98 SE, ME, 2000, XP, 7 x86 | revision 38, section 26 item 16: a stopped controller's line, cause and next try; the restart as administrator, standard user and elevated; a veto; a contained controller refused; the 9x calls read first; hold reason 4's notes |
| T.27 to T.34 | 98 SE, ME, 2000, XP, XP x64, Vista and 7 (both), 8, 8.1, 10 | revisions 42 to 46, section 27 item 15 (stage 1 the NT rows, stage 2 the 98 SE and ME rows): the restart's list, refusals, answers, elevation and Run As on each family, the start identity, storage and drive placement, an incomplete walk; the 9x calls' reading of a healthy controller (section 26's T.24) is `runs/run-50.md`, "50.32" |

## 12. Decisions

- **T1** (owner) A separate GUI tool, XHCITR / XHCITR64, not XHCIINST;
  3.0.0.0 waits for it; no admin at start; tray with eject menu, balloons
  and start at login; everything shown with check boxes; descriptors;
  driver changes allowed in both drivers.
- **T2** The tool reads the Thunderbolt state from a new query device in
  `xhcicm.sys` (section 4), not the registry record, because the ask needs
  an administrator on NT and the record lacks the details.
- **T3** `xhci98.sys` is not changed. Its existing hub IOCTLs (record 13
  section 8) give the USB branch everything but the negotiated SuperSpeed
  lane rate and count; the BOS gives what a device supports, and the tool
  labels it so. A change to the HCD this close to the cut would reopen its
  whole regression for that one detail. **Revised by revision 12** (the
  owner's PCI request and logging-consistency decision, 2026-10-16): the
  controller's configuration space is in nothing the door serves and no
  user-mode route reads it, and the owner chose to read `xhci98.sys` as
  `xhcicm.sys` is read, so `xhci98.sys` gains one read-only query request
  (section 4.8) serving its log, its counters and its configuration space,
  with the USBUSER pass-through unchanged; its default logging level moves
  to 2 in the same revision. The HCD's regression is run again for both.
  The negotiated lane rate and count stay "not reported".
- **T4** No new install question; `/notray` opts out of the Run value.
- **T5** The tool links `inst_core.c` for record 24's rules and texts, and
  shares XHCIINST's mutex, with a security descriptor both can open.
- **T6** (owner, 2026-10-16, revision 7; it reversed the first T6, "plain
  USB devices get no eject in the tool") Plain USB devices are stopped
  too, under section 6.5: one with a disk (or, on NT, a volume) in what
  stops with it; never a hub, an input device, a radio or the controller;
  on any controller the tool lists. The owner allowed "removable" on NT
  as well; it is not used, because `xhci98.sys` reports every device
  removable, internal ones too (section 6.5, "Why not removable").
- **T7** An eject waits for a connection-manager snapshot built after it
  began (two publications past its first query, section 6.2), in place of
  record 24's ask and echo, and checks eligibility on every answer up to
  the call; the pass's age only marks the display current or not.
- **T8** The tray's balloons from polling are descriptive; only an eject
  transaction reports an eject's outcome (section 7.2).
- **T9** No "unplugged while in use" balloon in this release, although
  the owner asked for a notice on an unsafe unplug: polling cannot prove
  one (section 7.2). The authoritative source is the child filter, which
  sees a unit's surprise removal on NT; publishing a count of those in the
  query is planned work for the owner to open. **Opened by revision 16**
  (roadmap-usb4 43.21): decision T22.
- **T10** A USB device's eject is section 6.5's short transaction - the
  shared mutex, the device found again by instance ID and its eligibility
  read again just before the call - not section 6.2's: no query device
  stands behind a USB device, and the configuration manager's own walk is
  the current state.
- **T11** The call goes to the USB device's devnode, the one Windows' own
  applet stops, never to a disk or a function below it, and on NT without
  record 24's not-removable fallback: a refusal is shown as it is.

- **T12** (owner, 2026-10-16, revision 11) A unit not marked removable is
  stopped by the query-and-remove route, and the tool asks for the
  administrator rights it needs for that one stop, by `runas`, waiting
  for the elevated run's answer (section 6.6). The elevated run trusts
  nothing a user can write: the confirmed list travels as a digest on its
  command line, its answer in a section the window created.
- **T13** (owner, revision 9) The tool shows both drivers' logs and
  counters, in the window and the report, from a release install, with
  no address in them and without writing the registry.
- **T14** `xhcicm.sys`'s log is a second request on the query device
  (section 4.5), copied from the ring under its own lock at the request,
  not drained and not snapshotted by the thread: a snapshot per pass would
  copy 16 KiB each pass whether anyone reads it or not, and a drain would
  take the log from every other reader. The counters, which the thread
  owns, are the thread's per pass.
- **T15** `xhci98.sys`'s log is read through the existing door, as
  `XHCISNAP` reads it (section 4.6); `xhci98.sys` stays unchanged
  (decision T3). At level 4 the tool withholds the ring rather than pick
  the address records out of it by label: the level-4 guarantee is the
  driver's, and a label list in the tool would drift from it.
  **Superseded by revision 12** (decision T3 revised): the tool reads
  `xhci98.sys` through its query request first (section 4.8), whose
  driver withholds the level-4 ring itself, and through the door only
  for a driver that refuses the request. **Revised for `3.5.0.0`** (the
  owner, 2026-11-13, "Show it as it is"): a user who chose level 4 to help
  sent a report with no log, so both drivers serve the level-4 log as it
  is and the tool shows it in the window, the report and the snapshot,
  through the door as well, with no sentence added (section 3.5).

- **T16** (owner, revision 12) A "PCI configuration" view, read-only,
  through this package's own drivers: the NHI, its tunnel ports and the
  functions below them through `xhcicm.sys`'s query device, each xhci98
  controller through its own query request (section 4.8); the BARs raw,
  never sized.
- **T17** The PCI answer is read at the request, not snapshotted by the
  thread (as decision T14 chose for the log): a pass would read every
  function's 256 bytes whether anyone asked or not. It is configuration
  space as read, decoded by the tool, so the driver carries no decoder
  that could drift from the tool's.
- **T18** (owner) `xhci98.sys`'s query request serves the configuration
  space at every level and is not gated by the door's enable step; what it
  serves of the log follows the level as `xhcicm.sys`'s does. Level 4's
  ring is withheld by the driver, as the tool already withheld it (up to
  `3.4.3.0`; served from `3.5.0.0`, decision T15 as revised).
- **T19** (owner, 2026-10-16) Both drivers' logging level defaults to 2:
  the ring records, with no addresses (`XhciLogVerbosity` for xhci98.sys,
  `XhciCmLogVerbosity` for xhcicm.sys). The owner's P14s reading put the
  cost at about 1 to 2% for large transfers and up to about 5% for small
  ones (design record 08 section 13.2, amended).

- **T20** (owner, 2026-10-16, revision 13) The tool sets the two
  drivers' logging levels and nothing else (section 7.5). A standard user
  on NT does it through a second mode of decision T12's elevated run,
  whose arguments are checked whole before anything is read; the
  elevated run finds the keys itself. (T13 to T19 are revisions 9 and
  12's.)

- **T21** (owner, 2026-10-18, roadmap-usb4 42-0.3; revision 14)
  XHCISNAP is retired in `3.1.0.0` and its reading moves into XHCITR
  (section 15): its engine is linked into the tool, its `-report`,
  `-dump` and `-probe` become `/snapshot`, `/dump` and `/probe`, and Save
  Report carries the snapshot, so one file is the bug report. Its level
  switches do not move: section 7.5 already replaces them.

- **T22** (owner, 2026-10-18, 2026-10-20 and 2026-10-22, roadmap-usb4
  43.21; revision 16) The tray says a unit "was removed unexpectedly" when the child
  filter saw Windows surprise-remove a running device of it, from a
  count in the query's answer (version 2), on NT only; never that it was
  unplugged or that data was lost (section 7.6). The count, not the 1 s
  diff, is the evidence. The answer's key for the last removal is its
  tunnel port, not a router, and the notice names no unit: neither the
  driver nor the tray can prove which router a removed device ran
  behind, and the unit's own "disconnected" balloon names it. The owner
  accepted that on 2026-10-22, had the warning shown on every NT system,
  2000 included, and asked for a per-user option to turn it off, on by
  default (section 7.6 item 8).
- **T23** (owner, 2026-10-22, roadmap-usb4 43.55; revision 17) On 98
  SE alone the tray draws its own balloon-like pop-up for each notice
  that gets a balloon elsewhere (section 16), through the one
  `tray_balloon` route, and a click on it is reported to the tray as
  the shell reports a balloon's click. Every other system keeps the
  shell's balloon. (T22 is revision 16's.)
- **T24** (owner, 2026-10-22, roadmap-usb4 43.57; revision 19) The
  tray's icon is red while a condition holds that is still present and
  that the user can put right: a USB 3 device below USB 3 speed, a High
  Speed device slowed by a Full Speed hub, a Thunderbolt or USB4 device
  waiting for approval, a controller that stopped answering (a real
  failure; not the "not monitored" state a sleep leaves); later
  conditions follow the same rule. The unsafe-unplug warning, plugs
  and ejects never turn it red. The icon is always shown while the
  tool runs, with no speed or text drawn in it; while red its tooltip
  names the first condition, with "and N more", within 63 characters
  on 98 SE and 127 from `shell32.dll` 5.0; when it is not red the
  tooltip is as before. A click leads to the window on the device
  concerned (section 7.2.1). The ranking of the conditions, the
  double click and menu item as "a click", `TR_FW_LINK` left out and
  `xhci98.sys`'s failed flag are orchestrator decisions for the owner to confirm.
- **T25** (task 44.9; revision 21) A USB4 router whose upstream side
  is Thunderbolt 3 - a Thunderbolt 3 host by its profile, or a
  Thunderbolt 3 router above it - says "running as Thunderbolt 3"; an
  unknown host is not guessed at (section 17). **Owner, 2026-10-25**:
  Falcon Ridge hosts are classed, and a router on one says "running as
  Thunderbolt 2".
- **T26** (task 44.8; revision 21) The running version is the drivers'
  own word in their query answers (xhci98's query version 2, xhcicm's
  removal block), compared with the file the next start loads, a
  pending replacement first; the status bar and the details warn, the
  tray does not (section 18). **Owner, 2026-10-25**: confirmed - the
  status bar and the details only, not the tray; xhciuas and Stor98 not
  compared.
- **T27** (task 44.35; revision 21) The transport decision is the
  driver's own, per device record, served by the query request at
  every level and by the snapshot; the tool matches it by port path
  and says when the devnode's driver disagrees (section 19). **Owner,
  2026-10-25**: QEMU's `usb-uas` offers UAS alone (one interface,
  protocol 62h), so the emulator cannot build a drive offering both; the
  stale binding is read there with a stand-in INF that binds Windows'
  `usbstor` to the emulated drive's devnode, and the natural case is
  read on the P14s Gen 1 (44-E.5).
- **T29** (task 44.80; revision 24) "Can run at" is held to the fastest
  of this computer's xHCI98 USB 3 ports and to SuperSpeed by a hub on the
  path that declares no more; a controller whose ports cannot be read
  is taken as SuperSpeed (section 7.7 item 10). The computer's best, not
  the root port's, and the details pane's note in place of a tray
  notice, are orchestrator decisions for the owner to confirm.
  **Owner, 2026-10-28**: Windows' own EHCI, OHCI and UHCI ports are
  watched only when the user turns the option on; off by default; the
  BOS is asked only from `bcdUSB` 0x0210, once per plug, never on
  xHCI98's ports, and a failure is "unknown, no notice" (item 11).
- **T30** (owner, 2026-11-17, tasks 50.30 to 50.32; revision 38) A
  controller stopped after repeated failures is shown as stopped, with
  the device and place the driver names as its cause and, under task
  50.31, its next try; the tool restarts a controller by Windows' own
  device restart (`DIF_PROPERTYCHANGE` with `DICS_PROPCHANGE` on 2000 and
  later; on 98 SE and ME a query-remove, remove and rescan of the parent,
  offered there only once a guest reading shows it works on both; owner,
  2026-11-17, revision 39), asked first, never for a contained
  controller, rechecked just before the call (section 26). D-1 and T6 stand for the eject and
  the settings window (section 26 item 14).
- **T31** (task 50.32; revisions 41 to 46) The restart's calls and
  answers are section 27's: on 2000 and later the seven SetupAPI routines
  of its item 2, looked up at run time; on 98 SE and ME `cfgmgr32.dll`'s
  query-remove, remove and rescan of the parent, under its item 5's
  checks, and never for a controller stopped after repeated failures there;
  a completed start claimed only on positive evidence - a changed
  start identity with the controller started (item 4's driver addition)
  or, on 98 SE and ME, the devnode's removal and return started - and
  never a start denied; a restart of the computer Windows asks for said,
  and nothing more done. Its item 16 lists what is the owner's.
- **T28** (task 44.62; revision 23) The PCI and bridge diagnostics are
  one tree node, not a window of their own: the details pane already is
  a read-only text with Edit > Copy of the whole, and Save Report
  already carries every node, so the owner's "without having to save the
  report and open it" is met with no new window, dialog or menu
  (section 20). The node reads only what the drivers already write, and
  writes nothing.

## 13. Open

- Revision 7: whether a USB disk on 98 SE (NUSB's `usbstor.sys` and
  `USBNTMAP`, or `xhciuas.sys`) and on ME (its own `USBSTOR`) is a devnode
  below the USB device in the configuration manager's tree, as an
  Stor98 disk is below its controller; whether a query-remove of the USB
  device is refused while a file is open on its disk; and what each NT
  target leaves of a USB device after its eject (Code 47, or gone). T.7
  read them (section 14).
- Whether every target lets an unelevated user open `\\.\HCD<n>` and
  `\\.\XHCICM<n>` with no access rights (record 13 section 8.10 left this
  open for the hub names); T.1 reads it.
- 98 SE's and ME's `\\.\` resolution of `\DosDevices\XHCICM<n>` (xhci98's
  `HCD<n>` names resolve there, record 13 section 8.2), and what NTKERN
  returns to `DeviceIoControl` for the removed-device answer.
- Whether the ICM's firmware messages carry a link rate the client could
  keep; until read, "not reported by the firmware".
- Revision 16: whether Windows 2000, which shows its own Unsafe Removal
  dialog after an unplug without an eject, gets the tray's notice too.
  Closed by the owner, 2026-10-22: it does, with the option of section
  7.6 item 8.
- Revision 37, for the owner: the names whose values' data is withheld
  (section 25 item 3). As built the fragments match anywhere in a name,
  and `KEY` and `PIN` as words at a capital, so a diagnostic value loses
  its data for a word in its name (`HotKey`, `KeyIndex`, a `TokenCount`
  counter), while a secret under an innocent name, or in a blob, is
  printed. The options: keep the list; match every fragment as a whole
  word only; or a shorter list. Closed by the owner, 2026-11-17: the
  terms are matched as words; a name whose last word is `Key` or `Keys`
  is withheld unless it is one of a short list of harmless names
  (`HotKey`), a secret term followed by a word of its form (`Hash`,
  `Hex`, `Value`, `Material`, `List`) is withheld, and so is a name in
  capitals alone ending in a term; `HotKey`, `KeyIndex`, `TokenCount`,
  `Monkey`, `Keyboard` and `*SSIdleTimeout` are shown. Section 25 item 3
  has the rule, its table and what it lets through.

## 14. As built

Where the code of `3.0.0.0` differs from the sections above, or settles
what they left open:

- **The ICM's link rate.** The ICM's device-connected event carries the
  link's speed and width (`tbt_icm.h`), so a Thunderbolt 3 router's
  extension has a rate and width with `CM_Q_XF_RATE_READ` clear, shown
  "reported by the firmware"; section 13's question is answered.
- **The name value at stop** is written empty, not deleted: there is no
  evidence that `ZwDeleteValueKey` works on Windows 98 (record 13
  Appendix A). The tool reads an empty `XhciCmQueryName` as no device.
- **The NHI's instance ID** comes from the link name of a private device
  interface registered on the NHI's PDO (never enabled), because
  Windows 2000's PCI bus answers `BusQueryInstanceID` with the last
  element only; `IRP_MN_QUERY_ID` is the fallback.
- **An empty file name on create** is required on NT only; what NTKERN
  leaves in the file name for a `\\.\` open is unread.
- **An inert start's first answer** carries `CM_Q_F_INERT` (task
  42-A.7). The answer the device holds until the thread's first pass is
  seeded from the thread's actual wait, which the thread records before
  each wait and clears after it (`CM_NHI.ThreadWait`,
  `CmQuerySeedInert`): no thread, or a thread already in its wait with no
  timeout, is inert; one in a timed wait is not; a thread between waits is
  judged by the wait it goes to, as its own publication judges it
  (`CmThrInertNext`). Until 42-A.7 the seed was set only with no thread,
  so the moment between the device's creation and that pass read as not
  inert; an eject refused it all the same (the record's stage is inert).
- **The confirmation** is a message box listing the unit and every
  devnode that will stop (at most 64; a larger unit is refused), not a
  dialog of its own.
- **Auto Refresh** rebuilds the whole tree when the 1 s poll sees a
  change. Without it, the window keeps the model its tree was built from
  and the status bar says the state changed; the details and an eject
  request use only that model.
- **Opening on 98 SE and ME.** A device whose open with no access rights
  is refused is opened for read and write, as XHCISNAP does; the IOCTLs
  stay any-access.
- **An eject's outcome on 98 SE** goes to a message box from the tray,
  there being no balloon; elsewhere an outcome longer than a balloon
  holds also goes to a message box.
- **`/selftest`** exists for `selftest.cmd`; with output redirected the
  one-shot modes write to it instead of a message box.
- **Install files.** XHCIINST stages through `.TMP` and, on 98 SE and ME,
  replaces through `.BAK`; on x64 the forms are named after
  `XHCITR64.EXE`. Record 23 section 9.5 has the details.

Revision 7, from leg T.7 (`runs/run-41.md`, "Plain USB devices"):

- **The disks on 98 SE and ME** are below the USB device, two devnodes
  down ("USB Mass Storage Device", "USB Disk", the disk), for NUSB's
  `usbstor.sys`, for `xhciuas.sys` and for ME's own `USBSTOR`; the removal
  of the subtree removes them, and Device Manager no longer lists the
  device.
- **The refusal on 98 SE** comes for a file held open on the drive. An
  MS-DOS Prompt whose current directory is the drive does not refuse, so
  the 9x text names an open file first.
- **Windows XP answers a vetoed eject with success.**
  `CM_Request_Device_EjectA` returned `CR_SUCCESS` with the veto type set;
  `tr_eject_cr` turns a success with a veto type into `CR_REMOVE_VETOED`,
  on the USB path and the Thunderbolt unit path alike. The veto came from
  the stick's own volume (`PNP_VetoDevice`, a `STORAGE\RemovableMedia`
  devnode); a device or driver veto from a devnode of the device's own set
  reads "F: is in use (a program has a file or folder open on it)".
- **Code 47 after an eject** (XP and 7; Code 24 on 2000 after a stop made
  here, revision 15): the disks go with the eject, so a
  device held for eject is listed, "[stopped - unplug it]", before the
  disk rule is applied. Any other problem code reads "not started -
  problem code N", never "unplug it", for a unit too.
- **Windows 7** answers `CR_NO_SUCH_VALUE` for a devnode with no relations
  of the kind; that is none. A set that cannot be read says which relation
  failed, in the details and `/report`.
- **Property reads.** Every configuration-manager property is read into a
  scratch buffer larger than any field and copied when it fits; the
  registry-only drive-letter value is no longer asked of the configuration
  manager. A page fault on ME, after the setup class moved from the stack
  into the node, led to both.
- **The walk's completeness** is kept per node: a node whose children could
  not all be read makes any set holding it unread, and a failed setup-class
  read is a node not read.
- **The UAS disk's re-plug** was not read: the emulated UAS device does not
  attach when added to a running machine.

Windows 2000, from the `3.1.0.0` matrix (`runs/run-42.md`, "XHCITR on
Windows 2000: the disk names, the letters and the stopped mark"):

- **A USB disk's setup class** is in its Enum key as `ClassGUID` alone,
  with no `Class` value, so the class read empty and the disk was no
  disk: the device was offered through its volume, under its own name,
  with no letter. An empty class name is now taken from the class key
  the `ClassGUID` names.
- **2000 has no Code 47.** A USB device stopped here stays attached at
  Code 24 (status `0x01806400`), which is also 2000's failed start. A
  devnode reads as held for eject at Code 24 only when this process
  stopped it, until it starts again or leaves the tree; a stop made
  elsewhere still reads "not started - problem code 24".

## 15. XHCISNAP's reading, moved here (revision 14)

The owner's 42-0.3 decision of 2026-10-18 (decision T21): `3.1.0.0`
ships no `XHCISNAP.EXE`, `SNAPON.BAT`, `SNAPGET.BAT` or `SNAPOFF.BAT`,
and `XHCITR.EXE` and `XHCITR64.EXE` take over exactly XHCISNAP's
reading. `docs\contributing\snapshot-reader.md` (XHCISNAP's guide,
moved) stays the reference for the route and the files.

1. **The source.** `xhcisnap\xhcisnap.c` is `xhcitr\tr_snap.c`, its
   `main` renamed `tr_snap_main` and its command line still spelled with
   dashes inside; `xhcitr\xhcitr.c` maps the user's words onto it.
   `xhcisnap\selftest.cmd` is `xhcitr\snaptest.cmd`, and
   `xhcisnap\check-counter-names.ps1` is
   `scripts\check-counter-names.ps1`. The `xhcisnap\` directory, its
   `build.cmd` and its three batch files are gone.
2. **`XHCITR /snapshot [NAME] [/c N] [/nosysinfo]`** is XHCISNAP's
   `-report`, the field report. It writes `BASE.DEV` first - every PCI
   xHCI controller's and this package's devnodes with their Device
   Manager status, problem code and allocated resources, the installed
   driver files' size, version and SHA-256, and the "captured on" system
   block - then the dump: `BASE.TXT` (the decoded header, note ring,
   slots region, root port enumeration and HCD region, PORTSC at level
   3), `BASE.BIN` (the raw extension image) and `BASE.PSC` (the raw
   PORTSC array). The files go in the current directory under the 8.3
   NAME given, else a name from the clock (`mDDhhmmS`); a set is never
   overwritten. The exit codes are XHCISNAP's, 0 and 2 to 8.
3. **`XHCITR /dump [/c N] [/o BASE] [/nosysinfo]`** is `-dump`:
   `BASE.TXT`, `.BIN` and `.PSC`, replacing an earlier set of that BASE.
4. **`XHCITR /probe`** is `-probe`: the route's four controls, nothing
   dumped.
5. **`XHCITR /snapshot /?`** is the long help. With its output
   redirected each of these modes writes its text there; otherwise a
   message box shows the text with the current directory.
6. **What is refused.** A word beginning with `-` after `/snapshot`,
   `/dump` or `/probe` is refused before anything is read, so no switch
   of the old tool is reached by its old spelling. XHCISNAP's level
   switches (`-verbosity`, `-on`, `-disable`, `-force`) did not move:
   section 7.5's Logging level and `/setlevel xhci98=<0-4>
   xhcicm=<0-4>` replace them and SNAPON.BAT and SNAPOFF.BAT, and the
   engine's answer to one names Logging level and `/setlevel`.
7. **`/snaptest`** is `snaptest.cmd`'s entry, not for users: it hands the
   rest of the command line to the engine unmapped, so the self-test
   reaches the canned decodes (`-selftest-*`) as XHCISNAP's did. The
   gated build runs `xhcitr\snaptest.cmd` after `xhcitr\build.cmd`, and
   `scripts\check-counter-names.ps1` checks the counter names that
   `XHCITR /snaptest -selftest-counters` prints against
   `src\xhci_counters.h`, as it checked XHCISNAP's.
8. **File > Save Snapshot...** picks a folder (`SHBrowseForFolder`),
   runs `/snapshot` there for the first xhci98 controller (every one since
   task 49.35, item 12) and shows the
   text, with the folder, in a message box. **Revision 25** (task 46.4,
   the owner, 2026-11-01): that message box held the engine's whole
   text, up to 6 KB, a window taller than a 640 by 480 screen. The
   folder dialog stays the one question (its prompt "Save the snapshot
   in:"); the files keep `/snapshot`'s own names from the date and time
   (`stamp_base`, unchanged, as is `XHCITR /snapshot` itself); and the
   window then says in a short message box, of a few lines whatever the
   engine printed, where the files went and whether they are whole:

   | `/snapshot`'s exit (`tr_snap.c`, `-report`'s list) | The message (drafts for the owner) |
   |---|---|
   | 0 | "Snapshot saved in <folder>: <name>.DEV, .TXT, .BIN and .PSC." |
   | 3 (the dump's `.TXT` incomplete or not written, or the extension's size disagreed) | "Snapshot saved in <folder> as <name>, but its .TXT is missing / incomplete. Send the files anyway." |
   | 4 | "Snapshot saved in <folder>: <name>.DEV only. No dump was taken; the .DEV says why." |
   | 5 (the `.DEV` not written in full, or not at all) | "The snapshot in <folder> is not complete: its device report <name>.DEV could not be written." |
   | 8 (a file of the set appeared after its name was chosen) | "The snapshot in <folder> is not complete: a file named <name> was made by something else meanwhile. Run it again." |
   | any other (6, the 26 names of a minute taken; 7, the clock), or no name read | "No snapshot was saved in <folder>." and the engine's first line, its "XHCITR /snapshot: " prefix left out |
   | not run, or not ended within two minutes | "The snapshot did not run, or did not end within two minutes." |

   None of them says which files exist beyond what its exit proves. The
   name is read from the engine's own line "files named <name>" (`,` or
   " (the name given)" after it; Save Snapshot gives no name), in the
   first 1 KB of the child's output, read on its own before the output
   is deleted (`snap_read_head`): the tail the message box used to show
   may begin after it. `tr_snap_named` (the name) and `tr_snap_saved`
   (the message) are the pure half's, `tr_core.c`, host-tested in
   `test_tr` (every exit, the name followed by `,` and by " (the name
   given)", a line with no name, a name longer than 8.3, a folder of
   `MAX_PATH` characters, the output's bound). The folder is the one
   picked, shown on this machine only, as before. Save Report's
   "Snapshot" part (item 9) and `/report` are unchanged.
9. **Save Report and `/report <file>`** end with a "Snapshot" part,
   always with the `.DEV`, once (it is the machine's): from the run for
   the first xhci98 controller, or, when none is listed, from a run for
   controller 0, which may take no dump, the `.DEV` being written before
   any controller is opened and reporting that case. Each xhci98
   controller's dump `.TXT` follows, whole, slots and enumeration regions
   included, at every level: up to `3.4.3.0` a dump whose header said
   level 4, where it can hold memory addresses, or said no level at all
   was withheld, as the "Driver log" node withheld the ring; from
   `3.5.0.0` it follows whole, addresses included (section 3.5). Save
   Report, or
   `/report`, alone is therefore the bug report; the raw `.BIN` and
   `.PSC` come only from `/snapshot` or `/dump`, when the maintainer
   asks for them.
10. **A child process.** The window and `/report` do not call the engine
    in their own process: they run the tool itself with `/snapshot` in a
    folder - for Save Snapshot the one picked, for `/report` a directory
    of its own under `%TEMP%`, removed afterwards with its files - and
    read the files and the text back, waiting up to two minutes. The
    moved engine keeps XHCISNAP's process-wide state, so it runs once per
    process, as it did in XHCISNAP.
11. **The door.** `xhci98.sys`'s USBUSER pass-through is kept unchanged
    (section 4.8): Windows' own property tabs use it, and the snapshot
    reads through it.
12. **Every controller (task 49.35, revision 32; the owner, 2026-11-13).** Item 8's Save Snapshot, the
    tray's quick save and `XHCITR /snapshot` with no `/c` took "the first xhci98 controller" only, and `/c N`
    is in no user document. A user's ASMedia ASM1042 failed beside a working Intel controller (issue 31), and
    two snapshots in a row were of the Intel one. These three now take every controller `xhci98.sys` drives,
    listed as Save Report lists them (item 9's `snap_hcd_number`): XHCI98HC<k> on NT, HCD<k> on 98 SE and ME,
    where another stack can hold HCD0. The connection manager's snapshot (task 49.37) writes `BASE.CT<n>` and
    `BASE.CB<n>` beside them, a different second letter.

    - **The names.** One BASE per run. Each controller with door number k, as one character (`0`-`9`, then
      `A`-`Z`; a controller numbered above 35 is not taken, and the message says so), gets four files:
      - `BASE.D<k>`: its device report. It is the machine's, but each one records its own controller's door
        and dump result, so none is dropped;
      - `BASE.T<k>`: the text;
      - `BASE.B<k>`: the image;
      - `BASE.P<k>`: the PORTSC array.

      For example `B112324A.D0`, `.T0`, `.B0`, `.P0`, `.D1`, `.T1`, `.B1`, `.P1`. The files' own texts name the
      set as `BASE.DEV`, `.TXT`, `.BIN` and `.PSC`, the engine's names. The message says how they are renamed.
    - **BASE.** The parent chooses it before any child runs (`tr_snap_pick_base`, pure, `tr_core.c`):
      - the NAME given to `/snapshot`, refused (exit 6, nothing written) when any file of the set exists,
        meaning `NAME.DEV`/`.TXT`/`.BIN`/`.PSC` or any `NAME.D?`/`.T?`/`.B?`/`.P?`;
      - else the engine's own clock name, `mDDhhmmS`, with the first letter whose whole set is free.

      A run that cannot choose one writes nothing, and says so.
    - **How.**
      1. For each controller the parent runs item 10's child, `/snapshot BASE /c <k>`, in a scratch directory
         of its own under `%TEMP%` (`snap_temp_dir`, as Save Report's runs do). The child's names cannot meet
         any other file, and the parent moves only what that directory holds.
      2. After the child, the parent moves its `BASE.DEV`, `.TXT`, `.BIN` and `.PSC` into the chosen folder
         as `BASE.D<k>`, `.T<k>`, `.B<k>`, `.P<k>`, without replacing (`MoveFile`; across volumes a copy that
         fails on an existing file, then the source deleted).
      3. It records for each controller the child's exit and each move's result, and removes the scratch
         directory.

      A move that fails leaves the scratch directory and its files in place, and the message names them.
      The engine is unchanged, and so is `/snapshot /c N`: one set, `BASE.DEV`/`.TXT`/`.BIN`/`.PSC`. `/dump`
      is unchanged.
    - **Time.** Each child keeps its two-minute bound. `snap_child` now waits at most ten seconds for a child
      it has terminated, instead of without end, then goes on to the next controller and says so. Six
      controllers can take about twelve minutes at worst; a run normally takes seconds each.
    - **A failed controller is still taken.** The door is the FDO's and answers in every controller state
      (logging off excepted, as before), LATCHED FAILED included, and the latched one is the controller
      wanted. A child that writes no dump (exit 4: its `.D<k>` says why), or does not end, leaves its
      controller's line saying so, and the others are still taken.
    - **The machine's parts once.** The device report's Thunderbolt networking section is the machine's, not
      a controller's: the first controller's `.D<k>` has it, and every later child runs with XHCITR's own
      `/nonet` (the engine's `-nonet`), whose section says where it is. The machine-wide parts taken once
      per run (the connection manager's snapshot, task 49.37) join after the controller loop.
    - **No xhci98 controller.** `/snapshot` without `/c` on a machine where `xhci98.sys` drives none runs
      `/snapshot [NAME] /c 0` as before, one set under the engine's names, whose device report says why.
      The closing text says it was taken only when that run ended with 0; otherwise it gives the exit and
      the run's own words. A NAME is checked before either path runs a child.
    - **The message** (Save Snapshot's box, the quick save's README.TXT and balloon, `/snapshot`'s closing
      text). It names the folder once, then one line per controller from the publication record (`tr_snap_saved_all`, pure):
      - "Controller 1 (XHCI98HC1): B112324A.D1, .T1, .B1 and .P1";
      - "... .D1 only: no dump, its .D1 says why";
      - "... the .T1 is not complete";
      - "... not taken: did not end within two minutes";
      - "... not moved: the files are in <scratch>".

      At most eight lines, then "and N more". It closes with "(each file's own text names it BASE.DEV, .TXT,
      .BIN or .PSC)". Its exit is 0 only when every controller's set is complete and moved, else the
      highest child exit, or 9 for a move that failed. The quick save's balloon falls back to its box when
      the text is longer, as it does today. Each line is short enough for item 8's 640 by 480 rule.
    - **The pure half** (`tr_core.c`, host-tested in `test_tr`):
      - `tr_snap_kchar(k)`: the character, none above 35;
      - `tr_snap_pick_base`: over a callback that says whether a name exists;
      - `tr_snap_saved_all`: the message and the exit, with every outcome, the "and N more" count and the
        bounds.
    - **Readers.** `docs/contributing/snapshot-reader.md` says the names. A script that expects one set reads
      `/c N`'s, which keep their names.

## 16. The 98 SE pop-up (revision 17, task 43.55)

Roadmap-usb4 43.55 (owner, 2026-10-22: "do this specially for 98 SE"):
on Windows 98 SE, whose `shell32.dll` (4.72) shows no balloons, the tray
draws a pop-up of its own for every notice that gets a balloon on ME,
2000 and later, and looks and behaves like a balloon as far as plain
Win32 on 98 SE allows. Only there: where `shell32.dll` is 5.0 or later
the shell's balloon stays, unchanged, and the pop-up is never created.
The tooltip's last event (section 7.1) is kept as it is.

1. **One hook.** `tray_balloon` is already the one route of every
   notice: the poll's events of section 7.2 (plug, approval,
   disconnection, the controller that stops answering), the outcome of
   an eject started from the tray (sections 6.2 and 6.5), and each later
   notice that raises a balloon through it (43.21's unexpected-removal
   warning, 43.27's "can run faster" notice). Where the shell has no
   balloons it hands the title, the text and the icon kind to the pop-up
   (`tr_pop_show`, `xhcitr\tr_popw.c`) after the tooltip; nothing else in
   the tray changes. The outcome of an eject from the tray, which took a
   message box on 98 SE for want of a balloon, takes the pop-up under the
   balloon's own rule (shorter than 250 characters, else the message
   box): the two tests read "the shell has balloons, or the pop-up is
   ready" in place of "the shell has balloons". A pop-up whose class or
   window cannot be made leaves 98 SE as it was before this revision:
   the tooltip, and the message box for an eject's outcome.
2. **The window.** A top-level `WS_POPUP` of its own class
   (`Xhci98TrPop`), `WS_EX_TOOLWINDOW | WS_EX_TOPMOST`, owned by the
   hidden tray window (so no taskbar button, and it goes with the tray),
   shown with `SW_SHOWNOACTIVATE` and answering `WM_MOUSEACTIVATE` with
   `MA_NOACTIVATE`: it never takes the focus or the keyboard. Its shape
   is a rounded rectangle with a stem toward the notification area
   (`SetWindowRgn` over `CreateRoundRectRgn` and `CreatePolygonRgn`).
   Colours and font are the system's: `COLOR_INFOBK` behind,
   `COLOR_INFOTEXT` for the text and a one-pixel border, and the tooltip
   font (`NONCLIENTMETRICS` `lfStatusFont`, the font 98's tooltips use;
   `DEFAULT_GUI_FONT` if that cannot be read), bold for the title. The
   icon is the shell's for the balloon's `NIIF_*` kind - `IDI_INFORMATION`
   for `NIIF_INFO`, `IDI_WARNING` for `NIIF_WARNING`, `IDI_ERROR` for
   `NIIF_ERROR`, none for `NIIF_NONE` - drawn at the small-icon size
   (`SM_CXSMICON`) beside the title; the close box is
   `DrawFrameControl`'s caption close button, as tall as the title line,
   at the top right. Title and text are cut where the balloon's fields
   cut them (63 and 255 characters).
3. **Its size** (`tr_pop_layout`, pure). Every length is from the font
   and the system metrics, never a fixed pixel count, so Large Fonts
   scale it: the padding is half a line, the stem a line high, the text
   wraps (`DrawText` `DT_WORDBREAK`) at 40 average characters or the
   screen's width less two margins and the padding, whichever is less,
   and a pop-up taller than the screen less the taskbar is cut to it (the
   text first, then the title row); a room too narrow for the icon and
   the close box loses the title, never the screen's edge. At
   640 x 480 with Large Fonts the longest notice (255 characters) fits.
4. **Where** (`tr_pop_edge`, `tr_pop_place`, pure; the primary monitor
   alone, since 98 SE's notification area is on it). The inputs are the
   screen (`SM_CXSCREEN`, `SM_CYSCREEN`), the work area
   (`SPI_GETWORKAREA`), the taskbar's rectangle (`FindWindowA
   ("Shell_TrayWnd")`), the notification area's (`FindWindowExA` of
   `TrayNotifyWnd` below it) and the pop-up's size.
   - The edge: a taskbar wider than tall is at the top when its middle is
     above the screen's, else at the bottom; one taller than wide at the
     left or the right by the same test. An auto-hidden taskbar keeps its
     rectangle mostly off the screen with a sliver on it, and the same
     test reads its edge. A rectangle that is empty, or touches no edge of
     the screen, counts as no taskbar; then the edge is the side where the
     work area falls short of the screen, and the bottom when it falls
     short of none.
   - The anchor: the middle of the notification area, when there is one
     and it lies in the taskbar; else the taskbar's far end (the right
     end of a top or bottom taskbar, the bottom end of a left or right
     one) less a margin; with no taskbar, that corner of the work area.
     The line the pop-up stands on is the taskbar's inner edge, held
     inside the screen (an auto-hidden taskbar's is the sliver's), or the
     work area's edge.
   - The place: beside that line on the screen's side, the stem at the
     anchor, the body reaching away from the screen's near corner (to
     the left of the anchor on a top or bottom taskbar, above it on a
     left or right one), then moved wholly inside the screen with a
     margin, the stem held between the body's rounded corners. It never
     covers the taskbar.
5. **Time, and one at a time** (`tr_popq_*`, `tr_pop_expired`, pure).
   A pop-up shows for 10 seconds, the timeout `tray_balloon` gives the
   shell (10 s is also the shell's least), counted from when it shows
   with `GetTickCount` (its wrap at 49.7 days taken by the unsigned
   difference). The shell of 2000 and ME also stops the count while the
   user is away from the machine; 98 SE has no `GetLastInputInfo`, so
   the pop-up does not, and the tooltip still carries the last event. A
   notice raised while one shows waits in a queue of 8 and shows when
   the one before closes, in the order raised, each for its full time,
   as 2000's shell showed this icon's balloons in turn (the 43.21 legs
   read the warning and then "disconnected" on 2000). A ninth waiting
   notice drops the oldest waiting information notice, or, when every
   waiting one is a warning or an error, the oldest waiting one. Should
   USER give no timer, the pop-up still closes by its close box or a
   click, or when the next notice is raised after its 10 seconds.
6. **The mouse.** A click on the close box closes the pop-up; so does a
   right click anywhere on it. A left click on the body closes it and
   does what a click on a balloon does: the tray window receives
   `WM_TRAYICON` with `NIN_BALLOONUSERCLICK` (`WM_USER + 5`) in the low
   word of `lParam`, which is how the shell reports a balloon's click
   after `NIM_SETVERSION` 3, so whatever the tray does for a balloon's
   click it does for the pop-up's, with no second path (until 43.27 it
   does nothing; 43.27's notice opens its list). `wParam` is the id the
   notice was raised with, kept with it in the queue (`TR_POP_NOTE`'s
   `Id`), not the last one raised: a notice queued behind another
   reports its own, so the tray opens 43.27's list for that notice's
   click alone (its id 2, every other notice's 1; the batch 7 merge,
   2026-10-23). The time running out
   and the close box send `NIN_BALLOONTIMEOUT` (`WM_USER + 4`), and the
   showing sends `NIN_BALLOONSHOW` (`WM_USER + 2`), as the shell does.
   Either way the next waiting notice shows. Showing takes nothing from
   the foreground window; a press on the pop-up takes its activation
   (98 SE's USER, despite `MA_NOACTIVATE`), as a press on the shell's
   own balloon does on ME (leg T.12, 2026-10-22).
7. **Changes around it.** A display change (`WM_DISPLAYCHANGE`) or a
   work-area change (`WM_SETTINGCHANGE` with `SPI_SETWORKAREA`, which a
   taskbar moved or resized sends) places the showing pop-up again; an
   Explorer restart needs nothing, each pop-up reading the taskbar when
   it shows. The tray's exit destroys it with the tray window, the queue
   with it. A full-screen program is not detected (98 SE's shell has no
   such test either).
8. **x64.** The pop-up's code is built into `XHCITR64.EXE` too, and never
   runs there: every x64 Windows has `shell32.dll` 6.0.
9. **Host vectors** (`test\test_trpop.c`, over `xhcitr\tr_pop.c`): the
   edge from a taskbar on each side, shown and auto-hidden; no taskbar,
   with the work area short on each side and on none; an empty taskbar
   rectangle and one that touches no edge; the anchor with the
   notification area, without it and outside the taskbar; the place at
   each edge, in a corner and held inside a 640 x 480 screen; the size at
   small and large font metrics, a 255-character text at 640 x 480, the
   cut to the screen's height; the queue's order, its bound and the drop
   rule; the timeout at the tick's wrap.
10. **Leg T.12** (section 11): on 98 SE, the pop-up for a plug of the
    Thunderbolt emulator's device and for the outcome of a Safely Remove
    from the tray, read by screenshot (`screendump` over QMP), the
    focus left where it was, a click on the body and on the close box,
    the timeout, two notices in turn; ME as the control, with the shell's
    balloon and no pop-up.

11. **A plug that raises the Add New Hardware wizard** (revision 20,
    task 43.72). Read in QEMU with the traced build: 98 SE broadcasts
    `DBT_DEVNODES_CHANGED` for a device that needs a driver only once its
    wizard ends (finished or cancelled), so the tool reads the device,
    and its notice's pop-up shows, then, for its 10 s; nothing is lost,
    and a check made while the wizard is up sees none (read both
    with the wizard cancelled and with it finished on `USBSTOR.INF`).

## 17. What the link runs as (revision 21, task 44.9)

Roadmap-usb4 44.9 (was TRGEN.1; owner, 2026-10-24): a USB4 device on a
Thunderbolt 3 host runs its link as Thunderbolt 3, and the "Generation"
line says both: "USB4 v1.0 device, running as Thunderbolt 3". Before
this revision the line said "USB4 v1.0" alone, which reads as a USB4
link.

1. **The router's own generation** stays what the connection manager
   reported (`CM_ST_E_GEN`: on every ICM row the router's USB4 Version
   from `ROUTER_CS_4` since task 43.44, record 21 section 5.3; under
   the USB4 software connection manager the router's own reading).
   Nothing in either driver changes for this section.
2. **The upstream side of its link.** Every link below a Thunderbolt 3
   link runs as Thunderbolt 3 (the USB4 Connection Manager Guide), so a
   router's link runs as Thunderbolt 3 when the host's router or any
   router between it and the host is Thunderbolt 3.
   - **The host's router** is classed by the NHI's profile
     (`CM_ST_O_PROFILE`, `src\cm\cm_vals.h`): Alpine Ridge, Titan Ridge
     and Ice Lake (`CM_PROFILE_ICM_AR`, `_ICM_TR`, `_ICM_ICL`) are
     Thunderbolt 3 hosts; Maple Ridge, Tiger Lake to Meteor Lake and the
     USB4 software connection manager (`_ICM_MR`, `_ICM_TGL`, `_USB4`)
     USB4 hosts; Falcon Ridge (`CM_PROFILE_ICM_FR`) a Thunderbolt 2 host
     (the owner, 2026-10-25, decision T25); an unknown profile is not
     classed. A record whose profile is 0 is classed by
     its NHI's PCI id (`CM_ST_O_NHI_ID`) through `cm_vals.c`'s table
     (`tr_profile_of_nhi`): on 98 SE and ME the driver starts before it
     reads the NHI's ids and its record says profile 0 (read on 98 SE's
     Alpine Ridge, task 44.9's leg).
   - **The routers between** are found by route: an entry's depth is the
     count of its route string's nonzero bytes from the lowest (a hop a
     byte, record 22; `tr_route_depth`), and its parent the entry of the
     same NHI whose route is this one's with the byte at depth - 1
     cleared (`tr_route_parent`), and so on up to depth 1. The depth comes
     from the route and not from `CM_ST_E_PLACE`, because the software
     connection manager's entries behind a router (`CM_ST_KIND_USB4_LINK`,
     `cm_status.c`) carry depth 0 there. An ICM entry whose route was not
     given (`CM_ST_UNKNOWN`) takes its place's depth, and has no routers
     between found unless it is at depth 1.
   - A router between is Thunderbolt 3 when its own generation is
     (`tr_gen_tbt3`: `CM_ST_GEN_TBT3`, or a USB4 Version of major 0) and
     USB4 when it is any other USB4 entry, its version read or not (the
     software connection manager's entries behind a router carry none).
   - The result (`tr_up_of_path`): Thunderbolt 2 when the host is (a
     Thunderbolt 2 host runs every link below it as Thunderbolt 2);
     else Thunderbolt 3 when the host or any router between is; USB4
     when the host is USB4 and every router between was found and is
     USB4; else not known.
3. **The line.** A USB4 router (a USB4 entry that is not Thunderbolt 3
   by item 2's test) whose upstream side is Thunderbolt 3 reads "USB4 v<major>.<minor>
   device, running as Thunderbolt 3" ("USB4 device, running as
   Thunderbolt 3" where the version byte is 0). Every other case reads
   as before - "Thunderbolt 3", "USB4 v1.0", "USB4", "generation not
   known": a USB4 router whose upstream side is USB4 or not known says
   nothing about the link, and an unknown profile is not guessed at. A
   Thunderbolt 3 router stays "Thunderbolt 3", but on a Falcon Ridge
   host: there every router of a known generation reads "<its
   generation> device, running as Thunderbolt 2" ("Thunderbolt 3
   device, running as Thunderbolt 2", "USB4 v1.0 device, running as
   Thunderbolt 2"; the owner, 2026-10-25).
4. **The link line follows** (section 3.3, `tr_link_text_gen`): a USB4
   router running as Thunderbolt 3 has its lane-adapter rate worded as a
   Thunderbolt 3 router's ("Thunderbolt 3, 20 Gbit/s per lane, x2 (40
   Gbit/s)"), never as a USB4 generation. Under the ICM the line is the
   firmware's, as before.
5. **Where.** The details pane's "Generation" line of a router (section
   3.3), and Save Report and `/report` with it. The tree's label and the
   tray's balloons keep `tr_gen_text`'s words.
6. **Host vectors** (`test\test_tr.c`: `tr_gen_link_text`,
   `tr_route_depth`, `tr_route_parent`, `tr_up_of_path`): a USB4 v1.0
   router at depth 1 on each profile; a Thunderbolt 3 and a USB4 router
   on a Falcon Ridge host ("running as Thunderbolt 2"); a Thunderbolt 3
   router on each; a
   USB4 router behind a Thunderbolt 3 router on a USB4 host, and two
   USB4 routers below a Thunderbolt 3 router (the last still running as
   Thunderbolt 3); a USB4 router behind a USB4 router on a Thunderbolt 3
   host; an unversioned USB4 entry behind a router; a parent route
   matching no entry; an unknown route; a USB4 Version byte of 0 and one
   of major 0; the link line of a USB4 router running as Thunderbolt 3,
   with and without a width.
7. **Leg T.16** (section 11): the emulator's Alpine Ridge profile with
   its USB4 device (`device-model=asm2464`) - "USB4 v1.0 device, running
   as Thunderbolt 3" - and with its Thunderbolt 3 dock; its USB4 profile
   with the same device - "USB4 v1.0" - and with the Thunderbolt 3 dock
   - "Thunderbolt 3"; on 98 SE and one NT target.

## 18. The running driver's version (revision 21, task 44.8)

Roadmap-usb4 44.8 (was TRVER.1): after an update the installed files
are new and the running drivers old until Windows restarts (on 98 SE
and ME the new file even waits beside the old one, named in
`WININIT.INI`'s `[rename]`, record 26 section 5.0); the tool says so.

1. **The running version.** Neither driver says its version before this
   revision. From it:
   - `xhci98.sys` answers its query request in version 2 (record 13
     section 8.12): an 80-byte header whose word at 64 is the driver's
     version packed one byte a field (`3.3.0.0` is `0x03030000`), made
     at compile time from `src\xhci_version.h`'s `XHCI_VER_CSV`, so it
     cannot drift from the version resource. A version 1 answer is a
     driver from before `3.3.0.0`; a refusal, a driver from before
     `3.0.0.0` or not this one.
   - `xhcicm.sys`'s query answer (`XCMQ`, version 2) gives the same word
     in its removal block's first reserved word (`CM_Q_RM_DRIVER`, at
     16), which every version 2 answer carries; 0 there is a driver from
     before `3.3.0.0`, and a version 1 answer one from before `3.2.0.0`.
     The layout keeps its shape, so `CM_Q_VERSION` stays 2 and the mark
     service and XHCIINST read it as before.
   - `xhciuas.sys`, `xhcihlpr.sys` and Stor98 have no query and are not
     compared: the package updates every driver together, so these two
     stand for it. A machine with neither running (no xhci98 controller
     and no NHI) shows nothing.
2. **The installed version** of the same driver is the file the next
   start loads: `%windir%\system32\drivers\xhci98.sys` (or `xhcicm.sys`),
   as the snapshot's `.DEV` names it, read with WOW64's redirection off
   - unless a replacement of that file is pending, whose source is then
   the file read: on 98 SE and ME a `WININIT.INI` `[rename]` line naming
   it (`inst_rename_scan`, as XHCIINST reads it), on NT a pair of
   `PendingFileRenameOperations` (`HKLM\SYSTEM\CurrentControlSet\Control\
   Session Manager`) whose target names it. The version is the file's
   `VS_FIXEDFILEINFO` file version; a file that cannot be read is "not
   read".
3. **The comparison** (`tr_ver_state`, pure): the running version known
   (a version 2 answer, or `CM_Q_RM_DRIVER` nonzero) and equal to the
   installed one: the same; known and different, either way: **differs**;
   not known (a version 1 answer, or 0) with an installed version of
   `3.3.0.0` or later: **differs** (the running driver predates the first
   that says it); anything else, or an installed version not read: not
   known, and nothing is said.
4. **The warning.** Where it differs:
   - the status bar's text ends "- restart Windows to finish the driver
     update";
   - the xhci98 controller's details (section 3.3) and the NHI's carry
     "Driver version: 3.2.0.0 running; the installed file is 3.3.0.0.
     Restart Windows to run the installed driver." ("older than 3.3.0.0
     running" for a driver that does not say); where it is the same,
     "Driver version: 3.3.0.0 running; the installed file is the same."
     (the owner's wording, 2026-11-20, revision 55; a driver that does
     not say is never the same, item 3); not known, no line;
   - Save Report and `/report` carry the details.
   The tray's icon, tooltip and balloons do not change: an update not
   restarted into is not a condition section 7.2.1 turns red for, and
   the installer has already asked for the restart.
5. **When it is read.** At each refresh, from what the refresh already
   reads: the header-alone answer of xhci98's query (section 7.2.1 item
   3's read, now with an 80-byte buffer) and xhcicm's answer; the
   installed file once per refresh per driver. No new request, no timer.
6. **An older tool** (`3.2.0.0`'s) asks xhci98 for the header with a
   64-byte buffer: the version 2 driver answers a buffer of 64 to 79
   bytes with the header's first 64 bytes, whose words keep version 1's
   places, so that tool's red icon still reads the failed flag; its full
   read meets version 2 and shows the log as not served. It reads
   xhcicm's answer as before.
7. **Host vectors** (`test\test_tr.c`, `test\test_xhq.c`,
   `test\test_cm_query.c`): `tr_ver_state` over each case of item 3;
   the packed version against `XHCI_VER_STR`; the header's version word
   and a 64-byte ask's answer; `CM_Q_RM_DRIVER` in a version 2 answer;
   the `PendingFileRenameOperations` reader (`tr_pfro_find`: the `\??\`
   and `!` prefixes, a delete, a target named twice, an odd count, a
   list not terminated).
8. **Leg T.17** (section 11): on 98 SE and 7 x86, the package installed;
   then an update to a build of a higher version, not restarted: the
   status bar and both details say it differs; restart: they say the
   same and the status bar nothing.
9. **A mixed install** (owner, 2026-11-13, from a field report: an entry
   installed from `2.2.0.1`'s files on a machine whose controller ran
   `3.4.3.0`). The "Device Manager entry" block (section 3.3) of an xhci98
   controller and of an NHI compares the entry's `DriverVersion` (its
   driver key, as the block's "Version:" line reads it; `tr_ver_parse`,
   four decimal fields of 0 to 255) with the running version of item 1
   (`tr_ver_mixed`, pure). Only an entry older than the running driver
   gets a note, after "Version:": "This entry was installed from another
   version's files (2.2.0.1) than the driver that is running (3.4.3.0):
   run SETUP.EXE to update it." Nothing for equal versions; nothing for
   an entry newer than the running driver (an update waiting for its
   restart, which item 4's "Driver version:" line already says); nothing
   when either version is not known, a running driver from before
   `3.3.0.0` (which says none) included. 98 SE and ME read the same
   value from the entry's `Services\Class` key and the running version
   through the same requests, so the rule is the same there. `xhciuas`'s
   entries and every device and tunnel port entry carry no note: no
   running version is known for them (item 1). The snapshot's `.DEV` is
   written before any controller is opened and carries the entry's
   `DriverVersion` without the note; Save Report and `/report` carry the
   details, the note with them. Host vectors in `test\test_tr.c`.
10. **Where an entry came from** (owner, 2026-11-13, the same report;
    the three facts only, Windows' driver-install log not read). The
    same block prints, after "Version:" and its note, each where the
    entry's driver key holds it as a nonempty string: "INF file:" (its
    `InfPath`, for example `oem12.inf`), "Matching ID:" (its
    `MatchingDeviceId`, the hardware or compatible id Windows matched)
    and "Provider:" (its `ProviderName`); an absent value leaves its line
    out. The block is every devnode's, so devices show them too. The
    snapshot's `.DEV` lists the same three values among its driver-key
    values (`InfPath = ...`), as it lists `DriverVersion`.
11. **The Intel port switchover's line** (task 50.34; record 16 section
    21.7). An xhci98 controller's details carry, after "Driver version:",
    "Port switch:" and the words `tr_pswt.c` gives the decision -
    `tr_psw_lines`, the same words `/snapshot` prints under the tolerance
    block and the `.DEV` and `/dump` give the registry record - read from
    the running controller's snapshot (one window of the HCD region at 0:
    the count at word 956, the record write's status at 957, the record's
    words from 958; a header shorter than 1016 words is a driver's from
    before them). A routed decision is said from the record's route
    outcome (moved, nothing accepted, or not every step), never from the
    decision alone. Where the driver serves no snapshot (its log off, the
    controller not started, an older driver), an Intel controller's
    registry record `XhciIntelPortSwitchResult` is shown instead, opening
    "As recorded at <time>:", never as the current state; a controller
    that is not Intel's shows nothing and its key is not read. Save Report
    and `/report` carry the details, and the line with them.

## 19. The storage transport (revision 21, task 44.35)

Roadmap-usb4 44.35 (owner, 2026-10-25): a drive that offers UAS ran
Bulk-Only under Windows' `usbstor` on the owner's P14s Gen 1, and the
bus's transport decision (record 13 section 10.7, `src\xhci_xport.h`)
was visible only in the debug trace. The tool shows it, and says when a
devnode's driver disagrees with it.

1. **The driver's half** (record 13 sections 8.12 and 10.7):
   - The six decisions counted (`hc->XportDecisions`, each
     `XHCI_XPORT_WHY_*` but `NOT_UAS`) move into the counter block,
     appended at words 246 to 251 (`XportUas`, `XportUasOnly`,
     `XportUasOnlyForced`, `XportBotForced`, `XportBotNoStreams`,
     `XportNoStreams`), zeroed at each start as the block is; the
     snapshot's HCD region and the query request carry them by name.
   - Each device record (`HCD_USB_DEVICE`) keeps its storage decision:
     the interface (0xFF for a device PDO), the transport, the why, the
     alternate the class driver selects, the inputs (`XHCI_XPORT_F_*`:
     SuperSpeed, the controller streams, `XhciForceBulkOnly` set), the
     class triple exposed and a count of the storage interfaces decided
     on it; written by the thread at the PDO's creation, under the
     controller lock, for a PDO whose interface is mass storage (class
     08), Bulk-Only alone included (`XHCI_XPORT_WHY_NOT_UAS` with its
     triple), and for a refused one, which exposes no triple. The record is keyed by the interface: a decision for an
     interface already recorded replaces it (a function PDO made again by
     `HcdDevicePdoRepresentFunctions` is recorded again after it takes
     the old PDO's decision, so the record is the PDO's final one), and a
     new interface is counted (a mask of the interfaces seen); a
     composite device with more than one keeps the lowest-numbered and
     counts the rest. The six counters count each decision as before, a
     function made again included.
   - The query answer's version 2 carries one 40-byte record per device
     with a decision, in Slot ID order (Slot ID, root port, route
     string, interface, transport, why, alternate, inputs, triple,
     count), at every level - it is not the log - after the
     configuration space; the snapshot's HCD region appends the same
     records, 32 of them, to its header (its version 1 unchanged: a
     reader walks by the header's size).
2. **The match.** A USB device node on an xhci98 controller is matched
   to its record by place: the node's port path (`3.2.1`) gives the root
   port (its first number, 1 to 255) and the route string (each further
   number a nibble, the first hub's lowest; `tr_path_route`), and the
   record with that root port and route is the device's. A path deeper
   than five hubs, or with a hub port of 0 or above 15, matches none. Windows' own controllers
   have no record and show nothing.
3. **The line**, in the USB device's details (section 3.3) after
   "Class", and in Save Report and `/report`:
   - "Transport:     UAS (the device offers Bulk-Only and UAS; UAS
     chosen)";
   - "UAS (the only transport the device offers)", with the force value
     set "...; XhciForceBulkOnly does not apply to it";
   - "Bulk-Only (the device offers UAS too; XhciForceBulkOnly is set on
     this controller)";
   - "Bulk-Only (the device offers UAS too; this controller cannot
     stream at SuperSpeed)";
   - "none (the device offers UAS alone, at SuperSpeed on a controller
     that cannot stream)";
   - "Bulk-Only (the device offers no UAS setting)" for a mass-storage
     interface whose alternate 0 is Bulk-Only with no UAS setting;
     another triple with none: "class 08, subclass ss, protocol pp (no
     UAS setting)";
   - for a composite device "(interface n)" after the transport's word,
     and "and N more storage interfaces" where the count says so;
   - a version 1 answer or a refusal, for a device whose class or
     configuration (where the tool read it) names mass storage:
     "Transport:     not reported by this xhci98.sys"; for any other
     device, nothing (read on 98 SE under `3.2.0.0`'s driver, where the
     tablet showed the line).
3a. **A drive with no transport** (revision 35, roadmap-usb4 task 50.5).
    Under the "none" line (`TR_XPORT_WHY_NO_STREAMS`) the details add
    "Note: This drive runs only as UAS, which needs streams at
    SuperSpeed, and streams are not available on this controller, so
    Windows has no driver for it here. Try a USB 2.0 port or hub: if the
    drive supports USB 2.0, it may work there." (`tr_xport_none_note`),
    for every such drive, whatever refused it: a send-back to USB 2.0 that
    never reached the companion and timed out (record 13 section 10.7.4),
    a root port with no companion, or a SuperSpeed hub. Save Report and
    `/report` carry it; the tool's self-test holds its text and wrapping.
4. **The binding check.** Where the transport is UAS and the device's
   devnode (for a composite, the function's) is bound to Windows'
   Bulk-Only driver (`usbstor`, `USBSTOR.SYS` on 98 SE and ME; compared
   without case or extension) - not to another UAS driver, such as
   Windows 8's `uaspstor` - the details add: "Windows bound this drive to
   <service>, not to the UAS driver: most likely its Device Manager
   entry was first installed while the drive was offered as Bulk-Only,
   and Windows keeps that driver for it. To run it as UAS, uninstall the
   entry in Device Manager, then unplug the drive and plug it in again."
   The words are the reading's of item 7; if it shows another cause, the
   note says that cause instead.
   The other way round (roadmap 47, issue 26, added 2026-11-07): where
   the bus offers a drive as Bulk-Only at SuperSpeed on a controller
   that cannot stream (the record's inputs: SuperSpeed without the
   controller's streams; why 5, or why 4 where `XhciForceBulkOnly` is
   set on such a controller too; `XhciStreamsOff` or a controller
   without streams) and the devnode is bound to `xhciuas` (compared as
   above), the details add: "Windows bound this
   drive to <service>, the UAS driver, but this controller cannot stream
   at SuperSpeed, so the UAS driver cannot start the drive here (Code
   10). Its Device Manager entry was most likely installed on a
   controller that streams, and Windows keeps that driver for it on
   every controller. To use the drive here, uninstall the entry in
   Device Manager, then unplug the drive and plug it in again: Windows'
   Bulk-Only driver then runs it on every controller." The cause: a
   drive with a serial number has one devnode wherever it is plugged in
   (`UniqueID`, section 10.7 of record 13), and the transport changes
   only its compatible ids, which Windows does not read again for an
   installed devnode; `xhciuas` selects the UAS setting, its
   `IOCTL_XHCI98_OPEN_STREAMS` is refused (the controller has none) and
   its start fails. Not under `XhciForceBulkOnly` (why 4) on a
   controller that streams, nor below SuperSpeed: there `xhciuas` opens
   its streams, or runs without them, and the drive runs as UAS. Read on
   XP x64 under `3.4.1.0` with a simulated
   drive offering Bulk-Only at alternate 0 and UAS at alternate 1 (the
   descriptor set of the reported `0781:55A9`, over `usb-redir`): the
   entry installed with `xhciuas` while the drive was offered as UAS,
   then `XhciStreamsOff` 2 and a replug: `XportBotNoStreams` 1, four
   endpoints selected (the UAS setting), four control transfers, a
   `REMOVE`, Code 10 on `xhciuas`; the same counts per cycle as the
   report's. Windows' `usbstor` forced onto the entry: Bulk-Only at
   alternate 0, the disk mounted.
   **Amended by roadmap-usb4 UASBOT.1 (2026-11-08).** The bus now gives
   that Bulk-Only form a devnode of its own (`&BOT` in its instance id,
   design record 13 section 10.7.3) while `XhciBulkOnlyOwnId` is on, the
   default, and says so in the record's inputs (bit 3,
   `TR_XPORT_F_BOT_OWN_ID`): Windows then matches its `Prot_50` ids and
   installs `usbstor`, and the note cannot arise from Windows' own
   matching. So the predicate (`tr_xport_unrunnable`) also requires that
   bit clear, and answers by the query answer's driver version
   (`TR_XPORT.Driver`): from 3.4.2.0 (`TR_XPORT_OWN_ID_DRIVER`), where the
   value was 0, the note reads "Windows bound this drive to <service>, the
   UAS driver, but this controller cannot stream at SuperSpeed, so the UAS
   driver cannot start the drive here (Code 10). XhciBulkOnlyOwnId is 0,
   so the drive has one Device Manager entry on every controller. Set it
   to 1 (the default) and plug the drive in again: it then gets an entry
   of its own here, which Windows' Bulk-Only driver runs."; for an older
   driver, which has no such value, the words above stand.
5. **What it reads.** The query request's whole answer, once per
   controller per refresh, when a USB device of that controller is
   shown or a report is made - the read the "Driver log" and "PCI
   configuration" nodes already make.
6. **The snapshot's `.TXT`** (`XHCITR /snapshot`, `/dump`): a "storage
   transport" section after the root ports, each record by Slot ID and
   place with item 3's words, and the six counters by name in the
   counter list (`scripts\check-counter-names.ps1` holds them to
   `src\xhci_counters.h`). A header shorter than the records' is a
   driver from before them, and the section says so.
7. **The reading** (leg T.18, section 11). QEMU's `usb-uas` offers UAS
   alone (`hw\usb\dev-uas.c`: one interface, protocol 62h; read on 7
   x86 on 2026-10-25, where with `XhciForceBulkOnly` 1 the drive still
   took `xhciuas`), so the emulator cannot build a drive that offers
   both. By the owner's decision of 2026-10-25 (T27) the stale binding
   is read there with a stand-in: a test INF of the leg's own, never
   shipped, that binds Windows' `usbstor` (`usbstor.inf`'s
   `USBSTOR_BULK` sections) to the emulated drive's devnode
   (`USB\VID_46F4&PID_0003`, named by its serial number), standing in
   for an entry installed while a drive was offered Bulk-Only. Read on 7
   x86 (and the transport line on 98 SE): the devnode, its driver and its
   compatible ids, the tool's two lines; the drive unplugged and plugged
   in again (the binding kept); then the stand-in INF removed from the
   driver store, the entry uninstalled and the drive plugged in again:
   `xhciuas`. The natural case, a drive offering both, is 44-E.5 on the
   owner's P14s Gen 1. What the readings show is written into item 4 and
   the release notes (how such a drive is given back to UAS).
8. **Host vectors**: `test_xhq` (the version 2 layout, the records' part
   at levels 0 to 4, the sizing); `test_snap` (the header's new words and
   the records' place); `test_trlog` and `test_tr` (`tr_xhq_parse` of
   both versions, the records' bounds, `tr_path_route`, each line of
   item 3 and the binding check's cases); the snapshot reader's
   `-selftest-xport` over canned headers.

## 20. The PCI and bridge diagnostics (revision 23, task 44.62)

The owner, 2026-10-27: XHCITR shows the PCI and bridge diagnostics the
package's drivers leave in the registry, in the report and in the tool
itself, "without having to save the report and open it". Until now they
were read with a registry editor or an export.

1. **Where.** One node, "PCI and bridge diagnostics", the last child of
   "My Computer" (section 3.2), present at every refresh on every system
   whatever the View check boxes say, since `XHCIPFW` concerns any
   bridge and not only the Thunderbolt branch. A refresh adds it and
   reads nothing; selecting it reads then, and so do Save Report and
   `/report`, once, as the "Driver log" nodes do (section 3.5). The
   details pane shows the text; Edit > Copy with nothing selected copies
   all of it (section 3.1), so no window, dialog or menu is added
   (decision T28).
2. **What it shows**, in this order, each value by its registry name
   with its raw form beside the plain words (a dword in hex, a binary
   value in hex bytes, a string as written), and "not recorded" for a
   value that is absent:
   - **`XHCIPFW`'s log** (design record 32 sections 6 and 11.2): on 98
     SE and ME the values under
     `HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW`, on 2000 and
     later those under
     `HKLM\SYSTEM\CurrentControlSet\Services\xhcipfw\Parameters`. The
     key's absence reads "not recorded: XHCIPFW is not installed, or has
     not run since it was installed".
   - **Per NHI that `xhcicm` drives** (the NHIs of section 3.2, by
     description and instance id), from the NHI's hardware key
     (`Enum\<id>` on 98 SE and ME, `Enum\<id>\Device Parameters` on NT,
     where `XhciCmStatus` is read): the bridge numbering's last attempt
     `XhciCmBridgeDiag` (record 21 section 6.1.1) and the no-interrupt
     start's last attempt `XhciCmNoIrqDiag` (record 28 section 7). Both
     are written on 98 SE and ME alone; on NT each reads "not recorded
     (written on Windows 98 SE and ME only)". With no such NHI the part
     says so in one line.
   - **The interrupt of a function hot-added behind a tunnel on 2000**
     (roadmap-usb4 task 44.53, record 21 section 6.2.1 on the branch that
     builds it): item 5.
3. **`XHCIPFW`'s values**, decoded (the names, types and layouts are
   record 32's; the tool keeps them in one table in `tr_diag.c`, so a
   rename touches one place):
   - `XhciPfwVersion` (`REG_SZ`): the image that wrote the log.
   - NT only, written first: `XhciPfwSkipped` (`REG_DWORD`, bits: 0 not
     a system it acts on, 1 Safe Mode, 2 not at the boot drivers' time,
     3 several processors and no way to hold them; 0 "it ran"),
     `XhciPfwMode` (0 nothing done, 1 one processor, 2 inside
     `KeIpiGenericCall`), `XhciPfwHoldTicks` and `XhciPfwTickHz` (the
     step's length in performance-counter ticks and the counter's
     frequency, low dwords; also said in microseconds where the
     frequency is not 0).
   - `XhciPfwResult` (`REG_DWORD`): each set bit named (0 the scan
     finished, 1 a bridge read otherwise at the close, 2 a write refused,
     3 a bridge parked, 4 a window across 4 GB, 5 a table full, 6 an xHCI
     controller's BAR above 4 GB); an unknown bit by number.
   - The counts `XhciPfwClosed`, `XhciPfwRefused`, `XhciPfwParked`,
     `XhciPfwAcross`, `XhciPfwBarsInside`, `XhciPfwFunctions`,
     `XhciPfwXhciHigh` (`REG_DWORD`), each with its meaning.
   - `XhciPfwBridges` (28-byte records): per record the bridge's
     location, its action (closed, refused, parked, across 4 GB, left;
     another value by number), and `24h`, `28h`, `2Ch` before and after,
     with each prefetchable window as a 64-bit range or "closed".
   - `XhciPfwBars` (8-byte records): the function, the BAR's offset and
     its upper dword.
   - `XhciPfwXhci` (16-byte records): the function, the BAR's offset, the
     vendor and device ids and the address.
   - A binary value whose length is not a whole number of records, or
     longer than its table (16, 16 and 8 records), is shown raw with
     "not a layout this tool reads"; a value of another type likewise.
4. **`XhciCmBridgeDiag`** (`REG_BINARY`, 20 dwords, `cm_statb.h`): the
   attempt's sequence and tunnel port; when flag bit 0 is set, "not
   attempted: a device below the port was still listed, or the list
   could not be read whole" (`cmWkChildren` incomplete sets it too);
   when bit 1,
   "not attempted: PCI.VXD's configuration route failed its check" with
   the guard's reading; otherwise the outcome (`tbt_brnum.h`'s
   `XHCI_TBT_BRNUM_*` in words), what was seen (functions, bridges,
   tries), the port's bus numbers, the windows' outcome with the port's
   memory window and the bytes they needed (the window for the outcomes
   after which `tbt_brnum.c` has read it, the need only after outcome 1,
   since a refusal can stop its sizing part-way; otherwise "not
   recorded", the raw dwords kept), where a refusal stopped, the
   bridges undone and those that could not be, the first two bridges'
   bus numbers and memory windows as left, flag bit 2 ("the port read
   up"), the interrupt lines' outcome with the port's line and pin and
   the functions' counts (likewise: the line and pin for outcomes 1, 2
   and 4, the counts for 1 and 4, "not recorded" for 3). A length other than 80 bytes is shown raw with
   "not a layout this tool reads". `XhciCmNoIrqDiag` (`REG_BINARY`, 12
   dwords, `cm_noirq.h`): the sequence, the member and the function,
   its pin and line as read, the interrupt outcome, the entry's state
   and reason (`CM_NI_IRQ_*`, `CM_NI_S_*`, `CM_NI_W_*` in words), the
   problem before, the step reached (`CM_NI_OP_*`) and its `CONFIGRET`,
   the status and problem after ("not recorded" where both are 0, as an
   exclusion writes them), the configurations created and the
   candidates, the refusals by reason (1 to 8, each held at 15), the
   pin-0 functions seen and started; a length other than 48 bytes is
   shown raw likewise. Every dword is also listed raw, five to a line.
5. **The 44.53 slot.** The child filter keeps no registry record of the
   interrupt it adds on 2000's Standard PC HAL: it logs it, to the NHI's
   `xhcicm` log, as `cm.chain.irq` (the Line in bits 31:16, the
   function's location in 15:0), `cm.chain.irq.filter` (the same),
   `cm.chain.irq.no` (a reason in 31:16), `cm.chain.irq.nomem`,
   `cm.chain.irq.start.refused`, `cm.chain.irq.start.nomem` and
   `cm.chain.irq.start.stripped` (the Line added in 31:16). On NT the
   node reads each NHI's log as its "Driver log" node does (section 4.5)
   and lists those records, decoded, each with its raw line, the last
   16 of them per NHI (and how many there are), so that the text stays
   within the details pane's 64 KiB whatever the ring holds; a log at a
   level below 2 records none, and the node says so, with the advice to
   set xhcicm's level to 2 in View > Logging level (section 7.5) and
   restart. On 98 SE and ME it reads "not on this system (Windows 2000
   on the Standard PC HAL only)". The labels are in the same table as
   item 3's names; should 44.53 gain a registry record, the slot reads
   that instead.
6. **Text.** Every line within 79 columns (a name, an instance id or a
   version string too long for its line is put on lines of its own); every byte of a string value
   outside printable ASCII shown as `.`; no address of the drivers' own
   is shown (none of these values holds one: they hold PCI locations,
   configuration registers and counts).
7. **What does not change.** Nothing is written to the registry or to a
   device; no driver changes; the tool needs no rights for these keys
   (reading values under `HKLM\SYSTEM` is open to every user on each
   target, as the `XhciCmStatus` read already relies on).
8. **Host vectors** (`test\test_trdiag.c`, over `xhcitr\tr_diag.c`):
   each `XHCIPFW` value present and absent, a 98 SE and an NT set, the
   result bits and an unknown bit, each action, a 64-bit window and a
   closed one, a value of the wrong type or a length that is not a
   whole number of records, a table longer than the bound;
   `XhciCmBridgeDiag` for each outcome, the two "not attempted" flags
   and a short value; `XhciCmNoIrqDiag` with a refusal mix and a short
   value; the 44.53 lines picked from a log with a partial first line
   and other records; and no line over 79 columns on a worst case.
9. **The legs** (T.19, section 11): 98 SE on q35 with `XHCIPFW.VXD`
   installed and the emulator's ASM4242 with its prefetchable windows
   above 4 GB, so that `XHCIPFW` closes some, and the ORICO enclosure
   plugged after the start, so that the bridge numbering writes
   `XhciCmBridgeDiag` (and, where its rules admit the NVMe,
   `XhciCmNoIrqDiag`): every part read with values, in the node and in
   Save Report; 7 x86 or XP on q35 with `xhcicm` driving the emulator's
   NHI: the NT wording of both records and the slot read through the
   log. 44.53's records themselves come only from Windows 2000 on the
   Standard PC HAL with 44.53's code, which is on its own branch: until
   the two are merged the host vectors hold the decoding, and a 2000
   Standard PC hot plug read through the node is owed at the merge.

## 21. The firmware tables (revision 26, task 46.21)

The owner's task of 2026-11-03 and its amendment of the same day: the
tool saves the computer's ACPI firmware tables to a folder the user
picks, so that another board's firmware can be read for record 32
section 12.8 (its handover code, 12.8.12) without asking the user for a
capture made under Linux. It is an item of its own; File > Save
Snapshot... and `/snapshot` are unchanged (section 15). The tables are
the user's own firmware, sent by the user's choice: read for facts only,
never tracked, never a fixture (the host vectors use made-up tables).

1. **The item and the switch.** File > **Save Firmware Tables...**,
   below Save Snapshot..., on every system and always enabled, asks for
   the folder alone (`SHBrowseForFolder`, its prompt "Save the firmware
   tables in:"), reads and writes in the window's own process (nothing
   in it is process-wide, so no child is run), and then says in one
   short message box what was written (item 6). **`XHCITR /acpi
   <folder>`** does the same, used alone (as `/report`): its message
   goes to the redirected output, wrapped at 79 columns, or else to a
   message box (`say`). Its exit: 0 tables written, 1 nothing to save
   on this system or the folder refused (item 5), 2 a write failed.
   Nothing but the files of item 5 is written, and only in that folder;
   nothing is read but the routes of items 2, 9 and 10 (on NT the
   interface and the registry's copies under `HKLM\HARDWARE\ACPI`, with
   `KEY_READ` alone; no device but the drivers' own, and no physical
   memory, from the tool itself).
2. **Where the tables come from.**
   - **NT, with the interface**: where `kernel32.dll` exports both
     `EnumSystemFirmwareTables` and `GetSystemFirmwareTable` (Vista and
     later, XP x64 and Server 2003 SP1), looked up at run time with
     `GetProcAddress` and never imported, so `XHCITR.EXE` still starts
     on 98 SE, ME, 2000 and 32-bit XP and section 9's import list does
     not change. Provider `'ACPI'`: the list enumerated (its size asked
     first, at most 256 ids); each distinct id asked once, its size
     first and then the table, at most 1 MB a table and 8 MB in all, a
     table whose second answer is not the size the first gave refused.
     The interface names a table by its signature alone, so of several
     tables with one signature (the SSDTs) it gives one: the index and
     the message say how many the list named and that the first alone
     was saved. `DSDT`, when the list does not name it, is asked by
     name once; no answer, and since revision 28 the registry's copy is
     read (item 10).
   - **NT without it** (2000 and 32-bit XP): nothing is read; the
     message says this Windows gives programs no way to read them. The
     driver's request of item 3 is not asked on NT (its NT answer is
     "not on this system").
   - **98 SE and ME**: Windows gives programs no way to read them. The
     tool asks `xhcicm.sys` for the tables its handover discovery read
     (record 32 sections 12.8.3 and 12.8.14) through the query device
     of each NHI the tree lists whose answer named it (section 4.2), in
     the tree's order, and takes the first that serves tables; where
     none does, the message gives the reason of the first answer (item
     6). So on these systems only an ASM4242 USB4 host's discovery
     gives tables, and only those it read: the root pointer, the root
     table, `MCFG` and the three named SSDTs whole, every other entry's
     header alone (listed in the index, no file).
3. **`IOCTL_XHCICM_ACPI`**, a fifth request on the query device:
   `CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA44, METHOD_BUFFERED,
   FILE_ANY_ACCESS)` = `0x00222910`, the same access rules and
   removed-device answer as section 4.3; a driver from before revision
   26 answers `STATUS_INVALID_DEVICE_REQUEST` ("this xhcicm.sys does
   not serve them"). The layout is `src\cm\cm_query.h`'s `CM_QA_*`,
   built by the pure `src\cm\cm_qacpi.c` (`CmQaBody`, `CmQaAnswer`):
   - **Paged.** The input is empty or one dword, the byte offset into
     the body to answer from (any other input length:
     `STATUS_INVALID_PARAMETER`; an offset past the body: the header
     alone, its part 0 and `CM_QA_F_PAST`). Under 64 bytes of output:
     `STATUS_BUFFER_TOO_SMALL`. Otherwise the 64-byte header and as
     much of the body from that offset as fits, at most 32,704 bytes
     (`CM_QA_PART_MAX`) a request, so that no answer is larger than the
     32 KiB the log request's reader already asks for on 98 SE; the reader
     asks again from the next offset. The body is built once per start
     and never changes after, so the parts of one epoch fit together;
     the reader takes parts only while the epoch and the body's size
     stay those of its first answer, and takes that first answer only
     when its epoch is the one of the `XCMQ` answer that named the NHI
     (section 4.2), as section 4.5's log is matched: a device name
     recycled by another start or NHI between the two opens is refused.
   - **The header**: magic `XCMA`, version 1, the answer's bytes, flags
     (`CM_QA_F_STOPPING`, `_CUT` a table kept as its header for the
     bound, `_NOMEM` a copy that could not be kept, `_PAST`), the start
     epoch (section 4.2's), the state (`CM_QA_S_*`: 1 tables kept, 2
     not on this system (NT), 3 not looked for - not an ASM4242 under
     the USB4 profile, or no discovery at this start - 4
     `XhciCmAsm4242Handover` 0, 5 the host's ports not found by bus, 6
     no root pointer found, 7 no memory for the copy), the discovery's
     own outcome (record 32 section 12.8.12's `cm.osc.refused` value, 0
     when it went through), its tier (12.8.11), the body's bytes, the
     entries' count and size, the part's offset and bytes.
   - **The body**: one 32-byte entry per table (signature, kind - the
     root pointer, the root table, a root entry and its index - what is
     held: the whole table, its 36-byte header, or nothing; the length
     its header gives; the offset and bytes of what is held), then the
     bytes held, each at a dword boundary. At most 66 entries and 256
     KiB of bytes held (`CM_QA_MAX_ENTRIES`, `CM_QA_DATA_MAX`).
   - **No address.** The answer holds the tables as the firmware gives
     them (their own pointer fields are physical addresses of the
     firmware's tables, as the ACPI Specification lays them out) and no
     kernel or driver address; where the tables lie in memory is not in
     the entries.
4. **Reading the answer** (`xhcitr\tr_acpi.c`, pure): the header's
   magic, version and sizes checked against the bytes returned; each
   entry's offset and bytes inside the body, its held bytes no more
   than its kind allows (the root pointer 20 to 255, a header 36, a
   whole table its own length field); a body that fails any of these
   is refused whole ("the driver's answer could not be read"), never
   saved in part.
5. **The files.** In the folder picked, one file per table held whole,
   named by its signature (a character outside `A`-`Z` and `0`-`9`
   written `_`; the root pointer is `RSDP`) and its index, from 1, among
   the tables whose name has that same stem, so that two signatures
   written alike never share a name: `RSDP1.DAT`, `XSDT1.DAT`,
   `FACP1.DAT`, `SSDT1.DAT`, `SSDT2.DAT`; every name is 8.3; and `TABLES.TXT`, the index: a line per table in the order read
   with the file (or "not saved"), the signature, the OEM ID, the OEM
   table ID, the revision, the length its header gives, the bytes
   saved and whether its checksum is right ("ok", "BAD", or "-" where
   there is none: `FACS`, or a header alone), then where the tables
   came from, the interface's one-per-signature limit or the driver's
   state, and on 98 SE and ME which tables the driver did not read.
   Lines within 79 columns, ASCII, a character outside printable ASCII
   in an id written `.`. Before anything is written every name is
   looked for in the folder: one that exists refuses the whole save
   ("already has", item 6), so a set is never mixed with another; each
   file is then created new (`CREATE_NEW`), and a write that fails
   stops the save, saying what was written.
6. **The message** (drafts for the owner, task 46.18's rule):

   | Case | The message |
   |---|---|
   | written | "This computer's firmware (ACPI) tables were saved in <folder>: <list>, and TABLES.TXT listing them." |
   | ... on NT, a signature listed more than once | then, one sentence for them all, "Windows gives one table of each name: of <sig> (<n>), <sig> (<n>), the first was saved." |
   | ... on 98 SE and ME | then "Windows 98 SE / ME give no access to these tables; these are the ones xhcicm.sys read for the USB4 handover." |
   | 2000 and 32-bit XP | "No tables were saved: this Windows gives programs no way to read the firmware (ACPI) tables. XP x64, Vista and later do." |
   | 98 SE and ME, no query device | "No tables were saved: Windows 98 SE / ME give no access to them, and xhcicm.sys, which reads them for an ASM4242 USB4 host, is not running here." |
   | 98 SE and ME, the driver kept none | "No tables were saved: xhcicm.sys did not read them at this start: <reason>." with the state's reason ("it reads them only for an ASM4242 USB4 host", "XhciCmAsm4242Handover is 0", "the ASM4242 host's ports were not found", "no ACPI tables were found in memory", "it had no memory to keep them", "this xhcicm.sys does not serve them", "the driver's answer could not be read") |
   | the interface answered nothing | "No tables were saved: Windows gave none." |
   | a name exists | "Nothing was saved: <folder> already has <name>. Pick an empty folder." |
   | a write failed | "Saving stopped at <name> in <folder>: <n> of <m> files were written." |

   `<list>` is the signatures of the tables saved (held whole), in the
   order read, a count after one saved more than once ("RSDP, XSDT,
   FACP, DSDT, APIC, MCFG, SSDT (3)"); a table held as its header alone
   is never in it. Where some were held as headers, the message adds
   "<n> more are listed in TABLES.TXT, not saved." ("1 more is listed
   ..." for one, since revision 28) (on 98 SE and ME that is every
   table discovery did not read whole).
7. **Host vectors** (`test\test_tracpi.c`, over `xhcitr\tr_acpi.c` and
   `src\cm\cm_qacpi.c`): made-up tables only (ids such as `TESTOE`,
   `TESTTBL1`), no board's bytes: the driver's body built and read
   back through parts of every size, a part past the body, the epoch or
   size changing between parts refused; each bad shape (an entry past
   the body, held bytes over its kind's, a bad magic or version, a
   count over the bound); the checksum of a good and a bad table, the
   root pointer of revision 0 and 2, `FACS`; the names (two of one
   signature, a signature with a bad character); the index's lines
   within 79 columns; every message, the list with counts.
8. **The legs**: 98 SE without ACPI on the emulator's ASM4242 with its
   tables (the `qemu` flavour's `xhcicm.sys`, record 32 section 12.8.8),
   the item at 640 by 480 and `/acpi`: the files compared with the
   tables QEMU was given; 32-bit XP: the message; 7 x86 and 10 x64
   (`XHCITR64.EXE`) at 96 and 120 DPI: the files and the index, the
   SSDT limit said.

9. **The full tables (revision 27, the owner's option B and the XP
   route, 2026-11-03).**
   - **98 SE and ME.** The tool asks `xhci98.sys` (design record 13
     section 8.13; record 32 section 12.8.16 is the reading) on the
     first controller the tree lists as driven by it, for the root, the
     DSDT and every SSDT whole, part by part with `XHA_IN_READ`, holding
     the parts of one save to the first part's version, body size,
     entry count and generation; it sends
     `XHA_IN_RELEASE` on every way out of the save once it has asked.
     Where `xhcicm.sys` answers too (item 3), its copies are saved
     beside them: a copy identical to `xhci98.sys`'s table of the same
     root entry is not written again (the index says "the same as read
     by xhcicm.sys at its start"); one that differs is saved as
     `<SIG><n>C.DAT`. Where `xhci98.sys` does not serve the request (a
     driver from before revision 27, no controller of it, or its state
     not 1), the save is item 3's kept set alone, as before, and the
     message says why the DSDT and the other SSDTs are not in it.
   - **NT with the interface, the root table** (the coordinator,
     2026-11-03, after w46-nthand's reading on 10 x64: `RSDT` given by name
     on a SeaBIOS boot, `XSDT` on an OVMF one, neither listed): after the
     list and `DSDT`, `XSDT` and then `RSDT` are asked for by name when the
     list names neither; the first that comes is saved. `TABLES.TXT` lists
     its entries by physical address, with how many tables Windows gave
     (each name once) and how many were not saved; Windows gives no
     address with a table, so no entry is matched to a file. The message
     adds "<n> SSDTs were not saved." where Windows' list names the SSDT
     n + 1 times.
   - **`TABLES.TXT`** gains a "From" column: `Windows`, `registry`,
     `xhci98` or `xhcicm`; a table not saved shows its reason (record 32
     section 12.8.16's texts).
   - **32-bit XP and 2000** (no firmware table interface; the owner,
     2026-11-03): the tool reads the copies Windows keeps in the registry
     with `KEY_READ` alone, so a standard user can: under
     `HKLM\HARDWARE\ACPI`, for each of `DSDT`, `FADT` and `RSDT`, every
     `<OEM ID>\<OEM table ID>\<revision>` subkey's value `00000000`, and
     `FACS`'s own value `00000000` (read on XP SP3, run 46). A value is
     kept only as a `REG_BINARY` of at least 36 bytes (the FACS, 64)
     whose signature is its key's (`DSDT`, `FACP`, `RSDT`, `FACS`), whose
     length field equals its size, at most 1 MB, and whose checksum is
     zero (the FACS has none); one that fails is listed "not saved" with
     its reason and never written. At most 8 subkeys a level, 4 tables a key.
     The registry holds no SSDT, `MCFG` or `APIC`: the index lists the
     RSDT's entries by their physical addresses, with how many of them
     the registry keeps (the FACP) and how many were not saved; it gives
     no address with a table, so no entry is matched to a file. The message: "This computer's
     firmware (ACPI) tables were saved in <folder>: <list>, and
     TABLES.TXT listing them. They come from the copies Windows keeps in
     the registry; the SSDTs cannot be read on this Windows." Where none
     is found, the "no way to read" message stays. 2000 is read the same
     way where its registry has these keys (checked in run 46).
   - **Host vectors**: `xhci98.sys`'s set built over a modelled memory
     (`src\xhci_acpi.c` with a reader over a byte array): the bootstrap
     pass, `MCFG` windows of every segment, a nonzero first bus, a
     misaligned base and a wrapping one, the FADT's 43, 44, 147 and 148
     bytes, a table moved (its signature changed after the header pass),
     a length changed, a bad checksum, the caps and the budget at their
     edges, a 64 KB piece starting mid-page, the timeout; the answer in
     parts, release, busy and past headers, the generation; the tool's
     merge with `xhcicm.sys`'s copies (identical and different), the
     registry walk over a modelled key tree (good values, a wrong
     signature, a length that disagrees, a `REG_SZ`, too many subkeys),
     the index's "From" and reasons, every message.
   - **Legs**: 98 SE without ACPI with the emulator's tables and no
     `xhcicm.sys`: the DSDT and every SSDT saved byte for byte as QEMU's
     (the SSDTs against the files QEMU was given, the DSDT against a
     dump of guest memory from QEMU's monitor at the address and length
     the saved FADT and DSDT header give); the same with `xhcicm.sys`:
     its copies identical, 46.12's notes and handover unchanged; a start
     nobody asks: `xhci98.sys`'s log unchanged. 32-bit XP, an
     administrator and a standard user: DSDT, FACP, FACS and RSDT saved,
     the DSDT and RSDT byte for byte as the registry and QEMU's monitor
     hold them; Windows 2000 SP4 (the ACPI base), an administrator and a
     standard user, the same, its registry having the same keys. Not
     covered by a leg: ME, a DSDT near 1 MB, a refused
     mapping.

10. **No `DSDT` from the interface, and the licence tables (revision 28,
    task 47.34; the owner, 2026-11-06).**
    - **Why.** On an HP EliteBook 8470p under XP x64 (task 47.33's
      reading), the interface listed eleven signatures, `FACP` and four
      SSDTs among them, and gave no `DSDT`, neither listed nor asked for
      by name, and neither root table; so the save held no `DSDT`,
      though Windows keeps a copy of it in the registry, which item 9
      reads on 2000 and 32-bit XP. Until this revision the interface
      route read nothing else. And the interface gave `MSDM` and `SLIC`,
      which hold the computer's Windows licence data (the product key,
      the OEM activation data), and the tool saved them whole, in a set
      the user is asked to send.
    - **The registry where the interface gives no `DSDT`.** On NT with
      the interface, once the list, `DSDT` by name and the root table
      by name are done (items 2 and 9): when the set holds a good
      `DSDT`, nothing more is read, so where Windows gives it (Vista and
      later, as read in run 46) the save is as before. A table is good
      here when it is held whole, at least its 36-byte header (the
      `FACS`: 64 bytes, as item 9 asks), its length field the bytes
      Windows gave, and its checksum zero (the `FACS`: no checksum); a
      `DSDT` that fails any of these does not count. Otherwise the registry is walked as item 9 walks it
      (`KEY_READ` alone, the same keys, bounds and checks: a
      `REG_BINARY` of the key's signature, its length field its size, at
      most 1 MB, its checksum zero), and of each of `DSDT`, `FACP`,
      `RSDT` and `FACS` the interface gave no good table of, every
      registry copy is added, its "From" `registry`; one that fails a
      check is listed "not saved" with its reason and never written, as
      in item 9 (a value not of its key's signature: "not the table its
      registry key names"). A signature the interface gave good is not
      taken again; one the interface gave that is not good stays as it
      was (saved, its Sum "BAD", or "-" for a `FACS`), the registry's
      copy beside it. The registry's
      `RSDT` is taken where the interface gave an `XSDT` and no `RSDT`.
      The interface answering nothing at all is the same case: the
      registry's copies alone, the save made when one of them checks
      out and then indexed and said as item 9's registry set (its
      source the registry, not Windows' interface), else "Windows gave
      none" as before. The index's root-entry count leaves out every
      `RSDT` and `XSDT`, whatever its size. On 98 SE and ME, the index's
      note that a "not saved" table is one the driver read the header of
      alone counts the driver's headers, not the licence tables.
    - **What it says.** `TABLES.TXT`, after the interface's lines, says
      for each signature taken "Windows gave no <SIG>; the copy it
      keeps in the registry (HKLM\HARDWARE\ACPI) was read.", or, where
      the interface gave one that is not good, "Windows' <SIG> did not
      check out; the copy it keeps in the registry (HKLM\HARDWARE\ACPI)
      was read too." The message
      adds "<SIG> came from the copy Windows keeps in the registry."
      (signatures joined with ", ", "copies" for more than one).
    - **The layout.** Microsoft's public documentation does not describe
      `HKLM\HARDWARE\ACPI`, and the DDK headers here (the 2000 DDK, WDK
      7.1) do not name it: the WDK's ASL compiler page documents only
      its own test override of a table (`asl /loadtable`), not this key.
      So the layout stays what run 46 read on 32-bit XP SP3 and 2000
      SP4, `<key>\<OEM ID>\<OEM table ID>\<revision>` with the value
      `00000000` (`FADT` holding the `FACP`, `FACS` its value itself),
      read again on XP x64 by this task's legs; and nothing rests on it
      beyond the checks, which keep a value only where it is a whole
      table of its key's signature.
    - **Never `MSDM` or `SLIC`, on every system.** Whatever the route
      (the interface, `xhci98.sys`'s set, `xhcicm.sys`'s copies; the
      registry keeps neither), a table whose signature is the four bytes
      `MSDM` or `SLIC` (not the root pointer) is held as its 36-byte
      header alone (one shorter, as nothing) before any file is named,
      so none is ever named or written: the rule is applied by
      `tr_acpi_names`, which every save goes through before it writes.
      On 98 SE and ME the merge with `xhcicm.sys`'s copies keeps such a
      header of `xhcicm.sys`'s where `xhci98.sys`'s set has no entry of
      that signature, so it is listed whichever driver read it.
      `TABLES.TXT` lists it as "not saved" by its header's fields (Saved
      0, Sum "-") with the line "not saved: licence data". The message's
      list of saved tables leaves it out, and it is counted in "<n> more
      are listed in TABLES.TXT, not saved."; the notice that Windows
      gives one table of each name and "the first was saved" leaves it
      out too. On NT the index's line on the root's entries counts the
      tables saved, not those Windows gave ("The XSDT lists <n> tables,
      at these addresses; <k> of them were saved (Windows gives each
      name once), so <m> were not saved:"), the registry's copies among
      those saved, and the licence tables among those not saved.
    - **Host vectors** (`test\test_tracpi.c`): the registry walked when
      the interface gave no `DSDT`, or one too short, of a length that
      disagrees or of a bad checksum, and not asked at all when it gave
      a good one; a registry `FACP`, `RSDT` or `FACS` not taken over the
      interface's own good one; the registry's `RSDT` beside the
      interface's `XSDT`; a registry `DSDT` with a bad checksum, one
      whose length disagrees and one of another signature, listed with
      the reason and never named; the interface answering nothing and
      the registry alone; `MSDM` and `SLIC` whole from the interface
      (once and listed twice), shorter than a header, as headers from
      `xhci98.sys`'s set and from `xhcicm.sys`'s answer (with and
      without `xhci98.sys`'s entry of it), and a whole copy merged from
      `xhcicm.sys`: none named, each listed "not saved: licence data",
      out of the list and the "first was saved" notice, in the count;
      `msdm` and `MSDX` saved as any other table; the root's entries
      counted; the index within 79 columns; the messages.
    - **Legs**: XP x64 (`XHCITR.EXE` and `XHCITR64.EXE`): the `DSDT` from
      the registry where the interface gives none, byte for byte as the
      guest's memory; 32-bit XP and 2000: unchanged, the `DSDT` saved;
      7 x64: unchanged, the `DSDT` from the interface; 98 SE: unchanged.
      QEMU gives no `MSDM` or `SLIC`, so that rule rests on the host
      vectors.

## 22. The Settings window and `/set` (revision 29, task 47.35)

The owner, 2026-11-07, from 47.33's bench (the HP EliteBook 8470p's
ports reach the xHCI under XP only with `XhciIntelPortSwitch` 1): "xhcitr
can have an additional function to open a window to set the registry
settings similar to what is done in setup", with "access to more common
settings with an advanced settings for xhci98, CM and Stor98 if it makes
sense" and a prompt to restart where a change needs one. The design was
put to the owner as options, reviewed and revised; the owner's choices of
that day are the ones below.

1. **Where.** View > Settings..., "Settings..." on the right-click of an
   `xhci98` controller and of an NHI (which opens on that driver's tab with
   that device as the scope), and the icon's menu. `settings_dialog` is the
   one entry point. The Logging level dialog (section 7.5) stays as it is;
   the Settings window has a "Logging level..." button that opens it, and
   no row of its own for either level (the owner's choice).
2. **The rows are Setup's.** XHCITR already links `xhciinst\inst_core.c`,
   so every row's meaning, choices, range, step and system rule is
   `instValues[]`'s or `instS98Values[]`'s, the same text Setup shows (the
   owner's 2026-10-16 wording rules included). The Tree adds two rows of
   its own in the same form (`trsExtra[]`, `xhcitr\tr_set.c`), which Setup
   does not offer: `XhciIntelIrqRoute` (every system) and
   `XhciIntelNoRouteIrq` (2000, XP and XP x64, the systems the driver
   reads it on, greyed elsewhere), each listed only where an `xhci98`
   controller passes `XhciPirqReviewed`. Their texts follow `tuning.md`;
   `XhciIntelNoRouteIrq`'s is held for task 47.33's wording.
   The Tree's own table (`trsRows[]`) gives each row its tab, view (common,
   advanced, or out with why), what a change takes (item 6), what the
   driver makes of a value (item 4) and the value the INFs write. The host
   suite (`test\test_trset.c`) fails when any row of Setup's table or of
   Stor98's is not in it, and holds each installed default to
   `src\xhci98.inf`, `src\xhci98-amd64.inf` and `src\cm\xhcicm.inf`.
3. **Which rows** (the owner's choice V-1 with `XhciIntelIrqRoute`).
   USB, common: `XhciIntelPortSwitch` (0 and 1; 2 stays a hand edit, as
   the port switchover's scripts left it until task 49.47 removed them), `XhciIntelIrqRoute`, `XhciIntelNoRouteIrq`,
   `XhciInterruptMode`, `XhciForceBulkOnly`, `XhciSilentInstall` (98 SE and
   ME; on 1 the Tree merges the list first with Setup's own code, item 7).
   Advanced: Setup's USB Advanced list (record 26 section 5.4.1).
   Thunderbolt / USB4, common: `XhciCmMode` (0 and 1; 2 and 3 with "Show
   advanced settings"), `XhciCmApprove`, `XhciCmSafeEject` (the Tree's own
   caution: the mark service is Setup's to install and remove); advanced:
   Setup's Thunderbolt list, `XhciPfwNhiLine` (98 SE and ME, with
   `XHCIPFW.VXD`'s key there) and the mark service's `Restart` (with its
   key there) among them. Stor98, common: `StorWriteThrough`,
   `StorPollMode`; advanced: the other eight. Out, as Setup: the log
   levels (the dialog's), `XhciCommandCheck`, the other hardware quirks and
   the Linux quirk values, `XhciCmTunnelBridge`, the bench switches, the
   networking values, every `XhciQemu*` value. **Revised by task 49.38**
   (the owner, 2026-11-13: "go through xhci98 USB tree to make sure it's
   advanced settings cover all the registry entries"): the paragraph's
   "Out" list is replaced by item 18's rule, under which the page covers
   every value a driver of the package reads.
4. **A row's value** is read as raw (machine-wide and each device's own:
   absent, a DWORD, another type), precedence (machine-wide when present,
   of any type, as `hcdReadDword` and `cm_reg.c` read it), and what the
   driver makes of it (`trs_effective`: a listed value or the default; any
   non-zero as on; only 1 as on; in range or the default; clamped; 0 as
   the default; held to another value). The Now column says it so: "1 -
   on", "1 - on (machine-wide)", "not set (the driver uses 1 - on)", "7
   (the driver reads it as 1 - on)", "not the same on every controller".
   The Default column is the value the install writes.
5. **Applies to** (the owner's choice S-3). On the USB and Thunderbolt
   tabs: every controller (the default), by the Logging level dialog's
   rule - the machine-wide value where one is set, else each controller's
   own driver key - or one controller, named with its maker, PCI ID and
   location, whose own key alone is written, offered only while no
   machine-wide value is set (the row says why otherwise; the Tree never
   moves or deletes one). `XhciPfwNhiLine`, `Restart` and Stor98's rows
   are one value for the computer and take no scope.
6. **What a change takes** (`trs_apply`). `XhciForceBulkOnly`: the drive
   plugged in again (on 2000 and later an installed disk uninstalled
   first), on every system. `XhciBulkOnlyOwnId` (UASBOT.1, an advanced
   USB row after it, with the Tree's own note): the drive plugged in
   again, on every system; with it on, the force value's change needs no
   uninstall, and the text's 2000-and-later caveat names it. `XhciIntelPortSwitch`, the two ASM4242
   handover rows, `XhciPfwNhiLine`, the mark service's `Restart`, every
   Stor98 row, and every row on 98 SE and ME: a restart. `XhciIntelNoRouteIrq`:
   the controller found again (a restart, or a Device Manager disable and
   enable). Every other row on 2000 to 10: the driver's next start. Where
   `tuning.md` promises that a Device Manager disable and enable applies a
   value (`XhciIntelNoRouteIrq`, `XhciInterruptMode`, the poll values,
   the Thunderbolt values), the text says so; the Tree never disables a
   controller itself (the owner's choice D-1). After Apply the tool says
   what was written and, for anything but a replug, "Restart Windows for
   this to take effect: <names>." and asks "Restart Windows now?"
   (`lv_restart`, unchanged). The changes this process wrote stay listed
   in the window's head with "Restart now..." until it ends; the tool
   never claims one is in force.
7. **Writing.** One writer, `set_apply`, for the window, its elevated
   run and `/set`: every change checked first (the row, `trs_parse`
   against the offered values, the scope, Stor98's state), and none
   written when one fails; then each written as `REG_DWORD` (Stor98's 98
   SE and ME value as its type; on NT `DriverParameter` read whole, one
   key changed with `inst_s98_param_set`, written back) and read back. A
   value already as asked is not written. `XhciSilentInstall` 1 first
   merges `tools\silent\SILENT.REG`'s list with `inst_silent_merge`
   (`xhciinst\inst_silent.c`, moved out of `inst_eng.c` so that Setup and
   the Tree run the same code; the Tree carries `src\silent.reg` as a
   resource), as Setup does, even where the switch already reads 1. Each
   Stor98 value written is named in Setup's record
   (`Software\xhci98\Stor98\Settings`), so that Setup's uninstall removes
   it as its own. Before any write the Tree refuses "Setup is running"
   when Setup's maintenance lock is held or one of `XHCIINST.EXE`,
   `XHCIIN64.EXE`, `XHCIWIZ.EXE`, `XHCIWZ64.EXE` is among the processes
   (Toolhelp, looked up at run time; the owner's choice K-2: no lock is
   added to Setup).
8. **Stor98's tab** is shown where the `stor98` service key exists and a
   devnode is bound to it (on 98 SE and ME by `PortDriver` `stor98.mpd`),
   and is read-only, saying why, unless Setup installed that Stor98 for
   its own drives alone: Setup's record exists and names every devnode of
   the service, present or not (on 98 SE and ME every present one, and
   every Stor98 driver key under `SCSIAdapter` belongs to one of them).
   Setup holds the settings when another Stor98 shares the service
   (record 26 section 5.6); it decides that from the tunnel's reading,
   which the Tree does not repeat, so the Tree asks for this narrower
   proof. The 32-bit tool on 64-bit Windows reads Setup's record through
   the 32-bit view, so the tab is read-only there too.
9. **Rights** (section 6.6's test). 98 SE and ME, and an administrator,
   write directly. Otherwise Apply reads "Apply as administrator..." (the
   shield on Vista and later) and starts `elev_launch_req` with `/set
   /request /result <section>`: the request rides in the section the
   window creates, never on the command line, built by `trs_req_make`,
   which refuses a request that does not fit rather than send a part of
   it; the child reads it once (`trs_req_parse`: its version line, one
   line per change, `END`, nothing after), checks and writes as item 7,
   and answers in the same section. `/set` from a command line without the
   rights refuses. XHCITR.EXE on 64-bit Windows hands `/set` to
   XHCITR64.EXE as it hands `/setlevel`.
10. **The port switchover.** A change of `XhciIntelPortSwitch` asks first
    (`trs_psw_text`): on, "the shared connectors (usually the blue ones)
    move from the USB 2.0 (EHCI) controller to xHCI98 ... Do not have a
    drive on them busy when you restart"; off, they stay with the USB 2.0
    controller, and where Windows has no working driver for it a device on
    them will not work; then the USB 2.0 controllers the walk finds (by
    compatible id `PCI\CC_0C0320`). `/set` prints the same and asks
    nothing. `XhciIntelNoRouteIrq` 0 on Windows 2000 where a controller
    is on that value's list (`XhciPirqNoRouteListed` over the hardware
    id's `SUBSYS_`; task 47.40), or where the device walk was not read
    whole and may have missed one, asks too, No by default (`trs_a5_confirm`,
    `trs_a5_text`): 0 there sends Windows 2000's request with the
    interrupt, which stops it at startup with STOP 0xA5 under an ACPI
    computer type (`tuning.md`); the question says so, that 0 is for the
    "Standard PC" computer type only, and that the way back is Last Known
    Good Configuration. `/set` prints the same and refuses it unless
    `/yes` is given; the window's elevated run does not ask again. The
    row's 0 and its note carry Windows 2000's meanings (1 and 2 poll the
    controller there).
11. **The command line.** `XHCITR /set Name=Value [Name=Value ...] [/on
    <instance id>] [/yes]` and `XHCITR /set /show`. A name is any row the window
    offers, Stor98's by either name; `Name=default` is the installed
    default. Exit codes are record 23 section 7.4's (the owner's choice
    E-1): 0 nothing needed a change, 1 refused, 2 failed, 3 done and a
    restart needed, 4 done and a step left (a replug); 4 outranks 3. The
    text ends with the same "Restart Windows for this to take effect"
    line. `/setlevel` keeps its own codes.
12. **The record** (the owner's choice L-1). Save Report and `/report`
    carry a "Settings" block after "Logging levels" (`set_block`): each
    tab's rows, the value now and the default, each device's own value
    where they differ, and this process's changes waiting for a restart.
    File > Save Snapshot... writes the same block beside the snapshot as
    `XHCISET.TXT`.
13. **Imports.** None new: the list and tab classes are registered by
    `InitCommonControlsEx`, looked up in comctl32 at run time (under
    Common Controls 6 `InitCommonControls` registers none), Toolhelp and
    the resource calls are looked up in kernel32, and `xhci_psw.c` and
    `xhci_pirq.c` are built in as Setup builds the first. The import gate
    holds both images to their rows unchanged.
14. **Quick save** (the owner's addition of 2026-11-07). The icon's menu
    has "Quick save (report + snapshot)". With no dialog it starts a
    worker thread (`qs_thread`), so neither the icon nor the window waits:
    the folder `<InstallDir>\Reports\YYYY-MM-DD_HHMMSS` (Setup's record,
    `HKLM\Software\xhci98` `InstallDir`; a name already there is never
    reused, `-2` and on after it, `trs_qs_name`), or where that cannot be
    made the user's documents' `xHCI98 Reports` (`SHGetSpecialFolderPathA`,
    looked up; `trs_qs_where`); then three runs of the program itself,
    each with nothing of the window's model shared: `/report`, `/snapshot`
    (its files in the folder) and `/set /show` (`XHCISET.TXT`); then
    `README.TXT` (the time, the version, what was saved). Its end is
    posted to the icon's window, which plays Windows' information or
    error sound (`MessageBeep`, looked up; "Sound when a quick save ends",
    per user, `QuickSaveSound` under `HKCU\Software\xHCI98\XHCITR`, on
    unless turned off) and then shows a box with the folder's full path,
    the documents fallback said, and what was not saved. One at a time.
    Revised by the owner the same day: the end is a balloon on the icon
    (`tray_balloon`; 98 SE: `tr_popw`'s own window, its id
    `TRAY_POP_QS`), "Quick save done" with "Saved to <folder>. Click to
    open the folder." (Windows 2000, which reports no click on a balloon,
    task 43.68: without the second sentence) (or "Quick save not complete" with what was not
    saved, a warning), whose click opens the folder (`qs_open_folder`,
    ShellExecuteEx); a text of 250 characters or more, or no icon, still
    goes in a box. "Sound when a quick save ends" decides alone: the
    balloon is always sent with NIIF_NOSOUND (7's shell plays none for an
    information balloon, XP's does), and with the option on Windows'
    information or error sound plays once (`MessageBeep`), on every
    Windows.
    Task 49.39 (issue 26 section 8; 2026-11-12): the reporter's quick
    saves taken just before a crash were lost - files a moment old can
    still be in Windows' cache. Each file the save wrote (`REPORT.TXT`,
    the snapshot's four, `XHCISET.TXT`, `README.TXT`) is opened again for
    writing and `FlushFileBuffers` called on it before the balloon is
    shown; a flush that fails is said in the balloon ("not all written to
    the disk") and the save counts as not complete. No directory flush is
    claimed. And when the folder's drive letter belongs to a disk the tree
    shows under a controller `xhci98.sys` drives (the tray's own model, read
    on the tray's thread before the save starts), the balloon adds "This
    folder is on a USB drive xHCI98 drives: if that drive is the one in
    trouble, save to another disk." - nothing is refused.
15. **Its hotkey.** Off until ticked in the Settings window ("Quick-save
    hotkey", on the tool's own tab, item 16), per user (`QuickSaveKey`, the same key: the
    `RegisterHotKey` modifiers << 16 | the virtual key). The choices
    (the owner's, 2026-11-07) are Ctrl+Alt+F11 (the default),
    Ctrl+Shift+F11, Ctrl+Alt+F10, Ctrl+Alt+F9, Ctrl+F11 and
    Ctrl+Alt+Shift+F11: a function key, never F12 (NT's debugger key),
    never the Windows key, always with Ctrl or Alt (`trs_hk_ok`); a
    stored key that is not one of them reads as off (`trs_hk_index`).
    Registered for the icon's window at the start and at each change
    (`RegisterHotKey`, looked up); a refusal is said beside the choice,
    and so, for a two-key choice (`trs_hk_keys`), is that it clashes
    with other programs' keys more often. Its limits are the user's to
    know: the program must be running, Windows must still take input,
    and a USB keyboard on a misbehaving controller may not deliver it.
16. **The icon's menu and the tool's own tab** (the owner, 2026-11-07).
    The icon's right-click menu is Open, Settings..., About, a
    separator, Quick save (report + snapshot), a separator, Exit. Refresh
    (the icon polls every second and follows WM_DEVICECHANGE; the window
    keeps its own), Start at login, Warn when a device is removed
    unexpectedly, Warn when a device can run faster and Watch Windows'
    own USB ports too leave it. They stay in the window's View menu and
    are on the Settings window's last tab, "xHCI98 USB Tree", with the
    quick-save hotkey and its sound: each saved for the user and in force
    at the click, as the menu items are; a notice option's scan waits for
    the window to close (its model is held while it is open,
    `setToolRefresh`); Apply is greyed on that tab. Since revision 30
    the tab also has "USB &drive notices" (section 7.8 item 1), and since
    revision 36 "Warn when a USB port is put &on hold" (section 24
    item 5; both "Warn when ..." labels the owner's wording of
    2026-11-15: "Both should put as warn." - access keys f and o). The
    driver tabs' list takes the room (about ten rows at 640x480). There is no Help button: this section never listed
    one.
17. **Host vectors** (`test\test_trset.c`): every row classified, the
    defaults against the INFs, the system rules on 98 SE, 2000, XP x64 and
    7, the values offered and parsed (`default`, a step, a Stor98 word),
    the driver's reading and the Now texts, the apply summary and the exit
    code with record 23's precedence, the request's round trip and each
    refusal (too small, too many, a line break, no `END`, another
    version, no NUL), the arguments, the port switchover's texts, and
    the quick save's folder name, its fallback and the hotkey's choices.
18. **Every value has a row** (task 49.38, the owner's request of
    2026-11-13, which replaces item 3's "Out" list, and the owner's
    answers of 2026-11-12). "Show advanced settings" lists every value the
    package's drivers read, and the safe-removal mark service's two, but
    the two log levels (the Logging level dialog's, item 1), `XhciCmNet`
    (Setup's Thunderbolt networking box, with the driver it stages), the
    network adapter's own values (its Properties, Advanced), every value of
    the qemu flavour and what the drivers write for the tools. **One
    table**: every row is a row of Setup's `instValues[]`, which both tools
    read (the Tree's own `trsExtra[]` is gone), so Setup's Advanced lists
    and its `/set` take the same values (record 26 section 5.4.1, "Since
    task 49.38"). `INST_VALUE` gains `Group` (the header both tools show
    above a group's first advanced row), `Kind` (a DWORD, or the text of
    `XhciCmTunnelBridge`) and `Accept` (a range's numbers below 32); the
    rules gain `INST_APPLY_NT5_XHCI` (2000, XP, XP x64),
    `INST_APPLY_NT5X86_XHCI` (32-bit 2000 and XP), `INST_APPLY_PIRQ` and
    `INST_APPLY_NOROUTE` (Setup links `src\xhci_pirq.c` for them), taken
    by pointer forms of Setup's rule functions (`inst_def_*`). The rows
    added: `XhciInterruptMode` 4 (with "Show advanced settings", 98 SE to
    XP x64; a 4 was read as the default before, a mistake), the seven
    mode-4 values, `XhciDmaStaging`, `XhciStagePoolKiB` (whole 64 KiB
    blocks), `XhciTolerance`, `XhciCommandCheck`, `XhciAccept096`,
    `XhciAcpiTables`, `XhciIntervalCap`, `XhciAvgTrbEsit`,
    `XhciUsb2HubSpeedFallback`, the fourteen Linux quirk values, five
    Thunderbolt networking values, `XhciCmInferAtStart`,
    `XhciCmAsm4242Handover`, `XhciCmGen4LinkRecovery`, `XhciCmSleepEntry`,
    `XhciCmAsmediaPcie7Credits`, `XhciCmTunnelBridge`, the mark service's
    `LogBytes`, and a UAS drive's `XhciUasNoAtaPassThrough` and
    `XhciUasFastReset`; and `XhciEventRingTrbs` (256 or 1024, any other
    value read as the driver reads it, 1024); and, with task 49.39,
    `XhciStreamDequeueRepair` in faults and checks (0 check and log only,
    1 repair; the owner's texts); and, with task 50.20,
    `XhciStreamDequeueAvoid` in controller quirks (0 off, 1 the listed
    controller, 2 every controller; draft texts for the owner). `XhciIntelNoRouteIrq` 0 on
    Windows 2000 stays the Tree's alone (item 10 asks first; Setup does
    not offer it there). A risky value carries its caution (what fails,
    and how to set it back) and every row its Default.
    *The groups*: the window shows a header row above each group
    ("Advanced settings" for the rows before, then one UAS drive,
    interrupt mode 4, memory, faults and checks, the controller quirks,
    Linux's controller quirks; on the Thunderbolt tab Thunderbolt
    networking, USB4 and the ASM4242); the list scrolls at 640 by 480 as
    before, and its row and pending-change arrays are 160.
    *The text row*: `XhciCmTunnelBridge` is a REG_SZ, offered in the number
    box with its digits-only style taken off; the page and `/set` take
    what the driver reads (`inst_bdf_list_ok`: `auto` in any case, empty,
    or one to four `bb:dd.f` separated by commas, under 64 characters) and
    write it as REG_SZ; Default writes `auto`.
    *The drive rows* (the owner's answer Q3): `XhciUasNoAtaPassThrough`
    and `XhciUasFastReset` live in each UAS drive's own key
    (`INST_LOC_DRIVE`, `TRS_LOC_DEVICE`; 2000 to 10 `Enum\<instance
    id>\Device Parameters`, 98 SE and ME `Enum\<instance id>`), which the
    driver reads when the drive starts, so a change takes the drive
    plugged in again. With one selected, "Applies to" lists "Every UAS
    drive here" and each present devnode bound to `xhciuas`; a UAS drive's
    right-click has "Settings...", which opens the USB tab with "Show
    advanced settings" on, the first drive row selected and that drive as
    the scope; there is no machine-wide form. The write: first the drive
    found present (`CM_Locate_DevNodeA`), with the driver key the window
    read and still bound to `xhciuas` (its service on 2000 and later, its
    driver key's `NTMPDriver` on 98 SE and ME); then the key opened, never
    made, with the rights the process has; on 2000 and later, refused for
    access, the drive's existing Enum key opened, its Device Parameters
    found there (a key that is not there is never made: the write is
    refused; one deleted after it was found is made again by the
    privileged open, which 2000 and XP offer no existing-only form of, and
    is left empty with nothing written and the write refused), and opened
    with
    `REG_OPTION_BACKUP_RESTORE`, the backup and
    restore privileges switched on for the one write (looked up at run
    time, as the shutdown privilege is) and given back on every exit;
    written and read back, and the drive found present again, else "was
    written, but the drive left meanwhile: it takes effect when it is
    plugged in again". The key's owner and permissions never change.
    *What is left open*: the Tree holds no handle on the devnode (a request
    to the driver for it is ruled out, section 22), so a drive can leave,
    or another be given its instance, between the checks and the write.
    Windows keeps an unplugged drive's Enum key, so such a write lands in
    that drive's own key and takes effect when it is plugged in again; the
    second check says so when it sees it. The window's list is held to a
    fresh reading before Apply, and the elevated request names each drive
    by its instance id and the driver key the window read, which the
    elevated run checks before it writes. Setup does nothing with them:
    no page, no `/set`, no uninstall step.
    *The controller's own row* (task 50.34; record 16 sections 21.5 and
    21.5.3): `XhciIntelPortSwitchReady` lives in each controller's own
    driver key and never machine-wide (`INST_LOC_CTRL`, `TRS_LOC_CTRL`):
    the machine-wide key is neither read nor written for it, and only the
    key of a controller the switchover applies to by its identity is
    (`trs_ready_applies`: a listed Intel controller, or any Intel one while
    `XhciIntelPortSwitch` 2 is in force for it, read as the driver reads
    it; never the excluded board or one whose subsystem is not in its
    hardware id). Another controller is skipped, shows "(the port
    switchover does not apply to this controller)" when it alone is the
    scope, and named to `/set` is refused. The row takes the window's
    selected controller as its scope, as a driver-key row does
    (`trs_scoped`), in reading, staging and applying. Listed with "Show
    advanced settings", on 2000 and later, where such a controller is
    present; 0 and 1
    offered; the driver reads it at a Windows session's first start of
    the controller, so a change takes a restart of Windows. It is the
    image preparation of record 16 section 21.5.3: 0 on each controller
    before an image is captured. Setup does nothing with it: no page, no
    `/set`, no uninstall step.
    `test\test_trset.c` holds the rule both ways: every value named by the
    INFs' `HKR` lines, the value definitions of the drivers' sources and
    the mark service's `Parameters` reads, and every value row of
    `docs\using\tuning.md`, is a row or on its commented exemption list;
    every row is in `tuning.md` (Stor98's own, its README's, by name), with
    the same range, default when absent and written value where
    `tuning.md`'s column is a number.

## 23. The connection manager's snapshot (revision 33, task 49.37)

The owner's decision of 2026-11-13 ("save snapshot for CM as well for
future debugging"): `XHCITR /snapshot` and File > Save Snapshot... take
`xhcicm.sys`'s state as they take `xhci98.sys`'s. The driver half, the
request `CM_QS_IOCTL` and its self-describing image `XCMS`, is design
record 21 section 8.4.

1. **Which NHIs.** Every query device that opens, `\\.\XHCICM0` to
   `\\.\XHCICM15` (section 4.2: 16 at most, the names decimal), on every
   target, each image kept only when its epoch equals that device's
   `XCMQ` answer's (as the log is matched, section 4.5). In the file
   names the device's number `n` is one character, 0-9 then A-F (device
   10 is `.CTA`), the form the controllers' `k` takes in the same run.
2. **The files**, beside the run's set and on the run's `BASE` whatever
   scheme names it: `BASE.CT<n>`, the decoded text, and `BASE.CB<n>`, the
   raw image as assembled from its windows. Made as the run's other files
   are: `.TMP` first with an exclusive create, renamed with a bare
   `MoveFileA`, never replacing a file the run did not make, and each name
   counted among the set whose existence moves the clock name's letter
   (`stamp_base`, section 15) and refuses a given name (exit 6). `/dump`
   stays the controller's dump alone. A connection manager adds no new exit code but takes part in
   the existing ones with their precedence: a `.CT<n>`, `.CB<n>` or one of
   their temporaries made by something else after the name was chosen is
   exit 8, as for a dump file (that image's own files removed, the other
   file left as it was); an image taken but its `.CT<n>` or `.CB<n>` not
   written in full is exit 3, as a dump's `.TXT` not written in full is.
   An image not taken - no connection manager, a driver that does not
   serve it, a device that answered nothing - changes no exit: it is said
   on the screen and in the `.DEV`, as a controller at a Code is.
3. **The `.DEV`** gains, after the "Thunderbolt networking" section,
   "Thunderbolt connection manager state (xhcicm.sys)": one line per
   query device - "XHCICM0: state saved in BASE.CT0 and BASE.CB0" with
   the image's size and "no counted event during the read" or "taken
   while it was changing", "not served by this xhcicm.sys", "not read:
   error <n>", "not kept: the connection manager restarted during the
   read" - or "No connection manager is running." Nothing else of the
   image goes there.
4. **The `.CT<n>`** is plain ASCII, CRLF, lines of 79 characters or
   fewer. A head - the tool's version and the local time, the device's
   number and PCI location, the image's version and size, the driver's
   version and level, the clock at the gather, and "taken whole" or "taken
   while it was changing: values may be a mixture" with the detector's two
   values - then one block per table in the image's order: its name, its
   record count, and for each record that has any nonzero value its index
   and its nonzero fields as `name=value` (hexadecimal with `0x` from 10
   up, decimal below; a signed field with a minus where negative), several
   to a line; records all zero are counted ("records 2 to 3: all zero"),
   not printed. A hashed field prints as `#3a7f`, and the head says once
   what that is: "#xxxx: an identity (a UUID, UID, MAC address or serial)
   as a hash salted anew at every start; equal hashes may be one
   device". The tables a maintainer reads first come first, in the
   driver's order (`nhi`, `cm.ctr`, `u4`, `u4.port`, `u4.link`, `u4.kid`,
   `u4.oblig`, `u4.xd`, `u4.ctr`, `icm`, `icm.dev`, `icm.ctr`, `ring`,
   `rirq`, `reenum`, `reenum.port`, `chain.bind`, `net`). A table name or
   a field name outside letters, digits and `._[]` is printed as `?`, and
   a field past the bytes received ends the block with "cut short".
5. **Privacy.** The image carries no address, no name and no user data
   (record 21 section 8.4), so the screening of section 15 has nothing
   to screen in its values; the head's own lines follow section 15's rules
   (no directory, no computer name). With `/nosysinfo` every hashed field
   prints as `withheld`, as the `.DEV`'s unscreened strings do, and the
   `.CB<n>` is written with those words zeroed and `CM_QS_F_WITHHELD` set,
   so a run that asked for no system details carries no identity in any
   form. An image of a version this tool does not read cannot be screened
   that way: under `/nosysinfo` its `.CB<n>` is not written at all, and
   the `.CT<n>` and the `.DEV` say so.
6. **Save Report and `/report`** carry in their "Snapshot" part, once,
   after the first controller's dump, each `.CT<n>`, whole: the image holds no
   address at any level, so none is withheld by level. This makes the
   report longer by about 7.5 KB a connection manager with one router
   attached and 3 KB for an inert one (read on the emulator, task 49.37's
   legs), more with more routers (owner
   question Q1). The raw `.CB<n>` comes only from `/snapshot` or `/dump`.
7. **Save Snapshot's message** (section 15 item 8's table) gains, after
   its line, one line naming the images the run saved (the owner, 2026-11-13):
   "The Thunderbolt / USB4 state is in <name>.CT0 and .CB0.", with more
   than one "<name>.CT0 and .CB0, .CT1 and .CB1.", a `.CT<n>` whose
   `.CB<n>` was not kept named alone, and no line when no query device
   answered. The window takes them from the run's own "files written"
   list in its output, not from the folder, so a file of that name another
   program made is never named (`tr_cms_saved_files` and
   `tr_cms_saved_line`, the pure half's, host-tested in `test_cm_snap`,
   sixteen images included). The
   engine's own text lists each `.CT<n>` and `.CB<n>` with the files
   written, and its attach line names each `.CT<n>`, with "(the .BIN,
   .PSC and .CB files only if asked)" on the line before it, within 79
   columns.
8. **Old and new.** A driver from before 49.37 answers
   `STATUS_INVALID_DEVICE_REQUEST`: "not served by this xhcicm.sys", no
   files. A newer image version: the `.CB<n>` is saved and the `.CT<n>`
   says "an image of version N, which this XHCITR does not read; send the
   .CB0". Within version 1 every directory decodes, older or newer.
9. **Retry.** The tool reads the whole image (window after window, each
   asking at the bytes it holds) up to three times, until one detector
   value holds before and after every window of the read and each window
   names the same whole size; it keeps the last read, and a read that
   never agreed is kept with `CM_QS_F_TORN` set in the file's header and
   the head's "taken while it was changing". The first window's header is
   the image's header in the `.CB<n>`, its detector words the read's
   lowest and highest.
10. **The pure half** (`xhcitr\tr_cms.c`, `tr_cms.h`): the image's checks
    and its decode into `TR_TEXT`, host-tested with the driver's own
    builder in `test_cm_snap` (section 8.4 of record 21) and against a
    fixture of canned bytes in the same suite; the Windows half
    (`xhcitr\tr_cmsw.c`): the device loop, the windows, the files.
11. **The all-controllers snapshot** (section 15 item 12: one `BASE` a
    run, `BASE.D<k>`, `.T<k>`, `.B<k>` and `.P<k>` a controller, each
    taken by a child run `/snapshot BASE /c k` in a scratch directory of
    its own and moved in without replacing). The connection managers'
    state belongs to the machine, not to a controller, so every child is
    started with `/nocm` (the engine's `-nocm`: no image taken), and after
    them one more child, `/snapshot BASE /cmonly` (the engine's
    `-cmonly`), started in the destination folder itself, takes it once
    with item 2's exclusive creates (`snap_cms`): a child, as the
    controllers' runs are, because the engine's state is process-wide and
    a quick save's worker allocates nothing and changes no directory. BASE
    is chosen over the whole set, `.CT<n>`, `.CB<n>` and their temporaries
    included (`tr_snap_pick_base`), so a given name with any of them is
    refused before anything is written and the clock's letter moves past
    them. The parent reads the child's list of files written for item 7's
    line; a clash ends the message with "The Thunderbolt / USB4 state was
    not saved: a file of its name was made by something else meanwhile.",
    a child that did not start or end in two minutes with "The
    Thunderbolt / USB4 state was not taken: its run did not start, or did
    not end within two minutes." (exit at least 2), one that could not be
    ended with "... did not end, and may still be writing." (exit 9, as a
    controller's), another exit with "... its run ended with <n>." (at
    least 2), a file not written in full with " Not every one was written
    in full." after item 7's line, or "The Thunderbolt / USB4 state was
    not saved in full: a file of it could not be written." when no
    `.CT<n>` was; and the exit joins the run's: a clash 8, a file not
    written in full 3, the run's 9 kept. The "Thunderbolt networking (xhcicm.sys)"
    section stays where section 15 item 12 puts it, once, in the first
    controller's `BASE.D<k>`. The image's own lines go to no device
    report in that run (the `.CT<n>` head carries them). `/snapshot /c N`
    keeps the single set, its `.DEV` and its images as items 2 and 3 have
    them.

## 24. A held port (roadmap-usb4 task 49.43, owner 2026-11-13)

A user whose device failed enumeration three times read the result as
"the device is not recognised": the driver's tolerance (design record 17)
had held the root port, and only the snapshot's tolerance block said so
("root port N charges 3: re-enumerations 3 of 3 ... NOW HELD,
re-enumerations spent (powered)"). The tool now says it where the user
looks, with no driver change.

1. **The reading.** Each xhci98 controller's hold bytes are read through
   the snapshot's own door windows, as `tr_snap.c`'s `print_tol` reads
   them, read only (since revision 36, roadmap-usb4 task 50.7, first
   through the holds request, record 13 section 8.12.1, answered at every
   logging level; the windows below only where a driver before it
   refuses that request): the HCD region's first window for the tolerance
   block's place in the extension (header words 8 to 18:
   `XHCI_SNAPSHOT_HCD_TOL_*`), then the extension's window at the block,
   and at most three more where a location lies past it; each location's
   hold is its byte 26 (`XHCI_TOL_LOC.Hold`; `tr_tol_map`,
   `tr_tol_hold_offset`, `tr_tol_hold` in `tr_log.c`, pure, and
   `hold_read` in `xhcitr.c`). The door copies memory and reads a
   register only of a started controller in D0 (`hcdDoorSnapshot`), so
   the reading never wakes a suspended one. At level 0 the door is closed,
   so with a driver before the holds request nothing is known then
   (`HoldState` -1); with this one the request answers at level 0 too. Read at each refresh of the
   window's and the report's model, on the thread that refreshes it, as
   its other requests to the controller are (the query's header among
   them, which waits on the same door gate); a tray with no window open
   does not read them there, only on its job thread (item 5).
2. **At the port.** A held root port is shown in the tree even when no
   device is reported there, its line ending " (on hold)"; its details
   carry, after "Connection:", for hold 1 (re-enumerations spent,
   powered: a replug or a controller start ends it) "Note: This port is on
   hold after repeated failures. Unplug the device and plug it in again.
   The hold also ends when the controller restarts." and for holds 2 and
   3 (unpowered: only a controller start ends them) "Note: This port is
   switched off after repeated failures. It stays off until the
   controller restarts: restart Windows, or disable and enable the
   controller in Device Manager." Save Report and `/report` carry the
   details.
3. **A hub's port.** The driver numbers a hub location by its hub object,
   which the tool cannot match to a hub of its tree, so a held hub port is
   said at the controller's details instead: "A port of a hub on this
   controller is on hold after repeated failures. ..." (or "is switched
   off ...", with the same advice), once per kind, and only while a hub is
   connected to that controller: the driver keeps a hub location's hold
   after the hub is unplugged, until it reuses that hub object or the
   controller starts (`hcd_hub.c`), so with a hub connected the note can
   still be an earlier hub's. A hub's hold is not announced in a balloon.
4. **No hold shown.** Where an xhci98 root port shows no hold note (none
   known - an older driver at level 0, or a read failed - or, since
   revision 36, none there) and its connection is "the device failed
   enumeration": "Note: If this keeps failing, unplug the device and plug
   it in again." A hold note keeps priority: a held port shows that note
   alone. The bus reports that connection at a root port alone, while the
   port's machine is in Failed - for good once its one retry is spent,
   and briefly between a failure and that retry, where the conditional
   advice suits as well (in the emulator it is made with the qemu
   flavour's fault 20, record 17 section 5); a hub's port
   with no device record is answered as no device connected
   (`hcd_door.c`, `hcdDoorConnection`), so the note never appears at a
   hub's port, although the tool would print it at any xhci98 port so
   reported (`details_uport` in `xhcitr.c`). Until revision 34 this item
   listed a hub's port among its cases; read on XP on 2026-11-14, a hub's
   port whose device failed its enumeration showed "no device connected"
   and no note (`runs/run-49.md`, "49.43"). Until revision 36, at logging
   level 0 the door was closed, so a held port showed "(empty)" with no
   tag, no note and no notice (read on XP on 2026-11-14); the holds
   request (item 1) answers at level 0, and with a driver before it that
   is still so. Read under task 50.7 (`runs/run-50.md`, "50.7"): on 2000 SP4
   and 98 SE at logging level 0, the fallback note at a root port the
   qemu flavour's fault 20 failed, and a held root port's tag, note and
   notice (the notice with a diagnostic build of the tool that also logged
   each watch, never shipped).
5. **The pop-up.** While the tray runs, every 10 s (`HOLD_POLL_EVERY`
   polls of its one-second timer) a job thread of its own, one at a time
   as the quick save's, reads each xhci98 controller's holds again and
   posts the results to the tray (`WM_TRHOLD`), so a controller whose
   door a recovery holds keeps only that job waiting, never the tray; no
   new job starts while one is out. The job and its buffer are allocated
   and freed on the tray's thread alone, and the job thread makes no C
   runtime heap call, because the x86 build links the single-threaded
   runtime, whose heap is not serialized; a job whose results cannot be
   posted is freed by the next watch. Every location read goes to an
   announce-once queue (`tr_holdq_observe`: a hold seen where none was,
   or another kind, is queued once; a location seen clear is forgotten,
   so a later hold announces again; one not read keeps what it had; at
   most 16 queued, the rest counted); an announcement whose location has
   since cleared or changed kind is dropped unshown (`tr_holdq_peek`), and
   one is shown only from a reading of its controller in the same job,
   while a controller not read this time keeps its own queued and lets
   the others' pass (`tr_holdq_take`); a controller no longer listed when
   the results come back is forgotten with its announcements, its reading
   not taken (`tr_holdq_keep_keys`). A change of a location's hold, a
   clear included, ends its episode and its queued announcement with it,
   so a location has one queued at most.
   At most one is shown a watch, and none within 10 s of another balloon
   of the tool's, so a removal warning keeps the screen; on 98 SE the
   tool's own pop-up is handed one only when none is showing or waiting,
   so it is shown at once and never after a wait in which the hold may
   have ended. Root ports alone are announced. The balloon is titled
   "USB port on hold": "Port N of <controller> is on hold after repeated
   failures. Unplug the device and plug it in again.", for holds 2 and 3
   "Port N of <controller> is switched off after repeated failures.
   Restart Windows to turn it on again." (both texts the owner's, the
   second accepted 2026-11-14). **The switch** (owner, 2026-11-15:
   "Make it switchable and perhaps off by default", taken as one switch,
   off by default on every Windows; revision 36): the watch and its notice
   are the user's to turn on, by the tick box "Warn when a USB port is
   put &on hold" on the Settings window's tool tab (section 22 item 16),
   under "USB &drive notices", the quick-save rows moving 14 DLU down
   again: the DWORD `HoldNotice` under `HKCU\Software\xHCI98\XHCITR`
   (`tr_hold_watch_on`: only a `REG_DWORD` 1 is on; absent, as after an
   install or an update from `3.5.0.0`, which stored none, off), as the
   drive notice's (section 7.8 item 1); no computer-wide value, no
   installer box, no driver value. Off, the tray starts no job, opens no
   controller and reads nothing for it, and shows no hold balloon; the
   window's own reading at each refresh (item 1) is not the switch's, so
   a held port's tag and note show either way. On, it works as above.
   Turned off, the queue, what it had seen, the re-send watch and an owed
   re-send are dropped at the click (and at the next watch for a change
   made elsewhere), so a hold already there when it is turned on again is
   announced once. The watch's earlier rule - on 2000 and later the
   removal warning's option, on 98 SE and ME always (owner, 2026-11-14;
   `tr_hold_notice_on`, kept and host-tested) - is replaced: the removal
   warning's option no longer touches it. Why off by default (the owner's
   reason through the coordinator): the watch is periodic kernel activity
   on every machine with the tray running since `3.5.0.0`, and the notice
   a convenience. An announcement leaves the queue only when
   its send is taken (`tr_holdq_find`, then `tr_holdq_sent`): the icon
   there and the shell's `Shell_NotifyIcon` call accepted, or on 98 SE
   the tool's pop-up ready. Until the icon is there (at login before it
   is added, or after Explorer restarts, the faster notice's rule of
   section 7.7) it waits uncounted; a refused call is tried again at a
   later watch, one try a watch, and dropped (counted) at the thirtieth
   (`TR_HQ_TRIES`, about five minutes); one whose hold has ended or
   changed meanwhile is dropped, never shown. Known limit: a balloon the
   shell accepted and then never showed (one queued behind Windows' own,
   section 7.7 item 9) was not sent again until revision 36: the hold
   notice now has the re-send watch of section 7.7 item 9 (`holdSend`,
   `holdKind`, `TRAY_BK_HOLD`), stepped by the shell's messages and the
   one-second tick. A re-send it calls due is owed, and made only at the
   next watch's results on that job's usable reading of the same
   controller (fresh, no sleep or device change since its read began, the
   controller still listed) still showing the same hold at the same
   location, the icon's balloon taken away first and the text as first
   sent; a usable reading showing anything else, the controller gone, the
   balloon shown meanwhile, or no usable reading within `TR_BS_NOSHOW_MS`
   of falling due drops it (`tr_hold_resend_decide`, host-tested). Any
   other balloon of the tool's, sent through `tray_balloon_send` or its
   wrapper, ends it; so does turning the notice off. 98 SE and ME: the
   pop-up answers no message, so nothing changes there. A controller's reading is used only within 3 s of the moment
   its read returned (`TR_HOLD_FRESH_MS`, so a hold read before another
   controller's door kept the job waiting is not announced after it may
   have ended), and only when no suspend, resume or device change came
   since that controller's own read began (one during an earlier
   controller's read no longer counts against it); the next watch reads
   again. Each job starts
   its reading one controller on from the last job's (`tr_hold_rot_index`),
   so every controller is in turn the last one read, and with N
   controllers a stable hold is announced within N watches however slow
   another's door is. Known limit: a controller whose door never returns
   holds the job, and the tray announces no hold until it does; the
   port's note in the window does not depend on the job. No new
   component and no new request.
6. **Host vectors** (`test\test_xhq.c`): the map against the driver's
   header words and `XHCI_TOL_LOC.Hold`, the hold values against
   `XHCI_TOL_HOLD_*`, bad shapes, a window that does not hold the byte,
   the texts word for word, a balloon within 255 characters, the
   announce-once rule, the option it follows on NT and on 98 SE and ME,
   an announcement owed with no icon and then sent, a refused send kept
   and then taken, the refusals' bound, one released while it waited,
   a reading aged past its bound or with a resume or device change since
   its own read began unused, one with a device change only during
   another controller's read still announced,
   a stable hold announced once within N watches with another
   controller's door slower than the bound every time (two and three
   controllers, one alone, the counter's wrap),
   an announcement cleared before it is shown, a
   removed controller and an unreadable one, and both bounds. The tool's self-test holds
   `hold_root`, `hold_hub_kinds` and the note's wrapping.

## 25. The USB devices' driver keys (roadmap-usb4 task 50.29, owner 2026-11-17)

The owner, 2026-11-17: Save Report and `/snapshot` list every value of
each USB device's driver key - name, type and data - read only, in both
registry layouts, whatever driver the device has, so that one file
carries what a driver records about its device (some USB audio and
network drivers keep their own diagnostic values there). The tree window
does not change, and nothing is written anywhere. Until this revision the
report read three values of that key beside `DriverVersion` (`InfPath`,
`MatchingDeviceId`, `ProviderName`, the owner's of 2026-11-13) and the
`.DEV` seven, the latter for this package's own devnodes only.

"Every value" means: every value's name, type and size, always; and its
data, within the limits of item 2, unless item 3 withholds it.

1. **What the walk reaches.** Read from the code before this revision:
   - *The report* lists every controller the tool opens: xhci98's doors
     (`XHCI98HC<n>` on NT, `HCD<n>`), every `HCD<n>` another stack makes,
     and on NT every device of the USB host controller interface, Windows'
     own controllers included (`usb_controllers`). Under each it walks the
     root hub's ports and every hub's ports below them, to six hubs deep
     (USB allows five), so a device behind a hub is reached
     (`usb_scan_hub`). Each connected port's device is matched to its
     devnode by the driver key name its hub returns (`walk_find_driver`),
     and the port's details print that devnode's Device Manager entry.
   - That devnode is the whole device's. A composite device's interfaces
     have devnodes of their own, children of it with `&MI_nn` in their
     device id (made by `xhci98.sys` under its `USB\XHCI98_COMPOSITE`
     parent, record 13 section 10.13, and by Windows' own composite
     driver on its own controllers). A driver bound to one interface - the
     audio driver of a USB headset, the network driver of one function -
     sits on such a child, and the function drivers of a device make
     devnodes of their own below it in turn, on other enumerators: an
     input device's HID collections (`HID\...`, bound to the mouse,
     keyboard or a vendor's HID driver), a storage device's disks
     (`USBSTOR\...`, or `SCSI\...` under `xhciuas.sys`), a composite
     function that is itself split again. **The report did not reach any
     of them**: only the storage transport's note looked at the children,
     for their service.
   - *The `.DEV`* lists the PCI xHCI controllers and the devnodes
     `xhci98.sys` or `xhciuas.sys` drives (controllers, root hubs, hubs,
     xhci98's composite parents, UAS drives): **a device run by any other
     driver is not in it at all**, so neither are its interfaces.

   The coverage this revision takes, for both outputs alike, follows the
   purpose: one file carries what any driver in a USB device's stack
   records. **A USB device is listed with its own devnode and every
   devnode below it, whatever its enumerator**, depth first, to
   `TR_REG_TREE_DEPTH` (4) levels below the device and at most
   `TR_REG_TREE_NODES` (32) devnodes a device: its functions and
   interfaces (level 1), a function split again and the HID collections
   or disks a function's driver made (level 2), and two levels more for a
   driver that makes devnodes of its own below those. Every enumerator is
   followed because a devnode below a USB device exists only because the
   device does, and the driver bound to it - the HID class's mouse or
   keyboard driver, a disk driver, a vendor's filter - keeps its settings
   and counts in that devnode's own driver key, so leaving an enumerator
   out would leave that driver's record out; and none is singled out
   because no list of enumerators would cover a vendor's own bus.
   **The walk never goes below a hub**: a hub's devnodes are the devices
   on its ports, and each is listed as a device of its own (in the report
   under its own port's node). A hub is told by its device id (a root hub,
   `ROOT_HUB` in it; xhci98's hubs, `USB\XHCI98_HUB...`, which carry no
   class id, `tr_reg_hub_id`) or by `USB\Class_09` among its hardware or
   compatible ids (Windows' own hubs, `tr_node_is_hub`); its own key is
   listed. Where the walk stops at a limit, or the devnodes below one were
   not all read, it says so on a line of its own: "(the devnodes below it
   are not listed: the report follows devnodes 4 levels below a USB
   device)", "(N more devnodes below this device not listed: the report
   lists 32 devnodes of one device)", "(the devnodes below it were not all
   read: error E)", or with no number where the tool's own walk marked a
   devnode's children incomplete. The two bounds keep one device from
   crowding the file; the legs have shown one level below a device, a
   nested function with HID collections below it takes three, and the
   fourth leaves room for a driver's own children.

   One pure walk serves both outputs (`tr_reg_tree_walk`, item 7), over
   callbacks: *the report* walks its devnode list (`gw`, the parent of
   each devnode recorded by the tool's walk), starting at each connected
   port's device; *the `.DEV`* walks the configuration manager's tree
   (`CM_Get_Child`, `CM_Get_Sibling`) from the root, and a USB device
   there is every devnode whose device id begins `USB\` reached through
   no other USB device - so a device on any controller, present, whatever
   drives it, and not started included - except the root hubs, which are
   a controller's, not a device's; the walk takes it and its devnodes, and
   goes on into a hub's devnodes for the devices on its ports. So each
   device reaches the same devnodes in both. On 98 SE and ME where
   `cfgmgr32.dll` does not answer, the `.DEV` reads `HKEY_DYN_DATA`'s
   list, which holds no tree: each `USB\` devnode is then listed on its
   own, nothing below it is followed, and the section says so. The `.DEV`
   can list more devices than the report (a device behind a seventh hub,
   one the report's port walk did not match to a devnode); a device in
   both carries the same keys. A devnode is listed once per device that
   reaches it, and a devnode reaches only one device, its ancestor.

2. **The size limits and how a cut is shown.** Per value: a string up to
   512 characters, then `..." (the first 512 of N characters)`; a
   multi-string's strings one to a line, up to 32 strings and 2048
   characters, then `(N more strings not shown)`; binary data, and every
   type the tool does not decode, in hexadecimal up to 256 bytes, sixteen
   to a line with the offset, and a cut one says `N bytes, the first 256
   shown`. A value is read whole or not at all (the registry offers no
   partial read): up to 65536 bytes, and a larger one is listed with its
   name, type and true size and no data (`"Big" = REG_BINARY, 70000
   bytes: more than 65536, not read`). A
   value's name is shown up to 128 characters. Per key: at most 256
   values or 64 KB of text, whichever comes first; the rest are counted
   by name and size alone and said ("N more values not listed: the report
   lists 256 values / 64 KB of a key"). A value whose name is too long for
   the name buffer still counts. A count that an enumeration error ends,
   or that reaches its own bound of 100000, is a lower bound and is said
   as one: "(N or more values not listed: ...; the count stopped at value
   I: error E)", "(N or more values not listed: ..., and counts no further
   than 100000)", or, when the error comes at the first value counted,
   "(the values past the report's 256 values / 64 KB of a key were not
   counted: the count stopped at value I: error E)". The bounds keep one
   driver's key from crowding out the report. **There is no bound on the
   whole**: a device takes at most 32 keys of 64 KB of text (about 2 MB
   with the report's indentation), and a machine with many USB devices
   whose drivers keep large keys makes a large file; the keys the legs
   have read take a few hundred bytes to a few KB each.

3. **What is not printed.** A driver key holds a driver's settings and
   records: numbers, names, paths, a locally administered network address
   (acceptable, as serial numbers already are in the report). Nothing
   there is expected to be a credential, but an older wireless adapter's
   driver could keep its network's keys among its settings, and a vendor's
   tool a licence key. The rule: **a value whose name says it may hold a
   secret is listed with its name, type and size, and its data withheld**
   ("withheld (its name suggests a key or password)"). The rule is the
   owner's, decided on 2026-11-17 (closing section 13's point; it replaces
   the wording of this item until then, which matched fragments anywhere
   in a name and, for a day, a narrower whole-word rule that showed every
   name ending in `Key` but a few compounds):
   - **A word of the name** is a run of its letters (ASCII `A` to `Z`,
     `a` to `z`; a byte from 0x80 up is not a letter). Words are split at
     every character that is not a letter (a digit, `_`, `-`, a space,
     `.`, anything else; it belongs to no word) and where a small letter
     is followed by a capital. So `WepKey1` is the words `Wep` and `Key`,
     `password_hash` is `password` and `hash`, `K_e_y` is `K`, `e` and
     `y`, and a name in capitals alone runs its words together into one:
     `USERPASSWORD`.
   - **The terms** are `KEY`, `PASSWORD`, `PASSWD`, `PASSPHRASE`, `PWD`,
     `SECRET`, `CREDENTIAL`, `PSK`, `TOKEN`, `LICENSE`, `SSID` (a wireless
     network's name, which public databases place on a map), `PIN`,
     `PINCODE`, `WPAPSK`, and `PIN CODE`: `PIN`, then one or more
     characters that are not letters, then `CODE` (`Pin Code`, `Pin_Code`;
     the one term whose letters a separator may part, and only there).
     **The words of a secret's form** are `HASH` (its plural `HASHES`
     too), `HEX`, `VALUE`, `MATERIAL` and `LIST`. `WPAPSK`, `PIN CODE` and
     `HASHES` were added the same day, 2026-11-17, within the owner's
     decision to withhold secrets, so that three ordinary spellings of
     names it already meant to withhold are withheld: `WPAPsk` (one word
     with small letters, where `PSK` does not begin a word), `Pin Code`
     and `PasswordHashes`.
   - **A name's data is withheld** when, case ignored, a term's letters
     stand side by side in the name beginning at the start of a word, or
     anywhere in a word with no small letter, and after them come, in
     order: an `S` or none; characters that are not letters, if any; one
     of the words of a secret's form, its letters side by side, with an
     `S` or none (`HASH` also as `HASHES`), or none of them; and nothing
     more but characters that are not letters to the name's end
     (`PasswordHash1`). So the last word `Key` or `Keys`
     withholds a name whatever comes before it (`EncryptionKey`,
     `WepKey1`, `Key0`), as does a secret term at the end
     (`UserPassword`), a secret term followed by a word of its form
     (`PasswordHash`, `PasswordHashes`, `WpaPassphraseHex`, `PskValue`,
     `KeyMaterial`, `SsidList`, `Key1Value`), and a term at the end of a
     name in capitals alone (`USERPASSWORD`, `WEPKEY`).
   - **The harmless names**: a name that is, whole and case ignored,
     `HotKey` or `ShortcutKey`, with an `S` or none, is shown. The list is
     a draft for the owner and kept short on purpose: `HotKey` is the
     owner's own example, a keyboard's or a remote control's hot key
     setting (a key code or a combination, no secret); `ShortcutKey` is
     the same setting under its other common name. Names that would only
     name a registry key (`SubKey`, `RegistryKey`) were left out: no
     driver key read here holds one. A harmless name with anything added
     (`HotKey1`, `HotKey_`) is not on the list and is withheld.

   The owner's examples of names to be shown - `HotKey`, `KeyIndex`,
   `TokenCount`, `Monkey`, `Keyboard`, `*SSIdleTimeout` - stay shown:
   `HotKey` is a harmless name, `KeyIndex` and `TokenCount` end in another
   word, and `Monkey`, `Keyboard` and `*SSIdleTimeout` have no term at a
   word's start with nothing after it (`key` in `Monkey` is inside a word
   with small letters; `SSIdle` is one word). A name of single letters
   that only spells a term across other characters, `K_e_y` or `K1E2Y`,
   has no term: the letters of a term stand side by side.

   | Name | Outcome | Why |
   |---|---|---|
   | `Password`, `UserPassword`, `TrkPassword`, `PassWord`, `Passwords` | withheld | `PASSWORD` from a word's start to the end, with an `S` or none |
   | `admin_pwd`, `AdminPwd`, `PASSPHRASE`, `Secret`, `ClientSecret`, `UserCredential`, `Credentials`, `WpaPsk`, `AuthToken`, `License` | withheld | the term ends the name |
   | `USERPASSWORD`, `WPAPSK`, `WEPKEY`, `MONKEY`, `TURKEY` | withheld | a term at the end of a word in capitals alone |
   | `password_hash`, `PasswordHash`, `PasswordHash1`, `PasswordHashes`, `PasswordHashes1`, `PASSWORDHASH`, `WpaPassphraseHex`, `PskValue`, `TokenList`, `KeyMaterial`, `Key1Value`, `KeyValue`, `SsidList`, `SSIDList`, `SsidLists` | withheld | a term, then a word of a secret's form (`HASHES` the plural of `HASH`), then digits or none |
   | `PasswordLength`, `SecretCount`, `LicenseType`, `PasswordHashed` | shown | another word after the term (`Hashed` is not `Hash`) |
   | `Pass_word`, `PasswordString`, `KeyData` | shown (known residuals) | a term parted by a separator; a word not of a secret's form after the term |
   | `TokenCount`, `TokenErrors` | **shown** (`TokenCount` an owner example) | `Count`, `Errors` after the term |
   | `WPAPsk` | withheld | `WPAPSK` at the word's start |
   | `WPAPskLength`, `WPAPSKLength` | shown | another word after `WPAPSK` |
   | `LicenseKey`, `ProductKey`, `Product Key`, `CdKey`, `WepKey1`, `Wep_Key_2`, `WpaKey`, `NetworkKey`, `Network Key`, `WirelessNetworkKey`, `DefaultKey`, `PreSharedKey`, `SharedKey`, `PassKey`, `EncryptionKey`, `PrivateKey`, `AuthKey`, `ApiKey`, `SessionKey`, `TrkKey1`, `my-key`, `HotKey1` | withheld | the last word `Key` |
   | `Key`, `KEY`, `Key0`, `KEY_1`, `Keys` | withheld | `Key` the last word, and the only one |
   | `HotKey`, `HotKeys`, `HOTKEY`, `ShortcutKey` | **shown** (`HotKey` an owner example) | a harmless name |
   | `KeyIndex`, `DefaultKeyId`, `WepKeyLength`, `KeyHexagon` | **shown** (`KeyIndex` an owner example) | another word after `Key` (`Hexagon` is not `Hex`) |
   | `K_e_y`, `K1E2Y`, `K_E_Y`, `K e y` | shown | single letters; no term's letters side by side |
   | `Keyboard`, `KeyboardDataQueueSize`, `Monkey`, `Turkey` | **shown** (`Keyboard`, `Monkey` owner examples) | no term with nothing after it; `key` inside a word with small letters |
   | `PIN`, `UserPin`, `PinCode` | withheld | `PIN`, `PINCODE` |
   | `Pin Code`, `Pin_Code`, `Pin-Code2`, `Pin  Code`, `Pin Codes` | withheld | `PIN CODE` |
   | `Pin Codec`, `PinCodeLength`, `Pin CodeLength`, `Spin Code` | shown | letters after `Code`; another word after `PinCode`; no `Pin` at a word's start |
   | `Pinned`, `Spindown`, `SpinUp`, `SpinValue` | shown | no `Pin` at a word's start |
   | `SSID`, `DesiredSSID`, `Ssid2` | withheld | `SSID` ends the name |
   | `*SSIdleTimeout`, `*SSIdleTimeoutScreenOff` | **shown** (`*SSIdleTimeout` an owner example) | `SSIdle` one word; the letters after `SSId` |
   | `*PMWiFiRekeyOffload`, `*SelectiveSuspend` | shown | no term |
   | `NetworkAddress`, `TrkNetworkAddress`, `DriverDesc`, `MatchingDeviceId`, `InfPath`, `DriverVersion`, `SetupPreferredAudioDevices` | shown | Windows' own values |
   | `XhciLinkKeep`, `Uac2UsbStats` | shown | this tree's and a driver's own values |

   Against what drivers keep: every value name this tree's INFs write
   (the `Xhci*`, `XhciCm*` and the NDIS miniport's parameters) and the
   values Windows writes (`DriverDesc`, `InfPath`, `MatchingDeviceId`,
   `ProviderName`, `NetworkAddress`, `SetupPreferredAudioDevices`) are
   shown; so are Microsoft's standardized INF keywords for network
   adapters `*SSIdleTimeout` and `*SSIdleTimeoutScreenOff` (NDIS
   selective suspend), which the rule before 2026-11-17 withheld for the
   letters `SSID` in `SSId`, and the sibling driver's `Uac2*` values (the
   50.29 XP leg). No public document seen lists a wireless driver's own
   value names for its keys.

   What the rule lets through, the known residuals: a term followed by a
   word that is not of a secret's form (`PasswordString`, `KeyData`,
   `PskBlob`); a term inside a word with small letters, not at its start
   (`userpassword`, `Mypassword`); a term parted by other characters
   (`Pass_word`; `PIN CODE` is the one term a separator may part, and no
   rule passes over separators in general, since the letters of a term
   stand side by side); and every secret kept under an innocent name or
   inside an opaque blob. What it withholds that is not a secret: a name in capitals
   alone ending in a term's letters (`MONKEY`, `TURKEY`), a term followed
   by a form word in a harmless sense (`KeyValue`), and every name ending
   in `Key` not on the harmless list (a keyboard's `MuteKey`). The rule
   reads no character before the name's start or past its end. It is
   `tr_reg_secret_name`, pure.

   **The rule is a heuristic, and the reports can still hold sensitive
   data.** It goes by the value's name alone, because the data of a
   credential looks like any other string: a password or a cryptographic
   key stored under a name the rule lets through (above), or inside an
   opaque blob, is printed. The user documentation says so, and that the
   report is to be read before it is shared.

   Beyond the name rule each output keeps its own privacy rule,
   unchanged in kind: the report, which already shows serial numbers and
   instance ids, prints the data as read; the `.DEV`, which is pasted
   publicly, screens it as it screens every other string (section 15;
   `docs\contributing\snapshot-reader.md`, "Privacy"): a string, and
   binary data, holding one of the machine's names is withheld ("withheld
   (holds this machine's name)"). The screen sees the bytes before
   anything is replaced for printing: the data as stored with its NULs
   left out (so a name kept as UTF-16 in ASCII letters reads whole),
   control bytes as blanks, and every byte of the system's code page kept,
   since the machine's names are in that code page and may have letters
   outside ASCII; then, the data read as UTF-16 at either byte alignment
   and converted to the system's code page (`WideCharToMultiByte`,
   `CP_ACP`, in the Windows half), so a user name with letters outside
   ASCII is found in either form. Only after the answer are the bytes
   outside printable ASCII shown as `?`. A text longer than the screen's
   2047 characters is screened in windows of 1800 overlapping by 300
   (`tr_reg_screen_windows`), so a name across a window's edge is whole
   in one. With `/nosysinfo`, or where the names were not read, no string
   or binary data is printed ("withheld (not screened)") and the value
   names keep only the characters of an identifier, while numbers are
   still printed. The device's instance id stays withheld in the `.DEV`
   as before.

4. **The format, and the readers of the two files.**
   - Each value is one line, then any lines of its data, indented under
     it: `"Name" = REG_SZ "text"`; `"Name" = REG_EXPAND_SZ "text"`, not
     expanded; `"Name" = REG_DWORD 305419896 (0x12345678)`;
     `REG_DWORD_BIG_ENDIAN` and `REG_QWORD` likewise (the QWORD's decimal
     made from its two halves, with no 64-bit arithmetic); `"Name" =
     REG_MULTI_SZ, 3 strings:` and each string quoted on its own line;
     `"Name" = REG_BINARY, 4 bytes: 78 56 34 12`, and above 16 bytes the
     offset lines; `REG_NONE`, `REG_LINK` and the three resource types by
     name and in hexadecimal; an unknown type as `type 0x0000002A` in
     hexadecimal. The unnamed value is `(default)`. A string is shown up
     to its first NUL or its stored size, whichever comes first; a byte
     outside printable ASCII is shown as `?`, as the `.DEV` shows every
     string; a quote inside a name or a string is shown doubled (`"say
     ""hi"""`), so the closing quote is the only single one. A DWORD or
     QWORD of the wrong size is shown in hexadecimal with its size:
     `REG_DWORD, 3 bytes (not 4): 01 02 03`.
   - The report: under each USB device's node, after its details and
     descriptors, a block "Driver key values (read only)": the key as
     Windows names it, the values, a count; then for each devnode below
     the device, in the walk's order, "Interface MI_nn" (a devnode with
     `&MI_` in its id) or "Devnode below it", with ", level N below the
     device" from level 2 on, its description, its instance id and its
     key's block; the walk's own lines (item 1) where they fall. The
     node's earlier lines are unchanged and in the same order, so a reader
     of the report that looks for them finds them where it did; the new
     lines carry the node's `| ` lead like every detail line. The window's
     details pane is the same function's other caller and does not show
     the block (`write_report` adds it).
   - The `.DEV`: a section of its own, "USB devices' driver keys", after
     "Thunderbolt networking" and before the dump's note, one entry per
     devnode: "USB device N" for a device, "USB device N, a devnode below
     it (level L)" for each devnode below it, each with its id (the
     instance withheld), its description, its key and its values; the
     walk's own lines; and at the end the count ("N USB device(s) listed,
     with M devnode(s) below them, of K devnodes looked at"). The `.DEV`
     has no layout version: it is read by people, by its section headings,
     and no script of this tree parses it; every earlier section keeps its
     text and place. The dump's `.TXT` and the snapshot's schema are not
     touched. Save Report's own snapshot run passes the engine's new
     `-nokeys` (XHCITR's `/nokeys`, as `/nonet`), so the report does not
     carry each key twice, and so does every controller after the first
     in a `/snapshot` of every controller; the section then says where
     the keys are ("left out of this run: Save Report lists them under
     each USB device, and a snapshot of every controller in its first
     controller's device report"). Since both outputs take the same walk,
     the report under its devices carries everything the section would.

5. **Both bitnesses and both registry families.**
   - The reading is `RegOpenKeyExA` with `KEY_READ` and `RegEnumValueA`,
     both in the tool's import allowlist already for both builds, as is
     `WideCharToMultiByte` (KERNEL32, the screen's UTF-16 view), so the
     allowlist does not change. `XHCITR64.EXE` reads natively. `XHCITR.EXE`
     on 64-bit Windows hands the window and `/report` to `XHCITR64.EXE`
     (the WOW64 hand-off), but runs `/snapshot` itself: under WOW64 the
     driver keys read the same either way, since
     `HKLM\SYSTEM\CurrentControlSet` is not one of the keys WOW64
     redirects. The formatter's sizes are 32-bit counts and its data is
     bytes, so nothing in it depends on the pointer size.
   - The key is `SYSTEM\CurrentControlSet\Control\Class\<Driver>` on 2000
     and later and `System\CurrentControlSet\Services\Class\<Driver>` on
     98 SE and ME, `<Driver>` the devnode's `Driver` value
     (`{guid}\nnnn` on NT, `<class name>\nnnn` on 9x), as both files open
     it today (`driver_key_path`, and `dev_consider`'s NT-then-9x try).
   - On 98 SE and ME a value is whatever type its writer gave it, and a
     number an INF or a driver writes there can be four bytes of
     `REG_BINARY` rather than a `REG_DWORD` (`tr_snap.c`'s `reg_dword`
     takes either form for that reason). Such a value is shown as what it
     is, `REG_BINARY, 4 bytes: 01 00 00 00`, never guessed into a number:
     four bytes of binary data need not be a number. The ANSI calls hand 9x's strings
     back as stored, which may lack the closing NUL, so a string is
     bounded by its size; on NT they hand back the system code page's
     form of each Unicode string. A value name on 9x is at most 255
     characters and on NT 16383; the name buffer holds the NT bound.

6. **A key that does not open, a value that changes.** A devnode with no
   `Driver` value has no driver installed and says so ("No driver key: no
   driver is installed for it."); a key that does not open says "not
   read: the key did not open (error N)". The values are read one index
   at a time into a 65536-byte buffer. A read answered "more data" asks
   again with no buffer for the name and size: a value larger than the
   buffer is listed as above; one that now fits had grown and shrunk, and
   is read again; after three reads that each found a different size the
   value is listed as "its size changed at each of 3 reads; not shown". A
   value gone between the read and the size query is not listed, and the
   size query's "no more items" ends the key there, as Windows says it
   ends; an enumeration
   that fails with any other error stops the key there and says so ("the
   reading stopped at value N: error E"), and the count past the limits
   says its error as item 2 does. A key written to while it is
   read may show a value twice or miss one, since the indexes move; the
   block is what each call answered. The type shown is the one the read
   that returned the data gave.

7. **The pure half** (`xhcitr\tr_reg.c`, host-tested in
   `test\test_trreg.c`): `tr_reg_secret_name`; `tr_reg_value_text`, one
   value's lines from its name, type, data, size and a screen (a
   callback for the text and one that converts UTF-16 to the system's
   code page); `tr_reg_key_text`, a key's block over a callback with
   `RegEnumValueA`'s contract, with the limits and every outcome of item
   6; `tr_reg_screen_windows`, the screen's windows; `tr_reg_tree_walk`,
   one device's devnodes over child, sibling, hub, block and line
   callbacks; `tr_reg_hub_id`. The vectors: every type; the bounds the
   suite names, at and past each (a 512 and a 513 character string, 32
   and 33 strings, 2048 characters of strings, 256 and 400 bytes, a value
   of exactly 65536 bytes and one of 65537, the 257th value, the 64 KB
   text bound, a 200-character name and one of the registry's 16383);
   a wrong-sized DWORD and QWORD, the 9x four-byte binary number shown as
   bytes, a string with no NUL and one with data after its NUL, the
   unnamed value, a value that grows between the size query and the read
   (once, then at every read), one larger than the buffer, one gone
   between the calls, a key that does not open, an enumeration error; the
   count past the limits (exact, a name too long for the buffer, an error
   at once and later, at, one short of and past its own bound); the name
   rule's table of item 3, each name with its outcome, and short names,
   the table's names and 16383-character names (ending in a term, in
   `Key` after a word, as the one word and in capitals, in a term or a
   form word cut by the name's end) read against a guard page after the
   name and against one before it, so a read past a name's end or before
   its start faults; names handed back by the enumeration as characters
   and a length with no NUL, each ended at its length;
   the screening answers (shown, withheld, not screened), a user name in
   the code page and in UTF-16 at either alignment in binary data, a name
   in a value's name, the windows' edges on texts up to 65536 characters
   and through a key; quotes doubled; and the walk over a fake tree: a
   composite device with a nested function and HID devnodes below it, the
   depth and devnode limits, a hub by flag and by id, devnodes not all
   read (an error and the tool's own mark), a block and a line that fail,
   and every devnode's key listed through the walk. Beyond the
   vectors, the bounds of the 256-value, 64 KB and 32-devnode limits are
   not tested past the cases named. The Windows half is
   `xhcitr\tr_regw.c`: the open, the enumeration callback and the UTF-16
   conversion. `xhcitr.c` adds the report's walk and blocks and
   `/nokeys`; `tr_snap.c` adds the `.DEV`'s walk and section and
   `-nokeys`.

8. **The user documentation.** `xhcitr\README.TXT` says what Save Report
   and the snapshot's device report now contain, that every value is
   listed by name, type and size and its data unless withheld, that the
   name test is a test by name alone, and that the report can still hold
   private data (a draft for the owner); `docs\contributing\snapshot-reader.md`
   describes the `.DEV`'s new section, its walk, its size and its privacy
   rule.

## 26. A controller stopped after repeated failures, and restarting a controller (roadmap-usb4 tasks 50.30 to 50.32, owner 2026-11-17)

Design before code, 2026-11-17. The driver's half is design record 17
section 4.15: a device that stops answering fails alone (task 50.30),
every reset request records its reason, its device's place and its slot,
and a controller that has latched failed is tried again (task 50.31). The
field report read for it (record 17 section 4.15.1) had a controller
latch and every device on it lost while the tool showed the controller as
started with empty ports, and no path in the tool restarted it (the later
saves follow restarts made outside the tool). This section is what the tool shows of a
stopped controller and the place involved, and task 50.32's restart (owner,
2026-11-17: "by Windows' own restart of the device, as Device Manager's
disable and enable", not a driver request).

1. **The reading.** The query request stays at version 3: its readers in
   every installed tool (`tr_xhq_parse`, `tr_xhq_driver`) accept versions
   1 to 3 and read a header-sized block first. The driver's new
   recovery-status request (record 13 section 8.12; record 17 section
   4.15.8), beside the holds request, answers at every logging level a
   bounded block: the terminal value (1 a recovery owed, 2 three failed
   in a row, 3 the window refused, 4 and 5 contained, 6 the lost
   message), the ring of the last four reset records (reason, extra,
   slot, root port, route string, VID:PID where read, how long ago in
   seconds, about), how many of the window's records involved the same
   place, `XhciDeviceIsolation` (value, listed, applies) and task 50.31's
   state (the value, the try number, the seconds to the next try, about,
   or none). The tool reads it with the query's header at each refresh,
   on the same thread and door gate, and in the report. A driver before
   it answers `STATUS_INVALID_DEVICE_REQUEST`: the tool then shows what
   `XHQ_F_FAILED` alone allows (item 2's first line, no detail) and offers
   no restart of a failed controller (item 9). An older tool with this
   driver sees the query as before. Pure readers in `tr_core.c` with host
   vectors.
2. **The controller's line and details.** Drafts for the owner. The
   texts step 1 built (`xhcitr\tr_log.c`, as at `407ec3258`: the tree
   line's suffixes, the status lines, the "involved" lines and the line
   naming no device, the restart line "Restart it from Device Manager,
   or restart Windows.", the place and name forms and the reasons in
   words) are approved as drafted (owner, 2026-11-17); the texts of this
   item that step 1 did not build stay drafts. A
   controller whose terminal is 2 or 3 has its tree line end " (stopped
   after repeated failures)" and its details, after its identity:
   "Status: Stopped after repeated failures. Its devices were removed."
   Then the last record: "Recovery requests involved: the device at port
   3 (USB Audio Device, 1234:5678); it did not answer." - the place in the
   tool's own words ("port 3", "port 2 of the hub at port 1", from the
   root port and the route string), the name found for the VID:PID among
   the devices Windows has recorded (`DeviceDesc` under the
   `USB\VID_vvvv&PID_pppp` instance keys, read only, both registry
   layouts) or the ids alone, and the reason in words (a table of record
   17 section 4.15.8's reasons). Where several of the window's records
   name the same place: "3 of the last 4 recovery requests involved this
   port." A record naming no device: "The driver did not record which
   device was involved." Then task 50.31's state: "The driver may try
   again automatically; next attempt in about 20 seconds." (with, where
   the controller has root ports, "A device unplugged from or plugged
   into one of this controller's own ports, not a hub's, can bring it
   sooner."), or at `XhciFailedRetrySec` 0: "Automatic retries are off:
   it stays stopped until it is restarted." Last: "To restart it,
   right-click the controller and choose Restart Controller." A contained
   controller (terminals 4 and 5) shows " (closed off)" and item 9's
   text, never a retry line; the lost message (terminal 6) shows "
   (stopped: interrupts lost)" with the snapshot's existing explanation
   and no retry line. The tray's red icon and its tooltip (section 7.2.1)
   are unchanged. Save Report and `/report` carry the details.
3. **A held place's reason 4.** Section 24's notes gain hold 4 (record 17
   section 4.15.4: the device stopped answering): at the port, " (on
   hold)" as for hold 1, and the note "Note: This port is on hold because
   the device on it stopped answering. Unplug the device, leave it
   unplugged for a few seconds, and plug it in again; the hold also ends
   when the controller restarts. Programs that were using the device may
   need to open it again."; at a
   controller's details for a hub's port: "A port of a hub on this
   controller is on hold because the device on it stopped answering.
   ..."; the balloon, where the hold notice is on (section 24 item 5):
   "Port N of <controller> is on hold: the device on it stopped answering.
   Unplug the device and plug it in again." Drafts for the owner. Hold 4
   is powered, so it is announced and released as hold 1 is.
4. **The snapshot text.** `tr_snap.c`'s terminal line (`tol_terminal_text`,
   `print_tol`) keeps its texts and replaces "(it stays failed until a
   stop and start)" with "(next attempt in about N s, try K)" under task 50.31, and keeps it at
   `XhciFailedRetrySec` 0 and for the terminals never tried. After it, a
   line per reset record: "reset requested: <reason> at root port P route
   R slot S [vvvv:pppp], about T s ago", the latch's record marked, and the
   reason counters. `snaptest.cmd` gains a fixture of each.
5. **Where the action appears.** "Restart Controller..." on a controller's
   right-click menu and on the Action menu while a controller is
   selected, for every xhci98 controller the tool lists; for a stopped
   one (terminal 2 or 3) also as the last line of its details (item 2)
   and on the tray's red-icon menu after its existing item ("Restart
   <controller>..."). Not offered for another driver's controller, for a
   Thunderbolt NHI (`xhcicm.sys` has its own life) or where item 9
   refuses it.
6. **The confirmation.** Before anything is done the tool asks, listing
   what will disconnect, from its own model of the controller's subtree
   (every device under it, by the names the tree shows; a drive with its
   letters; hubs counted, not listed): "Restart <controller>? These
   devices will disconnect and connect again: <list>. A drive's open files
   may be lost: close them first. A keyboard or mouse on this controller
   stops for a few seconds." Buttons "Restart" and "Cancel" (Cancel the
   default). For a stopped controller, whose devices are already gone, the
   list is replaced by "Its devices were removed when it stopped." Drafts
   for the owner.
7. **The calls, 2000 and later** (both executables; `XHCITR64.EXE` on x64
   and, through the WOW64 hand-over of section 9, for `XHCITR.EXE` there):
   the controller's device information element from its instance id
   (`SetupDiCreateDeviceInfoList` and `SetupDiOpenDeviceInfoA`);
   `SetupDiSetClassInstallParamsA` with `SP_PROPCHANGE_PARAMS`
   (`DIF_PROPERTYCHANGE`, `StateChange` `DICS_PROPCHANGE`, `Scope`
   `DICS_FLAG_GLOBAL`, `HwProfile` 0), then
   `SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, ...)`, then
   `SetupDiGetDeviceInstallParamsA`. This is the restart Microsoft
   documents for `DICS_PROPCHANGE` and the form the Thunderbolt mark
   service already sends (`xhcimark.c`, record 21 section 7.4). The new
   routines are looked up at run time through `load_routines`, so the
   import allowlists gain no row (section 9). A disable then an enable was
   rejected: ended between the two, the tool would leave the controller
   disabled, with every USB device on it, and a config-specific disable is
   stored; `DICS_PROPCHANGE` stores nothing.
8. **The calls, 98 SE and ME** (owner, 2026-11-17). A remove and rescan:
   `CM_Query_Remove_SubTree` on the controller's devnode with
   `CM_QUERY_REMOVE_UI_NOT_OK`, as the eject's 9x path sends it (section
   6.2 step 6), so a refusal is known before anything stops; then
   `CM_Remove_SubTree`; then `CM_Reenumerate_DevNode` on the controller's
   parent (the PCI bridge), which finds the controller again and loads its
   driver, the remove and rescan the 98 SE and ME legs have read from
   Device Manager since `2.3.0.0`. Whether that sequence works from a Win32
   program through `cfgmgr32.dll` on both 98 SE and ME is **unread**: the
   first step of the implementation is that reading in both guests. If it
   works on both, the action is offered there; if not, it is not offered
   on 98 SE and ME at all (the menu item absent, the details' last line
   saying "Restart it in Device Manager, or restart Windows."). A removal
   that succeeds and a rescan that fails leave the controller absent until
   the next hardware scan or restart; the tool says so: "The controller
   was removed but Windows did not find it again. Use Refresh in Device
   Manager, or restart Windows." No disable is used on 9x, so none can be
   left behind.
9. **Refused for a contained controller.** A controller whose terminal is
   4 or 5 is not restarted: record 17 section 4.6's known limitation (a
   stop on the pinned branch can wait for ever in `HcdDmaMapDrain`) means
   a restart could stop Windows answering. The menu item is shown
   disabled and the details say, draft: "This controller was closed off
   because it stopped answering. Restarting it could stop Windows
   responding: save your work and restart Windows." (Step 1 built this
   text as a status line: approved as drafted, owner, 2026-11-17.) The
   same refusal
   where the driver does not answer the recovery-status request and the
   controller reads failed: the tool cannot tell latched from contained
   there. The terminal is read again immediately before the call, by the
   process that makes it - the elevated one on Vista and later reads it
   itself - since the menu's state can be stale; contained by then, the
   call is not made.
10. **Rights and elevation.** As the eject (section 6.4, 6.6): on 2000
    and XP a user who is not an administrator gets "Restarting a
    controller needs an administrator." and nothing is done (the call
    fails with `ERROR_ACCESS_DENIED`, read after the call, nothing having
    changed); on Vista and later the tool restarts itself elevated
    (`elev_launch_req`, `ShellExecuteEx` "runas") with `/restart
    "id=<instance id>" /result <section> /confirmed <digest>`, the
    confirmation having been given in the unelevated window and its
    digest checked by the elevated one as the eject's is, and waits for
    the answer in the named section up to `TR_ELEV_WAIT_MS`. The manifest
    stays `asInvoker`. 98 SE and ME need no rights.
11. **What Windows answers, and what the driver says.** "Windows
    completed the restart request" and "the driver recovered the
    controller" are two different facts, and the tool reports each only
    from its own evidence: a latched controller can already look started
    to Windows, which is this task's original problem. On 2000 and later:
    a failed call (any `SetupDiCallClassInstaller` or parameter call
    returning `FALSE`) is reported with its error and is never shown as a
    restart (`ERROR_ACCESS_DENIED` as item 10). After a successful call
    the flags returned are kept, and before anything is claimed the tool
    reads both the controller's devnode (`CM_Get_DevNode_Status`: started,
    its problem code) and the driver's recovery-status request again
    (item 1), waiting up to a few seconds (`TR_RESTART_SETTLE_MS`, a
    bounded poll) for the driver's start to answer. The answers, drafts
    for the owner:
    - started, no problem, no flag, status read and not failed: "Windows
      restarted <controller>. It is running."
    - started, no problem, no flag, but status read and still failed
      (terminal 1 to 3): "Windows completed the restart request, but
      <controller> is still stopped after repeated failures." followed by
      item 2's details as now read; never "restarted".
    - status read as contained (4 or 5) or interrupts lost (6): item 2's
      text for that terminal, after "Windows completed the restart
      request, but".
    - status unavailable (a driver before the request, or the request
      failing): "Windows completed the restart request. The tool could
      not read from the driver whether <controller> is working." followed
      by the devnode's state as read ("Windows shows it running." or "It
      is stopped (problem N).").
    - `DI_NEEDREBOOT` or `DI_NEEDRESTART`: "Windows reports that a system
      restart is required to finish restarting <controller>." followed by
      the state as read by the same two readings; these flags are not
      taken for a refusal, nor for the controller having been left
      running.
    - the devnode not started, or with a problem code: "Windows completed
      the restart request, but <controller> did not start (problem N)."
    On 98 SE and ME (where item 8's reading lets the action be offered)
    the same two readings follow a rescan that found the controller, with
    the same texts; a refused query-remove gives the eject's 9x answer
    text (`tr_usb_answer9x`, "in use ... DOS box") and nothing else is
    done. No second attempt, no forced fallback after a refusal, no prompt
    to restart Windows. A time-out of the elevated process is shown as "No
    answer from the elevated tool; the controller's state is:" with the
    two readings.
12. **After a restart.** The restart is a stop and a start of the
    controller: the driver's start clears the latch, the window, the
    holds of every place (record 17 section 4.5, "a controller start") and
    task 50.31's backing off, and enumerates what is plugged in. A device
    still failing is then failed alone and its place held again where
    `XhciDeviceIsolation` applies and its abort and teardown end cleanly
    (record 17 section 4.15.4); otherwise, or where its abort fails, the
    controller can latch again. The tool refreshes when Windows
    reports the device change, as now; the stopped line is gone.
13. **The user documentation.** `xhcitr/README.TXT` gains the action and
    the stopped controller's line; `docs/using/` (the USB Tree's page)
    the same, no testing status. The release notes' known limitations
    gain a line, draft for the owner: "If every device on one USB
    controller disappears, open the xHCI98 USB Tree: it shows the
    controller as stopped after repeated failures and, where the driver
    recorded it, the port and device involved. Unplug that device and leave
    it unplugged for a few seconds. The driver may try the controller again
    by itself (the Tree shows the next attempt); it does not when automatic
    retries are off (`XhciFailedRetrySec` 0), and unplugging a device
    behind a hub does not bring an attempt sooner. Otherwise restart the
    controller from the USB Tree (right-click it, Restart Controller) or
    in Device Manager (disable and enable it), or restart Windows. A
    controller shown as closed off is never tried again and should not be
    restarted: save your work and restart Windows. Programs that were using
    a device may need to open it again. On Windows ME, a device that does
    not come back may need to be unplugged and plugged in again." The
    restart sentence is conditional on item 8's reading: if the action is
    not offered on 98 SE and ME, it reads instead "Otherwise, on Windows
    2000 and later, restart the controller from the USB Tree (right-click
    it, Restart Controller) or in Device Manager (disable and enable it);
    on Windows 98 SE and ME, restart Windows." The line is settled at the
    cut from T.24's result.
14. **Decisions superseded.** Section 22 item 6's "the Tree never
    disables a controller itself (the owner's choice D-1)" and T6's and
    section 6.5's "never ... the controller" stand for the eject and the
    settings window; the restart of this section is the owner's decision
    of 2026-11-17 (T30), a Windows restart of the device asked for in
    words and confirmed, with no disable on either family.
15. **Host vectors** (`test\test_xhq.c`, `test\test_tr.c`): the
    recovery-status block against the driver's layout (`test_xhq` holds them equal), every
    reason's words, the place's words from root port and route string,
    the texts word for word, the terminals each item shows or refuses,
    the elevated command line's parse and its digest, and the version 1
    to 3 query unchanged, the request refused by an older driver, and the
    devnode state's words after each answer of item 11.
16. **Legs.** T.21: on 98 SE, 2000, XP and 7 x86 (qemu flavour), a
    controller latched by record 17 section 4.15.13's leg (a) (code `05`
    four times inside the window) and by section 4.15.9's leg 13 (the
    field report emulated end to end): the line, the
    details with the place involved and the next attempt, the snapshot's
    lines, the
    tray's item; then Restart Controller: on 2000, XP and 7 as an
    administrator and as a standard user (7 through the elevation), on 98
    SE: the controller restarts and its devices come back. T.22: a
    healthy controller with a stick holding an open file (2000, 7) and on
    98 SE a DOS box on the stick: item 11's answer and state, and nothing
    else. T.23:
    a controller contained by injection `07`: the action disabled with its
    text. T.24: 98 SE and ME, the reading of item 8 first, then the
    restart. T.25: a hold of reason 4 (record 17 section 4.15.9's leg 1):
    the port's tag and note, the balloon with the notice on. T.26: this
    tool with a `3.5.0.0` driver (no recovery-status request) and a
    `3.5.0.0` tool with this driver (the query unchanged), on 98 SE and 7.

## 27. Restarting a controller: the calls, the answers and the legs (roadmap-usb4 task 50.32, owner 2026-11-17)

Design before code, revision 50 (2026-11-19): revisions 41 to 46 of
2026-11-18, revised for the owner's answers of that day, revision 47,
reviewed and revised, revision 48, the owner's decision of
2026-11-19 on the drive letters no devnode claims (item 7), reviewed and
revised, revision 49, the owner's two later decisions of that day -
the floppy letters no devnode claims (item 7) and C10 kept dropped -
reviewed and revised, and revision 50, the owner's decision of that day
on the answer after a restart Windows refuses in the session (items 2,
8 and 12, H1), reviewed and revised.
Task 50.32's line in `roadmap-usb4.md` leaves to its design the calls on
98 SE and ME and on 2000 and later, both executables, the user
documentation, and the release notes' line for a controller whose devices
are all gone. Section 26 drew the restart before code (items 5 to 11, 13
and 16); this section settles what it left open and replaces it where it
says so. The texts the owner approved on 2026-11-17 (section 26 item 2) do
not change. Every new user-visible string here is a draft for the owner,
with the id of its entry in item 12, except H1, whose wording the owner
accepted on 2026-11-19 (revision 50). Where an owner's answer of 2026-11-18
decides a point, the place is marked "(owner, 2026-11-18)"; the owner's
decisions of 2026-11-19, given through the coordinator, are marked
"(owner, 2026-11-19)".

Tags: **[hdr]** read in a kit header this repository builds against or
holds (the Windows 2000 DDK's `tools\ntddk\inc`, MSVC 6.0's
`tools\MSVC600\VC98\Include`, WDK 7.1's `tools\WinDDK71\inc\api`), names,
values and declarations, and the structure sizes held by a compile-time
check in each (nothing run); **[emu]** read in this project's guests on
the pinned emulator (`runs/run-50.md`, "50.32"); **[rec]** recorded
earlier in this repository, cited where it is; **[src]** read in this
repository's sources at this revision.

**Two stages.** The section is built in two stages, and stage 1 is
complete in itself:
- **Stage 1, 2000 and later:** items 1, 2 and 4 (with the driver's
  lifetime words), item 5 but its check 6, items 6 (the confirmation
  list), 8 (the four-part answer, its NT rows), 9 (elevation and Run As),
  10 to 14 for 2000 and later, and legs T.28 to T.34's NT rows.
- **Stage 2, 98 SE and ME:** items 3 and 7, item 5's check 6, item 8's 9x
  rows, the 9x vectors of item 14, and legs T.27 and T.32 to T.34's 98 SE
  rows.

Stage 1 may ship with the 98 SE and ME action not offered: until stage 2 is
cleared and its legs pass, `XHCITR.EXE` on 98 SE and ME offers no M1, M2 or
M3, every controller there keeps the approved "Restart it from Device
Manager, or restart Windows." where step 1 built it, `/restart` there
answers R3, and the user documentation carries item 13's stage-1 text.
Nothing of stage 2 is compiled into a build that ships stage 1 alone.

1. **What is being built.** An action on each xhci98 controller, offered
   also on the line of one stopped after repeated failures with the place
   and device involved shown; asked first, with every device that will
   disconnect listed; where Windows asks for a restart of the computer,
   the tool says so and does no more; administrator rights on 2000 and
   later, asked for on Vista and later as the eject asks, and on 2000 and
   XP offered through Run As (item 9) (roadmap-usb4 task 50.32; decision
   T30, section 12). On 2000 and later the action is Windows' single
   stop-and-restart request for the device, `DIF_PROPERTYCHANGE` with
   `DICS_PROPCHANGE`, accepted in place of a literal disable followed by an
   enable (owner, 2026-11-18; it supersedes the description of
   the restart "as Device Manager's disable and enable" in section 26's
   opening paragraph and item 7). It is not the same operation as Device
   Manager's Disable and Enable, which are two requests (`DICS_DISABLE`,
   `DICS_ENABLE`) and store a disabled state between them, and the user
   documentation does not say that it is. On 98 SE and ME it is a
   query-remove, a removal and a rescan of the parent, under item 5's
   checks.

2. **The calls, 2000 and later.** One sequence, in both executables. Every
   routine is resolved before anything is called; one missing makes the
   action unavailable (R3), never a partial sequence.
   1. The baseline (item 4) and item 5's checks, by the process that makes
      the call (on Vista and later the elevated one, on 2000 and XP the
      Run As one), immediately before it.
   2. `SetupDiCreateDeviceInfoList(NULL, NULL)`; then
      `SetupDiOpenDeviceInfoA(set, id, NULL, 0, &data)` with
      `data.cbSize = sizeof(SP_DEVINFO_DATA)`. Either failing: no call is
      made (A18; `ERROR_NO_SUCH_DEVINST` from the open is R10).
   3. `SetupDiGetDeviceInstallParamsA(set, &data, &ip)` with
      `ip.cbSize = sizeof(SP_DEVINSTALL_PARAMS_A)`. Its `DI_NEEDREBOOT` and
      `DI_NEEDRESTART` are kept as the earlier requirement `P`; a failed
      read makes `P` unknown and the sequence goes on. Where either is set,
      both are cleared through `SetupDiSetDeviceInstallParamsA` with the
      same structure, so that the read after the call is this call's; no
      other flag is changed. `P` is kept whatever follows and is reported
      (item 8, line 4). A failed clear is recorded: the later flags are then
      reported as read and as possibly earlier.
   4. `SetupDiSetClassInstallParamsA(set, &data, &p.ClassInstallHeader,
      sizeof(SP_PROPCHANGE_PARAMS))` with `p.ClassInstallHeader.cbSize =
      sizeof(SP_CLASSINSTALL_HEADER)`, `InstallFunction =
      DIF_PROPERTYCHANGE` (`0x12`), `StateChange = DICS_PROPCHANGE`
      (`0x3`), `Scope = DICS_FLAG_GLOBAL` (`0x1`), `HwProfile = 0`.
      Failing: no call is made (A18).
   5. `SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, set, &data)`;
      `FALSE` is read with `GetLastError` at once.
   6. `SetupDiGetDeviceInstallParamsA` again with a fresh `cbSize`: its
      `Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)` (`0x100`, `0x80`) are the
      call's requirement `F`; **a failed read makes `F` unknown, never
      absent**. It is read after a `FALSE` call too.
   7. `SetupDiDestroyDeviceInfoList` on every path that created the list;
      then the settle and the answer (items 4 and 8).

   **The seven SetupAPI routines** - `SetupDiCreateDeviceInfoList`,
   `SetupDiOpenDeviceInfoA`, `SetupDiGetDeviceInstallParamsA`,
   `SetupDiSetDeviceInstallParamsA`, `SetupDiSetClassInstallParamsA`,
   `SetupDiCallClassInstaller` and `SetupDiDestroyDeviceInfoList` - and
   `SP_DEVINFO_DATA`, `SP_DEVINSTALL_PARAMS_A`, `SP_CLASSINSTALL_HEADER`,
   `SP_PROPCHANGE_PARAMS`, `DIF_PROPERTYCHANGE`, `DICS_PROPCHANGE`,
   `DICS_FLAG_GLOBAL`, `DI_NEEDREBOOT` and `DI_NEEDRESTART` are declared,
   with the same values, in all three `SETUPAPI.H` [hdr]: the Windows 2000
   DDK's, MSVC 6.0's (what `XHCITR.EXE` is built with) and WDK 7.1's
   `inc\api` (what `XHCITR64.EXE` is built with). The x86 headers pack to
   one byte (`pshpack1.h`), WDK 7.1's to eight under `_WIN64`; the sizes,
   held by a compile-time check in each kit, are x86 and x64:
   `SP_DEVINFO_DATA` 28 and 32, `SP_DEVINSTALL_PARAMS_A` 296 and 320,
   `SP_CLASSINSTALL_HEADER` 8 and 8, `SP_PROPCHANGE_PARAMS` 20 and 20. The
   tool uses the headers' structures and `sizeof` in both builds, as
   `xhcimark.c` does, never a copy or a literal size. `cfgmgr32.h` is in
   the 2000 DDK and WDK 7.1 and not in MSVC 6.0, so the configuration
   manager's values stay the tool's own defines, checked against both:
   `CR_SUCCESS` 0, `CR_INVALID_DEVNODE` `0x5`, `CR_NO_SUCH_DEVNODE` `0xD`,
   `CR_REMOVE_VETOED` `0x17`, `CR_ACCESS_DENIED` `0x33`,
   `CM_QUERY_REMOVE_UI_NOT_OK` and `CM_REMOVE_UI_NOT_OK` `0x1`,
   `CM_REENUMERATE_NORMAL` 0, `DN_STARTED` `0x8`, `DN_NEED_RESTART`
   `0x100`, `CM_DRP_CLASS` `0x8`. `ERROR_IN_WOW64` (`0xE0000235`) is in WDK
   7.1's `SETUPAPI.H` alone and is a define of the tool's own as well.
   Each routine is looked up by `GetProcAddress` in `load_routines`, so the
   static imports and both architectures' rows of `xhcitr-imports.allow`
   are unchanged (section 9). The routines exist on 2000 and every later
   target: `XHCIINST` resolves `SetupDiSetClassInstallParamsA`,
   `SetupDiCallClassInstaller`, `SetupDiGetDeviceInstallParamsA` and
   `SetupDiSetDeviceInstallParamsA` and the mark service the rest on 2000
   to 10 [rec]. 2000 has no x64 edition among the targets; XP x64 is the
   first x64 case. `SetupDiRestartDevices` is not used: WDK 7.1 declares it
   only for `_SETUPAPI_VER >= _WIN32_WINNT_WS03` and the other two kits do
   not declare it [hdr]. The 9x routines of item 3 (`CM_Remove_SubTree`
   above all, which WDK 7.1's `cfgmgr32.h` marks obsolete in favour of
   `CM_Query_And_Remove_SubTree` [hdr]) are never called on NT.

   On 2000 and later the stop is a query-remove, and the refusals of a
   volume with open files or of a paging, dump or hibernation path are the
   participating drivers' (record 21 section 7.4 [rec]); the controller's
   own handler passes the query on and proves none of them [src]. The tool
   relies on Windows for them and claims nothing about why Windows refused
   or asked for a restart, with one exception (owner, 2026-11-19, given
   through the coordinator; revision 50): where Windows accepted the
   request, no new start was seen and Windows asks for a restart of the
   computer or has marked the controller to be finished at the next
   restart - A1 with S3, and W1 or W5 - the answer ends with H1 (item 8),
   which offers a program using one of the controller's drives as a
   possibility, never as the cause, and gives the two next steps. The
   reading behind it [emu]: with an Explorer window open on the
   controller's stick, Vista refused the stop in the session - a
   query-remove, then a cancel-remove, no new start - while the call
   returned `TRUE` and the class installer's flags and the devnode's mark
   were set; with the window closed the same request restarted the
   controller (`runs/run-50.md`, "50.32 stage 1 on Vista x64, and a
   restart Windows refuses in the session").

   **Under WOW64.** `XHCITR.EXE` on 64-bit Windows hands its window and its
   one-shot modes to `XHCITR64.EXE` (section 9): `/restart` joins both the
   hand-over's dispatch and its list of waited one-shot modes, so its exit
   code comes back. Where `XHCITR64.EXE` is absent and the 32-bit tool runs
   itself, the action is shown disabled with R4: SetupAPI refuses a 32-bit
   process's device installation request on 64-bit Windows
   (`ERROR_IN_WOW64` [hdr]), and the tool does not try. The elevated run
   starts the running executable by its own path (`GetModuleFileNameA`),
   so on 64-bit Windows it is `XHCITR64.EXE`; a file gone by then fails the
   start (A16).

3. **The calls, 98 SE and ME, and the reading** (stage 2). `XHCITR.EXE` calls the
   32-bit configuration manager, `cfgmgr32.dll`, which 98 SE and ME
   provide over their configuration manager; it carries no 16-bit code and
   does not use the 16-bit `setupx` device installer (record 27's helper's
   route, whose disable writes the configuration flags). The sequence,
   after item 5's checks:
   1. The baseline (item 4); `CM_Get_Parent` read and kept, since the
      controller's devnode handle is not valid once it is removed
      (`CR_INVALID_DEVNODE` read on both [emu]). `CM_Get_Parent` failing
      ends the sequence before the query: nothing is removed, and no call
      is made through a handle that was not returned (A21).
   2. `CM_Query_Remove_SubTree(dn, CM_QUERY_REMOVE_UI_NOT_OK)`. Any answer
      but `CR_SUCCESS` ends the sequence: nothing is removed, and **no
      removal ever follows a refused query**.
   3. Accepted: `CM_Remove_SubTree(dn, CM_REMOVE_UI_NOT_OK)`, as the
      eject's 9x path does (record 24 section 7.1). A removal that does
      not answer `CR_SUCCESS` (A12) is followed by step 4's rescan of the
      parent alone, once, which finds again anything the failed removal
      took away; no root rescan follows it.
   4. `CM_Reenumerate_DevNode(parent, CM_REENUMERATE_NORMAL)`; its answer
      is kept. It returns at once and the controller appears later [emu],
      so the settle follows (item 4). Where the controller is not found by
      the settle's end, or the parent's rescan did not answer
      `CR_SUCCESS`, one `CM_Reenumerate_DevNode` of the root devnode
      (`CM_Locate_DevNodeA(NULL)`, its answer kept too), the settle again,
      then the answer. A root locate that fails makes no second rescan (no
      call through a handle that was not returned): A22, then the answer
      from what the first settle found. Both are rescans; nothing is forced
      or removed again.

   The removal deletes nothing from the registry, so Windows finds the
   controller again under its instance key with its driver; Device
   Manager's Remove deletes that key, and its Refresh then runs the Add New
   Hardware wizard (`runs/run-50.md`, "50.30 to 50.32 steps 1 and 2", L1).

   **The reading** (`runs/run-50.md`, "50.32"; emulation, single
   readings; a healthy controller). On 98 SE and on ME, each with the
   `3.5.0.0` driver, a tablet and a Bulk-Only stick, a throwaway test
   program found every routine above in `cfgmgr32.dll`, as record 24 A.1's
   names say. Five removals on 98 SE and three on ME were each followed by
   the controller's return with all seven devnodes of its subtree started,
   1.5 to 3.4 seconds after the rescan, with no dialog or wizard on the
   screen and the stick and the tablet working after. A program's file
   held open for writing on the stick was refused on both
   (`CR_REMOVE_VETOED`). On 98 SE a DOS box whose current drive was the
   stick, and a file that DOS box held open, did not refuse it (one
   reading; that the file was whole after says nothing of a write in
   flight); ME's DOS box leg did not run. So the remove and rescan works
   on both, as the roadmap's condition asks, and Windows' refusal of a
   drive in use cannot be relied on there: the action is therefore
   refused on 98 SE and ME while any USB storage device is on the
   controller (item 7; owner, 2026-11-18). **A controller
   stopped after repeated failures** (terminal 2 or 3) is not offered the
   action on 98 SE and ME (owner, 2026-11-18): the driver has removed its
   devices and it answers no port reading, so the tool cannot see what is
   plugged into it, and a model the tool took before the stop can be
   stale - a drive plugged in and mounted after it and before the stop is
   not in it. Such a controller there shows the approved "Restart it from
   Device Manager, or restart Windows."; M1, M2 and M3 are not offered
   for it. What the user has instead: task 50.31's automatic try of the
   controller (the details' next-try line), unplugging the device involved,
   Device Manager, or a restart of the computer. Nothing of this case is
   built on 98 SE and ME.

4. **What the tool can say about a start.** Neither family's answer to the
   call says that the controller started again; a latched controller
   already reads started to Windows. The tool keeps four facts apart:
   **the request accepted** (item 2's call `TRUE`; on 9x the query and the
   removal `CR_SUCCESS`), **a start attempted**, **a start completed**, and
   **the state now**. Its evidence of a start is positive only: it can see
   that a new start completed, and nothing it reads proves that none did.

   The driver's tolerance clock (`XHR_O_CLOCK`) is not used. It is zeroed
   at the start of `hcdStartBody`, before its fallible work, so it shows an
   attempt, not a completion; a baseline can be zero or small; an answer
   with `XHR_F_STARTED` clear carries a zero that is not a running clock;
   the clock counts timer callbacks, which can run late, so it cannot be
   compared with elapsed time; and it attributes nothing [src]. No other
   value the driver serves today numbers its starts: `CountersStart`, the
   image-wide number `HcdCountersStart` draws (`hcd_log.c`), is written
   only to the qemu flavour's trace [src].

   **The driver addition** (owner, 2026-11-18). The
   recovery-status request (record 13 section 8.12, `src\xhci_qry.h`
   `XHR_*`) gains a lifetime part after its counts, flagged
   `XHR_F_LIFETIME` (`0x10`; `0x08` is record 38's `XHR_F_RETRY_NEVER`,
   section 15.1), its version staying 1. The answer's layout
   with the two other additions of Phase 50, which take words of the
   header the request already reserves:

   | Part | Bytes | Flag | Words |
   |---|---|---|---|
   | header | 0 to 63 | | as now |
   | isolation (task 50.30) | 32, 36, 40 | `XHR_F_ISOLATION` `0x02` | value, listed, applies |
   | retry (task 50.31, record 38 section 15.1) | 44, 48, 52 | `XHR_F_RETRY` `0x04` | value, try, next seconds |
   | records | 64 to 207 | | as now |
   | counts | 208 to 311 | | as now |
   | lifetime (this task) | 312, 316, 320 | `XHR_F_LIFETIME` `0x10` | image time low, image time high, start number |

   The words: the system time `KeQuerySystemTime` returned at the image's
   `DriverEntry`, its two words as returned (no arithmetic; the import is
   already a row of `xhci98-imports.allow`, and inline on amd64), kept in a
   variable of the image; and `CountersStart`, kept in the controller's FDO
   extension outside `Hc`, the number of the start this FDO last began - 0
   when none has begun on this FDO. The start counted is the one that
   reaches `HcdCountersStart` in `hcdStartBody`: a `START_DEVICE` whose
   forwarding to the lower drivers succeeded (`hcd_pnp.c`) and that entered
   the start body; nothing between the body's entry and the draw can fail
   [src]. A `START_DEVICE` the lower drivers fail draws no number.

   The buffer tiers, every one defined:

   | Output buffer | Answer |
   |---|---|
   | below 64 bytes | `STATUS_BUFFER_TOO_SMALL`, nothing |
   | 64 to 311 | the header alone, `XHR_O_BYTES` 324, `XHR_F_LIFETIME` clear |
   | 312 to 323 | the earlier whole answer, `XHR_O_BYTES` 312, `XHR_F_LIFETIME` clear |
   | 324 and above | the whole answer, `XHR_O_BYTES` 324, `XHR_F_LIFETIME` set |

   `XHR_F_LIFETIME` is set only in an answer that carries the tail. The
   installed readers ask with 312 bytes and accept a larger `XHR_O_BYTES`
   (`tr_log.c` `tr_xhr_parse`), so they keep their whole answer. The new
   parser **keeps the 312-byte minimum** for the answer and for
   `XHR_O_BYTES`, so an older driver still parses, and reads the lifetime
   words only when the flag is set, at least 324 bytes came back and
   `XHR_O_BYTES` is at least 324; a flag without them makes that answer an
   invalid lifetime sample. It asks with 324 bytes or more.

   **Synchronization.** The lifetime words are copied with the flags inside
   the door gate (`HcdDoorGateEnter`), which `HcdStartController` holds
   around the whole start and the door holds around every stop, so an
   answer never sees a start half made or a stop half done [src]: it sees
   the number and `XHR_F_STARTED` of one moment between them. The image
   time is the image's and the number lives outside `Hc`, so neither needs
   the extension to be initialized; the part is served whether or not the
   controller is started. `XHR_F_STARTED` keeps its meaning (the
   controller's start completed and it has not stopped since). The
   records, counts and terminal stay as they are, filled only for a
   started controller.

   **The identity and what it shows.** A start's identity is the triple
   (image time low, image time high, start number); a *valid sample* is an
   answer with the tail and a start number other than 0. Two valid samples
   with **different** identities belong to different starts: within one
   image a number is drawn once (the counter would have to pass all of its
   32 bits to repeat), and a different image time is a different load of
   the image. Two samples with **equal** identities do not show the same
   start: a reload can give the same time (the system time moves in ticks
   and can be set) and draw the same number, and a full pass of the
   counter is not excluded, only beyond any interval the tool waits. A
   number of 0 says that no start has begun on this FDO; a changed image
   time with a number of 0 is not an attempt.

   **The sampling rule.** The baseline `I0` is the last valid sample before
   the call. After it, every 500 ms of the settle, one sample. The tool
   keeps a history, never only the last reading: whether any valid sample
   differed from `I0`, whether any such sample had `XHR_F_STARTED` set,
   how many valid samples there were, and the first failure of each kind.
   **A start is seen completed** (S1) when a valid sample differing from
   `I0` has `XHR_F_STARTED` set; once seen, it stays seen whatever later
   samples read - a failed read, a stop, another start - and the state now
   (line 3) says what the controller is at the end. **A start is seen to
   have begun** (S2) when a valid sample differed from `I0` and none of
   those had `XHR_F_STARTED` set: the tool saw a new start and did not see
   it running, which may be a start that failed or one that ran and
   stopped between samples. **No new start was seen** (S3) when `I0` is
   valid and every valid sample equals it, with at least one valid sample:
   an observation of the samples, not a proof that none happened. **The
   tool cannot tell** (S4) when `I0` is not valid, or no sample after the
   call is, or the driver serves no lifetime part.

   On 98 SE and ME the removal is evidence of its own: the query and the
   removal answered `CR_SUCCESS`, the id was then not found, and a devnode
   of the same id found again later, started with no problem, is a new
   start that completed (S1), lifetime part or not; found again and never
   seen started is S2; not found again is A11. Where the id is still found
   when the removal has returned, and after A12, the removal is no
   evidence and line 2 comes from the lifetime samples alone, as on NT.
   On 98 SE and ME the driver's door is `HCD<n>`, found by its driver key
   as on NT. Nothing says *who* made a
   start: another restart of the same controller inside the settle reads
   the same, so the answer says that the controller started again, never
   that this request started it. Task 50.31's try of a failed controller
   resets it in place without `hcdStartBody` and leaves the identity as it
   is (record 38 section 7.4: a try is committed inside `hcdRecover`).

   **The settle.** After the call the tool polls every 500 ms, pumping the
   window's messages as the elevated wait does, for at most
   `TR_RESTART_SETTLE_MS` (30 s); it ends early once S1 is seen and the
   devnode reads started with no problem. It is not ended early by `F`
   or the mark read set (revision 50, a restart Windows refused in the
   session): neither shows that no start follows, and a start that comes
   late in the settle must still be seen (S1). The periodic refresh does
   not run during the call or the settle, and no handle of this process on the
   controller (`\\.\HCD<n>`, `\\.\XHCI98HC<k>`) stays open across the call.
   The operation and the rescan need no further input from the user from
   the confirmation on, so a keyboard or mouse that goes with the
   controller does not hold them up.

5. **Before the call: the checks.** Each refusal is a message box (from the
   tray, its balloon; on 98 SE its pop-up), and nothing is called. In this
   order:
   1. a routine missing: R3; WOW64 without `XHCITR64.EXE`: R4;
   2. a contained controller (terminal 4 or 5), or one read failed by a
      driver without the recovery-status request: R2, R8 (section 26
      item 9); on 98 SE and ME a terminal 2 or 3 controller, for which the
      action is not offered at all (item 3) - found so by a later check,
      the approved "Restart it from Device Manager, or restart Windows." is
      the answer;
   3. 2000 and XP without administrator rights (`ej_is_admin`): R1 with the
      Run As offer (item 9);
   4. Setup running (`set_setup_running`): R6;
   5. the fresh walk of the controller's subtree incomplete (item 6): R11;
   6. 98 SE and ME only (owner, 2026-11-18 and 2026-11-19): item 7's
      rules in their order - the drive rule (R13, then R5, then R18), the
      storage rule (R12, R16), the network rule (R15);
   7. the shared eject mutex (section 6.3), taken by the process that
      calls, not had within its 10 s: R7.

   **Immediately before the call**, after the mutex, the process that
   calls repeats checks 2, 5 and 6 in full - the walks, the descriptors,
   the letters and their claimants, the classification - rather than
   trusting the confirmation's: any refusal they now give is the answer,
   and any change of the subtree's digest is R9. On 98 SE and ME a drive
   letter on the controller and USB storage on it are both refusals, so
   the confirmation's list there never holds either, and what it shows is
   what the digest covers.

6. **The confirmation and its list** (section 26 item 6, settled). Built
   from a **fresh, complete walk** of the controller's subtree, never from
   the rows the tree shows nor from the eject's eligibility:
   `walk_from` below the controller's devnode, with every devnode's id,
   class (`CM_DRP_CLASS`), status and problem read. The walk is complete
   only when no devnode below the controller is marked `Incomplete` or
   `ReadOk` clear, the walk's `Bad` count did not rise inside it, and no
   bound (`TR_MAX_DEVS`, depth 32) was met; otherwise the action is
   refused with R11. The walk made immediately before the call is held to
   the same rule: two incomplete walks that match do not pass.

   **The groups.** Every devnode below the controller belongs to one line.
   The root hub has none of its own. A *device* is a devnode whose parent
   is the root hub or a hub's FDO devnode of this driver; its line holds
   it and every devnode below it that is not itself a device: a composite
   device's function devnodes, their HID collections and class devnodes,
   a disk and its volume devnodes. A hub is a device with its own line
   ("USB hub"), and each device behind it has its own line with its place
   ("port 2 of the hub at port 4", `tr_place_words`), so nothing nested is
   left out or folded into the hub. A line is the device's name as the
   tree shows it (`dn_name`), its place, its drive letters where its group
   has a disk (`Letters`), and its kinds where its group has them
   ("keyboard", "mouse", "storage"). The list is **complete and
   scrollable**: a read-only multi-line box in the confirmation dialog,
   every line in it, no cut (C2). The digest is `tr_res_digest` over the
   controller's instance id and the instance id of **every** devnode of the
   subtree, root hub included, sorted (upper-cased, as the digest does) so
   that walk order does not matter. A different digest at the walk before
   the call is R9: the window asks again with the new list; a one-shot run
   refuses.

   **A stopped controller.** C3 ("Its devices were removed when it
   stopped.") replaces the list only when the fresh walk is complete and
   finds nothing below the controller but the root hub; a terminal value
   alone is not an inventory. Otherwise its devnodes are listed as any
   others.

   **Keyboards and mice.** Where a group holds a devnode of class
   `Keyboard` or `Mouse`, C7 says that it stops and may not come back if
   the restart fails, and that the restart needs nothing more from the
   user once confirmed. C8 ("It is the only keyboard (or mouse) Windows
   has.") is shown only when a complete walk of the whole devnode tree
   (the completeness rule above, over every devnode) found no other
   started devnode of that class outside this controller; where that walk
   is not complete, C8a says that the tool could not tell. C4, on the files
   of a drive, follows on 2000 and later where a group has a disk. Buttons
   Restart and Cancel, Cancel the default; on Vista and later without an
   elevated token Restart carries the shield (`BCM_SETSHIELD`, as
   `elev_proc` does) and starts the elevated run; on 2000 and XP without
   administrator rights the confirmation follows R1's Run As offer and its
   Restart starts the Run As run (item 9).

7. **98 SE and ME: the drives Windows needs, storage, and the network**
   (stage 2; owner, 2026-11-18 and 2026-11-19). Each rule decides
   affirmatively or refuses; a reading that fails or cannot be completed
   is a refusal, never a pass. The rules are taken in this order, the
   first refusal being the answer: the drive rule, whose R13 concerns the
   whole computer and which nothing on the controller changes, then its
   R5 and R18; the storage rule; the network rule.

   **The drives Windows needs.** Rather than locate the active swap file,
   which 98 SE and ME do not report to a Win32 program and which can lie
   anywhere `SYSTEM.INI` once named before an edit, the rule covers every
   drive:
   1. `GetWindowsDirectoryA` and `GetSystemDirectoryA`: a return of 0 or of
      the buffer's size or more, or a path not of the form `X:\...` with a
      letter, is R13 in its form naming no letter (a path beginning `\\`,
      a network installation, is no failure: it counts as a network path
      for step 4).
   2. `GetLogicalDrives` (an answer of 0, which no running system gives,
      is R13), then `GetDriveTypeA` of each letter's root with
      `SetErrorMode(SEM_FAILCRITICALERRORS)`: fixed, removable, CD-ROM and
      RAM-disk letters are *local*, remote letters *network*; a letter the
      first reports that the second answers unknown or without a root is
      R13.
   3. Every local letter is read against the whole devnode tree, walked
      completely (item 6's rule over every devnode), with the
      `CurrentDriveLetterAssignment` of every devnode read from its `Enum`
      key - absent is a reading; a key that does not open, a failed read,
      or an incomplete walk is R13. The walk descends from the root, so a
      claimant's chain of parents is the walk's own. Then each letter is
      one of three:
      - **placed** - exactly one devnode claims it (owner, 2026-11-18).
        **A placed letter whose chain passes through this controller
        refuses, whatever the storage rule finds**: R5 where it is the
        Windows or the system directory's drive, R18 otherwise. A placed
        letter elsewhere passes.
      - **claimed twice or more**: R13.
      - **unclaimed** - no devnode claims it: R13, whatever is on the
        controller, an unclaimed floppy letter excepted (below) (owner,
        2026-11-19, given through the coordinator; it
        replaces the allowance of 2026-11-18 for such a letter, which the
        owner knowingly narrows). The guests' reading found the drives in
        MS-DOS compatibility mode unclaimed; what else an unclaimed letter
        is (a compressed volume, `SUBST`, a RAM drive, a network
        redirector's or a boot firmware's disk) the tool does not try to
        tell, and it does not try to show that such a drive does not
        depend on this controller: a drive the firmware serves from a USB
        device on this controller reads the same. No port of the
        controller is sampled and no table of passing devices is kept.
      - **a floppy letter unclaimed** - the one exception to the
        unclaimed letter's R13 (owner, 2026-11-19, a later decision
        given through the coordinator; revision 49): a letter **A: or
        B:** whose root step 2's `GetDriveTypeA` answers removable
        (`DRIVE_REMOVABLE`, 2) and that no devnode claims neither
        refuses nor passes anything: the rule goes on to the next
        letter. "Unclaimed" is told as for every letter above - no
        devnode's `CurrentDriveLetterAssignment` holds it, in a walk
        complete by item 6's rule - and the letters, their types and
        their claimants are read by the same calls and keys on 98 SE and
        on ME. Nothing else is excepted: an A: or B: of any other type
        (fixed, CD-ROM, RAM disk, or one step 2 cannot type) that no
        devnode claims is R13; a floppy letter claimed twice or more is
        R13; a floppy letter claimed once is placed and takes the placed
        letter's rule (R18 where its chain passes through this
        controller); and every fault that is no one letter's (below)
        still refuses whatever the floppy letters read. Why: with no
        floppy controller 98 SE still lists A:, a removable drive in
        MS-DOS compatibility mode that no devnode claims
        (`runs/run-50.md`, "50.32 stage 2"), so under revision 48 alone
        such a machine was refused whatever was plugged in.
        **The risk the owner accepted.** Such a letter may be a drive
        Windows reaches through the firmware, and the tool cannot tell
        which device the firmware would serve it from. Where that is a USB
        floppy drive, or another USB device, on the controller being
        restarted, the drive rule no longer stops the restart, and a
        program using that drive at the time can lose what it was
        writing or fail. The storage rule (below) still refuses every
        device on the controller that shows itself as USB storage, so a
        floppy drive of the mass-storage class there is refused with
        R12 whatever its letter; what now passes is a firmware-served
        A: or B: whose device the storage rule does not recognise as
        storage. The owner was told this risk and accepted it.
      Every letter is read before any refusal is chosen. R13 comes first:
      in its form naming no letter where the fault is no one letter's - a
      step 1 failure, `GetLogicalDrives` answering 0, an incomplete walk,
      or a devnode's `Enum` key or `CurrentDriveLetterAssignment` that
      could not be read, any of which leaves every letter's reading unsure
      - and otherwise naming the first letter, in alphabetical order, that
      step 2 could not type or that is unclaimed (an unclaimed floppy
      letter excepted) or claimed twice. Then R5,
      naming the Windows or the system directory's letter; then R18, the
      first other placed letter on the controller.
   4. **The network.** A devnode of class `Net` in the controller's
      subtree while any network letter exists or step 1 found a network
      path: R15. (A controller stopped after repeated failures, whose
      earlier network adapters this rule could not see, is not offered the
      action on 98 SE and ME at all, item 3.) It is checked after the
      storage rule.

   **Storage on the controller** (owner, 2026-11-18: no restart while
   USB storage is on the controller). For every device of item 6's
   groups, and for every connected port of the controller and of its hubs
   in a reading of its ports made with the walk (the root hub's and each
   hub's `USB_NODE_CONNECTION_INFORMATION`, which lists a device whose
   devnode has gone), the tool fetches the active configuration's
   descriptor through the existing on-demand fetch (the device descriptor
   is the connection answer's), before deciding and whatever the tree's
   view already holds. A device is **storage** when any of these holds:
   its device class or any interface class of its active configuration is
   08h; any devnode of its group has a hardware or compatible id with
   `Class_08` (the ids this driver gives a mass-storage device or
   function, so an unbound or failed function and an empty card reader
   count); any devnode of its group is of class `DiskDrive`, `CDROM`,
   `hdc` or `SCSIAdapter`, has an id beginning `USBSTOR\` or `SCSI\`, or
   the service `USBSTOR`, `xhciuas` or `USBNTMAP`. A composite device is
   storage when any of its functions is.
   - storage, whether its devnodes are present (bound, unbound, failed or
     empty) or all gone while its port still shows it connected (stopped
     with Safely Remove, say): R12, naming each, and asking for it to be
     unplugged. Revisions 44 to 47 allowed a device whose devnodes this
     process's own Safely Remove had taken away, on a record of that
     removal (C10); revision 48 dropped it, and it stays dropped by the
     owner's decision (owner, 2026-11-19, given through the
     coordinator): a stick this tool has already stopped with Safely
     Remove is unplugged like any other storage before the restart, so
     no such record is kept and no stopped storage device is listed;
   - a connected port whose configuration descriptor cannot be fetched, a
     root hub or hub whose ports cannot be read, or a port the tree's
     model of the controller does not hold (its reading bound met): R16.

   What is established, then: an unclaimed letter never passes, an
   unclaimed removable A: or B: excepted (with the risk above); a placed
   drive passes only off this controller; no USB storage device is on the
   controller, stopped or not; and a network letter passes only where no
   network adapter is on it. Every other case refuses.

   `SYSTEM.INI` is not read, so no profile routine is needed; every routine
   above is a row of `xhcitr-imports.allow` already.

   **What the guests show of the rule** (`runs/run-50.md`, "50.32 The drive
   letters and the devnodes that claim them"; emulation, a single reading
   each). On 98 SE and on ME every letter a protected-mode driver serves -
   the floppy, ME's IDE disk on the secondary channel, the USB stick - is
   claimed by exactly one devnode with a readable chain, and the whole-tree
   walk was complete on both. Every letter served in MS-DOS compatibility
   mode is claimed by none: the system disk C: on both (98 SE's on the
   AHCI controller through the firmware, ME's on the IDE primary channel),
   and 98 SE's AHCI transfer disk D:; Windows reports its paging in
   compatibility mode on both. The CD-ROM had no letter on either. Under
   step 3, on both images as they are launched the system disk's letter
   is unclaimed, so the action is refused there whatever is plugged in
   (R13). The legs of the restart itself (item 15) therefore run the
   images with the system disk on the PIIX IDE controller and none of the
   launcher's geometry hints, the arrangement under which 98 SE's own IDE
   driver served C: in Phase 49 (`runs/run-49.md`, the readiness of the
   UHCI round's vehicle and the vehicle of the standby choice), and record
   whether each system then places every letter. Stage 2's legs found
   every letter placed there with a floppy controller, and A: unclaimed
   without one on 98 SE; under revision 49 that A: no longer refuses.

8. **The answer.** Up to four lines, each from its own readings, so that
   every combination has an answer and no line says more than its readings
   show; on 2000 and later a fifth, the hint H1, follows them in one
   combination (below). Item 5's refusals end before any reading below
   and give only their own text, with W5 or W6 after it where the
   devnode's `DN_NEED_RESTART` reads set.

   **Line 1, the request.**

   | Read | Line |
   |---|---|
   | NT: the list's creation, the element's open or the class parameters failed (no call made) | A18 (R10 for `ERROR_NO_SUCH_DEVINST` from the open) |
   | NT: the call `FALSE`, `ERROR_ACCESS_DENIED` | R1 |
   | NT: the call `FALSE`, any other error | A9 |
   | NT: the call `TRUE` | A1 |
   | 9x: `CM_Get_Parent` failed (no query made) | A21 |
   | 9x: the query `CR_REMOVE_VETOED` | A10 |
   | 9x: the query any other answer | A19 |
   | 9x: the query accepted, the removal not `CR_SUCCESS` | A12, and after it A20 where the parent's rescan (item 3 step 3) returned other than `CR_SUCCESS` |
   | 9x: both accepted | A13, and after it A20 for each rescan that returned other than `CR_SUCCESS` and A22 where the root could not be located for the second |

   **Line 2, the start** (item 4): S1, S2, S3 or S4; on 98 SE and ME after
   A13, S1 or S2 by the removal's evidence, or A11 (and then no line 3),
   and where the removal is no evidence (item 4), S1 to S4 from the
   lifetime samples.
   Not made after R10, A10, A19, A21 or R1 from the call (nothing was done
   to the device); after A18, A9 and A12 it is made, since a failed request
   can still have changed the device.

   **Line 3, the state now** (fresh readings at the end: the devnode, then
   the recovery-status request).

   | Read | Line |
   |---|---|
   | `CM_Locate_DevNodeA` answers `CR_NO_SUCH_DEVNODE` (NT) | N1 |
   | the locate or the status read fails otherwise | N6 |
   | present, not started or with a problem | N2 |
   | started; the request fails, or its answer does not parse | N5 |
   | started; the answer has `XHR_F_STARTED` clear | N7 |
   | started; terminal 0 | N3 |
   | started; terminal 1 (a recovery owed) | N9 |
   | started; terminal 2 or 3 | N4, then section 26 item 2's details as now read |
   | started; terminal 4 or 5 | the approved closed-off status text |
   | started; terminal 6 | the approved interrupts-lost status text |
   | started; any other terminal | N8 |

   **Line 4, a restart of the computer** (each that applies, in this
   order; the owner's case: said, and nothing more is done).

   | Read | Line |
   |---|---|
   | NT: `F` set | W1 |
   | NT: `F` unknown (the read after the call failed) | W2 |
   | NT: `P` set (Windows asked before this request), whether or not its clear worked | W3 |
   | NT: `P` unknown (the read before the call failed) | W4 |
   | both: the devnode's `DN_NEED_RESTART` set at the end | W5 |
   | both: set at the baseline, whatever the end reads (an earlier mark is reported as earlier) | W6 |
   | both: the end's status not read and the baseline's not read either | W7 |
   | none of these | no line |

   **Line 5, the hint** (2000 and later only; owner, 2026-11-19, given
   through the coordinator; revision 50). H1 is the answer's last line,
   after every line of line 4, and is shown exactly where all three hold:
   line 1 is A1 (the call `TRUE`; 98 SE and ME never answer A1), line 2
   is S3, and line 4 holds W1 or W5 (`F` set after the call, or the
   devnode's `DN_NEED_RESTART` set at the end; with either, whatever else
   line 4 holds). It is not shown where a new start was seen (S1), where
   a start was seen to begin (S2), where the tool cannot tell (S4), where
   line 4 holds neither W1 nor W5 (W2, W3, W4, W6 or W7 alone), after any
   refusal of item 5 or R1, R10, A9 or A18 from the call, nor on 98 SE
   and ME, whose refused query already answers A10. It changes neither
   the exit code (3, from W1 or W5) nor the title (B2). Where line 3 is
   N4, the stopped controller's details the caller adds follow the answer,
   after H1. From the tray the answer with H1 is longer than the balloon
   takes (`TR_RST_BALLOON_CHARS`), so it is shown in the message box, as
   every answer of that length is.

   The lines of line 4 claim no cause and no state: the flags and the
   devnode's mark say only that Windows asks for a restart of the computer;
   lines 2 and 3 say what happened to the controller. H1 offers a cause as
   a possibility only. "Nothing more" is the owner's rule: no second
   call, nothing forced, no prompt to restart Windows (no
   `SetupPromptReboot`), and nothing Windows keeps is cleared -
   item 2's clear is of the tool's own information element, destroyed at
   its step 7, and the devnode's `DN_NEED_RESTART` is left as Windows set
   it. A failure after the confirmation can leave a keyboard or mouse of
   the controller gone; the answer box then waits like any other and
   nothing depends on its button.

   **Exit codes** of the one-shot run, in this order of precedence (record
   23 section 10's values):
   1. 1 for item 5's refusals, R1, R10, A10, A14, A19 and A23 - nothing was
      done;
   2. 2 for A18, A9, A12, A11, A15, A16, A17, A20, A21 and A22 - the request
      failed, or could not be prepared, or its end is not known;
   3. then, the request accepted (A1 or A13): 3 where line 4 holds W1, W3,
      W5 or W6; 0 where line 2 is S1, line 3 is N3 and line 4 is empty; 2
      otherwise (S2 to S4, any other line 3, W2, W4 or W7).

   **Success requires an accepted request**: A9 beside an S1 (a restart
   another process made in the interval) is exit 2, its lines saying both.
   The answer box's title, from the tray its balloon's, is B1 for exit 0
   and B2 otherwise.

9. **Rights: elevation and Run As** (section 26 item 10, settled). 98 SE
   and ME need no rights. An administrator (2000 and XP: a member of
   Administrators by `CheckTokenMembership`; Vista and later: an elevated
   token; both looked up at run time, `ej_is_admin`) restarts in place.
   Otherwise:
   - **Vista and later:** the confirmation's shield button starts the
     running executable through `elev_launch_req` with the verb `runas`,
     which raises User Account Control's prompt.
   - **2000 and XP** (owner, 2026-11-18): R1 is shown with a
     second button, "Restart as administrator...", which shows the
     confirmation and then starts the same run through the same routine and
     verb. On 2000 and XP `runas` raises Windows' Run As dialog, where the
     user names an administrator's account and its password (section 6.6
     item 3, read on both [rec]). `ShellExecuteExA` and
     `SHELLEXECUTEINFOA` are declared in MSVC 6.0's and WDK 7.1's
     `shellapi.h` [hdr], and `ShellExecuteExA` is already a row of
     `xhcitr-imports.allow` for both architectures, so nothing new is
     imported. The answer section (`Local\Xhci98TrAnswer-<process
     id>-<tick>`) grants write to SYSTEM and the Administrators, who include
     the account named in the dialog (section 6.6 item 4).
   Either way the run is `/restart "id=<instance id>" /result <section>
   /confirmed <digest>`, a one-shot. **Its first step is to open the
   section for writing** (`OpenFileMappingA` with `FILE_MAP_WRITE`, as
   `elev_child_answer` does). A run that cannot - a Run As account that is
   not an administrator, or XP's "Protect my computer and data from
   unauthorized program activity" box ticked, whose restricted token holds
   the Administrators group for deny only, neither of which the section's
   access list lets write - does nothing else: no check, no walk, no call,
   and exits 1 at once. The window, which holds the run's process handle
   (`SEE_MASK_NOCLOSEPROCESS`), then reads exit 1 with no answer in the
   section and shows A23, which says that nothing was restarted, as is
   exact, and offers the two likely causes without asserting either. The
   section's access list is not widened: a section any account could write
   would let any program put an answer in the window. A run that can write
   makes item 5's checks itself and walks again; where its digest differs
   from `/confirmed` it refuses with R9, as item 6's one-shot run does (a
   digest can spare a question, never add a device), and a `/restart`
   given no `/confirmed` asks its own confirmation; it then takes the
   mutex, repeats item 5's checks after it, makes the call and the
   settle, writes its answer
   (`tr_res_answer`) and exits with item 8's code. The window waits up to
   `TR_ELEV_WAIT_MS` as the eject's does and shows the answer it reads
   from the section. The outcomes of the start: the prompt or dialog
   cancelled (`ShellExecuteExA` fails with `ERROR_CANCELLED`) is A14; any
   other failure to start - the Run As service (Secondary Logon) stopped or
   disabled, a logon the dialog refused, the executable gone - is A16 with
   the error; the run ended with exit 1 and no answer, A23; the run started
   and no answer within the wait, an end with no answer and any other exit
   code, or an exit code that cannot be read, is A15 with line 3 as the
   window reads it. The tool
   makes no second attempt by itself. `/restart "id=..."` without
   `/result` is the command line's form (A17 for an unknown id). The
   manifest stays `asInvoker`.

10. **The stopped controller's line and the device involved.** As built by
    task 50.30's step 1 and approved (section 26 item 2): the tree line's
    suffix, the status lines, and "Involved:", which names the newest reset
    record's place (`tr_place_words`, from its root port and route string),
    its VID:PID, and, where `rec_name` finds one, a description. That
    description is the `DeviceDesc` of the first instance Windows recorded
    under `Enum\USB\VID_vvvv&PID_pppp` (`SYSTEM\CurrentControlSet\Enum` on
    NT, `Enum` on 98 SE and ME, read only), the text after an INF
    reference's last `;`: a best-effort description of *a* device of that
    VID:PID, which may be a stale entry or another device of the same
    model, and none for a composite device whose parent instance carries
    no description. The place and the VID:PID are the record's own and are
    always shown with it. The word is "involved", not "caused": a reset
    request associated with a device does not show that it caused the
    controller's failure (owner, 2026-11-18; it supersedes
    decision T30's "the device and place the driver names as its cause").
    No new driver value is needed for the line. This task adds M1 on the
    right-click and Action menus, M2 on the tray's red-icon menu and M3 as
    the details' last line where M1 is offered; where it is not (R3, R4, or
    a stopped controller on 98 SE and ME, item 3), the approved "Restart it from Device
    Manager, or restart Windows." stays, and R8's controller keeps the
    lines step 1 built.

11. **Both executables.** The NT path builds in both with the headers each
    is built with (item 2); the 9x path is compiled in both and runs only
    in `XHCITR.EXE` on 98 SE and ME (`envNt` clear). `XHCITR64.EXE` serves
    64-bit XP to 10 and, through the hand-over, `XHCITR.EXE` started there.
    No import, manifest or subsystem changes (section 9): every routine the
    restart adds is looked up at run time, and every other it uses is
    already an imported row.

12. **The strings.** Every one a draft for the owner but H1, whose
    wording he accepted on 2026-11-19 (revision 50); the full list, with
    the user documentation's drafts, is kept with the task's notes.
    Menus: M1 "Restart Controller...", M2 "Restart <controller>...", M3 "To
    restart it, right-click the controller and choose Restart Controller."
    The confirmation: C1 "Restart <controller>?"; C2 "These devices will
    disconnect, and should connect again when it has restarted:" and the
    scrolling list, a line a device; C3 "Its devices were removed when it
    stopped."; C4 "A drive's open files may be lost: close them first.";
    C7 "A keyboard or mouse on this controller stops while it restarts,
    and may not come back if the restart fails. Once you choose Restart,
    nothing more is asked of you until the answer."; C8 "It is the only
    keyboard (or mouse) Windows has."; C8a "The tool could not tell whether
    Windows has another keyboard (or mouse)." The Run As
    button: "Restart as administrator...". Refusals: R1 "Restarting a
    controller needs an administrator."; R2 the approved closed-off text;
    R3 "This Windows does not offer the call that restarts a device."; R4
    "Restarting a controller on 64-bit Windows needs XHCITR64.EXE in the
    same folder as XHCITR.EXE."; R5 "Windows uses drive C: on this
    controller (the Windows folder / the system folder), so the controller
    cannot be restarted while Windows runs. Restart Windows instead."; R6
    and R7 the tool's existing "Setup is running: try again when it has
    finished." and "Another program is removing a device; try again when
    it has finished."; R8 "The tool cannot tell how this controller
    stopped, so it does not restart it. Restart it from Device Manager, or
    restart Windows."; R9 "The devices on <controller> changed. Nothing was
    restarted. Look at the list again."; R10 "<controller> is no longer
    present; nothing was restarted."; R11 "The tool could not read every
    device on <controller>, so it does not restart it. Try again, or
    restart it from Device Manager."; R12 "USB storage is connected to
    <controller>: <list>. Unplug it, and then restart the controller.";
    R13 "The tool could not tell which controller drive C: is on, so it
    does not restart this controller. Restart Windows instead.", and where
    no one letter is at fault (item 7 step 3) "The tool could not tell which
    controller the Windows drive is on, so it does not restart this
    controller. Restart Windows instead."; R15 "A network adapter on <controller> may be
    carrying a network drive Windows uses, so the controller is not
    restarted. Disconnect the network drives first, or restart Windows.";
    R16 "The tool could not read what is plugged into <controller>, so it
    does not restart it. Try again, or restart Windows."; R18 "Drive E: is
    on <controller>, so the controller is not restarted. Unplug the drive's
    device, and then restart the controller." (R12a and R19 to R22, the
    refusals of revisions 45 to 47 for a letter unclaimed, and C10 are
    gone with revision 48.)
    Answers, line 1: A1 "Windows accepted the restart request
    for <controller>."; A9 "Windows did not accept the restart request
    (error 0xNNNNNNNN)."; A10 "Windows refused to remove <controller>. A
    program may be using one of its devices: close the programs using its
    drives and try again."; A11 "The controller was
    removed but Windows did not find it again. Use Refresh in Device
    Manager, or restart Windows."; A12 "Windows accepted the request but
    could not remove <controller> (code 0xNNNNNNNN)."; A13 "Windows removed
    <controller> and looked for it again."; A18 "The tool could not prepare
    the restart request (error 0xNNNNNNNN); nothing was asked of Windows.";
    A19 "Windows could not prepare the removal of <controller> (code
    0xNNNNNNNN). Nothing was removed."; A20 "Windows' search for the
    controller answered code 0xNNNNNNNN."; A21 "The tool could not find
    the device <controller> is attached to (code 0xNNNNNNNN). Nothing was
    removed."; A22 "The tool could not start a second search of the whole
    computer (code 0xNNNNNNNN)." Line 2: S1 "It has started
    again."; S2 "A new start of it began, but the tool did not see it
    running."; S3 "The tool saw no new start of it."; S4 "The tool cannot
    tell whether it was restarted." Line 3: N1 "Windows does not list it
    now."; N2 "Windows shows it stopped (problem N)."; N3 "It is running.";
    N4 "It is stopped after repeated failures."; N5 "Windows shows it
    running; the tool could not read the driver's state."; N6 "The tool
    could not read Windows' state of it (code 0xNNNNNNNN)."; N7 "Windows
    shows it started, but the driver reports it is not running."; N8 "The
    driver reports a state this tool does not know (N)."; N9 "It is
    running, with a recovery of the controller pending." Line 4: W1
    "Windows asks for the computer to be restarted."; W2 "The tool could not
    read whether Windows asks for the computer to be restarted."; W3
    "Windows had already asked for the computer to be restarted before this
    request."; W4 "The tool could not read whether Windows had asked for
    the computer to be restarted before this request."; W5 "Windows has
    marked <controller> to be finished at the next restart of the
    computer."; W6 "Windows had marked <controller> to be finished at the
    next restart of the computer before this request."; W7 "The tool could
    not read whether Windows has marked <controller> to be finished at the
    next restart of the computer." Line 5: H1 "A program may be using one
    of its drives: close it and try again, or restart the computer." (the
    owner's wording, 2026-11-19, accepted as drafted). Rights: A14
    "Nothing was restarted: administrator rights were not given."; A15 "No answer from the
    administrator's run."; A16 "The tool could not start as an
    administrator (error N). Nothing was restarted."; A17 "No xhci98
    controller has the instance ID <id>."; A23 "Nothing was restarted: the
    run as another account could not report back to this window. The
    account may not be an administrator, or "Protect my computer" may have
    been ticked; try again with an administrator's account."
    Titles: B1 "Controller restarted", B2 "Controller restart".

13. **The user documentation** (drafts; nothing is changed in `docs/using/`
    or `xhcitr\README.TXT` before the owner's answer, and none carries
    testing status). `xhcitr\README.TXT` gains a paragraph after Safely
    Remove: the action, Windows' stop-and-restart of the device (not
    Device Manager's Disable and Enable), the complete list and question,
    the stopped controller's line and the tray's item, the rights (Run As
    on 2000 and XP, the prompt on Vista and later), the restart of the
    computer Windows may ask for, on 98 SE and ME the storage that must be
    unplugged first and the drives the tool must be able to place (none
    in MS-DOS compatibility mode but a floppy drive A: or B:), the
    closed-off
    controller, and `XHCITR /restart
    "id=<instance id>"`; section 7.3's synopsis gains `/restart`. The
    release notes' "The xHCI98 USB Tree" gains a bullet with the same facts
    and "What `3.6.0.0` changes" an item. The known limitations take
    section 26 item 13's line in its first form, the action being offered
    on 98 SE and ME, and a line for 98 SE and ME: "The USB Tree does not
    restart a USB controller while a USB storage device is connected to it
    (unplug it first), nor while Windows has a drive, other than a floppy
    drive (A: or B:), that the tool cannot place - one in MS-DOS
    compatibility mode (System Properties, Performance), for one: restart
    Windows instead. It does not restart a
    controller that has stopped after repeated failures; unplug the device
    involved, wait for the driver's next attempt, or restart Windows."
    Where stage 1 ships alone (the two stages, above), the bullet, the
    readme and the known limitations say instead that on 98 SE and ME the
    restart is not offered: "On Windows 98 SE and ME the USB Tree does not
    restart a controller; restart it in Device Manager, or restart
    Windows." The readme goes in with the code and the release notes'
    lines at 50.final with the rest of the release text.

14. **Host vectors** (section 26 item 15, with): the lifetime part's
    layout beside the isolation and retry words, its flag, each buffer
    tier, an old reader of the new answer, the new reader of an old answer
    (the 312-byte minimum), a flag without its tail (`test_xhq`); the
    identity rules - different numbers, different times with the same
    number, equal identities read as S3 and never as a proof, number 0
    with a changed time, a completed-then-stopped pair of samples (S1
    kept), a failed read after S1 (kept), samples that differ with
    `XHR_F_STARTED` clear (S2), no valid baseline (S4), no valid sample
    (S4); item 8's four tables row by row and their combinations (A9 with
    S1 is exit 2; W1 with S1, S3 and N4; W2 and W4 with every line 2; W5
    after a refusal; W6 with a clear end; W7; A21 and A22 with no call
    through an unreturned handle; N6 apart from N1 and N2; N8 for terminals
    7 and up; on 98 SE and ME A12 with its parent's rescan, A20 where that
    fails, and line 2 from the lifetime samples after A12 and where the id
    is still found after the removal; H1 (revision 50) present, last, with
    A1, S3 and W1, W5 or both, and absent beside S1, S2 and S4 with W1 and
    W5, with S3 and neither W1 nor W5 (W3 or W6 alone), after A9 and A18,
    after a refusal before the call, and on 98 SE and ME (A13 with S3 and
    W5), the exit code unchanged; the answer with H1 longer than the
    tray's balloon takes),
    the exit codes' precedence; the walk's completeness rule (a bound met, a
    failed read, a `Bad` rise), the grouping (a composite device with HID
    and disk children, a hub behind a hub with devices on both, the root
    hub), the sorted digest over every devnode and its change by one
    devnode anywhere; item 7's storage rule clause by clause (each id,
    class and service form, an interface 08h on an unbound function, an
    empty reader, a composite device with one storage function, a port
    with no devnode and readable descriptors - R12 whether or not this
    process stopped it - one with unreadable descriptors, an unread hub
    and a port bound met (R16)), a stopped controller on 98 SE and ME
    never offered, and the drive rule (each failure of steps 1 and 2, the
    form of R13 naming no letter for each of its causes (a step 1
    failure, `GetLogicalDrives` answering 0, an incomplete walk, a key or
    an assignment not read) and before any letter's R13, a letter other
    than an unclaimed removable A: or B: (C:, say) with no claimant - R13
    with
    nothing on the controller, with only a keyboard and mouse, and beside a
    stick, a placed letter on the controller and a network adapter, R13
    each time - an unclaimed removable A: and B: passed over (revision
    49) with nothing else at fault, beside a keyboard (passes), beside a
    stick (R12) and beside an unclaimed C: (R13 naming C:), an unclaimed
    A: of another type (R13 naming A:), a removable A: claimed twice (R13)
    and claimed once on the controller (R18), a no-letter fault beside an
    unclaimed floppy letter (R13 naming no letter) - two claimants, a key
    that does not open, a failed
    `CurrentDriveLetterAssignment` read, an incomplete walk, the first
    letter named, a placed letter elsewhere, a placed letter on the
    controller on a device the storage rule did not match (R18), the
    Windows drive on it (R5) before R18, R5 and R18 before R12, the
    network case R15 after R12, a network installation's path); the Run As run
    that cannot open the section (exit 1 at once, nothing called; A23 in
    the window, A15 for any other exit with no answer); the `/restart`
    command line with `/result` and `/confirmed`.

15. **Acceptance legs.** On the `qemu` flavour in the emulator unless a row
    says otherwise, with item 4's driver addition built; then the published
    `release` package on one guest a family at 50.final's install legs.
    Each row records what was observed; where an answer is named, it is the
    one the readings select, and a row passes when the answer matches what
    the guest did, not when a given answer appears. "Latched" is record 17
    section 4.15.13's leg (a) (code `05` four times inside the window);
    "contained" is injection `07`; "in use" a program holding a file open
    for writing on a drive of the controller. A "stand-in" is a test build
    of the tool, never shipped, that forces one reading's answer.

    | Leg | Windows | What is read |
    |---|---|---|
    | T.27 (stage 2) | 98 SE, ME | on the q35 images as launched, whose system disk runs in MS-DOS compatibility mode (an unclaimed letter, item 7 step 3): the action refused with R13 naming C: with nothing on the controller, with a keyboard and mouse on it, and with a stick on it, nothing called; then on each image with its system disk on the PIIX IDE controller without the geometry hints, where the reading shows every letter placed (on a system that does not place them, its row says so and its restart rows are owed): Device Manager's Performance tab and the letters read as in 50.32's reading; a composite audio device on a root port, and a keyboard and a mouse on the controller (C7, C8a or C8 as read, the PS/2 mouse or keyboard outside): the action offered, the complete list, every device back after A13, S1, N3; a stick with its letter (R18) and after this tool's Safely Remove of it (R12), nothing called either time; a hub with a device behind it (offered); the emulator's network device with a mapped network drive where one can be made, else by a stand-in (R15); a stopped controller: no item offered, the approved line; a letter claimed twice, a failed `CurrentDriveLetterAssignment` read and a step 1 failure (R13, by stand-ins); a removal failure (A12, then the parent's rescan), `CM_Get_Parent` failing (A21), a root locate failing (A22) and rescans returning errors (A20), by stand-ins, and a rescan that finds nothing (A11); a settle that times out; under revision 49, on each image with no floppy controller and every other letter placed: the unclaimed removable A: read, a restart with a keyboard or tablet on the controller (offered, A13), the same with a stick (R18 or R12), a hard-disk letter unclaimed (R13), and a USB floppy drive on the controller where the emulator can present one (else said so) |
    | T.28 | 2000 SP4 and the SMP image | administrator and a user: healthy and latched restarts; in use: whatever 2000 answers, recorded with lines 2 to 4 (the `DICS_DISABLE` reading of 49.5 is not taken for it); a user: R1 with Run As - an administrator's account given (the restart), the dialog cancelled (A14), a limited account given (exit 1, A23, nothing restarted), the Secondary Logon service stopped (A16); contained: R2, the item disabled; a keyboard and mouse on the controller; `DN_NEED_RESTART` set before the request (W6) |
    | T.29 | XP SP3, XP x64 | as T.28, with XP's "Protect my computer" box ticked (exit 1, A23); on XP x64 `XHCITR.EXE` handing over (the exit code back), R4 with `XHCITR64.EXE` moved away, and A16 with it moved away after the window started |
    | T.30 | Vista SP2 and 7 SP1, x86 and x64 | as T.28, elevated and as a standard user: the shield, the prompt allowed and cancelled (A14), R9 by a device plugged during the prompt, a device behind the 13th line still in the digest; on x64 R4 and A16 as T.29; a restart Windows refuses in the session (an Explorer window open on the stick): A1, S3, N3, W1, W5 and H1, exit 3, and with the window closed the restart without H1 (revision 50) |
    | T.31 | 8, 8.1, 10, x86 and x64 | healthy, latched, in use, the elevated run, the hand-over on x64, R4 and A16 as T.29 |
    | T.32 | 98 SE, 7 x86 | the tool with the `3.5.0.0` driver and with a driver without the lifetime part: S4 on 7, S1 by the devnode on 98 SE; R8 for a failed controller |
    | T.33 | 7 x86 and 98 SE (stand-ins) | the lifetime readings: an equal identity throughout (S3), a new identity with `XHR_F_STARTED` clear (S2), one completed and then stopped before the next sample (S1 kept, N2 or N7), a reload with an equal image time and number (S3, by a stand-in), a reload with a new time and the same number (S1), a counter at its last value before the wrap, a failed baseline read (S4), failed samples between valid ones, a rejected request (A9) beside a start another process made (exit 2); the devnode's status read failing (N6); a terminal 9 (N8); the final parameter read failing (W2), `P` set before the call (W3), the first read failing (W4), `DN_NEED_RESTART` set (W5, also after R1); a task 50.31 try during the settle where that task's code is in |
    | T.34 | 98 SE, 7 x86 | an incomplete walk (a stand-in bound): R11 on the first and on the second walk; on 98 SE a query answering an error other than a veto (A19, by a stand-in), and, between the confirmation and the call, a letter other than a removable A: or B: (C:, say) left unclaimed (R13) or a stick plugged in (R18 where it has taken a letter, R12 where it has none yet), by the checks repeated before the call |

    None of these is a reading of real hardware; the release's bench gives
    that where the owner takes it.

16. **For the owner.** Decided, and marked "(owner, 2026-11-18)"
    above: the stop-and-restart request in place of Disable then Enable
    (item 1), a controller stopped after repeated failures not offered the
    action on 98 SE and ME (item 3), the 9x storage, drive and network
    refusals (items 5 and 7), the driver addition (item 4), Run As on 2000
    and XP (item 9) and "involved" (item 10). Decided on 2026-11-19 and
    marked "(owner, 2026-11-19)", given through the coordinator: on 98 SE
    and ME the restart is refused whenever a local drive letter is
    unclaimed (item 7 step 3, R13); no port sampling and no table of
    passing devices is built, and the owner's allowance of 2026-11-18 for
    such letters is narrowed by it, as he knows. The questions that hung on
    that allowance - the table of passing devices, keyboards, mice and
    tablets while a letter is unclaimed and their refusal's wording, the
    driver's serving of HID report descriptors - no longer arise. What the
    refusal costs, stated plainly: on a 98 SE or ME machine with any drive
    other than a floppy drive in MS-DOS compatibility mode - both q35
    images as launched - the tool restarts no controller at all; Device Manager and a restart of Windows
    remain. Decided later on 2026-11-19, also given through the
    coordinator (revision 49): a floppy letter, a removable A: or B:, that
    no devnode claims no longer refuses the restart, while every other
    unclaimed local letter still does (item 7 step 3), the owner having
    been told and having accepted the risk that item states - a
    firmware-served A: or B: on the controller being restarted that the
    storage rule does not recognise as storage then passes; so a 98 SE
    or ME machine with no floppy controller, whose A: is such a letter,
    is no longer refused for it alone. And a USB storage device stopped
    with Safely Remove but still plugged in is refused like any other
    (item 7, C10 stays dropped), taken in revision 48 as the smaller
    design and since decided by the owner. Decided later on 2026-11-19,
    given through the coordinator (revision 50): after a restart request
    Windows accepted with no new start seen and Windows asking for a
    restart of the computer or the controller marked (A1, S3, W1 or W5),
    the answer ends with H1 on 2000 and later, in the wording drafted for
    him (items 2, 8 and 12). Open: the wording of the answer's other lines
    and of every other string here, drafts for the owner.

## 28. Line widths in the window and Save Report (roadmap-usb4 task 50.39, owner 2026-11-19)

Design before code, revision 52 (2026-11-20); items 4 to 8 revised in
revision 53 (2026-11-20) after the owner's answers. The owner decided on
2026-11-19 that the widths of this tool's window and of Save Report are
swept in this release, after tasks 50.30 to 50.32. The rule is the one
sections 3.6, 6.5 and 20 already state: every line within 79 columns.
Task 50.30 brought the snapshot engine's report within it
(`snapshot-reader.md`; table rows, verbatim data and self-test output
excepted) and left this tool's own texts. Section 7.3's addendum, that
Save Report's tree lines can pass 79 columns at depth, stays for the
tree's labels.

1. **What the sweep found** (from the source at `319f8b6aa`: every text
   call of `xhcitr\`, each number at the most digits it can hold, each
   string at its longest; then measured in guests). The details pane,
   which Save Report repeats node by node: 21 line formats pass 79
   columns with their words and numbers alone - among them the recovery
   requests' `reset requested:` line (section 26), the descriptors'
   companion and SuperSpeedPlus lines, the Thunderbolt router's tunnels
   line, the PCI and bridge diagnostics' counts, the network link's
   logins - and about 105 more with long values (names, the transport's
   reasons, the Settings rows, the counts at their bounds). Save
   Report's own lines: its header, the driver keys' notes (task 50.29)
   and the Settings block. The dialogs, measured in their own font on 98
   SE and XP (the restart dialog of section 27 with the longest name and
   every note, Settings, Logging level, the stop with administrator
   rights, About): every text fits its control, but Settings' heading
   when six or more settings wait for a restart (two lines high). The
   tray's menu items with a long device name pass the screen's width.
   Balloons are bounded already (an answer longer than 250 characters
   goes to a message box), and message boxes wrap their own text.
2. **The fitting** (`tr_fit_text`, the pure half). A line within the
   width is written as it is. A wider one is broken between words: after
   a clause's end (the space after a `,`, `;`, `.` or `:`) that falls in
   the line's second half by column, else at the last space that fits;
   never inside a quoted string, inside the label or before its first
   word. Its further lines are put under its first value - the column
   after a short label ("Label:" and its spaces, the label at most 24
   characters with no `,`, `;`, `.` or `"`, the value no further in than
   half the width) - else under the line's own indent, again no further
   in than half the width. A word or a quoted string wider than a line is
   written whole and the line broken after it. A line that begins with
   the mark `TR_FIT_WHOLE` is a value read as it is - a name, an id, a
   path, a version, a serial number or a string the device or Windows
   gave - and is written whole, the mark dropped. A field's paragraph - a
   "Label:" line and the lines under its value that follow it, which its
   maker already wrapped - is joined from its first line wider than the
   width to its end (at most 2048 bytes: past that, the rest is fitted on
   its own) and fitted again as one, so that a narrower width does not
   leave it in short pieces; a paragraph that fits is not touched, and
   a line without a label (a note and the count after it, a list) is never
   joined to the next. Only spaces and line ends change: no word, no order,
   no punctuation. It is the rule task 50.30
   gave the snapshot engine, applied to this tool's text once it is made,
   so a line added later is fitted too.
3. **Where.** The details pane and Edit > Copy (the pane as shown), at 79
   columns; Save Report's and `/report`'s header, Logging levels and
   Settings blocks at 79; each node's details, and the heads and notes of
   the driver keys' blocks (task 50.29), at 79 less the node's lead
   (`tr_report_width`: the lead is two spaces a level of depth, four more
   and `| `), so that with the lead every line stays within 79 columns
   down to a node 26 levels deep (item 6 says what passes it deeper)
   (`tr_reg`'s notes take their width from the key, `TR_REG_KEY`'s
   `Width`; the `.DEV` keeps 79); the Logging level dialog's list,
   `/set /show` and the `XHCISET.TXT` beside a snapshot, at 79, a block
   that no longer fits its caller ending with the cut mark (both 16 KiB
   callers now have 64 KiB). Marked whole (`TR_FIT_WHOLE`): the Device
   Manager entry's description, instance ID, hardware ID, driver, driver
   key, version, INF file, matching ID and provider; a USB device's
   driver key, manufacturer, product, serial number, string descriptors
   and hub name; a router's name; a controller's "Opened as", root hub and
   location; the other computer's name, vendor and device on a network
   link (the window's form only: the snapshot's carries no mark); the
   PCI and bridge diagnostics' `XhciPfwVersion` value and their names and
   ids; the driver-key path in the Thunderbolt log's advice; and every
   line that is a node's name or path - a unit's, a device's, a log's or a
   PCI node's heading, a USB port's, a tunnel port's and a root hub's first
   line, the host interfaces and the devices that stop with a unit or a
   USB device, one a line - and the driver keys' block heads in Save
   Report. The Logging levels block's names are controllers' and host
   interfaces' own names (at most 63 characters) and fit as they are. Not
   fitted: a
   registry value's line (its data as read, as task 50.30 left verbatim
   data); the logs' records, which hold no space and so are never broken;
   the tree's labels (section 7.3); the snapshot engine's text (task
   50.30's own fitting); the console's one-shot answers (already wrapped
   at 79); message boxes and balloons.
4. **The dialogs.** Settings' heading (`IDC_S_HEAD`) is three lines high
   instead of two, the settings list one line shorter and the boxes of
   the tool's own tab eight units lower; the dialog keeps its height,
   which fills a 640 by 480 screen's work area. Since the owner's answer
   of 2026-11-20 the heading is a read-only text box with no border, in
   the label's place and size, that scrolls, so that every setting
   waiting for a restart can be read however many there are: its
   vertical scroll bar is shown, and the box is a stop of the Tab key,
   only while its text needs more lines than the box shows (the lines
   counted with the bar shown, against the height of the box's
   formatting rectangle in lines of its font), so that a heading that
   fits looks as the label did; its text
   margins are none, so that its words start where the label's did; and
   its text holds every row's name (it was cut at 600 bytes). Its words
   are as they were. Every other dialog text fits as it is.
5. **The tray menu** (the owner, 2026-11-20). Its item for a device,
   "Safely remove" and the device's label (its disks and their letters,
   else its name), with the reason it cannot be stopped where there is
   one, cuts the label in the tray menu alone, with the "..." the label
   already ends with when it is cut, so that the item keeps within the
   screen. The label's length is at most `tr_tray_label_chars`: 60
   characters on a screen 640 pixels wide and in proportion to the width
   (three thirty-seconds of it: 75 at 800, 96 at 1024, 120 at 1280, and
   from 1696 the 159 the label has always been held to), and no more than
   leaves the whole item within the width at 6 pixels a character once
   48 pixels are left for the menu's own margins (the longest label
   measured, an item of 158 characters, was 817 pixels in 98 SE's menu
   font and 797 in XP's, about 5.2 and 5.0 a character): with a reason of
   43 characters, the longest a USB device gives, 38 at 640. It is never
   under 24 characters, so that a label keeps a few words, and a width
   that cannot be read leaves the 159 of before; either can leave an item
   wider than the screen (at 640 the floor with the longest reason, a
   Thunderbolt unit's 61 characters, comes to about 660 pixels by the
   same count), and so can a label of wide letters (capitals). The width
   is that of the work area of the monitor the menu opens on, at the
   cursor (`MonitorFromRect` and `GetMonitorInfo`, looked up at run time
   as for the window's place; the primary's work area where they are
   missing). The cut is the tray's own (`tr_tray_label`): `tr_unit_label`
   at the tray's length, the letters kept whole, except that a name with
   no disks, or disks whose letters leave no room, is cut with "..." too,
   where `tr_unit_label` cuts without one. On a double-byte code page
   the cut falls between characters, never between a character's lead
   and trail bytes, and can be a byte shorter: the pure half takes a test
   of a lead byte from its caller, which passes `IsDBCSLeadByte` (already
   imported) for the system's code page (revision 54). Every other place
   a label is shown - the tree, the details, the window's Action menu, the
   confirmations and the notices - keeps `tr_unit_label` and its own
   bound as before.
6. **What it leaves.** Lines whose words alone pass the width: a value
   marked whole, a word, a quoted string or an indent wider than the
   line. A report node more than 26 levels deep: 79 less its lead would
   leave fewer than 20 columns, and `tr_report_width` keeps 20, so its
   lines can pass 79 with their lead. The tree's labels at depth (section
   7.3). The details pane at its
   default size shows about 68 of its 79 columns (820 by 560, the
   splitter at 300, a 7-pixel font; at 640 by 480 about 45): kept so by
   the owner (2026-11-20). Fitting each report node to its own width,
   rather than keeping the pane's 79 under the lead, was taken as the
   recommended choice under the 79-column rule (revision 52, reviewed)
   and kept by the owner the same day.
7. **Host vectors.** `test_tr`: lines within the width kept to the byte,
   CR LF, LF and a lone CR, the break under a label's value, the clause
   rule by column, a field's paragraph joined and fitted again (from its
   first line or a later one, with no line end at its last, CR LF and a
   lone CR, ended by a line marked whole or a blank line, and past its
   buffer) while a line with no label is not, a time is
   not a label, a word, a quoted string and an
   indent wider than a line, a value marked whole, empty input, spaces
   alone, a label alone, 80 columns, the bound and the cut mark, the
   report's widths and a node at depth 4 within 79 with its lead, and a
   decoded descriptor's companion line, each with every word kept in
   order; `tr_tray_label_chars` at 640, 800, 1024, 1280 and 1696 pixels
   with no reason, a short one and the longest, at a narrow width (24)
   and at one not read (159), and `tr_tray_label` cutting a name with no
   disks, and disks whose letters leave no room, with "...", and keeping
   the letters whole within the tray's length, `tr_unit_label` without
   a lead test unchanged (item 9);
   double-byte names (Shift-JIS lead bytes) cut between characters on
   each path - a name alone, disks' names before their letters, disks
   whose letters leave no room, under five bytes - a trail byte that
   looks like a lead byte, a one-byte character before them, and no lead
   test (every byte a character);
   `test_xhq`: the recovery status at its widest (every reason
   counted at its largest, four records with the longest words and places,
   a long name, isolation and a pending try) within 79 columns with every
   word kept; `test_trreg`: the key's notes on their fitted lines, at 79
   and at a node's width.
8. **Legs** (emulated guests, the qemu flavour): 98 SE and 2000, a
   controller stopped after repeated failures by the injection codes, its
   details and Save Report read and every line counted; the dialogs'
   measurements as item 1. After items 4 and 5: 98 SE at 640 by 480 and
   XP, a screenshot of each surface changed - the details pane, Save
   Report, Settings' heading with a long list waiting for a restart, and
   the tray menu with a device whose label is long - and Settings'
   heading at work: one that fits (no scroll bar, no Tab stop), a long
   list (the bar, scrolled by the keyboard, a Tab stop) and back to one
   that fits (another tab, the tool's own tab), with Tab and Shift+Tab,
   Enter and Escape and the Restart button beside it; the long list's
   last name reached by scrolling as soon as it is set, and the bar as
   set after a repaint and after the focus moves.
9. **The tree's own label** (the owner, 2026-11-20, revision 55). Every
   cut `tr_unit_label` makes - the label of the tree, the details, the
   window's Action menu, the confirmations, the notices and Save Report,
   every place but the tray menu - falls between characters on a
   double-byte code page too, as the tray's does (item 5): the caller
   passes the same test of a lead byte (`IsDBCSLeadByte`, the system's
   code page), and the same rounding down to a character's end serves
   each of its cuts - a name with no disks, the disks' names before
   their letters, disks whose letters leave no room. Before, these cuts
   were by bytes, and disks' names joined past the label's 159 bytes
   could end on a character's first byte. No word changes, the bounds
   are as before, a cut label can be a byte shorter, and on a
   single-byte code page nothing changes. The later cuts of a name in
   the "can run faster" notice's set and in the red icon's tooltip
   (`tr_fast_name`, `tr_red_tip`), left by bytes here, follow in item
   10.
   Host vectors (`test_tr`, Shift-JIS lead bytes): disks' names a byte
   under, at, one and two bytes over their room before the letters, and
   a name alone the same way, with the test and without it; disks whose
   letters leave no room; single-byte names (and a single-byte code
   page's test) cut as without a test.
10. **The notice's and the red icon's names** (the owner, 2026-11-20,
    revision 56). The same test of a lead byte, and the same rounding
    down to a character's end (`tr_char_floor`, the one routine items 5
    and 9 use, with `tr_copy_lead`, a copy that ends between
    characters), serve the other cuts of a device's name: `tr_fast_name`
    - the "can run faster" notice's names, the lines that name a slow
    device and its hub, and the red icon's set, held in 72 bytes - for
    the label, the product string and the class-name label alike;
    `tr_red_tip`, the red icon's tooltip (64 or 128 bytes) and the tray
    menu's item, where a name is cut to the room its fixed words leave
    and ends "..."; and the copy of a controller's name into the red
    icon's list. XHCITR passes `IsDBCSLeadByte` to each. No word
    changes; a cut name can be a byte shorter on a double-byte code
    page; with no test (NULL) every cut is byte for byte as before, and
    on a single-byte code page nothing changes. The "can run faster"
    list's own layout of a name after `tr_fast_name`, left by bytes
    here, follows in item 11.
    Host vectors: in `test_tr`, `tr_copy_lead` at 8 bytes, and
    `tr_fast_name` at 72 bytes on each of its three paths, each with
    double-byte strings a byte under, at, one and two bytes over the
    room, with the test and some without it (a product's spaces taken
    off first); in `test_trred`, the tooltip's name the same
    way at a 64-byte tip; single-byte names, and a single-byte code
    page's test, cut as with none.
11. **The rest, and the search** (the owner, 2026-11-20, revision 57).
    The same routines, with the same test passed in, serve the last
    cuts of a device's name: the "can run faster" list's entry
    (`tr_fast_entry`), whose name line and "Behind <hub> (<speed>)" are
    cut to their 160 bytes between characters, and whose wrapping keeps
    of a word longer than its 96-byte line's room only whole characters
    (a word starts after a space, and a space is never the second byte
    of a double-byte character); the USB drive notice (`tr_stor_text`),
    whose names cut to their room before "..." end between characters;
    the console's wrap of a one-shot answer (`tr_wrap`, a word longer
    than the line, and the end of its 16 KiB), the recovery details'
    fields (`tr_xhr_details`) and the PCI and bridge diagnostics' NHI
    heading (`tr_diag_wrap`), where a word longer than the line is broken
    between characters; the 98 SE and ME storage refusal of a restart
    (`tr_rst_rules9x`), cut to its room between characters; and, in the
    window's half, a controller's name copied into the port-hold
    notice's list, a recovery record's device name (its `DeviceDesc`)
    copied into 96 bytes, and the last notice kept for the tooltip of a
    shell before 5.0 and that tooltip. No word changes; with no test
    every cut is byte for byte as before for the texts the tool makes
    (names tidied of line ends). xhcitr was searched once more, every
    copy, format and wrap of a text that can hold a device's name
    against its destination's room. The sources that can hold a
    double-byte name are a devnode's description and friendly name (read
    whole or not at all), the labels of items 5 and 9, the names of item
    10 and a registry `DeviceDesc`; IDs, product strings, Thunderbolt
    router names and the network link's peer texts are ASCII alone. Left
    by bytes, each never shown as a split character: the joined disk
    names behind a label (512 bytes, of which a label keeps at most 156);
    the snapshot engine's raw property and registry texts (written only
    as printable ASCII); the driver keys' screening windows (`tr_reg`,
    1,800 bytes, matched and never shown); and the storage refusal's
    list (1,600 bytes, cut only past the refusal's 600). A text that
    overflows its whole buffer (the details pane's 64 KiB, Save Report's
    blocks) is a bound of the text, not of a name: where it is marked
    cut, the mark follows the last line end within 256 bytes, or, with
    none there, the last byte that fits. Host
    vectors: in `test_tr`, the entry's name line, a continuation line
    and the hub line, the drive notice with one name and with two, and
    the console's wrap and its end; in `test_xhq`, the recovery details'
    name; in `test_trrst`, the storage refusal; in `test_trdiag`, the
    NHI heading; double-byte names under, at and over each limit with
    the test, and some of the cases over it also without the test
    (today's bytes, a lead byte last); single-byte names, and a
    single-byte code page's test, cut as with none.
