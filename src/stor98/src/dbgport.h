// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2026, Yeo Kheng Meng
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Debug instrumentation (test builds only, STOR98_DBGPORT; docs/TESTING.md
// "Debug-port stage codes").
//
// Each stage is written to:
//   1. the usb4-emu debug port (usb4-debug-port, I/O 0x5a0-0x5af; usb4-emu
//      docs/HARNESS.md "Debug port"): argument to +4, stage code to +0. The
//      host traces it as usb4_dbg_stage, and harness rules (at=stage) fire
//      on it synchronously;
//   2. a QEMU isa-debugcon text line at I/O 0xe9:
//      "n98 <TSC, 16 hex> <code, 2 hex> <arg, 8 hex>\n".
// DBGMARK also writes the TSC as a clock marker (+8 low, +C high, traced as
// usb4_dbg_marker).
//
// Nothing is written unless the debug port answers its signature, so a test
// build is inert without the port. Without STOR98_DBGPORT every macro
// compiles to nothing. 9x and NT builds share this one table.
//
// Codes follow the unified table of TESTING.md and are never reused with a
// different meaning. The request-engine codes are in the free ranges
// 0xB5-0xBF and 0xC7-0xFF (0xC0-0xC6 stay the CM stand-in's).
//

#ifndef _STOR98_DBGPORT_H_
#define _STOR98_DBGPORT_H_

// Shared layer, 0x01-0x7F
#define STG_DRIVER_ENTRY        0x01    // arg: 0
#define STG_FIND_ADAPTER        0x02    // arg: bus << 16 | slot
#define STG_FIND_RESULT         0x03    // arg: SP_RETURN_*
#define STG_OPTIONS             0x04    // arg: gone ms (15:0) | idle-flush ms / 10 << 16 (clamped)
#define STG_FIND_BAR            0x05    // arg: BAR_F_* flags (7:0) | BAR_* reason << 8 | length in 4 KiB (or the command register read back) << 16 (nvme2k.c)
#define STG_STARTIO             0x10    // arg: STG_SRB_ARG(Srb, state)
#define STG_SRB_HELD            0x11    // arg: STG_SRB_ARG | TimeOutValue << 24 (low byte); held while not dispatchable
#define STG_SRB_DISPATCH_HELD   0x12    // arg: held ms << 8 | cdb0
#define STG_SRB_BUSY_STARTING   0x13    // arg: STG_SRB_ARG; BUSY while starting (no hold possible)
#define STG_SRB_FAILED_STATE    0x14    // arg: function | cdb0 << 8 | state << 16 | SrbStatus << 24
#define STG_SRB_HELD_TO_BUSY    0x15    // arg: held ms << 8 | cdb0 (hold-then-BUSY)
#define STG_SRB_COMPLETE        0x21    // arg: SrbStatus << 24 | NVMe status << 16 | cdb0 << 8 | function; just before RequestComplete
// 0x22, 0x23 are reserved for the RMW stages (after pre-read, before write-back)
#define STG_HWINIT_ENTER        0x30    // arg: CSTS
#define STG_HWINIT_CC           0x31    // arg: CC value written
#define STG_HWINIT_RETURN       0x32    // arg: return value | state << 8 | resume << 16
#define STG_HWINIT_CAP          0x33    // arg: CAP low dword
#define STG_STATE               0x40    // arg: old << 8 | new | reason << 16 | bring-up step << 24
#define STG_RDY_SEEN            0x41    // arg: ms in WAIT_RDY
#define STG_ADMIN_DONE          0x42    // arg: status << 16 | CID
#define STG_BRINGUP_DONE        0x43    // marker; arg: ms since HwInitialize
#define STG_BRINGUP_FAIL        0x44    // arg: reason (ENG_FAIL_*)
#define STG_DISABLE_ACK         0x45    // arg: ms waited for RDY=0
#define STG_QUARANTINE          0x46    // arg: quarantined children | quarantined PRP pages << 8 | bounce pages << 16
#define STG_TIMER_FIRST         0x47    // marker; arg: state; first timer call after HwInitialize
#define STG_NS_REFUSED          0x48    // arg: reason (NSF_*, nsfmt.h) | FLBAS << 8 | LBADS << 16 | DPS << 24; bring-up fails
#define STG_RESET_BUS           0x50    // marker; arg: PathId | issued children << 8 | state << 16 | call count << 24
#define STG_SRB_RESET_BUS       0x51    // arg: STG_SRB_ARG
#define STG_SRB_RESET_DEVICE    0x52    // arg: STG_SRB_ARG
#define STG_SRB_ABORT           0x53    // arg: target parent | outcome << 8 (0 not found, 1 done at once, 2 waits)
#define STG_SRB_SHUTDOWN        0x60    // arg: shutdown count << 8 | state
#define STG_SRB_FLUSH           0x61    // arg: shutdown count << 8 | state
#define STG_SYNC_CACHE          0x62    // arg: cdb0 | shutdown count << 8
#define STG_ADAPTER_CONTROL     0x63    // marker; arg: ControlType
#define STG_ADAPTER_STATE       0x67    // marker; arg: SaveState (9x)
#define STG_CLOCK_MARK          0x7F    // marker; arg: units per ms
// Gone state, MD-1 deadline, truthful reset (0x80-0x8E, as G2/G4)
#define STG_GONE_ENTER          0x81    // marker; arg: issued children | source << 16 | old state << 24
#define STG_GONE_FASTFAIL       0x82    // arg: STG_SRB_ARG (23:0) | SrbStatus << 24; first 32, then every 32nd
#define STG_GONE_DEADLINE       0x83    // marker; arg: SRBs completed | deadline count << 16
#define STG_GONE_HELD_DONE      0x84    // arg: SrbStatus | cdb0 << 8 | parent << 16 | per-SRB bound (1) << 24
#define STG_GONE_SUMMARY        0x86    // arg: issued children | held parents << 8 | seconds in gone << 16
#define STG_RESET_RESULT        0x87    // arg: return value | CSTS low byte << 8 | state << 16 | issued << 24
#define STG_RESET_ACK           0x88    // marker; arg: ms waited | SRBs completed << 16 (late acknowledgement)
#define STG_GONE_CONTACT        0x89    // arg: CSTS read in gone that is not all-ones
#define STG_BUS_CHANGE          0x8A    // arg: state; BusChangeDetected notified
#define STG_STOP_NOHW           0x8B    // arg: issued | state << 16; stop with no hardware access
#define STG_IO_ERROR            0x8C    // arg: SrbStatus << 24 | NVMe status (SCT<<8|SC, low 16 bits)
#define STG_FAILED_IO           0x8D    // arg: STG_SRB_ARG; failed with HARDWARE ERROR in failed/quarantined
// Qualification-layer meanings kept (0xA1-0xB4)
#define STG_DIRTY               0xA1    // arg: write generation; first write submitted while clean
#define STG_CLEAN               0xA2    // arg: generation; an ordered Flush covering every write completed
#define STG_SHN_FLUSH_DONE      0xA3    // marker; arg: NVMe status | shutdown count << 16 | dirty << 24
#define STG_SHN_WRITE           0xA4    // arg: CC written (SHN=01b)
#define STG_SHN_DONE            0xA5    // marker; arg: ms | CSTS << 16; SHST complete, SHUTDOWN SRB completed next
#define STG_SHN_TIMEOUT         0xA6    // marker; arg: ms | CSTS << 16; not clean (or all-ones)
#define STG_REENABLE            0xA7    // marker; arg: STG_SRB_ARG; request after a restartable shutdown: re-enable
#define STG_SHUT_REPEAT         0xA8    // arg: STG_SRB_ARG; SHUTDOWN/FLUSH while already shut down
#define STG_STOP_ENTER          0xA9    // marker; arg: CSTS low byte | dirty << 8 | state << 16 | issued << 24
#define STG_STOP_FLUSH_SUBMIT   0xAA    // marker; arg: write generation
#define STG_STOP_FLUSH_DONE     0xAC    // marker; arg: stalls | not done << 23 | NVMe status << 24; 0xFFFFFFFE clean
#define STG_STOP_SHN            0xAD    // marker; arg: CC written
#define STG_STOP_SHST_DONE      0xAF    // marker; arg: stalls | not seen << 23 | CSTS << 24
#define STG_STOP_EN0            0xB0    // marker; arg: CC written
#define STG_STOP_RDY_DONE       0xB2    // marker; arg: stalls | not seen << 23 | CSTS << 24
#define STG_STOP_SKIP           0xB3    // arg: 1 all-ones CSTS, 2 not running (no hardware access), 3 all-ones while the stop Flush was polled
#define STG_BUSY                0xB4    // arg: count (11:0) | SRB address bits 15:4 << 12 | cdb0 << 24; BUSY returned
// Request engine (0xB5-0xBF, 0xC7-)
#define STG_ENG_ADMIT           0xB5    // arg: parent | kind << 8 | cdb0 << 16 | flags << 24
#define STG_ENG_CHILD_SUBMIT    0xB6    // arg: CID | parent << 16 | bounce << 24 (bit 0) | admin (bit 1)
#define STG_ENG_CHILD_DONE      0xB7    // arg: CID | status (SC, low byte) << 16 | parent << 24
#define STG_ENG_PARENT_DONE     0xB8    // arg: parent | children << 8 | SrbStatus << 16 | cancel << 24
#define STG_ENG_STALE_CQE       0xB9    // arg: CID | reason << 16 (1 not issued, 2 generation, 3 controller generation, 4 not engine CID, 5 late completion of a quarantined child | parent << 24)
#define STG_ENG_CAP_WAIT        0xBA    // arg: reason (1 parents, 2 children, 3 PRP page, 4 bounce, 5 barrier) | waiting parents << 8
#define STG_ENG_CAP_RESUME      0xBB    // arg: free parents | free children << 8
#define STG_ENG_BOUNCE          0xBC    // arg: CID | pages << 16 | forced << 24
#define STG_ENG_MAP_FAIL        0xBD    // arg: reason (1 zero length, 2 alignment, 3 PA zero, 4 span not covered) | parent << 8
#define STG_ENG_CANCEL          0xBE    // arg: parent | cause << 8 | outstanding << 16
#define STG_ENG_IDLE_FLUSH      0xBF    // arg: write generation; MD-7 (a) idle flush submitted
#define STG_QUEUE_CAPS          0xC7    // arg: queue size | DSTRD << 16 | MPSMIN << 20 | result << 24 (0 ok)
#define STG_BUSY_LIMIT          0xC8    // arg: STG_SRB_ARG; failed instead of a 21st BUSY
#define STG_ENG_STATS           0xC9    // arg: parents completed (15:0) | stale CQEs << 16 | blocked double completions << 24
#define STG_CLOCK_CAL           0xCA    // arg: TSC ticks per ms (0 = fallback clock)
#define STG_DMA_NEGOTIATE       0xCB    // arg: max transfer KiB (15:0) | physical breaks << 16
#define STG_TUNABLES            0xCC    // arg: frag KiB (15:0) | force bounce << 16 | resumed << 17 | parsed << 18 | shnflush << 19
#define STG_ISOLATION_VIOLATION 0xCD    // marker; arg: CC low byte | CSTS low byte << 8 (controller kept old state across a gap)
#define STG_ENG_DOUBLE          0xCE    // arg: parent | state << 8; a second completion of one parent blocked
#define STG_STOP_RESULT         0xCF    // marker; arg: acknowledged (bit 0) | clean (bit 1) | quarantined (bit 2) | stalls << 8 | parents owned with nothing outstanding << 16 | with children issued << 24 (forgotten, not completed)
#define STG_ENG_ADMITTED        0xD0    // arg: parents admitted (15:0) | children issued << 16 (low)
#define STG_EVIDENCE            0xD1    // marker; arg: evidence (1 disable acknowledged, 2 link down) | state << 8
#define STG_DMA_ACQUIRE         0xD2    // arg: contiguous pages reported (15:0) | base page offset << 16; 0xFFFFFFFF none, 0xFFFFFFFE cache mode
#define STG_CLOCK_RECAL         0xD3    // arg: old TSC units per ms (15:0) | new (30:16) | rejected << 31; re-calibration against the OS clock
#define STG_CLOCK_OS            0xD4    // arg: OS clock state | ms << 8: 1 on trial at calibration (OS ms then); marker: 2 qualified, 3 rejected (window OS ms)
#define STG_SETTLE_REENABLE     0xD6    // arg: issued children | quarantined << 16; released by the re-enable's acknowledged disable
#define STG_SENSE_KEPT          0xD7    // arg: key | ASC << 8 | ASCQ << 16 | sense buffer length (capped 0xFE) << 24; no autosense: kept for REQUEST SENSE; length 0xFF: not kept, every entry in use
#define STG_SENSE_READ          0xD8    // arg: key | ASC << 8 | entries kept before << 16 | bytes returned << 24; REQUEST SENSE
#define STG_DMA_GAP             0xD9    // marker; arg: quarantined blocks | provider << 8 | state << 16; 9x: quarantine of port memory (owned only until removal, MD-4 pending), first time
#define STG_CQ_RESYNC           0xDA    // retired in review round 2 (was the CQ head doorbell replay on empty interrupts); never reused
#define STG_SHN_SUPPRESSED      0xDC    // arg: parent | cancel << 8 | state << 16; a SHUTDOWN's Flush completed outside running: no CC.SHN, the SRB completes with the recovery's status
#define STG_WRITE_THROUGH       0xDE    // arg: HwFindAdapter 0x100 | configured | resolved << 4 | Me << 12; bring-up 0x200 | verified << 4 | fua << 5 | vwc << 6 | Get dw0 (7:0) << 8 | Set status << 16 (PLAN D3)
#define STG_IDLE_FLUSH_TIMEOUT  0xDD    // marker; arg: ms since issue (trusted estimate, 23:0) | state << 24; the idle Flush missed its deadline: disable recovery (review round 4)
#define STG_INT_STORM           0xDB    // marker; arg: action (1 vector masked, polling; 2 retired (was controller disabled); 3 unmask trial; 4 storm went on masked, poll hold extended) | state << 8 | count (15:0 of masks) << 16
#define STG_DISABLE_REWRITE     0xD5    // arg: CC read; quarantined with CC.EN still 1: disable written again (first time)

// Polling mode (PLAN "Polling mode", 0.4.0.0), 0x150-0x15F
#define STG_POLL_TUNABLES       0x150   // arg: mode | clamped (POLL_CLAMP_*) << 4 | resumed << 8 | spin us / 10 << 9 (6 bits) | busy ms << 16 (capped 0xFF) | idle s << 24 (capped 0xFF, 0 = stops); a clamp is logged here
#define STG_POLL_RAW            0x151   // arg: present (POLL_CLAMP_*) | raw mode << 4 (capped 0xF) | raw busy us / 1000 << 8 (capped 0xFFFF) | raw idle ms / 100 << 24 (capped 0xFF); only when a value was clamped
#define STG_POLL_DECIDE         0x152   // marker; arg: pin | line << 8 | why (POLL_WHY_*) << 16 | pci flags (S98_PCI_*) << 24
#define STG_POLL_PORTIRQ        0x153   // arg: port-supplied BusInterruptLevel (15:0) | BusInterruptVector << 16 (both as the port gave them)
#define STG_POLL_REPORT         0x154   // arg: result of writing the interrupt report value (0 written, else the OS status low 16 bits) | skipped reason << 16 (1 resumed, 2 dump)
#define STG_POLL_IDLE           0x155   // arg: timer ticks while polling (15:0) | completions found by HwStartIo (spin included) << 16 (capped); each time a polling adapter goes idle
#define STG_POLL_ISR            0x156   // arg: HwInterrupt calls while polling (logged at the first call only; then counted)

// Maximum transfer setting (xfertun.h; PLAN "0.4.0.0 plan", owner decision 2026-10-16), 0x160-0x16F
#define STG_XFER_TUNABLE        0x160   // arg: limit used, KiB (15:0) | result (XFER_DEFAULT 0, XFER_SET 1, XFER_REFUSED 2) << 16 | resumed << 20 | parsed << 21; every HwFindAdapter
#define STG_XFER_REFUSED        0x161   // arg: the refused raw value as read (KiB; 0xFFFFFFFF saturated); 0 also when it is not a number

// Packed request description: function | cdb0 << 8 | state << 16 | flags << 24
// (flags bit 0 tagged, bit 1 queue action ORDERED)
#define STG_SRB_ARG(Srb, St) \
    ((ULONG)(Srb)->Function | ((ULONG)(Srb)->Cdb[0] << 8) | ((ULONG)(St) << 16) | \
     ((((Srb)->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb)->QueueTag != SP_UNTAGGED) ? 0x01000000UL : 0) | \
     ((((Srb)->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb)->QueueAction == SRB_ORDERED_QUEUE_TAG_REQUEST) ? 0x02000000UL : 0))

#ifdef STOR98_DBGPORT

#define STOR98_DBGPORT_BASE     0x5A0
#define STOR98_DBGPORT_SIG      0x42443455  // "U4DB"
#define STOR98_DEBUGCON_PORT    0xE9

VOID DbgPortInit(VOID);
VOID DbgStage(IN ULONG Code, IN ULONG Arg);
VOID DbgMark(IN ULONG Code, IN ULONG Arg);

#define DBGSTAGE(c, a)  DbgStage((ULONG)(c), (ULONG)(a))
#define DBGMARK(c, a)   DbgMark((ULONG)(c), (ULONG)(a))
#define DBGINIT()       DbgPortInit()

#else

#define DBGSTAGE(c, a)
#define DBGMARK(c, a)
#define DBGINIT()

#endif // STOR98_DBGPORT

#endif // _STOR98_DBGPORT_H_
