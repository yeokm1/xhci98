// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// vmmprobe.h - test-only STOR98_TEST_VMMPROBE (xhci98 3.4.1.0, task 46.20,
// the 98 SE faults inside VMM): what runs inside what on 98 SE's port, and
// what the 9x engine touches at interrupt time. A debug-port build only
// (STOR98_DBGPORT, 9x); never staged. Nothing here changes what the driver
// does; it counts and logs (dbgport.h, STG_TEST_VMM_*).
//
// - Nesting: each of HwStartIo, the timer routine and HwInterrupt counts its
//   own entries in progress (every adapter together); an entry while any of
//   them is in progress is logged with the set in progress.
// - The interrupt flag at each entry (EFLAGS.IF): an entry with IF set can be
//   interrupted by the timer's or the drive's interrupt.
// - Stack headroom at each entry: the bytes from ESP down to the first page
//   below it that is not present (VMM _CopyPageTable, at most 32 KiB looked
//   at), a new least value logged per callback.
// - The arena of every READ/WRITE buffer and scatter/gather fragment
//   (private below 80000000h, shared below C0000000h, system above), and a
//   bounce copy to or from a non-system address made while HwInterrupt or
//   the timer routine is in progress (another process's memory context).
// - Each RequestTimerCall by the set in progress when it is made.
// STOR98_TEST_VMMMAP (nvme2k.c) adds one deliberate fault inside a chosen
// VMM service; STOR98_TEST_FINDNONE (nvme2k.c) makes the 9x find refuse;
// STOR98_TEST_FWDECODEOFF (nvme2k.c) turns the found function's memory
// decode off for the set-up check (PLAN 3.1).
//

#ifndef _STOR98_VMMPROBE_H_
#define _STOR98_VMMPROBE_H_

#if defined(STOR98_TEST_FINDNONE) && (!defined(STOR98_DBGPORT) || !defined(NVME2K_W9X))
#error STOR98_TEST_FINDNONE is test-only and 9x-only: build it with STOR98_DBGPORT=1 for 9x (out\<flavor>-dbgport, never staged)
#endif
#if defined(STOR98_TEST_FWDECODEOFF) && (!defined(STOR98_DBGPORT) || !defined(NVME2K_W9X))
#error STOR98_TEST_FWDECODEOFF is test-only and 9x-only: build it with STOR98_DBGPORT=1 for 9x (out\<flavor>-dbgport, never staged)
#endif
#if defined(STOR98_TEST_VMMMAP) && !defined(STOR98_TEST_VMMPROBE)
#error STOR98_TEST_VMMMAP needs STOR98_TEST_VMMPROBE
#endif

#ifdef STOR98_TEST_VMMPROBE
#if !defined(STOR98_DBGPORT) || !defined(NVME2K_W9X)
#error STOR98_TEST_VMMPROBE is test-only and 9x-only: build it with STOR98_DBGPORT=1 for 9x (out\<flavor>-dbgport, never staged)
#endif
#ifdef STOR98_TEST_REENTRY
#error STOR98_TEST_VMMPROBE and STOR98_TEST_REENTRY wrap the same callbacks: build one at a time
#endif

#define VP_STARTIO      0
#define VP_TIMER        1
#define VP_INTERRUPT    2
#define VP_KINDS        3

// The set in progress, as a mask: 1 HwStartIo, 2 the timer, 4 HwInterrupt.
ULONG VpMask(VOID);
// Entry and exit of a callback; Enter returns nothing the caller keeps.
VOID VpEnter(IN ULONG Kind);
VOID VpExit(IN ULONG Kind);
// A READ/WRITE SRB's buffer (engine.c, EngPfSrbIn).
VOID VpArena(IN ULONG What, IN ULONG Linear, IN ULONG Bytes, IN ULONG Func, IN ULONG Cdb0);
// A bounce copy's caller-side span (engine.c, EngCallerCopy).
VOID VpCopy(IN ULONG Linear, IN ULONG Bytes, IN BOOLEAN ToCaller);
// A RequestTimerCall about to be made (lifecycle.c, EngTimerEnsure and
// EngStop).
VOID VpTimerRequest(IN ULONG PeriodUs);

#endif // STOR98_TEST_VMMPROBE

#endif // _STOR98_VMMPROBE_H_
