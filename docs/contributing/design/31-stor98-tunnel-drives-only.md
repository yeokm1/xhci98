# Design Record 31 - Stor98 for Tunnelled Drives Only: the Tunnel Id

Status: draft 7, 2026-10-27: section 6.2, on 2000 and XP the
safe-removal mark's restart sets a plain driverless drive's install to
run again, as the tag's restart does (task 44.52, owner 2026-10-27).
Draft 6, 2026-10-25: Q1 settled by its reading - the class match
kept on 98 SE and ME through a separate INF, `stor98w9.inf` (section 5.2,
owner: option a2); the id lost at a restart of the device's own stack,
read on 8.1, and its design, section 4.8, put to the owner with the scope
options (not coded); the second review's points. Draft 5, 2026-10-25, reviewed and revised: the owner's decisions
on Q1 to Q3 carried through every section (the 9x worker's two steps and
their lifetime, `Write_Registry_Value`'s contract, the mark service's
unit restart on NT 5.x with its budget and gate, the 8.1 arm as 8.1's
ordinary route, Q1's fallback shape stated where it would change a
rule), the readings done and owed listed apart, and the stale text of
drafts 1 to 4 rewritten. Draft 4, 2026-10-25: the owner's decisions on
Q1 to Q3 (section 10). Draft 3, 2026-10-25: the readings of section 9.3
taken (section 9.4). Draft 2, 2026-10-25, reviewed and revised (the
`Removable` gate for a kernel-attached child on a bridge that is not
active; the 8.1 arm of the fallback; the installer's candidates found by
class, its earlier INF removed from the store at an update, the
per-device route's class selection; the id list from paged pool; the
rank after the insertion `0x2006`; the earlier INF's removal on 2000 and
the 9x refresh's orphaned copies). Draft 1, 2026-10-25, before code.
Written for `roadmap-usb4.md` tasks 44.2 and 44.3, release `3.3.0.0`,
from the owner's lean of 2026-10-24 and 2026-10-25 (a custom device id
that only a tunnelled drive is given, so that Windows binds Stor98 to
tunnelled drives alone) and the owner's decisions of 2026-10-25 (section
10), in their current form:

- the id is **structural** on Windows 2000 and later: every NVMe-class
  function below a bridge the tunnel-port filter INF names as a
  Thunderbolt or USB4 tunnel port carries it, at any depth, decided by
  the filter alone at enumeration, with no wait for the connection
  manager's verification of the port (section 3.4);
- on Windows 98 SE and ME the connection manager's CONFIGMG worker adds
  the id with CONFIGMG's `Add_ID` service, a statement only: their
  wizard does not search again for a drive whose first search found
  nothing, so Stor98 installs there by its class match, from an INF the
  installer stages on those two systems alone, `stor98w9.inf` (section
  5; Q1, options (a) and (a2), after section 9.3 item 9's reading);
- the id goes first among the device's **compatible** ids on NT (on 9x
  `Add_ID` appends it), and on 2000 to 8 Stor98's INF matches it alone;
  8.1 and 10 keep `3.2.0.0`'s model, the class as the compatible id
  (scope B, section 4.8), so on 8.1 the mark service's 8.1 arm, which
  takes a tunnelled drive off `stornvme`, is the route (sections 4.7 and
  6.1; D3, Q3; an 8.1 fix is sought);
- Windows 10 keeps task 43.59's policy: Windows' own `stornvme` is the
  default there and Stor98 an opt-in per drive (section 4.7);
- a tunnelled drive that arrives on NT without the id (section 4.4) is
  covered by the mark service where it can be: on Vista to 8.1 it
  installs Stor98 on that one device by model selection from the staged
  INF, within seconds; on 2000 and XP, where a service cannot install the
  unsigned package, it restarts the enclosure's unit once its bridges
  are installed so the drive comes back with the id for Windows' own
  wizard (Q2); the installer, which runs in the user's session, installs
  the drives present at its run per device by model selection on every
  NT system (sections 6.2 and 7).

Task 44.3 is the code, the host vectors and the legs; the readings of
section 9.3 come before each part's code (section 9.4 lists those taken
and those owed).

## 1. What it is for

### 1.1 The problem

Stor98's two INFs (`src\stor98\inf\stor98-x86.inf`, `stor98-x64.inf`,
staged as `stor98.inf`) match every NVMe controller by its class,
`PCI\CC_010802`. The package stages the INF so that a tunnelled drive
that appears after the install - the usual case, since the tunnel may
come up only after the restart - gets Stor98 with no further action.
The same match makes Windows offer Stor98 for an **internal** NVMe
drive, which Stor98 is not for (its `AGENTS.md`: meant to be used with
xhci98 only, robust against surprise removal behind a tunnel; the owner's
rule, record 23 section 4.4 and record 26 section 5.3.1: never on an
internal drive). Today that is held off by text and by the installer's
own selection only:

- on 2000 to 7, Windows offers the staged Stor98 for an internal NVMe
  drive that has no driver, and the release notes, `README.md` and the
  package's readme say to decline it (record 23 section 4.4, "What
  remains"; `make-release.ps1`'s readme text);
- on 8, which has no NVMe driver of its own, Windows itself puts the
  staged Stor98 on an internal drive that has no driver;
- on 8.1 and 10, since task 43.59, the INF names `PCI\CC_010802` as a
  compatible id only, so `stornvme` outranks it on an internal drive;
  but the 8.1 x64 and 10 x64 reading of 2026-10-23 (`runs/run-43.md`,
  "43.59") found that a **catalogued, test-signed** Stor98 ranks as
  "Authenticode" against `stornvme`'s "Inbox", equal at `00FF2005` when
  both match the class, and wins by its newer date: Stor98 took an
  internal NVMe hot-added on the root bus on both systems. The
  compatible-id-only model line closed that for the class match; it is
  the only thing between Stor98 and an internal drive there.

The installer selects tunnel drives only (record 23 section 4.4:
behind a tunnel, internal, unknown), and that holds for the drives
present at its run. It cannot hold for a drive plugged in later, when
the installer is not running: there Windows chooses, and Windows chooses
by id.

The owner's answer (2026-10-24, 2026-10-25): give a tunnelled drive an
id of this project's own, and have Stor98's INF match that id and
nothing generic. Then Windows' own matching does the gating, at the
plug, on every system, with no installer and no text asking the user to
decline.

### 1.2 Scope

- One id, section 3, carried by an NVMe-class PCI function below a
  Thunderbolt or USB4 tunnel port, and by nothing else.
- Windows 2000 and later: the id given by the connection manager's
  tunnel-port filter and its child filters at enumeration (section 4).
- Windows 98 SE and ME: the id given by the connection manager's CONFIGMG
  worker as a statement, and Stor98 installed by the class match of a
  9x-only INF (section 5).
- Stor98's INFs matching the id alone, with the per-system ranking that
  keeps task 43.59's policy on 8.1 and 10 (section 4.7).
- The mark service: its 8.1 switch kept as 8.1's arm; for a drive that
  arrived without the id, a per-device install on Vista to 8.1 and a
  restart of the enclosure's unit on 2000 and XP (section 6).
- The installer: present drives installed per device by model selection;
  the shared-id refusals gone (section 7).
- The texts that say "Windows may still offer Stor98 for an internal
  drive" (section 8).

### 1.3 Not in scope

- Stor98's driver code. Nothing in `src\stor98\src` changes; the INFs
  do, under Stor98's own rules (its `AGENTS.md`: no "NVMe" in a product,
  file, service or display name, which the id respects).
- A drive already bound. Windows does not rank again for a configured
  devnode: an internal drive a user put on Stor98 by hand stays there,
  and a tunnel drive bound under `3.2.0.0`'s class match stays bound
  (section 4.6). The installer's shared-file rules for another Stor98
  (record 23 section 4.4) stand.
- The connection manager not being a boot driver (task 44.32). This
  record needs a filter of this driver's in the tunnel port's stack
  (section 4.5 says why); 44.32's "no bridge filter" option would take
  the id's vehicle away and is excluded by this record unless it gives
  the id another one.
- Booting from a tunnelled drive. Stor98 is demand-start and data disks
  only (its INFs' header); nothing here changes that.
- Other tunnelled classes (a dock's xHCI, Ethernet). The id names the
  NVMe class; another class can take an id of the same form later, but
  none is given now (section 3.3).

## 2. What exists today (the readings this record rests on)

### 2.1 Stor98's INFs and the ranking

`stor98-x86.inf`: `[Manufacturer]` is `%Mfg%=Stor98_Models,NTx86.6.3`;
`[Stor98_Models]` gives `PCI\CC_010802` as the hardware id (98 SE, ME,
2000 to 8), `[Stor98_Models.NTx86.6.3]` gives it as the compatible id
with the hardware id empty (8.1 and 10). `stor98-x64.inf` is the same
with `NTamd64` and `NTamd64.6.3`, plus `CatalogFile=stor98.cat`.
`scripts\inf-gate\check-stor98-inf.ps1` holds exactly that shape: one
`[Manufacturer]` line with exactly those decorations, so that no later
decoration brings the class match back.

Windows' ranking (record 21 section 7.5.1, from Microsoft's "How Windows
Ranks Driver Packages" and its companions, read 2026-10-22): a rank is
`0xSSGGTHHH`, the lowest wins; `SS` the signature score, `GG` the feature
score, `THHH` the identifier score. A device hardware id matching a
model's hardware id scores `0x0000` to `0x0FFF`; a device hardware id
matching a model's compatible id `0x1000` to `0x1FFF`; a device
compatible id matching a model's hardware id `0x2000` to `0x2FFF`; a
device compatible id matching a model's compatible id `0x3000` to
`0x3FFF`; within a band, the id's position in the device's list is added.
Date and version decide only between equal ranks. Read on 8.1 x64 and 10
x64 (`runs/run-43.md`, "43.59", 2026-10-23): `stornvme.inf`'s one model,
`PCI\CC_010802`, against the sixth compatible id `pci.sys` gives an NVMe
function, ranks `00FF2005`; a catalogued test-signed Stor98 matching the
same id as a hardware id ranked the same `00FF2005` with signer score
"Authenticode" against "Inbox", and was selected by its newer date.
With the id as the model's compatible id (task 43.59's INFs) Stor98's
rank is `0x3005` and `stornvme` wins. So on 8.1 and 10 a test-signed
catalogue ranked "Authenticode" and tied, and the identifier band
decided - **with the project's test certificate imported into the
guests' Root and TrustedPublisher stores** (`runs/run-43.md`, "43.59":
"The certificate was added to Root and TrustedPublisher in each guest;
no publisher prompt appeared"), which record 19 section 3 says a user
need not and should not do. Under the ordinary configuration, the
certificate not imported, the signature score of a test-signed catalogue
was unread then (record 21 section 7.5.1 inferred "not a trusted
signature"); read 2026-10-25 on 8.1 x64 (section 9.4): "Signer Score =
0x80000000", the package ranked unsigned, so `stornvme` binds first there
and the 8.1 arm of section 6.1 is the ordinary route.

The device's own ids: `pci.sys` gives an NVMe function the hardware ids
`PCI\VEN_v&DEV_d&SUBSYS_s&REV_r`, `PCI\VEN_v&DEV_d&SUBSYS_s`,
`PCI\VEN_v&DEV_d&REV_r`, `PCI\VEN_v&DEV_d`, `PCI\VEN_v&DEV_d&CC_010802`,
`PCI\VEN_v&DEV_d&CC_0108`, and the compatible ids
`PCI\VEN_v&DEV_d&REV_r`, `PCI\VEN_v&DEV_d`, `PCI\VEN_v&CC_010802`,
`PCI\VEN_v&CC_0108`, `PCI\VEN_v`, `PCI\CC_010802`, `PCI\CC_0108`
(Microsoft's "Identifiers for PCI devices" page, which `pci.sys` follows;
the list is what task 44.3's leg reads in Device Manager, and the rank
above shows `PCI\CC_010802` at position 5 of the compatible list on 8.1
and 10). Windows 98 SE and ME keep the same two
lists in the devnode's `Enum\PCI\...` key, `HardwareID` and
`CompatibleIDs`, written by `PCI.VXD`.

### 2.2 The installer's selection

Record 23 section 4.4 ("The bundled Stor98 driver: tunnel drives only",
revision 18 onward): Stor98 is in the run only with the Thunderbolt
component; where a drive sits is read from the connection manager's
confirmed reading (`tb_read_all`, `inst_s98_place`: behind a tunnel,
internal, unknown); only a present device behind a tunnel is eligible
(`inst_s98_eligible`); on NT one `UpdateDriverForPlugAndPlayDevices`
call reaches every device of a hardware id, so a tunnel drive sharing
its id with a drive the gate leaves is refused (`hwid_clear`, and the
"drives sharing it" refusal of `inst_eng.c`'s install loop); on 8.1 a
drive on `stornvme` is installed over with `INSTALLFLAG_FORCE` (record 26
section 5.3.1 case 3b). On 98 SE and ME a device Stor98 runs is never
installed again; a device with no driver behind a tunnel is installed by
the helper (record 27).

### 2.3 The connection manager's filters on NT

Record 21 sections 3.2, 6.2 and 7.4, as built in `src\cm\cm_filt.c` and
`cm_reenum.c`:

- **The bridge filter** (role 2): `xhcicm.sys` as the device-specific
  upper filter of every PCI-to-PCI bridge `xhcicmf.inf` names - the
  discrete controllers' switch ports, the integrated controllers' tunnel
  root ports, the USB4-only hosts' tunnel ports and the emulator's, every
  line `&CC_0604` (record 21 section 3.3, record 22 section 3.7). It is
  installed from the registry (`UpperFilters`), so it is in the bridge's
  stack from the bridge's start, at boot included (which is why the
  installer makes `xhcicm` BOOT_START, record 23 section 5.10; task
  44.32). It reads every `BusRelations` answer on its way back
  (`cmBridgeRelations`, `CmRegRelations`), keeps the snapshot, and on an
  **active** bridge (one its domain's NHI verified and assigned) with
  `XhciCmSafeEject` 1 attaches a child filter to each PDO new by a whole
  snapshot, before the request is completed (`cmChildCreate`,
  `CmPortsChildFirst`, `CmPortsChildAttach`): directly above the PDO, or
  above bus filters another driver already put there.
- **The child filter** (role 3), on the device at the top of the tunnel
  port's secondary bus - record 24 section 4.3's unit: an enclosure's
  controller, or the upstream port of an enclosure's or a dock's switch.
  Its one act is `Removable` in the `IRP_MN_QUERY_CAPABILITIES` answer.
  Since section 7.4 revision 3 the mark service also registers it on the
  unit's devnode as a device-specific `LowerFilters` value, and
  `CmChildAddDevice` attaches it at every rebuild of the unit's stack.
- **The timing that matters here.** The PnP manager sends a new PDO's
  `IRP_MN_QUERY_ID` requests to the top of its stack as it stands when
  the `BusRelations` answer is processed: a filter attached in the
  answer's completion is above the PDO for them. A filter the registry
  names (`LowerFilters`) arrives later, at `AddDevice`, after the ids
  are read and recorded. So only a filter attached at `BusRelations`
  can change what Windows records as a device's ids, and it must be
  there the first time the devnode is created.
- **The loss** (record 21 section 7.4 revision 2, read on 2000 and XP;
  section 7.5.2 (a)): a bridge's first-time driver install - the
  enclosure's upstream port on a never-seen enclosure - rebuilds the
  unit's stack from the registry and loses the kernel-attached child.
  On Vista and later that loss is unread.
- The connection manager's pool goes through `cm_pool.c` alone
  (`CmAlloc`, `CmFree`: `ExAllocatePoolWithTag`, nonpaged, and
  `ExFreePool`; the allowlists hold the pair to `cm_pool.obj`).

### 2.4 The connection manager's 9x worker and CONFIGMG

Record 21 section 6.1, as built in `src\cm\cm_cfgmg.c` and
`cm_cfgwk.c`: on 98 SE and ME no driver of this project ever sits on a
bridge devnode; the NHI driver's worker thread calls CONFIGMG's services
through the VMM dynalink. On a tunnel up it numbers any bridge the tunnel
brought up (section 6.1.1), calls `Reenumerate_DevNode` on the tunnel
port's devnode, and in a marking window (`CM_WK_MARK_MS`, 10 s) gives
each new child of an active member `DN_REMOVABLE`; design record 28's
no-IRQ window walks the member's descendants and already thunks
`Reprocess_DevNode` (service 92), `Setup_DevNode` (52) and
`Read_Registry_Value` (62). At its start the worker records each
member's children unmarked (the start baseline).

The service this record adds is in the same table. Read 2026-10-25 from
the Windows 98 SE DDK's `CONFIGMG.H` (the archive of
`docs/references/README.md`, "Windows 98 DDK"; the `INCS_DDK.CAB`
member `INC_WIN98_CONFIGMG.H`, 106,882 bytes, dated 1998-09-03): the
service table entry `_CONFIGMG_Add_ID` is line 618, ordinal **85** by
the rule `legal-provenance.md`'s row for `cm_cfgmg.h` states (the table
from line 533, `Get_Version` 0, no non-entry line before 624); its
flags are `CM_ADD_ID_HARDWARE` `0x00000000`, `CM_ADD_ID_COMPATIBLE`
`0x00000001` and `CM_ADD_ID_BITS` `0x00000001` (lines 1252 to 1254); its
ring-3 prototype (line 2888) takes a devnode, a string and the flags.
Values only; the row in `legal-provenance.md` section 4 grows by these
in task 44.3, as every number of `cm_cfgmg.h` did. The run-time guards
of record 21 section 6.1 (`Get_Version`, the root id) stay before it.

Q1's option (b) (section 5.2, set aside by its reading) took one more,
read from the same files on 2026-10-25 and kept here as read; no code
calls it:
`_CONFIGMG_Write_Registry_Value` is line 596, ordinal **63** (the
`Read_Registry_Value` entry of record 28 is line 595, ordinal 62); its
ring-3 prototype (line 2866) takes a devnode, a subkey, a value name, a
type, a buffer, the buffer's length **by value** (where the read takes a
pointer to it) and the flags. `CM_PROB_FAILED_INSTALL` is `0x0000001C`
(`CONFIGMG.H` line 1075). The Windows 98 DDK's `REGSTR.H`
(`INC_WIN98_REGSTR.H`): `REGSTR_VAL_CONFIGFLAGS` "ConfigFlags", "(binary
ULONG)" (line 959), `CONFIGFLAG_REINSTALL` `0x00000020` (line 985) and
`CONFIGFLAG_FAILEDINSTALL` `0x00000040` (line 986). Values only, into the
same `legal-provenance.md` row.

### 2.5 The mark service's 8.1 switch

Record 21 section 7.5 (task 43.59): on Windows 8.1 alone, `xhcimark`
installs the staged Stor98 on a tunnelled NVMe devnode Windows has just
started under `stornvme` (`mk_s98_wanted`: a PCI NVMe controller, on
`stornvme`, the policy value `Parameters\Stor98Inf` naming a staged INF,
below a verified tunnel port, not protected, started, its record
allowing an attempt, no Windows installation pending), through
`SetupDiBuildDriverInfoList`, `SetupDiGetSelectedDriverW` and
`newdev.dll`'s `DiInstallDevice` under the tools' eject mutex and the
installer's pause event, with records under `Services\xhcimark` and a
recovery of an owed enable. The installer writes the value on 8.1 alone
(`inst_s98_switch_os`) and deletes a stale one elsewhere. Section 7.5.2
(a) of that record weighed the id route this record takes and set it
aside for 43.59 on two grounds that this record answers: depth (section
4.3) and the first plug (sections 4.4 and 6.2); its third ground, that a
private id cannot outrank `stornvme`, was overturned where the test
certificate is trusted - a catalogued Stor98 then ties the signature score
and the identifier band decides (2026-10-23) - and still holds in the
ordinary configuration, where it ranks unsigned (2026-10-25, section 2.1), which is why the switch stays as
8.1's arm (section 6.1).

## 3. The id

### 3.1 The string

`PCI\XHCI98_TUNNEL&CC_010802`.

- `PCI\`: the device is a PCI function and its enumerator is `PCI`; the
  prefix keeps the id in the form every tool, INF parser and class
  installer expects of a PCI device's id, and reads naturally beside the
  device's own ids in Device Manager's "Compatible Ids". No inbox INF
  matches a `PCI\` id that carries no `VEN_` and no `CC_` at its head, so
  nothing but this package's INFs can match it.
- `XHCI98_TUNNEL`: the project's token, as `USB\XHCI98_HUB` and
  `USB\XHCI98_HUB30` are the bus's (design record 13 section 10.13),
  and what it states: the function is below a Thunderbolt or USB4 tunnel
  port of this driver's.
- `&CC_010802`: the class, subclass and programming interface the id is
  given for, in the form `pci.sys` uses, so that the id says what kind
  of function carries it and an id of the same form can be given to
  another class later without a second token. No word "NVMe" anywhere
  (Stor98's `AGENTS.md`); the class code is the descriptive form.

One id, the same on every target and both architectures, carried by
every tunnelled NVMe function whatever its vendor: it names a place, not
a model.

### 3.2 Where it sits: the first compatible id

On NT the id is put **first in the device's compatible ids**, and the
device's hardware ids are not touched; on 98 SE and ME CONFIGMG's
`Add_ID` puts it **last** (read 2026-10-25, section 5.1), which the 9x
wizard does not mind (the last item below).

- The hardware ids stay what `pci.sys` (or `PCI.VXD`) gave: the first
  hardware id, which the installer, the tools and Device Manager show
  and match on (`first_hw`, record 24's listings), is unchanged, and a
  vendor's own driver for the function, were there one, ranks as before.
- First, so that against a model that names it as a hardware id the
  rank is `0x2000`, below `stornvme`'s on 8.1, which the insertion moves
  from `0x2005` to `0x2006` (section 4.7). Any position before the class
  id would outrank it too, and a position after it would lose; first is
  chosen so that the result does not depend on the shape of the bus
  driver's list, and never on the date, which the reading of 2026-10-23
  showed deciding a tie - a rule this record does not rest on.
- A compatible id and not a hardware id, so that Windows 10's section of
  the INF can list it as a compatible id and rank `0x3000`, behind
  `stornvme`, which is task 43.59's policy there (section 4.7). As a
  hardware id it would rank `0x0000` or `0x1000` against any model line
  and Stor98 would take every tunnelled drive on 10 too.
- On 98 SE and ME the hardware wizard matches `HardwareID` and then
  `CompatibleIDs` against the INFs it searches; with no other driver for
  the class, the position is immaterial: the compatible list is chosen
  for the same shape on every target, and `Add_ID`'s append is taken as
  it comes.

### 3.3 What carries it

An NVMe-class function: one whose compatible ids, as the bus driver gave
them, contain `PCI\CC_010802` (class `01`, subclass `08`, programming
interface `02`). That is the test on NT (section 4.2), made on the
`BusQueryCompatibleIDs` answer itself, and on 9x (section 5.1), made on
the devnode's `CompatibleIDs` value. No other class is tagged: not the
enclosure's bridges, not a dock's xHCI or Ethernet, and never the NHI or
the host's own xHCI, which on a discrete controller sit below switch
ports of the same ids the filter INF names.

### 3.4 What it means: the structural rule (owner, 2026-10-25)

The id states that the function is below a bridge the tunnel-port
filter INF named as a Thunderbolt or USB4 tunnel port, and nothing more.
It is decided by the filter alone, at the function's enumeration, from
what the filter knows synchronously: that it sits on such a bridge, and
that the function below is of the NVMe class. It does not wait for the
connection manager's NHI to verify the port, assign it to a domain or
bring a tunnel up.

Why that is sound: every id the filter INF names is a port whose purpose
is tunnelling - a discrete controller's switch ports (whose other ports
hold the NHI and the host xHCI, never a drive), the integrated
controllers' dedicated tunnel root ports, the USB4-only hosts' tunnel
ports, the emulator's. No machine puts an internal drive below one of
them. The verification record 21 section 6.3 makes is about which domain
owns a port and which bridge corresponds to which adapter, not about
whether a device below it is tunnelled.

Why not the verified port: a drive behind a tunnel built before the
operating system (pre-boot, firmware, a preserved tunnel of record 22
section 9.2) is enumerated at boot, with the bridge filter in its stack
and the NHI not yet started. Waiting for the verification would mean
holding such a drive back from `BusRelations` until the NHI decided,
with a bound for an NHI that never will (`XhciCmMode` 0, an inert start,
no NHI in the package), and re-reporting it later - more code, a boot
dependency, and the same answer on every common path. The owner chose
the structural rule.

What the rule does not cover:

- a tunnel port the filter INF does not name, whose set comes from the
  override `XhciCmTunnelBridge` alone: no filter is installed there today
  (the installer installs `xhcicmf.inf` by its ids), so no id, and
  record 21 section 6.4's limits apply as they do to re-enumeration;
  section 6.2's install reaches such a drive on Vista to 8.1; on 2000 and
  XP nothing automatic does (section 4.4), and the installer or Device
  Manager is the route;
- a drive whose first enumeration the chain did not reach (section 4.4);
- Windows 98 SE and ME, where there is no filter and the rule is the
  worker's: an active member's descendants (section 5), which is the
  verified port by construction.

## 4. Windows 2000 and later: the filter chain

### 4.1 The attach, on every filtered bridge

Today the bridge filter attaches a child only on an active bridge with
safe eject on (section 2.3). For the id it attaches a child to **every
PDO new by a whole snapshot on every bridge it sits on**, from the
bridge's start, whatever its active mark and `XhciCmSafeEject` say:

- the attach decision of `cmChildCreate` is unchanged (directly above
  the PDO, above another driver's bus filters, or skipped when anything
  else is attached or the snapshot was truncated);
- the child's `Removable` answer keeps its conditions, which means one
  predicate changes: today `CmRegAttMayMark` answers yes for every
  kernel-attached child without a test, because such a child existed only
  on an active bridge with safe eject on; with children on every bridge
  it answers for a kernel-attached child as it does for a registered one
  (the walk under `cmRegLock`: its bridge an active, exclusive member
  with safe eject on), so a child attached for the id on a bridge that is
  not active marks nothing, and the NHI's and the host xHCI's PDOs are
  never answered Removable. A host vector of the marks covers both kinds
  (section 9.1);
- the eject's bookkeeping (`CmRegAttRemoved`, the marks table) reads the
  child's events as it does now; a child on a non-active bridge reports
  to no domain, as a bridge filter with no owner does.

The host's own functions below a discrete controller's switch ports (the
NHI, the xHCI) therefore gain a child filter of this driver that does
nothing: a passive pass-through on two boot-path devices. Section 9.3
item 1 reads that on 2000 and 7 before the code is committed, and the
alternative, should it cost anything, is to attach only to a function
of the NVMe class or a PCI-to-PCI bridge (the chain of section 4.3 must
still pass through every bridge of an enclosure), which needs the class
before the attach (`HalGetBusData` on the bridge's secondary bus, which
the filter can read from its location) and is the fallback design.

### 4.2 `IRP_MN_QUERY_ID`: the compatible ids rewritten

A child filter handles `IRP_MN_QUERY_ID` with `IdType`
`BusQueryCompatibleIDs` (and that type alone: `BusQueryDeviceID`,
`BusQueryHardwareIDs` and `BusQueryInstanceID` go down untouched):

1. Forward it synchronously (`CmForwardSync`) and read the answer: a
   `MULTI_SZ` of `WCHAR` in `IoStatus.Information`, which the PnP manager
   frees with `ExFreePool`.
2. If the status is a success, the buffer is not null, and the list
   contains `PCI\CC_010802` (a whole string, compared case-insensitively
   on the `WCHAR`s), build a new `MULTI_SZ` of the id followed by the
   old list, in that order, from **paged pool**, as `IRP_MN_QUERY_ID`'s
   contract requires of the answer, through a second helper of
   `cm_pool.c` (`CmAllocPaged`: `ExAllocatePoolWithTag(PagedPool, ...)`,
   PASSIVE_LEVEL, this answer its only caller, so the allowlists' one
   pair stays held to `cm_pool.obj`; the connection manager's other
   allocations stay nonpaged), free the old one with `ExFreePool`, and
   complete with the new one.
3. Otherwise complete the request as it came back, untouched.
4. Any failure of the allocation completes the request untouched: the
   device then has its plain ids, which section 6.2 covers; it is
   counted (`cm.reenum.id.nomem`) and logged.

The list walk, the class test, the size arithmetic (bounded; a list
over a limit, say 4 KiB, is passed untouched and counted) and the
building of the new list are one pure function in `cm_ports.c`
(`CmPortsTunnelIds`), with host vectors in `test_cm_ports` (section
9.1). The filter counts the ids it gave (`cm.reenum.id.given`) and
logs each at level 4 with the bridge's location and the PDO's address,
and the status record of record 24 shows the count.

The PnP manager records the answer in the devnode's `CompatibleIDs`
value and ranks with it; it asks again only when the devnode is created
anew (a boot, a re-plug, a removal and re-enumeration), and the chain
must be in place each such time, which section 4.5 and the `LowerFilters`
mark (section 4.3) see to.

### 4.3 Depth: the chain through an enclosure's switch

In every real enclosure read so far the device at the top of the
tunnel's secondary bus is a switch, not the drive: the ORICO ASM2464PD
and a Thunderbolt 3 enclosure's controller both put an upstream and a
downstream port between the tunnel port and the NVMe function (record
21 section 7.5.2); a dock adds a switch of its own above an enclosure's.
Only the emulator's `usb4-tunnel-port` and `usb4-tunnel-bridge` with
`nvme-drive=` put the controller directly on the tunnel bus. The id must
therefore reach a function several bridges down, and the filter that
rewrites its ids must be in the stack of **its** parent bridge when that
bridge reports it.

So a child filter of this driver that sits on a PCI-to-PCI bridge - its
`BusQueryCompatibleIDs` answer contains `PCI\CC_0604` - handles
`IRP_MN_QUERY_DEVICE_RELATIONS` for `BusRelations` as the bridge filter
does: it keeps a snapshot of its own and attaches a child to each PDO
new by a whole snapshot, which in turn does the same if it is a bridge.
The chain is bounded by depth (`CM_TUNNEL_DEPTH_MAX`, counted from the
topmost bridge a bridge filter sits on - on a discrete controller its
switch's upstream port, two levels above the tunnel port (read on 7,
2026-10-25: a drive directly below the tunnel port at depth 3) - so 8: the
controller's switch ports, a dock's upstream and downstream ports, an
enclosure's upstream and downstream ports, the function, with one to
spare) and by the snapshot's
room (`CM_SNAP_MAX`); a bridge past the bound attaches nothing and
counts it. A child on a bridge keeps the registration list's attachment
entry it has now; nothing of this is published to the NHI as a member,
and the eject's unit stays what record 24 section 4.3 says.

**The loss, and the re-attach.** On 2000 and XP the first-time driver
install of a bridge below the tunnel port rebuilds that bridge's stack
from the registry and loses the kernel-attached child (section 2.3).
Two things follow:

- The PnP manager restarts an installed devnode by re-enumerating its
  parent; that `BusRelations` answer passes through the filter above
  the parent, and the installed bridge's PDO is **in the snapshot** but
  has no child of ours any more. The relations handler attaches again
  to a PDO in its snapshot whose child entry is gone (the attachment
  list says so; `CmPortsChildFirst` gains that case) - before the PnP
  manager builds the restarted bridge's stack, so the child is below
  the new `pci.sys` FDO and sees that bridge's own `BusRelations`. This
  is the hypothesis [hyp] that the restart re-enumerates the parent
  before it starts the child; section 9.3 item 2 reads it on 2000 and
  XP before the code, with the emulator's switch model. Where it holds,
  the chain survives the first-time install of each bridge in turn, and
  the function at the bottom is tagged on its first enumeration; where
  it does not, the first plug of a never-seen enclosure enumerates the
  function with plain ids, and section 6.2 covers it.
- The mark service's `LowerFilters` value on the unit (section 7.4
  revision 3) makes the unit's child registered, so from the unit down
  the chain's root survives every rebuild after the service has written
  the mark; the `AddDevice` child on a bridge reads `BusRelations` as
  the kernel one does. The service writes the mark seconds after the
  unit appears, which on a first plug is after the unit's first-time
  install began, so it is not relied on for the first plug.

On Vista and later whether the first-time install loses the child is
unread (section 2.3); section 9.3 item 2 reads it on 7, 8.1 and 10 too.

**Read 2026-10-25 (section 9.4): the [hyp] does not hold on 2000 or XP.**
The chain tags the function at depth 3 on its first enumeration, while
the enclosure's bridges run from the critical device database; each
bridge's first-time install then removes the children below it, and no
`BusRelations` of the parent reaches a filter of this driver afterwards,
so nothing re-attaches and the function is created again with its plain
ids. The first plug of a never-seen switch enclosure therefore arrives
untagged on 2000 and XP, as section 4.4's first case allows; a re-plug,
with the bridges known, is tagged, and on XP the mark service's restart
of the marked unit (record 21 section 7.4 revision 3), which came after
the bridges' installs, tagged it too. The re-attach of the code
(`CmPortsChildFirst`'s case for a bare PDO in the snapshot) never fired
in these readings; it is kept as a harmless case, since a parent's
`BusRelations` may yet arrive on another system.

### 4.4 What arrives without the id

- The first plug of a never-seen enclosure with a switch on 2000 or XP
  where the re-attach of section 4.3 does not hold, or on a later system
  where the loss is read to happen.
- A function reported in a truncated snapshot, above the depth bound, or
  whose rewrite failed for want of pool (section 4.2 item 4).
- A drive below a port that carries no filter: an override-only port
  (section 3.4), a port of a switch whose filter install the user
  declined, or a port on a machine where the filter INF was never
  installed.
- A drive present at the install of the package, before the filter and
  the connection manager were there (section 7).

Each is a tunnelled NVMe function with plain ids and, since the INF has
no generic match, no driver of this package's: on 2000 to 8 Code 28, and
Windows' own hardware wizard on 2000 to 7 finding nothing; on 8.1 and 10
`stornvme`, which Windows keeps for a configured devnode whatever a
later enumeration adds to its ids. What reaches it (section 6.2):

- on Vista to 8.1, the mark service's per-device install within seconds
  (over nothing, or on 8.1 over `stornvme`), whatever the case;
- on 2000 and XP, the mark service's restart of the unit, for the first
  case only - a switch enclosure whose bridges are now installed - and
  only where the unit holds nothing else; a re-plug does the same. The
  other cases (a direct drive on a port with no filter, a depth or pool
  failure, a drive present at the install the installer could not take)
  have no automatic route there: a re-plug helps only where the reason
  is gone (the bridges installed, the filter now present), else the
  installer's per-device route at its next run or the user's Device
  Manager update;
- on 10 nothing does, by policy.

The release notes say so (section 8).

### 4.5 Boot, pre-boot tunnels and task 44.32

The bridge filter is a registered upper filter of the tunnel port and
starts with it, at boot included; a drive behind a pre-boot or preserved
tunnel (record 22 section 9) is enumerated through it and, its bridges
being known to the system by then, through the chain. That is the
structural rule's whole point, and it is why this record needs a filter
of this driver's in the tunnel port's stack at every enumeration of its
children. Task 44.32's three options are read against that: "the start
type put back" and "only tunnel ports filtered" keep the vehicle; "no
bridge filter" removes it, and this record then needs another way to be
above the children at `BusRelations`, which there is not. 44.32's design
section is to say which it keeps, and this record's section is amended
if it is not one of the first two. Decided (owner, 2026-10-25; record 23
section 5.14): the start type put back, so the vehicle is kept.

### 4.6 Devnodes already bound

A devnode Windows has configured keeps its driver; the ranking runs only
for a devnode with none, or when a driver is updated. So, after an
update to this release:

- a tunnel drive bound to Stor98 under `3.2.0.0`'s class match stays
  bound, with plain ids until its next enumeration gives it the id (the
  install's own restart of the connection manager and filter, or the
  next boot); nothing depends on when;
- an internal drive a user put on Stor98 by hand stays there, as record
  23 section 4.4's shared-file rules say; it never gets the id;
- an internal drive on `stornvme`, or with no driver, stays as it is on
  every system: Stor98's INF offers nothing for it any more.

An uninstall that removes Stor98 from its devices (record 26 section 9.3)
leaves the id in the devnode's recorded ids until the connection manager
is gone too; a devnode whose ids name a driver no INF matches is a
devnode with no driver, as before the package.

### 4.7 The ranking per system (scope B, owner 2026-10-25)

The owner's scope decision (section 4.8, option B): the id alone where
a restart of the drive's own stack was read to keep the driver (7 and 8,
and with them 2000 to 8), and `3.2.0.0`'s models on 8.1, where the
install's own restart lost the id and the driver, and on 10 (assumed
alike, unread). `stor98-x86.inf`:

```
[Manufacturer]
%Mfg%=Stor98_Models,NTx86.6.3

[Stor98_Models]                 ; 2000 to 8 (2000 reads no decoration)
%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802

[Stor98_Models.NTx86.6.3]       ; 8.1 and 10: 3.2.0.0's model
%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802
```

`stor98-x64.inf` the same with `NTamd64` and `NTamd64.6.3`.
`check-stor98-inf.ps1` holds that shape: the id as the hardware id in
the section before NT 6.3, the class as the compatible id (hardware id
empty) in the `6.3` one, and no class match in any other model line.
Two decorations on a 9x models line were read on 98 SE and ME on
2026-10-25 (section 9.4 item 4), before this shape needed only one.

**98 SE and ME** (section 5.2, Q1 options (a) and (a2)) install from
`stor98w9.inf`, a third INF beside the two (`src\stor98\inf\stor98-9x.inf`):

```
[Manufacturer]
%Mfg%=Stor98_Models

[Stor98_Models]                 ; 98 SE, ME: the id and the class
%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802,PCI\CC_010802
```

with no NT section and `stor98.mpd` its one file; the gate holds it to
that shape. A decoration on `stor98.inf`'s line (`NTx86`) would not have
served: Windows 2000 reads no models decoration and takes the
undecorated section, so a class match there would have reached 2000.

| System | Stor98's rank for a tunnelled drive | The other candidate | Who binds at the plug |
|---|---|---|---|
| 98 SE, ME | the class (and the id, appended where the worker reached the devnode first), matched by `stor98w9.inf` | none | Stor98, by the wizard at the plug (section 5) |
| 2000, XP, Vista, 7 | `0x2000` (device compatible, model hardware) | none | Stor98 |
| 8 | `0x2000` | none (no `stornvme`) | Stor98 |
| 8.1, 10 | `3.2.0.0`'s: the class, compatible to compatible, `0x3005`-band, behind `stornvme` whatever the signature (task 43.59) | `stornvme`, class as hardware id | `stornvme`; on 8.1 the mark service's 8.1 arm installs Stor98 over it within seconds, as `3.2.0.0`'s switch did; on 10 the installer's case 3b or the user (Stor98 opt-in) |
| XP x64, Server 2003 x64, Vista x64, 7 x64, 8 x64 | as the x86 row of each, through `stor98-x64.inf` | as above | as above; x64 test-signing as record 19 |

An internal NVMe drive carries no id, so on 2000 to 8 Stor98's INF
matches nothing of it: the wizard on 2000 to 7 finds no driver and 8
leaves it without one. On 8.1 and 10 `stornvme` keeps it, as in
`3.2.0.0`. On 98 SE and ME the wizard offers Stor98 for it by
`stor98w9.inf`'s class match, as before this record (section 5.2). That
is the record's checkpoint on each system (section 9.2).

### 4.8 The id across a restart of the device's own stack (owner, 2026-10-25: scope B; option A a task of the next release; an 8.1 fix sought)

**What was read.** The id lives only in the answer to
`IRP_MN_QUERY_ID`, which the kernel-attached child of section 4.1
rewrites. That child is part of the device's stack and leaves it, today,
at every `IRP_MN_REMOVE_DEVICE` the stack receives - not only when the
device is gone, but at every removal the PnP manager makes of a device
that stays present: a disable, a driver install or update on the device,
a `DICS_PROPCHANGE` restart, the first-time install of a bridge (which
is how the chain is lost on 2000 and XP, section 4.3). When the PnP
manager starts such a device again it asks its ids again before any
`AddDevice`, so neither the kernel child (gone) nor a registered child
(not yet attached) answers, and Windows records the plain list. Read on
8.1 x86 (section 9.4): the 8.1 arm disabled the tagged drive, installed
Stor98 on it (`DiInstallDevice` success, selected on the id), and at the
enable the devnode's `CompatibleIDs` were plain and the drive came back
on `stornvme`. Section 9.3 item 10 (b) and (d), read the same evening on
the qemu build of `949abab8` (`runs/run-44.md`, "Reading 10"):

| System | The restart | The id after it | The driver after it |
|---|---|---|---|
| 7 x86 | Device Manager disable and enable, a chain drive and a direct drive with the registered child | gone (already at the disable) | Stor98, running; no driver search |
| 8 x86 | disable and enable, a direct drive with the registered child | gone (at the enable) | Stor98, running; no driver search |
| 8.1 x86 | the installer's case 3b (`DiInstallDevice` with its own restart, no disable) | gone | `stornvme`: Windows chose again, and Stor98 never ran (the installer: "did not end as installed", exit 2) |

So 7 and 8 keep a configured devnode's driver when its ids change. On
8.1 the install's own restart loses the id the model was matched on, and
Windows binds `stornvme`: with the id alone, Stor98 can be put on a
tunnelled drive on 8.1 neither by the mark service nor by the installer.
Whether a drive already configured on Stor98 on 8.1 leaves it at a later
restart was not read (no such drive could be made), nor 10.

**Why it matters.** On 8.1 Windows chose a driver again when the device
restarted with ids its new driver no longer matched, so with the id alone
Stor98 cannot be installed there at all (the install restarts the
device); 10 is assumed alike [hyp]. On 2000 to 8 the id lost at a restart
costs nothing for a bound drive (read on 7 and 8; 2000, XP and Vista
assumed alike [hyp]).

**The direction: a kernel child that lives as long as its PDO.** A
filter attached above a PDO outside `AddDevice`, at the parent's
`BusRelations`, sits where a bus filter sits, and a bus filter that stays
attached across a removal of the function's stack is cleaned up by the
I/O manager's detach callback: the driver's `FastIoDispatch`
`FastIoDetachDevice`, called when the device object below it is deleted
(Microsoft's Driver Verifier rules name that callback as the cleanup of
such a filter). The design therefore is:

1. At `IRP_MN_REMOVE_DEVICE` a kernel child forwards the request and
   does a **logical removal**: its started state, its marks and its
   chain snapshot are cleared, and the departure is recorded for record
   21's eject evidence as today (`CmRegAttRemoved`: a removal after a
   query-remove is still a pending departure the eject confirms), but it
   neither detaches nor deletes itself. A later start of the same stack
   re-arms it as a new arrival of the device (its marks, the eject's
   stale-departure test).
2. **Final destruction** happens in `FastIoDetachDevice` alone, when the
   PDO below it is deleted - by `pci.sys`, after the parent reports the
   device missing or when the parent's own FDO goes. The callback runs
   in the I/O manager's deletion path: it detaches, removes the
   attachment entry, and deletes the device object, after the rundown of
   every request and notification still holding the child (item 3).
3. **The state and its rundown**, separate from today's `Removing` /
   `Outstanding` / `Drained` protocol: a logical-removal state that lets
   requests through to the PDO; a destruction state entered once, by
   the callback; a reference for each request in dispatch, each
   completion routine and each registration-list notification; the
   callback's wait bounded by those references and taken with no lock
   held; exactly-once deletion whatever the order of a failed start, a
   repeated removal, power and WMI traffic, the uninstall and the
   driver's unload (which cannot happen while a kept child exists).
4. Kernel children on bridges (the chain, section 4.3) and on functions
   alike; a registered child (`AddDevice`) keeps the stack's lifetime.
   Attachments above ACPI's bus filter (section 2.3, `child.under`) are
   included; the order in which the I/O manager calls the two filters'
   detach callbacks is read, not assumed.

**What must be read before any code** (section 9.3 item 11): on 2000,
XP, 7, 8.1 and 10, with Driver Verifier on the filter driver, a
prototype's behaviour at a disable and enable, a driver update, the
switch enclosure's first plug (the bridges' installs), a surprise unplug
with I/O open, an eject, a nested bridge's restart, a parent's removal
with disabled children, a removal delayed by an open handle, and the
uninstall - whether `FastIoDetachDevice` is called for each deleted PDO
the child sits on, in what order with ACPI's filter, and at what IRQL.

**What it buys.** The id survives every restart of the device's own
stack on every NT system; the chain survives a bridge's first-time
install on 2000 and XP, so the first plug of a never-seen switch
enclosure is tagged there too and section 6.2's unit restart is left for
the rarer cases; the 8.1 arm keeps the id and binds Stor98.

**The cost and the risk.** Larger than a first estimate: the two-state
protocol, the detach callback and its rundown, the eject evidence kept
independent of the attachment, a pure decision with host vectors, and
the reading matrix above - of the order of 400 to 700 lines in
`cm_filt.c`, `cm_reenum.c` and `cm_entry.c`, and a reading and leg
campaign of several days. The risk is in the removal paths of every NT
target: an error is a use after free at an unplug or an eject, a
bugcheck, on a path every tunnel drive takes; and it departs from how
this driver's filters handle a removal today.

**The scope options** (the owner's, with this section and item 10's
reading in hand):

| | (A) ship the design above | (B) tunnel-only on 2000 to 7 | (C) defer 44.3 |
|---|---|---|---|
| 98 SE, ME | class match by `stor98w9.inf` (section 5.2) | the same | `3.2.0.0`'s INF |
| 2000, XP, Vista, 7, 8 | the id alone | the id alone (read on 7 and 8: a restart keeps Stor98) | `3.2.0.0` |
| 8.1, 10 | the id alone | the class match as the compatible id, hardware id empty, as `3.2.0.0` (task 43.59's policy; the 8.1 arm as `3.2.0.0`'s switch, read working then; case 3b as `3.2.0.0`'s, its 8.1 leg unread then) | `3.2.0.0` |
| code | this section's, plus everything coded | what is coded, with `stor98.inf`'s `6.3` sections (x86 and x64) back to the class alone and no `10.0` sections - no id in them, so a restart loses nothing Windows matched | none of 44.3 ships |

Under (B) 2000 to 8 read the undecorated (x86) or `NTamd64` (x64)
section, the id alone, and 8.1 and 10 read `6.3`, the class
as the compatible id; the NT 5.x unit restart, the installer's
per-device route and candidates by class, the 9x INF and the earlier
INF's removal stay as coded. What (B) leaves on 8.1 and 10 is `3.2.0.0`'s
state: the class as the model's compatible id, which ranks behind
`stornvme` on an internal drive whatever the signature (task 43.59's
reading; the trusted-certificate win was of the earlier, hardware-id
shape).

**Decided (owner, 2026-10-25): (B)** for 98 SE and ME, 2000 to 8 and 10;
(A) a task of its own in the next release, prototyped and read under
Driver Verifier before any code; for 8.1 a proper fix is sought - read
first, then designed and converged, and put to the owner before its code
(section 9.3 item 12) - with `3.2.0.0`'s model in place meanwhile. Under
(B) the mark service's 8.1 arm installs over `stornvme` by the class
model, as `3.2.0.0`'s switch did; records an id-only test build wrote
never shipped and need no migration.

**The 8.1 fix, read and decided (owner, 2026-10-25).** Section 9.3 item
12, read on 8.1 x86 the same evening (`runs/run-44.md`, "Reading 11"):
at any restart of the drive's stack the kernel reads the ids again from
the bare PDO and chooses a driver again over the driver store
(`Microsoft-Windows-Kernel-PnP/Configuration` event 400: `stornvme.inf`,
"Driver Rank: 0xFF2005, Matching Device Id: PCI\CC_010802, Outranked
Drivers: (empty), Device Updated: true"); the plain list leaves an
id-only Stor98 no candidate at all. An install made without its restart
(`DI_DONOTCALLCONFIGMG`, 0x00020000) and then restarted, with or without
the driver key's `MatchingDeviceId` rewritten to `pci\cc_010802`, ended
on `stornvme` as well: no user-mode route keeps an id-only model bound on
8.1. What holds is `3.2.0.0`'s model, matched on the class, an id the
plain list keeps [hyp: inferred from the 8.1 x64 reading of `3.2.0.0`'s
arm, which left Stor98 running]. Decided: 8.1 keeps `3.2.0.0`'s model in
`3.3.0.0` (candidate 1); the kernel child's lifetime limited to NT 6.3
(candidate 2) folds into option (A)'s task.

**No id on 8.1 and 10 (read 2026-10-25 in the legs, `runs/run-44.md`).**
With `3.2.0.0`'s class model, the 8.1 arm of this record's code still
ended on `stornvme` on 8.1 x86: event 400 configured `oem3.inf`, and half
a second later, at the arm's restart, `stornvme.inf`; `3.2.0.0`'s own
arm on the same image held (one event 400, Stor98 running after the
restart). The difference is the id: the drive had been enumerated with
it, and the restart read the ids without it, and 8.1 chooses a driver
again for a device whose ids changed. So the filter gives the id on
Windows 2000 to 8 alone (`cmChildIds`: the kernel's shared data
page reading NT 6.2 or below; `IoIsWdmVersionAvailable` cannot tell 8
from 8.1); on 8.1 and 10, where nothing of Stor98's matches it under scope
B, no id is given and the drive's ids are `3.2.0.0`'s. D1's structural
rule is read with that bound.

**Open for (A)'s own task** (not settled here): the detach callback's
rundown without a circular wait (a forwarded removal can still hold a
reference when the lower object's deletion calls it); the lifetime of a
kept child's links to its parent and bridge filters (`Parent`, `Bridge`)
and of attachments above ACPI's filter; the registration list's
attachment membership kept apart from a logical departure
(`CmRegAttRemoved` today unlinks and counts), with the eject's
stale-departure test; the readings on Vista, 8 and NT 5.2 as well.

## 5. Windows 98 SE and ME: the class match kept, the id a statement

### 5.1 `Add_ID` on an active member's descendants

The CONFIGMG worker (section 2.4) gives each NVMe-class devnode below an
active member the id, through the bounded walk record 28's window
already makes (`CM_WK_NI_DEPTH`, `CM_WK_WALK_NODES`): for each devnode
below the member whose `CompatibleIDs` value (read with
`Read_Registry_Value`, the thunk record 28 added) contains
`PCI\CC_010802` and does not yet contain the id, it calls
`Add_ID(devnode, "PCI\XHCI98_TUNNEL&CC_010802",
CM_ADD_ID_COMPATIBLE)`, once per devnode per enumeration, counted and
logged; nothing else follows the call. Read 2026-10-25 on 98 SE and ME:
`Add_ID` returns 0 and **appends** the id last to the comma-separated
`CompatibleIDs` value (one `REG_SZ` on 9x); a boot does not rewrite the
list, and a reinstall pass (`ConfigFlags` 0x20) does, dropping the id.
The pass runs in the marking window after a `Reenumerate_DevNode` and at
the worker's start (the start baseline); the reading found that window
able to run out while the worker's calls wait behind an open wizard, so
some devnodes are never tagged. On 98 SE and ME the id binds nothing
(section 5.2), so that is a gap in a statement, not in the install; it
is left as it is and stated (section 11).

On 98 SE and ME the rule is the worker's, so the id marks the verified
port by construction: an active member's descendants, exactly the set
the `DN_REMOVABLE` mark reaches.

### 5.2 Why the class match stays on 98 SE and ME (owner, 2026-10-25, Q1)

A 9x devnode's install runs once, when `PCI.VXD` enumerates it - in the
`Reenumerate_DevNode` call, or at the boot - and CONFIGMG hands it to the
hardware wizard at once, before the worker can reach it. With no class
match that search finds nothing, and the devnode is left with no
`Driver` value, `CONFIGFLAG_FAILEDINSTALL` (0x40) in its `ConfigFlags`
and the problem `CM_PROB_FAILED_INSTALL` (0x1C). Read 2026-10-25 on 98 SE
and ME (section 9.4), nothing makes Windows search for that devnode again
with the id in place: not `Reprocess_DevNode`, not
`Setup_DevNode(CM_SETUP_DEVNODE_READY)`, not `ConfigFlags` cleared to 0
(with the `Class` value kept or removed), not the devnode's `Enum` key
rewritten, at a re-plug or at a cold boot; `ConfigFlags` 0x20 does make
the boot reinstall it, but the reinstall reads the ids from the
enumerator again and drops the id. The one arrival that worked is a 98 SE
hot plug where the id landed while the wizard's first page waited for
the user.

So, as the owner's decision says for that outcome (section 10, Q1,
option (a); option (a2) for its file): on 98 SE and ME Stor98 is
installed by its **class match**, from a separate INF the installer
stages on those two systems alone - `src\stor98\inf\stor98-9x.inf`,
packaged as `stor98w9.inf` beside `stor98.inf` in the x86 package. Its
one models section, undecorated, names the tunnel id and `PCI\CC_010802`
as its compatible id; it has no NT section and installs `stor98.mpd`
alone. `stor98.inf` matches the id alone on 2000 to 8, Windows 2000
included (8.1 and 10 keep `3.2.0.0`'s class model, section 4.7): Windows
2000 reads no models decoration, so a class match kept
in `stor98.inf`'s undecorated section would have offered Stor98 for an
internal drive there too. On 98 SE and ME, which have no NVMe driver of
their own, Windows may therefore offer Stor98 for an internal NVMe drive,
as before this record; the installer puts it on tunnel drives only, and
the release notes say so.

### 5.3 The wizard, and what the user sees

On a first plug the hardware wizard finds Stor98 by the class match in
`stor98w9.inf`, from `INF\OTHER` or the install directory on `DevicePath`
(record 23 section 4.4; `s98_inf_refresh_9x`), as it did before this
record, with the one source-path prompt for `stor98.mpd` record 23
already states. The worker's id lands in the devnode's key before or
after that search; it changes nothing the user sees.

### 5.4 Persistence

The id, once in the key, stays there across boots as every value of an
`Enum` key does; `PCI.VXD` does not rewrite the lists at a boot (read
2026-10-25), and a reinstall pass does. An internal NVMe drive's devnode
never gets the id: the worker reaches only an active member's
descendants.

### 5.5 The helper and the installer on 9x

The installer's 9x device helper (record 27) installs a present drive
with no driver behind a tunnel through the setup engine's
compatible-driver selection over `stor98w9.inf`, which matches it by the
class as it matched by the class match before this record (with the id
alone the list was empty, "no-match", read 2026-10-25). The installer's
9x INF refresh and sweep (section 7.1) write `stor98w9.inf`'s text into
the copies they replace, and its local copy never carries `stor98w9.inf`
on NT.

## 6. The mark service

### 6.1 Windows 8.1: the switch kept as the 8.1 arm (owner, 2026-10-25, D3 and Q3)

Draft 2 expected Windows 8.1 to bind Stor98 at the plug by its own
ranking, the id first among the compatible ids, and retired the switch
of record 21 section 7.5 as the route, keeping its displacing install as
the fallback's 8.1 arm. The reading of 2026-10-25 (section 4.7) found
that the package ranks unsigned on 8.1 unless the project's test
certificate is trusted - the ordinary configuration, record 19 section
3 - so `stornvme` binds a tunnelled drive first there in both
architectures, and the **8.1 arm is 8.1's ordinary route** (Q3, a change
of text only): the service polls a started `stornvme` drive below a
verified tunnel port and displaces it, as the switch did. Under scope B
(section 4.8) the `6.3` model is the class as the compatible id, which
`stornvme` outranks whatever the signature, so `stornvme` binds a
tunnelled drive first and the arm installs Stor98 over it by that model,
whose matching id survives the install's own restart.

What changes from the switch of `3.2.0.0`: the 8.1-only gate on the
policy value (`inst_s98_switch_os`, `mk_s98_os81`) goes, the value
being written on 2000 to 8.1 (section 6.2); the install inside the stop
and start becomes section 6.2's model selection, since the INF no longer
matches the class. What stays: the poll, the stop-install-start sequence,
the pause event and switch mutex with the installer, the records under
`Services\xhcimark`, the recovery of an owed enable, the
one-attempt-then-wait rule (`mk_s98_retry`), the veto rule (section 11),
and the eligibility walk (`mk_s98_wanted`'s class, walk, tunnel and
protection tests). Record 21 section 7.5 is amended to point here, its
contract text kept as the record of `3.2.0.0`.

**Read 2026-10-25 on 8.1 x86 (section 9.4): the arm as first coded does
not hold the id across its own stop.** The drive arrived tagged (the id
first in its compatible ids) and on `stornvme`; the arm stopped it
(disable), which removed the kernel-attached child with the stack;
`DiInstallDevice` answered success; at the enable Windows read the ids
again with no child above the PDO (a registered child arrives at
`AddDevice`, after the ids, section 2.3), recorded the plain list and
brought the drive back on `stornvme`; the record "failed" then barred
every later attempt, a re-plug included. `3.2.0.0`'s switch did not meet
this because its INF matched the class, which survives the re-read. The
arm's shape followed the owner's scope choice of section 4.8: (B), so
`stor98.inf`'s 8.1 section matches the class again, as in `3.2.0.0`, and
the arm installs the class model, whose matching id survives the
re-read, as `3.2.0.0`'s switch did. No arm that stops the device can keep
an id-only model (any stop removes the child that gives the id); the 8.1
fix sought (section 4.8) is what would.

### 6.2 The fallback: a drive that arrived without the id

The fallback differs by system, because a service has no desktop:

- **Vista to 8.1: an install.** A package already in the driver store
  (staged by the installer, the user having answered any signing
  question then) installs on a device with no question asked (read on
  8.1 x64, 2026-10-25: the 8.1 arm's `DiInstallDevice` over `stornvme`
  with the certificate not trusted; Vista, 7 and 8 owed in the legs).
- **2000 and XP (NT 5.x): the unit restarted** (owner, 2026-10-25, Q2).
  Read on 2000 and XP (section 9.4): the model-selection install below
  works from the service only under the driver-signing policy Ignore;
  under the default (Warn) 2000 waited on a question the service cannot
  show, and XP refused it ("blocked (server install)", error 1168). The
  x86 package is never signed, so the service runs setup non-interactive
  (`SetupSetNonInteractiveMode`, `DI_QUIETINSTALL`), never installs on
  NT 5.x, and restarts the drive's unit instead, below.
- **10: nothing**, by policy (the value is deleted there, and the gate
  refuses on 10 whatever the value says): Stor98 on 10 is the installer's
  case 3b or the user's.
- **98 SE and ME**: there is no service; section 5 is the whole route.

With `XhciCmSafeEject` 0 the service is not installed and there is no
fallback of either kind (record 21 section 7.5.6, Q1, unchanged); the
chain still tags, so on 2000 to 8 a re-plug of a switch enclosure whose
bridges are installed is the route there; on 8.1, where `stornvme`
outranks the package in the ordinary configuration, it is not, and the
installer's case 3b or Device Manager is; the release notes say so.

**The candidate.** A devnode that:

- is a PCI function of the NVMe class (`mk_s98_wanted`'s `IsPci`,
  `Nvme`, from its compatible ids);
- is below a verified tunnel port of a verifiable domain, read as the
  switch read it (the driver's status record and the tree walk, whole),
  and not protected (record 24 section 4.3's protected functions);
- has **no driver of its own**: `Service` empty, not started, not
  disabled - never a devnode on Stor98 or on another driver; and on 8.1
  also one on `stornvme`, started with no problem (the 8.1 arm, section
  6.1). On Vista to 8.1 the id's presence is not a condition (a tagged
  drive `stornvme` outranked on 8.1, or one whose wizard failed, is taken
  as an untagged one is); on NT 5.x it is (below);
- has no Windows installation pending on it (`InstallPending`: the
  hardware wizard or a silent install still running, which the service
  never races; it waits a poll);
- the policy value names a staged INF that exists (`Stor98Inf`, written
  on 2000 to 8.1 by an install or update run with Stor98 ticked, the
  staging succeeded and the service kept; deleted on 10 and by the
  uninstall, as now);
- its record allows an attempt (`mk_s98_retry`: one attempt, then the
  record's wait, so a refused install is not retried every second).

**The install (Vista to 8.1).** The switch's routine built a
**compatible**-driver list over the staged INF, which needs the INF to
match one of the devnode's ids; with the class match gone that list is
empty for a plain drive. The per-device route selects the model without
an id match - the programmatic form of Have Disk with "Show all
devices": the devnode's setup class is first set to `SCSIAdapter`
(`SPDRP_CLASSGUID` through `SetupDiSetDeviceRegistryProperty`, which
writes the class name with it; `SPDRP_CLASS` is read-only and is not
written), because a class-driver list for a device is restricted to
that device's class; then a **class**-driver list is built over the
staged INF alone (`SPDIT_CLASSDRIVER`, `DI_ENUMSINGLEINF`,
`DI_FLAGSEX_ALLOWEXCLUDEDDRVS`), the one model selected
(`SetupDiSetSelectedDriver`), and `newdev.dll`'s `DiInstallDevice` called
on that devnode alone; every routine looked up at run time. No
`DIF_REGISTERDEVICE`: a devnode Plug and Play enumerated is registered
(read on 2000, where the same route under Ignore installed with
`DIF_INSTALLDEVICE` alone). A failed install gives the class back. For a
devnode with no driver there is no stop and no start: the install starts
it, under the record "give" (written before the call; recovered as done
or failed once the devnode is back, never as an owed enable). On the 8.1
arm the switch's stop, install and start sequence runs over `stornvme`
(its shape open: section 6.1's dated note).
Under the tools' eject mutex and the installer's pause event. The
service logs each install with the devnode's instance id and why the id
was absent where it can tell (the tunnel port's filter not registered,
the depth, a first enumeration the chain did not reach, or the id
present and another driver or none bound).

**The unit restart (NT 5.x).** The tag's restart is its own path,
beside the safe-removal mark's restart of record 21 section 7.4 revision
3, which is unchanged (its reason - a unit whose stack lacks the
registered child or whose child never answered `Removable` -, its gates,
`Restart=0` and its once-per-serial budget). The two meet only through
the drive: a mark restart made after the enclosure's bridges were
installed gives the drive the id - and, since task 44.52, sets its
install to run again (*The mark's restart*, below) -, and a drive with
the id is no candidate here, so no second restart follows; one made
earlier leaves it without, and the tag's restart follows:

- *Which unit.* The device at the top of the tunnel port's secondary bus
  above the candidate (record 24 section 4.3's unit), when it is a
  PCI-to-PCI bridge - a switch-shape enclosure or dock. A drive directly
  on the tunnel bus is its own unit and is tagged at its first plug by
  the bridge filter; an untagged one there (no filter: an override-only
  port, a filter install declined) has no route here, and a re-plug does
  not help it either (section 4.4).
- *When.* The candidate carries **no id**, and every bridge between the
  unit and it has a driver and is started (the enclosure's first-time
  bridge installs are done, which is what lost the chain, section 4.3). A
  candidate that carries the id with no driver was offered Stor98 by the
  wizard and declined, or its wizard is still open: the service leaves
  it.
- *The gate.* The unit's whole subtree, as this poll's whole walk read
  it and read again devnode by devnode under the locks just before the
  call (service and status), holds only PCI-to-PCI bridges that are
  started with a driver and NVMe-class functions with no driver, not
  started, with no problem or the problem "not configured" (1), "failed
  install" (28) or "reinstall" (18) - never a disabled one - at least one
  of them without the id. Anything else - a dock's xHCI or Ethernet,
  a drive already running (its volume), a function with a problem other
  than no driver, a subtree that does not read whole - refuses the
  restart, logged with the re-plug sentence of the release notes. The
  restart is made under the tools' eject mutex and the Stor98 switch
  mutex with no installer pause (as the 8.1 arm's install), with no
  installation pending and the unit, read again, started with no
  problem; a poll in which the mark's restart was made, or after this
  restart, does nothing more (the walk no longer shows the tree).
- *The budget.* One tag restart per **arrival of the unit as the
  service's walk sees it** (present, after a walk in which it was
  absent), in the service's run, spent only when the call is made (a lock
  held or an installation pending defers it to the next poll). It is
  kept in the service's memory, not persisted: the service stops with
  Windows - whose next start enumerates the unit anew through the chain,
  then in place, so the drive comes back tagged with no restart - or for
  an installer run, after which the unit may be restarted once more if a
  drive below it is still without the id; a bounded repeat, on a unit
  whose only functions are driverless drives. Several drives below one
  unit share the one restart.
- *The install set to run again.* Just before the call, each NVMe
  function below the unit with no driver and no id has
  `CONFIGFLAG_FAILEDINSTALL` (0x40) cleared and `CONFIGFLAG_REINSTALL`
  (0x20) set in its `ConfigFlags`, every other flag kept (on NT the
  reinstall reads the ids from the device again, through the chain, so
  the id is kept, unlike 9x's): XP's wizard, answered with its
  default-ticked "Don't prompt me again", installs the null driver, and
  clearing the failed flag alone left the drive with no wizard (read
  2026-10-26); a drive seen with the id and no driver in the same
  arrival - the wizard's offer declined - is left so for the arrival,
  whatever its ids read later, and never set to install again; XP's wizard,
  answered with its default-ticked "Don't prompt me again", leaves it set,
  and Windows then asks nothing when the drive comes back with the id
  (read on XP in the legs, 2026-10-25: the tag restart made, the id first,
  no wizard, problem 28). A drive the user disabled is no candidate (the
  gate). Where a drive's flags cannot be read or written, the restart is
  deferred to the next poll, its budget unspent.
- *The outcome.* `DICS_PROPCHANGE` on the unit, as the mark's restart is
  made; the unit's stack is rebuilt with its registered child, the chain
  attaches again below it, and the drives are enumerated anew and come
  back with the id (read on XP, 2026-10-25: the mark's restart, made
  after the bridges' installs, gave the drive the id). Windows' own wizard
  then finds Stor98 by the id in the user's session and the user answers
  the signing question, as for any plug of a known enclosure. A veto or a
  failure is logged and the budget is spent: the drive waits for a
  re-plug. A drive still without the id at a later poll of the same
  arrival is logged once, with the re-plug sentence, and left.

**The mark's restart** (task 44.52; owner, 2026-10-27). Read on XP and
XP x64 in the legs (`runs/run-44.md`, 44.45 continued, 2026-10-27): with
`Restart` 1, the default, a never-seen switch enclosure's drive arrived
without the id; XP's wizard for it, answered with its defaults, left
`CONFIGFLAG_FAILEDINSTALL`; the mark's restart was then made (the
bridges installed, no installation pending) and the drive came back
with the id, the flag still set, so Windows asked nothing; the service
read the id with no driver as the user's refusal, and the drive stayed
driverless (problem 28 on XP; on XP x64 a re-plug changed nothing). So
on NT 5.x the mark's restart makes the tag restart's flag step its own:

- *Which functions.* Each devnode of this poll's walk strictly below the
  unit the mark restarts that the tag's step would set: an NVMe-class
  function with no driver (its `Service` read, and empty), in a state the
  tag's gate admits (not started; no problem, or problem 1, 28 or 18;
  never disabled), without the id, not seen in this arrival with the id
  and no driver (the user's refusal, as above), and not protected. Never
  a function with a driver, and nothing else below the unit: a dock's
  xHCI or network function is left as it is (the mark's restart keeps
  its own reason and gates, record 21 section 7.4). The refusal is read
  without touching the fallback's arrival bookkeeping, which stays the
  fallback's poll's: the mark's restart runs before that poll in the
  service's round, so an entry counts only when the fallback's last walk
  saw the drive, and one that walk did not see is a new arrival with no
  refusal.
- *Read again.* The walk only names the functions. Each is read again
  just before its write - after the installation settle and with both
  mutexes held (below) -: present, still below the unit, its driver,
  status and compatible ids; one that has found a driver or the id since
  the walk, or whose state the rule no longer admits, is left, and one
  that cannot be read again defers the restart (below).
- *Where.* NT 5.x alone (2000, XP, XP x64), and only while the policy
  value names a staged INF that exists (the fallback's own condition,
  above): with Stor98 not staged nothing matches the id, and the wizard
  would only come back to find nothing. Vista to 8.1 are unchanged: the
  service's install by model selection takes a driverless drive there
  with or without the id, whatever its flags.
- *When.* Inside the mark's restart, after its fresh reads have passed
  and just before its call, under the tools' eject mutex it already
  holds and the Stor98 switch mutex taken after it, with no installer run
  in progress (the tag restart's locks), taken only when a function is
  to be set; with none, the mark's restart is made as before. The step
  is the tag's: `CONFIGFLAG_FAILEDINSTALL` cleared, `CONFIGFLAG_REINSTALL`
  set, every other flag kept, flags already so left unwritten, each write
  logged. A function with no `ConfigFlags` value yet (one never
  installed) reads as no flags and is given `CONFIGFLAG_REINSTALL` alone,
  but only when the value is verifiably absent: SetupAPI answers an
  absent value and one it cannot read alike, so the absence is read from
  the configuration manager (`CR_NO_SUCH_VALUE` for
  `CM_DRP_CONFIGFLAGS`); any other failure, or a value that is not a
  `REG_DWORD`, is a flag that cannot be read. The tag's restart reads the
  flags the same way.
- *When the step cannot be made.* The switch mutex not taken, an
  installer run in progress, or a function's flags that cannot be read
  or written: the mark's restart is deferred to the next poll, its
  once-per-serial budget unspent, the reason logged once (the mark
  restart's gate log), as the tag's restart defers. A drive whose flags
  stay unwritable thus holds back its unit's mark restart, and the
  unit's listing for safe removal waits for that or for a re-plug, as for
  the mark restart's other gates.
- *What follows.* The unit's stack is rebuilt, the drive is enumerated
  anew through the chain with the id and its install set to run, and
  Windows' own wizard in the user's session finds Stor98 by the id, as
  after the tag's restart. A drive the service reads with the id and no
  driver before that wizard starts is logged as the user's, as now (the
  wizard is the route, not the service). A restart vetoed or failed
  leaves the flags set: the drive's install runs at its next enumeration
  (a re-plug or Windows' next start), the same wizard's question.
- *What does not change.* The mark restart's reason, gates, budget and
  `Restart` 0 (no mark restart, so no step: the tag's restart stays the
  route, as 44.45's 2000 leg read); the tag's restart, which a drive the
  mark's restart gave the id never reaches.

NT 5.x includes XP x64 and Server 2003 x64 (NT 5.2), whose package is
test-signed and, the certificate untrusted, expected to be refused a
server install as XP's is; this record reads the owner's "2000 and XP"
to cover them (section 10, Q2, for the owner to confirm), and the legs
read it on XP x64.

### 6.3 The window and the console

The service shows and asks nothing: its log and the installer's log
(which names the service as the one that puts Stor98 on a drive left
plain on Vista to 8.1, or restarts its enclosure on 2000 and XP) are the
record. On 2000 and XP the question that follows a unit restart is
Windows' own wizard's, in the user's session, not the service's.
`XHCITR`'s tree shows the drive under Stor98 once it is.

### 6.4 The Critical Device Database on 2000 and XP (read 2026-10-25)

Read in the legs on 2000 and XP (`runs/run-44.md`, "CDDB reading"): at
the first install of Stor98 on a drive that carries the tunnel id,
Windows writes
`HKLM\SYSTEM\CurrentControlSet\Control\CriticalDeviceDatabase\pci#xhci98_tunnel&cc_010802`
with `Service` "stor98" and `ClassGUID` SCSIAdapter's, and nothing else.
A drive enumerated later with the id and no configuration of its own then
starts on Stor98 through that key, with no wizard: `DeviceDesc` "PCI
Device", no `Driver` value, `ConfigFlags` 0x400 (finish install); it
stayed on Stor98 at re-plugs with the flag left, and at a re-plug and a
restart with it cleared (a restart with it left was not read).
Deleting the key broke nothing: drives with their own driver key kept
Stor98 across a re-plug and a restart. The key is keyed by the tunnel id
alone, so it never reaches an internal drive. No dedicated code was
added for it (the coordinator's decision for the owner, 2026-10-25;
owner, 2026-10-26: no new code for the key). The uninstall
deletes it under its general rule for Critical Device Database keys that
name a service of the package the run removed or found absent (record 23
section 9.2 step 6a; read on 2000, `runs/run-44.md`, the q35 legs), so
it stays while Stor98 stays, kept for a drive that still uses it; it is
harmless either way, since once the connection
manager's filter is gone no devnode carries the id. A `pci#cc_010802` key, which a
`3.2.0.0` install might have written for the class, was not read (no NT
5 guest here ever had `3.2.0.0`).

## 7. The installer

### 7.1 The INF, staged as before

`stor98.inf` is staged on NT (the driver store, `SetupCopyOEMInf`), and
`stor98w9.inf` copied to `INF\OTHER` and the install directory on 98 SE
and ME as record 23 section 4.4 says of `stor98.inf`, when Stor98 is in
the run (section 5.2). On 2000 to 8, with no generic match in
`stor98.inf`, the staging no longer costs the "Windows may offer Stor98
for an internal drive" caveat, and the readme's and release notes'
sentences about declining it go there (section 8); on 8.1 and 10 the
class model ranks behind `stornvme` on an internal drive, as in
`3.2.0.0`; on 98 SE and ME the caveat stays. The shared-file rules
(another release of Stor98 installed, a Stor98 of the user's own) and
the "no Stor98 change at all" outcome stand unchanged.

**The earlier package's INF leaves the store.** `SetupCopyOEMInf` of a
different INF adds a second published copy and removes nothing
(`inst_eng.c`'s staging: only the identical-INF `/restage` case removes
one), so an update over `3.2.0.0` would leave that release's
`stor98.inf`, with its `PCI\CC_010802` model, in the store beside the
new one, and Windows would still offer it for an internal drive on 2000
to 8. So the update, once the new INF is staged, removes the published
copy of **this package's own earlier** `stor98.inf` - the one the
install record names (its `oemNN.inf`, the name `Stor98Inf` carries on
8.1 today and the record carries on every system from `3.1.0.0`), or
one whose `Provider` and `DriverVer` version are this project's and
below the package's - with `SetupUninstallOEMInf` and its force flag on
XP and later, as `/restage` does. Windows 2000 has no
`SetupUninstallOEMInf` (`/restage` refuses there for that reason), and
the installer's own removal of a published INF on 2000 (`s98_un_inf_nt`,
the uninstall's route) deletes the `oemNN.inf` and its `.PNF` by hand
only while **no devnode names it** in its `InfPath`. The update brings
every present Stor98 drive onto the new INF (record 23 section 4.4's
update of the devices Stor98 runs), after which they name the new one;
a drive not present at the run - an enclosure left unplugged - still
names the old INF, and on 2000 the old INF then stays until a later run
finds nothing naming it and removes it. That limitation is stated for
2000 (the installer says so in its log and its summary when it leaves
the INF: "kept while <device> names it"); on XP and later the forced
removal takes it at once and a bound devnode keeps its driver and its
files, its next reinstall matching the new INF by the id it gains at
its next enumeration (section 4.6). Where the shared-file rule finds
another Stor98 (a release that is not this package's), nothing is
removed, as nothing is changed there, and the limitation is stated: that
INF may still offer Stor98 for an internal drive until the user removes
it.

On 98 SE and ME `s98_inf_refresh_9x` makes the INF copy each Stor98
driver key names the bundled one's, and leaves a key whose `InfPath`
matches no copy or more than one (`s98_inf_find`'s count other than one,
logged "left"). An earlier copy no driver key names - an orphan of a
removed drive - or one left as ambiguous therefore keeps its
`PCI\CC_010802` model and can still offer Stor98 for an internal drive
through the wizard's search of `INF\OTHER`. So the 9x refresh gains a
second pass over `INF` and `INF\OTHER`: every copy that is **a Stor98
INF of this project's** - its `[Version]` `Provider` this project's,
its sections naming `stor98.mpd` and the `stor98` service and no other
driver's file, and its `DriverVer` version below the package's - is
replaced by the bundled INF under its own name, whether or not a driver
key names it. The Provider alone is no test: `xhci98.inf`,
`xhciuas.inf` and `xhcicm.inf` carry the same Provider and version, and
an older copy of one of them must never become `stor98.inf`. The identity
test is new work: the installer's `s98_file_names_stor98` only finds the
`stor98.mpd` token in an INF's text, so the sweep's test parses the
`[Version]`, `[SourceDisksFiles]`, the copy-files and the service
sections of the copy (the INF reader the installer already has for the
package's own INFs), requires `stor98.mpd` and `stor98.sys` as its only
files - listed, or copied by section or by name - and `stor98` as its
only service, a service required unless `stor98.mpd` is its one file (the
98 SE and ME INF, `stor98w9.inf`), and refuses a copy that names any
other file or service, with host vectors for an `xhci98.inf`,
an `xhciuas.inf` and a Stor98 INF with an added file (section 9.1). A
copy that cannot be read, or is not this project's Stor98 INF, is left
and said. Section 9.2's T.7 reads both halves.

### 7.2 Present drives on NT: per device, by model selection

**Candidates by class, not by the INF.** The installer's device read
matches every devnode against the package's INFs and keeps only the
matched ones (`inst_eng.c`, the read's `inst_ids_match` over `infIds`
and its `d->Inf < 0` skip), and the Stor98 baseline and shared-file
reading do the same (`s98_base`, and `s98_base_9x`'s `Enum` read on 98
SE and ME). With the id alone in `stor98.inf`, a tunnel drive with plain
ids - every drive at a first install, and any drive the chain has not
reached - would never become a candidate. So Stor98's candidates are
found by **class**: a devnode whose compatible ids carry `PCI\CC_010802`
is Stor98's (`INST_INF_S98`) for the plan, the baseline, the shared-file
reading and the uninstall's inventory, whether or not the INF's ids
match it - the same test the mark service makes (`mk_s98_wanted`'s
`IsPci` and `Nvme`), as one pure function of `inst_core.c` with the
vectors of section 9.1. The INF's ids are then used for what the store
needs and nothing else.

A present tunnel drive at the installer's run is found by place as today
(`inst_s98_place`: behind a tunnel, from the connection manager's
confirmed reading); at a first install of the package the connection
manager may not be running yet and the drive has no id, and at an update
it may or may not. So the installer's Stor98 install on NT no longer
goes through `UpdateDriverForPlugAndPlayDevices` with a hardware id,
which reaches every device of that id and needs the INF to match it: it
installs **each eligible devnode by itself**, by the model-selection
route of section 6.2 (the same steps, in `inst_eng.c`, on the
devnode's own `SP_DEVINFO_DATA`; on 2000 and XP through
`DIF_INSTALLDEVICE`, where the installer, running in the user's
session, may show the signing question the service cannot), unforced on
a device with no driver,
and as a forced replacement where the plan says an update or a `/force`
reinstall of a device Stor98 runs. What that removes: `hwid_clear` for
Stor98's INF, the "drives sharing it" refusal, and the per-id grouping of
flags; a tunnel drive is installed whatever an internal drive of the
same model carries. What stays: record 23 section 4.4's gate (eligible
only behind a tunnel with the Thunderbolt component in the run;
"skip-internal", "skip-unknown" and exit 4), the settle, the barrier and
the reconnection rules of record 26 section 6.3, and the Stor98 record.

On 8.1, case 3b (record 26 section 5.3.1) stays for the drives present
at the run: a tunnel drive on `stornvme` is installed over by the same
per-device route, forced, when Stor98's box is ticked. On 10 it is the
opt-in it was. On 8 and 2000 to 7 a present drive with no driver is
installed unforced.

A tunnel drive the installer leaves plain (unknown place at the end of
the run, exit 4) is a drive section 6.2 reaches once the connection
manager runs and confirms its port: on Vista to 8.1 the installer's
`LEFT:` text says the mark service puts Stor98 on it then, in place of
"run the installer again"; on 2000 and XP, where the service only
restarts a switch enclosure (section 6.2), it says the drive gets Stor98
when the enclosure is plugged in again, or at the next run of the
installer.

### 7.3 The drives the chain tags later

A drive plugged after the install is Windows' to bind, by the id
(sections 4 and 5); the installer takes no part. An update run finds
such a drive on Stor98 and updates it as record 23 section 4.4 says.

### 7.4 98 SE and ME

The file-never-a-running-device rule (record 23 section 4.4, revision
23) and the `WININIT.INI` update stand. The installer reads and stages
`stor98w9.inf` on 98 SE and ME in place of `stor98.inf` (section 5.2): a
present drive with no driver at a first install is matched by its class
and installed by record 27's helper as before this record; the INF
refresh and sweep of section 7.1 write `stor98w9.inf`'s text into the
copies they replace; the 9x `LEFT:` sentence for a drive of unknown place
stays the one of `3.2.0.0`.

**`stor98.mpd` placed before the first plug (task 44.77, 2026-10-28).**
Read in task 44.78 on 98 SE: with no `stor98.mpd` installed, the first
device install of a tunnelled drive found the INF on `DevicePath` but
asked for "Stor98 Storage Controller Disk", looking for the file in
Windows' own source; with the file already in `SYSTEM\IOSUBSYS` it
asked nothing (`3.2.0.0` asks too). So a run that includes Stor98, with
no other Stor98 sharing the file and no `stor98.mpd` there, puts the
bundled file in `SYSTEM\IOSUBSYS` itself before any device installs
(`inst_s98_mpd_place`): written as `STOR98.NEW`, checked against the
package's version, moved, recorded as the record's `MpdPlaced` (its
SHA-256). Nothing runs it until a drive binds, so no restart is owed.
An installed `stor98.mpd` stays the file update's. The uninstall
removes it with the bundled Stor98, under the record's rules as
before (section 7.5).

### 7.5 The uninstall

Unchanged: Stor98 is removed from the devices its record names, and
the service's removal takes the value and the records with it (record
26 section 9.3). The id stays in a devnode's recorded ids until the
connection manager's filter is gone and the devnode is enumerated again
without it, and matches nothing by then. On 2000 and XP the Critical
Device Database key Windows wrote for the id is deleted by the general
rule for keys that name a service of the package the run removed or
found absent, so it stays while Stor98 stays for a drive that still
uses it; no code was added for it (owner, 2026-10-26; section 6.4).

## 8. The release notes, the readmes and the installer's texts

- The release notes' Stor98 paragraph and `README.md`'s: "Stor98 is only
  for NVMe drives behind a Thunderbolt or USB4 port" stays; "On 2000 to 7
  Windows may offer it for an internal NVMe drive with no driver:
  decline" and the Windows 8 sentence go, replaced by: the package gives
  a drive behind a Thunderbolt or USB4 port an id of its own and
  Stor98's INF matches that id alone on Windows 2000 to 8, so Windows
  never offers Stor98 for an internal drive there (on 8.1 and 10 the INF
  keeps `3.2.0.0`'s class model, which `stornvme` outranks on an internal
  drive; on 98 SE and ME, which have no NVMe driver of their own, Windows
  may still offer it for an internal NVMe drive with no driver, to be
  declined, section 5.2) - with the one exception section 7.1 leaves: on
  Windows 2000, after an update over `3.2.0.0` or earlier, that
  release's INF stays in the INF directory while a Stor98 drive that was
  not plugged in at the update still names it, and may be offered for an
  internal drive until a later run of the installer removes it (the
  installer says which drive names it); what binds at the plug on each
  system (section 4.7's last column); and the rough edges: on Vista to
  8.1 a drive that Windows enumerated before the id reached it (the first
  plug of a never-seen enclosure, a port the package does not know) gets
  Stor98 from the safe-removal mark service within seconds; on 2000 and
  XP the service restarts the enclosure once its bridges are installed
  and Windows' wizard then offers Stor98, and unplugging the enclosure
  and plugging it in again does the same (a drive directly on a port the
  package does not filter has no automatic route there: the installer
  or Device Manager); on 98 SE and ME the wizard offers Stor98 by its
  class at the plug, as before.
- The package's generated readme (`make-release.ps1`'s Stor98
  paragraph) and Stor98's `docs/package-readme.txt` the same, under
  Stor98's rules for its own file; `stor98-settings.txt`'s comparison
  (source-files.md) is unchanged by it.
- The release notes' targets table, 8.1's row: "put on the drive by the
  installer or, for a drive plugged in later, by the safe-removal mark
  service" stays, with "Windows binds its own `stornvme` first; the
  service then puts Stor98 on the drive within seconds; an internal drive
  keeps `stornvme`".
- `README.md`'s support table, the 8.1 row, likewise.
- The installer's `LEFT:` sentence for a drive of unknown place on NT
  names the service (section 7.2).
- "Known limitations": `XhciCmSafeEject` 0 leaving no fallback; a port
  the filter INF does not name; on 2000 and XP the first plug of a
  never-seen enclosure with a switch, which needs the service's restart
  or a re-plug; on 8.1 a drive the arm cannot stop (an open file) staying
  on `stornvme` until a re-plug (section 11).
- The tunnel id itself, stated in `docs/using/release-notes.md` under
  the Thunderbolt section and in `XHCITR`'s README where it lists what a
  device's ids show, so that a user who sees
  `PCI\XHCI98_TUNNEL&CC_010802` in Device Manager knows what wrote it.

## 9. Verification

### 9.1 Host vectors

- `test_cm_ports` (`src\cm\cm_ports.c`): `CmPortsTunnelIds` - an answer
  with `PCI\CC_010802` gains the id first and keeps every string and the
  double terminator; an answer without it is left (a bridge's
  `PCI\CC_0604`, a USB controller's `PCI\CC_0C0330`, an empty list, a
  null buffer); case and a list whose last string is the class; a list
  at and over the size bound; an id already present (idempotent); the
  depth bound and the re-attach case of `CmPortsChildFirst` (a PDO in
  the snapshot with no child entry).
- `test_cm_tagid` (`src\cm\cm_tagid.c`): the id test on a 9x comma list
  and an NT `MULTI_SZ` (whole entries, case, spaces); the tag decision
  (class, id present, unread); the per-arrival record (a handle reused
  with another instance id is a new arrival, the table's bound).
- `test_cm_marks` (`src\cm\cm_marks.c`, `cm_ports.c`): the `Removable`
  gate for a kernel-attached child on a bridge that is not active, not
  exclusive, or without safe eject, and on one that is all three.
- `test_inst` (`xhciinst\inst_core.c`): Stor98's candidates by class
  (`PCI\CC_010802` among the compatible ids, a bridge's and a USB
  controller's refused); the eligibility with the shared-id refusals
  gone; the per-device action from the place, the bound service and the
  plan; the earlier INF's removal rule (this package's own below the
  version, another Stor98's left) and the 9x sweep's identity test (a
  Stor98 INF taken, `xhci98.inf`, `xhciuas.inf` and a Stor98 INF with an
  added file or service refused); the policy value's systems (2000 to
  8.1 write, 10 delete).
- `xhcimark`'s self-test (`mark_core.c`): `mk_s98_wanted` for the
  fallback - no service goes on 2000 to 8.1, `stornvme` goes on 8.1
  alone; on Stor98, on another driver, pending, protected, not below a
  verified port, on 10 each refused with its reason text; the "give"
  record and its recovery (done or failed, never an enable); the action
  per system (install on Vista to 8.1, unit restart on NT 5.x, nothing on
  10); the unit restart's gate over a subtree (bridges started with a
  driver and driverless NVMe functions, one without the id: go; a running
  drive, a dock's xHCI, a function with another problem, an unread
  subtree, a bridge not yet installed: each refused; every candidate
  carrying the id: nothing), its budget (one per unit arrival in the
  service's run, spent only at the call; a veto spends it; a lock held or
  an installation pending defers it).
- `check-stor98-inf.ps1 -SelfTest`: the model rule of scope B (the id
  alone, as the hardware id, before NT 6.3; the class as the compatible
  id, hardware id empty, in `6.3`; no class match in any other model line;
  `stor98w9.inf` the id and the class, no NT section, `stor98.mpd`
  alone), its mutants refused.
- `test-package.ps1`: the INFs staged as before.

### 9.2 The legs (task 44.3)

Each guest has an internal NVMe on its root bus (`-device nvme` on
`pcie.0` or `pci.0`) and one behind the emulator's tunnel port, in two
shapes: the direct one (`usb4-device device-model=asm2464` on i440FX, which
with emulator build `2a893ea` puts the drive directly on the tunnel bus,
read 2026-10-25) and the switch one (the explicit chain behind the tunnel
bridge, `usb4-asm-pci-usp` -> `pci-bridge-seat` -> `nvme`, as the m32 N4
kit's `-Chain` builds it); each leg records the drive's PCI parent chain
from the setup log or Device Manager, so the two shapes are told apart. Read with
`XHCITR /snapshot` and the connection manager's counters
(`cm.reenum.id.given`), Device Manager's id lists and the setup log
(`setupapi.log` on 2000 and XP, `setupapi.dev.log` on Vista and later)
or `SETUPAPI.LOG` and the `Enum` key (98 SE and ME):

| Leg | Systems | What is read |
|---|---|---|
| T.1 the internal drive | 98 SE, ME, 2000, 7, 8 (x86 and x64), 8.1, 10, both shapes, with Stor98 staged | no id on its devnode; no driver on 2000 to 7 (the wizard offers nothing), Stor98 offered by the class on 98 SE and ME (`stor98w9.inf`, section 5.2), `stornvme` on 8.1 and 10, untouched across every other leg |
| T.2 the direct drive, hot plug | the same, 8 x86 and x64 included | the id first in `CompatibleIDs` on NT, appended to it on 98 SE and ME; Stor98 bound by Windows at the plug on 2000 to 8 (on 98 SE and ME by the wizard, through `stor98w9.inf`'s class match, section 5.2); on 8.1 `stornvme` first and the service's 8.1 arm within seconds (Stor98 at the plug, the rank `0x00FF2000` against `0x00FF2006` in `setupapi.dev.log`, only with the certificate trusted); `stornvme` on 10; the volume mounted |
| T.3 the switch drive, first plug | 2000, XP, XP x64, 7, 8.1, 10 | the chain through the enclosure's first-time bridge installs (lost on 2000 and XP, section 4.3; read on 7 and later); on 2000, XP and XP x64, where no drive has yet installed Stor98 through the id (no `pci#xhci98_tunnel&cc_010802` key in the Critical Device Database, section 6.4) and with the mark's own restart off (`Restart` 0), the service's unit restart once the bridges are installed (its "restart made" line), the drive back with the id and Windows' wizard installing Stor98, its log lines; the refusal for a unit with a running function (a second, mounted drive behind the same switch), a veto (an open handle on the unit), a mark restart after the bridges' installs counted as the tag restart, two drives below one unit sharing one restart, a declined wizard left alone; on 7 and 8.1 the service's install within seconds (over nothing on 7, over `stornvme` on 8.1); the re-plug tagged everywhere; with the key present, the drive starting on Stor98 through it with no wizard |
| T.4 the switch drive on 98 SE and ME | 98 SE, ME | the worker's `Add_ID` on the descendant at a hot plug (appended; where the window reaches it); Stor98 installed by the wizard through `stor98w9.inf` at the plug, ME's automatic search included; a boot with the drive present (the start baseline); the internal drive offered Stor98 by the class, as before |
| T.5 pre-boot tunnel | 98 SE, 2000, 7, 8.1 | `preos-cm=on`: on NT the drive tagged at boot through the bridge filter, and on 2000 and 7 Stor98 bound before the connection manager verifies the port; on 8.1 `stornvme` first and the service's 8.1 arm once the port is verified (Stor98 first only with the certificate trusted); on 98 SE the drive installed by the class match at its first boot, and the id given at the worker's start baseline once the member is active |
| T.6 present at the install | 98 SE, ME, 2000, 7, 8.1 | a plain drive found as a candidate by class and installed by the installer's per-device route on NT (on 2000 with the signing question answered in the installer's session); on 98 SE and ME the helper installing it through `stor98w9.inf`'s class match; `hwid_clear` gone: an internal and a tunnel drive of the same model, the tunnel one installed |
| T.7 the update from `3.2.0.0` | 98 SE, ME, 2000, 7, 8.1 | a drive bound by the class match stays bound; it gains the id at the next enumeration; on 7 and 8.1 `3.2.0.0`'s `stor98.inf` gone from the store at once; on 2000 gone when every drive was present at the update, and kept and said when a drive was unplugged, then gone at the next run with that drive present and updated; an internal NVMe hot-added after the removal offered nothing; on 98 SE and ME the copy a driver key names refreshed, an orphaned copy (its drive's key deleted by hand before the update) and a second copy of the same name in `INF\OTHER` both replaced, and an older `xhci98.inf` copy left alone; the internal drive on `stornvme` untouched |
| T.8 the 32-bit package on 8.1 | 8.1 x86 | `stornvme` binds the uncatalogued package's drive (section 4.7); the service's 8.1 arm installs Stor98 over it |
| T.9 the uninstall | 2000, 7, 8.1 | Stor98 given back; the id left harmless; a re-install finds the drive again |
| T.10 the passive child | 2000, 7 | the NHI's and the host xHCI's start and the connection manager's own work with a passive child filter on their PDOs (section 4.1) |
| T.11 the policy values | 2000, 7 | `XhciCmSafeEject` 0: no service, no install and no unit restart, the chain still tagging, a re-plug of an installed switch enclosure tagged; `XhciCmMode` 0 on a `usb4-generic` host with a pre-OS tunnel: the drive tagged and bound with the connection manager inert |

T.1 and T.2 are also read on XP, XP x64, Vista x86, Vista x64, 7 x64,
8 x64, 8.1 x64 and 10 x64 (owner, 2026-10-25: 44.3's behaviour read on
every supported system), after the legs above and with no checkpoint tax;
on 10 x64 that is the id given, `stornvme` bound, and Stor98 put on the
drive by the installer's case 3b. The x64 legs run the unsigned package
signed in the guest with a throwaway certificate, as task 43.36 did; a
system with no guest image here is listed as owed in `runs/run-44.md`. Real hardware is
44-E.2's (the ORICO enclosure, one with an internal switch): T.3's shape
on metal.

### 9.3 Readings before the code

1. **The passive child on the host's own functions** (section 4.1): on
   2000 and 7 in the emulator's `tb3-alpine-ridge` profile, a child
   filter of this driver attached to the NHI's and the xHCI's PDOs at
   their enumeration, both drivers starting and running as before. If
   anything costs, the attach is gated on the class read first.
2. **The restart's order** (section 4.3 [hyp]): on 2000 and XP, then 7,
   8.1 and 10, with the switch shape, whether the first-time install of a
   bridge below the tunnel port is followed by a `BusRelations` query of
   its parent before the bridge's own start, so the re-attach is in place
   for the bridge's children; and whether Vista and later lose the child
   at all. A trace build of the filter logging each `BusRelations` and
   each attach, read against the setup log's timestamps (`setupapi.log`
   on 2000 and XP, `setupapi.dev.log` on 7 and later).
3. **The id as recorded, and the signature score without the
   certificate**: that the PnP manager records the rewritten
   `CompatibleIDs` and ranks with them, on 2000 (`setupapi.log`) and on
   8.1 x64 (`setupapi.dev.log`, `0x2000` against `0x2006`), the latter
   with the project's test certificate **not** imported into any store,
   which is where the signer score of the test-signed catalogue is
   unread (section 2.1); the 8.1 row of section 4.7 is written from the
   result.
4. **Two decorations on the 9x models line** (section 4.7): 98 SE and ME
   installing from an INF whose `[Manufacturer]` line carries
   `NTx86.6.3,NTx86.10.0`, read in the emulator before the INF is
   committed; the INF gate's rule written from the result.
5. **`Add_ID` from the worker** (section 5.1): on 98 SE and ME, the id
   appearing in the devnode's `CompatibleIDs` after the call, the return
   code, and whether `PCI.VXD` rewrites the list at the next boot.
6. **`Reprocess_DevNode` on a devnode whose install found no driver**
   (section 5.2), with task 44.30's reading: whether it runs the install
   again; `Setup_DevNode(CM_SETUP_DEVNODE_READY)` read in the same leg if
   not.
7. **The 9x helper's model selection** (section 7.4): whether record
   27's helper can install a selected model whose id the devnode does
   not carry, or the worker's route is the one for a present drive.
8. **The per-device route** (sections 6.2 and 7.2): the devnode's class
   set to `SCSIAdapter`, a class-driver list over the staged INF alone
   yielding Stor98's model for a devnode whose ids it does not match,
   then `DIF_REGISTERDEVICE` and `DIF_INSTALLDEVICE` on 2000 and XP and
   `DiInstallDevice` on 7, with the drive started and the volume mounted
   after it; read in the emulator before the code, from a test build of
   the service's `-once` mode.
9. **The failed install cleared on 98 SE and ME** (section 5.2, Q1's
   option (b)): read 2026-10-25, by hand - it does not work (section
   9.4); option (a) taken.
10. **The id across a restart of a drive's own stack** (section 4.8),
   each a reading of its own, on the systems named: (a) the 8.1 arm's
   first switch (read on 8.1 x86: the id lost, `stornvme` back); (b) a
   drive Stor98 already runs, tagged, disabled and enabled in Device
   Manager, on 7, 8 and 8.1 (x86; x64 where an image is at hand) and 10
   (opted in); (c) the installer's update of the drives Stor98 runs, and
   a `/force` reinstall, on 7 and 8.1; (d) case 3b on 8.1 and 10; each
   with the binding, the recorded ids and the volume read after it, in
   both shapes and with `XhciCmSafeEject` 0 once. These read today's
   behaviour; they say nothing yet of section 4.8's design.
11. **Section 4.8's prototype under Driver Verifier**: option (A)'s own
   task, in the next release.
12. **The 8.1 fix** (section 4.8, owner 2026-10-25; read, section 4.8's
   dated paragraph): why 8.1 chooses a
   driver again at the install's restart where 7 and 8 keep it (the
   setup log at the restart; the driver key's `MatchingDeviceId`), and
   whether a user-mode route holds - the install made without its
   restart (`DI_DONOTCALLCONFIGMG`), the driver key's matching id set to
   one the re-read keeps, then the restart, a disable and enable and a
   boot - read on 8.1 before a design is written.

### 9.4 The readings, 2026-10-25

Taken in the emulator on the qemu build of commit `949abab8`
(`runs/run-44.md`, "44.3", has the systems, command lines and hashes).

**Taken:**

1. The passive child (item 1), **on 7 x86 only**: in the
   `tb3-alpine-ridge` profile the NHI and the Thunderbolt xHCI started
   with a child filter of this driver on their PDOs, no problem code, the
   connection manager working, and a drive below the enclosure's switch
   tagged at depth 3 and bound to Stor98 by Windows' ranking
   (`0x80FF2000`, the x86 package, no competitor). The attach stays
   ungated on that reading; 2000 is owed (T.10), and a cost read there
   gates it on the class as section 4.1 says.
2. The restart's order (item 2), on 2000 and XP: section 4.3's dated note
   - the [hyp] does not hold; a re-plug is tagged.
3. The id as recorded (item 3): on 2000 the id is first in the recorded
   `CompatibleIDs` and `setupapi.log` finds Stor98 by it ("Found
   PCI\XHCI98_TUNNEL&CC_010802 in ... oem7.inf"); an internal NVMe gets
   no id and no offer. On 8.1 x64 without the certificate the package
   ranks unsigned (section 4.7's 8.1 row).
4. Two decorations on the 9x models line (item 4): 98 SE and ME install
   from the undecorated section.
5. `Add_ID` (item 5): returns 0, appends, survives a boot (section 5.1).
6. `Reprocess_DevNode` and `Setup_DevNode(READY)` (item 6): install
   nothing on a failed install (section 5.2); and the tag window can run
   out behind the wizard (section 5.1).
7. The 9x helper (item 7): "no-match" for a present plain drive (section
   5.5).
8. The per-device route (item 8): on 2000, `DIF_INSTALLDEVICE` from the
   service installs Stor98 on a drive whose ids the INF does not match
   under the signing policy Ignore, with no `DIF_REGISTERDEVICE`; under
   Warn 2000 waits on the question and XP refuses it (section 6.2). On
   8.1 x64 `3.2.0.0`'s switch's `DiInstallDevice` installed with no
   question, the certificate not trusted.
9. The 8.1 arm of the first code on 8.1 x86 (T.2's direct shape): the id
   given at the plug (first in `setupapi.dev.log`'s compatible ids),
   `stornvme` bound first, the arm's install answered success and the
   drive came back on `stornvme` with its ids re-read plain (section
   6.1's dated note).

9. The failed install cleared (item 9, Q1's option (b)), by hand with
   `.REG` files on 98 SE and ME: `ConfigFlags` 0 with the `Class` value
   kept, and the devnode's `Enum` key rewritten with the ids, the id and
   `ConfigFlags` 0 and no `Class`: no wizard and no install at a hot
   re-plug or at a cold boot with the drive present, on either system;
   `ConfigFlags` 0x20 reinstalls at the boot but drops the id first.
   Option (b) does not hold for any arrival but the 98 SE hot plug that
   already worked; option (a) is taken (section 5.2).
10. The restart's order on 7 (item 2, the switch shape): the bridges'
   first-time installs ran before they started, nothing was removed, the
   drive was tagged at its first plug and Windows bound Stor98 (rank
   `0x80FF2000`, the x86 package); the loss is 2000's and XP's.

**Owed:** item 10 (b) on 8.1 for a drive already on Stor98 (none could
be made under the id-only model; under scope B the class model's
matching id survives, read in the legs), on x64 and on 10; (c) and (d)
in the legs under scope B; item 12 (the 8.1 fix); item 2 on 8.1 and 10 with
the switch shape; item 1 on 2000; item 8 on 7 (`DiInstallDevice` on a
driverless devnode); the unit restart of Q2 itself is read in T.3,
its vehicle (the mark's restart giving the id back) having been read on
XP once.

**Read 2026-10-28 (task 44.77): item 10 (c), the update from `3.2.0.0`
of a tunnel drive Stor98 runs, on an ASMedia ASM4242 host** (the
emulator's board-like layout of task 44.45; 7 x86, XP and 2000's ACPI HAL,
`runs/run-44.md` "44.77"). The first run of `3.3.0.0` read the place from
the connection manager then running, `3.2.0.0`'s: its record was
confirmed and listed no tunnel port, because `3.2.0.0` had no family for
the ASM4242 (its `1B21:2423` was in the unpaired list until task 44.40),
so the drive read as below an unverified port, of unknown place. A Stor98
of unknown place at another release holds the shared file (record 23
section 4.4, "The shared file and service"), so Stor98 stayed at `3.2.0.0`; the run after
the restart, under `3.3.0.0`'s connection manager, updated it. Reading the
place again later in the same run does not help on 2000: the record still
listed no port after the connection manager's own update (the old image,
which the tunnel-port filters keep loaded until the restart, is the
likely reason; not traced).
Nothing else establishes the place soundly (the installer's own record
of a drive it found Windows had bound is not one: up to `3.2.0.0` the INF
matched every NVMe controller). So the rule stays and the texts say what
to do: when the run installs or updates the connection manager on a
present controller, a present drive run by an older Stor98 at an unknown
place is read again after the restart, and its LEFT line, the shared
file's note and the run's end say "run the installer again after the
restart" instead of "update Stor98 by hand" (`inst_tb_cm_changes`,
`inst_s98_read_later`, `inst_s98_later_said`, `inst_s98_left_text`),
except where another Stor98 drive the reading would not free (an
internal one, or one not read again) holds the shared file too, which
keeps the earlier texts. The run after the restart
updates Stor98 and asks for the drive's re-plug as any update of a tunnel
drive does. The other hosts' tunnel ports were in `3.2.0.0`'s families,
so an update over them should read the place in the first run (not read
here).

## 10. Decisions

The owner's, 2026-10-25, on this record's questions:

- **D1** (bounded to 2000 to 8 by section 4.8's reading under scope B:
  8.1 and 10 take no id) The structural rule on Windows 2000 and later: every NVMe-class
  function below a bridge the filter INF names as a tunnel port carries
  the id, at any depth, decided by the filter alone at enumeration
  (section 3.4). Task 44.32 keeps a filter of this driver in the tunnel
  port's stack (section 4.5).
- **D2** (revised by Q1 below: the class match kept on 98 SE and ME
  through `stor98w9.inf`, the id a statement there) Windows 98 SE and ME: `Add_ID` and `Reprocess_DevNode` from the
  connection manager's CONFIGMG worker (section 5), over pre-seeding the
  `Enum` key and over keeping the class match on 9x.
- **D3** (made 8.1's ordinary route by Q3 below) The mark service's 8.1
  switch is retired as the route in 44.3; the ranking binds Stor98 on 8.1
  where it can. Revised after review (section 6.1): its displacing
  install stays as the fallback's 8.1 arm, because 8.1 binds `stornvme`,
  not nothing, to a drive the id or the rank did not reach. Confirmed in
  this shape by the owner, 2026-10-25.
- **D4** Windows 10 keeps task 43.59's policy: `stornvme` the default,
  Stor98 opt-in per drive (section 4.7).
- **D5** (revised by Q2 below for 2000 and XP: the unit restarted, not
  an install) Id first, service fallback: a drive that arrives without the id
  on 2000 to 8.1 is installed by the mark service per device by model
  selection; the installer uses the same per-device route for present
  drives (sections 6.2 and 7.2).

The orchestrator's, in writing this, for the owner to confirm:

- **D6** The string `PCI\XHCI98_TUNNEL&CC_010802` (section 3.1).
- **D7** First among the compatible ids, the hardware ids untouched
  (section 3.2).
- **D8** NVMe-class functions only, by `PCI\CC_010802` in the bus
  driver's compatible ids (section 3.3).
- **D9** (revised by the scope decision, section 4.8) The section read
  before NT 6.3 gives the id as the hardware id; the `6.3` sections keep
  `3.2.0.0`'s class as the compatible id; no `10.0` section (section
  4.7).
- **D10** The child filter attached on every filtered bridge, the
  `Removable` answer gated for a kernel-attached child as for a
  registered one (section 4.1); the chain through bridges bounded at
  depth 8, counted from the topmost filtered bridge (section 4.3).
- **D11** The new id list from paged pool through `cm_pool.c`'s one new
  helper, freed by the PnP manager's `ExFreePool` (section 4.2).
- **D12** The fallback takes a devnode with no driver on 2000 to 8, and
  on 8.1 one with none or on `stornvme`; it never displaces Stor98 or
  any other driver (section 6.2; on NT 5.x by the unit restart of Q2).
- **D15** The update removes this package's own earlier `stor98.inf`
  from the driver store; another Stor98's INF is left and the limitation
  stated (section 7.1).
- **D16** Stor98's candidates in the installer are found by the NVMe
  class in the compatible ids, not by the INF's ids (section 7.2).
- **D13** The policy value `Stor98Inf` written on 2000 to 8.1, deleted
  on 10 (section 6.2).
- **D14** The installer's Stor98 install per devnode by model selection,
  `UpdateDriverForPlugAndPlayDevices` no longer used for Stor98's INF
  (section 7.2).

The owner's, 2026-10-25, on the questions the readings of section 9.4
put (Q1 to Q3):

- **Q1** Windows 98 SE and ME (D2 revised): `Reprocess_DevNode` does not
  re-run an install that found no driver. Decided: option (b) read first
  - the worker clears the devnode's failed install so that a re-plug or
  a start runs the wizard again, which finds Stor98 by the id (section
  5.2), keeping 98 SE and ME tunnel-only; read on 98 SE and ME for a hot
  plug, ME's automatic search, a drive present at a boot and one present
  at the install (section 9.3 item 9). If it fails any of them, option
  (a): Stor98's class match on 98 SE and ME alone, NT reading a decorated
  section with the id alone, with its decoration reading. Set aside:
  (c), the `Enum` key pre-seeded. **Read 2026-10-25:** (b) failed
  (section 9.4); (a) taken, and since Windows 2000 reads no models
  decoration the owner chose option (a2) the same day: a separate INF,
  `stor98w9.inf`, with the class match, staged on 98 SE and ME alone,
  and `stor98.inf` the id alone for 2000 to 8 (section 5.2; 8.1 and 10
  by the scope decision, section 4.8).
- **Q2** Windows 2000 and XP (D5 revised): the service cannot install the
  unsigned package there. Decided: option (a) - once the enclosure's
  bridges are installed the mark service restarts the tunnel unit, the
  drive comes back with the id and Windows' own wizard installs Stor98,
  the user answering the signing question (section 6.2); the release
  notes also state that a re-plug does the same (option (b) as the
  text). This record reads "2000 and XP" as NT 5.x, taking in XP x64 and
  Server 2003 x64, whose test-signed package a service is expected to be
  refused alike; for the owner to confirm (the legs read XP x64). The
  route's budget, gate and shared restart coordinator are the record's
  (section 6.2).
- **Q3** Windows 8.1 (D3, noted): without the certificate trusted the
  package ranks unsigned, so the mark service's 8.1 arm is the normal
  route on 8.1 in both architectures; a change of text only (section
  4.7's 8.1 row, section 8).
- **Scope** (section 4.8, owner 2026-10-25): option (B) - 98 SE and ME
  by `stor98w9.inf`'s class match, 2000 to 8 the id alone, 8.1 and 10
  `3.2.0.0`'s class model; option (A), the kept child, a task of the
  next release, prototyped and read under Driver Verifier first; for 8.1
  a proper fix sought, read and designed before its code and put to the
  owner (section 9.3 item 12).
- **CDDB** (owner, 2026-10-26) No new code for the Critical Device
  Database key Windows 2000 and XP write for the id: the uninstall's
  general rule for keys that name a service of the package the run removed
  or found absent deletes it (it stays while Stor98 stays for a drive that
  still uses it), and it is harmless either way (section 6.4).
- **44.52** (owner, 2026-10-27) On 2000 and XP the safe-removal mark's
  restart sets the install of each plain driverless drive below the unit
  to run again, the tag restart's flag step, so that Windows offers
  Stor98 after it; never a function with a driver (section 6.2, *The
  mark's restart*).

## 11. Open, and the risks

- **98 SE and ME keep the class match** (section 5.2): Windows may offer
  Stor98 for an internal NVMe drive there, as before this record; the
  worker's id is a statement only, and the tag window can miss a devnode
  whose wizard was open (section 5.1).
- **The id lost at a restart of the device's own stack** (section 4.8):
  on 8 to 10 a Stor98 drive that restarts its stack may leave Stor98; the
  design is put to the owner with the scope options, not coded.
- **The first plug of a never-seen switch enclosure on 2000 and XP**
  arrives untagged (section 4.3, read): the service's unit restart once
  the bridges are installed, or a re-plug. The restart is refused for a
  unit that holds anything running, and a veto spends the budget: the
  user's re-plug is then the route, and the release notes say so.
- **No automatic route on 2000 and XP** for an untagged drive directly on
  a port with no filter, a depth or pool failure, or a drive the
  installer could not take at its run (section 4.4): the installer's next
  run or Device Manager.
- **The passive child on the host's own functions** (section 4.1): read
  harmless on 7; 2000 owed (T.10), and a cost there gates the attach on
  the class.
- **The 8.1 arm is best effort, not a bound** (inherited from the
  switch, record 21 section 7.5.4; `runs/run-43.md` "43.59" step 3): a
  drive the arm must take off `stornvme` is stopped first, an open file
  vetoes the stop, and after the attempts `mk_s98_retry` allows the drive
  stays on `stornvme` for that arrival, until a re-plug. Since the
  package ranks unsigned on 8.1 without the certificate (section 4.7),
  that is 8.1's ordinary route, not a corner.
- **8.1 keeps `3.2.0.0`'s class model** (section 4.8, scope B): there
  Stor98 is not tunnel-only by the id; the fix sought is section 9.3
  item 12. With the id-only model 8.1 had no working route at all, the
  installer's case 3b included (section 4.8's reading).
- **A declined wizard is final for that arrival** on NT 5.x: the service
  restarts only for a drive without the id (section 6.2).
- **An override-only tunnel port** has no filter and no id; on Vista to
  8.1 the service's install covers it, on 2000 and XP nothing automatic
  does (above), and on 98 SE and ME the worker covers it (the override is
  the member). A later task may have the installer install the filter
  INF on the override's bridges too.
- **Record 21 section 7.5.2 (a)'s objection** that the rewrite "changes
  what Windows records about the device": it does, by one compatible id
  at the head of the list (the tail on 9x), which the release notes
  state; the hardware ids are untouched.
- **Two filters of this driver on a unit's stack**: after a mark restart
  the unit holds its registered child and, where the bridge filter
  attached one above ACPI's bus filter at the plug, that kernel child
  too (read on 2000); both are passive or idempotent (the id rewrite
  finds the id present), as under `3.2.0.0`'s mark.
- **A device with the id and no Stor98** (Stor98 unticked, the
  Thunderbolt component installed): the id is given regardless and
  matches nothing; the drive has no driver on 98 SE to 8 as it had none
  before the package, and `stornvme` on 8.1 and 10. Whether the id
  should be given only when Stor98 is staged is a question for the
  owner; this record gives it always, since the id is a statement about
  the device, and `stor98.inf`'s presence is the user's choice.

## 12. Files

| File | Change |
|---|---|
| `src\cm\cm_filt.c`, `cm_reenum.c`, `cm_ports.c`, `cm.h` | the attach on every bridge, the `Removable` gate, `IRP_MN_QUERY_ID`, the chain, the re-attach, the counters (sections 4.1 to 4.3) |
| `src\cm\cm_pool.c` | `CmAllocPaged`, the id list's paged allocation (section 4.2) |
| `src\cm\cm_cfgwk.c`, `cm_cfgmg.c`, `cm_cfgmg.h`, `cm_tagid.c`, `cm_tagid.h` | the `Add_ID` thunk and the worker's tag pass (section 5.1) |
| `src\stor98\inf\stor98-x86.inf`, `stor98-x64.inf`, `stor98-9x.inf` (new) | the models (sections 4.7 and 5.2), under Stor98's rules |
| `scripts\package\package-common.ps1`, `make-package.ps1`, `test-package.ps1` | `stor98w9.inf` in the x86 package (section 5.2) |
| `scripts\inf-gate\check-stor98-inf.ps1` | the model rule and its self-test |
| `xhcimark\xhcimark.c`, `mark_core.c`, `mark_core.h` | the 8.1 arm, the install on Vista to 8.1 with its "give" record, setup non-interactive, the restart coordinator with the unit restart on NT 5.x, its gate and budget (section 6) |
| `xhciinst\inst_eng.c`, `inst_core.c`, `inst_core.h` | the candidates by class, the per-device route, the refusals gone, the earlier INF's removal, the value's systems, the texts (section 7) |
| `test\test_cm_ports.c`, `test\test_cm_marks.c`, `test\test_cm_tagid.c`, `test\test_inst.c` | the vectors (section 9.1) |
| `docs\contributing\design\21-thunderbolt3-icm-client.md` | section 7.5 amended: the switch retired, the fallback, the pointer here |
| `docs\contributing\design\23-install-upgrade-tool.md`, `26-windowed-installer.md` | section 4.4's "What remains" and case 3b amended |
| `docs\contributing\legal-provenance.md` | section 4's `cm_cfgmg.h` row: `Add_ID` 85 and its flags, `Write_Registry_Value` 63, `CM_PROB_FAILED_INSTALL`, and `REGSTR.H`'s `ConfigFlags` values, as read for section 2.4 |
| `docs\using\release-notes.md`, `README.md`, `scripts\package\make-release.ps1`, `src\stor98\docs\package-readme.txt`, `xhcitr\README.TXT` | the texts (section 8) |
| `docs\contributing\source-files.md`, `runs\run-44.md` | the files, the legs |

## Sources

- `roadmap-usb4.md` tasks 43.59, 44.2, 44.3, 44.30, 44.32; the Phase 44
  decisions row.
- Design record 21 sections 3.2, 3.3, 6.1 to 6.5, 7.4 (revisions 2 and
  3) and 7.5; record 22 sections 7.6, 9 and 10; record 23 sections 4.4,
  5.5 and 5.10; record 24 section 4.3; record 26 sections 5.3.1 and 6.3;
  record 27; record 28's dated note.
- Microsoft, "Identifiers for PCI devices" (the id forms `pci.sys` gives
  a function).
- `runs/run-43.md`, "43.59" (the ranks and signer scores of 2026-10-23,
  the surprise-unplug check); `runs/run-44.md`, "44.3" (the readings of
  2026-10-25).
- `src\cm\cm_filt.c`, `cm_reenum.c`, `cm_ports.c`, `cm_cfgwk.c`,
  `cm_cfgmg.c`, `cm_cfgmg.h`, `cm_tagid.c`, `cm_pool.c`; `src\stor98\inf\*.inf`;
  `scripts\inf-gate\check-stor98-inf.ps1`; `xhcimark\xhcimark.c`,
  `mark_core.c`; `xhciinst\inst_eng.c`, `inst_core.c`.
- The Windows 98 SE DDK's `CONFIGMG.H` (`docs/references/README.md`,
  "Windows 98 DDK"): the service table (lines 533 to 663), `Add_ID` at
  618, the `CM_ADD_ID_*` flags at 1252 to 1254, the prototype at 2888;
  `Write_Registry_Value` at 596 and its prototype at 2866,
  `CM_PROB_FAILED_INSTALL` at 1075; and its `REGSTR.H`, lines 959, 985
  and 986; values only.
- Microsoft, "How Windows Ranks Driver Packages", "Identifier Score",
  "Signature Score" (as record 21 section 7.5.1 read them, 2026-10-22).
- Stor98's `AGENTS.md` (the naming rule) and `docs/PLAN.md`.
