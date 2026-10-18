<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright (c) 2026, Yeo Kheng Meng -->

# The rename from nvme98 to Stor98

The driver was developed as **nvme98** and is released as **Stor98** (display name), with
`stor98` as its file, service and key name. NVM Express, Inc.'s trademark guidelines bar
"NVMe" in product names and reserve the marks to members; the maintainer renamed the
project before its first public release (2026-10-16). "NVMe" stays only where it says
which devices the driver is for ("Stor98 Storage Controller for NVMe SSDs").

NVM Express and NVMe are trademarks of NVM Express, Inc.; Stor98 is independent and not
endorsed by NVM Express or Microsoft.

nvme98 was never released; no migration. The first release is Stor98 0.3.0.0 (there was
no 0.2.0.0; the integrated 0.2 work ships in 0.3.0.0).

## Mapping

| What | nvme98 | Stor98 |
|---|---|---|
| Windows 98 SE / ME image | `nvme98.mpd` | `stor98.mpd` |
| Windows 2000-7 image (x86, x64) | `nvme98.sys` | `stor98.sys` |
| Symbols | `nvme98.pdb` | `stor98.pdb` |
| Shipped INF (`x86\`, `x64\`) | `nvme98.inf` | `stor98.inf` |
| INF sources | `inf/nvme98-x86.inf`, `inf/nvme98-x64.inf` | `inf/stor98-x86.inf`, `inf/stor98-x64.inf` |
| INF sections and strings keys | `NVMe98_*`, `%NVMe98.DeviceDesc%` | `Stor98_*`, `%Stor98.DeviceDesc%` |
| NT service (AddService, ServiceBinary, event log source) | `nvme98` | `stor98` |
| 9x `PortDriver` | `nvme98.mpd` | `stor98.mpd` |
| INF `Provider` | `"nvme98"` | `"Yeo Kheng Meng"` |
| INF `Mfg` | `"Standard NVM Express Controller"` | `"Stor98 Project"` |
| INF device description | `"NVM Express Controller (nvme98)"` | `"Stor98 Storage Controller for NVMe SSDs"` |
| Service display name | `"nvme98 NVMe SCSIport miniport"` | `"Stor98 Storage Controller Driver (stor98)"` |
| INF disk name | `"nvme98 Installation Disk"` | `"Stor98 Storage Controller Disk"` |
| Version resource ProductName / CompanyName | `nvme98` / `nvme98` | `Stor98` / `Yeo Kheng Meng` |
| Version resource FileDescription | `nvme98 NVMe SCSI(port) miniport ...` | `Stor98 SCSI(port) miniport for NVMe SSDs (...)` |
| Version header | `src/nvme98ver.h`, `NVME98_VER_*` | `src/stor98ver.h`, `STOR98_VER_*` |
| CM interface header | `include/nvme98cm.h` | `include/stor98cm.h` (see below) |
| 9x settings and CM locator key | `HKLM\System\CurrentControlSet\Services\nvme98` | `...\Services\stor98` |
| 9x pool accounting | `HKLM\Software\nvme98\Pool` (`AcctRef`, `AcctBoot`) | `HKLM\Software\stor98\Pool` |
| 9x test switches | `HKLM\Software\nvme98\Q9x`, `...\Cm9x` | `HKLM\Software\stor98\Q9x`, `...\Cm9x` |
| 9x current-boot proof | `HKEY_DYN_DATA\Nvme98LinkState` | `HKEY_DYN_DATA\Stor98LinkState` |
| NT callback object | `\Callback\Nvme98LinkState` | `\Callback\Stor98LinkState` |
| Build switches | `NVME98_TARGET`, `NVME98_DBGPORT`, `NVME98_TESTDEF`, `NVME98_DMA_POOL`, ... | `STOR98_*` |
| Test-signing password variable | `NVME98_TESTSIGN_PW` | `STOR98_TESTSIGN_PW` |
| Test certificate | `CN=nvme98 project test signing (not trusted)`, `nvme98-test.{pfx,cer}` | `CN=stor98 project test signing (not trusted)`, `stor98-test.{pfx,cer}` |
| Repository | `github.com/yeokm1/nvme98` | [github.com/yeokm1/stor98](https://github.com/yeokm1/stor98) (a new repository; no redirect) |

The driver, INF and symbol file names are 8.3 (`stor98.inf`, `.mpd`, `.sys`, `.pdb`); the
release's other files (`stor98-test.cer`, `SHA256SUMS.txt`, ...) are not installed by
Windows 9x and need not be.

**Kept as they are:** the upstream names and copyright lines (nvme2k by Dominik Behr,
nvme9x/nvment by SweetLow) and `third_party/`; `LICENSE` (its copyright line still reads
"(nvme98)"; see "For the maintainer"); the dated records `docs/gates/`, `docs/reviews/`, `docs/INTEGRATION-0.2.md`;
the test host's VM folder (named for the former project); the debug-port trace prefixes `n98`, `cm9x`,
`ctl` (test-only tags that every recorded log and `tests/cm9x/q9log.py` match); the device
extension magic `NE98` and pool tags (internal values, not names).

## How it was done

1. `scripts/rename-to-stor98.py`, a deterministic rewrite with the reviewed tables at its
   top: four `git mv` renames (`PATH_MAP`), excluded paths (`EXCLUDE`), kept spans
   (`KEEP_PATTERNS`), the `'N98L'` value (`LITERAL_RULES`), case-preserving
   `NVME98`/`NVMe98`/`Nvme98`/`nvme98` replacements (`CASE_RULES`; every spelling has the
   same length, so string lengths written in code stay right) and whole-line rules for the
   INF `[Strings]` and the version resource (`LINE_RULES`).

   ```
   python scripts/rename-to-stor98.py            # dry run: the plan
   python scripts/rename-to-stor98.py --apply    # git mv + rewrite, then --check
   python scripts/rename-to-stor98.py --check    # verify a tree
   ```

   `--apply` wants a clean tree and runs once: it refuses a tree that already passes
   `--check`. `--check` fails on a path or text that
   still has the old name outside the kept spans, an unmapped path, an INF or version line
   that does not hold its expected value, or a shipped name that is not 8.3. It passes on
   the final tree too: the deliberate mentions of the former name and of the former
   signature are kept spans, so a dry run on the final tree rewrites nothing.
2. Hand-written commits for what a script cannot decide: the interface version, the
   INQUIRY identity, the test harness paths, the version number, history and notices.

## The CM interface (`include/stor98cm.h`): change list for xhci98

For a CM implementer. Copy `include/stor98cm.h` unchanged in place of `nvme98cm.h`.

| | nvme98 (`nvme98cm.h`) | Stor98 (`stor98cm.h`) |
|---|---|---|
| (a) table and locator signature | `'N98L'` = `0x4C38394E` (`NVME98LS_SIGNATURE`) | `'S98L'` = `0x4C383953` (`STOR98LS_SIGNATURE`); also `STOR98LS_QUERY` on NT |
| (a) IOCTL signature (interface 1, diagnostic) | `"NVME98CM"` | `"STOR98CM"` (8 bytes, no NUL) |
| (b) table version `..LS_VERSION` | 1 | **4** |
| (b) 9x locator version `..LS9X_LOCATOR_VERSION` | 1 | **4** |
| (b) IOCTL message version `..CM_VERSION` | 1 | **4** |
| (c) NT callback object | `\Callback\Nvme98LinkState` | `\Callback\Stor98LinkState` |
| (c) 9x locator | `HKLM\System\CurrentControlSet\Services\nvme98`, value `LinkState` | `HKLM\System\CurrentControlSet\Services\stor98`, value `LinkState` |
| (c) 9x current-boot proof | `HKEY_DYN_DATA\Nvme98LinkState` (`BootCookie`, `TableLinear`) | `HKEY_DYN_DATA\Stor98LinkState` (same values) |
| identifiers | `NVME98LS_*`, `NVME98LS9X_*`, `NVME98RM_*`, `NVME98CM_*`, `NVME98_CASSERT` | `STOR98LS_*`, `STOR98LS9X_*`, `STOR98RM_*`, `STOR98CM_*`, `STOR98_CASSERT` |

Layouts, sizes, offsets, flags, outcome values, bounds and rules R1-R12 are unchanged.

**(b) Why 4.** The nvme98 header's version fields were all 1; "interface 1/2/3" are the
names of its three variants (IOCTL, NT table, 9x table), not version numbers. The
maintainer asked for a version above 3 so that no number of the old interface, including
the "3" of interface 3, can be taken for the new one: every Stor98 version field is 4.
**No old-name compatibility in either direction:** Stor98 neither reads nor writes the
nvme98 objects and keys, and an nvme98-era receiver never finds a Stor98 CM. A mixed pair
is R2's "no channel" case (the MD-1 gone deadline applies; the CM never reports a clean
eject). A CM may publish both namespaces in one boot if it must serve both, as two
independent tables.

**(d) Migration: nothing from xhci98's installer.** nvme98 was never released; no
migration. Stor98 needs no help from xhci98's installer; the only installer change is the
file and INF names below.

**Bundling (xhci98's release scripts and installer):**

- Copy a complete Stor98 release folder: `x86\stor98.inf`, `x86\stor98.mpd`,
  `x86\stor98.sys` into `release-x86`; `x64\stor98.inf`, `x64\stor98.sys` (and
  `x64\stor98-test.cer` while test-signed) into `release-x64`; `LICENSE` and `NOTICE.TXT`
  with them. Paths in xhci98 that name `nvme98.*` change to `stor98.*`.
- The installer's skip of the bundled driver: match `stor98.inf` / `stor98.mpd` /
  `stor98.sys` and the NT service name `stor98`; the INF `Provider` is now
  `"Yeo Kheng Meng"` and `Mfg` `"Stor98 Project"`, and the device description
  `"Stor98 Storage Controller for NVMe SSDs"`.
- `sc config` / `sc query` and similar use the service name `stor98`.
- The hardware ID the INF matches is unchanged: `PCI\CC_010802`.
- Links to the project: `https://github.com/yeokm1/stor98` (new repository; the old URL
  does not redirect).

## Disk identity change (0.3.0.0)

The disk's SCSI INQUIRY vendor and product identification come from the NVMe model number
(`src/inqid.c`, tests in `tests/unit/inqid_ut.c`). Up to 0.1.x the first 8 characters were
the vendor and the next 16 the product, so QEMU's "QEMU NVMe Ctrl" became `QEMU NVM` +
`e Ctrl`. From 0.3.0.0 a first word of 1 to 8 characters followed by more text is the
vendor and the rest the product (`QEMU` + `NVMe Ctrl`; "Samsung SSD 970 EVO Plus 1TB" gives
`Samsung` + `SSD 970 EVO Plus`); otherwise the vendor is the generic `NVMe` (as in the NVM
Express SCSI translation reference) and the product the model number from its start.

**Hardware-ID change.** Windows builds the disk's hardware and compatible IDs from these
fields (NT: `SCSI\Disk<vendor><product><revision>`, for example
`SCSI\DiskQEMU____NVMe_Ctrl_______<revision>`; 9x: the IOS/ESDI disk devnode under
`Enum\SCSI\`). A disk seen by an earlier build therefore appears once as a new disk device
after updating; the old disk devnode stays as a non-present device. The data, the volume
and (on NT) the mount manager's drive letter, which follows the disk signature, are not
affected. Tools or scripts that match the disk's hardware ID must use the new strings.

**0.3.0.1: underscore.** An underscore ends the first word too (whichever of space or `_`
comes first; the spaces and underscores after the word are skipped). xhci98 met an SK hynix
drive whose model number is "SKHynix_HFS256GD4..." (no space): 0.3.0.0 gave `NVMe` +
`SKHynix_HFS256GD`, cutting off the distinguishing part; 0.3.0.1 gives `SKHynix` +
`HFS256GD4...`. Two kinds of model get a new hardware ID, and so a new disk device, once
more: those with an underscore before their first space (also "WD_BLACK SN770 1TB", now
`WD` + `BLACK SN770 1TB`), and those whose first word is followed by spaces and underscores
mixed ("ACME _ Product", now `ACME` + `Product`, was `ACME` + `_ Product`). Every other model
is unchanged.

## Importing the history into the stor98 repository

The stor98 checkout (origin `git@github.com:yeokm1/stor98.git`) holds the
full nvme98 history (option A, maintainer 2026-10-16; the maintainer may squash later):

```
git fetch --no-tags <former checkout> refs/heads/main:refs/remotes/nvme98/main refs/heads/int-0.2:refs/remotes/nvme98/int-0.2
git update-ref refs/heads/main bd88af6        # stor98 main = nvme98 main
git worktree add -b stor98-0.3 <worktree folder> 97eed40   # int-0.2's final head
```

The rename and the 0.3.0.0 work are commits on `stor98-0.3`, merged into `main` locally.
Nothing is pushed; the maintainer pushes. `refs/remotes/nvme98/*` are local import refs
and can be deleted after the push.

## For the maintainer

- **Test certificate.** `-TestSignPfx` now requires the subject
  `CN=stor98 project test signing (not trusted)`. Either make a new key with
  `scripts\make-testcert.ps1` (writes `stor98-test.pfx` / `.cer` to
  `scripts\local\testsign\`), or keep the old key and sign a stage with
  `scripts\sign-x64.ps1 -Pfx <old .pfx>` (any certificate; it is staged as
  `x64\stor98-test.cer`). The password variable is now `STOR98_TESTSIGN_PW`. Test machines
  that imported the old certificate need the new one only if a new key is made.
- **LICENSE.** The rename left its copyright line `Copyright (c) 2026, Yeo Kheng Meng (nvme98)`
  alone (licence text); the maintainer changed the label to `(Stor98)` for
  0.3.0.1, with the upstream lines, conditions and disclaimer unchanged, and the version
  resource's LegalCopyright to match. Releases copy `LICENSE` as is.
- **GitHub.** Push `main` to the new `yeokm1/stor98`; nothing was pushed or changed on
  GitHub. The old `yeokm1/nvme98` repository gives no redirect.
- **xhci98** applies the change list above (its copy of the header, locator and proof key,
  callback name, bundle paths, installer skip).
