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
// building and sending NVME commands
#include "nvme2k.h"
#include "utils.h"

//
// NVMe Register Access Functions
//

ULONG NvmeReadReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset)
{
    return ScsiPortReadRegisterUlong((PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset));
}

VOID NvmeWriteReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value)
{
    ScsiPortWriteRegisterUlong((PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset), Value);
}

ULONGLONG NvmeReadReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset)
{
    ULONGLONG value;
    PULONG ptr = (PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset);
    
    value = ScsiPortReadRegisterUlong(ptr);
    value |= ((ULONGLONG)ScsiPortReadRegisterUlong(ptr + 1)) << 32;
    
    return value;
}

VOID NvmeWriteReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONGLONG Value)
{
    PULONG ptr = (PULONG)((PUCHAR)DevExt->ControllerRegisters + Offset);
    
    ScsiPortWriteRegisterUlong(ptr, (ULONG)(Value & 0xFFFFFFFF));
    ScsiPortWriteRegisterUlong(ptr + 1, (ULONG)(Value >> 32));
}

//
// NvmeRingDoorbell - Ring submission or completion queue doorbell. The
// stride was validated against the BAR before CC.EN (PLAN 0.9).
//
VOID NvmeRingDoorbell(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT QueueId, IN BOOLEAN IsSubmission, IN USHORT Value)
{
    ULONG offset;

    offset = NVME_REG_DBS + (2 * QueueId * DevExt->DoorbellStride);
    if (!IsSubmission) {
        offset += DevExt->DoorbellStride;
    }

    NvmeWriteReg32(DevExt, offset, Value);
}

//
// NvmeSubmitCommand - Submit a command to a queue. The engine keeps fewer
// commands outstanding than the queue holds, so the full check is a guard.
//
static BOOLEAN NvmeSubmitCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_QUEUE Queue, IN PNVME_COMMAND Cmd)
{
    PNVME_COMMAND sqEntry;
    USHORT nextTail;
    ULONG currentHead;

    nextTail = (USHORT)((Queue->SubmissionQueueTail + 1) & Queue->QueueSizeMask);
    currentHead = (USHORT)(Queue->SubmissionQueueHead & Queue->QueueSizeMask);
    if (nextTail == currentHead) {
        return FALSE;
    }

    sqEntry = (PNVME_COMMAND)((PUCHAR)Queue->SubmissionQueue +
                              (Queue->SubmissionQueueTail * NVME_SQ_ENTRY_SIZE));
    RtlCopyMemory(sqEntry, Cmd, sizeof(NVME_COMMAND));
    Queue->SubmissionQueueTail = nextTail;
    NvmeRingDoorbell(DevExt, Queue->QueueId, TRUE, (USHORT)(Queue->SubmissionQueueTail));
    return TRUE;
}

BOOLEAN NvmeSubmitIoCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd)
{
    return NvmeSubmitCommand(DevExt, &DevExt->IoQueue, Cmd);
}

BOOLEAN NvmeSubmitAdminCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd)
{
    return NvmeSubmitCommand(DevExt, &DevExt->AdminQueue, Cmd);
}

//
// Bring-up admin commands (CIDs without ENG_CID_FLAG), chained from the
// admin completion handler while the bring-up timer polls the admin queue.
//
BOOLEAN NvmeCreateIoCQ(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_CREATE_CQ;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_CREATE_IO_CQ;
    cmd.CDW10 = ((ULONG)(DevExt->IoQueue.QueueSize - 1) << 16) | DevExt->IoQueue.QueueId;
    // CDW11: PC=1 (bit 0), IEN=1 (bit 1) to enable interrupts, IV=0 (bits 31:16)
    cmd.CDW11 = NVME_QUEUE_PHYS_CONTIG | NVME_QUEUE_IRQ_ENABLED | (0 << 16);
    cmd.PRP1 = DevExt->IoQueue.CompletionQueuePhys.QuadPart;

    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

BOOLEAN NvmeCreateIoSQ(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_CREATE_SQ;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_CREATE_IO_SQ;
    cmd.CDW10 = ((ULONG)(DevExt->IoQueue.QueueSize - 1) << 16) | DevExt->IoQueue.QueueId;
    cmd.CDW11 = NVME_QUEUE_PHYS_CONTIG | ((ULONG)DevExt->IoQueue.QueueId << 16);  // CQID
    cmd.PRP1 = DevExt->IoQueue.SubmissionQueuePhys.QuadPart;

    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

//
// NvmeIdentifyController - Send Identify Controller command
//
BOOLEAN NvmeIdentifyController(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_IDENTIFY;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_IDENTIFY_CONTROLLER;
    cmd.NSID = 0;
    cmd.PRP1 = DevExt->UtilityBufferPhys.QuadPart;
    cmd.PRP2 = 0;
    cmd.CDW10 = NVME_CNS_CONTROLLER;

    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

//
// Volatile write cache feature (FID 06h, NVM Express Base 1.4c 5.21.1.6) for
// write-through (PLAN D3): Set with WCE=0, then Get (current value) to verify.
//
BOOLEAN NvmeVwcFeature(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Set)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));
    cmd.CDW0.Fields.Opcode = (UCHAR)(Set ? NVME_ADMIN_SET_FEATURES : NVME_ADMIN_GET_FEATURES);
    cmd.CDW0.Fields.CommandId = (USHORT)(Set ? ADMIN_CID_SET_VWC : ADMIN_CID_GET_VWC);
    cmd.NSID = 0;
    cmd.CDW10 = 0x06;               // FID 06h; Set: SV=0; Get: SEL=000b (current)
    cmd.CDW11 = 0;                  // Set: WCE=0
    return NvmeSubmitAdminCommand(DevExt, &cmd);
}

//
// NvmeIdentifyNamespace - Send Identify Namespace command (NSID 1; namespace
// selection is PLAN 1.9)
//
BOOLEAN NvmeIdentifyNamespace(IN PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_IDENTIFY;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_IDENTIFY_NAMESPACE;
    cmd.NSID = 1;
    cmd.PRP1 = DevExt->UtilityBufferPhys.QuadPart;
    cmd.PRP2 = 0;
    cmd.CDW10 = NVME_CNS_NAMESPACE;

    return NvmeSubmitAdminCommand(DevExt, &cmd);
}
