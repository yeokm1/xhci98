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
// handling NVME completions
#include "nvme2k.h"
#include "utils.h"
#include "nsfmt.h"

//
// NvmeCompleteLogPage - fill an SRB from a completed Get Log Page (the data
// is in the child's PRP page). Sets SrbStatus; the engine completes the SRB.
//
VOID NvmeCompleteLogPage(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN USHORT status, IN PVOID prpBuffer)
{
    UNREFERENCED_PARAMETER(DevExt);

    if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI && Srb->Cdb[0] == SCSIOP_LOG_SENSE) {
        // LOG SENSE
        if (status == NVME_SC_SUCCESS && prpBuffer) {
            PNVME_SMART_INFO nvmeSmart = (PNVME_SMART_INFO)prpBuffer;
            UCHAR pageCode = ScsiGetLogPageCodeFromSrb(Srb);
            ULONG bytesWritten = 0;

            if (NvmeLogPageToScsiLogPage(nvmeSmart, pageCode, Srb->DataBuffer,
                                         Srb->DataTransferLength, &bytesWritten)) {
                Srb->DataTransferLength = bytesWritten;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            } else {
                Srb->DataTransferLength = 0;
                Srb->SrbStatus = SRB_STATUS_ERROR;
            }
        } else {
            Srb->DataTransferLength = 0;
            Srb->SrbStatus = SRB_STATUS_ERROR;
        }
    } else if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
               (Srb->Cdb[0] == SCSIOP_ATA_PASSTHROUGH16 || Srb->Cdb[0] == SCSIOP_ATA_PASSTHROUGH12)) {
        // SAT ATA PASS-THROUGH (SMART READ DATA)
        if (status == NVME_SC_SUCCESS && prpBuffer) {
            UCHAR ataCommand;
            UCHAR ataFeatures;
            UCHAR ataCylLow;
            UCHAR ataCylHigh;

            if (!ScsiParseSatCommand(Srb, &ataCommand, &ataFeatures, &ataCylLow, &ataCylHigh)) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            } else if (ataCommand == ATA_SMART_CMD && ataFeatures == ATA_SMART_READ_DATA) {
                NvmeSmartToAtaSmart((PNVME_SMART_INFO)prpBuffer, (PATA_SMART_DATA)Srb->DataBuffer);
                Srb->DataTransferLength = 512;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            } else {
                Srb->SrbStatus = SRB_STATUS_ERROR;
            }
        } else {
            Srb->DataTransferLength = 0;
            Srb->SrbStatus = SRB_STATUS_ERROR;
        }
    } else if (Srb->Function == SRB_FUNCTION_IO_CONTROL) {
        // SMART IOCTL
        PSRB_IO_CONTROL srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;
        PSENDCMDOUTPARAMS sendCmdOut = (PSENDCMDOUTPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));

        if (status == NVME_SC_SUCCESS && prpBuffer) {
            NvmeSmartToAtaSmart((PNVME_SMART_INFO)prpBuffer, (PATA_SMART_DATA)sendCmdOut->bBuffer);
            sendCmdOut->cBufferSize = 512;
            RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
            srbControl->ReturnCode = 0;
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
        } else {
            sendCmdOut->cBufferSize = 0;
            sendCmdOut->DriverStatus.bDriverError = 1;
            sendCmdOut->DriverStatus.bIDEError = 0x04;  // Aborted
            srbControl->ReturnCode = 1;
            Srb->SrbStatus = SRB_STATUS_ERROR;
        }
    } else {
        Srb->SrbStatus = SRB_STATUS_ERROR;
    }
}

//
// Completion queues. Every entry is handed over by CID: engine CIDs
// (ENG_CID_FLAG) go to the engine, which accounts for each child exactly
// once; the bring-up chain's admin CIDs are handled here while bring-up runs.
//
// End of the admin chain: write-through for this bring-up (PLAN D3).
static VOID NvmeBringupWtDone(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Verified, IN ULONG Dw0)
{
    if (DevExt->WtMode == PLAT_WT_FUA) {
        DevExt->WtFua = TRUE;
    } else if (DevExt->WtMode == PLAT_WT_CACHEOFF) {
        // Cache off and verified, or no volatile cache at all: a write is
        // durable at its completion. Refused or not verified: FUA on every
        // write instead.
        DevExt->WtFua = (BOOLEAN)!Verified;
    } else {
        DevExt->WtFua = FALSE;
    }
    DBGSTAGE(STG_WRITE_THROUGH, 0x200 | ((ULONG)Verified << 4) | ((ULONG)DevExt->WtFua << 5) |
             ((ULONG)DevExt->Vwc << 6) | ((Dw0 & 0xFF) << 8) | ((ULONG)DevExt->VwcSetStatus << 16));
    DevExt->AdminChainDone = TRUE;
}

static VOID NvmeBringupAdminDone(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT commandId, IN USHORT status,
                                 IN ULONG dw0)
{
    PUCHAR id = (PUCHAR)DevExt->UtilityBuffer;

    DBGSTAGE(STG_ADMIN_DONE, ((ULONG)status << 16) | commandId);
    if (DevExt->State != ENG_ST_STARTING || DevExt->Bringup != ENG_BU_ADMIN || DevExt->AdminChainDone) {
        DevExt->StaleCqes++;
        DBGSTAGE(STG_ENG_STALE_CQE, commandId | (4UL << 16));
        return;
    }
    if (commandId == ADMIN_CID_SET_VWC) {
        // A refusal is not a bring-up failure: the Get decides, and a cache
        // not verified off falls back to FUA (PLAN D3).
        DevExt->VwcSetStatus = (USHORT)(status & 0x7FF);
        if (!NvmeVwcFeature(DevExt, FALSE)) {
            NvmeBringupWtDone(DevExt, FALSE, 0);
        }
        return;
    }
    if (commandId == ADMIN_CID_GET_VWC) {
        NvmeBringupWtDone(DevExt, (BOOLEAN)((status & 0x7FF) == NVME_SC_SUCCESS && DevExt->VwcSetStatus == 0 &&
                                            (dw0 & 1) == 0), dw0);
        return;
    }
    if ((status & 0x7FF) != NVME_SC_SUCCESS) {
        // The bring-up timer sees AdminError and disables the controller.
        DevExt->AdminError = commandId | ((ULONG)status << 16);
        return;
    }
    switch (commandId) {
        case ADMIN_CID_CREATE_IO_CQ:
            if (!NvmeCreateIoSQ(DevExt)) {
                DevExt->AdminError = commandId | 0xFFFF0000;
            }
            break;

        case ADMIN_CID_CREATE_IO_SQ:
            if (!NvmeIdentifyController(DevExt)) {
                DevExt->AdminError = commandId | 0xFFFF0000;
            }
            break;

        case ADMIN_CID_IDENTIFY_CONTROLLER:
            {
                PNVME_IDENTIFY_CONTROLLER ctrlData = (PNVME_IDENTIFY_CONTROLLER)DevExt->UtilityBuffer;

                RtlCopyMemory(DevExt->ControllerSerialNumber, ctrlData->SerialNumber, 20);
                DevExt->ControllerSerialNumber[20] = 0;
                RtlCopyMemory(DevExt->ControllerModelNumber, ctrlData->ModelNumber, 40);
                DevExt->ControllerModelNumber[40] = 0;
                RtlCopyMemory(DevExt->ControllerFirmwareRevision, ctrlData->FirmwareRevision, 8);
                DevExt->ControllerFirmwareRevision[8] = 0;
                DevExt->NumberOfNamespaces = ctrlData->NumberOfNamespaces;
                DevExt->Mdts = id[77];      // MDTS, byte 77
                DevExt->Vwc = (BOOLEAN)(id[525] & 1);   // VWC, byte 525 bit 0
                DevExt->SMARTEnabled = TRUE;
                if (!NvmeIdentifyNamespace(DevExt)) {
                    DevExt->AdminError = commandId | 0xFFFF0000;
                }
            }
            break;

        case ADMIN_CID_IDENTIFY_NAMESPACE:
            {
                // The block size is the one of the format FLBAS selects; a
                // namespace that cannot be represented (no size, an invalid
                // format, a block size not served, metadata, protection) is
                // refused, never served under another size (PLAN 1.1,
                // src/nsfmt.h). The refusal fails the bring-up: no disk.
                ULONGLONG nsze;
                ULONG bs, why;
                UCHAR lbads;

                why = NsDecodeFormat(id, &nsze, &bs, &lbads);
                if (why != NSF_OK) {
                    DBGSTAGE(STG_NS_REFUSED, why | ((ULONG)id[26] << 8) | ((ULONG)lbads << 16) |
                             ((ULONG)id[29] << 24));
                    DevExt->AdminError = commandId | ((0xFF00UL | why) << 16);
                    break;
                }
                DevExt->NamespaceSizeInBlocks = nsze;
                DevExt->NamespaceBlockSize = bs;
                if (DevExt->WtMode == PLAT_WT_CACHEOFF && DevExt->Vwc) {
                    // Write-through (PLAN D3): the cache is switched off in
                    // every bring-up (a reset or re-enable may restore it).
                    DevExt->VwcSetStatus = 0xFFFF;
                    if (!NvmeVwcFeature(DevExt, TRUE)) {
                        NvmeBringupWtDone(DevExt, FALSE, 0);
                    }
                } else {
                    // OFF or FUA, or no volatile write cache to switch off.
                    DevExt->VwcSetStatus = 0;
                    NvmeBringupWtDone(DevExt, (BOOLEAN)!DevExt->Vwc, 0);
                }
            }
            break;

        default:
            DevExt->StaleCqes++;
            break;
    }
}

static BOOLEAN NvmeProcessQueue(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_QUEUE Queue, IN BOOLEAN Admin)
{
    PNVME_COMPLETION cqEntry;
    BOOLEAN processed = FALSE;
    USHORT status;
    USHORT commandId;
    ULONG dw0;
    ULONG queueIndex;
    ULONG expectedPhase;
    ULONG consumed;

    if (Queue->CompletionQueue == NULL || Queue->QueueSize == 0) {
        return FALSE;
    }
    // At most one queue's worth of entries per call: a controller that keeps
    // posting (or a CQ page a misbehaving device keeps rewriting) can never
    // hold a callback here; the rest is consumed at the next interrupt or
    // tick.
    for (consumed = 0; consumed < Queue->QueueSize; consumed++) {
        queueIndex = Queue->CompletionQueueHead & Queue->QueueSizeMask;
        expectedPhase = (Queue->CompletionQueueHead >> Queue->QueueSizeBits) & 1;
        cqEntry = (PNVME_COMPLETION)((PUCHAR)Queue->CompletionQueue + (queueIndex * NVME_CQ_ENTRY_SIZE));

        if ((cqEntry->Status & 1u) != expectedPhase) {
            break;
        }
        processed = TRUE;

        // Status field without the phase tag: SC in bits 7:0, SCT in 10:8.
        status = (USHORT)((cqEntry->Status >> 1) & 0x7FFF);
        commandId = cqEntry->CID;
        dw0 = cqEntry->DW0;
        Queue->SubmissionQueueHead = cqEntry->SQHead;
        Queue->CompletionQueueHead++;

        if (commandId & ENG_CID_FLAG) {
            EngChildCompleted(DevExt, Admin, commandId, status, dw0);
        } else if (Admin) {
            NvmeBringupAdminDone(DevExt, commandId, status, dw0);
        } else {
            DevExt->StaleCqes++;
            DBGSTAGE(STG_ENG_STALE_CQE, commandId | (4UL << 16));
        }
    }

    // One doorbell write for everything consumed
    if (processed) {
        NvmeRingDoorbell(DevExt, Queue->QueueId, FALSE, (USHORT)(Queue->CompletionQueueHead & Queue->QueueSizeMask));
    }
    return processed;
}

BOOLEAN NvmeProcessAdminCompletion(IN PHW_DEVICE_EXTENSION DevExt)
{
    return NvmeProcessQueue(DevExt, &DevExt->AdminQueue, TRUE);
}

BOOLEAN NvmeProcessIoCompletion(IN PHW_DEVICE_EXTENSION DevExt)
{
    return NvmeProcessQueue(DevExt, &DevExt->IoQueue, FALSE);
}
