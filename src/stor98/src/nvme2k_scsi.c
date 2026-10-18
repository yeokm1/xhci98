// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2025, Dominik Behr (nvme2k)
// Windows 9x/NT4 port and changes (nvme9x/nvment): Copyright (c) SweetLow
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
// handling SCSI commands
#include "nvme2k.h"
#include "utils.h"
#include "inqid.h"

//
// Completion of SRBs the driver answers itself (no NVMe command). The
// engine decides about NextRequest (EngStartIo).
//
BOOLEAN ScsiSuccess(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    EngCompleteSrb(DevExt, Srb);
    return TRUE;
}

BOOLEAN ScsiError(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR SrbStatus)
{
    if (SrbStatus == SRB_STATUS_ERROR && Srb->ScsiStatus == SCSISTAT_CHECK_CONDITION &&
        (Srb->SrbStatus & SRB_STATUS_AUTOSENSE_VALID)) {
        SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    }
    Srb->SrbStatus = SrbStatus;
    EngCompleteSrb(DevExt, Srb);
    return TRUE;
}

// Sense construction, retention and REQUEST SENSE: sense.c.

//
// CHECK CONDITION, ILLEGAL REQUEST with Asc (ASCQ 0) and no data: INVALID
// COMMAND OPERATION CODE (20h) for a command the driver does not implement,
// INVALID FIELD IN CDB (24h) for a field or page it does not support.
//
BOOLEAN ScsiIllegalRequest(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR Asc)
{
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    ScsiSetSense(DevExt, Srb, SCSI_SENSE_ILLEGAL_REQUEST, Asc, 0);
    Srb->DataTransferLength = 0;
    return ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
}

//
// CHECK CONDITION, NOT READY, LOGICAL UNIT NOT READY (02/04/00), no data:
// the namespace's size is not known.
//
static BOOLEAN ScsiNotReady(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    ScsiSetSense(DevExt, Srb, SCSI_SENSE_NOT_READY, SCSI_ADSENSE_LUN_NOT_READY, 0);
    Srb->DataTransferLength = 0;
    return ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
}

//
// ScsiHandleInquiry - Handle SCSI INQUIRY command
//
// Standard data, or VPD pages 00h, 80h and 83h; anything else is ILLEGAL
// REQUEST, INVALID FIELD IN CDB (src/inqid.c, InqResponse). The response is
// cut at the allocation length and at the data buffer, so a short buffer
// gets the first bytes of the data.
//
BOOLEAN ScsiHandleInquiry(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR data[INQ_RESP_MAX];
    ULONG n, i;

    n = InqResponse(Srb->Cdb, DevExt->ControllerSerialNumber, DevExt->ControllerModelNumber,
                    DevExt->ControllerFirmwareRevision, data);
    if (n == INQ_REFUSE) {
        return ScsiIllegalRequest(DevExt, Srb, SCSI_ADSENSE_INVALID_CDB);
    }
    if (n > Srb->DataTransferLength) {
        n = Srb->DataTransferLength;
    }
    if (Srb->DataBuffer == NULL) {
        n = 0;
    }
    for (i = 0; i < n; i++) {
        ((PUCHAR)Srb->DataBuffer)[i] = data[i];
    }
    Srb->DataTransferLength = n;
    return ScsiSuccess(DevExt, Srb);
}

//
// ScsiHandleReadCapacity - Handle SCSI READ CAPACITY(10) command
//
BOOLEAN ScsiHandleReadCapacity(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PULONG capacityData;
    ULONG lastLba;
    ULONG blockSize;
    
    if (Srb->DataTransferLength < 8) {
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }
    
    capacityData = (PULONG)Srb->DataBuffer;
    
    // The bring-up refuses a namespace without a size or block size
    // (src/nsfmt.h), so neither is 0 here; never a made-up capacity.
    if (DevExt->NamespaceSizeInBlocks == 0 || DevExt->NamespaceBlockSize == 0) {
        return ScsiNotReady(DevExt, Srb);
    }
    // Check if capacity exceeds 32-bit
    if (DevExt->NamespaceSizeInBlocks > 0xFFFFFFFF) {
        lastLba = 0xFFFFFFFF;  // Indicate to use READ CAPACITY(16)
    } else {
        lastLba = (ULONG)(DevExt->NamespaceSizeInBlocks - 1);
    }
    blockSize = DevExt->NamespaceBlockSize;
    
    // Return in big-endian format
    capacityData[0] = BSWAP(lastLba);
    capacityData[1] = BSWAP(blockSize);
    
    Srb->DataTransferLength = 8;
    return ScsiSuccess(DevExt, Srb);
}

//
// ScsiHandleReadCapacity - Handle SCSI READ CAPACITY(16) command
//
BOOLEAN ScsiHandleReadCapacity64(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PREAD_CAPACITY16_DATA capacityData;
    ULARGE_INTEGER lastLba;
    ULONG blockSize;
    
    if (Srb->DataTransferLength < sizeof(*capacityData)) {
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }
    
    // As READ CAPACITY(10): never a made-up capacity.
    if (DevExt->NamespaceSizeInBlocks == 0 || DevExt->NamespaceBlockSize == 0) {
        return ScsiNotReady(DevExt, Srb);
    }
    capacityData = (PREAD_CAPACITY16_DATA)Srb->DataBuffer;
    RtlZeroMemory(capacityData, Srb->DataTransferLength);
    lastLba.QuadPart = DevExt->NamespaceSizeInBlocks - 1;
    blockSize = DevExt->NamespaceBlockSize;
    
    // Return in big-endian format
    BSWAPCopy(&capacityData->LogicalBlockAddress,&lastLba);
    capacityData->BytesPerBlock = BSWAP(blockSize);

    Srb->DataTransferLength = sizeof(*capacityData);
    return ScsiSuccess(DevExt, Srb);
}

//
// ScsiHandleLogSense - Handle SCSI LOG SENSE command
// Translates to NVMe Get Log Page for SMART/Health data
//
BOOLEAN ScsiHandleLogSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR pageCode;

    // Extract page code from CDB
    pageCode = ScsiGetLogPageCodeFromSrb(Srb);

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ScsiHandleLogSense - Page Code=0x%02X\n", pageCode);
#endif

    // We only support the SMART/Health Information log page for now
    // A real implementation would check for other pages.
    // For simplicity, we assume any log sense is for SMART data.
    if (pageCode == SCSI_LOG_PAGE_INFORMATIONAL) {

        // NVMe Get Log Page (SMART/Health) through the engine
        return EngSubmitLogPage(DevExt, Srb, NVME_LOG_PAGE_SMART_HEALTH);
    } else {
        // Unsupported log page
        return ScsiIllegalRequest(DevExt, Srb, SCSI_ADSENSE_INVALID_CDB);
    }
}

//
// ScsiHandleSatPassthrough - Handle SAT ATA PASS-THROUGH commands (0x85, 0xA1)
// Only supports SMART read operations (SMART READ DATA and IDENTIFY DEVICE)
//
BOOLEAN ScsiHandleSatPassthrough(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR ataCommand;
    UCHAR ataFeatures;
    UCHAR ataCylLow;
    UCHAR ataCylHigh;

    // Parse and validate the SAT command
    if (!ScsiParseSatCommand(Srb, &ataCommand, &ataFeatures, &ataCylLow, &ataCylHigh)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: SAT command rejected - not a supported SMART read\n");
#endif
        return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: SAT PASS-THROUGH - ATA Cmd=0x%02X Feature=0x%02X CylLow=0x%02X CylHi=0x%02X\n",
                   ataCommand, ataFeatures, ataCylLow, ataCylHigh);
#endif

    // Check if this is SMART READ DATA or IDENTIFY DEVICE
    if (ataCommand == ATA_SMART_CMD && ataFeatures == ATA_SMART_READ_DATA) {
        // SMART READ DATA - translate to NVMe Get Log Page
        // Ensure we have buffer space for output (512 bytes)
        if (Srb->DataTransferLength < 512) {
            return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
        }

        // NVMe Get Log Page (SMART/Health) through the engine
        return EngSubmitLogPage(DevExt, Srb, NVME_LOG_PAGE_SMART_HEALTH);
    } else if (ataCommand == ATA_SMART_CMD && ataFeatures == ATA_SMART_READ_LOG) {
        // SMART READ LOG - return empty log (NVMe doesn't support ATA-style log pages)
        // Ensure we have buffer space for output (512 bytes)
        if (Srb->DataTransferLength < 512) {
            return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
        }

        // Return empty 512-byte buffer (all zeros)
        RtlZeroMemory(Srb->DataBuffer, 512);
        Srb->DataTransferLength = 512;

        return ScsiSuccess(DevExt, Srb);    
    } else if (ataCommand == ATA_IDENTIFY_DEVICE) {
        if (Srb->DataTransferLength < 512) {
            return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
        }

        NvmeToAtaIdentify(DevExt, (PATA_IDENTIFY_DEVICE_STRUCT)Srb->DataBuffer);
        return ScsiSuccess(DevExt, Srb);
    } else {
        // Unknown command (should not reach here due to validation)
        return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }
}

//
// ScsiHandleReadDefectData10 - Handle SCSI READ DEFECT DATA (10) command
//
BOOLEAN ScsiHandleReadDefectData10(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PUCHAR buffer;
    PCDB_READ_DEFECT_DATA cdb = (PCDB_READ_DEFECT_DATA)Srb->Cdb;
    ULONG allocationLength;
    UCHAR defectListFormat;
    BOOLEAN requestPList;
    BOOLEAN requestGList;

    // Parse CDB
    // Allocation length is at bytes 7-8 (big-endian)
    allocationLength = (cdb->AllocationLength[0] << 8) |
                       cdb->AllocationLength[1];

    // Defect list format byte (byte 2 for READ DEFECT DATA 10):
    // Bit 4: PLIST (request Primary defect list - factory defects)
    // Bit 3: GLIST (request Grown defect list - defects developed during use)
    // Bits 2-0: Format of defect descriptors
    requestPList = (cdb->FormatByte & 0x10) ? TRUE : FALSE;
    requestGList = (cdb->FormatByte & 0x08) ? TRUE : FALSE;
    defectListFormat = cdb->FormatByte & 0x07;

    // Check buffer size - need at least 4 bytes for header
    if (Srb->DataTransferLength < 4) {
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }

    buffer = (PUCHAR)Srb->DataBuffer;
    RtlZeroMemory(buffer, Srb->DataTransferLength);

    // Build READ DEFECT DATA (10) response header (4 bytes)
    // Per SBC-3 specification section 5.7

    // Byte 0: Reserved
    buffer[0] = 0x00;

    // Byte 1: Response flags
    // Bit 7: PS (Parameters Saveable) = 0 (not saveable)
    // Bit 4: PLIST = echo from CDB (indicates if P-list is being returned)
    // Bit 3: GLIST = echo from CDB (indicates if G-list is being returned)
    // Bits 2-0: Defect List Format = echo from CDB
    // For NVMe SSDs: We acknowledge the request but return empty lists
    buffer[1] = (cdb->FormatByte & 0x1F);  // Copy PLIST, GLIST, and format bits

    // Bytes 2-3: Defect list length (big-endian, excludes the 4-byte header)
    // For NVMe SSDs, both P-list and G-list are empty (length = 0)
    // NVMe drives handle bad blocks transparently via internal wear leveling
    buffer[2] = 0x00;
    buffer[3] = 0x00;

    // Set actual transfer length (just the 4-byte header, no defect descriptors)
    Srb->DataTransferLength = 4;
    return ScsiSuccess(DevExt, Srb);
}

//
// Changeable values (PC = 01b): nothing can be changed (MODE SELECT is not
// implemented), so the body of every page is zeros; its code and length
// stay.
//
static VOID ModePageChangeable(IN PUCHAR Page, IN ULONG Size)
{
    ULONG i;

    for (i = 2; i < Size; i++) {
        Page[i] = 0;
    }
}

//
// ScsiHandleModeSense - Handle SCSI MODE SENSE(6) and MODE SENSE(10) commands
//
// PC (CDB byte 2 bits 7:6) is the 2-bit field: 0 current, 1 changeable,
// 2 default, 3 saved; default and saved values are the current ones. The
// response is built whole and its header describes all of it; a short
// allocation returns the first bytes (SPC).
//
#define MODE_SENSE_FULL 128     // header 8 + descriptor 8 + every page (104)
// DPOFUA (device-specific parameter bit 4): READ/WRITE(10)/(16) FUA is
// honoured (the engine sets NVMe FUA on every child), so disk.sys may send it.
#define MODE_DSP_FUA_BIT 0x10

BOOLEAN ScsiHandleModeSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    PUCHAR buffer;
    ULONG allocationLength;
    UCHAR pageCode;
    UCHAR pageControl;
    BOOLEAN dbd;  // Disable Block Descriptors
    BOOLEAN isModeSense10;
    BOOLEAN changeable;
    ULONG offset;
    ULONG headerSize;
    ULONG blockDescLength;
    ULONG totalLength;
    UCHAR full[MODE_SENSE_FULL];

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ScsiHandleModeSense called DataTransferLength:%d\n", Srb->DataTransferLength);
#endif

    // The whole response is built in full[]; the caller gets its prefix.
    buffer = full;
    isModeSense10 = (Srb->Cdb[0] == SCSIOP_MODE_SENSE10);

    // Parse the CDB based on command type
    if (isModeSense10) {
        pageCode = cdb->MODE_SENSE10.PageCode;
        pageControl = cdb->MODE_SENSE10.Pc;
        dbd = cdb->MODE_SENSE10.Dbd;
        allocationLength = (cdb->MODE_SENSE10.AllocationLength[0] << 8) | cdb->MODE_SENSE10.AllocationLength[1];
        headerSize = 8;  // MODE SENSE(10) header is 8 bytes
    } else {
        pageCode = cdb->MODE_SENSE.PageCode;
        pageControl = cdb->MODE_SENSE.Pc;
        dbd = cdb->MODE_SENSE.Dbd;
        allocationLength = cdb->MODE_SENSE.AllocationLength;
        headerSize = 4;  // MODE SENSE(6) header is 4 bytes
    }

    // Check buffer size
    if (Srb->DataTransferLength < headerSize) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: ModeSense buffer too small\n");
#endif
        return ScsiError(DevExt, Srb, SRB_STATUS_DATA_OVERRUN);
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ModeSense pageCode=0x%02X pageControl=0x%02X dbd=%d\n",
                   pageCode, pageControl, dbd);
#endif

    // If unsupported page requested and not "return all", return error
    if (pageCode != MODE_SENSE_RETURN_ALL &&
        pageCode != MODE_PAGE_FORMAT_DEVICE &&
        pageCode != MODE_PAGE_CACHING &&
        pageCode != MODE_PAGE_CONTROL &&
        pageCode != MODE_PAGE_RIGID_GEOMETRY &&
        pageCode != MODE_PAGE_POWER_CONDITION &&
        pageCode != MODE_PAGE_FAULT_REPORTING) {
        return ScsiIllegalRequest(DevExt, Srb, SCSI_ADSENSE_INVALID_CDB);
    }

    RtlZeroMemory(full, sizeof(full));

    // MODE_SENSE_CHANGEABLE_VALUES (0x40) is the value of CDB byte 2, not
    // of the 2-bit Pc field: compared with Pc it never matched.
    changeable = (BOOLEAN)(pageControl == 1);

    // One block descriptor unless DBD is set. The header describes the
    // whole response, also when the allocation is shorter (SPC): a caller
    // probing with a short buffer learns the length it needs.
    blockDescLength = 0;

    offset = headerSize;
#if 1
    // Add block descriptor if not disabled
    if (!dbd && (offset + 8) <= sizeof(full)) {
        PUCHAR blockDesc = buffer + offset;
        ULONG blockSize = DevExt->NamespaceBlockSize ? DevExt->NamespaceBlockSize : 512;
        ULONGLONG numBlocks = DevExt->NamespaceSizeInBlocks;

        // Block descriptor format (8 bytes):
        // Byte 0: Density code (0 = default)
        blockDesc[0] = 0x00;

        // Bytes 1-3: Number of blocks (or 0xFFFFFF if > 24-bit)
        if (numBlocks > 0xFFFFFF) {
            blockDesc[1] = 0xFF;
            blockDesc[2] = 0xFF;
            blockDesc[3] = 0xFF;
        } else {
            blockDesc[1] = (UCHAR)((numBlocks >> 16) & 0xFF);
            blockDesc[2] = (UCHAR)((numBlocks >> 8) & 0xFF);
            blockDesc[3] = (UCHAR)(numBlocks & 0xFF);
        }

        // Byte 4: Reserved
        blockDesc[4] = 0x00;

        // Bytes 5-7: Block length (big-endian)
        blockDesc[5] = (UCHAR)((blockSize >> 16) & 0xFF);
        blockDesc[6] = (UCHAR)((blockSize >> 8) & 0xFF);
        blockDesc[7] = (UCHAR)(blockSize & 0xFF);

        // Page control applies to the mode pages; the block descriptor
        // keeps its current values for every PC.
        blockDescLength = 8;
        offset += blockDescLength;
    }
#endif
#if 1
    // Add mode pages based on pageCode; for changeable values each page
    // has a zero body (ModePageChangeable).
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_FORMAT_DEVICE) {
        // Page 03h: Format Device Page
        if ((offset + 24) <= sizeof(full)) {
            PUCHAR formatPage = buffer + offset;
            ULONG sectorsPerTrack = 63;
            ULONG blockSize = DevExt->NamespaceBlockSize ? DevExt->NamespaceBlockSize : 512;

            formatPage[0] = MODE_PAGE_FORMAT_DEVICE;  // Page code
            formatPage[1] = 22;  // Page length (n-1, total 24 bytes)

            // Bytes 2-3: Tracks per zone (big-endian, 0 = not reported)
            formatPage[2] = 0x00;
            formatPage[3] = 0x00;

            // Bytes 4-5: Alternate sectors per zone (big-endian, 0 = not reported)
            formatPage[4] = 0x00;
            formatPage[5] = 0x00;

            // Bytes 6-7: Alternate tracks per zone (big-endian, 0 = not reported)
            formatPage[6] = 0x00;
            formatPage[7] = 0x00;

            // Bytes 8-9: Alternate tracks per logical unit (big-endian, 0 = not reported)
            formatPage[8] = 0x00;
            formatPage[9] = 0x00;

            // Bytes 10-11: Sectors per track (big-endian)
            formatPage[10] = (UCHAR)((sectorsPerTrack >> 8) & 0xFF);
            formatPage[11] = (UCHAR)(sectorsPerTrack & 0xFF);

            // Bytes 12-13: Data bytes per physical sector (big-endian)
            formatPage[12] = (UCHAR)((blockSize >> 8) & 0xFF);
            formatPage[13] = (UCHAR)(blockSize & 0xFF);

            // Bytes 14-15: Interleave (big-endian, 0 = default)
            formatPage[14] = 0x00;
            formatPage[15] = 0x00;

            // Bytes 16-17: Track skew factor (big-endian, 0 = default)
            formatPage[16] = 0x00;
            formatPage[17] = 0x00;

            // Bytes 18-19: Cylinder skew factor (big-endian, 0 = default)
            formatPage[18] = 0x00;
            formatPage[19] = 0x00;

            // Byte 20: Flags
            // Bit 7: SSEC (Soft Sector) = 1 (soft sectored)
            // Bit 6: HSEC (Hard Sector) = 0
            // Bit 5: RMB (Removable) = 0
            // Bit 4: SURF (Surface) = 0
            // Bits 3-0: Reserved
            formatPage[20] = 0x80;  // SSEC=1 (soft sectored)

            // Bytes 21-23: Reserved
            formatPage[21] = 0x00;
            formatPage[22] = 0x00;
            formatPage[23] = 0x00;

            if (changeable) {
                ModePageChangeable(formatPage, 24);
            }
            offset += 24;
        }
    }
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_CACHING) {
        // Page 08h: Caching Parameters Page
        if ((offset + 20) <= sizeof(full)) {
            PUCHAR cachePage = buffer + offset;

            cachePage[0] = MODE_PAGE_CACHING;  // Page code
            cachePage[1] = 18;  // Page length (n-1, total 20 bytes)

            // Byte 2: Flags
            // IC (Initiator Control) = 0
            // ABPF (Abort Pre-Fetch) = 0
            // CAP (Caching Analysis Permitted) = 0
            // DISC (Discontinuity) = 0
            // SIZE (Size enable) = 0
            // WCE (Write Cache Enable, bit 2), PLAN "Durability" (0.11):
            //     1 only when the controller has a volatile write cache
            //     (Identify Controller VWC) that this bring-up did not
            //     verify switched off. Write-through cacheoff verified
            //     (WtFua clear) gives 0; off leaves the cache as found and
            //     fua keeps it on (every write FUA), as does a cacheoff
            //     the controller refused (FUA fallback, PLAN D3).
            // MF (Multiplication Factor) = 0
            // RCD (Read Cache Disable) = 0 (read cache enabled)
            cachePage[2] = (UCHAR)((DevExt->Vwc &&
                                    !(DevExt->WtMode == PLAT_WT_CACHEOFF && !DevExt->WtFua)) ? 0x04 : 0x00);

            // Byte 3: Read retention priority (4 bits) and Write retention priority (4 bits)
            cachePage[3] = 0x00;  // Equal priority

            // Bytes 4-5: Disable pre-fetch transfer length (big-endian)
            cachePage[4] = 0x00;
            cachePage[5] = 0x00;

            // Bytes 6-7: Minimum pre-fetch (big-endian)
            cachePage[6] = 0x00;
            cachePage[7] = 0x00;

            // Bytes 8-9: Maximum pre-fetch (big-endian)
            cachePage[8] = 0xFF;
            cachePage[9] = 0xFF;

            // Bytes 10-11: Maximum pre-fetch ceiling (big-endian)
            cachePage[10] = 0xFF;
            cachePage[11] = 0xFF;

            // Byte 12: Flags
            // FSW (Force Sequential Write) = 0
            // LBCSS (Logical Block Cache Segment Size) = 0
            // DRA (Disable Read Ahead) = 0
            // NV_DIS (Non-Volatile Cache Disable) = 0
            cachePage[12] = 0x00;

            // Byte 13: Number of cache segments
            cachePage[13] = 0x00;

            // Bytes 14-15: Cache segment size (big-endian)
            cachePage[14] = 0x00;
            cachePage[15] = 0x00;

            // Bytes 16-19: Reserved
            cachePage[16] = 0x00;
            cachePage[17] = 0x00;
            cachePage[18] = 0x00;
            cachePage[19] = 0x00;

            if (changeable) {
                ModePageChangeable(cachePage, 20);
            }
            offset += 20;
        }
    }
#endif
#if 1
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_RIGID_GEOMETRY) {
        // Page 04h: Rigid Disk Geometry Page
        if ((offset + 24) <= sizeof(full)) {
            PUCHAR geometryPage = buffer + offset;
            ULONGLONG totalCylinders;
            ULONG sectorsPerTrack = 63;
            ULONG heads = 64;
            ULONGLONG numBlocks = DevExt->NamespaceSizeInBlocks;
            ULONG blockSize = DevExt->NamespaceBlockSize ? DevExt->NamespaceBlockSize : 512;

            // Calculate number of cylinders based on: Total Blocks = Cylinders * Heads * Sectors
            // Cylinders = Total Blocks / (Heads * Sectors)
            if (numBlocks > 0) {
                totalCylinders = numBlocks / (heads * sectorsPerTrack);
                if (totalCylinders == 0) {
                    totalCylinders = 1;  // Minimum 1 cylinder
                }
            } else {
                totalCylinders = 1;
            }

            // Clamp to 24-bit max (16777215 cylinders)
            if (totalCylinders > 0xFFFFFF) {
                totalCylinders = 0xFFFFFF;
            }

            geometryPage[0] = MODE_PAGE_RIGID_GEOMETRY;  // Page code
            geometryPage[1] = 22;  // Page length (n-1, total 24 bytes)

            // Bytes 2-4: Number of cylinders (24-bit big-endian)
            geometryPage[2] = (UCHAR)((totalCylinders >> 16) & 0xFF);
            geometryPage[3] = (UCHAR)((totalCylinders >> 8) & 0xFF);
            geometryPage[4] = (UCHAR)(totalCylinders & 0xFF);

            // Byte 5: Number of heads
            geometryPage[5] = (UCHAR)heads;

            // Bytes 6-8: Starting cylinder - write precompensation (obsolete, set to 0)
            geometryPage[6] = 0x00;
            geometryPage[7] = 0x00;
            geometryPage[8] = 0x00;

            // Bytes 9-11: Starting cylinder - reduced write current (obsolete, set to 0)
            geometryPage[9] = 0x00;
            geometryPage[10] = 0x00;
            geometryPage[11] = 0x00;

            // Bytes 12-13: Device step rate (big-endian, 0 = default)
            geometryPage[12] = 0x00;
            geometryPage[13] = 0x00;

            // Bytes 14-16: Landing zone cylinder (obsolete, set to 0)
            geometryPage[14] = 0x00;
            geometryPage[15] = 0x00;
            geometryPage[16] = 0x00;

            // Byte 17: RPL (Rotational Position Locking)
            // Bits 1-0: 00b = None
            geometryPage[17] = 0x00;

            // Byte 18: Rotational offset (0 = not supported)
            geometryPage[18] = 0x00;

            // Byte 19: Reserved
            geometryPage[19] = 0x00;

            // Bytes 20-21: Medium rotation rate (big-endian)
            // 0x0000 = Not reported
            // 0x0001 = Non-rotating media (SSD)
            // 0x0002-0x0400 = Reserved
            // 0x0401-0xFFFE = Rotations per minute
            // 0xFFFF = Reserved
            // For SSDs, we should use 0x0001
            geometryPage[20] = 0x00;
            geometryPage[21] = 0x01;  // Non-rotating media (SSD)

            // Bytes 22-23: Reserved
            geometryPage[22] = 0x00;
            geometryPage[23] = 0x00;

            if (changeable) {
                ModePageChangeable(geometryPage, 24);
            }
            offset += 24;
        }
    }
#endif
#if 1
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_CONTROL) {
        // Page 0Ah: Control Mode Page
        if ((offset + 12) <= sizeof(full)) {
            PUCHAR controlPage = buffer + offset;

            controlPage[0] = MODE_PAGE_CONTROL;  // Page code
            controlPage[1] = 10;  // Page length (n-1, total 12 bytes)

            // Byte 2: TST (Task Set Type), TMF_ONLY, etc.
            controlPage[2] = 0x00;

            // Byte 3: QERR (Queue Error Management), etc.
            // Bits 1-0: QERR=00b (restricted reordering - safest default)
            // Bits 3-2: Reserved = 0
            // Bits 5-4: Queue Algorithm Modifier = 0 (restricted reordering)
            // Bits 7-6: Reserved = 0
            controlPage[3] = 0x00;  // QERR=0, all other bits=0

            // Byte 4: Flags (ATO, TAS, AUTOLOAD MODE, etc.)
            controlPage[4] = 0x00;

            // Bytes 5-6: Reserved
            controlPage[5] = 0x00;
            controlPage[6] = 0x00;

            // Bytes 7-8: Busy timeout period (big-endian)
            controlPage[7] = 0x00;
            controlPage[8] = 0x00;

            // Bytes 9-10: Extended self-test completion time (big-endian)
            controlPage[9] = 0x00;
            controlPage[10] = 0x00;

            // Byte 11: Reserved
            controlPage[11] = 0x00;

            if (changeable) {
                ModePageChangeable(controlPage, 12);
            }
            offset += 12;
        }
    }
#endif
#if 1
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_POWER_CONDITION) {
        // Page 1Ah: Power Condition Mode Page
        if ((offset + 12) <= sizeof(full)) {
            PUCHAR powerPage = buffer + offset;

            powerPage[0] = MODE_PAGE_POWER_CONDITION;  // Page code
            powerPage[1] = 10;  // Page length (n-1, total 12 bytes)

            // Byte 2: Reserved
            powerPage[2] = 0x00;

            // Byte 3: Flags (STANDBY, IDLE)
            // All power conditions disabled (NVMe manages power internally)
            powerPage[3] = 0x00;

            // Bytes 4-7: Idle condition timer (big-endian, in 100ms units)
            powerPage[4] = 0x00;
            powerPage[5] = 0x00;
            powerPage[6] = 0x00;
            powerPage[7] = 0x00;

            // Bytes 8-11: Standby condition timer (big-endian, in 100ms units)
            powerPage[8] = 0x00;
            powerPage[9] = 0x00;
            powerPage[10] = 0x00;
            powerPage[11] = 0x00;

            if (changeable) {
                ModePageChangeable(powerPage, 12);
            }
            offset += 12;
        }
    }
#endif
#if 1
    if (pageCode == MODE_SENSE_RETURN_ALL || pageCode == MODE_PAGE_FAULT_REPORTING) {
        // Page 1Ch: Informational Exceptions Control (IEC) Page
        // This page indicates SMART-like health monitoring capability
        if ((offset + 12) <= sizeof(full)) {
            PUCHAR iecPage = buffer + offset;

            iecPage[0] = MODE_PAGE_FAULT_REPORTING;  // Page code 0x1C
            iecPage[1] = 10;  // Page length (n-1, total 12 bytes)

            // Byte 2: Flags
            // Bit 7: PERF (Performance) = 0
            // Bit 6: EBF (Enable Background Function) = 0
            // Bit 5: EWASC (Enable Warning) = 0
            // Bit 4: DEXCPT (Disable Exceptions) = 0 (enabled - we support SMART)
            // Bit 3: TEST = 0
            // Bit 2: EBACKERR = 0
            // Bit 1: Reserved
            // Bit 0: LOGERR (Log Errors) = 0
            iecPage[2] = 0x00;  // DEXCPT=0 means informational exceptions enabled

            // Byte 3: MRIE (Method of Reporting Informational Exceptions)
            // 0h = No reporting of informational exception conditions
            // 2h = Generate unit attention
            // 3h = Conditionally generate recovered error
            // 4h = Unconditionally generate recovered error
            // 5h = Generate no sense
            // 6h = Only report informational exception on request (like SMART)
            iecPage[3] = 0x06;  // Report on request (LOG SENSE)

            // Bytes 4-7: Interval Timer (big-endian, in 100ms units)
            // 0 = vendor specific
            iecPage[4] = 0x00;
            iecPage[5] = 0x00;
            iecPage[6] = 0x00;
            iecPage[7] = 0x00;

            // Bytes 8-11: Report Count (big-endian)
            // Number of times to report an exception
            // 0 = unlimited
            iecPage[8] = 0x00;
            iecPage[9] = 0x00;
            iecPage[10] = 0x00;
            iecPage[11] = 0x01;  // Report once

            if (changeable) {
                ModePageChangeable(iecPage, 12);
            }
            offset += 12;
        }
    }
#endif
    // Calculate total length
    totalLength = offset;

    // Fill in the mode parameter header
    if (isModeSense10) {
        // MODE SENSE(10) header
        USHORT modeDataLength = (USHORT)(totalLength - 2);  // Length excludes the length field itself

        buffer[0] = (UCHAR)((modeDataLength >> 8) & 0xFF);
        buffer[1] = (UCHAR)(modeDataLength & 0xFF);
        buffer[2] = 0x00;  // Medium type (0 = default)
        buffer[3] = MODE_DSP_FUA_BIT;  // Device-specific parameter: DPOFUA
        buffer[4] = 0x00;  // Reserved
        buffer[5] = 0x00;  // Reserved
        buffer[6] = (UCHAR)((blockDescLength >> 8) & 0xFF);
        buffer[7] = (UCHAR)(blockDescLength & 0xFF);
    } else {
        // MODE SENSE(6) header; all pages together are far below 256 bytes
        UCHAR modeDataLength = (UCHAR)(totalLength - 1);  // Length excludes the length field itself

        buffer[0] = modeDataLength;
        buffer[1] = 0x00;  // Medium type (0 = default)
        buffer[2] = MODE_DSP_FUA_BIT;  // Device-specific parameter: DPOFUA
        buffer[3] = (UCHAR)blockDescLength;
    }

    // Update transfer length
    if (totalLength > allocationLength) {
        totalLength = allocationLength;
    }
    if (totalLength > Srb->DataTransferLength) {
        totalLength = Srb->DataTransferLength;
    }

    RtlCopyMemory(Srb->DataBuffer, full, totalLength);
    Srb->DataTransferLength = totalLength;
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: ModeSense completed, returning %d bytes\n", totalLength);
#endif
    return ScsiSuccess(DevExt, Srb);
}

//
// HandleIO_NVME2KDB - Process NVME2KDB custom IOCTLs
//
BOOLEAN HandleIO_NVME2KDB(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PSRB_IO_CONTROL srbControl;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleIO_NVME2KDB called - DataTransferLength=%u\n",
                   Srb->DataTransferLength);
#endif

    // Validate minimum size for SRB_IO_CONTROL header
    if (Srb->DataTransferLength < sizeof(SRB_IO_CONTROL)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HandleIO_NVME2KDB invalid transfer length - DataTransferLength=%u\n",
                       Srb->DataTransferLength);
#endif
        return FALSE;
    }

    srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;

    // Check signature
    if (RtlCompareMemory(srbControl->Signature, "NVME2KDB", 8) != 8) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: Invalid NVME2KDB signature\n");
#endif
        return FALSE;
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: NVME2KDB IOCTL ControlCode=0x%08X Length=%u\n",
                   srbControl->ControlCode, srbControl->Length);
#endif

    switch (srbControl->ControlCode) {
        case 0x1000:  // NVME2KDB_IOCTL_QUERY_INFO
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: NVME2KDB QUERY_INFO\n");
#endif
            srbControl->ReturnCode = 0;  // Success
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            return TRUE;

        // 0x1001 / 0x1002 were TRIM_MODE_ON / TRIM_MODE_OFF, the magic-pattern
        // TRIM (removed, PLAN 1.5): rejected like any unknown code.
        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: NVME2KDB unknown ControlCode: 0x%08X\n", srbControl->ControlCode);
#endif
            srbControl->ReturnCode = 1;  // Error
            // Don't set SRB status - let caller set SRB_STATUS_INVALID_REQUEST
            return FALSE;
    }
}

//
// HandleIO_SCSIDISK - Process SMART/ATA pass-through IOCTLs
//
// for now we dont really support any SCSIDISK IOCTLS so all return FALSE
// smartctl calls 001B0500 which seems to be from windows XP
//
// Every control code returns FALSE in the switch below (INVALID_REQUEST),
// so the ATA/SMART handling after it is not reached today. Its paths that
// return TRUE set SrbStatus themselves or admit the SRB to the engine
// (ScsiDispatch completes the SRB if neither happened).
BOOLEAN HandleIO_SCSIDISK(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PSRB_IO_CONTROL srbControl;
    PSENDCMDINPARAMS sendCmdIn;
    PSENDCMDOUTPARAMS sendCmdOut;
    PIDEREGS regs;
    ULONG dataBufferSize;
    ULONG requiredSize;
    NVME_SMART_INFO smartInfo;
    PHYSICAL_ADDRESS smartPhys;
    ULONG returnedLength;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl called - DataTransferLength=%u\n",
                   Srb->DataTransferLength);
#endif

    // Validate minimum size for SRB_IO_CONTROL header
    if (Srb->DataTransferLength < sizeof(SRB_IO_CONTROL)) {
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl invalid transfer length - DataTransferLength=%u\n",
                   Srb->DataTransferLength);
#endif
        return FALSE;
    }

    srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;

    // Check signature (Windows 2000 uses "SCSIDISK" for ATA pass-through)
    if (RtlCompareMemory(srbControl->Signature, "SCSIDISK", 8) != 8) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: Invalid SRB_IO_CONTROL signature\n");
#endif
        return FALSE;
    }

    switch (srbControl->ControlCode) {
        case IOCTL_SCSI_PASS_THROUGH:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_PASS_THROUGH\n");
#endif
            return FALSE;
        case IOCTL_SCSI_MINIPORT:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_MINIPORT\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_INQUIRY_DATA:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_INQUIRY_DATA\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_CAPABILITIES:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_CAPABILITIES\n");
#endif
            return FALSE;
        case IOCTL_SCSI_PASS_THROUGH_DIRECT:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_PASS_THROUGH_DIRECT\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_ADDRESS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_ADDRESS\n");
#endif
            return FALSE;
        case IOCTL_SCSI_RESCAN_BUS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_RESCAN_BUS\n");
#endif
            return FALSE;
        case IOCTL_SCSI_GET_DUMP_POINTERS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_GET_DUMP_POINTERS\n");
#endif
            return FALSE;
        case IOCTL_SCSI_FREE_DUMP_POINTERS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_SCSI_FREE_DUMP_POINTERS\n");
#endif
            return FALSE;
        case IOCTL_IDE_PASS_THROUGH:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK IOCTL_IDE_PASS_THROUGH\n");
#endif
            return FALSE;
        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SCSIDISK unknown IOCTL:%08X function:%03X\n", srbControl->ControlCode, (srbControl->ControlCode >> 2) & 0xFFF);
#endif
            //srbControl->ReturnCode = STATUS_INVALID_DEVICE_REQUEST;
            return FALSE; // returns SRB_STATUS_INVALID_REQUEST
    }

    // Validate that we have enough space for SENDCMDINPARAMS
    requiredSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDINPARAMS) - 1;
    if (Srb->DataTransferLength < requiredSize) {
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl called - DataTransferLength=%u required size = %u\n",
                   Srb->DataTransferLength, requiredSize);
#endif
        return FALSE;
    }

    // Get the SENDCMDINPARAMS structure (follows SRB_IO_CONTROL header)
    sendCmdIn = (PSENDCMDINPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));
    sendCmdOut = (PSENDCMDOUTPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));
    regs = &sendCmdIn->irDriveRegs;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl SMART IOCTL IO:%08X sz:%08X Cmd=0x%02X Feature=0x%02X CylLow=0x%02X CylHi=0x%02X Drive:%02X\n",
                   srbControl->ControlCode, sendCmdIn->cBufferSize, regs->bCommandReg, regs->bFeaturesReg, regs->bCylLowReg, regs->bCylHighReg, sendCmdIn->bDriveNumber);
#endif

    switch (regs->bCommandReg) {
        case ATA_IDENTIFY_DEVICE:
            // Validate SMART signature in cylinder registers
            if (regs->bCylLowReg != SMART_CYL_LOW || regs->bCylHighReg != SMART_CYL_HI) {
                // Not a SMART command, could be IDENTIFY or other ATA command
                dataBufferSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + 512 - 1;
                if (Srb->DataTransferLength < dataBufferSize) {
                    return FALSE;
                }
                NvmeToAtaIdentify(DevExt, (PATA_IDENTIFY_DEVICE_STRUCT)sendCmdOut->bBuffer);
                RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                sendCmdOut->DriverStatus.bDriverError = 0;
                sendCmdOut->DriverStatus.bIDEError = 0;
                sendCmdOut->cBufferSize = 512;
                srbControl->ReturnCode = 0;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                return TRUE;
            } else {
                return FALSE;
            }
            break;
        case ATA_SMART_CMD:
            // Process based on SMART subcommand (features register)
            switch (regs->bFeaturesReg) {
                case ATA_SMART_READ_DATA:
                    // Read SMART data / attributes - translate to NVMe Get Log Page
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART READ DATA/ATTRIBS request\n");
        #endif
                    // Ensure we have buffer space for output
                    dataBufferSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + 512 - 1;
                    if (Srb->DataTransferLength < dataBufferSize) {
                        return FALSE;
                    }

                    // NVMe Get Log Page (SMART/Health) through the engine;
                    // the engine admits or completes the SRB.
                    return EngSubmitLogPage(DevExt, Srb, NVME_LOG_PAGE_SMART_HEALTH);

                case ATA_SMART_RETURN_STATUS:
                    // Return SMART status - check if device is healthy
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART RETURN STATUS request\n");
        #endif
                    // Initialize output structure
                    RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                    sendCmdOut->cBufferSize = 0;
                    sendCmdOut->DriverStatus.bDriverError = 0;  // Success
                    sendCmdOut->DriverStatus.bIDEError = 0;     // No error

                    // In ATA SMART, threshold exceeded is indicated by:
                    // Cylinder registers: 0x2C (low) and 0xF4 (high) = FAILING
                    // Cylinder registers: 0x4F (low) and 0xC2 (high) = PASSING
                    // We'd need to read NVMe SMART critical warning to determine this
                    // For now, return passing status
                    regs->bCylLowReg = SMART_CYL_LOW;   // 0x4F = passing
                    regs->bCylHighReg = SMART_CYL_HI;   // 0xC2 = passing

                    srbControl->ReturnCode = 0;  // Success
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    return TRUE;

                case ATA_SMART_ENABLE:
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART ENABLE (NVMe always enabled)\n");
        #endif
                    // NVMe SMART is always enabled
                    RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                    sendCmdOut->DriverStatus.bDriverError = 0;
                    srbControl->ReturnCode = 0;
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    return TRUE;

                case ATA_SMART_DISABLE:
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: SMART DISABLE (NVMe cannot disable)\n");
        #endif
                    // NVMe SMART cannot be disabled, return success anyway
                    RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                    sendCmdOut->DriverStatus.bDriverError = 0;
                    srbControl->ReturnCode = 0;
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    return TRUE;

                case ATA_SMART_AUTOSAVE:
                case ATA_SMART_READ_THRESHOLDS:
                default:
        #ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: Unsupported SMART subcommand 0x%02X\n",
                                regs->bFeaturesReg);
        #endif
                    return FALSE;
            } // switch bFeatursReg
            break;

        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl unhandled - Cmd=0x%02X Feature=0x%02X CylLow=0x%02X CylHi=0x%02X\n",
                   regs->bCommandReg, regs->bFeaturesReg, regs->bCylLowReg, regs->bCylHighReg);
#endif
            return FALSE;
    } // switch bCommandReg
}
