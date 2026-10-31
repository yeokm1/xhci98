# Design Record 36 - The Download by Windows Family

Status: revision 3, 2026-10-31, before code (revision 1 of the same
day reviewed and revised: the cut re-lays the flat package, the engine's
reads that bypass its source copy, the family filter of each INF's file
list, the catalogs across the re-lay, Stor98's copies by their record,
the helper's range, the deepest-path reading; revision 3 the same day:
every published file compared with its verified flat source, the
reconstruction's checks kept to `-UploadSetOnly`'s own, the helper on 8
to 10 as the installer offers it). Written for `roadmap-usb4.md`
task 45.44 (owner, 2026-10-30 and 2026-10-31), release `3.4.0.0`. It
replaces the download's flavour-and-architecture directories with one
folder per Windows family and architecture, and says what that changes in
the packager, the cut, the gates, the installer engine, the documents and
the legs. Design record 26 section 2 (where the installer lives in the
package), record 23 section 4.2 (the package check) and record 19 section
3a (the x64 catalogs) are amended where this record says so; every other
rule of theirs stands. Section 12 lists the owner's questions.

## 1. The task, and what it is not

The download of `3.3.0.0` holds `release-x86\` and `release-x64\` (and,
in that cut, `debug-x86\` and `debug-x64\`, which task 45.45 retired),
each a flat directory of every INF and image for its architecture, with
the x86 filter INF in `nt\`. A person integrating the drivers into a
Windows setup image (nLite, NTLite, DISM) had to know which files their
Windows reads and that the filter INF must not be integrated; a person
installing by hand on 98 SE had a folder holding NT INFs too. The owner's
decision (2026-10-30): three folders, one per family and architecture,
the INFs choosing per system by their decorated sections as they do
today:

| Folder | Windows | Architecture |
|---|---|---|
| `98-me\` | 98 SE, ME | x86 |
| `nt-x86\` | 2000, XP, Vista, 7, 8, 8.1, 10, 32-bit | x86 |
| `nt-x64\` | XP x64 and Server 2003 x64, Vista to 10 x64 | x64 |

Not in this task: any change to an INF's sections, to a driver, to
`txtsetup.oem`'s options, to what the installer installs on a system, or
to the local copy the installer makes in the install directory (section
6.1: its shape is kept). The Thunderbolt networking files are placed by
task 45.59's step S10, after this task (section 9).

## 2. The layout

```
<download>\
  SETUP.EXE  INSTALL.BAT  readme.txt  LICENSE  INTEGRATE.TXT
  tools\       (as in 3.3.0.0: xhciinst, xhcimark, xhciqual, xhcitr,
                portsw, silent)
  USBDRV\      (as in 3.3.0.0: NUSB33\ in the base download, FREE\ and
                NONFREE\ in the free and non-free downloads)
  98-me\
    xhci98.inf  xhci98.sys  xhciuas.inf  xhciuas.sys
    cm\      xhcicm.inf  xhcicm.sys  XHCIPFW.VXD
             tbnet\       (45.59 S10)
    stor98\  stor98.inf (the 9x INF, was stor98w9.inf)  stor98.mpd
             S98VSD.VXD  stor98-readme.txt  stor98-LICENSE.txt
             stor98-NOTICE.txt
  nt-x86\
    xhci98.inf  xhci98.sys  xhcihlpr.sys  xhciuas.inf  xhciuas.sys
    txtsetup.oem
    cm\      xhcicm.inf  xhcicm.sys  xhcicmf.inx  XHCIPFW.SYS
             tbnet\       (45.59 S10)
    stor98\  stor98.inf  stor98.sys  stor98.mpd  stor98-readme.txt
             stor98-LICENSE.txt  stor98-NOTICE.txt
  nt-x64\
    xhci98.inf  xhci98.cat  xhci98.sys  xhciuas.inf  xhciuas.cat
    xhciuas.sys  txtsetup.oem  xhci98-test.cer
    cm\      xhcicm.inf  xhcicm.cat  xhcicm.sys  xhcicmf.inx
             xhcicmf.cat
             tbnet\       (45.59 S10)
    stor98\  stor98.inf  stor98.cat  stor98.sys  stor98-readme.txt
             stor98-LICENSE.txt  stor98-NOTICE.txt
```

Every name is 8.3-clean (`98-me`, `nt-x86`, `nt-x64`, `cm`, `stor98`,
`tbnet`; the files as in `3.3.0.0`, `xhcicmf.inx` new), because 98 SE
and ME read the folders through their shell and their 16-bit setup
engine. Each family folder is a whole driver source for its systems: an
integration tool pointed at it with recursion takes every INF it should
and none it should not (section 4), and a person installing by hand
points the hardware wizard at the root (the controller, the UAS driver),
at `cm\` (the connection manager) or at `stor98\` (Stor98).

**What the INFs choose (no change).** `98-me\` and `nt-x86\` carry the
same bytes of `xhci98.inf`, `xhciuas.inf`, `xhcicm.inf`, `xhci98.sys`,
`xhciuas.sys` and `xhcicm.sys`, the one 32-bit binary of each (AGENTS.md,
"One binary"): 98 SE and ME read their undecorated sections and 2000 to
10 their `.NTx86` and `NTx86.6.0` sections, exactly as from
`release-x86\` today. `nt-x64\` carries the amd64 INFs and images of
`release-x64\`. A family folder holds only what its systems install: so
`98-me\` holds no NT-only file (`xhcihlpr.sys`, `XHCIPFW.SYS`,
`stor98.sys`, the filter, `txtsetup.oem`) and `nt-x86\` no 9x-only file
(`XHCIPFW.VXD`, `S98VSD.VXD`, the 9x Stor98 INF). The one exception is
`stor98.mpd` in `nt-x86\stor98\` (section 3.3).

**`USBDRV\`, `tools\` and the root as in `3.3.0.0`**, with `INTEGRATE.TXT`
new (section 8.2). The cut's records stay in `releases\<version>\` and
out of the download, as they already are (section 7.3).

## 3. The design points of the task

### 3.1 The helper's groups

`xhcihlpr.sys` is the x86 helper of 2000 to 7 (design record 18 section
15). `xhci98.inf` installs it from `[Xhci.Dev.NTx86]` (2000, XP) and
`[Xhci.Dev6.NTx86]` (Vista, 7), the sections the INF gate's `HLPR-*`
rules hold it to, so it goes in `nt-x86\` at the root, beside
`xhci98.inf`, which names it in `[SourceDisksFiles]` with the root as its
disk; never in `98-me\` (98 SE and ME never read an `.NTx86` section) and
never in `nt-x64\` (the 64-bit package never carried it). `xhcimsi.sys`,
its 2.3.x predecessor, is never shipped: `[Xhci.Dev6.NTx86]`'s
`DelService=xhcimsi` and its delete line remove it in an update, and the
gate's `MSI-*` rules stay as they are.

**The task's "2000 to 7 alone" and the INF.** `xhci98.inf`'s
`NTx86.6.0` decoration has no upper bound: 32-bit 8 to 10, which read it
too, would take `[Xhci.Dev6.NTx86]` and the helper with it. They are not
targets of the USB drivers - the installer offers them the connection
drivers on 8 to 10 unticked by default, and ticking them (or `/usb`)
selects them with a warning that they are not supported there (record 26
sections 5.3.1 and 5.3.2; `inst_core.c`'s case 3) - so "2000 to 7" is the
helper's supported range, not a bound the INF enforces: a user who ticks
the USB drivers on 8 to 10 x86 past the warning gets the helper too, as
in `3.3.0.0`. This task changes no INF
(section 1); an upper bound would be an INF change with its own legs,
and is left as it is (section 12, Q6).

`98-me\`'s `xhci98.inf` still names `xhcihlpr.sys` in `[SourceDisksFiles]`
(the same bytes as `nt-x86\`'s): 98 SE's and ME's engine copies only
what the section it runs names, and their sections name no helper. So
the files a family folder must hold are not the INF's whole
`[SourceDisksFiles]` list but the family's: the cut's family table
(section 5.1) and the engine's package check (section 6.4) read each
INF's list filtered by the family, a name the family does not carry
(`xhcihlpr.sys` in `98-me\`) skipped, and every other name required.

### 3.2 The connection manager's filter on 2000 to 10

`xhcicmf.inf`, the tunnel-port filter, applies on every NT system of the
package: its models are undecorated for 2000 and XP and `NTx86.6.0` (or
`NTamd64`, `NTamd64.6.0` in the 64-bit file) for Vista and later, so 8,
8.1 and 10 take it, and record 23 section 5.14.2's table reads it on 8,
8.1 and 10 x64 and on 8, 8.1 and 10 x86 (the bridge started with
`UpperFilters` `xhcicm`). So both NT folders carry it, in `cm\`; `98-me\`
never does.

**It ships as `xhcicmf.inx`, its one copy, beside `xhcicm.sys` in
`cm\`** (owner, 2026-10-30). An integration tool pointed at a family
folder with recursion takes every `*.inf` it finds; with the filter among
them, every tunnel bridge would get `xhcicm` as an upper filter at the
first boot of the integrated system, before `SETUP.EXE` ran - with the
connection manager's service promoted to boot start by Windows (record 23
section 5.14.2) and none of the installer's precautions (its put-back of
the start type, its Thunderbolt question, its refusal of Intel's own
driver on the NHI, record 23 section 8.5.1). With the extension `.inx`
no tool and no wizard takes it as an INF. The installer is its one
consumer: it reads `cm\xhcicmf.inx` and makes it `xhcicmf.inf` in the
local copy (section 6.2). The bytes are `src\cm\xhcicmf.inf`'s (x86) and
`src\cm\xhcicmf-amd64.inf`'s (x64), unchanged.

So `nt-x86\` no longer needs the `nt\` directory of `3.3.0.0`, whose
reason was that 98 SE and ME read a `$Windows NT$` INF beside the one
they are pointed at (`runs/run-39.md`, 39-0.4): 98 SE and ME now search
only `98-me\`, which holds no filter. Nor does it need `nt\xhcicm.sys`,
the second copy that Vista's and 7's driver store wanted beside the INF
it stages (SetupCopyOEMInf 0xA1): in the download the filter sits beside
the one `xhcicm.sys` in `cm\`. (The local copy keeps its own second copy,
section 6.2.)

### 3.3 Stor98's `.mpd` on NT

`stor98.inf` for x86 (`src\stor98\inf\stor98-x86.inf`) names both
`stor98.mpd` and `stor98.sys` in its `[SourceDisksFiles]`, under its own
`AGENTS.md`; the x64 one names `stor98.sys` alone. No NT install copies
or stores `stor98.mpd` (the engine's store comparison skips it,
`inst_store_verdict`, `runs/run-43-24.md`), but the package is held to
the INF's whole list: the engine's package check (`s98_check_package`
walks every named file), the cut's family check (`Test-FamilyLayout`,
section 5.2, which reads each INF's named files beside it; Stor98's own
gate, `check-stor98-inf.ps1`, reads the source INFs alone and is
unchanged) and a by-hand Have Disk expect it beside the INF. So `nt-x86\stor98\` keeps `stor98.mpd`
for the package's completeness, as `release-x86\` did. Removing the line
from Stor98's INF is Stor98's change, not this task's (section 12, Q3).
`98-me\stor98\` carries `stor98.mpd` because its INF installs it;
`nt-x64\stor98\` none.

### 3.4 The 9x Stor98 INF under its new name

On `98-me\` the 9x INF (`src\stor98\inf\stor98-9x.inf`) ships as
`stor98\stor98.inf`, no longer `stor98w9.inf` (owner, 2026-10-30): with
no NT Stor98 INF in the folder, there is nothing to tell it apart from.
The x86 NT INF ships as `nt-x86\stor98\stor98.inf` as before. The bytes
of both are unchanged (their comments name `stor98w9.inf` and the "x86\"
folder; they are Stor98's own files and their comments are refreshed by
Stor98's next change, not here; section 12, Q4).

What reads the name:

- **The engine's package check** (`s98_check_package`): on 98 SE and ME
  it reads `98-me\stor98\stor98.inf` and requires its class match (the
  rule of record 31 section 5.2, unchanged); on NT `nt-x86\stor98\` or
  `nt-x64\stor98\stor98.inf`. The `stor98w9.inf` lookup goes; a family
  folder that has a `stor98w9.inf` is not this layout and is refused
  (section 6.4).
- **The local copy on 98 SE and ME** (the install directory, record 23
  section 9.6) takes the 9x INF as `stor98.inf`. A `3.3.0.0` install
  directory holds `stor98w9.inf` (the 9x INF) and, when Stor98 was
  ticked, `stor98.inf` (the NT INF, which the 9x engine reads on
  `DevicePath` as it reads every INF there, and whose undecorated model,
  the tunnel id `PCI\XHCI98_TUNNEL&CC_010802`, no 9x device carries, so
  98 SE and ME never install from it). The update's order is record 23 section
  9.6's (the new files staged in `XHCIINST.NEW`, the directory off
  `DevicePath`, the old list's files deleted, the new moved in): the old
  list names both, so both go and the new `stor98.inf` (the 9x text)
  takes the name. Nothing else in the install directory changes name.
- **The setup engine's copies in `INF\OTHER`** (SETUPX's
  `<Provider><name>` copies, "Yeo Kheng Mengstor98w9.inf", with their
  `OEM Name` values). The 9x INF refresh (`s98_inf_refresh_9x`, record 31
  section 7.4) finds each copy through the Stor98 driver keys' `InfPath`,
  not by name, and writes the bundled text into it, so a `3.3.0.0` copy
  keeps its name and gets this release's text. A drive plugged after the
  update may make a second copy under the new name, "Yeo Kheng
  Mengstor98.inf". Stor98's copies are never the general 9x INF sweep's
  (`infs_9x` leaves every INF holding Stor98's fields to Stor98's own
  record, `inst_core.c`'s classifier); each copy a run stages or adopts
  is written to Stor98's `Staged` value by its exact name and alias, and
  the uninstall (`s98_un_inf_9x` over `Staged`) deletes it with its
  `.PNF` and forgets its `OEM Name` value by that name, whatever the name
  is. So a `3.3.0.0` copy recorded as `...stor98w9.inf` is removed by its
  record, and a copy under the new name is recorded when a run adopts it
  (the adoption of drives bound after the run, record 26 section 9.3), as
  any Stor98 copy is. Nothing in the record or the uninstall reads the
  package's file name; the 9x legs read both names (section 10, L1 and
  L10).
- **The mark service** (`xhcimark`, record 31 section 6) reads Stor98's
  published INF on 8.1 and 10 (`%windir%\INF\oemN.inf`, `mk_s98_inf_ok`)
  and never a package's name: no change.
- **The NT install** stages `stor98.inf` with `SetupCopyOEMInf` from the
  local copy, whose name for it is unchanged: no change.

### 3.5 The catalogs (record 19 section 3a amended)

The x64 catalogs are made and checked where they are today: in the flat
staged package `make-package.ps1` writes (section 5.1), where the filter
INF is `xhcicmf.inf` at the root beside `xhcicm.sys`, by
`New-PackageCatalogFile` (WDK 7.1's MakeCat) and `Test-CatalogOfPackage`,
which find each catalog through `$script:PackageCatalogs`' `Inf` and
check its members by name and hash. So `xhcicmf.cat` is made with its INF
member named `xhcicmf.inf`, the installed name: the member the
installer's staging of the local copy's `xhcicmf.inf` verifies. The
cut's re-lay (section 5.1) then writes the filter to `cm\xhcicmf.inx`
(the same bytes, compared by hash across the re-lay) and `xhcicmf.cat`
beside it; the family table records the pair (disk name
`cm\xhcicmf.inx`, catalog member and installed name `xhcicmf.inf`), and
every later check of a published tree reads the catalogs through the
flat view the table rebuilds (`ConvertFrom-FamilyLayout`, section 7.2),
where the member name and the file name agree again. The x86 folders
carry no catalog, as before.

The test certificate's public half, `xhci98-test.cer`, stays at
`nt-x64\`'s root beside `xhci98.inf`, the place the readme names.

### 3.6 `txtsetup.oem`'s paths

Both files name their files at the disk's root: `[Disks]` `d1 = "...",
\xhci98.sys, \` (and `d2` for the 2000 option's three CD files), and
`[Files.scsi.*]` lines `xhci98.sys`, `xhci98.inf` and, in the x86 file,
`xhcihlpr.sys`. The F6 floppy is the family folder's root: copy
`nt-x86\`'s (or `nt-x64\`'s) root files to a floppy, without `cm\` and
`stor98\`. Every file `txtsetup.oem` names is at that root in the new
layout, as it was at `release-x86\`'s and `release-x64\`'s, so **neither
file changes**. `nt-x64\`'s `txtsetup.oem` is staged from
`src\txtsetup-amd64.oem`, read by XP x64 and Server 2003 x64; Vista and
later never read a `txtsetup.oem`. The INF gate's `check-txtsetup-oem.ps1`
`-PackageDir` check reads the family folder's root (section 5.2). The
root files of `nt-x86\` (about 0.5 MB) fit a 1.44 MB floppy.

## 4. Integration: what a tool takes from each folder

An integration tool pointed at a family folder with recursion (nLite's
and NTLite's driver folders, `DISM /Add-Driver /Recurse`) takes every
`*.inf` under it:

| Folder | INFs taken | Not taken |
|---|---|---|
| `nt-x86\` | `xhci98.inf`, `xhciuas.inf`, `cm\xhcicm.inf`, `stor98\stor98.inf`; with S10, `cm\tbnet\`'s INF | `cm\xhcicmf.inx` (the filter) |
| `nt-x64\` | the same, with each `.cat` | the same |

So an integrated system has the controller, the UAS driver, the NHI's
connection manager and Stor98's model lines in its driver store or
`OemDir`, and no filter. What each brings at the first boot:

- `xhci98.inf` binds the xHCI controller and its root hub, writing every
  value the driver reads at its default (owner rule, 2026-10-30, task
  45.50); `xhciuas.inf` binds a UAS function.
- `xhcicm.inf` binds a Thunderbolt or USB4 NHI with `xhcicm.sys`, writing
  its values at their defaults, `XhciCmNet` 0 (record 35). This is an
  integration the owner's layout allows on purpose (the connection
  manager is in `cm\`, beside its filter); `INTEGRATE.TXT` says that a
  person who does not want it leaves `cm\` out. Without the filter a
  tunnelled device enumerates as Windows does with no filter (record 22),
  until `SETUP.EXE` installs the filter.
- `stor98.inf`: on 2000 to 8 its model matches the tunnel id alone,
  which only `xhcicm`'s filter gives a device, so with no filter it binds
  nothing; on 8.1 and 10 its class model ranks below `stornvme` (record
  31 section 4.8). So integrating `stor98\` changes nothing at the first
  boot of an internal-drive system.
- `xhcicmf.inx`: never.

What `SETUP.EXE` still does after an integrated install (the text of
`INTEGRATE.TXT`, section 8.2): XHCIPFW, placed by the installer alone
(record 32): `XHCIPFW.VXD` on 98 SE and ME, `XHCIPFW.SYS` on 32-bit 2000
to 7 and, as a filter of the PCI root bus, on 32-bit 8 and 8.1 (10 does
not initialise it there, `inst_core.c`'s system table, run-45 "45.9"); S98VSD on 98 SE and ME (record 30), the mark service (`xhcimark`,
record 31 section 6), the connection manager's tunnel-port filter, its
start type put back (record 23 section 5.14), the tools (`XHCITR`, the
tray), the Add/Remove Programs entry, and the settings it asks. A run of
`SETUP.EXE` over an integrated system finds the drivers already on the
devices: it is an update over the same version, and adds what the
integration left out.

98 SE and ME have no integration route; their folder serves the hardware
wizard and `SETUP.EXE`.

## 5. The packager and the gates

### 5.1 `make-package.ps1` stays flat; the cut re-lays

`make-package.ps1` keeps staging one flat package per flavour and
architecture, `out\pkg-<flavour>-<arch>\`, exactly as today (the x86
filter in `nt\` with its `nt\xhcicm.sys`, the x64 filter at the root, the
x64 catalogs made there), with every gate it runs today. That package is
the build's output and the VM matrix's and the leg kits' input
(`scripts\vm-matrix\lib\fresh.ps1`'s `Get-QemuPackageProblem`,
`prepare-image.ps1`, `run-matrix.ps1` and their self-tests read its root
INF and image), and none of them changes. The `qemu` flavour's packages
are never cut, so they stay flat.

**The family layout is the cut's**: `make-release.ps1` re-lays each
flat package into its family folders as it publishes (section 7.1), by
one table in `package-common.ps1`, `$script:FamilyLayout`: per flat name,
the family folders and the path in each (root, `cm`, `stor98`,
`cm\tbnet`), with the renames `nt\xhcicmf.inf` (x86) and `xhcicmf.inf`
(x64) to `cm\xhcicmf.inx` and `stor98w9.inf` to `98-me\stor98\stor98.inf`,
and the dropped `nt\xhcicm.sys` (the same bytes as `xhcicm.sys`, which
`CM-NO9XBRIDGE` already compares). Two functions read it:
`ConvertTo-FamilyLayout` (flat to families, every flat file placed or
named as dropped, so a file the table does not know is a refusal) and
`ConvertFrom-FamilyLayout` (families to a flat view, a file present in
two families required to be the same bytes, `nt\xhcicm.sys` rebuilt from
`cm\xhcicm.sys`). The engine's source map (section 6.2) is the same
table's local-name side, and a host vector holds the two equal.

So the family filter of each INF's `[SourceDisksFiles]` (section 3.1)
is the table's: `-EmitMediaLayout` keeps emitting the INF's whole list
for the flat staging, and the table decides which family folder carries
each name.

### 5.2 The INF gates

- `check-inf.ps1 -PackageDir` and the UAS, CM and txtsetup gates'
  package halves (Stor98's gate has none: it reads the source INFs) keep
  reading the flat package (`make-package.ps1`'s,
  and the flat view the cut rebuilds from a published tree), as today.
  The family folders get one check of their own, `Test-FamilyLayout` in
  `package-common.ps1` (`PKG-FAMILY`): every file where the table puts it
  and nothing else, each INF's family-filtered `[SourceDisksFiles]`
  present beside it (section 3.1), a shared file the same bytes in both
  x86 families, and `cm\tbnet\` holding only what the table lists.
- **The CM gate's `nt\` rule is replaced by the `.inx` rule** for the
  published layout (`CM-INX`, `check-cm-inf.ps1`'s package half taking a
  family folder as well as a flat package): in an NT
  family folder `cm\xhcicmf.inx` is present, is the bytes of the gated
  filter INF, sits beside `cm\xhcicm.sys`, and no file anywhere in the
  family folder is named `xhcicmf.inf`; no family folder has an `nt\`;
  `98-me\` holds no `xhcicmf.*`. `CM-NO9XBRIDGE`'s INF half (the
  filter's `$Windows NT$` signature, no undecorated install section
  98 could run, no `$CHICAGO$` INF binding a bridge) stays as it is.
- `PKG-MSFILE` and the packager's "nothing it did not stage" rule read
  every folder of the layout, recursively.
- `BOTH-CATALOG` (every x64 INF a catalog of its own) reads each INF's
  `CatalogFile=` beside it, `xhcicmf.cat` beside `xhcicmf.inx`.

### 5.3 `test-package.ps1` and the regression fixtures

The fixtures that stage a flat package are unchanged. The ones that cut a
release or rebuild an upload set read the family folders: the cut's
stand-in releases (`-DriverPath` text stand-ins), a converted image
changed after the conversion refused by `Assert-FamilyFromLegs`, the
catalog cases (the
`.inx` read through the flat view), the `-UploadSetOnly` rebuild of a
published version (`3.3.0.0` and earlier at their published layout,
section 7.2), and new cases: `ConvertTo-FamilyLayout` and
`ConvertFrom-FamilyLayout` round trips on an x86 and an x64 stand-in; a
flat file the table does not know refused; a family folder holding a
file outside its family list, an `nt\`, an `xhcicmf.inf`, a
`stor98w9.inf`, a `98-me\` NT-only file or an `nt-x86\` without the
helper refused; two families' copies of a shared file differing refused;
`cm\tbnet\` absent accepted and a stray file under it refused until S10
lists its files; `INTEGRATE.TXT` held to the table.

## 6. The installer engine

### 6.1 The local copy keeps its shape

The engine copies the package into a local copy (on NT a content-tag
subfolder of the install directory; on 98 SE and ME the install directory
itself, named on `DevicePath`, record 23 section 9.6, owner 2026-10-16:
"no subfolder") and installs from it. **That copy keeps `3.3.0.0`'s
shape**: flat, with on x86 NT the filter as `nt\xhcicmf.inf` and its
`nt\xhcicm.sys`, on x64 the filter at the root. Only where the engine
reads each file in the download changes. So the content tag, the copy's
lists, the update's order, the uninstall's removal of earlier copies,
the 98 SE and ME `DevicePath` entry and the setup engine's records of
every earlier release keep working as they are, and an update over
`3.3.0.0` changes no local name but the 9x Stor98 INF's (section 3.4).

### 6.2 The source map

`pkg_src(rel)` today joins the flavour directory and the local name. It
becomes a map from the local name to the download's path, one table
(`instPkgMap`, in `inst_core.c`, a host vector holding it against
`$script:FamilyLayout`):

| Local name | `98-me\` | `nt-x86\` | `nt-x64\` |
|---|---|---|---|
| `xhci98.inf`, `xhci98.sys`, `xhciuas.inf`, `xhciuas.sys` | root | root | root |
| `xhcihlpr.sys`, `txtsetup.oem` | - | root | `txtsetup.oem` root |
| `*.cat` of the root INFs, `xhci98-test.cer` | - | - | root |
| `xhcicm.inf`, `xhcicm.sys` (and x64 `xhcicm.cat`) | `cm\` | `cm\` | `cm\` |
| `nt\xhcicmf.inf` | - | `cm\xhcicmf.inx` | - |
| `nt\xhcicm.sys` | - | `cm\xhcicm.sys` | - |
| `xhcicmf.inf` (x64), `xhcicmf.cat` | - | - | `cm\xhcicmf.inx`, `cm\xhcicmf.cat` |
| `XHCIPFW.VXD` / `XHCIPFW.SYS` | `cm\` | `cm\` | - |
| `stor98.inf` | `stor98\stor98.inf` (9x) | `stor98\` | `stor98\` |
| `stor98.mpd`, `S98VSD.VXD` | `stor98\` | `stor98.mpd` `stor98\` | - |
| `stor98.sys`, `stor98.cat`, `stor98-*.txt` | texts | `stor98\` | `stor98\` |

`package_files` walks the map, not a directory listing, so a file the map
does not name in a family folder is never copied, and a mapped file that
is missing is the package check's refusal as today. Every rule of
`package_files` on which file enters the copy (the UAS pair left out, the
connection manager alone, Stor98 only when ticked, the Thunderbolt
files on a No) applies to the local name as now. `infRel[]` keeps the
local names; a second array, `pkgRel[]`, holds each INF's path in the
family folder, and `inf_source` resolves an INF's `[SourceDisksFiles]`
in that INF's own download folder when its base is the package and in
the local copy (flat) otherwise. `pkg_src` keeps its override for NUSB
3.3's seven (`storPackDir`) ahead of the map. `cm\tbnet\` is not in the
map until S10 (section 9); a file there is ignored by this release's
engine.

**The reads that bypass `pkg_src`.** Four places read the package by a
name at the flavour directory's root and are moved to the map, detection
and placement both: `vxd_check_package` (S98VSD and XHCIPFW's VXD,
`98-me\stor98\S98VSD.VXD` and `98-me\cm\XHCIPFW.VXD`) and its placement
copy; `pfwsys_check_package` (`nt-x86\cm\XHCIPFW.SYS`) and its placement
copy. Without that they would read the root, find nothing and take the
file as not bundled, silently. A host vector holds every name the engine
joins to `pkgDir` to be a row of the map (the four, the INFs,
`xhcicm.sys`, `xhcicm.cat`), and the legs read XHCIPFW and S98VSD placed
(section 10).

**The family filter in the package check.** `check_package` walks each
INF's `[SourceDisksFiles]` and requires every named file; it now skips a
name the map gives no place in this family (`xhcihlpr.sys` on 98 SE and
ME, section 3.1) and requires the rest at the map's place, so `98-me\`
passes without the helper and `nt-x86\` is refused without it.

### 6.3 Which folder

The x86 engine reads `98-me\` on 98 SE and ME and `nt-x86\` on NT; the
x64 engine `nt-x64\`. `/dir` takes `98-me`, `nt-x86` or `nt-x64`, refused
on a system or architecture that is not its family's, with the
mixing-architectures text of today; `release-x86` and `release-x64` are
refused with "this package has no <name>; it is laid out by Windows
family (98-me, nt-x86, nt-x64)". `INSTALL.BAT` sets `98-me` on 98 SE and
ME and `nt-x86` or `nt-x64` on NT, and its "run from the top" text names
the three folders. `XHCIWIZ.EXE` and `SETUP.EXE` take no folder (the
engine's default, its family's). The window's and console's
"Package/Directory" lines print the family folder.

### 6.4 The package check

`check_package` refuses a family folder that is not this layout: a
`stor98w9.inf`, an `nt\`, or an `xhcicmf.inf` in it ("this folder is not
this release's layout: <file>"). On NT a filter is `cm\xhcicmf.inx`; the
Thunderbolt triple is `cm\xhcicm.inf`, `cm\xhcicmf.inx` and
`cm\xhcicm.sys` together, on 98 SE and ME the first and third; the x64
connection manager's catalog refusal (record 23 D20 (a)) reads
`cm\xhcicm.cat`. Its texts name the family folder.

### 6.5 What reads the filter

On NT the engine stages the filter from the local copy's
`nt\xhcicmf.inf` (x86) or `xhcicmf.inf` (x64), as today: its model ids
(`inst_core.c`, held against `src\cm\xhcicmf.inf` by `test_inst`), its
`SetupCopyOEMInf`, its `OEM Name`-free NT record and the uninstall's
`pnputil`-free removal by the published name are unchanged. The x64
catalog is the local copy's `xhcicmf.cat`, copied with it.

## 7. The cut

### 7.1 `make-release.ps1`

The cut keeps its per-architecture assembly: each leg's flat package is
staged, gated and checked as today (the INF gates, the declared media,
the flavour marker and machine of every image, the x64 signatures and
catalogs, `Assert-UasPublished`, `Assert-CmPublished`,
`Assert-HelperPublished`), in a work folder of the cut beside the
version directory rather than as `release-x86\` and `release-x64\` of
the version. Then `ConvertTo-FamilyLayout` writes `98-me\` and `nt-x86\`
from the x86 leg and `nt-x64\` from the x64 one, `Test-FamilyLayout`
checks them, and **every file of the family folders is compared by hash
with the verified flat file it came from** (`Assert-FamilyFromLegs`: each
row of the table, a file present in both x86 families compared in each,
the renamed filter and 9x Stor98 INF included), after the conversion and
again just before the upload set is assembled and the version directory
swapped in, so a file changed between the checks and the publication is a
refusal; the readme's hashes are read from the family folders. The
family tree is built under the version directory's own staging place
(the swap of `make-release.ps1` stands), the flat work files outside it.
Then the version directory publishes the family folders with
the root (`SETUP.EXE`, `INSTALL.BAT`, `readme.txt`, `LICENSE`,
`INTEGRATE.TXT`) and `tools\` as before. The upload set, the readme's
contents list and the "unclaimed flavour directory" check name the
family folders. `-Arch` keeps its meaning (which architectures the cut
publishes); an x86-only cut publishes `98-me\` and `nt-x86\`.

**The x64 half needs the key, as before.** `make-release.ps1` refuses an
x64 cut without the test-signing key, with no switch past it, and that
stays: the catalogs and signatures of `nt-x64\` are made by
`make-package.ps1 -TestSignPfx` in the flat staging. A clone without the
key cuts x86 alone; its x64 legs take the unsigned flat package re-laid
by `ConvertTo-FamilyLayout` on the host (a leg kit's script calling it)
and sign in the guest (section 10).

### 7.2 Earlier versions

A published version is never edited (`releases\README.md`).
`-UploadSetOnly` rebuilds a published version's download from its tree at
the layout it was published with: the flavour directories up to
`3.3.0.0`, the family folders from `3.4.0.0`, decided by which the tree
holds (a tree with both, or with neither, is refused). For a family tree
`ConvertFrom-FamilyLayout` rebuilds a flat view in a work folder, and on
it run exactly the checks `-UploadSetOnly` runs today on a flavour
directory (the declared media, the catalogs, the upload set's own
checks, the architecture discovery and the Stor98 guard), never the
cut's build-origin asserts (`Assert-UasPublished`, `Assert-CmPublished`,
`Assert-HelperPublished`, which compare build outputs and source stamps
a reconstruction does not have): no build output and no key is needed,
as today. `Test-FamilyLayout` runs on the family tree itself, and the zip
carries the family folders.

### 7.3 The records

`downloads.txt`, `drvpack.txt`, `otherpkgs.txt` and `wdmfree-pin.txt`
are written to `releases\<version>\` by the cut, where the next cut reads
them, and the upload set leaves them out (`ReleaseRecordOnlyNames`, owner
2026-10-24): that already holds and is kept, and `test-package.ps1`'s
case for it reads the new layout. The pin's input stays
`scripts\package\wdmfree-pin.txt`; for `3.4.0.0` it names usb-wdm-free
v0.1.6 (`bc036ce`, uac2 0.1.6.0; the other packages and every system as
v0.1.5's), task 45.44's own commit.

## 8. The documents

### 8.1 The download readme, README, AGENTS.md, build-and-test.md

The readme `make-release.ps1` writes, `README.md`'s package section, the
release notes' install and "Manual install" paragraphs (handed to
45.final), AGENTS.md ("The INF and install media", the flavour-directory
paragraph, the x64 signing paragraph) and `build-and-test.md` (the
packaging, the install procedure, the q35 legs' package path) name the
family folders: which folder for which Windows, the hardware wizard
pointed at the root, `cm\` or `stor98\`, the F6 floppy from the root of
`nt-x86\` or `nt-x64\`. `tools\xhciinst\readme.txt` names `/dir`'s three
values. Record 26 section 2 and record 23 section 4.2 get one paragraph
each pointing here.

### 8.2 `INTEGRATE.TXT`

A plain-text file at the root (78 columns, CRLF, ASCII), written by
`make-release.ps1` from one template in `package-common.ps1`
(`Get-IntegrateText`), held by `test-package.ps1` against the layout
table: which folder for which Windows (the table of section 1); point
nLite, NTLite or DISM at that folder with recursion (DISM: `/Add-Driver
/Driver:<folder> /Recurse`, and on x64 `/ForceUnsigned`, since the x64
drivers carry the project's test signature, which loads only in
test-signing mode, record 19); for 2000 and XP's text mode, the F6
floppy from the folder's root (`txtsetup.oem`); that `cm\xhcicmf.inx` is
not an INF for integration and is installed by `SETUP.EXE` alone; that
leaving `cm\` out leaves the Thunderbolt and USB4 connection manager out,
and `stor98\` Stor98; and what `SETUP.EXE` still does after an integrated
install (section 4's list). It carries no testing-status wording.

## 9. Thunderbolt networking's place (task 45.59, step S10)

`cm\tbnet\` is a place in `$script:FamilyLayout` for each family, with no
files: the packager, the gates and the cut accept it empty and create no
empty folder (git keeps none, and a published empty folder says nothing),
and refuse a file there that the table does not list. S10 adds its files
to the table (record 35 section 9.3), and the engine's map with them; an
integration tool pointed at a family folder takes its INF unless `cm\tbnet\`
is left out, one folder.

**The 98 SE and ME search path, measured** (the task's check). Two paths
count:

1. **`DevicePath`.** The installed copy is flat (section 6.1): the
   install directory is one entry whatever the download's folders, so
   `cm\tbnet\`'s files, placed by S10 in the local copy beside
   `xhcicm.inf`, cost no entry. `C:\WINDOWS\INF;C:\XHCI98` is 24
   characters with the default install directory, leaving 256 on 98 SE
   (280) and 235 on ME (259) for the free and non-free sets' staged
   folders (record 26 section 6.7.13), as in `3.3.0.0`. Had the installed
   copy mirrored the download, one entry a folder, the default install
   directory would cost `C:\XHCI98` (9), `;C:\XHCI98\CM` (13),
   `;C:\XHCI98\STOR98` (17) and `;C:\XHCI98\CM\TBNET` (19) after
   `C:\WINDOWS\INF;` (15): 73 characters, leaving 207 on 98 SE and 186 on
   ME, about one staged package's folder fewer (43 to 65 characters
   each), and `cm\tbnet\` alone 19 of them. A longer install directory
   costs its length once flat and four times mirrored (`C:\Program
   Files\xHCI98` is 23: 38 flat, 129 mirrored). So the flat copy is
   kept, and the owner's fallback (S10's files loose in `98-me\cm\`) is
   not needed for `DevicePath`.
2. **The hardware wizard's "Specify a location"**, for a person
   installing by hand from the download. The deepest folder of `98-me\`
   is `cm\tbnet\`; unzipped on the 98 SE desktop the download's
   longest such path is
   `C:\WINDOWS\DESKTOP\xhci98-3.4.0.0-nonfree\98-me\cm\tbnet`, 56
   characters (its short form `C:\WINDOWS\DESKTOP\XHCI98~1.0-N\98-ME\CM\TBNET`
   is 46). Before S10 the folder is empty, so the legs read it with a
   stand-in: on 98 SE and ME the download unzipped at a location of 120
   characters, `xhcicm.inf` and `xhcicm.sys` copied into its
   `98-me\cm\tbnet\`, and the Update Driver wizard of the NHI (the
   emulator's) pointed at that folder by Specify a location, its full
   path typed: the wizard finds the INF there (section 10, L1 and L2).
   If it refuses that depth, the fallback is the owner's: on `98-me\`
   alone S10 places the files in `cm\`.

**Record 35 section 9.3** assumed one `DevicePath` entry per folder
(`cm\tbnet\` adding one). With the flat installed copy (Q1 (a)) it adds
none; S10 amends record 35 to that when it places the files, or to the
mirrored model if the owner answers Q1 (b).

## 10. The legs

One gated build, then an x86 test cut of this tree (`98-me\`,
`nt-x86\`, the root and `tools\`) and, for the x64 legs, the unsigned
flat amd64 package re-laid into `nt-x64\` by `ConvertTo-FamilyLayout`
and signed in the guest with a throwaway certificate, as task 43.36 did
(the packager's own signing and catalogs in a key-holding cut are the
owner's cut's to read, and owed), each on a fresh q35 overlay, each read for
the registry defaults (the controller's key holds every value of
`tuning.md`'s tables at its default) and for no CM filter bound before
`SETUP.EXE` (no `xhcicm` in any bridge's `UpperFilters`, no `xhcicmf`
in the driver store or `INF`, `Services\xhcicm\Enum` naming no bridge):

| Leg | System | What |
|---|---|---|
| L1 | 98 SE | `SETUP.EXE` from the download, the Thunderbolt box ticked; `DevicePath` read (its length, one install directory entry); the 9x `stor98.inf` in the install directory; the wizard's deepest path (section 9, item 2) |
| L2 | ME | the same as L1 |
| L3 | 2000 SP4 | `SETUP.EXE`; and the F6 option from a floppy made of `nt-x86\`'s root (the `XHCI98W2K` option with the CD's three files) |
| L4 | XP SP3 | `SETUP.EXE` |
| L5 | 7 x86 | `SETUP.EXE` |
| L6 | XP x64 | `SETUP.EXE`; and the F6 option from a floppy made of `nt-x64\`'s root |
| L7 | 10 x64 (or 8.1 x64) | `SETUP.EXE`, signed in the guest |
| L8 | XP SP3 nLite | `nt-x86\` integrated into an XP SP3 source with nLite (run inside a guest), installed through text mode and GUI setup; the drivers bound at the first boot; no filter; then `SETUP.EXE` |
| L9 | 7 x64 DISM | `nt-x64\` added with DISM `/Recurse` (inside a guest) to a 7 x64 `boot.wim` and `install.wim`, installed; the drivers in the store, no `xhcicmf`; then `SETUP.EXE` |
| L10 | XP and 98 SE | an update over `3.3.0.0`'s flat package: the local copy, `stor98w9.inf` gone and `stor98.inf` the 9x text on 98 SE, every user value kept |

## 11. Host vectors and checks

- `test_inst.c`: the source map's rows per family, held against a copy of
  `$script:FamilyLayout` the packager's self-test writes; `/dir`'s
  three values accepted on their family and refused off it, the old
  names refused; the 9x Stor98 copy recognised by text under both names.
- `selftest.cmd` and the window's `/selftest`: `/dir release-x86`
  refused, `/dir nt-x86` taken.
- `test-package.ps1`: section 5.3's cases; `INTEGRATE.TXT` held to the
  layout table; the download readme naming the three folders.

## 12. The owner's questions

- **Q1. The installed copy on 98 SE and ME: flat, or one folder a
  download folder?** (a) Flat, as today: one `DevicePath` entry, the
  install directory the user types into the wizard (the owner's rule of
  2026-10-16), S10's networking files in it beside `xhcicm.inf`, no
  budget spent. (b) Mirror the download: one entry per folder, 73
  characters with the default directory, about one staged package fewer.
  Recommended: (a).
- **Q2. `cm\tbnet\` before S10:** (a) a place in the tables, no folder
  published until S10 fills it; (b) an empty folder in the download now
  (it cannot be tracked in `releases\` by git; the zip would carry it).
  Recommended: (a).
- **Q3. `stor98.mpd` in `nt-x86\stor98\`:** (a) kept, because the x86
  `stor98.inf` names it and the package is held to every named file
  (section 3.3), though no NT install copies it; (b)
  removed with its line in Stor98's x86 INF, a Stor98 change with its own
  legs on Vista to 10 x86. Recommended: (a) for `3.4.0.0`.
- **Q4. Stor98's INF comments naming `stor98w9.inf`:** (a) left for
  Stor98's next change (no byte of its INFs changes here); (b) refreshed
  now, which changes the INFs' bytes and their gate's recorded digests.
  Recommended: (a).
- **Q6. The helper on 8 to 10 x86:** `xhci98.inf` would install it
  there when the USB drivers are ticked past the installer's warning
  (section 3.1). (a) leave the INF as it is, as `3.3.0.0` has it; (b)
  bound the INF's NT 6 path to Vista and 7, an INF change with its own
  legs. Recommended: (a).
- **Q5. `INTEGRATE.TXT` and `cm\`:** the connection manager's INF is
  integrated with the rest unless `cm\` is left out (section 4). (a) say
  so, leave it to the user; (b) move `xhcicm.inf` out of the integrated
  set too (as `.inx`), so only `SETUP.EXE` installs the connection
  manager. Recommended: (a), the owner's layout puts it there to be
  integrated.

## 13. Files

`scripts\package\package-common.ps1` (`$script:FamilyLayout`,
`ConvertTo-FamilyLayout`, `ConvertFrom-FamilyLayout`, `Test-FamilyLayout`,
`Get-IntegrateText`), `make-release.ps1`, `test-package.ps1`,
`cut-builds.ps1` (its package paths), `wdmfree-pin.txt`;
`scripts\inf-gate\check-inf.ps1`, `check-cm-inf.ps1`,
`check-uas-inf.ps1`, `check-stor98-inf.ps1`, `check-txtsetup-oem.ps1`;
`scripts\vm-matrix\leg\LEG9X.BAT` (`/dir 98-me`) and
`scripts\vm-matrix\lib\iso.ps1`'s comment (the matrix's flat
`out\pkg-*` readers unchanged, section 5.1);
`xhciinst\inst_eng.c`, `inst_core.c`, `inst_core.h`, `INSTALL.BAT`,
`selftest.cmd`, `README.TXT`; `test\test_inst.c`; `AGENTS.md`,
`README.md`, `docs\contributing\build-and-test.md`, records 23 and 26
(one paragraph each), this record, `docs\contributing\design\README.md`;
the roadmap's 45.44 line and `runs\run-45.md`'s "45.44".
