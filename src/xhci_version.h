/*
 * xhci_version.h - the package version and its release date, in one place
 * (roadmap task 14.1.10).
 *
 * **This file is the single editable source of the version.** Bump it here and
 * nowhere else. Four other sites take their value from it by including it -
 * `src\xhci98.rc`'s four resource fields, `xhciqual\qual.h`'s `TOOL_VERSION`,
 * and `xhcisnap\xhcisnap.c`'s `XHCISNAP_VERSION` - and one cannot, because it
 * is not compiled: `src\xhci98.inf`'s `DriverVer`. That one keeps a literal and
 * is **checked** against this file by `scripts\inf-gate\check-inf.ps1`, which
 * every build runs.
 *
 * **Three toolchains include this**: the Win2000 DDK's `rc.exe` (the driver
 * resource), MSVC 6.0's `cl.exe` (the snapshot reader and the host test suite)
 * and Open Watcom's `wcc386` (the DOS qualifier). So it must stay what all
 * three accept without argument - `#define` lines and comments, no types, no
 * declarations, no pragmas, and nothing that needs an include of its own.
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
 * `src\xhci98.inf`'s `DriverVer` carries and what Windows 2000's setup engine
 * ranks a candidate driver by *first* - a package whose date does not exceed
 * the installed one is declined as not better, whatever its version says. Set
 * it to the day the release is cut. The `built <stamp>` lines that `XHCIQUAL`
 * and `XHCISNAP` print are their compilers' `__DATE__`/`__TIME__` and are a
 * different fact: they say *which build* between two cuts of one version, which
 * is exactly what a version cannot (task 13-L.4).
 *
 * See `docs\contributing\build-and-test.md`, "Versioning the driver", for what
 * each field surfaces as on each target, and for the two gates that cover the
 * copies this file cannot reach.
 */

#ifndef XHCI_VERSION_H
#define XHCI_VERSION_H

/* The four-part package version, as four integers - what FILEVERSION and
 * PRODUCTVERSION take, and what the Windows shell sorts by. */
#define XHCI_VER_CSV            1,0,0,1

/* The same number as a string - the resource's two version strings, the DOS
 * qualifier's banner, and the snapshot reader's report header. Must agree with
 * XHCI_VER_CSV above; the INF gate refuses a build where it does not. */
#define XHCI_VER_STR            "1.0.0.1"

/* The release date, in the MM/DD/YYYY form `DriverVer` takes, zero-padded -
 * the INF gate refuses an unpadded one, because Windows 98's 16-bit parser is
 * the reason the padding rule exists. */
#define XHCI_DRIVERVER_DATE     "09/02/2026"

#endif /* XHCI_VERSION_H */
