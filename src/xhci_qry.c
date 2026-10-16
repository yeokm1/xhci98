/*
 * xhci_qry.c - the answer of xhci98.sys's query request (xhci_qryb.h,
 * xhci_qry.h; design record 13 section 8.12, design record 25 revision
 * 12). Written for this project, in the framing of xhcicm.sys's log answer
 * (src\cm\cm_query.c CmQueryLogAnswer), which it does not change.
 *
 * IRQL: any; no lock, no DDK call.
 */

#include "xhci_qryb.h"

static VOID xhqPut(UCHAR *buf, ULONG offset, ULONG value)
{
    buf[offset] = (UCHAR)(value & 0xFFUL);
    buf[offset + 1] = (UCHAR)((value >> 8) & 0xFFUL);
    buf[offset + 2] = (UCHAR)((value >> 16) & 0xFFUL);
    buf[offset + 3] = (UCHAR)((value >> 24) & 0xFFUL);
}

ULONG XhciQryAnswer(UCHAR *out, ULONG room, const XHCI_QRY_IN *in,
                    PULONG info)
{
    ULONG flags;
    ULONG ctr;
    ULONG used;
    ULONG mask;
    ULONG start;
    ULONG skip;
    ULONG logBytes;
    ULONG pciBytes;
    ULONG whole;
    ULONG at;
    ULONG i;
    UCHAR c;

    *info = 0;
    if (out == NULL || room < XHQ_HDR_BYTES) {
        return XHQ_ANS_TOO_SMALL;
    }
    flags = 0;
    if (in->Refused) {
        flags |= XHQ_F_REFUSED;
    }
    if (in->Started) {
        flags |= XHQ_F_STARTED;
    }
    ctr = 0;
    if (in->Verbosity >= 1 && in->Counters != NULL &&
        in->CounterWords <= XHQ_CTR_MAX / 4) {
        ctr = in->CounterWords * 4;
        flags |= XHQ_F_COUNTERS;
    }
    used = 0;
    mask = 0;
    if (in->Verbosity >= 2) {
        flags |= XHQ_F_RECORDING;
        if (in->Emitted) {
            flags |= XHQ_F_EMITTED;
        }
    }
    if (in->Verbosity >= 4) {
        flags |= XHQ_F_WITHHELD;
    } else if (in->Verbosity >= 2 && in->Ring != NULL && in->RingBytes != 0 &&
               in->RingBytes <= XHQ_LOG_MAX &&
               (in->RingBytes & (in->RingBytes - 1)) == 0 &&
               in->Used <= in->RingBytes) {
        used = in->Used;
        mask = in->RingBytes - 1;
    }
    start = (in->Head - used) & mask;
    skip = 0;
    if (used != 0 && (in->Dropped != 0 || in->Emitted)) {
        /* The oldest bytes may be the tail of a record: from the next
         * record, so no value is shown without its label. */
        while (skip < used && in->Ring[(start + skip) & mask] != '\n') {
            skip++;
        }
        if (skip < used) {
            skip++;
        }
        flags |= XHQ_F_LOG_CUT;
    }
    logBytes = used - skip;
    pciBytes = (in->PciState == XHQ_PCI_READ && in->Pci != NULL)
                   ? XHQ_PCI_BYTES : 0;
    whole = XHQ_HDR_BYTES + ctr + logBytes + pciBytes;

    for (i = 0; i < XHQ_HDR_BYTES; i++) {
        out[i] = 0;
    }
    xhqPut(out, XHQ_O_MAGIC, XHQ_MAGIC);
    xhqPut(out, XHQ_O_VERSION, XHQ_VERSION);
    xhqPut(out, XHQ_O_BYTES, whole);
    xhqPut(out, XHQ_O_FLAGS, flags);
    xhqPut(out, XHQ_O_VERBOSITY, in->Verbosity);
    xhqPut(out, XHQ_O_VERBOSITY_READ, in->VerbosityRead);
    xhqPut(out, XHQ_O_CTR_OFFSET, XHQ_HDR_BYTES);
    xhqPut(out, XHQ_O_CTR_BYTES, ctr);
    xhqPut(out, XHQ_O_LOG_OFFSET, XHQ_HDR_BYTES + ctr);
    xhqPut(out, XHQ_O_LOG_BYTES, logBytes);
    xhqPut(out, XHQ_O_PCI_OFFSET, XHQ_HDR_BYTES + ctr + logBytes);
    xhqPut(out, XHQ_O_PCI_BYTES, pciBytes);
    xhqPut(out, XHQ_O_APPENDS, in->Appends);
    xhqPut(out, XHQ_O_DROPPED, in->Dropped);
    xhqPut(out, XHQ_O_PCI_STATE, in->PciState);
    xhqPut(out, XHQ_O_FLAVOUR, in->Flavour);
    if (room < whole) {
        *info = XHQ_HDR_BYTES;
        return XHQ_ANS_HEADER;
    }
    at = XHQ_HDR_BYTES;
    for (i = 0; i < ctr / 4; i++) {
        xhqPut(out, at, in->Counters[i]);
        at += 4;
    }
    for (i = 0; i < logBytes; i++) {
        c = in->Ring[(start + skip + i) & mask];
        if ((c < 0x20 || c > 0x7E) && c != '\r' && c != '\n') {
            c = '.';
        }
        out[at++] = c;
    }
    for (i = 0; i < pciBytes; i++) {
        out[at++] = in->Pci[i];
    }
    *info = whole;
    return XHQ_ANS_WHOLE;
}
