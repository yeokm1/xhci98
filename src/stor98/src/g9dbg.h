// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// g9dbg.h - gate G9 instrumentation (docs/PLAN.md 0.0 G9, 0.12; docs/gates/G9.md).
//
// Test-only. Compiled in only for the Windows 9x target when the build sets
// STOR98_G9 (scripts\build.cmd passes the environment variable STOR98_G9=1, 2
// or 3 through src\sources):
//
//   STOR98_G9       log lines to the QEMU debug console (I/O port 0xE9, QEMU
//                   -debugcon) and stage codes / clock markers to usb4-emu's
//                   usb4-debug-port (I/O 0x5a0, docs/HARNESS.md in usb4-emu).
//                   Also routes the inherited ScsiDebugPrint calls (NVME2K_DBG)
//                   to the debug console.
//   STOR98_G9_GONE  (level 2) the minimal gone-state prototype of the gate:
//                   all-ones CSTS moves to "gone", new SRBs fast-fail with
//                   SRB_STATUS_NO_DEVICE, outstanding SRBs are completed with
//                   SRB_STATUS_NO_DEVICE at a 5 s deadline (decision MD-1).
//   STOR98_G9_PROBE (level 3) level 2 plus a RequestTimerCall probe armed in
//                   HwAdapterState and at SRB_FUNCTION_SHUTDOWN.
//   STOR98_G9_CACHES (level 4) level 3 plus CachesData=TRUE and a read-only
//                   log of the IOS DDB that holds the device extension.
//   STOR98_G9_DDBPOKE (level 5) level 4 plus setting DDB_ios_flags bit 0x04 at
//                   the first read. Experiment only (docs/gates/G9.md, "Windows
//                   Me stop investigation").
//
// Without STOR98_G9 every macro here expands to nothing.

#ifndef _G9DBG_H_
#define _G9DBG_H_

#if defined(STOR98_G9) && defined(NVME2K_W9X)

#define G9_DEBUGCON_PORT    0xE9
#define G9_DBGPORT_BASE     0x5A0
#define G9_DBGPORT_STAGE    (G9_DBGPORT_BASE + 0)
#define G9_DBGPORT_ARG      (G9_DBGPORT_BASE + 4)
#define G9_DBGPORT_MARK_LO  (G9_DBGPORT_BASE + 8)
#define G9_DBGPORT_MARK_HI  (G9_DBGPORT_BASE + 12)

// Stage codes written to the debug port (traced by usb4-emu as usb4_dbg_stage).
// Moved on qual-9x from 0x01-0x60 (which collided with the unified NT table,
// docs/TESTING.md) to 0xD0-0xDF, and on int-0.2 to 0x1D0-0x1DF (below);
// traces from phase0-g9 and qual-9x use the older values.
#define G9S_DRIVER_ENTRY    0x1D0
#define G9S_FIND_ADAPTER    0x1D1
#define G9S_FOUND           0x1D2
#define G9S_HWINIT_ENTER    0x1D3
#define G9S_HWINIT_EXIT     0x1D4
#define G9S_STARTIO         0x1D5    // arg: func<<24 | cdb0<<16 | tag<<8 | flags
#define G9S_COMPLETE        0x1D6    // arg: srbstatus<<24 | cdb0<<16 | tag<<8
#define G9S_RESET_BUS       0x1D7
#define G9S_ADAPTER_STATE   0x1D8    // arg: SaveState
#define G9S_SHUTDOWN_CTRL   0x1D9
#define G9S_TIMER           0x1DA
#define G9S_PROBE_TIMER     0x1DB
#define G9S_GONE            0x1DC
#define G9S_GONE_DEADLINE   0x1DD
#define G9S_FASTFAIL        0x1DE
#define G9S_SUMMARY         0x1DF

VOID G9Log(const char *Fmt, ...);
VOID G9DebugPrint(ULONG Level, const char *Fmt, ...);
VOID G9Stage(ULONG Code, ULONG Arg);
VOID G9Marker(VOID);
ULONG G9Tick(VOID);

struct _HW_DEVICE_EXTENSION;
VOID G9NewInstance(struct _HW_DEVICE_EXTENSION *DevExt);
VOID G9Summary(struct _HW_DEVICE_EXTENSION *DevExt, const char *Why);
VOID G9ProbeConfig(struct _HW_DEVICE_EXTENSION *DevExt, const char *Where);
VOID G9StartIoLog(struct _HW_DEVICE_EXTENSION *DevExt, PSCSI_REQUEST_BLOCK Srb);
VOID G9CompleteLog(struct _HW_DEVICE_EXTENSION *DevExt, PSCSI_REQUEST_BLOCK Srb, USHORT Cid, USHORT Status);
#if defined(STOR98_G9_GONE)
BOOLEAN G9CheckGone(struct _HW_DEVICE_EXTENSION *DevExt, const char *Where);
BOOLEAN G9StartIoGone(struct _HW_DEVICE_EXTENSION *DevExt, PSCSI_REQUEST_BLOCK Srb);
BOOLEAN G9TimerGone(struct _HW_DEVICE_EXTENSION *DevExt);
#endif
#if defined(STOR98_G9_CACHES)
VOID G9DdbLog(struct _HW_DEVICE_EXTENSION *DevExt, const char *Where);
#endif
#if defined(STOR98_G9_PROBE)
VOID G9ArmProbe(struct _HW_DEVICE_EXTENSION *DevExt, const char *Where);
#endif

#define G9LOG(args)         G9Log args
#define G9STAGE(code, arg)  G9Stage((code), (ULONG)(arg))

#else

#define G9LOG(args)
#define G9STAGE(code, arg)

#endif

#endif // _G9DBG_H_
