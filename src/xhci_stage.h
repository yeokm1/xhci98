/*
 * xhci_stage.h - staging, its pure half (task 48.33; design record 13
 * section 11.11): every transfer's data through common buffer the driver
 * owns, never a client page.
 *
 * What is arithmetic is here, host-tested in test\test_stage.c: the shared
 * pool's size from XhciStagePoolKiB, the scatter/gather elements of a run
 * of staging cut at every 64 KB line (xHCI 1.2c 6.4.1), a chunk's length
 * from the room it was given, an isochronous pipe's share, and the boot
 * ledger's test against its cap. hcd_stage.c is the rest.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_STAGE_H
#define XHCI_STAGE_H

#include "xhci_compat.h"
#include "xhci_usbport.h"

/* A slab, the unit of large staging (section 11.11.1). */
#define XHCI_STAGE_SLAB             0x10000UL
/* A small buffer: one a transfer record, 1 KiB-aligned in its page. */
#define XHCI_STAGE_SMALL            0x400UL
/* A page reserve. */
#define XHCI_STAGE_PAGE             0x1000UL
/* The slabs one TD may hold, and so its bytes at most (1 MiB). */
#define XHCI_STAGE_TD_SLABS         16UL
#define XHCI_STAGE_TD_BYTES         (XHCI_STAGE_TD_SLABS * XHCI_STAGE_SLAB)

/* XhciStagePoolKiB (Q-STG.2, the owner's answer (b)): absent or of another
 * type the default; a value below the minimum or above the maximum is held
 * to it; the slabs are whole 64 KiB ones. */
#define XHCI_STAGE_POOL_KIB_DEFAULT 2048UL
#define XHCI_STAGE_POOL_KIB_MIN     256UL
#define XHCI_STAGE_POOL_KIB_MAX     16384UL

/* An isochronous share at most (section 11.11.5): the 32 pages one URB's
 * single chunk could span under the map registers. */
#define XHCI_STAGE_ISO_SHARE_MAX    0x20000UL

/* The boot ledger's cap (Q-STG.3, the owner's answer (a)): 64 MiB. */
#define XHCI_STAGE_LEDGER_CAP       0x4000000UL

/* The shared pool's slabs for a value read (`found` non-zero) as `value`
 * KiB, or for none. */
ULONG XhciStagePoolSlabs(ULONG found, ULONG value);

/* XhciDmaStaging (owner, 2026-11-10: off by default): staging on (1) for
 * a DWORD read (`found` non-zero) as anything but 0; off (0) for a DWORD
 * 0, or for none. */
ULONG XhciStagingOn(ULONG found, ULONG value);

/* Whether a PCI command word read at a quiesce proves the function masters
 * nothing (1) or not (0): Bus Master Enable clear; all ones (0xFFFF) only
 * with staging off (`staging` 0), as 3.4.2.0 read it - with staging on it
 * proves nothing (design record 17 section 4.13.8). */
#define XHCI_STAGE_PCI_COMMAND_BME  0x0004UL
ULONG XhciStageCommandProof(ULONG command, ULONG staging);

/*
 * Append the elements of `bytes` of physically contiguous staging at `pa`
 * to el[*count..cap), the first at offset `offset` of the TD, each cut at
 * every 64 KB line. Returns 1 and moves *count on success; 0, nothing
 * written, when bytes is 0, the run wraps the 32-bit space or more than
 * `cap` elements would be needed.
 */
ULONG XhciStageRuns(PUSBPORT_SCATTER_GATHER_ELEMENT el, PULONG count,
                    ULONG cap, ULONG pa, ULONG bytes, ULONG offset);

/*
 * A chunk's length: `remaining` bytes of the request left, `room` bytes of
 * staging given. All of the remainder when it fits; otherwise, for a
 * transfer that may be split (`control` 0), the room cut to a whole number
 * of `mps`-byte packets (USB 2.0 5.8.3); 0 when it cannot be split, the
 * room holds no packet or mps is 0.
 */
ULONG XhciStageChunk(ULONG remaining, ULONG room, ULONG mps, ULONG control);

/* An isochronous pipe's share for each record: `packets` (its request's
 * packet bound) times `payload` (its bytes a service interval), at most
 * XHCI_STAGE_ISO_SHARE_MAX; 0 for a payload of 0 or above a slab, or a
 * packet count of 0. */
ULONG XhciStageIsoShare(ULONG packets, ULONG payload);

/* Whether `bytes` more fit beside `live` and `kept` under `cap`, the sum
 * checked against overflow. */
ULONG XhciStageLedgerFits(ULONG live, ULONG kept, ULONG bytes, ULONG cap);

/* `bytes` rounded up to whole pages, 0 for a size that would wrap. */
ULONG XhciStagePages(ULONG bytes);

#endif /* XHCI_STAGE_H */
