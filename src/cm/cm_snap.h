/*
 * cm_snap.h - the connection manager's state snapshot on the wire
 * (design record 21 section 8.4, design record 25 section 23; roadmap-usb4
 * task 49.37).
 *
 * Windows-type free, as cm_query.h is: shared by xhcicm.sys, XHCITR and the
 * host tests. The builder's interface is cm_snapb.h.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CM_SNAP_H
#define CM_SNAP_H

/* CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA46, METHOD_BUFFERED,
   FILE_ANY_ACCESS) on the query device, opened with no access rights.
   Input: none, or one ULONG, the byte offset into the image's body (the
   image less its header) at which the window starts.
   - output buffer < CM_QS_HDR_BYTES: STATUS_BUFFER_TOO_SMALL, nothing;
   - otherwise STATUS_SUCCESS: the header, then the body from the offset
     asked, as much as fits and at most CM_QS_WINDOW_MAX bytes;
     Information = CM_QS_HDR_BYTES + CM_QS_O_WIN_BYTES. An offset at or past
     the body's end gives the header alone.
   The removed-device answer is CM_Q_IOCTL's (cm_query.h). An older driver
   answers STATUS_INVALID_DEVICE_REQUEST. */
#define CM_QS_IOCTL             0x00222918UL
#define CM_QS_WINDOW_MAX        61440UL     /* 60 KiB of body a window   */
#define CM_QS_ASK_BYTES         (64UL + 61440UL)    /* the reader's buffer */
#define CM_QS_MAX_BYTES         262144UL    /* the whole image's bound   */

#define CM_QS_MAGIC             0x534D4358UL    /* 'XCMS' */
#define CM_QS_VERSION           1UL

/* The header: 16 ULONGs, little-endian. Every offset below the header is
   from the image's start (the header's first byte), so a reader that has
   put the windows' bodies after one header reads the image as one. */
#define CM_QS_HDR_BYTES         64UL
#define CM_QS_O_MAGIC           0UL
#define CM_QS_O_VERSION         4UL
#define CM_QS_O_HDR_BYTES       8UL
#define CM_QS_O_BYTES           12UL    /* the whole image, header included */
#define CM_QS_O_FLAGS           16UL    /* CM_QS_F_*                         */
#define CM_QS_O_TABLES_AT       20UL    /* the table directory               */
#define CM_QS_O_TABLES          24UL    /* its entries                       */
#define CM_QS_O_DET_BEFORE      28UL    /* the tear detector before ...      */
#define CM_QS_O_DET_AFTER       32UL    /* ... and after this window's gather */
#define CM_QS_O_EPOCH_LO        36UL    /* the start epoch, as XCMQ's        */
#define CM_QS_O_EPOCH_HI        40UL
#define CM_QS_O_DRIVER          44UL    /* the version, a byte each, high first */
#define CM_QS_O_LEVEL           48UL    /* XhciCmLogVerbosity applied        */
#define CM_QS_O_CLOCK_MS        52UL    /* the CM's millisecond clock        */
#define CM_QS_O_LAYOUT          56UL    /* sizeof (CM_NHI): which build      */
#define CM_QS_O_WIN_AT          60UL    /* this window's body offset; its
                                           length is the answer's
                                           Information less the header     */

#define CM_QS_F_TORN            0x00000001UL    /* the detectors disagreed  */
#define CM_QS_F_CUT             0x00000002UL    /* the image bound reached  */
#define CM_QS_F_STOPPING        0x00000004UL    /* a stop or removal began  */
#define CM_QS_F_U4              0x00000008UL    /* the USB4 CM runs         */
#define CM_QS_F_ICM             0x00000010UL    /* the ICM client runs      */
#define CM_QS_F_INERT           0x00000020UL    /* no client runs           */
#define CM_QS_F_NO_THREAD       0x00000040UL    /* the detector is the log's */
#define CM_QS_F_WITHHELD        0x00000080UL    /* hashes zeroed (the tool's
                                                   /nosysinfo, in its file) */

/* A table directory entry: 6 ULONGs. */
#define CM_QS_TAB_BYTES         24UL
#define CM_QS_T_NAME            0UL     /* its name's offset                 */
#define CM_QS_T_FIELDS_AT       4UL
#define CM_QS_T_FIELDS          8UL
#define CM_QS_T_RECORDS_AT      12UL
#define CM_QS_T_RECORDS         16UL
#define CM_QS_T_RECORD_WORDS    20UL

/* A field directory entry: 2 ULONGs. The info word: the width in bits 3:0
   (1, 2 or 4 bytes; 0 a hash), the flags in bits 15:8, the element count
   (words this field takes in a record) in bits 31:16. */
#define CM_QS_FLD_BYTES         8UL
#define CM_QS_FL_NAME           0UL
#define CM_QS_FL_INFO           4UL
#define CM_QS_FI_WIDTH(i)       ((i) & 0xFUL)
#define CM_QS_FI_FLAGS(i)       (((i) >> 8) & 0xFFUL)
#define CM_QS_FI_COUNT(i)       (((i) >> 16) & 0xFFFFUL)
#define CM_QS_FD_SIGNED         0x01UL
#define CM_QS_FD_HASH           0x02UL  /* one word: a 16-bit hash          */

/* Names: NUL-terminated ASCII, at most CM_QS_NAME_MAX characters. */
#define CM_QS_NAME_MAX          47UL

#endif /* CM_SNAP_H */
