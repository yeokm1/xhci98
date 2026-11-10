/*
 * xhci_stage.c - staging, its pure half (task 48.33; design record 13
 * section 11.11). See xhci_stage.h.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_stage.h"

ULONG XhciStagePoolSlabs(ULONG found, ULONG value)
{
    ULONG kib;

    kib = found ? value : XHCI_STAGE_POOL_KIB_DEFAULT;
    if (kib < XHCI_STAGE_POOL_KIB_MIN) {
        kib = XHCI_STAGE_POOL_KIB_MIN;
    }
    if (kib > XHCI_STAGE_POOL_KIB_MAX) {
        kib = XHCI_STAGE_POOL_KIB_MAX;
    }
    return kib / (XHCI_STAGE_SLAB / 1024UL);
}

ULONG XhciStagingOn(ULONG found, ULONG value)
{
    return (found && value != 0) ? 1UL : 0UL;
}

ULONG XhciStageCommandProof(ULONG command, ULONG staging)
{
    if ((command & 0xFFFFUL) == 0xFFFFUL) {
        return staging ? 0UL : 1UL;
    }
    return (command & XHCI_STAGE_PCI_COMMAND_BME) == 0 ? 1UL : 0UL;
}

ULONG XhciStageRuns(PUSBPORT_SCATTER_GATHER_ELEMENT el, PULONG count,
                    ULONG cap, ULONG pa, ULONG bytes, ULONG offset)
{
    ULONG n;
    ULONG need;
    ULONG piece;
    ULONG walk;
    ULONG left;

    if (bytes == 0 || bytes - 1UL > 0xFFFFFFFFUL - pa) {
        return 0;
    }
    /* Counted before anything is written: the 64 KB lines the run
     * crosses, plus one. */
    need = ((pa & 0xFFFFUL) + (bytes - 1UL)) / 0x10000UL + 1UL;
    n = *count;
    if (n > cap || need > cap - n) {
        return 0;
    }
    walk = pa;
    left = bytes;
    while (left > 0) {
        /* Modular, as xhci_xfer.c's split: at 0xFFFF0000 the next line
         * wraps to 0, and the check above keeps the walk below it. */
        piece = ((walk | 0xFFFFUL) + 1UL) - walk;
        if (piece > left) {
            piece = left;
        }
        el[n].SgPhysicalAddressLo = walk;
        el[n].SgPhysicalAddressHi = 0;
        el[n].SgTransferLength = piece;
        el[n].SgOffset = offset;
        n++;
        walk += piece;
        offset += piece;
        left -= piece;
    }
    *count = n;
    return 1;
}

ULONG XhciStageChunk(ULONG remaining, ULONG room, ULONG mps, ULONG control)
{
    if (remaining <= room) {
        return remaining;
    }
    if (control || mps == 0 || room < mps) {
        return 0;
    }
    return room - (room % mps);
}

ULONG XhciStageIsoShare(ULONG packets, ULONG payload)
{
    if (packets == 0 || payload == 0 || payload > XHCI_STAGE_SLAB) {
        return 0;
    }
    if (packets > XHCI_STAGE_ISO_SHARE_MAX / payload) {
        return XHCI_STAGE_ISO_SHARE_MAX;
    }
    return packets * payload;
}

ULONG XhciStageLedgerFits(ULONG live, ULONG kept, ULONG bytes, ULONG cap)
{
    if (live > cap || kept > cap - live) {
        return 0;
    }
    return (bytes <= cap - live - kept) ? 1UL : 0UL;
}

ULONG XhciStagePages(ULONG bytes)
{
    if (bytes > 0xFFFFFFFFUL - (XHCI_STAGE_PAGE - 1UL)) {
        return 0;
    }
    return (bytes + XHCI_STAGE_PAGE - 1UL) & ~(XHCI_STAGE_PAGE - 1UL);
}
