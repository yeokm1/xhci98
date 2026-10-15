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
//
// nvme98cm.h: the interface between xhci98's connection manager (CM) and
// nvme98 for the PCIe link state of a tunnelled NVMe function.
//
// LICENSING OF THIS FILE. This header is an interface definition, licensed
// on its own under the BSD-3-Clause licence above, independently of the code
// that implements either side of it. nvme98 (BSD-3-Clause) implements the
// receiving side; xhci98 (GPL-2.0) implements the sending side in its CM and
// may include this file as is. Including it in a GPL-2.0 work does not change
// the licence of this file, and keep it that way: this header must stay a
// permissive, separately licensed definition. Do not copy GPL-licensed code,
// structure definitions or text into it, and do not move nvme98 or xhci98
// implementation code into it. Changes to the interface are made here, in
// the nvme98 repository, and copied unchanged into xhci98.
//
// Status (nvme98 docs/PLAN.md, maintainer decision MD-6, 2026-10-15): the
// link-state table ("interface 2" below) is the adopted NT interface. It
// passed gate G3 qualification on Windows 2000 and XP x64 in the prototype
// (docs/gates/G3.md; prototype branch phase0-g3g6, commit 4e1e55d). The
// IOCTL_SCSI_MINIPORT route ("interface 1") is rejected as the carrier of
// the link-down statement: under a saturated queue it is delivered only
// after the gone deadline. It is kept here as a diagnostic query only.
// Windows 9x is designed, not prototyped: the same table, found through a
// rendezvous that follows maintainer decision MD-5 (docs/gates/G3.md,
// "Windows 9x receiving interface"). This file defines the NT rendezvous
// only.
//
// The types used (ULONG, PVOID, ULONG_PTR) are the Windows DDK base types;
// include the kit's headers before this one.
//

#ifndef _NVME98CM_H_
#define _NVME98CM_H_

//
// The statement both interfaces carry (nvme98 PLAN 0.10, evidence 2;
// MD-2): "the PCIe link to function (bus, slot) went down, the tunnel's
// PCIe path is closed and the endpoint reset has completed". The CM may
// make it only after completed quiescence, never as the teardown starts.
//

//
// Interface 2 (adopted, MD-6): link-state table.
//
// Owner and lifetime. The CM allocates one NVME98LS_TABLE in non-paged
// memory and never frees it, not even at its own unload, so a pointer taken
// by nvme98 never dangles. nvme98 registers nothing with the CM: on NT a
// miniport gets no callback before its image is unloaded after a surprise
// removal.
//
// Rendezvous (NT). The CM creates the callback object
// \Callback\Nvme98LinkState (ExCreateCallback, OBJ_PERMANENT) and registers
// one callback that, for Argument1 == NVME98LS_QUERY, stores the table
// address in *(PNVME98LS_TABLE *)Argument2. nvme98 opens the object without
// creating it, once, in HwFindAdapter (PASSIVE_LEVEL), calls
// ExNotifyCallback once, keeps the pointer and polls its entry from its own
// timer and HwStartIo. A missing object means no CM on this system, so no
// link-down statement (the gone deadline, MD-1, applies). nvme98 does not
// use the table in dump mode.
//
// Entry protocol. The CM owns every field except AckGeneration.
//  1. Tunnel established: Sequence odd; LinkGeneration++;
//     Flags = IN_USE | UP; Sequence even. Done before the OS can enumerate
//     the endpoint (before IoInvalidateDeviceRelations on the bridge).
//  2. Link down, only after the tunnel's PCIe path is closed and the
//     endpoint reset has completed: Sequence odd;
//     Flags = IN_USE | DOWN_QUIESCED; Sequence even.
//  3. Then the CM waits for AckGeneration == LinkGeneration, for at most
//     the gone deadline, and only then starts the OS removal path.
//  4. The CM never re-establishes a tunnel to an endpoint that was not
//     reset since the last tunnel (MD-2).
// nvme98 records LinkGeneration at HwFindAdapter. It applies DOWN_QUIESCED
// for that generation or a newer one (held requests completed, held DMA
// memory released, gone deadline cancelled, BusChangeDetected), then writes
// AckGeneration = LinkGeneration. DOWN without QUIESCED, or an older
// generation, is ignored. An entry read with an odd Sequence, or whose
// Sequence changed during the read, is read again later.
//
#define NVME98LS_CALLBACK_NAME      L"\\Callback\\Nvme98LinkState"
#define NVME98LS_QUERY              ((PVOID)(ULONG_PTR)0x4C38394E)  // 'N98L'
#define NVME98LS_SIGNATURE          0x4C38394E
#define NVME98LS_VERSION            1
#define NVME98LS_ENTRIES            8

#define NVME98LS_IN_USE             0x00000001
#define NVME98LS_UP                 0x00000002
#define NVME98LS_DOWN_QUIESCED      0x00000004

typedef struct _NVME98LS_ENTRY {
    volatile ULONG Sequence;        // odd while the CM is updating the entry
    ULONG BusNumber;                // PCI bus of the NVMe function (the tunnel bridge's secondary bus)
    ULONG SlotNumber;               // PCI_SLOT_NUMBER.u.AsULONG (device 4:0, function 7:5)
    volatile ULONG LinkGeneration;  // CM's count of tunnel establishments for this slot
    volatile ULONG Flags;           // NVME98LS_*
    volatile ULONG AckGeneration;   // written by nvme98 only
    ULONG Reserved[2];
} NVME98LS_ENTRY, *PNVME98LS_ENTRY;

typedef struct _NVME98LS_TABLE {
    ULONG Signature;                // NVME98LS_SIGNATURE
    ULONG Version;                  // NVME98LS_VERSION
    ULONG Count;                    // NVME98LS_ENTRIES
    ULONG Reserved;
    NVME98LS_ENTRY Entry[NVME98LS_ENTRIES];
} NVME98LS_TABLE, *PNVME98LS_TABLE;

//
// Interface 1 (diagnostic only; rejected as the statement carrier, MD-6):
// IOCTL_SCSI_MINIPORT to the SCSI port device of the nvme98 adapter
// (\Device\ScsiPortN), SRB_IO_CONTROL header first, METHOD_BUFFERED: the
// request and the reply share the buffer that follows the header.
//
// Measured limits (G3): the port routes it to LU 0 and starts it only when
// that LU has a free slot, so with the queue full of held requests it
// arrives after the gone deadline; once the disk is gone it fails with
// STATUS_DEVICE_DOES_NOT_EXIST. NVME98CM_CODE_QUERY is the supported use.
// NVME98CM_CODE_LINK_DOWN is the prototype's statement over this route and
// is not part of the adopted contract.
//
#define NVME98CM_SIGNATURE          "NVME98CM"      // SRB_IO_CONTROL.Signature (8 bytes, no NUL)
#define NVME98CM_VERSION            1

// SRB_IO_CONTROL.ControlCode
#define NVME98CM_CODE_QUERY         0x00980001      // reply only: who is this adapter
#define NVME98CM_CODE_LINK_DOWN     0x00980002      // prototype only (see above)

// Request flags
#define NVME98CM_FLAG_QUIESCED      0x00000001      // path closed and endpoint reset completed (required)

// SRB_IO_CONTROL.ReturnCode
#define NVME98CM_RC_ACCEPTED        0   // statement applied (held requests completed, memory released)
#define NVME98CM_RC_BAD_REQUEST     1   // length, version or code wrong
#define NVME98CM_RC_NOT_THIS_FUNC   2   // bus/slot do not name this adapter's function
#define NVME98CM_RC_CONTRADICTED    3   // the controller still answers with an enabled controller
#define NVME98CM_RC_NOT_QUIESCED    4   // NVME98CM_FLAG_QUIESCED missing: not evidence
#define NVME98CM_RC_ALREADY         5   // the same link generation was already applied

// Payload after SRB_IO_CONTROL (SRB_IO_CONTROL.Length = sizeof(NVME98CM_MSG)).
typedef struct _NVME98CM_MSG {
    // Request (LINK_DOWN); ignored for QUERY
    ULONG Version;          // NVME98CM_VERSION
    ULONG Flags;            // NVME98CM_FLAG_*
    ULONG BusNumber;        // PCI bus of the NVMe function (the tunnel bridge's secondary bus)
    ULONG SlotNumber;       // PCI_SLOT_NUMBER.u.AsULONG (device 4:0, function 7:5)
    ULONG LinkGeneration;   // CM's count of tunnel establishments for that slot
    // Reply (both codes)
    ULONG ReplyVersion;
    ULONG MyBusNumber;      // the adapter's own location (ConfigInfo)
    ULONG MySlotNumber;
    ULONG VendorDevice;     // device ID << 16 | vendor ID
    ULONG StateBefore;      // driver state | bring-up state << 8 (implementation-defined values)
    ULONG StateAfter;
    ULONG HeldCompleted;    // held requests completed on this statement
    ULONG PortRetired;      // held requests the port had already retired
    ULONG Csts;             // CSTS read when the statement arrived
} NVME98CM_MSG, *PNVME98CM_MSG;

#endif // _NVME98CM_H_
