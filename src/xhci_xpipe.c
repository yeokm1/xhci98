/*
 * xhci_xpipe.c - stage 2 of the transfer size, its pure half (task 36.24;
 * xhci_xpipe.h, design record 13 section 11.6). DDK-free. C89.
 */

#include "xhci_xpipe.h"

ULONG XhciXpipePlan(const USBPORT_SCATTER_GATHER_ELEMENT *el, ULONG count,
                    ULONG length, ULONG planned, ULONG mps,
                    ULONG maxElements, PXHCI_XPIPE_CURSOR cur,
                    PXHCI_XPIPE_TD td)
{
    ULONG i;
    ULONG n;
    ULONG room;
    ULONG take;
    ULONG bytes;
    ULONG rest;
    ULONG used;

    if (planned >= length) {
        return XHCI_XPIPE_DONE;
    }
    if (el == NULL || mps == 0 || maxElements == 0 ||
        maxElements > XHCI_XPIPE_TD_ELEMENTS || cur->Element >= count ||
        cur->Skip >= el[cur->Element].SgTransferLength) {
        return XHCI_XPIPE_BAD_LIST;
    }
    rest = length - planned;

    /* What up to maxElements elements from the cursor hold, no more than
     * the URB has left. */
    bytes = 0;
    n = 0;
    for (i = cur->Element; i < count && n < maxElements && bytes < rest;
         i++) {
        room = el[i].SgTransferLength;
        if (room == 0) {
            return XHCI_XPIPE_BAD_LIST;
        }
        if (i == cur->Element) {
            room -= cur->Skip;
        }
        take = (room < rest - bytes) ? room : rest - bytes;
        bytes += take;
        n++;
    }
    if (bytes < rest) {
        if (n < maxElements) {
            /* The list ended before the URB did. */
            return XHCI_XPIPE_BAD_LIST;
        }
        /* Not the last TD: end on a packet boundary. */
        bytes -= bytes % mps;
        if (bytes == 0) {
            return XHCI_XPIPE_NO_PACKET;
        }
    }

    /* The elements that many bytes touch, and the cursor past them. */
    td->First = cur->Element;
    td->FirstSkip = cur->Skip;
    td->Bytes = bytes;
    used = 0;
    n = 0;
    i = cur->Element;
    room = el[i].SgTransferLength - cur->Skip;
    for (;;) {
        n++;
        if (bytes - used < room) {
            /* Ends inside element i: the next TD starts there. */
            cur->Element = i;
            cur->Skip = el[i].SgTransferLength - room + (bytes - used);
            break;
        }
        used += room;
        i++;
        if (used == bytes) {
            cur->Element = i;
            cur->Skip = 0;
            break;
        }
        room = el[i].SgTransferLength;
    }
    td->Count = n;
    return XHCI_XPIPE_OK;
}

ULONG XhciXpipeBuild(const USBPORT_SCATTER_GATHER_ELEMENT *el, ULONG count,
                     const XHCI_XPIPE_TD *td,
                     PUSBPORT_SCATTER_GATHER_LIST out, ULONG outRoom)
{
    ULONG k;
    ULONG skip;
    ULONG left;
    ULONG len;
    const USBPORT_SCATTER_GATHER_ELEMENT *from;
    PUSBPORT_SCATTER_GATHER_ELEMENT to;

    if (td->Count == 0 || td->Count > outRoom ||
        td->Count > XHCI_XPIPE_TD_ELEMENTS || td->First >= count ||
        td->Count > count - td->First) {
        return 0;
    }
    left = td->Bytes;
    for (k = 0; k < td->Count; k++) {
        from = &el[td->First + k];
        to = &out->SgElement[k];
        skip = (k == 0) ? td->FirstSkip : 0;
        if (skip >= from->SgTransferLength) {
            return 0;
        }
        len = from->SgTransferLength - skip;
        if (len > left) {
            len = left;
        }
        if (len == 0) {
            return 0;
        }
        to->SgPhysicalAddressLo = from->SgPhysicalAddressLo + skip;
        to->SgPhysicalAddressHi = 0;
        to->SgTransferLength = len;
        to->SgOffset = td->Bytes - left;
        left -= len;
    }
    if (left != 0) {
        return 0;
    }
    out->SgElementCount = td->Count;
    return td->Count;
}
