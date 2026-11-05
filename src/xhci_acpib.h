/*
 * xhci_acpib.h - the reading and the answer of xhci98.sys's firmware
 * tables request (xhci_acpi.h; design record 13 section 8.13, design
 * record 32 section 12.8.16): pure, DDK-free, host-tested by
 * test\test_tracpi.c over a modelled memory. hcd_acpi.c gives the
 * physical reader, the allocator and the clock, and answers the IOCTL.
 *
 * IRQL: whatever the host's routines allow (hcd_acpi.c: PASSIVE_LEVEL);
 * no lock.
 */

#ifndef XHCI_ACPIB_H
#define XHCI_ACPIB_H

#include "xhci_compat.h"
#include "xhci_acpi.h"

/* `bytes` of physical memory at `phys` into `out`, each byte the one
 * asked for; the walk keeps [phys, phys + bytes) inside one window of
 * XHA_MAP_MAX bytes counted from phys's page, and within its bounds
 * (below 1 MB only for the BIOS areas). FALSE when the mapping or the
 * read failed. */
typedef BOOLEAN (*XHA_READER)(PVOID ctx, ULONG phys, UCHAR *out, ULONG bytes);
typedef PVOID (*XHA_ALLOC)(PVOID ctx, ULONG bytes);
typedef VOID (*XHA_FREE)(PVOID ctx, PVOID p);
typedef ULONG (*XHA_NOWMS)(PVOID ctx);

typedef struct _XHA_HOST {
    XHA_READER Read;
    XHA_ALLOC Alloc;
    XHA_FREE Free;
    XHA_NOWMS NowMs;
    PVOID Context;
} XHA_HOST;

/*
 * The set read now (record 32 section 12.8.16): XHA_S_READ with *body (an
 * Alloc of *bytes, the caller's to Free), *count entries and *flags
 * (XHA_F_*); else the state, *outcome for XHA_S_REFUSED, nothing
 * allocated.
 */
ULONG XhaReadSet(const XHA_HOST *h, UCHAR **body, PULONG bytes,
                 PULONG count, PULONG flags, PULONG outcome);

typedef struct _XHA_IN {
    ULONG State;
    ULONG Outcome;
    ULONG Flags;            /* XHA_F_*                                    */
    ULONG SetGen;           /* the set's, 0 with none                     */
    const UCHAR *Body;      /* NULL with BodyBytes 0: none                */
    ULONG BodyBytes;
    ULONG Count;
} XHA_IN;

/* The request's answer for body offset `from`: 1 (too small) under
 * XHA_HDR_BYTES, nothing written; else 0 with the header and the part
 * from `from` that fits, at most XHA_PART_MAX (past the body, or no body:
 * the header alone, XHA_F_PAST, generation 0). *info the bytes written. */
ULONG XhaAnswer(UCHAR *out, ULONG room, const XHA_IN *in, ULONG from,
                PULONG info);

/* The DSDT's address from a FADT of `len` bytes: X_DSDT (offset 140) from
 * one of at least 148 bytes when its high dword is 0 and its low dword
 * not 0, else DSDT (offset 40) from one of at least 44. */
BOOLEAN XhaFadtDsdt(const UCHAR *fadt, ULONG len, PULONG phys);

/* Every allocation of an MCFG of `len` bytes as a half-open window
 * [base + (first << 20), base + ((last + 1) << 20)) into lo[], hi[], *n:
 * 44 bytes plus a whole number of 16-byte allocations, at most
 * XHA_MCFG_WINDOWS, each base 1 MB aligned with a high dword of 0, its
 * first bus not past its last, no sum past 4 GB. FALSE when any fails. */
BOOLEAN XhaMcfgWindows(const UCHAR *mcfg, ULONG len, PULONG lo, PULONG hi,
                       PULONG n);

/* The root pointer in `area` (bytes of it), on a 16-byte boundary, its
 * first 20 bytes summing to zero and, from revision 2, its length (36 to
 * 255, inside the area) summing to zero: its offset, or 0xFFFFFFFF. */
ULONG XhaFindRsdp(const UCHAR *area, ULONG bytes);

#endif /* XHCI_ACPIB_H */
