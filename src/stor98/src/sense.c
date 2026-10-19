// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2025, Dominik Behr (nvme2k)
// Windows 9x/NT4 port and changes (nvme9x/nvment): Copyright (c) SweetLow
// Sense retention and REQUEST SENSE (stor98): Copyright (c) 2026, Yeo Kheng Meng
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
//
// Sense data (PLAN "Sense rules", R3-9), moved here from nvme2k_scsi.c so
// that the host unit test (tests/unit) can build it alone.
//
// Autosense is built only with a sense buffer of at least 18 bytes and
// SRB_FLAGS_DISABLE_AUTOSENSE clear: then exactly 18 bytes of fixed-format
// sense and SRB_STATUS_AUTOSENSE_VALID. Otherwise no byte of the buffer is
// written and the sense is kept for LU 0 (DevExt; only LU 0 is ours, NULL
// for another LU) for a following REQUEST SENSE. The Information field
// stays 0 (no failing LBA known).
//
// Ownership (review round 2). Each kept entry belongs to the request that
// failed without autosense; nothing else consumes it:
//
//   - Autosense delivered for another request leaves kept entries alone
//     (it used to clear them: two tagged requests failing in one completion
//     batch, A without autosense and B with it, left A's REQUEST SENSE with
//     NO SENSE).
//   - Several requests failing without autosense keep one entry each, in
//     completion order (up to ENG_SENSE_KEEP). REQUEST SENSE returns and
//     removes the oldest.
//   - Overflow (review round 3, N4): a failure that finds every entry in use
//     is not stored, but its place in the order is: a placeholder ("sense
//     lost", SenseLost) queues behind the kept entries. Every later failure
//     queues as a placeholder too while one is pending, because storing it
//     in a freed slot would put it ahead of the lost one and hand its sense
//     to the dropped request. A REQUEST SENSE that reaches a placeholder
//     returns NO SENSE (00/00/00), a defined result that is never another
//     request's sense. Stored entries resume once every placeholder has been
//     read. Placeholders saturate at 255 (review round 4): from then on
//     the loss state is sticky, every REQUEST SENSE past the stored entries
//     returns NO SENSE and nothing is stored again until a new start,
//     resume, acknowledged reset or new command ends the kept sense, so
//     attribution never resumes early.
//   - A new command for LU 0 other than REQUEST SENSE, INQUIRY and REPORT
//     LUNS ends every kept entry (the sense reports the previous command
//     only), as do a new start or resume (EngResetSoftState) and an
//     acknowledged reset (ScsiSenseReset).
//

#include "nvme2k.h"

// SenseLost value meaning "saturated": sticky until ScsiSenseReset or a new
// command ends the kept sense (review round 4).
#define SENSE_LOST_STICKY   0xFF

static VOID ScsiBuildSense(OUT PUCHAR Sense, IN UCHAR Key, IN UCHAR Asc, IN UCHAR Ascq)
{
    ULONG i;

    for (i = 0; i < 18; i++) {
        Sense[i] = 0;
    }
    Sense[0] = 0x70;        // current error, fixed format
    Sense[2] = Key;
    Sense[7] = 10;          // additional sense length
    Sense[12] = Asc;
    Sense[13] = Ascq;
}

VOID ScsiSetSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR Key, IN UCHAR Asc,
                  IN UCHAR Ascq)
{
    PUCHAR sense = (PUCHAR)Srb->SenseInfoBuffer;
    ULONG slot;

    Srb->SrbStatus &= ~SRB_STATUS_AUTOSENSE_VALID;
    if (sense != NULL && Srb->SenseInfoBufferLength >= 18 &&
        !(Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE)) {
        // Autosense: delivered with this request; kept entries of other
        // requests are not touched.
        ScsiBuildSense(sense, Key, Asc, Ascq);
        Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
        return;
    }
    if (DevExt == NULL) {
        return;
    }
    if (DevExt->SenseCount >= ENG_SENSE_KEEP || DevExt->SenseLost != 0) {
        // Full, or a placeholder already queued: this request's place in the
        // order is kept as a placeholder (N4), its sense is lost.
        // SENSE_LOST_STICKY (0xFF) is saturation: from then on the count of
        // placeholders is unknown, so it is never decremented again and no
        // entry is stored until the kept sense ends (review round 4).
        DevExt->SenseDropped++;
        if (DevExt->SenseLost < SENSE_LOST_STICKY) {
            DevExt->SenseLost++;
        }
        DBGSTAGE(STG_SENSE_KEPT, Key | ((ULONG)Asc << 8) | ((ULONG)Ascq << 16) | 0xFF000000UL);
        return;
    }
    slot = (DevExt->SenseHead + DevExt->SenseCount) % ENG_SENSE_KEEP;
    ScsiBuildSense(DevExt->SenseQ[slot], Key, Asc, Ascq);
    DevExt->SenseCount++;
    DBGSTAGE(STG_SENSE_KEPT, Key | ((ULONG)Asc << 8) | ((ULONG)Ascq << 16) |
             ((Srb->SenseInfoBufferLength > 0xFE ? 0xFE : Srb->SenseInfoBufferLength) << 24));
}

// Every kept entry ends (new start, resume, acknowledged reset).
VOID ScsiSenseReset(IN PHW_DEVICE_EXTENSION DevExt)
{
    DevExt->SenseCount = 0;
    DevExt->SenseHead = 0;
    DevExt->SenseLost = 0;
}

// A new command for LU 0 other than REQUEST SENSE, INQUIRY and REPORT LUNS
// ends the kept sense (it reports the previous command only).
VOID ScsiSenseNewCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    if ((DevExt->SenseCount != 0 || DevExt->SenseLost != 0) && Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
        Srb->Cdb[0] != SCSIOP_REQUEST_SENSE && Srb->Cdb[0] != SCSIOP_INQUIRY && Srb->Cdb[0] != 0xA0) {
        ScsiSenseReset(DevExt);
    }
}

//
// REQUEST SENSE (bounded): the oldest kept entry of LU 0, or NO SENSE (none
// kept, or the oldest is a placeholder for sense lost on overflow), in
// fixed format; at most the allocation length, 18 bytes and the data
// buffer. Reading an entry removes it. Descriptor format (DESC=1) is
// refused with ILLEGAL REQUEST, INVALID FIELD IN CDB.
//
BOOLEAN ScsiHandleRequestSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR data[18];
    ULONG n, i;
    ULONG kept = DevExt->SenseCount + DevExt->SenseLost;

    if (Srb->Cdb[1] & 0x01) {
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        ScsiSetSense(DevExt, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
        Srb->DataTransferLength = 0;
        return ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
    }
    if (DevExt->SenseCount != 0) {
        for (i = 0; i < 18; i++) {
            data[i] = DevExt->SenseQ[DevExt->SenseHead][i];
        }
        DevExt->SenseHead = (UCHAR)((DevExt->SenseHead + 1) % ENG_SENSE_KEEP);
        DevExt->SenseCount--;
    } else {
        // Nothing kept, or the oldest is a placeholder (sense lost on
        // overflow): NO SENSE, never a later request's entry.
        if (DevExt->SenseLost != 0 && DevExt->SenseLost != SENSE_LOST_STICKY) {
            DevExt->SenseLost--;
        }
        ScsiBuildSense(data, SCSI_SENSE_NO_SENSE, 0, 0);
    }
    n = Srb->Cdb[4];
    if (n > 18) {
        n = 18;
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
    DBGSTAGE(STG_SENSE_READ, data[2] | ((ULONG)data[12] << 8) | ((kept > 0xFF ? 0xFF : kept) << 16) | (n << 24));
    Srb->DataTransferLength = n;
    Srb->ScsiStatus = SCSISTAT_GOOD;
    return ScsiSuccess(DevExt, Srb);
}
