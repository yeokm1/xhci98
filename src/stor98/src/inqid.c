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
// inqid.c - the disk's SCSI identity from the NVMe model number.
//
// From Stor98 0.3.0.0 (docs/RENAME-STOR98.md, "Disk identity change"):
// when the model number (MN), after leading spaces, starts with a word of 1
// to 8 characters followed by more text, that word is the vendor and the
// rest, after the spaces between them, is the product, cut at 16
// characters ("QEMU NVMe Ctrl" -> "QEMU" + "NVMe Ctrl"; "Samsung SSD 970
// EVO Plus 1TB" -> "Samsung" + "SSD 970 EVO Plus"). Otherwise (one word, or
// a first word longer than 8) the vendor is the generic "NVMe" that the NVM
// Express SCSI translation reference uses and the product is MN from its
// first character. Up to 0.1.x the first 8 characters were the vendor
// whatever they were ("QEMU NVM" + "e Ctrl"). The fields are space-padded;
// MN is read only up to its NUL. Windows builds the disk's hardware IDs
// from these fields, so the change gives an existing disk a new device
// node once (docs/RENAME-STOR98.md).
//
// From 0.3.0.1 an underscore ends the first word too, whichever of space
// or '_' comes first, and the delimiters (spaces and underscores) between
// the word and the rest are skipped: "SKHynix_HFS256GD4..." -> "SKHynix" +
// "HFS256GD4...", where 0.3.0.0 gave "NVMe" + "SKHynix_HFS256GD" (seen by
// xhci98 on real hardware). A model with an underscore before its first
// space ("WD_BLACK SN770" -> "WD" + "BLACK SN770") gets a new device node
// once more.
//
// Vital product data (INQUIRY with EVPD=1): pages 00h (the supported
// pages), 80h (unit serial number) and 83h (device identification). Every
// other page is refused (InqResponse; the caller answers ILLEGAL REQUEST,
// INVALID FIELD IN CDB). The serial number is the Identify Controller SN
// as reported (20 characters, space-padded). Page 83h carries one
// designator, T10 vendor ID based (ASCII, associated with the logical
// unit): vendor "NVMe" padded to 8, then the 40-character model number
// and the 20-character serial number. The namespace's NGUID and EUI-64
// are not read; only namespace 1 is ever exposed, so the controller's
// identity is the logical unit's. Text fields are read only up to their
// NUL and space-padded. Up to 0.4.0.0 an EVPD request was answered with
// the standard data (even pages) or refused without sense (odd pages).
//

#include "nvme2k.h"
#include "inqid.h"

#define INQ_MN_MAX  40

VOID InqIdentity(IN const UCHAR *Mn, OUT UCHAR *Out)
{
    static const UCHAR generic[] = "NVMe";
    ULONG i = 0, k, word, j;

    while (i < INQ_MN_MAX && Mn[i] == ' ') {
        i++;
    }
    for (k = i; k < INQ_MN_MAX && Mn[k] != 0 && Mn[k] != ' ' && Mn[k] != '_'; k++) {
    }
    word = k - i;
    while (k < INQ_MN_MAX && (Mn[k] == ' ' || Mn[k] == '_')) {
        k++;
    }

    if (word >= 1 && word <= 8 && k < INQ_MN_MAX && Mn[k] != 0) {
        for (j = 0; j < word; j++) {
            Out[j] = Mn[i + j];
        }
        i = k;
    } else {
        for (j = 0; generic[j] != 0; j++) {
            Out[j] = generic[j];
        }
    }
    while (j < 8) {
        Out[j++] = ' ';
    }

    for (j = 0; j < 16 && i < INQ_MN_MAX && Mn[i] != 0; j++, i++) {
        Out[8 + j] = Mn[i];
    }
    while (j < 16) {
        Out[8 + j++] = ' ';
    }
}

// Copies Len characters of Src (up to its NUL), space-padded.
static VOID InqText(IN const UCHAR *Src, IN ULONG Len, OUT UCHAR *Out)
{
    ULONG j;

    for (j = 0; j < Len && Src[j] != 0; j++) {
        Out[j] = Src[j];
    }
    while (j < Len) {
        Out[j++] = ' ';
    }
}

VOID InqStandardData(IN const UCHAR *Mn, IN const UCHAR *Fr, OUT UCHAR *Out)
{
    ULONG j;

    for (j = 0; j < INQ_STD_LEN; j++) {
        Out[j] = 0;
    }
    Out[0] = 0x00;          // direct access block device
    Out[1] = 0x00;          // RMB = 0 (not removable)
    Out[2] = 0x05;          // version: SPC-3
    Out[3] = 0x02;          // response data format
    Out[4] = INQ_STD_LEN - 5;   // additional length
    Out[7] = 0x02;          // CmdQue = 1 (tagged command queuing)
    // Vendor (8) and product (16) from the model number, revision (4)
    // from the firmware revision.
    InqIdentity(Mn, &Out[8]);
    InqText(Fr, 4, &Out[32]);
}

ULONG InqVpdPage(IN UCHAR Page, IN const UCHAR *Sn, IN const UCHAR *Mn, OUT UCHAR *Out)
{
    static const UCHAR vendor[] = "NVMe    ";
    ULONG j;

    for (j = 0; j < INQ_VPD_MAX; j++) {
        Out[j] = 0;
    }
    Out[0] = 0x00;          // direct access block device
    Out[1] = Page;
    switch (Page) {
    case 0x00:
        // C9h, stor98's identity page (include/stor98pf.h), is built by
        // ScsiHandleInquiry, which knows the adapter's PCI location.
        Out[3] = 4;
        Out[4] = 0x00;
        Out[5] = 0x80;
        Out[6] = 0x83;
        Out[7] = 0xC9;
        return 4 + 4;
    case 0x80:
        Out[3] = 20;
        InqText(Sn, 20, &Out[4]);
        return 4 + 20;
    case 0x83:
        Out[3] = 4 + 68;
        Out[4] = 0x02;      // code set: ASCII
        Out[5] = 0x01;      // PIV 0, associated with the LU, T10 vendor ID based
        Out[7] = 68;        // designator length
        for (j = 0; j < 8; j++) {
            Out[8 + j] = vendor[j];
        }
        InqText(Mn, 40, &Out[16]);
        InqText(Sn, 20, &Out[56]);
        return 4 + 4 + 68;
    default:
        return 0;
    }
}

ULONG InqResponse(IN const UCHAR *Cdb, IN const UCHAR *Sn, IN const UCHAR *Mn, IN const UCHAR *Fr,
                  OUT UCHAR *Out)
{
    ULONG len, alloc;

    // EVPD is bit 0 of byte 1, the page code byte 2 (SPC-3 6.4.1); CmdDt
    // (byte 1 bit 1, obsolete) is not supported.
    if (Cdb[1] & 0x02) {
        return INQ_REFUSE;
    }
    if (Cdb[1] & 0x01) {
        len = InqVpdPage(Cdb[2], Sn, Mn, Out);
        if (len == 0) {
            return INQ_REFUSE;
        }
    } else {
        if (Cdb[2] != 0) {
            return INQ_REFUSE;
        }
        InqStandardData(Mn, Fr, Out);
        len = INQ_STD_LEN;
    }
    alloc = ((ULONG)Cdb[3] << 8) | Cdb[4];
    return alloc < len ? alloc : len;
}
