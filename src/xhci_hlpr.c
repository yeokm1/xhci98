/*
 * xhci_hlpr.c - both ends of the x86 helper's channel, the pure half
 * (xhci_hlpr.h; design record 18 sections 3.2 and 15.3; minor 1, record
 * 32 section 12.8.15.15).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_hlpr.h"

ULONG XhciHlprAnswer(PVOID buffer, ULONG inputLength, ULONG outputLength,
                     PVOID connectEx, PVOID disconnectEx, PVOID setTimer,
                     PVOID findRoutine, ULONG helperVersion, ULONG inject,
                     PULONG information)
{
    PXHCI_HLPR_QUERY q;
    XHCI_HLPR_QUERY1 answer;
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
    answer.V2.Magic = XHCI_HLPR_MAGIC;
    answer.V2.HelperMajor = XHCI_HLPR_MAJOR;
    answer.V2.HelperMinor = XHCI_HLPR_MINOR;
    answer.V2.HelperVersion = helperVersion;
    /* A service is answered whole or not at all. */
    if (connectEx != NULL && disconnectEx != NULL) {
        answer.V2.Services |= XHCI_HLPR_SVC_MSI;
        answer.V2.ConnectEx = connectEx;
        answer.V2.DisconnectEx = disconnectEx;
    }
    if (setTimer != NULL) {
        answer.V2.Services |= XHCI_HLPR_SVC_TIMER;
        answer.V2.SetTimerResolution = setTimer;
    }
    answer.V2.Flags = (inject != XHCI_HLPR_INJ_NONE) ? XHCI_HLPR_F_INJECTED
                                                     : 0UL;
    n = (size < (ULONG)sizeof(XHCI_HLPR_QUERY1))
            ? size
            : (ULONG)sizeof(XHCI_HLPR_QUERY1);
    switch (inject) {
    case XHCI_HLPR_INJ_MAJOR:
        answer.V2.HelperMajor = XHCI_HLPR_MAJOR + 1;
        break;
    case XHCI_HLPR_INJ_SHORT:
        n = 0x30UL;
        break;
    case XHCI_HLPR_INJ_NULL_DISC:
        answer.V2.DisconnectEx = NULL;
        break;
    case XHCI_HLPR_INJ_MAGIC:
        answer.V2.Magic = ~XHCI_HLPR_MAGIC;
        break;
    case XHCI_HLPR_INJ_TIMER_OFF:
        answer.V2.Services &= ~XHCI_HLPR_SVC_TIMER;
        answer.V2.SetTimerResolution = NULL;
        break;
    case XHCI_HLPR_INJ_TIMER_NULL:
        answer.V2.Services |= XHCI_HLPR_SVC_TIMER;
        answer.V2.SetTimerResolution = NULL;
        break;
    case XHCI_HLPR_INJ_MSI_OFF:
        answer.V2.Services &= ~XHCI_HLPR_SVC_MSI;
        answer.V2.ConnectEx = NULL;
        answer.V2.DisconnectEx = NULL;
        break;
    case XHCI_HLPR_INJ_ROUTINE_OFF:
        findRoutine = NULL;
        break;
    default:
        break;
    }
    /* Minor 1's service only to a caller whose block holds its field: a
     * V2.0 caller is never told of a field it did not send. */
    if (findRoutine != NULL && n >= XHCI_HLPR_V21_SIZE) {
        answer.V2.Services |= XHCI_HLPR_SVC_ROUTINE;
        answer.FindRoutine = findRoutine;
    }
    answer.V2.Returned = n;

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

ULONG XhciHlprAcceptRoutine(ULONG succeeded, ULONG information,
                            ULONG capacity, const XHCI_HLPR_QUERY1 *query,
                            PULONG routineVerdict)
{
    ULONG verdict;
    ULONG msi;
    ULONG timer;
    ULONG bit;

    *routineVerdict = XHCI_HLPR_SVC_NONE;
    verdict = XhciHlprAccept(succeeded, information, capacity, &query->V2,
                             0UL, &msi, &timer);
    if (verdict != XHCI_HLPR_ACCEPTED) {
        return verdict;
    }
    bit = (query->V2.Services & XHCI_HLPR_SVC_ROUTINE) ? 1UL : 0UL;
    if (query->V2.Returned < XHCI_HLPR_V21_SIZE) {
        *routineVerdict = bit ? XHCI_HLPR_SVC_MALFORMED
                              : XHCI_HLPR_SVC_NOT_OFFERED;
    } else if (bit && query->FindRoutine != NULL) {
        *routineVerdict = XHCI_HLPR_SVC_ACCEPTED;
    } else if (!bit && query->FindRoutine == NULL) {
        *routineVerdict = XHCI_HLPR_SVC_NOT_OFFERED;
    } else {
        *routineVerdict = XHCI_HLPR_SVC_MALFORMED;
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
