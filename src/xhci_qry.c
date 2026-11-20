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

static ULONG xhqGet(const UCHAR *buf, ULONG offset)
{
    return (ULONG)buf[offset] | ((ULONG)buf[offset + 1] << 8) |
           ((ULONG)buf[offset + 2] << 16) | ((ULONG)buf[offset + 3] << 24);
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
    ULONG xportBytes;
    ULONG portBytes;
    ULONG records;
    ULONG whole;
    UCHAR head[XHQ_HDR_BYTES];
    ULONG at;
    ULONG i;
    UCHAR c;

    *info = 0;
    if (out == NULL || room < XHQ_HDR_BYTES_V1) {
        return XHQ_ANS_TOO_SMALL;
    }
    flags = 0;
    if (in->Refused) {
        flags |= XHQ_F_REFUSED;
    }
    if (in->Started) {
        flags |= XHQ_F_STARTED;
    }
    if (in->Terminal >= XHQ_TERMINAL_LATCHED) {
        flags |= XHQ_F_FAILED;
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
    /* Level 4's ring holds kernel addresses and is served as it is (the
     * owner, 2026-11-13). XHQ_F_WITHHELD stays set beside XHQ_F_ADDRESSES
     * so a reader before 3.5.0.0 says it shows no log, as it always did,
     * rather than calling level 4 off. */
    if (in->Verbosity >= 4) {
        flags |= XHQ_F_WITHHELD | XHQ_F_ADDRESSES;
    }
    if (in->Verbosity >= 2 && in->Ring != NULL && in->RingBytes != 0 &&
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
    records = (in->XportRecords < XHQ_XPORT_MAX) ? in->XportRecords
                                                 : XHQ_XPORT_MAX;
    xportBytes = records * XHQ_XPORT_RECORD_BYTES;
    portBytes = (in->Ports < XHQ_PORTS_MAX) ? in->Ports : XHQ_PORTS_MAX;
    whole = XHQ_HDR_BYTES + ctr + logBytes + pciBytes + xportBytes +
            portBytes;

    /* Built aside, so a room under the whole header gets its first
     * XHQ_HDR_BYTES_V1 bytes and nothing past the room is written. */
    for (i = 0; i < XHQ_HDR_BYTES; i++) {
        head[i] = 0;
    }
    xhqPut(head, XHQ_O_MAGIC, XHQ_MAGIC);
    xhqPut(head, XHQ_O_VERSION, XHQ_VERSION);
    xhqPut(head, XHQ_O_BYTES, whole);
    xhqPut(head, XHQ_O_FLAGS, flags);
    xhqPut(head, XHQ_O_VERBOSITY, in->Verbosity);
    xhqPut(head, XHQ_O_VERBOSITY_READ, in->VerbosityRead);
    xhqPut(head, XHQ_O_CTR_OFFSET, XHQ_HDR_BYTES);
    xhqPut(head, XHQ_O_CTR_BYTES, ctr);
    xhqPut(head, XHQ_O_LOG_OFFSET, XHQ_HDR_BYTES + ctr);
    xhqPut(head, XHQ_O_LOG_BYTES, logBytes);
    xhqPut(head, XHQ_O_PCI_OFFSET, XHQ_HDR_BYTES + ctr + logBytes);
    xhqPut(head, XHQ_O_PCI_BYTES, pciBytes);
    xhqPut(head, XHQ_O_APPENDS, in->Appends);
    xhqPut(head, XHQ_O_DROPPED, in->Dropped);
    xhqPut(head, XHQ_O_PCI_STATE, in->PciState);
    xhqPut(head, XHQ_O_FLAVOUR, in->Flavour);
    xhqPut(head, XHQ_O_DRIVER_VERSION, in->DriverVersion);
    xhqPut(head, XHQ_O_XPORT_OFFSET,
           XHQ_HDR_BYTES + ctr + logBytes + pciBytes);
    xhqPut(head, XHQ_O_XPORT_BYTES, xportBytes);
    xhqPut(head, XHQ_O_XPORT_RECORD, XHQ_XPORT_RECORD_BYTES);
    xhqPut(head, XHQ_O_PORTS_OFFSET,
           XHQ_HDR_BYTES + ctr + logBytes + pciBytes + xportBytes);
    xhqPut(head, XHQ_O_PORTS_BYTES, portBytes);
    if (room < whole) {
        at = (room < XHQ_HDR_BYTES) ? XHQ_HDR_BYTES_V1 : XHQ_HDR_BYTES;
        for (i = 0; i < at; i++) {
            out[i] = head[i];
        }
        *info = at;
        return XHQ_ANS_HEADER;
    }
    for (i = 0; i < XHQ_HDR_BYTES; i++) {
        out[i] = head[i];
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
    for (i = 0; i < xportBytes + portBytes; i++) {
        out[at++] = 0;
    }
    *info = whole;
    return XHQ_ANS_WHOLE;
}

ULONG XhciHoldsAnswer(UCHAR *out, ULONG room, ULONG started, ULONG root,
                      ULONG hub, ULONG hubPorts, const UCHAR *holds,
                      PULONG info)
{
    ULONG whole;
    ULONG i;

    *info = 0;
    if (out == NULL || room < XHH_HDR_BYTES) {
        return XHQ_ANS_TOO_SMALL;
    }
    if (root > XHH_ROOT_MAX) {
        root = XHH_ROOT_MAX;
    }
    if (hub > XHH_HUB_MAX) {
        hub = XHH_HUB_MAX;
    }
    if (holds == NULL) {
        root = 0;
        hub = 0;
    }
    whole = XHH_HDR_BYTES + root + hub;
    for (i = 0; i < XHH_HDR_BYTES; i++) {
        out[i] = 0;
    }
    xhqPut(out, XHH_O_MAGIC, XHH_MAGIC);
    xhqPut(out, XHH_O_VERSION, XHH_VERSION);
    xhqPut(out, XHH_O_BYTES, whole);
    xhqPut(out, XHH_O_FLAGS, started ? XHH_F_STARTED : 0UL);
    xhqPut(out, XHH_O_ROOT, root);
    xhqPut(out, XHH_O_HUB, hub);
    xhqPut(out, XHH_O_HUB_PORTS, hubPorts);
    if (room < whole) {
        *info = XHH_HDR_BYTES;
        return XHQ_ANS_HEADER;
    }
    for (i = 0; i < root + hub; i++) {
        out[XHH_HDR_BYTES + i] = holds[i];
    }
    *info = whole;
    return XHQ_ANS_WHOLE;
}

/* Task 50.30 step 1 (design record 17 section 4.15.8); task 50.32's
 * lifetime part and its buffer tiers (design record 25 section 27 item 4):
 * XHR_F_LIFETIME is this routine's alone, set only where the tail is
 * written. */
ULONG XhciRecoveryAnswer(UCHAR *out, ULONG room, const ULONG *head,
                         const ULONG *recs, const ULONG *counts,
                         const ULONG *life, PULONG info)
{
    ULONG kept;
    ULONG flags;
    ULONG i;
    ULONG j;

    *info = 0;
    if (out == NULL || room < XHR_HDR_BYTES) {
        return XHQ_ANS_TOO_SMALL;
    }
    for (i = 0; i < XHR_HDR_BYTES; i++) {
        out[i] = 0;
    }
    xhqPut(out, XHR_O_MAGIC, XHR_MAGIC);
    xhqPut(out, XHR_O_VERSION, XHR_VERSION);
    kept = 0;
    if (head != NULL) {
        for (i = XHR_O_FLAGS; i <= XHR_O_RECORDS; i += 4) {
            xhqPut(out, i, head[(i - XHR_O_FLAGS) / 4]);
        }
        kept = head[(XHR_O_RECORDS - XHR_O_FLAGS) / 4];
    }
    flags = xhqGet(out, XHR_O_FLAGS) & ~XHR_F_LIFETIME;
    xhqPut(out, XHR_O_FLAGS, flags);
    if (kept > XHR_REC_MAX || recs == NULL) {
        kept = recs == NULL ? 0 : XHR_REC_MAX;
    }
    xhqPut(out, XHR_O_RECORDS, kept);
    if (room < XHR_MAX_BYTES) {
        xhqPut(out, XHR_O_BYTES,
               life != NULL ? XHR_LIFE_BYTES : XHR_MAX_BYTES);
        *info = XHR_HDR_BYTES;
        return XHQ_ANS_HEADER;
    }
    xhqPut(out, XHR_O_BYTES, XHR_MAX_BYTES);
    for (i = XHR_HDR_BYTES; i < XHR_MAX_BYTES; i++) {
        out[i] = 0;
    }
    for (i = 0; i < kept; i++) {
        for (j = 0; j < XHR_REC_WORDS; j++) {
            xhqPut(out, XHR_HDR_BYTES + i * XHR_REC_BYTES + j * 4,
                   recs[i * XHR_REC_WORDS + j]);
        }
    }
    for (i = 0; counts != NULL && i < XHR_COUNTS; i++) {
        xhqPut(out, XHR_O_COUNTS + i * 4, counts[i]);
    }
    *info = XHR_MAX_BYTES;
    if (life == NULL || room < XHR_LIFE_BYTES) {
        return XHQ_ANS_WHOLE;
    }
    for (i = 0; i < XHR_LIFE_WORDS; i++) {
        xhqPut(out, XHR_O_LIFE_TIME_LO + i * 4, life[i]);
    }
    xhqPut(out, XHR_O_BYTES, XHR_LIFE_BYTES);
    xhqPut(out, XHR_O_FLAGS, flags | XHR_F_LIFETIME);
    *info = XHR_LIFE_BYTES;
    return XHQ_ANS_WHOLE;
}
