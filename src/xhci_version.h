/*
 * xhci_version.h - the package version and its release date, in one place
 * (roadmap task 14.1.10).
 *
 * **This file is the single editable source of the version.** Bump it here and
 * in the seventeen literals listed below, and nowhere else. Every file that
 * includes it takes its value from it, and the tree's own `#include` lines
 * are the list of those (a grep finds them all); by kind they are:
 *
 *   - the drivers' version resources and code: xhci98.sys (`src\xhci98.rc`,
 *     `hcd_entry.c`, `hcd_msi86.c`, and `src\xhci_verpk.h`, the packed
 *     form), xhciuas.sys (`src\uas`), xhcicm.sys (`src\cm`: the resource,
 *     `cm_entry.c`, `cm_nhi.c`, `cm_oscnt.c`), xhcihlpr.sys (`src\hlpr`),
 *     the two network drivers (`src\net\ndis5`, `src\net\ndis6`),
 *     XHCIPFW.SYS (`src\pfw\nt`) and the two VxDs' data blocks
 *     (`src\pfw\pfw_data.c`, `src\vsd\vsd_data.c`)
 *   - Stor98 (roadmap-usb4 task 42-A.30): `src\stor98\src\stor98ver.h`,
 *     which `nvme2k.rc` reads for stor98.mpd and both stor98.sys images
 *   - the tools: `xhciqual\qual.h`'s `TOOL_VERSION`; XHCITR
 *     (`xhcitr\tr_core.h`, `xhcitr.c`, `xhcitr.rc`, and `tr_snap.c`, the
 *     snapshot reader, whose macro keeps the retired XHCISNAP's name,
 *     `XHCISNAP_VERSION`); the installer (`xhciinst\inst_eng.c`, `xhciwiz.c`,
 *     `setup.rc`, `xhciwiz.rc`, and `di\xhcidi.c` with its test
 *     `di\test\ditest.c`); `xhcimark\xhcimark.c`; and XHCIPERF
 *     (`xhciperf\xhciperf.c`, `xhciperf.rc`)
 *
 * Seventeen cannot include it, because an INF and a manifest are not
 * compiled, and keep a literal:
 *
 *   - the `DriverVer` of all thirteen INFs, each checked against this file on
 *     every build (`build-driver.cmd` runs each gate): `src\xhci98.inf` and
 *     `src\xhci98-amd64.inf` (`scripts\inf-gate\check-inf.ps1`),
 *     `src\uas\xhciuas.inf` and `xhciuas-amd64.inf` (`check-uas-inf.ps1`),
 *     `src\cm\xhcicm.inf`, `xhcicm-amd64.inf`, `xhcicmf.inf` and
 *     `xhcicmf-amd64.inf` (`check-cm-inf.ps1`), `src\net\xhcinet.inf` and
 *     `xhcinet-amd64.inf` (`check-net-inf.ps1`), and Stor98's
 *     `src\stor98\inf\stor98-9x.inf`, `stor98-x86.inf` and `stor98-x64.inf`
 *     (`check-stor98-inf.ps1`)
 *   - the `assemblyIdentity` version of four manifests:
 *     `xhcitr\xhcitr.manifest` (`check-inf.ps1`, since 2026-10-18), and
 *     `xhciinst\setup.manifest`, `xhciinst\xhciwiz.manifest` and
 *     `xhciperf\xhciperf.manifest`, which no gate reads: bump them by hand
 *
 * **Every toolchain of the tree includes this**: the Win2000 DDK's `rc.exe`
 * and MSVC 6.0's `cl.exe` (the 32-bit drivers' resources and code, the tools,
 * the host test suite), WDK 7.1's `cl.exe` and `rc.exe` (the amd64 builds,
 * xhcihlpr.sys, the NDIS 6 driver), Open Watcom's `wcc386` (the
 * DOS qualifier and the two VxDs) and its 16-bit `wcc` (XHCIDI.EXE). So it
 * must stay what all of them accept without argument - `#define` lines and
 * comments, no types, no declarations, no pragmas, and nothing that needs an
 * include of its own.
 *
 * **Why both forms of the number are spelled out.** `FILEVERSION` and
 * `PRODUCTVERSION` take four comma-separated integers and cannot be given a
 * string; every other consumer wants the dotted string. Deriving one from the
 * other needs preprocessor stringizing that this era's `rc.exe` does not handle
 * reliably, so the two are written out and `check-inf.ps1` proves they agree
 * rather than trusting them to. Two adjacent lines that a gate compares beat
 * one clever line that no toolchain agrees about.
 *
 * **The date is the release date, not a build date.** It is what
 * `src\xhci98.inf`'s `DriverVer` carries, and Windows 2000's setup engine
 * ranks equally matched candidate drivers by that date first and by the
 * version only when the dates tie: a package dated earlier than the installed
 * one is declined as not better whatever its version says, and one of the same
 * date wins only with a higher version. So set it to the day the release is
 * cut, never to a later day to win the ranking; a second release on one day
 * is told apart by its higher version. The `built <stamp>` lines that
 * `XHCIQUAL` and `XHCITR` print are their compilers' `__DATE__`/`__TIME__` and
 * are a different fact: they say *which build* between two cuts of one
 * version, which is exactly what a version cannot (task 13-L.4).
 *
 * **Since 2026-10-02 this is the successor HCD's version** (design record 13;
 * roadmap-hcd.md). The miniport, last released as 1.2.0.0, left the tree that
 * day and the HCD took over src\ and the name xhci98.sys. Until the cut it
 * carried 1.99.0.0, a development number that ranked above the installed
 * 1.2.0.0 and claimed nothing; task 32.3 set 2.0.0.0, the HCD's first release.
 *
 * See `docs\contributing\build-and-test.md`, "Versioning the driver", for what
 * each field surfaces as on each target, and for the gates that cover the
 * copies this file cannot reach.
 */

#ifndef XHCI_VERSION_H
#define XHCI_VERSION_H

/* The four-part package version, as four integers - what FILEVERSION and
 * PRODUCTVERSION take, and what the Windows shell sorts by. */
#define XHCI_VER_CSV            3,5,0,0

/* The same number as a string - the resources' two version strings, the DOS
 * qualifier's banner, and the snapshot reader's report header. Must agree with
 * XHCI_VER_CSV above; the INF gate refuses a build where it does not. */
#define XHCI_VER_STR            "3.5.0.0"

/* The release date, in the MM/DD/YYYY form `DriverVer` takes, zero-padded -
 * the INF gate refuses an unpadded one, because Windows 98's 16-bit parser is
 * the reason the padding rule exists. */
#define XHCI_DRIVERVER_DATE     "11/14/2026"

#endif /* XHCI_VERSION_H */
