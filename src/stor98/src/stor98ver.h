/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * stor98ver.h - the driver's version, in one place.
 *
 * Since xhci98 3.1.0.0 Stor98 is built in xhci98's tree (src\stor98) and
 * carries the xhci98 package's version and release date: one version for
 * every binary and INF of the package (owner, 2026-10-19). Its single
 * source is xhci98's src\xhci_version.h, two directories up, which holds
 * only #define lines and comments. Used by nvme2k.rc for the version
 * resource. Both INFs' DriverVer carry the same version and date as
 * literals; xhci98's scripts\inf-gate\check-stor98-inf.ps1 checks them
 * against xhci_version.h on every gated build.
 *
 * Stor98's own interface versions (STOR98CM_VERSION, STOR98LS_VERSION and
 * the others in include\stor98cm.h) are protocol versions, not this.
 * Releases up to 0.4.0.0 had numbers of their own (releases\history.md).
 */

#ifndef _STOR98VER_H_
#define _STOR98VER_H_

#include "..\..\xhci_version.h"

/* FILEVERSION and PRODUCTVERSION take four comma-separated integers. */
#define STOR98_VER_CSV      XHCI_VER_CSV
#define STOR98_VER_STR      XHCI_VER_STR

#endif /* _STOR98VER_H_ */
