/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * inqid.h - the disk's SCSI identity (INQUIRY vendor and product) from the
 * NVMe model number. See inqid.c.
 */

#ifndef _STOR98_INQID_H_
#define _STOR98_INQID_H_

// Fills Out[0..7] (vendor identification) and Out[8..23] (product
// identification), space-padded, from Mn: the Identify Controller model
// number, at most 40 characters, NUL-terminated at or before Mn[40].
VOID InqIdentity(IN const UCHAR *Mn, OUT UCHAR *Out);

// Standard INQUIRY data (INQ_STD_LEN bytes) for model number Mn and
// firmware revision Fr (at most 8 characters, NUL-terminated at or before
// Fr[8]).
#define INQ_STD_LEN     36
VOID InqStandardData(IN const UCHAR *Mn, IN const UCHAR *Fr, OUT UCHAR *Out);

// Vital product data page Page into Out (INQ_VPD_MAX bytes, zeroed first)
// from serial number Sn (at most 20 characters, NUL-terminated at or
// before Sn[20]) and model number Mn. Returns the page's length, or 0 when
// the page is not supported.
#define INQ_VPD_MAX     76
ULONG InqVpdPage(IN UCHAR Page, IN const UCHAR *Sn, IN const UCHAR *Mn, OUT UCHAR *Out);

// The response to the INQUIRY CDB Cdb (6 bytes) into Out (INQ_RESP_MAX
// bytes): standard data or a VPD page. Returns the number of bytes to
// return, already cut at the allocation length (bytes 3-4), or INQ_REFUSE
// when the CDB asks for something not supported (CmdDt, an unsupported VPD
// page, a page code with EVPD=0): ILLEGAL REQUEST, INVALID FIELD IN CDB.
#define INQ_RESP_MAX    (INQ_VPD_MAX > INQ_STD_LEN ? INQ_VPD_MAX : INQ_STD_LEN)
#define INQ_REFUSE      0xFFFFFFFF
ULONG InqResponse(IN const UCHAR *Cdb, IN const UCHAR *Sn, IN const UCHAR *Mn, IN const UCHAR *Fr,
                  OUT UCHAR *Out);

#endif /* _STOR98_INQID_H_ */
