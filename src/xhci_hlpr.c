/*
 * xhci_hlpr.c - both ends of the x86 helper's channel, the pure half
 * (xhci_hlpr.h; design record 18 sections 3.2 and 15.3).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_hlpr.h"

ULONG XhciHlprAnswer(PVOID buffer, ULONG inputLength, ULONG outputLength,
                     PVOID connectEx, PVOID disconnectEx, PVOID setTimer,
                     ULONG helperVersion, ULONG inject, PULONG information)
{
    PXHCI_HLPR_QUERY q;
    XHCI_HLPR_QUERY answer;
    PUCHAR from;
    PUCHAR to;
    ULONG size;
    ULONG n;
    ULONG i;

    *information = 0;
    /* Nothing of the block is read until its length is known to cover the
     * header (record 18 section 3.2). */
    if (buffer == NULL || inputLength != outputLength) {
        return XHCI_HLPR_ANS_INVALID;
    }
    if (inputLength < XHCI_HLPR_V2_MIN) {
        return XHCI_HLPR_ANS_TOO_SMALL;
    }
    q = (PXHCI_HLPR_QUERY)buffer;
    size = q->Size;
    if (size != inputLength) {
        return XHCI_HLPR_ANS_INVALID;
    }
    if (q->CallerMajor != XHCI_HLPR_MAJOR) {
        return XHCI_HLPR_ANS_REVISION;
    }

    to = (PUCHAR)&answer;
    for (i = 0; i < (ULONG)sizeof(answer); i++) {
        to[i] = 0;
    }
    answer.Magic = XHCI_HLPR_MAGIC;
    answer.HelperMajor = XHCI_HLPR_MAJOR;
    answer.HelperMinor = XHCI_HLPR_MINOR;
    answer.HelperVersion = helperVersion;
    /* A service is answered whole or not at all. */
    if (connectEx != NULL && disconnectEx != NULL) {
        answer.Services |= XHCI_HLPR_SVC_MSI;
        answer.ConnectEx = connectEx;
        answer.DisconnectEx = disconnectEx;
    }
    if (setTimer != NULL) {
        answer.Services |= XHCI_HLPR_SVC_TIMER;
        answer.SetTimerResolution = setTimer;
    }
    answer.Flags = (inject != XHCI_HLPR_INJ_NONE) ? XHCI_HLPR_F_INJECTED
                                                  : 0UL;
    n = (size < (ULONG)sizeof(XHCI_HLPR_QUERY))
            ? size
            : (ULONG)sizeof(XHCI_HLPR_QUERY);
    switch (inject) {
    case XHCI_HLPR_INJ_MAJOR:
        answer.HelperMajor = XHCI_HLPR_MAJOR + 1;
        break;
    case XHCI_HLPR_INJ_SHORT:
        n = 0x30UL;
        break;
    case XHCI_HLPR_INJ_NULL_DISC:
        answer.DisconnectEx = NULL;
        break;
    case XHCI_HLPR_INJ_MAGIC:
        answer.Magic = ~XHCI_HLPR_MAGIC;
        break;
    case XHCI_HLPR_INJ_TIMER_OFF:
        answer.Services &= ~XHCI_HLPR_SVC_TIMER;
        answer.SetTimerResolution = NULL;
        break;
    case XHCI_HLPR_INJ_TIMER_NULL:
        answer.Services |= XHCI_HLPR_SVC_TIMER;
        answer.SetTimerResolution = NULL;
        break;
    case XHCI_HLPR_INJ_MSI_OFF:
        answer.Services &= ~XHCI_HLPR_SVC_MSI;
        answer.ConnectEx = NULL;
        answer.DisconnectEx = NULL;
        break;
    default:
        break;
    }
    answer.Returned = n;

    /* The helper's fields only, from Magic on; the caller's own are left as
     * it wrote them. */
    from = (PUCHAR)&answer;
    to = (PUCHAR)q;
    for (i = XHCI_HLPR_HELPER_FIELDS; i < n; i++) {
        to[i] = from[i];
    }
    *information = n;
    return XHCI_HLPR_ANS_OK;
}

ULONG XhciHlprAccept(ULONG succeeded, ULONG information, ULONG capacity,
                     const XHCI_HLPR_QUERY *query, ULONG nt6,
                     PULONG msiVerdict, PULONG timerVerdict)
{
    ULONG msiBit;
    ULONG timerBit;

    *msiVerdict = XHCI_HLPR_SVC_NONE;
    *timerVerdict = XHCI_HLPR_SVC_NONE;
    if (!succeeded) {
        return XHCI_HLPR_REFUSE_STATUS;
    }
    if (query->Returned < XHCI_HLPR_V2_MIN ||
        query->Returned != information || information > capacity) {
        return XHCI_HLPR_REFUSE_LENGTH;
    }
    if (query->Magic != XHCI_HLPR_MAGIC ||
        query->HelperMajor != XHCI_HLPR_MAJOR) {
        return XHCI_HLPR_REFUSE_IDENTITY;
    }

    msiBit = (query->Services & XHCI_HLPR_SVC_MSI) ? 1UL : 0UL;
    timerBit = (query->Services & XHCI_HLPR_SVC_TIMER) ? 1UL : 0UL;
    if (!nt6) {
        *msiVerdict = XHCI_HLPR_SVC_NOT_ASKED;
    } else if (msiBit && query->ConnectEx != NULL &&
               query->DisconnectEx != NULL &&
               query->ConnectEx != query->DisconnectEx) {
        *msiVerdict = XHCI_HLPR_SVC_ACCEPTED;
    } else if (!msiBit && query->ConnectEx == NULL &&
               query->DisconnectEx == NULL) {
        *msiVerdict = XHCI_HLPR_SVC_NOT_OFFERED;
    } else {
        *msiVerdict = XHCI_HLPR_SVC_MALFORMED;
    }

    if (timerBit && query->SetTimerResolution != NULL &&
        query->SetTimerResolution != query->ConnectEx &&
        query->SetTimerResolution != query->DisconnectEx) {
        *timerVerdict = XHCI_HLPR_SVC_ACCEPTED;
    } else if (!timerBit && query->SetTimerResolution == NULL) {
        *timerVerdict = XHCI_HLPR_SVC_NOT_OFFERED;
    } else {
        *timerVerdict = XHCI_HLPR_SVC_MALFORMED;
    }
    return XHCI_HLPR_ACCEPTED;
}

ULONG XhciHlprPackVersion(const char *text)
{
    ULONG packed;
    ULONG part;
    ULONG parts;

    packed = 0;
    part = 0;
    parts = 0;
    for (;; text++) {
        if (*text >= '0' && *text <= '9') {
            part = part * 10UL + (ULONG)(*text - '0');
            if (part > 255UL) {
                part = 255UL;
            }
            continue;
        }
        if (parts < 4) {
            packed = (packed << 8) | part;
            parts++;
        }
        part = 0;
        if (*text != '.') {
            break;
        }
    }
    while (parts < 4) {
        packed <<= 8;
        parts++;
    }
    return packed;
}
