/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * pciscan.h - the two decisions of HwFindAdapter's NT4-ABI bus scan
 * (nvme2k.c), which 9x uses: whether a slot holds a function, and which
 * slot comes next. Pure, so the host unit tests hold them
 * (tests/unit/pciscan_ut.c); no DDK header needed.
 */

#ifndef _STOR98_PCISCAN_H_
#define _STOR98_PCISCAN_H_

// Every PCI bus number (0-255), not the first 16 the base scanned: a
// firmware numbers a hot-plug port's range far up (the emulator's asm4242
// model gives its two USB4 ports buses 19h-48h and 49h-78h), and a drive
// tunnelled there sits inside it.
#define PCISCAN_LAST_BUS    255UL
#define PCISCAN_SLOTS       256UL   // 32 devices x 8 functions per bus

// TRUE when a probe of a slot's vendor id (4 bytes from offset 0) found no
// function there (PLAN 3.4): a return of 0 (no such bus), 2 (no function)
// or anything short of the 4 bytes asked for, or a vendor id of 0000h or
// FFFFh.
static __inline int PciScanAbsent(unsigned long BytesRead,
                                  unsigned short VendorId)
{
    return BytesRead < 4UL || VendorId == 0xFFFF || VendorId == 0x0000;
}

// Moves *Bus and *Slot to the next slot to probe; FALSE once past the
// last bus (the scan is over, nothing found).
static __inline int PciScanNext(unsigned long *Bus, unsigned long *Slot)
{
    (*Slot)++;
    if (*Slot >= PCISCAN_SLOTS) {
        *Slot = 0;
        (*Bus)++;
        if (*Bus > PCISCAN_LAST_BUS) {
            return 0;
        }
    }
    return 1;
}

#endif // _STOR98_PCISCAN_H_
