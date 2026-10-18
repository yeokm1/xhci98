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
#ifndef SCSI64_H
#define SCSI64_H

#define SCSI_SRV_ACT_IN 0x9E  // SERVICE ACTION IN
  #define SCSI_RD_CAPAC16 0x10  // Read Capacity (16) (O)

#define SCSI_SRV_ACT_OUT  0x9F // SERVICE ACTION OUT

#define SCSIOP_READ16              0x88
#define SCSIOP_WRITE16             0x8A
#define SCSIOP_WRITE_VERIFY16      0x8E
#define SCSIOP_VERIFY16            0x8F
#define SCSIOP_SYNCHRONIZE_CACHE16 0x91

#define SENSE_RESPONSE_FIXED_CURRENT       0x70
#define SENSE_RESPONSE_FIXED_DEFERRED      0x71
#define SENSE_RESPONSE_DESCRIPTOR_CURRENT  0x72
#define SENSE_RESPONSE_DESCRIPTOR_DEFERRED 0x73


#define SENSE_DESCRIPTOR_TYPE_INFORMATION   0x00
#define SENSE_DESCRIPTOR_TYPE_COMMAND       0x01
#define SENSE_DESCRIPTOR_TYPE_SENSE_KEY     0x02
#define SENSE_DESCRIPTOR_TYPE_FRU           0x03
#define SENSE_DESCRIPTOR_TYPE_STREAM        0x04 //SSC-3+
#define SENSE_DESCRIPTOR_TYPE_BLOCK         0x05 //SBC-2+
#define SENSE_DESCRIPTOR_TYPE_OSD_OBJECT    0x06 //OSD-1+
#define SENSE_DESCRIPTOR_TYPE_OSD_INTEGRITY 0x07 //OSD-1+
#define SENSE_DESCRIPTOR_TYPE_OSD_ATTRIBUTE 0x08 //OSD-1+
#define SENSE_DESCRIPTOR_TYPE_ATA_RETURN    0x09 //SAT-1+

#include <pshpack1.h>

// READ CAPACITY(16) parameter data, full 32-byte form. Named apart from the
// DDK's 12-byte READ_CAPACITY_DATA_EX, which newer kits (WDK 7.1) declare.
typedef struct _READ_CAPACITY16_DATA {
    ULARGE_INTEGER LogicalBlockAddress;
    ULONG BytesPerBlock;
    UCHAR Flags;
    UCHAR Reserved[19];
} READ_CAPACITY16_DATA, *PREAD_CAPACITY16_DATA;

//
// Standard 10-byte CDB
//

typedef struct {
    UCHAR OperationCode;
    UCHAR RelativeAddress : 1;
    UCHAR Reserved1 : 2;
    UCHAR ForceUnitAccess : 1;
    UCHAR DisablePageOut : 1;
    UCHAR LogicalUnitNumber : 3;
    UCHAR LogicalBlockByte0;
    UCHAR LogicalBlockByte1;
    UCHAR LogicalBlockByte2;
    UCHAR LogicalBlockByte3;
    UCHAR Reserved2;
    UCHAR TransferBlocksMsb;
    UCHAR TransferBlocksLsb;
    UCHAR Control;
} CDB10, *PCDB10;

//		
// Standard 16-byte CDB
//

typedef struct {
    UCHAR OperationCode;
    UCHAR RelativeAddress : 1;
    UCHAR Reserved1 : 2;
    UCHAR ForceUnitAccess : 1;
    UCHAR DisablePageOut : 1;
    UCHAR LogicalUnitNumber : 3;
    UCHAR LogicalBlock[8];      // [0]=MSB, [7]=LSB
    UCHAR TransferLength[4];    // [0]=MSB, [3]=LSB
    UCHAR Reserved2;
    UCHAR Control;
} CDB16, *PCDB16;

typedef struct _SENSE_DATA_DESCRIPTOR {
    UCHAR ResponseCode:7;                //00
    UCHAR Reserved1:1;                   //00
    UCHAR SenseKey:4;                    //01
    UCHAR Reserved2:4;                   //01
    UCHAR AdditionalSenseCode;           //02
    UCHAR AdditionalSenseCodeQualifier;  //03
    UCHAR Reserved3[3];                  //04
    UCHAR AdditionalSenseLength;         //07
    UCHAR DescriptorsData[0];            //08
} SENSE_DATA_DESCRIPTOR, *PSENSE_DATA_DESCRIPTOR; 

typedef struct _SENSE_DESCRIPTOR {
    UCHAR DescriptorType;                //00
    UCHAR AdditionalLength;              //01
    UCHAR DescriptorData[0];             //02
} SENSE_DESCRIPTOR, *PSENSE_DESCRIPTOR; 

#include <poppack.h>

#endif // SCSI64_H
