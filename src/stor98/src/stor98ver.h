/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * stor98ver.h - the driver's version, in one place.
 *
 * Used by nvme2k.rc for the version resource. The INFs' DriverVer and the
 * release directory name (releases\<version>\) must carry the same four
 * numbers; scripts\make-release.ps1 refuses a cut where they differ.
 */

#ifndef _STOR98VER_H_
#define _STOR98VER_H_

#define STOR98_VER_MAJOR    0
#define STOR98_VER_MINOR    4
#define STOR98_VER_PATCH    0
#define STOR98_VER_BUILD    0
#define STOR98_VER_STR      "0.4.0.0"

#endif /* _STOR98VER_H_ */
