# Run 44 - Release `3.3.0.0`

Phase 44 of `roadmap-usb4.md`, for release `3.3.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 44.3 the readings before the code: Stor98 for tunnelled drives only (2026-10-25)

Design record 31 section 9.3's readings, taken before the code of each
part (the record's section 9.4 is the summary, and its sections 4.3,
4.7, 5.1, 5.2, 6.1 and 6.2 carry what they changed). Runtime,
development host A, QEMU under TCG, 11:50 to 15:45; several guests at
once, at most 8 virtual processors in all.

**Verdict: the NT filter chain and the id hold; four parts of draft 2
did not.** On 2000 and 8.1 the id is first among the recorded compatible
ids and Windows matches Stor98's INF by it; 98 SE and ME read the INF's
two decorations and `Add_ID` appends the id. But the re-attach of record
31 section 4.3 does not happen on 2000 or XP; `Reprocess_DevNode` does
not re-run a failed 9x install; the mark service cannot install the
unsigned package on 2000 or XP; and the 8.1 arm as first coded loses the
id across its own stop. The owner decided Q1 to Q3 the same day (record
31 section 10).

### The vehicle

- The build: the gated `qemu` x86 build of branch `p44-stor` at
  `949abab8` (`build-driver.cmd qemu`: "BUILD + GATES PASSED (qemu)"),
  staged with `make-package.ps1 -Flavor qemu -Arch x86` into scratch
  directories and laid into the `3.2.0.0` download's layout with the
  worktree's `XHCIINST`, `XHCIWIZ`, `SETUP`, `XHCIMARK` and `XHCITR`.
  The installer refuses a driver of the `qemu` flavour in `release-x86`
  ("a driver of another build flavour"), so the guests took either a
  test copy of `XHCIINST.EXE` with its wanted flavour word changed (2000,
  XP, 7; never published) or `3.2.0.0`'s release drivers with the `qemu`
  `xhcicm.sys` copied over the installed one before the first restart
  (98 SE, ME, 8.1). Hashes: `xhcicm.sys` (qemu) `4ad53921...`,
  `xhci98.sys` `869abaf8...`, `stor98.sys` `1f46ef08...`, `stor98.inf`
  `7bc6140d...`, `XHCIMARK.EXE` `bac6bcbf...`; the full lists are in the
  kits' `hashes.txt`, `hashes-p86.txt` and `pkg-hashes.txt`.
- The emulator: `vm\usb4\bin-2a893ea` (`6a1dfcc8...`). Profiles: the
  `usb4-generic` host with an SHPC tunnel bridge at 00:05 and an
  ASM2464 enclosure, which on this build puts the drive **directly** on
  the tunnel bus (the direct shape); the switch shape from the bridge
  chain `usb4-asm-pci-usp` (1B21:2421) -> `pci-bridge-seat` -> `nvme`
  behind the tunnel bridge (the "-Chain" topology of the m32 kit); the
  `tb3-alpine-ridge` profile (NHI and Thunderbolt xHCI below the switch
  ports the filter INF names); an internal `nvme` on the root bus in
  every guest.
- The guests, each a fresh overlay: 98 SE (`vm\t26\win98-clean.qcow2`)
  and ME (`vm\t37me\winme-stock-base.qcow2`), 1 vCPU, 512 MB; 2000 SP4
  (`vm\win2k-smp.img`), XP SP3 (`vm\winxp.img`) and 7 SP1 x86
  (`vm\win7.img`), 2 vCPUs; 8.1 x64 and x86 (task 43.24's prepared
  images), 2 vCPUs, 4 and 3 GB. The kits are `E:\xhci98-vm\p44\p44-stor\`
  `r9x`, `rnt` and `r81` (not in the tree; command lines, serial and
  debug-port logs, setup logs and screens are there).
- The connection manager's records came from the `qemu` flavour's
  port-0xE9 mirror: `cm.chain.*` on NT (fields: `rel` = count, depth,
  bus/device/function of the bridge; `attach` = decision, the child's
  depth, the parent's location; `id` = depth, location) and
  `cm.reenum.9x.id.*` on 9x.

### 98 SE and ME

**Two decorations on the 9x models line (item 4): pass.** A test INF,
`3.2.0.0`'s `stor98.inf` with `%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0`,
an added `[Stor98_Models.NTx86.10.0]`, and each section's description
made distinct, every section still matching `PCI\CC_010802` so that the
internal NVMe matched. 98 SE: the Add New Hardware wizard, "Specify a
location", offered "Stor98 R4 undecorated" and installed it (`Enum`:
`Driver` `SCSIAdapter\0000`; after a cold start `STOR98.MPD` loaded and
the volume mounted). ME: with the devnode key removed and the directory
on `DevicePath`, the wizard's automatic search installed the same model
and the volume mounted with no restart. Neither engine used the `.6.3` or
`.10.0` section.

**The internal NVMe (T.1): pass.** No id in any `Enum` capture, no
driver; the installer listed it as a candidate by class and left it ("an
internal drive" on 98 SE; of unknown place on ME, the connection manager
not yet running at the plan); the wizard found nothing for it.

**`Add_ID` (item 5): pass.** ME, a pre-boot drive present at boot with
no driver: `cm.reenum.9x.id.read=00010086`, `add=00000000`,
`after=000300A2`; the `Enum` key's `CompatibleIDs` then the seven PCI
ids followed by `,PCI\XHCI98_TUNNEL&CC_010802` - **appended last**, the
value one comma-separated `REG_SZ`. A cold boot with nothing changed:
`read=000300A2` and no `add`, so `PCI.VXD` does not rewrite the list at a
boot. A reinstall pass does: with `ConfigFlags` 0x20 written by hand the
boot's reinstall re-read the ids from the enumerator and dropped the id
(`ConfigFlags` back to 0x40). 98 SE hot plug: `add=00000000`,
`after=000300A2`.

**`Reprocess_DevNode` and `Setup_DevNode` (item 6): contradict draft 2.**
ME, the devnode whose install found no driver (`driver=25`, that is no
`Driver` value; `status=08000620`, `problem=1C`): `reprocess=00000000`
and nothing installed (`Enum`: `Class` "Unknown", `ConfigFlags` 0x40, no
`Driver`); a re-plug of the enclosure: nothing, no wizard.
`Setup_DevNode(CM_SETUP_DEVNODE_READY)` (the reading build's switch):
`setup=00000000`, nothing installed. `ConfigFlags` cleared to 0 by hand
and `Reprocess_DevNode` again: still nothing. What installed Stor98 was
the wizard's own search with the id already in the list: a 98 SE hot
plug where `Add_ID` landed while the wizard's first page ("PCI Card")
waited for the user; Next three times found "Stor98 Storage Controller
for NVMe SSDs" in `C:\XHCI98\STOR98.INF`, one source prompt for
`stor98.mpd` (default `D:\WIN98`; `C:\XHCI98` typed), and the volume
mounted.

**A defect of the first worker code.** Where the wizard opened before
the worker reached a new devnode, the devnode was never tagged: ME at a
boot with the drive present and on a hot plug of a second enclosure, and
98 SE at a cold boot with a second enclosure, each showed one `read`
for the devnode tagged earlier and no `devid`, `read` or `add` for the
new one, 60 s after the wizard closed. The worker's 10 s tag window ran
out while its CONFIGMG calls waited behind the wizard (record 31 section
5.1 now sweeps instead).

**The 9x helper with a present plain drive (item 7).** ME, first
install with a pre-boot drive present and no driver: the plan found it a
candidate by class; once the connection manager ran, the helper
(`XHCIDI`, its source unchanged) answered "no-match" over
`C:\XHCI98\stor98.inf` ("the INF has no driver for it"); at the next
start the worker tagged it and reprocessed it, and nothing installed.

### 2000 SP4

**The id as recorded (item 3, the direct shape): pass.** Hot plug: the
records `cm.chain.rel=01000028`, `rel.new=1`, `cm.chain.attach=01010028`,
`cm.chain.id=00010028`; `setupapi.log`:

```
Searching for compatible ID(s): pci\xhci98_tunnel&cc_010802,pci\ven_1b36&dev_0010&rev_02,...,pci\cc_010802,pci\cc_0108
Found PCI\XHCI98_TUNNEL&CC_010802 in C:\WINNT\inf\oem7.inf; Device: Stor98 Storage Controller for NVMe SSDs
```

The wizard named the device "Stor98 Storage Controller for NVMe SSDs";
the `Enum` key's `CompatibleIDs` began with the id. 2000's
`setupapi.log` prints no rank. The internal NVMe after the restart: the
wizard for "PCI Device" found nothing, its list without the id, the mark
service "not below a verified tunnel port".

**The restart's order (item 2, the switch shape): the [hyp] does not
hold.** The enclosure at power-on, both bridges never seen:
`cm.chain.rel=02000028` (`new=2`); `attach=02010028` for the upstream
port, above ACPI's bus filter; `cm.chain.bridge`, `cm.chain.start` at
depth 1; `rel=01010118`, `attach=02020118` at depth 2; `rel=01020200`,
`attach=02030200`, and `cm.chain.id=00030200`: the drive tagged at depth
3 on its first enumeration, the bridges running from the critical
device database. Then, at the bridges' first-time installs ("Found New
Hardware: PCI Device"): `cm.chain.remove=00020118`,
`cm.chain.remove=00010028`, and no further `cm.chain.rel` at any depth,
no `cm.chain.reattach`. The drive was recorded with plain compatible
ids and a wizard "PCI Device" opened for it. A second, fresh guest
(signing policy Ignore, enclosure hot-plugged) read the same, the mark
service's `LowerFilters` mark having rebuilt the upstream port's stack
with its registered child before the second bridge's install removed
the depth-2 child. A **re-plug** with the bridges installed:
`cm.chain.id` at depth 1 and 3, no remove, the drive's list beginning
with the id, Stor98 started on both drives; the upstream port's stack
then held two filters of this driver (`cm.chain.start=00010028` twice:
the registered child and the kernel child above ACPI).

**The per-device route (item 8).** Under the default signing policy
(Warn), the direct drive with its wizard cancelled:

```
13:55:31 Stor98: giving ...\4&1D29C6E2&0&0828 Stor98: it has no driver (status 01802400 problem 1; member 0028; it carries the tunnel id; Windows ranked another driver first)
13:55:31 ...: setup class already SCSIAdapter
13:55:31 ...: 1 model(s) in C:\WINNT\INF\oem7.inf for the class (the list ended with error 259)
13:55:31 ...: model "Stor98 Storage Controller for NVMe SSDs" (Stor98 Project) selected
```

`setupapi.log`: "Device install function: DIF_INSTALLDEVICE. / Doing
full install of ..." and nothing more for over ten minutes, no dialog on
the desktop: the call waited on the unsigned-driver question a service
cannot show, holding the tools' eject mutex (`XHCIINST /devices`:
"Another XHCIINST /devices or /eject ... is running", exit 1). After a
restart its record "install" was recovered as a switch's owed enable
and never retried. Under the policy **Ignore** on the second guest:

```
15:03:35 ...0828: DIF_INSTALLDEVICE answered success (error 0) / now runs Stor98
15:04:20 giving ...\6&3A7C4023&0&00001828 Stor98: it has no driver (...; no id: enumerated before the filter chain reached it)
15:04:20 setup class {00000000-0000-0000-0000-000000000000} set to SCSIAdapter (it read back)
15:04:31 DIF_INSTALLDEVICE answered success (error 0) / now runs Stor98
```

Both drives started and the volume mounted: the class-list model
selection installs on 2000 a drive whose ids the INF does not match,
with no `DIF_REGISTERDEVICE`. The reason texts logged were not what
happened (a declined wizard; a drive tagged and then lost) and were
corrected in `26ae6062`, with the service made non-interactive and the
driverless install given its own record.

### XP SP3

The default policy (Warn), the switch shape, the enclosure hot-plugged.
T.1: the internal NVMe's wizard found nothing. **Item 2**: the first
enumeration tagged (`cm.chain.id=00010028` direct, `=00030200` at depth
3); at the bridges' first-time installs `cm.chain.remove=00030200`,
`00020118`, `00010028` and no re-attach; the wizard showed the drive as a
plain "PCI Device". The mark service's own restart of the unit
("restart made: ...1B21&DEV_2421...1828 (serial 2)"), which came after
the bridges' installs, rebuilt the upstream port's stack with its
registered child (`cm.chain.start=00010118`, `rel`, `attach=02020118`,
`attach=02030200`, `cm.chain.id=00030200`), and the drive's list then
began with the id. **Item 8**: both drives (wizards cancelled) were taken
by the fallback - class already `SCSIAdapter`, one model, selected - and
refused:

```
15:21:54 ...0828: DIF_INSTALLDEVICE answered failure (error 1168)
#E358 An unsigned or incorrectly signed file "c:\windows\inf\oem4.inf" for driver "Stor98 Storage Controller for NVMe SSDs" blocked (server install). Error 1168: Element not found.
```

No wait on XP, but the service cannot install the unsigned package.

### 7 SP1 x86

**The passive child (item 1): pass**, in the `tb3-alpine-ridge` profile:
the NHI (8086:15BF) and the Thunderbolt xHCI (8086:15C1) started with
problem 0 and a child filter of this driver on their PDOs
(`child.attached` 6), the connection manager working; an enclosure
behind the switch tagged at depth 3 and bound to Stor98 by Windows'
ranking (`0x80FF2000`, the uncatalogued x86 package, no competitor).

### 8.1 x64: the signature score without the certificate (item 3)

The `3.2.0.0` release package (its x64 drivers and catalogs signed by
the project's test certificate), test-signing on, the certificate in
none of Root, TrustedPublisher, CA or My (read in the guest first).
`INSTALL /console /cmonly /yes`: exit 3, the publisher question three
times, answered "Install this driver software anyway". `setupapi.dev.log`
at the staging of `stor98.inf`:

```
sig: Verifying file against specific Authenticode(tm) catalog failed! (0x800b0109)
sig: Signer Score = 0x80000000
sig: Signer Name  = <unsigned>
```

So without the certificate trusted the package ranks unsigned: its rank
on a tunnelled drive would be `0x80FF2000` against `stornvme`'s
`0x00FF2006`, and `stornvme` binds first. With `3.2.0.0`'s code the
internal NVMe at boot and one hot-added kept `stornvme`; the tunnelled
drive went to `stornvme` first and the switch took it: "Stor98:
switching ... from stornvme (member 0028)" at 12:31:16, "now runs
Stor98" at 12:32:17, with no question asked.

### 8.1 x86: the id and the 8.1 arm (T.2, the direct shape)

The install (`/cmonly /yes`) exit 3; `Stor98Inf` written. First plug:
`cm.chain.rel=01000028`, `cm.chain.attach=01010028`,
`cm.chain.id=00010028`; `setupapi.dev.log`'s compatible ids for the
drive begin `pci\xhci98_tunnel&cc_010802, pci\ven_1b36&dev_0010&rev_02,
..., pci\cc_010802, pci\cc_0108`. Windows bound `stornvme` first (the
uncatalogued package). The 8.1 arm of the first code:

```
15:23:35 switching ...\4&7EBBD&0&0828 from stornvme (member 0028; it carries the tunnel id; Windows ranked another driver first)
15:24:15 model "Stor98 Storage Controller for NVMe SSDs" (Stor98 Project) selected
15:24:47 DiInstallDevice answered success (error 0)
15:24:48 ...reads service stornvme, status 0180200A, problem 0 after the switch (failed)
```

`setupapi.dev.log`: the driver node `stor98.inf:...:pci\xhci98_tunnel&cc_010802`
installed, "Device not started: Device has problem: 0x16:
CM_PROB_DISABLED". The debug port: `cm.chain.remove=00010028` at the
disable, a child started at the enable, and no `cm.chain.id` after. The
`Enum` key: the drive's compatible ids plain again, `Service` `stornvme`.
**A defect**: the arm's stop removed the kernel child, the enable re-read
the ids without it, and the drive came back on `stornvme`; the record
"failed" then refused every later attempt, a re-plug (tagged again)
included. The internal NVMe kept `stornvme` throughout (T.1 pass).

### 7 x86, the switch shape (record 31 section 9.3 item 2)

The m32 N4 kit's `-Chain` topology on a fresh 7 x86 overlay, the
enclosure hot-plugged: the bridges' first-time installs ran before they
started, no `cm.chain.remove` followed, the drive was tagged at depth 3
on its first plug and Windows bound Stor98 (rank `0x80FF2000`, the x86
package). The loss of the chain at a bridge's first-time install is 2000's
and XP's alone.

### 98 SE and ME, the failed install cleared by hand (record 31 section 9.3 item 9)

By `.REG` files on the overlays of the readings above, each followed by
a hot re-plug and by a cold boot with the drive present:

- ME, devnode `082800`: the id appended to `CompatibleIDs`, `ConfigFlags`
  `00,00,00,00`, its `Class` value "Unknown" kept: no wizard, nothing
  installed, at the re-plug and at the boot.
- ME, `082800`: the devnode's `Enum` key deleted and written again with
  its ids, the id, `DeviceDesc`, `Capabilities` 14, `HWRevision` and
  `ConfigFlags` 0, no `Class`, `ClassGUID` or `Driver`: no wizard, no
  driver, no drive letter, at the re-plug and at the boot.
- 98 SE, `083000`, the same rewrite: no wizard at the hot plug or at the
  cold boot with both enclosures; still "PCI Card" with no driver.
- `ConfigFlags` 0x20 (`CONFIGFLAG_REINSTALL`): the boot reinstalls, but
  reads the ids from the enumerator first, drops the id and fails again,
  back to 0x40.

The only arrival that ends with Stor98 is the 98 SE hot plug where the
id lands while the wizard's first page waits. Q1's option (b) does not
hold; option (a) is taken, as `stor98w9.inf` (record 31 section 5.2).

### Reading 10: the id across a restart of a drive's own stack (record 31 section 9.3 item 10)

The same qemu build. A guest-side PowerShell script written for the
reading (`DEVCTL.PS1`, kept with the kit, not in the tree) disables and
enables a devnode through SetupAPI and writes its status, ids,
`LowerFilters` and the volumes to COM1.

- 7 x86 (the switch-shape overlay above, both enclosures' drives on
  Stor98 with the id first; the direct one with the mark service's
  registered child): a disable then an enable of each. The ids were plain
  already at the disable and stayed plain after the enable; the debug
  port showed `cm.chain.remove`, then `cm.chain.start` of the registered
  child and no `cm.chain.id`; `setupapi.dev.log` no new section; Stor98
  bound and running after, the volume back.
- 8 x86 (a fresh overlay of the p43-24 kit's 8 image): the tagged drive
  bound to Stor98 by Windows at the plug (`Rank - 0x80ff2000`, "Not
  digitally signed"); the internal drive left with no driver and no id.
  A disable then an enable: the id still there at the disable, gone after
  the enable; no new setup-log section; Stor98 bound and running, the
  volume back.
- 8.1 x86 (the overlay above, the drive on `stornvme` with the id first
  after a re-plug): the installer's case 3b by the per-device route of
  the first code. `setupapi.dev.log`: the driver node
  `stor98.inf:...:pci\xhci98_tunnel&cc_010802`, "Install Device:
  Restarting device", exit SUCCESS, no disable; the drive came back with
  plain ids on `stornvme`, and the installer said "did not end as
  installed: stornvme", exit 2. A disable and enable of a drive Stor98
  runs could not be read on 8.1, since none could be made.

So 7 and 8 keep a configured devnode's driver when its ids change, and
8.1 chooses `stornvme` again at the install's own restart: with the id
alone Stor98 cannot be put on a tunnelled drive on 8.1. The owner's scope
decision followed (record 31 section 4.8, option B).

### Reading 11: why 8.1 chooses `stornvme` again (record 31 section 9.3 item 12)

8.1 x86, the overlay above; a guest-side PowerShell script with SetupAPI
(`R11.PS1`, kept with the kit) reading the devnode's `Enum` values, its
driver key and its `Microsoft-Windows-Kernel-PnP/Configuration` events,
and installing Stor98 by the class-driver list over the staged INF.

- Reading 10's case 3b, from the PnP log: 17:03:19 event 400 configured
  `oem3.inf` (Stor98); 17:03:26 event 411, problem 0x15, at the stack's
  removal; 17:03:28 event 400 configured `stornvme.inf`, "Driver Rank:
  0xFF2005, Matching Device Id: PCI\CC_010802, Outranked Drivers:
  (empty), Device Updated: true"; event 410 started `stornvme`. The rank's
  position 5 is the plain list's: the ids were read again without the
  tunnel id. `setupapi.dev.log` shows nothing after "Restarting device".
- Stor98 installed with `DI_DONOTCALLCONFIGMG` (0x00020000, WDK 7.1
  `setupapi.h`): the device ran on `stornvme` in memory, its driver key
  written for Stor98 (`MatchingDeviceId` `pci\xhci98_tunnel&cc_010802`,
  `InfPath` `oem3.inf`). Restarted (`DICS_PROPCHANGE`): event 400
  `stornvme.inf`, the same rank; the ids plain.
- The same after a re-plug, with `MatchingDeviceId` rewritten to
  `pci\cc_010802` before the restart: event 400 `stornvme.inf`, the same.

At every restart 8.1 reads the ids from the bare PDO and chooses a driver
again; no user-mode route keeps an id-only Stor98 bound. The owner kept
`3.2.0.0`'s 8.1 model for this release (record 31 section 4.8).

### The Critical Device Database on 2000 and XP (2026-10-25, i440FX)

The legs' 2000 and XP guests (QEMU `pc`, i440FX), the scope-B package of
`09c720d3`; `reg` exports of `CriticalDeviceDatabase` and `Enum\PCI` to
COM1, writes as SYSTEM through `at`.

- The one key of interest on either guest:
  `CriticalDeviceDatabase\pci#xhci98_tunnel&cc_010802`, `Service`
  "stor98", `ClassGUID` `{4D36E97B-E325-11CE-BFC1-08002BE10318}`. Windows
  writes it at the first install of Stor98 on a drive that carries the id
  (2000: deleted at 21:44, written again by the wizard's install at
  21:50). No `pci#cc_010802` key on either guest; neither ever had
  `3.2.0.0` (that case is unread).
- A drive started through it: `DeviceDesc` "PCI Device", no `Driver`
  value, `ConfigFlags` 0x400. On Stor98, with no wizard, at re-plugs with
  0x400 left, and at a re-plug and a restart with it cleared (a restart
  with it left was not read).
- The key deleted on 2000: the drive with its own driver key re-plugged
  onto Stor98; a restart with the enclosure present kept every tunnelled
  drive on Stor98, and the key was not written again.
- A "PCI Device" wizard seen at a 2000 boot was the internal NVMe drive's
  (no driver; skipped, it asks again at every boot), not a tunnelled
  drive's.
- Decided on this reading (owner, 2026-10-26): no new code for the key.
  The uninstall deletes it under its general rule for Critical Device
  Database keys that name a service of the package the run removed or
  found absent, so it stays while Stor98 stays for a drive that still
  uses it (read on 2000 q35, Stor98 removed, below: "CriticalDeviceDatabase\pci#xhci98_tunnel&cc_010802 was
  deleted (it named a service of this package)"); harmless either way
  (record 31 section 6.4).

### The legs (record 31 section 9.2), 2026-10-25 and 26

QEMU `pc` (i440FX) for every leg below: they began before the owner's
q35 rule of 2026-10-25 23:00, and no q35 base for 8.1, 10, XP or x64
existed by their end; the headline legs are owed again on q35. Emulator
`vm\usb4\bin-2a893ea`; an internal NVMe on `pci.0` in every guest; the
direct shape is the `asm2464` device on the tunnel bridge, the switch
shape the N4 kit's `-Chain` (`usb4-asm-pci-usp` -> `pci-bridge-seat` ->
`nvme`). Packages (test cuts, never published, the version still reading
`3.2.0.0`): A, the gated build of `09c720d3` (scope B before the 8.1
fix); B, of `5dd7a7d7` (no id on 8.1 and later); D, of `5314c9bd`, the
final reviewed code (x86 zip `AE1DA5F4...`; x64 unsigned, its drivers and
catalogs signed in the guest with a throwaway certificate, nothing signed
on the host). Evidence (the kit, not in the tree): the guests' serial
logs, setup logs, Kernel-PnP events and screenshots under the worker's
`legs\` folder.

| Leg | System | Package | Result |
|---|---|---|---|
| T.1 | 2000, XP, 7 x86, 8 x86, 7 x64 | A, D | pass: the internal drive plain, no driver offered |
| T.1 | 8.1 x86, 10 x86 | A, B | pass: `stornvme`, plain ids |
| T.1 | 98 SE, ME | A | as designed: the wizard offers Stor98 by `stor98w9.inf`'s class match (declined on 98 SE; accepted blind on ME, which then runs Stor98 on it) |
| T.2 | 2000, XP, 7 x86, 8 x86, 7 x64 | A, D | pass: the id first, Stor98 bound by Windows (2000 and XP through the wizard), the volume mounted |
| T.2 | 98 SE | A | pass: Stor98 by the wizard through `stor98w9.inf`; the worker's id missed (section 5.1's stated gap) |
| T.2, T.8 | 8.1 x86 | A | fail, then fixed: the drive enumerated with the id, the arm's class-model install undone 0.5 s later at its restart (`stornvme` chosen again); `3.2.0.0`'s arm on the same image held. Fixed by `5dd7a7d7` (no id on 8.1 and later) |
| T.2, T.8 | 8.1 x86 | B, D | pass: no id, the arm's install holds (one Kernel-PnP event 400 for `oem3.inf`, no re-selection), disable and enable keep Stor98 |
| T.2 | 10 x86 | B | as designed: `stornvme` on a drive plugged after the install |
| case 3b | 10 x86 | D | pass: Stor98 at the install, still after the restart, through a disable and enable |
| T.3 | 7 x86 | A | pass: no loss of the chain on 7 |
| T.3 | 2000, XP | A | Stor98 reached, the unit restart not exercised: on 2000 through the Critical Device Database entry, on XP through the safe-removal mark's own restart |
| Q2 (`Restart` 0, no CDDB key, a never-seen chain) | 2000 | A | pass: "restart made", the wizard offered Stor98 by the id |
| Q2 | XP | A, C | fail, then fixed: the restart made, no wizard - XP's default-ticked "Don't prompt me again" installs the null driver; clearing the failed flag alone did not help (C) |
| Q2 | XP | D | pass: "install set to run again (flags 00000040 -> 00000020)", "restart made", Found New Hardware back, Stor98 installed by the id |
| T.5 | ME | A | pass: a pre-boot drive installed by the wizard through `stor98w9.inf`; the id appended at the worker's start baseline |
| T.9 | 7 x86 | A | pass: Stor98 removed from both tunnel drives, the INF out of the store, the service gone |
| T.10 | 7 x86 (icm) | A | pass: the NHI and the Thunderbolt xHCI run with the passive children; a drive below the enclosure tagged and on Stor98 |
| 44.23 case 3b | 8.1 x86 | B | pass at the install (no re-selection); after the restart not read (the emulated enclosure does not reconnect across a guest restart) |

### The q35 re-runs, 2026-10-26

QEMU `q35` (SeaBIOS) from the q35 bases, the emulator's ASM4242 model
(the emulator's asm4242 preset, pre-OS connection manager) with the ORICO enclosure
(`asm2464`) on USB4 port 1. Package E, a test cut of `e8cab09e` (the
44.3 code rebased onto `3.3.0.0`; x86 zip `7DA3293B...`, the version
still reading `3.2.0.0`); package F, a test cut of `a1a07528`
(`3.3.0.0` with StorHold9xMs and the 44.43 9x shutdown fix; x86 zip
`AECAB5B5...`), for XP and 2000 (piix3-ide, one vCPU for 2000, two for
XP; 2000 shut down by Shut Down, then QEMU quit at "It is now safe to
turn off your computer"). A hot plug of the enclosure after the OS
started brought the PCIe tunnel up, but the enclosure's upstream port
stayed at problem 31 with no resources, and the model's root-port
padding (the launcher's `-AsmReserve`) sent 8.1 and 10 to Automatic
Repair after the install; so "plugged after the install" was run as
"the enclosure present for the first time at the next cold boot" (the
coordinator's instruction: enclosure-at-boot cases where the leg allows).
The rows marked "T.2 (cold boot)" are that substitute: a first arrival
through the pre-OS tunnel, not record 31's hot plug, which stays owed on
q35.

| Leg | System | Package | Result |
|---|---|---|---|
| 7 x86 gap | 7 x86 | the cut of `85153004`, E | the earlier cut's connection manager listed no tunnel port on the ASM4242 (its port-mapping request refused by the host router model), so the mark service logged "not below a verified tunnel port" and the drive stayed at problem 28 with the id first; on E (the ASM4242 tunnel-port family, 44.40) the installer put Stor98 on the drive at the install, kept after a cold restart: pass |
| T.2 (cold boot), T.8 | 8.1 x86 | E | pass: `stornvme` first, then the mark service "switching ... from stornvme (member 0200; no id on Windows 8.1 and later, by design)", the class-model install, "now runs Stor98"; disable and enable keep Stor98, no re-selection |
| T.2 (cold boot) | 10 x86 | E | as designed: `stornvme`, plain ids, the mark only |
| 44.23 case 3b | 10 x86 | E | pass: Stor98 put on a drive running `stornvme` by the install ("recorded as this installer's", exit 3); after a cold restart `svc=stor98`, problem 0, the volume mounted; disable and enable keep Stor98 |
| T.2 (cold boot) | XP | F | pass: installed with no enclosure (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, the unsigned prompts answered), then the enclosure present for the first time at the next cold boot: the drive's `CompatibleIDs` began with the id, `setupapi.log` "Found PCI\XHCI98_TUNNEL&CC_010802 in ...\oem4.inf", the wizard installed Stor98 (Continue Anyway), `Service` stor98, `ConfigFlags` 0, "ORICO (D:)" mounted; Windows wrote the Critical Device Database key for the id (section above) |
| T.2 (cold boot) | 2000 | F | pass: the same order (install exit 3, Shut Down, the enclosure first at the next cold boot): the wizard named the drive "Stor98 Storage Controller for NVMe SSDs" through the id, the signature question answered Yes, `Service` stor98, `ConfigFlags` 0, "ORICO (E:)" mounted; the key for the id written as on XP |
| Q2 | XP | - | not reachable on q35 with the enclosure at boot: through the pre-OS tunnel the drive enumerates with the id at its first boot (the T.2 row), so there is no driverless drive for the unit restart to act on; Q2 needs the hot plug, whose Code 31 on q35 is a separate investigation (its `pc` reading above, D, stands) |
| T.2 (cold boot) | Vista x86 | G | pass: installed with no enclosure (exit 3; Found New Hardware suppressed in the guest by the `SuppressNewHWUI` policy, the unsigned prompts answered), then the enclosure first at the next cold boot: Stor98 bound through the id, the key for the id written, "ORICO (D:)" mounted |
| T.2 (cold boot) | Vista x64 | G, x64 staged unsigned and signed in the guest | pass: the x64 package staged unsigned with its catalogs (`make-package`, MakeCat), its drivers and catalogs signed inside this guest's overlay with a throwaway certificate made there (test-signing on; never the project's key, nothing signed on the host); install exit 3; at the next cold boot the drive's `CompatibleIDs` began with the id, Stor98 bound, the key written, "ORICO (D:)" mounted |
| T.2 (cold boot) | XP x64 | G, x64 signed in the guest as for Vista x64 | pass: the unsigned staging failed the controller's update (error 0x57); signed in the guest, the install ended exit 3; at the next cold boot Windows' wizard installed Stor98 through the id, `Service` stor98, "ORICO (D:)" mounted after the restart |
| T.4 (cold boot) | 98 SE | G | pass: a boot with the drive present after the install: the id appended to the tunnel drive's `CompatibleIDs` (the start baseline), the internal drive without it; Stor98 on the tunnel drive alone, "ORICO (E:)" mounted. The hot-plug half moved to 44.45 |
| T.4 (cold boot) | ME | G | pass: after the install's restart ME's wizard installed Stor98 through `stor98w9.inf` (its automatic search) on the tunnel drive and offered it on the internal drive by the class, as before (accepted here); the id appended to the tunnel drive alone; "ORICO (E:)" mounted. The hot-plug half moved to 44.45 |
| T.6 | 98 SE | G | pass: the enclosure and an internal NVMe of the same model present at the install; the tunnel drive verified behind a tunnel and installed by the helper through `stor98w9.inf` ("installed, restart pending"), the internal drive left (`hwid_clear` gone); exit 4 for the USB storage drivers this test cut leaves out |
| T.6 | ME | G | read: with the enclosure's bridges installed and an internal NVMe of the same model present, ME had not enumerated the tunnel drive at the install (it appears once the connection manager runs, after the install's restart), so the helper had no tunnel drive to install; it left the internal drive ("where it sits could not be read ... left as it is"); after the restart ME's wizard installed Stor98 on the tunnel drive through `stor98w9.inf` (the T.4 row) |
| T.6 | 2000 | G | read: on a first install the connection manager is not yet running, so the installer cannot place the drives and leaves them ("where it sits could not be read ... the safe-removal mark service sees to Stor98 then (on Windows 2000 and XP through Windows' own wizard)"); after the restart Windows' wizard installed Stor98 through the id. The installer's per-device route with the connection manager already running is the update case, T.7 (44.final). Emulator artifact: an internal NVMe behind a QEMU `pcie-root-port` (`1B36:000C`, which `xhcicmf.inf` names as the emulator's q35 tunnel port) is tagged too, so on q35 the internal drive belongs directly on `pcie.0` |
| T.9 | 2000 | G | pass: the uninstall gave both drives back ("PCI Device", no service, plain ids after the restart, the filter gone) and deleted the key for the id (the general rule above); a re-install, after its restart, found the drive again and Windows' wizard put Stor98 back through the id, "ORICO (E:)" |
| T.9 | 8.1 x86 | E | pass: the uninstall (exit 3) gave the drive back to `stornvme`, plain ids, "ORICO (E:)"; the re-install (`/cmonly /stor98`) found it again: "Stor98 now runs ...: recorded as this installer's" |
| T.10 | 2000 | G | pass: the ASM4242 NHI (`xhcicm`) and xHCI (`xhci98`), which sit below two of the four filtered DSPs, installed and running at every boot of the legs, the connection manager working (the drive tagged and mounted); the bridge filter's children attached on the PDOs below the filtered ports, the NHI's and xHCI's among them, `cm.reenum.child.attached` 12 (`XHCITR /report`); the `pc` stall at boot not seen |
| T.11 | 7 x86 | E (REN's overlay) | pass (cold boot, the values in the machine-wide `Services\USB` key): `XhciCmSafeEject` 0: no install and no unit restart - the mark service's process still runs (its start is the install's, not the value's) and acts on nothing, which is how this record reads T.11's "no service" ("notice not acted on: member 0200 unit 03 serial 1: safe eject is off for the domain", "mark removed: ... (safe eject is off; member 0200)"), the drive still tagged and on Stor98; `XhciCmMode` 0: `XHCITR /report` "no connection manager, idle (inert)", `cm.mode=00000000`, the drive tagged (the id in its `CompatibleIDs`), on Stor98, mounted. The re-plug half moved to 44.45 |
| T.11 | 2000 | G | pass (cold boot): `XhciCmSafeEject` 0 in the NHI's key: no mark service among the started services, the chain still tagging (the id first); `XhciCmMode` 0 with the pre-OS tunnel: `XHCITR /report` "no connection manager, idle (inert)", the drive tagged and on Stor98, "ORICO (E:)". On the ASM4242 profile, the q35 kit having no `usb4-generic` host; the re-plug half moved to 44.45 |
| StorHold9xMs | ME | debug-port Stor98 of `92513538` (with `-device usb4-debug-port`) | pass: `StorHold9xMs`="2500" read (stage 0x06 `0x9C4`, both adapters) |
| StorHold9xMs | 98 SE | debug-port Stor98 of `92513538` on an internal NVMe | pass: `StorHold9xMs`="2500" read (stage 0x06 `0x9C4`); a DWORD 10001 refused (stage 0x07 `0x2711`) and the default used (stage 0x06 `0xFA0`); `StorGoneMs` beside it unchanged |

Package G is a test cut of `cfe47f77` (x86 zip `E1A976AD...`; the x64
package staged unsigned, signed in the guests as the rows say).
The q35 enclosure (`asm2464`: upstream port, downstream port, NVMe) is
the switch shape, so every q35 row above reads it, at a cold boot; its
first-plug half (the chain through the bridges' first-time installs,
T.3) is a hot plug. Carried: the hot-plug halves (T.2's hot plug, T.3's
first plug, Q2 on XP, T.4's plug, T.11's re-plug) to 44.45, the first hot plug's Code 31 on q35 (owner,
2026-10-26); T.7 and the installer's per-device route on an update to
44.final. Not readable here: 44.23's "does a later `stornvme` mend the
hang at an unplug" (the 8.1 image carries `stornvme` 6.3.9600.16421 alone
and nothing can be fetched). Seen on ME with the ASM4242 profile and
this cut: Shut Down left a black screen with QEMU running (twice, over
2.5 minutes), while a restart completed; 44.43's fix was read on the
Alpine Ridge profile only. Emulator limits met: the enclosure does not
reconnect across a guest restart on 8.1 and 10; three chains behind one
tunnel port exhaust its window (problem 12).

### Owed after these readings

Record 31 section 9.4's list, which is the complete one: item 10 (b) on
8.1 for a drive already on Stor98, on x64 and on 10, and (c) and (d);
item 12 (the 8.1 fix's reading). Item 10 accounted for: (b) read - 8.1
x86 (T.8), 10 x86 (the case 3b row's disable and enable) - and on x64
carried to 44.final's x64 install legs (no x64 leg here disabled and
enabled a drive); (c), the installer's update of drives Stor98 runs and a
`/force` reinstall on 7 and 8.1, carried to 44.final with T.7, the
update legs; (d) read on 10 x86 through its restart and on 8.1 x86 at
the install, its after-restart reading on 8.1 carried with 44.23. Item
12 is the 8.1 fix's own reading (record 31 section 10, scope). Read since, in "The q35 re-runs": the switch shape on 8.1 and 10 (the
q35 enclosure, at a cold boot); the passive child on 2000 (T.10);
`DiInstallDevice` on a driverless devnode on 7 (the 7 x86 gap row: the
installer's model selection on a problem-28 drive); the 9x INF
(`stor98w9.inf`) installing on 98 SE and ME (T.4, T.6). Record 31
section 9.2's T.1 and T.2 on 8 x64, 8.1 x64 and 10 x64 were not read in
these legs: carried to 44.final's x64 install legs (coordinator, 2026-10-26).

## 44.4, 44.5, 44.7, 44.10, 44.11, 44.13 and the storage row - the installer's legs (2026-10-25)

Development host A, QEMU 11.1.0 (scoop), headless, TCG; screens by QMP
`screendump`, keys by QMP `send-key`, no display client. Kit
`E:\xhci98-vm\p44\p44-inst` (shots `s98-*`, `me-*`, `w7-*`; not files a
clone has).

### The vehicle

- The gated `release` x86 build of branch `p44-inst` at `980320d8`
  (`BUILD + GATES PASSED`; `test_inst` 4108 checks, `test_wizlay` 199,
  the installer's self-tests passed, the window's `st_welcome_rows` and
  `st_comp_layout` among them), the installer rebuilt at `2b53466b`,
  staged by `make-release.ps1 -Arch x86 -Flavor release -TestLabel`
  with both driver packs (`USBDRV\FREE` from usb-wdm-free `v0.1.0`,
  `USBDRV\NONFREE` from usb-wdm-nonfree `v1.0.0`, `USBDRV\NUSB33`):
  the `-nonfree` download unzipped as `PKG`, the base download as
  `PKGB`, on an IDE VVFAT drive. SHA-256 prefixes: `XHCIWIZ.EXE`
  `836b9dfb`, `XHCIINST.EXE` `f0d77b12`, `xhci98.sys` `86798e79`,
  `xhci98.inf` `38958592`, `INSTALL.BAT` `f2574278`. The package still
  reads `3.2.0.0` (the version is not bumped).
- 98 SE: p43-5's pristine 98 SE (640x480, 16 colours), its first-start
  wizard for the `qemu-xhci` finished with no driver, 1 vCPU, 512 MB.
  ME: the stock ME base, the same. 7 SP1 32-bit: 43.11's `w7` overlay
  (`2.4.0.0` on the controller, `LogPixels` 120, 800x600), 2 vCPUs.

### 44.4 `INSTALL.BAT` refusing too many arguments: PASS on 98 SE and ME

`TOOMANY.BAT` from `D:\PKG` under `COMMAND.COM`: `call INSTALL.BAT
/console a b c d e f g h` printed the refusal ("Too many arguments for
INSTALL.BAT on this Windows (eight at most, seven after /console; ...")
from `XHCIINST /toomany` and left `ERRORLEVEL 1`; `call INSTALL.BAT
/console /list` then left `ERRORLEVEL 0`. The same on ME. (Before
`3.3.0.0` the refusal left the errorlevel as it was, the `3.2.0.0` cut.)

### 44.13 a leftover `xhci98.tmp`: the fix, PASS on 98 SE and ME

- 98 SE: `COMMAND.COM`'s bytes planted as `SYSTEM32\DRIVERS\XHCI98.TMP`
  (93,890 bytes, no `WININIT.INI`), then the window's install. Its
  Finish page: "Deleted C:\WINDOWS\SYSTEM32\DRIVERS\xhci98.tmp, an
  earlier install's leftover: Windows would have put it in place of
  xhci98.sys at the start after a USB controller is added." After the
  restart (a cold start) `XHCI98.SYS` 310,161 bytes, no `XHCI98.TMP`.
- ME: on the stock ME the first install itself left `xhci98.tmp` with
  `WININIT.INI`'s `[rename]` naming it (ME's engine copies through the
  temporary name even with no driver loaded; the swap made at the next
  start). After that start, ME's `COMMAND.COM` planted as `xhci98.tmp`,
  then `INSTALL /console /yes /nopause /force`: the plan said "...
  xhci98.tmp, an earlier install's leftover that no pending rename
  names, will be deleted ...", and the run "Deleted ...xhci98.tmp",
  exit 3.

### 44.5 the Properties title: PASS on 98 SE and ME

Device Manager after the install's restart, Universal Serial Bus
controllers: the tree "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)", and
its Properties title "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)
Prop..." (the word Properties cut at 640x480, as task 43.73 read for a
name of 43 characters or more), the General tab the same name. The same
on ME (after the `/force` reinstall). Before, the title was the plain
"xHCI98 USB 3 Controller Properties" (43.73).

### 44.7 the Welcome page: PASS on 98 SE at 640x480 and on 7 at 120 DPI

- 98 SE: "This computer: Windows 98 SE (4.10.2222 A ), 32-bit." directly
  under the paragraph, the log's line under it, and the device table from
  there to the status box: eight rows' room, one device ("PCI Universal
  Serial Bus ... install (no driver)"), the columns within the table's
  width (no horizontal bar).
- 7 at 120 DPI (800x600): the same order; the table holds about seven
  rows under its header, the root hub and the controller in it, no
  horizontal bar.

### The storage row (the owner's request of 2026-10-25): PASS on 98 SE

98 SE at 640x480, the Components page: "USB storage drivers for Windows
98 SE (NUSB 3.3's)", under it "Every USB disk on Windows 98 SE needs
these, Bulk-Only and UAS alike. Leave this unticked if NUSB is
installed." and "Not installed now: USB disks do not work without
them.", whole, as plain text with no scroll bar; every row below on the
page. (On 7 the row is not shown, the Components page unchanged.)

### 44.10 the two pages: PASS on 98 SE, 7 at 120 DPI, and the base download

- The `-nonfree` download: after Components, "Free USB drivers" (the
  free set's box, sentence and address, then its packages) and then
  "Non-free USB drivers" (the non-free set's alone: on 7 its game pad
  and Ethernet packages under HID and Network; on 98 SE no package and
  the copy-only line). Back from the non-free page went to the free page.
- The base download (`PKGB`, no `USBDRV\FREE` or `USBDRV\NONFREE`), on
  7: Next on Components went straight to the Summary.
- Found and fixed after the 98 SE leg (`f60c9bba`, not read again in a
  guest): the non-free page's empty-list line still named
  `USBDRV\FREE\...` too; each set's page now names its own folder.

### 44.11 the two audio packages on `DevicePath`: 98 SE PASS; ME by the pack; the device leg owed

- 98 SE, the window: the free page offered "USB audio (uac2)" and "MIDI
  (usbmidi)" under "Free drivers: Audio"; with the copy box and both
  ticked, the Summary listed both. After the run and a cold start
  `DevicePath` read `C:\WINDOWS\INF`, `C:\XHCI98`,
  `C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me` and
  `C:\XHCI98\USBDRV\FREE\audio\usbmidi\w98se` (108 characters, the two
  folders 84 of them with their separators), and the set's record
  `Staged` held exactly those two folders.
- ME: `/wdmfree:stage=audio/uac2,audio/usbmidi` was refused before any
  change ("free:audio/uac2: on Windows 98 SE and ME this package is
  copied, not staged ..."): the pinned usb-wdm-free `v0.1.0`'s
  `STAGE.TXT` has no `WME` line for either package (`uac2`: "ME has no
  leg"; `usbmidi`'s `wme` folder a `COPY` line, by hand), so the box
  rule of section 6.7.11 offers none on ME. Whether ME should stage them
  is the pack's lines' decision (its maintainer, the owner).
- The device: QEMU models no USB Audio Class 2.0 or MIDI device, so the
  device leg is owed. By the INFs: `uac2wdm.inf` (`w98se-me`) binds
  `USB\VID_041E&PID_3278&MI_03` and `USB\Class_01&SubClass_01&Prot_20`,
  `usbmidi.inf` (`w98se`) `USB\Class_01&SubClass_03&Prot_00`; 98 SE's
  `WDMA_USB.INF` matches by `USB\Class_01`, a later and less exact entry
  of the device's compatible ids, so with the folder on `DevicePath` the
  package's line ranks first.

## 44.9, 44.8 and 44.35 - the USB Tree: what a link runs as, the running driver's version, the storage transport (2026-10-25)

Design record 25 revision 21 (sections 17 to 19) and design record 13
sections 8.12 ("Version 2") and 10.7 ("The decision kept"), reviewed and
revised, on branch `p44-tr`. Runtime, development host A, QEMU, one guest
at a time, at most two virtual processors.

### Two readings that changed the design before the legs

- **98 SE's status record says profile 0.** On the emulator's Alpine
  Ridge under 98 SE (`3.2.0.0`'s drivers, the new `XHCITR` of `1a471173`)
  the NHI's details read "Profile: 0" with the ICM running, and the
  ASM2464 USB4 device read "Generation: USB4 v1.0" with no "running as
  Thunderbolt 3": 98 SE's `xhcicm.sys` starts before it reads the NHI's
  ids. From `a5085505` a record whose profile is 0 is classed by its NHI's
  PCI id (record 25 section 17 item 2). Under 7 x86 on the generic USB4
  host the record says profile 5 and the device read "USB4 v1.0", as it
  should.
- **QEMU's `usb-uas` offers UAS alone** (`hw\usb\dev-uas.c`: one
  interface, protocol 62h). Read on 7 x86 with `3.2.0.0`'s drivers: with
  `XhciForceBulkOnly` 1 set machine-wide and the guest restarted, the
  drive (`P44UAS0001`) still took `xhciuas` (`Enum\USB\VID_46F4&PID_0003\
  P44UAS0001`: compatible ids `USB\Class_08&SubClass_06&Prot_62` ...,
  Service `xhciuas`). The emulator cannot build a drive that offers both,
  so the stale binding is read with a stand-in INF (the owner,
  2026-10-25; record 25 decision T27), and the natural case is 44-E.5.
- Also from the 98 SE reading: under an older driver every device on the
  controller read "Transport: not reported"; from `6e814d00` only a
  mass-storage device does.

### The vehicle

- The build: branch `p44-tr` at `67519428`, the gated `build-driver.cmd
  all` (x86) and `all -amd64` both "BUILD + GATES PASSED (debug release
  qemu)". Staged with `make-release.ps1 -Arch x86 -Flavor release
  -TestLabel "44 legs" -SkipQualtool -NoDriverPack -NoWdmFree -NoNonFree
  -SkipUploadSet` into a scratch directory; the tree still reads
  `3.2.0.0`. SHA-256: `xhci98.sys` `ab39f755b1812503...`, `xhcicm.sys`
  `1518cfe9593fcca8...`, `xhciuas.sys` `9ccc408ce4bd014a...`,
  `XHCITR.EXE` `78ee2aa61570a0ea...`.
- The emulator: `usb4-emu`'s QEMU (SHA-256 `6a1dfcc8...`, task 43.44's
  copy), TCG. 98 SE: one processor, 256 MB, the Alpine Ridge tree with
  BIOS Assist off and pre-boot on, the ASM2464 USB4 enclosure
  (`device-model=asm2464`) or the Thunderbolt 3 one (`dialect=tb3`). 7
  x86: two processors, 2 GB, `usb4-generic` at 00:04 with its tunnel
  bridge at 00:05. Both: `qemu-xhci,p2=4,p3=4`, a USB tablet, and a
  `usb-uas` drive with serial `P44UAS0001` hot-added at port 2
  (SuperSpeed, `attached` set by QMP).
- The guests: overlays of task 43.44's 98 SE and 7 images, carrying
  `3.2.0.0` as released (and NUSB 3.3 on 98 SE) before the update. An
  update over `3.2.0.0` by `INSTALL /console /yes /thunderbolt /force`
  installs the same version number: on 98 SE the setup engine queued
  `xhci98.sys` in `WININIT.INI` (`xhci98.sys=xhci98.tmp`), replaced at
  the restart; on 7 it left the `3.2.0.0` `xhci98.sys` and `xhciuas.sys`
  in place (same file version), so the new two were copied over them by
  hand and the guest restarted. `XHCITR` was run from the package
  (`tools\xhcitr\XHCITR.EXE`), since the installed copy was judged
  current; on 7 the logon's tray instance was ended first.
- Readings: `XHCITR /report` sent over COM1, screendumps of the window
  over QMP.

### 44.9 - what a link runs as (leg T.16)

- 98 SE, Alpine Ridge, the USB4 enclosure: every router line reads
  "Generation: USB4 v1.0 device, running as Thunderbolt 3" (the NHI's
  details still "Profile: 0", classed by its id `8086:15BF`). Read
  before the reboot into the new drivers too: it is the tool's reading
  of the record. **Pass.**
- 98 SE, Alpine Ridge, the Thunderbolt 3 enclosure: "Generation:
  Thunderbolt 3", link "20 Gbit/s per lane, x2 (reported by the
  firmware)". **Pass.**
- 7 x86, generic USB4 host (profile 5), the USB4 enclosure (under
  `3.2.0.0`'s drivers and the new tool): "USB4 v1.0". **Pass.**
- 7 x86, generic USB4 host, the Thunderbolt 3 enclosure: "Generation:
  Thunderbolt 3", link "Thunderbolt 3, 20 Gbit/s per lane, x2 (40
  Gbit/s) (read from the lane adapter)". **Pass.**
- Not read: a USB4 router below a Thunderbolt 3 router. The emulator's
  chain (`downstream-ports=1` on a `dialect=tb3` enclosure, a USB4
  enclosure below) showed the first router alone; the case rests on the
  host vectors (`test_tr`). A Falcon Ridge host (the owner's "running as
  Thunderbolt 2") has no emulator model and rests on the host vectors.

### 44.8 - the running driver's version (leg T.17)

No build of a higher version was made (the tree is `3.2.0.0`), so the
differing file is `3.1.0.0`'s `xhci98.sys` (`releases\3.1.0.0`), named as
a pending replacement - the route an update not restarted into takes.

- 98 SE, after the restart into the new drivers: "Driver version:
  3.2.0.0 running, the installed file's" on both xhci98 controllers and
  the NHI; the status bar plain. With `C:\WINDOWS\WININIT.INI` reading
  `[rename]` / `C:\WINDOWS\SYSTEM32\DRIVERS\xhci98.sys=C:\OLD310.SYS`:
  "Driver version: 3.2.0.0 running; the installed file is 3.1.0.0.
  Restart Windows to run the installed driver." on both controllers, the
  NHI's unchanged, and the window's status bar "2 USB devices, 1
  Thunderbolt/USB4 device - restart Windows to finish the driver update"
  (`b2-08.png`). `WININIT.INI` deleted, F5: the status bar plain again
  (`b2-09.png`). **Pass.**
- Before that restart (the new files placed, `3.2.0.0`'s drivers
  running, which say no version) nothing was said for the controllers,
  as section 18 item 3 has it for an installed file below `3.3.0.0`;
  the NHI, whose new `xhcicm.sys` the update had already started, read
  "3.2.0.0 running, the installed file's". **Pass.**
- 7 x86, after the restart: "running, the installed file's" for the
  controller and the NHI. With `PendingFileRenameOperations` holding
  `\??\C:\OLD310.SYS` / `!\??\C:\Windows\system32\drivers\xhci98.sys`:
  the controller's details and the status bar as on 98 SE
  (`c1-09.png`); the value deleted, F5: plain (`c1-10.png`). **Pass.**

### 44.35 - the storage transport (leg T.18)

- 98 SE: the `usb-uas` drive raised the New Hardware wizard once (Next
  to Finish), took `xhciuas` and mounted as `F:`; its details read
  "Transport: UAS (the only transport the device offers)". **Pass.**
- 7 x86 under `3.2.0.0`, read first: with `XhciForceBulkOnly` 1 set
  machine-wide by `INSTALL /set` and the guest restarted, the drive took
  `xhciuas`: QEMU's `usb-uas` offers UAS alone (`dev-uas.c`), so the
  force value does not apply - the emulator cannot build the dual case.
- 7 x86 under the new drivers (`XhciForceBulkOnly` back to 0): the drive
  on `xhciuas`, "Transport: UAS (the only transport the device offers)";
  the controller's "Driver log (xhci98)" counters and the snapshot's
  list show `XportUasOnly 3` (three plugs since the start).
- The stand-in (the owner, 2026-10-25, record 25 decision T27): a test
  INF of the leg's own, `STALE.INF`, never shipped (`Include=usbstor.inf`,
  `Needs=USBSTOR_BULK.NT` for `USB\VID_46F4&PID_0003`), installed on the
  drive's devnode by `UpdateDriverForPlugAndPlayDevices` (unsigned,
  accepted). The devnode `USB\VID_46F4&PID_0003\P44UAS0001` then read
  Service `USBSTOR` with compatible ids still
  `USB\Class_08&SubClass_06&Prot_62`, ... The tool's details: "Transport:
  UAS (the only transport the device offers)" and "Note: Windows bound
  this drive to USBSTOR, not to the UAS driver: most likely its Device
  Manager entry was first installed while the drive was offered as
  Bulk-Only, and Windows keeps that driver for it. To run it as UAS,
  uninstall the entry in Device Manager, then unplug the drive and plug
  it in again." **Pass.**
- Unplugged and plugged in again: Service still `USBSTOR` (the binding
  kept, the entry named by the serial). **Pass.**
- The remedy: the entry uninstalled (`DIF_REMOVE`), the stand-in INF
  deleted from the store (`pnputil -d oem7.inf`, which a real drive does
  not need: its `usbstor` binding comes from Windows' own INF), the drive
  unplugged and plugged in again: Service `xhciuas`, the note gone.
  **Pass.**
- Owed: the natural case, a drive offering both, on the owner's P14s Gen
  1 (44-E.5).

Evidence on development host A's scratch disk (`p44-p44-tr`): the COM1
captures `w98-serial-b1.log`, `w98-serial-b2.log`, `w7-serial-b1.log`,
`w7-c1.txt` to `w7-c5.txt`, and the screendumps.

## 44.12 the boot-driver gate on Vista and 7 x64, and the put-back: the legs (2026-10-25)

Design record 23 section 5.14. Runtime, development host A, QEMU under
TCG, the 7 x64 kit above on a fresh overlay (test-signing mode on).

### The vehicle

- This branch's gated build (`61f55842`; the code at `a7530b40`; both
  architectures' `build-driver.cmd all` passed): the x86 test cut of
  `make-release.ps1 -Arch x86 -TestLabel` for the package's top level
  (`INSTALL.BAT`, `SETUP.EXE`, `tools\`: `XHCIIN64.EXE` and `XHCIMK64.EXE`
  of this build), and `release-x64\` from `make-package.ps1 -Arch amd64
  -Flavor release` without the key, its five catalogs made unsigned by the
  packager's own `New-PackageCatalogFile` (task 43.36's route).
- **Nothing was signed with the project's key.** In the guest a throwaway
  certificate (`CN=p44-boot guest throwaway (not trusted)`, WDK 7.1's
  MakeCert) signed the four x64 drivers and the five catalogs (SignTool,
  SHA-1); a copy of the package as staged, unsigned, was kept beside it.

### 7 SP1 x64

| Step | Reading |
|---|---|
| The unsigned copy, `INSTALL /console /thunderbolt /yes` (test-signing on) | the refusal alone - "REFUSED: on this 64-bit Windows the connection manager is installed only in test-signing mode, from a test-signed package. ... or leave the Thunderbolt 3 / USB4 support out. Nothing was changed." - and none of the USB drivers' notes that end "If this start is an F8 start, go on"; exit code 1; no `xhcicm` service before or after |
| The signed package, `INSTALL /console /thunderbolt /stor98 /yes` | "Test-signing mode is on: the test-signed x64 drivers load." and the new warning ("Windows makes the connection manager a boot driver when it installs its filter on a PCI bridge. This installer sets it back to start on demand ..."); "Adding the filter to PCI standard PCI-to-PCI bridge ... done"; exit code 3; `Start` 3 after the run, `DEMAND_START`. The mark service, installed and started in the run, had set it back first: `XHCIMARK.LOG` "xhcicm: start type 0 set back to demand start" at its start, so the installer's own finalizer found 3 |
| The filter installed again on the bridge (`UpdateDriverForPlugAndPlayDevices`, forced, from the driver store), the service running | `Start` 0 right after the call, 3 five seconds later; the service's log "xhcicm: start type 0 set back to demand start" one second after the call |
| The same with the service stopped | `Start` 0 thirty seconds later and still (the warning's case) |
| The installer run again (same version) | exit code 0, "No changes were made"; the mark service updated and started by the run set `Start` back to 3 at its start (its log), before the finalizer |
| `bcdedit /set testsigning off`, restart | a plain start to the desktop; the bridge, the NHI, both xHCI controllers and the Stor98 drive Code 52; the install whole (`C:\XHCI98`, the Add/Remove Programs entry); `Start` 3; no repair (`SrtTrail.txt` absent). `3.2.0.0` at the same step undid the whole install (the `3.2.0.0` matrix) |
| `INSTALL /console /thunderbolt /yes` with test-signing off | the refusal, "Nothing was changed.", exit code 1; `Start` 3 before and after |
| `bcdedit /set testsigning on`, restart | - |

**Verdict on 7 x64: passes**; the finalizer's branches below.

### 7 SP1 x64: the finalizer's branches

The same 7 x64 overlay and package, test-signing mode on. In each leg
the filter was first installed again on the bridge
(`UpdateDriverForPlugAndPlayDevices`, forced, from the driver store) so
that Windows made `xhcicm` a boot driver (`Start` 0) before the run;
`XHCIINST.LOG`'s `xhcicm:` lines read after each.

| Leg | Reading |
|---|---|
| `INSTALL /console /thunderbolt /set XhciCmSafeEject=0 /yes` | "XhciCmSafeEject is 0: the safe-removal mark service is not used; it is removed"; exit code 3; no `xhcimark` service |
| No service: the filter installed again, then `INSTALL /console /thunderbolt /yes` | `Start` 0 ten seconds after the install (nothing sets it back); the run: "The Thunderbolt driver (xhcicm) is set back to start on demand (Windows made it a boot driver when it installed its filter).", exit code 3, `Start` 0 before and 3 after; the log "the system disk's path names it: 0", "Start 0 set to 3" |
| `/restage` | the line, exit code 3, `Start` 0 to 3 |
| A run failing after the filter's install (`/force`, the install directory `C:\XHCI98` denied to the administrators by `icacls`) | "The xHCI98 USB Tree could not be copied ... (error 5)" and the installer's copies refused; the set-back line; exit code 2; `Start` 0 to 3 |
| The change refused (`xhcicm`'s security descriptor denying `SERVICE_CHANGE_CONFIG`, `sc sdset`) | "The Thunderbolt driver (xhcicm) could not be set back to start on demand: its service could not be opened to change it. ... it stays one. Run INSTALL.BAT again."; exit code 2; `Start` 0 before and after; the log "Start 0 not set back (the service not opened)". The descriptor put back after |
| The system disk's path naming `xhcicm`: `UpperFilters` `xhcicm` set on the guest's IDE controller (`PCI\VEN_8086&DEV_7010`, the system disk's ancestor) with the mark service stopped, the filter installed again, the service started | the service's log "xhcicm: start type 0 kept: the system disk's path names it"; `Start` 0 twenty seconds later. The run (`/set XhciCmSafeEject=0`): "NOTE: the Thunderbolt driver (xhcicm) stays a boot driver: the disk Windows starts from is below a device it filters ... is not supported.", exit code 4, `Start` 0 after; the log "the system disk's path names it: 1". The value removed and the installer run again: the set-back line, exit code 3, `Start` 3 |
| An uninstall that keeps the connection manager (`INSTALL /console /uninstall /yes` with `xhcimark`'s descriptor denying the administrators `DELETE`) | "The service xhcimark could not be removed (error 5)", "STOPPED: ... so xhcicm and the mark service stay."; the set-back line; exit code 4; `Start` 0 to 3, `xhcicm` `DEMAND_START` |

A first try of the system disk's path used `LowerFilters` instead: the
mark service's inventory removed it within a second as a mark not behind
an active tunnel port ("mark removed: PCI\VEN_8086&DEV_7010...") and then
set the start type back - the scrub that also clears the
CriticalDeviceDatabase's copy of a mark. A first try of the failing run,
the service's `Parameters` key or `HKLM\SOFTWARE\xhci98` denied, did not
fail the run (the installer recreated the first and only notes the
second).

**Verdict: every guest leg record 23 section 5.14.4 names was read on 7
x64 and passed** (the update over `3.2.0.0` and the restart before the
put-back on q35, below). Two branches are not runtime readings: a change that
succeeds and then reads back other than 3 (no fault a guest can inject
without a hook; its code was read), and the system disk's path that
cannot be read (`-1`, likewise by inspection and the host vectors of
`inst_cm_start_action`).

### 7 SP1 x64 on q35: the update over `3.2.0.0`, and a restart before the put-back

The q35 kit's 7 x64 base (test-signing mode on) on an overlay, QEMU q35,
the ASM4242 profile; the same staged package, signed in the guest by the
throwaway certificate, beside the published `3.2.0.0` (its certificate
trusted in the guest).

| Leg | Reading |
|---|---|
| `3.2.0.0`, `INSTALL /console /thunderbolt /yes`, restart | exit 3; `xhcicm` `BOOT_START` after the restart (`3.2.0.0` sets nothing back) |
| This release over it, `INSTALL /console /thunderbolt /yes` | the new warning; exit 4, the store holding "a DIFFERENT build of xhcicm.inf under an identical INF name" (this test package still reads `3.2.0.0`: section 5.7's trap, `/restage` named); `Start` 0 before, 3 after - set back by the mark service the run updated and started ("xhcicm: start type 0 set back to demand start", its first poll) |
| `xhcimark` stopped and disabled, the filter installed again (`Start` 0), restart | a plain start in test-signing mode with `xhcicm` `BOOT_START` (`Start` 0 kept: the warning's case) and the service stopped |
| The installer run again | exit 4 (the same trap); `Start` 0 to 3: the run re-registered and started the mark service, whose first poll set it back |

**Verdict: both legs pass.** With the service in the run, the
installer's finalizer finds `Start` already 3; its own set-back line is
read in the `XhciCmSafeEject` 0 legs above.

### Vista SP2 x64

An overlay of the `3.2.0.0` matrix's prepared Vista x64 image
(test-signing mode on), 2 vCPU, 2 GB, the same emulator profile and
package (signed in the guest the same way, by cmd steps: Vista x64 has no
PowerShell).

| Step | Reading |
|---|---|
| The unsigned copy, `INSTALL /console /thunderbolt /yes` | the refusal alone, "Nothing was changed.", exit code 1; no `xhcicm` service |
| The signed package, `INSTALL /console /thunderbolt /stor98 /yes` | first stopped by Vista's own "Found New Hardware" dialog for the controllers (the installer's stop under a pending install, "Nothing was changed", exit code 4); with the dialogs answered "Ask me again later", run again: "Test-signing mode is on", the new warning, the filter added, exit code 3, `Start` 3 after the run - the mark service, started in the run, had set it back: `XHCIMARK.LOG` "xhcicm: start type 0 set back to demand start", so Vista x64 makes `xhcicm` a boot driver too (task 44.12's unread case) |
| Restart | every device Code 0 at demand start |
| `bcdedit /set testsigning off`, restart | a plain start to the desktop, no repair (`SrtTrail.txt` absent), the install whole; both xHCI controllers, the NHI and the bridge Code 39 (Vista's code for a driver that does not load); `Start` 3 |
| `INSTALL /console /thunderbolt /yes` with test-signing off | the refusal, "Nothing was changed.", exit code 1; `Start` 3 |
| `bcdedit /set testsigning on`, restart, the enclosure plugged | the tunnelled NVMe function (`4&1D29C6E2&0&0828`) Code 0 |
| Powered off, started again with `preboot=on` and the enclosure attached (the trace "insert endpoint gen 1 ... cold=1") | the bridge Code 0, the tunnelled NVMe function Code 0 at boot; `Start` 3 |

**Verdict on Vista x64: passes, for 44.12 and for 44.32's condition**
(boot at demand start, a hot plug, a pre-boot tunnel, test-signing off).

## 44.13 - a controller added later on 98 SE, and a leftover `xhci98.tmp` (2026-10-25)

Task 44.13 (was W9XRENAME.1; design record 23 section 6.6). Development
host A, QEMU 11.1.0 (scoop), headless, TCG, one vCPU, 512 MB.

### Why the `[rename]` line comes with no file

The vehicle: 98 SE with `3.2.0.0` installed and two `qemu-xhci`
controllers registered (the `3.2.0.0` matrix's update guest, an overlay of
its own), `xhci98.sys` 310,160 bytes (`3.2.0.0`). `WININIT.BAK` held the
matrix's processed line `C:\WINDOWS\System32\Drivers\xhci98.sys=
C:\WINDOWS\System32\Drivers\xhci98.tmp`, and no `WININIT.INI`.

- A file of other bytes planted as `SYSTEM32\DRIVERS\XHCI98.TMP`
  (`COMMAND.COM`'s, 93,890 bytes), then a shutdown and a cold start with a
  third `qemu-xhci` at `00:08.0`.
- The Add New Hardware Wizard named the device "xHCI98 USB 3 Controller",
  found `C:\XHCI98\XHCI98.INF` and finished with no prompt. After it:
  `XHCI98.TMP` still 93,890 bytes, `XHCI98.SYS` 310,160, and `WININIT.INI`
  held `[rename]` `C:\WINDOWS\System32\Drivers\xhci98.sys=
  C:\WINDOWS\System32\Drivers\xhci98.tmp`: the engine, with `xhci98.sys`
  loaded and the package's file of the same version, copied nothing and
  still asked for the swap.
- After a shutdown and a cold start: `XHCI98.SYS` was 93,890 bytes (the
  planted file), no `XHCI98.TMP`, no `WININIT.INI`, and `WININIT.BAK`
  held the line. **A leftover `xhci98.tmp` replaces the driver** at the
  first start after a controller is added; without one the line is
  skipped (the `3.2.0.0` matrix, N1 O2).

Screenshots `r98-a1` to `r98-c1` and the guest's `serial-*.log`
captures, kit `E:\xhci98-vm\p44\p44-inst` (not files a clone has).

## The emulator legs' common setup (44.14 to 44.18), 2026-10-25

**Vehicle.** The `3.2.0.0` release as published:
`xhci98-3.2.0.0.zip`, SHA-256 `336be62c...`, the same files as
`releases/3.2.0.0` (`release-x86\xhci98.sys` `dd230d50...`,
`release-x86\xhcicm.sys` = `nt\xhcicm.sys` `d1ba7adc...`, `stor98.mpd`
`b3305de4...`, `stor98.sys` `49c88c1c...`). Nothing under `src\` changed
between `v3.2.0.0` and `4122d90b` (the branch point of these legs on
`3.3.0.0`), so the driver code read here is `3.3.0.0`'s; no build was
made. Release flavour throughout: the evidence is the emulator's trace,
`XHCIINST /devices` and `XHCITR /report` (the drivers' logs and
counters), sent from the guest to COM1.

**Emulator.** `usb4-emu` QEMU `51d078a`, the head of that repository's
fork as of 2026-10-25, used as a copy (`qemu-system-x86_64.exe`
`d76beec0...`); read-only in that repository.

**Topologies.** i440FX. The Alpine Ridge tree as `scripts/run-win98.sh`
builds it in BIOS mode: root bridge `tbrp` (`usb4-tunnel-bridge`, bus 0
slot 6), switch `usb4-tb3-pci-bridge` ports, NHI `usb4-host
profile=tb3-alpine-ridge xhci=tbxhci` with `bios-assist`, `preboot`,
`power-off-delay-ms` / `power-off-delay-usb4-ms` as each leg says, the
tunnel bridge `tb0`, the controller's own xHCI `tbxhci`; `root-hotplug`
on `tbrp` as each leg says. Titan Ridge: the same with `usb4-tr-*` and a
second tunnel bridge. Ice Lake: `profile=tb3-ice-lake` with two
`usb4-tunnel-bridge` ports on bus 0 (task 43.44's layout). The generic
USB4 host for 42-A.10's recovery: `usb4-host profile=usb4-generic` behind
its own gated root bridge `nhirp` (`x-force-tunnel=on`), tunnel bridge
`tb0`, as 42-A.10's legs placed it. Devices: `usb4-device
device-model=asm2464` (the ORICO enclosure) or `dialect=tb3` (the TS3
Plus-style dock), an NVMe on an overlay of `vm\u4-asm\base\nvme-fat.qcow2`.

**Guests**, overlays only: 98 SE (`vm\t26\win98-clean.qcow2`, which runs
in ACPI mode), 2000 SP4 with `acpi=off` (`vm\win2k-xonly.img`), 2000 SP4
with ACPI (`vm\win2k-smp.img`, one vCPU), XP SP3 (`vm\winxp.img`, one
vCPU). The package was installed on each with `INSTALL /console /yes
/thunderbolt /stor98` and the Found New Hardware questions answered; each
installed disk was then kept read-only as the base of that guest's legs.

**Data check.** `CHK.BAT`: the volume's `TESTDATA.BIN` compared with
`fc /b` against the transfer disk's copy, then a 1 MiB `WRITE.BIN` copied
to a new name and compared.

## 44.14 - The generation read on the emulator's ICM rows: 2000, 98 SE and 7 (2026-10-25)

What is read: task 43.44's reading of an ICM-announced router's
generation from the router itself (`ROUTER_CS_0` to `4` at the entry's
route), shown as the device's generation in `XHCIINST /devices` and
XHCITR, on every ICM row the emulator models. Each leg hot-plugs the
device into a running guest (Assist off, pre-boot off) and reads
`RN.BAT` / `R9.BAT` (`XHCIINST /devices`, `XHCITR /report`) on COM1.

| Leg | Guest | Row (`cm.icm.row`) | Device | Read | Verdict |
|---|---|---|---|---|---|
| g2kar a1 | 2000 `acpi=off` | Alpine Ridge (2) | ORICO (`asm2464`) | header `2463174C 001101C0 00000001 80000000 200000FE`, `tbt.icm.version=00000120`; "USB4 (version 1.0), link 1, depth 1, approved"; XHCITR "ASMedia 246x (USB4 v1.0)", Generation "USB4 v1.0"; tunnel up, F: | pass |
| g2kar b1 | 2000 `acpi=off` | Alpine Ridge (2) | TB3 dock | header `15D38086 0612C105 ...`, `tbt.icm.version=00000202`; "Thunderbolt 3, link 1, depth 1, approved"; Generation "Thunderbolt 3"; F: | pass |
| g2ktr t1 | 2000 `acpi=off` | Titan Ridge (3) | ORICO | announced with key 0 (Titan Ridge has none), approved 62 ms after the event; `tbt.icm.version=00000120`; "USB4 (version 1.0)"; Generation "USB4 v1.0"; tunnel up | pass |
| g2kicl c1 | 2000 `acpi=off` | Ice Lake (4) | ORICO | the emulator approves it itself on this row ("auto-approved"); `tbt.icm.version=00000120`; "USB4 (version 1.0), approved"; Generation "USB4 v1.0"; tunnel up, F: | pass |
| v98a o1 | 98 SE | Alpine Ridge | ORICO | `tbt.icm.version=00000120`; "USB4 (version 1.0)"; "ASMedia 246x (USB4 v1.0)"; F: on Stor98, CHK clean | pass |
| b98ar c1 | 98 SE | Alpine Ridge | TB3 dock | `tbt.icm.version=00000202`; "Thunderbolt 3, link 1, depth 1, approved"; F: on Stor98 | pass |
| i71 c1 | 7 SP1 x86 | Ice Lake (4) | ORICO | auto-approved by the emulator; `tbt.icm.version=00000120`; XHCITR "ASMedia 246x (USB4 v1.0)", Generation "USB4 v1.0"; F: behind the port, tunnel up | pass |

On the 2000 Titan Ridge leg the controller's own xHCI of
the new tree was offered the INF of an older `xhci98` release left in the
2000 image's driver store (`oem0.inf`, 1.1.0.0) by the Found New Hardware
wizard, which asked for its file; the wizard was cancelled. That
controller is not this task's subject, and a later `INSTALL` run gives it
this package's driver.

Evidence: `E:\xhci98-vm\p44\p44-emu\<leg>\serial-*.log` (COM1) and
`trace-*.log` (the emulator's `usb4_icm*` trace).

The 7 leg needed one more `INSTALL` run on the Ice Lake tree before the
plug: the host interface was already on `xhcicm` from the staged INF, and
the run added the tunnel-port filter to the two new ports, as the
installer's own text says to do when a controller appears later.

**Verdict: done** on every ICM row the emulator models (Alpine Ridge,
Titan Ridge, Ice Lake) on 2000, 98 SE and 7: a USB4 router announced by
the firmware connection manager is read as USB4 version 1.0 and a
Thunderbolt 3 router as Thunderbolt 3. The Framework 13 reading stays
ICMGEN.1's.

## 44.15 - The 39-V Alpine Ridge legs on the current emulator (2026-10-25)

Record 21 section 10.2's 98 SE table (39-V.1) and the 2000 legs
(39-V.2), read again on QEMU `51d078a` with the `3.2.0.0` release
flavour. 98 SE legs start from fresh overlays of the installed 98 SE base;
2000 legs from the installed 2000 `acpi=off` base.

| Leg | Setup | Read | Verdict |
|---|---|---|---|
| V.1.1 install, approve, the volume | 98 SE base install with the TB3 dock at boot (`OFF_OFF`) | NHI on `xhcicm` (Code 0); dock announced, approved, tunnel up; Stor98 bound by Found New Hardware; F: (`b98ar` c1). ORICO hot-plugged: announced, approved 86 ms after the event, F: on Stor98 with no Refresh, data clean (`v98a` o1) | pass |
| V.1.2 unplug and re-plug | `OFF_OFF`, ORICO | `device_del`: disconnect event, `cm.icm.tunnel-downs` 1, the re-enumeration, F: gone, the port listed with nothing behind it; `device_add`: approved, F: back, data clean (`v98a` o2, O3) | pass |
| V.1.3 boot-tunnelled device | `OFF_ON`, ORICO at power-on | the emulator's pre-boot approval at power-on although the enclosure is not in the boot ACL; announced approved (flags `0xa8`); no mailbox `0x06` and no tunnel down in the trace; "approved before Windows started"; F: on Stor98, data clean (`v98b`). The `ON_ON` case is 44.18's | pass |
| V.1.4 nothing at boot, controller present | `OFF_OFF`, nothing attached | bound with an empty table; the later plug announced and approved (`v98d`) | pass |
| V.1.5 Assist, nothing attached | `ON_OFF`, nothing attached | the controller powered off from power-on; "No Thunderbolt or USB4 controller driven by this package was found" (`v98e`), the release notes' limitation as documented | pass |
| V.1.6 disable and enable | `OFF_OFF`, TB3 dock at power-on | Device Manager's "Disable in this hardware profile": mailbox `0x07` (driver unloads), the NHI to D3, no tunnel down, Code 22, F: still read and written clean; enabled again: mailbox `0x23`, `driver ready`, the dock re-announced approved, F: clean (`v98k`) | pass |
| V.1.7 `XhciCmApprove` 0, `XhciCmMode` 0 | `OFF_OFF` | approve 0 (`INSTALL /console /yes /thunderbolt /stor98 /set XhciCmApprove=0`, then a cold start; without `/thunderbolt` the console did not write the value): the TB3 dock announced and not approved, no tunnel, `cm.icm.approve.withheld`; the ORICO fell back to USB (44.16). Mode 0: "[no connection manager], idle (inert)", no ICM command in the trace (`v98a` c, d) | pass |
| V.1.8 the dock's xHCI | - | not on i440FX (39-0.7); the bench clause went with 39-E.1 | n/a |
| V.1.9 the 9x half of safe eject | `OFF_OFF`, ORICO | the NVMe devnode "marked removable by the Thunderbolt driver"; `XHCIINST /eject 1`: "Windows has stopped Stor98 ... You can now unplug the device.", exit 0; the tunnel up until the unplug (`v98j`) | pass |
| V.2 on 2000 (`acpi=off`) | AR tree, TB3 dock and ORICO | the console install with the filter INF (two runs, exit 3); plug, approve, generation, F: (44.14's 2000 legs); `XHCIINST /eject 1` then the unplug: no dialog; re-plug: F: back, data clean; a plain unplug 15 s after a write: "Unsafe Removal of Device" (Stor98), "Delayed Write Failed" for the file just written, XHCITR's "removed unexpectedly" balloon, which is what an unsafe unplug does on 2000 (`g2kv`) | pass |

S3 on 2000 was closed with `3.0.0.0` (`runs/run-41.md`).

**Verdict: done** for every leg the emulator offers (V.1.8 has no
i440FX vehicle). Not read again: 39-V.2's Driver Verifier SMP legs and
the filter's boot-start question, the code unchanged since `3.0.0.0`.

## 44.16 - The ORICO enclosure on the emulator's Alpine Ridge profile (2026-10-25)

98 SE, `OFF_OFF`, `usb4-device dialect=usb4,device-model=asm2464` with the
host's `xhci=tbxhci`.

- **Approved and tunnelled** (`v98a` a): the event in the USB4 form (flags
  `0xa8`, UID `174c608e70912700`), the approve 86 ms later, tunnel up; F:
  on Stor98, data clean; the generation USB4 v1.0.
- **`XhciCmApprove` 0** (`v98a` c, a cold start after the setting):
  announced at 10:36:22.586 and not approved; the emulator's "not approved
  in time: USB fallback" and a device-disconnected event at 10:36:31.971,
  9.385 s later; the enclosure then a SuperSpeed UAS disk "ASMT ASM246X"
  on port 1 of the controller's own xHCI (`8086:15C1`) under `xhci98.sys`
  and `xhciuas.sys`, New Hardware answered once, F:, data clean. A second
  plug on the same boot fell back after 9.385 s again.

**Verdict: done.**

## 44.17 - The R32 power-off with the root hot-plug modes, and 42-A.10's recovery (2026-10-25)

The Alpine Ridge tree with `bios-assist=on`; `root-hotplug=acpi` on the
ACPI guests and `restore` on 98 SE; the last device unplugged with QMP
`device_del`.

| Leg | Guest | Read | Verdict |
|---|---|---|---|
| `restore`, ORICO (`power-off-delay-usb4-ms=11000`, pre-boot on) | 98 SE (`v98c`) | unplug at 10:57:17.555: the disconnect event, the ICM silent; power off at 10:57:28.556 (+11.0 s), the tree gated and quiesced; the driver ABSENT (`cm.live.state` `0x103`, Liveness "gone", `cm.icm.lost` 1, the 9x unavailable route's removal). Re-plug: the six functions' configuration restored, a new `driver ready` 0.64 s after the arrival, the enclosure announced and approved, `cm.icm.recoveries` 1, F: back, data clean | pass |
| `acpi`, TB3 dock (`power-off-delay-ms=11000`) | 2000 SP4 with ACPI (`a2k1`) | unplug at 11:23:17.336; power off +11.0 s; the Bus Check +4.4 s later; 2000 removed the tree ("Unsafe Removal of Device": the Thunderbolt PCI Bridge and the TB3 controller), no controller listed. Re-plug: the arrival and its Bus Check, a new instance bound, approved, tunnel up; but the tunnel bridge `02:01.0` came back at Code 12 (no resources: it asks for 4096 I/O ports and two 2 MB windows), so the drive did not return | pass for the ICM client; the drive owed (platform) |
| `acpi`, TB3 dock | XP SP3 (`axp1`) | the same sequence; the tree removed, no controller listed; re-plug: bound again in under a second, approved, tunnel up, F: back, data clean, no Code 12 | pass |
| `acpi`, TB3 dock | 7 SP1 x86 (`a71`) | the same; XHCITR's balloon "The Thunderbolt/USB4 controller stopped answering" before the removal; the tree removed; re-plug: approved, tunnel up, data clean | pass |

**42-A.10's recovery on `usb4-generic`.** The emulator's `root-hotplug`
does not reach a `usb4-generic` host: behind a root
`usb4-tunnel-bridge` with `root-hotplug=restore` its `root-slot` reads
`domain=0`, and `qom-set present false` is refused with "not the power
domain of a usb4-host" (only the Alpine Ridge profile registers a power
domain). So the USB4 profile's containment against a PnP removal or a
restored bridge cannot be driven through the root hot-plug modes; that
half is owed to the emulator. The recovery was read as 42-A.10's own legs
read it, with the harness gate `x-gate closed` and then `normal` on the
root bridge (a platform that restores the host interface with its
configuration intact):

| Guest | Read | Verdict |
|---|---|---|
| 98 SE (`v98g`) | contained (`cm.live.state` `0x107`, `u4.contained`, the 9x unavailable removal); after `normal` `u4.recover.ok` 1, `preserved` 1, Liveness "answering", F: back, data clean | pass |
| 2000 `acpi=off` (`g2kg`) | contained (`cm.nvlink.withheld`, `lost-up` 0); recovered, preserved; F: kept, data clean | pass |
| XP SP3 (`xpg`) | contained; recovered, preserved; data clean (after `INSTALL` had added the filter to the new tunnel port, as the installer says to do) | pass |

**Verdict: partial.** Owed: the USB4 profile under `root-hotplug` (an
emulator model of a `usb4-generic` power domain), and the 2000 drive after
an ACPI re-arrival (Code 12 on the fresh tunnel bridge).

## 44.18 - The P14s behaviour on the emulator's Alpine Ridge profile (2026-10-25)

| Clause | Leg | Read | Verdict |
|---|---|---|---|
| Assist decided at power-on | `v98e`, `v98h` | Assist on with nothing attached: the controller off from power-on; Assist on, pre-boot off, the ORICO at power-on: the NHI hidden ("USB4 device in USB mode under BIOS Assist"), the controller's own xHCI with no resources (`xhci98` Code 10), the P14s's "unusable" cell | pass |
| pre-boot tunnels every device | `v98b` | `OFF_ON` with the ORICO, which is not in the boot ACL: approved at power-on, announced approved | pass |
| Assist, pre-boot and a USB4 device: one lane, flags `0x88`, the 2.77 s hold | `v98c` | `driver ready` answered `01002203`; the client's configuration reads sent 0.0, 0.09, 0.49, 1.65 and 2.59 s after it were all answered at 2.771 s, with the event `01008803` (flags `0x88`), approved; nothing timed out (5 s a try) | pass |
| `driver ready` flags `0x22` | `v98c`, `v98i`, `a2k1` | `0x22` under Assist; `0xa2` with Assist off (every other leg) | pass |
| the power-off delays | `v98c`, `v98i` | the ORICO last: power off 11.0 s after the departure (`power-off-delay-usb4-ms=11000`); the TB3 dock last, the defaults: 35.0 s | pass |
| a pending approval cancelled by an unplug | `v98d` (`approve-link-ms=6000`, `approve-resp-ms=8000`) | the approve in progress, `device_del` 3.6 s later: the emulator dropped the stale approve and answered it with the error flag; the driver had already abandoned it at the disconnect (`cm.icm.approve.abandoned`) and counted the late answer as a stray | pass |
| a pending approval cancelled by a re-plug | `v98d` | the approve for id 2 in progress, unplug and plug within 2 s: the stale approve dropped, the error flag. The new connection's approve (id 3) was sent at once, and the emulator, its earlier approve still in progress until that one's timers ended, answered it with the error flag too; the driver does not retry an error flag (record 21 section 5.4), so the device stayed "approval refused by the controller" until the next plug. The window is the leg's (an 8 s approve; the P14s took 383 ms) | pass, with the observation |
| a pending approval cancelled by mailbox `0x06` | - | `xhcicm` never sends `0x06` (record 21), and the emulator has no outside control that issues it | not readable here |

**Verdict: partial**; the mailbox `0x06` clause cannot be read here.
Closed at the owner's word of 2026-10-25: mailbox `0x06` recorded as
not readable, the task ticked once the rest was read (roadmap 44.18,
the decisions table's "44.18").

## 44.21 - XHCIPERF's open items, and a benchmark that is not the bottleneck (2026-10-26)

Task 44.21 (was PERF.1). Design record 29 section 16 (draft 10) is the
design and section 16.7 the readings in short; this section has the
legs. XHCIPERF stays a developer diagnostic, not shipped.

Development host A, the q35 bases of `E:\xhci98-vm\q35-bases\` (98 SE on
the ICH9 AHCI disk through SeaBIOS INT 13h, or with `-Ide piix` on a
piix3-ide disk that 98 SE drives with Windows' own IDE driver after its
wizard; ME on piix3-ide; 7 x86 on AHCI; XP on piix3-ide), the
launcher's emulator (`asm4242-20261025-2230`), TCG, headless, screens by
QMP `screendump`, keys by QMP `send-key`. Every machine **q35**. 98 SE and
ME 1 vCPU and 512 MB, 7 x86 and XP 2 vCPUs. Kit
`E:\xhci98-vm\p44\p44-perf21` (`runs\serial-*.log` the reports as the
guests copied them to COM1, `shots\`, `src\` the three probes); not files
a clone has.

### The vehicle

The tool as built by `xhciperf\build.cmd` on branch `p44-perf21`
(MSVC 6.0 x86, the gated `release` builds x86 and amd64 at `afab0921`:
`BUILD + GATES PASSED`, xhciperf self-tests 15 checks each image,
`test_pf` 311 and `test_pfs98` 70 checks):

| Build | SHA-256 of `XHCIPERF.EXE` | Legs |
|---|---|---|
| `afab0921` | `3e820211...a4bff3` | 98 SE A (AHCI), B (piix) |
| `7856e66e` with the leg-driven course line | `ca1ac6e1...dc742ea1a` | 98 SE C, D, E; ME; 7 x86 |
| `2002a921` (the dpi scaling) | `dd29dba2...3e040a1` | 98 SE at 1024x768; XP |

Probes, built for the legs and run only in the guests: `BUSY.EXE <wait
s> <spin s>`, `CPUSTAT.EXE <n> <ms>` (starts `KERNEL\CPUUsage`, prints n
readings, stops it), `QPCCOST.EXE` (20,000 `QueryPerformanceCounter`
calls timed). For the Stor98 legs on 7 x86, `xhci98-3.2.0.0.zip`
installed by `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`
(exit 4, then a restart), `-Usb4 asm4242 -EncAtBoot`: the ORICO model's
NVMe on a blank 3 GiB disk the leg formatted (`diskpart`: NTFS, `N:`),
driven by Stor98 `3.2.0.0` (its counter answer, MD-8, is in that
release).

### 16.1 - the window (P.4b)

| Reading | Result |
|---|---|
| 98 SE, 640x480, small fonts, `afab0921` | Start and Stop whole; still whole after the folder was set to `C:\WINDOWS` and the estimate line showed the refusal "The Windows folder is not tested; choose another folder, such as the drive's root." (`a-win640*.png`) |
| 98 SE, 640x480, large fonts (120 dpi), `7856e66e` | Start and Stop whole; the labels cut by the 96-dpi layout: "Passe", "Thread", "Depth" over its box, the Threads box's "Prese" (`g-win-large.png`) |
| 98 SE, 1024x768, large fonts, Cirrus (`-vga cirrus`, Windows' own Cirrus driver), `2002a921` | the window drawn at 125%: every label, box and button whole, the Summary page of a run whole (`k-win1024-large.png`, `k-run1024.png`) |

### 16.2 - the processor (P.11)

| Reading | Result |
|---|---|
| 98 SE (piix), `CPUSTAT` idle, 1 s | 0, 0, 2, 3, 0, 0, 0, 0 |
| the same with `BUSY 3 5` | 21, 100, 100, 100, 100, 97, 0, 0, 1, 1, 3, 2 |
| the same at 250 ms, `BUSY 2 3` | 100 (11 readings), 66, then 2 to 10 |
| ME (piix), idle 1 s; then 500 ms with `BUSY 2 3` | 0 0 0 0 0 1; then 4, 6, 100 (6 readings = 3 s), 26, 1, 1, 1 |
| 98 SE and ME, every disk step | 99.7 to 100% busy (one ME step with no request issued: 29.7%) |
| 7 x86, Quick on `C:` and Standard on `N:` | busy 22 to 73% of 2 (the larger at depth 32), interrupts and DPCs 0.4 to 5.5%, this program 20 to 529 us a request |
| 7 x86, Sequential throughput on `N:`, with and without `/nolatency` | busy 26 to 45% of 2, interrupts and DPCs 0.6 to 4.7%, this program 263 us to 1.82 ms a request (the larger for 8 MiB) |
| 7 x86, Quick on `N:` beside `BUSY 0 300` | busy 75 to 93% of 2, interrupts and DPCs 0.4 to 7.8% |
| XP, Quick on `C:`; the same beside `BUSY 0 150` | busy 5 to 20% of 2; 56 to 65% |

`KERNEL\CPUUsage` is the share since its previous reading on 98 SE and
ME; the 9x figure is shown without an "estimate" label (`7856e66e`).

### 16.3 - large sequential transfers and the tool's own cost (P.13)

7 x86, Sequential throughput on Stor98's `N:` (1 GiB, best of 3 passes
of 5 s), without and with `/nolatency`, MB/s:

| Test | write | read | write, `/nolatency` | read, `/nolatency` |
|---|---|---|---|---|
| `Seq 1M d1 t1` | 646.9 | 746.8 | 664.1 | 846.0 |
| `Seq 1M d8 t1` | 749.5 | 867.1 | 758.2 | 936.4 |
| `Seq 8M d1 t1` | 743.6 | 886.1 | 735.5 | 903.2 |
| `Seq 8M d8 t1` | 785.0 | 967.5 | 785.0 | 981.7 |

Stor98's page for every one of them: "Largest request granted: 256 KiB,
physical breaks 65; largest command: 256 KiB (drive limit 512 KiB)";
the tests' requests reached Stor98 in the "to 256 KiB" class, beside a
few smaller ones of the file system (as the Quick run on `N:` says it:
"The test issued 1258 writes of 1M. Stor98 received 0 requests in that
size class"); split by Stor98 0, bounced 0; most commands at the drive at
once 5-8 for 1 MiB at depth 1, 3-4 at depth 8 (5-8 in the `/nolatency`
run), 9-16 for 8 MiB.

98 SE (AHCI), matched 1 MiB sequential reads, 64 MiB, 3 passes of 10 s:
45.8 MB/s with per-request timing, 45.1 and 46.5 without. One
`QueryPerformanceCounter` call: 15.3 and 14.9 us on 98 SE, 17.9 us on
ME (8254, 1,193,180 Hz), 965 ns on 7 x86 (100 MHz), 759 ns on XP
(3,579,545 Hz). 98 SE's Sequential throughput preset on the piix disk
(128 MiB, 1 pass): reads 13.6 to 40.6 MB/s, writes 0.4 to 0.6 MB/s with
one request of 104 to 166 s in each write pass (16.4).

### 16.4 - the long timed steps (P.12)

| Run | Overruns (the report's line) |
|---|---|
| 98 SE AHCI, Quick 64 MiB | Seq 1M write: ran 36.86 s for 2 s, last request issued at 1.06 s, longest request 35.79 s; Rnd 4K write: 32.60 s, issued at 1.95 s, longest 30.64 s |
| the same again, beside `BUSY` | Seq 1M write 76.98 s (issued 1.95 s, longest 75.02 s); Rnd 4K write 12.60 s (longest 11.49 s) |
| 98 SE piix, Quick 64 MiB | Seq 1M write 74.40 s (issued 0.99 s, longest 73.40 s); Rnd 4K write 45.47 s (longest 43.50 s) |
| 98 SE piix, Quick 16 MiB (the original run's file size) | Seq 1M read 29.58 s (issued 0.64 s, longest 28.93 s); Rnd 4K read 22.40 s, no request completed |
| 98 SE piix, Quick 16 MiB, `7856e66e` | Seq 1M write 17.34 s (longest 17.07 s); Seq 1M read 4.31 s (longest 4.16 s); Rnd 4K read 13.53 s "no request issued, workers ended at 13.52 s" |
| ME piix, Quick 64 MiB | Seq 1M read 14.04 s "no request issued" |

In every one the last request was issued before the time ran out; the
time went into one request after writes, or into the wait before the
first request; the tool's deadline is not the cause (record 29 section
16.7). The original i440FX symptom (a 16 MiB Quick run's 1 MiB read of
27.8 s after a fast write) reproduced on q35's piix3-ide with the same
shape.

### P.1, P.4 and P.8 on ME, P.3, P.4 and P.6 on 7 x86, P.4 on XP

| Reading | Result |
|---|---|
| ME, the timer | "performance counter, 1193180 Hz" |
| ME, `/test seq,1024,2,1` | refused: "Queue depth 1: Windows 98 and ME file I/O waits for each request." |
| ME, `/folder C:\WINDOWS` | refused: "The Windows folder is not tested; ..." |
| ME, a foreign `XHCIPERF.TMP` (4 bytes) in `C:\` | refused: "XHCIPERF.TMP is in this folder and is kept; choose another folder."; the file still there |
| ME, `/size 16000` on FAT32 | refused: "On a FAT drive the file is at most 1 GB; lower the file size." |
| ME, a `SUBST` drive | not read: no report came back from the run (the batch's `subst` step) |
| 7 x86, Quick on `C:` (msahci) | 620.2 / 762.0 MB/s sequential, the Stor98 line "not reported by this disk's driver" |
| 7 x86, Standard on `N:` (1 GiB, 1 pass) | the Stor98 page read for every step through the volume's pass-through as administrator (P.3): "Stor98 3.2.0.0 (x86 build), PCI bus 5 device 0 function 0"; `Seq 1M d8` read 814.2 MB/s, `Rnd 4K d32` read 6564 IOPS; most commands at the drive at once 5-8 for every 1 MiB step, but **2** for `Rnd 4K d32` (32 issued at once by the program) and 1 to 2 at depth 1 (P.6) |
| XP, Quick on `C:` (piix3-ide) | 228.1 / 231.7 MB/s sequential; the processor figures with 15.6 ms steps (XP's tick) |
| XP, the `3.2.0.0` install for P.3 and P.6 | not read: the console install ended exit 2 with Found New Hardware wizards open, as the q35 base's own check saw on its profiles |

### Owed

- ME: P.2, P.5b (the second study run) and P.7 need Stor98 on ME behind
  the tunnel, which the q35 bases do not yet install (task 44.3); a
  `SUBST` drive on ME.
- P.3 and P.6 on 2000, XP and 10; P.4 on 2000 and 10; the Quick run on 8
  and 8.1 (their q35 bases were not ready); the 32-bit tool alone under
  WOW64 on 7 x64 (P.11).
- The real-hardware readings: 98 SE at a high resolution on the owner's
  machine (the emulator's Cirrus reading stands for it); P.10.

## 44.22 - Windows 8 and 8.1 under `xhci98.sys`: the notice beside Windows' own driver, UAS, Stand by, the uninstall (2026-10-25)

Task 44.22 (was W8USB.1). The same vehicle as 44.24 (43.28's QEMU whose
stick declares `bcdUSB` 0x0210 at High Speed), on overlays of 43.24's
prepared Windows 8 x86 and 8.1 x86 images, 2 GB, 2 vCPUs; the `3.2.0.0`
release zip.

| Reading | 8.1 x86 | 8 x86 |
|---|---|---|
| Explorer under Windows' own driver | the SuperSpeed stick "Connected to USB 3.0"; the High Speed stick "Device can perform faster when connected to USB 3.0" | - |
| `INSTALL /console /usb /yes` | exit 3 in 245 s; the controller and its root hub on `xhci98`, both sticks on `USBSTOR` | exit 3 in 136 s; the same |
| XHCITR's notice | "Can run faster ... (G:) ... Runs at: High Speed (480 Mbit/s), Can run at: SuperSpeed (5 Gbit/s)" | the same |
| **Two controllers** (a QEMU `nec-usb-xhci`, `1033:0194`, added after the install, with a stick of its own) | - | Windows bound `USBXHCI` to the new controller ("Renesas USB 3.0 eXtensible Host Controller"), `qemu-xhci` stayed on `xhci98`; Explorer showed **"Device can perform faster when connected to USB 3.0" for the High Speed stick on the `xhci98.sys` controller**, as design record 13 section 6.12.11 predicted (Explorer's test asks for any controller under `usbxhci`), and XHCITR's notice named the same stick: both notices at once on such a computer |
| UAS | `usb-uas` Code 10 under `UASPStor`, as under Windows' own driver (the vehicle; 44.24) | the same |
| `INSTALL /console /uninstall /yes` | **one run**, exit 3 in 45 s; after the restart the controller on `USBXHCI` | **one run**, exit 3 in 52 s, with the second controller present; after the restart both controllers on `USBXHCI` |

### Stand by on 8.1 x86

As on 10 (44.24), Windows' Basic Display driver disables S3. QEMU's
`virtio-vga` with virtio-win 0.1.302's `viogpudo` (`w8.1\x86`): `pnputil`
staged the package but Windows kept the Basic Display driver (the package
is not signed for 8.1 and ranks below the in-box driver), so it was
installed on the device by `UpdateDriverForPlugAndPlayDevicesW` with
`INSTALLFLAG_FORCE` (returned TRUE); `powercfg /a` then listed "Standby
(S3)".

| Reading | Result |
|---|---|
| `INSTALL /console /usb /yes` on that overlay | exit 3 in 228 s, the controller on `xhci98` |
| Stand by under `xhci98.sys` (an elevated prompt, `rundll32 powrprof.dll,SetSuspendState 0,1,0`) | QEMU "paused (suspended)" 13 s after the command and still suspended after 90 s; `system_wakeup`: running, `powercfg /lastwake` "Fixed Feature, Power Button"; the controller, its root hub and both sticks started; a 2 MB file on each stick compared, "FC: no differences encountered" on both. (An earlier attempt woke 7 s after it slept, its wake source the same "Power Button": keys sent to the guest while it went to sleep, the vehicle's.) |
| Stand by on 8 x86 | not read: `viogpudo` (`w8\x86`) did not install on the device ("No more data is available"), and the forced install that worked on 8.1 was not tried there |

## 44.24 - Windows 10 under `xhci98.sys`: the notice, UAS, Sleep, the uninstall (2026-10-25)

Task 44.24 (was W10USB.1); the owner's answers of 2026-10-25 (the
decisions table, "44.24"). Development host A, QEMU under TCG, headless,
2 vCPUs; the guest `w10x86-usb`, an overlay of 43.33's prepared Windows 10
22H2 x86 image (legacy BIOS, test-signing on), 2 GB. The QEMU is 43.28's
build whose `usb-storage` declares `bcdUSB` 0x0210 at High Speed
(`E:\xhci98-vm\p43-28\qemu-bcd`), `qemu-xhci,p2=4,p3=2`: a stick `P44SS1`
on a USB 3 port (SuperSpeed), a stick `P44HS1` on a USB 2.0-only port
(High Speed), a `usb-uas` disk on a USB 3 port, the tablet. The package:
the `3.2.0.0` release zip (`6da9540f...0820`).

### Under Windows' own driver

| Reading | Result |
|---|---|
| Devices | the controller on `USBXHCI`; both sticks on `USBSTOR`; `usb-uas` Code 10 under `UASPStor` |
| Explorer's details pane (This PC, each drive selected) | the SuperSpeed stick: "Connected to USB 3.0"; **the High Speed stick: "Device can perform faster when connected to USB 3.0"** - the line design record 13 section 6.14.4 read statically, shown on 10 for the first time here (QEMU's own stick, `bcdUSB` 0x0200, cannot raise it) |

### `xhci98.sys` opted into (`INSTALL /console /usb /yes`)

| Reading | Result |
|---|---|
| The install | exit 3 in 1,308 s (the "can't verify the publisher" prompts answered); the controller "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)" on `xhci98`, its root hub `USB\XHCI98_ROOT_HUB`, both sticks on `USBSTOR`; `xhciuas.inf` staged, not bound |
| Explorer's details pane | the SuperSpeed stick: "Connected to USB 3.0"; the High Speed stick: **no line**, as record 13 section 6.14.4 predicts (Explorer tests for `usbxhci`) |
| XHCITR's report (the notice) | "Can run faster ... QEMU QEMU HARDDISK USB Device (G:) (46F4:0001), Port 5 of xHCI98 USB 3 Controller, Runs at: High Speed (480 Mbit/s), Can run at: SuperSpeed (5 Gbit/s)"; XHCITR running in the tray |
| UAS | `usb-uas` Code 10 under `UASPStor`, `DEVPKEY_Device_ProblemStatus` 0xC0000001, the stack `\Driver\xhci98` alone (`uaspstor`'s start failed and it left the stack). **The same under Windows' own driver**: Code 10, 0xC0000001, the stack `\Driver\USBHUB3` alone (read on the same overlay after the uninstall). So QEMU's `usb-uas` fails `uaspstor`'s start the same way on both stacks, and this vehicle cannot tell whether 10's `uaspstor` takes this bus's `0x602` contract (record 13 section 6.14.2); the streams quirk (`XhciStreamsOff`) is off for QEMU's controller and the device's endpoints declare 16 streams. A qemu-flavour `xhci98.sys` of this branch was put in place to read the URBs `uaspstor` sends, but its port-0xE9 trace stayed empty on 10 (not chased). A real UAS disk is needed |
| The uninstall (`/uninstall /yes`) | **one run**, exit 3 (the controller and the root hub removed, both INFs, `xhci98`; `xhcihlpr` marked for deletion); after the restart the controller on `USBXHCI` again, the sticks on `USBSTOR` |

### Sleep, with a display driver that allows it

Windows' Basic Display driver disables S3 ("Graphics") whatever QEMU
offers (standard VGA, or no display adapter at all). With QEMU's
`virtio-vga` and the virtio-win 0.1.302 display driver (`viogpudo`,
`w10\x86`, added by `pnputil` from the virtio-win ISO; not this project's)
`powercfg /a` lists "Standby (S3)" as available.

| Reading | Result |
|---|---|
| Control, under Windows' own driver | sleep from an elevated prompt (`rundll32 powrprof.dll,SetSuspendState 0,1,0`): QEMU "paused (suspended)"; `system_wakeup`: back at the lock screen |
| Under `xhci98.sys` (`/usb /yes` again on the same overlay, exit 3 in 307 s) | the same sleep: "paused (suspended)" within seconds; woken by `system_wakeup` (`powercfg /lastwake`: "Fixed Feature, Power Button"); after it the controller, the root hub and both sticks started, and a 2 MB file written to each stick and compared: "FC: no differences encountered" on both |

The same call from the runner task did not put the guest to sleep (no
suspend in 150 s); from an interactive elevated prompt it does, so that
is the vehicle's, not the driver's.

### 10 x64 (`w10x64-usb`, 4 GB, the x64 package's test certificate trusted in the guest)

| Reading | Result |
|---|---|
| `INSTALL /console /usb /yes` | exit 3 in 207 s: the controller on `xhci98`, and the `usb-uas` disk on **`xhciuas`**, "xHCI98 USB Attached SCSI Storage ... started" - with the signer trusted, Windows ranked the staged `xhciuas.inf` above `uaspstor` (as the `3.2.0.0` matrix's N5 read on 8.1 x64 and 10 x64) |
| UAS under this bus | the disk `XHCIUAS\DISK&VEN_QEMU&PROD_QEMU_HARDDISK...` formatted, a 2 MB file written to it and to both sticks, each compared: "FC: no differences encountered" |
| `INSTALL /console /uninstall /yes` | **one run**, exit 3 in 537 s (the UAS disk, the controller and the root hub removed, both INFs, `xhci98` and `xhciuas`) |

So the one-run uninstall the owner's answer asks for (the decisions
table, "44.24") was read on 10 x86 and 10 x64 here, and on 8 x86 and 8.1
x86 in 44.22: case 3a's "may need a restart and a second run" is
dropped (`e1da6108`). The `3.2.0.0` matrix (N5) once needed a second run
on 10 x64 when a UAS restart was pending; the run's own recovery still
says so if it happens.

## 44.25 - Secure Boot and memory integrity said before the install; the OVMF guest (2026-10-25)

Task 44.25 (was SECBOOT.1). Design record 26 section 5.3.3, the owner's
decision of 2026-10-25 (the decisions table, "44.25"); the installer at
`0f0f50e3` (its x86 and amd64 builds with every gate passed there).

### The OVMF guest (owed since 43.32)

Built on development host A, 2026-10-25, under TCG with 2 vCPUs and 4 GB,
headless:

- QEMU 11.1.0 (scoop) `-machine q35,smm=on`, the Secure Boot build of
  OVMF that QEMU ships (`edk2-x86_64-secure-code.fd`, read-only, with
  `cfi.pflash01` `secure=on`), and a variable store made from QEMU's
  `edk2-i386-vars.fd` by `virt-fw-vars` (virt-firmware 26.10, run from its
  wheel, nothing installed) with `--enroll-microsoft --secure-boot`:
  Microsoft's OEM PK, KEK 2011 and 2023, the Windows and UEFI CA 2011 and
  2023 in db, Secure Boot enabled.
- Windows 10 22H2 x64 installed unattended from the 22H2 ISO (`Win10_22H2_English_x64v1.iso`)
  onto a GPT disk (EFI, MSR, Windows), the answer file on a USB stick; no
  test-signing in specialize; the first logon made the runner task. About
  90 minutes from the ISO's boot to the first logon's power-off.
- The base kept read-only for later legs, each on an overlay with a
  private copy of the variable store.

### Readings (overlay `leg-a`)

| Reading | Result |
|---|---|
| `UEFISecureBootEnabled` | 1; `Confirm-SecureBootUEFI` True; `SystemStartOptions` ` NOEXECUTE=OPTIN  NOVGA`; no "Test Mode" on the desktop |
| `bcdedit /set {current} testsigning on` | refused, "An error has occurred setting the element data" |
| Memory integrity | `HypervisorEnforcedCodeIntegrity` `Enabled` absent; Device Guard: nothing configured or running (no hypervisor under TCG) |
| `INSTALL /console /cmonly /yes` (this branch) | exit 1 in 34 s: the Secure Boot sentence ("Secure Boot is on: Windows does not load the test-signed drivers ..."), then the x64 gate's refusal, "No changes were made" |
| `INSTALL /console /usb /yes` (this branch) | exit 1 in 30 s: the Secure Boot sentence, the 8-and-later note ("... on a start with driver signature enforcement disabled (Advanced startup, Troubleshoot, Startup Settings). Otherwise the controllers show Code 52 ..."), then "REFUSED: Secure Boot is on, and Windows does not load the xHCI98 USB drivers then: every USB 3 controller, the keyboard's and mouse's included, would stop working after the restart. Turn Secure Boot off in the computer's firmware setup first, or leave the USB drivers out. Nothing was changed." The controller stayed on `USBXHCI` |
| Sleep | `powercfg /a`: S3 "disabled by ... Graphics" here too |

Not read: memory integrity turned on (no guest here runs the hypervisor
under TCG); the 32-bit refusal of the connection manager (no 32-bit UEFI
guest; it rests on the host vectors and Microsoft's documented policy).
A wording point for a later pass: with Secure Boot on, the USB drivers'
run prints the test-signing note's "If this start is such a start, go
on" before its refusal.

## 44.26 - a tunnel this driver built, adopted after a PnP restart of the NHI, rebuilt across a hibernate (2026-10-25)

Task 44.26 (planned as RESUME.1), found by task 43.71 (`runs/run-43.md`,
"43.71", runs 1 and 2). Branch `p44-res`, development host A.

### The design and the code

- Design record 22 section 9.4 (new), with sections 7.6, 11 and 14.1
  pointing to it: `b6210716`, revised `6cf689ec` and `5b1b4617`;
  reviewed and revised until the review returned nothing. An instance's
  stop hands the next instance on the same NHI a record of the tunnels
  it built; the successor matches a port it finds UP and preserved
  against it as a whole, and section 7.6 then counts its tunnels as
  built here, so a sleep that loses the domain's configuration rebuilds
  them.
- The code: `dd22f045`, revised `c4f87338`, `6145c548` and `72444a77`
  (`src\cm\tbt_u4cm.c` and `.h`: `TbtU4SetCarry`, `TbtU4GetCarry`, the
  `Own` mark, the match after a completed discovery; `src\cm\cm_nhi.c`,
  `cm.h` and `cm_entry.c`: four image-static slots keyed by the NHI's
  device object and ids, stamped, taken at every start, emptied at a
  surprise removal). Reviewed and revised until the review returned
  nothing.
- Host vectors: `test\test_u4cm.c` `test_carry` (the 43.71 case rebuilt;
  no record, another UUID, other HopIDs, another host router or port, a
  recorded PCIe tunnel beside an unrecorded USB3 one, an orphaned USB3
  lane entry, a HELD port and an ejected port's lone USB3 tunnel all
  preserved as someone else's and HELD after a lost sleep; the record
  handed on to a third instance; none from a contained, failed, recovered
  or suspended instance or after an unmet resume obligation; forwarded by
  a start whose rings were refused). `test_u4cm` 1230 checks, 0 failures;
  three mutations of the code each failed the vectors meant to catch them
  (`Own` dropped from `TbtU4Suspend`; the device-lane check removed; the
  latch cleared at a resume).

### The leg: 7 SP1 x86, the emulator's SHPC tunnel bridge

The kit of the `3.2.0.0` matrix (`E:\xhci98-vm\m32\N4`, copied to
`E:\xhci98-vm\p44\p44-res`): i440FX with ACPI, the emulator copy
`bin-2a893ea` (SHA-256 `6a1dfcc8...`), `usb4-host` `profile=usb4-generic`
`host-model=amd` `host-domain=3`, the SHPC tunnel bridge `tb0`, the
ASM2464 enclosure with an NVMe on an overlay of `nvme-fat.qcow2`, attached
at every start. Windows 7 SP1 x86 (`vm\win7.img` overlay, 2 vCPUs, 2 GB),
`3.2.0.0`'s release package installed by `INSTALL /console /yes
/thunderbolt /stor98` (exit 3, F: "ORICO" on Stor98). The NHI's PnP
restart is made by `devcon restart *DEV_00F4*` (WDK 7.1's `devcon.exe`,
in the guest only): Windows removes and adds the NHI's function driver,
as it did by itself at boot in 43.71 on q35.

| Step | `3.2.0.0`'s `xhcicm.sys` (`6be7fcd2...`), the control | This branch's release `xhcicm.sys` (`7bca5644...`, built at `6145c548`) |
|---|---|---|
| Start, then the NHI restarted | F: present; the new instance's log `u4.preserved=00010809` | F: present; `u4.carry.in=00000001`, `u4.preserved=00010809`, `u4.preserved.own=00010001` |
| A 1 MiB write to F: (`CHK.BAT`) | `W1.BIN` written and compared | `W2.BIN` written and compared |
| `shutdown /h`, the emulator started again with the enclosure attached, resumed to the lock screen | `u4.resume.held=00000001`, `cm.reenum.resumed=00000000`; F: missing ("The system cannot find the path specified"), the USB Tree's warning in the taskbar | `cm.reenum.hold.passed=00000028`, `cm.resume=00000001`, `u4.pcie.up=00010809` (the tunnel rebuilt), `cm.reenum.resumed=00000001`, `cm.reenum.hold.released=00000028`; F: present with `TESTDATA.BIN`, `W1.BIN` and `W2.BIN` |
| `shutdown /s` straight after the resume | - | completed in 15 s |

The same leg on Windows 10 22H2 x86 (`w10x86-pristine.qcow2` overlay, the
same kit and topology, 2 vCPUs, 2 GB; `3.2.0.0` installed by `INSTALL
/console /yes /cmonly /stor98`, exit 3, F: "ORICO" on Stor98; this
branch's `xhcicm.sys` `7bca5644...` copied over the installed one and a
restart): after `devcon restart *DEV_00F4*` the log read
`u4.carry.in=00000001`, `u4.preserved=00010809`,
`u4.preserved.own=00010001`; `W1.BIN` written and compared; `shutdown
/h`, the emulator started again with the enclosure attached, the lock
screen after about 7 minutes under host load; the log
`cm.reenum.hold.passed=00000028`, `cm.resume=00000001`,
`u4.pcie.up=00010809`, `cm.reenum.resumed=00000001`,
`cm.reenum.hold.released=00000028`; F: present with `TESTDATA.BIN` and
`W1.BIN`. `shutdown /s` straight after the resume completed in 45 s.
**PASS** (evidence `w10-serial-b.log`, `w10-serial-c.log`,
`w10-after-resume.png`).

On Windows 2000 (`win2k-smp.img` overlay, ACPI, the same topology) the
NHI was restarted through Device Manager's Disable and Enable (WDK 7.1's
`devcon.exe` is not a Windows 2000 program): the guest under host load
took over two minutes between the two, and the new instance read
`u4.carry.in=00000000` and `u4.preserved=00010809` - the record was older
than section 9.4's 120 s and was dropped, as the section says. Not a
reading of the match on 2000; owed (a faster restart, or the `qemu`
flavour's log of `u4.carry.out` beside it).

Verdict: **PASS**. The control reproduces 43.71's second cause on the SHPC
bridge too, not only on q35's native hot-plug port, and the change
removes it there. On a native hot-plug port the drive is still reported
missing at the resume by `pci.sys` (43.71's first cause, the release
notes' limitation); this change does not touch that.

The leg's binary is the one built at `6145c548`; `72444a77` changes one
declaration's `const` for the x64 compiler and nothing the x86 code does.

Evidence: `E:\xhci98-vm\p44\p44-res\evidence\` (the serial logs of the
`XHCITR /report` runs, `control-serial-b.log`, `control-serial-c.log`,
`fixed-serial-d.log`, `fixed-serial-e.log`; the screens after each
resume), `shots\s7-*.png`, `pids.txt`.

## 44.27 - the hibernate fix re-checked with 44.26's change (2026-10-25, partial)

Task 44.27 (planned as HIBER.1). The hibernate fix of 43.24 (no NT bridge
D0 held, `p43-24n`) read again together with 44.26's change, on the kit
and topology of 44.26's leg (SHPC tunnel bridge, `usb4-generic`
`host-model=amd`, the ASM2464 enclosure with its NVMe on Stor98 attached
at every start), each guest with `3.2.0.0` installed by `INSTALL
/console /yes` and this branch's release `xhcicm.sys` (`7bca5644...`,
built at `6145c548`) copied over the installed one. Each read: the
resume's log through `XHCITR /report`, the drive and its files after the
resume, and the first `shutdown /s` straight after it. Host load was
high throughout (other workers' builds and guests), so the times are
upper bounds.

| Guest | The tunnel at the hibernate | Resume (emulator start to logon) | After the resume | First shutdown after it |
|---|---|---|---|---|
| 2000 SP4, ACPI (`win2k-smp.img`, `/thunderbolt /stor98`; the power button set to Hibernate, the password prompt off) | built by the instance running (a restart before) | about 5 minutes | `cm.reenum.hold.passed=00000028`, `cm.resume=00000001`, `u4.pcie.up=00010809`, `cm.reenum.resumed=00000001`, `cm.reenum.hold.released=00000028`; F: with `W1.BIN` and `W2.BIN` | 31 s |
| 7 SP1 x86 (`/thunderbolt /stor98`) | the earlier instance's, after `devcon restart` (44.26's leg) | under 4 minutes | the same lines; F: with its files | 15 s |
| 8.1 x86 (`w81x86-pristine.qcow2`, `/cmonly /stor98`) | the earlier instance's, after `devcon restart` (`u4.preserved.own=00010001`) | about 8 minutes, to the lock screen | the same lines; F: with `W1.BIN` | 21 s |
| 10 22H2 x86 (`/cmonly /stor98`) | the earlier instance's, after `devcon restart` (44.26's leg) | about 7 minutes, to the lock screen | the same lines; F: with `W1.BIN` | 45 s |

No bug check, no stall and no five-minute watchdog on any of the four;
the bridge's D0 passed at once (`cm.reenum.hold.passed`) on 2000 and 7 as
on 8.1 and 10. On 2000, under the same host load, the desktop twice
stopped updating for about five minutes (before the hibernate and after
the resume) and then went on by itself; both processors were running
throughout (QMP `info registers`).

Not read here, owed to 44.27: Vista x86 and 8 x86; XP x86 (its image
offers no hibernation, as in the `3.2.0.0` matrix); the x64 halves of XP,
Vista and 7, which need the unsigned x64 package signed in the guest with
a throwaway certificate; and the first shutdown after such a resume
stalling on XP x64 and 7 x64 (`runs/run-43-24.md`), whose guests run the
Thunderbolt 3 (Titan Ridge) profile and need the same signing.

Evidence: `E:\xhci98-vm\p44\p44-res\evidence\` (`a2k-serial-*.log`,
`w81-serial-*.log`, `w10-serial-*.log`, the screens after each resume),
`pids.txt`.

### 44.27, the second part (2026-10-26, partial)

On `3.3.0.0` at `034b2826` (branch `p44-res2`): gated builds of both
architectures passed (`BUILD + GATES PASSED`), and an x86 test cut
(`make-release.ps1 -Arch x86 -Flavor release -TestLabel "44.27 legs"`,
without the driver pack and the two free and non-free sets; x86
`xhcicm.sys` `2e2159a9...`). The amd64 release package was staged
unsigned by `make-package.ps1`, with its five catalogs made by the
packager's own `New-PackageCatalogFile` (MakeCat), as 43.36 did; in each
x64 guest a throwaway certificate (`CN=p44-res guest throwaway (not
trusted)`, WDK 7.1 `MakeCert`) was made and trusted in that guest alone,
and the four `.sys` and five `.cat` files were signed there with WDK 7.1
`SignTool` (x64 `xhcicm.sys` `e27a887b...` before signing). Nothing was
signed on the host. The same kit and topology as the first part (SHPC
tunnel bridge, `usb4-generic` `host-model=amd`, the ASM2464 enclosure
attached at every start), installed by `INSTALL /console /yes` with
`/thunderbolt /stor98` (Vista, 7 x64) or `/cmonly /stor98` (8).

| Guest | Before the hibernate | After the resume | First shutdown after it |
|---|---|---|---|
| Vista SP2 x86 (`vista.img`) | after `devcon restart`: `u4.carry.in=00000001`, `u4.preserved.own=00010001`; `W1.BIN` written and compared | `cm.reenum.hold.passed=00000028`, `cm.resume=00000001`, `u4.pcie.up=00010809`, `cm.reenum.resumed=00000001`, `cm.reenum.hold.released=00000028`; F: with `W1.BIN` | 12 s |
| 8 x86 (`w8x86-pristine.qcow2`) | the same lines (`u4.preserved.own=00010001`); `W1.BIN` | the same lines; F: with `W1.BIN` | 15 s |
| 7 SP1 x64 (`v-x64-base\w7x64-ts`, test-signing on) | the tunnel built at boot (`u4.pcie.up`); install with no prompt (the certificate trusted); `W1.BIN` | the same lines; F: with `W1.BIN` | 13 s, no stall |

**PASS** on all three. Vista went back into hibernation by itself about
two minutes after a resume left at the lock screen with no input (an
unattended wake); the reading above is of a resume answered within that
time. Under `usb4-generic` on 7 x64 the first shutdown after a resume
with the drive mounted did not stall; 43.24's stall was read under the
Thunderbolt 3 (Titan Ridge) profile and is not yet read again here.

Not read, owed: Vista x64 (installed, exit 3; its restart then took about
25 minutes to the desktop under host load, not investigated), XP x64
(installed up to the Stor98 prompts when the work was paused), and the
XP x64 and 7 x64 shutdown stall under the Titan Ridge profile. XP x86 stays
not read: its image offers no hibernation (as in the first part).

Evidence: `E:\xhci98-vm\p44\p44-res\evidence\` (`vi-serial-*`,
`w8-serial-*`, `s7x64-serial-*`, the screens after each resume),
`pids.txt`.

## 44.28 - Intel's Thunderbolt software beside this package (2026-10-25)

Task 44.28 (was INTELTB.1). Development host A, QEMU under TCG, headless,
2 vCPUs, 4 GB; the guest an overlay of 43.33's prepared Windows 10 22H2
x64 image (`w10x64-intel`), legacy BIOS, test-signing mode on; usb4-emu's
QEMU (`vm\usb4\bin-2a893ea`) with the `tb3-alpine-ridge` profile (NHI
`8086:15BF` at 00:04, tunnel bridge at 00:05, the controller's xHCI
`8086:15C1` at 00:07), a `usb-uas` disk on `qemu-xhci`. The package: the
`3.2.0.0` release zip (`xhci98-3.2.0.0.zip`, SHA-256 `6da9540f...0820`),
its x64 half's test certificate added to the guest's Root and
TrustedPublisher stores.

### Intel's driver in the guest

Intel's DCH Thunderbolt driver 1.41.1379.0 (the Microsoft Update Catalog
package design record 21 section 3.6 read statically; `drv.cab` SHA-256
`d01be657...69eb1c6`; all four INFs added by `pnputil -i -a`, each
"Successfully installed", Microsoft "Windows Hardware Compatibility
Publisher" signature):

| Reading | Result |
|---|---|
| Before | the NHI "Base System Device", Code 28; both xHCI controllers on `USBXHCI`; `usb-uas` Code 10 under `UASPStor` (the vehicle, 43.33) |
| After `pnputil`, and after a restart | the NHI "Thunderbolt(TM) Controller - 15BF", service **`nhi`** (`TbtBusDrv.sys`, demand start), `oem2.inf` 1.41.1379.0, upper filter `WUDFRd`; **Code 31**, System event 9006 from `nhi` ("Error code: 0x1F7, Sub error code: 0x227") at each start; `TbtP2pShortcutService` running. The emulator's NHI does not satisfy Intel's driver, but the driver is bound, which is the case the installer meets |
| The Thunderbolt Control Center | not available offline (a Store application); not read |

So on a 10 x64 machine with Intel's DCH driver the NHI's service is `nhi`,
as both of Intel's packages name it (record 21 section 3.6, static).

### `3.2.0.0` over Intel's driver

| Leg | Reading |
|---|---|
| `INSTALL /console /cmonly /yes` | exit 3 in 1,957 s; "The connection manager alone: installed without asking"; the NHI listed "INF xhcicm.inf; driver nhi 1.41.1379.0; another driver: left", problem code 31; **but** the tunnel bridge was given the filter ("Adding the filter to PCI-to-PCI Bridge ... done"), `xhcicm` became `BOOT_START`, and the mark service `xhcimark` was installed and running; the Add/Remove Programs entry "xHCI98 Thunderbolt 3 / USB4 connection manager 3.2.0.0". After the restart: the NHI still Intel's (Code 31), the bridge "xHCI98 PCI Bridge" with `UpperFilters` `xhcicm` |
| `INSTALL /console /uninstall /yes` | exit 3 in 749 s, one run: `xhcimark` stopped and removed, the filter taken off the bridge and the port given back to Windows' own driver, `oem6.inf` and `oem7.inf` removed, `xhcicm` removed; the NHI untouched (`nhi`, Code 31, `WUDFRd`) |

### This branch's installer over Intel's driver (record 23 section 8.5.1)

The same overlay, Intel's DCH driver on the NHI (Code 31, `nhi`), the
`3.2.0.0` package with this branch's `XHCIIN64.EXE`, `XHCIWZ64.EXE` and
`SETUP.EXE` at `0f0f50e3` in place of its own:

| Leg | Reading |
|---|---|
| `INSTALL /console /list` | "Intel's Thunderbolt driver runs Thunderbolt(TM) Controller - 15BF."; "/list: it would be asked: Y install it, N leave it out; Enter would give [N]"; "Intel's Thunderbolt driver is left as it is (/thunderbolt replaces it)."; Stor98 "left out, it comes with the Thunderbolt 3 / USB4 support"; exit 0 |
| `INSTALL /console /cmonly /yes` | exit 3 in 36 s: "/yes: N (/thunderbolt installs it)", Intel's driver left; only the tools installed (the USB Tree, the window, the console tool) and an Add/Remove Programs entry; no filter on the bridge, no `xhcicm`, no `xhcimark`; the NHI still `nhi`. The entry was named "xHCI98 USB 3.x drivers 3.2.0.0" though no driver was installed (the name rule's default; record 26 section 5.3.1's case 5 has the same) |
| `INSTALL /console /thunderbolt /yes` | exit 3 in 115 s: the NHI listed "driver nhi 1.41.1379.0; install (no driver)", problem code 31; the boot-driver warning; then "WARNING: Intel's Thunderbolt driver runs this computer's Thunderbolt controller. Ticked, the xHCI98 connection manager replaces it there, and Intel's Thunderbolt software no longer controls it. The uninstall gives the controller back to Intel's driver. Windows Update may also put Intel's driver back." (each line within 78 columns); "Installing Thunderbolt(TM) Controller - 15BF ... done"; afterwards "xHCI98 TB3 Controller (Intel Alpine Ridge): xhcicm 3.2.0.0, started", the bridge filtered, `xhcimark` running. After the restart the NHI on `xhcicm`, Code 0 |
| `INSTALL /console /uninstall /yes` | exit 3 in 94 s, one run: `xhcimark` removed, the filter off the bridge, the NHI's devnode removed, both INFs and `xhcicm` removed; after the restart the NHI on Intel's `nhi` again (Code 31, upper filter `WUDFRd`), the bridge on `pci` alone |

The `3.2.0.0` install left Intel's NHI alone while installing the half of
the connection manager that cannot work without the NHI's other half: the
case record 23 section 8.5.1 removes. Nothing in the reading contradicts
the owner's decision of 2026-10-25 (the decisions table, "44.28").

## 44.29 - The 43.47 Stall on 98 SE and ME, Read Again (2026-10-25)

Roadmap 44.29 (was STALL47.1): the cause of 43.47's connection-manager
thread stall on 98 SE and ME (`runs/run-43.md`, "43.47r"), under 43.47's
stress loop in the emulator.

### The vehicle

Development host A, QEMU under TCG, headless, one vCPU, 256 MB, one
guest at a time; the emulator binary of 43.47 (SHA-256 prefix
`9dfdcf9b`); 43.47's 98 SE image under the Alpine Ridge topology of the
ICM client and its ME image under the generic USB4 topology, each an
overlay of a copy; 43.47's stress loop (`stress.ps1`: an MS-DOS Prompt's
`copy /b E:\BIG.BIN NUL` with its 20th read held by the harness, the
unplug, the re-plug while vetoed, Abort, the Explorer window closed, the
prompt exited), extended to take, at a stall, `info registers` and a
`pmemsave` of the guest's 256 MB.

| Run | System, build | Cycles | Stalls |
|---|---|---|---|
| u2 | ME, USB4; the 44.30 reading build (this branch at `4122d90b`, the backoff of 43.47f included) | 20 | 0 |
| s1 | 98 SE, ICM; the `qemu` build of `62a5649d` | 1 | 1, at cycle 1 |

### The stall (98 SE, s1, cycle 1)

As 43.47r's: the vanished child's query vetoed (17h) at the backed-off
times, the re-plug's nudge, five more vetoes, then after the holder's
release nothing more from the connection-manager thread - no sixth query,
and an unplug of the enclosure three minutes later not logged. This time
the system went on (the tray clock ran; the CPU idle, `HLT=1`), as on
ME's stall of 43.47f.

### What the memory image shows

Read from the `pmemsave` image with this driver's own map (the `qemu`
image relinked with `-map` from the same objects, the same layout) and,
for the frames below the driver, a static reading of 98 SE's `VMM32.VXD`
(the VMM), `NTKERN.VXD` and `VWIN32.VXD` from the owner's CD placed by
their code in the image (`legal-provenance.md` section 4, the row of
task 44.29); runtime facts here are the image's, static ones the files'.

1. **The thread.** It is inside its own loop's timed wait
   (`cm_thr.c`, `cmThread`: `KeWaitForSingleObject(&WorkEvent, ...,
   50 ms)`, `ThreadWait` = timed), below it NTKERN's
   `KeWaitForSingleObject` -> VWIN32's `_VWIN32_WaitSingleObject` ->
   VMM's `Wait_Semaphore`. Its two workers (the 98 CONFIGMG worker and
   the call worker) are idle in untimed waits on their own events. No
   CONFIGMG call of this driver is in flight.
2. **Why the wait does not end.** [static] On 98 SE a timed kernel wait
   is a VMM *global* time-out (`Set_Global_Time_Out`) whose callback
   signals the waiting thread's semaphore. [runtime, the image] The
   thread's time-out is still queued, first on the VMM's global list
   with one millisecond left, and the list has not been processed for
   about 158 s: VMM's accumulator of unprocessed time is non-zero, which
   (static) the VMM requests processing for only when it goes from zero
   to non-zero, yet no processing event is queued or running and the
   event node it would use is back on the free list. Every global
   time-out of the system is stopped with it: two other kernel threads
   of another WDM driver and four Win32 threads sit in timed waits whose
   time-outs are queued behind it; the *asynchronous* time-out list, and
   with it the clock, still runs.
3. **Not a missed signal.** The work event is unsignalled with the
   thread's wait block still queued on it: nothing set it during the
   wait. On the ICM client this driver polls the host interface from the
   thread itself, and only a PnP or power request or a query device opening sets it otherwise; none came.

**Verdict: the stall is the 98 SE VMM's global time-out processing
stopping, not a wait or a lock of this driver's.** What removed the
processing event without running it is **not identified**: this driver
calls no VMM event or time-out service (none of `src\cm`, `src\vsd` or
Stor98's 9x sources does), and NTKERN and VWIN32 cancel no such event
by a kept handle; the likeliest shape (inferred) is a cancel through a
stale event handle by some VxD on the path the holder's release takes
(the aborted I/O on the removed drive, the system-modal error screen),
which the image cannot name. Nothing in the tree changes for it; the
backoff of 43.47f stays the mitigation, and the known limitation stays
in the release notes.

**What would name it.** A live read on the same loop: a QEMU gdbstub
write watchpoint on the VMM's accumulator (linear `C0011418` in this
boot) or a breakpoint on the VMM's restricted-event cancel routine
(`C00034E4` here), stopping at the call that cancels the processing
event. And what would let this driver ride through it: waiting on a
kernel timer (`KeSetTimer`, which 98 SE's VWIN32 places on the
*asynchronous* list) beside the work event, in the thread's loop and its
sleeps, instead of a timed wait - which keeps the connection manager
running but not the rest of the system, whose own timed waits stay
stopped (the owner's call; not done).

Images and readings: `E:\xhci98-scratch\p44-9x\dump-w98-s1-c1.bin`
(SHA-256 `dc4dac71...5c665c8`), `E:\xhci98-vm\p44\p44-9x\keep\stall-w98-s1-c1.log`,
`regs-w98-s1-c1.txt`, `stress-me-u2.txt`, `stress-w98-s1.txt`; the
readings' scripts and listings under `E:\xhci98-scratch\p44-9x\ntk\`.

### The live read: the canceller named (2026-10-25)

The owner chose the live read first (2026-10-25); no product code.

**The vehicle.** The same 98 SE image and build as s1 (the Alpine
Ridge topology, the `qemu` `xhcicm.sys` of `62a5649d`, SHA-256 prefix
`6c972b47`), a fresh overlay, AutoPlay off, with QEMU's gdbstub on the
worker's own port and a small client of its remote protocol
(`scripts\gdbcancel.py` of the kit): one breakpoint inside the VMM's
event-cancel routine, past its test that the event has neither run nor
been cancelled (`C00034EF`, the routine behind `Cancel_Restricted_Event`
and `Cancel_Priority_VM_Event`, read statically), about 20 hits a second
at rest; each hit reads the cancelled event's node, and a node whose
callback is the time-out lists' handler (`C0004047`, the routine that
processes the global, per-VM and per-thread time-out lists) is logged
with the stack. The breakpoint is lifted while the harness types (QEMU
drops keys while the guest stops many times a second), and 43.47f's
`stress2.ps1` loop runs as before. A first try that watched every
processing episode of the global list (some 66 a second) slowed the
guest past what the loop's typing survives and was dropped.

**The reading** (debugger, in `legal-provenance.md`'s sense: read
through QEMU's stub, not this driver;
`E:\xhci98-vm\p44\p44-9x\keep\gdbc-g2.log`). In the
fourth cycle, with the MS-DOS Prompt being closed, one cancel of a node
whose callback is the lists' handler: the node `C145B9E4`, its reference
a VM's time-out list (`C29200AC`, the VM block `C2920074` plus `38h`),
cancelled from `C00707EB` with `C02C0E79` above it on the stack. The
global list went on (its accumulator 0); a per-VM list's processing
event was cancelled instead.

**Whose code that is** (static, `legal-provenance.md` section 4, the
row of task 44.29's live read): Windows 98 SE's `VMOUSE.VXD`, placed in
the s1 image by its code (object 1 at `C007073C`, object 3 at
`C02C0E40`). That the live boot had it at the same addresses is
inferred, not read in that boot: both return addresses on the live stack
fall on VMOUSE's return points at that placement (`C00707EB` after its
cancel, `C02C0E79` after its call of that routine), and the callback
`C02C0EA7` the live counts name is VMOUSE's own entry at that placement.
`C00707EB` is VMOUSE object 1 offset `AFh`, the return of its
`Cancel_Restricted_Event` on the handle it keeps at offset 4 of its
per-VM data (`xchg [edx+4],esi` then the cancel, offset `A4h`), called
from object 3 offset `34h`, the routine that moves the mouse to another
VM (`xchg [C0070FD0],ebx`). VMOUSE stores at that offset the handle of a
priority VM event it schedules in three places (object 1 offsets `E9h`
and `170h`, object 3 offset `9C2h`: `Call_Priority_VM_Event`, then
`mov [edx+4],esi`); two of the three callbacks clear the offset first
(object 3 offsets `9D3h` and `9F0h`, `mov dword ptr [edx+4],0`), and the
third, `C02C1A2C` (object 3 offset `BECh`, the one scheduled at object 1
offset `E9h`), never does. The offset is otherwise written only by the
two paths that exchange it out before a cancel (object 1 offset `A6h`
above, and object 3 offset `B5h`, `xchg [ebx+4],esi` with ESI 0, then
`Cancel_Priority_VM_Event`). After that third callback has run, the
offset keeps a handle
the VMM has freed and may have given to another event; the next move of
the mouse to another VM cancels whatever event now holds the node.
Where that is the global time-out list's processing event, every global
time-out of the system stops, this driver's timed waits among them -
which is the shape of s1's image (the event node freed with no handler
run, the list frozen), though s1's canceller itself was not read.

**Verdict: VMOUSE's stale-handle cancel is read live and is the
supported explanation of the stall, which is then neither this driver's
nor Thunderbolt's; for s1 itself it is inferred.** The live read caught
VMOUSE cancelling a time-out list's processing event (a per-VM list's);
s1's image shows the global list's processing event freed without
running, with no canceller recorded. On that explanation the trigger is
mouse input followed by the mouse changing VM - the MS-DOS
Prompt being focused, left or closed, as 43.47's legs and the stress
loop do at every cycle; the vanished-child transaction is where it was
seen because its legs exercise exactly that, and its 2 s retries made a
stopped thread visible. Which system event the stale handle hits is
chance: a per-VM list's here (no stall), and, inferred, the global
list's in s1 (the stall). ME's `VMOUSE.VXD` was not read, and whether
ME's stalls are the same bug is not established. Nothing in this repository can mend `VMOUSE.VXD` (no binary
is patched here); what this driver can do is the owner's choice: keep
its own waits off the global time-out list (kernel timers, which 98 SE's
VWIN32 puts on the asynchronous list) so that the connection manager
outlives such a cancel, while the rest of the system's timed waits stay
stopped until a restart.

Not read: a capture of the global list's own event being cancelled (the
loop ran 22 cycles under the breakpoint (21 passed, the 22nd a harness
slip), some 75,000 cancels in all, with
that one cancel of a processing event and no stall; the per-VM case shows the
mechanism and the canceller, s1's image the global outcome); ME.

### The installer's record, the owner's decision (2026-10-25)

The owner's decision of 2026-10-25 (`roadmap-usb4.md`, decisions table,
"44.30"): on 98 SE and ME the installer writes the Stor98 record's
`Version` whenever it stages the bundled Stor98, drive present or not.
Record 26 draft 43 (written as draft 41 on its branch) (`bf0a0ca4`, `67aaa66b`; record 28 section 12.3), the
code `59887865` and `9b7b3d72` (`inst_s98_version_owed`, its vectors in
`test\test_inst.c`); reviewed and revised until a round found nothing.
`scripts\build-driver.cmd all` passed at `9b7b3d72` (`test_inst` 3268
checks); the installer by `xhciinst\build.cmd x86`.

**The leg (98 SE, passed).** A pristine 98 SE image (an overlay of
43.5's `w98se-pristine`) under the generic USB4 topology with the first
tunnel bridge at 00:1Ch and no drive attached; the release package of
`9b7b3d72` (`make-package.ps1 -Flavor release`, `xhcicm.sys` SHA-256
`3f45b983...7737`, `XHCIINST.EXE` `7c6c3324...9cc9`) laid out as the
download is (`INSTALL.BAT`, `TOOLS\XHCIINST` with `XHCIDI.EXE`,
`release-x86`).

| Step | Reading |
|---|---|
| `INSTALL /console /thunderbolt /stor98 /yes`, no drive attached | "Copied 15 files to C:\XHCI98", the controller and the USB4 router installed, exit 3 |
| The record | `HKLM\Software\xhci98\Stor98`: `"PreExisting"=dword:00000000`, **`"Version"="3.2.0.0"`** (the branch's stamp) - nothing imported |
| Cold start, the drive plugged | the Stor98 wizard for the new device; it asked once for "Stor98 Storage Controller Disk" (`stor98.mpd`), answered `C:\XHCI98`, finished |
| Unplug and re-plug (the window the wizard outlasted) | `E:` (`Orico`) opened with its files; `StorInterrupt 2.1.0` = `pin 1 line 0 msi no msix no port 0/0 polling (interrupt line not set)`: started by the memory-only form, the record read as written by the install |

Found on the way, not this task's: the first plug's Stor98 wizard asks
once for the disk on a fresh 98 SE install (`C:\XHCI98` answers it), as
the controller added later does (W9XPROMPT.1, kept as how it works).

Screenshots `shots\c2-*`, `c3-*`.

## 44.30 - A 9x Function With No Interrupt, Read First (2026-10-25)

Roadmap 44.30 (was NOIRQ.1 and NV9X.4): on 98 SE in the emulator,
whether `Reprocess_DevNode` or `Setup_DevNode(READY)` clears a Code 29
that `PCI.VXD` set on a memory-only configuration, and where `PCI.VXD`
sets it (design record 28's note of 2026-10-19).

### The static reading

Windows 98 SE's `PCI.VXD` 4.10.2222 and `CONFIGMG.VXD`, and ME's
`PCI.VXD` 4.90.3000, read with an LE parser and a disassembler, nothing
executed (`legal-provenance.md` section 4, the two rows of task 44.30).
`PCI.VXD` disables the function itself, once, while it enumerates the
parent bus: with an interrupt pin, no routing-table link, a line of 0 or
`FFh` and its decode off, it builds no IRQ descriptor and calls
`Disable_DevNode(dn, CM_DISABLE_HARDWARE)`, which sets problem 29; a
function whose devnode already exists is skipped, and its filter and
start handlers never refuse a configuration without an IRQ. CONFIGMG's
`Reprocess_DevNode` and `Setup_DevNode(READY)` both clear a problem
whatever its code. Design record 28 section 12.1 has the detail.

### The runtime reading

- **Vehicle.** Development host A, QEMU under TCG, headless, one vCPU
  (`pentium3,-apic`), 256 MB; the emulator binary of the 43.18 and
  43.47 legs (SHA-256 prefix `9dfdcf9b`). 98 SE: an overlay of 43.47's
  98 SE image under the generic USB4 topology (3.1.0.0 installed,
  Stor98 bound on its earlier ports), with the first tunnel bridge moved
  to **00:1Ch**, where SeaBIOS's routing table (bus 0 devices 1 to 6)
  has no entry; the second at 00:06; the ASM2464 enclosure on port 1.
  The new bridge location took Windows' PCI-to-PCI bridge wizard
  (`MACHINE2.INF`), and the enclosure's NVMe at the new location the
  Stor98 wizard (`C:\XHCI98\STOR98.INF`).
- **Build.** A reading build of `xhcicm.sys`, the `qemu` flavour of this
  branch's tree at `4122d90b` with the reprocess-only attempt taken for
  the memory-only case and each status read logged (uncommitted; built
  with the DDK's `build` in `src\cm` alone, not gated; SHA-256
  `E85986FF...932E`), copied over the installed one and booted cold.
- **The installer record.** `HKLM\Software\xhci98\Stor98` held
  `PreExisting` and no `Version` on this image (installed with no drive
  attached), so the first window refused the drive "not Stor98's"
  (`cm.reenum.9x.noirq.refused=02080204`); `"Version"="3.1.0.0"` was
  imported by hand (`regedit /s`), as 42-A.15 did. Design record 28
  section 12.3.

| Step | Reading |
|---|---|
| The drive plugged, the tunnel up, the member re-enumerated | the NVMe at 02:01.0, status `00004620` with problem `1D` (29) at every read of the window |
| Its configurations (`cm.reenum.9x.noirq.lc`, `.rd`) | one basic configuration at `LCPRI_NORMAL` (`01003000`); one memory descriptor of 28h bytes (`01012800`), no IRQ, I/O or DMA one (`0400000F`, `0200000F`, `0300000F`), the every-type read meeting the one memory descriptor (`00012800`, `0000000F`) - as 42-A.15 read |
| The reprocess-only attempt (`cm.reenum.9x.noirq.after=02080300`, ACTIVE) | the status read again (problem 29), then `Reprocess_DevNode`: `cm.reenum.9x.noirq=0B000000` (`CR_SUCCESS`) |
| The status after it | `00006AA6`, no problem; then `00006AEE` (`DN_STARTED`); the line read (`.noirq=0D000000`), DONE (`.after=02080500`). `Setup_DevNode(READY)` not reached |
| Stor98 | `StorInterrupt 2.1.0` = `pin 1 line 0 msi no msix no port 0/0 polling (interrupt line not set)` |
| The volume | `E:` (`ORICO`) listed; `BIG.BIN` (16 MiB) copied to `C:`, copied back to `E:\W1.BIN`, `fc /b`: no differences |

**Verdict: the route exists.** On 98 SE, `Reprocess_DevNode` alone
clears the Code 29 `PCI.VXD` set, and the function starts on its memory
alone with Stor98 polling. `Setup_DevNode(READY)` alone was not read.
Design record 28 section 12 is the design that follows (draft 12).

The guest's log: `E:\xhci98-vm\p44\p44-9x\keep\debugcon-p3.log`;
screenshots `shots\p3-*.png`.

### The design, the code and the review

Design record 28 draft 12, section 12, the memory-only form (`acd5f42b`,
`18cdb8eb`, `1733b199`); the code in `src\cm\cm_noirq.c` and
`cm_noirq.h`, the vectors in `test\test_cm_noirq.c` (`70c6cb4a`,
`62a5649d`); reviewed and revised until a round found nothing.
`scripts\build-driver.cmd all` passed at `62a5649d` on x86 and on amd64
(`test_cm_noirq` 285 checks; the `qemu` `xhcicm.sys` SHA-256
`6c972b47...ab01a2`).

### The legs (2026-10-25, the build of `62a5649d`)

The vehicle as for the reading above; the `qemu` `xhcicm.sys` of
`62a5649d` copied over the installed one and booted cold on each image.
ME: an overlay of 43.47's ME image (generic USB4 topology, 3.1.0.0
installed), its first tunnel bridge moved to 00:1Ch the same way (the
PCI-to-PCI bridge wizard, a restart declined, then the Stor98 wizard
at the new location); its installer record held no `Version` either,
and `"Version"="3.1.0.0"` was imported by hand as on 98 SE.

| Leg | System | Reading |
|---|---|---|
| L1 the IRQ withheld (tunnel bridge 00:1Ch), a cold start and the drive plugged | 98 SE | the configuration read as before (one memory descriptor, no IRQ); `.noirq.after=02080300` (ACTIVE), `Reprocess_DevNode` `CR_SUCCESS` (`0B000000`), the line read (`0D000000`), DONE (`.after=02080500`); `StorInterrupt 2.1.0` = `pin 1 line 0 msi no msix no port 0/0 polling (interrupt line not set)`; a 16 MiB file written to `E:` and compared: no differences. **Pass** |
| L2 unplug and re-plug (a new devnode, a new generation) | 98 SE | the vanished child swept and retired, `UP` at the next generation, the form taken again and DONE; `E:` back with its files. **Pass** |
| L3 `XhciCmNoIrqStart` 0 machine-wide (`HKLM\System\CurrentControlSet\Services\USB`), a cold start, the drive plugged | 98 SE | no `noirq` operation at all, the drive not started (`dir E:\`: "Invalid drive specification"). **Pass**; the value set back to 1 |
| L4 the routed control: the tunnel bridge at 00:05 (in the BIOS's table), another overlay of the same image | 98 SE | the NVMe started by `PCI.VXD` with its IRQ (`StorInterrupt 1.1.0` as before, `E:` listed); never a candidate: `.noirq.refused=01080203` (EXCLUDED, "header": its line set). **Pass** |
| L1 and L2 on ME (tunnel bridge 00:1Ch) | ME | the first plug met the Stor98 wizard (the window closed meanwhile); after it, an unplug and re-plug: the same configuration, `Reprocess_DevNode` `CR_SUCCESS`, DONE; `StorInterrupt 2.1.0` = `pin 1 line 0 msi no msix no port 0/0 polling (interrupt line not set)`; 16 MiB written and compared: no differences. **Pass** |

Not read: `Setup_DevNode(READY)` alone (the reprocess sufficed every
time); `XhciCmNoIrqStart` 0 and the routed control on ME; a Device
Manager Refresh after the start; real hardware (no device at hand
presents the case, record 28 section 8.2).

Logs: `E:\xhci98-vm\p44\p44-9x\keep\debugcon-l1.log`, `debugcon-l3.log`,
`debugcon-r1.log`, `debugcon-m1.log`; screenshots `shots\l1-*`, `l2-*`,
`l3-*`, `r1-*`, `m1-*`.

## 44.31 the cut's two builds at once (2026-10-25)

The design is `build-and-test.md`, "The cut's two builds at once"
(`e2e6bb5d` to `60670ebc`, reviewed and revised); the code is
`scripts\package\cut-builds.ps1` and `package-common.ps1`'s
`Import-GatedBuild` and `Invoke-CutBuilds` (`4e4612c1` to `08d42090`,
reviewed and revised). Development host A, no virtual machine; other
work was running on the host throughout, so the times are a loaded
host's.

**Verdict: the two builds at once took 30:06 against 55:52 one after the
other, 25:46 saved (46 %); the import took 2 s; `make-release.ps1
-CheckOnly` admitted all four legs' records, the x64 two imported, and
the x86 half of a dry cut passed.** The x64 legs of the dry cut were not
packaged (below).

### The vehicle

- Tree A, the cut's: worktree `E:\xhci98-wt\p44-tree`, branch `p44-tree`.
  Tree B: a second worktree of the repository, detached, its `tools\`
  entries junctioned to A's.
- The packager's self-tests (`scripts\package\test-package.ps1`) passed
  at each step, 1646 checks at `08d42090`, the import's and the
  launcher's cases among them (stand-in builds, run through
  `Invoke-CutBuilds`).

### The readings

| Step | Commit | Result | Time |
|---|---|---|---|
| Serial baseline, in tree B: `build-driver.cmd all`, then `all -amd64` (each with `TEMP` under B's `out\`) | `4122d90b` | both exit 0 | x86 26:00, amd64 29:52, 55:52 in all |
| `cut-builds.ps1 -X64Tree <B>`: x86 in A and amd64 in B at once | `cceae656` | both exit 0; release, debug and qemu amd64 records imported | x86 30:05, amd64 28:19, 30:06 first start to last end; import 0:02 |
| `make-release.ps1 -CheckOnly` with the dry cut's arguments (`-TestLabel`, `-ReleasesDir` and `-UploadDir` in a scratch folder, `-SkipQualtool`) | `08d42090` | exit 0; each of release-x86, debug-x86, release-x64 and debug-x64: "the gated build record admits these binaries; HEAD is 1 commit(s) past its commit cceae656, none touching a build input" | 1:59 |
| Dry cut, x86 half: `make-release.ps1 -Arch x86` with the same arguments | `08d42090` | exit 0; three downloads written to the scratch folder, at the unbumped version `3.2.0.0`, TEST banner | 6:15 |

The x86 build ran slower beside the other than alone (30:05 against
26:00; amd64 28:19 against 29:52, on a host whose other load varied),
and the saving is still most of the shorter build. The imported
records (B's, byte for byte), SHA-256:

- release: `xhci98.sys` `431286D7...`, `xhciuas.sys` `D36132E0...`,
  `xhcicm.sys` `F17FD23F...`, `stor98.sys` `95C82987...`
- debug: `xhci98.sys` `2CB349A8...`, `xhciuas.sys` `EA01DC5B...`,
  `xhcicm.sys` `CE33551D...`, `stor98.sys` `95C82987...`
- qemu: `xhci98.sys` `6072CF71...`, `xhciuas.sys` `54868443...`,
  `xhcicm.sys` `31E1762B...`, `stor98.sys` `95C82987...`

The x86 release record: `xhci98.sys` `78DB2C2C...`, `xhciuas.sys`
`E3E3618D...`, `xhcicm.sys` `0669E3E1...`, `xhcihlpr.sys` `6120D138...`,
`stor98.sys` `C7481946...`, `stor98.mpd` `7245DAF6...`.

### Not read

- The x64 half of the dry cut, which only the key can run: the signed
  legs of `make-release.ps1` on the imported build (signing, the
  published binaries held to the build and the signature, the catalogs,
  the four-directory upload set). The owner's cut, or a signed rehearsal
  of it, reads it.
- Two package checks short of that, not run here (refused by this
  session's tool permissions, not by the scripts): `make-package.ps1
  -Arch amd64` unsigned on the imported build, every gate run in A on
  the copies; and `make-package.ps1 -SignOnly -GatedManifestPath` on an
  imported record, which checks the record against the binaries and
  stops (signing from it needs `-TestSignPfx`). The imported record is
  B's byte for byte in A's `out\`, where `-SignOnly` reads its own.

## 44.32 the connection manager off the boot path: the readings (2026-10-25)

Design record 23 section 5.14. The owner chose "the start type put back"
on 2026-10-25, on the condition that it is read first to hold on 2000 to
10: a demand-start `xhcicm` filter attaching to the bridges enumerated at
boot, a pre-boot or preserved tunnel's included, and at a hot plug; on
x64 with test-signing mode on and off. Runtime, development host A, QEMU
under TCG, one guest at a time.

### The vehicle

- The published `3.2.0.0` package, `out\3.2.0.0\xhci98-3.2.0.0.zip`
  (SHA-256 `336be62c...b35e4`), its x64 half test-signed by the project
  certificate (thumbprint `A512DF45...ABFF`), the certificate's public
  half put into the guest's Root and TrustedPublisher stores first, so
  no publisher prompt appeared. Nothing was signed.
- The put-back stood in for by hand: `sc config xhcicm start= demand`
  after the install (on 2000, which has no `sc.exe`, the same value by
  `regedit /s`).
- The emulator `vm\usb4\bin-2a893ea` (SHA-256 `6a1dfcc8...bd66`),
  i440FX, the `tb3-alpine-ridge` NHI at 00:04 with `bios-assist=off`,
  the tunnel bridge `usb4-tunnel-bridge` (`1B36:0001`, a line of
  `xhcicmf.inf`) at 00:05, its xHCI at 00:07, an internal NVMe drive at
  00:09; the ASM2464 enclosure with an NVMe drive (a FAT volume
  "ORICO") plugged by QMP, or on the command line with `preboot=on` for
  the pre-boot tunnel.

### 7 SP1 x64

An overlay of the `3.2.0.0` matrix's prepared 7 x64 image (test-signing
mode on), 2 vCPU, 2 GB.

| Step | Reading |
|---|---|
| `INSTALL /console /thunderbolt /yes` | exit 3; `setupapi.dev.log` at the bridge's `xhcicmf.inf` install: "Filter Service: Modified service 'xhcicm'"; System log 7040 "The start type of the xHCI98 Thunderbolt and USB4 Connection Manager (xhcicm) service was changed from demand start to boot start"; `Start` 0; five restore points "Device Driver Package Install: Yeo Kheng Meng ..." (one per package) |
| The enclosure plugged, boot start | "ORICO (F:)", the tunnelled drive on Stor98 |
| `sc config xhcicm start= demand`, restart with the enclosure attached | `Start` 3 after the start (not promoted again at boot); the bridge Code 0 with `UpperFilters` `xhcicm`, the NHI on `xhcicm` Code 0, `xhcicm` `DEMAND_START` running. The enclosure came back on USB (`USB\VID_174C&PID_2463`, `xhciuas`), not tunnelled: with `Start` 0 put back and the same restart, the same (below), so it is the emulator's, not the start type's |
| `sc config xhcicm start= boot`, the same restart (the control) | the enclosure on USB as well |
| Demand start again, the enclosure unplugged, restart, then the enclosure plugged | the balloon "ASMedia 246x connected (USB4 v1.0)", "ORICO (F:)", the tunnelled function on Stor98 Code 0 with its disk, the mark service's `LowerFilters` `xhcicm` on it; `Start` 3 |
| `bcdedit /set testsigning off`, restart, demand start | a plain start to the desktop (about two minutes), no repair (`SrtTrail.txt` absent), no "Test Mode"; the install whole (`C:\XHCI98`, the Add/Remove Programs entry, the services, the bridge's `UpperFilters`); the bridge, the NHI, both xHCI controllers and the Stor98 drive Code 52; System log Kernel-PnP 219 "The driver \Driver\xhcicm failed to load for the device ..." for the NHI and the bridge (and `xhci98`, `stor98` for theirs); Code Integrity 3004 for each file; `Start` 3 |
| `bcdedit /set testsigning on`, restart | every device Code 0 again, `xhcicm` `DEMAND_START` running |
| Powered off, started again with `preboot=on` and the enclosure attached (a pre-boot tunnel; the emulator's trace "insert endpoint gen 1 ... cold=1") | at demand start the bridge Code 0 with its filter, the tunnelled NVMe function on Stor98 Code 0 at boot with its disk; `Start` 3 |

**Verdict on 7 x64: option 1 holds.** A demand-start `xhcicm` is loaded
for the bridge's stack at boot, the tunnelled drive of a pre-boot tunnel
enumerates below it, a hot plug works, and with test-signing mode off
Windows starts normally with the Thunderbolt devices at Code 52 instead
of undoing the install.

### 10 x64 (22H2, 19045)

An overlay of the Windows 10 kit's prepared 10 x64 image (test-signing
mode on, SeaBIOS: no Secure Boot), 2 vCPU, 4 GB; the package's
connection manager alone, as 10 installs it.

| Step | Reading |
|---|---|
| `INSTALL /console /cmonly /yes` | exit 3 in 742 s; the boot-driver warning of `3.2.0.0`; `setupapi.dev.log` "Updated service 'xhcicm' to be boot start."; System log 7040 demand start to boot start; `Start` 0 |
| `sc config xhcicm start= demand`, restart | `Start` 3 after the start; the bridge Code 0 with `UpperFilters` `xhcicm`, the NHI on `xhcicm` Code 0, `xhcicm` `DEMAND_START` running |
| The enclosure plugged | the tunnelled function (`4&1D29C6E2&0&0828`) on 10's `stornvme` Code 0 with its disk, the mark service's `LowerFilters` `xhcicm` on it |
| `bcdedit /set testsigning off`, restart, demand start | a plain start to the desktop, no "Test Mode", no Automatic Repair (`SrtTrail.txt` absent), `Select` Current 1 Failed 0; the bridge and the NHI Code 52 (Kernel-PnP 219 "The driver \Driver\xhcicm failed to load"; Code Integrity 3004 for `xhcicm.sys`); the install whole (`C:\XHCI98`, the Add/Remove Programs entry, the bridge's `UpperFilters`, the mark); `Start` 3. The enclosure, attached across the restart, came back on USB (Windows' own UAS driver, Code 10), as on 7 |
| `bcdedit /set testsigning on`, restart | the connection manager running again |
| Powered off, started again with `preboot=on` and the enclosure attached (the trace "insert endpoint gen 1 ... cold=1") | at demand start the bridge Code 0 with its filter, the tunnelled function on `stornvme` Code 0 at boot with its disk; `xhcicm` `DEMAND_START` running |

### 2000 SP4

An overlay of `vm\win2k-smp.img` (ACPI multiprocessor HAL, 2 vCPU, 512
MB, with `xhci98` 1.0.0.3), the `3.2.0.0` matrix's 2000 kit: the
emulator's USB4 profile (`usb4-generic`, the software connection
manager, NHI `1B36:00F4`) with the same tunnel bridge and enclosure. No
kernel-mode signing on 2000, so no test-signing leg.

| Step | Reading |
|---|---|
| `INSTALL /console /thunderbolt /stor98 /yes` | exit 3 (one "Digital Signature Not Found" answered); `Services\xhcicm` `Start` 0, `Group` "Base" |
| `Start` 3 by `regedit /s`, restart | `Start` 3 after the start; `Services\xhcicm\Enum` lists the NHI and the bridge (both started on `xhcicm`); `XHCIINST /devices`: "Behind the Thunderbolt port: xHCI98 PCI Bridge, PCI 00:05.0" (the bridge filter registered with the NHI) |
| The enclosure plugged | "ASMedia 246x ... approved", "PCIe up"; the NVMe function (`4&1D29C6E2&0&0828`) below the bridge, "removable: yes", "listed in Safely Remove Hardware" (the child filter the bridge filter attaches in its `BusRelations` answer); no driver, the Found New Hardware wizard having been cancelled here |
| Powered off, started again with the enclosure attached | Found New Hardware "PCI Device" at boot: the NVMe function enumerated below the bridge at boot, removable, listed; `Start` 3 |

**Verdict on 2000: option 1 holds.**

### 8.1 x64 and 8 x64

Overlays of the Windows 8 kit's prepared 8.1 x64 and 8 x64 images
(test-signing mode on, SeaBIOS), 2 vCPU, 4 GB, the same emulator profile
and `3.2.0.0` package (`INSTALL /console /cmonly /yes`, Stor98 by its
default there).

| Step | 8.1 x64 (9600) | 8 x64 (9200) |
|---|---|---|
| The install | exit 3; "Updated service 'xhcicm' to be boot start."; `Start` 0 | exit 3; `xhcicm` `BOOT_START` after the run |
| `Start` 3 by hand, restart | every device Code 0; `xhcicm` `DEMAND_START` running; `Start` 3; no `StartOverride` key | the same |
| The enclosure plugged | the tunnelled function (`4&7EBBD&0&0828`) Code 0, switched to Stor98 by the mark service (8.1's switch), the mark | the tunnelled function (`4&2C663E53&0&0828`) on Stor98 Code 0, the mark |
| Test-signing off, restart | a plain start, no Automatic Repair (`SrtTrail.txt` absent), the install whole; the bridge and the NHI Code 52; `Start` 3; no `StartOverride` | the Start screen, no Automatic Repair, the install whole; the bridge, the NHI and the Stor98 drive Code 52; `Start` 3; no `StartOverride` |
| Test-signing on, powered off, `preboot=on` with the enclosure attached | the bridge Code 0 with its filter, the tunnelled function on Stor98 Code 0 at boot | the same |

Windows wrote no `StartOverride` key for `xhcicm` on 8 x64 and 8.1 x64
(read at each step) nor, after the demand-start restart, on 10 x86 and
the q35 x86 systems below; 10 x64 was not read for it.

**Verdict on 8.1 x64 and 8 x64: option 1 holds.**

### 10 x86 (22H2, 19045)

The Windows 10 kit's prepared 10 x86 image on an overlay, QEMU `pc`
(i440FX), 2 vCPU, 4 GB, the same emulator profile and `3.2.0.0` package
(its x86 half; 32-bit Windows enforces no kernel-mode signature with
Secure Boot off, so there is no test-signing step).

| Step | Reading |
|---|---|
| `INSTALL /console /cmonly /yes` | exit 3 in 305 s; `xhcicm` `BOOT_START` after the run |
| `Start` 3 by hand, restart | every device Code 0; `xhcicm` `DEMAND_START` running; `Start` 3; no `StartOverride` |
| The enclosure plugged | the tunnelled function (`4&1D29C6E2&0&0828`) on `stornvme` Code 0, the mark |
| Powered off, `preboot=on` with the enclosure attached | the bridge Code 0 with its filter, the tunnelled function Code 0 at boot |

**Verdict on 10 x86: option 1 holds.** Every leg of this section before
it also ran on QEMU `pc` (i440FX); the 32-bit halves of Vista, 7, 8 and
8.1, and XP, are owed on q35 (the owner's rule of 2026-10-25, about
23:00, for new legs).

### XP SP3: why the install stopped

The 2000 kit's XP overlay has no driver on its controllers, so XP raises
a Found New Hardware wizard at every start. The first install stopped at
once: "Windows is installing something ... usually a Found New Hardware
window waiting for an answer ... Nothing was changed", exit code 4 - the
installer's own stop under an open wizard (record 23 section 5.8), not a
fault of this task's. Run again after the wizards were answered, it
staged both connection-manager INFs and installed `xhci98`, then stopped
once more on a wizard a new devnode raised (exit code 4). XP was then
read on q35, below.

### The q35 legs (2026-10-26): XP, 7 x86, Vista x86, 8 x86, 8.1 x86

From here every leg runs on QEMU q35 (the owner's rule of 2026-10-25,
about 23:00, for new legs), on the q35 kit's bases
(`E:\xhci98-vm\q35-bases\<os>`, each on an overlay of this task's own;
XP's system disk on a piix3-ide controller, the others on the board's
ICH9 AHCI), 2 vCPU, the emulator snapshot of that kit, the `3.2.0.0`
package (`INSTALL /console /thunderbolt /stor98 /yes`), `xhcicm`'s
`Start` set to 3 by hand for the demand-start steps. The profile is the
emulator's ASMedia ASM4242 USB4 host (`-Usb4 asm4242`: a root port, the
ASM4242 switch with its tunnel downstream ports `1B21:2423`, a line of
`xhcicmf.inf`, its xHCI and the NHI `1B21:2425`) and the ORICO enclosure
(ASM2464); on XP also the Thunderbolt 3 Alpine Ridge profile (`-Usb4
tb3ar`, the firmware connection manager, tunnel ports `8086:15C0`). On
q35 the filter INF's emulator line `1B36:000C` also matches the board's
PCIe root ports, so they carry the filter too (none holds the system
disk here).

| System | The install | Demand start, restart | First plug of the enclosure | Restart with it attached (the firmware model's pre-OS tunnel) | Second plug |
|---|---|---|---|---|---|
| 7 SP1 x86 | exit 3; `Start` 0 | `Start` 3; `Services\xhcicm\Enum`: the root ports, the four ASM4242 downstream ports, the NHI | the enclosure's switch (`1B21:2463`) enumerated below the tunnel port, Code 31 | the tree Code 0 (both switch ports, the NVMe function), "ORICO (D:)" | the tree Code 0, D: |
| Vista SP2 x86 | exit 3; `Start` 0 | as 7 | the same, Code 31 | the tree Code 0, D: | the tree Code 0, D: |
| 8 x86 | exit 3; `Start` 0 | as 7 | the same, Code 31 | the tree Code 0, E: | the tree Code 0, E: |
| 8.1 x86 | exit 3; `Start` 0 | as 7 | the same, Code 31 | the tree Code 0, E: | the tree Code 0, E: |
| XP SP3 (ASM4242) | exit 3 (after a first run stopped by an answered-wrong logo prompt); `Start` 0 | `Start` 3; `Enum` as 7 | the tunnel approved ("PCIe up"), nothing below the port: `3.2.0.0` does not confirm the ASM4242's ports, and XP has no native PCIe hot plug | the enclosure's switch enumerated at boot below the filtered port, "pci, started" (the NVMe inside not reached, the same at boot start) | - |
| XP SP3 (Alpine Ridge) | exit 3; `Start` 0 | `Start` 3; both tunnel ports confirmed ("Behind the Thunderbolt port ... PCI 02:01.0, 02:02.0") | the enclosure approved, "PCIe up", nothing below the port - and **the same at boot start** (the control) | - | - |

**The first plug's Code 31 is not the start type's**: the same first
plug on a fresh 7 x86 overlay at boot start (`Start` 0, never set back)
gave the same Code 31 on the enclosure's switch (the control read on 7
x86 alone; on Vista, 8 and 8.1 x86 the same cause is inferred). On 7,
Vista, 8 and 8.1 x86 a restart, or the next plug, brought the whole tree
up; on XP the NVMe function was not reached at either start type. No `StartOverride`
key on any of them.

**Verdict: option 1 holds on 7, Vista, 8 and 8.1 x86, and on XP for the
boot and the pre-OS tunnel.** XP's hot plug works at neither start type
on q35 (with either profile), so the condition's hot-plug clause is not
read on XP; it is read on 2000, the same NT 5 PnP manager, on QEMU `pc`.

## 44.34 the free driver pack at usb-wdm-free 0.1.1, with `ax88179` (2026-10-25)

Download 2 re-pinned from usb-wdm-free `v0.1.0` to `v0.1.1`, which adds
`ax88179` (the owner's decision of 2026-10-25: required for the cut).
Development host A, branch `p44-free`.

**Verdict: done here; the pin is re-checked at 44.final** (the roadmap
line takes the pin last before the cut, and `v0.1.1` is a local tag
until the owner pushes it).

### usb-wdm-free 0.1.1

Made in a worktree of usb-wdm-free (branch `rel-011` from its `main`
`896c598`), by its own rules and release operation:

- `a081065`: `ax88179` at version 0.1.1 (`DriverVer=10/25/2026,0.1.1.0`,
  the image's version resource 0.1.1.0); no code change.
- `ba08cbc` "usb-wdm-free 0.1.1", then `ca2348c` and `8380d6e` (wording
  after review): `release\0.1.1.psd1`, 0.1.0's definition plus
  `net/ax88179` (DEVICE, folders `w98se-me` and `w2k`, an `EXCLUDES`
  line with usb-wdm-nonfree's `ethernet/ax88179` on every system); every
  0.1.0 package and tool taken from the 0.1.0 tree, so their 81 files keep
  their git blobs. `ax179f.sys` built at `a081065` (22,368 bytes,
  SHA-256 `3643d0df...ad636bd`), its host tests passing; the release
  operation (`release.ps1 -NonFree <usb-wdm-nonfree v1.0.0>`) PASS, 85
  files. `STAGE.TXT`: `AX179F98.INF` STAGE on 98 SE and ME, `AX179F.INF`
  COPY on 2000 (no leg). Its release notes, README, status and the
  record `docs/testing/2026-10-25-release-0.1.1.md` there.
- Tag `v0.1.1` (annotated) on `8380d6ed224724288abf2e562ddf79ca6daab57c`,
  local; `main` and every earlier tag untouched.

The device legs are usb-wdm-free's, with the owner's real AX88179
passed through to the guest (QEMU has no AX88179 model): 98 SE under
xHCI98 3.1.0.0 and under NUSB 3.3's EHCI, ME under xHCI98 3.1.0.0,
install, DHCP, pings to 4000 bytes and removal under traffic passed
(its `docs/testing/2026-10-22-ax88179-9x.md`); the settle cycle and the
final source on 98 SE (`2026-10-22-ax88179-fcs.md`). Open point, theirs:
the FCS safety net fired once in the first session after a plug-in
under NUSB's EHCI; 2000 not evaluated.

### This tree

- `scripts\package\wdmfree-pin.txt`: `Tag: v0.1.1`, its commit,
  `Package: net/ax88179 | W98SE WME`, the four `ax88179` files and the
  new `STAGE.TXT` hash; every other line as before.
  `INST_WDMFREE_SUMS` (`xhciinst\inst_core.h`):
  `F3EE1FDE3A5AB3EFD5CDBF2E1CBD983482D59745C42927358386F1B72D153D92`,
  the SHA-256 of the `USBDRV\FREE\SHA256.TXT` the cut writes for that
  pin (`b4a03365`).
- The installer's package table (`instOtherPkgs`): the free
  `net/ax88179` row current again, in the words of the pack's `PACKAGE`
  line (`063fb5f5`).
- Record 26 section 6.7.11 (`43657d18`, `77c8198c`): 3.2.0.0's table had
  `free:net/ax88179` withdrawn, and the cut refused to make a withdrawn
  row current. A withdrawn row now returns to current only when a carried
  set ships that package again (a package of its exported `STAGE` or
  `COPY` lines in that cut), its kind unchanged; withdrawn to known stays
  refused. `Test-OtherPkgsTransition` takes the cut's shipped ids
  (`063fb5f5`).
- Found by this task's test cut (`4ce16db3`): `make-release.ps1` ran the
  other sets' checks before it read the version from the INF, so a cut
  without `-Version` never looked for the previous `otherpkgs.txt`, and
  `releases\3.2.0.0\otherpkgs.txt` was written with an empty `Version:`
  line, which the reader refused. The checks now get the INF's version,
  and the reader takes the release folder's version for an empty line;
  `releases\3.2.0.0\` is left as published.
- `make-release.ps1`'s `-NoWdmFree` help and a `test_inst.c` comment
  (`33c4eef7`); the release notes (a paragraph on `ax88179` in the
  `-free` download) and `legal-provenance.md` (the free set's releases)
  in `b4a03365`.

Reviewed and revised (the design before its code, and usb-wdm-free's
release).

### Builds and checks

- `xhciinst\build.cmd`, `xhcimark\build.cmd`, then
  `scripts\build-driver.cmd all` (x86) at `b4a03365`: PASS (text checks,
  packager self-tests 1531, the XHCIINST self-test, host tests, test_inst
  0 failed). `XHCIINST.EXE` `6cccf55f...0bf6d1`. The driver sources did
  not change, so no amd64 driver build was run.
- `test-package.ps1` after `4ce16db3`: 1533 checks PASS.
- `scripts\check-text.ps1` before each commit: OK.

### The test cut

`make-release.ps1 -Arch x86 -Flavor release -SkipQualtool -TestLabel
"44.34 free set, not for release" -WdmFreeRoot <the rel-011 worktree>
-DriverPackRoot <usb-wdm-nonfree>`, `-ReleasesDir` and `-UploadDir` under
`out\` (never signed; the version still reads `3.2.0.0`). The scratch
releases folder held `history.md` and a copy of
`releases\3.2.0.0\otherpkgs.txt` under an earlier version's folder, so
that the history check ran: "download 2: usb-wdm-free v0.1.1
(8380d6ed2247), 91 files in USBDRV\FREE\"; "the package table hold,
against 3.1.99.0's otherpkgs.txt" (the revival accepted); the new
`otherpkgs.txt` names its version and has `free:net/ax88179` current.
`xhci98-3.2.0.0-free.zip` SHA-256 `96d050ae...6706506`; its
`USBDRV\FREE\SHA256.TXT` `f3ee1fde...153d92` (the installer's value).

### The 98 SE leg

98 SE on a fresh overlay of `p43-5\base\w98se-pristine.qcow2`, stock
QEMU 11.1.0, TCG, 1 vCPU, 512 MB, `qemu-xhci` with no device; the
download unpacked on the transfer drive (`D:\PKG`); readings over COM1.

| Step | Result |
|---|---|
| `XHCIINST /dir release-x86 /list /wdmfree:copy` | PASS, exit 0: the free set offered ("Free USB class drivers (usb-wdm-free, experimental)"), the installer accepting its `SHA256.TXT` |
| `... /list /wdmfree:copy /wdmfree:stage=net/ax88179` | PASS, refused as record 26 section 6.7.6 says, exit 1: "REFUSED: free:net/ax88179: on Windows 98 SE and ME the drivers are copied, not staged ... Nothing was changed." |
| `... /yes /nopause /wdmfree:copy` | PASS, exit 3 (restart): "91 of its 91 files are in C:\XHCI98\USBDRV\FREE"; `DevicePath` `C:\WINDOWS\INF;C:\XHCI98` (no free folder); the Finish text lists the folders to point the wizard at |
| The copy read back | `C:\XHCI98\USBDRV\FREE\net\ax88179\w98se-me\` holds `AX179F98.INF` and `AX179F.SYS`, `w2k\` `AX179F.INF` and `AX179F.SYS`; `AX179F98.INF` (`515d0053...9c696d`), `STAGE.TXT`, `READ.TXT` and `SHA256.TXT` byte-identical to the download; `AX179F98.INF` is where its `STAGE.TXT` line names it (`free/net/ax88179/w98se-me/AX179F98.INF`) |

Not run here: the device (no AX88179 in QEMU; the legs above are the
owner's dongle, already read); 2000 and later (the 2000 INF is copy
only). Evidence: `E:\xhci98-vm\p44\p44-free\` (`evidence\L1-*`,
`L2-*`, `I1-*`, `X1-*`, `shots\w98f-a-*.png`, `w98f\cmdline-a.txt`).

### The re-pin after `uac2`'s ME line (2026-10-25, branch `p44-free2`)

The owner asked (2026-10-25, from 44.11) for WME `STAGE` lines for `uac2`
and `usbmidi` in usb-wdm-free 0.1.1, where an ME leg has passed (that
repository's rule).

- `uac2` had no ME leg, so one was run: ME on a fresh overlay of
  usb-wdm-free's `me-hcd310` image (ME with xHCI98 3.1.0.0), the released
  `w98se-me` `uac2wdm.inf` in ME's INF folder before the first arrival,
  a simulated X4 (`uacemu.py x4full`) over `usb-redir` on the xHCI port:
  installed at the plug (KS file prompts skipped), one waveOut and one
  waveIn device, playback at 48 and 44.1 kHz 8/8 with signal at the
  simulator, capture 8/8 with the simulator's 1 kHz tone, unplug clean:
  PASS. Its line is now `STAGE | W98SE WME`.
- `usbmidi`: not added. Its ME INF's only model is `USBMIDI_BY_HAND`
  (ME's own `usbaudio.sys` serves USB MIDI; the driver binds listed
  devices only, and none is listed), so a `STAGE` line would match no
  device; it stays `COPY`.
- usb-wdm-free commits `ba097a4` and `646b51a` (reviewed and revised);
  the local tag `v0.1.1` moved to `646b51aa6dc0205714b02be5635028cc102629a1`.
  Only `STAGE.TXT` changes under the pinned files.
- Here: `wdmfree-pin.txt` names that commit, `audio/uac2` on
  `W98SE WME WXP W7 W7X64`, the new `STAGE.TXT` hash;
  `INST_WDMFREE_SUMS`
  `23F39D425C61AC798013A1813E21635A8004295F872C58D7D5CF24A1F2F9C92F`.
  The installer tools are rebuilt for the new value. The pin is still
  re-checked at 44.final.

Evidence of the ME leg: `E:\xhci98-vm\p44\p44-free\` (`evidence\ML-OUT.txt`,
`MP-OUT.txt`, `MP4-OUT.txt`, `MR-OUT.txt`, `uac-me1.jsonl`, `shots\mef-a-*.png`).

### The re-pin to usb-wdm-free v0.1.2 (2026-10-26, branch `p44-free3`)

The owner's decisions of 2026-10-25 and 2026-10-26: usb-wdm-free moves
from GPL-2.0-only to GPL-2.0-or-later, its one portion adapted from a
GPL-2.0-only source (`rtl815x`'s chip core, from Linux `r8152.c`)
rewritten, and every package whose released files carried the old
licence text re-released as `v0.1.2`; download 2 re-pinned to it.

usb-wdm-free (worktree `relicense` from `rel-011` `646b51a`, by its own
rules and release operation; its records `docs/testing/2026-10-25-rtl815x-rewrite.md`,
`2026-10-26-release-0.1.2.md`, `2026-10-26-rtl815x-0.1.2.md`):

- `e7e1b3c`: `rtl815x`'s chip core rewritten from the chips' register
  facts as tables of register steps under its own names; against the
  earlier core, under one register model, every chip id at three USB
  speeds and 24 model variants, the logged control requests, sleeps and
  results are byte-identical; with a failure injected at each request
  (5,394 runs, summarised: request count, status, the register model's
  hash) they match too, but for the core's diagnostic `Timeouts` count,
  which the new core no longer raises after an I/O failure.
- `253f8d7`, `65d09eb`, `435ec69`: the SPDX lines and prose to
  GPL-2.0-or-later, its sources rule and `NOTICE.md` rows, the
  third-party licence statements as their upstream files state them.
- `877757c`, `d91c3cb`: `DriverVer=10/26/2026,0.1.2.0` (`noaud`
  1.0.1.0), `rtl815x.sys` 0.1.2.0.
- `c9b5269` "usb-wdm-free 0.1.2": `release.ps1` PASS, 85 files. 40
  files change against `v0.1.1`: 34 INFs in their SPDX line and
  `DriverVer` only, four texts (usbflop's and CamCheck's), and
  `RTL815X.SYS` (rebuilt). Every other image keeps its `v0.1.1` bytes.
  The six x64 folders that change (`uac2` `wxp-x64` and `wv-8-x64`,
  `usbmidi` `wxp-x64` and `wv-8-x64`, `cdcnet` `wxp-x64` and `wv-7-x64`)
  keep the owner's signed 0.1.0 images, byte for byte, and only their
  INFs change: usb-wdm-free's signing policy is an embedded test
  signature on x64 images and no catalog for a package with an image of
  its own (its `docs/design/build.md` section 7), so nothing was signed
  for `v0.1.2` (owner, 2026-10-26). `STAGE.TXT` changes only in
  `rtl815x`'s note; no package's systems change.
- Tag `v0.1.2` (annotated, unsigned) on
  `c9b5269c953ab59b2cd0abf6920a10858fdf6243`, local.

`rtl815x`'s legs are all with the simulated adapter (`rtlemu.py`, QEMU
has no Realtek model) on 98 SE: RTL8153 under xHCI98 3.1.0.0 and NUSB's
EHCI, RTL8153B under xHCI98 (DHCP, ping, cable flap, removal), and the
RTL8152 under xHCI98 with the released image: once TCP/IP was bound to
the adapter (that guest had six TCP/IP bindings already, and the seventh
adapter got none until one test adapter was removed), DHCP and
`ping -n 8 -l 1472` 8/8. It stays copy only (no real adapter).

Here:

- `scripts\package\wdmfree-pin.txt`: `Tag: v0.1.2`, its commit, the 42
  changed `SHA256` lines (the 40 files, `NOTICE.md`, `STAGE.TXT`); the
  `Package` lines unchanged (the same packages on the same systems), so
  the installer's package table needs no change.
- `INST_WDMFREE_SUMS`
  `8AD01AB571EE18760D9650D2F87B4BC3D146904B103736416EE82649FC286505`,
  the SHA-256 of the `USBDRV\FREE\SHA256.TXT` the cut writes for the
  pin; the installer and the tools that include `inst_core.h` rebuilt.
- `releases\history.md` (3.3.0.0's entry), the release notes' `-free`
  paragraph, `legal-provenance.md` (the pin, usb-wdm-free's licence) and
  `make-release.ps1`'s `-NoWdmFree` help name `v0.1.2`.

Builds and checks: `xhciinst`, `xhcicln`, `xhcitr`, `xhcimark` and
`xhciperf` rebuilt for the new value; `scripts\build-driver.cmd all`
(x86), then `all -amd64`, from PowerShell with `TEMP` under `out\`:
BUILD + GATES PASSED both (host tests and `test_inst` 0 failed);
`scripts\check-text.ps1`: OK.

The test cut: `make-release.ps1 -Arch x86 -Flavor release -SkipQualtool
-TestLabel "44.34 free set v0.1.2, not for release" -WdmFreeRoot <the
relicense worktree> -DriverPackRoot <usb-wdm-nonfree>`, `-ReleasesDir`
and `-UploadDir` under `out\` (a scratch `history.md` and 3.2.0.0's
`otherpkgs.txt` under `3.1.99.0`, as before): "download 2: usb-wdm-free
v0.1.2 (c9b5269c953a), 91 files in USBDRV\FREE\"; the package table
holds against `3.1.99.0`'s. `xhci98-3.2.0.0-free.zip` SHA-256
`7b7ddbfe...d67a6c69` (never published; the version still reads
`3.2.0.0`). Its first run stopped, as it should, on the old
`INST_WDMFREE_SUMS`, and the next on an `XHCICLN.EXE` older than
`inst_core.h`.

The 98 SE leg: the q35 base (`E:\xhci98-vm\q35-bases\w98se`, launched
with `launch-q35.ps1 -Os w98se`, an overlay, 1 vCPU, `qemu-xhci` with no
device), the test cut's download on the transfer drive (`D:\PKG`),
readings over COM1:

| Step | Result |
|---|---|
| `XHCIINST /dir release-x86 /list /wdmfree:copy /wdmfree:stage=audio/uac2` | PASS, exit 0: the free set offered and accepted, `audio/uac2` to be staged |
| `... /yes /nopause /wdmfree:copy /wdmfree:stage=audio/uac2` | PASS, exit 3 (restart): "91 of its 91 files are in C:\XHCI98\USBDRV\FREE", "free:audio/uac2 staged" |
| The copy read back | `RTL815X.SYS`, `STAGE.TXT`, `SHA256.TXT` and `NOTICE.md` byte-identical to the download (`fc /b`); `net\rtl815x\w98se-me\` holds `RTL81598.INF` and `RTL815X.SYS` (30,464 bytes) |
| After a clean shutdown and a cold start | the copy in place; DevicePath holds `C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me` (98 SE's staging) |
| `XHCIINST /uninstall /yes /nopause` | PASS, exit 3: "C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me taken out of DevicePath", "Free USB class drivers ...'s copies in C:\XHCI98\USBDRV\FREE were removed", "C:\XHCI98 removed"; `C:\XHCI98` gone |

The guest was shut down with `rundll32 shell32.dll,SHExitWindowsEx 1`
after each session. Evidence: `E:\xhci98-vm\p44\p44-free3\evidence\`
(`I1-serial-a.log`, `U1-serial-b.log`).

## 44.44 the free class drivers matched by their USB class (2026-10-26)

The owner's decision of 2026-10-26: usb-wdm-free's class drivers match
their USB class's compatible ids on the systems where Windows has no
class driver of its own, the device lists kept elsewhere, so Windows' own
class driver stays the default there; the vendor-chip drivers and `noaud`
stay device-listed. Branch `p44-free4` from `3.3.0.0` `cfe47f77`.

**Verdict: done here; the pin is re-checked at 44.final.**

### usb-wdm-free 0.1.3

Worktree `relicense` of usb-wdm-free, after `v0.1.2`, by its own rules
and release operation (its record `docs/testing/2026-10-26-release-0.1.3.md`):

- `90fd078`, `619a3f8`: its `docs/design/build.md` section 7.1 tabulates
  each class driver and system, with the source of each fact. `uac2`
  (USB Audio Class 2.0, every target: no UAC 2.0 driver before Windows 10
  1703), `usbmidi` (98 SE and 2000, where `usbaudio.sys` has no MIDI),
  `cdcnet` (CDC ECM and NCM everywhere; RNDIS on 98 SE, ME and 2000),
  `usbflop` (UFI: no `usbstor.inf` matches `08/04`) and `uvc98` (98 SE,
  ME, 2000) already matched their classes there. Changed: `usbcom`
  matches `USB\Class_02&SubClass_02` (CDC ACM) on 98 SE and ME
  (`USBCOM.INF`) and on 2000 (`USBCDC.INF`, Windows' own `usbser.sys`), as
  it did on XP and later; `cdcnet`'s `CDCRNDIS.INF` no longer claims
  `EF/04/01` on 7, which Windows 7's own `rndiscmp.inf` matches.
- `062989b` "usb-wdm-free 0.1.3": `release.ps1` PASS, 85 files; seven
  INFs change against `v0.1.2` (0.1.3.0), no image, so nothing was signed
  (the six x64 folders keep the signed 0.1.0 images under the no-catalog
  policy). Tag `v0.1.3` (annotated, unsigned) on
  `062989b3a88df71d566ed31c53bd18f911f97129`, local.

Legs (owner, 2026-10-26: 98 SE legs only where Windows has a broader
match of its own to beat; elsewhere "ranking read from the INFs, no
leg"): on 98 SE under xHCI98 3.1.0.0, `uac2`'s and `usbmidi`'s INFs in
Windows' INF folder before any arrival, a simulated USB Audio Class 2.0
device (`uacemu.py x4full` at `1209:0002`) and a lone MIDIStreaming
interface (`midiemu.py lone` at `FFFF:0042`), ids no INF lists. With both
attached at boot, the audio function got "UAC2 USB audio (experiment)"
(`MatchingDeviceId` `USB\Class_01&SubClass_00&Prot_20`) and the MIDI
interface "USB MIDI device" (`USB\Class_01&SubClass_03&Prot_00`), each
above `wdma_usb.inf`'s `USB\CLASS_01`: PASS. Plugged after the desktop
instead, the guest stopped responding after the device's HID function
installed: the 98 SE hang at the first USB audio arrival after a boot,
which usb-wdm-free's README and this release's notes already describe.
`usbcom`, `cdcnet`, `usbflop`, `uvc98`: ranking read from the INFs, no
leg.

### This tree

- `scripts\package\wdmfree-pin.txt`: `Tag: v0.1.3`, its commit, eight
  changed `SHA256` lines (the seven INFs and `STAGE.TXT`); the `Package`
  lines unchanged (the same packages on the same systems), so the
  installer's package table needs no change.
- `INST_WDMFREE_SUMS` `8B3B89214582EB4DFD04770BB9FA2D9FE4E65A1C5A1E8EE7B64FB21D213679D8`; the installer and the tools that include
  `inst_core.h` rebuilt.
- The user text names the class, not one device: README's 98 SE/ME note
  and the release notes' known limitation now say a USB Audio Class 2.0
  device needs `uac2`, which matches the class (the Sound Blaster X4 kept
  as the device the fatal exception was read with); the history line
  likewise. No other driver's user text named one device. The release
  notes' `-free` paragraph, `releases\history.md`, `legal-provenance.md`,
  `make-release.ps1`'s help, `docs\README.md` and the roadmap name `v0.1.3`.

Builds and checks: the installer and tools, `scripts\build-driver.cmd
all` (x86), then `all -amd64`, from PowerShell with `TEMP` under `out\`:
BUILD + GATES PASSED both; `scripts\check-text.ps1`: OK.

The test cut (as 44.34's): "download 2: usb-wdm-free v0.1.3
(062989b3a88d), 91 files in USBDRV\FREE\"; `xhci98-3.2.0.0-free.zip`
SHA-256 `2a163068...1a942d66` (never published).

The 98 SE leg, as 44.34's, on the q35 base (`launch-q35.ps1 -Os w98se`,
1 vCPU, readings over COM1): `/list` exit 0; `/yes /nopause /wdmfree:copy
/wdmfree:stage=audio/uac2` exit 3, "91 of its 91 files", "free:audio/uac2
staged", the copied `serial\usbcom\w98se-me\USBCOM.INF`, `STAGE.TXT`,
`SHA256.TXT` and `NOTICE.md` byte-identical to the download (`fc /b`);
after a clean shutdown and a cold start, `/uninstall /yes /nopause` exit 3:
uac2's folder taken out of `DevicePath`, the free set's copies removed,
`C:\XHCI98` removed. Clean shutdowns (`SHExitWindowsEx 1`). Evidence:
`E:\xhci98-vm\p44\p44-free4\evidence\`.

## 44.38 - the project's own source relicensed GPL-2.0-or-later, the portions adapted from Linux rewritten (2026-10-26)

Roadmap 44.38 (owner, 2026-10-25): every portion adapted from a source
licensed GPL-2.0-only rewritten, then the project's own source relabelled
GPL-2.0-or-later. The audit before it (worker p44-lic-audit, 2026-10-25,
read-only) found two files with such portions, both from Linux
`drivers/thunderbolt`: `src/cm/tbt_u4cm.c` (27 functions translated and
10 with cited Linux portions, about 1,400 to 1,600 lines) and
`src/cm/tbt_ring.c` (the acknowledgements, about 50 lines); design record
20's xHCI quirks copied nothing. The rewrite was done without reading the
Linux text, from the USB4 Specification 2.0, the Connection Manager Guide
2.0, the USB4 DROM Specification 1.2, `usb4-emu`'s `ICM-PROTOCOL.md`,
`u4cm.c`, `u4hi.c` and `usb4-router-tb3.c`, and the captures; a separate
reviewer compared each step with the Linux mirror.

The owner's statements recorded with it (2026-10-25): 44.38 is in
`3.3.0.0`; the owner holds `usb4-emu`'s copyright, and the code this
project takes from `usb4-emu` is offered to it under GPL-2.0-or-later,
`usb4-emu` itself staying GPL-2.0-only (`legal-provenance.md` section 2a).

### The request traces

Before the rewrite, a request trace was added to the host suites' fake
fabric (`test/u4_fakefab.c`, `U4_TRACE`), the modelled host interface
(`test/tbt_fake.h`, `TBT_TRACE`) and the `usb4-emu` cross-check
(`scripts/usb4-crosscheck/u4cm-crosscheck.c`, `XC_REQ_TRACE`): every
frame the code under test transmits (route, space, adapter, offset and
count, the values written, every acknowledgement), every host-interface
register write and every log line, in order, with the virtual clock and
each vector's name. `scripts/usb4-trace/u4-trace.ps1` captures
`test_u4cm`, `test_tbt_ring`, `test_tbt_icm` and, with `-CrossCheck`, the
cross-check, and compares two captures vector by vector; a trace counts
only when it ends with the suite's `== end`. The captures stay under the
ignored `out\` and outside the tree: they pin the exact operations one
build performs in the exercised vectors, which most later behaviour
changes in `src\cm` would alter, so they serve this rewrite's
comparison, not a standing gate. `test_u4cm` gained
`test_bw_units`, the USB3 bandwidth conversions checked against Spec
Table 8-14 for every Mb/s up to 40,000 and every 12-bit value at Scale 0
to 16.

The golden capture: the harness at `99d73b1b` (with `test_tbt_icm`'s
odd-frames vector fixed at `c46b1387`) over `src\cm` as it was at
`3f6d4fd0` (plus the two host-test wrappers), in a separate worktree;
`usb4-emu` `asm4242-model` `07cf6ad`, QEMU fork `e05bf66`
(the emulator's asm4242 preset for the ASM4242 cases). Deterministic: two
captures of the same tree compared identical.

| Trace | Vectors | Lines | SHA-256 (golden) |
|---|---|---|---|
| `test_u4cm` | 50 | 444,908 | `2F2F4E09...9A03130D09` |
| `test_tbt_ring` | 24 | 5,155 | `83EA1929...9F8A66EB1E` |
| `test_tbt_icm` | 30 | 123,875 | `553469AF...FD76B5D1EC428` |
| cross-check | 19 | 70,084 | `672B2069...A1626892734` |

### The steps

Each step: the host suites, the four traces against the golden capture,
a review that compared the new text with the Linux mirror ("does it still
follow Linux's expression?"), then the commit.

| Step | What | Commits | Traces |
|---|---|---|---|
| 1 | `tbt_ring.c`'s plain, enhanced and hot plug acknowledgements (Spec 6.4.2.7 to 6.4.2.10, 6.6, 6.8) | `a20ddbf4` | identical |
| 2 | the USB3 bandwidth group (Spec Table 8-14; Guide 6.1.2.3, 6.2.2.1, 6.2.2.2, Table 6-2) | `cf0cc8fc`, `c46b1387` | identical |
| 3 | path teardown, credits, the cleaning's sweep, the Buffer Allocation Request (Guide 5.1.4, 5.2.1, 5.2.2; Spec Tables 5-16, 8-56 to 8-58) | `722c74a9` | identical |
| 4 | the Thunderbolt 3 vendor capabilities and the EEPROM DROM read (Spec 13.4.5, 13.6.1.2, 13.6.1.5; `ICM-PROTOCOL.md` 5.1, 5.4) | `e51c0dc6`, `5ffeae00` | identical |
| 5 | the TMU (Guide 7.4.3.1, 7.4.3.4.1, 7.4.3.4.2, Tables 7-2, 7-3) | `8f34ee11`, `1fb06dbc` | identical |
| 6 | lane bonding, the configured mark, the unconfigure (Guide 3.4.1, 7.1, 7.5; Spec 8.2.2.3, 8.2.2.4, 13.6.1.5) | `2f3c30ae`, `6c7f9487` | identical |
| 7 | the PCIe tunnel's order and the discovery of a tunnel (Guide 5.3.1, 5.5.1, Table 5-2) | `bc3e4918` | identical |
| 8 | the cited portions of the other functions (the capability walk, the DROM's entry start, the timings) | `813e958f`, `c9d57267` | identical |

What keeps a [linux] tag, each stated as a value or an order through
design record 22: VSC_CS_1 bit 2 with the Alpine Ridge exception, the
UUID at DW 3 and LC_SX_CTRL bit 30 (chapter 13 leaves them
TBT3-Compatible); the default credits (32 bonded, 16 single, 2 for a
control path that does not report them, the Thunderbolt 3 router's
protocol adapter written as a lane's); the device-first order of the
TMU's EnableUniDirectionalMode and Disable Time Sync writes (the Guide
names no order for the first two and the host router first for the
enable's Disable Time Sync); the 1.5 s Ack bound, the 900 Mb/s floor of a
release, the router operation's and lane 1's bounds; the quirk table; an
unknown DROM Version read as the TBT3 layout; the Thunderbolt 3 upstream
adapter written at enumeration.

Read against the Linux mirror after the last step, neither `tbt_u4cm.c`
nor `tbt_ring.c` was found to hold a function or block that still follows
a Linux function's structure beyond the protocol steps the specifications
prescribe.

### The overlap audit, again

Method as the audit's (its section 0), run by the reviewer, since the
rewriter does not read the Linux text: every tracked `.c`, `.h`, `.asm`
and `.inc` outside `src/stor98` at `ce9b3090` (this task's tree before
the rebase onto `3.3.0.0`, `c9d57267` after it; 341 files) against the 55
Linux mirror files (the 13 xHCI files, `pci_ids.h`, `message.c` and the
40 `drivers/thunderbolt` files).

- **Verbatim lines** (whitespace-normalised, 30 characters or more, not
  comments): three lines, eleven places, all idioms -
  `for (i = 0; i < length; i++) {` (`cm_thr.c` three times, `tbt_ring.c`,
  `hcd_pdo.c`, `hcd_svc.c`, `uas_pdo.c`, `xhci_desc.c`),
  `for (i = 0; i < dwords; i++) {` (`xhci_ctx.c`) and
  `memset(data, 0, sizeof(data));` (`inst_eng.c` twice); none matches an
  xHCI file, `pci_ids.h` or `message.c`.
- **Token structure** (30-token windows, identifiers and numbers
  normalised), coverage against `drivers/thunderbolt`, the audit's scope:

| File | The audit (`cf765e70`) | After the rewrite (`ce9b3090`) |
|---|---|---|
| `src/cm/tbt_u4cm.c` | 2.6% | 2.51% |
| `src/cm/tbt_ring.c` | 0.90% | 0.90% |
| `src/cm/tbt_icm.c` | 4.2% | 4.15% |
| `src/cm/cm_reenum.c` (the control: adapts nothing) | 2.8% | 2.60% |
| `src/hcd_quirk.c` | 9.2% | 9.16% |
| `src/xhci_quirk.c` | 0% | 0% |

Against all 55 files together the figures are `tbt_u4cm.c` 3.84%,
`tbt_ring.c` 2.89%, `tbt_icm.c` 4.52%, `cm_reenum.c` 4.17%,
`hcd_quirk.c` 9.19%, `xhci_quirk.c` 0%. As the audit said, these checks
cannot tell a translation from independent code (the control scores as
high); the rewrite's evidence is the record of what each portion was
written from and the review's reading against the mirror.

### Notices and the relabel

`6254b7f4` and `6e133a5e`, with `d3291b1c` (reviewed and revised):

- `tbt_u4cm.c` carries `SPDX-License-Identifier: GPL-2.0-or-later`; the
  Linux copyright lines and source list are gone; `u4cm.c`'s notice is
  quoted with the owner's statement. `tbt_u4cm.h`'s sources paragraph says
  the same. `tbt_ring.c`'s header now names the GPL-2.0-only `u4hi.c` text
  task 40-A.1 took at `3c74189`, with the statement, beside the MIT notice
  of `a68bc74` it keeps (the audit's record gap). `u4_fakefab.c`,
  `u4cm-crosscheck.c` and `run-crosscheck.sh` quote their `usb4-emu`
  notice with the statement; `u4cm-glue.c` and `.h` carry
  GPL-2.0-or-later.
- `legal-provenance.md`: section 2's licence row dated; section 2a the
  owner's statement, the "Rewritten from" table in place of the Linux
  row, each file's notices from 2026-10-26, the `tbt_ring.c` row's
  notices; section 3 the project's licence dated, "under version 2"
  dropped from the xHCI quirk exception with the fact that nothing was
  copied under it, and the Thunderbolt rule from 2026-10-26.
- `LICENSE` (the statement grants version 2 or any later version, SPDX
  GPL-2.0-or-later, the scope note's Thunderbolt paragraph; the licence
  text itself unchanged), `README.md`, `AGENTS.md`, design records 21 and
  22 (dated notes), the eight version resources' `LegalCopyright` ("GNU
  GPL v2 or later."), XHCITR's and XHCIPERF's About text, XHCITR's
  readme, and `make-release.ps1`'s generated readmes and notices,
  rewrapped to 78 columns (`scripts\check-text.ps1`: OK). Unchanged: the
  MIT notices of `tbt_regs.h` and `tbt_ring.c`/`.h`, Stor98, the releases'
  `LICENSE` copies and the dated run sheets; `package-common.ps1`'s "their
  licence, the GNU GPL v2" describes `usb-wdm-free`'s drivers and stays.

### Builds

At `ce9b3090`, this task's tree before the rebase onto `3.3.0.0`
`71092b63` (the rebase changed no file of this task, but brought 44.43's
changes to other `src\cm` files, so no gated build of the rebased tree
was run here), in a separate worktree, `TEMP` under its `out\`:

- x86 `scripts\build-driver.cmd all`: BUILD + GATES PASSED (debug release
  qemu), dirty 0; `test_u4cm` 1335 checks, `test_tbt_ring` 522, each 0
  failures. `xhcicm.sys` qemu `3FD94416...AA21EC`.
- amd64 `all -amd64`: BUILD + GATES PASSED (debug release qemu), dirty 0;
  `xhcicm.sys` release `4DBE6FD0...A5A511`, qemu `3E4CE4CA...6FC440`.
- After the rebase (`d3291b1c`), host-side only: `test\run-host-tests.cmd`
  PASSED, and the four traces identical to the golden capture again.

### Legs (q35, the `usb4-emu` fork)

Machine q35 (SeaBIOS), the bases of `E:\xhci98-vm\q35-bases` through
`launch-q35.ps1`, emulator `asm4242-20261025-2230` (`usb4-emu` QEMU
11.1.2 `g02dda40`, the tree committed as `e05bf66`). Vehicle: the
`3.2.0.0` download installed (`XHCIINST /thunderbolt /stor98`, exit 3),
then the gated `qemu` `xhcicm.sys` of `ce9b3090` (`3FD94416...`) copied
over the installed one, so the connection manager's log reaches the
debugcon file. On 7 x86 a reinstall of the NHI from the driver store
puts the store's copy back at each start's PnP remove and restart (seen
with the base's own build too), so there the copy also went into the
store's two `xhcicm.sys` (`xhcicm.inf` and `xhcicmf.inf`), in the test
guest only. One vCPU (98 SE) and two (7 x86), one guest at a time, QMP
49190 and 49191. Evidence: `E:\xhci98-vm\p44\p44-relic\` (`w98\`,
`w7\` debugcon logs, `shots\`, `pids.txt`).

| Leg | System | Host / device | Reading |
|---|---|---|---|
| pre-OS | 98 SE | ASM4242 (the emulator's asm4242 preset), ORICO at power-on, the firmware model's tunnel | `u4.drom.unused=00000010` (adapter 4, the TBT3-layout DROM), `u4.preserved=00010808`, port 1 started UP; the NVMe behind the tunnel found by 98 SE (Stor98's wizard, answered `C:\XHCI98`) |
| hot plug | 98 SE | ASM4242, ORICO plugged after the start, unplugged, re-plugged | `u4.router.up=174C2463`, `u4.pcie.up=00010809`, `u4.port.state=00000105`; unplug `u4.linkdown=01000101`; re-plug up again |
| Thunderbolt 3 behind USB4 | 98 SE | ASM4242, the TB3 dock (`dialect=tb3,dock=on`) plugged | `u4.drom.unused=00000620`: adapters 5, 9 and 10 unused, read through the EEPROM bit-bang as `ICM-PROTOCOL.md` 5.3 records the dock's DROM; `u4.router.up=808615D3`, PCIe tunnel up |
| USB3, TMU, bonding | 98 SE | ASM4242, a generic USB4 device plugged, unplugged, re-plugged | `u4.pcie.up=00010809`, `u4.usb3.up=00010A0B` both times |
| A/B | 7 x86 | Alder Lake (`intel-adl`), ORICO at power-on, no pre-OS tunnel | the base's build (`3f6d4fd0`, `059B88F1...`) and this task's: the same `u4.*` lines (`u4.host.tmu=0`, the DROM, router up, PCIe up, the PnP restart, then `u4.preserved=00010809`), the only difference `u4.port-checks` (polls by the clock); `ORICO (D:)` mounted |
| hot plug | 7 x86 | Alder Lake: ORICO unplugged, a generic USB4 device plugged, unplugged, the TB3 dock plugged | `u4.usb3.up=00010A0B` with the 463E quirk; Windows' balloon "QEMU USB4 NVMe Enclosure connected (USB4 v1.0)"; the dock `u4.drom.unused=00000620`, "QEMU USB4 Dock connected (Thunderbolt 3)" (its DROM names through the EEPROM) |
| pre-OS | 7 x86 | ASM4242, ORICO at power-on | `u4.preserved=00010808` in both instances; `ORICO (D:)` mounted |

98 SE was powered off through QMP after each leg: its Shut Down hang with
`xhcicm` on q35 is 44.43's, whose fix the build these legs ran predates;
7 x86 shut down by itself.

### What is left

Nothing of the task's own. For the owner: the questions in the report
(the Disable Time Sync order, whether the xHCI quirk exception and the
adaptation of GPL-2.0-only code stay closed, the release notes' licence
line for `3.3.0.0`). The hardware bench's readings of the connection manager
(44-E.6) read the rewritten code as they read any other.

## 44.39 to 44.42 - real USB4 hardware: the plan (2026-10-25)

Development host A, branch `p44-u4hw`. U4HW.1, opened by the owner on
2026-10-25 (decisions table, "U4HW.1" and "44.39"). No guest leg: the
hardware readings are the hardware bench's, 44-E.6.

**44.39, the plan**: design record 22 section 18, from the ASM4242 data
that `usb4-emu` holds (`7ec3865`, `3090d19`, `5d7345a`, `07e2782`,
`430f54a`, `9f333f3`, `3fa47e8`): the firmware's pre-OS tunnel and
Linux's own, under UEFI and the CSM, the ORICO enclosure at power-on
and hot-plugged.
Reviewed and revised; the provenance row in `legal-provenance.md`
section 4. The owner's answers to section 18.7 are in; the task stays
open for the bench's reading of its plan.

**44.40, the ASM4242's tunnel-port family** (`a9c3d7c7`, `514a056e`;
reviewed and revised): `cm_ports.c`'s `asmedia-asm4242`, NHI `1B21:2425`
with switch port `1B21:2423`; `test_cm_ports` and `test_tbt_topo`'s
`test_asm4242_host` (the ASM4242's topology: `UNLISTED` without the family, the
set {14:00.0, 14:01.0} by the switch source with it).

**44.41, `XhciCmAsmediaPcie7Credits`** (`3268aa12`, `680bece2`; reviewed
and revised): the opt-in experiment of record 22 section 18.8;
`test_cm`, `test_u4cm`'s `test_asm7` (off, on, each router id wrong
alone, the right pair with a USB3 tunnel, a preserved tunnel) and
`test_asm4242_preos` (the firmware's four entries as record 22 section
18.3 gives them,
preserved and unwritten).

**44.42, the cross-check on the emulator's asm4242 model** (`2fc1f033`, `cf52e8bb`;
reviewed and revised): `scripts/usb4-crosscheck` against `usb4-emu`
`asm4242-model` `91a12c8`, QEMU fork `02dda40`, the emulator's asm4242 preset:
254852 checks, 0 failures (`out\u4-crosscheck.log`): every host
adapter's type (lanes 1-4, the host interface 5, PCIe Down 6-7, USB3
Down 8-9, DP IN 10-11), PCIe and USB3 Down per port, the ASM2464 behind
it cold, hot plugged and pre-OS, and once with the credits experiment
(its gate open for each of the three tunnels built, thirteen lines
each). Run as `runs/run-40.md` says, with `USB4EMU` naming the
`asm4242-model` worktree.

**Host suites**: `test\run-host-tests.cmd`, PASSED (`test_tbt_topo` 567
checks, `test_u4cm` 1301, each 0 failures, in the x86 build's run).

**Gated builds** (`TEMP` under the worktree's `out\`): x86 `all`, BUILD
+ GATES PASSED, `xhcicm.sys` (qemu) `E017F20A...D5770235`, record at
`680bece2`; amd64 `all -amd64`, BUILD + GATES PASSED, `xhcicm.sys`
(qemu) `469EB89F...A277EBB9`, record at `cf52e8bb`. Both records say
"with uncommitted changes during the build": test, document and
cross-check script edits landed while they ran; no file a driver is
built from changed after `680bece2`. A cut builds again from a clean
tree.

## 44.43 - The 98 SE and ME Shutdown Hang on q35 (2026-10-26)

A task of Phase 44 opened by the coordinator on the owner's behalf
(2026-10-26; the number proposed by this worker): the q35 base images
(`build-and-test.md`, "The q35 base images") found 98 SE hanging at
"Windows is shutting down" once `3.2.0.0`'s connection manager is
installed, on each of its three connection-manager profiles, and their
notes give ME the same, the profile not stated; the same images shut
down in seconds without it. This section reproduced the hang on 98 SE and on
ME on the Alpine Ridge profile, and read the fix there only.

### The vehicle

Development host A, the q35 base images' emulator and launcher
(`E:\xhci98-vm\q35-bases`: QEMU's q35 board, SeaBIOS, 98 SE installed
with ACPI, ME on its IDE disk), one guest at a time, the Alpine Ridge
profile (`-Usb4 tb3ar`, the enclosure attached at power-on), overlays of
the bases in this worker's folder, QEMU's gdbstub on the worker's port.
The package: `3.2.0.0` as published (`shared\xfer320`, the console
install with `/thunderbolt /stor98`); this branch's builds of
`xhcicm.sys` (the `qemu` flavour) copied over the installed one and
booted cold.

### Readings

| System, build | Shutdown |
|---|---|
| 98 SE, before any install (the profile's bridges installed) | powered off in about 4 s |
| 98 SE, `3.2.0.0` installed (the install's own session) | hung at "Windows is shutting down" for over 3 minutes |
| 98 SE, `xhcicm.sys` of `3.3.0.0`'s tip (`bb25142b`) | hung, as above |
| 98 SE, `xhcicm.sys` with the first fix (`c5028c94`'s change) | powered off in about 3 s, two cold starts out of two |
| ME, `3.2.0.0` installed (the install's own session) | hung (black screen) for over 2.5 minutes |
| ME, `xhcicm.sys` of `0c6fe197` | powered off in about 3 s |

**The legs** (the gated build of `17bb726b`, `scripts\build-driver.cmd all`
passed; its `qemu` `xhcicm.sys` SHA-256 prefix `5fb78819` copied over the
installed one, its `release` `xhcicm.sys` prefix `fe3ba839` put in a copy
of the `3.2.0.0` package for the install leg):

| Leg | Reading |
|---|---|
| 98 SE, a cold start with the connection manager started, Start > Shut Down, twice | powered off after 2 s, both |
| ME, the same, twice | powered off after 3 s and 2 s |
| 98 SE, a fresh overlay of the base, the package installed with the fixed `release` `xhcicm.sys` (exit 3, "xhcicm ... started"; the second xHCI's disk prompt answered `C:\XHCI98`), then Shut Down in that same session (43.65's case) | powered off after 3 s |

**Where it waits** (runtime: the `qemu` build of the tip in the hung
guest, `info registers` sampled and a physical memory image taken;
`xhcicm.sys`'s map from a link of the same objects with `-map`,
`out\map3\xhcicm.map` of the worktree; the stack scan
`E:\xhci98-scratch\p44-9x\scan-q35-d.txt`): the CPU in the VMM's idle
loop at every sample (`EIP` `C000A880`-`C000A884`, `C00036D0`-
`C00036DF`); the connection-manager thread's stack page (physical
`0x1D18B000`) holding the return addresses `cmThread+0x105` ->
`CmPowerDirectWork+0x31` -> `CmNhiQuiesce+0x89` -> `CmNhiSetBusMaster+0x12`
-> `CmCfgRead32+0x18` -> `cmConfig+0xA2` -> `cmConfigOnce+0xD1` (at
`0x1D18BD98`). From the source (`cm_thr.c`), `cmConfigOnce+0xD1` is its
`KeWaitForSingleObject` with no timeout on the completion of the
`IRP_MN_READ_CONFIG` of the Command register it sends to the function's
PDO before the Bus Master bit is cleared; that the thread is blocked
there, and that the system IRP this direct transition completes is what
the shutdown waits for, is inferred from that and from the fix's effect. The
same path completed on the i440FX images of the earlier legs (`cm.power.direct`
and `cm.shutdown` logged and the guest powered off: 43.65's ME legs, and
the 98 SE and ME shutdowns of 44.30 and 44.29's kits).

**The fix** (design record 21 section 5.6, "Shutdown"; `cm_pwrr.c`
`CmPowerShutdownConfig`, host suite `test_cm_pwrr`): on 98 SE and ME,
once a shutdown's system IRP has gone down, no IRP of this driver's goes
to the bus - the quiescence leaves out the Bus Master clear and the
force-power release, a stop after it the same, a start queued beside it
goes inert before its first read, and the configuration route
(`cmConfigOnce`) and the query device's identity read (`cmQId`) refuse
every request from work already running. NT and sleeps are unchanged.
Reviewed and revised.

**The NVMe 98 SE does not list** on the Alpine and Titan Ridge profiles
(the q35 bases' other reading) is a separate failure, not investigated
here: it shows at run time, after the tunnel is built, where this one
is in the shutdown's power path; whether the two share a cause is not
read.

Images and logs: `E:\xhci98-scratch\p44-9x\dump-q35-b-shut.bin` (3.2.0.0)
and `dump-q35-d-shut.bin` (the tip's `qemu` build, with
`scan-q35-d.txt`); the guests' logs and screenshots under
`E:\xhci98-vm\p44\p44-9x\q35\` and `shots\q-*`, `shots\qm-*`.

## 44.46 - `XhciCmBridgeNumbering` and `XhciCmNoIrqStart` written by the 98 SE and ME install (2026-10-26)

The owner's decision of 2026-10-26 (decisions table "44.46"): the two
values are written by the x86 `xhcicm.inf`'s 98 SE and ME section at their
default 1, only where missing, like every other value;
`XhciCmTunnelBridge` stays unwritten, its absence meaning no override.
The change: `80c8e516` (the INF, the INF gate's 9x-only value list and
its vectors, the installer's table and `test_inst`, the documents) and
`7ee3a12e` (record 28 section 7 and issue 17), reviewed and revised.

**Verdict: done.**

### Builds

The host tests passed at `80c8e516`; `scripts\build-driver.cmd all`
passed for x86 at `80c8e516` and for amd64 at `7ee3a12e` (the
difference is documents only). `xhciinst` and `xhcimark` built again at
`7ee3a12e` (both include `inst_core.c`, which `80c8e516` changed). The
test cut `p44storK` of `7ee3a12e`: `make-release.ps1 -Arch x86
-NoNonFree -NoWdmFree -NoDriverPack -SkipQualtool`, the download's
SHA-256 prefix `3a5fca9f`; in it `xhcicm.inf` `c396482c`, `xhcicm.sys`
`02d3dee0`, `XHCIINST.EXE` `074a9f66`.

### Legs (q35)

Development host A, the q35 base image of 98 SE (an overlay), the
`usb4-emu` emulator's ASM4242 host router with the ASM2464 enclosure
attached at power-on, `-smp 1 -m 512`; the package on a second disk,
installed with `XHCIINST /dir release-x86 /yes /nopause /thunderbolt
/stor98`; `Class\System` exported with `regedit /e` after each run.

| Leg | Reading |
|---|---|
| A: a fresh install | exit 3, "xHCI98 USB4 Router: xhcicm ... started"; the router's key `Class\System\0052` (`PCI\VEN_1B21&DEV_2425`, "xHCI98 USB4 Router", `InfSection` `Cm.Dev`) holds `XhciCmBridgeNumbering` and `XhciCmNoIrqStart` `dword:00000001` beside the other values, and no `XhciCmTunnelBridge` |
| B: `XhciCmBridgeNumbering` set to 0 in that key (`REGEDIT4` file, `regedit /s`) after a cold start, then the same install with `/force` (the package's `DriverVer` the same) | before the run 0 read back; the run reinstalled the router ("installed", exit 3); after it `XhciCmBridgeNumbering` `dword:00000000` kept, `XhciCmNoIrqStart` `dword:00000001`, no `XhciCmTunnelBridge` |

Seen on the way, not this task's: each run raised "Insert Disk" once,
in a USB controller's install, for `xhci98.sys` ("Copy files from"
`C:\WIN98`), answered `C:\XHCI98` (44.43's legs and 43's 98 SE pristine
leg met the same prompt); and a first run given an empty argument `""`
printed the usage, with nothing done.

Logs and screenshots: `E:\xhci98-vm\p44\p44-stor\n46\` (`serial-a.log`,
`serial-b.log`, `a*.png`, `b*.png`).

### Two readings settled

- **`INSTALL /set` takes `XhciCmBridgeNumbering`** on 98 SE and ME (0
  or 1) and refuses it on NT, as record 23 says; `tuning.md`, which said
  otherwise, was corrected in `80c8e516`.
- **The 2000 q35 "`/set XhciCmSafeEject=0` not written, exit 4" was not
  a defect.** The run's log (44.3's 2000 kit on cut G) reads "wrote
  HKLM\System\CurrentControlSet\Services\USB\XhciCmSafeEject = 0" and the
  mark service removed; `/set` writes that machine-wide key and never the
  device's class key, which was the key read. The exit code 4 came from
  the run's other steps on that test cut (a Stor98 drive left: "where it
  sits could not be read").

## 44.19 and 44.20 - The Installer's Window Driven From the Host, and the Legs `3.2.0.0` Left (2026-10-25 and 26)

Roadmap-usb4 tasks 44.19 (the installer's window legs driven headless
through QMP, was GUILEG.1) and 44.20 (the virtual-machine legs `3.2.0.0`
left, was OWED32.1), on branch `p44-gui`, one or two guests at a time
within two vCPUs. This sheet records each leg and its reading; the
roadmap's task lines say what is ticked. Runtime, QEMU 11.1.0 under TCG on
development host A, the host loaded by other agents' guests and builds.

### The vehicle

- **The package**: the `3.2.0.0` release downloads as published, copied
  from the cut's output and unpacked: `xhci98-3.2.0.0.zip` SHA-256
  `336be62c...35e4`, `-free` `486df50b...7b6f`, `-nonfree`
  `49ef219e...4a66`. In the base download `release-x86\xhci98.sys` is
  `dd230d50...` (the file `releases\3.2.0.0` holds), `SETUP.EXE`
  `2b1ea62c...`, `XHCIWIZ.EXE` `835401d9...`, `XHCIINST.EXE` `c056cea7...`.
  Nothing of the package was run on the host.
- **The scripts**: `scripts\vm-matrix\` on `p44-gui`, the commit each
  leg ran named in its row. The configuration (git-ignored) holds the
  guests of `release.config.sample.psd1` with this host's paths, ports
  49140 to 49148 and `MaxVcpu` 2.
- **Starting points**: 98 SE and XP copied from 43.12's kit (made
  2026-10-22 under the same QEMU and machine, so their hashes match); ME
  and 2000 made here with `make-start.ps1` (cold start to the poller
  40.6 s and 85.9 s, states 68 MB and 135 MB).
- **Evidence**: `E:\xhci98-vm\p44\p44-gui\runs\<leg>-<n>\` (`result.json`,
  the COM1 logs with the install and uninstall logs inside, the
  screendumps), outside the repository.

### 44.19 the installer's window, driven from the host

What it is and how it works: `build-and-test.md`, "The installer's
window, driven from the host". The legs below are `run-leg.ps1 -Leg
<id>-window` with nothing typed or clicked by a person.

Every leg below ran on QEMU's `pc` machine (i440FX), the machine of the
starting points 43.12 made; the owner's decision of 2026-10-25 (~23:00)
moves new legs to `q35`, so the Vista and 7 legs and every later one wait
for the `q35` bases.

| Leg | System | Scripts at | Result | Minutes | What the window part did |
|---|---|---|---|---|---|
| `w98se-window` (run 4) | 98 SE | `917d580e` | PASS, every check (driver file, `/list` current, stick E: seen, 4 MiB `fc /b` clean, `/list` after the uninstall clean); exits install 3, list 0, uninstall 3, list-final 0 | 12.4 | 4 page keys; the run ended at "Steps left for you"; Enter, Enter (Finish, Restart now); QEMU ended at the restart. The next starts: the controller's wizard walked (Insert Disk once, the directory typed once), the stick's wizard (5 pages), the wizard after the uninstall escaped |
| `w2k-window` (run 6) | 2000 SP4 | `67b8869c` | PASS, every check; exits 3, 0, 3, 0 | 4.8 | 4 page keys; no Steps page; one Enter (Finish); QEMU ended at the restart |
| `me-window` (run 4) | ME | `78984cf3` | PASS, every check; exits install **4**, list 0, uninstall 3, list-final 0 | 11.9 | 4 page keys (Alt+N, then the pages walked to Install); the run left a step (the controller, whose wizard the starting point had escaped), so Finish defaulted to "Restart Windows later": Enter, Enter, then the host's shut down from Start, Run. At the next start ME installed the controller by itself and asked to restart: Yes (`restart-me`) |
| `xp-window` (run 1) | XP SP3 | `78984cf3` | PASS, every check; exits 3, 0, 3, 0 | 9.5 | 4 page keys; Found New Hardware pages answered during the run: 3 (the root hub); no Steps page; one Enter; QEMU ended at the restart. Two more wizard pages answered at the next start |

What the runs before these found, each fixed before the run that passed:

- 98 SE, runs 1 and 2: the hidden poller box stopped at "Sharing
  violation reading drive C / Abort, Retry, Fail?" (its `find` read the
  log the window holds open), and once the batch had ended, its box, left
  "Finished", took the focus from the installer's window. Fixed by
  `COMMAND /F` with a marker file (`438ed41a`) and a closing `CLS`
  (`1a2ca13f`).
- 2000, run 5: every check passed; the leg failed on a host step after the
  uninstall that read the screen of a guest already switched off. That step
  is no longer taken on 2000, and it now says so instead of failing
  (`67b8869c`). Run 4 never started: the CD image builder's COM class
  refused an instance (0xC0AAB138) with two legs started together; it is
  now asked again (`917d580e`).
- ME, runs 1 to 3: a run with steps left does not restart at Finish (run
  1 waited for a restart that never came); ME then asked to restart before
  its desktop (run 2, answered by hand, so no verdict); Enter sent to that
  question while ME was still busy was lost (run 3). Fixed by the host's
  shut down (`20ec2e4c`) and `restart-me` with Yes sent again
  (`3b58d8a2`, `78984cf3`).

ME0E.1 (the stop "0E : 0197 : BFF62170"): not seen in the four ME runs
(eight cold starts after a fresh install, no NVMe enclosure attached; the
screendumps of every start show no stop). That is not the original
vehicle (it had the enclosure on the `usb4-generic` emulator).

#### The same legs on `q35` (2026-10-26)

From 2026-10-26 the legs run on QEMU's `q35` machine, from the `q35` bases
(`E:\xhci98-vm\q35-bases`, made and checked by another worker): the disk on
the board's AHCI (98 SE) or on a piix3-ide (ME, 2000, XP), the controller
behind a PCIe root port, on the QEMU build those bases were checked with
(usb4-emu's QEMU 11.1.2, SHA-256 `da60139f...0ce93`, a copy; with QEMU
11.1.0 98 SE's start stopped at the logo). Each starting point was made
again on it.

| Leg | System | Scripts at | Result | Minutes | Notes |
|---|---|---|---|---|---|
| `w98se-window` (q35 run 2) | 98 SE | `df27c28e` | PASS, every check; exits 3, 0, 3, 0 | 10.0 | Started cold (its medium is a disk, below); one installer box answered OK (the 9x gate, "If Windows has a hardware wizard open ..."); the run installed the controller itself, so no wizard at the next start; the stick's wizard, 5 pages |
| `w2k-window` (q35 run 3) | 2000 SP4 | `868896bc` | PASS, every check; exits 3, 0, 3, 0 | 5.7 | Its starting point walks 2000's three wizards for platform devices with no driver once (`walk-fnh`) |
| `me-window` (q35, QEMU 11.1.0, run 2) | ME | `1d5ee5ee` | PASS, every check; exits 3, 0, 3, 0 | 6.7 | the gate box answered OK; no restart question at the next start |
| `xp-window` (q35 run 3) | XP SP3 | `56c33712` | PASS, every check; exits 3, 0, 3, 0 | 7.8 | Found New Hardware answered during the run, 3 pages (the root hub), one more at the next start |
| `w7-window` (q35 run 9) | 7 SP1 | `60d463fc` | PASS, every check; exits 3, 0, 3, 0 | 16.3 | The consent prompt answered Yes (Alt+Y); the Welcome page opened without the focus (Alt+Tab once); "Windows can't verify the publisher of this driver software" twice, each answered "Install this driver software anyway" after bringing it forward through the taskbar; Finish took no Enter (the focus again), so the host shut Windows down |
| `me-window` (q35, runs 1 and 3) | ME | `1d5ee5ee`, `df27c28e` | FAIL: **ME stops responding at the first start after the install** | - | below |

What `q35` changed for the scripts: 98 SE reaches AHCI through INT 13h
alone and sees no CD there, and a piix3-ide added beside it for the CD
stopped its start at the logo; its leg's files are therefore a read-only
FAT disk on AHCI port 1 (QEMU's vvfat), which it reads as D:, and its legs
start cold, since a disk cannot be changed under a running guest
(`fca9fa65`). 2000 raises a Found New Hardware wizard at every start for
each platform device with no driver, and an install or a stick waits on it
(run 1: the stick was never seen and the uninstall exited 4, "Windows is
still installing something"), so its starting point walks them to their
end (`walk-fnh`, `6865ca3d`). On ME and 98 SE the installer asks whether a
hardware wizard is open (OK/Cancel) during the run, answered OK
(`1d5ee5ee`).

**Finding (ME on `q35`, outside these scripts): ME stops responding at the
first start after the install, 2 of 3 runs.** Runs 1 (QEMU 11.1.0) and 3
(the bases' build): the window's run ended with exit 3 and Finish restarted
Windows; at the next cold start ME stopped before the poller said BOOT. Run
1 froze at the desktop as the poller's box opened, the clock at 02:55, the processor
in the VMM with interrupts off (`EIP c005255a`, `EFL 00000046`, sampled
six times, once `c005543e`); run 3 froze at the desktop with the poller's
box up and the clock stopped at 04:03, the processor in the VMM with
interrupts on (`EIP` cycling `c000b94a`, `c000ba9c`, `c000bde4`,
`c00149ea`; `info registers`, `info pic`, `info irq`, `info pci` saved in
the run's `hang-b2.txt`). Run 2 on the same overlay recipe passed. No
"0E" stop screen in any (ME0E.1's text). On the `pc` machine the four ME
runs never stopped. Not attributed; owed to whoever takes ME on `q35`.

What XP and 7 needed on `q35` (each found by a run that failed, fixed
before the run in the table): XP's Found New Hardware wizard opened behind
the installer's window, its panel hidden, and the installer waited out its
ten minutes and ended with exit 4 (run 1; every check passed); the host now
sends Alt+Tab after a still minute and a half, and finds the wizard's title
bar in more columns (run 2 sent keys to the wrong window). On 7: the logon
task that starts the poller never ran (the bases restart the guest clock at
a fixed date, before the moment the task was made); the first elevation
after a restored state was refused once with no prompt, then prompted on
the next runs; the poller's window stood over the installer's; the
Welcome page took no key until Alt+Tab; a balloon going and the default
button's pulse were read as page changes; the unsigned-driver question
took no key until brought forward through the taskbar (`f87fa94d` to
`60d463fc`).

Not run here, owed: Vista (its starting point is being made), a `3.3.0.0`
package (the pages are walked by `until-install`, which does not depend on
the package's page count).
### 44.20 the virtual-machine legs `3.2.0.0` left

Work in progress (2026-10-26); all on `q35`, the bases' QEMU build.

- **43.12's window legs on Vista, 7, ME and 2000**: read by 44.19's legs
  above. On the final scripts (`cc115c1a` and later): `w98se-window` (run
  3) PASS 11.6 min, `w2k-window` (with the `-nonfree` download, its Other
  USB drivers page walked, the pack left unticked as by default) PASS 6.2
  min, `vista-window` (run 5) PASS 12.1 min (two unsigned-driver
  questions clicked; Finish restarted), `w7-window` (run 10) PASS 14.2 min
  (two unsigned-driver questions; Finish restarted). ME: PASS once, and
  the stop at the first start after the install twice (above).
- **43.48's legs on ME, 2000 and XP** (console legs with the `-nonfree`
  download, `37bbd02d` to `4c7e93ec`): ME (`me-nonfree`,
  `/wdmnonfree:copy`) PASS: 457 of 457 files copied to
  `C:\XHCI98\USBDRV\NONFREE`, the uninstall removed them. 2000
  (`w2k-nonfree`, `/wdmnonfree:stage=` the five packages `STAGE.TXT` gives
  2000) PASS: 7 INFs staged, four "Digital Signature Not Found" answered
  Yes, the uninstall removed `oem2.inf` to `oem8.inf` and the copies. XP
  (`xp-nonfree`, the six packages) PASS: 6 INFs staged, the uninstall
  removed six from the driver store. 7 (`w7-nonfree`) not run yet.
- **ME0E.1**: no "0E" stop in any ME run (no enclosure attached).
- Not started: 43.5's copy cut, 43.17's full matrix, 43.24's dock on 8
  and 8.1 (the `q35` bases now make it possible), 43.43's case 2, 43.45's
  2000 resume (the 2000 base has no ACPI), 43.57's T.14, 43.69's fault
  rate.

## 44.48 - The ASM4242 Layout with "Above 4G Decoding" Off (2026-10-26 and 27)

The ASM4242 layout with the option off is design record 22 section
18.5: the firmware leaves the bridges' 64-bit prefetchable
windows above 4 GB as with the option on, the host bridge's `_CRS` loses
its window above 4 GB, and Linux reassigns those windows below 4 GB
itself.

### The emulator

`usb4-emu` branch `asm4242-model` at `bd0c497`, its QEMU fork at
`73780e9` (a `q35-pcihost` property `acpi-pci-hole64`, off: no 64-bit
window in `_CRS`; `pci-hole64-size=0` does not remove a 64-bit window
the firmware placed), `qemu-system-x86_64.exe` `08D49470...`. Two
layouts beside the 4G-on one: the 4G-off layout, the windows as the
firmware leaves them (the guest's processor must report 40 address
bits; on the `pentium3` bases `lm=on,xlevel=0x80000008,phys-bits=40`),
and the low-window layout, the windows as Linux leaves them, all below
4 GB (root port `bus-reserve=103,mem-reserve=776M,io-reserve=24K,
pref32-reserve=4M,hotplug=off`). A Linux 7.0 guest on the 4G-off
layout logs "can't claim" and reassigns 4 MB, 4 MB, 2 MB and 2 MB, as
record 22 section 18.5 has Linux do; the low-window layout gives the same
windows from SeaBIOS. Not modelled: the per-port split of 48 buses,
384 MB and 128 GB, and the absolute addresses.

- XP on the q35 base (`pci-hole64-size=0`), ORICO at boot: boots on
  the low-window layout (every bridge problem 0, the NVMe, xHCI and NHI
  problem 1 before an install, no problem 12) and on the 4G-off layout
  with a 40-bit processor (XP moves the windows below 4 GB itself); the
  4G-on layout still stops it with 0xA5. The ASMedia reserve is usable
  on XP's base now, for 44.45.
- On the 4G-off layout: 2000 stops at "Starting up"; 98 SE and ME reach
  the desktop with the empty tunnel port at problem 12 (its window placed
  at 0x1_0020_0000, whose low half overlaps memory).
- The 4G-on layout on the `pentium3` bases programs an invalid window
  (36 address bits cannot hold 256 GB): kits that used it on 98 SE, ME
  or 2000 had such windows.

### Legs (q35, the low-window layout, the ORICO on USB4 port 2)

Test cut p44storK (`7ee3a12e`, zip `3A5FCA9F...`); `I46.BAT` on 98 SE
and ME, `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98` on
2000. The tunnel is on the second port (14:01.0 in record 22 section 18.1), where the
q35 re-runs' rows used the first.

| Leg | System | Result |
|---|---|---|
| cold boot | 98 SE | pass: install exit 3 (the controller's Insert Disk prompt answered `C:\XHCI98`); at the next cold boot every ASM4242 function, both tunnel ports, both enclosure bridges, the NVMe and both root hubs problem 0, "ORICO (E:)"; 14:01.0's subtree buses 4-6, memory ce400000-ce5fffff, prefetchable 2 MB, the NVMe's BAR ce400000 |
| cold boot | ME | pass: install exit 3, a restart, then at the cold boot all problem 0 as on 98 SE and "ORICO (E:)". That guest reset by itself about a minute after the readings and next started in Safe mode with a registry error; the hot plug ran on a fresh overlay |
| cold boot | 2000 | pass: install exit 3, the wizards finished at the cold boot; the NHI, the filtered ports, the enclosure's bridges and Stor98 problem 0, "ORICO (E:)" |
| hot plug | 98 SE | fail, on this layout and on the default one alike: the tunnel came up and the connection manager re-enumerated twice, but the enclosure's upstream port got no bus number (SeaBIOS gives the ASMedia ports no spare buses: the second port has buses 4-4) and 98 SE created no devnode behind it. Carried to 44.45 |
| hot plug | ME | fail, the same as 98 SE; a restart afterwards left a black screen |

Against "The q35 re-runs" (4G on, port 1) the cold-boot rows read the
same.

## 44.47 - ME's Shut Down on q35 with the ASM4242 profile, and ME's first start after the install (2026-10-27)

A task of Phase 44 (the owner, 2026-10-26): 44.3's q35 re-runs saw ME
with the ASM4242 profile and test cut G leave a black screen at Shut
Down (twice, over 2.5 minutes) while a restart completed, and 44.43's
fix had been read on the Alpine Ridge profile only; and the q35 window
legs of branch `p44-gui` ("44.19 and 44.20" above) saw ME stop responding at the
first start after the install in 2 of 3 runs. This section places the
first, fixes it, and reads the second again.

### The vehicle

Development host A; QEMU's q35 board from the q35 base images
(`E:\xhci98-vm\q35-bases`: the ME base on a piix3-ide disk, SHA-256
`42fea19a...`; the 98 SE base on AHCI), the emulator snapshot
`bin\asm4242-20261025-2230` (SHA-256 `da60139f...`) and the bases'
launcher, `-smp 1 -m 512`, one or two guests at a time, every guest an
overlay in this worker's folder. The ASM4242 profile (`-Usb4 asm4242`:
the ASM4242 switch, its xHCI `1b21:2426` and NHI `1b21:2425`, the
`usb4-generic` host with `host-model=asm4242`), the ORICO enclosure
model (`asm2464`, its NVMe) attached at power-on unless a row says
otherwise. A first boot of each base with the enclosure had Windows'
wizards install its bridges (Enter on each page) and was shut down
(2 s on ME, 3 s on 98 SE); every row below is an overlay of that.

The packages: cut G (44.3's test cut of `cfe47f77`, x86 zip
`E1A976AD...`), and cut A, a test cut of `3.3.0.0` at `2aabaa07`
(`make-release.ps1 -Arch x86 -TestLabel p44meshutA -NoNonFree -NoWdmFree
-NoDriverPack -SkipQualtool`, zip `172A1D4D...`; `xhcicm.sys` release
`5d69ee4c...`), both with 44.43's change. Installed with `XHCIINST /dir
release-x86 /yes /nopause /thunderbolt /stor98` from a second disk
(exit 3, "xHCI98 USB4 Router: xhcicm ... started"; the NHI's key
`Class\System\0054`, Stor98 on the enclosure's drive), then Restart in
that session, which ends QEMU (`-action reboot=shutdown`); each row's
start is a cold start of a fresh overlay of that state. The Shut Down
dialog's choice was set by key on every row (ME keeps the last one: two
early rows below were Restart, as marked).

For the traces, `xhcicm.sys` and `xhci98.sys` of the `qemu` flavour
(`ecc98f5d...`, `6d229315...`) and Stor98's debug-port image
(`STOR98_DBGPORT=1`, `57e99358...`, with `-device usb4-debug-port`)
copied over the installed ones in a session without the enclosure,
which shut down in 3 s; and two diagnostic builds of `xhcicm.sys`, the
`qemu` flavour with lines written to port `0xE9` at the system
SET_POWER's dispatch and route, the direct route's work, a CONFIGMG
device-id call, and (the second, `053248cb...`) the thread's sleeps once
the shutdown state is published - not committed.

### Readings before the fix (ME, ASM4242 profile)

| Guest | Shut Down |
|---|---|
| no package, the enclosure attached | powered off in 2 s |
| cut G, in the install's own session | hung, a black screen, over 150 s |
| cut G, cold starts, Restart (the dialog kept Restart) | hung both times, the screen black in text mode, over 100 s (`EIP` in the VMM: `c005255a`, `c000b5d2`, `c0013be4`); a Restart in the install's session itself completed in 3 s |
| cut G, cold start, the enclosure absent (Restart) | 2 s |
| cut G, `XhciCmMode` 0 in the NHI's key, the enclosure attached | 3 s (after an unintended Stand by and resume in the same session) |
| cut A, cold start | hung, over 120 s |
| cut A, the `qemu` `xhcicm.sys` | powered off in 2 s once (`cm.power.direct`, `cm.shutdown`, `u4.stop`, `cm.shutdown.no-config` logged), hung once with no line of the shutdown |
| cut A, the traced binaries, the enclosure absent | 3 s |
| the same, the enclosure attached | hung |
| the same, `XhciCmMode` 0 | 2 s |
| the same, Stor98's image renamed away (no driver on the drive) | hung |
| the first diagnostic build | hung once, 3 s once |
| the second diagnostic build | hung twice |

With the connection manager running and the enclosure attached, seven of
nine cold-start shutdowns of cut A hung; without the enclosure (a cold
start, and six sessions that prepared the traced overlays) or with
`XhciCmMode` 0 (one), none of eight. Stor98 is not needed for it.

**Where it waits.** The traces order the 9x shutdown's power IRPs as
one `xhci98.sys` controller's S5 then D3, then this driver's system
IRP, then the other controller's, then Stor98's SHUTDOWN request (stage
`0x60`). In each of the three hangs of the diagnostic builds the line
after the first controller's D3 was the
diagnostic `cmdiag.power=00020006` (SET_POWER, system, S5) and
`cmdiag.sysdone.route=00000002` (the direct route queued to the
thread), and nothing after it - no `cmdiag.direct`, no sleep returning
once the shutdown state was published (the second build); in the
diagnostic build's one pass `cmdiag.direct`
followed at once. The processor sat in the VMM's idle loop with the
timer's interrupt still counting (`info irq`: 114540, then 115172 three
seconds later). Memory images of two hangs (`pmemsave`, read with the
diagnostic build's map from a link of its objects with `-map`): the
connection-manager thread's stack page held `cmThread` ->
`CmNhiPollPass` -> `cmU4LearnPending` -> `cmU4Learn` ->
`tbtU4Evidence` -> `XhciTbtLearn` -> `xhciTbtOccupancy` ->
`xhciTbtRead` -> `TbtCtlRead`, the idle pass's learning retried for a
port still pending (record 22 section 7.6), inside a ring transfer, and
was byte-identical in two images a minute apart; the CONFIGMG worker's
(`cm_cfgwk.c`) held
`cmWkThread` -> `cmWkFindDevnode` -> `CmCfgmgDeviceId`. So the
system IRP, completed only by the thread, waited on a thread that did
not run again; an idle thread is woken by the queueing itself, which
fits the enclosure (with it the idle pass makes ring transfers for the
pending learning) and `XhciCmMode` 1 both being needed. Why a thread
inside a transfer whose waits are timed does not run again at that point
of a 9x shutdown is not read, nor why the learning stays pending with
this enclosure model. Placed in this driver: the order 43.65 gave the 9x
shutdown made the system IRP wait for the thread; Stor98 is not needed
for it.

98 SE on the same profile with cut A, the enclosure attached: two
cold-start shutdowns, 2 s each.

### The fix

Design record 21 section 5.6 ("And on Windows 98 SE and ME the system
IRP does not wait for the thread"), `3d35f7ab` and `b8eed690`; the code
`8303178f` and `da7b348b`: on 98 SE and ME a shutdown's system IRP takes
the route `CM_PWR_DETACH` (`cm_pwrr.c`): the completion routine
publishes the shutdown state, queues the quiescence to the thread with
no IRP (`CM_WORK_QUIESCE`, run by `CmPowerQuiesceWork`, logged
`cm.power.detached`) and lets the system IRP go on at once. 44.43's
rule for the quiescence is unchanged; NT and sleeps are unchanged; the
USB4 profile's recovery counts the new work bit as a power transition.
Host suite `test_cm_pwrr`. Reviewed and revised. The gated builds of
`da7b348b`: x86 `scripts\build-driver.cmd all` passed
(`out\build-c-x86.log` of the worktree), amd64 likewise
(`out\build-c-amd64.log`).

### The legs (cut B)

Cut B is a test cut of `da7b348b` (`-TestLabel p44meshutB`, the same
flags as cut A; zip `E0F69B7B...`, `xhcicm.sys` release `c7e00d3f...`),
installed as above on a fresh overlay of each profile's base and
restarted; every row is a cold start of a fresh overlay of that state.

| System, profile | Reading |
|---|---|
| ME, ASM4242, the enclosure attached | Shut Down: powered off in 2, 2, 3 and 3 s (four starts); Restart: QEMU ended at the reset after 2 s |
| ME, ASM4242, the `qemu` `xhcicm.sys` of `da7b348b` (`62883356...`) | powered off in 3 s twice; the log: `cm.power.detached=00000006`, `cm.shutdown=00000002`, `u4.stop=00000000`, `cm.shutdown.no-config=00000002` |
| 98 SE, ASM4242, the enclosure attached | powered off in 3 s twice (cut A: 2 s twice before the fix) |
| ME, Alpine Ridge (`-Usb4 tb3ar`, the TB3 enclosure attached at power-on; the NHI "xHCI98 TB3 Controller (Intel Alpine Ridge)") | powered off in 2 s twice (44.43's reading holds) |

Every first start of these overlays reached the desktop and opened the
Start menu and the Shut Down dialog.

### ME's first start after the install

`p44-gui`'s finding (ME stops responding at the first start after the
install, 2 of 3 runs, on the q35 ME base with no USB4 profile) read
again on cut A, three fresh installs on that base (`-Usb4 none`, the
q35 board's `qemu-xhci`): each install exit 3 and a Restart in its
session (2 to 3 s), then a cold first start watched for three minutes
(screenshots at 60, 120 and 180 s, the clock moving), the Start menu
opened, and Shut Down in 3 s. **0 of 3 stopped responding.** On the
ASM4242 and Alpine Ridge profiles, none of the fifteen further first
starts after an install in this task either (cut G four, cut A three,
cut B eight; each a fresh overlay of an install's state). Those
runs used the console installer (`XHCIINST /yes`), not `p44-gui`'s
window and its poller; not attributed further.

### Release note

Fixed in `3.3.0.0`: Windows 98 SE and ME could hang at Shut Down or
Restart with a USB4 enclosure attached and the connection manager
running.

Kit, logs, screenshots and memory images: `E:\xhci98-vm\p44\p44-meshut\`
(`STATE.md`, `pids.txt`, one folder per guest: `debugcon-a.log`,
`serial-a.log`, `*.png`), `E:\xhci98-scratch\p44-p44-meshut\`
(`dump-*.bin`), the stack scans `scan-*-cm.txt` and `stkscan.py`.

## 44.49 - The ASM4242 Layout's Prefetchable Windows Above 4 GB: Which Systems Need Them Moved, and Why 2000 Stops (2026-10-27)

A task of Phase 44 (the owner, 2026-10-27; decisions table, row
"44.49"): which systems meet the ASM4242 layout's prefetchable windows above 4 GB
with "Above 4G Decoding" off, what stops 2000 at "Starting up", and the
design of a step that moves those windows before 98 SE and ME read them
(design record 32, before code).

### The vehicle

Development host A; QEMU's q35 board from the q35 base images
(`E:\xhci98-vm\q35-bases`, each an overlay in this worker's folder; base
SHA-256 prefixes: Vista x86 `4d1aa775`, 7 x86 `95893177`, 8 x86
`b8e091bd`, 8.1 x86 `9714c3d8`, 10 x86 `ac361c39`, 10 x64 `915dd350`,
2000 `7fc9714e`, 98 SE `ab1d4d65`, ME `42fea19a`); the emulator
`bin\asm4242-20261026-4goff` (`usb4-emu` `asm4242-model` `bd0c497`, its
QEMU `73780e9`, `qemu-system-x86_64.exe` `08D49470...`) with the
launcher of "44.48", the ASM4242 profile and the ORICO enclosure model
attached at power-on (on the first tunnel port, 02:00.0 in the guests'
numbering, buses 3 to 5). The 4G-off layout is `-AsmReserve4gOff`
(`acpi-pci-hole64=off`; on the `pentium3` bases a 40-bit processor);
the 4G-on layout is `-AsmReserve` (the option on); the low-window layout is
`-AsmReserve4gOffLow`. No package of this project is installed on any
guest. The NT 6 readings are `wmic path Win32_PnPEntity` (problem code
and device id) on the guest's screen and QMP `info pci` for the
registers; the firmware's registers are those of the Vista guest, which
match its `info pci` taken while the firmware ran (the qemu64 guests);
on the `pentium3` bases QEMU's `pci_cfg_write` trace (`-d
trace:pci_cfg_write`) gives every configuration write in order.

### Readings (the 4G-off layout unless a row says)

The firmware's windows on the qemu64 guests: the ASM4242's root port
00:03.0 prefetchable 7F_8000_0000 to BF_7FFF_FFFF, the upstream port
7F_8000_0000 to 7F_803F_FFFF, the tunnel ports 7F_8020_0000 to
7F_803F_FFFF (02:00.0, and the enclosure's two bridges the same) and
7F_8000_0000 to 7F_801F_FFFF (02:01.0), the xHCI's and NHI's ports
closed; every non-prefetchable window and BAR below 4 GB.

| System | Boots | ASM4242 root port and below | Windows' registers |
|---|---|---|---|
| Vista SP2 x86 | desktop | the root port 00:03.0 problem 12; no ASM4242 function, tunnel port, enclosure bridge or NVMe enumerated | unchanged from the firmware's |
| 7 SP1 x86 | desktop | the same | unchanged |
| 8 x86 | no: the spinner at the logo for over 25 minutes on two fresh overlays; the low-window control reached the Start screen in under five | - | unchanged while it spins |
| 8.1 x86 | desktop | root port problem 12, nothing behind it | unchanged |
| 10 x86 (19041) | desktop | root port problem 12, nothing behind it | unchanged |
| 10 x64 (19041), option off | desktop | root port problem 12, nothing behind it | rewritten above 4 GB: the root port 40_0000_0000 to 7F_FFFF_FFFF, the upstream port 40_0000_0000 to 40_3FFF_FFFF, 02:00.0 and the enclosure's bridges 40_2000_0000 to 40_3FFF_FFFF, 02:01.0 40_0000_0000 to 40_1FFF_FFFF |
| 10 x64, option on (the 4G-on layout) | desktop | every bridge, the xHCI and the NVMe ("Standard NVM Express Controller") problem 0, the NHI 28 (no driver) | the same addresses as the row above |

XP 32-bit moves the windows below 4 GB itself ("44.48"). So of the
32-bit NT 6 systems none enumerates anything behind the ASM4242 with the
option off, and 8 does not start; 10 x64 needs the option on.

**98 SE and ME** (the trace, from a cold start to the desktop): SeaBIOS
programs the root port 1_0000_0000 to 40_FFFF_FFFF on these bases (the
low registers `0001h` and `FFF1h`, so 0 to 4 GB in a 32-bit view, as in
record 22 section 18.1's layout), the upstream port 1_0000_0000 to
1_003F_FFFF, 02:01.0 1_0000_0000 to 1_001F_FFFF, 02:00.0 and the
enclosure's bridges 1_0020_0000 to 1_003F_FFFF, and the xHCI's and NHI's
ports closed with upper halves 1. Both systems then write only `24h` and
`26h` (16-bit writes, `FFF0h` and `0000h`) of the root port, the
upstream port, 02:01.0 and the xHCI's and NHI's ports, and never `28h`
or `2Ch` of any bridge: the root port's window is left 1_FFF0_0000 to
40_000F_FFFF, open in 64-bit terms; 02:00.0's and the enclosure
bridges' windows are not written. On
98 SE Device Manager shows 02:00.0 (the port the enclosure is on, the
one the enumerator left) at problem 12, "This device cannot find any
free Memory Range resources to use", no conflicting device listed, and
nothing behind it; the other three ports without a problem, the xHCI
and the NHI below two of them without a driver ("44.48" read the problem-12 port as the empty one; in this
kit it is the enclosure's).

**2000** (the q35 base, Standard PC HAL): "Starting up" with the bar
partly drawn, for good. The trace: SeaBIOS programs the root port's
window 1_0000_0000 to 40_FFFF_FFFF (`24h` base `0001h`, limit `FFF1h`,
`28h` 1, `2Ch` `40h`); the PCI driver then writes all-ones into the
header and writes it back, and finally writes `24h` = `FFF00000h`, `28h`
= 0 and `2Ch` = `FFFFFFFFh` (a window from 0 to `FFFFFFFF_FFFFFFFF`) and
the command register, and no configuration write follows. The processor
then loops at `F200974B`-`F200976C`: `pci.sys` (version resource
"5.00.2195.6655", "NT Plug and Play PCI Enumerator", image base
`F2000000`, offset `974Bh` in its `PAGE` section), walking a list's back
links while each node's type byte is 1 and its 64-bit start not above
the previous one's; the list (`EDI` = `E12605E8`) has one node, linked
to itself, start 0 and end `FFFFFFFF_FFFFFFFF`, type 1, so the walk
never ends. The return addresses on the stack are `pci.sys` (`+89E`,
`+6AE0`, `+E45`) and then the kernel. Read with QMP `info registers`
and `memsave` (debugger) and the loop's bytes disassembled on the host
with capstone (static); nothing tracked (`legal-provenance.md` section
4). With every window below 4 GB (the low-window layout) 2000 boots
("44.48").

**ME and `AUTOEXEC.BAT`** (for the design's choice of where the step
runs): on the q35 ME base, with no USB4 model, a line `@echo
ran>c:\aeran.txt` appended to `C:\AUTOEXEC.BAT` (Notepad showed it
there) and the guest shut down and started cold: `C:\AERAN.TXT` did not
exist; ME did not run the line.

### The design

Design record 32 (draft 1, before code): a static VxD,
`XHCIPFW.VXD`, at `Sys_Critical_Init` ahead of the configuration
manager and the PCI enumerator, that closes every bridge's 64-bit
prefetchable window whose upper halves are not 0 (upper halves 0, base
`FFF0h`, limit `0000h`), counts BARs above 4 GB and logs to the
registry; for 98 SE and ME only; the owner's questions in its section 9.

Kit, scripts, screenshots, `info pci` dumps and traces:
`E:\xhci98-vm\p44\p44-pref\` (`STATE.md`, `pids.txt`, one folder per
guest: `infopci-*.txt`, `trace-*.log`, `*.png`; `nt5mem.py`,
`polltr.py`).

### XHCIPFW.VXD's load on 98 SE and ME (record 32 section 5.1, the owner's Q1 read)

The qemu-flavour image of the first code (`src\pfw`, unreviewed then),
copied by hand to `C:\WINDOWS\SYSTEM` with the key
`HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW`, `StaticVxD` =
`XHCIPFW.VXD`, `Start` = `00`; q35, the 4G-off layout, the ORICO at power-on,
cold starts (`-action reboot=shutdown`). On both systems:

- the debug console: `sys_critical_init order 16300000`, a scan of 18 (98
  SE) or 19 (ME) functions, nine bridges closed (00:03.0, 00:1c.0, 01:00.0,
  02:00.0 to 02:03.0, 03:00.0, 04:00.0) from their firmware values (the
  root port `24h` `FFF10001h`, `28h` 1, `2Ch` `40h`) to `0001FFF1h`, 0, 0,
  none refused or parked, no window across 4 GB, no BAR inside, no xHCI
  above 4 GB; `device_init`, the registry write's code 0;
- QEMU's `pci_cfg_read` and `pci_cfg_write` trace: the VxD's scan is the
  first configuration access after the firmware's last (98 SE line 5 067,
  ME 5 116), its writes come before any access by Windows to a bridge;
- at the desktop (`regedit /e` of the key and of `HKEY_DYN_DATA\Config
  Manager\Enum`, sent to COM1): `Result` 1, `Closed` 9, `Functions` 12h
  or 13h, `Bridges` nine records as above, `Version` the stamp; every
  ASM4242 bridge and both enclosure bridges problem 0 (98 SE's 02:00.0
  had problem 12 without the step), the NVMe, the ASM4242 xHCI and the
  NHI enumerated at problem 28 (no driver installed on these overlays);
  the Add New Hardware wizard found the new bridges and installed
  `MACHINE2.INF`'s "PCI standard PCI-to-PCI bridge".

So the static-VxD key loads it early enough on both, and the `SYSTEM.INI`
fallback is not used.

### 2000 and the 32-bit Vista to 10: what runs before the PCI driver (record 32 section 11)

A probe of this project's own (a boot-start driver of the group `System
Reserved`, built with the Windows 2000 DDK in a scratch folder, not
tracked) marks its `DriverEntry` in the configuration trace with three
reads of 00:00.0 offset `F8h`; on 2000 (q35 base, Standard PC HAL) the mark
is at line 311 854 and the PCI driver's first read of the root port's
window at 311 914, its writes from 314 443; on 7 x86 the mark is at 13 330
and Windows' first read of the window at 13 406. A second probe runs
record 32's step (`pfw_scan.c` through `CF8h` and `CFCh`) there and prints
its counts to the debug console; installed on a start without the USB4
model, then started cold on the 4G-off layout (8 and 10 with `powercfg /h
off`, so that the start is a full one):

| System | Debug console | Result (`wmic` `Win32_PnPEntity` on NT 6; `Enum\PCI` on 2000) |
|---|---|---|
| 2000 | `C 1 13 9 0 0 0` | the desktop, past "Starting up"; every ASM4242 bridge, both enclosure bridges, the xHCI, the NHI and the NVMe in `Enum\PCI`, Found New Hardware for them |
| 2000, ACPI HAL (`q35-bases\w2kacpi`, `halacpi.dll`, one processor) | `C 1 13 9 0 0 0` | the same; without the step it stops at "Starting up" as under the Standard PC HAL: the same writes (`24h` `FFF00000h`, `28h` 0, `2Ch` `FFFFFFFFh`) and the processor at `F2009754`, `F200975C`, `F200974B` in the image at `F2000000` (`pci.sys`'s loop); with the step the mark at trace line 13 107 and the PCI driver's first read of the window at 13 119 |
| Vista SP2 x86 | `C 1 12 9 0 0 0` | the root port and every bridge 0; the xHCI and NHI 1 |
| 7 SP1 x86 | `C 1 12 9 0 0 0` | every bridge 0; NVMe, xHCI and NHI 28 |
| 8 x86 | `C 1 12 9 0 0 0` | the Start screen; every bridge and the xHCI 0, the NVMe and NHI 28 |
| 8.1 x86 | `C 1 12 9 0 0 0` | every bridge, the NVMe and the xHCI 0, the NHI 28 |
| 10 x86 | `C 1 12 9 0 0 0` | every bridge, the NVMe (stornvme) and the xHCI 0, the NHI 28 |

8's hang without the step: its PCI driver probes the root port's window
with all-ones, writes it back, clears the root port's AER status, and no
configuration write follows; QMP `info registers` shows one processor
idling in the HAL and the other in the kernel on its idle stack (image
bases found by their `MZ` headers in guest memory, read-only), so the
start waits. 10 x86 on the 4G-on layout (the option on) stops at Windows'
recovery screen on a fresh overlay and on the probe's alike.

Kit, scripts and evidence: `E:\xhci98-vm\p44\p44-pref\` (`w2k-probe`,
`w7-probe`, `w8x86-tr`, `w8-probe`, `vx-probe`, `w81-probe`,
`w10-probe`, `w10-on`; `xferp`, `xferp2`; `dyn.py`, `enumpci.py`,
`modbase.py`, `stackmods.py`); the probes' sources in
`E:\xhci98-scratch\p44-p44-pref\probe` and `probe2`.

### XP on the 4G-off layout, read again (record 32 section 7)

XP SP3 on its q35 base (`pentium3` with 40 address bits,
`pci-hole64-size=0`, two processors), a fresh overlay, QEMU's
`pci_cfg_write` trace; the firmware's last write is its `PAM` write to
00:00.0 `90h` (line 454 or 455), so every later write is XP's.

| Start | The firmware's windows | XP's writes | Device list (`wmic` `Win32_PnPEntity`) |
|---|---|---|---|
| 1, ORICO at power-on | the root port `24h` base `0000h`, `28h` 1, `2Ch` `40h`; the upstream port, the tunnel ports and the enclosure's bridges with upper registers 1 | every one rewritten with upper registers 0: the root port `24h` `CCF0C900h` (`CC900000h` to `CCFFFFFFh`), the upstream port `CCF0CCC0h`, 02:00.0 and the enclosure's bridges `CCF0CCE0h`, 02:01.0 `CCD0CCC0h`, the xHCI's and NHI's ports closed (`FFFFh`), 00:1c.0 `C8F0C8E0h` | every bridge problem 0, the xHCI and NHI 1 (no driver); the NVMe not listed at that reading, though XP had configured it (its BAR `CE600000h`) - the first start after the bridges were found, Found New Hardware open |
| 2, ORICO at power-on | the same | the same values | every bridge 0, the NVMe present ("PCI Device", problem 1, no driver), the xHCI and NHI 1 |
| 3, no enclosure | the same | the same values | every ASM4242 bridge 0, the xHCI and NHI 1 |

So 32-bit XP moves the windows below 4 GB itself at every start and
needs no step; XP x64 was not read. Evidence:
`E:\xhci98-vm\p44\p44-pref\xp-off` (`trace-a.log` to `trace-c.log`,
`serial-a.log` to `serial-c.log`, `infopci-a.txt`).

### What the installer reads for the owner's Q7 (record 32 sections 9 and 11.3)

On the 4G-off layout, fresh overlays of the 7 x86 and 10 x86 q35 bases,
nothing of this project installed: the ASM4242's root port (00:03.0) at
problem 12, and its `Enum\PCI\VEN_1B36&DEV_000C&...\LogConf`
`BootConfig` (`REG_RESOURCE_LIST`, read with `reg query` to
COM1) the same on both: interface 5 (PCI), five descriptors - two memory
ranges below 4 GB (`FEA11000h`, `CE000000h`), a `CmResourceTypeMemoryLarge`
(type 7) with flags `0244h` (prefetchable, window decode, 40-bit length)
starting at `7F_8000_0000`, a port range and an interrupt. So Windows'
own record of the firmware's window names it above 4 GB, which is the
rule the installer reads (`inst_pfw_bootcfg_high`; host vectors built on
these bytes).

### XHCIPFW.VXD through the installer on 98 SE and ME (record 32 section 10.2 items 1, 4 and 7)

The reviewed code (`src\pfw\`, the installer at 432ab917; the build
`scripts\build-driver.cmd all` passed on x86 and amd64), a test cut
(`make-release.ps1 -Arch x86 -TestLabel p44pfw -NoNonFree -NoWdmFree
-NoDriverPack -SkipQualtool`, into a scratch folder; `XHCIPFW.VXD`
SHA-256 `F42BB63E...A2CC`, 9 793 bytes, at the root of `release-x86\` and
`debug-x86\`); q35, the 4G-off layout, the ORICO at power-on, fresh
overlays of the q35 98 SE and ME bases; the console tool from the cut's
`tools\xhciinst\` (`XHCIINST.EXE /dir release-x86 /yes /nopause`), its
output and the readings sent to COM1.

| Step | 98 SE | ME |
|---|---|---|
| install, `/yes` | the question "XHCIPFW.VXD, PCI windows above 4 GB closed at start", `/yes: Y`; "XHCIPFW.VXD is in C:\WINDOWS\SYSTEM\XHCIPFW.VXD", "registered as a static VxD"; the key's `StaticVxD` and `Start` = `00`; the record's `File` the package's hash; exit 3 (restart) | the same |
| cold start (QEMU's configuration trace) | the VxD's scan the first access after the firmware's last (line 5 067), its writes from 21 634; the log `Result` 1, `Closed` 9, `Functions` 12h, nine bridge records, `Version` "XHCIPFW.VXD 3.2.0.0"; every ASM4242 bridge and both enclosure bridges problem 0, the ASM4242 xHCI on `xhci98.sys` with its root hub problem 0 | the same (scan at 5 116, writes from 21 692, `Functions` 13h) |
| `/thunderbolt /stor98` (the NHI now enumerated), then a cold start | the NHI on `xhcicm.sys` problem 0, the NVMe on Stor98 problem 0 with its disk, "ORICO (E:)" listed | the same |
| uninstall (the word REMOVE) | "XHCIPFW.VXD is no longer registered", "C:\WINDOWS\SYSTEM\XHCIPFW.VXD was deleted"; no key, no file | the same |
| cold start after it | 02:00.0 problem 12 again, nothing behind it | the same (ME shows the problem 12 too) |

Not run here: the window's box (the self-test lays it out, record 26),
`/noxhcipfw` and an unticked box over an installed file, an update over
an older image, standby and resume (record 32 section 10.2 item 5), a
device with a prefetchable BAR above 4 GB (item 6), and the hardware bench (item
8, 44-E.6).

Evidence: `E:\xhci98-vm\p44\p44-pref\w98se-leg` and `me-leg`
(`serial-a.log` to `serial-d.log`, `trace-b.log`, screenshots), the cut
in `E:\xhci98-scratch\p44-p44-pref\rel`, `xferi\` (the batches).

### XHCIPFW.SYS through the installer on 2000, 7, 8, 8.1 and 10 x86 (record 32 sections 11.1 and 11.4)

The reviewed code at 30410253 (`scripts\build-driver.cmd all` passed on
x86 and amd64), a test cut (label `p44pfw2`, into a scratch folder;
`XHCIPFW.SYS` SHA-256 `E76013F6...20E0`, 9 849 bytes, the release image);
fresh overlays of each system's q35 base; the console tool from the
cut (`XHCIINST.EXE /dir release-x86 /yes /nopause`), its output and the
readings (`regedit /e` or `reg query` of the service key and the
installer's record, `wmic` problem codes) sent to COM1; then a cold
start on the 4G-off layout with the ORICO at power-on. 2000 and 8 were
installed on the q35 board without the USB4 model (they do not start
with the windows above 4 GB), 7, 8.1 and 10 on the 4G-off layout itself;
fast startup off on 8, 8.1 and 10 (`powercfg /h off`). The log's names
are those of that image (no `XhciPfw` prefix yet).

| System | Install | Cold start on the 4G-off layout |
|---|---|---|
| 2000 (Standard PC HAL, one processor) | ticked (2000: always), `xhcipfw.sys` registered, exit 3 (a first run exit 4: Windows' found-new-hardware wizard was pending) | the desktop; `Mode` 1, `Closed` 9, the hold 3.4 ms; every ASM4242 bridge and the NVMe in `Enum` |
| 2000 (ACPI HAL, one processor) | the same | the desktop; `Mode` 1, `Closed` 9, the hold 43.7 ms; the same devices |
| 7 SP1 (two processors) | ticked by the installer itself (a bridge at problem 12 with its `BootConfig` window above 4 GB), exit 3 | `Mode` 2 (inside `KeIpiGenericCall`), `Closed` 9, the hold 47.9 ms; every bridge problem 0 |
| 8 (two processors) | ticked (8: always), exit 3 | stops at the spinner as without the step (25 minutes and more), every window still above 4 GB (`info pci`); the next start without the USB4 model read `Skipped` 4 ("late"), `Mode` 0 |
| 8.1 (two processors) | ticked by the installer itself, exit 2 | the desktop; `Mode` 2, `Closed` 9, `HoldTicks` 3F7A14h at 100 MHz (41.6 ms); `Version` "XHCIPFW.SYS 3.2.0.0"; no device with a problem but QEMU's `fw_cfg` (28); the NVMe disk listed |
| 10 (19041, two processors) | the same | the desktop; `Mode` 2, `Closed` 9, `HoldTicks` 43F422h (44.5 ms); no device with a problem but `fw_cfg` (28); the NVMe disk listed |

8's result: its `\Driver\PCI` object exists when this driver's
`DriverEntry` runs, although 8 reads the bridges after it (the probe,
which made no such check, closed them and 8 reached the Start screen).
The check now asks whether the `SOFTWARE` hive is loaded (record 32
section 11.2; cfc36595), to be read again on 8 and on every system.
(Read again in "44.72": that 8 reads the bridges after this driver held
at the probe's first start only; later starts probe the root bus first.)

Evidence: `E:\xhci98-vm\p44\p44-pref\w2k-leg`, `w2ka-leg`, `w7-leg`,
`w8-leg`, `w81-leg`, `w10-leg` (`serial-a.log` the install, `serial-b.log`
the cold start's readings, `serial-c.log` 8's plain start; screenshots),
the cut in `E:\xhci98-scratch\p44-p44-pref\rel2`, `xfern\` (the
batches).

### "Above 4G Decoding" on: XP, 2000, 98 SE and ME (the owner's request of 2026-10-27)

The 4G-on layout (the option on: the ASM4242 root port's 256 GB
prefetchable reserve, `-AsmReserve`, the ORICO at power-on), fresh
overlays of each q35 base, nothing of this project installed unless a
row says; the processor `pentium3,lm=on,xlevel=0x80000008,phys-bits=40`
(40 address bits) unless a row says; 2000 and XP run with
`q35-pcihost.pci-hole64-size=0` (the launcher's setting for them) unless
a row sets it. "Host window" is the host bridge's 64-bit window, QEMU's
`qom-get /machine/q35 pci-hole64-start` and `pci-hole64-end`, read 12 to
20 seconds after power-on (SeaBIOS programs it and the ACPI `_CRS`
describes it); "root port" the ASM4242 root port's prefetchable window
as SeaBIOS left it (`info pci`). On these `pentium3` bases SeaBIOS
starts the 64-bit windows at 4 GB (or just above) even with 40 bits, not
at 7F_8000_0000 as on the `qemu64` bases of "44.56".

**XP SP3 x86** (`xp4g` a and b, `xp4g-plain` c to h):

| Row | Root port | Host window | Result |
|---|---|---|---|
| a, 4G-on | 1_0000_0000-40_FFFF_FFFF | 1_0000_0000-41_0000_0000 | STOP 0xA5 (0x2, 0x867A7660, 0xE10004B0, 0x867E33C8) |
| b the reserve 4 GB (`-Pref64 4G`) | 1_0000_0000-1_FFFF_FFFF | 1_0000_0000-2_0010_0000 | STOP 0xA5 (0x2, ...) |
| c plain q35 (no USB4 model), `pci-hole64-size=1G` | - | 1_0000_0000-1_4000_0000 | the desktop |
| e plain, `3G` | - | 4 to 7 GB | the desktop |
| f plain, `4G` | - | 4 to 8 GB | STOP 0xA5 (0x2, ...) |
| d plain, `8G` | - | 4 to 12 GB | STOP 0xA5 (0x2, ...) |
| h `qemu64` (40 bits), a root port with a 1 GB 64-bit reserve, size 0 | FF_0000_0000-FF_3FFF_FFFF | FF_0000_0000-FF_4020_0000 | the desktop |

(`4095M` rounds to the same 4 to 8 GB window as `4G`: 0xA5.) The rule
every row fits: **32-bit XP stops with 0xA5, first parameter 2, when the
host bridge's 64-bit window is 4 GB long or longer, wherever it lies**;
a window under 4 GB long starts, at 4 GB as at 1020 GB [emu]. Parameter
2 of 0xA5 is ACPI's own (the BIOS "not fully ACPI compliant" text on the
screen); that a resource length of 4 GB or more is what the 32-bit
system rejects is an inference from the rows [inf]. With "Above 4G
Decoding" off (the 4G-off layout) the host bridge describes no such
window and XP starts and moves the bridges' windows below 4 GB itself
(above, "XP on the 4G-off layout, read again"). A board whose firmware
puts the windows above 4 GB with the option on gives the host bridge a
window far longer than 4 GB (as the 4G-on layout's in row a above)
[inf], so XP needs the option off there.

**2000 SP4** (`w2k4g`, `w2ka4g`, the 4G-on layout; the firmware's layout as
XP's row a: root port 1_0000_0000-40_FFFF_FFFF, host window to
41_0000_0000):

| Base | Result |
|---|---|
| Standard PC HAL | "Starting up" stops part way, as on the 4G-off layout (section "2000 and the 32-bit Vista to 10"); the root port's window reads back [0, FFFF_FFFF_FFFF_FFFF] (2000's own probe left in it), the processor running, not halted |
| ACPI HAL | STOP 0xA5 (0x2, 0x820866E8, 0x82071228, 0x1), as XP |

**98 SE and ME** (`w98se4g`, `me4g`, the 4G-on layout; root port
1_FFF0_0000-40_000F_FFFF, host window to 40_0010_0000): both reach the
desktop. `HKEY_DYN_DATA\Config Manager\Enum` (regedit's export sent to
COM1): the root port 00:03.0, the ASM4242's upstream port and its
other three downstream ports problem 0; the tunnel port 02:00.0 (the ORICO's)
problem 12, nothing behind it (no enclosure bridge, no NVMe); the
ASM4242 xHCI, the NHI and QEMU's xHCI problem 28 (no driver). The same
on both: the 4G-off layout (section "XHCIPFW.VXD through
the installer"), the windows above 4 GB again.

Evidence: `E:\xhci98-vm\p44\p44-pref\xp4g`, `xp4g-plain`, `w2k4g`,
`w2ka4g`, `w98se4g`, `me4g` (`cmdline-<row>.txt`, screenshots,
`serial-a.log`), and `notes-4gon.txt` there.

### The reviewed cut through the installer, with the option off and on (2026-10-27, evening)

The code at fcdddaa2 (the owner's Q9: the box ticked with the
Thunderbolt box; `scripts\build-driver.cmd all` passed on x86 at cfe3907d
and on amd64 at fcdddaa2), a test cut (label `p44pfw3`, into a scratch
folder; `XHCIPFW.SYS` SHA-256 `E9057A42...1B2C`, 10 041 bytes;
`XHCIPFW.VXD` `E22DA214...2E6F`, 9 877 bytes; the release images); a
fresh overlay of each system's clean q35 base for each row, nothing else
of this project's or Stor98's on it (the NVMe without a driver before the
install); the console tool from the cut (`XHCIINST.EXE /dir release-x86
/yes /nopause /thunderbolt`), its output and the readings sent to COM1;
cold starts (98 SE and ME: Shut down, then QEMU started again). `4goff`
is the 4G-off layout; "on" is the 4G-on layout (`-AsmReserve`, the 256 GB
reserve, the `pentium3` processor with 40 address bits; 2000 and XP with
`pci-hole64-size=0` as before), the ORICO at power-on in both.

| System | Install | Cold start | Result |
|---|---|---|---|
| 98 SE, `4goff` | on `4goff`: the box's line "Close PCI windows above 4 GB (XHCIPFW.VXD)", `/yes: Y`; Stor98 `/yes: Y, with the Thunderbolt support`; exit 3 | 1st: `XhciPfwResult` 1, `XhciPfwClosed` 9, `XhciPfwFunctions` 12h, nine `XhciPfwBridges` records (the root port from `FFF10001h`, 1, `40h` to `0001FFF1h`, 0, 0), `XhciPfwVersion` "XHCIPFW.VXD 3.2.0.0"; every ASM4242 bridge and both enclosure bridges problem 0, the NVMe problem 14 (Windows' hardware wizard installed "Stor98 Storage Controller for NVMe SSDs" from `C:\XHCI98`); 2nd: the same log | the NVMe's `Driver` `SCSIAdapter\0000`, `PortDriver` `stor98.mpd`, problem 0, its SCSI disk problem 0 |
| 98 SE, on | on the option-on layout, the same lines and exit 3 | the same as the row above at both starts (the same nine records: on these bases SeaBIOS leaves the same windows above 4 GB with the option on) | 02:00.0 (the tunnel port the ORICO is on) problem 0 and everything behind it: both enclosure bridges, the NVMe on Stor98 problem 0; without the package it was problem 12 with nothing behind it (above) |
| ME, on | the same, exit 3 | the same, `XhciPfwFunctions` 13h | the same as 98 SE |
| 7 SP1 x86, `4goff` | on `4goff`: the box's text with its "Above 4G Decoding" lines, `/yes: Y`; exit 2 (the root port still at problem 12 before the restart) | `XhciPfwSkipped` 0, `XhciPfwMode` 2, `XhciPfwClosed` 9, `XhciPfwHoldTicks` 426594h at 100 MHz (43.5 ms), `XhciPfwVersion` "XHCIPFW.SYS 3.2.0.0" | "ORICO (D:)" mounted; the NVMe on Stor98 (`oem6.inf`); no device with a problem but the ASM4242 xHCI (18, the restart its driver asked for) and QEMU's `fw_cfg` (28) |
| 8 x86 | on the q35 board without the USB4 model, fast startup off (`powercfg /h off`) first; `/yes: Y`; exit 3 | the spinner at the logo for 20 minutes (the time-box), the root port's window still 7F_8000_0000 to BF_7FFF_FFFF (`info pci`) | does not start, as without the package; see below |
| 2000 SP4, Standard PC HAL, on | on the q35 board without the USB4 model, `/yes: Y`; exit 3 | the desktop (Found New Hardware for the new devices); `XhciPfwSkipped` 0, `XhciPfwMode` 1, `XhciPfwClosed` 9, `XhciPfwFunctions` 13h, the root port's record as 98 SE's | every ASM4242 bridge, both enclosure bridges, the ASM4242 xHCI, the NHI and the NVMe ("PCI Device", no driver yet) in `Enum\PCI`; without the package it stopped at "Starting up" (above) |
| 2000 SP4, ACPI HAL, on | the same, exit 3 | STOP `0xA5` (0x2, 0x820864C8, 0x8201B0A8, 0x1) | as without the package |
| XP SP3, on | the same (the box ticked with the Thunderbolt box, no `/xhcipfw` needed), exit 3 | STOP `0xA5` (0x2, 0x867A7448, 0xE1392628, 0x867915C8) | as without the package |

8's start, read further: QEMU's `pci_cfg_write` trace of the same
overlay's next cold start on `4goff` holds no write by the step (the
root port's `24h`, `28h` and `2Ch` written only by the firmware and by
Windows' probe and write-back, `7FF18001h`, `7Fh`, `BFh`), where the
probe of "2000 and the 32-bit Vista to 10" above, in the same group,
wrote its closes before the PCI driver's first write. With the `qemu`
image of the same source copied over the driver, a plain start (no USB4
model) prints its line on QEMU's debug console (`mode/skip` 2 and 0,
`closed` 0; `XhciPfwSkipped` 0 there with the release image too), and a
cold start on `4goff` prints nothing and writes nothing, then spins.
So on 8 with the windows above 4 GB the step does not run to its log
line before the start stops; whether it is not reached or does not
return is not read (owed: the reads trace and the processors' state at
that start; read in "44.72": it is not reached).

So: with the option on, 98 SE and ME find the enclosure through
`XHCIPFW.VXD` as with it off; 2000 under the Standard PC HAL starts with
`XHCIPFW.SYS`; 2000 under the ACPI HAL and XP stop with `0xA5` with it as
without it (ACPI's own check of the host bridge's `_CRS`, which the step
does not change [inf]). 7 on `4goff` and 8 at a plain start read
`XhciPfwSkipped` 0, the "late" check reading the `SOFTWARE` hive
(record 32 section 11.2); 8 on `4goff` still does not start.
Record 32 section 11.3 now lists the option per system.

Evidence: `E:\xhci98-vm\p44\p44-pref\w98se-p3`, `w98se4g-p3`, `me4g-p3`
(`serial-a.log` the install, `serial-b.log` and `serial-c.log` the two
cold starts, `xferi\R.BAT` and `C.BAT`, `nvme98.py`, `dyn.py`), `w7-p3`,
`w8-p3`, `w2k-p3`, `w2ka-p3`, `xp-p3` (`serial-a.log` the install,
`serial-b.log` the cold start's readings, screenshots; 8's
`serial-c.log`, `trace-d.log`, `trace-f.log`, `debugcon-f.log` and
`debugcon-g.log`), the cut in
`E:\xhci98-scratch\p44-p44-pref\rel3`.

## 44.45 the first hot plug of a tunnelled drive on q35 (2026-10-27)

The first hot plug of the ORICO enclosure model on QEMU `q35` left the
enclosure's upstream port at problem 31 on 7, Vista, 8 and 8.1 x86
(44.32's legs, at boot start too), XP's hot plug reached no device, and
98 SE's and ME's reached no devnode (44.48's legs). The question first
asked: the platform or this project's drivers. The answer is both, in
order: three things the emulated platform did not do as an ASM4242 firmware does,
and behind them one defect of Stor98's.

### The platform: what the emulated board did not do

The ASM4242 layout of record 22 section 18.1 gives each USB4 tunnel
port of its switch 48 buses, 12 KB I/O, 384 MB memory and 128 GB
prefetchable with nothing attached; the bus count and the two memory
sizes are firmware settings on its USB4 configuration page (per USB4
port: PCIe Bus Number 16 to 56, PCIe Non-Prefetchable MMIO 256 MB to
4 GB, PCIe Prefetchable MMIO 1 to 512 GB; 48, 384 MB and 128 GB there).
The emulator did not:

1. **No bus range per tunnel port.** SeaBIOS (rel-1.17.0, the share
   dir's `bios-256k.bin`) honours QEMU's reservation capability only on
   Red Hat bridge ids (`pci_find_resource_reserve_capability`: vendor
   `1B36`, device `000C` or `0001`), and its bus counter is never read
   back from configuration space. The ASM4242 ports (`1B21:2423`) got
   what was behind them at POST: an empty tunnel port with secondary =
   subordinate (44.48's `tp1` buses 4-4). A switch plugged later had no
   bus number below it.
2. **A power controller on the tunnel ports.** QEMU's XIO3130 port has
   one, resets an empty slot to power off, and a device plugged below it
   stays unpowered (configuration reads all ones) until the guest powers
   the slot. Windows 98 SE, ME, 2000 and XP never do. The ASM4242's tunnel
   ports have none (record 22 section 18.1).
3. **No interrupt pin on the switch ports.** Every ASM4242 port and both
   ASM2464 ports have "Interrupt: pin A" (record 22 section
   18); QEMU's had none, so no Interrupt Line was written on the
   tunnel port, and the 9x worker's interrupt step (record 21 section
   6.1.1 step 5) had no known wire.

The model was changed (usb4-emu `asm4242-model`, its QEMU fork's branch
`usb4-emu`, all opt-in but item 3): `usb4-asm-usp` `fw-bus-pad=N`
renumbers the switch when SeaBIOS writes its final subordinate bus, each
port with a hot-plug slot given at least N buses inside the root port's
`bus-reserve` (`e028b6a`); `fw-io-pad`, `fw-mem-pad` and `fw-pref-pad`
lay out its windows again inside the root port's when SeaBIOS first
enables the upstream port, a subtree moving with its window (`4b495b9`);
`usb4-tunnel-dsp` `power-controller=off` (`7780a6e`); Interrupt Pin A on
every switch port (`d2443fa`); qtest `usb4-asm4242-test` 15 cases, 0
failures; documentation, `run-linux.sh` `ASM_FW=board` and the patch
export in usb4-emu `4371201`. Emulator build
`E:\xhci98-vm\q35-bases\bin\asm4242-20261027-boardint\` (QEMU 11.1.2,
`v11.1.2-246-gd2443fa`; `qemu-system-x86_64.exe` SHA-256
`deca3fcb9cc8f374a010bbbc30cb662ea4fb67bbb22db5e5cc519d3eee4979ce`). The
ASM4242 layout of these legs: the root port
`bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref32-reserve=4M,
hotplug=off` (44.48's low-window layout), `usb4-asm-usp
...,fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, both tunnel ports
`power-controller=off`. SeaBIOS's result (`info pci`): tunnel ports
03-32 and 33-62 (hex), 384 MB and 12 KB each, the xHCI on bus 63, the
host interface on 64, the next root port at 69. The prefetchable windows
stay as Linux leaves them with the option off (2 MB a port, below
4 GB): the firmware's 128 GB per port is above 4 GB, which these targets do not
address with Above 4G Decoding off; `fw-pref-pad=128G` gives it with the
4G-on layout's reservations, not read on a target.

### The driver: Stor98's 9x scan stopped at bus 15

With these ranges, 98 SE numbered the hot-plugged chain inside
port 2's range (`cm.brnum.9x.port-buses=00623302`, the bridges on buses
33h and 34h, the drive on 35h, its line written,
`cm.brnum.9x.irq=00010A01`), Windows installed both bridges and Stor98,
and the drive stayed at problem 10. Stor98's `HwFindAdapter` on 9x is
the NT4-ABI scan, seeded at bus 0, which stopped after bus 15 (the
base's bound): a debug-port Stor98 traced `DriverEntry` and the
calibration and no find-adapter stage. In record 22 section 18.1's layout the USB4
ports' ranges are 19h-48h and 49h-78h, so the same would hold there, at a
cold boot too [inf: the scan's bound against those ranges; read only in
the emulator]. Fixed in `cd1865db` (reviewed and revised in `00440095` and
`fec5574b`): the scan walks buses 0 to 255, probing each slot's vendor
id with a 4-byte read first (`src\stor98\src\pciscan.h`, host unit test
`pciscan_ut`); Stor98's PLAN 3.4 and its release history say so. The
same debug image with the fix: found at 35h, `E:` mounted; the scan from
bus 0 to it took about 5 ms. Gated builds of `fec5574b`: x86 and amd64
passed (`out\build-4445-x86-2.log`, `out\build-4445-amd64.log`);
release 9x image SHA-256 `58D16014...050731`.

### The legs (q35, the ASM4242 layout above)

Package: test cut p44storK (`7ee3a12e`, `rel46`, the version reading
3.2.0.0), installed by `INSTALL.BAT /console /yes /nopause /thunderbolt
/stor98` (on 9x `XHCIINST /dir release-x86 /yes /nopause /thunderbolt
/stor98`); the enclosure (`usb4-device ... device-model=asm2464`)
hot-plugged on USB4 port 2 by QMP `device_add` after the desktop. Stor98
on 98 SE and ME: the fixed release image of `fec5574b` copied over the
installed or staged one. 9x guests one vCPU, the rest two.

| Leg | System | Result |
|---|---|---|
| A, the control | 7 x86 | the same installed overlay and root port, no pads, the power controller kept, on the build of `7780a6e` (`asm4242-20261027-board`, which has no interrupt pins either): the enclosure's upstream port `PCI\VEN_1B21&DEV_2463` at **Code 31**, as 44.32 read. Leg B differs in all three (pads, slot, pins), so it does not separate them |
| B, first plug | 7 x86 | the same installed overlay with the ASM4242 layout: "ORICO (D:)", the drive on Stor98 (`Service` stor98, Code 0), no device with a problem code; the id first in `CompatibleIDs`, `setupapi.dev.log` "Matching DeviceID=pci\xhci98_tunnel&cc_010802" (`oem6.inf`); the mark service "mark written: ... (member 0208, unit 33:00.0)" then "a Windows installation is pending": **pass** (T.3's first plug on 7: the bridges' first-time installs and the drive, at the first plug) |
| T.11 re-plug | 7 x86 | `XhciCmSafeEject` 0 machine-wide, a cold boot, the enclosure plugged again: the drive tagged (the id first) and on Stor98, "ORICO (D:)"; the mark service "notice not acted on: member 0208 unit 33 serial 1: safe eject is off for the domain": **pass** |
| first plug | Vista x86 | "ORICO (D:)", Stor98, no Code 31: **pass** (the only problem device the base's SM Bus controller) |
| first plug | 8 x86 | "ORICO (E:)", Stor98, no problem code: **pass** |
| first plug | 8.1 x86 | "ORICO (E:)", the drive on Stor98, no problem code: **pass** |
| hot plug, the switch shape | 10 x86 | `stornvme`, "ORICO (E:)", no problem code: as designed (record 31's T.2 is the direct shape, which the q35 enclosure is not) |
| hot plug | XP | after the install completed: "Found PCI\XHCI98_TUNNEL&CC_010802 in ...\oem4.inf" (`setupapi.log`), the wizard installed Stor98, "ORICO (D:)": **pass**. An earlier plug on the same overlay found nothing below the port: the install had stopped at a wizard (exit 4, `XHCIINST.LOG`), so the filter was on no ASM4242 port (`xhcicm.sys`'s member flags `11`, no filter published) and nothing asked XP to enumerate the switch, a leg-handling error, not the driver's; once the run finished (exit 3) all four ports carried it |
| T.3 first plug, unit restart | 2000 | the mark service's own restart off (`Restart` 0): a first wizard for a "PCI Device" that found no driver (skipped), then "Stor98: ... its install set to run again before its unit's restart (flags 00000040 -> 00000020)", "restart made: PCI\VEN_1B21&DEV_2463 ... so that its drives come back through the filter chain with the tunnel id", and Windows' wizard named the drive "Stor98 Storage Controller for NVMe SSDs" from `oem4.inf`: **pass** for the unit restart; the wizard then asked for a restart (see below) |
| re-plug after the restart | 2000 | **open**: the drive on Stor98 (`ActiveService` stor98, the id first) at problem 10, "Resources: none allocated", its requirement 16 KB of memory and no IRQ; no disk below it; the same on port 1 (the wizard again "will not work until you restart"). At a cold boot with the enclosure present the drive asked for and got IRQ 11 and its disk came up as "ORICO (E:)" |
| T.4 plug | ME | the bridge and Stor98 wizards at the first plug (no restart asked), "ORICO (E:)" with its file; the chain numbered inside port 2's range (`port-buses=00623302`, `irq=00010A01`): **pass**. ME's Shut Down then powered off |
| T.4 plug | 98 SE | the same with the release image: "ORICO (E:)": **pass**; on the debug image a re-plug gave the id (`cm.reenum.9x.id.given=00000001`) and the no-IRQ form ran |

**Verdict.** The Code 31 on 7, Vista, 8 and 8.1 x86 and the missing
devices on 98 SE and ME were the emulated platform's (items 1 to 3
above); with the ASM4242 layout's reservation and slot they are gone, and the
legs read the drive at the first plug. XP's leg here read its missing
device as an install stopped at a wizard (the XP row); 44.32's XP legs,
on the old layout, are not re-read, so how much of their failure was
the platform's is not separated [inf: the same items 1 and 2 applied
there]. Behind them Stor98's 9x scan
missed a drive above bus 15, fixed here. One case stays open on 2000: a
hot-plugged drive behind the enclosure's switch is offered no interrupt
(`pci.sys` under the Standard PC HAL routes a hot-added function through
the BIOS's table, which covers bus 0 [inf]) and Stor98's start fails
there, where the same drive at a cold boot is given IRQ 11. On QEMU `pc`
(44.3) the drive sat directly on the tunnel bridge and started at the
plug. Whether an ASM4242 firmware's CSM table covers the hot-plug ranges is not
known; the owner's decision what to do with it (below).

Owed: the x64 systems' hot plug (not read here; the x64 package would be
signed in the guest as 43.36 did), T.2's hot plug in the direct shape on
q35 (the q35 enclosure is the switch shape), T.3's refusals and vetoes
and, on 7 and 8.1, the service's install within seconds of the first
plug as record 31 section 9.2 states it (here 7 bound Stor98 through
Windows' own install, the service logging the installation pending,
and 8.1 ended on Stor98 with the steps not separated),
Q2 on XP (the drive was installed at the plug through the id, so no
driverless drive was left for the unit restart), 2000's hot-plug start
(above). Logs and screenshots: `E:\xhci98-vm\p44\p44-hotplug\` (per
guest folder, `pids.txt`).

## 44.50 Windows 2000's hot plug after a restart: where the start fails (2026-10-27)

44.45 left one case open: on 2000 a tunnelled drive hot-plugged behind
the enclosure's switch stays on Stor98 at problem 10, "Resources: none
allocated", its requirement 16 KB of memory and no IRQ, while the same
drive present at a cold boot is given IRQ 11 and mounts. A bounded read:
the debug-port traces of the two, the polled path, and the BIOS's
interrupt routing only if the miniport is never reached.

**Setup.** QEMU `q35`, the 2000 base `E:\xhci98-vm\q35-bases\w2k\`
(Standard PC HAL), an overlay of 44.45's 2000 leg after its cold boot
(test cut p44storK installed, the drive's devnode on Stor98), one vCPU,
512 MB, emulator build `asm4242-20261027-boardint` with 44.45's
ASM4242 layout and `-device usb4-debug-port` beside the `0xE9`
debugcon. Stor98 in the guest: the x86 free image of `078e3d4c` built
with `STOR98_DBGPORT=1` (`src\stor98\scripts\build.cmd x86 free`;
SHA-256 `5263B457...EEA116F9D2`) copied over
`system32\drivers\stor98.sys`. The enclosure (`usb4-device ...
device-model=asm2464`, `port=1`) plugged by QMP `device_add` at the
desktop; its chain came up on buses 3 to 5 inside the first tunnel
port's range (the switch's upstream port, its downstream port, the
drive at 05:00.0).

| Leg | Result |
|---|---|
| a, hot plug after a restart without the enclosure | the debug port: `01` (`DriverEntry`) and nothing after it: no find-adapter stage (`02`), no BAR (`05`), no poll decision (`0x152`, `0x153`), no `HwInitialize` (`30`). The System log: source stor98, event 13, "The driver was configured with an incorrect interrupt for \Device\Scsi\stor981." (data: major function `1Bh`, `IRP_MJ_PNP`; error `C004000Dh`, `IO_ERR_INCORRECT_IRQ`; unique value `418h`). The drive's configuration space read through the monitor: command `0000h`, BAR0 `00000004h` (64-bit, no address), Interrupt Pin 1, Line 0. Device Manager: "This device cannot start. (Code 10)", "Location: PCI Slot 2 (PCI bus 5, device 0, function 0)" |
| b, cold boot with the enclosure present | `01`, `04`, `02` `00050000` (bus 5, slot 0), `05` `00040005` (the port's range, 64-bit, 16 KB), `0x153` `000B000B` (the port gave level and vector 0Bh), `0x152` `0B000B01` (pin 1, line 0Bh, interrupt-driven), `03` found, `30` to `32`, bring-up done (`43`), I/O |
| c, as a, with a test image whose `HW_INITIALIZATION_DATA` has `HwInterrupt` NULL (SHA-256 `CD9C3DB6...ECD5E0945`, not kept) | the same: `01` only, event 13 with unique value `418h` |

So the miniport is never reached: on this guest (q35, Standard PC HAL)
SCSIport failed the PnP start of an adapter given no interrupt resource
before it called `HwFindAdapter`, whatever the miniport's `HwInterrupt`.
Stor98's no-IRQ polled path (`PollDecide`, reason "no port IRQ") sits in
`HwFindAdapter`, so no setting of Stor98's can affect this failure;
`storpollmode=1` was not set, because it is parsed there too. Other 2000
configurations are not read. Stor98's PLAN, "Polling mode", records
the finding. Nothing in Stor98 to fix.

**The interrupt routing.** At POST SeaBIOS writes the Interrupt Line of
every function it finds (the drive's 0Bh at the cold boot, read as IRQ
11); a function the guest's own enumeration finds later keeps Line 0.
SeaBIOS's `$PIR` table (F000:55A0, read with `pmemsave`) has six entries,
bus 0 devices 1 to 6 (the switch's root port, 00:03.0, is slot 2, which
Device Manager shows as the drive's location), links 60h to 63h, IRQ map
`DEF8h`, and names its router as 8086:122E (a PIIX3) at 00:01.0, where
the q35 machine has its VGA (1013:00B8); the q35 chipset's interrupt
router is the ICH9 LPC at 00:1F.0. A possible explanation [inf]: the
table names a router this machine does not have, 2000 routes nothing
through it, and the IRQ it gives is the Line SeaBIOS wrote (the cold-boot
IRQ equals the Line; the hot-added function's Line is 0 and it got none).
Whether 2000 used the table at all was not read. A try to show it directly (the monitor writing Line 0Bh
into the drive after the plug, then Device Manager disabling and
enabling the drive, then the bridge above it) did not settle it: the
drive stayed at Code 10, and after the bridge's restart its Line read 0
again.

**Verdict.** Not Stor98's. On this 2000 guest (Standard PC HAL) a
function hot-added behind the tunnel port was given no interrupt, and
SCSIport refused to start the miniport without one. The emulated platform differs
from a board in its `$PIR` (a PIIX3 table on an ICH9 machine); whether
a real ASM4242 firmware's CSM table and 2000 together route a hot-added
function is not known (no such `$PIR` has been read). A candidate workaround, not
shown to work and needing its own investigation, is in the connection
manager on 2000 (giving the new functions their wire, as the 9x worker
does by record 21 section 6.1.1 step 5); the other course is a stated
limitation; the owner's decision. Evidence:
`E:\xhci98-vm\p44\p44-w2khp\` (`w2k\debugcon-a.log`, `-b.log`, `-c.log`,
`w2k\infopci-a-plug.txt`, `w2k\a4.png` and `c2.png` the events,
`w2k\b11.png` the Code 10, `pids.txt`).

## 44.45 (continued): the owed legs, 2026-10-27

The legs 44.45 left owed, on QEMU `q35` with its ASM4242 layout
(emulator build `asm4242-20261027-boardint`; the root port
`bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref32-reserve=4M,
hotplug=off`, `fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, both tunnel
ports `power-controller=off`). The build: `6b35a399` (`078e3d4c`'s code;
44.50 changed documents only), gated `release` builds x86
(`out\build-x86.log`) and amd64 (`out\build-amd64.log`) passed. Packages:
an x86 test cut by `make-release.ps1 -Arch x86 -Flavor release -TestLabel`
(without the driver packs and the DOS qualifier, which this worktree does
not have; `xhci98.sys` SHA-256 `15927FAE...`, `stor98.sys` `F873BE59...`),
and the amd64 package staged unsigned by `make-package.ps1 -Arch amd64`
(`xhci98.sys` `1FC238F6...`, `stor98.sys` `646C6706...`), its catalogs made
unsigned with WDK 7.1's MakeCat on the host, then the drivers and catalogs
signed inside the XP x64 guest with a throwaway certificate made there and
trusted in its machine Root and TrustedPublisher stores (as 43.36 and
44.3 did; nothing signed on the host). Installed on the two XP systems by
`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98` on fresh
overlays of the q35 bases; on both XP systems the first run stopped at
Windows' own wizards for the new root hubs (exit code 4), and the second
ended with exit code 3. The mark service's own restart was left on
(`Restart` 1, the default). Two vCPUs each.

| Leg | System | Result |
|---|---|---|
| T.2, the direct shape, hot plug | 7 x86 | 44.45's 7 x86 overlay (its test cut p44storK installed, not this round's build); a `usb4-device` with no `device-model` puts the NVMe directly on the tunnel port's bus (`tp0-nvme1`, bus 3, IRQ 23): `CompatibleIDs` begin with `PCI\XHCI98_TUNNEL&CC_010802`, `setupapi.dev.log` "Matching DeviceID=pci\xhci98_tunnel&cc_010802" (`oem6.inf`), `Service` and `ActiveService` stor98, "ORICO (D:)" mounted: **pass** |
| T.2, the direct shape, hot plug | 2000 | 44.50's overlay (p44storK, Stor98's debug-port image of 44.50): the drive found through the id (the wizard named it "Stor98 Storage Controller for NVMe SSDs" and installed it from `oem4.inf`), then "The hardware you installed will not work until you restart your computer"; the debug port shows `DriverEntry` only, consistent with 44.50's failure before the find-adapter stage [inf: the interrupt resources and the System log were not read in this leg]. The warm restart that followed stopped before Windows (a blank screen, the CPU looping in the loader's segment), so the guest was stopped; not read further |
| T.3 first plug, x64 | XP x64 | the ORICO model plugged on USB4 port 1 after the restart: "ASMedia 246x connected", the bridges installed by Windows, a wizard for "PCI Device" (the drive, no id) answered with its defaults (no driver found, "Don't prompt me again" ticked); the mark service: "mark written ... (member 0200, unit 03:00.0, serial 1)", "restart not made yet ...: an installation is pending", then its own "restart made: PCI\VEN_1B21&DEV_2463 ... (serial 1)"; the drive came back with the id first, `ConfigFlags` 0x40, no driver and no wizard; the service: "carries the tunnel id and has no driver: Windows' wizard offers Stor98 for it; left to the user". A re-plug: the mark written again (serial 2), the drive tagged, `ConfigFlags` still 0x40, no wizard. **fail** (below) |
| T.3 first plug | XP | a dock (`usb4-device ... dock=on`: switch, NVMe, NIC, xHCI) plugged after the restart: the switch's upstream port and its first downstream port installed, the other two downstream ports at Code 12 (so the NIC and the dock's xHCI were not enumerated), the drive at Code 28; the mark service the same as on XP x64: the mark's restart made after the bridges' installs, the drive back with the id, `ConfigFlags` 0x40, no wizard, "left to the user". **fail** (below); the refusal it was set up for not reached |

**The finding.** On both XP guests, with the mark's restart on (the
default), a never-seen switch enclosure's drive stayed driverless after
the mark's restart (Code 28 on XP), with the tunnel id and no new wizard
offering Stor98 (the first wizard, for the drive without the id, came
before it); on XP x64 a re-plug left it the same. Record 31 section 6.2 counts a mark restart made after the
bridges' installs as the tag's restart ("a drive with the id is no
candidate here, so no second restart follows"), but only the tag's own
path first sets the drive's install to run again (`CONFIGFLAG_FAILEDINSTALL`
cleared, `CONFIGFLAG_REINSTALL` set). The drive's first wizard, answered
with its default "Don't prompt me again", left `CONFIGFLAG_FAILEDINSTALL`,
and after the mark's restart Windows asks nothing, as section 6.2 itself
records for the tag's restart without the flags (read on XP, 2026-10-25).
The service then reads the id with no driver as the user's refusal. 44.45's
own T.3 leg on 2000 ran with `Restart` 0 and passed through the tag's path;
its XP leg had the bridges installed earlier. Not this round's area (the
mark service); for the coordinator.

**Not read, owed:** T.3's refusal (a unit with a running function: here
the dock's xHCI and NIC never enumerated, their ports at Code 12 inside
the ASM4242 layout's port window, and the mark's restart came first), the veto
(an open handle on the unit), two drives below one unit sharing one
restart, a declined wizard left alone, and on 7 and 8.1 the service's
install within seconds; the first plug on the other x64 systems (Vista,
7, 8, 8.1 and 10 x64). Evidence: `E:\xhci98-vm\p44\p44-w2khp\` (`w7\`,
`w2k\` legs d, `xp\`, `xp64\` with `serial-*.log` and the wizard
screenshots, `pids.txt`).

## 44.23 (continued): 8.1's case 3b after the restart, and the enclosure across a restart (2026-10-27)

**The emulator.** The request (`.claude/usb4-emu-requests.md`, last
entry: "the emulated USB4 enclosure must reconnect across a guest
restart") was read before any change. A guest restart is a machine
reset in QEMU. On q35 with the emulator's ASM4242 model (the emulator's asm4242
preset, whose pre-OS connection manager is on by default) the enclosure
comes back at every reset: the model's firmware connection manager
tunnels it again before SeaBIOS enumerates, whether it was present at
power-on or hot-plugged later. Read on 8.1 x86 with `usb4-emu` builds
`d2443fa` (`bin\asm4242-20261027-boardint`, `qemu-system-x86_64.exe`
SHA-256 `deca3fcb...`) and `e05bf66` (`bin\asm4242-20261025-2230`,
`da60139f...`), below. The "does not reconnect" of the 44.3 legs was the
Alpine Ridge profile on QEMU `pc` with `preboot=off` (`legs\kr81`):
there the model makes a machine reset a power-on of the controller, and
a USB4 device that no pre-boot approval tunnels falls back to USB before
the OS, as the ThinkPad P14s does at power-on with pre-boot off
(`usb4-emu` `docs/ICM-PROTOCOL.md` section 13.1, R25, R33). Read with a
`system_reset` on q35 (`tb3ar`, the ORICO model at power-on, no guest
driver): with `preboot=off` the enclosure is a USB disk (`ASM246X`) on
the controller's xHCI after the reset and no tunnel exists; with
`preboot=on` the drive is behind the tunnel port before and after the
reset. No emulator change was needed; `usb4-emu` `88c34d3` records it in
`docs/XHCI98-HANDOFF.md` section 6. The same sentence in "The q35
re-runs" above ("the enclosure does not reconnect across a guest restart
on 8.1 and 10") is not borne out on q35: those legs took their readings
after cold restarts, and their one warm-restart reading (10 x86, case
3b) was stopped before it was taken (the worker's result record), so the
sentence carried over the QEMU `pc` observation; with the same model and
builds the enclosure reconnected in every warm restart below.

**The vehicle.** QEMU `q35` from the q35 base `w81x86` (`disk.qcow2`
SHA-256 `9714c3d8...`), a fresh overlay (`E:\xhci98-vm\p44\p44-emu5\w81`),
the launcher's ASM4242 model (`-Usb4 asm4242 -EncAtBoot -Tablet`, no
board padding), the ORICO model (`asm2464`) with the FAT NVMe on USB4
port 1 at power-on. Package: test cut K of `7ee3a12e`
(`E:\xhci98-scratch\p44-p44-stor\rel46`, zip SHA-256 `3a5fca9f...`,
`release-x86\xhcicm.sys` `02d3dee0...`, `stor98.sys` `47d9f299...`,
`stor98.inf` `3b793bf3...`, `XHCIINST.EXE` `074a9f66...`). Code an NT
guest runs did change between `7ee3a12e` and `078e3d4c` (Stor98's
`HwFindAdapter` gained 44.45's PCI probe; 44.47 changed the connection
manager's power path); these readings were made on cut K and not
repeated on `078e3d4c`. The
guest steps are the N5 kit's `leg.ps1` (readings to COM1).

| Step | Result |
|---|---|
| Before the install (`rd_a`) | the tunnelled drive `PCI\VEN_1B36&DEV_0010...\8&13AE00C&0&000000000018` on `stornvme`, problem 0; no package |
| `INSTALL.BAT /console /cmonly /stor98 /yes` (`cms_a`), case 3b | exit 3 in 1730 s (the publisher prompts answered by a click); "Installing Standard NVM Express Controller ... done, restart needed", "Stor98 Storage Controller for NVMe SSDs: stor98 3.2.0.0, started", "Stor98 now runs Stor98 Storage Controller for NVMe SSDs: recorded as this installer's"; the drive on `stor98`, problem 0; then the installer's restart (`shutdown /r /t 20`, a warm restart) |
| After the installer's restart (`rd_b`) | the same drive on `stor98`, running, problem 0, its disk present, "ORICO (E:)" mounted (the `stornvme` service still running, no device listed on it); the NHI on `xhcicm`, the mark service running, its mark written. **Pass** |
| A second warm restart with the connection manager running from boot (`restart`, `rd_c`) | the same: `stor98`, problem 0, `stornvme` stopped, "ORICO (E:)". Pass |
| The same overlay on the `e05bf66` build, cold start then a warm restart (`rd_d`, `rd_e`) | the drive on `stor98` both times |
| The enclosure removed (`device_del`) and plugged again (`device_add`), then a warm restart (`rd_f`) | the enclosure's switch and drive back after the restart (QEMU `tp0-sw2-nvme`), the drive on `stor98`, problem 0, "ORICO (E:)" |

Evidence: `w81\serial-a.log`, `serial-b.log`, `trace-a.log` (the shutdown
path: the connection manager stops its rings, the tunnel is not torn
down, and the reset's pre-OS connection manager builds it again),
screenshots `w81\s14.png`, `s17.png`, `b6.png`.

**Verdict.** 8.1 x86's case 3b holds across the restart on q35: the
per-drive install's Stor98 is still bound after the installer's warm
restart and after a second one. Not readable here: whether a later
`stornvme` mends the hang at an unplug; the 8.1 image carries `stornvme`
6.3.9600.16421 alone and nothing can be fetched. A real 8.1 machine
stays NVME81.1 in "Planned work".

## 44.17 (continued): the USB4 profile under the root hot-plug modes, on q35 (2026-10-27)

**The emulator.** The request ("root-hotplug on a usb4-generic host",
`.claude/usb4-emu-requests.md`) was met on i440FX by `usb4-emu` QEMU
`bf83b8f` (2026-10-25): a `usb4-generic` host behind the root-bus
`usb4-tunnel-bridge` registers its power domain, so `present` and every
`root-hotplug` mode act behind it. The legs now run on q35 (the owner's
machine rule), where the controller sits below a PCIe root port and
nothing modelled the modes. `usb4-emu` QEMU `47081be`, `4a0f092` and
`fa428b4` (outer `88c34d3`, `3fd53bd`) move the domain into a shared
`usb4-root-domain.c` (the i440FX bridge unchanged) and give the q35 root
port type `usb4-tunnel-port` `root-hotplug=none|restore|native`,
`present` and `root-slot`:

- `restore`: nothing is told; on the return the configuration of every
  function below is written back, on q35 including the PCIe control
  registers (a tunnel port's Slot Control among them) and the MSI and
  MSI-X programming. For guests without PCIe native hot-plug.
- `native`: the root port's own hot-plug slot reports the departure
  (presence and link down) and the fresh arrival (presence and link up),
  as a root port does. For guests with PCIe native hot-plug.
- There is no `acpi` mode on q35: the ICH9 has no ACPI hot-plug on the
  root bus. 2000 with ACPI, the guest that would need it, does not run on
  q35 here either (below).

Reviewed and revised; the `usb4` qtests pass (13 suites, 225 cases, three
new). Build: `E:\xhci98-vm\q35-bases\bin\asm4242-20261027-roothp3`
(`qemu-system-x86_64.exe` SHA-256 `94c75112...`). The launcher option
`-AsmRootHp restore|native` (the worker's copy) puts
`usb4-tunnel-port,...,usb4-dvsec=off,power_controller_present=off,root-hotplug=<mode>`
in place of the `pcie-root-port` above the ASM4242 switch.

**The vehicle.** QEMU `q35` from the q35 bases, overlays under
`E:\xhci98-vm\p44\p44-emu5\` (`xpr`, `w7n`, `w98r`), the ASM4242 model
(no board padding) with the ORICO model and the FAT NVMe on USB4 port 1
at power-on. Packages: XP and 7, test cut K of `7ee3a12e` (44.23's
vehicle; code an NT guest runs changed up to `078e3d4c`, see "44.23
(continued)", and these readings were not repeated on it); 98 SE, a
test cut of `078e3d4c` (it needs 44.45's Stor98 9x bus scan), below. The
departure is QMP `qom-set /machine/peripheral/asmrp present false`, the
return `true`. Readings: `XHCIINST /devices` and `XHCITR /report` to COM1
(`RN.BAT`, `R9.BAT`); data: `CHK.BAT` (a 1 MiB file written to the
volume and compared, and one written before the departure compared).

| Guest | Mode | Read | Verdict |
|---|---|---|---|
| XP SP3 (`xpr`) | `restore` | departure 10:02:21: the connection manager contained (`cm.live.state` `0x107`, `u4.contained`, `u4.contained.lost-up` 0, Liveness "contained"; its counters `u4.drv.contained` 1); XP's PnP logged the enclosure's two bridges, Stor98 and the disk as "disappeared from the system without first being prepared for removal" (event 12, 10:02:20); "Delayed Write Failed" for D: at 10:02:34 (event 50): `PRE.BIN`, written 80 s before the departure, was not on the volume afterwards. Return 10:03:49: `u4.recover.ok` 1, `committed` 1, `preserved` 1, Liveness "answering", the device approved, Stor98 and the disk started, "ORICO (D:)", a new write clean | pass (containment and 42-A.10's recovery); the write still cached at the departure lost |
| 7 SP1 x86 (`w7n`) | `native` | departure 10:08:40: 7 removed the controller's tree through the root port's slot (`XHCIINST /devices`: no controller driven by this package); return 10:09:40: a new instance of the connection manager bound, the device approved, PCIe up, Stor98 and the disk started, "ORICO (D:)"; `PRE2.BIN` (written 90 s before the departure) intact, a new write clean | pass |
| 98 SE (`w98r`) | `restore` | departure 10:37:19: contained (`cm.live.state` `0x107`, `u4.contained`, Liveness "contained"); the 9x unavailable route removed the three members below the tunnel port (the enclosure's two bridges and Stor98: `cm.reenum.9x.unavailable.members` 3, `query-remove` 0, `remove` 0). Return 10:38:44: `u4.recover.ok` 1, `committed` 1, `preserved` 1, Liveness "answering", the device approved, PCIe up; but the drive came back as "Stor98 Storage Controller for NVMe SSDs - stor98, problem code 14" (restart needed) and E: was gone ("Invalid drive specification"). After a cold start (the drive on Stor98 again), first a control: the enclosure alone removed and plugged again (`device_del`, `device_add`): Stor98 started, "ORICO (E:)" back, `PRE.BIN` and a new write clean; then the departure (10:49:26) and return (10:50:07) again: problem 14 again. A re-plug after the failure was not read | pass for the containment and 42-A.10's recovery; **the drive does not return after the recovery** (a cold start brings it back; a product reading, below) |

Evidence: `xpr\serial-b.log`, `xpr\trace-b.log`, `w7n\serial-c.log`,
`w7n\trace-c.log`, screenshots `xpr\b2.png`, `w7n\c3.png`; `w98r\serial-c.log`, `w98r\serial-d.log`, `w98r\c3.png`.

**2000 with ACPI.** Not readable: no Windows 2000 install with the ACPI
HAL boots on q35 here (worker `p44-legs`: the `win2k-smp` image hangs in
the loader on q35), and 2000 runs on q35 only (owner, 2026-10-27). The
2000 drive after an ACPI re-arrival (its tunnel bridge at Code 12 in the
i440FX leg of 2026-10-25, `p44-emu\a2k1`) was read again from that
leg's record: after the fresh arrival the tunnel bridge `02:01.0` asked
for 4096 I/O ports, a 2 MB memory and a 2 MB prefetchable window and got
none ("Resources: none allocated", problem 12), while the controller's
other functions (the host interface among them) were placed and started,
and the connection manager approved the device and brought the tunnel
up. The windows a freshly arrived bridge needs come from the root
bridge's windows, which the firmware sized at power-on and which stay;
2000 does not move a parent bridge's windows (XP placed the same tree,
no Code 12). This driver assigns no bridge resources on NT, so the most
likely cause is the platform's (the root bridge's windows), not this
driver; not read again, owed with a 2000 ACPI base on q35.

**The 98 SE drive after the recovery.** With the ASM4242 model the
tunnel ports are part of the controller, so the drive leaves with it
(on the i440FX `usb4-generic` vehicle of 2026-10-25 the tunnel bridge
stood outside the gated tree and the drive stayed). The connection
manager's 9x containment removes the members below the tunnel port by
the unavailable route (no query-remove, no remove), and on the return
the re-enumerated Stor98 devnode is at problem 14 and the volume does
not return (a cold start brings it back; a re-plug after the failure was
not read), while a plain unplug and re-plug of the enclosure (the normal
removal), read on the same boot before the departure, works. Which side should change (the 9x unavailable
route for members that have left with the controller, or Stor98's 9x
start after such a removal) is not decided here: reported to the
coordinator with this reading, not changed. The 98 SE package for this
row: a test cut of `078e3d4c` (`p44emu5`, zip SHA-256 `c71bd574...`,
`release-x86\xhcicm.sys` `80ec7282...`, `stor98.mpd` `d2ca8e47...`,
`stor98.sys` `9dc7501b...`, `xhci98.sys` `3a1837c7...`), installed with
`XHCIINST /dir release-x86 /yes /nopause /thunderbolt /stor98` (exit 3)
and a cold start.

**Verdict: partial.** The emulator models the root hot-plug modes for a
`usb4-generic` host on q35 (`restore`, `native`); the USB4 profile's
containment and 42-A.10's recovery pass on XP (`restore`) and 7
(`native`), and on 98 SE (`restore`) up to the drive, which does not return
after the recovery (a cold start brings it back). Owed: 2000 with ACPI (no q35 base yet; on q35 it would
run with `restore`, the q35 port having no `acpi` mode), and the 98 SE
drive after the recovery (above).

## 44.11 the two audio packages staged on 98 SE and ME: the legs on q35, and the device by a simulator (2026-10-27)

The legs the section "44.11 the two audio packages on `DevicePath`"
above left owed, on branch `p44-stage` from `078e3d4c`. Development host
A, headless, TCG, one processor and 512 MB per guest; keys by QMP
`send-key`, screens by QMP `screendump`, readings over COM1 (`regedit
/e` of `CurrentVersion`, `Software\xhci98`, `Enum\USB` and
`Class\MEDIA`). Kit `E:\xhci98-vm\p44\p44-stage` (shots `me-*`,
`w98q-*`, `p98-*`; readings under `evidence\`; `pids.txt`; not files a
clone has).

### The pack: ME now offers `uac2`, and only `uac2`

- The pin is usb-wdm-free `v0.1.3` (`062989b3`). Its `STAGE.TXT`:
  `uac2` `STAGE | W98SE WME` for `free/audio/uac2/w98se-me/uac2wdm.inf`
  (its ME line added after 44.34's ME leg); `usbmidi` `STAGE | W98SE`
  for `w98se\usbmidi.inf` and `COPY | WME` for `wme\usbmidi.inf`, whose
  only model is `USBMIDI_BY_HAND` (ME's own `usbaudio.sys` serves USB
  MIDI), so a `STAGE` line there would match no device. Nothing to ask
  of the pack: ME offers `uac2` alone, as its lines say.
- Found on ME and fixed (`d2a24a18`, reviewed and revised): the console
  refused `/wdmfree:stage=audio/usbmidi` with "only free audio/uac2 and
  audio/usbmidi are staged there", naming as staged the package it
  refused. A package on the installer's list whose set stages nothing
  for this system is now refused with "its set stages nothing for it on
  this Windows, so it is copied, not staged (use /wdmfree:copy, then
  point the hardware wizard at the package's folder)"; record 26 section
  6.7.12 says so. Also corrected (`1c0eaeb7`, `9963c631`): the readme's
  switch paragraph still said every stage switch is refused on 98 SE and
  ME; record 26 and `test_inst` named `usbmidi`'s 98 SE folder
  `w98se-me` (87 characters); it is `w98se`, 84.

### The vehicle

- The gated `release` x86 build of `p44-stage` at `d2a24a18` ("BUILD +
  GATES PASSED (release)"; `test_inst` 4241 checks, 0 failures; host
  tests passed), staged by `make-release.ps1 -Arch x86 -Flavor release
  -TestLabel "44.11 legs, not for release" -SkipQualtool -NoDriverPack
  -NoNonFree -WdmFreeRoot <a usb-wdm-free tree at v0.1.3>`; the `-free`
  download unzipped as `D:\PKG` on a VVFAT drive. SHA-256 prefixes:
  `XHCIINST.EXE` `99041f3f`, `XHCIWIZ.EXE` `30a1762d`, `xhci98.sys`
  `8d374612`, `uac2wdm.inf` (`w98se-me`) `8d0de7c4`, `usbmidi.inf`
  (`w98se`) `c4beb3fe`, `STAGE.TXT` `9b41f6be`. The ME install, X4 and
  MIDI rows and the 98 SE device rows ran on the same tree at
  `9963c631` (the refusal text the only difference; `XHCIINST.EXE`
  `3cadecbc`); the ME refusal was read again on `d2a24a18`.
- The device: QEMU models no USB Audio Class 2.0 or MIDI device of its
  own (its `usb-audio` is Audio Class 1.0), and `usb-host` passthrough is
  not used. usb-wdm-free's own simulators model both over `usb-redir`, a
  socket on the host and no physical device: `uacemu.py --profile
  x4full` (the captured Sound Blaster X4 layout, `041E:3278`: HID at
  interface 0, CDC ACM at 1 and 2, the UAC 2.0 function at 3 to 6) and
  `midiemu.py` (`acms`: AudioControl and MIDIStreaming, `FFFF:0001`;
  `lone`: one MIDIStreaming interface, `FFFF:0003`), copied from
  usb-wdm-free's tree at `v0.1.3`.
- The machines: 98 SE and ME on q35 from the q35 bases. The emulator
  snapshot the bases name (`asm4242-20261025-2230`, QEMU 11.1.2) is built
  without `usb-redir`; the stock QEMU 11.1.0 that has it starts ME's q35
  base but hangs 98 SE's at the start-up logo (12 minutes, the processor
  busy; the same was seen by an earlier worker). So: ME on q35 with the
  stock QEMU (`-Emu`), installer rows and device rows; 98 SE's installer
  rows on q35 with the bases' emulator; 98 SE's device rows on QEMU's
  i440FX (`pc`) with the stock QEMU, p43-5's pristine 98 SE, its
  first-start wizard for the `qemu-xhci` finished with no driver.

### ME (q35, stock QEMU 11.1.0, `piix3-ide` system disk): PASS

| Step | Result |
|---|---|
| `XHCIINST /dir release-x86 /list /wdmfree:copy /wdmfree:stage=audio/uac2` | exit 0, "/free: copied without asking, these packages staged: audio/uac2" |
| `... /wdmfree:stage=audio/usbmidi` | exit 1, refused; on `d2a24a18`: "REFUSED: free:audio/usbmidi: its set stages nothing for it on this Windows, so it is copied, not staged ... Nothing was changed." |
| `... /wdmfree:stage=net/cdcnet` | exit 1, refused (off the list) |
| The window, Free USB drivers page | under "Free drivers: Audio" one box, "USB audio (uac2): USB Audio Class 2.0 sound cards and DACs", open once the set's box is ticked; its details "Stages for this Windows: uac2wdm.inf ... C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me"; the Summary listed it (`me-w6`, `me-w7`; cancelled there) |
| `... /yes /nopause /wdmfree:copy /wdmfree:stage=audio/uac2` | exit 3; `DevicePath` `C:\WINDOWS\INF;C:\XHCI98;C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me`; `Other\Free\Staged` that folder alone |
| Cold start, the simulated X4 at port 1 | "New Hardware Found: UAC2 USB audio (experiment)" with no wizard; ME asked for `ksproxy.ax` (no ME CD on the machine; skipped, as 44.34's ME leg); `VID_041E&PID_3278&MI_03` (compatible ids `USB\CLASS_01&SUBCLASS_00&PROT_20, ...`) "UAC2 USB audio (experiment)", `MEDIA\0000`, `usb-wdm-free`, `InfSection` `UAC2`; `MI_00` HID bound by ME; no `USBAUDIO` |
| The simulated MIDI device (`acms`) | ME's wizard "USB Audio Device" (ME's own driver, as the pack says); cancelled |
| Second run, nothing ticked (`/yes /nopause /wdmfree:copy`) | "Staged by an earlier run: free:audio/uac2 (still offered to Windows) ..."; the folder kept on `DevicePath`; exit 4 for two PCI controllers the base recorded in its own build boots and not present now, not for the set |
| `/uninstall /yes /nopause` | exit 3, "C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me taken out of DevicePath"; `DevicePath` `C:\WINDOWS\INF`; `Software\xhci98` gone |

### 98 SE, the installer (q35, the bases' emulator, AHCI system disk): PASS

| Step | Result |
|---|---|
| `... /list /wdmfree:copy /wdmfree:stage=audio/uac2,audio/usbmidi` | exit 0, both staged in the plan |
| `... /wdmfree:stage=net/cdcnet` | exit 1, "REFUSED: free:net/cdcnet: on Windows 98 SE and ME this package is copied, not staged (only free audio/uac2 and audio/usbmidi can be staged there, each where its set stages it; ...)" |
| `... /yes /nopause /wdmfree:copy /wdmfree:stage=audio/uac2,audio/usbmidi` | exit 3, "free:audio/uac2 staged. free:audio/usbmidi staged."; after a cold start `DevicePath` `C:\WINDOWS\INF;C:\XHCI98;C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me;C:\XHCI98\USBDRV\FREE\audio\usbmidi\w98se` (108 characters), `Staged` exactly the two folders |
| Second run, nothing ticked | "Staged by an earlier run: free:audio/uac2, free:audio/usbmidi (still offered to Windows) ..."; both kept (`DevicePath` with the install directory's entry moved last); exit 4 for the base's recorded ASM4242 xHCI, as on ME |
| `/uninstall /yes /nopause` | exit 3, each folder "taken out of DevicePath"; `DevicePath` `C:\WINDOWS\INF` |

### 98 SE, the device (i440FX, stock QEMU 11.1.0): PASS

The same install (exit 3, `DevicePath` 108 characters with the two
folders), a cold start, then:

| Step | Result |
|---|---|
| The simulated X4 at port 1 | "New Hardware Found: UAC2 USB audio (experiment)" with no wizard; the engine asked for "UAC2 experimental build folder" and then `ks.cat` (skipped; section 6.7.9's prompt, said by the details); `VID_041E&PID_3278&MI_03` "UAC2 USB audio (experiment)", `MEDIA\0000`, `usb-wdm-free`; no `USBAUDIO`, no exception. The HID interface's wizard was cancelled |
| The simulated MIDI device, `lone` | "New Hardware Found: USB MIDI device" with no wizard ("usbmidi installation disk", then the KS files skipped); `VID_FFFF&PID_0003` (compatible ids `USB\CLASS_01&SUBCLASS_03&PROT_00, ...`) "USB MIDI device", `MEDIA\0007`, `usb-wdm-free`, `MatchingDeviceId` `USB\Class_01&SubClass_03&Prot_00` |
| The simulated MIDI device, `acms` | the wizard for "USB Audio Device", best driver `C:\WINDOWS\INF\WDMA_USB.INF` (its `USB\CLASS_01` line): the AudioControl interface, which `usbmidi.inf` does not match; cancelled, so the MIDIStreaming interface behind it was not reached [inf] |

**Verdict.** On 98 SE both folders on `DevicePath` make the packages'
lines win at the first plug: the X4's audio function gets `uac2`, a
MIDIStreaming interface `usbmidi`. On ME only `uac2` is offered, as the
pack's lines say, and wins the same way. Not read: playback and capture
(the packages' own legs), the window on 98 SE (read in the section
above), 98 SE's device rows on q35 (the stock QEMU, the one here with
`usb-redir`, hangs 98 SE's q35 base at its start; the i440FX rows stand
in).

## 44.21, the second pass - the buttons' cause, ME's second run, the legs left (2026-10-27)

The rest of task 44.21 after the first pass ("44.21 - XHCIPERF's open
items" above). Design record 29 section 16.8 has the readings in short.
XHCIPERF stays a developer diagnostic, not shipped.

Development host A; QEMU's **q35** board from `E:\xhci98-vm\q35-bases`
(98 SE on AHCI; ME, 2000 and XP on piix3-ide; 7 x64, 8 x86, 8.1 x86 and
10 x86 on AHCI), the launcher's emulator `asm4242-20261025-2230`
(`da60139f...`), TCG, headless, screens by QMP `screendump`, keys by
QMP `send-key`; `-Usb4 asm4242 -EncAtBoot` (the ASM4242 profile, the
ORICO model's FAT NVMe disk of 128 MB at power-on) where a row names
Stor98. 98 SE and ME 1 vCPU and 512 MB; 2000 1 vCPU; XP, 7 x64, 8, 8.1
and 10 2 vCPUs. Kit `E:\xhci98-vm\p44\p44-perf` (`runs\serial-*.log`
the reports as the guests copied them to COM1, `shots\` and the guest
folders' screenshots, `src\aspi2.c` the ASPI probe); not files a clone
has.

### The vehicle

| Item | Where from | SHA-256 |
|---|---|---|
| Test cut A | `make-release.ps1 -Arch x86 -Flavor release -TestLabel p44perf -NoNonFree -NoWdmFree -NoDriverPack -SkipQualtool` at `078e3d4c` (the gated `all` x86 build passed, `BUILD + GATES PASSED (debug release qemu)`), into a scratch releases and upload folder | zip `79d75039...` |
| `XHCIPERF.EXE` of `078e3d4c` | that tree's build | `9e958593...` |
| `XHCIPERF.EXE` with this section's two changes | the gated `release` build at `4d473b8f` | `6244f522...` |
| `XHCIPERF.EXE` before record 29 section 16.1 | the bench build of 2026-10-23 (`317f39a0`) | `3be1f508...` |
| `ASPI2.EXE` | `src\aspi2.c`, MSVC 6.0, run in the guests only: INQUIRY, INQUIRY C9h and READ BUFFER 3Ch/53h to every disk target through `WNASPI32.DLL`, printing `Status`, `HaStat`, `TargStat`, the sense key and the wait | - |

Installs: ME by `XHCIINST /dir release-x86 /yes /nopause /thunderbolt
/stor98` (one overlay with `/s98vsd` too; both got `S98VSD.VXD`, which
the 9x install ticks by default), exit 3, Restart (QEMU ends), cold
starts; Stor98's disk `E:`. 2000, XP and 10 x86 by `INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98` (exit 3 on each; Windows'
own wizard named the drive "Stor98 Storage Controller for NVMe SSDs" on
2000 and XP and was answered; on 10 the unsigned-driver prompts were
clicked after a `usb-tablet` was hot-plugged), then a cold start (2000,
XP) or a restart (10); Stor98's disk `E:` (2000, 10) or `D:` (XP).

### The Start and Stop buttons (P.4b)

98 SE, Windows' own Cirrus driver, small fonts (96 dpi), 16-bit colour.

| Build | 1280x1024 | 640x480 |
|---|---|---|
| `3be1f508` (before the fix) | Start and Stop two thin lines, their faces painted over, before and after a setting changed the estimate (`shots\h1-old-a.png`, `h1-old-b.png`) | the same (`h1-old640-a.png`, `h1-old640-b.png`) |
| `9e958593` | every button whole, before and after (`h1-new-a.png`, `h1-new-b.png`) | whole (`h1-new640-b.png`) |

### ME: the second study run (P.5b)

Each row a cold start of one overlay with the enclosure; the study preset
`/preset study9x /min 128 /max 256 /size 16 /folder E:\` (the first row
`/max 2048`) from a batch, each report copied to COM1.

| Session (EXE) | Between the runs | The second run's Stor98 pages |
|---|---|---|
| `mvc` (`9e958593`) | `ASPIPROBE 3` (the 43.50 kit's probe: INQUIRY, C9h, READ BUFFER), which ended in "Aspiprobe has caused an error" | none: "The disk holding E: (QEMU____NVME_CTRL_______1) is on no adapter that answers as Stor98 (ASPI adapters: 1)"; rates about half the first run's |
| `mv3` (`9e958593`) | nothing | 12 of 12 |
| `mv2` (`9e958593`) | `ASPIPROBE 0` (loads `WNASPI32.DLL`, asks the support information) before, between and after | 3 of 12; 9 "Stor98's counters could not be read" |
| `mv4` (`9e958593`) | `ASPIPROBE 0 f` (the same, freeing the library) | 3 of 12 |
| `mv5` (`9e958593`) | `ASPIPROBE 0` | 0 of 12 |
| `mp1` (`9e958593`) | `ASPI2 2` before the first run, twice between, once after the second | 12 of 12 |
| `mn` (`6244f522`) | `ASPI2 1`, then `ASPIPROBE 3` (failed again, its error closed) | 12, 12 and 12 (three runs) |

`ASPI2` in `mp1`: before any XHCIPERF run, every request `Status` 01h,
`HaStat` 00h, `TargStat` 00h; after the first run had ended, every
request (INQUIRY, C9h, READ BUFFER, both rounds, both probe runs)
`Status` 01h, `HaStat` 00h, **`TargStat` 05h**, the sense empty, 0 to
5 ms; after the second XHCIPERF run, 00h again. In `mn` the probe read
05h and the changed XHCIPERF read every page.

The requests at Stor98 (`mv3`, nothing between the runs): in the first
run every step "IOS handed over: 128 (or 64) linear, 0 scatter/gather
lists ... double-buffered 0", each request in its own size class; in
the second the 256 KiB steps "received 0 requests in that size class",
writes "below 4 KiB: 128, to 128 KiB: 128", reads "0 linear, 128
scatter/gather lists", one step "Split by Stor98: 112; bounced: 448";
in `mn` the third run was linear again. "IOS: largest transfer 2048
KiB, 17 scatter/gather elements; the system demands physical
scatter/gather: no (an IOS layer above SCSIPORT removed the demand)" in
every run.

### ME: P.2, P.7, P.8's `SUBST`

| Leg | Result |
|---|---|
| P.2 (`mac2`, `9e958593`, study `/max 2048`) | every step "Buffer layout: scattered, as seen by Stor98", the aligned buffers included; requests whole to 2 MiB (512 KiB commands at the drive, its limit), double-buffered 0; every write marked "stamping"; busy 100.0% "(Windows' own figure)" on each step |
| P.7 (`mw2`, `6244f522`, `/test seq,1024,1,1 /ops write /time 60 /passes 3 /size 16 /folder E:\`), QMP `device_del` of the enclosure in pass 2 | "Stopped: The disk stopped answering (error 31: A device attached to the system is not functioning). The run was stopped."; pass 1 49.0 MB/s, pass 2 "failed", the Stor98 page "Pass 2 of 3: Stor98's counters could not be read"; the report written and the program ended by `/exit` (`mw2\w3.png`, `w4.png`) |
| P.7, the next start (`mw3`, the enclosure back) | the window opened with no offer; `E:\` held no `XHCIPERF.TMP` and `HKCU\Software\xHCI98\XHCIPERF` no record |
| P.8 (`mac2`): `subst G: C:\R`, Quick on `G:\` | "Refused: This drive letter looks like another name (SUBST) of the Windows drive; choose that drive itself." |
| ME's first cold start after the install (`mac`, an overlay of `ma`) | at 120 s "Error: 0E : 01A7 : BFF62170" (ME0E.1's address, another selector), then the New Hardware window for the root hub did not end in 12 minutes; quit. A second overlay of the same install (`mac2`) started clean |

### P.3, P.4 and P.6 on 2000, XP and 10; 8 and 8.1; WOW64

| System | Reading |
|---|---|
| 2000 SP4 (`9e958593`, administrator) | Quick on `C:` 448.5 / 546.7 MB/s sequential, "Stor98: ... not reported by this disk's driver"; Standard on `E:` (`/size 48 /passes 1 /time 2`): the Stor98 page through the volume on all 8 steps, "Stor98 3.2.0.0 (x86 build), PCI bus 5 device 0 function 0", most commands at once 5-8 at depth 8, 2 at depth 32 (4 KiB), 1 to 4 at depth 1; busy 63 to 100% of 1 |
| XP SP3 (`9e958593`, administrator) | the same on `D:`: 8 of 8 pages, 5-8 at depth 8, 2 at depth 32 |
| 10 x86 22H2 (`9e958593`, unelevated) | Standard on `E:`: all 8 pages read (the volume is removable, so an unelevated handle may send the pass-through), 3-4 to 5-8 at depth 8; the default folder `E:\` refused for a Quick run ("The drive has 132 MB free; this run needs 68 MB and must leave 67 ..."); Quick in `%TEMP%` 358.7 / 399.3 MB/s |
| 8 x86 and 8.1 x86 (`9e958593`, unelevated) | the window's default folder `D:\` (the first fixed drive whose root is writable); Quick there and in `%TEMP%`; `C:\` refused: "You cannot write to this folder (error 5: Access is denied); choose another folder." |
| 7 x64, the 32-bit tool alone (`9e958593`; `XHCIPF64.EXE` in another folder) | Quick in `%TEMP%`: busy 13 to 19% of 2 idle, 68 to 82% with `BUSY 0 150` beside it; the system line "Windows 6.1.7601 Service Pack 1 x86" |
| 7 x64, `XHCIPF64.EXE` | Quick, "x64", 341.6 / 404.5 MB/s |
| 7 x64, `6244f522` alone | "System: Windows 6.1.7601 Service Pack 1 64-bit (WOW64)" |

### The changes

- `e6d85a90`, `16d648e0`: the system line under WOW64 (`IsWow64Process`
  at run time), kept within the report's 79 columns.
- `4d473b8f`: `aspi_exec` takes ASPI's `SS_COMP` alone as success; the
  target status is not read; the identity page, the model and the
  counter answer are still checked by their content.

Both reviewed and revised; the gated `release` builds x86 and amd64 at
`4d473b8f` passed.

### Left

- The owner's real-check list (record 29 section 14.2): 98 SE at a high
  resolution on real hardware, the Quick preset's 2 s steps on a real
  IDE disk, P.10.
- P.4's Browse... and its run on a USB stick, and P.6's standard-user
  refusal and "Read as administrator..." button on 2000, XP and 7 (2000
  and XP were logged on as an administrator here; 10 read unelevated).
- For Stor98, not this task: the requests it answers itself leave the
  SRB's SCSI status as they found it (`ScsiSuccess`, `nvme2k_scsi.c`);
  that this is where ME's ASPI found 05h is inferred from that code and
  the probe, not read in the driver.

## 44.20 (continued) - The Legs `3.2.0.0` Left, on `q35` (2026-10-27)

Roadmap-usb4 task 44.20, continued on branch `p44-legs` from "44.19 and
44.20" above. Every leg on QEMU's `q35` machine from the `q35` bases
(`E:\xhci98-vm\q35-bases`, read-only overlays) on the bases' emulator
build, under TCG on development host A. The package is the `3.2.0.0`
release as published (the `-nonfree` download: `release-x86\xhci98.sys`
`dd230d50...`, `SETUP.EXE` `2b1ea62c...`, `release-x64\xhci98.sys`
`3b166bff...`), nothing of it run on the host. Evidence:
`E:\xhci98-vm\p44\p44-legs\` (`runs\`, `dock8\`, `dock81\`, `x64nf\`,
`pids.txt`), outside the repository.

### The scripts: 7's question for a publisher it does not trust yet

The first `w7-nonfree` run stopped at "Would you like to install this
device software? Name: Masahiko Morii Human Interface Devices" (the
`x360c` package is signed, by a publisher 7 does not trust yet), which
`PromptStep` `nt6` did not answer: the box opens without the focus and
its default button is Don't Install. `76fe9ee9` to `3ea8e962` recognise
it on a screendump (its white upper part meeting its grey lower part at
one height, the warning's yellow shield at the lower part's left; no
false match on the 264 earlier 7 and Vista screendumps), click where the
pointer rests to give it the focus and send Alt+I; should the box still
stand, Tab or Shift+Tab, by whether the click changed the "Always trust"
tick, and Space. Reviewed and revised; `build-and-test.md` says how.

### The legs

| Leg | System | Configuration | Result |
|---|---|---|---|
| 43.48's `w7-nonfree` (run 2) | 7 SP1 x86 | `run-leg.ps1`, scripts at `3ea8e962`, `/wdmnonfree:stage=ethernet/ax88179,ethernet/rtl815x,hid/x360c,hid/x360wc` | **PASS** in 13.5 min, every check (driver file, `/list` current, stick E: seen, copy clean, `/list` after the uninstall clean); exits install 3, list 0, uninstall 3, list-final 0; 4 non-free INFs staged; the two Morii boxes each answered by the click and Alt+I (the fallback not needed); five driver-install questions in all |
| 43.48's x64 leg | 7 SP1 x64 (test mode, the base's) | `launch-q35.ps1 -Os w7x64`, elevated `INSTALL.BAT /console /yes /wdmnonfree:stage=` the same four, prompts answered by the scripts' routine | **PASS**: exit 3; "On 64-bit Windows a staged driver loads only if Windows accepts its signature" said; `xhci98.inf` and `xhciuas.inf` staged (`oem2`, `oem3`) and the four non-free INFs (ASIX, Realtek, Morii twice; `oem4` to `oem7`); the uninstall exit 3, the six removed from the driver store, `pnputil -e` back to Microsoft's two |
| 43.24's device behind a dock, 8 | 8 Pro x86 | 3.2.0.0 `/thunderbolt /stor98` (exit 3, restart); the emulator's TB3 dock (`dialect=tb3,dock=on,usb-drive=`) hot-plugged through QMP | Alpine Ridge (`-Usb4 tb3ar`): the dock's tunnel up on the emulator's side ("QEMU USB4 Dock connected" from XHCITR), but Windows gave its bridge no bus numbers: the switch's tunnel port spans bus 4 alone, the dock needs five, and that emulated port takes no bus reservation, so none of its functions was seen (the emulator's limit, not read further). Alder Lake (`-Usb4 adl`, tunnel root ports reserving 8 buses): `xhcicm` bound the USB4 router, the dock connected and the USB disk behind the dock's own xHCI enumerated ("QEMU HARDDISK USB Device", USB Mass Storage Device OK, Windows' `usbxhci`), beside the dock's NVMe as E:; the USB disk got no letter, being the same image as the NVMe (disk signature `46416078` on both). **PASS** for the enumeration |
| 43.24's device behind a dock, 8.1 | 8.1 Pro x86 | as 8 on Alder Lake, the dock without its NVMe | **PASS**: the USB disk behind the dock is E: "ORICO"; `TESTDATA.BIN` read back, SHA-256 `b0e3c8dc...209a4f`, the source's |
| `xp-window`, again on the final scripts | XP SP3 | `run-leg.ps1`, scripts at `3ea8e962`, the `-nonfree` download | **PASS** in 10.2 min, every check; exits 3, 0, 3, 0 |
| `me-window`, again on the final scripts (run 1) | ME | as above | **FAIL**: install 3, list 0, the stick seen and its copy clean; then the uninstall started (XHCIINST on the taskbar, the hourglass) and nothing more came on COM1 for 15 minutes; no stop screen |
| `me-window` (run 2, a fresh overlay) | ME | as above | **PASS** in 8.5 min, every check; exits 3, 0, 3, 0. Run 1's stall not seen again (1 of 2) |
| 43.43's case 2, 98 SE | 98 SE | `-Usb4 asm4242`, `INSTALL.BAT /console /yes /cmonly` | `/cmonly` accepted with the warning ("Without the xHCI98 USB drivers, Windows 98 SE and ME have no driver of their own for a USB 3 controller: ... Only the Thunderbolt 3 / USB4 connection manager is installed; xHCI98 USB drivers already installed stay as they are."); "The connection manager alone: installed without asking"; the ASM4242 router bound ("xHCI98 USB4 Router: xhcicm ..., started", through the helper), no `xhci98.sys`; the storage row ticked by default as on any 98 SE run, its seven installed; exit 3. Shut Down then stayed at "Windows is shutting down" (`3.2.0.0`'s connection-manager hang, 44.43 and 44.47); cold start clean, the tray icon up. The uninstall: the router's devnode, `xhcicm.inf`'s copy and `xhcicm.sys` removed, the seven deleted, the entry removed, exit 3; Shut Down then powered off. **PASS**, but the Add/Remove Programs entry read "xHCI98 USB 3.x drivers 3.2.0.0" with only the connection manager installed (record 26 section 9.3: its name follows what the machine has) |
| 43.43's case 2, ME | ME | as 98 SE | the same warning and binding, exit 3, the same entry name; Shut Down left a black screen (44.47's, on `3.2.0.0`); cold start clean, no stop screen; the uninstall exit 3 (router, `xhcicm.inf`, `xhcicm.sys`, `S98VSD.VXD` and the entry removed); Shut Down powered off. **PASS**, with the same entry name. The install also printed ME's UAS hint ("plug in an ordinary USB stick once before the first UAS disk"), which concerns the USB drivers this run left out |

ME0E.1: no "0E" stop in any ME start of this part (the window legs' and case 2's, eight starts).

### 43.17's full matrix: the console legs on `q35`

`release-matrix.ps1 -Only` the six console legs (the window and
`-nonfree` legs are above and in "44.19 and 44.20"), `-MaxVcpu 6`, three
guests at once, scripts at `3ea8e962`, the `-nonfree` download
(`runs\20261027-111527`, then `runs\20261027-114749` for the three that
failed):

| Leg | First run | Again (fresh overlays) |
|---|---|---|
| `w98se-console` | FAIL: the host step `wizard98-hcd` saw no wizard's last page in 300 s, then nothing on COM1 for 900 s | **PASS** in 11.9 min, every check; exits 3, 0, 3, 0 |
| `me-console` | FAIL: `restart-me`, QEMU still running 300 s after Yes | **PASS** in 5.1 min; exits 3, 0, 3, 0 |
| `xp-console` | FAIL: `stick-copy` | FAIL: `stick-copy` again; every other check passes, exits 3, 0, 3, 0 |
| `w2k-console` | **PASS** in 3.7 min; exits 3, 0, 3, 0 | - |
| `vista-console` | **PASS** in 8.8 min; exits 3, 0, 3, 0 | - |
| `w7-console` | **PASS** in 6.6 min; exits 3, 0, 3, 0 | - |

The first 98 SE and ME failures were the host's steps with three guests
at once; neither recurred. XP's, in both runs: the guest's `fc /b` of
the 4 MiB file printed its "Comparing files" line and nothing more, so
the check found no "no differences"; the stick's `RAND.BIN`, read from
its image after the run, is byte for byte the file the host made (the
same seed), and `xp-window`'s same check passes on the same base. The
data path is sound; the console leg's check on XP under `q35` is not
(the `copy` whose errors go to `NUL`, and `fc` reading before the
stick's write has finished, are the candidates): owed to the scripts,
not read further.

### 44.17's 2000 leg

The 2000 leg 44.17 left ("2000 with ACPI"), on the new `q35` base with
the ACPI PC HAL (`E:\xhci98-vm\q35-bases\w2kacpi`, SHA-256
`9a72a82e...`, made by worker `p44-w2kacpi`), mirroring "44.17
(continued)"'s XP leg: emulator `bin\asm4242-20261027-roothp3`
(`qemu-system-x86_64.exe` `94c75112...`), the root port above the
ASM4242 a `usb4-tunnel-port` with `root-hotplug=restore` (the q35 port
has no `acpi` mode), the ORICO model with the FAT NVMe on USB4 port 1 at
power-on, `-smp 1`; test cut K (XP's), `INSTALL.BAT /console /yes
/thunderbolt /stor98`: exit 3 (one "Digital Signature Not Found" for the
package, Yes; Stor98's Found New Hardware wizard walked to `oem4.inf`,
its signature question Yes); Windows Restart. Readings: `RN.BAT` and
`CHK.BAT` as there.

| Step | Reading |
|---|---|
| Before | "Stor98 Storage Controller for NVMe SSDs - stor98, started", ORICO E:, liveness "answering"; `PRE.BIN` written and compared clean |
| Departure (80 s later, 12:23:12, `present false`) | 2000's "Unsafe Removal of Device" box (the enclosure's PCI-to-PCI bridge and the NVMe disk); the connection manager contained: `cm.live.state` `0x107`, `u4.contained` `0x107`, `u4.contained.lost-up` 0, `u4.drv.contained` 1, liveness "contained" |
| Return (12:27:03, `present true`) | `u4.recover.ok` 1, `u4.recover.committed` 1, `u4.recover.preserved` 1, `cm.live.state` `0x109` then `0x101`, liveness "answering"; Stor98 started, ORICO E: back, no problem code; `PRE.BIN` intact, a new write clean |

**Verdict: pass** (the containment and 42-A.10's recovery on 2000 with
`restore`, the drive back; no Code 12 on the bridge here, unlike the
i440FX leg of 2026-10-25). Evidence: `e17\w2k\serial-a.log`,
`trace-a.log`, `c1.png` (the box).

### 43.45's resume on 2000

On the same base, the `3.2.0.0` download (43.45's package) on a fresh
overlay, the emulator's AMD host model as 43.45 used it (`usb4-host ...
host-model=amd,host-domain=0`) on the Alder Lake profile's topology
(tunnel root ports 00:05.0 and 00:06.0), the ORICO model at power-on:
exit 3, "xHCI98 USB4 Router: xhcicm 3.2.0.0, started"; Windows Restart.
The connection manager approved the device ("PCIe up"), but 2000 never
configured the enclosure's switch behind the tunnel port (`XHCIINST
/devices`: "(nothing behind it now)"; QEMU: the switch's secondary bus
0): on `q35` the tunnel ports are PCIe native hot-plug ports, which 2000
does not drive, and this host model has no pre-OS tunnel. Then Shut
Down, Hibernate: QEMU ended at the S4 power-off; launched again, the
same session came back, the window as left. After the resume:
`cm.suspends` 1, `cm.resumes` 1, `cm.suspend` 5, `cm.resume` 1,
`cm.reenum.resumed` 1, `u4.resume.bound-ms` `0x84C6` (34 s), the device
approved, "PCIe up", liveness "answering". **Verdict: the connection
manager's resume passes on 2000**; a drive across the resume is not
readable with this host model on `q35` (there was none before it
either). Evidence: `r45\w2k\serial-a.log`, `serial-b.log`, `c0.png` to
`c2.png`.

### How a 2000 with the ACPI HAL came to run on `q35`

The `q35` 2000 base has the Standard PC HAL, which offers neither Stand by
nor Hibernate. A 2000 with the ACPI HAL was tried on `q35` (a fresh
install from the SP4 CD, about 90 minutes); two findings about `q35`
itself, and one left open:

- **`q35`'s FADT stops 2000's ACPI HAL.** QEMU's `q35` builds a revision
  3 FADT (244 bytes, `X_FIRMWARE_CTRL` 0); 2000's HAL stops in phase 0
  with bug check 0x5C (HAL_INITIALIZATION_FAILED, first parameter
  0xDEAD000A), nothing on the screen past "Setup is starting Windows
  2000". The `pc` machine's FADT is revision 1 (116 bytes), its MADT the
  same bytes as `q35`'s. With the table rewritten in guest memory as
  revision 1 (length 116, checksum), the HAL starts; a boot-sector stage
  doing that rewrite at every start lets an ACPI 2000 boot on `q35`
  (read with an older ACPI Multiprocessor image, which then reached its
  desktop).
- **A table crossing a 4 KiB page stops 2000 earlier**: its loader and
  HAL map one page of it (a fault loop in the loader, or a triple fault).
  Where the tables fall depends on the devices: with no USB4 profile the
  FADT crosses; with the ASM4242, Alpine Ridge, Titan Ridge and Alder Lake
  profiles none does.
- GUI-mode setup under TCG with the APIC HAL (which `q35`'s MADT makes
  Setup choose) restarted the machine after installing the networking
  components, and started over, every time (also seen on `pc` for
  43.45); under WHPX the start stalled; without a local APIC Setup still
  chose the APIC HAL and stopped with no timer. The base's maker got past
  it by putting the ACPI PC HAL (`halacpi.dll`) in place during GUI-mode
  setup, and found two more `q35` fixes for 2000 (COM1's interrupt
  descriptor, the local APIC after a warm restart): its `README.txt`.

The owner's decision (2026-10-27): a clean 2000 image with the ACPI HAL
built by another worker; that base (`w2kacpi`: the ACPI PC HAL, the
FADT rewritten by its boot sector) carries the two legs above.

### Left in 44.20

43.5's package copy cut between the INFs (not provoked, as in 43.5),
43.17's `xp-console` copy check (above), 43.57's T.14 and 43.69's 98 SE
fault rate (not run in this part).

## 44.32 (continued): XP's hot plug, XP x64 and `StartOverride` on 10 x64 (2026-10-27)

What 44.32's section above left owed: XP's hot plug at either start
type, XP x64, and whether 10 x64 writes a `StartOverride` key for
`xhcicm`. Since then 44.45 gave the emulated board the ASM4242's bus,
window and slot layout (no power controller on the tunnel ports,
interrupt pin A on the switch ports), so XP's hot plug is read again on
that layout.

**Setup.** QEMU `q35`, the q35 bases `xp`, `xpx64` and `w10x64`
(`E:\xhci98-vm\q35-bases\<os>`) on overlays of this task's own, two
vCPUs each; emulator build `asm4242-20261027-boardint`
(`qemu-system-x86_64.exe` SHA-256 `deca3fcb...4979ce`) with 44.45's
ASM4242 layout (the root port `bus-reserve=103,mem-reserve=776M,
io-reserve=24K,pref32-reserve=4M,hotplug=off`, `usb4-asm-usp`
`fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, both tunnel ports
`power-controller=off`); the ORICO model (`usb4-device ...
device-model=asm2464`) plugged on USB4 port 1 by QMP `device_add` at the
desktop. The package: that of "44.45 (continued)" (`284baa85`), built from `6b35a399`
(`078e3d4c`'s code; this branch's code differs from it only in comments
and installer texts): the x86 test cut (`xhci98.sys` SHA-256
`15927FAE...`, `xhcicm.sys` `F257E875...`, `stor98.sys` `F873BE59...`)
and the amd64 package staged unsigned (`xhcicm.sys` `B15F066A...`,
`xhci98.sys` `1FC238F6...`, `stor98.sys` `646C6706...`), its drivers and
catalogs signed inside each x64 guest with a throwaway certificate made
there and trusted in its machine Root and TrustedPublisher stores (as
43.36 did; nothing signed on the host). Installed by `INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98`. On XP and XP x64 the mark
service's own restart was turned off (`Restart` 0) after the install, so
that the first plug of a never-seen enclosure goes through the tag's
path; with the default (`Restart` 1) such a plug leaves the drive
without a wizard on XP ("44.45 (continued)", `284baa85`), which is the mark service's
and not this question's. The boot-start control: the mark service
stopped, `Start` 0 written, restart; at the next start the service's log
reads "xhcicm: start type 0 set back to demand start", so `xhcicm` was
loaded as a boot driver for that start.

| Leg | System | Result |
|---|---|---|
| The install | XP SP3 | the first run stopped at Windows' wizards for the new root hubs (exit code 4); answered, the second ended with exit code 3; `Start` 3 after it |
| The install | XP x64 SP2 | signed in the guest ("SIGN done", every file "Successfully verified"); two runs stopped at the root hubs' wizards (exit code 4), the third ended with exit code 3; `Start` 3 |
| Demand start: the first plug | XP | "ASMedia 246x connected (USB4 v1.0)"; Windows installed the switch's two bridges, then a wizard for "PCI Device" (the drive, before the tag) found no driver and was finished with its defaults; the mark service: "mark written ... (member 0200, unit 03:00.0, serial 1)", "its install set to run again before its unit's restart (flags 00000040 -> 00000020)", "restart made"; a second wizard installed "Stor98 Storage Controller for NVMe SSDs" (`setupapi.log` "Found "PCI\XHCI98_TUNNEL&CC_010802" in C:\WINDOWS\inf\oem4.inf"); the drive Code 0 on `stor98`, `ConfigFlags` 0, "ORICO (D:)": **pass** |
| Boot start: the first plug | XP | a fresh overlay of the same install, the control above (`Start` 0 at that start): the same steps, the same wizards, the same service lines, the drive Code 0 on `stor98`, "ORICO (D:)": **pass** |
| Demand start: a restart with the enclosure attached | XP | the two bridges and the drive Code 0 at boot, `ActiveService` stor98, "ORICO (D:)"; `Start` 3: **pass** |
| Demand start: the first plug | XP x64 | as on XP: the "PCI Device" wizard, the tag's path and the unit's restart, the Stor98 wizard; the drive Code 0 on `stor98`, "ORICO (D:)": **pass** |
| Boot start: the first plug | XP x64 | the control (the service's log "start type 0 set back" at 12:11:22, after the start): the same, "ORICO (D:)": **pass** |
| Demand start: a restart with the enclosure attached | XP x64 | the bridges and the drive Code 0 at boot, "ORICO (D:)"; `Start` 3: **pass** |
| The install | 10 x64 (22H2, 19045) | test-signing mode on (the base's); signed in the guest; exit code 3, "Test-signing mode is on: the test-signed x64 connection manager loads." and the boot-driver warning; `Start` 3 after the run, the service's log "xhcicm: start type 0 set back to demand start" |
| A restart | 10 x64 | `xhcicm` `DEMAND_START` running, `Start` 3, **no `StartOverride` key** (`reg query ...\xhcicm /s` lists only `Enum`), `Select` Current 1 Failed 0 |
| The enclosure plugged | 10 x64 | the drive on 10's `stornvme` Code 0 (43.59's policy), "ORICO (E:)", "mark written ... (member 0200, unit 03:00.0, serial 1)" |
| A restart with the enclosure attached | 10 x64 | the drive Code 0 at boot, "ORICO (E:)"; the four ASM4242 downstream ports Code 0 ("xHCI98 USB4 PCI Bridge"), the NHI on `xhcicm`; `Start` 3, no `StartOverride` |

XP x64, like XP, enforces no kernel-mode signature, so test-signing mode
has no XP x64 leg. The XP x64 base raises a Found New Hardware wizard
for an "Unknown" device at each start, cancelled (not a device of this
package's).

The earlier 10 x64 reading, on QEMU `pc` (i440FX): the overlay of the
leg above ("10 x64 (22H2, 19045)", the `3.2.0.0` package with the
put-back by hand), started again after its demand-start, test-signing
and pre-boot restarts: "STARTOVERRIDE none", `Start` 3, `xhcicm`
`DEMAND_START` running, `Select` Current 1 Failed 0, no `SrtTrail.txt`.

**Verdict.** On the ASM4242 layout XP's hot plug works, and works the
same at demand start and at boot start, on XP and on XP x64; the drive
present at a restart starts at demand start too. The start type changes
nothing on either. These legs exclude a start-type difference in this
configuration; they do not separate the cause of the XP failure in
44.32's section above, on the old layout, which was not re-read: it is
consistent with 44.45's items 1 and 2 [inf]. Windows 10 x64 writes no `StartOverride` for `xhcicm` (read on
q35 and on i440FX), as on 8 x64, 8.1 x64 and 10 x86. **Option 1 holds
on every system the condition names, 2000 to 10, x86 and x64, and on XP
x64**: the condition is the filter's attachment; 2000's hot-plugged
drive behind the switch on q35, whose start SCSIport refuses for want
of an interrupt (44.50), is not closed by it. Evidence: `E:\xhci98-vm\p44\p44-boot\b2\` (`xp-dem\`, `xp-boot\`,
`x64-dem\`, `x64-boot\`, `w10q35\` with `serial-*.log`; the earlier
reading in `w10x64so\`; `shots\`; `pids.txt`).

## 44.52 - the mark's restart sets a plain drive's install to run again on 2000 and XP (2026-10-27)

**The defect** (44.45 continued, XP and XP x64 on q35): with the mark
service's `Restart` 1, the default, the first plug of a never-seen switch
enclosure left its drive without a driver. The drive arrived without the
tunnel id; Windows' wizard for it, answered with its defaults ("Don't
prompt me again" ticked), set `CONFIGFLAG_FAILEDINSTALL`; the
safe-removal mark's restart of the enclosure's unit (record 21 section
7.4) then gave the drive the id, but only the tag restart's path cleared
that flag, so Windows asked nothing, and the service read "the id and no
driver" as the user's refusal.

**The fix** (owner, 2026-10-27: in the restart step). Design record 31
section 6.2, "The mark's restart" (draft 7, reviewed and revised): on NT
5.x, with the Stor98 policy value naming a staged INF, the mark's restart
of a unit first gives each plain NVMe function below that unit - no
driver, no id, in a state the tag restart admits, not declined in this
arrival, not protected - the tag restart's flag step
(`CONFIGFLAG_FAILEDINSTALL` cleared, `CONFIGFLAG_REINSTALL` set, every
other flag kept). Each function is read again just before its write,
under the eject and Stor98 switch mutexes with no installer run; a
function with a driver is never written; a function that cannot be read
or written again defers the restart to the next poll. An absent
`ConfigFlags` is told from an unread one through the configuration
manager (both restarts). Vista to 8.1 are unchanged. Code: `xhcimark.c`
(`mk_mark_rerun`, called from `mk_restart`), the pure half in
`mark_core.c` (`mk_s98_mark_sets`, `mk_s98_rerun_flags`,
`mk_s98_driverless`) with its self-test vectors, and a host test
(`test_tr.c`, `test_mark_rerun`); commits `47677ad6`, `1c2484a4` (design),
`5e2ce21c`, `a20d1dc2` (code), reviewed and revised. Host tests pass;
gated `release` builds x86 (`out\build-x86.log`) and amd64
(`out\build-amd64.log`) passed at `a20d1dc2`, the mark service's
self-test among them.

**The vehicle.** QEMU `q35` from the q35 bases, overlays under
`E:\xhci98-vm\p44\p44-xpmark\` (`xp`, `xp64`, `w7`), the
ASM4242 layout of 44.45 (continued) (emulator build
`asm4242-20261027-boardint`; root port `bus-reserve=103,mem-reserve=776M,
io-reserve=24K,pref32-reserve=4M,hotplug=off`, `fw-bus-pad=48,
fw-io-pad=12K,fw-mem-pad=384M`, both tunnel ports
`power-controller=off`), two vCPUs each. Packages of `a20d1dc2`: an x86
test cut (`make-release.ps1 -Arch x86 -Flavor release -TestLabel "44.52
legs"`, without the driver packs and the DOS qualifier; `stor98.sys`
SHA-256 `BD716C5F...`, `xhci98.sys` `8B0541A9...`, `XHCIMARK.EXE`
`B7E92EF5...`), and the amd64 package staged unsigned by
`make-package.ps1 -Arch amd64` (`stor98.sys` `868B1FBB...`, `xhci98.sys`
`0CA0603A...`, `XHCIMK64.EXE` `E59F0775...`), its five catalogs made
unsigned with WDK 7.1's MakeCat on the host, then the drivers and
catalogs signed inside the XP x64 guest with a throwaway certificate made
and trusted there (as 43.36 did; nothing signed on the host). Installed
by `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98` on fresh
overlays, the enclosure absent; the mark service's `Restart` left at its
default. Then a cold start, and the ORICO model (`usb4-device ...
device-model=asm2464`) plugged on USB4 port 1 over QMP.

| Leg | System | Result |
|---|---|---|
| The failing case: first plug, `Restart` 1 | XP SP3 | install exit 3 (the root hubs' wizards answered); the plug: the enclosure's bridges installed by Windows, a wizard for "PCI Device" (the drive, no id) answered with its defaults: "Cannot Install this Hardware", "Don't prompt me again" ticked, Finish (`xp\b3.png`). The service: "restart not made yet ...: an installation is pending", then "Stor98: PCI\VEN_1B36&DEV_0010...: its install set to run again before its unit's restart (flags 00000040 -> 00000020)" and "restart made: PCI\VEN_1B21&DEV_2463... (serial 1)" four seconds later. Windows' wizard then came for "Stor98 Storage Controller for NVMe SSDs" (`xp\w3-0-name.png`), answered (automatic, Continue Anyway, Finish); the drive at Code 0 on `stor98`, `ConfigFlags` 0, "ORICO (D:)" mounted, `setupapi.log` 'Found "PCI\XHCI98_TUNNEL&CC_010802" in C:\WINDOWS\inf\oem4.inf'. **pass** |
| The same | XP x64 | install: the first run ended with exit code 2 (the second controller's install "FAILED (error 0x800B0109)" while the wizards were being answered), the second with exit 3. The plug: the drive's wizard came twice ("PCI Device", no id; the unit's mark was written again between them), each answered with its defaults; then "its install set to run again before its unit's restart (flags 00000040 -> 00000020)", "restart made ... (serial 1)", "restarted, now listed"; Windows' wizard for "Stor98 Storage Controller for NVMe SSDs" (`xp64\w8-0-name.png`), answered; the drive at Code 0 on `stor98`, `ConfigFlags` 0, "ORICO (D:)". **pass** |
| Regression: first plug | 7 SP1 x86 | install exit 3 (the publisher prompts answered); the plug at 12:03:15 (guest clock): the mark written 15 s later, "a Windows installation is pending" for the drive, and "ORICO (D:)" with "Your device is ready to use" within a minute (`w7\b2-4.png`); the drive on `stor98`, `ConfigFlags` 0; no restart was made and no flag line was written (the step is NT 5.x alone). **pass** |
| A drive with a driver | XP SP3 | the enclosure of the first leg unplugged and plugged again: the mark removed and written again (serial 2), no restart; the drive stayed on `stor98`, `ConfigFlags` 0, "ORICO (D:)": nothing written to it. A second ORICO model on USB4 port 2 (a never-seen location): its drive's first wizard was taken through Have Disk to the package's `stor98.inf` and failed ("The driver selected for this device does not support Windows XP"); after it the service read the drive's state as one it does not admit ("its state does not allow it"), so the mark's restart (serial 3) was made with no flag line; Windows' wizard then offered Stor98 for it by itself, and it was installed ("ORICO (F:)"). Not read: a drive that already has its driver below a unit the mark restarts. In these legs the attempted Have Disk install failed, so no drive was bound before a mark restart, and on 7 no mark restart was made; the rule that never writes such a drive is held by the host vectors alone. Pass for what was read |

Evidence: `E:\xhci98-vm\p44\p44-xpmark\` - `xp\serial-b.log` (the mark
log and the reads `R1` to `R3`, `READ 1` and `READ 3`), `xp64\serial-b.log`
(`R1`, `R2`, `READ 2`), `w7\serial-b.log` (`R1`), the screenshots named
above, `pids.txt`.

## 44.56 - 32-bit Windows on the ASM4242 Layout with "Above 4G Decoding" On: the Recovery Screen (2026-10-27)

A task of Phase 44 (the owner, 2026-10-27): task 44.49's worker saw
32-bit Windows 10 on the emulated ASM4242 layout with "Above 4G
Decoding" on (the 4G-on layout) land on the recovery screen, with and without
its early window-closing probe (`E:\xhci98-vm\p44\p44-pref\w10-on`, and
`w10-probe` start `e`, whose `debugcon-e.log` has the probe's line
`C 00000001 00000012 00000009 ...`: 12h functions scanned, 9 bridges'
windows closed), while the option off (the 4G-off layout) reached the
desktop and 10 x64 worked with the option on ("44.49"). The cause was not
read. This section reads it.

### The vehicle

Development host A; QEMU's q35 board from the q35 base images
(`E:\xhci98-vm\q35-bases`, each guest an overlay in this worker's folder;
base SHA-256 prefixes: 10 x86 `ac361c39`, 8.1 x86 `9714c3d8`, 8 x86
`b8e091bd`, 7 x86 `95893177`, Vista x86 `4d1aa775`); the emulator
`bin\asm4242-20261026-4goff` (`qemu-system-x86_64.exe` `08D49470...`)
with the launcher of "44.48" (this worker's copy adds `-Pref64`, the
ASM4242 root port's `pref64-reserve`, 256 GB on the 4G-on layout);
`-machine q35 -smp 2 -m 2048`, `-cpu qemu64,+3dnowprefetch` (8, 8.1,
10) or `qemu64` (Vista, 7), the processor reporting 40 physical address
bits unless a row says otherwise. The
ASM4242 profile and the ORICO enclosure model at power-on as in "44.49"
unless a row says otherwise. **No package of this project is on any
guest**: the bases hold nothing of it (their `README.txt`), and none was
installed.

The host bridge's 64-bit window, the one its `_CRS` describes to the
operating system, was read for each row (for most rows of the 39-bit
processor from the same layout started again, `fwprobe.ps1`: read 12
seconds after power-on, then stopped; Windows' PCI driver runs later
than that under this emulation [inf: the stop screens came 3 to 4
minutes after power-on]) as QEMU's
`qom-get /machine/q35 pci-hole64-start` and `pci-hole64-end`: the span of
the 64-bit windows the firmware programmed, at least
`q35-pcihost.pci-hole64-size` (32 GB by default) long. SeaBIOS places
that window by the processor's address width: with 40 bits the
4G-on layout's root port gets 7F_8000_0000 to BF_7FFF_FFFF and the
host bridge's window is 7F_8000_0000 to BF_801F_FFFF; with 39 bits or
fewer it starts at 4 GB. Rows marked "plain" have no USB4 model at all,
only the q35 board, with `pci-hole64-size` set to move the window's end.

To read the stop code, an overlay of the 10 x86 base was started on
the 4G-off layout (desktop), set with `bcdedit /set {default}
recoveryenabled no`, `bootstatuspolicy ignoreallfailures`, `bootlog yes`,
`CrashControl\AutoReboot` 0 and `powercfg /h off`, and shut down; the
rows marked "prep" are overlays of that disk, so a stop stays on the
screen. 7 x86 and 8.1 x86 were prepared the same way.

### Readings

**What the recovery screen is.** A fresh overlay of the 10 x86 base on
the 4G-on layout: "Choose your keyboard layout" (the recovery environment)
within three minutes; keys sent there changed nothing, the processor
idle (`HLT`). On the prepared disk: the stop screen, **Stop code:
INACCESSIBLE_BOOT_DEVICE** (0x7B). 8.1 x86 prepared: the same stop. 7
x86 prepared: `*** STOP: 0x0000007B (0x80786B58,0xC0000034,0x00000000,
0x00000000)` (`C0000034`, object name not found: the boot device was not
there). Fresh overlays without the preparation: 8.1 x86 a stop screen,
then the recovery environment's keyboard layout page; 8 x86 that page
within two and a half minutes (its stop screen not caught); 7 x86
"System Recovery Options" within 50 seconds, keys ignored there too;
Vista x86 "Windows Error Recovery", "Windows failed to start", and
again after each start (its stop screen not caught). So every 32-bit NT 6 system read stops; 10 x64
with the option on boots ("44.49"). (32-bit XP stops on this layout with
0xA5, "44.48": a different stop, not read here.)

**What decides it** (10 x86, prep, one start each; "window" is the
ASM4242 root port's prefetchable window as SeaBIOS programmed it, "host
bridge" the 64-bit window above):

| Row | Bits | Window | Host bridge's 64-bit window | Result |
|---|---|---|---|---|
| 4G-on | 40 | 7F_8000_0000-BF_7FFF_FFFF | 7F_8000_0000-BF_801F_FFFF | 0x7B |
| 4G-on, no enclosure at power-on | 40 | the same | the same | 0x7B |
| 256 GB reserve (fresh overlay) | 36 | 1_0000_0000-40_FFFF_FFFF (beyond 36 bits: invalid) | - | recovery screen |
| 4 GB reserve (fresh overlay) | 36 | 1_0000_0000-1_FFFF_FFFF | 1_0000_0000-8_FFFF_FFFF (the default 32 GB) | desktop; only the NHI and `QEMU0002` at problem 28 |
| 256 GB | 39 | 1_0000_0000-40_FFFF_FFFF | 1_0000_0000-41_001F_FFFF | 0x7B |
| 252 GB | 39 | 1_0000_0000-3F_FFFF_FFFF | 1_0000_0000-40_001F_FFFF | 0x7B |
| 124 GB | 39 | 1_0000_0000-1F_FFFF_FFFF | 1_0000_0000-20_001F_FFFF | 0x7B |
| 123 GB | 39 | 1_0000_0000-1F_BFFF_FFFF | 1_0000_0000-1F_C01F_FFFF | desktop |
| 92 GB, 76 GB, 61 GB, 60 GB | 39 | ends 17_FFFF_FFFF, 13_FFFF_FFFF, 10_3FFF_FFFF, F_FFFF_FFFF | ends 18_001F_FFFF, 14_001F_FFFF, 10_401F_FFFF, 10_001F_FFFF | desktop (61 GB: only the NHI and `QEMU0002` at problem 28) |
| 4 GB reserve, host bridge widened (`pci-hole64-size=200G`) | 39 | 1_0000_0000-1_FFFF_FFFF | 1_0000_0000-32_FFFF_FFFF | 0x7B |
| 4 GB reserve | 40 | FD_8000_0000-FE_7FFF_FFFF | FD_8000_0000-105_7FFF_FFFF (past the 40 bits) | 0x7B |
| 4 GB reserve, `pci-hole64-size=1G` | 40 | FD_8000_0000-FE_7FFF_FFFF | FD_8000_0000-FE_7FFF_FFFF | 0x7B |
| plain, `pci-hole64-size=108G` | 40 | - | 1_0000_0000-1B_FFFF_FFFF | desktop |
| plain, `pci-hole64-size=124G` | 40 | - | 1_0000_0000-1F_FFFF_FFFF | desktop |
| plain, `pci-hole64-size=125G` | 40 | - | 1_0000_0000-20_3FFF_FFFF | 0x7B |
| plain, `pci-hole64-size=125G`, `-m 1024` | 40 | - | the same | 0x7B |
| plain, `pci-hole64-size=762G` | 40 | - | 1_0000_0000-BF_7FFF_FFFF | 0x7B |

The rule every row fits: **32-bit Windows 10 does not start when the
host bridge's 64-bit window reaches 128 GB (2^37) or above** (measured
on 10 x86 only; 7 and 8.1 x86 stop with the same 0x7B on the 4G-on layout, and
Vista and 8 x86 do not start there, but the boundary was not read on
them [inf: the same limit]). It is the host
bridge's window that counts, not a bridge's: the widened rows stop with
the root port's window at 4 to 8 GB, and the plain rows stop with no
USB4 model, no enclosure and no 64-bit window of any device. Its length
does not count (the 4 GB window near 1 TB stops), nor the processor's
width from 39 to 40 bits, nor the memory size (1 and 2 GB alike). The
enclosure at power-on does not matter. 44.49's probe closed nine
bridges' windows (its log line above); the host bridge's window is not a
bridge register and stays as the firmware left it, which fits the probe
not helping [inf].

**Where it stops** (QEMU's `pci_cfg_write` trace from power-on, plain,
`pci-hole64-size=762G` against `124G`; `n3t-h762\trace-a.log`,
`n1t-h124\trace-a.log`): both have the firmware's writes and then
Windows' interrupt-link setup on the LPC bridge (`60h` to `6Bh` written
`8xh`, the links disabled); after that the booting run (`124G`) sizes and
programs the root bus's devices (the root port 00:1c.0, the display, the
AHCI controller 00:1f.2 ...), and the stopping run (`762G`) writes no
configuration register again (the trace has writes only, not reads). So
the PCI driver never configures the root bus's devices: the AHCI
controller the system disk is on is never started (hence 0x7B and
`C0000034`), and the recovery environment, the same
system, has no keyboard (the PS/2 controller is behind the LPC bridge on
that bus) [inf from the trace and the screens]. Which test inside
Windows rejects a host-bridge window at or above 128 GB was not read
[inf: a limit of the 32-bit system on the physical addresses it
accepts there; a 64 GB (36-bit) limit would have stopped the rows that
end at 96 GB, which start].

**The processor's width and PAE/NX.** With 36 bits SeaBIOS cannot fit
the 4G-on layout's 256 GB reserve and programs a window beyond the address width
(the same invalid layout "44.48" noted on the `pentium3` bases); that row
says nothing about 10. With 39 and 40 bits the limit is the same. The
`qemu64` model reports PAE and NX in every row (32-bit 8 and 10 need
both); not varied.

### Verdict: the platform's, not this package's

The stop is the emulated platform's layout meeting a limit of 32-bit
Windows: nothing of this package runs (no package installed; the plain
q35 board alone stops it). Nothing to fix here.

**What a real firmware would do [inf]**: with "Above 4G Decoding" on,
a firmware that puts the bridges' prefetchable windows far above 4 GB,
as the 4G-on layout does (7F_8000_0000 and above, the table above)
[emu], gives the host bridge a 64-bit window far above 4 GB, and
32-bit Vista, 7, 8, 8.1 and 10 would stop with INACCESSIBLE_BOOT_DEVICE on such a board as here; with the option off
the host bridge describes no
window above 4 GB, and on the emulator Vista, 7, 8.1 and 10 x86 then
start with the ASM4242's root port at problem 12 and nothing behind it,
while 8 x86 still does not start ("44.49"; the owner's answer Q5,
`roadmap-usb4.md` decisions table row "44.49", takes these systems into
44.49). Not read on hardware.

**What a user should set [inf]**: "Above 4G Decoding" off for 32-bit
Windows Vista, 7, 8.1 and 10 on such a board, the setting that avoids this
stop (with the limits "44.49" records for the option off: the root port
at problem 12, and 8 not starting on the emulator); the same for any
board whose firmware puts the PCI host bridge's 64-bit window at or above
128 GB. Of 64-bit Windows only 10 x64 was read: it starts either way and
reaches the USB4 controller only with the option on ("44.49").

Evidence: `E:\xhci98-vm\p44\p44-w10rec\` (`STATE.md`, `pids.txt`, one
folder per row: `cmdline-a.txt`, the screenshots `a*.png` and `s*.png`;
`w10-on2\a1.png`, `w7-on2\a1.png`, `w81-on2\a1.png` the stop screens; the
traces above; `fwprobe.ps1` with `fw\` the placements read 12 seconds
after power-on).

## 44.21, the third pass - Browse..., a USB stick, a standard user (2026-10-27)

The VM-readable rest of task 44.21 after "44.21, the second pass": P.4's
Browse... and its Quick run on a USB stick, and P.6's standard-user
refusal and "Read as administrator..." button, on 2000, XP and 7 (P.4
also on 10); and ME's P.1 file calls, its P.7 safe removal during a run and the
rest of its P.8. XHCIPERF `6244f522...` (the gated `release` build at
`4d473b8f`), run in the guests only. Every machine **q35**, the
emulator and launcher of the second pass, `-Usb4 asm4242 -EncAtBoot`
(Stor98's drive the ORICO model's FAT NVMe disk); the USB stick a
`usb-storage` device on the board's `qemu-xhci` (driven by `xhci98.sys`
on 2000, XP and 7, by Windows' own driver on 10), its disk an overlay of
the same 128 MB FAT image (so it is labelled "ORICO" too); a
`usb-tablet` for the clicks. Kit `E:\xhci98-vm\p44\p44-perf` (guest
folders `k3`, `xp3`, `s73`, `t10c`; `runs\serial-*-p3.log`).

### The guests

| Guest | From | Stor98's drive, the stick |
|---|---|---|
| 2000 SP4 (`k3`) | an overlay of the second pass's 2000 install of test cut A | `E:`, `F:` |
| XP SP3 (`xp3`) | an overlay of the second pass's XP install | `D:`, `F:` |
| 7 SP1 x86 (`s73`) | a new install of test cut A (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, exit 3, restart); the stick and tablet hot-plugged after it; Windows 7 had left the stick offline (its disk signature the same as the enclosure's, a copy of one image), brought online with `diskpart` | `D:`, `F:` |
| 10 x86 22H2 (`t10c`) | an overlay of the second pass's 10 install; the stick present at the start, so it took `E:` and the enclosure's disk, offline for the same reason, was brought online and took `F:` | `F:`, `E:` |

A standard user `std` made with `net user` on 2000, XP and 7, its runs
started by `runas /user:std`; on 7 an administrator `adm` made too, for
the credential prompt (the image's own administrator has a password
this kit does not have). 10 was read unelevated, as in the second pass.

### P.6: the standard user

| System | Quick (`/size 32`) on Stor98's drive as `std`: the report's Stor98 line | The window as `std`, the Stor98 tab | "Read as administrator..." |
|---|---|---|---|
| 2000 | "Stor98: This disk is driven by Stor98. Reading its counters needs administrator rights." (300.2 / 335.9 MB/s) | the same sentence, the button beside the test list (`k3\g2.png`) | a message: "Run the xHCI98 Disk Benchmark as an administrator to read Stor98's counters." (`k3\g3.png`) |
| XP | the same sentence (108.0 / 93.1 MB/s) | the same (`xp3\g2.png`) | the same message (`xp3\g3.png`) |
| 7 x86 | the same sentence (312.2 / 330.6 MB/s) | the same (`s73\g2.png`); no UAC prompt when `std` started the program | Windows' administrator prompt for `XHCIPERF.EXE` ("unknown publisher"); a wrong password left it open with "Logon failure", No closed it and left the window as it was (`s73\g3.png` to `g5.png`); `adm`'s password: a new window as `adm` opened on the same folder and the first closed, its Stor98 tab "Stor98 3.2.0.0 (x86 build), PCI bus 5 device 0 function 0", "Largest request granted: 256 KiB ...", "Commands at once: 63 ..." (`s73\g8.png`, `g9.png`) |

### P.4: Browse... and the USB stick

| System | Browse... | Quick (`/size 32`) on the stick |
|---|---|---|
| 2000 (`std`) | "Browse for Folder", "Choose the drive or folder to test", the drives listed; `F:` chosen, the folder box `F:\`, the Stor98 tab "not reported by this disk's driver" (`k3\g4.png`, `g5.png`) | 133.8 / 171.2 MB/s sequential, 5.9 / 3.9 MB/s 4 KiB; "Stor98: ... not reported by this disk's driver" |
| XP (`std`) | the same (`xp3\g4.png`, `g5.png`) | 20.1 / 34.2 MB/s, 0.7 / 0.9 MB/s |
| 7 x86 (`adm`, the elevated window) | the same; `F:` chosen (`s73\g10.png`, `g11.png`) | as `std`: 51.7 / 167.2 MB/s, 2.2 / 7.0 MB/s |
| 10 x86 (unelevated) | the same; Stor98's `F:` chosen, its page read (`t10c\g2.png` to `g4.png`) | `E:`, "Disk: QEMU QEMU HARDDISK": 93.1 / 215.9 MB/s, 1.2 / 4.3 MB/s; the run on `F:` read Stor98's page with "Disk: QEMU NVMe Ctrl" |

On every system the drives held no `XHCIPERF.TMP` after the runs
(`dir` to COM1). The Quick preset's 64 MiB does not fit the 128 MB
stick with the free-space reserve ("this run needs 68 MB and must leave
67"), hence `/size 32` on these runs.

### ME: P.1's file calls, P.7's safe removal and the rest of P.8

An overlay of the second pass's ME session `mvc` (Stor98's drive `F:`
here) with an empty CD drive added (`ide-cd` on the piix3-ide secondary
channel, `E:`); `src\p1.c`, a kit probe run in the guest only (as the
43.50 kit's `aspiprobe` level 14, the drive as an argument).

| Leg | Result |
|---|---|
| P.1, the counter | "QueryPerformanceFrequency 1: 1193180" |
| P.1, an `OVERLAPPED` on `F:\OV.TMP` (opened with `FILE_FLAG_OVERLAPPED` and `FILE_FLAG_NO_BUFFERING`) | the open succeeded; `WriteFile` and `ReadFile` with an `OVERLAPPED` both refused, error 87, 0 bytes |
| P.1, `FILE_FLAG_DELETE_ON_CLOSE` on `F:\DOC.TMP` | the open succeeded, a 64 KiB write too, a second handle opened while it was open; after the first close and after both the file was still there (attributes 20h): the flag is ignored |
| P.7, safe removal while running (`mej`, another overlay of `mvc`, Stor98's drive `E:`) | during `/test seq,1024,1,1 /ops write /time 60 /passes 3 /size 16 /folder E:\`, `C:\XHCI98\XHCIINST.EXE /eject 1`, answered Y: "Windows refused: the device is in use (a program may have a file open on it, or its driver refused). Close any program using it and try again.", "No changes were made.", exit code 4 (`mej\e3.png`); the run went on and ended normally, all three tests 44.5 to 46.9 MB/s (`e4.png`) |
| P.8, the CD drive (`/folder E:\`) | "Refused: Only fixed and removable drives are tested; not network, CD or DVD drives." (the program's kept folder was `E:\`, the letter the enclosure's drive had in `mvc`) |
| P.8, a half-prepared leftover | a Quick run with `/size 500` on `C:` ended by cutting the guest's power (QMP `quit`) during its preparation (Close Program's End Task had brought "Stop the test and close?" and then Windows' "not responding" window); at the next cold start the window offered "A test file from an earlier run is still here: C:\XHCIPERF.TMP (80 MB) Delete it?" (`mp8\o1.png`); No kept it |
| P.8, a header without a record | the record removed (`regedit /s` deleting `Leftover` and `LeftoverPath` under `HKCU\Software\xHCI98\XHCIPERF`); the window's start offered nothing; `/run /preset quick /size 32 /folder C:\` asked "This looks like a test file of this tool's, but this computer has no record of creating it: C:\XHCIPERF.TMP (80 MB) Delete it?" (`mp8\o6.png`); Yes: the run went on, and `C:\XHCIPERF.TMP` was gone afterwards |
| P.8, the free space and the Windows drive's reserve | Stor98's drive (`mw`, a write test with the default 64 MiB): "Refused: The drive has 132 MB free; this run needs 68 MB and must leave 67 MB. Lower the file size or choose another drive."; the Windows drive (`mrs`, another overlay of `mvc`; `C:` filled to 367 MB free by a 3150 MiB file made by its end of file alone, the 43.50 kit's `aspiprobe` level 16), Quick on `C:\`: "Refused: The drive has 367 MB free; this run needs 68 MB and must leave 536 MB. ..." (the system drive's reserve, a tenth of the volume and at least 512 MiB) |

### Left

Nothing that a virtual machine can read. The bench's (record 29 section
14.2): 98 SE at a high resolution on real hardware, the Quick preset's
2 s steps on a real IDE disk, P.10.

## 44.58 Stor98's SCSI status on the requests it answers itself (2026-10-27)

The finding: 44.21's second pass ("44.21, the second pass", the end)
read on ME, after an XHCIPERF run had ended, a target status 05h on
every ASPI request to Stor98 (`SS_COMP`, host adapter status 00h, the
sense empty).

**The reading in the driver.** Stor98 never read or cleared the SRB's
SCSI status byte on these completions:
`ScsiSuccess` (`src/stor98/src/nvme2k_scsi.c`: TEST UNIT READY,
VERIFY, START STOP UNIT, INQUIRY, READ CAPACITY, MODE SENSE, READ
BUFFER; only REQUEST SENSE set GOOD before it), the BUSY returns
(`EngBusyOrFail`), the abort and reset SRBs completed at once (`LcAbortSrb`,
`LcResetSrb`; parked ones end through `EngParentStatus`), the fail-fast of a function other than EXECUTE SCSI
(`LcFailSrb`), the I/O control answers, the LOG SENSE and SAT answers
built in `nvme2k_cpl.c` from an NVMe log page, the 9x removal's SHUTDOWN, and the answer for
another target in `HwStartIo`. Each completed with whatever byte the
port's SRB held. The NVMe-backed parents (`EngParentStatus`), the
gone and removal fast-fails and every CHECK CONDITION path set it
already. That 05h was such a stale byte is inferred from that code;
the legs below read the fix.

**The change** (`07e85e96`, `8facc315`; Stor98's PLAN "SCSI status of
a completion"): `HwStartIo` starts every SRB's SCSI status as GOOD;
`ScsiSuccess` sets GOOD; `EngCompleteSrb`, the completion every path
ends in, and the answer for another target or LU give every SRB status
other than ERROR the status GOOD (`ScsiCompletionStatus`,
`src/stor98/src/sense.c`). An ERROR keeps what its path set: CHECK
CONDITION with its sense as before (autosense or kept; another LU's
sense is never kept, so without usable autosense it has none, as before), or GOOD. Host
unit test `src/stor98/tests/unit/sense_ut.c` case 10 (NT and 9x
`SRB.H`): a stale 05h through every port-level status, ERROR with and
without autosense, the success. Reviewed and revised.

**Builds.** `scripts\build-driver.cmd all` x86 at `07e85e96`: `BUILD +
GATES PASSED (debug release qemu)`; `all -amd64` at `8facc315`: the same.
`8facc315` changed comments and Stor98's PLAN only.

**The vehicle.** Development host A; QEMU **q35** from
`E:\xhci98-vm\q35-bases` (ME on piix3-ide, 98 SE and 7 x86 on AHCI),
the launcher's emulator `asm4242-20261025-2230` (`da60139f...`), TCG,
headless, `-Usb4 asm4242 -EncAtBoot` (the ORICO model's 128 MB FAT NVMe
disk at power-on); ME and 98 SE 1 vCPU and 512 MB, 7 x86 2 vCPUs and
2 GB. Kit `E:\xhci98-vm\p44\p44-s98st` (`runs\serial-*.log`; not files
a clone has).

| Item | Where from | SHA-256 |
|---|---|---|
| Test cut | `make-release.ps1 -Arch x86 -Flavor release -TestLabel p44s98st -NoNonFree -NoWdmFree -NoDriverPack -SkipQualtool` on the x86 build above | zip `b9c45db3...` |
| `stor98.mpd`, `stor98.sys`, `S98VSD.VXD` | that cut's `release-x86` | `0d706f9f...`, `886d7818...`, `9e5542db...` |
| `XHCIPERF.EXE` | the same build | `5b373026...` |
| `XHCIPOLD.EXE` | 44.21's `XHCIPERF.EXE` of `078e3d4c`, which took a target status other than 00h as a failure | `9e958593...` |
| `ASPI2.EXE` | 44.21's probe (INQUIRY, INQUIRY C9h, READ BUFFER to every disk target through `WNASPI32.DLL`) | `971ee1c4...` |

Installs: ME (an overlay that had answered the q35 and enclosure
wizards) and 98 SE by `XHCIINST /dir release-x86 /yes /nopause
/thunderbolt /stor98 /s98vsd`, exit 3 (on 98 SE the documented "xhci98
xHCI Host Controller Disk" prompt answered with `C:\XHCI98`), Restart
(QEMU ends), a cold start; Stor98's disk `E:`. 7 x86 by `INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98` from an elevated prompt,
the publisher prompts answered, exit 3 ("Stor98 Storage Controller for
NVMe SSDs: stor98 3.2.0.0, started"), a restart; "ORICO (D:)".

**The legs.** 98 SE and ME: one batch in one session, each report and
probe output copied to COM1: `ASPI2 1`; XHCIPERF `/run /preset study9x
/min 128 /max 256 /size 16 /folder E:\ /exit`; `ASPI2 2`; the same run
again; `ASPI2 1`; the same run by `XHCIPOLD.EXE`; `ASPI2 1`.

| System | Read | Verdict |
|---|---|---|
| ME (`mt1`) | every probe request, before the first run, between the runs (both rounds) and after each: `Status` 01h, `HaStat` 00h, **`TargStat` 00h**, the sense empty, 0 to 5 ms; the Stor98 page on 12 of 12 steps in each of the three runs, `XHCIPOLD.EXE`'s included (before the fix 0 to 3 of 12 after a probe, 44.21); no "counters could not be read" | pass |
| 98 SE (`st1`) | the same: `TargStat` 00h on every request, 12 of 12 pages in each run, `XHCIPOLD.EXE`'s included | pass |
| 7 SP1 x86 (`w7`), regression | an 8 MiB file copied to `D:` eight times and compared (`fc /b`: "no differences encountered" 8 of 8); XHCIPERF Quick (`/size 16 /folder D:\`): 275.7 / 316.2 MB/s sequential, the Stor98 page on every step ("Stor98 3.2.0.0 (x86 build), PCI bus 5 device 0 function 0", failed 0); Quick in `%TEMP%` on the system disk: "not reported by this disk's driver" | pass |

## 44.45 (continued, 2): the remaining owed legs, 2026-10-27

The legs 44.45's second section left owed, on QEMU `q35` with its
ASM4242 layout (emulator build `asm4242-20261027-boardint`,
`qemu-system-x86_64.exe` SHA-256 `deca3fcb...`; the root port
`bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref32-reserve=4M,
hotplug=off`, `fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, both tunnel
ports `power-controller=off`) unless a row says otherwise. Packages: the
same two as that section (`6b35a399`; the code up to this section's base
`ffde63d4` differs from it only in comments and installer texts): the x86
test cut (`xhci98.sys` SHA-256 `15927FAE...`, `xhcicm.sys` `F257E875...`,
`stor98.sys` `F873BE59...`, `stor98.inf` `3B793BF3...`, `XHCIINST.EXE`
`318DD20A...`) and the amd64 package staged unsigned by `make-package.ps1
-Arch amd64` (`xhci98.sys` `1FC238F6...`, `xhcicm.sys` `B15F066A...`,
`stor98.sys` `646C6706...`), its drivers and catalogs signed inside each
x64 guest with a throwaway certificate made there and trusted in its
machine Root and TrustedPublisher stores (43.36's method; the x64 bases
run with test signing on; nothing signed on the host). Each system was
installed once by `INSTALL.BAT /console /yes /nopause /thunderbolt
/stor98` on a fresh overlay of its q35 base (exit code 3; on XP the
first run stopped at Windows' wizards for the new root hubs, exit code
4, and the second ended with 3), shut down, and every leg ran on an
overlay of that installed disk. The enclosure is the ORICO model
(`usb4-device ... device-model=asm2464`) on USB4 port 1, plugged by QMP
`device_add` at the desktop, unless a row says otherwise. 2000 is not in
these legs (its hot-plugged drive's start is 44.50's).

### T.3's refusal: the dock behind the ASM4242 layout's port window

The refusal needs a unit whose subtree holds a function other than a
driverless drive. The emulator's dock (`usb4-device ... dialect=usb4,
dock=on`: a TI XIO3130 switch with three downstream ports, the NVMe, an
e1000e and a qemu-xhci) was read first for where its Code 12 comes from.

| Leg | System | Result |
|---|---|---|
| dock hot plug, the ASM4242 layout's window | XP | as in 44.45's second section: the first downstream port and the drive enumerated, the other two downstream ports at problem 12, the NIC and the dock's xHCI never enumerated. `info pci` after XP's assignment: the switch's upstream port given 4 KB I/O, 64 MB memory and 2 MB prefetchable inside the tunnel port's window (12 KB, 384 MB, 2 MB); the first and second downstream ports both programmed with the upstream port's whole windows, the third with none. The mark service: "its unit is not restarted: a bridge of the enclosure is not installed and started yet" |
| the same, 20 KB I/O a port (`fw-io-pad=20K`, the root port's `io-reserve=40K`) | XP | the same pattern: the second and third downstream ports at problem 12, the upstream port 4 KB of I/O |
| the same dock, the ASM4242 layout's window | 7 x86 | all three downstream ports placed in windows of their own (the NIC's port with the 4 KB of I/O), the drive on Stor98, the NIC on `e1qexpress`, the dock's xHCI on xHCI98 (problem 1 until a restart Windows asked for) |
| the dock at power-on (SeaBIOS lays out the windows) | XP | every downstream port with a window of its own; the dock's xHCI on xHCI98 and started, the NIC at problem 28 (no XP driver), the drive at problem 28 without the id (`ConfigFlags` 0x40) |

So the port window is large enough for the dock's tree: 7 places it in
the same window, and XP fails the same way with 20 KB of I/O. The Code
12 is XP's own placement of a hot-added switch with several downstream
ports [inf: read only on the emulated switch; this package assigns no
bridge resources on NT and its filter passes the resource requests
down]; XP did the same with a hot-plugged two-level chain (below). The
refusal was therefore read with the dock present at power-on, and with
the mark's own restart off (`Restart` 0), so that the mark's restart
does not run first:

| Leg | System | Result |
|---|---|---|
| T.3 refusal (dock at power-on, `Restart` 0) | XP | after the bridges' and the dock xHCI's installs and the drive's wizard (no driver found): "Stor98: PCI\VEN_1B36&DEV_0010...\8&214C4045&0&000000000018 has no tunnel id and no driver; its unit is not restarted: something below the unit runs, is disabled or is not a drive. Unplug the enclosure and plug it in again, and Windows' own wizard offers Stor98"; no restart made. **Pass** |

### The veto, a declined wizard, two drives sharing one restart (XP, `Restart` 0)

| Leg | System | Result |
|---|---|---|
| the veto: an open handle on the unit | XP | a test helper (guest only, not tracked) found the unit's devnode (the enclosure's upstream port, `PCI\VEN_1B21&DEV_2463...\6&1A528B6B&0&00000018`), read its PDO name (`\Device\NTPNP_PCI0016`) and opened it through a DOS device name: "open failed 1" (`ERROR_INVALID_FUNCTION`: the create is refused on the bridge's stack). No other way to hold the unit's stack open was found here (the gate passes only bridges with a driver and driverless drives, none of which offers a handle to user mode on this guest): **the veto was not reproduced**; its branch (`DI_NEEDRESTART` or `DI_NEEDREBOOT` after `DIF_PROPERTYCHANGE`, or a failed call, the budget spent, the re-plug sentence) is read in the code only |
| T.3 first plug, Q2, a declined wizard | XP | the same plug (`Restart` 0): the drive's first wizard ("PCI Device") found no driver and was finished with "Don't prompt me again" ticked; then "Stor98: ...\8&35A4905B&0&000000000018: its install set to run again before its unit's restart (flags 00000040 -> 00000020)", "Stor98: restart made: PCI\VEN_1B21&DEV_2463...\6&1A528B6B&0&00000018, so that its drives come back through the filter chain with the tunnel id (no id: the filter chain did not reach its first enumeration (a never-seen enclosure's bridges))"; Windows' wizard then named "Stor98 Storage Controller for NVMe SSDs" and was cancelled; 58 s after the restart "Stor98: ... carries the tunnel id and has no driver: Windows' wizard offers Stor98 for it; left to the user". Three minutes later: the id first in `CompatibleIDs`, no driver, `ConfigFlags` 0x20, no second restart. **Pass** (Q2 on XP, and the declined wizard left alone) |
| two drives below one unit (a two-level chain at power-on) | XP | `usb4-device ... dialect=usb4,downstream-ports=1` (the emulator's default device: a XIO3130 switch, its NVMe and one downstream USB4 port) with a second `usb4-device ... parent=enc0` (its NVMe directly on that port) behind it, each with its own NVMe disk, `-global usb4-tunnel-dsp.power-controller=off`. Plugged at the desktop, XP gave the second downstream port the first one's windows again and the second drive was never reached, as with the dock; so both were present at power-on. Both drives without the id, both wizards finished with no driver; then both "its install set to run again before its unit's restart (flags 00000040 -> 00000020)" (`...\8&214C4045&0&000000000018` and `...\8&F821D8C&0&000800000018`, the same second) and one "restart made: PCI\VEN_104C&DEV_8232...\6&1A528B6B&0&00000018"; both came back with the id, two wizards named Stor98 and installed it; both on Stor98, started, with their disks: "ORICO (D:)" and "ORICO (F:)". **Pass** |

None of these XP legs met 44.45's second section's finding (a drive's
failed install surviving the mark's restart): with `Restart` 0 the tag's own path set
the install to run again each time.

### The service's install within seconds (7 x86, 8.1 x86)

| Leg | System | Result |
|---|---|---|
| first plug | 7 x86 | the drive arrived with the id first and Windows installed Stor98 by it ("Matching DeviceID=pci\xhci98_tunnel&cc_010802", `oem6.inf`), "ORICO (D:)"; the service logged only "a Windows installation is pending": its install is not reached on 7 at a first plug, because the chain gives the id there |
| the service's install over nothing | 7 x86 | to leave a drive driverless with the id, Stor98's package was removed from the store before the plug (`pnputil -d oem6.inf`): the drive at problem 28, the service "Stor98Inf names C:\Windows\INF\oem6.inf, a file that is not there; no drive is given Stor98"; then the package staged again (`pnputil -a`, the publisher prompt answered, published again as `oem6.inf`). The service's next poll: "giving ...\8&13AE00C&0&000000000018 Stor98: it has no driver (status 01802400 problem 28; member 0200; it carries the tunnel id; Windows put no driver or another one on it)" (12:13:28, guest time), the class set to SCSIAdapter, the model selected, "DiInstallDevice answered success" (12:13:33), "now runs Stor98"; `Service` stor98, "ORICO (D:)". **Pass** (5 s from the decision to Stor98 running) |
| first plug, the 8.1 arm | 8.1 x86 | plugged 14:41:51; `stornvme` bound first (no id on 8.1, by design); "switching ...\8&13AE00C&0&000000000018 from stornvme (member 0200; no id on Windows 8.1 and later, by design)" at 14:42:00, the class model selected, "DiInstallDevice answered success", "now runs Stor98" at 14:42:52; `Service` stor98, "ORICO (E:)". **Pass** (the switch decided 9 s after the plug, Stor98 running 61 s after it) |

### The first plug on the x64 systems

| Leg | System | Result |
|---|---|---|
| first plug | Vista x64 | the id first in `CompatibleIDs`, Windows installed Stor98 by it (`Service` and `ActiveService` stor98), "ORICO (D:)" opened; the service "a Windows installation is pending". **Pass** (a "Device driver software was not successfully installed" balloon was shown, its device not read; the base's SM Bus controller has no driver) |
| first plug | 7 x64 | the same: the id first, Stor98, "ORICO (D:)", "Your device is ready to use". **Pass** |
| first plug | 8 x64 | the same: the id first, Stor98, "ORICO (E:)". **Pass** |
| first plug | 8.1 x64 | `stornvme` first (no id), then the 8.1 arm: "switching ... from stornvme" (15:16:35; the plug at 15:15:00, one poll deferred for "a Windows installation is pending"), "DiInstallDevice answered success", "now runs Stor98" (15:17:11); "ORICO (E:)". **Pass** |
| first plug | 10 x64 | `stornvme`, no id, "ORICO (E:)"; the service writes the mark only: as designed (Stor98 on 10 is the installer's case 3b) |

XP x64's first plug is 44.45's second section's (with its finding on the
mark's restart).

**Verdict.** Every leg owed by 44.45's second section that can be made
here is read and passes: T.3's refusal (with the dock at power-on, its
hot-plug Code 12 being XP's placement of a hot-added multi-port switch,
not the window's size), a declined wizard, two drives sharing one
restart, Q2 on XP, the service's install over a driverless drive on 7
and over `stornvme` on 8.1, and the first plug on Vista, 7, 8, 8.1 and 10
x64. The timings are not all from the plug: on 7 the service's install
is reached only when Windows' own install has left the drive driverless
(here by removing the package from the store and staging it again), and
was read as 5 s from the service's decision to Stor98 running; on 8.1
the switch was decided 9 s after the plug and Stor98 ran 61 s after it.
Not reproduced: the veto (no way to hold the unit's stack open was found
here). Nothing of this package's was changed. Still open around 44.45:
2000's hot-plugged drive's start (44.50) and XP's failed install across
the mark's restart (44.45's second section's finding, for the
coordinator). Evidence: `E:\xhci98-vm\p44\p44-hp45\` (`xp\` (the dock legs
`dk` and `e3`, `infopci-*.txt`), `xpr0\` (`Restart` 0: `rf`, `vt`, `td`,
`tc`), `w7\`, `w81\`, `vx64\`, `w7x64\`, `w8x64\`, `w81x64\`, `w10x64\`,
each with `serial-*.log` and screenshots; `pids.txt`; `hashes.txt`); the
test helper in `E:\xhci98-scratch\p44-p44-hp45\openpdo\`.

## 44.51 - The 64-bit Systems Through UEFI: 10 x64 and 8.1 x64 on OVMF (2026-10-27)

A task of Phase 44 (the owner, 2026-10-27; decisions table, row
"44.51"). On an ASM4242 platform with a CSM a 64-bit Windows starts through
UEFI when the CSM is off ("Re-Size BAR" on turns it off), and every 64-bit leg on an
emulated USB4 host until now started through SeaBIOS (44.25's OVMF guest
read only the installer's refusal with Secure Boot on). Here 10 x64 and 8.1 x64 were installed
afresh on q35 with UEFI firmware and read on the emulated ASM4242 host:
a cold start with the ORICO enclosure attached, a hot plug, and a
hibernation and resume with the drive.

### The vehicle

- **Firmware.** OVMF as QEMU ships it, from the share directory of the
  QEMU the q35 legs use (`usb4-emu` `vms\qemu-baseline\share`):
  `edk2-x86_64-code.fd` (the build without Secure Boot) SHA-256
  `33090cc0...6dbb2a`, read-only, and a private variable store per
  guest made from `edk2-i386-vars.fd` (`5d2ac383...5d1e`; no platform
  key, so Secure Boot is off: `Confirm-SecureBootUEFI` "Variable is
  currently undefined: 0xC0000100"). `%firmware_type%` UEFI, boot loader
  `winload.efi`, `SystemStartOptions` ` TESTSIGNING  NOEXECUTE=OPTIN
  NOVGA`.
- **Emulator.** `E:\xhci98-vm\q35-bases\bin\asm4242-20261027-boardint`
  (44.45's build, QEMU 11.1.2 `v11.1.2-246-gd2443fa`,
  `qemu-system-x86_64.exe` `deca3fcb...`), `-machine q35`, TCG, two
  vCPUs, 4 GB, `-cpu qemu64,+3dnowprefetch`, the system disk on the
  ICH9 AHCI port 0, the transfer disk on port 1.
- **Bases** (new, read-only, each with `README.txt`, its variable store
  template and a copy of the firmware): `q35-bases\w10x64-uefi`
  (Windows 10 Pro 22H2 19045.2965, `disk.qcow2` `ba66deee...`) and
  `q35-bases\w81x64-uefi` (8.1 Pro with Update 9600.17415,
  `38bc96fb...`). Installed unattended from the same ISOs as the SeaBIOS
  pristine images (`win8-10\RECIPE.txt`) onto a GPT disk (EFI, MSR,
  Windows), with the pristine images' answer-file settings (test-signing
  on in specialize, the same account and autologon, Windows Update and
  hibernation off) and a logon task that runs the transfer disk's
  script; TCG throughout, 45 minutes (8.1) and 63 minutes (10) from the
  ISO's start to the first logon's power-off. A settle start on the
  ASM4242 profile without the enclosure let Windows install the switch
  ports (the NHI: problem 28, no driver), as the SeaBIOS bases were made.
- **Package.** The x86 test cut of this branch at `726ef3ef`
  (`make-release.ps1 -Arch x86 -Flavor release -TestLabel`, zip
  `D59E967B...`; its `tools\` carry the x64 installer) with the `release`
  amd64 package staged by `make-package.ps1` without the key and its
  catalogs made by the packager's own `New-PackageCatalogFile`; in each
  guest a throwaway certificate (10: `New-SelfSignedCertificate`; 8.1:
  WDK 7.1 `MakeCert`) in Root and TrustedPublisher signed the four
  `.sys` and five `.cat` files there, as 43.36 did; then `INSTALL.BAT
  /console /yes /nopause /thunderbolt /stor98`. Gated builds of
  `726ef3ef`: `release` x86 and `release -amd64` passed. Staged amd64
  hashes: `xhci98.sys` `6D20C2BB...`, `xhcicm.sys` `8DD6FDBE...`,
  `stor98.sys` `8EF3FC4D...`, `xhciuas.sys` `5378E04F...` (before the
  guest's signature).
- **Layout of the legs** (the ASM4242 layout with "Above 4G Decoding" on,
  as 44.49's 4G-on layout, with 44.45's switch model): the root port
  `bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref64-reserve=256G`
  (hot-plug on), `usb4-asm-usp ...,fw-bus-pad=48,fw-io-pad=12K,
  fw-mem-pad=384M,fw-pref-pad=128G`, both tunnel ports
  `power-controller=off`; the enclosure `usb4-device ...,
  device-model=asm2464` on USB4 port 1 at power-on (cold start) or
  plugged on port 2 by QMP `device_add` (hot plug).
- **Readings.** From the guest to COM1 (one script, a minute's pause
  after each run; a run took three to six minutes under TCG): `wmic`
  `Win32_PnPEntity` (problem code, service), the memory, I/O and IRQ
  resources Windows gave each PCI function (`Win32_PnPAllocatedResource`),
  the volumes, `XHCIIN64 /devices`, `XHCITR64 /report` (the connection
  manager's counters and log), the last Kernel-Boot event 27 (boot
  type); QMP `info pci` for the registers.

### What the firmware places

`info pci` when OVMF hands over to the boot manager (serial "BdsDxe:
starting Boot0004"), the same in every OVMF start of this layout, and
the SeaBIOS bases (`w10x64` `915dd350`, `w81x64` `27776857`) on the
same command line at POST:

| | SeaBIOS (`bios-256k.bin`) | OVMF |
|---|---|---|
| Root port 00:03.0 buses | 1-104 (`bus-reserve` honoured) | 1-104 |
| Root port memory | `CE000000`-`FE7FFFFF` (776 MB, `mem-reserve` honoured) | `81200000`-`817FFFFF` (6 MB: what its children need) |
| Root port prefetchable | `40_0000_0000`-`7F_FFFF_FFFF` (256 GB) | `C0_0000_0000`-`C0_3FFF_FFFF` (1 GB) |
| Root port I/O | `1000`-`6FFF` | none |
| Tunnel port 02:00.0 | buses 3-50, memory 384 MB, prefetchable 128 GB from `40_0000_0000`, I/O 12 KB | buses 3-50, memory 2 MB, prefetchable 512 MB at `C0_2000_0000`, no I/O |
| Tunnel port 02:01.0 | buses 51-98, 384 MB, 128 GB from `60_0000_0000`, 12 KB | buses 51-98, 2 MB, 512 MB at `C0_0000_0000`, no I/O |
| The model's window pads | laid out (the ASM4242 layout's sizes) | not laid out: "fw-mem-pad=0x18000000 does not fit the mem window of the port above (0x81200000-0x817fffff)", the same for the prefetchable pad, and "the port above has no io window" |
| The enclosure at power-on | buses 3-5, its bridges' memory `CE000000`-`CE1FFFFF`, prefetchable `40_0000_0000`-`40_1FFF_FFFF`, the NVMe's BAR `CE000000` | buses 3-5, memory `81400000`-`814FFFFF`, prefetchable closed, the NVMe's BAR `81400000` |
| xHCI, NHI | below 4 GB (`FE000000`, `FE200000`) | below 4 GB (`81700000`, `81600000`) |

So OVMF honours the root port's bus reservation and nothing else of it:
the memory and prefetchable reservations are not applied, every
prefetchable window is above 4 GB (from 768 GB), and each hot-plug port
of the switch gets OVMF's own 2 MB and 512 MB. The 64-bit BARs of the
NVMe, the xHCI and the NHI are non-prefetchable and stay below 4 GB on
both firmwares. With the root port's hot-plug off (the ASM4242 layout's slot,
as 44.45's legs), OVMF applies none of the reservation: buses
1-6, each tunnel port one bus (3-3, 4-4), the same 2 MB and 512 MB
(`settle81\infopci-boot.txt`, `w81c\infopci-preplug.txt`). A real firmware's own UEFI layout (48 buses,
384 MB and 128 GB per tunnel port) is not reproduced on either setting
[inf: OVMF takes a bridge's padding only for a hot-plug-capable port,
from QEMU's hint on Red Hat bridge ids only, as the switch model's
comment says; why the root port's memory and prefetchable reservations
were not applied was not read in the firmware's source].

**What Windows does with it.** On this layout 10 x64 and 8.1 x64 keep
the firmware's windows on both firmwares (`info pci` at the desktop differs from the
hand-over only in the Interrupt Line registers, and on OVMF in the
enclosure's upstream port, which 10 gave the 512 MB window of its
tunnel port). This differs from 44.49's 10 x64 on the 4G-on layout without the
model's pads, where SeaBIOS left the windows at `7F_8000_0000` and 10
moved them to `40_0000_0000`.

### Legs (q35, OVMF, the layout above)

| Leg | System | Result |
|---|---|---|
| install, the ORICO at power-on | 10 x64 | the throwaway signature "Valid" on all nine files; exit 3; the run added the tunnel-port filter to both root ports and the four ASM4242 ports, installed `xhcicm` on the NHI and Stor98 on the drive ("Stor98 now runs Stor98 Storage Controller for NVMe SSDs"). After the restart: every PCI function problem 0, the NVMe on `stor98`, "ORICO (E:)" with its file, `XHCIIN64 /devices` "USB4 (version 1.0), port 1, depth 1, approved ... PCIe up (found at start)": **pass** |
| the same | 8.1 x64 | the same (SignTool exit 0 on all nine; exit 3; E: on Stor98, problem 0 throughout): **pass** |
| cold start | 10 x64, 8.1 x64 | after a shutdown, a new QEMU with the enclosure at power-on: boot type 0x0, E: on Stor98, all problem 0, the connection manager "Stage: active", "Unexpected removals: none": **pass** on both |
| hibernation and resume | 10 x64, 8.1 x64 | `powercfg /h on`, `shutdown /h` with E: mounted (QEMU powered off), then a new QEMU with the enclosure: boot type 0x2; E: on Stor98 with its file, all problem 0; the connection manager `cm.suspends` 1, `cm.resumes` 1, `cm.reenum.hold.passed` and `.released` for 0200 and 0208 (both tunnel ports): **pass** on both. Windows came back at the lock screen; an ACPI power-button press there (QMP `system_powerdown`) shut neither down within 8 minutes; signed in, `shutdown /s` powered off in 49 s (10) and 21 s (8.1) with the drive attached |
| hot plug | 8.1 x64 | installed without the enclosure, restarted, then the enclosure plugged on port 2: at the next reading (it started within a minute of the plug) the enclosure's upstream port on bus 51 with buses 52-53 below it, inside 02:01.0's range, its memory `81300000`-`813FFFFF` in that port's 2 MB, its prefetchable window closed, the NVMe's BAR `813FC000`, Stor98 started on it, "ORICO (E:)", every function problem 0; the connection manager `u4.routers.enumerated` 1, `cm.reenum.ups` 1: **pass** |
| hot plug | 10 x64 | the enclosure's upstream port on bus 51 given the whole of 02:01.0's range (buses 52-98, memory `81200000`-`813FFFFF`, prefetchable `C0_0000_0000`-`C0_1FFF_FFFF`), its downstream port (on bus 52, bus 53 below it, `81300000`-`813FFFFF`) and the NVMe (on bus 53, BAR `813FC000`) as on 8.1; the drive on `stornvme` (Code 0, "ORICO (E:)"), as designed on 10 and as 44.45's 10 x86 hot plug; the mark service listed it: **pass** (as designed). `shutdown /s` afterwards in 51 s |

Against the SeaBIOS runs of the same systems (the q35 bases' own checks
with `3.2.0.0`, ORICO at power-on, E: on both; 44.49's 4G-on layout row
for 10 x64, every function problem 0 but the NHI's 28, no driver, before
an install): no problem code differs (the NHI is on `xhcicm` here once the
package is installed). What
differs is where the windows are and how large: on OVMF the tunnel
ports have the 2 MB and 512 MB that one ORICO needs, not the firmware's
384 MB and 128 GB, so a device behind a tunnel port that needs more
than 2 MB of memory or 512 MB of prefetchable space would not fit
without Windows rebalancing [inf: not read; no such device model here].

**The ASM4242 layout's slot setting on OVMF.** The same legs with the root port's
hot-plug off (44.45's `hotplug=off`, everything else as
above): OVMF then numbers the root port 1-6 and each tunnel port one bus
(02:00.0 3-3, 02:01.0 4-4). At the desktop the root port's memory
window was `FE600000`-`FEBFFFFF`, with the four ports' windows (2 MB
each for the tunnel ports) and the xHCI's and NHI's BARs inside it, and
the prefetchable windows at `C0_0000_0000` as in the table (`w10c`,
`w81c` `infopci-preplug.txt`); who placed the memory windows there,
OVMF or Windows, was not separated. Installed
without the enclosure, restarted, the enclosure plugged on port 2: on
8.1 x64 and 10 x64 alike the connection manager brought the tunnel up
(`u4.routers.enumerated` 1, `cm.reenum.ups` 1, "port 2, depth 1,
approved ... PCIe up") and the enclosure's upstream port
`PCI\VEN_1B21&DEV_2463` stayed at **problem 31** with nothing behind it
over the next two readings on each (five to seven minutes apart;
`info pci`: its secondary bus 0), where the bus numbers are missing: 44.45's first
item, back on OVMF, and Windows did not renumber. So on this firmware
the hot plug needs the root port's bus reservation applied, which OVMF
does only for a hot-plug-capable port; whether a real firmware's own UEFI
numbers its tunnel ports as record 22 section 18.1 has them with the CSM off is not read here.

### 7 x64 and Vista x64

Neither starts under this firmware (no CSM, `-vga std`):

- **7 SP1 x64** (`en_windows_7_professional_with_sp1_vl_build_x64_dvd_u_677791.iso`):
  OVMF loads the DVD's boot manager, Setup's "Starting Windows" appears
  with its animation frozen; about nine minutes later the same frame,
  the vCPU halted (`HLT=1`, `RIP=fffff880034691f2`), no I/O on the DVD
  for over seven minutes.
- **Vista SP2 x64** (`en_windows_vista_sp2_x64_dvd_342267.iso`): "Windows
  is loading files..." with the bar full, then nothing for 19 minutes,
  the vCPU halted (`RIP=fffffa6001708062`), no I/O.

So no UEFI base was made for them [inf: the known want of a video BIOS
(INT 10h) that 7 and Vista have at the start without a CSM; no video
shim was tried]. A real firmware's CSM-off start of 7 x64 is not read here.

### Not read

A real firmware with "Above 4G Decoding" on under its own UEFI (48 buses,
384 MB and 128 GB per tunnel port: the emulator does not give them on
OVMF); Secure Boot on (44.25 read the installer's refusal); the
owner-signed x64 package (the throwaway signature stands for it in
test-signing mode); 7 x64 and Vista x64 through UEFI (above).

Kit, scripts, logs and screenshots: `E:\xhci98-vm\p44\p44-uefi\`
(`STATE.md`, `pids.txt`, `inst\` the installs, `launch-uefi.ps1`,
`xfer\auto\` the guest scripts, one folder per guest: `serial-*.log`,
`infopci-*.txt`, `summ-*.txt`; `shots\`); the bases' `README.txt`.

## 44.55 - Controller names in Device Manager: the ASM4242's controllers, the widened table, the ASM4242 router by FriendlyName (2026-10-27)

What was read: design record 23 section 5.13.8 (revision 48) on the
emulated ASMedia ASM4242 - its xHCI `1B21:2426` named
by the table and by the 32-bit INF's 9x models line, its USB4 router
`1B21:2425` named by the installer's FriendlyName (no id line, so no
change of rank), its downstream ports named by the filter INF on 7 -
and QEMU's `qemu-xhci` (`1B36:000D`) named as before.

**Build and package.** Branch `p44-names` at `3edf3b16`; gated builds
x86 and amd64 `all` PASS (development host A). Test cut `make-release
-Arch x86 -Flavor release -TestLabel "p44-names legs"` (version text
`3.2.0.0`, the branch's): zip SHA-256 `aa7e10f5...`; in `release-x86\`
`xhci98.inf` `2fc69bfb...`, `xhci98.sys` `5da71b82...`, `xhcicm.sys`
`104478b6...`; `tools\xhciinst\XHCIINST.EXE` `5fbcd709...`.

**Machine.** q35 bases (`E:\xhci98-vm\q35-bases\<os>`, overlays), the
launcher's `-Usb4 asm4242` (root port 00:03.0, the ASM4242 switch
`2421`/`2423`, its xHCI `1B21:2426` and NHI `1B21:2425`, no enclosure)
and `qemu-xhci` behind root port 00:1c.0, on the usb4-emu snapshot
`asm4242-20261027-boardint` (`qemu-system-x86_64.exe` `deca3fcb...`); the
transfer folder as the second disk. Install from a batch on it:
`XHCIINST /dir release-x86 /yes /nopause /thunderbolt` on 98 SE and ME,
`INSTALL.BAT /console /yes /nopause /thunderbolt` elevated on 7; then the
devnodes' `HKLM\Enum\PCI` (9x, `regedit /e`) or `FriendlyName` and
`DeviceDesc` (7, `reg query`) to COM1.

| System | Install | Device Manager after the restart | Result |
|---|---|---|---|
| 98 SE (q35, AHCI by INT 13h) | exit 3; "Device Manager names: named: xHCI98 USB 3 Controller (ASMedia ASM4242) ... xHCI98 USB4 Router (ASMedia ASM4242) ... xHCI98 USB 3 Controller (Red Hat QEMU xHCI)"; `Enum\PCI`: ASM4242 xHCI `DeviceDesc` and `FriendlyName` "xHCI98 USB 3 Controller (ASMedia ASM4242)", NHI `DeviceDesc` "xHCI98 USB4 Router" and `FriendlyName` "xHCI98 USB4 Router (ASMedia ASM4242)", `qemu-xhci` both "(Red Hat QEMU xHCI)" | cold start: under Universal Serial Bus controllers "xHCI98 USB 3 Controller (ASMedia ASM4242)" and "(Red Hat QEMU xHCI)", under System devices "xHCI98 USB4 Router (ASMedia ASM4242)"; Properties titles "xHCI98 USB 3 Controller (ASMedia ASM4242) Propert...", "xHCI98 USB 3 Controller (Red Hat QEMU xHCI) Prop...", the router's "xHCI98 USB4 Router Properties" (its class line's `DeviceDesc`, as section 5.13.8 states), each General tab the full name, all "working properly" | PASS |
| ME (q35, PIIX disk) | exit 3; the same three names in `Enum\PCI` | the same list, titles and General tabs as 98 SE | PASS |
| 7 SP1 x86 (q35, AHCI) | exit 3; the plan's "Device Manager name: xHCI98 USB4 Router (ASMedia ASM4242)" on the NHI; three publisher prompts answered; `FriendlyName` of the ASM4242 xHCI, the NHI and `qemu-xhci` as above; the four `1B21:2423` ports' `DeviceDesc` "xHCI98 USB4 PCI Bridge (ASMedia ASM4242)"; the upstream port `1B21:2421` "PCI Express standard Upstream Switch Port" (`machine.inf`) | after a restart: "xHCI98 USB 3 Controller (ASMedia ASM4242)", "(Red Hat QEMU xHCI)", "xHCI98 USB4 Router (ASMedia ASM4242)", four "xHCI98 USB4 PCI Bridge (ASMedia ASM4242)", the upstream port Windows' name | PASS |

Evidence: `E:\xhci98-vm\p44\p44-names\` (`s98\`, `me\`, `w7\`: the
serial logs `serial-a.log`, the screenshots `b-dm*.png` and
`b-prop-*.png`, `a-inst*.png`), not tracked.

Not read here: a real ASM4242, the uninstall's
clearing of the router's name (the path the controllers' names already
take, section 5.13.4, its vectors in `test_inst`), the amd64 package.

**The owner's questions answered the same day** (folded into section
5.13.8 before these legs): pci.ids is a source for a name where no other
source names the controller (80 ids joined, section 5.13.2's second
table); the ASM4242's router takes no id line (no change of rank) and is
named by the FriendlyName.

**Verdict: pass.**

## 44.63 - A Switch with Several Downstream Ports Hot-Added on XP and 2000: Where Its Windows Come From (2026-10-27)

"44.45 (continued, 2)" found that on XP a multi-port switch hot-added
behind a tunnel port (the emulator's dock, or two enclosures chained) is
not placed: `info pci` showed the switch's first and second downstream
ports programmed with the upstream port's whole windows and the third
with none, two ports at problem 12 and the dock's NIC and xHCI not
started; the same with 20 KB of I/O a tunnel port; 7 x86 placed the
same dock in the same window, and XP placed it when SeaBIOS had laid it
out at power-on. The question (owner, 2026-10-27, time-boxed): what XP's
PCI driver does with a hot-added bridge hierarchy, whether this package
could help, and at what size; or whether it is XP's limitation.

**Setup.** QEMU `q35`, emulator build `asm4242-20261027-boardint`
(`qemu-system-x86_64.exe` SHA-256 `deca3fcb...4979ce`) with 44.45's
ASM4242 layout (the root port `bus-reserve=103,mem-reserve=776M,
io-reserve=24K,pref32-reserve=4M,hotplug=off`, `usb4-asm-usp`
`fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, both tunnel ports
`power-controller=off`). XP: an overlay of "44.45 (continued, 2)"'s XP
install with the mark service's own restart off (`Restart` 0; its x86
test cut of `6b35a399`), two vCPUs. 2000: an overlay of 44.50's 2000
overlay (test cut p44storK, Stor98's debug-port image of 44.50), one
vCPU. The dock (`usb4-device ... dialect=usb4,dock=on`: a TI XIO3130
switch, `104C:8232` upstream and three `104C:8233` downstream ports, with
the NVMe, an e1000e and a qemu-xhci) plugged on USB4 port 1 by QMP
`device_add` at the desktop. QEMU's trace event `pci_cfg_write` from
power-on, on XP also `pci_cfg_read` from the plug (`-trace`, `-D`; 2000's
trace has the writes only); `info pci`
after the assignment; in the guest, Device Manager's codes (`wmic`) and
each bridge's `Enum` key (`LogConf\BasicConfigVector`, the requirement
list the PCI driver answered; `Control\FilteredConfigVector`, the list
after the device's filters; `Control\AllocConfig`, the assignment),
written to the serial port and decoded on the host.

| Leg | System | Result |
|---|---|---|
| dock hot plug | XP | as "44.45 (continued, 2)": the upstream port and the first downstream port started (Code 0), the second and third downstream ports at problem 12; the drive behind the first was enumerated again after the last restart of its port (its configuration read; a Found New Hardware wizard open, its code not read). The trace: XP's PCI driver numbered the buses itself (the upstream port 4 to 1Bh, its downstream ports 5, 10h and 1Bh), programmed the upstream port with I/O `3000h-3FFFh`, memory `E2000000h-E5FFFFFFh` and prefetchable `CD200000h-CD3FFFFFh` (the top 4 KB, 64 MB and 2 MB of the tunnel port's 12 KB, 384 MB and 2 MB) and started it; then started the first downstream port with exactly those windows (its whole configuration header written back, then the command register `7`); then disabled it, started the second downstream port with the same windows (the NIC behind it was enumerated and its BARs sized, so its devnode is in the registry), disabled that one and started the first again; the NIC was not started. The third downstream port's windows were never written, only sized and written back closed. No other window write |
| the requirement lists | XP | each of the four bridges (the upstream port and the three downstream ports alike): one I/O descriptor of 4 KB, alignment 4 KB, with no alternative; memory 64 MB preferred with alternatives of 32, 16, 8, 4, 2 and 1 MB, alignment 1 MB; prefetchable memory the same ladder, 64 MB down to 1 MB; each window followed by a device-private descriptor of the PCI driver's. `FilteredConfigVector` equal to `BasicConfigVector` on every bridge: no filter changed a list (on the upstream port `xhcicm` is a lower filter). `AllocConfig` of the upstream port and of the first downstream port the same three windows (`3000h` 4 KB, `E2000000h` 64 MB, `CD200000h` 2 MB) |
| dock hot plug | 2000 | the same steps and the same placement (`info pci`: the upstream port and the first and second downstream ports with I/O `3000h-3FFFh`, memory `E5F00000h-E5FFFFFFh`, prefetchable `CC300000h-CC3FFFFFh`; the third closed), and the same juggling in the trace. 2000's list for a bridge it found closed: 4 KB of I/O, 1 MB of memory and 1 MB prefetchable, each with no other size (once assigned, the current window first and the same fixed size as its alternative). Device Manager's codes were not read on 2000 |

**What the PCI driver of XP and 2000 does with a hot-added bridge.**
1. It numbers the buses below the bridge itself, within the tunnel
   port's range: the 48 buses a tunnel port has are enough.
2. It gave each of these four bridges, found with its windows closed,
   the same fixed requirement, whatever was behind it (an NVMe drive, a
   NIC with an I/O BAR, an xHCI, or three more bridges): on XP 4 KB of
   I/O, and memory and prefetchable windows of 64 MB at most (1 MB at
   least); on 2000 4 KB, 1 MB and 1 MB. The upstream port's requirement
   did not grow with the ports below it [inf: that it never does is
   read from these lists alone; with 20 KB of I/O a tunnel port the
   upstream port was again assigned 4 KB, "44.45 (continued, 2)"'s leg,
   its list not read].
3. Each downstream port is a child of the upstream port and asks the
   same: 4 KB of I/O with no smaller choice. The upstream port has 4 KB,
   so one downstream port can have it; the arbiter gives the first port
   the whole window, and the PnP manager's attempts to place the second
   stop the first. The prefetchable window is short too where the tunnel
   port's is small: 2 MB here (the ASM4242 layout's with Above 4G Decoding
   off, 44.45), and each port takes at least 1 MB. On 2000 the 1 MB memory
   window a port also cannot be split. A port that fails ends at problem
   12, and what is behind it is not started (the NIC was enumerated
   while its port held the windows, then left).
4. A bridge its firmware configured is kept as found: at power-on
   SeaBIOS gave each downstream port windows of its own (2 MB of memory
   and 2 MB prefetchable each) and I/O to the NIC's port alone, and XP
   kept them and placed the dock ("44.45 (continued, 2)"'s `rf` leg,
   `infopci-rf-boot.txt`). That layout is not the hot plug's: SeaBIOS
   sized the tunnel port from the dock (6 MB prefetchable, against 2 MB
   with the port empty).

7 x86 placed the same dock in the same window, a window of its own per
port and the NIC's port with the 4 KB of I/O ("44.45 (continued, 2)")
[inf: that it sizes a hot-added bridge from what is behind it is read
from that outcome only]. So in these legs the problem 12 is the PCI
driver's of XP and 2000, not this package's and not the window's size: this package assigns no bridge resources on NT,
its filters pass the requirement lists through unchanged (the vectors
above), and the 9x numbering (`XhciCmBridgeNumbering`, record 21 section
6.1.1) does not run on NT. Read here: the emulator's dock on XP and
2000, and on XP the two-level chain of "44.45 (continued, 2)" (the same
placement); an enclosure whose switch has one downstream port (the ORICO
model) is placed. [inf: any hot-added switch with two or more downstream
ports, each found closed, meets the same 4 KB of I/O; real docks and
enclosures were not read, nor XP x64, whose PCI driver is not XP's, nor
a real ASM4242 firmware's own placement at a cold boot with a dock;
record 22 section 18.3 has such a firmware build the tunnel before the
hand-off with an enclosure attached.]

**What this package could do, and its size.** The vehicle exists in
part: the child filter (record 21 section 7.4, record 31 section 4.3)
attaches above each PCI-to-PCI bridge of the chain, within the depth
bound, and would see that bridge's `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`
and its start. But on 2000 and XP each bridge's first-time install
rebuilds its stack and drops the children attached below it, and no
`BusRelations` of the parent re-attaches them (record 31 section 4.3,
read 2026-10-25); only the unit keeps a registered child (the mark
service's `LowerFilters` value, seconds after the unit appears). A
never-seen dock's downstream ports, restarted after their installs,
would be without the filter, so a fix needs a registered attachment on
every bridge of the chain first, and a leg for it. Both options are for
2000 and XP (and XP x64 if its PCI driver behaves the same, not read);
Vista and later were not read with a dock beyond 7 x86's leg [inf: that
they need neither].

- **A. Rewrite the bridges' requirement lists** (on
  `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`, the list in the shape of a
  bridge found closed). For a downstream port, read the header type and
  BAR type bits of the functions directly below it (`HalGetBusData`,
  reads only) and drop the I/O window, and the prefetchable one, with
  their private descriptors, where nothing below has such a BAR. That
  needs the port's secondary bus numbered when its list is read: the
  trace shows the buses written before the port's first start, not when
  the request is sent [inf: the order of the request against the
  numbering is a hypothesis for the first leg]. For an upstream port,
  whose downstream ports' buses are not numbered when its own list is
  read, count its downstream ports and ask N times the memory window
  (2000's 1 MB cannot be split) and I/O by a fixed rule (say one 4 KB
  window, the most the ports could keep; a second port with an I/O BAR
  would still fail). Size: a design record section, a list parser and
  rewriter host-tested on the lists captured here (about 300 to 400
  lines with its test), the glue in `cm_filt.c`, the NT 5 gate and the
  registered attachment on the chain's bridges above (about 150 to 250
  lines), the design and code review loop, gated x86 and amd64 builds,
  and legs: the attachment across the bridges' first-time installs, the
  dock and the two-level chain hot plugged on XP, 2000 and XP x64, the
  ORICO model hot plugged and at a cold boot (no regression), and 7 x86
  to show the gate. Risk: medium to high. It edits the lists of the PCI driver's own bridges and keeps its
  undocumented private descriptors in step; whether that driver starts a
  bridge with a window missing from its list, and leaves that window
  closed, is not known and is the first leg; a function behind a nested
  switch is not seen when its parent's list is read, so an I/O BAR two
  levels down needs a rule of its own; a mistake leaves a bridge at
  problem 12 or 10, at boot as well as at a plug.
- **B. Program the downstream ports' windows before the PCI driver
  reads them**, in the child filter's completion of the upstream port's
  start: split its assigned windows among its ports (`HalSetBusData`),
  so the PCI driver finds them configured as at power-on. Smaller code
  (about 200 to 300 lines), higher risk: it writes the configuration of
  devices the PCI driver owns; the downstream ports' secondary buses are
  not numbered yet at that point (the trace: after the upstream port's
  start), so what each port needs cannot be read and I/O goes to none,
  which leaves the NIC's I/O BAR unplaced; XP's 2 MB prefetchable
  window here cannot give three ports their 1 MB each, and 2000's 1 MB
  upstream windows cannot be split at all, so B cannot place this dock
  without A's rewrite; a later rebalance of the upstream port would leave
  the programmed windows outside its new one; and the PCI driver's
  keeping of a configured bridge it finds at a hot add is
  read only at power-on [inf].
- **C. No code: the limitation in the release notes**, with the
  workaround read in the emulator on XP: the dock attached at power-on,
  placed by the firmware. Not read: 2000 at power-on with a dock, and
  a real ASM4242 firmware with a dock.

**Recommendation.** C for `3.3.0.0`. A is the better fix if hot-plugged
docks on 2000 and XP are wanted later; it would be a task of its own,
design first, and its first legs are whether a registered attachment
survives the bridges' first-time installs and whether the PCI driver of
2000 and XP starts a bridge whose list lost a window. B is not
recommended.

The release-notes text proposed (for the coordinator):

> **Windows 2000 and XP: docks and chained enclosures.** A dock, or an
> enclosure plugged into another enclosure, has several PCI Express
> ports inside it. Plugged in while Windows 2000 or XP runs, it gets
> resources for one of them only, so the devices behind the others do
> not start (Device Manager: "This device cannot find enough free
> resources that it can use. (Code 12)"). On XP, attach it before
> switching the computer on, so that the computer's firmware sets it
> up (not confirmed on Windows 2000, nor with every computer's
> firmware). An enclosure with a single drive behind a single
> downstream PCI Express port is not affected on XP.

[The text's scope is what was read: the cold-boot sentence on XP in the
emulator; the last sentence the ORICO model on XP (44.45's and 44.32's
legs); 2000's hot-plugged drive has its own open case (44.50).]

Nothing of this package's was changed. Evidence:
`E:\xhci98-vm\p44\p44-xpdock\` (`xp\`: `trace-a.log`, the plug from line
1123, `infopci-a1.txt` to `a3.txt`, `serial-a.log` with the codes and
the `Enum` keys, `reqs-a.txt` the decoded lists, `a*.png`; `w2k\`:
`trace-w.log`, the plug from line 926, `infopci-w1.txt`, `serial-w.log`
(the `regedit` export), `reqs-w.txt`, `w*.png`; `pids.txt`); the decoder
in `E:\xhci98-scratch\p44-p44-xpdock\tools\`.

## 44.27 (continued)

On `3.3.0.0` at `ffde63d4` (branch `p44-res3`), 2026-10-27: gated builds
of both architectures (`build-driver.cmd all` and `all -amd64`, each
`BUILD + GATES PASSED (debug release qemu)`), then an x86 test cut
(`make-release.ps1 -Arch x86 -Flavor release -TestLabel "44.27 legs"
-NoWdmFree -NoNonFree -NoDriverPack -SkipUploadSet -SkipQualtool`; the
tree's version is still `3.2.0.0`; x86 `xhcicm.sys` `fc3f2332...`) and
the amd64 release package staged unsigned by `make-package.ps1`, its five
catalogs made by the packager's own `New-PackageCatalogFile` (x64
`xhcicm.sys` `2ca615f8...`, x64 `stor98.sys` `426122eb...` before
signing). In each x64 guest a throwaway certificate (`CN=p44-res3 guest
throwaway (not trusted)`, WDK 7.1 `MakeCert`) was made and trusted in that
guest alone and the drivers and catalogs signed there with WDK 7.1
`SignTool`, as in the second part; nothing was signed on the host. Each
install was `INSTALL /console /yes /nopause /thunderbolt /stor98`.
Development host A, QEMU under TCG, headless, 2 vCPUs per guest.

Machines. The new legs ran on **q35** (the q35 bases and
`launch-q35.ps1`'s machine: SeaBIOS, ICH9 AHCI or, for XP x64, a PIIX3
IDE disk; emulator snapshot `da60139f...`), with the launcher's `tb3tr`
Titan Ridge profile (the captured Titan Ridge 4C switch behind a gated
root port at 00:08.0, the `dialect=tb3` enclosure with its NVMe attached
at power-on) or, for Vista x64, its `asm4242` profile (`usb4-generic`,
`host-model=asm4242`, the ORICO ASM2464 enclosure at power-on). To read
the stall where 43.24 saw it, further legs ran on **i440FX** with 43.24's
own Titan Ridge topology (the `usb4-tr-*` bridges,
`profile=tb3-titan-ridge,bios-assist=off,preboot=on`, the same emulator
build as 43.24, `6a1dfcc8...`), first with the `dialect=tb3` enclosure,
then with 43.24's enclosure (`dialect=usb4,device-model=asm2464`).

### The hibernate fix: Vista x64 and XP x64

| Guest | Machine, profile | Before the hibernate | After the resume | First shutdown after it |
|---|---|---|---|---|
| Vista SP2 x64 (`vx64`) | q35, `asm4242` | install exit 3 (a first run stopped at exit 4 behind Vista's Found New Hardware windows; answered, run again); the restart reached the desktop in under 4 minutes; D: with `W1.BIN` | `cm.suspend=00000005`, `cm.reenum.hold.passed` 0208, 0200, 0210, `cm.resume=00000001`, `cm.reenum.resumed=00000001`, `cm.reenum.hold.released` for each; D: with `W2.BIN` written and compared | 21 s |
| XP x64 SP2 (`xpx64`) | q35, `tb3tr`, `preboot=on,bios-assist=off` | install exit 3 (each unsigned-driver prompt answered Continue Anyway); at the next start Windows' wizard put Stor98 on the drive (Continue Anyway); D: with `W1.BIN`, `W2.BIN` | resumed to the session (resume password off); `cm.reenum.hold.passed` 0208, 0210, 0220, `cm.resume=00000001`, `cm.reenum.resumed=00000001`, released for each; D: with `W3.BIN` written and compared | `shutdown -s -t 0`: off within 60 s |
| XP x64 SP2 (`winxp64.img`, fresh overlay) | i440FX, 43.24's Titan Ridge, ASM2464 enclosure | install exit 3; F: (Stor98) with `W1.BIN` | F: with `W2.BIN` | 9 s |
| 7 SP1 x64 (`w7x64`) | q35, `tb3tr`, `preboot=on,bios-assist=off` | D: (Stor98) with `W3.BIN` | D: with `W4.BIN` | 12 s |
| 7 SP1 x64 (`w7x64-ts`, fresh overlay) | i440FX, 43.24's Titan Ridge, `dialect=tb3` enclosure, then ASM2464 | F: (Stor98) with `W1.BIN` | `cm.reenum.hold.passed` 0208 and 0220, `cm.resume`, `cm.reenum.resumed`, `cm.reenum.hold.released` 0208 and 0220 (43.24's lines); F: with `W2.BIN`, then `A2.BIN` | 12 s, then 13 s |
| 7 SP1 x64, the `3.2.0.0` release as published (fresh overlay) | i440FX, 43.24's Titan Ridge, both enclosures | the release zip's x64 test certificate (public half) trusted in the guest, install exit 3; F: (Stor98) | F: with `W2.BIN`, then `A2.BIN` | 12 s, then 12 s |

**PASS**: Vista x64 and XP x64 resumed with the drive and shut down in
under a minute, and with this package's Stor98 on the drive no shutdown
after a resume stalled, on any of the seven readings.

### The XP x64 and 7 x64 shutdown stall: not this package's

The stall did reproduce on **43.24's own 7 x64 guest** (a new overlay of
`E:\xhci98-vm\p43-24\w7x64\disk.qcow2`, i440FX, its topology and
enclosure, F8 "Disable Driver Signature Enforcement" at the cold start as
43.24 did). There the tunnelled NVMe runs on **nvme98**, Stor98's older
version, never released (owner, 2026-10-27) (`driverquery`:
`nvme98`, "NVMe SCSIport m...", 2026-10-14; `INSTALL /console /list`:
"NVM Express Controller (nvme98) ... driver nvme98 0.1.1.0"), the NVMe
miniport p43-21's kit had staged in that guest, not on Stor98. Three
readings:

| Guest, drivers | Resume | First shutdown after it |
|---|---|---|
| 43.24's guest as it was (its `xhcicm.sys` of 2026-10-24, nvme98 on the drive) | F: present | stopped at "Shutting down..." for over 10 minutes; quit. Both processors halted in the idle loop (QMP `info registers`); in the memory dump the connection manager's log ends at the resume's lines (`cm.suspend`, `cm.reenum.hold.passed` 0208 and 0220, `cm.resume`, `cm.reenum.resumed`, `cm.reenum.hold.released` 0208 and 0220), no `cm.shutdown`: 43.24's reading |
| the same guest after this tree's install (test-signing turned on; the installer left nvme98 on the drive, "another driver: left") | F: present | the same: over 9 minutes; quit, no `cm.shutdown` |
| **a fresh `w7x64-ts` overlay with nvme98 alone** (the same nvme98 files, its own test certificate trusted in the guest, `pnputil -i -a`; **no xHCI98 driver installed**, `sc query xhcicm`: no such service), i440FX, 43.24's topology and enclosure, the pre-boot tunnel giving Windows the drive | F: with `N1.BIN`, present after the resume | **the same: over 9 minutes at "Shutting down..."; quit** |

So the first shutdown after a resume stalls with nvme98 on the tunnelled
drive whether or not this package is installed, and does not stall with
this package's Stor98 on it (the table above). 43.24's control "no package,
enclosure attached" ran on the untouched base image (its launcher's
`-Plain`), which holds none of p43-21's preparation, nvme98 included, so
no driver ran the drive there (an inference from that launcher, not read
again here), and its control with the drive ejected first stopped
nvme98 too; so the stall looked like this package and the drive together. The cause is not this package's; nothing is changed here. What
in nvme98 (or in its interplay with the emulator) holds the shutdown was
not read: nvme98 is another project's driver, outside this one, and the
reading is recorded for it here. The dumps are kept (below).

XP x64: 43.24's XP x64 guest (`E:\xhci98-vm\p43-24\wxp64-z1`, p43-21's
prepared one) runs its drive on the same nvme98 too (a new overlay,
started once: `driverquery` lists `nvme98` of 2026-10-14 and `xhcicm` of
2026-10-24, no Stor98; F: with `TESTDATA.BIN`). Its own stall was not
run again; the control was: a fresh overlay of `winxp64.img` with nvme98
alone (Windows' wizard, from a folder added to `DevicePath`, Continue
Anyway; **no xHCI98 driver**, `sc query xhcicm`: no such service), i440FX,
43.24's topology and enclosure. Before the hibernate F: answered (`N1.BIN`
written and compared). After the resume the first file check on F: did
not return (the console could not be ended), and the shutdown by the ACPI
power button did not finish within 15 minutes; quit. That is not 43.24's
XP reading in every detail (there F: answered after the resume and the
shutdown ended after 12 minutes), but it is a shutdown after a resume held
with nvme98 on the drive and nothing of this package installed, while with
Stor98 XP x64 resumed with the drive and shut down at once, on q35 and on
43.24's topology (the first table).

### Two readings on the way

- On q35 with the launcher's default `tb3tr` (no pre-boot tunnel), 7 x64:
  D: was mounted before the hibernate, but not after the resume. The log
  reads `tbt.icm.device-gone=00000500` at the resume, then the tunnel down
  and up again (`cm.action.tunnel-down`, `cm.action.tunnel-up`) and
  `cm.reenum.departure` and `cm.reenum.unexpected` for 0208; the
  emulator's new NVMe (`tp0-nvme3`) was never enumerated by Windows
  (`devcon rescan`: nothing behind the port); that shutdown took 9 s. The
  same machine before the install (the base image, the enclosure
  attached): hibernation, resume and shutdown in 11 s. XP x64 on that
  machine: after the install's restart the NVMe was never enumerated
  behind the port ("nothing behind it now"), though the connection
  manager read the tunnel up; its resume was not reached. Both with
  `preboot=on,bios-assist=off` (as 43.24's topology) took the drive at the
  start and, on both, kept it across the resume (the first table).
  Whether a tunnel the connection manager builds after the start should
  reach Windows' enumeration on this emulator snapshot's q35 port was not
  read further here.
- An XP x64 resume that stopped at the logon screen (a password not
  recorded for this image) was shut down by the ACPI power button through
  the "Other people are logged on" question; that shutdown did not begin
  within 9 minutes (the connection manager still polling, no
  `cm.shutdown`); a second press powered it off at once. Not taken as a
  reading; the leg was run again with the resume password off (the first
  table).

The Vista x64 restart that took about 25 minutes in the second part took
under 4 on the fresh q35 overlay. XP SP3 x86 stays not read: its q35 base
too answers `powercfg /hibernate on` with "System does not support
hibernation" (read before any install), as its i440FX image did in the
first part.

Evidence: `E:\xhci98-vm\p44\p44-res\` (`q-vx64`, `q-w7tr`, `q-xp64tr`,
`i-w7tr`, `i-w7c320`, `i-xp64`, `i-w7orig`, `i-w7orig2`, `i-w7nv`,
`i-xp64orig`, `i-xp64nv`: `serial-*.log` with each step's
`XHCIIN64 /devices`, the installer's log and the file checks, `cmdline-*.txt`), the screens in
`shots3\`, `pids.txt`, and the memory dumps in
`E:\xhci98-scratch\p44-p44-res\` (`*-res1.dmp` after each resume;
`orig-stall1.dmp`, `orig2-stall.dmp`, `nv98-stall.dmp` and
`xp64nv-stall.dmp` of the four stalls; the connection manager's notes
extracted to `*.notes`).

## 44.57 the connection manager's idle pass and the learning that never ends (2026-10-27)

A task of Phase 44 (the owner, 2026-10-27): 44.47's memory images of a
98 SE / ME shutdown with the ORICO enclosure attached at power-on had
the connection-manager thread inside its idle pass, with
`cmU4LearnPending` among the return addresses, "the idle pass's
learning retried for a port still pending". Read whether that is the
emulator's or this driver's, fix it if ours, and measure what it costs.

### What the learning waits for

Record 22 section 7.6's learned correspondence binds a USB4 port to the
member of the tunnel-port set whose secondary bus newly shows a
function 0 after that port's PCIe tunnel came up. It waits for no
control packet and no answer from the device router: it is host-side
PCI configuration reads (`XhciTbtLearn` -> `xhciTbtOccupancy`, through
`HalGetBusData`), 33 a member each try (the bridge's bus numbers and one
function-0 probe per device number). A port stayed pending until it was
learned or proven down, and was tried at every idle pass (200 ms by
default).

In these power-on legs the tunnel was found up at start and preserved
(section 9.2) and its function already answered when the learning
baseline was taken, so no gain followed: the port stayed pending for
the instance's life and every idle pass read both members' buses for
it. (A preserved tunnel whose function starts answering only after the
baseline is learned as any other; this is the case of a function that
answered before it and never changes.) On the `asm4242` model the host
router answers Get PCIe Downstream Entry Mapping Not Supported
(`u4.map.none=FFFFFFF5`), so no router-named binding takes the port out
of learning either. Nothing is owed by the device router or the
emulator here: the cause is this driver's (no USB4 Specification or
Connection Manager Guide request is left unanswered); the same would
follow on a real board whose host declines the mapping operation, with
a drive whose function answers before the baseline [inf] (whether the
ASM4242 answers the operation is unknown, section 18.3).

44.47's reading of the return addresses as one call chain does not hold
as a chain: `cmU4Learn` does not call `tbtU4Evidence`, and
`xhciTbtRead` reaches `HalGetBusData`, not `TbtCtlRead`; a scan of a
stack page lists stale return addresses beside live ones. The ring
transfers of every idle pass are the hot-plug poll's (`TbtU4Poll` ->
`tbtU4Check`, record 22 section 7.1), which runs with or without the
learning: about 4 control reads a pass with no enclosure and 7 with one,
before and after this change.

### The vehicle

- QEMU `q35`, emulator build `asm4242-20261027-boardint`
  (`E:\xhci98-vm\q35-bases\bin\`, sha256 `deca3fcb...`), the ORICO
  model (`usb4-device ... dialect=usb4,device-model=asm2464`) on USB4
  port 1, at power-on or plugged with `device_add` after the desktop.
- 98 SE: the ASM4242 layout of 44.45 (root port
  `bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref32-reserve=4M,
  hotplug=off`, `fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, tunnel
  ports `power-controller=off`); an overlay of the q35 98 SE base with
  the bridges' wizards answered, then a test cut of `ffde63d4`
  (`make-release.ps1 -Arch x86 -TestLabel p44idleA`, without the driver
  pack, the DOS qualifier and the free and non-free sets) installed with
  `XHCIINST /dir release-x86 /yes /nopause /thunderbolt /stor98`
  (exit 3) and a restart. Over it the `qemu` flavour's `xhcicm.sys`
  (its log mirrored to port 0xE9): before, `ffde63d4`'s
  (`AB48867D...`); after, `8a2dc34e`'s (`E3B7F2B2...`), copied by a
  batch and followed by a restart.
- ME: 44.47's ME overlay with its cut B installed (`p44-meshut\bm`) on
  44.47's layout (root port `bus-reserve=100,mem-reserve=64M`, no pads)
  for the power-on rows, and 44.45's ME overlay (`p44-hotplug\meh`) on
  the ASM4242 layout for the hot plug (on 44.47's layout the tunnel
  ports carry a power controller, which ME never switches on, so a hot
  plugged drive never answers there, 44.45's finding); the `qemu`
  `xhcicm.sys` of `8a2dc34e` copied over each the same way.
- 7 SP1 32-bit: 44.45's 7 x86 overlay on the ASM4242 layout
  (`p44-hotplug\w7b`, its test cut p44storK installed), its
  `xhcicm.sys` replaced from an elevated prompt by the `release` one of
  `ffde63d4` (`4DB841E0...`) before and of `8a2dc34e` (`71C03641...`)
  after, then a restart.
- The counters through `XHCITR /report` in the guest (`cm.passes`,
  `u4.drv.learn-tries`, `u4.drv.learned`, `cm.ring.ctl.reads`), twice a
  few minutes apart; the cost as 43.13 measured it, the QEMU process's
  host processor time over 60 s as a share of one host processor, three
  windows, the guest idle at its desktop (98 SE and ME 1 vCPU, 7 2
  vCPUs; other workers' guests ran on the host at the same time, so
  only readings taken side by side are compared).
- Kit, scripts and evidence: `E:\xhci98-vm\p44\p44-idle` (`pids.txt`,
  each leg's folder with its `serial-*.log`, `debugcon-*.log`,
  `cpu.txt`, screenshots; `*.meas` the leg summaries).

### Before the change (`ffde63d4`)

| Leg | Learning | Idle pass | Host CPU, 3 x 60 s |
|---|---|---|---|
| 98 SE, ORICO at power-on (`a1c2`) | `u4.preserved=00010808`, members `02:00.0` and `02:01.0`; `u4.drv.learn-tries` equal to `cm.passes` + 1 at both readings (465 and 1438) | 973 passes in 234 s (one each 240 ms) | 25.3, 24.8, 24.9 % |
| 98 SE, no enclosure, beside it (`a1c3`) | none | - | 24.0, 24.6, 24.0 % |
| 98 SE, ORICO plugged after the desktop (`a1c4`) | `u4.learn=00000103`: learned at the first try, none after | - | 23.9, 23.9, 23.7 % |
| ME, ORICO at power-on (`me0`, cut B's own `xhcicm.sys`) | learn-tries 726 = passes 725 + 1 | - | not read |
| 7 x86, ORICO at power-on (`w7p1`) | learn-tries = passes + 1 at both readings (743, 1709): 966 tries in 192 s | one each 200 ms | 72.7, 71.3, 60.5, then 71.0, 83.9, 66.3 % (7's own idle load dominates) |

So the learning tried at every idle pass for as long as the system ran,
on 98 SE, ME and 7 alike, but only with a tunnel found at start; after a
hot plug it learned at once. What it cost: 66 configuration reads a pass
(two members), about 275 a second on 98 SE. The host-side readings do
not isolate it: on 98 SE the guest with the enclosure at power-on
averaged 25.0 % against 24.2 % for the one without beside it (the
enclosure also adds the hot-plug poll's reads), and 23.8 % with the
enclosure plugged later and learned; on 7 the idle load swamps it.

### The change

Record 22 section 7.6, "The learning's window" (`cf183733`, `8ab4ecaf`,
`d4347823`), and its code (`69f5b4d7`, `8b397fde`, `8a2dc34e`; reviewed
and revised): a pending port is tried at every pass for its first
second, then with a gap doubling from one poll period to 5 s, and after
60 s no more; the port stays pending (so no gain is ever attributed to
another port on its account, and section 7.7 part 4's handoff is
unchanged), and `u4.learn.expired` is logged. A window opens at the
first pass that sees the port pending and at each `TUNNEL_UP` of it, is
kept per connection generation and is forgotten where the pending mark
is cleared; a port bound by a naming after its window closed has its
mark consumed without a read; a port is tried at most once a thread
pass. Files: `src/cm/tbt_topo.h`, `tbt_topo.c` (the clock),
`src/cm/cm.h`, `cm_nhi.c`, `cm_thr.c`, `test/test_tbt_topo.c` (631
checks). Gated builds of `8a2dc34e` passed, x86 (`out\build-d-x86.log`)
and amd64 (`out\build-d-amd64.log`); host tests passed
(`out\host-d.log`).

### After the change (`8a2dc34e`)

| Leg | Learning | Drive | Host CPU, 3 x 60 s | Shutdown |
|---|---|---|---|---|
| 98 SE, ORICO at power-on (`b1c1`) | `u4.learn.expired=00000001`; learn-tries 17 at both readings (passes 552 and 1528) | `E:` "ORICO", `TESTDATA.BIN` listed | 19.3, 21.0, 22.6 % | Shut Down: off in 4 s |
| 98 SE, no enclosure, beside it (`b1c2`) | none | - | 22.9, 22.3, 23.3 % | Shut Down: 3 s |
| 98 SE, ORICO plugged after the desktop (`b1c3`) | `u4.learn=00000103`, 1 try | `E:` "ORICO" | 22.5, 22.3, 22.3 % | Restart: QEMU ended in 4 s (98 SE's restart is a power-off here) |
| 98 SE, ORICO at power-on (`b1c4`) | expired, as `b1c1` | `E:` "ORICO" | - | Restart: 4 s |
| ME, ORICO at power-on (`mfc1`) | learn-tries 21 at both readings (passes 572 and 1539) | `E:` "ORICO" | 18.6, 18.1, 18.5 % | Shut Down: 4 s |
| ME, ORICO at power-on (`mfc3`) | - | `E:` "ORICO" | - | Restart: 4 s |
| ME, ASM4242 layout, ORICO plugged after the desktop (`mhc2`) | `u4.learn=00000103`, 1 try | the bridges' and Stor98's wizards answered, `E:` "ORICO" | 19.9, 19.0, 19.4 % | Shut Down: 4 s |
| 7 x86, ORICO at power-on (`w7f1` a) | learn-tries 22 at both readings (passes 545 and 1507) | "ORICO (D:)", `TESTDATA.BIN` listed | 83.5, 74.3, 72.0 % | `shutdown /s`: 10 s |
| 7 x86, ORICO plugged after the desktop (`w7f1` b) | learned at the first try | "ORICO (D:)" | 60.8, 58.5, 61.1 % | `shutdown /s`: 10 s |

The tries a whole window costs at the 200 ms default: 17 to 22 here
(about 21 by the schedule), then none; the idle pass's other work (the
hot-plug poll's control reads) is unchanged. On ME's ASM4242-layout hot
plug a first run (`mhc1`) and a control without the new `xhcicm.sys`
(`mhk1`, the overlay's own) both left the bridge wizard waiting
unfocused behind the report batch's window with the guest at 99 % of a
host processor; with the wizard brought to the front (`mhc2`) it went
through: the harness's, not the driver's.

The 44.43 and 44.47 shutdown cases on 98 SE and ME with the enclosure
attached (at power-on and after a hot plug; Shut Down and Restart) end
in 3 to 4 s, as 44.47 recorded them after its fix.

**Verdict: done.** The learning that never ended was this driver's
(record 22 section 7.6 had no bound for a tunnel found at start whose
function already answered); it is bounded to a minute after the window
opens, on every target. Its cost was about 275 configuration reads a
second on 98 SE at the default poll period, too small for these
host-side readings to separate from the rest of the guest's idle load;
the idle pass's ring transfers are the hot-plug poll's and stay. No emulator change is asked for. Left as
it was: a preserved port that is never attributed still blocks the
learning of another port's tunnel while it is pending (section 7.6's
rule, unchanged), which matters when another unattributed port of the
same domain needs learning or a section 7.7 part 4 rebuild, partly
named domains included.

## 44.65 - `hcdQuirkAsmWait` rewritten clean-room (2026-10-27)

**What.** `hcdQuirkAsmWait` (`src/hcd_quirk.c`), the ASM1042A's wait for
the write bit of configuration byte E0h to clear before each half of its
vendor flow-control write (design record 20 section 4.8, Q9,
`XhciAsmediaFlowControl`), rewritten at the owner's word (2026-10-27).

**From which facts.** Record 20 section 4.8: the control byte E0h, its
write-busy bit, the condition (wait for it to clear; give up on a read of
FFh) and the bound (1000 reads 50 us apart), each cited there as a fact;
the bit's value, 02h, from this project's existing `HCD_QK_ASM_WRITE_BIT`
(`hcd_quirk.c`); and the project's own contract: the caller
`HcdQuirkAsmFlow` (PASSIVE_LEVEL, the window checked before the wait, a
reason other than `XHCI_QK_WHY_NONE` skipping the rest as a give-up that
is counted and logged, service going on), `hcdQuirkConfigByte` (FFh when
the read fails) and the reasons of `xhci_quirk.h` (`XHCI_QK_WHY_NONE`,
`_READ_FF`, `_BUSY`). The contract was written down before the new body
(the worker's `STATE.md`); the old body was deleted unread and read only
afterwards, to compare.

**The clean-room rule.** No Linux source was opened, read, searched or
listed for this task, in any tree or on the web, and no quoted Linux text
was read; the review was told not to quote any. One line of the old body
(its stall call, this project's code) showed in a search's output before
the rewrite.

**The rewrite.** The loop is now in the pure half, `XhciQuirkAsmWait`
(`src/xhci_quirk.c`, `.h`), with the byte read and the stall as callbacks
(the form of `xhci_psw.h`), so a host suite can drive it; it runs at its
callbacks' IRQL. `hcdQuirkAsmWait` supplies the configuration read and
`KeStallExecutionProcessor(50)` and passes the bit and the 1000 tries.
Compared with the old body afterwards: the same read (byte E0h), the same
order of tests (FFh first, then the bit), a stall after each read that
finds the bit set (the last included), the same bound and the same three
results; no intended difference beyond the call through the callbacks.
Commits `3a01b4d6`, `a7cc978a` (reviewed and revised).

**Host vectors** (`test/test_quirk.c`, `test_asm_wait`, a scripted
control byte that counts reads and stalls and a stall out of its order):
ready at once (one read, no stall); other bits set but not the write bit
(ready); ready after three busy reads (four reads, three stalls); never
ready (`XHCI_QK_WHY_BUSY` after 1000 reads and 1000 stalls); clear at the
last read the bound allows and one read past it; FFh at once and after two
busy reads (`XHCI_QK_WHY_READ_FF`); no tries. `test\run-host-tests.cmd`:
PASSED; `test_quirk` 1169 checks, 0 failures.

**Builds.** `scripts\build-driver.cmd all` (x86): BUILD + GATES PASSED
(debug release qemu), the qemu flavour's `xhci98.sys`
`245585AEE9978A9A81B87138FDFE9ED029197BB7920B4758F8E6EF569CD283E2`.
`scripts\build-driver.cmd all -amd64`: BUILD + GATES PASSED (debug release
qemu), the qemu flavour's `xhci98.sys`
`CDDC3336AED8D97345EFC80CBB3149DE0C932C899A6ED61761ACEE93B3502C12`. Both
records name `a7cc978a` with uncommitted changes during the build (this
record's two documents only, no source). The
builds' self-tests ran `test_quirk` at 1169 checks, 0 failures.

**No VM leg.** QEMU has no ASMedia xHCI model (its controller is
1B36:000D, on no quirk list, record 20 section 13.7), so a guest never
runs the wait; the host vectors are its test. Not read on hardware.

## 44.53 Windows 2000: the interrupt of a function hot-added behind a tunnel (2026-10-27)

44.50 found that on 2000 with the Standard PC HAL a drive hot-plugged
behind the tunnel after the start is given no interrupt and SCSIport
fails its start before Stor98 is called. The owner asked for a design of
a fix in the connection manager on 2000 (as the 9x worker gives a
hot-added function its line, record 21 section 6.1.1 step 5), and for a
first reading of whether 2000 on the ACPI HAL has the failure at all.

**The design.** Record 21 section 6.2.1: the shapes compared (the Line
written before `pci.sys` reads it; an interrupt added to the function's
requirements by its child filter; a polled start in the port driver; a
stated limitation) and the second chosen: on 2000 with the Standard PC
HAL only, the child filter adds to a function's requirements, where
`pci.sys` gave none, the interrupt the tunnel port's own wire carries
(the PCI-to-PCI bridge swizzle up to the tunnel port's pin, the IRQ the
BIOS wrote in the tunnel port's Line), keeps it out of the start it
passes to `pci.sys` as the requirements contract asks of a bus filter,
and lets SCSIport start the adapter; Stor98 then polls the drive (its
Line stays 0, `POLL_WHY_NO_LINE`). Reviewed and revised to convergence
before any code. Not built: see the verdict.

**The ACPI-HAL reading.** QEMU `q35`, the 2000 ACPI-HAL base
`E:\xhci98-vm\q35-bases\w2kacpi\` (`disk.qcow2` SHA-256 `9a72a82e...`;
`halacpi.dll`, the 8259 PIC; its boot sector rewrites the FADT in
memory as a revision 1 table of 116 bytes, without which the base's
README records bug check `5Ch`), a fresh overlay, one vCPU, 512 MB, emulator build
`asm4242-20261027-boardint`, 44.50's ASM4242 layout (the root port
`bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref32-reserve=4M,
hotplug=off`, `fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, the tunnel
ports `power-controller=off`). The package: 44.50's test cut K
(`E:\xhci98-scratch\p44-p44-stor\rel46`; `xhci98.sys` SHA-256
`37432517...`, `xhcicm.sys` `02d3dee0...`, `stor98.sys` `47d9f299...`),
installed by `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`.

| Step | Result |
|---|---|
| a, a cold start with the ORICO model (`asm2464`) on USB4 port 1 at power-on, the install, a restart | the first run ended with exit code 2 after Windows' own wizards were answered by keys (the NHI's among them, left without its driver); the second ended "No changes were made", exit 4, "the Thunderbolt driver is staged"; the first Shut Down, Restart stopped at "explorer.exe has generated errors" and Windows stayed up, the second restarted; after it `XHCIINST /devices`: the NHI on the connection manager, "ASMedia 246x", approved, PCIe up; behind tunnel port 02:00.0 the switch's two ports on `pci`, "Stor98 Storage Controller for NVMe SSDs - stor98, started", "Disk drive - disk, started" |
| b, a cold start without the enclosure, then the enclosure plugged by QMP `device_add` at the desktop | the chain on buses 3 to 5 as in 44.50; the drive at 05:00.0: "IRQ 11, pin A", BAR0 `E5FFC000h`, and both switch ports "IRQ 11, pin A" (QEMU's `info pci`: the Lines written by Windows); `XHCIINST /devices`: Stor98 started, the disk started, the upstream port "removable: yes ... listed in Safely Remove Hardware"; `mountvol` and `dir E:\`: "Volume in drive E is ORICO", `TESTDATA.BIN`. **Pass** |

So Windows 2000 on this ACPI HAL gives a hot-added function an
interrupt: the drive and the bridges above it got the Line the tunnel
port has [inf: written from the interrupt Windows assigned, as `_PRT`
routing up through the bridges would give; the resource list was not
read], and Stor98 started and its volume mounted [inf: interrupt-driven;
its polling decision was not read]. Of the two configurations read, the
failure of 44.50 is the Standard PC HAL's; the MPS and the ACPI APIC
HALs are not read. Not
read: a warm restart before the plug (the cold start leaves the drive as
absent at POST as 44.50's restart did), and the Stor98 debug-port trace
(a release image; the interrupt is read from the configuration space and
the start from Device Manager's state).

**Verdict: design converged; the build is the owner's decision.**
2000 with the ACPI HAL works; 2000 with the Standard PC HAL needs record
21 section 6.2.1, which gives Stor98 a start (polled) and leaves its
Line at 0. Whether that is worth building depends on the HAL 2000 runs
on real ASM4242 hardware, which is not read (on `q35` the ACPI HAL started
only with its FADT rewritten, the cause within the table not isolated,
so whether it starts on a real firmware's tables is open). Until the owner decides, the release notes' course is the stated
limitation (a drive plugged after the start on 2000's Standard PC HAL
needs a restart with it attached). Evidence:
`E:\xhci98-vm\p44\p44-w2kirq\acpi\` (`serial-a.log`, `serial-b.log`,
`infopci-b-plug.txt`, screenshots `a1.png` to `a13.png`, `b1.png` to
`b3.png`, `cmdline-a.txt`, `cmdline-b.txt`), `pids.txt`.

## 44.53 (continued) Windows 2000's Standard PC HAL: the interrupt built and read (2026-10-27)

The owner chose to build record 21 section 6.2.1 in 3.3.0.0 (option B of
44.53). Whether Stor98 should trust the port's interrupt on NT when the
Line is 0 (interrupt-driven instead of polled) is not part of it.

**The code.** `ac2256cc` (the pure half `src\cm\cm_irq.c` with host
vectors `test\test_cm_irq.c`, x86 and amd64; the child filter's query,
filter-request and start handling in `cm_filt.c`; the wire's walk in
`cm_reenum.c`; the gate's read in `cm_reg.c`, called from `DriverEntry`),
`5fecd939` (the layout asserts one member level at a time: the 2000 DDK's
compiler took no nested member offset as a constant, error C2057),
`fd9f355f` and `e01d2b6c` (below). Reviewed and revised. Gated `release`
builds of `e01d2b6c` passed, x86 (`out\build-x86.log`) and amd64
(`out\build-amd64.log`); `xhcicm.sys` x86 SHA-256 `478e66e0...`, amd64
`6fab13b9...`. The host suites passed in both (`test_cm_irq`, 141 checks
on each architecture).

**A finding on the way.** The first gated image (`5fecd939`) on the
failing leg left the drive at Code 10: its log read `cm.chain.irq.no` with
reason 6 (no tunnel port) and no location for every function. A
diagnostic image (built from a copy of the tree, not kept) read why: when
the requirements are queried, `IoGetDeviceProperty` answers
`DevicePropertyAddress` but fails `DevicePropertyBusNumber` with
`STATUS_OBJECT_NAME_NOT_FOUND` (`C0000034h`), so the child was not yet
located. `fd9f355f` takes the device and function from the address and
the bus from the secondary bus register of the bridge above the function;
`e01d2b6c` reads each bridge on the wire as a present type 1 header first.
Record 21 section 6.2.1 steps 3 and 4 say so.

**The legs.** QEMU `q35`, emulator `asm4242-20261027-boardint`, 44.50's
ASM4242 layout (the root port
`bus-reserve=103,mem-reserve=776M,io-reserve=24K,pref32-reserve=4M,
hotplug=off`, `fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, the tunnel
ports `power-controller=off`), the ORICO model (`asm2464`) on USB4 port
1. The guests are copies of the 44.45 and 44.50 overlays (test cut K of
`7ee3a12e` installed: `stor98.sys` `47d9f299...`) and 44.53's ACPI-HAL
overlay, with only `xhcicm.sys` replaced by the gated x86 image above
(copied from the transfer disk over `system32\drivers\xhcicm.sys`, then
a cold start or a restart). Stor98's interrupt report, the value
`StorInterrupt 5.0.0` under `Services\stor98` that Stor98 writes in
`HwFindAdapter`, was deleted before each 2000 leg's start of the drive
and exported after it, so it is that start's.

| Leg | System | Result |
|---|---|---|
| L1, hot plug after a restart (the failing case) | 2000, Standard PC HAL | the restart a Shut Down and a cold start without the enclosure (a Windows Restart stops in the loader on this base, 44.45's 2000 leg), then `device_add`: the log `cm.chain.irq.no=00070300` and `00070400` (the two switch ports, reason 7: a type 1 header), `cm.chain.irq=000B0500` (05:00.0 given IRQ 11), `cm.chain.irq.start.stripped=000B0500`; `XHCIINST /devices`: "Stor98 Storage Controller for NVMe SSDs - stor98, started"; the report "pin 1 line 0 msi no msix yes port 11/11 polling (interrupt line not set)"; `dir E:\`: "Volume in drive E is ORICO", `TESTDATA.BIN`; the drive's configuration space: Line 0, BAR0 assigned. **Pass** |
| L2, a cold boot with the enclosure attached | 2000, Standard PC HAL | the drive at Line 0Bh from the BIOS; no `cm.chain.irq` record (`pci.sys` asked for its own interrupt); Stor98 started, the report "pin 1 line 11 ... port 11/11 interrupt"; "ORICO" read. **Pass** (unchanged) |
| L3, hot plug after a Windows Restart | 2000, ACPI HAL | no `cm.chain.irq` record of any kind (the gate is shut); the drive at IRQ 11 from Windows (Line 0Bh written), Stor98 started, the report "pin 1 line 11 ... port 11/11 interrupt", "ORICO" read. **Pass** (unchanged) |
| L4, hot plug after a restart | 7 x86 | the drive at IRQ 23; no `cm.chain.irq` record; the tool's report: the unit with "Stor98 Storage Controller for NVMe SSDs" below it, problem 0, "QEMU NVMe Ctrl SCSI Disk Device (D:)"; `dir D:\`: "ORICO". **Pass** (unchanged) |
| L5, hot plug after a restart | XP | on this overlay the enclosure's first plug (the scripted run before the replacement) brought Windows' wizard for the drive without the id ("PCI Device"), cancelled, as record 31 section 4.3 records for a never-seen switch enclosure on XP; after the replacement and a restart, the plug: the drive tagged (`cm.chain.id=00050400`), Windows' wizard installed Stor98 (the unsigned-driver warning answered), the drive at IRQ 23, no `cm.chain.irq` record, "Volume in drive D is ORICO". **Pass** (unchanged) |

Not read: XP on the Standard PC HAL (the gate keeps it out), the dock's
other functions, a function on another pin of the tunnel port, Windows
2000's resume from standby or hibernation with the added interrupt
(record 21 section 6.2.1 keeps both records across a stop), and the
hardware bench (44-E.6).

**Verdict.** On Windows 2000 with the Standard PC HAL a drive
hot-plugged behind the tunnel after the start now starts on Stor98,
polled, and its volume mounts; the cold boot, 2000 on the ACPI HAL, 7
and XP are unchanged. Stor98 runs such a drive polled because its Line
stays 0 (`POLL_WHY_NO_LINE`); making it interrupt-driven is a change to
Stor98's polling decision, left as a follow-up. Evidence:
`E:\xhci98-vm\p44\p44-w2kirq\` (`sp\serial-j.log`, `infopci-j-plug.txt`
for L1, `sp\serial-k.log` for L2, `acpi\serial-c.log` for L3,
`w7\serial-b.log` and `b9.png` for L4, `xp\serial-a.log` and
`x12.png` to `x15.png` for L5; the first image's failure in
`sp\serial-b.log`, the diagnostic in `sp\serial-d.log`; `pids.txt`).

## 44.53 (continued, 2) The Interrupt Line, and the other HALs (2026-10-27)

The owner asked for two things, on top of 44.53's polled start:

- that the child filter also write the IRQ it gives into the function's
  Interrupt Line, so that Stor98 runs the drive interrupt-driven, with a
  proof on 2000 that the interrupt really fires;
- a reading of whether the other non-ACPI HALs have the same gap.

All runs on QEMU `q35`, emulator `asm4242-20261027-boardint`, 44.53's
ASM4242 layout and ORICO model. Each was a hot plug after a Shut Down
and a cold start without the enclosure, or a cold boot with it, as
stated. The images below are test images built from a copy of the tree
at `e01d2b6c` plus the change named; none was kept or committed.

**Stor98 without an interrupt.** Read in `src\stor98\src\lifecycle.c`:
in the interrupt mode the timer runs at the watchdog tick
(`ENG_TICK_WATCH_US`, 100 ms) while requests are outstanding, and
`LcRunningStep` reaps the completion queues at each tick. An interrupt
that never fires would therefore not hang the drive. It would slow it to
about one batch of completions per 100 ms. Stor98 has no first-interrupt
check that falls back to polling; its storm handling covers the opposite
case (too many interrupts). Nothing in Stor98 was changed.

**The Line.** A test image (`xhcicm.sys` SHA-256 `323A78A0...`) sent
`IRP_MN_WRITE_CONFIG` for the byte at 3Ch, holding 0Bh, through the
function's own stack once `pci.sys` had completed the stripped start.
The log read `cm.chain.irq.line=00000500`: the request succeeded, the
byte it carried came back 0, and a read-back through the stack read 0.
`cm.chain.irq.line.hal=00000500`: a read through `HalGetBusData` read 0
too. Stor98 reported "pin 1 line 0 ... port 11/11 polling (interrupt
line not set)". So this write, through the function's stack after the
start, did not reach Stor98 [inf: `pci.sys` keeps the Line, putting its
own value in place of a written one, and SCSIport reads the miniport's
header through `pci.sys`; no other write path was tried, and
`xhcicm.sys` imports no configuration write through the HAL, record 21
section 10.3]. The write was taken out again; the branch carries none.

**The interrupt, read.** A second test image (`ED5896C6...`) left the
added interrupt in the start passed to `pci.sys`: the stripping was
skipped, the log read `diag.nostrip=00000500`. `pci.sys` started the
function with IRQ 11 and Stor98 reported "pin 1 line 11 ... port 11/11
interrupt". A copy of a 100 MiB file of random bytes (SHA-256
`681cbbd0...`, on the transfer disk) to the ORICO volume, then
`fc /b` of source and copy, was read three ways. The counts are QEMU's
`info irq`, IRQ 11 on the 8259, before and after the copy.

| Run | Stor98 | IRQ 11 during the copy | Copy | `fc /b` |
|---|---|---|---|---|
| n: hot plug, the stripped start with the Line write | polled | not counted at all | 8 s | no differences |
| p: hot plug, the added interrupt left in `pci.sys`'s start | interrupt | 55 to 1,662 (1,607) | 10 s | no differences |
| q: cold boot with the enclosure, for comparison | interrupt | 58 to 1,664 (1,606) | 10 s | no differences |

So on 2000 with the Standard PC HAL the drive's interrupt does fire on
the tunnel port's wire, as the cold boot's does, once `pci.sys` itself
holds the interrupt. Two routes remain, and neither is built:

- leave the added interrupt in `pci.sys`'s start (run p), which departs
  from the requirements contract the start's stripping follows;
- have Stor98 trust the port's interrupt when the Line reads 0 on NT,
  which is Stor98's change.

That is the owner's decision. The branch keeps the reviewed stripped
start, so the drive runs polled.

**The other HALs.** The HAL and kernel files were the systems' own, from
`sp4.cab` (2000) and `sp3.cab` (XP), copied to `system32`. They were
booted through `boot.ini`'s `/HAL=` and `/KERNEL=` on copies of the
overlays, and the gated `xhcicm.sys` of `e01d2b6c` was installed in each.

| System | HAL | Result |
|---|---|---|
| 2000 | Standard PC (`hal.dll`, installed) | the gap (44.50), closed by 44.53's polled start (44.53 (continued)) |
| 2000 | ACPI PC (`halacpi.dll`, installed) | no gap (44.53) |
| 2000 | MPS Multiprocessor (`halmps.dll`, `ntkrnlmp.exe`, two vCPUs) | booted; the ASM4242's own upstream switch port (`PCI\VEN_1B21&DEV_2421`, bus 1) "pci, problem code 35", so the USB4 host never started and the plug brought nothing: not readable on `q35` |
| 2000 | MPS Uniprocessor (`halapic.dll`, one vCPU) | the same Code 35: not readable |
| XP | Standard PC (`hal.dll` of `sp3.cab` as `halstd.dll`, its uniprocessor kernel as `ntkrnlup.exe`, `/NOPAE`) | booted, but Windows enumerated its devices again: a wizard for the display, both xHCI controllers "no driver, problem code 1", and COM1 gone. After the plug the tunnel was up ("ASMedia 246x", "PCIe up"), but the chain stopped below the enclosure's upstream port and no drive was enumerated: not a usable reading without a clean Standard PC install |
| XP | MPS | not tried (time box); on `q35` it would meet the same MP table |

On the two MPS rows the gate, which reads the HAL devnode's id, still
reads `e_isa_up`. `/HAL=` swaps the file but not the devnode, so the
gate would have acted had a drive appeared. No HAL besides 2000's
Standard PC HAL is shown to have the gap, so the gate is not widened.
Why the MP table leaves bus 1 out, and whether a real ASM4242 firmware's CSM
table does the same, is not read [inf: SeaBIOS's MP table lists the
root bus alone].

**Verdict.** No code change on the branch. The Line write is not built:
`pci.sys` replaces it. The interrupt is shown to fire on 2000 with the
Standard PC HAL when `pci.sys` holds it, and the owner's decision stands
between that route, Stor98's change, and the polled start. The gate stays
2000 on the Standard PC HAL. Record 21 section 6.2.1 says so ("The
Interrupt Line", "Other HALs"). Evidence: `E:\xhci98-vm\p44\p44-w2kirq\`:

- `sp\serial-n.log`, `-p.log`, `-q.log`, `irq-n-*.txt`, `irq-p-*.txt`,
  `irq-q-*.txt` and `copy-*.txt` for the three runs;
- `spm\serial-c.log` and `-e.log` for the MPS HALs;
- `xps\a2.png` to `a8.png` for XP;
- `xferD-rand.sha256` and `pids.txt`.

## 44.60 and 44.61 - The /cmonly entry's name and hints, and the package's own names "USB 3" (2026-10-27)

What was read: design record 26 section 9.1 (draft 45) and record 23
section 9.1 (revision 49) - the Add/Remove Programs entry named for what
the run installs, on 98 SE and ME by the two drivers' files, ME's UAS
hint only in a run with the UAS driver - and the names of task 44.61
(record 13 section 2, record 26 sections 5 and 9): "xHCI98 USB 3 Root
Hub", "xHCI98 USB 3 Controller Driver" and the entry "xHCI98 USB 3
drivers", an update renaming the one entry in place.

**Build and package.** Branch `p44-usb3` at `117e8993`; gated build x86
`all` PASS (development host A; the record notes the branch's head
moved during the build, by commits to `xhciinst\` only, whose tools
were then rebuilt by `xhciinst\build.cmd`). Test cut `make-release -Arch
x86 -Flavor release -TestLabel "p44-usb3 legs"` (version text `3.2.0.0`,
the branch's, so the update legs give `/force`): zip SHA-256
`2016d9df...`; in `release-x86\` `xhci98.inf` `3740ac44...`
(`RootHubDesc="xHCI98 USB 3 Root Hub"`, `Hub30Desc="xHCI98 USB 3 Hub"`,
`XhciSvcDesc="xHCI98 USB 3 Controller Driver"`), `xhci98.sys`
`8d253824...`, `xhcicm.sys` `f928e50c...`; `tools\xhciinst\XHCIINST.EXE`
`2b166c4c...`, `XHCIWIZ.EXE` `772e10e5...`. The `3.2.0.0` install is the
published package (`q35-bases\shared\xfer320\PKG`).

**Machine.** q35 bases (`E:\xhci98-vm\q35-bases\<os>`, overlays) on the
usb4-emu snapshot `asm4242-20261025-2230` (`da60139f...`), `qemu-xhci`
behind root port 00:1c.0; `-Usb4 asm4242` (no enclosure) for the
`/cmonly` legs and 7's update leg, none for 98 SE's update leg; the
transfer folder as the second disk. Runs from a batch on it: 9x
`XHCIINST /dir release-x86 /yes /nopause` (`/force` for the update,
`/cmonly` for the connection manager alone), 7 elevated `INSTALL.BAT
/console /yes /nopause` (the same switches); then `regedit /e` of
`HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall` and
`HKLM\Enum\USB` (9x), or `reg query` of the `xhci98` key, the
`Uninstall` key searched for "xHCI98", `Enum\USB` `DeviceDesc` and the
services' `DisplayName` (7), to COM1.

| System | Run | What was read | Result |
|---|---|---|---|
| 98 SE (q35, AHCI by INT 13h) | `3.2.0.0` install, exit 3; Shut Down, cold start | the entry "xHCI98 USB 3.x drivers 3.2.0.0"; `Enum\USB\XHCI98_ROOT_HUB` `DeviceDesc` "xHCI98 USB 3.x Root Hub" | baseline |
| 98 SE | this branch over it, `/force`, exit 3: "Add/Remove Programs lists it: xHCI98 USB 3 drivers 3.2.0.0", "Afterwards: ... xHCI98 USB 3 Root Hub: xhci98 ..., started"; Shut Down, cold start | `Uninstall`: NetMeeting and the one `xhci98` key, "xHCI98 USB 3 drivers 3.2.0.0"; the root hub's `DeviceDesc` "xHCI98 USB 3 Root Hub"; Device Manager (by connection) "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)" with "xHCI98 USB 3 Root Hub" beneath (`s98\c-dm2.png`); Add/Remove Programs one line, "xHCI98 USB 3 drivers 3.2.0.0" (`s98\c-arp.png`) | PASS: renamed in place |
| 98 SE, a fresh overlay | `/cmonly`, exit 3 (the router bound, the storage row as on any 98 SE run, no `xhci98.sys`) | "Add/Remove Programs lists it: xHCI98 Thunderbolt 3 / USB4 connection manager 3.2.0.0"; `System32\Drivers` holds `XHCICM.SYS` alone of the package's; Shut Down powered off; after a cold start `XHCIINST /uninstall /list` began "*** This removes the xHCI98 Thunderbolt 3 / USB4 connection manager. ***" (before task 44.60 the console showed the USB drivers' warning on 98 SE and ME whatever was installed) | PASS |
| ME (q35, PIIX disk), a fresh overlay | `/cmonly`, exit 3 | the entry "xHCI98 Thunderbolt 3 / USB4 connection manager 3.2.0.0"; no "plug in an ordinary USB stick once before the first UAS disk" in the run's output or log; Shut Down powered off | PASS |
| 7 x86 (q35, AHCI) | `3.2.0.0` install, exit 3; restart | the entry "xHCI98 USB 3.x drivers 3.2.0.0"; both root hubs "@oem2.inf,%roothubdesc%;xHCI98 USB 3.x Root Hub"; the service "xHCI98 USB 3.x eXtensible Host Controller Driver (xhci98)" | baseline |
| 7 x86 | this branch over it, `/force`: both controllers and the root hubs reinstalled, "Add/Remove Programs lists it: xHCI98 USB 3 drivers 3.2.0.0"; exit 4 for the same-version `xhciuas.inf` already in the driver store ("run INSTALL.BAT /restage"), which the test cut's unchanged version text makes; restart | the `Uninstall` key searched for "xHCI98": the one `xhci98` key, "xHCI98 USB 3 drivers 3.2.0.0"; both root hubs "@oem4.inf,%roothubdesc%;xHCI98 USB 3 Root Hub"; the service "xHCI98 USB 3 Controller Driver"; Device Manager "xHCI98 USB 3 Controller (ASMedia ASM4242)", "(Red Hat QEMU xHCI)" and two "xHCI98 USB 3 Root Hub" (`w7\c-dm2.png`); Programs and Features one line, "xHCI98 USB 3 drivers 3.2.0.0" (`w7\c-arp.png`) | PASS: renamed in place |
| 7 x86 | the uninstall, `C:\XHCI98\XHCIINST.EXE /uninstall /yes /nopause`: the console tool beside the window the entry's `UninstallString` names (`C:\XHCI98\XHCIWIZ.EXE /uninstall`), on the same engine; it started its copy in `TEMP` and returned exit 0 at once, and the copy's own exit code was not captured | some minutes later no `xhci98` key, no `Enum\USB` root hub, no `xhci98` service | PASS (by the registry) |
| 7 x86, a fresh overlay | `/cmonly`, exit 3 | the entry "xHCI98 Thunderbolt 3 / USB4 connection manager 3.2.0.0"; the NHI "xHCI98 USB4 Router (ASMedia ASM4242)"; no `xhci98` service | PASS |

Not read: a hub's SuperSpeed half, "xHCI98 USB 3 Hub" (QEMU has no USB
3 hub; the INF string and the driver's device text are the change);
the F6 floppy's list; the Remove through Programs and Features' own
button (no pointer in these guests; the console tool from the install
directory was used instead, and its relaunched copy's exit code was not
captured); XHCITR's port counts on screen (host
tests). Evidence: `E:\xhci98-vm\p44\p44-usb3\` (`s98`, `s98c`, `mec`,
`w7`, `w7c`: serial logs, screenshots, command lines).

**Verdict: pass.**

## 44.62 - XHCITR's PCI and bridge diagnostics node (2026-10-27)

What was read: design record 25 section 20 (revision 23) - the tree's
last item, "PCI and bridge diagnostics", showing `XHCIPFW`'s log,
`XhciCmBridgeDiag`, `XhciCmNoIrqDiag` and the slot for task 44.53's
records, each value decoded beside its raw form, in the window and in
`XHCITR /report`, and Edit > Copy with nothing selected copying the
whole text.

**Build and package.** Branch `p44-trdiag`; the gated build x86 `all`
PASS (development host A; XHCITR and the host suites built from
`7a0e4a1f`'s sources, `test_trdiag` among them), the legs' build; then
the gated build amd64 `all` PASS (its host suites on `3b9b2f6e`'s, the
last code change) and x86 `all` PASS again at `35842bb9`
(`test_trdiag` 185 checks). Test cut `make-release
-Arch x86 -Flavor release -TestLabel "44.62 legs"` (version text
`3.2.0.0`, the branch's): `tools\xhcitr\XHCITR.EXE` `790f53ca...`,
`release-x86\xhcicm.sys` `ab7ffe52...`. The value names this node reads
are branch `p44-pref`'s (`a761cca6`), whose `XHCIPFW.VXD` writes them;
this branch's own `src\pfw\pfw.c` still writes the unprefixed names, so
the 98 SE guest ran `XHCIPFW.VXD` built from `p44-pref`'s `pfw.c` over
this branch's `src\pfw` in a scratch tree (`check-pfw` passed; SHA-256
`E22DA214...`), copied over the installed one before the cold start.

**Machine.** q35 bases (`E:\xhci98-vm\q35-bases\<os>`, overlays), the
usb4-emu snapshot `asm4242-20261027-boardint` (`deca3fcb...`) through
44.45's launcher: `-Usb4 asm4242`; on 98 SE also `-AsmReserve4gOff`
(the root port's prefetchable reserve above 4 GB, `acpi-pci-hole64=off`,
a 40-bit processor) with the ASM4242 layout's buses and windows (`-AsmBusPad 48
-AsmWinPad ',fw-io-pad=12K,fw-mem-pad=384M' -AsmNoPcp`), the ORICO
enclosure plugged by QMP `device_add` after the desktop; on 7 the ORICO
at power-on. Install from the transfer disk: `XHCIINST /dir release-x86
/yes /nopause /thunderbolt /stor98 /xhcipfw` on 98 SE, `INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98` elevated on 7. The report
by `XHCITR /report` (98 SE `start /w`, 7 from a standard user's prompt
into `%TEMP%`), copied to COM1.

| System | What was read | Result |
|---|---|---|
| 98 SE (q35, AHCI by INT 13h) | install exit 3; cold start; ORICO plugged: two "PCI standard PCI-to-PCI bridge" and the Stor98 wizards (restarts declined), "Orico (E:)". The node, in the report and the window: `XhciPfwVersion` "XHCIPFW.VXD 3.2.0.0", `XhciPfwResult` 00000001 "the scan finished", `XhciPfwClosed` 7, `XhciPfwFunctions` 0Fh, `XhciPfwBridges` 7 records: 00:03.0 closed from `24h` FFF10001, `28h` 1, `2Ch` 40h (window 00000001_00000000-00000040_FFFFFFFF) to 0001FFF1, 0, 0 ("closed"), then 00:1C.0, 01:00.0, 02:00.0 to 02:03.0 likewise; `XhciPfwBars` and `XhciPfwXhci` none - each matching `regedit /e` of the key; the router's `XhciCmBridgeDiag` attempt 2 at the empty port 02:01.0 (the last attempt after the enclosure's port), outcome 0, buses 02/33/62; `XhciCmNoIrqDiag` record 1, port 02:00.0, function 03:00.0, pin INTA, line 11, excluded for its header (its line already set by the numbering), the status after "not recorded"; the 44.53 slot "Not on this system". End selected the node; Edit > Copy and a paste into Notepad gave the same text | PASS |
| 7 SP1 x86 (q35, AHCI) | install exit 3, a restart; as a standard user: `XHCIPFW` "Not recorded" (`xhcipfw\Parameters` absent: no `XHCIPFW.SYS` in this branch's package), both connection-manager records "Not recorded (written on Windows 98 SE and ME only)", the 44.53 slot read through the router's log at level 2, "None in its log"; the same text in the window | PASS |

Evidence: `E:\xhci98-vm\p44\p44-trdiag\` (`w98\`: `r1.txt` the report
and registry export from COM1, `s15.png` to `s18.png` the window, the
node and the Notepad paste; `w7\`: `r2.txt`, `s09.png`; `pids.txt`),
not tracked.

Two decoding refinements came from reading the 98 SE text and were
reviewed after the legs (`3299b0c8`, `b9800064`, `3b9b2f6e`): a field of
`XhciCmBridgeDiag` that the attempt's outcome says was not read (the
port's window and need when the window stage did not run or failed its
read, the need unless the sizing finished, the line and counts
likewise) is no longer printed as a value;
the raw dwords keep it. The legs above ran the build before them; the
host vectors hold them.

Not read here: 44.53's records on 2000 with the Standard PC HAL (its code
is on branch `p44-w2kirq`; owed at the merge, record 25 section 20 item
9); `XHCIPFW.SYS`'s NT values (`p44-pref`; owed at the merge); ME; the
amd64 XHCITR64 in a guest.

**Verdict: pass**, with the two merge-time readings owed.

## 44.64 Part B: sizing - daisy chains under both connection managers (2026-10-27)

Task 44.64 asks two things: whether the release notes can claim
Thunderbolt 3 daisy chains on the hosts whose firmware runs the
connection manager (Part A, the legs, in the section "44.64 Part A"
that follows this one), and what
chain support would cost (this part: sizing only, no code). The
release notes say today, under "Which controllers": "Daisy chains (a
device behind a device) ... are not supported in this release".

One reading shapes item 1 below (read in the emulator's source here,
and in Part A's traces): the emulator's firmware
connection manager (`usb4-emu`'s `tb3-alpine-ridge` and
`tb3-titan-ridge` profiles) handles one level of device routers only.
Its source says so (`hw/usb4/usb4-icm.c`'s header, "Scope: one level of
device routers below the host router (depth 1)"); its hot plug hook
drops every event from a router that is not the host's ("Lane 1 events
and deeper routers: nothing to announce"), and its `get route` answers
depth 1 only (`ICM-PROTOCOL.md` R9). A router chained behind the first
device is attached in the fabric (the q35 tunnel slot even builds the
first device's switch with a gated downstream port for it) but is never
announced, so no client can approve it there [model].

Sizes are rough: lines of C (driver and host tests apart), working days
for one person who knows the code, and a risk. Line counts of today's
files, for scale: `tbt_u4cm.c` 8,285, `tbt_icm.c` 2,328, `cm_reenum.c`
3,431, `cm_cfgwk.c` 4,011, `tbt_brnum.c` 1,493, `tbt_topo.c` 1,636.

**1. The firmware connection manager's chain (Thunderbolt 3 hosts).**
The firmware builds the chained tunnel itself; this driver's share is
the client, the PCI side and the legs.

| Piece | What | Size | Risk |
|---|---|---|---|
| The ICM client | Already written for any depth: `get route` by link and depth (Alpine Ridge), the walked route (Falcon Ridge), the route in the event (Titan Ridge, Ice Lake), entries keyed to depth 6, and a disconnect or replacement removing every entry below, deepest first (record 21 section 5.3; `test_tbt_icm.c`'s depth-2 vectors). Left to check: that a child's approval follows its parent's tunnel (the firmware announces in order [inf]), with a vector for it | under 100 lines, 1 day | low |
| The emulator | The `tb3-*` profiles announce depth 1 only (above). Announcing a chained router, `get route` past depth 1, the approve building the second tunnel through the parent (the fabric already does it on q35 under a software connection manager), and the disconnects of a subtree, in `usb4-emu`: that project's work, asked of it, not done here | 300 to 500 lines in that project, 2 to 3 days | medium: the order and form of the real firmware's depth-2 messages are not captured [inf] |
| PCI side, NT | The chained device appears behind a downstream port of the first device's PCIe switch, not on a tunnel port. Today's re-enumeration invalidates the tunnel ports' bus relations only, so on 2000 and XP (no native PCIe hot plug) nothing asks Windows to look behind that downstream port [inf]. Route: the child filter that already attaches above bridges below a member (`CmRegChainRelations`) invalidates that bridge's bus relations after an approve and after a disconnect below it | 300 to 600 lines, 2 to 4 days | medium |
| PCI side, 9x | `CONFIGMG` re-enumeration of that downstream port's devnode, and bus numbers and a memory window for a subtree added behind a switch already in use: `tbt_brnum.c` numbers a fresh tunnel's chain to exactly the buses it needs and closes the windows of empty ports, and works only on a member with no child devnode, so a later chained device finds no bus numbers and no window. Needs spare buses and a window left on each hot-plug downstream port at the first numbering, or the whole subtree renumbered (the first device's drive removed and found again) | 400 to 800 lines, 3 to 6 days | high: PCI.VXD's own pass over a subtree it already holds (record 21 section 6.1) |
| The drive's link-state entry | `CmNvlinkExpose` scans only the tunnel port's own secondary bus (`cm_nvlinkos.c`), so a drive behind an enclosure's or dock's switch is not published for Stor98 at any depth; a recursive scan below the member | 150 to 300 lines, 1 to 2 days | medium |
| Safe removal and the marks | The unit at depth 2: its Removable mark, its eject, its entry in the USB Tree; record 25's T.11 notes the chain case unreadable on the emulator | 200 to 300 lines, 2 days | medium |
| Legs | 98 SE, ME, 2000, XP, 7 x86 and x64 on q35 (`usb4-tunnel-port`; i440FX has no chained PCIe), plug, unplug of the end and of the middle, re-plug, safe removal | 3 days | - |

Total: about 1,150 to 2,100 lines here plus the emulator's share, 12 to
18 working days (two and a half to four weeks); **medium**, most of it the PCI side, which is shared with
item 2.

**2. Daisy chains under this project's USB4 connection manager**
(record 22 section 1.3 "Out", section 7.1: depth 2 and deeper
acknowledged, the port left locked; section 9.2 discovery already reads
paths at any depth and watches discovered links).

| Piece | What | Size | Risk |
|---|---|---|---|
| The data model | The core is per host port: one `TBT_U4_PORT` holds the device router, the lane fields saved for the unplug, the TMU flags and the PCIe and USB3 tunnels, with the generation, the recovery (section 7.7) and the carry across sleep (section 9.4) keyed to it. A chain needs that state per link (parent route, its downstream lane adapter, the child), either by generalising the port or by a child connection hanging off each port | generalising: 1,500 to 2,500 lines changed, 6 to 10 days; a child connection per port: 600 to 900 lines, 3 to 5 days | high: every depth-1 sequence is touched; the child form keeps depth 1 as it is |
| Enumeration at depth | Route strings are already depth-generic (`TBT_ROUTE`, `TBT_U4_DEPTH_MAX` 5) and `tbtU4Enumerate` writes the depth it is given. New: unlocking the parent's downstream lane (ADP_CS_4 Lock), its hot plug enabled, the parent's port marked configured (PORT_CS_19; on a Thunderbolt 3 parent the link controller's SX_CTRL for that port, as the emulator's chain needs), ROUTER_CS_1 to 4 on the child, Router Ready, the DROM | 300 to 500 lines, 2 to 3 days | medium |
| Lane bonding of the inner link | Section 6.5 on the parent's downstream lanes (the TS3 Plus bonds with LB on its adapter 3 and then reports adapter 4 unplugged, `USB4-HOSTS.md` section 8) | 100 to 200 lines with the model above, 1 day | medium |
| Hot plug from a device router | Today acknowledged and counted. New: a device router's lane event queues a check of that link (as section 7.1 already does for a discovered one), the Thunderbolt 3 parent's Plug Events enable for its downstream lanes | 150 to 250 lines, 1 to 2 days | low |
| Paths through the intermediate router | A PCIe tunnel is hop by hop between neighbours (Guide 5.3.1): the parent's PCIe Down adapter for that port, the parent's downstream lane, the child's lane and PCIe Up, HopID 8 at both protocol adapters, as the emulator's chain reads (3:7 to 303:3). Which PCIe Down pairs with which downstream port comes from the router (the DROM, or the adapters' order) - the host's mapping operation does not cover a device router. HopIDs and credits are accounted on the parent's lane adapter the way the host's are (section 6.8); the parent's tunnel must be up first and the child's goes first at teardown | 300 to 500 lines, 2 to 3 days | medium |
| The TMU | Not needed for PCIe (section 6.6: PCIe has no accuracy requirement). A USB3 tunnel at depth 2 would need the TMU on each link of the chain and a bandwidth allocation counted through the parent | PCIe only: none; USB3 at depth 2: 400 to 600 lines, 3 to 4 days | high for USB3 |
| Unplug of the middle router | One removal takes a subtree: the parent side of each path through the removed router torn down on the routers that remain (Guide 3.4.1), leaf first, with the quarantine; the generation of every connection below raised; each removed endpoint's Stor98 entry retired. The link-down statement itself is withheld today wherever the source is not the emulator's SHPC bridge (`CmNvlinkU4Verdict`, `cm_nvlink.c`), so on q35 and on hardware it stays withheld at depth 2 as at depth 1, a stated limitation; making it per tunnel rather than per port (`TBT_U4_LINKDOWN` carries a port) matters only once a statement is qualified there. `tbtU4RemoveLink` does the parent-side half for discovered links today | 300 to 500 lines, 3 to 4 days | high: the teardown order |
| PCI side | As item 1's NT, 9x and link-state-entry rows (shared code); the tunnel-port set gains the parent's switch downstream port as a learned member, since no router names it | as item 1, shared | high on 9x |
| Safe removal | Eject by router rather than by port (`TbtU4Eject` takes a port), the marks table and the child filter's Removable at depth 2, the USB Tree's view | 300 lines, 2 days | medium |
| Preserved chains | A depth-2 tunnel this driver's earlier instance built, matched by section 9.4's carry record, adopted as its own (rebuilt across sleep as at depth 1); one only discovered (a pre-OS connection manager's) stays watched, not owned, as section 9.4 rules - adopting those would be a change of that policy, the owner's | 200 lines, 1 to 2 days | medium |
| Host suites and the cross-check | Depth-2 vectors in the core's suite (enumeration, bonding, paths, the middle unplug, recovery during a chain); the `usb4-emu` cross-check linked with the fabric model and run on its chain (`CHAIN=1`, the `linux-ts3plus-orico` capture as the reference sequence) | 800 to 1,500 lines of tests, 3 to 5 days | low |
| Legs | `host-model=intel-adl` with the TS3 Plus and the ASM2464 behind it, and ASM2464 behind ASM2464, on q35 only (i440FX has one tunnel-bridge level): 98 SE, ME, 2000, XP, 7 x86 and x64, 8 to 10 x64; plug, unplug of the end and the middle, re-plug, safe removal, a pre-OS chain | 3 to 4 days | - |

Total for the whole of item 2, the rows added up with the generalised
model, USB3 at depth 2 and item 1's PCI rows counted once: about 5,200 to
8,750 lines with the tests, 33 to 52 working days (seven to eleven weeks).
**Big.**

**A minimal "depth 2, PCIe only" version** of item 2: one level below
each host port, the depth-2 router's own downstream ports left locked as
today's depth-1 ones are, no USB3 tunnel and so no TMU at depth 2, no
DisplayPort, the approval rule as at depth 1, the child connection
hanging off its port (depth 1 unchanged). Pieces: the child connection,
enumeration, inner-link bonding, the hot plug check, the PCIe paths, the
middle unplug (the statement withheld as today), the PCI side and the
link-state entry (item 1's rows, done once for both profiles), eject by
router, tests and legs. Those rows add up to about 3,700 to 6,350 lines
with the tests and 26 to 41 working days (five to eight weeks), the 9x
PCI side the least certain part; an NT-only first cut (9x with a
release-notes line) drops that row, 400 to 800 lines and three to six
days. If item 1 is done first, its PCI rows (850 to 1,700 lines, six to
twelve days) are already paid, and the USB4 half alone is about 2,850
to 4,650 lines and four to six weeks.

## 44.64 Part A: Thunderbolt 3 daisy chains on the firmware connection manager's hosts, on q35 (2026-10-27)

The question: can the release notes claim daisy chains (a Thunderbolt 3
device behind another) on the hosts whose firmware runs the connection
manager, and with what limits. Read on the emulator's Alpine Ridge model
on q35, on 98 SE, ME, 2000, XP and 7 x86 (ME and XP added by the owner,
2026-10-27). Part B, the sizing, is the section before this one.

**Setup.** Machine `q35` (SeaBIOS), the q35 bases of
`E:\xhci98-vm\q35-bases\` as overlays, the launcher's `-Usb4 tb3ar`
(`profile=tb3-alpine-ridge` behind the captured switch: tunnel port
`02:01.0`), emulator `bin\asm4242-20261025-2230`
(`qemu-system-x86_64.exe` SHA-256 `da60139f...`). The chain: the first
enclosure `usb4-device ... dialect=tb3,drive=nvme0,downstream-ports=1`
(enc0) and a second Thunderbolt 3 enclosure on its downstream port
`usb4-device ... parent=enc0,port=1,dialect=tb3,drive=nvme1` (enc1), each
with its own NVMe image. With `downstream-ports=1` the first enclosure's
tunnel brings up a PCIe switch (`104C:8232` upstream port; two `104C:8233`
downstream ports, one with the NVMe, one the gated hot-plug port the
chained device's tunnel would land on, USB4-HOSTS.md section 8
"Chains"). Two arrangements: plugged by QMP `device_add` at the desktop
(enc0, then enc1 a minute later), and both attached at power-on with
`preboot=on`, so the firmware model tunnels the first device before the
operating system starts. Package: a test cut of this tree at 18f88c78
(`make-release -Arch x86 -TestLabel p44chain`, `xhci98.sys`
`db71a7e1...`, `xhcicm.sys` `7f9c6750...`), installed with
`/thunderbolt /stor98`; 98 SE and ME with one virtual processor, 2000
one, XP two, 7 x86 one. Readings: the emulator's `usb4_icm*` and tunnel
slot trace events, `XHCIINST /devices` and `XHCITR /report` to the serial
port, `info pci`, on XP the bridges' `Enum` keys.

**What the emulator does with a chain under its firmware connection
manager [model].** It handles one level of device routers
(`hw/usb4/usb4-icm.c`, "Scope: one level of device routers below the
host router (depth 1)"): in every leg the trace shows the first device
announced (`device connected link 1`), `get route link 1 depth 1`, the
approve and the tunnel up, and then the chained router's own lane events
dropped (`ICM hot plug router 0x1 adapter 3 ... ignored`, adapter 4 the
same) while the fabric attaches it (`device router enc1 parent enc0
downstream port 1: plugged`). The second device is never announced, so
no client can approve it here, and its drive never appears. Nothing of
the second device's approval, tunnel or drive is therefore read in any
leg below; what is read is how the first device and the driver behave
with a chain attached.

| Leg | System | Result |
|---|---|---|
| hot plug from an empty port | ME, 2000, XP, 7 x86, 98 SE | the first device approved (`XHCIINST /devices`: "Thunderbolt 3, link 1, depth 1, approved"), its tunnel up; its switch got no bus numbers: the tunnel port spans bus 4 alone and the emulated port takes no bus reservation (`info pci`: `104C:8232` at bus 4 with secondary and subordinate 0), the limit "44.20 (continued)" met with the dock on 8 x86. 98 SE, ME, 2000 and XP: "(nothing behind it now)" under the tunnel port (98 SE's bridge numbering found nothing to number, `cm.brnum.9x.outcome=00020800`); 7 x86: "PCI Express standard Upstream Switch Port - pci, problem code 31". The second device never announced |
| unplug of the end device | ME, 2000, XP, 7 x86 (hot); 98 SE, ME, 2000, XP, 7 x86 (power-on) | nothing on either side: no event reached the driver (the emulator drops it), the first device and its drive unchanged |
| unplug of the middle device | ME, 2000, XP, 7 x86 (hot); 98 SE, ME, 2000, XP, 7 x86 (power-on) | `device disconnected link 1`, the tunnel down; the second device goes with it in the fabric. 2000 (power-on chain): "Unsafe Removal of Device" listing the two PCI-to-PCI bridges (a plain unplug). 7 x86 (power-on chain): `XHCIINST /eject 1` first ("Windows has stopped PCI Express standard Upstream Switch Port. You can now unplug the device."), then the unplug: the notice "QEMU USB4 NVMe Enclosure was disconnected" |
| re-plug of the chain | ME, 2000 (hot); 98 SE, ME, 2000, XP, 7 x86 (after the power-on chain) | approved again (a new connection id), tunnel up. Hot: as the first plug, no bus numbers. After the power-on numbering the buses are there: 2000 and XP started the switch and both downstream ports again; 98 SE and ME found the drive again as E: with no action (Explorer opened it), the file written before the unplug intact and a second one written and compared; 7 x86 found the drive again as D:, the file written before the unplug intact (`fc`) and a second one written. The second device never announced |
| chain at power-on | 2000 | SeaBIOS numbered the switch (buses 4 to 7) and gave each downstream port 2 MB of memory and 2 MB prefetchable; 2000 started the upstream port and both downstream ports; the NVMe at problem 18 (its Found New Hardware window was cancelled at the desktop by the operator, not read further) |
| chain at power-on | XP | as 2000: the switch and both downstream ports started; the NVMe at problem 1 with its wizard pending (Stor98 then offered by the mark service's "Found New Hardware: Stor98 Storage Controller for NVMe SSDs"), not read further |
| chain at power-on | 7 x86 | the switch, both downstream ports, Stor98 and the disk started; the drive as D:, a file written and compared (`fc`: no differences) |
| chain at power-on | ME | the switch and both downstream ports, Stor98 and the drive (E:, the test image's "ORICO" volume) started; a file written and compared (`fc`: no differences) |
| chain at power-on | 98 SE | as ME: the drive as E:, a file written and compared; Stor98's first install on this overlay asked for its disk (`C:\XHCI98`) and a restart, after which the chain was attached at power-on again |

**XP and the hot-added switch of "44.63".** "44.63" found that XP gives a
bridge it finds closed a fixed list with 4 KB of I/O and no
alternative, so a hot-added switch with several downstream ports gets
one placed and the others at problem 12. On this layout a chain hot-added
from an empty port does not reach that point: the tunnel port has no
spare bus, so XP never sees past the upstream port. With the chain
attached at power-on the firmware numbered and opened the bridges, and
XP kept the placement; after an unplug and re-plug of the chain the
bridges came back with the same instance ids, and XP's lists
(`LogConf\BasicConfigVector`, decoded) were: the upstream port 4 MB of
memory at its earlier window `FE000000h` with a 4 MB alternative
anywhere, 4 MB prefetchable the same way; each downstream port 2 MB and
2 MB the same way, its earlier window first; **no I/O descriptor on any
of the three**; `FilteredConfigVector` equal to `BasicConfigVector`;
`AllocConfig` each at its earlier window. Both downstream ports started,
no problem 12. So XP's 4 KB I/O case is met only by a bridge found
closed with no earlier assignment, which is what a never-seen chain or
dock hot-added on a port with spare buses would be [inf: the hot-added
case with spare buses was not reached on this layout; "44.63" read it on
the ASM4242 layout].

**Other readings, not the chain's.** 2000 hung in real mode after its
warm restart following the install (instruction pointer `0D4Fh`, no
progress); a cold start was clean. 98 SE's Add New Hardware wizards for
the switch's bridges took minutes per page on one virtual processor, and
98 SE stopped at its shutdown as the q35 bases' README records.

### Verdict

The release notes cannot claim daisy chains on the Thunderbolt 3 hosts:
no leg here read a chained device's approval, tunnel or drive, because
the emulator's firmware model announces depth 1 only, and no real chain
has been read. What was read: a chain attached does not disturb the
first device, its tunnel, its drive (98 SE, ME and 7 x86) or the driver; unplugging the
end device changes nothing; unplugging the middle one takes the chain
cleanly; re-plugging brings the first device back. On real firmware the
second device would be announced and this driver's client, which is
written for any depth (Part B), would approve it, but the drive behind it
lands behind the first device's switch, where today's re-enumeration
does not look on 2000 and XP and where 98 SE and ME find no bus numbers
or window for it (Part B, item 1) [inf].

Evidence: `E:\xhci98-vm\p44\p44-chain\` (`STATE.md`, `pids.txt` per
guest folder, `cmdline-*.txt`, `trace-*.log`, `serial-*.log`, the
screenshots; XP's lists in `xp\serial-b.log`).

## 44.20 (continued, 2) - The Copy Cut, T.14, 98 SE's Fault Rate and XP's Copy Check, on `q35` (2026-10-27)

Roadmap-usb4 task 44.20, continued on branch `p44-legs2` from "44.20
(continued)" above (branch `p44-legs`, `76fe9ee9` to `3f2cf570`). Every
leg on QEMU's `q35` machine from the `q35` bases
(`E:\xhci98-vm\q35-bases`, read-only overlays) on the bases' emulator
build (usb4-emu's QEMU 11.1.2, SHA-256 `da60139f...0ce93`), under TCG on
development host A, except where a row names another build. Packages, as
published and run only in the guests: `3.2.0.0` (`release-x86\xhci98.sys`
`dd230d50...`, `release-x64\xhci98.sys` `3b166bff...`, `SETUP.EXE`
`2b1ea62c...`, `XHCITR.EXE` `f4699a7e...`, `XHCITR64.EXE` `d410c88d...`;
the `-nonfree` download where a leg needs `USBDRV\`), and `3.1.0.0`
(`releases\3.1.0.0`, `xhci98.sys` `66dbdb08...`). Evidence:
`E:\xhci98-vm\p44\p44-legs2\` (`runs\`, `c5b\`, `c5c\`, `t14*\`, `s3*\`,
`pids.txt`), outside the repository.

### 43.17's `xp-console` copy check: the cause, and the scripts

The check failed twice in "44.20 (continued)" although the stick's
`RAND.BIN` was byte for byte the host's. Read again with the guest's
`copy` and `fc` output and their exit codes captured (a diagnostic copy of
`LEGNT.CMD`, not committed), three runs, two of them at once to load the
host: two passed; the third failed the same way and said why. `copy`
printed "1 file(s) copied" and exit 0, then `fc /b` exit 2, "FC: cannot
open E:\RAND.BIN - No such file or folder", and again 14 s later. The
guest's own files place it: the copy ended 16:07:12; XP's `setupapi.log`
has the stick's first-plug install of its volume, "Doing full install of
STORAGE\REMOVABLEMEDIA\...", at 16:07:14 (the full install restarts the
volume, its letter already given); `fc` ran at 16:07:14. The stick image
afterwards holds the file. So the check raced XP's first-plug install,
the timing deciding the outcome: the scripts' fault, not the data path's.

`2fc9d2b8` to `9955019c` (reviewed and revised): before the plug
`LEGNT.CMD` counts the device-install log's lines that name a `STORAGE\`
device; after the stick shows up the copy waits until the volume's
install is logged as finished (2000 and XP: one more such line saying
"finished successfully" in `setupapi.log`; Vista and 7: more such lines
in `setupapi.dev.log` and no section left open) and the log has then not
grown for 15 s, checked every 5 s, at most 200 s; a stick whose volume was
installed before logs nothing new, and the copy goes on once the log is
quiet, with a note on COM1; anything else at the bound fails the check. It
then finds the stick's letter again. Copy's and `fc`'s own messages and
exit codes now reach COM1 on a failure, and a failed compare is never
tried again. `build-and-test.md` says so.

| Leg | Scripts at | Result |
|---|---|---|
| `xp-console` | `9955019c` | **PASS** in 9.2 min, every check; exits 3, 0, 3, 0; "stick settle: 4 checks, STORAGE\ lines 2 then 3" (also PASS at `fe5ddc2e`, the round before) |
| `w2k-console` | `9955019c` | **PASS** in 3.9 min; settle 4 checks, lines 1 then 2 |
| `vista-console` | `9955019c` | **PASS** in 10.9 min; settle 5 checks, lines 23 then 28 |
| `w7-console` | `9955019c` | **PASS** in 9.7 min; settle 4 checks, lines 56 then 62 |

Each with the `-nonfree` download, `run-leg.ps1`, its own starting point
(copied from `p44-legs`), one guest at a time beside the 98 SE legs below.

### 43.5's package copy cut between the INFs

43.5 left it "not provoked (the copy is staged and put in place whole; no
way to cut it from inside the guest was found)". Provoked here from the
host: QEMU's gdb stub with a breakpoint on 98 SE's `KERNEL32!MoveFileA`
(`0xBFFA1A44`, read from the base's `KERNEL32.DLL` 4.10.2222), every call's
two paths logged, and the machine switched off through QMP `quit` while
stopped at the call whose source is `C:\XHCI98\XHCIINST.NEW\USBSTOR.INF` -
that is, after `USBNTMAP.INF` was put in place and before `USBSTOR.INF`
(record 23 section 4.4's move-in; record 26's pair rule). 98 SE from the
`q35` base, `-nonfree` download of `3.2.0.0` on a read-only FAT disk (D:),
`INSTALL.BAT /console /yes` (the storage row ticked by default). The
order the log shows: the five storage binaries through `.TMP` names into
`SYSTEM32\DRIVERS` and `SYSTEM\IOSUBSYS`, then `USBNTMAP.INF` and then
`USBSTOR.INF` into `C:\WINDOWS\INF`, then the package's move-in from
`XHCIINST.NEW` (17 files, `USBNTMAP.INF` before `USBSTOR.INF`).

| Cut | State on the disk after the cut | The next start and run |
|---|---|---|
| b: as installed (write-behind caching on) | nothing of `C:\XHCI98` (the staging and the moves were still in the cache); the INF pair whole in `C:\WINDOWS\INF`; the five binaries in place; the install log empty; `DevicePath` `C:\WINDOWS\INF` (the run's registry writes lost) | cold start clean; `INSTALL.BAT /console /yes` again: exit 3, "Copied 7 files to C:\XHCI98", `DevicePath` `C:\WINDOWS\INF;C:\XHCI98`; the seven taken as "Found installed ... They are left as they are" (case 1: the earlier run's record was lost with the cut) |
| c: write-behind caching off (`DriveWriteBehind` 0, set and restarted first) | `C:\XHCI98` holds `USBNTMAP.INF` and the five binaries, `USBSTOR.INF` still in `XHCIINST.NEW`, no `XHCIINST.LST`; `DevicePath` `C:\WINDOWS\INF` (the directory off it); the INF pair whole in `C:\WINDOWS\INF` | cold start clean; the run again: exit 3, the staging dropped and the copy made whole (`XHCIINST.LST` with its tag and seven files), `DevicePath` names the directory again; case 1 as in b |

**Verdict: pass for the pair rule.** In neither cut does a directory
Windows searches hold `USBSTOR.INF` without `USBNTMAP.INF`: the package
directory is off `DevicePath` while it holds the half, and the next run
finishes it. **Finding:** the cut also loses the run's registry record
(98 SE writes its registry out later) while the files it placed in the
system directories survive, so the next run takes the seven as "found
installed" and the uninstall leaves them: after b's uninstall (exit 3,
`HKLM\Software\xhci98\Storage` deleted) `USBSTOR.INF`, `USBNTMAP.INF`
(whole), `USBSTOR.SYS`, `USBNTMAP.SYS`, `USBAUTH.SYS`, `USBU2A.SYS` and
`USBMPHLP.PDR` stay on the disk. Not changed here (the installer is not
this task's): reported, with the reading that flushing the storage record
before the first placement (as `RegFlushKey` already does for the
non-free set's record) would keep it.

### 43.57's T.14: the red tray icon on the systems 43.57 left

`3.2.0.0`'s tray, by screenshot (`t14*\`), the q35 base of each system
with the package installed from the console, the conditions made with
QEMU's Full Speed `usb-hub` and `usb-storage` sticks (High Speed devices)
and, where named, the usb4-emu Alpine Ridge profile (`launch-q35.ps1
-Usb4 tb3ar`) or a QEMU whose `usb-storage` declares bcdUSB 0x0210 at
High Speed (43.28's build, SHA-256 `a67b019f...`, QEMU 11.1.0).

| System | Read |
|---|---|
| 2000 SP4 (Standard PC base) | a stick behind the Full Speed hub: icon red, tooltip "xHCI98 USB Tree: QEMU QEMU HARDDISK USB Device (E:) is slowed by a USB 1.1 hub"; a second one: "... and 1 more" (tooltip and the left-click menu's first item); that item and the double click open the window on port 8.1; Explorer ended (Task Manager) and started again: red at once; "Tell me when a device can run faster" off: the usual icon at once, on: red at once; both sticks moved off the hub: the usual icon |
| 2000 SP4 (`w2kacpi`, the ACPI HAL) | **a sleep**: red (a stick behind the hub), Hibernate, QEMU ended at the S4 power-off, started again with the same devices: the session back, the icon red, "... is slowed by a USB 1.1 hub" |
| XP SP3, Alpine Ridge profile, `INSTALL /set XhciCmApprove=0` | **waiting**: the TB3 device plugged: balloon "QEMU USB4 NVMe Enclosure is waiting to be approved.", icon red, tooltip "xHCI98 USB Tree: QEMU USB4 NVMe Enclosure is waiting to be approved"; a slow stick beside it: "... waiting to be approved and 1 more..." (waiting ranked first); the item opens the window on the device ("Approval: not approved"). **Failed controller**: every ICM reply dropped (`x-reply-faults`) and the host router reset (`x-host-reset=router`): Liveness "recovery failed", menu "Thunderbolt/USB4 controller stopped answering and 1 more..." (ranked first), the double click opens the window on the controller; put right by a restart. Explorer ended (`taskkill`) and started: red. The device unplugged and the stick taken away: the usual icon |
| XP SP3, the bcdUSB 0x0210 QEMU | a stick on a USB 2.0-only root port: balloon "... runs at High Speed (480 Mbit/s) on a USB 2.0 port; it can run at SuperSpeed (5 Gbit/s)", icon red; with a stick behind a Full Speed hub as well: "QEMU QEMU HARDDISK USB Device (D:) runs below USB 3 speed and 1 more..." (ranked first), the item opens the window on port 9; the stick moved to a USB 3 port: "... is slowed by a USB 1.1 hub..."; the hub taken away: the usual icon |
| 7 SP1 x64 (test mode) | two sticks behind the hub: red in the overflow area, tooltip "... (D:) is slowed by a USB 1.1 hub and 1 more"; double click: the window on port 8.1; Explorer ended and started: red; the notice off and on: the usual icon, then red; the hub taken away: the usual icon. A sleep not readable here: Sleep is not offered (S3 refused by the base's `VGAPNP.SYS`), and the boot manager refused to resume from Hibernate ("0xc0000411") |
| ME | two sticks behind the hub: red, tooltip "xHCI98 USB Tree: QEMU QEMU HARDDISK (E:) is slowed by a USB 1.1 hub and 1 more", the item opens the window on port 8.1; Explorer ended (Close Program, End Task twice): red when the taskbar came back; the notice off and on; put right: the usual icon |
| 98 SE (NUSB 3.3 and `3.2.0.0`), **16 colours** (the base's "Standard PCI Graphics Adapter (VGA)", 640 by 480, 16 Colors) | a stick behind the hub: the icon's stack drawn red (VGA colour 9, no dithering); put right: green again. No Stand by is offered on this base (Shut Down offers shut down, restart, MS-DOS mode) |

**Verdict: T.14 passes** on every system 43.57 left and for every
condition and action the record names. A sleep was read on 2000 with the
ACPI HAL (Hibernate); XP (Stand By greyed), 7 x64 and 98 SE offer none on
these bases. Two side readings, not this task's: ME shut down from Close
Program with Explorer already ended stayed at a black screen for over 3
minutes (V86 code, then `EIP c0013518` in the VMM; QEMU quit), and a
normal Shut Down from Start on the same machine afterwards powered off.

### 43.69's 98 SE fault rate: `3.1.0.0` against `3.2.0.0`

43.69's method (its kit's `pullrun.ps1` and `cmpset.ps1`, adapted): 98 SE
with NUSB 3.3 and xHCI98 `3.1.0.0` (`b69`: the `q35` base, NUSB 3.3's own
installer, `3.1.0.0` installed from the console, its controller's first
start), and the same with `3.2.0.0` installed over it as an update
(`b69u`); a fresh overlay per series, one vCPU, 512 MB, the stick a 128
MB FAT `usb-storage` at QEMU port 2. Per pull: plug; `copy` of a 4 MB file
to it; the stick pulled 6 s in; a screendump 15 s later classified (the
VxD exception box, the "Insert Disk" text prompt, or the desktop); Enter
up to three times; replug; `copy` and `fc /b` again, DEBUG writing the
result to port E9. No result in 150 s, also on a second try: the guest is
started again cold, and the stick plugged and checked once before the
next pull (as at a series' start). A QEMU that exits at the pull (it does
at a `device_del`, as in 43.69) voids that attempt.

| Series | Driver | Valid pulls | VxD exception 0E at the pull | "Insert Disk" prompt | Desktop | No result after the replug | `fc /b` of the others |
|---|---|---|---|---|---|---|---|
| `s31c` | `3.1.0.0` | 10 (18 attempts) | 8 | 0 | 2 | 1 (the desktop shown, the check not run) | 9 clean |
| `s31d` | `3.1.0.0` | 10 (12 attempts) | 4 | 3 | 3 | 1 (a black display) | 9 clean, two of them at the second try |
| `s32c` | `3.2.0.0` | 10 (14 attempts) | 7 | 0 | 3 | 1 (the guest answering, the Run box missed the line) | 9 clean |
| `s32d` | `3.2.0.0` | 10 (15 attempts) | 5 | 3 | 2 | 1 (a black display) | 9 clean |

Of the 24 0E boxes 23 read "An exception 0E has occurred at 0028:C003C413
in VxD IOS(01) + 0000156B. This was called from 0028:... in VxD
voltrack(01) + 00000267", 43.69's fault (the boxes grouped by their pixels,
one of each group read); one, in s32c, read "... at 0028:FF09188A in VxD
USBNTMAP(01) + 00000A4A. This was called from ... voltrack(01) +
00000267" (NUSB 3.3's USBNTMAP.SYS, which these images run); none in
xHCI98's own files. **The rate: `3.1.0.0` 12 of 20 pulls, `3.2.0.0`
12 of 20** (Fisher exact, two-sided, p = 1); a guest left with no result
after the replug 2 of 20 each (one black display each); every compare
after a replug that answered was clean, 18 of 18 for each driver. On
`q35` the two releases fault equally often, so the difference 43.69 left
open (on `pc`, stock QEMU alone: `3.1.0.0` 9 of 20 against the `3.2.0.0`
line's 20 of 30; with the patched QEMU's series too, 15 of 30 against 26 of
40; not significant) does not reappear. QEMU exited at a
`device_del` of the stick in 19 of 59 attempts (both builds of 43.69 did
too); those attempts are not counted.

Four earlier series are void, the harness's fault, not counted: in `s31a`
and `s32a` the finished DOS boxes stayed open and piled up until the
typed lines went to the wrong window (`CP.BAT` and `CHK.BAT` now end with
`CLS`, which closes them); in `s31b` and `s32b` every pull after a cold
restart was that boot's first plug of the stick and ended at the "Insert
Disk" prompt with no result (the warm-up plug and check above).

### Left in 44.20

Nothing that can be read on these machines. 43.5's copy cut, 43.57's
T.14, 43.69's fault rate and 43.17's `xp-console` are read above; with
"44.19 and 44.20" and "44.20 (continued)" every leg of the task line is
read. Reported, not this task's: the storage record lost with a power
cut, so an uninstall then leaves the seven files (43.5 above), and ME's
black screen after a Shut Down with Explorer already ended (T.14 above).

ME0E.1: no "0E" stop at any ME start of this part (three cold starts,
one of them the first after the install).

## 44.54 - A drive that left with the controller on 98 SE and ME: the reading, and its removal after the return (2026-10-27)

Task 44.17 (above, "44.17 (continued)") left the 98 SE drive at problem
14 after the ASM4242 controller left and came back. The owner's decision
of 2026-10-27: for members that left together with the controller, the
normal removal, or the recovery followed by a removal and a
re-enumeration, designed under design record 21 and 42-A.10's recovery,
with Stor98's 9x side read first.

**The vehicle.** QEMU `q35`, `usb4-emu` build
`E:\xhci98-vm\q35-bases\bin\asm4242-20261027-roothp3` (the 44.17 build:
`usb4-tunnel-port,...,root-hotplug=restore` above the ASM4242 model, the
ORICO model with the FAT NVMe on USB4 port 1 at power-on), one virtual
processor, 512 MB; the departure QMP `qom-set /machine/peripheral/asmrp
present false`, the return `true`. The guest: a copy of 44.17's 98 SE
overlay (`p44-emu5\w98r`, the test cut of `078e3d4c` installed), in
`E:\xhci98-vm\p44\p44-9xret\w98`, with Stor98's debug-port image built
from this branch's tip (`STOR98_DBGPORT=1 scripts\build.cmd 9x free`;
Stor98's sources unchanged since `078e3d4c`) copied over `STOR98.MPD`,
and the emulator's `usb4-debug-port` and `isa-debugcon` (each Stor98
stage as a `n98 <TSC> <code> <arg>` line in `debugcon-<tag>.log`).
Readings: `XHCIINST /devices` and `XHCITR /report` to COM1 (`R9.BAT`),
`CHK.BAT` for data.

**The reading.**

| Tag | What was done | Read | Meaning |
|---|---|---|---|
| b | `PRE.BIN` written to E:, departure 12:37:37, return 12:38:54 | at the departure Stor98 entered gone (`0x81`), failed the cached writes `NO_DEVICE` (`0x82`), then `HwAdapterState(FALSE)` (`0x67` 0) and the stop with no hardware access (`0xA9`, `0xB3` 1 all-ones CSTS, `0x8B`, `0xCF` 0, state 7 to 5 to 6); the connection manager's log: `cm.reenum.9x.query-remove=00000000`, `cm.reenum.9x.remove=00000000`, then `cm.reenum.9x.reenumerate` of both members; at the return no Stor98 stage at all; `u4.recover.ok`, `preserved` 1; the Stor98 devnode `PCI\VEN_1B36&DEV_0010&...\00000000001800` status `0x18006EA6`, problem 14 | the unavailable route does query-remove and remove (those log values are the `CONFIGRET` results, `CR_SUCCESS`, not counts: 44.17's reading of them as "no query-remove, no remove" is corrected here); Stor98 is stopped as at any removal; its next instance is never started: no `DriverEntry` or `HwFindAdapter` stage (`DN_DRIVER_LOADED` and `DN_ENUM_LOADED` set, `DN_STARTED` and `DN_NOT_FIRST_TIME` clear) |
| b | then the enclosure removed and plugged again (`device_del enc0`, `device_add`) | the enclosure's upstream bridge at problem 29 (`CM_PROB_HARDWARE_DISABLED`), "Resources: none allocated", nothing below it; no Stor98 stage | a re-plug after the failure does not recover it (44.17's open question) |
| c | departure, `device_del enc0` while away, return, `device_add enc0` (a fresh enclosure, its configuration reset) | the enclosure's bridges started, Stor98 at problem 14, no Stor98 stage | not the restored configuration of the drive |
| f | the connection manager built with `CmCfgwkUnavailable`'s request removal-only (no `Reenumerate_DevNode` during the loss; a scratch build, not committed), departure and return | removal as in b, no re-enumeration logged during the loss; Stor98 at problem 14 | not the containment's re-enumerations of an unreadable bus |
| g | control on a fresh start: the enclosure alone removed and plugged again on a controller that answers | Stor98's stop stage for stage as in c and f (`0x67` 0 ... `0xCF` 0); at the re-plug `DriverEntry` (`0x01`), `HwFindAdapter`, started | Stor98's own stop is the same in both cases |
| i | the connection manager built to post no `CONTROLLER_UNAVAILABLE` request (no removal during the loss; scratch), departure 13:19:55, return 13:20:41 | the old Stor98 instance still bound; an INQUIRY at the return completed; `CHK` wrote and compared `I1.BIN` and compared `PRE.BIN` clean (the emulator's drive was not reset); then the enclosure removed and plugged again on the recovered controller: `DriverEntry` (`0x01`), started | a removal made while the controller answers works after a recovery |

The variants support, as an inference, the removal's timing as the
trigger: made while the member (the tunnel port the removed subtree hangs
from) and everything above it read all ones. Stor98's new instance is
never started after it; that excludes its start-up path, not an effect of
the old instance's teardown or of IOS, and how CONFIGMG and IOS then hold
the next instance at problem 14 was not read. Stor98's old instance stops
the same way in the working and the failing case, so no Stor98 change is
designed. Evidence: `E:\xhci98-vm\p44\p44-9xret\w98\` (`serial-b.log`,
`serial-c.log`, `serial-f.log`, `debugcon-b.log` to `debugcon-i.log`,
`trace-*.log`, screenshots `b1.png`, `c2.png`, `e1.png` to `e4.png`).
Tags d and e were the scratch build's install: d hung at a text cursor
after Shut Down (stopped by QMP `quit`, the scratch file copied before
it), e started in Safe mode after it and copied the image again.

**The design.** Design record 21 section 6.1.2 (reviewed and revised): on
98 SE and ME, under the USB4 profile, the `CONTROLLER_UNAVAILABLE`
request's removal is held; the recovery drains and rebuilds the held
members' PCIe tunnels, and at the commit the removal is released for each
member whose ports are proved closed - drained in this loss with the
read-back passed, or vacant (the link read disconnected, nothing
recorded on the port) - so it is made with the member answering and the
endpoint cut off (tag i's working case), and the drive comes back with a
new generation. A final refusal releases nothing; a member not proved
closed keeps the hold until the restart.

**A pre-check on scratch builds** (not the legs; `src\cm` compiled alone
with the release flavour's settings, copied over `XHCICM.SYS` with
`CMSW.BAT`):

| Tag | Image | Read | Meaning |
|---|---|---|---|
| l | `9c485c73` (`x9\CMF.SYS`) | departure 14:39:32: `cm.reenum.9x.unavailable.deferred=00000003`, `u4.contained.lost-up=00000003`, no removal, Stor98 and E: still listed; return 14:41:27: `u4.recover.demote=00000101` (port 1 drained), `u4.recover.debt=00000001`, `u4.recover.barrier=00000003`, then nothing released and `cm.reenum.9x.retire` every 5 s; no new Stor98 load | the ASM4242 model's port correspondence is not known at the commit (`u4.map.none`; the tunnel found at the start leaves none learned), so the drained port proved nothing for a member with no port bound to it while the unused port 2 was unproved |
| n | `2f53c536` (`x9\CMG.SYS`, SHA-256 `eccf5010...`; `TbtU4PortVacant`) | departure 14:55:02, return 14:55:47: the old instance's stop (`0x67` 0 ... `0xCF` 0), then `DriverEntry` (`0x01`), `HwFindAdapter`, started; `CHK`: `N0.BIN` (written before the departure) and `N1.BIN` (after the return) compared clean, E: listed | the vacant port 2 and the drained port 1 prove the members closed; the drive returns with its volume, no restart |
| ME c | the test cut of `078e3d4c` (44.17's package, its `PKG98` copied), installed on a copy of `p44-meshut\m0` (ME on q35 with the ASM4242 model and the ORICO at power-on, no package) with `XHCIINST /dir release-x86 /yes /nopause /thunderbolt /stor98` (exit 3), Shut Down, cold start | `MC0.BIN` written to E:; departure 15:16:32, return 15:17:18: `u4.recover.ok` 1, `cm.reenum.9x.query-remove=00000000`, `remove=00000000`, the Stor98 devnode at problem 14 | ME shows 98 SE's failure |
| ME f | `2f53c536` (`CMG.SYS` over it; tag d of this pair hung at a text cursor during the start and was stopped by QMP `quit`, tag e was Safe mode and ScanDisk) | departure 15:33:59, return 15:34:44: `deferred=00000003`, `lost-up=00000003`, `u4.recover.demote=00000101`, `released=00000003`, query-remove and remove `CR_SUCCESS`; Stor98 started; `CHK`: `MC0.BIN` and `MF1.BIN` compared clean | ME passes the pre-check |

**A first leg on the gated build of `fce2c523`** (98 SE `p`, its test
cut installed the same way): the removal was released and made, the
tunnel rebuilt, and then the enclosure's upstream bridge stood at
problem 29 with nothing below it. The log: a `cm.reenum.9x.reenumerate`
of member 0 with no `cm.brnum.9x` line before it, then
`cm.brnum.9x.held` - the re-enumeration the held request had posted at
the release, retried until the barrier lifted, ran without the numbering
(`NoScan`) before the rebuild's own post. Fixed in `40189f87`,
`672ccb54`, `a7aadac1` and `894ac5bc` (reviewed and revised): the held
re-enumeration is dropped at the release, the worker holds a member
until its PCIe rebuild has been attempted, and the hold's lift clears the
no-scan mark.

**Legs on the gated build of `62990014`** (code `894ac5bc`; test cut
`p44-9xret 44.54`, `xhci98-3.2.0.0.zip` SHA-256 `bcfa3470...`, release
`xhcicm.sys` `9e1587b4...`, `stor98.mpd` `d166fda6...`; installed over the
guests with `XHCIINST /dir release-x86 /yes /nopause /thunderbolt
/stor98`, exit 3 on 98 SE and 4 on ME - the ME guest's ASM4242 xHCI has
no driver, as before this task - then Shut Down and a cold start; on 98
SE the release `stor98.mpd` copied over the debug-port image):

| Guest | Leg | Read | Verdict |
|---|---|---|---|
| 98 SE z | the departure 18:34:48, `XHCITR` report during the loss, the return 18:36:30 | during the loss Stor98 listed, `deferred=00000003`, `lost-up=00000003`, Liveness "contained"; after: `demote=00000101`, `released=00000003`, query-remove and remove `CR_SUCCESS`, retired, `u4.recover.rebuild.pcie`, `cm.nvlink.u4-up`, `cm.brnum.9x.outcome=00020001` before `cm.reenum.9x.reenumerate=00020000`, Stor98 started; `Z0.BIN` (before) and `Z2.BIN` compared clean | pass |
| 98 SE z | `LOOP.BAT` copying to E: across the departure 18:39:35 and return 18:40:31 | "Disk Write Error" on E: during the loss (dismissed); the drive back; `Z0.BIN` and `Z3.BIN` clean | pass |
| 98 SE z | a plain unplug and re-plug of the enclosure (18:42:37, 18:43:07) | Stor98 back, `Z4.BIN` clean | pass |
| 98 SE z | Safely Remove: `XHCIINST /eject id=PCI\VEN_1B21&DEV_2463&...\0000001800 /yes` | exit 0, "Windows has stopped PCI standard PCI-to-PCI bridge ... You can now unplug the device", nothing listed behind the port; unplug 18:49:15, re-plug 18:49:41: back, `Z7.BIN` clean | pass |
| 98 SE z, zb | Shut Down with the enclosure and the connection manager running, after the legs and again after a fresh start (`ZB.BIN` clean first) | QEMU exited within 5 s of the Shut Down each time | pass |
| ME h | the departure 19:00:09, the return 19:01:32, the `XHCITR` report taken during the loss | released, removed, retired, rebuilt, then `cm.reenum.9x.reenumerate=00020000` with no `cm.brnum.9x` line before it: the enclosure's upstream bridge at problem 29, no Stor98; the log carried `cm.brnum.9x.pci-refused=00000000` from the report during the loss | **fail** |

The ME failure: the USB Tree's report during the loss made the worker
open PCI.VXD's route for its answer below the members; the guard's
identity read found the member all ones and the refusal it latched
turned the bridge numbering off for the boot. 98 SE escaped it only
because its report before the departure had opened the route. Fixed in
`74d6acda`, `1913489f` and `09ad7527` (an unanswered or not-ready
identity on either read gives no verdict; design record 21 section
6.1.2, "The PCI.VXD route during a loss"); a pre-check on ME with a
scratch build of `74d6acda` (`x9me\CML.SYS`), the report again taken
during the loss (departure 19:11:38, return 19:12:52): no refusal,
`cm.brnum.9x.pci-open` after the return, the numbering, Stor98 started,
`J0.BIN` and `J2.BIN` clean.

**The legs on the gated build of `09ad7527`** (x86 `all` and amd64 `all`
passed, `out\logs\build-x86-all-4.log`, `build-amd64-all-4.log`; test cut
`p44-9xret 44.54`, `xhci98-3.2.0.0.zip` SHA-256 `aa01be01...`, release
`xhcicm.sys` `867b6a55...` (271,269 bytes), `stor98.mpd` `50137ec4...`),
installed over the same guests as above (exit 3 on 98 SE, 4 on ME for
the reason above), the release `stor98.mpd` copied in, Shut Down and a
cold start. The `XHCITR` report was taken during each loss, with no
report before it, so PCI.VXD's route was first attempted while the
controller was away (and, with the fix, opened only after the return):

| Guest | Leg | Read | Verdict |
|---|---|---|---|
| 98 SE fb | the departure 20:31:11, the return 20:32:31 | during the loss: Stor98 listed, `deferred=00000003`, `lost-up=00000003`, no refusal; after: `demote=00000101`, `released=00000003`, query-remove and remove `CR_SUCCESS`, retired, `u4.recover.rebuild.pcie`, `cm.brnum.9x.pci-open`, `cm.brnum.9x.outcome=00020001`, then `cm.reenum.9x.reenumerate=00020000`; Stor98 started, E: back; `FB0.BIN` and `FB2.BIN` compared clean | pass |
| 98 SE fb | `LOOP.BAT` across the departure 20:35:44 and the return 20:36:39 | "Disk Write Error" during the loss (dismissed); the drive back; `FB0.BIN` and `FB3.BIN` clean | pass |
| 98 SE fb | a plain unplug and re-plug (20:38:38, 20:39:09) | back, `FB4.BIN` clean | pass |
| 98 SE fb | Safely Remove (`XHCIINST /eject id=... /yes`), then unplug 20:42:26, re-plug 20:42:52 | exit 0, "Windows has stopped ...", nothing behind the port; back, `FB6.BIN` clean | pass |
| 98 SE fb, fc | Shut Down with the enclosure and the connection manager running, after the legs and after a fresh start (`FC.BIN` clean first) | QEMU exited within 5 s each time | pass |
| ME l | the departure 20:53:13, the return 20:54:27 | as on 98 SE: deferred, marked, released, removed, retired, rebuilt, `pci-open`, the numbering, the re-enumeration; Stor98 started; `ML0.BIN` and `ML2.BIN` clean | pass |
| ME l | a plain unplug and re-plug (20:57:08, 20:57:39) | back, `ML3.BIN` clean | pass |
| ME l | Safely Remove, then unplug 21:00:08, re-plug 21:00:34 | exit 0, "Windows has stopped ..."; back, `ML4.BIN` clean | pass |
| ME l, m | Shut Down with the enclosure and the connection manager running, after the legs and after a fresh start (`MM.BIN` clean first) | QEMU exited within 5 s each time | pass |

Evidence: `E:\xhci98-vm\p44\p44-9xret\w98\` (`serial-p.log`,
`serial-z.log`, `serial-fb.log`, `serial-fc.log`, `fb1.png`), `E:\xhci98-vm\p44\p44-9xret\me1\`
(`serial-l.log`, `serial-m.log`, `serial-h.log`), `pids.txt` in each.
Two starts of these guests stopped at a text cursor during the session
(98 SE `u`, ME `d`, each after a session with a departure; stopped by QMP
`quit`, the next start in Safe mode) and were not chased; the starts on
the reviewed builds did not.

**Verdict: pass on q35 for 98 SE and ME.** A tunnelled drive that leaves
with the whole USB4 controller comes back with its volume and data, no
restart, after the controller returns; while it is away the drive stays
listed and its I/O fails. Not read and owed (design record 21 section
6.1.2, "Legs"): a refusal followed by its release, a final refusal, a
drain whose read-back fails, a host whose port correspondence is learned
with an unused port carrying a USB3-only router, a cooldown or power
interruption during the hold, a second loss before the commit, a disable
during the hold, and the ICM client with a drive attached at the loss.
No Stor98 change.

## 44.59 - ME's Uninstall Stalling in the Window Leg (2026-10-27)

A task of Phase 44 (the owner, 2026-10-27): "44.20 (continued)" saw
`me-window` run 1 stop in its uninstall on `q35`, 15 minutes with
XHCIINST on the taskbar and an hourglass, nothing on COM1, no stop
screen, while run 2 on a fresh overlay passed. This section repeats it,
reads where it waits and says whose it is.

### The vehicle

Development host A; QEMU's `q35` board, the ME base of
`E:\xhci98-vm\q35-bases\me` (SHA-256 `42fea19a...`) as overlays in this
worker's folder, the bases' QEMU build (`qemu-system-x86_64.exe`
`da60139f...`), `-cpu pentium3 -smp 1 -m 512`, the disk on a
`piix3-ide`, `qemu-xhci` on a root port: the `me` guest of
`scripts\vm-matrix`'s release legs. Packages: the `3.2.0.0` download as
released (with the driver pack), and test cut A of this branch at
`18f88c78` (`make-release.ps1 -Arch x86 -TestLabel`, no driver pack;
`xhci98.sys` `b5c2cf0b...`, `XHCIINST.EXE` `e28fd1fe...`, `XHCIDI.EXE`
`713ca3bc...`). For the traced runs the `qemu` flavour of the same tree
(`a6b979dd...`, the 0xE9 trace) was copied over the installed driver
inside the guest after the install, over both `xhci98.sys` and the
`xhci98.tmp` the install had queued in `WININIT.INI` (copied over the
first only, the queued rename put the release driver back at the next
start).

### What run 1 left

The leg's console uninstall (`INSTALL.BAT /console /uninstall /yes`,
from the poller's minimized DOS box; "XHCIINST" on the taskbar is that
box) never wrote its log: run 1's `C:\XHLEG\UNINST.LOG` is empty. Its
disk holds `C:\XHCI98\DIJOB.TXT`, a `remove` job for
`USB\XHCI98_ROOT_HUB\0PCI&VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01&00E000`,
and an empty `XHCIDI.RES`: the 16-bit helper was removing the root hub
(record 27 section 9), the first removal of the uninstall. The screen
captured at 10:56 shows the taskbar clock at 10:42: the whole desktop
had stopped, not only the installer.

### How often

The leg itself (`run-leg.ps1 -Leg me-window`, this worker's copy of the
configuration, three guests at once while other workers' builds and
guests loaded the host): seven runs reached the uninstall, three on
`3.2.0.0` and four on cut A, and all seven exited 3. Most runs failed
elsewhere, none at the uninstall: the host's steps (below), ME stopping
at a start, ME not switching off.

A shorter loop of this worker's (`E:\xhci98-vm\p44\p44-meun\tools\uloop.py`
and `ukit\`): a base switched off right after a console install of
cut A, and per run a fresh overlay of it started cold (ME's first
start after the install, as in the leg's phase 2), then the leg's phase
2 order: the first stick plugged (QMP, `usb-storage` on the xHCI), its
4 MiB copy and `fc`, the stick unplugged when the guest asks (1 to 5 s
after `fc`), 5 s, the console uninstall. A run whose COM1 stays quiet
300 s after the uninstall starts is a stall: screendump, registers, the
guest's RAM through the monitor.

| Variant | Driver | Uninstalls | Stalls |
|---|---|---|---|
| The leg's order | release | 25 | **6** |
| The leg's order | `qemu` | 28 | **2** |
| No stick | release | 5 | 0 |
| 60 s between the unplug and the uninstall | release | 10 | 0 |
| 20 s between `fc` and the unplug, then the leg's 5 s | `qemu` 28, release 8 | 36 | 0 (one exit 2, below) |
| The leg's order without the uninstall (120 s watched) | `qemu` | - | - (the stick's removal came in all 10) |

Every one of the 8 stalls looks like run 1: XHCIINST on the taskbar, the
hourglass, the clock stopped (`runs\u1-2\stall\screen.png`).

### Where it waits

- **The helper**: in each of the 8 RAM images its result text ends
  `XHCIDI 3.2.0.0`, `JOB remove USB\XHCI98_ROOT_HUB\...`,
  `CALL DiGetClassDevsEx DIGCF_ALLCLASSES 0`, `FOUND 1`,
  `CALL DiCreateDeviceInfo USB 0`, and no more: `DiCreateDeviceInfo`
  returned, and the helper is in one of the two calls `di_sys_remove`
  makes before its next line, `DiDestroyDeviceInfoList` of the
  enumerator's list or `DiCallClassInstaller(DIF_REMOVE)` for the root
  hub (`di_sys.c`); [inf] the second. The installer's log text in RAM
  ends at "helper: D:\PKG\TOOLS\XHCIINST\XHCIDI.EXE 3.2.0.0
  C:\XHCI98\DIJOB.TXT" after "[settled after 3125 ms]": the installer
  is in its wait for the helper (`helper_job`, which has no bound).
- **The driver**: the `qemu` trace of both traced stalls (`qq3-4`,
  `qt3-1`) ends with the root hub's `IRP_MN_QUERY_DEVICE_RELATIONS`
  answered without the stick; the stick's `IRP_MN_REMOVE_DEVICE` never
  came, and the root hub's FDO, which logs every PnP minor on entry,
  got none after it: no query-remove or remove of the uninstall reached
  the driver in those two. In the six release images the driver's note
  ring has no `pnp.rh` record, the note 42-A.16 makes when the root
  hub's PDO receives a query-remove or a remove, and its last record is
  the port's look after the disconnect. In every passing trace the
  query-remove went down to the PDO before the remove, so [inf] none
  reached the driver there either (a release ring of a passing run was
  not read).
- **The timing** (host seconds from the unplug's port change, the trace
  and COM1 merged; `tools\tl.py`):

| Runs | Uninstall starts | Root hub asked for its relations | Stick's REMOVE | Root hub's QUERY_REMOVE |
|---|---|---|---|---|
| The leg's order, passing (11 traced) | +3.2 to +5.0 | +2.5 to +11.8 | +8.1 to +13.1 | +11.9 to +17.7 |
| The leg's order, stalled (`qt3-1`) | +4.2 | +12.0 | never | never |
| No uninstall (10) | - | +0.0 to +25.5 | +6.6 to +44.8 | - |
| 20 s between `fc` and the unplug (28) | +4.0 to +15.1 | within 1 s in 23, at most +10.4 | +2.7 to +12.1 | +11.7 to +34.0 |

### Why

What was measured: the leg unplugs the stick 1 to 5 s after `fc` has
compared its 4 MiB copy, and with 20 s added there, or 60 s between the
unplug and the uninstall, no run of 46 stalled, against 8 of 53 in the
leg's order. [inf] `fc` compares from Windows' cache, so in the leg's
order the copy's data is still unwritten when the stick goes (an unsafe
removal), and the 20 s let the write-behind finish; nothing in the runs
reads the cache or the stick's contents. ME on its own UHCI driver with
the same copy and an unplug right after it (a `piix3-usb-uhci` control
with no package of this project's, `runs\uhp-1`): a blue "Error Reading
Disk in Drive E: Please Insert Disk ORICO", then "Disk Write Error ...
Unable to write to disk in drive E: Data or files may be lost", then
"Unsafe Removal of Device" (`runs\evidence\uhci-*.png`), each waiting
for a key; that control shows the prompts, not the uninstall's stall,
which was not tried on it.

Under `xhci98.sys` no prompt came. A request queued for the stick when
it left is held on its PDO, cancellable, until its client cancels it or
aborts its pipe, or the PDO is stopped, surprise removed or removed,
and a request sent after the departure is refused (`hcd_io.c`,
`HcdIoPark` and the block above it, kept for 98 SE's reason that
`hcdReportGone` gives). Windows asked the root hub for its relations up
to 25 s after the unplug in the leg's order, where with the 20 s it
asked within a second in 23 of 28, and sent the stick's REMOVE later
still. Left alone that removal always ended (10 of 10). The uninstall's
removal, started 3 to 5 s after the unplug, falls inside it; in 8 of
53 runs neither finished. [inf] Windows' processing of the departed
disk and the setup engine's removal wait on each other inside ME, and
the 16-bit helper's call, which never returns, holds the desktop with
it (the clock stops). Whether the requests this driver holds for the
departed stick take part in that wait, and so whether a driver that
failed them would avoid it, was not read.

One run of the 36 with the 20 s exited 2 (`fl1-7`): the uninstall's
reading of the tree met the stick's devnode while Windows was removing
it ("the device tree could not be walked (0x00000005)", "2 device
entries could not be read whole: nothing is changed", "STOPPED"), the
installer's refusal working; [inf] a second run would go on (not run).

### Verdict: met through the harness's order; not placed in this package

The stall needs the stick to leave a few seconds after the copy and
the uninstall to start a few seconds after that. The installer waits
for the helper and the helper is in the setup engine; no query-remove
or remove of the uninstall reached the driver in the two traced stalls
([inf] nor in the six release ones). [inf] The wait is ME's, between
its handling of a departed disk and the setup engine; this package's
part in the setting (the driver holding the departed stick's requests,
the 16-bit helper) is not shown to cause it, and not ruled out.
Nothing is changed in the package. For the harness
(`scripts\vm-matrix`, not changed here): `LEG9X.BAT` waits before
asking for the unplug after the copy (20 s was enough in 36 runs) and
leaves 30 to 60 s between the unplug and
the uninstall (60 s was enough in 10); `LEGNT.CMD` has the same order,
not read here. Whether the installer should also wait out a departure
Windows has not finished is the owner's question in 44.59's line: the
settle cannot see one, since [inf] the tree stands still until Windows
removes the devnode (the stalled runs settled in about 3 s). [inf] A
user who pulls a stick without Safe Removal and starts the uninstall
within seconds can meet the same stall on ME.

### Seen on the way, not 44.59's

- ME not switching off after "XHLEG POWEROFF": a black text screen with
  the cursor, the processor in the VMM, 16 times in these runs (after
  an uninstall and after an install alike, and with no stick).
- ME stopping at a start before the poller's first COM1 line (the
  "START /m C:\XHLEG\XHPOLL.BAT" window, the hourglass, the clock
  stopped): 11 first starts after the install, 2 starts after the
  uninstall, and once a cold start of the base with no package ever
  installed (`runs\evidence\bare-start-boot-freeze-1905.png`); 12
  further cold starts of the base did not stop. The first-start case
  is what 44.47 re-read on the console installer without the poller
  (0 of 18).
- The harness: `restart-me` read ME's "New Hardware Found" box (the root
  hub being installed) as the restart question, sent Enter and Alt+Y,
  and waited 300 s, so the stick came after the guest had stopped
  waiting for it (`runs\cur-s1-2\b2-restart-q.png`); after a restart
  the question did ask for, the phase-2 run in the next start got no
  stick (`cur-s2-3`); the window walk's first Alt+N changed nothing in 2
  runs; one ISO build failed ("Cannot initialize file-system stash
  file").

Evidence: `E:\xhci98-vm\p44\p44-meun\` (`STATE.md`, `pids.txt`;
`runs\<variant>-summary.txt`; per run `serial-u.log`,
`debugcon-u.log`, `timeline.txt`, and for a stall `stall\screen.png`,
`regs.txt`, `ram.bin`; the leg runs `runs\r32-*`, `runs\cur-*`; the
control `runs\uhp-1`; `runs\evidence\`).

## 44.45 (continued, 3): Windows 2000's owed legs (2026-10-27)

The two items 44.45 still owed on Windows 2000, now that 44.53 gives a
function hot-added behind a tunnel on 2000's Standard PC HAL the tunnel
port's interrupt: T.2's hot plug in the direct shape, and T.11's re-plug
(record 31 section 9.2: with `XhciCmSafeEject` 0, a re-plug of an
installed switch enclosure tagged; the re-plugs here were also made with
the default value, after Safely Remove Hardware).

**Setup.** QEMU `q35`, emulator `asm4242-20261027-boardint`
(`qemu-system-x86_64.exe` SHA-256 `deca3fcb...`), 44.45's ASM4242
layout (the root port `bus-reserve=103,mem-reserve=776M,io-reserve=24K,
pref32-reserve=4M,hotplug=off`, `fw-bus-pad=48,fw-io-pad=12K,
fw-mem-pad=384M`, both tunnel ports `power-controller=off`), one vCPU and
512 MB each. Two fresh overlays: the Standard PC HAL base
`E:\xhci98-vm\q35-bases\w2k\` and the ACPI PC HAL base
`E:\xhci98-vm\q35-bases\w2kacpi\`. The package: a test cut of this tree
at `6f250605` (44.53's code; gated `all` x86 build passed,
`out\build-x86.log`; `make-release.ps1 -Arch x86 -Flavor release
-TestLabel p44w2kleg -NoDriverPack -NoNonFree -NoWdmFree -SkipQualtool`,
the version reading 3.2.0.0; zip SHA-256 `345a5cde...`, `xhci98.sys`
`c80eec29...`, `xhcicm.sys` `712bd010...`, `stor98.sys` `b42857a8...`,
`stor98.inf` `3b793bf3...`, `XHCIINST.EXE` `1ab9fce9...`, `XHCIMARK.EXE`
`cbae8f8a...`), installed by `INSTALL.BAT /console /yes /nopause
/thunderbolt /stor98` with no enclosure. The first run ended with exit
code 2 on both HALs: the q35 board's own QEMU xHCI was left at problem
31 after its update, and the run said to restart and run it again. After
a Shut Down and a cold start the second run ended with exit code 0
("No changes were made"); `XHCIINST /devices` then listed the ASM4242's
router on the connection manager and both tunnel ports with the filter.
The mark service ran with `Restart` 1 (the default). Every start of the
Standard PC guest was a cold start (a Windows Restart hangs in its
loader); the ACPI guest was powered off by its power button and cold
started too. The devices were plugged on USB4 port 1 by QMP `device_add`
at the desktop: the direct shape `usb4-device,...,dialect=usb4,
drive=nvme0` (no `device-model`: the NVMe at 03:00.0, on the tunnel
port's own bus) and the switch shape (`device-model=asm2464`: the
enclosure's two switch ports on buses 3 and 4, the NVMe at 05:00.0). A
4 MiB file of random bytes (`RAND.BIN`, SHA-256 `43ad4ae8...`) was
copied from the transfer disk to the drive and compared with `fc /b`.
Stor98's report `StorInterrupt 5.0.0` (or `3.0.0` for a function on bus
3) was deleted before a plug where the row reads it.

| Leg | HAL | Shape | Result |
|---|---|---|---|
| c, first plug | Standard PC | direct | Windows' wizard named the drive "Stor98 Storage Controller for NVMe SSDs" through the id and installed it from `oem4.inf` (the signature question answered Yes), no restart asked; the log `cm.chain.id=00030200`, `cm.chain.irq=000B0300` (03:00.0 given IRQ 11), `cm.chain.irq.start.stripped=000B0300`; `XHCIINST /devices`: "stor98, started", "Disk drive - disk, started", listed in Safely Remove Hardware; Stor98's report "pin 1 line 0 msi no msix yes port 11/11 polling (interrupt line not set)"; "Volume in drive E is ORICO"; `RAND.BIN` written, `fc /b` "no differences encountered". **Pass** |
| c, first plug | ACPI PC | direct | the same wizard and driver; no `cm.chain.irq` record; Stor98 "pin 1 line 11 ... port 11/11 interrupt"; "ORICO" (E:), `fc /b` no differences. **Pass** (unchanged) |
| d, re-plug after Safely Remove | Standard PC | direct | "Stop" in Unplug or Eject Hardware (the drive, its volume and disk), "can now be safely removed", `device_del`, then `device_add`: the volume back with `RAND1.BIN` and `fc /b` no differences; a second `cm.chain.id`, `cm.chain.irq=000B0300` and `...stripped` in the log; Stor98 started. **Pass** |
| d, re-plug after Safely Remove | ACPI PC | direct | the same, no `cm.chain.irq` record. **Pass** |
| e, the switch enclosure's first plug (after d's drive was safely removed and unplugged) | both | switch | Windows installed the two switch ports; a wizard for the drive as "PCI Device" was cancelled; then "Windows 2000 has finished installing new devices. You must restart your computer" (answered No). The drive's devnode on `stor98`: started on the ACPI HAL; on the Standard PC HAL at **problem 10**, though the log for this plug reads `cm.chain.id=00050400`, `cm.chain.irq=000B0500`, `cm.chain.irq.start.stripped=000B0500` and `cm.chain.start=00050400` (see below). The mark service: "mark written ... (member 0200, unit 03:00.0, serial 3)", no unit restart |
| f, re-plug after Safely Remove, no restart | Standard PC | switch | the enclosure's upstream port "Stop"ped in Unplug or Eject Hardware, `device_del`, `device_add`: the log again `cm.chain.id=00050400`, `cm.chain.irq=000B0500`, `...stripped`; Stor98 started, its report "pin 1 line 0 ... polling (interrupt line not set)"; "ORICO" (E:) with d's `RAND1.BIN`, `fc /b` no differences. **Pass** |
| f, re-plug after Safely Remove, no restart | ACPI PC | switch | the same, Stor98 "pin 1 line 11 ... interrupt", `fc /b` no differences. **Pass** |
| f2, Windows' wizard for f's devnode | Standard PC | switch | f's plug left a Found New Hardware wizard for the drive's devnode (`ConfigFlags` 400h, the id first, `Service` stor98); finished (Stor98 from `oem4.inf`): "The hardware you installed will not work until you restart your computer", the drive at **problem 10**, the log `cm.chain.remove=00050400` and no `cm.chain` record for 05:00.0 after it. On the ACPI HAL the same wizard finished with the drive started |
| h, T.11: `XhciCmSafeEject` 0 | Standard PC | switch | the value 0 written under `Services\USB` (machine-wide), Shut Down, a cold start without the enclosure, the plug: the drive tagged (`cm.chain.id=00050400`), `cm.chain.irq=000B0500` and `...stripped`, "Stor98 Storage Controller for NVMe SSDs - stor98, started", polling; the unit "not marked removable"; the mark service "notice not acted on: member 0200 unit 03 serial 1: safe eject is off for the domain"; "ORICO" (E:), `fc /b` no differences. Windows asked for a restart at the plug (answered No). **Pass** |
| h, T.11: `XhciCmSafeEject` 0 | ACPI PC | switch | the same: tagged, Stor98 started ("... interrupt"), "safe eject is off for the domain", `fc /b` no differences. **Pass** |
| h2, a surprise unplug and re-plug, `XhciCmSafeEject` 0 | both | switch | `device_del` with the volume mounted: Windows' "Unsafe Removal of Device" (the bridge and the disk), then `device_add`: Stor98 started, "ORICO" (E:), `fc /b` no differences; on the Standard PC HAL a second `cm.chain.irq=000B0500`. **Pass** |

**What the Standard PC rows show.** 44.53's interrupt step serves both
shapes at a hot plug and at every re-plug read here: the direct shape's
drive is given IRQ 11 on bus 3, the switch shape's on bus 5, both with
the stripped start, and Stor98 runs them polled; the data written at the
first plug read back unchanged after each re-plug, with and without Safely
Remove and with safe eject on and off. The ACPI HAL is unchanged
(Windows' interrupt, Stor98 interrupt-driven).

**Two problem 10s on the Standard PC HAL, not owed by 44.45.** Both came
after a start of the drive the log records with the added interrupt:

- e: the switch enclosure's first plug on this guest, with the drive
  already known to Windows through d's direct-shape install. The log
  reads the interrupt added and stripped and the start passed on; Device
  Manager read problem 10. Stor98's report for that start was not read,
  and the order of Windows' installs and the drive's start was not
  separated [inf: the drive's stack restarted by Windows' installs at that
  plug, as in f2].
- f2: Windows' class installer reinstalling Stor98 on the running drive.
  The log reads `cm.chain.remove=00050400` and nothing for 05:00.0 after
  it, so the drive's next start had no interrupt [inf: the child filter is
  attached when the chain reports the drive (record 21 section 6.2.1),
  and a removed stack is built again without it until the next
  enumeration].

A re-plug (f) or a cold start (h) gave the drive its interrupt again
each time. Neither failure is one of 44.45's owed legs; whether a
drive's stack rebuilt after a removal (a driver update, a disable and
enable) should get the interrupt is for record 21 section 6.2.1, for the
coordinator.

**Verdict.** 44.45's last owed items on 2000 read: T.2's hot plug in the
direct shape and T.11's re-plug (with the switch shape and the direct
shape, after Safely Remove, and with `XhciCmSafeEject` 0) pass on both
the Standard PC and the ACPI PC HAL, the drive coming back with its data
each time. Nothing in this package was changed. Evidence:
`E:\xhci98-vm\p44\p44-w2kleg\` (`sp\` and `acpi\`: `serial-c.log` for
c to f2, `serial-h.log` for h and h2, `infopci-c-plug.txt`,
`infopci-c-run.txt`, screenshots `c3.png` to `c9.png`, `d1.png` to
`d5.png`, `e1.png` to `e4.png`, `f1.png` to `f9.png`, `h1.png` to
`h4.png`, `cmdline-*.txt`; `pids.txt`, `hashes.txt`, `rand.sha256`).

## 44.72 - 32-bit Windows 8 With "Above 4G Decoding" Off and `XHCIPFW.SYS`: Why the Start Still Stops (2026-10-27 and 28)

A task of Phase 44 (the owner, 2026-10-27): 8 x86 does not start on
the 4G-off layout with `XHCIPFW.SYS` installed ("44.49", the reviewed
cut's legs). Find out why; fix it if the cause is this package's and the
fix is small, else document it.

### The vehicle

Development host A; q35, the emulator and launcher of "44.49"
(`bin\asm4242-20261026-4goff`, `qemu-system-x86_64.exe` `08D49470...`),
the 4G-off layout with the ORICO at power-on, `qemu64` processors, two of
them per guest, 2 GB. Fresh overlays of the clean q35 bases of 8 x86
(`b8e091bd`), 8.1 x86 (`9714c3d8`) and 10 x86 (`ac361c39`), except `d1`,
an overlay of "44.49"'s `w8-p3` guest (the reviewed cut `p44pfw3`
installed with `/thunderbolt`), used for the first reads only. A scratch
build of `src\pfw\nt` at e6e02860 (not tracked, `qemu` flavour, SHA-256
`C575721C...BA91`) adds stage marks: a line on QEMU's debug console at
`DriverEntry`'s entry, after each input of the decision and after the
step, and reads of 00:00.0 offset `F8h` in the configuration trace (three
at the entry). Installed by hand where a row says "alone": the image
copied to `system32\drivers\xhcipfw.sys` and the service key the
installer writes (Type 1, `Start` 0, `ErrorControl` 0, `Group` `System
Reserved`), with `powercfg /h off`; nothing else of this project's.
"First", "second" and "third" count the cold starts on the 4G-off layout
after that install. QEMU's `pci_cfg_read` and `pci_cfg_write` traces.

### Readings

| Guest | Start | The driver's lines | The start |
|---|---|---|---|
| `d1` (the cut installed) | the 4G-off layout, read and write trace | none; no access by the step in the trace | stops at the logo, as in "44.49" |
| `d1`, the marked image | the same | none, and no `F8h` mark: `DriverEntry` is not entered | stops at the logo |
| `d1`, the marked image | plain (no USB4 model), traced | every mark, `mode` 2, `late` 0 | the Start screen; the PCI driver's probe of the root bus's bridges (all-ones into `24h`) at trace line 9 861, the entry mark at 10 321 |
| `f2`, the marked image alone | first | every mark, `closed` 9 | the Start screen |
| `f2` | plain, traced | every mark | the Start screen; the root bus probed at 9 749 to 10 517 (00:1c.0's bus included), the entry mark at 10 596 |
| `f2` | second, third | none | stop at the logo, as without the step |
| `f2`, `Group` set to `Core` | fourth | none | stops at the logo |
| `f1`, the earlier probe alone (`pfwprobe.sys`) | first | its line, nine closed | the Start screen |
| 8.1 x86, the marked image alone | first, second | every mark, `closed` 9, both | the desktop, both |
| 10 x86 (19041), the marked image alone | first, second | every mark, `closed` 9, both | the desktop, both |

On `f2`'s first start (writes only) the step's closes come at trace
lines 444 to 447 and the PCI driver's probe of the root bus at 486 on.
On the plain starts and on `d1` the PCI driver probes the root bus
first; on `d1`'s plain start with the earlier probe installed beside
`XHCIPFW.SYS`, both run after it. On a stopped start (`d1`, QMP `info registers` with each
`cpu-index`) one processor stays at one address and the other moves
through kernel addresses, as "44.49" read without the package; this
driver's code is not among them (it was never entered).

8's services (`reg query` on `f2`): ACPI `Group` `Core`, `Tag` 2; the
PCI driver `Group` `Boot Bus Extender`, `Tag` 3; `ServiceGroupOrder`
begins with `System Reserved`; `GroupOrderList` has no `Core` entry.

### The cause

On 8's plain starts the PCI driver probes the root bus's bridges before
`XHCIPFW.SYS`'s `DriverEntry` (and the earlier probe's) is entered
[read: the traces]. On the starts with the windows above 4 GB that
stopped, the entry mark never came [read]; that the PCI driver's work on
the windows above 4 GB is where they stop, as "44.49" read without the
package, and that the step is therefore never reached, is an inference
[inf]. The first start after installing the driver alone is the
exception: the step ran before the PCI driver's probe, closed the
windows, and 8 started [read: `f2`, `f1` and "44.49"'s probe leg]; at the
second and third starts it did not [read: `f2`]. What orders the first
start differently is not read. That 8 starts the ACPI driver (`Group`
`Core`) and through it the PCI driver ahead of the boot drivers of
`System Reserved`, where 7 reads the bridges after them ("44.49"'s probe
on 7), is an inference [inf]. The group `Core` did not help either
[read]. 8.1 and 10 x86 closed the windows and started at both starts
read [read]. So the step's place (record 32 section 11.2) does not hold
on 8. The earlier probe and the cut differ in nothing read here: both
ran after the PCI driver on the same plain start, and each, installed
alone, closed the windows on its first start.

"44.49"'s 8 leg stopped at its first start, where `f2` started. That
guest had the whole package; whether its other parts (the tunnel-port
filter on the q35 root port, whose id the ASM4242's root port shares)
move the PCI driver earlier at the first start too is not read.

### No fix here

A boot driver of a later group is later still. A step earlier than the
PCI driver on 8 would need another place: a filter in the PCI root bus's
own stack (record 32 section 11.2 set aside an upper filter, its timing
not read; whether a lower filter would run before the PCI driver reads
the bridges is not read either) or a step in the boot loader's chain
(also set aside there). Either is a new design, not a small change. So 8 x86 with the windows
above 4 GB is a limitation of this release. The owner's choice (below).

Found on the way: `d1` after several stopped starts came back with its
`SYSTEM` hive rolled back (no `xhcipfw` or `xhcicm` service, the files
still there); its later readings are not used. A guest that stopped at
start many times is not a reading of the package.

Evidence: `E:\xhci98-vm\p44\p44-w8pfw\` (`STATE.md`, `pids.txt`; `d1`,
`f1`, `f2`, `g81`, `g10`: `debugcon-*.log`, `trace-*.log`,
`serial-*.log`, screenshots; `xferd`, `xferf` the batches), the marked
build in `E:\xhci98-scratch\p44-p44-w8pfw\diag`.

Owner's choice (revised in "44.72 (continued)" below: the order varies
on 8.1 and 10 too, and the recommendation there is (b')): (a) keep 8 x86 in the box, with the readme and the
release notes saying 8 may start with it only at the first start after
the install (read with the driver alone; the whole package's first start
stopped in "44.49"); (b) leave 8 x86 out of the box (the installer's
list and the readme), 8.1 and 10 x86 unchanged; (c) a new design for 8:
a filter in the PCI root bus's stack (record 32 section 11.2's first
alternative names an upper one; whether either kind runs before the PCI
driver reads the bridges is to be read first). Recommendation: (b) for this release, (c)
as a task of its own if 8 x86 on such boards is wanted.

## 44.72 (continued) - Vista, 7, 8.1 and 10 x86: Does the Step Run Before the PCI Driver at Every Start? (2026-10-28)

The owner's follow-up of 2026-10-28: read Vista SP2, 7 SP1 and 10 x86
as 8 was read above, to see whether any of them is "first start only";
a third start on 8.1 x86; on 10 x86 the whole package as well.

### The vehicle

As above: q35, the 4G-off layout with the ORICO at power-on, the
`qemu64` processor, two processors, 2 GB, a fresh overlay of each clean
q35 base (Vista x86 `4d1aa775`, 7 x86 `95893177`, 8.1 x86 `9714c3d8`, 10
x86 `ac361c39`), and QEMU's `usb-tablet`. "Alone" is the marked image of
the section above (SHA-256 `C575721C...BA91`) with the installer's
service key, copied in by hand on the 4G-off layout itself, fast startup
off (8 in the section above: on the plain board). "Package" is the cut
`p44pfw3` installed with `XHCIINST.EXE /dir release-x86 /yes /nopause
/thunderbolt` on the 4G-off layout (exit 2: the tunnel-port filter on the
root port, "Restart"), the marked image then copied over `xhcipfw.sys`.
Each start a cold start; "RW" a trace of reads and writes (slower), "W"
writes only. Columns: the line of `DriverEntry`'s entry mark (seven
reads of 00:00.0 `F8h` in a row; RW only), of the step's close of the
root port (`24h` <- `FFF0h`), and of the PCI driver's probe of the root
port (`24h` <- all-ones); "closed" the driver's count on the debug
console; the problem codes from `wmic` sent to COM1 at the desktop.

### Readings

| System | Start | Trace | Entry | Close | PCI probe | Closed | Result |
|---|---|---|---|---|---|---|---|
| Vista SP2, alone | 1st | RW | 5 076 | 21 660 | 23 001 | 9 | not read (the reading's batch did not run) |
| | 2nd | RW | 5 076 | 21 660 | 23 049 | 9 | stopped after a minute, not read |
| | 3rd | RW | 5 076 | 21 660 | 23 001 | 9 | the desktop; every ASM4242 bridge and both enclosure bridges 0; the NVMe, xHCI and NHI 1 (no driver) |
| | 4th | RW | 5 076 | 21 660 | 23 001 | 9 | the same |
| 7 SP1, alone | 1st | RW | 13 330 | 29 914 | 31 381 | 9 | not read |
| | 2nd | RW | 13 330 | 29 914 | 31 381 | 9 | stopped after a minute, not read |
| | 3rd | RW | 13 330 | 29 914 | 31 381 | 9 | the desktop; every bridge 0; the NVMe, xHCI and NHI 28 |
| | 4th | RW | 13 330 | 29 914 | 31 381 | 9 | the same |
| 8.1, alone (`g81`) | 1st, 2nd | W | - | 446 | 511 | 9 | the desktop (the section above) |
| | 3rd | RW | none | none | 14 763 | none | Windows' recovery screen, "It looks like Windows didn't load correctly"; the driver's line not printed |
| | 4th | RW | 14 457 | 31 041 | 31 603 | 9 | the desktop; every bridge 0, only the NHI and QEMU's `fw_cfg` 28 |
| | 5th | W | - | 446 | 511 | 9 | the same |
| | 6th | RW | 14 457 | 31 041 | 31 603 | 9 | the same |
| 10 (19041), alone (`g10`) | 1st, 2nd | W | - | 446 | 593 | 9 | the desktop (the section above) |
| | 3rd | RW | 31 405 | 47 989 | 15 497 | 9 | the desktop (screenshot); the problem codes not read (the batch did not run) |
| | 4th | RW | 14 603 | 31 187 | 32 337 | 9 | the desktop; every bridge 0, only the NHI and `fw_cfg` 28 |
| | 5th | W | - | 446 | 593 | 9 | the same |
| | 6th | RW | 14 603 | 31 187 | 32 337 | 9 | the same |
| 10, package (`j10`) | 1st | W | - | 446 | 593 | 9 | the desktop; only `fw_cfg` 28 (the NHI not listed: its driver installed) |
| | 2nd | W | - | 446 | 593 | 9 | a black screen at seven minutes, not read |
| | 3rd | W | - | 446 | 593 | 9 | the desktop; only `fw_cfg` 28; `XhciPfwSkipped` 0, `XhciPfwClosed` 9 |

8 (the section above, `f2`, alone), for comparison: 1st start W, close
446 before the probe 511, the Start screen; 2nd W, 3rd RW and 4th W
(group `Core`) no close, the PCI driver's probe at 475 or 14 762, the
start stops.

On Vista and 7 every start traced the same sequence: the entry, the
step's close, then the PCI driver's probe, at the same lines but for
Vista's second start's probe (23 049) [read]. On 8.1
and 10 the order changed from start to start: five of six starts each
had the step first; 8.1's third start had the PCI driver's probe and no
entry (as 8's), and 10's third start had the PCI driver's probe of the
root port (line 15 497, written back with its window above 4 GB) before
the entry (31 405), the step closing the windows after it (47 989), and
the desktop all the same [read]. Of 8's four starts one had the step
first [read].

### What it says

- Vista and 7: the step runs before the PCI driver at every start read
  (four each) [read], in a fixed order [inf: the same sequence each
  time, nearly the same lines].
- 8.1 and 10: the step runs first at most starts, not at every one
  (five of six each) [read]. When it does not, 8.1 stopped and showed
  Windows' recovery screen, then started at the next start [read]; 10
  reached the desktop with the windows closed after the PCI driver had
  read them [read], whether its devices behind the root port were all
  right at that start was not read.
- 8: the step was not first at three starts of four [read]; that this is
  the same race as on 8.1 and 10 is an inference [inf].
- So from 8 on, the boot drivers of `System Reserved` and the PCI
  driver's enumeration of the root bus are not ordered, and which comes
  first varies with timing [inf]; on 8 the PCI driver usually comes
  first, on 8.1 and 10 the step usually does. The "first start only"
  of the section above is this race on 8 [inf].
- The whole package on 10: the step came before the PCI driver's probe
  at all three starts [read]; the first and third reached the desktop,
  the second showed a black screen at seven minutes and was not read.

### The owner's options for 8, revised

The race is not 8's alone, so leaving 8 out (option (b) above) leaves
8.1 and 10 with a step that occasionally misses, and on 10 a step that
can close a window after the PCI driver read it. The options:
(a) keep the box on 8, 8.1 and 10 and say in the readme and the release
notes that a start can still stop and that another start may help
(read once on 8.1; on 8 the starts after a stopped one stopped too
[read], so not to be relied on); (b') offer the box on
2000, XP, Vista and 7 only, where the order was the same at every start
read; (c) a new design for 8, 8.1 and 10, a step in the PCI root bus's
stack whose timing is read first (record 32 section 11.2's alternatives).
A guard that skips the step when the PCI driver has already probed the
root port would stop the late close on 10 but not the stopped starts;
the registers alone do not tell (the PCI driver writes the firmware's
values back, "44.49"), so it would need another sign, not read. Recommendation: (b')
for this release, with the readme's line for 8, 8.1 and 10 on such
boards; (c) as its own task.

Evidence: `E:\xhci98-vm\p44\p44-w8pfw\` (`hv` Vista, `h7` 7, `g81`, `g10`,
`j10`: `trace-*.log`, `debugcon-*.log`, `serial-*.log`, `leg-*.txt`, the
screenshots; `cold.sh`, `post.sh`, `xferv\W.BAT`, `xferp3` the package
batch; `pids.txt`, `STATE.md`).

## 44.72 (the owner's decision) - `XHCIPFW.SYS` on 2000 to 7 Only: the Installer's Legs (2026-10-28)

The owner's decision of 2026-10-28 on the options above: (b'),
`XHCIPFW.SYS` offered on the 32-bit 2000, XP, Vista and 7 only; on the
32-bit 8, 8.1 and 10 not offered, `/xhcipfw` refused, and one the
installer placed removed (design record 32 sections 9 and 11.3); (c) is
planned work, PFW8.1.

### The change

`p44-pfw7` `36e45178`, `b820e95f`, `b2f3188b` (reviewed and revised):
`inst_pfw_sys` gives the 32-bit 8, 8.1 and 10 `INST_PFW_SYS_GONE`;
`inst_pfw_box` shows no box there and `inst_pfw_mode` answers
`DESELECTED` whatever the boxes, so a run removes an `XHCIPFW.SYS` this
installer's record holds, by record 32 section 11.2's rules (the
service's image must be one the record holds; another installation's
image keeps its service, an unreadable one or an unresolved path removes
nothing); with no record the run says nothing of it. The console says
the removal where the question would be (`/list` included) and the
window's Summary carries it. `/xhcipfw` is refused there with
"REFUSED: /xhcipfw: XHCIPFW.SYS may start too late on Windows 8, 8.1 and
10.", `/noxhcipfw` with the line of every system not offered it. Host
vectors in `test_inst.c`.

### Builds

`test\run-host-tests.cmd` passed; `xhciinst\build.cmd` EXIT 0;
`scripts\check-text.ps1` OK; `scripts\package\test-package.ps1` OK (1678
checks). For the legs the pre-cut matrix's package
(`E:\xhci98-vm\m33\pkg\xhci98-3.3.0.0.zip`, `4ee205cf...`) as `PKGO`, and a
second copy, `PKG`, with this branch's `XHCIINST.EXE` (`ac5bfcb1...`),
`XHCIWIZ.EXE` (`5aa61f97...`), `XHCIIN64.EXE`, `XHCIWZ64.EXE` and
`SETUP.EXE` (`44b4e2f1...`) at `b2f3188b` in place, built with the
version header stamped 3.3.0.0 for the run only and reverted (as
"44.79"). The package's `XHCIPFW.SYS` `21fb9f81...`.

### Legs

Development host A; q35 through `launch-q35.ps1`, `-Usb4 none` (the
`qemu-xhci` behind a root port), fresh overlays of the clean bases
`w10x86` (19045), `w81x86` and `w7x86`, two processors, 2 GB; the
packages on the read-only FAT transfer disk; batches in an elevated
prompt, their output sent over COM1.

| Guest | Step | Result |
|---|---|---|
| 10 x86 | `PKGO` (the m33 cut as it is): `INSTALL /console /yes /nopause /thunderbolt` | its question "Close PCI windows above 4 GB (XHCIPFW.SYS)", `/yes: Y`; "XHCIPFW.SYS is in C:\Windows\system32\drivers\xhcipfw.sys and registered"; exit 3; the record `File` `21FB9F81...`; after a restart `sc qc xhcipfw` `BOOT_START`, `xhcipfw.sys` |
| | `PKG`, the window, `SETUP.EXE /thunderbolt` | Components: no XHCIPFW box (the Thunderbolt box ticked and greyed, Stor98 under it); Summary: "PCI windows above 4 GB closed at start (XHCIPFW.SYS): removed"; cancelled: PASS |
| | `PKG`, `INSTALL /console /list /nopause /thunderbolt` | no question; "PCI windows above 4 GB closed at start (XHCIPFW.SYS): removed." and "XHCIPFW.SYS is not offered on Windows 8, 8.1 and 10.": PASS |
| | `... /yes /nopause /thunderbolt /xhcipfw` | "REFUSED: /xhcipfw: XHCIPFW.SYS may start too late on Windows 8, 8.1 and 10.", exit 1: PASS |
| | `... /yes /nopause /noxhcipfw` | "REFUSED: /xhcipfw and /noxhcipfw are for Windows 98 SE, ME, 2000, XP and the 32-bit Vista and 7 only.", exit 1: PASS |
| | `... /yes /nopause /thunderbolt` | "XHCIPFW.SYS is no longer registered; Windows stops loading it at the next start."; exit 3; the service key, the record and `xhcipfw.sys` gone, no pending rename: PASS |
| | after a restart | `sc query xhcipfw` 1060 (no such service), no service key, no record, no `xhcipf*.sys`; the desktop: PASS |
| 8.1 x86 | the window, `SETUP.EXE /thunderbolt` | no XHCIPFW box; the Summary without an XHCIPFW line (nothing recorded); cancelled: PASS |
| | `INSTALL /console /list /nopause /thunderbolt` | no XHCIPFW line: PASS |
| | `... /thunderbolt /xhcipfw` | the refusal above, exit 1: PASS |
| | `... /yes /nopause /thunderbolt` | exit 3 (the connection manager installed); no `xhcipfw` service key, no record, no `xhcipf*.sys`: PASS |
| 7 x86 | `INSTALL /console /list /nopause /thunderbolt` | "Close PCI windows above 4 GB (XHCIPFW.SYS): Windows 2000 and XP, and the 32-bit Vista and 7", the new text, "Enter would give [Y]": PASS |
| | `... /thunderbolt /noxhcipfw` | "/noxhcipfw: left out without asking.": PASS |
| | the window, `SETUP.EXE /thunderbolt` | Components: "Close PCI windows above 4 GB (XHCIPFW.SYS)" shown and ticked under the ticked Thunderbolt box; cancelled: PASS |

Not read: a removal past another installation's `xhcipfw` image (the
code path is reviewed, not run), XP and 2000 (their kinds are unchanged).
Evidence: `E:\xhci98-vm\p44\p44-pfw7\` (`STATE.md`, `pids.txt`,
`hashes.txt`; `w10\serial-a.log`, `w10\legB.txt`, `w81\legE.txt`,
`w7\serial-a.log`; `shots\w10-w*.png`, `w81-w*.png`, `w7-w*.png`).

## 44.74 - Windows 2000 and a High Speed device left unconfigured (2026-10-28)

A task of Phase 44 (the coordinator, 2026-10-28): usb-wdm-free's record
of the newer Realtek chips (branch `rtl-new`,
`docs/testing/2026-10-27-rtl815x-newchips.md`, "Notes on 2000") read
that on Windows 2000 under xHCI98 3.1.0.0 the simulated RTL8156 at High
Speed "was never configured: six control requests, then nothing", while
98 SE and ME configured it. Find out whether 3.3.0.0 has this and why.

### The vehicle

Development host A; q35 with the stock QEMU 11.1.0 (`usb-redir` is not
in the emulator build the q35 launcher pins), `qemu-xhci` with `p2=4`
and `p3=4` behind a root port, as `launch-q35.ps1` lays it out. Fresh
overlays of the clean q35 base of 2000 SP4 (`7fc9714e`, Standard PC
HAL), one processor, 512 MB. The simulator is usb-wdm-free's `rtlemu.py`
at `48c1366` (SHA-256 `ab99a242...`), copied, on a TCP port of this
worker's range, plugged as `usb-redir` (`chardev-add socket ...`,
`device_add usb-redir,...,bus=xh.0,port=N`), `--chip 8156`, with
`--speed high` for High Speed. Its descriptors: one configuration (the
vendor one, interface class FFh, value 1), `iSerialNumber` 0, `bcdUSB`
2.10 at High Speed. The driver is that record's `rtl815x.sys`
(`4ff13e51...`) with `RTL815X.INF` (`42d166f1...`), unsigned.

- `g0`: 3.2.0.0 as published (`xhci98.sys` `dd230d50...`), installed by
  `INSTALL /console /yes`.
- `g1`: this branch at `d0ee405e`, the gated x86 `release` build
  (`build-driver.cmd all`, EXIT 0; `xhci98.sys` `7f61a5f6...`, staged by
  `make-package.ps1`), installed the same way with this branch's
  `XHCIINST.EXE`.

QEMU 11's `qemu-xhci` numbers its USB 3 ports first: bus port 1 is xHCI
port 1 at SuperSpeed and port 5 at High Speed, bus port 2 is port 6 at
High Speed (XHCITR's report: "[Port 5, USB 2] ... High Speed").

### Readings

| Guest | Step | The simulator's log | Windows |
|---|---|---|---|
| `g0` | High Speed, bus port 1, first plug | enumeration, then nothing until the driver was installed | Found New Hardware wizard; the driver from a folder; "Digital Signature Not Found": Yes |
| `g0` | High Speed again | `SET_CONFIGURATION` 1, link 1000, DHCP | started |
| `g0` | SuperSpeed, bus port 1 | 8 requests, then none until answered | "Digital Signature Not Found", no wizard |
| `g0` | answered Yes | `SET_CONFIGURATION` 1, link 2500, DHCP | started |
| `g0` | High Speed, bus port 2, a command window open | 8 requests, then none | "Digital Signature Not Found" over the window |
| `g0` | the leg's text typed: `ping -n 8 -l 1472 192.168.77.1`, Enter | none | the dialog closed; the window got `-n 8 -l 1472 ...` |
| `g0` | unplugged, plugged again | 8 requests, then none | no dialog, no wizard; instance `...&0&6`: `ConfigFlags` 40h, no `Driver` |
| `g1` | SuperSpeed, bus port 1, first plug | `SET_CONFIGURATION` 1, link 2500, DHCP after the wizard | wizard, the driver, Yes; started |
| `g1` | High Speed, bus port 1, a command window open | 8 requests, then none | "Digital Signature Not Found" |
| `g1` | the leg's text typed, as above | none | the dialog closed |
| `g1` | unplugged, plugged again | 8 requests (`GET_DESCRIPTOR` device 8 and 18, BOS 5 and 12, configuration 9 and 39, string 0 and 2), then none | no dialog; `...&0&5`: `ConfigFlags` 40h, no `Driver`; XHCITR: "Configuration: 0", "Driver key: (not reported)" |
| `g1` | High Speed, bus port 2 | 8 requests, then none until answered | "Digital Signature Not Found": Yes |
| `g1` | answered | `SET_CONFIGURATION` 1, link 1000, DHCP | started |

3.1.0.0 read no BOS at High Speed, hence that record's six requests;
3.2.0.0 and this branch read it, hence eight.

### The cause

The bus enumerates the device and reports it at every speed and port
[read]. The simulated device has no serial number, so its instance id is
the location form (design record 13 section 10.7): one devnode per xHCI
port (`...&0&1`, `&5` and `&6` read), as for any device without a
serial. Each new devnode is a new install on 2000; with the INF already
copied by an earlier one, 2000 goes straight to "Digital Signature Not
Found" for the unsigned driver and loads nothing until it is answered
[read]. Typed text answers it: the `n` of `ping` is the No button's
accelerator. A declined install leaves the devnode with `ConfigFlags`
40h (`CONFIGFLAG_FAILEDINSTALL`) and no driver, and a later plug of that
devnode raises nothing and configures nothing [read]. That record's leg
`L32` typed the same ping text about 75 s after its first High Speed
plug, and its later "pre" plug of the same device saw nothing; that its
dialog was answered so is an inference from the same text and the same
outcome here [inf]. 98 SE and ME install an unsigned driver without the
prompt, so their legs configured the device.

### Verdict

Windows 2000 behaviour (a pending, then declined, unsigned-driver prompt
for a new devnode) met by the harness's keystrokes; not this package and
not the simulator. 3.3.0.0 configures the device at High Speed once the
prompt is answered Yes [read: `g1`]. No change here. For the
usb-wdm-free record: answer the prompt before typing into the guest (or
read `ConfigFlags` of the new instance); a devnode left at 40h is
reinstalled from Device Manager (Reinstall Driver), which was not read
here. A device whose serial number the bus accepts as its instance id
(section 10.7: not a duplicate, read without failure) keeps one devnode
across ports and speeds, so it meets the prompt once.

Evidence: `E:\xhci98-vm\p44\p44-w2khs\` (`STATE.md`, `pids.txt`; `g0`,
`g1`: screenshots, `serial-*.log` with the `Enum` exports and the
XHCITR report; `ev\`: the simulator's logs `g0-*` and `g1-*`).

## 44.75 - The Settings Scripts and the Controller's Model Names (2026-10-28)

The pre-cut matrix N1 (2026-10-28, its failure F1) found
`TOOLS\SILENT\silent-on.bat` and `TOOLS\PORTSW\portsw-on.bat` saying "No
xhci98 controller was found in the registry, so nothing was read or
changed. Install the driver first." with the driver installed and
started, on 98 SE, ME and 2000. Both `src\silent.vbs` and
`src\portsw.vbs` took a class key only when its `DriverDesc` was, as a
whole string, "xHCI98 USB 3 Controller" (`3.2.0.0`'s name) or "xHCI98 USB
3.x eXtensible Host Controller" (`2.0.0.0` to `3.1.0.0`); since 44.55
`src\xhci98.inf` gives each controller it lists its own name, "xHCI98 USB
3 Controller (<maker product>)", 108 of them, and that is the
`DriverDesc`. Only a controller bound by the class line kept the plain
name.

### The consumers read

Every place that could identify this driver's controller by a name:

| Consumer | How it finds the controller | Verdict |
|---|---|---|
| `src\silent.vbs` (`TOOLS\SILENT`) | `DriverDesc` whole-string, and `NTMPDriver` `xhci98.sys` | broken; fixed |
| `src\portsw.vbs` (`TOOLS\PORTSW`) | `DriverDesc` whole-string; `NTMPDriver` on 98 SE and ME | broken; fixed |
| `xhciinst` (installer, window) | the service, `NTMPDriver`, the driver key's `InfPath`; names only written (`FriendlyName`) | not affected |
| `xhcitr` (`tr_snap.c`) | the service `xhci98` or `NTMPDriver` `xhci98.sys` | not affected |
| `xhcicln` | the devnode's service (`CM_DRP_SERVICE`) | not affected |
| `xhcimark` | the service name | not affected |
| `scripts\bench\IMOD.BAT`, `IMOD98.BAT` | `find /i "xhci98"` in `DriverDesc` or the export | not affected (a substring) |
| `scripts\package\test-package.ps1` | portsw's `XHCI_DESC` held to the INFs' `XhciDesc` | still true; the model names now held by `check-text.ps1` |
| `scripts\vm-matrix\lib\leghost.ps1` | a comment naming the old name; the wizard is read from the screen | not affected |
| the release readme (`make-release.ps1`) | tells a person the controller's name for Update Driver | text for a person, not a match |

### The fix (`p44-desc` `dfc9f731`, `0f9e6153`)

Both scripts call one function, `IsXhciDesc`, the same text in both: the
plain name, the old name, or the plain name followed by " (" and ending in
")", case-insensitive. On 98 SE and ME the `NTMPDriver` check stays.
`scripts\check-text.ps1` runs `package-common.ps1`'s
`Get-ControllerDescFaults`: the function word for word in both scripts
(`$script:XhciDescFunction`), their two constants equal, and the rule
(`Test-XhciDesc`) run over every models line of both INFs - each
`Xhci.Dev*` name must be taken (108 names, 154 lines, in `xhci98.inf`;
the plain name in `xhci98-amd64.inf`), every other device name refused
(the root hub, the hubs, the composite device) - and over the names of
`2.0.0.0` to `3.2.0.0`, which must be taken. Its self-test refuses the
whole-name match, a changed function, a model named "xHCI98 USB 3.x
Controller (...)" (also on an indented line), the root hub named as a
controller and the two scripts' constants differing.

Host checks: `check-text.ps1` and `-SelfTest` OK; `test\run-host-tests.cmd`
passed; `test-package.ps1` OK (1678 checks).

### The legs

Development host A; q35 through `launch-q35.ps1` (emulator
`asm4242-20261025-2230`, `-Usb4 none`: `qemu-xhci` `p2=4,p3=4` behind a
root port), one processor, 512 MB, fresh overlays of the clean bases
`q35-bases\w98se` and `q35-bases\w2k`. The package: the base download
of the `3.3.0.0` test cut (`xhci98-3.3.0.0.zip` `4ee205cf...`), its
`TOOLS\SILENT` and `TOOLS\PORTSW` as shipped (`SILENT.VBS` `1abdfa6f...`),
and the same folders with this branch's `silent.vbs` (`d18b5226...`) and
`portsw.vbs` (`079238d6...`) in place of the shipped ones. Each folder
copied to C: and its batch run from there, output and registry exports
sent over COM1.

- 98 SE: `XHCIINST /dir release-x86 /yes /nopause` exit 3 ("xHCI98 USB 3
  Controller (Red Hat QEMU xHCI): installed"); a cold start. The shipped
  `silent-on.bat`: "No xhci98 controller was found ..." and "Nothing was
  changed"; `Services\USB` has no `XhciSilentInstall`. The fixed one:
  `Class\USB\0000` read (`DriverDesc` "xHCI98 USB 3 Controller (Red Hat
  QEMU xHCI)", `NTMPDriver` `xhci98.sys`; the root hub at `0001` not
  taken), the six values of `SILENT.REG` read back, "XhciSilentInstall is
  now 1 machine-wide"; `Services\USB` `XhciSilentInstall`=dword:1.
- 2000 SP4: two Found New Hardware wizards (the controller and a "PCI
  Device") answered by Finish with no driver found, as the installer asks;
  `INSTALL.BAT /console /yes /nopause` exit 3 ("xHCI98 USB 3 Controller
  (Red Hat QEMU xHCI): xhci98 3.3.0.0, started"). The shipped
  `portsw-on.bat`: "No xhci98 controller was found ...". The fixed one:
  the controller's key `{36FC9E60-...}\0000` shown with its own value 0,
  then "XhciIntelPortSwitch is now 1 machine-wide";
  `Services\USB` `XhciIntelPortSwitch`=dword:1. The script sets the value
  on any controller it finds; whether the switchover applies is the
  driver's decision for the listed Intel chipsets, so QEMU's controller
  draws no refusal from the script.

Not read: ME (the scripts' path there is 98 SE's), XP to 7 (`portsw.vbs`
takes the same branch as on 2000), and real hardware, where the matrix's
"left for the owner's bench" item 1 stands.

## 44.79 - The window's Thunderbolt box after the connection manager alone, and the x86 boot-driver note (2026-10-28)

A task of Phase 44 (the coordinator, 2026-10-28), from the pre-cut
matrix's N5 leg on 10 x86: in the window, after the "connection manager
alone" state (the USB drivers' box unticked, the Thunderbolt 3 / USB4 box
ticked and greyed), ticking the USB drivers left the Thunderbolt box
ticked, and XHCIPFW's box under it, although the page said no controller
was found and the console (`/usb /list`) gave [N]. And the x86 note "The
connection manager is a boot driver here" was printed on 8, 8.1 and 10
x86 although task 44.32 has the run set `xhcicm` back to demand start.

### Cause

The window's read job set the box's own state (`tbOwn`, the state it
takes back when the USB drivers are ticked again) from the engine's
`inst_eng_tb_state`, which reports the decision itself as the default when
the question is not asked. With the USB drivers out of the run
(`INST_OS_CMONLY`'s default, or `/cmonly`) `inst_tb_decide` answers yes
unasked, so the box's own state became "ticked". Record 26 section 5.3
already said it is unticked unless the user ticked it. Not a regression:
`3.2.0.0`'s window (`git show v3.2.0.0:xhciinst/xhciwiz.c`, the read job,
and `inst_eng.c`'s `tb_decide`, which passed `!usbInclude` as
`/thunderbolt`) took the same yes, so its Thunderbolt box did the same.
XHCIPFW's box is new in `3.3.0.0` and followed its parent.

The note: since 44.32 the end of every NT run that reached its changes
sets `xhcicm` back to start on demand, keeping boot start only where
the system disk is below a
filtered device or its path cannot be read (record 23 section 5.14). So
it is a boot driver while Windows installs its filter and in those cases,
not as a standing fact.

### Change

`410b1997`, `8198b3d6` and `6b6d48e5` (reviewed and revised):
`inst_tb_box_default` (inst_core.c) gives the box's default as the
console's answer with the USB drivers in the run and `/yes` (unticked, a
controller found or not, but by `/thunderbolt`); the window takes `tbOwn`
and its unlocked `tbChoice` from it. Host vectors in `test_inst.c`.
`inst_cm_x86_boot_text` now says Windows makes the connection manager a
boot driver when it installs its filter, that the installer sets it back
at the run's end, the two cases where it keeps it, and that with Secure
Boot on a start while it is still a boot driver would stop in Automatic
Repair. The 32-bit Secure Boot refusal says Windows does not load it then
and makes it a boot driver while it installs its filter. README.TXT, the
release notes and record 26 sections 5.3 and 5.3.3 follow.

### Builds

`test\run-host-tests.cmd` passed (`test_inst` 7045 checks, 0 failures);
`xhciinst\build.cmd` EXIT 0; `scripts\check-text.ps1` OK. For the legs the
package of the pre-cut matrix (`E:\xhci98-vm\m33\pkg\xhci98-3.3.0.0.zip`,
stamped 3.3.0.0) with this branch's `XHCIWIZ.EXE`, `XHCIINST.EXE`,
`XHCIWZ64.EXE`, `XHCIIN64.EXE` and `SETUP.EXE` at `6b6d48e5` in place,
built with the version header stamped 3.3.0.0 for the run only (the
tree's is 3.2.0.0, and the window refuses a package of another version);
`XHCIWIZ.EXE` `d438249c...`, `XHCIINST.EXE` `5298984a...`.

### Legs

Development host A, QEMU q35, fresh overlays of the q35 bases, the package
on a read-only FAT disk (D:), the window started from `D:\PKG\SETUP.EXE`.

| Guest | Step | Result |
|---|---|---|
| 10 x86 (19045), no USB4 model, tablet | Components page opened | USB box unticked, Thunderbolt box ticked and greyed, XHCIPFW ticked: as before |
| | tick the USB drivers | Thunderbolt box unticked and open, "No Thunderbolt 3 / USB4 controller was found ...", XHCIPFW and Stor98 unticked and greyed: PASS (N5 had it ticked) |
| | untick, tick the USB drivers again | ticked and greyed, then unticked again: PASS |
| | `XHCIINST /list /usb /dir release-x86` | Thunderbolt "Enter would give [N]", XHCIPFW "follows the Thunderbolt 3 / USB4" support: the window now matches |
| | `XHCIINST /list /dir release-x86` (the connection manager alone, Secure Boot not known on a legacy BIOS) | the new note's five lines, as committed: PASS |
| 10 x86 (19045), the ASM4242 model | Components page opened | as above, "A Thunderbolt 3 / USB4 controller was found: Universal Serial Bus (USB) Controlle..." |
| | tick the USB drivers | Thunderbolt box unticked (the default is unticked with a controller too, record 26 section 5.3's table), XHCIPFW greyed: PASS |
| | tick the Thunderbolt box, untick and tick the USB drivers | XHCIPFW ticks with it; locked on; back ticked, XHCIPFW ticked (the user's choice kept): PASS |
| | `XHCIWIZ /selftest`, `XHCIINST /selftest` (elevated cmd) | "627 checks, 0 failures", "11 checks, 0 failures" |
| 98 SE, no USB4 model, keyboard | Components page opened | USB box ticked, Thunderbolt box unticked |
| | untick the USB drivers | Thunderbolt box ticked and greyed, XHCIPFW.VXD, Stor98 and S98VSD ticked |
| | tick them again | Thunderbolt box unticked, its children unticked and greyed: PASS |
| | tick the Thunderbolt box, untick and tick the USB drivers | back ticked with its children: PASS |

Nothing was installed (each window cancelled). Evidence:
`E:\xhci98-vm\p44\p44-wbox\` (`STATE.md`, `pids.txt`, `hashes.txt`;
`shots\n-*.png` 10 without the model, `a-*.png` with it, `w98-*.png`).

## 44.78 - 98 SE: Yes to the bridge wizard's restart leaves the enclosure unenumerated (2026-10-28)

A task of Phase 44 (the coordinator, 2026-10-28), from the pre-cut
matrix (`E:\xhci98-vm\m33\N3\RESULT.md`, F1 and N2). F1: on 98 SE with
the `3.3.0.0` test cut installed with the Thunderbolt support and
Stor98, the first hot plug of the ORICO enclosure after the desktop
brings Windows' wizard for each of the enclosure's two bridges
(`MACHINE2.INF`, "PCI standard PCI-to-PCI bridge"), and each asks "To
finish setting up your new hardware, you must restart your computer. Do
you want to restart your computer now?". Answered Yes both times, the
enclosure's upstream bridge is started but never enumerated from then
on, and the drive never comes back. N2: Stor98's first install on 98 SE
asks for "Stor98 Storage Controller Disk". Find the cause of both, say
whether 3.2.0.0 had them, and find a recovery or a fix.

### The vehicle

Development host A. q35 (SeaBIOS, AHCI disk), TCG, one processor,
512 MB, `-display none`, `-action reboot=shutdown` (every restart a cold
start). The emulator's ASM4242 layout (N3's "board" profile:
`asm4242-20261027-boardint`, root port `pref32-reserve=4M`,
`usb4-asm-usp fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, tunnel
ports `power-controller=off`), the ORICO (`usb4-device`,
`device-model=asm2464`) hot-plugged by QMP `device_add` after the
desktop or present at power-on. Fresh overlays of the q35 98 SE base
(`ab1d4d65`). Packages: the `3.3.0.0` test cut (`xhci98-3.3.0.0.zip`
`4ee205cf...`; `release-x86`: `xhci98.sys` `e9236fe1...`, `xhcicm.sys`
`7c5d1aa9...`, `stor98.mpd` `580f7c6d...`, `stor98w9.inf`
`e3f8b8b0...`, `stor98.inf` `00c3ba0c...`; `XHCIINST.EXE`
`9c962f83...`) and `3.2.0.0` as published (`xhci98-3.2.0.0.zip`),
each installed by `XHCIINST /dir release-x86 /yes /nopause /thunderbolt
/stor98` (exit 3), then Shut Down and a cold start.

- `f3`, `f2`: overlays of N3's `sb3` (the failing guest) and `sb2` (the
  guest answered No), started without the enclosure only to export the
  whole registry (`regedit /e`), read offline from the disk image.
- `y`: `3.3.0.0`, the failing path again, then the recoveries.
- `r`: `3.2.0.0`, the same steps.
- `t`: `3.3.0.0`, N2's experiment, the enclosure at power-on.

### The status bit

The stuck bridge's devnode reads `Status` 18006ACEh where a working one
reads 18086ACEh. The missing bit, 00080000h, is `DN_NOT_FIRST_TIMEE`,
"Has received a config enumerate" (the Windows 98 DDK's `CONFIGMG.H`,
its "4.0 OPK2 Flags", line 2069; `CFG.H` defines the flags up to
`DN_WILL_BE_REMOVED` only). The bridge was started (`DN_STARTED`,
`DN_ENUM_LOADED`) and has no child devnode; no enumeration of it
was completed.

### The cause: Windows' Automatic Skip Driver Agent

The two whole-registry exports differ, beyond the drive's own keys and
the user's history, in one value [read: `f3`, `f2`]:

    HKLM\System\CurrentControlSet\Control\ASD\Prob\
      {9B4E7760-3196-11CF-97EA-00AA0034319D}
      "PCI\VEN_1B21&DEV_2463&SUBSYS_00000000&REV_00\0000001800"

01 on the failing guest, 00 on the working one. The key is under `REGSTR_PATH_ASD_PROB`
(`REGSTR.H` line 93); the sibling `ASD\List` (line 94) names that GUID
"Enumerating a device" in its `Problem` value [read], and the failing guest's `C:\ASD.LOG` reads
"Enumerating a device / PCI standard PCI-to-PCI bridge" with that
instance id. It is Windows 98's Automatic Skip Driver Agent: an
operation it holds as having hung the computer is skipped at every
later start and plug. `Enum` and `LogConfig` of the bridges, their
`Class\System` keys and the connection manager's records are the same
on both guests.

How the flag is set, read step by step on `y` [read]:

| Step | The upstream bridge's devnode | ASD "Enumerating a device" |
|---|---|---|
| first plug, its wizard open | problem 1, `DN_NEED_TO_ENUM` | no value |
| at the restart question, before Yes | problem 1 | no value |
| after Yes, the next start | - | no value |
| second plug, the downstream bridge's wizard open | started, `DN_NEED_TO_ENUM`, no `DN_NOT_FIRST_TIMEE` | no value (its "Starting a device" 00) |
| Yes to that wizard's restart, the next start | - | 01 |
| plug again | 18006ACEh, nothing below it | 01 |

The readings above, and the order of the values in the working guest
(the upstream bridge's written after its children's, the drive's
included), fit one mechanism: the upstream bridge's first enumeration on
that installation is still open while the wizard for its child, the
downstream bridge, runs; Yes restarts Windows with it open, and at the
next start the agent marks it as hung [inf]. The
first Yes leaves no value at 01, and the tunnel port's value reads 00
before and after it [read]. That the operation open at that restart was
the tunnel port's enumeration, which had completed at every start
before, and that the agent marks only an operation that never
completed before, are inferences from these readings [inf].

Not this package's state: the connection manager's log at the stuck
plug is a working plug's (N3), and the agent's value is Windows'. The
connection manager makes the hot plug that reaches this wizard; with the
enclosure at power-on, the wizards run during the start and their
restart question comes only after Stor98's (below).

### 3.2.0.0

`r`, the same steps with `3.2.0.0` [read]:

- Plugged after the desktop: no devnode below the tunnel port at all.
  XHCITR's log shows the PCIe tunnel up and `cm.reenum.no-route`; the
  3.2.0.0 connection manager finds no tunnel port to re-enumerate on
  this layout (the bus numbering of 44.46 is this release's).
- At power-on: the wizards for both bridges and Stor98 during the start,
  one restart question after Stor98's; answered Yes, the next start has
  both bridges, the drive's controller and its disk started and no ASD
  value at 01.
- Its 98 SE Shut Down stayed at "Windows is shutting down" four times
  (quit by QMP after 3 minutes or more; noted, not read further).

So not a regression of a path 3.2.0.0 had: 3.2.0.0 cannot bring the
enclosure up by a hot plug on this layout, and its power-on path does
not leave the flag. `3.3.0.0` at power-on (`t`) behaves as 3.2.0.0
does: one restart question after Stor98's, Yes, and the next start has
the chain, no flag at 01 and "ORICO" on E: [read].

### Recovery

Read on `y` [read]:

- Windows' own tool: Start, Run, `asd`. "Automatic Skip Driver" lists
  "Enumerating a device => PCI standard PCI-to-PCI bridge"; ticking it
  and OK asks for a restart; after it, with the enclosure attached, both
  bridges, the drive's controller and its disk start and the value reads
  00 (`y-f2-asd.png` to `y-f4.png`, `y` g).
- Without a restart: the value set to 00 (`regedit /s` of a two-line
  `.REG`), then the enclosure unplugged and plugged again: the chain
  comes up, Stor98's wizard runs (its disk prompt, N2) and "Orico (E:)"
  opens (`y-d10.png`). So the value set during a start takes effect at
  the next plug of that start [read].
- Device Manager's Refresh after the value is 00 also enumerates the
  stuck bridge, without a replug; the drive's controller then stayed at
  problem 14 there (the connection manager's no-IRQ step, 44.30, runs
  after its own re-enumeration, not after Windows'). A Refresh with the
  value still 01 changes nothing (N3, `sb3` i).

Answering No to both restart questions avoids it (N3 `sb2`).

### What the package could do (owner's decision)

The bridges' wizard and its restart question are Windows' own
(`MACHINE2.INF`'s empty `PCI_BRIDGE_DRV`; the bridge is left at problem
1 after its install), so the package cannot remove the question. It can
undo the flag: on 98 SE and ME the CONFIGMG worker of `xhcicm.sys`
(`cm_cfgwk.c`) already walks every devnode below an active member after
each of its re-enumerations and at its start (the tunnel id, task 44.3).
For a devnode there that is started without `DN_NOT_FIRST_TIMEE` and
whose "Enumerating a device" value is 01, it could set the value to 00
(the VMM's `_RegSetValueEx`, beside the `_RegOpenKey` and
`_RegQueryValueEx` thunks it has) and re-enumerate that devnode, once
per devnode per start, and log it. That overrides a Windows safety
mechanism for devices behind a tunnel: a bridge whose enumeration really
hangs would hang again at each start with the enclosure attached. No
code was written; the release notes can carry the recovery instead.

Suggested release-notes text (known limitations, Windows 98 SE):
"When an enclosure is first plugged in after Windows 98 SE has started,
Windows asks to restart after it installs each of the enclosure's PCI
bridges. Answer No. If you answered Yes, Windows may skip the
enclosure's bridge from then on and the drive does not appear. To undo
it, run `asd` from Start, Run. Tick "Enumerating a device => PCI
standard PCI-to-PCI bridge". Click OK. Restart Windows."

### N2: the Stor98 disk prompt

After the console install, `stor98.mpd` is not in
`SYSTEM\IOSUBSYS` (N3 `s4`, R a1: only `S98VSD.VXD`); the INFs are in
the install folder and in `INF\OTHER`. The wizard finds Stor98 in
`C:\XHCI98` (it showed `STOR98W9.INF`, and once `STOR98.INF`, whose 9x
section is the same) and then looks for `stor98.mpd` in the Windows
source folder (`C:\WIN98` offered), not in the INF's folder, so it asks
for the disk [read: `y`, `r`]. 3.2.0.0 asks the same (`r-c6.png`): not
a regression.

On `t`, `stor98.mpd` was copied by hand into `SYSTEM\IOSUBSYS` after
the install and before the enclosure's first start: Stor98's wizard
installed with no prompt, and E: read "ORICO" [read]. N3's second drive
(`s4` e), with the file already there, also asked nothing. The
demonstrated remedy on 98 SE is the installer placing `stor98.mpd` in
`SYSTEM\IOSUBSYS` when it stages Stor98 (as it places `S98VSD.VXD`);
for ME, which never asked (N3), the same step is proposed. No INF
change (a tag file in `[SourceDisksNames]` among them) was tried. That is the installer's Stor98 path, which another task of this phase
changes; not changed here.

Evidence: `E:\xhci98-vm\p44\p44-yes\` (`STATE.md`, `pids.txt`;
`y\`, `r\`, `t\`: `serial-*.log` with the ASD key and the dynamic enum
at each step, `asdshow.py`'s reading of them; `shots\`); the whole
registry exports and the diff in `E:\xhci98-scratch\p44-p44-yes\`
(`f3x\YES\ALLa.REG`, `f2x\YES\ALLa.REG`, `diff-f3-f2.txt`).

## 44.77 - The update from 3.2.0.0 of a tunnel drive Stor98 runs, on an ASM4242 host; and stor98.mpd before the first plug on 98 SE (2026-10-28)

The pre-cut matrix (N4, failure F2; `E:\xhci98-vm\m33\N4\RESULT.md`,
legs `w7u`, `xpb`, `w2h`): on 7 x86, XP and 2000's ACPI HAL, with the
ORICO enclosure's drive on Stor98 `3.2.0.0`, the first run of `3.3.0.0`
left Stor98 at `3.2.0.0` and said "Update Stor98 by hand", where the
release notes promised the update; a second run after the restart
updated it. Second part (the coordinator, from 44.78's N2 reading): on
98 SE the first install of a tunnelled drive asked for "Stor98 Storage
Controller Disk".

### The cause [read]

N4's 2000 leg's `C:\XHCIINST.LOG` (read from its overlay, flattened):
"tunnel reading: 82 devnodes, 0 not read whole, changing 0, 1
connection managers, all confirmed 1", then the drive "is unknown
(found 1, below a verified port 0, below an unverified port 1)". The
connection manager answered; its record verified no port. On a fresh 7
x86 overlay with `3.2.0.0` installed, the ASM4242 NHI's `XhciCmStatus`
reads port count 0 and active mask 0 (bytes 48 and 52; `w7\serial-b.log`).
`3.2.0.0`'s `cm_ports.c` had no family for the ASM4242 (`1B21:2423` in
the unpaired list); task 44.40 (`1a223e23`) added it. So a tunnel drive
reads as of unknown place under `3.2.0.0`'s connection manager, and a
Stor98 of unknown place at another release holds the shared file
(record 23 section 4.4). On 2000 the place stayed unknown after the
connection manager's own update later in the same run. Nothing else
places the drive soundly: up to `3.2.0.0` Stor98's INF matched every
NVMe controller, so the installer's own record of a drive Windows bound
is no proof of a tunnel. The rule stays; the texts change (record 31
section 9.4, "Read 2026-10-28").

### The change

`ad95ba58`, `d76c2d04`, `201210f3`, `4f7ef57f` (reviewed and revised):
`inst_tb_cm_changes`, `inst_s98_read_later`, `inst_s98_later_said`, the
`later` argument of `inst_s98_left_text`. When the run installs or
updates the connection manager, a present drive on an older Stor98 at
an unknown place is read again after the restart: its LEFT line, the
shared file's note and the run's end say "run the installer again after
the restart", unless another Stor98 binding that reading would not free
keeps the file held. The log lists each confirmed record's tunnel ports.
`1c922958` (reviewed and revised): on 98 SE and ME, with Stor98 ticked,
no other Stor98 and no `stor98.mpd` installed, the installer puts the
bundled file in `SYSTEM\IOSUBSYS` before any device install
(`inst_s98_mpd_place`), recorded as `MpdPlaced`. Host vectors in
`test_inst` (7051 checks, 0 failures). The gated x86 `release` build
passed at `201210f3`'s code; the TEST cut below built all three x86
flavours at `1c922958`.

### The legs

Development host A, q35, TCG; the ASM4242 layout of 44.45
(`asm4242-20261027-boardint`, `deca3fcb...`), the ORICO (`asm2464`) at
power-on; fresh overlays of `q35-bases\w7x86`, `xp` and `w98se`.
`3.2.0.0`: `xhci98-3.2.0.0.zip` `336be62c...35e4` (`xhcicm.sys`
`d1ba7adc...`, `stor98.sys` `49c88c1c...`, `XHCIINST.EXE`
`c056cea7...`). The update: an x86 TEST cut of this branch at
`1c922958` with the pre-cut matrix's version stamp (`539558af`
cherry-picked on a throwaway branch, never merged),
`xhci98-3.3.0.0.zip` `7543a45d...5120` (`XHCIINST.EXE` `088d899f...`,
`xhcicm.sys` `da645518...`, `stor98.sys` `1dda9cd4...`, `stor98.mpd`
`10a7d134...`). Console runs: `INSTALL.BAT /console /yes /nopause
/thunderbolt /stor98` (98 SE: `XHCIINST.EXE /dir release-x86 /yes
/nopause /thunderbolt /stor98`).

| System | Step | Result |
|---|---|---|
| 7 x86 | `3.2.0.0` install, cold start | exit 4; D: "ORICO" on `stor98` 3.2.0.0, `fc /b` clean |
| 7 x86 | update, run 1 (`u1`) | Stor98 LEFT: "the Thunderbolt driver running now does not list its port, and this run installs or updates that driver. Run the installer again after the restart"; the hold note says the same; the end: "Stor98 is left in this run (see above): restart Windows, then run INSTALL.BAT again to update it."; no "by hand"; exit 4 |
| 7 x86 | restart, run 2 (`u2`) | "Stor98 was updated on ..."; the re-plug asked; exit 4 |
| 7 x86 | re-plug (QMP), run 3 (`u4`) | "driver stor98 3.3.0.0; current (this release)", exit 0; D: ORICO, `fc /b` clean |
| XP | `3.2.0.0` install | exit 4 twice while Found New Hardware wizards were walked by keys, then installed and recorded; cold start: D: ORICO on `stor98` |
| XP | update, run 1 (`u1`, `u2`) | `u1` exit 2: the harness's keys declined XP's unsigned-driver prompt (`xhciuas.inf` 0xE000022F); `u2`, Continue Anyway: the same LEFT, note and end lines as 7, exit 4 |
| XP | restart, run 2 (`u3`) | "Stor98 was updated on ...", exit 4 |
| XP | re-plug, run 3 (`u4`) | "driver stor98 3.3.0.0; current", exit 0; D: ORICO, `fc /b` clean |
| 98 SE | install, no enclosure (`s1`) | "stor98.mpd is in C:\WINDOWS\SYSTEM\IOSUBSYS\stor98.mpd, for the first NVMe drive behind a tunnel."; exit 3 |
| 98 SE | power-on with the enclosure | two bridge wizards, then Stor98's from `C:\XHCI98\STOR98W9.INF`: no disk prompt (`98-s-b-s98-*.png`); its restart question: Yes |
| 98 SE | next start | E: "ORICO", `fc /b` clean; `IOSUBSYS\STOR98.MPD` 55,281 bytes; `MpdPlaced` = the package file's SHA-256 |
| 98 SE | uninstall, no enclosure | "C:\WINDOWS\SYSTEM\IOSUBSYS\stor98.mpd was deleted.", exit 3 |

The 7 log's tunnel readings: under `3.2.0.0`'s connection manager the
NHI "lists 0 tunnel ports, active 0x0" and the drive is unknown; after
the restart "lists 2 tunnel ports, active 0x3" and the drive is "behind
a tunnel". On 7 a reading later in run 1, after the connection
manager's update, already found the port verified, but the hold made
with the run's copy stands to the run's end (record 23 section 4.4).
The second run on XP and 7 updates Stor98 and asks for the re-plug, as
any update of a tunnel drive does: one run is not reachable on this
host from `3.2.0.0` under the rule above. 2000's ACPI HAL was not run
again here (N4's `w2h` log reads the same cause). Not read: a host
whose tunnel ports `3.2.0.0` listed, where the update should read the
place in the first run.

Evidence: `E:\xhci98-vm\p44\p44-s98up\` (`STATE.md`, `pids.txt`;
`w7\`, `xp\`, `k98\s\`: `serial-*.log`, `cmdline-*.txt`; `shots\`);
N4's 2000 log at `E:\xhci98-scratch\p44-p44-s98up\w2h\log\XHCIINST.LOG`.

## 44.80 - "Can run faster" held to what this computer gives, and Windows' own ports (2026-10-28)

A user's report via the owner (2026-10-28, XP, a VIA VL805, whose USB 3
ports run at 5 Gbit/s at most): an RTL8157 behind a hub plugged in by its
USB 2.0 pins was listed "Can run at: SuperSpeedPlus (10 Gbit/s or more)",
and plugged in properly it stays counted at 5 Gbit/s, the best that
computer gives. The owner added the same day: an option to watch the
ports Windows' own driver runs, off by default (design record 25
revision 24, section 7.7 items 10 and 11).

### The cause

`tr_fast_check` took "Can run at" from the device's own `_EX_V2` capable
flags (its BOS), never held to the path. No request the tool sent read a
port's top rate, so a SuperSpeedPlus device was counted at SuperSpeed on
any computer (record 25 section 7.7 item 8 said so). `xhci98.sys` knows
each root port's top from the Supported Protocol capability; nothing
served it.

### The fix

- `xhci98.sys`: the query request's version 3 (record 13 section 8.12)
  adds a ports part, each root port's top (`XhciPortTop`: USB 2.0,
  SuperSpeed, or SuperSpeedPlus when a PSI DWORD of its table says so; a
  USB 3 group with no table is SuperSpeed).
- XHCITR: "Can run at" is the least of the device's declaration, the
  fastest of this computer's xHCI98 USB 3 ports (a controller whose ports
  cannot be read counts as SuperSpeed) and SuperSpeed where a hub on the
  path declares no more. The details pane notes a SuperSpeedPlus device
  held to 5 Gbit/s.
- The option "Watch Windows' own USB ports too" (`FasterWindowsPorts`,
  off by default): EHCI, OHCI and UHCI ports; the BOS asked through
  Windows' hub only from `bcdUSB` 0x0210, once per plug, never on
  xHCI98's ports; a failure is "unknown, no notice".

Commits on `p44-fast`: `7b568528` (the change), `99a949da`, `42c056dc`
and `c9238447` (reviewed and revised). Host vectors: `test_tr`
(`test_fast_path`), `test_xhq` (`test_v3`), `test_caps`
(`test_port_top`); the host suite passed, and the gated x86 `release`
build (`build-driver.cmd release`, EXIT 0) of `7b568528` gave the
driver of the legs.

### The vehicle

Development host A; the stock QEMU 11.1.0 (`usb-redir`; the usb4-emu
build the q35 launcher pins has none). The device is usb-wdm-free's
`rtlemu.py` as 44.74 ran it (SHA-256 `ab99a242...`), `--chip 8157`: at
SuperSpeed `bcdUSB` 3.20 and a BOS with SuperSpeed and SuperSpeedPlus
capabilities. For the slower paths a copy patched for this leg
(`rtlemu-u3.py`, `f431b060...`, kept in this worker's scratch folder,
not tracked): with `RTLEMU_USB3_BELOW=1` its BOS carries the same two
capabilities at High and Full Speed, and its `bcdUSB` is 2.10 at Full
Speed too, as a USB 3 device's is below SuperSpeed. Each plug is
`usb-redir` on a TCP port of this worker's range. The package is the
`3.3.0.0` test cut (`E:\xhci98-vm\m33\pkg`, `xhci98-3.3.0.0.zip`,
installed by `INSTALL /console /yes`), then this branch's `xhci98.sys`
(`5a220d2f...`, `7b568528`'s build, file version 3.2.0.0 as the tree is
stamped) and `XHCITR.EXE` copied over the installed ones before the
restart (the installer refuses a driver whose version differs from the
package's).

- XP: q35, a fresh overlay of the q35 base of XP SP3, `qemu-xhci` behind
  a root port and `usb-ehci` on `pcie.0` (Windows' own `usbehci`),
  `XHCITR.EXE` of `99a949da`'s fixes (`687e8545...`).
- 98 SE: the q35 base hangs at its splash on QEMU 11.1.0 (as `p44-stage`
  read), so 98 SE ran on QEMU's i440FX (`pc`), a fresh overlay of
  `p43-5`'s pristine 98 SE, as 44.11's device leg did, with `qemu-xhci`
  and `piix3-usb-uhci` (Windows' own `uhcd`); `XHCITR.EXE` of
  `42c056dc` (`d6f71fcf...`).

### Readings

| System | Step | Result |
|---|---|---|
| XP | the RTL8157 at SuperSpeed on xHCI port 1 | no balloon, the icon not red; the report: no "Can run faster" entry, "Note: This device can run at SuperSpeedPlus. This computer's USB 3 ports run at 5 Gbit/s at most." The test cut's own `XHCITR.EXE` on the same plug: "Can run faster", "Runs at: SuperSpeed (5 Gbit/s)", "Can run at: SuperSpeedPlus (10 Gbit/s or more)" (the report's case) |
| XP | the patched copy at High Speed on the same connector (xHCI port 5) | balloon "RTL8157 Ethernet - SIMULATED runs at High Speed (480 Mbit/s) on a USB 2.0 port; it can run at SuperSpeed (5 Gbit/s)."; the icon red; the list: "Can run at: SuperSpeed (5 Gbit/s)" |
| XP | the patched copy at High Speed on `usb-ehci`, the option off | no balloon, the icon not red; the simulator saw no BOS request |
| XP | `FasterWindowsPorts` 1, the device plugged again | balloon as above, "on a USB 2.0 port"; the icon red; the list: "Port 1 of Intel(R) 82801DB/DBM USB 2.0 Enhanced Host Controller - 24CD", "Runs at: High Speed (480 Mbit/s)", "Can run at: SuperSpeed (5 Gbit/s)", "Try: Connect it to a USB 3 port of this computer."; the simulator saw one BOS request pair (5 bytes, then 42) over every refresh while it stayed; the icon's menu shows "Watch Windows' own USB ports too" ticked |
| 98 SE | the RTL8157 at SuperSpeed on xHCI port 1 | the icon not red; the report: no "Can run faster" entry, the note as on XP, the driver's version 3.2.0.0 (this branch's) |
| 98 SE | the patched copy at High Speed (xHCI's USB 2.0 port) | the icon red; the report: "Can run faster", "Can run at: SuperSpeed (5 Gbit/s)" |
| 98 SE | the patched copy at Full Speed on `piix3-usb-uhci`, the option off, then on | nothing either way: no BOS request, no notice; the report lists no port of the UHCI controller at all (the tool's scan finds none of Windows' own on 98 SE) |

Machine types: XP q35; 98 SE i440FX (the q35 base does not start on the
emulator with `usb-redir`). ME and 2000 to 7 were not read for the
option; the 10 Gbit/s cases rest on the host vectors (no emulated
controller here declares SuperSpeedPlus).

### Verdict

Fixed: on a computer whose USB 3 ports run at 5 Gbit/s at most a
SuperSpeedPlus device at SuperSpeed is not counted, and the fallen-back
path still is, with "SuperSpeed (5 Gbit/s)". The option works on XP as
the owner set it and does nothing on 98 SE. `3.2.0.0` users are
affected (history entry in `releases/history.md`).

Evidence: `E:\xhci98-vm\p44\p44-fast\` (`STATE.md`, `pids.txt`,
`hashes.txt`; `shots\`: `xp-l1-report2.png`, `xp-l1-oldtool.png`,
`xp-l2-a.png`, `xp-l2-list.png`, `xp-l3off.png`, `xp-l3on.png`,
`xp-l3on-list.png`, `xp-menu.png`, `p98-l1-report.png`, `p98-l2-report.png`,
`z-p98-l1b.png`, `z-p98-l2-report.png`, `p98-l3-report.png`,
`p98-l3-ctrl2.png`; `ev\`: the simulator's logs).

## 44.82 - XHCICLN dropped (2026-10-28)

The owner, 2026-10-28: "Xhcicln tool can be dropped." From `3.3.0.0`
the download carries no `tools\xhcicln\`, and the tree no longer holds
its source. The old `XHCI98\` devnodes an update from `2.3.x` or earlier
left stay. After the restart they are not present and do no harm
(design record 13 section 10.13.6).
On Windows 2000 and later the installer's uninstall still removes them
with its set (record 23 section 9.2). The downloads from `2.4.0.0` to
`3.2.0.0` still carry the tool.

### The change

- `xhcicln\` deleted. `build-driver.cmd` no longer builds it or runs its
  self-test. Its x86 tool step rebuilds XHCITR and XHCIPERF.
- `make-release.ps1`: no `-CleanToolDir` or `-SkipCleanTool`, no
  staging, no readme or notice, no readme contents entry. `xhcicln` is
  in `$retiredToolNames` at `3.3.0.0`, as `xhcisnap` is at `3.1.0.0`, so
  the upload assembly still reads a published `2.4.0.0` to `3.2.0.0`
  tree. `test-package.ps1` holds both retired tools the same way. The
  readme template's update section names no tool.
- `package-common.ps1` and `check-readme-text.ps1`: four tool readmes.
- The installer's last line after an update from `2.3.x` or earlier was
  "N old XHCI98\ entries from 2.3.x remain; harmless - tools\xhcicln\
  removes them if you want." It is now "N old XHCI98\ entries from 2.3.x
  remain. Nothing needs them removed." The smaller change: the line
  still tells the user the entries are there. It does not say they are
  not present: it can show before the restart, while 2.3.x still runs.
  It still changes no exit code (record 23 section 5.11).
- The documents: design records 13, 21, 23, 26, 29 and 33 mark the
  tool's parts historical. The README, the release notes, the release
  acceptance test, `source-files.md`, `legal-provenance.md`,
  `build-and-test.md` and `releases\README.md` changed to match.

### Readings

Both architectures' gated builds through `build-driver.cmd` passed
(`all`, amd64 then x86), the host tests with them (`test_inst` 7062
checks, 0 failures) and the packager's self-tests. `xhciinst\build.cmd`
built the installer with the new line. `make-release.ps1 -CheckOnly
-SkipQualtool` ran to its end. It refused three things this worktree
lacks: the two driver packs under `scripts\local\`, and a version not
yet bumped (`releases\3.2.0.0` exists). XHCIQUAL was not built here. No
guest leg: the installer's change is one line of text, and nothing else
that runs on a target changed.

## 44.76 - Windows 2000: the mark service's program not replaced by the update over 3.2.0.0 (2026-10-28)

A task of Phase 44 (the coordinator, 2026-10-28), from the pre-cut
matrix's F1 (`E:\xhci98-vm\m33\N4\RESULT.md`, legs `w2b` and `w2h`): on
Windows 2000, both HALs, the update over `3.2.0.0` with the Thunderbolt
3 / USB4 support printed "Stopping the service xhcimark ... stopped."
then "XHCIMARK.EXE could not be put into C:\WINNT\system32 (error 5).",
kept the old program running and ended with exit 2; a second run after a
restart replaced it. XP and 7 did not show it.

### The cause

`3.2.0.0`'s service (`xhcimark\xhcimark.c` at `v3.2.0.0`,
`mk_service_main`) reports `SERVICE_STOPPED` as its last act and returns;
its process then returns from `StartServiceCtrlDispatcher`, leaves `main`
and ends. The service controller answers `SERVICE_STOPPED` from that
report, so the state reads stopped before the process has ended and let
go of its image [read: the source]. The `3.2.0.0`-era installer
(`mk_install` in `xhciinst\inst_eng.c` at `2caf2d51`) polled the state
every 250 ms and moved its copy over `XHCIMARK.EXE` at the first
"stopped"; a file still mapped as a running image refuses that move with
error 5 [inf]. Whether it fails is a race: on a single-processor TCG
guest it failed in the matrix's two legs and in one of three control
runs here, all from the same saved state (below) [read]. The released
`3.2.0.0` program cannot change, and `3.3.0.0`'s own service ends the
same way, so the installer is the side to fix.

### The change

- `mk_stop` opens the service's process (its id from
  `QueryServiceStatusEx`, looked up at run time, present from Windows
  2000) before it sends the stop, and after "stopped" waits up to 30 s
  for the process to end; the log says how long it took.
- `mk_put` moves the copy over the old program and, on error 5 or 32,
  tries again every 500 ms for 10 s. A registered service whose program
  is still in use then gets the copy queued for the restart
  (`MOVEFILE_DELAY_UNTIL_REBOOT`, from `XHCIMARK.TMP`), the earlier
  program started again until then, and the run asks for the restart
  instead of failing. `inst_mk_put_next` is the decision, with vectors in
  `test\test_inst.c`.
- A queued `XHCIMARK.TMP` is deleted at every later install run (also
  when system32 already holds this package's copy) and by the removal, so
  no restart puts a superseded or removed program back; one that cannot
  be deleted leaves the service as it is and fails the step.
- XP to 10 run the same code, and the same report-then-exit order
  applies there. The pre-cut matrix's update legs on XP and 7 x86
  replaced the program with the old installer; how long the process takes
  to end was not read on any of them.

Commits `becd7906`, `7eee64ce`, `08b99744`, `d2523f49`, `114c8010`,
reviewed and revised.

### The vehicle

Development host A; q35, the emulator copy and the ASM4242 layout of
the pre-cut matrix (`bin\boardint`, `deca3fcb...`, its `launch.ps1`
copied, ports 49200 and 49201), no enclosure. Fresh overlays of the
clean q35 bases `w2k` (Standard PC HAL, `7fc9714e...`) and `w2kacpi`
(ACPI HAL, `9a72a82e...`), one processor, 512 MB.

- `3.2.0.0` as published (`xhci98-3.2.0.0.zip` `336be62c...`;
  `XHCIINST.EXE` `c056cea7...`, `XHCIMARK.EXE` `e017b64a...`, 114,688
  bytes), installed `INSTALL /console /yes /nopause /thunderbolt
  /stor98`. On the Standard PC overlays the first run met the pending
  Found New Hardware windows (exit 4) and was run again after they were
  walked (Skip); then exit 2 (2000's first filter install), the mark
  service installed and running. Shut down, then each overlay copied
  (`disk-after32.qcow2`): every update row below starts from that copy,
  cold started.
- Cut S: this branch at `08b99744` with the pre-cut matrix's version
  stamp (`f9cf50c8`) on a local branch, the gated x86 build
  (`build-driver.cmd all`, EXIT 0) and `make-release.ps1 -Arch x86
  -Flavor release -TestLabel "44.76 legs"` (`xhci98-3.3.0.0.zip`
  `3a4fbf5b...`; `XHCIINST.EXE` `b5a1fb0d...`, `XHCIMARK.EXE`
  `991a5cd7...`, 139,264 bytes). Cut T and cut U: cut S with
  `XHCIINST.EXE` of `d2523f49` (`fb700d5d...`) and of `114c8010`
  (`30528e01...`), built the same way.
- Control: the pre-cut matrix's cut (`xhci98-3.3.0.0.zip` `4ee205cf...`,
  `XHCIINST.EXE` `9c962f83...`, the installer before this change).

### Readings

| Overlay | Package | Mark service step | Exit |
|---|---|---|---|
| `w2b2` (Standard PC) | cut S | "Stopping the service xhcimark ... stopped." "... is updated and running"; log: "its process ended 0 ms after it read stopped" | 2 |
| `w2h2` (ACPI) | cut S | the same; 0 ms | 2 |
| `w2h2` | control | updated and running | 2 |
| `w2h2` | control, again | updated and running | 2 |
| `w2b2` | control | "XHCIMARK.EXE could not be put into C:\WINNT\system32 (error 5)."; the 3.2.0.0 program started again (its log: "stopped" and "started: xhcimark 3.2.0.0" in the same second) | 2 |
| `w2b2` | cut T | updated and running; 0 ms, no retry logged | 2 |
| `w2h2` | cut T | the same | 2 |
| `w2h2`, `XHCIMARK.EXE` held open (`more < XHCIMARK.EXE` in a window) | cut T | 10 ms; "still in use after 10015 ms (error 5)"; queued; "is still in use: the package's copy replaces it at the restart" | 2 |
| the same, cold started | - | "started: xhcimark 3.3.0.0"; `XHCIMARK.EXE` 139,264 bytes; no `XHCIMARK.TMP` | - |
| `w2h2`, held open | cut U | 0 ms; "still in use after 10014 ms (error 5)"; the message reads "... replaces it at the restart. Until then the service xhcimark runs the earlier copy:" | 2 |
| the same, cold started | - | "started: xhcimark 3.3.0.0" | - |

In every update row but the failing control, exit 2 is the q35 board's
own `qemu-xhci` left at Code 31 after its update on 2000 ("did not end
as installed"), as in the pre-cut matrix's runs (its note N4, and 44.45
(continued, 3)), not the mark service's; the failing control has both. An earlier pair of overlays (`w2b`, `w2h`) updated
with a cut of `08b99744` without the stamp: its drivers read "current",
so no driver was updated, and the mark service was updated and running
(exit 3, 0 ms) on both; that pair did not reproduce the matrix's run.

### Verdict

Fixed in the installer. With the change, every update over `3.2.0.0` on
2000 read here replaced the mark service's program (four runs on the
3.3.0.0 update path, two on the shorter one), and a program still in use
is replaced at the restart instead of failing the run. In every run with
the change the wait for the process returned within 0 or 10 ms, and in
the two cut T runs with the program not held open no retry was logged
(cut S does not log a retry that succeeds), so these runs do not
separate the wait from chance; the control's error 5 on the
same saved state shows the race the wait and the retry close [inf].

Evidence: `E:\xhci98-vm\p44\p44-mark\` (`STATE.md`, `pids.txt`;
`w2b`, `w2h`, `w2b2`, `w2h2`: `serial-*.log` with the install logs and
`XHCIMARK.LOG`, `cmdline-*.txt`; `shots\`).

## 44.81 - Windows 2000: the board's own controller at Code 31 after the update on the emulated ASM4242 board (2026-10-28)

A task of Phase 44 (the coordinator, 2026-10-28), read first. On Windows
2000, both HALs, on q35 with the emulator's ASM4242 layout (the
Thunderbolt and USB4 legs' layout), the update over `3.2.0.0` and
44.45 (continued, 3)'s first install ended their first run with exit 2:
the board's own QEMU xHCI (`qemu-xhci`, on `xhci98.sys`) was left at
problem 31 ("did not end as installed") and the run said to restart and
run it again; after a cold start the second run was clean ("44.76", the
pre-cut matrix's N4 `w2b` and `w2h`). On the plain 2000 q35 base (the
pre-cut matrix's N1) the update ended with exit 3. Find out why, say
whether a real 2000 user with the Thunderbolt support meets it, and fix
it if it is real and small.

### The vehicle

Development host A; q35, TCG, one processor (`pentium3`), 512 MB, the
emulator copy of the pre-cut matrix (`bin\boardint`, `deca3fcb...`). One
guest: a copy of 44.76's saved state `w2b2\disk-after32.qcow2`
(`3.2.0.0` as published, installed with `/thunderbolt /stor98` on a
fresh overlay of the clean Standard PC HAL base `q35-bases\w2k`; the
copy's SHA-256 `6115a1ad...`), cold started for every boot; after one
boot whose overlay was spoilt (a wrong pattern disabled most devices) a
fresh copy of the same file. Layouts: the ASM4242 layout of 44.45; plain
(no ASM4242 model: the `qemu-xhci` behind root port 00:1c.0 alone);
plain with a second `qemu-xhci` on the root bus (`-device
qemu-xhci,id=xh2,p2=4,p3=4,bus=pcie.0,addr=0x6`); plain with an ICH9
UHCI or an ICH9 EHCI function (Windows' own drivers).

Read with a scratch console tool (not tracked; its source is in the
evidence folder, built with MSVC 6.0 and run in the guest only): each
devnode's status and problem (`CM_Get_DevNode_Status`), a restart,
disable or enable (`DIF_PROPERTYCHANGE`), an uninstall (`DIF_REMOVE`), a
bridge's `UpperFilters` cleared, the names under `\Device`, `\??` and
`\Driver`, and the loaded kernel modules (`NtQuerySystemInformation`).
Two guest memory dumps (QMP `dump-guest-memory`) were read offline.
Drivers: `3.2.0.0` as published (`xhci98.sys` `dd230d50...`); 44.76's
cut S (`3.3.0.0`, `xhci98.sys` `321ddbc8...`), installed by `INSTALL
/console /yes /nopause` over `3.2.0.0` on the two-controller layout
(exit 3, both controllers started) and read after a cold start; and the
change below, first as a DDK build of `xhci98.sys` alone (not gated,
`744e217d...`), then the release `xhci98.sys` of the gated x86 build of
`22654b48` (`build-driver.cmd all`, EXIT 0; `145202d7...`), each copied
over `C:\WINNT\system32\drivers\xhci98.sys` in the guest and read after a
cold start.

### Readings

| Boot | Layout, driver | Steps | Result |
|---|---|---|---|
| b | board, 3.2.0.0 | restart of root port 00:1c.0 (the `qemu-xhci`'s parent) | the `qemu-xhci` started again |
| b | the same | restart of root port 00:03.0 (the ASM4242 model's parent), then of 00:1c.0 | the ASM4242 model's upstream port `1B21:2421` at **Code 12**, its whole tree gone; then the `qemu-xhci` at **Code 31**; a restart of the `qemu-xhci` itself: Code 31, "restart needed" |
| c | board, 3.2.0.0 | restart of the `qemu-xhci`; restart of the ASM4242's downstream port 2 (`1B21:2423`, the parent of the chip's own xHCI) | both came back started |
| c | the same | restart of 00:03.0 (the ASM4242's tree gone), then of the `qemu-xhci` itself | **Code 31**, restart needed |
| d | board, 3.2.0.0 | the ASM4242's xHCI disabled, then the `qemu-xhci` restarted | **Code 31** |
| e | board, 3.2.0.0 | the ASM4242's xHCI enabled again; `UpperFilters` cleared on 00:1c.0 and on downstream port 2 and both restarted (no child filter under either xHCI from then on); the ASM4242's xHCI disabled; the `qemu-xhci` restarted | **Code 31**, restart needed; `xhci98.sys` still in the module list |
| f | board, 3.2.0.0, the filters still cleared, a cold start with the ASM4242's xHCI disabled since the boot | the `qemu-xhci` restarted | started |
| f | the same | the ASM4242's xHCI enabled and disabled again, then the `qemu-xhci` restarted | **Code 31**, restart needed |
| h | plain + second `qemu-xhci`, 3.2.0.0 | the second disabled, the first restarted | **Code 31** |
| i | the same | the same, with the names read | before: `\Device\XHCI98HC1`, `HC2`, `\??\HCD0`, `HCD1`, `\Driver\xhci98`; after: no name of the driver left, `\Driver\xhci98` gone, **`xhci98.sys` still loaded** |
| j | the same | the second uninstalled (`DIF_REMOVE`) instead of disabled, then the first restarted | **Code 31** |
| k | plain, one controller, 3.2.0.0 | disable, then enable | started; between them `xhci98.sys` unloaded, then loaded at another base |
| l, m | plain + second, cut S (`3.3.0.0`) | the update (exit 3); a cold start; the second disabled, the first restarted | **Code 31**, as with `3.2.0.0` |
| n | plain, one controller, cut S | disabled; `\??\HCD0` made by a program (`DefineDosDevice` to `\Device\Null`); enabled (it took `HCD1`); disabled; enabled | started each time; `xhci98.sys` unloaded in between |
| p | plain + second, the change (DDK build) | the second enabled after the first (it took `HCD1` without trying `HCD0`), disabled, the first restarted | **started**; `xhci98.sys` unloaded and loaded at another base |
| q | plain + ICH9 UHCI, the change | Windows' `uhcd` started on the UHCI | no `HCD` link of its own (`\??\HCD0` the only one, `xhci98`'s) |
| r | plain + ICH9 EHCI, the change | Windows' `usbehci` started (`\Device\USBFDO-0`); `xhci98` held `\??\HCD0`; the one `qemu-xhci` restarted | **Code 31**, restart needed |
| t | plain + second, the gated build, both controllers started at the boot (the second enabled before the shut down) | the second disabled, the first restarted | **started**; `xhci98.sys` unloaded and loaded at another base |

The dumps. After m's failure: the driver object of `xhci98` alive with
no device object on its list, its flags 13h (`DRVO_UNLOAD_INVOKED` among
them), one pointer reference; one of the two controller FDOs (the one at
BAR `FEA10000h`) deleted but alive with one pointer reference (extension
flags `C0000012h`); and an unnamed symbolic link object (pool tag `Sym`,
its target string freed, one pointer reference, no handle) whose
`LinkTargetObject` is that FDO. No pointer to the link object was found
in memory. After r's failure the same picture with the one controller's
FDO.

### The cause

Two parts.

1. **Why the board's `qemu-xhci` is the driver's last controller when it
   is restarted (the emulator's layout).** `xhcicmf.inf` names
   `PCI\VEN_1B36&DEV_000C&CC_0604`, the emulator's q35 tunnel port, which
   is also the id of QEMU's ordinary PCI Express root port; on the ASM4242
   layout both root ports carry it, 00:03.0 above the ASM4242 model and
   00:1c.0 above the `qemu-xhci`, so the installer updates the filter on
   both (or adds it, at a first install). On 2000 the restart of 00:03.0
   leaves the ASM4242 model's upstream port at Code 12 until a restart
   (the update's "gone after its install" lines), which takes the
   ASM4242's own xHCI away; the restart of 00:1c.0 then restarts the
   `qemu-xhci` with no other controller of the driver left [read: b, c].
   The child filter is not involved [read: e, f].
2. **Why the last controller cannot come back (the driver, not the
   emulator).** Each controller makes `\DosDevices\HCD<n>` at its start,
   trying n from 0 until a create succeeds (`hcd_door.c`,
   `HcdDoorControllerStart`). A second controller of the driver tried
   `HCD0`, which the first one held; the create failed with a name
   collision, and an unnamed symbolic link was left holding a reference
   on a controller's FDO [read: the dumps]. That FDO is never freed after
   its remove, so its reference on the driver object stays: when the last
   controller of the driver goes, the driver's unload runs but the image
   stays loaded, and the next start of any of its controllers fails, Code
   31 on 2000, until Windows restarts [read: f, i, m]. Which link keeps
   the reference, the one that held the name or the failed create, and so
   which controller's FDO it holds, is not read: the start order of m's
   boot was not read [inf: that a collision is what leaves the link rests
   on p and n below, not on a reading of the object manager]. One
   controller alone never collides with itself, which is why N1's update
   and every single-controller disable and enable here pass [read: k, n].

### Real or emulator-only

The exit 2 of those runs is the emulator's: it needs the root port above
the board's own xHCI to carry the filter, and a real machine's root ports
have other ids that `xhcicmf.inf` does not name. On a real machine with an ASM4242
the ASM4242's own xHCI sits behind its downstream port `1B21:2423`, which
the filter does serve, but a restart there keeps the driver loaded while
another controller of the driver runs [inf, from c on the emulator's
model].

The leak is not the emulator's: it needs only two controllers on
`xhci98.sys`, read here with two QEMU xHCIs on q35 (h to m). On a real
2000 machine with two such controllers (a real machine with an ASM4242
has the chipset's own and the ASM4242's) the restart of the last controller left
would then fail until Windows restarts: the update after the user
disabled one of them, or a disable and enable of each in turn [inf].
Recovery is the restart the run already asks for.

### The change

`22654b48`: a driver-global bitmap of the `HCD<n>` numbers this driver's
controllers hold, under a spin lock, set up in `DriverEntry`
(`HcdDoorInit`); a start claims the first number none of them holds,
gives it back when the create fails and claims the next; the remove
gives it back. A controller of the driver therefore never tries a number
another one holds; a number another driver holds is still tried past as
before. Reviewed and revised. Read with the change: p (the DDK build)
and t (the gated build) pass.

**What it does not cover [read: r].** With Windows' own EHCI driver
started beside one `xhci98` controller that held `\??\HCD0`, the restart
of that controller still failed with Code 31, and the dump showed the
same unnamed link holding its FDO. The create that collided there was
not read [inf: Windows' EHCI stack trying `HCD0` after this driver had
made it]. If that holds, a 2000 machine with an EHCI controller on
Windows' driver started after an xHCI on this one would still meet Code
31 at the update's restart of the controller, until the restart the run
asks for [inf: one VM reading, one start order]. Avoiding that needs `HCD<n>` numbers Windows' stack does not
reach first, or no create of a name that may be taken, which changes
what USBView and this package's tools find (design record 13 section
8.8): the coordinator's and the owner's call.

### Verdict

Cause found: the ASM4242 layout restarts the `qemu-xhci` as the driver's
last controller (the emulator's id on the root ports), and on 2000 a
second controller's create of a taken `\DosDevices\HCD<n>` leaves a link
holding a controller's FDO, so the image never unloads and the next
start fails. Fixed for two controllers of this driver (`22654b48`); the
case beside Windows' own EHCI driver is owed (above). Evidence:
`E:\xhci98-vm\p44\p44-c31\` (`STATE.md`, `pids.txt`, `w2b\serial-*.log`
per boot letter, `w2b\cmdline-*.txt`, `shots\`, `tools\` with the console
tool's source and the dump readers); the dumps
`E:\xhci98-scratch\p44-c31\m3.elf` and `r2.elf`.

### The proper fix (the coordinator and the owner, 2026-10-28, the cut held for it)

The owner asked for the reference to be found exactly and for a fix
that holds against a collision with anyone, in any start order.

#### Which object holds the reference

Read on the same guest family (`w2b`, the gated build of `22654b48`
copied in) and on dumps read offline with a pool walk of the symbolic
link objects (`E:\xhci98-scratch\p44-c31\an2\syms.py`).

| Boot | Layout | Steps | Result |
|---|---|---|---|
| r (dump `r2.elf`, read again) | plain + ICH9 EHCI | after the failed restart | the object pinning the FDO is this driver's own `HCD0` link after `IoDeleteSymbolicLink`: no name, flags `22h` (permanent cleared), one pointer reference, no handle, `LinkTargetObject` the FDO; Windows' EHCI (`\Device\USBFDO-0`) has no `HCD` link at all |
| u | plain, one controller, nothing else | `\??\HCD0` held by this driver (`-> \Device\XHCI98HC1`); a user-mode `NtCreateSymbolicLinkObject` of `\??\HCD0` to `\Device\Null` (`C0000035`, the name unchanged); the controller restarted | **Code 31**, restart needed; `\Driver\xhci98` gone, `xhci98.sys` still loaded |
| w | plain + two ICH9 EHCI | the names at the boot | `HCD0 -> \Device\USBFDO-0`, `HCD1 -> \Device\XHCI98HC1`, the second EHCI (`USBFDO-1`) with no `HCD` name |

So on 2000 a create that collides with an existing `\DosDevices` link
adds a pointer reference to the existing link, never released, and
every `\??` link to a device holds that device (`LinkTargetObject`; the
dump shows it on every such link) until the link is freed. The holder's
FDO and image stay; the collider's own failed create leaves nothing
(boot n above). In r the collider was Windows' EHCI stack: 2000's
`usbport.sys` takes the lowest free `\Device\USBFDO-<i>` and creates
`\DosDevices\HCD<i>` once, with no retry [static: `legal-provenance.md`
section 4, the 44.81 row; read at runtime in w].

XP SP3 and 7 SP1 x86 were read by a helper leg
(`E:\xhci98-vm\p44\p44-c31x\`, `STATE.md` and its serial logs) with a
`3.3.0.0` test build from before the fix (44.76's cut, `xhci98-3.3.0.0.zip`
`7e542fd2...`, the same `HCD<n>` names and collisions): one controller of
this driver beside an EHCI on Windows' driver, and two of this driver's,
each restarted and disabled in both orders. No Code 31, and the image
reloaded at a new base after each restart: the pin was not read there.
Whether the same collisions leave the reference on XP and 7 was not read
from memory. On both, an EHCI started while this driver held `HCD0` got no
`HCD` name at all, as on 2000. Vista, XP x64, 8.x and 10 were not read
for the leak.

#### What 2000's and XP's USB walk

`usbui.dll`'s `EnumerateAll` (the notification popups and the
control-panel walk, design record 13 section 8.1) opens `\\.\HCD0` to
`\\.\HCD9` on 2000 and XP SP3 and passes over a name that does not open
[static]. Vista and 7 enumerate the host controller interface (section
8.1, from before).

#### The options weighed

- High `HCD<n>` numbers on NT: usbport still meets them once it has
  that many controllers, and the walk above stops at 9.
- No `\DosDevices` name on NT, the interface only: the 2000 and XP
  pages would need an interface path through `SymbolicName`, which was
  never read, and the control-panel walk loses the controller.
- A name of this project's own, `\DosDevices\XHCI98HC<k>`: nobody else
  makes it. Taken (the owner), with the alias below so the 2000 and XP
  control-panel walk keeps the controller.

#### The alias, read on 2000 before it was built in

`w2i`: a fresh copy of `p44-mark\w2b2\disk-after32.qcow2` (3.2.0.0 on a
fresh overlay of `q35-bases\w2k`), plain q35, one `qemu-xhci`; a DDK
build of the change (`xhci98.sys` `77b437ea...`) copied in, then a cold
start.

| Boot | Steps | Result |
|---|---|---|
| c | the names | `\??\XHCI98HC0 -> \Device\XHCI98HC1`, `LinkTargetObject` the FDO; `\??\HCD0 -> \DosDevices\XHCI98HC0`, **`LinkTargetObject` NULL** (dump `c0`); `\\.\HCD0` and `\\.\XHCI98HC0` both open |
| c | a user-mode create of `\??\HCD0` (`C0000035`), then the controller restarted | **started**, no Code 31; `xhci98.sys` unloaded and loaded at another base (`BFE1F000h` to `BF25A000h`); both names made again |
| c | dump `c3` after the restart | the orphan is the same object as the alias before the collision (body `81FF4AD0h`): no name, flags `22h`, one pointer reference, `LinkTargetObject` NULL - it keeps nothing |

#### The fix

`bf7595c7` (`src\xhci_dname.c` and `hcd_door.c`; host suite
`test_dname`):

- 98 SE and ME: `\DosDevices\HCD<n>` at the first free `n`, as before.
- NT (2000 to 10): the door is `\DosDevices\XHCI98HC<k>`, from 0 up.
- 2000, XP and XP x64 (NT before 6.0): also `\DosDevices\HCD<m>`, whose
  target is the string `\DosDevices\XHCI98HC<k>`, from `HCD9` down. It
  meets usbport only when more than ten USB host controllers run at once.
- On NT every name is probed first (`IoGetDeviceObjectPointer`; 98's
  NTKERN has no `ZwOpenSymbolicLinkObject`) and passed over when anything
  answers. A failed probe or create gives the number back and tries the
  next, within 64 tries.
- Each name is deleted as made, once. Its number is given back only when
  the delete succeeded or the name was already gone.
- XHCITR: scans `XHCI98HC0` to `63` beside `HCD0` to `63` and the
  interface. `/c N` opens `XHCI98HC<N>` on NT and `HCD<N>` on 98 SE and
  ME, and the report's snapshot goes by that name.

What it costs: on 2000, XP and XP x64 a tool that scans `\\.\HCD0` to
`HCD9` (an old USBView) still finds the controller, through the alias. On
Vista and later such a tool no longer finds it; whether the WDK 7.1
USBView also walks the interface was not read. XHCITR `3.2.0.0` opens
`\\.\HCD<N>`: on 2000, XP and XP x64 it reaches the controller through
the alias (`/c m` for `HCD<m>`, usually 9), on Vista and later not at all. A program that
creates `\??\XHCI98HC<k>` while this driver holds it would still pin the
FDO; nothing does that but a deliberate test.

#### The legs on the built package (2026-10-28)

The x86 `release` flavour of the gated build of `c260abd1` (EXIT 0;
`xhci98.sys` `2b9c5a3d...`, the gated record's), staged as a TEST cut of
the x86 release flavour (`make-release -Arch x86 -Flavor release
-TestLabel`). The version strings still read `3.2.0.0`, so the installer
took the same version as current, and `/force` reinstalled it. Windows
2000 Professional SP4, Standard PC HAL, q35, TCG, one `pentium3`, 512 MB:
`w2L`, a fresh copy of `p44-mark\w2b2\disk-after32.qcow2` (`3.2.0.0`
installed with `/thunderbolt /stor98` on a fresh overlay of
`q35-bases\w2k`, SHA-256 `6115a1ad...`), cold started for every boot.

| Boot | Layout | Steps | Result |
|---|---|---|---|
| a | board (44.45's) | `3.2.0.0`'s names | `HCD0 -> \Device\XHCI98HC1`, `HCD1 -> \Device\XHCI98HC2`: `3.2.0.0`'s second controller tried `HCD0` first at this boot |
| a | the same | `INSTALL /console /yes /nopause` | exit 3, "Every device is current" (the same version) |
| a | the same | the same with `/thunderbolt /force` | exit 2: the `qemu-xhci` at problem 31, "did not end as installed", and the ASM4242 model's tree "gone after its install". The image `3.2.0.0` pinned at the boot could not unload, as in 44.76 |
| b | board, after a cold start | the names | `XHCI98HC0 -> \Device\XHCI98HC1`, `XHCI98HC1 -> \Device\XHCI98HC2`, `HCD9 -> \DosDevices\XHCI98HC0`, `HCD8 -> \DosDevices\XHCI98HC1`; each `\\.\` name opens and answers the driver key and the root hub's name |
| b | the same | `INSTALL /console /yes /nopause /thunderbolt /force` (the update over itself) | exit 2 for the emulator's ASM4242 tree alone ("gone after its install"; its upstream port at Code 12, part 1 above). **The `qemu-xhci` started**, the driver's last controller, and `xhci98.sys` loaded at another base (`BFD8C000h` to `BF1F6000h`) |
| c | plain + ICH9 EHCI + a USB stick on the `qemu-xhci` | the names | the EHCI holds `HCD0 -> \Device\USBFDO-0` (under `3.2.0.0` it had none, boot r); `HCD9 -> \DosDevices\XHCI98HC0` |
| c | the same | a program's create of `\??\HCD9` (`C0000035`), then the controller restarted | **started**; `xhci98.sys` at another base (`BFD8C000h` to `BF234000h`) |
| c | the same | the EHCI disabled; the controller restarted; the EHCI enabled | each started; the EHCI took `HCD0` again, and the alias stayed `HCD9` |
| c | the same | the controller disabled, then enabled | no `xhci98.sys` in the module list while disabled; started again at `BF1CD000h` |
| c | the same | `XHCITR /snapshot Tc12 /c 0` from the package's `tools\xhcitr` | exit 0; "opening `\\.\XHCI98HC0`", the dump complete |
| c | the same | the controller's property sheet, then the root hub's | the Advanced tab with its bandwidth list; the Power tab with the stick at 0 mA |
| c | the same | Safely Remove Hardware | the stick listed; Stop; "can now be safely removed" |
| c | the same | the USB popups | none raised: this driver reports nothing that makes `usbui.dll` show one. The walk's opens were read instead: `\\.\HCD9` opens and answers the driver key and the root hub's name, which is what `EnumerateAll` asks |
| f | plain + a second `qemu-xhci` (installed at boot e through the Found New Hardware wizard from the staged INF) | the names | `XHCI98HC0`, `XHCI98HC1`, `HCD9`, `HCD8` |
| f | the same | the second disabled; the first restarted, twice; the second enabled | each started, no Code 31; `xhci98.sys` at another base after the first restart (`BFE5A000h` to `BF256000h`) |

Boot d ended before its readings. The installer waited on a Found New
Hardware window that 2000 kept pending after it was cancelled (exit 4,
"Nothing was changed"). Boot e was spoilt: a pattern with an `&`
disabled the root bus's devices, all enabled again at once. Boot f read
the same steps cleanly.

So the fix takes effect from the first start of a driver that has it. An
update from `3.2.0.0` or earlier on a 2000 machine with two controllers
on this driver, or with Windows' EHCI started after it, may still end at
Code 31 and ask for the restart, once (boot a).

The other targets were read by helper legs on the same kit, reported to
the coordinator. Evidence: `E:\xhci98-vm\p44\p44-c31-hx\` for XP SP3 and
7 SP1 x86, `p44-c31-h9\` for 98 SE with NUSB 3.3 and for ME, and
`p44-c31-h8\` for the static reading of Vista to 10 and the 10 x86 leg.
On 98 SE and ME the door was `HCD0` as under `3.2.0.0`. XHCITR's snapshot
opened `\\.\HCD0`, and the pages, Unplug or Eject, and disable, enable,
remove and refresh ran with no restart prompt.

### Verdict (the proper fix)

The holder of the name keeps the reference. On 2000 a create that
collides with an existing `\DosDevices` link adds a reference to it, and
a link to a device holds that device. Fixed in `bf7595c7` (reviewed and
revised in `0f16a773`, `d54ec956` and `c260abd1`). On NT the door is
`\DosDevices\XHCI98HC<k>`, a name only this driver makes. On 2000, XP and
XP x64 an `HCD<m>` alias from `HCD9` down names that link, not the FDO.
98 SE and ME keep `HCD<n>`. Read on 2000 with the built package: the
program's collision, the EHCI in both orders, two controllers, the ASM4242
layout's update over itself, the snapshot, the pages and Safely Remove.
`docs/issues/20-w2k-hcd-link-collision-code-31.md` has the case.
Evidence: `E:\xhci98-vm\p44\p44-c31\` (`w2b\`, `w2i\` and `w2L\` with
each boot's serial log and command line; `shots\La*`, `Lb*`, `Lc*`,
`Le*`, `Lf*`), the dumps `E:\xhci98-scratch\p44-c31\r2.elf` and `c3.elf`,
the pool reader `an2\syms.py`, and the listings under `ui\` (the dumps, `syms.py` and the listings were deleted in the disk cleanup of 2026-10-28; the VM folders remain).

## 44-E.7, the install legs from the asset

Read after the cut on the release zips as published (`xhci98-3.3.0.0.zip`
`4bca2b5f...`, `-free` `d9dffa26...`, `-nonfree` `7fd232a2...`), in QEMU
on development host A, 2026-10-28 and 29, five matrix agents, each leg on
a fresh overlay of the q35 bases. `3.2.0.0` as published (`336be62c...`)
for the updates. The x64 legs on the signed `release-x64` (`A512DF45...`),
the zip's test certificate trusted in the guest and test-signing on.
Thunderbolt legs on the emulator's ASM4242 model with 44.45's board-like
layout and the ORICO enclosure on Stor98.

- **T.1 and T.2 on 8, 8.1 and 10 x64, and record 31 section 9.3 item
  10.** Pass. The internal drive never got the id: no driver on 8,
  `stornvme` on 8.1 and 10. On 8 the hot-plugged drive's compatible ids
  began with `PCI\XHCI98_TUNNEL&CC_010802` and Windows bound Stor98 by
  it within a second (`setupapi.dev.log` rank `0x00ff2000`), in the direct
  and the switch shape. On 8.1 `stornvme` bound first and the mark
  service's 8.1 arm moved the drive to Stor98. On 10 it stayed on
  `stornvme` until a second installer run (case 3b). A Device Manager
  disable and enable kept Stor98 on 7, 8, 8.1 and 10 x64 (item 10 (b)).
  The update over `3.2.0.0` on 8.1 x64 updated Stor98 in its second run
  and removed the earlier INF, and `/force` kept it bound (item 10 (c)).
  On 7 x64, where `3.2.0.0` had also put Stor98 on the internal drive,
  every run left Stor98 at `3.2.0.0` and said "Update Stor98 by hand", as
  the release notes say.
- **T.7, the update from `3.2.0.0`.** As designed on 98 SE, ME, 2000, 7
  x86, 7 x64 and 8.1 x86, with one finding on 2000 (below). The drive
  stayed bound and gained the id at the next enumeration. On 2000 and 7
  the first run stages nothing and the run after the restart updates
  Stor98 and removes `3.2.0.0`'s INF. On 8.1 one run did both, the
  internal drive staying on `stornvme`. On 2000 with the drive unplugged
  the run kept the earlier INF and said "Update Stor98 by hand" (record
  31's "kept while ... names it" line is not reached), and the next run
  with the drive present removed it. On 98 SE and ME the interactive run
  replaced the driver key's copy, a second copy of the same name and an
  orphaned copy, and left an older `xhci98.inf` copy alone. Issue 20's
  Code 31 came once in the first run of each 2000 update with the
  Thunderbolt support, and a restart cleared it.
- **Finding: the class key `3.2.0.0` left on 2000.** `3.2.0.0`'s class
  match made Windows write `CriticalDeviceDatabase\pci#cc_010802` with
  `Service` `stor98`. The update removes the INF but not that key, so an
  internal NVMe drive added after the update starts on Stor98 with no
  wizard, as under `3.2.0.0`. Seen on two fresh `3.2.0.0` installs. A
  fresh `3.3.0.0` install writes only the tunnel id's key (record 31
  section 6.4). Documented in the release notes; the fix is planned item
  STORCDDB.1 (owner, 2026-10-29).
- **x64 without Thunderbolt: XP x64, Vista x64, 7, 8, 8.1 and 10 x64.**
  Pass. Install to Code 0 in one run, a 16 MB copy to the stick and back
  clean, `xhcicm` demand start after each run and restart (44.32), and
  set back from boot start by the update over `3.2.0.0` on 7 and 10 x64.
  With test-signing off, Vista, 7 and 10 x64 reached the desktop with
  the filtered ports at Code 52 (39 on Vista); the Thunderbolt support
  was refused with nothing changed (44.12). The uninstall left only
  Microsoft's INFs. The `-nonfree` update on 7 x64 staged `rtl815x` once,
  trusted usb-wdm-free's certificate and removed it at the uninstall. On
  XP x64 the controller is `XHCI98HC0` with `HCD9` naming it, both open.
  Under OVMF with no platform key Windows writes no Secure Boot state and
  the installer says nothing about it, as designed.
- **x64 Thunderbolt: XP x64, Vista x64 and 7 x64.** Pass. The install
  mounted the drive on Stor98 with a clean `fc /b`. Hot plug, an unplug
  during a copy, the USB Tree's eject (the tunnel not confirmed closed,
  the documented limitation) and a hibernation with the drive all held,
  and the uninstall left no service. The first shutdown after the resume
  took 8 to 19 s. On XP x64 with the connection manager present the
  doors were `XHCI98HC0` and `XHCI98HC1` with `HCD9` and `HCD8`.
- **x86 regression after 44.81 and 44.82: 98 SE, ME, 2000, XP, 7, 8.1 and
  10.** Pass. Fresh install, update over `3.2.0.0` and uninstall, exit 3
  throughout. 98 SE and ME keep `HCD0`. On 2000 and XP Windows' EHCI
  keeps `HCD0` and two controllers get `HCD9` and `HCD8`; `XHCITR /c 0`
  and `/c 1`, the Advanced tab and Safely Remove work. On 7, 8.1 and 10
  no alias and Safely Remove still lists the controller. No XHCICLN in
  any zip; on XP an old `2.3.1.0` root hub entry was reported by the
  update and removed by the uninstall. On 98 SE the `-free` window's page
  offered `uac2` and `usbmidi` only.

Left for the hardware bench (planned items DUALMODE.1 to U4HW.1): the same
legs on real Thunderbolt and USB4 hosts, Secure Boot on and off on real
firmware, XP's popups with a real EHCI, and the uac2 and usbmidi devices
on 98 SE. Evidence: `E:\xhci98-vm\m33\` `E1`, `E2`, `X1`, `X2` and `R1`,
each with its `RESULT.md`, serial logs and screenshots.
