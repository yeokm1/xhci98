// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// vfychk.h - what the driver checks of a VERIFY(10) or VERIFY(16) CDB
// (SBC), and the pure arithmetic of a VERIFY's work (xhci98 3.4.2.0, its
// roadmap 47.22; PLAN "VERIFY"). Pure byte decoding with no port or OS
// call, so the host unit test (tests/unit/vfychk_ut.c) includes it
// unchanged.
//
// What it can answer without the medium it answers here: a range past the
// namespace is LOGICAL BLOCK ADDRESS OUT OF RANGE, as for READ and WRITE
// (a range that starts at or past the end is told apart, VFY_PAST_END: the
// 9x build answers it GOOD with no media access, PLAN "VERIFY"),
// and a VERIFY that asks for what the driver does not do is INVALID FIELD
// IN CDB rather than a success: a byte-by-byte compare with data sent by
// the caller (BYTCHK, byte 1 bits 2:1), protection information checks
// (VRPROTECT, byte 1 bits 7:5; the driver serves no namespace with
// protection, src/nsfmt.h), or more than VFY_MAX_SECTORS sectors, the
// work bound of one VERIFY. A length of 0 verifies nothing and succeeds.
// The rest is read from the medium (nvme2k_scsi.c, engine.c): an NVMe
// Verify where the controller has it, a read into the bounce pages
// otherwise.
//
// VERIFY(6) (13h) is a sequential-access command; for a disk it is not
// decoded here: the dispatch answers it as an unsupported operation code.
//

#ifndef _STOR98_VFYCHK_H_
#define _STOR98_VFYCHK_H_

#define VFY_OK              0
#define VFY_BAD_FIELD       1       // 05/24/00
#define VFY_OUT_OF_RANGE    2       // 05/21/00: the range ends past the namespace
#define VFY_PAST_END        3       // the range starts at or past the end: 05/21/00, on 9x GOOD (PLAN "VERIFY")

#define VFY_OP_10           0x2F
#define VFY_OP_16           0x8F

// The most sectors one VERIFY may ask for: 32 MiB of the view's sectors, at
// most VERIFY(10)'s own maximum (65535 sectors of 512 bytes, 8192 of 4096:
// PLAN "VERIFY", 47.31). A larger VERIFY(16) is refused before any work.
#define VFY_MAX_SECTORS     65535UL
#define VFY_MAX_BYTES       (32UL * 1024 * 1024)

static ULONG VfyMaxSectors(ULONG SectorBytes)
{
    ULONG n = (SectorBytes != 0) ? VFY_MAX_BYTES / SectorBytes : VFY_MAX_SECTORS;

    return n > VFY_MAX_SECTORS ? VFY_MAX_SECTORS : n;
}

// The LBA and the count of a VERIFY(10) or (16) CDB.
static VOID VfyDecode(const UCHAR *Cdb, ULONGLONG *Lba, ULONG *Count)
{
    ULONGLONG lba = 0;
    ULONG i;

    if (Cdb[0] == VFY_OP_16) {
        for (i = 2; i < 10; i++) {
            lba = (lba << 8) | Cdb[i];
        }
        *Count = ((ULONG)Cdb[10] << 24) | ((ULONG)Cdb[11] << 16) | ((ULONG)Cdb[12] << 8) | Cdb[13];
    } else {
        lba = ((ULONG)Cdb[2] << 24) | ((ULONG)Cdb[3] << 16) | ((ULONG)Cdb[4] << 8) | Cdb[5];
        *Count = ((ULONG)Cdb[7] << 8) | Cdb[8];
    }
    *Lba = lba;
}

// Cdb is the CDB of a VERIFY(10) or (16); Blocks the namespace's size in
// the sectors the OS sees; MaxSectors the bound (VfyMaxSectors). Returns
// VFY_OK, VFY_PAST_END (no sector of the range is on the namespace; checked
// before the work bound) or the refusal.
static ULONG VfyCheck(const UCHAR *Cdb, ULONGLONG Blocks, ULONG MaxSectors)
{
    ULONGLONG lba;
    ULONG count;

    if ((Cdb[1] & 0xE6) != 0) {             // VRPROTECT, BYTCHK
        return VFY_BAD_FIELD;
    }
    VfyDecode(Cdb, &lba, &count);
    if (count == 0) {
        return VFY_OK;
    }
    if (lba >= Blocks) {
        return VFY_PAST_END;
    }
    if ((ULONGLONG)count > Blocks - lba) {
        return VFY_OUT_OF_RANGE;
    }
    if (count > MaxSectors) {
        return VFY_BAD_FIELD;
    }
    return VFY_OK;
}

// The native blocks a VERIFY of Count (> 0) sectors from Lba touches, with
// 2^Shift sectors a native block: the first and how many (whole blocks: a
// block asked for in part is read whole; nothing is written). VfyCheck has
// bounded Lba + Count by the namespace.
static VOID VfyNativeRange(ULONGLONG Lba, ULONG Count, ULONG Shift, ULONGLONG *First, ULONG *Blocks)
{
    ULONGLONG last = (Lba + Count - 1) >> Shift;

    *First = Lba >> Shift;
    *Blocks = (ULONG)(last - *First + 1);
}

// Verify Size Limit (NVM Command Set, the I/O Command Set specific Identify
// Controller's byte 0) in native blocks: 2^Vsl units of CAP.MPSMIN (4 KiB,
// the only page size the driver runs), 0 for none or unknown, at most
// 0xFFFFFFFF.
static ULONG VfyVslBlocks(ULONG Vsl, ULONG BlockSize)
{
    ULONGLONG bytes;

    if (Vsl == 0 || BlockSize == 0) {
        return 0;
    }
    if (Vsl > 40) {
        return 0xFFFFFFFFUL;                // far above every other bound
    }
    bytes = (ULONGLONG)4096 << Vsl;
    if (bytes / BlockSize > 0xFFFFFFFFUL) {
        return 0xFFFFFFFFUL;
    }
    return (ULONG)(bytes / BlockSize);
}

// The Verify Size Limit in the I/O Command Set specific Identify Controller
// data (CNS 06h, CSI 00h) at Id: byte 0 (byte 1 is the Write Zeroes Size
// Limit, not used).
static ULONG VfyIocsVsl(const UCHAR *Id)
{
    return Id[0];
}

// Native blocks of the next VERIFY child: at most Remaining and the read
// child size (MaxChildBytes); for an NVMe Verify (Cmd TRUE) at most 65536
// blocks (a 16-bit NLB) and the size limit VslBlocks (0 none); for a read
// into the bounce pages (Cmd FALSE) at most BounceBytes. At least one block
// while Remaining is not 0.
static ULONG VfyChildBlocks(int Cmd, ULONG Remaining, ULONG MaxChildBytes, ULONG BlockSize, ULONG VslBlocks,
                            ULONG BounceBytes)
{
    ULONG n = Remaining;

    if (BlockSize != 0 && n > MaxChildBytes / BlockSize) {
        n = MaxChildBytes / BlockSize;
    }
    if (Cmd) {
        if (n > 65536UL) {
            n = 65536UL;
        }
        if (VslBlocks != 0 && n > VslBlocks) {
            n = VslBlocks;
        }
    } else if (BlockSize != 0 && n > BounceBytes / BlockSize) {
        n = BounceBytes / BlockSize;
    }
    if (n == 0 && Remaining != 0) {
        n = 1;
    }
    return n;
}

#endif /* _STOR98_VFYCHK_H_ */
