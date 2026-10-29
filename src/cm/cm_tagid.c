/*
 * cm_tagid.c - the pure half of the tunnel id on Windows 98 SE and ME
 * (design record 31 section 5; cm_tagid.h). DDK-free; host-tested by
 * test\test_cm_tagid.c.
 *
 * IRQL: any. No lock, no pool, no call out.
 */

#include "cm_tagid.h"

static char cmTagLower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static ULONG cmTagBlank(char c)
{
    return (ULONG)(c == ' ' || c == '\t');
}

ULONG CmTagListHas(const char *list, ULONG bytes, const char *id)
{
    ULONG i;
    ULONG start;
    ULONG end;
    ULONG k;
    ULONG n;
    ULONG same;

    if (list == NULL || id == NULL) {
        return 0;
    }
    n = 0;
    while (id[n] != 0) {
        n++;
    }
    if (n == 0) {
        return 0;
    }
    i = 0;
    while (i < bytes) {
        if (list[i] == 0 && (i + 1 >= bytes || list[i + 1] == 0)) {
            break;
        }
        start = i;
        while (i < bytes && list[i] != ',' && list[i] != 0) {
            i++;
        }
        end = i;
        while (start < end && cmTagBlank(list[start])) {
            start++;
        }
        while (end > start && cmTagBlank(list[end - 1])) {
            end--;
        }
        if (end - start == n) {
            same = 1;
            for (k = 0; k < n; k++) {
                if (cmTagLower(list[start + k]) != cmTagLower(id[k])) {
                    same = 0;
                    break;
                }
            }
            if (same) {
                return 1;
            }
        }
        if (i < bytes) {
            /* Past the separator; a NUL followed by a NUL ends the list
             * at the top of the loop. */
            if (list[i] == 0 && i + 1 < bytes && list[i + 1] == 0) {
                break;
            }
            i++;
        }
    }
    return 0;
}

ULONG CmTagDecide(ULONG readOk, ULONG hasClass, ULONG hasId)
{
    if (!readOk) {
        return CM_TAG_NO_READ;
    }
    if (!hasClass) {
        return CM_TAG_NOT_CLASS;
    }
    if (hasId) {
        return CM_TAG_HAS_ID;
    }
    return CM_TAG_ADD;
}

VOID CmTagSeenInit(PCM_TAG_SEEN s)
{
    ULONG k;

    s->Count = 0;
    s->Next = 0;
    for (k = 0; k < CM_TAG_SEEN_MAX; k++) {
        s->Node[k] = 0;
        s->Hash[k] = 0;
    }
}

ULONG CmTagSeenHas(const CM_TAG_SEEN *s, ULONG node, ULONG hash)
{
    ULONG k;

    for (k = 0; k < s->Count && k < CM_TAG_SEEN_MAX; k++) {
        if (s->Node[k] == node && s->Hash[k] == hash) {
            return 1;
        }
    }
    return 0;
}

VOID CmTagSeenAdd(PCM_TAG_SEEN s, ULONG node, ULONG hash)
{
    if (CmTagSeenHas(s, node, hash)) {
        return;
    }
    if (s->Count < CM_TAG_SEEN_MAX) {
        s->Node[s->Count] = node;
        s->Hash[s->Count] = hash;
        s->Count++;
        return;
    }
    if (s->Next >= CM_TAG_SEEN_MAX) {
        s->Next = 0;
    }
    s->Node[s->Next] = node;
    s->Hash[s->Next] = hash;
    s->Next++;
}
