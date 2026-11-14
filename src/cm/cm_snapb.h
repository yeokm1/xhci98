/*
 * cm_snapb.h - the state snapshot's builder (cm_snap.c, pure) and its field
 * directories (cm_snapf.c, generated; cm_snapn.h, generated, kernel side).
 * Design record 21 section 8.4; roadmap-usb4 task 49.37.
 *
 * IRQL: any; the builder touches only the memory it is handed.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CM_SNAPB_H
#define CM_SNAPB_H

#include "../xhci_compat.h"
#include "cm_snap.h"

/* One field of a structure: its name, where it lies, how many bytes it
   takes (an array whole), the width of one element (1, 2 or 4; 0 a hash
   of the whole field) and CM_QS_FD_* flags. */
typedef struct _CM_QS_FIELD {
    const char *Name;
    ULONG Offset;
    ULONG Size;
    ULONG Width;
    ULONG Flags;
} CM_QS_FIELD;

#define CM_QS_F(type, member, width, flags)                                  \
    { #member, XHCI_FIELD_OFFSET(type, member),                             \
      (ULONG)sizeof(((type *)0)->member), (width), (flags) }

/* One table: Records instances of one structure, Stride bytes apart. */
typedef struct _CM_QS_TABLE {
    const char *Name;
    const CM_QS_FIELD *Fields;
    ULONG FieldCount;
    const UCHAR *Base;
    ULONG Records;
    ULONG Stride;
} CM_QS_TABLE;

/* The header's words the caller supplies (cm_snap.h CM_QS_O_*). */
typedef struct _CM_QS_IN {
    ULONG Flags;
    ULONG DetBefore;
    ULONG EpochLo;
    ULONG EpochHi;
    ULONG Driver;
    ULONG Level;
    ULONG ClockMs;
    ULONG Layout;
    ULONG Salt;
} CM_QS_IN;

/* Writes the header and the body's bytes from `bodyAt` into `out`, as many
   as `room` less the header holds and at most CM_QS_WINDOW_MAX; the
   detector after is the before value until CmQsSetAfter. Returns the bytes
   written (0 when room is under the header); *total the whole image's. */
ULONG CmQsBuild(UCHAR *out, ULONG room, ULONG bodyAt, const CM_QS_IN *in,
                const CM_QS_TABLE *tables, ULONG count, PULONG total);
/* The detector after the gather, and CM_QS_F_TORN when it moved. */
VOID CmQsSetAfter(UCHAR *out, ULONG detAfter);
/* A field's elements' words in a record (a hash is one), and a record's. */
ULONG CmQsFieldWords(const CM_QS_FIELD *f);
ULONG CmQsRecordWords(const CM_QS_FIELD *fields, ULONG count);
/* The 16-bit identity hash: FNV-1a over the salt's four bytes, then the
   field's, folded; 0 for an identity not set (every byte 0), never 0 for
   one that is. */
ULONG CmQsHash(ULONG salt, const UCHAR *p, ULONG bytes);
/* The version's four bytes, high first. */
#define CM_QS_DRIVER_OF(a, b, c, d)                                          \
    ((((ULONG)(a)) << 24) | (((ULONG)(b)) << 16) | (((ULONG)(c)) << 8) |     \
     ((ULONG)(d)))

/* The generated directories of the pure core's structures (cm_snapf.c). */
extern const CM_QS_FIELD CmQsF_TBT_U4CM[];
extern const ULONG CmQsN_TBT_U4CM;
extern const CM_QS_FIELD CmQsF_TBT_U4_PORT[];
extern const ULONG CmQsN_TBT_U4_PORT;
extern const CM_QS_FIELD CmQsF_TBT_U4_LINK[];
extern const ULONG CmQsN_TBT_U4_LINK;
extern const CM_QS_FIELD CmQsF_TBT_U4_KID[];
extern const ULONG CmQsN_TBT_U4_KID;
extern const CM_QS_FIELD CmQsF_TBT_U4_OBLIGATION[];
extern const ULONG CmQsN_TBT_U4_OBLIGATION;
extern const CM_QS_FIELD CmQsF_TBT_U4_COUNTERS[];
extern const ULONG CmQsN_TBT_U4_COUNTERS;
extern const CM_QS_FIELD CmQsF_TBT_U4_XD[];
extern const ULONG CmQsN_TBT_U4_XD;
extern const CM_QS_FIELD CmQsF_TBT_ICM[];
extern const ULONG CmQsN_TBT_ICM;
extern const CM_QS_FIELD CmQsF_TBT_ICM_DEVICE[];
extern const ULONG CmQsN_TBT_ICM_DEVICE;
extern const CM_QS_FIELD CmQsF_TBT_ICM_COUNTERS[];
extern const ULONG CmQsN_TBT_ICM_COUNTERS;
extern const CM_QS_FIELD CmQsF_TBT_RING[];
extern const ULONG CmQsN_TBT_RING;
extern const CM_QS_FIELD CmQsF_XHCI_CHAIN_BIND[];
extern const ULONG CmQsN_XHCI_CHAIN_BIND;

#endif /* CM_SNAPB_H */
