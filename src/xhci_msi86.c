/*
 * xhci_msi86.c - both ends of the x86 MSI helper's channel, the pure half
 * (xhci_msi86.h; design record 18 section 3.2).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_msi86.h"

ULONG XhciMsi86Answer(PVOID buffer, ULONG inputLength, ULONG outputLength,
                      PVOID connectEx, PVOID disconnectEx,
                      ULONG helperVersion, ULONG inject,
                      PULONG information)
{
    PXHCI_MSI86_QUERY q;
    XHCI_MSI86_QUERY answer;
    PUCHAR from;
    PUCHAR to;
    ULONG size;
    ULONG n;
    ULONG i;

    *information = 0;
    /* Nothing of the block is read until its length is known to cover the
     * header (record 18 section 3.2). */
    if (buffer == NULL || inputLength != outputLength) {
        return XHCI_MSI86_ANS_INVALID;
    }
    if (inputLength < XHCI_MSI86_V1_MIN) {
        return XHCI_MSI86_ANS_TOO_SMALL;
    }
    q = (PXHCI_MSI86_QUERY)buffer;
    size = q->Size;
    if (size != inputLength) {
        return XHCI_MSI86_ANS_INVALID;
    }
    if (q->CallerMajor != XHCI_MSI86_MAJOR) {
        return XHCI_MSI86_ANS_REVISION;
    }

    to = (PUCHAR)&answer;
    for (i = 0; i < (ULONG)sizeof(answer); i++) {
        to[i] = 0;
    }
    answer.Magic = XHCI_MSI86_MAGIC;
    answer.HelperMajor = XHCI_MSI86_MAJOR;
    answer.HelperMinor = XHCI_MSI86_MINOR;
    answer.HelperVersion = helperVersion;
    answer.ConnectEx = connectEx;
    answer.DisconnectEx = disconnectEx;
    answer.Flags = (inject != XHCI_MSI86_INJ_NONE) ? XHCI_MSI86_F_INJECTED
                                                   : 0UL;
    n = (size < (ULONG)sizeof(XHCI_MSI86_QUERY))
            ? size
            : (ULONG)sizeof(XHCI_MSI86_QUERY);
    if (inject == XHCI_MSI86_INJ_MAJOR2) {
        answer.HelperMajor = 2;
    } else if (inject == XHCI_MSI86_INJ_SHORT) {
        n = 0x20UL;
    } else if (inject == XHCI_MSI86_INJ_NULL_DISC) {
        answer.DisconnectEx = NULL;
    } else if (inject == XHCI_MSI86_INJ_MAGIC) {
        answer.Magic = ~XHCI_MSI86_MAGIC;
    }
    answer.Returned = n;

    /* The helper's fields only, from Magic on; the caller's own are left as
     * it wrote them. */
    from = (PUCHAR)&answer;
    to = (PUCHAR)q;
    for (i = 0x0CUL; i < n; i++) {
        to[i] = from[i];
    }
    *information = n;
    return XHCI_MSI86_ANS_OK;
}

ULONG XhciMsi86Accept(ULONG succeeded, ULONG information, ULONG capacity,
                      const XHCI_MSI86_QUERY *query)
{
    if (!succeeded) {
        return XHCI_MSI86_REFUSE_STATUS;
    }
    if (query->Returned < XHCI_MSI86_V1_MIN ||
        query->Returned != information || information > capacity) {
        return XHCI_MSI86_REFUSE_LENGTH;
    }
    if (query->Magic != XHCI_MSI86_MAGIC ||
        query->HelperMajor != XHCI_MSI86_MAJOR) {
        return XHCI_MSI86_REFUSE_IDENTITY;
    }
    if (query->ConnectEx == NULL || query->DisconnectEx == NULL ||
        query->ConnectEx == query->DisconnectEx) {
        return XHCI_MSI86_REFUSE_ROUTINES;
    }
    return XHCI_MSI86_ACCEPTED;
}

ULONG XhciMsi86PackVersion(const char *text)
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
