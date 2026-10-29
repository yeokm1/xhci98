/*
 * cm_irq.h - the pure half of the interrupt a child filter gives a
 * function hot-added behind a tunnel on Windows 2000's Standard PC HAL
 * (design record 21 section 6.2.1; roadmap-usb4.md task 44.53), kept apart
 * so the host suite (test\test_cm_irq.c) reaches it:
 *
 *   - the HAL's hardware id test (the Standard PC HAL, e_isa_up);
 *   - the wire: the PCI-to-PCI bridge swizzle from the function up to the
 *     tunnel port's own pin, and the tunnel port's Interrupt Line;
 *   - the requirements: whether an IO_RESOURCE_REQUIREMENTS_LIST holds an
 *     interrupt, and the copy with one interrupt descriptor added to each
 *     alternative list;
 *   - the start: whether a start's raw and translated CM_RESOURCE_LISTs
 *     hold the interrupt this filter added, and their copies without it.
 *
 * The lists are read and written as bytes at the DDK's offsets (below),
 * since this file includes no DDK header; cm_filt.c asserts the offsets and
 * sizes against the DDK's own structures at compile time, for x86 and
 * amd64. Multi-byte fields are little-endian, as on both targets.
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any.
 */

#ifndef CM_IRQ_H
#define CM_IRQ_H

#include "../xhci_compat.h"

/* ---- The DDK's layouts, as bytes (cm_filt.c asserts each) ---- */

/* IO_RESOURCE_REQUIREMENTS_LIST: ListSize at 0, AlternativeLists at 28,
 * the first IO_RESOURCE_LIST at 32. */
#define CM_IRQ_REQ_HDR          32UL
#define CM_IRQ_REQ_ALTS_AT      28UL
/* IO_RESOURCE_LIST: Version, Revision, Count at 4, descriptors at 8. */
#define CM_IRQ_IOL_HDR          8UL
#define CM_IRQ_IOL_COUNT_AT     4UL
/* IO_RESOURCE_DESCRIPTOR, 32 bytes: Option 0, Type 1, ShareDisposition 2,
 * Flags 4 (16 bits), u.Interrupt.MinimumVector 8, MaximumVector 12. */
#define CM_IRQ_IOD_SIZE         32UL
#define CM_IRQ_IOD_TYPE_AT      1UL
#define CM_IRQ_IOD_SHARE_AT     2UL
#define CM_IRQ_IOD_FLAGS_AT     4UL
#define CM_IRQ_IOD_MIN_AT       8UL
#define CM_IRQ_IOD_MAX_AT       12UL
/* CM_RESOURCE_LIST: Count at 0, the first CM_FULL_RESOURCE_DESCRIPTOR at
 * 4; a full descriptor: InterfaceType 0, BusNumber 4, its
 * CM_PARTIAL_RESOURCE_LIST's Version 8, Revision 10, Count 12, the partial
 * descriptors at 16. */
#define CM_IRQ_CML_HDR          4UL
#define CM_IRQ_CMF_HDR          16UL
#define CM_IRQ_CMF_COUNT_AT     12UL
/* CM_PARTIAL_RESOURCE_DESCRIPTOR (packed to 4): Type 0, ShareDisposition
 * 1, Flags 2, u at 4 (u.Interrupt.Level 4, Vector 8; a device-specific
 * descriptor's DataSize 4, its data after the descriptor). Its union holds
 * u.Interrupt.Affinity, a KAFFINITY, pointer-sized: 16 bytes on x86, 20
 * on amd64. */
#ifdef _WIN64
#define CM_IRQ_CMD_SIZE         20UL
#else
#define CM_IRQ_CMD_SIZE         16UL
#endif
#define CM_IRQ_CMD_LEVEL_AT     4UL
#define CM_IRQ_CMD_DATASIZE_AT  4UL

/* The DDK's values (cm_filt.c asserts them). */
#define CM_IRQ_TYPE_INTERRUPT   2UL     /* CmResourceTypeInterrupt        */
#define CM_IRQ_TYPE_DEVSPECIFIC 5UL     /* CmResourceTypeDeviceSpecific   */
#define CM_IRQ_SHARE_SHARED     3UL     /* CmResourceShareShared          */
#define CM_IRQ_FLAGS_LEVEL      0UL     /* CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE */

/* Bounds: a requirements list over 4 KiB is left as it came; a start list
 * of more full descriptors, or a full descriptor of more partial ones, is
 * refused. */
#define CM_IRQ_REQ_MAX_BYTES    4096UL
#define CM_IRQ_CML_MAX_FULL     8UL
#define CM_IRQ_CMF_MAX_PARTIAL  64UL

/*
 * The HAL: TRUE when the first string of `ids` (a MULTI_SZ or a plain
 * string of 16-bit characters, at most `chars` of them read) is
 * "e_isa_up", ASCII case ignored: the Standard PC HAL's hardware id (the
 * devnode Root\PCI_HAL\0000, read on the 2000 q35 base). Anything else, a
 * NULL list or an unterminated first string included, is FALSE.
 */
BOOLEAN CmIrqHalStdPc(const USHORT *ids, ULONG chars);

/*
 * The wire (section 6.2.1 step 4). `pin` is the function's Interrupt Pin;
 * devs[0] its device number, devs[1..n-1] the device numbers of the
 * bridges between it and the tunnel port, nearest first (the tunnel port's
 * own is not among them); `portPin` and `portLine` the tunnel port's
 * Interrupt Pin and Line. The function's interrupt reaches the tunnel
 * port's primary side as pin ((pin - 1 + the sum of devs) mod 4) + 1; when
 * that is the port's own pin, the port's Line is the function's IRQ.
 * Returns the Line, or 0 with *why one of:
 */
#define CM_IRQ_WHY_GIVEN        0UL
#define CM_IRQ_WHY_NO_PIN       1UL     /* the function's pin not 1 to 4  */
#define CM_IRQ_WHY_PORT_PIN     2UL     /* the port's pin not 1 to 4      */
#define CM_IRQ_WHY_PORT_LINE    3UL     /* the port's Line not 3 to 15, or
                                           8 or 13                        */
#define CM_IRQ_WHY_OTHER_PIN    4UL     /* arrives on another of its pins */
#define CM_IRQ_WHY_PATH         5UL     /* n 0 or over CM_IRQ_PATH_MAX, or
                                           a device number over 31        */
#define CM_IRQ_PATH_MAX         8UL
/* The driver's own reasons (cm_reenum.c CmRegIrqWire), beside CmIrqWire's: */
#define CM_IRQ_WHY_NO_PORT      6UL     /* no active tunnel port above it,
                                           an ancestor not located, or the
                                           ancestry over the bound        */
#define CM_IRQ_WHY_UNREAD       7UL     /* the function or a bridge not
                                           read, or the function not a
                                           present type 0 header          */
#define CM_IRQ_WHY_MOVED        8UL     /* a bridge's secondary bus is not
                                           the bus of the link below it   */
ULONG CmIrqWire(ULONG pin, const ULONG *devs, ULONG n, ULONG portPin,
                ULONG portLine, PULONG why);

/*
 * The requirements (section 6.2.1 step 2). `list` is an
 * IO_RESOURCE_REQUIREMENTS_LIST of ListSize bytes (read from the list).
 *
 *   CM_IRQ_REQ_BAD    NULL, ListSize under the header or over
 *                     CM_IRQ_REQ_MAX_BYTES, no alternative list, or the
 *                     lists' counts not summing to ListSize exactly: left;
 *   CM_IRQ_REQ_HAS    some alternative list holds an interrupt descriptor;
 *   CM_IRQ_REQ_NONE   none does: *newBytes is the size of the list with one
 *                     descriptor added to each alternative.
 */
#define CM_IRQ_REQ_BAD          0UL
#define CM_IRQ_REQ_HAS          1UL
#define CM_IRQ_REQ_NONE         2UL
ULONG CmIrqReqScan(const UCHAR *list, PULONG newBytes);

/*
 * The new list (step 5): `list` as CmIrqReqScan read it (CM_IRQ_REQ_NONE)
 * copied to `out` (`outBytes` its *newBytes), each alternative list given
 * one more descriptor after its own, zeroed whole, then Option 0, Type
 * interrupt, ShareDisposition shared, Flags level-sensitive, MinimumVector
 * and MaximumVector `line`; ListSize the new size, Count one more. Returns
 * the bytes written, or 0 (nothing written) when the list does not scan as
 * CM_IRQ_REQ_NONE or `outBytes` is not its new size.
 */
ULONG CmIrqReqAdd(const UCHAR *list, UCHAR *out, ULONG outBytes, ULONG line);

/*
 * The start (section 6.2.1, "The start"). `raw` and `tr` are a start's
 * AllocatedResources and AllocatedResourcesTranslated (either NULL);
 * `level` is IrqAdded.
 *
 *   CM_IRQ_START_NONE    the raw list is NULL or holds no interrupt
 *                        descriptor: the start goes down unchanged;
 *   CM_IRQ_START_STRIP   the raw list holds exactly one, its level `level`
 *                        (not 0), the translated list has the same shape
 *                        (the count of full descriptors and of partial
 *                        descriptors in each) and an interrupt descriptor
 *                        at the same position: *s says where;
 *   CM_IRQ_START_REFUSE  anything else: more than one interrupt, one at
 *                        another level, a translated list that is NULL,
 *                        of another shape or with no interrupt there, a
 *                        device-specific descriptor (its data's size is
 *                        not bounded here), a count over the bounds.
 */
#define CM_IRQ_START_NONE       0UL
#define CM_IRQ_START_STRIP      1UL
#define CM_IRQ_START_REFUSE     2UL
typedef struct _CM_IRQ_STRIP {
    ULONG Bytes;        /* each list's size, both the same                */
    ULONG At;           /* the descriptor's offset, in both lists         */
    ULONG CountAt;      /* its full descriptor's partial Count's offset   */
} CM_IRQ_STRIP, *PCM_IRQ_STRIP;
ULONG CmIrqStartScan(const UCHAR *raw, const UCHAR *tr, ULONG level,
                     PCM_IRQ_STRIP s);

/*
 * One list without the descriptor *s names: `in` (s->Bytes) copied to `out`
 * (s->Bytes - CM_IRQ_CMD_SIZE bytes, `outBytes`), that descriptor left out
 * and its full descriptor's Count one less. Returns the bytes written, or 0
 * when `outBytes` is not that size or *s does not fit inside `in`.
 */
ULONG CmIrqStartStrip(const UCHAR *in, UCHAR *out, ULONG outBytes,
                      const CM_IRQ_STRIP *s);

/*
 * The filter's two records (section 6.2.1, "What the driver does"):
 * PciNoIrq, whether the last successful query answer from below held no
 * interrupt, and Added, the Line this filter last added (0 for none). Only
 * a successful query answer with a well-formed list changes PciNoIrq; a
 * failed query, a stop and a filter request change neither. The driver
 * calls these on the system the gate admits only (elsewhere the requests
 * pass untouched and no record is kept).
 */
typedef struct _CM_IRQ_STATE {
    ULONG PciNoIrq;
    ULONG Added;
} CM_IRQ_STATE, *PCM_IRQ_STATE;

#define CM_IRQ_ACT_LEAVE        0UL     /* the answer goes up untouched   */
#define CM_IRQ_ACT_ADD          1UL     /* add an interrupt at *line      */
#define CM_IRQ_ACT_PASS         2UL     /* a start: down unchanged        */
#define CM_IRQ_ACT_STRIP        3UL     /* a start: down without it       */
#define CM_IRQ_ACT_FAIL         4UL     /* a start: failed                */

/*
 * A query's answer: `ok` 1 for a success carrying a list, `scan` its
 * CmIrqReqScan result, `wire` the line CmIrqWire gave (0 for none; read by
 * the caller only for a CM_IRQ_REQ_NONE answer). ADD with *line the wire
 * for an answer without an interrupt on a wire; the caller then calls
 * CmIrqQueryAdded once the new list is in place.
 */
ULONG CmIrqQueryStep(PCM_IRQ_STATE st, ULONG ok, ULONG scan, ULONG wire,
                     PULONG line);
VOID CmIrqQueryAdded(PCM_IRQ_STATE st, ULONG line);

/*
 * A filter request's answer: `ok` 1 for a success or STATUS_NOT_SUPPORTED
 * carrying a list. ADD with *line the recorded Line when both records are
 * set and the list scans CM_IRQ_REQ_NONE; LEAVE otherwise. No record
 * changes.
 */
ULONG CmIrqFilterStep(const CM_IRQ_STATE *st, ULONG ok, ULONG scan,
                      PULONG line);

/*
 * A start: PASS unless both records are set; then by CmIrqStartScan's
 * result (with level st->Added): NONE PASS, STRIP STRIP, REFUSE FAIL.
 * `scan` is read only when both records are set.
 */
BOOLEAN CmIrqStartNeedsScan(const CM_IRQ_STATE *st);
ULONG CmIrqStartStep(const CM_IRQ_STATE *st, ULONG scan);

#endif /* CM_IRQ_H */
