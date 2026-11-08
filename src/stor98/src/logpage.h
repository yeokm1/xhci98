// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// logpage.h - LOG SENSE page 2Fh (Informational Exceptions) from the NVMe
// SMART / Health Information log. Pure byte building with no port or OS
// call, so the host unit test (tests/unit/logpage_ut.c) includes it
// unchanged.
//
// Parameter 0000h is the one SPC defines, at its minimum length of 4: the
// informational exception's ASC and ASCQ (00h/00h when there is none;
// 5Dh/00h, FAILURE PREDICTION THRESHOLD EXCEEDED, while the log's Critical
// Warning is not 0), the most recent temperature in degrees Celsius (FFh
// when the log has none) and one vendor-specific byte, 0.
// Parameters 0001h to 0007h are vendor specific: the log's available spare,
// percentage used and critical warning (one byte each), then the low 8
// bytes of power-on hours, data units read and written and media errors,
// little-endian as the log holds them. Every parameter is a binary list
// (control byte 03h). The page is built whole, its page length that of the
// whole page; the caller cuts it at the allocation length and the buffer.
//

#ifndef _STOR98_LOGPAGE_H_
#define _STOR98_LOGPAGE_H_

#define LOG2F_PAGE          0x2F
#define LOG2F_MAX           75      // 4 + 8 + 3 x 5 + 4 x 12

static ULONG Log2fParam(UCHAR *Out, ULONG At, UCHAR Code, const UCHAR *Value, ULONG Length)
{
    ULONG i;

    Out[At++] = 0x00;
    Out[At++] = Code;
    Out[At++] = 0x03;                       // binary format list
    Out[At++] = (UCHAR)Length;
    for (i = 0; i < Length; i++) {
        Out[At++] = Value[i];
    }
    return At;
}

// Smart is the 512-byte SMART / Health Information log page (NVMe Get Log
// Page 02h); Out holds LOG2F_MAX bytes. Returns the page's length.
static ULONG Log2fBuild(const UCHAR *Smart, UCHAR *Out)
{
    UCHAR ie[4];
    ULONG kelvin = (ULONG)Smart[1] | ((ULONG)Smart[2] << 8);
    ULONG at;

    ie[0] = (UCHAR)(Smart[0] != 0 ? 0x5D : 0x00);
    ie[1] = 0x00;
    if (kelvin == 0) {
        ie[2] = 0xFF;                       // no reading
    } else if (kelvin <= 273) {
        ie[2] = 0;
    } else if (kelvin - 273 > 0xFE) {
        ie[2] = 0xFE;
    } else {
        ie[2] = (UCHAR)(kelvin - 273);
    }
    ie[3] = 0x00;                           // vendor specific
    Out[0] = LOG2F_PAGE;
    Out[1] = 0x00;
    at = 4;
    at = Log2fParam(Out, at, 0x00, ie, 4);
    at = Log2fParam(Out, at, 0x01, Smart + 3, 1);      // available spare
    at = Log2fParam(Out, at, 0x02, Smart + 5, 1);      // percentage used
    at = Log2fParam(Out, at, 0x03, Smart + 0, 1);      // critical warning
    at = Log2fParam(Out, at, 0x04, Smart + 128, 8);    // power-on hours
    at = Log2fParam(Out, at, 0x05, Smart + 32, 8);     // data units read
    at = Log2fParam(Out, at, 0x06, Smart + 48, 8);     // data units written
    at = Log2fParam(Out, at, 0x07, Smart + 160, 8);    // media errors
    Out[2] = (UCHAR)((at - 4) >> 8);
    Out[3] = (UCHAR)(at - 4);
    return at;
}

#endif /* _STOR98_LOGPAGE_H_ */
