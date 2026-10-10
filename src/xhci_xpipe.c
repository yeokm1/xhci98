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

ULONG XhciXpipeEligible(ULONG bulk, ULONG in, ULONG stream, ULONG chunk,
                        ULONG length, ULONG span, ULONG grant,
                        ULONG entryFree)
{
    if (!bulk) {
        return XHCI_XPIPE_ELIG_TYPE;
    }
    if (chunk >= length) {
        return XHCI_XPIPE_ELIG_ONE_CHUNK;
    }
    /* After a short packet the controller goes on to the next TD; only a
     * stream's ring is the command's alone (section 11.6, point 1). */
    if (in && !stream) {
        return XHCI_XPIPE_ELIG_DIRECTION;
    }
    if (span == 0 || span > XHCI_XPIPE_WIDE_ELEMENTS || span > grant) {
        return XHCI_XPIPE_ELIG_SPAN;
    }
    if (!entryFree) {
        return XHCI_XPIPE_ELIG_NO_ENTRY;
    }
    return XHCI_XPIPE_ELIG_OK;
}

ULONG XhciXpipePromote(ULONG baseNull, ULONG grant, ULONG enabled)
{
    if (!enabled) {
        return XHCI_XPIPE_PROMOTE_OFF;
    }
    if (grant != XHCI_XPIPE_FREE_GRANT) {
        return XHCI_XPIPE_PROMOTE_GRANT;
    }
    if (!baseNull) {
        return XHCI_XPIPE_PROMOTE_BASE;
    }
    return XHCI_XPIPE_PROMOTE;
}

ULONG XhciXpipeAppendRun(PUSBPORT_SCATTER_GATHER_ELEMENT el, PULONG count,
                         ULONG cap, ULONG pa, ULONG run, ULONG offset)
{
    ULONG n;
    ULONG need;
    ULONG piece;

    if (run == 0 || pa + run - 1UL < pa) {
        return 0;
    }
    need = ((pa & (XHCI_XPIPE_PAGE - 1UL)) + run + XHCI_XPIPE_PAGE - 1UL) /
           XHCI_XPIPE_PAGE;
    n = *count;
    if (n > cap || need > cap - n) {
        return 0;
    }
    while (run > 0) {
        piece = XHCI_XPIPE_PAGE - (pa & (XHCI_XPIPE_PAGE - 1UL));
        if (piece > run) {
            piece = run;
        }
        el[n].SgPhysicalAddressLo = pa;
        el[n].SgPhysicalAddressHi = 0;
        el[n].SgTransferLength = piece;
        el[n].SgOffset = offset;
        n++;
        pa += piece;
        offset += piece;
        run -= piece;
    }
    *count = n;
    return 1;
}

/* The bytes a list covers, from 0 and in order; ~0 when it does not tile. */
static ULONG xpipeListBytes(const USBPORT_SCATTER_GATHER_ELEMENT *el,
                            ULONG n)
{
    ULONG i;
    ULONG total;

    total = 0;
    for (i = 0; i < n; i++) {
        if (el[i].SgOffset != total || el[i].SgTransferLength == 0 ||
            el[i].SgPhysicalAddressHi != 0) {
            return 0xFFFFFFFFUL;
        }
        total += el[i].SgTransferLength;
    }
    return total;
}

ULONG XhciXpipeCheckCompare(const USBPORT_SCATTER_GATHER_ELEMENT *a,
                            ULONG na, ULONG baseANull,
                            const USBPORT_SCATTER_GATHER_ELEMENT *b,
                            ULONG nb, ULONG baseBNull, ULONG bytes)
{
    ULONG i;

    if (!baseANull || !baseBNull) {
        return XHCI_XPIPE_CHECK_BASE;
    }
    if (xpipeListBytes(a, na) != bytes || xpipeListBytes(b, nb) != bytes) {
        return XHCI_XPIPE_CHECK_SHORT;
    }
    if (na != nb) {
        return XHCI_XPIPE_CHECK_DIFFER;
    }
    for (i = 0; i < na; i++) {
        if (a[i].SgPhysicalAddressLo != b[i].SgPhysicalAddressLo ||
            a[i].SgTransferLength != b[i].SgTransferLength) {
            return XHCI_XPIPE_CHECK_DIFFER;
        }
    }
    return XHCI_XPIPE_CHECK_PASS;
}

/* ----------------------------------------------------------------------- */
/* The record machine                                                       */
/* ----------------------------------------------------------------------- */

VOID XhciXpipeRecInit(PXHCI_XPIPE_REC r, ULONG length, ULONG mapState)
{
    r->Length = length;
    r->Planned = 0;
    r->Committed = 0;
    r->Published = 0;
    r->Retired = 0;
    r->Limit = XHCI_XPIPE_NO_LIMIT;
    r->NoMore = 0;
    r->Truncating = 0;
    r->Finalized = 0;
    r->MapState = mapState;
    r->OutcomeKind = XHCI_XPIPE_OUT_NONE;
    r->OutcomeAt = 0;
    r->Outcome = 0;
    r->TdBytes[0] = 0;
    r->TdBytes[1] = 0;
}

/* The status the URB will report, offered by an event; see the header. */
static VOID xpipeOffer(PXHCI_XPIPE_REC r, ULONG kind, ULONG at, LONG status)
{
    if (r->OutcomeKind == XHCI_XPIPE_OUT_NONE ||
        (r->OutcomeKind == XHCI_XPIPE_OUT_PREFAIL &&
         kind == XHCI_XPIPE_OUT_END && at < r->OutcomeAt)) {
        r->OutcomeKind = kind;
        r->OutcomeAt = at;
        r->Outcome = status;
    }
}

/* The one finalizer: nothing of the mapping in flight, nothing on the
 * ring, and either every byte committed or no more to come. */
static ULONG xpipeFinal(PXHCI_XPIPE_REC r)
{
    if (r->Finalized) {
        return 0;
    }
    if (r->MapState != XHCI_XPIPE_MAP_ALLOCATED &&
        r->MapState != XHCI_XPIPE_MAP_REFUSED &&
        r->MapState != XHCI_XPIPE_MAP_NOT_QUEUED) {
        return 0;
    }
    if (XhciXpipeOnRing(r) != 0) {
        return 0;
    }
    if (r->Committed != r->Length && !r->NoMore) {
        return 0;
    }
    r->Finalized = 1;
    return XHCI_XPIPE_F_FINAL;
}

ULONG XhciXpipeRecPublish(PXHCI_XPIPE_REC r, ULONG bytes)
{
    ULONG slot;

    if (r->NoMore || r->Finalized || XhciXpipeOnRing(r) >= 2 ||
        bytes == 0 || bytes > r->Length - r->Planned) {
        return XHCI_XPIPE_NO_SLOT;
    }
    slot = r->Published & 1UL;
    r->TdBytes[slot] = bytes;
    r->Planned += bytes;
    r->Published++;
    return slot;
}

ULONG XhciXpipeRecRetire(PXHCI_XPIPE_REC r, ULONG slot, ULONG moved,
                         LONG status, ULONG removed)
{
    ULONG flags;
    ULONG n;
    ULONG asked;

    if (XhciXpipeOnRing(r) == 0) {
        return XHCI_XPIPE_F_ORDER;
    }
    flags = 0;
    n = r->Retired;
    if (slot != (n & 1UL)) {
        /* Ring order is the controller's (xHCI 4.10.1.1); a step that says
         * otherwise is counted, and the oldest TD is the one retired. */
        flags |= XHCI_XPIPE_F_ORDER;
    }
    asked = r->TdBytes[n & 1UL];
    if (moved > asked) {
        moved = asked;
    }
    r->Retired++;
    if (n < r->Limit) {
        r->Committed += moved;
        if (status != 0 || moved < asked) {
            r->Limit = n + 1UL;
            r->NoMore = 1;
            flags |= XHCI_XPIPE_F_SEALED;
            xpipeOffer(r, removed ? XHCI_XPIPE_OUT_STOP : XHCI_XPIPE_OUT_END,
                       n, status);
            if (!removed && XhciXpipeOnRing(r) != 0 && !r->Truncating) {
                r->Truncating = 1;
                flags |= XHCI_XPIPE_F_TRUNCATE;
            }
        } else {
            if (XhciXpipeOnRing(r) != 0) {
                flags |= XHCI_XPIPE_F_OVERLAP;
            }
            if (!r->NoMore && r->Planned < r->Length) {
                flags |= XHCI_XPIPE_F_REFILL;
            }
        }
    }
    if (r->Truncating && XhciXpipeOnRing(r) == 0) {
        r->Truncating = 0;
        flags |= XHCI_XPIPE_F_TRUNC_END;
    }
    return flags | xpipeFinal(r);
}

ULONG XhciXpipeRecStop(PXHCI_XPIPE_REC r, ULONG kind, LONG status)
{
    r->NoMore = 1;
    if (kind == XHCI_XPIPE_OUT_PREFAIL) {
        /* The TDs already published keep counting when they retire. */
        if (r->Published < r->Limit) {
            r->Limit = r->Published;
        }
    } else {
        kind = XHCI_XPIPE_OUT_STOP;
    }
    xpipeOffer(r, kind, r->Published, status);
    return xpipeFinal(r);
}

ULONG XhciXpipeRecMapState(PXHCI_XPIPE_REC r, ULONG mapState)
{
    r->MapState = mapState;
    return xpipeFinal(r);
}

LONG XhciXpipeRecStatus(const XHCI_XPIPE_REC *r, ULONG shortOk,
                        LONG shortStatus)
{
    LONG s;

    s = (r->OutcomeKind == XHCI_XPIPE_OUT_NONE) ? 0 : r->Outcome;
    if (r->OutcomeKind == XHCI_XPIPE_OUT_STOP && r->Committed == r->Length) {
        s = 0;
    }
    if (s == 0 && r->Committed < r->Length && !shortOk) {
        s = shortStatus;
    }
    return s;
}
