// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// q9x.h - Windows 9x qualification layer (docs/gates/Q-9x.md). Test builds only.
//
// Compiled in only for the Windows 9x target when the build sets STOR98_Q9X=1
// (scripts\build.cmd passes the environment variable through src\sources). It
// sits on the G9 instrumentation (g9dbg.h, always on with it) and prototypes:
//
//   - the 9x receiver of the CM link-state table (include/stor98cm.h,
//     interface 3): per-boot registry record read once at HwFindAdapter,
//     table validated (R5), bound (R8), polled from the timer and HwStartIo,
//     DOWN_QUIESCED as evidence 2;
//   - the MD-5 removal transaction (REMOVE_PENDING: drain, ordered Flush,
//     CC.SHN, SHST, CC.EN=0, RDY=0, acknowledgement; terminal afterwards);
//   - the MD-4 DMA pool (VMM _PageAllocate, ownership records in the CM-owned
//     table, boot-wide budget in an stor98 accounting block);
//   - write-through for Windows Me (FUA on every write, or the volatile write
//     cache disabled with Set Features and checked with Get Features);
//   - deferred bring-up (G1 on 9x) and a truthful HwResetBus (G4 on 9x).
//
// Options are read once, at HwFindAdapter, from HKLM\Software\stor98\Q9x
// (DWORD values; ArgumentString is NULL on 9x, G9). Not product code.

#ifndef _Q9X_H_
#define _Q9X_H_

#if defined(STOR98_Q9X) && defined(NVME2K_W9X)

// Stage codes (usb4-emu debug port). 9x qualification layer: 0xE0-0xFF of
// the unified table (docs/TESTING.md). Markers also write the TSC.
#define Q9S_OPTIONS         0xE0    // arg: option bits
#define Q9S_RENDEZVOUS      0xE1    // arg: result (0 ok, else reason) | gen << 16
#define Q9S_POOL            0xE2    // arg: pool index | how << 8 | pages << 16
#define Q9S_BRINGUP         0xE3    // arg: step | ms << 8
#define Q9S_READY           0xE4    // arg: bring-up ms
#define Q9S_HOLD            0xE5    // arg: hold count
#define Q9S_BUSY            0xE6    // arg: busy count
#define Q9S_GONE            0xE7    // arg: depth | nontagged << 8
#define Q9S_DEADLINE        0xE8    // MD-1 gone deadline: arg completed count
#define Q9S_LS_SEEN         0xE9    // DOWN_QUIESCED seen: arg gen | mygen << 8 | state << 16
#define Q9S_LS_ACK          0xEA    // link acknowledgement written: arg tunnel | done << 8
#define Q9S_RM_SEEN         0xEB    // REMOVE_PENDING seen: arg request | gen << 16
#define Q9S_RM_STEP         0xEC    // arg: step | ms << 8
#define Q9S_RM_ACK          0xED    // arg: outcome | request << 8
// 0xEE, 0xEF: v1 cancel and INVALIDATED, withdrawn (codes are never reused)
#define Q9S_RESET           0xF0    // HwResetBus: arg result | state << 8
#define Q9S_RESET_LATE      0xF1    // late reset acknowledgement
#define Q9S_FAILED          0xF2    // state failed: arg reason
#define Q9S_STOP            0xF3    // HwAdapterState(FALSE): arg result
#define Q9S_WT              0xF4    // write-through: arg mode | verified << 8 | vwc << 16
#define Q9S_SHUTDOWN        0xF5    // SRB_FUNCTION_SHUTDOWN: arg step
#define Q9S_POOL_CHECK      0xF6    // pool sentinel check at reuse: arg ok
#define Q9S_TICK            0xF7    // periodic summary
#define Q9S_IDENTITY        0xF8    // entry snapshot rejected or identity lost: arg reason
#define Q9S_REFUSED         0xF9    // request refused after the acknowledgement: arg count

// Options (HKLM\Software\stor98\Q9x)
typedef struct _Q9X_OPTIONS {
    ULONG Pool;             // 1: DMA memory from the _PageAllocate pool (default 1)
    ULONG Bounce;           // 1: every read/write up to 64 KiB through a pool bounce buffer
    ULONG WriteThrough;     // 0 off, 1 FUA on every write, 2 VWC off (Set Features), 3 both
    ULONG Deferred;         // 1: deferred bring-up from the timer (default 1)
    ULONG HoldMs;           // bring-up hold bound before BUSY (default 0: hold until bring-up ends)
    ULONG GoneMs;           // gone deadline (default 5000)
    ULONG TickMs;           // timer period (default 100)
    ULONG NoAck;            // 1: never acknowledge a removal request (CM timeout test)
    ULONG ShstMs;           // SHST wait in the handshake (default 2000)
    ULONG PoolFree;         // 1: _PageFree a CLEAN block at HwAdapterState(FALSE)
    ULONG CapPages;         // boot-wide pool budget (R11; default 264 pages = 8 blocks)
    ULONG ResetMode;        // 0: truthful reset, 1: always return FALSE (G4)
    ULONG BringupMs;        // bring-up deadline for RDY (default: CAP.TO)
    ULONG IgnoreCm;         // 1: skip the rendezvous (test of the "no CM" case)
    ULONG CorruptRec;       // 1: corrupt this load's record when releasing it (validation test)
    ULONG CorruptAcct;      // at pool give-up: 1 damage the boot block, 2 truncate, 3 damage the reference, 4 delete it and malform AcctBoot (fail-closed tests)
    ULONG IsrProbe;         // 1: a terminal, detached instance calls its own HwInterrupt every tick
    ULONG BudgetSkewMs;     // test: added to the handshake's elapsed time (a late first sight, R10)
} Q9X_OPTIONS;

// Adapter states
#define Q9_STARTING     1   // deferred bring-up in progress
#define Q9_RUNNING      2
#define Q9_GONE         3   // presence lost, no evidence yet
#define Q9_LINKDOWN     4   // evidence 2 applied
#define Q9_REMOVING     5   // MD-5 handshake in progress
#define Q9_QUIESCED     6   // handshake done (controller disabled; terminal, R9d)
#define Q9_FAILED       7
#define Q9_RESETTING    8   // truthful reset waiting for RDY=0
#define Q9_SHUTDOWN     9   // restartable SHUTDOWN done (CC.SHN, enabled)
#define Q9_STOPPED      10  // HwAdapterState(FALSE) done

struct _HW_DEVICE_EXTENSION;

ULONG Q9FindAdapter(struct _HW_DEVICE_EXTENSION *DevExt, PPORT_CONFIGURATION_INFORMATION ConfigInfo);
BOOLEAN Q9PoolInstall(struct _HW_DEVICE_EXTENSION *DevExt);
BOOLEAN Q9HwInitialize(struct _HW_DEVICE_EXTENSION *DevExt);
BOOLEAN Q9StartIo(struct _HW_DEVICE_EXTENSION *DevExt, PSCSI_REQUEST_BLOCK Srb);
BOOLEAN Q9IoCompletionHook(struct _HW_DEVICE_EXTENSION *DevExt, USHORT Cid, USHORT Status);
VOID Q9BeforeComplete(struct _HW_DEVICE_EXTENSION *DevExt, PSCSI_REQUEST_BLOCK Srb, USHORT Status);
BOOLEAN Q9AdminHook(struct _HW_DEVICE_EXTENSION *DevExt, USHORT Cid, USHORT Status, ULONG Dw0);
BOOLEAN Q9BuildBounce(struct _HW_DEVICE_EXTENSION *DevExt, PSCSI_REQUEST_BLOCK Srb, PNVME_COMMAND Cmd, BOOLEAN IsWrite);
VOID Q9WriteFlags(struct _HW_DEVICE_EXTENSION *DevExt, PNVME_COMMAND Cmd);
BOOLEAN Q9ResetBus(struct _HW_DEVICE_EXTENSION *DevExt, ULONG PathId);
VOID Q9AdapterStop(struct _HW_DEVICE_EXTENSION *DevExt);

#define Q9_INTERNAL_CID_BASE    0x7F00  // internal I/O commands (bit 14 set: no SRB)
#define Q9_ADMIN_CID_BASE       0x7E00  // internal admin commands

#endif

#endif // _Q9X_H_
