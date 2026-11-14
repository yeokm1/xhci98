/*
 * cm_qnet.c - the query device's networking answer, laid out (cm_qnetb.h;
 * design record 35 section 17, task 48.18). Pure: no DDK call, no lock, no
 * allocation; host-tested by test\test_cm_qnet.c.
 *
 * IRQL: any.
 */

#include "cm_qnetb.h"

static VOID cmQnPut(UCHAR *buf, ULONG offset, ULONG value)
{
    buf[offset] = (UCHAR)(value & 0xFFUL);
    buf[offset + 1] = (UCHAR)((value >> 8) & 0xFFUL);
    buf[offset + 2] = (UCHAR)((value >> 16) & 0xFFUL);
    buf[offset + 3] = (UCHAR)((value >> 24) & 0xFFUL);
}

static ULONG cmQnGet(const UCHAR *buf, ULONG offset)
{
    return (ULONG)buf[offset] | ((ULONG)buf[offset + 1] << 8) |
           ((ULONG)buf[offset + 2] << 16) | ((ULONG)buf[offset + 3] << 24);
}

/* A peer's text: printable ASCII kept, every other byte '?', at most
 * `chars`, NUL-padded over `bytes`. */
static VOID cmQnText(UCHAR *out, const char *text, ULONG bytes, ULONG chars)
{
    ULONG i;
    UCHAR c;

    for (i = 0; i < bytes; i++) {
        out[i] = 0;
    }
    if (text == NULL) {
        return;
    }
    for (i = 0; i < chars && text[i] != '\0'; i++) {
        c = (UCHAR)text[i];
        out[i] = (UCHAR)((c >= 0x20 && c <= 0x7E) ? c : '?');
    }
}

static ULONG cmQnLow16(ULONG v)
{
    return v > 0xFFFFUL ? 0xFFFFUL : v;
}

ULONG CmNetQueryBuild(UCHAR *out, ULONG room, const CM_QN_IN *in)
{
    ULONG at;
    ULONG n;
    ULONG i;
    ULONG k;
    ULONG flags;
    ULONG cbytes;
    UCHAR *e;
    const CM_QN_LINK_IN *l;

    if (out == NULL || in == NULL || room < CM_QN_MAX_BYTES) {
        return 0;
    }
    for (i = 0; i < CM_QN_MAX_BYTES; i++) {
        out[i] = 0;
    }
    flags = in->Flags & (CM_QN_F_PDO | CM_QN_F_OPEN | CM_QN_F_UP | CM_QN_F_MAC);
    n = in->Links != NULL ? in->LinkCount : 0;
    if (n > CM_QN_MAX_LINKS) {
        n = CM_QN_MAX_LINKS;
        flags |= CM_QN_F_LINKS_CUT;
    }

    cmQnPut(out, CM_QN_O_MAGIC, CM_QN_MAGIC);
    cmQnPut(out, CM_QN_O_VERSION, CM_QN_VERSION);
    cmQnPut(out, CM_QN_O_EPOCH_LO, in->EpochLo);
    cmQnPut(out, CM_QN_O_EPOCH_HI, in->EpochHi);
    cmQnPut(out, CM_QN_O_PASS_AGE_MS, CM_QN_UNKNOWN);
    cmQnPut(out, CM_QN_O_NET_VALUE, in->NetValue);
    cmQnPut(out, CM_QN_O_E2E_VALUE, in->E2EValue);
    cmQnPut(out, CM_QN_O_CM, in->Cm);

    /* The service block. */
    at = CM_QN_HDR_BYTES;
    e = out + at;
    cmQnPut(out, CM_QN_O_SVC_OFFSET, at);
    cmQnPut(out, CM_QN_O_SVC_BYTES, CM_QN_SVC_BYTES);
    cmQnPut(e, CM_QN_S_STATE, in->State);
    cmQnPut(e, CM_QN_S_LINK, in->Link);
    for (i = 0; i < 6; i++) {
        e[CM_QN_S_MAC + i] = in->Mac[i];
        e[CM_QN_S_PEER_MAC + i] = in->PeerMac[i];
    }
    cmQnPut(e, CM_QN_S_PEER_STNS, in->PeerStns);
    cmQnPut(e, CM_QN_S_OUR_STNS, in->OurStns);
    cmQnPut(e, CM_QN_S_E2E, in->E2E);
    cmQnPut(e, CM_QN_S_MBPS, in->Mbps);
    cmQnPut(e, CM_QN_S_LANES, in->Lanes);
    cmQnPut(e, CM_QN_S_MAX_FRAME, in->MaxFrame);
    cmQnPut(e, CM_QN_S_TX_DESC, in->TxDesc);
    cmQnPut(e, CM_QN_S_RX_DESC, in->RxDesc);
    cmQnPut(e, CM_QN_S_GENERATION, in->Generation);
    cmQnPut(e, CM_QN_S_RX_FRAMES, in->RxFrames);
    cmQnPut(e, CM_QN_S_RX_ERRORS, in->RxErrors);
    cmQnPut(e, CM_QN_S_RX_DROPPED, in->RxDropped);
    cmQnPut(e, CM_QN_S_TX_FRAMES, in->TxFrames);
    cmQnPut(e, CM_QN_S_TX_REFUSED, in->TxRefused);
    cmQnPut(e, CM_QN_S_LINK_UPS, in->LinkUps);
    cmQnPut(e, CM_QN_S_LINK_DOWNS, in->LinkDowns);
    cmQnPut(e, CM_QN_S_RINGS_FAILED, in->RingsFailed);
    cmQnPut(e, CM_QN_S_IRQ_STATE, in->IrqState);
    cmQnPut(e, CM_QN_S_IRQ_KICKS, in->IrqKicks);
    cmQnPut(e, CM_QN_S_IRQ_KIND, in->IrqKind);
    at += CM_QN_SVC_BYTES;

    /* The links. */
    cmQnPut(out, CM_QN_O_LINK_OFFSET, at);
    cmQnPut(out, CM_QN_O_LINK_SHAPE, n | (CM_QN_LINK_BYTES << 16));
    for (k = 0; k < n; k++) {
        l = &in->Links[k];
        e = out + at;
        cmQnPut(e, CM_QN_L_LINK, l->Link);
        cmQnPut(e, CM_QN_L_FLAGS, l->Flags);
        cmQnPut(e, CM_QN_L_DISC_STATE, l->DiscState);
        cmQnPut(e, CM_QN_L_NET_STATE, l->NetState);
        if (l->Flags & CM_QN_LF_UUID) {
            for (i = 0; i < 4; i++) {
                cmQnPut(e, CM_QN_L_UUID + 4 * i, l->Uuid[i]);
            }
        }
        if (l->Flags & CM_QN_LF_BLOCK) {
            cmQnPut(e, CM_QN_L_VENDOR_ID, l->VendorId);
            cmQnPut(e, CM_QN_L_DEVICE_ID, l->DeviceId);
            cmQnPut(e, CM_QN_L_DEVICE_REV, l->DeviceRev);
            cmQnPut(e, CM_QN_L_PRTCSTNS, l->Prtcstns);
            cmQnText(e + CM_QN_L_VENDOR_TEXT, l->VendorText, CM_QN_TEXT_BYTES,
                     CM_QN_TEXT_CHARS);
            cmQnText(e + CM_QN_L_DEVICE_TEXT, l->DeviceText, CM_QN_TEXT_BYTES,
                     CM_QN_TEXT_CHARS);
            cmQnText(e + CM_QN_L_VENDOR_TEXT_L, l->VendorText, CM_QN_TEXT_L_BYTES,
                     CM_QN_TEXT_L_CHARS);
            cmQnText(e + CM_QN_L_DEVICE_TEXT_L, l->DeviceText, CM_QN_TEXT_L_BYTES,
                     CM_QN_TEXT_L_CHARS);
            cmQnPut(e, CM_QN_L_FLAGS, l->Flags | CM_QN_LF_TEXT_L);
        }
        cmQnPut(e, CM_QN_L_MBPS, l->Mbps);
        cmQnPut(e, CM_QN_L_LANES, l->Lanes);
        cmQnPut(e, CM_QN_L_SENT, l->Sent);
        cmQnPut(e, CM_QN_L_RECEIVED, l->Received);
        cmQnPut(e, CM_QN_L_RESENDS, l->Resends);
        cmQnPut(e, CM_QN_L_MALFORMED, l->Malformed);
        cmQnPut(e, CM_QN_L_LOGINS,
                cmQnLow16(l->LoginsSent) | (cmQnLow16(l->LoginFailures) << 16));
        cmQnPut(e, CM_QN_L_PEER_LOGINS,
                cmQnLow16(l->LoginsReceived) | (cmQnLow16(l->LoginsRefused) << 16));
        cmQnPut(e, CM_QN_L_SERVICES_UP, l->ServicesUp);
        cmQnPut(e, CM_QN_L_SERVICE_FAILS, l->ServiceFailures);
        cmQnPut(e, CM_QN_L_RESTARTS, l->Restarts);
        at += CM_QN_LINK_BYTES;
    }

    /* The client block: its words, the first its bytes. */
    if (in->ClientWords != NULL && in->ClientWordCount != 0) {
        cbytes = in->ClientWordCount * 4UL;
        if (in->ClientWordCount > CM_QN_CLIENT_MAX / 4UL) {
            cbytes = CM_QN_CLIENT_MAX;
        }
        e = out + at;
        cmQnPut(out, CM_QN_O_CLIENT_OFFSET, at);
        cmQnPut(e, CM_QN_C_BYTES, cbytes);
        for (i = 1; i < cbytes / 4UL; i++) {
            cmQnPut(e, 4 * i, in->ClientWords[i]);
        }
        flags |= CM_QN_F_CLIENT;
        at += cbytes;
    }

    cmQnPut(out, CM_QN_O_FLAGS, flags);
    cmQnPut(out, CM_QN_O_BYTES, at);
    return at;
}

ULONG CmNetQueryAnswer(UCHAR *out, ULONG room, const UCHAR *built,
                       ULONG bytes, ULONG pass, BOOLEAN stopping,
                       PULONG info)
{
    ULONG i;
    ULONG n;

    *info = 0;
    if (out == NULL || built == NULL || room < CM_QN_HDR_BYTES ||
        bytes < CM_QN_HDR_BYTES) {
        return 2UL;     /* CM_Q_ANS_TOO_SMALL */
    }
    n = room < bytes ? CM_QN_HDR_BYTES : bytes;
    for (i = 0; i < n; i++) {
        out[i] = built[i];
    }
    cmQnPut(out, CM_QN_O_PASS, pass);
    if (stopping) {
        cmQnPut(out, CM_QN_O_FLAGS, cmQnGet(out, CM_QN_O_FLAGS) | CM_QN_F_STOPPING);
    }
    *info = n;
    return room < bytes ? 1UL : 0UL;    /* CM_Q_ANS_HEADER : _WHOLE */
}

VOID CmNetQuerySetAge(UCHAR *out, ULONG info, ULONG pass, ULONG ageMs)
{
    if (info >= CM_QN_HDR_BYTES) {
        cmQnPut(out, CM_QN_O_PASS_AGE_MS, pass != 0 ? ageMs : CM_QN_UNKNOWN);
    }
}
