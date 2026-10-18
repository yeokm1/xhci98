// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// xfertun.h - the maximum transfer size setting (PLAN "0.4.0.0 plan", owner
// decision 2026-10-16): bounds and the validation rule. Pure arithmetic with
// no port or OS call, so the host unit test (tests/unit/xfer_ut.c) includes
// it unchanged.
//
// Where the value comes from (read once in HwFindAdapter, kept in the device
// extension, which survives an NT resume that brings no ArgumentString):
//   NT  DriverParameter (REG_SZ) in HKLM\System\CurrentControlSet\Services\
//       stor98\Parameters\Device: maxtransferkb=<n> (decimal). No older
//       DriverParameter key is a suffix of it, and it is a suffix of none.
//   9x  REG_SZ (decimal) or REG_DWORD value StorMaxTransferKB in
//       HKLM\System\CurrentControlSet\Services\stor98.
//
// The value, in KiB, can only lower the build's maximum (STOR98_MAX_TRANSFER:
// 512 KiB on 9x, 256 KiB on NT), never raise it: the PRP list and NLB checks
// of nvme2k.h cover the build's maximum only. Accepted: XFER_KB_MIN to the
// build's maximum, in multiples of XFER_KB_STEP (one page). Anything else,
// including a value that is present but not a number, is refused: the
// build's maximum stands and the refusal is logged (debug-port stages
// STG_XFER_TUNABLE and STG_XFER_REFUSED). Absent: the build's maximum.
//

#ifndef _STOR98_XFERTUN_H_
#define _STOR98_XFERTUN_H_

#define XFER_KB_MIN             64
#define XFER_KB_STEP            4

// XferTunApply results
#define XFER_DEFAULT            0   // absent: the build's maximum
#define XFER_SET                1   // accepted
#define XFER_REFUSED            2   // present but invalid: the build's maximum

// The whole value as a decimal number: blanks, digits, blanks, then the end
// of the string, or a ';' too when Field is TRUE (an NT DriverParameter
// field; a 9x REG_SZ value must end there). FALSE for no digit or anything
// else after the digits ("128.5", "128k"). Too large for 32 bits saturates
// (and is then refused).
static BOOLEAN XferParseDec(const char *p, BOOLEAN Field, ULONG *Out)
{
    ULONG v = 0;
    BOOLEAN any = FALSE;

    if (p == NULL) {
        return FALSE;
    }
    while (*p == ' ') {
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        ULONG d = (ULONG)(*p - '0');

        v = (v > (0xFFFFFFFFUL - d) / 10) ? 0xFFFFFFFFUL : v * 10 + d;
        any = TRUE;
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    if (!any || (*p != 0 && !(Field && *p == ';'))) {
        return FALSE;
    }
    *Out = v;
    return TRUE;
}

// Present: the value was found. Valid: it parsed as a number (RawKb). MaxKb:
// the build's maximum in KiB. *OutKb receives the limit to use.
static ULONG XferTunApply(BOOLEAN Present, BOOLEAN Valid, ULONG RawKb, ULONG MaxKb, ULONG *OutKb)
{
    *OutKb = MaxKb;
    if (!Present) {
        return XFER_DEFAULT;
    }
    if (!Valid || RawKb < XFER_KB_MIN || RawKb > MaxKb || (RawKb % XFER_KB_STEP) != 0) {
        return XFER_REFUSED;
    }
    *OutKb = RawKb;
    return XFER_SET;
}

#endif // _STOR98_XFERTUN_H_
