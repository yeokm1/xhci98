/*
 * cm_osc.c - the pure half of the platform's USB4 handover on 98 SE and
 * ME (cm_osc.h; design record 32 section 12.8). The ACPI structures are
 * the ACPI Specification's (the root pointer, the description header,
 * RSDT and XSDT, AML's DefOpRegion, DefField, PkgLength and NameString)
 * and the PCI Firmware Specification's (MCFG); the firmware's own fields
 * and tables are section 12.8.1's.
 */

#include "cm_osc.h"

static ULONG cmOscLe32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

BOOLEAN CmOscSumZero(const UCHAR *p, ULONG n)
{
    UCHAR s;
    ULONG i;

    s = 0;
    for (i = 0; i < n; i++) {
        s = (UCHAR)(s + p[i]);
    }
    return (BOOLEAN)(s == 0);
}

static BOOLEAN cmOscIsRsdp(const UCHAR *p, ULONG avail)
{
    static const char sig[] = "RSD PTR ";
    ULONG i;
    ULONG len;

    if (avail < 20UL) {
        return FALSE;
    }
    for (i = 0; i < 8UL; i++) {
        if (p[i] != (UCHAR)sig[i]) {
            return FALSE;
        }
    }
    if (!CmOscSumZero(p, 20UL)) {
        return FALSE;
    }
    if (p[15] < 2) {
        return TRUE;
    }
    if (avail < 36UL) {
        return FALSE;
    }
    len = cmOscLe32(p + 20);
    if (len < 36UL || len > 255UL || len > avail) {
        return FALSE;
    }
    return CmOscSumZero(p, len);
}

ULONG CmOscFindRsdp(const UCHAR *area, ULONG bytes)
{
    ULONG at;

    for (at = 0; at + 20UL <= bytes; at += 16UL) {
        if (cmOscIsRsdp(area + at, bytes - at)) {
            return at;
        }
    }
    return 0xFFFFFFFFUL;
}

VOID CmOscRsdpTables(const UCHAR *rsdp, PULONG rsdt, PULONG xsdt)
{
    *rsdt = cmOscLe32(rsdp + 16);
    *xsdt = 0;
    if (rsdp[15] >= 2 && cmOscLe32(rsdp + 28) == 0) {
        *xsdt = cmOscLe32(rsdp + 24);
    }
}

VOID CmOscHeader(const UCHAR *p, CM_OSC_HDR *h)
{
    ULONG i;

    h->Signature = cmOscLe32(p);
    h->Length = cmOscLe32(p + 4);
    for (i = 0; i < 6UL; i++) {
        h->OemId[i] = p[10 + i];
    }
    for (i = 0; i < 8UL; i++) {
        h->OemTableId[i] = p[16 + i];
    }
    h->OemRevision = cmOscLe32(p + 24);
}

BOOLEAN CmOscRootEntry(const UCHAR *t, ULONG length, BOOLEAN xsdt, ULONG n,
                       PULONG phys)
{
    ULONG size;
    ULONG at;

    size = xsdt ? 8UL : 4UL;
    if (length < 36UL || n >= (length - 36UL) / size) {
        return FALSE;
    }
    at = 36UL + n * size;
    *phys = cmOscLe32(t + at);
    if (xsdt && cmOscLe32(t + at + 4) != 0) {
        *phys = 0;
    }
    return TRUE;
}

BOOLEAN CmOscMcfg(const UCHAR *t, ULONG length, ULONG busLo, ULONG busHi,
                  PULONG base, PULONG busEnd)
{
    ULONG at;
    ULONG lo;
    ULONG first;
    ULONG last;
    ULONG found;

    /* The header, 8 reserved bytes, then 16-byte allocations: base
     * (64 bits), segment (16), first and last bus (8 each), 4 reserved
     * [acpi]. Segment 0 alone; exactly one covering the buses. */
    found = 0;
    if (length < 44UL || ((length - 44UL) % 16UL) != 0) {
        return FALSE;
    }
    for (at = 44UL; at + 16UL <= length; at += 16UL) {
        lo = cmOscLe32(t + at);
        first = t[at + 10];
        last = t[at + 11];
        if ((t[at + 8] | t[at + 9]) != 0) {
            continue;
        }
        if (busLo < first || busHi > last || first > last) {
            continue;
        }
        if (cmOscLe32(t + at + 4) != 0 || (lo & 0xFFFFFUL) != 0 ||
            lo == 0) {
            return FALSE;
        }
        found++;
        /* The base is that of bus 0 of the segment, whatever the first
         * bus of the range [acpi]. */
        *base = lo;
        *busEnd = last;
    }
    return (BOOLEAN)(found == 1);
}

BOOLEAN CmOscEcam(ULONG base, ULONG busEnd, ULONG bdf, ULONG offset,
                  PULONG phys)
{
    ULONG bus;
    ULONG page;

    bus = (bdf >> 8) & 0xFFUL;
    if (bdf > 0xFFFFUL || bus > busEnd || offset > 0xFFCUL ||
        (offset & 3UL) != 0) {
        return FALSE;
    }
    page = (bus << 20) | (((bdf >> 3) & 0x1FUL) << 15) |
           ((bdf & 7UL) << 12);
    if (base > 0xFFFFFFFFUL - page - 0xFFFUL) {
        return FALSE;
    }
    *phys = base + page + offset;
    return TRUE;
}

BOOLEAN CmOscInRam(ULONG phys, ULONG bytes, ULONG topMem)
{
    ULONG end;

    if (bytes == 0 || phys > 0xFFFFFFFFUL - bytes) {
        return FALSE;
    }
    end = phys + bytes;
    if (end <= 0xA0000UL) {
        return TRUE;
    }
    if (phys >= 0xE0000UL && end <= 0x100000UL) {
        return TRUE;
    }
    return (BOOLEAN)(phys >= 0x100000UL && end <= topMem);
}

/* ---- AML --------------------------------------------------------------- */

/* A PkgLength at p (avail bytes): the value and the bytes it takes, or 0. */
static ULONG cmOscPkg(const UCHAR *p, ULONG avail, PULONG value)
{
    ULONG n;
    ULONG i;
    ULONG v;

    if (avail == 0) {
        return 0;
    }
    n = (ULONG)(p[0] >> 6);
    if (n == 0) {
        *value = p[0] & 0x3FUL;
        return 1;
    }
    if (avail < n + 1UL || (p[0] & 0x30) != 0) {
        return 0;
    }
    v = p[0] & 0x0FUL;
    for (i = 0; i < n; i++) {
        v |= (ULONG)p[1 + i] << (4UL + 8UL * i);
    }
    *value = v;
    return n + 1UL;
}

static BOOLEAN cmOscLead(UCHAR c)
{
    return (BOOLEAN)((c >= 'A' && c <= 'Z') || c == '_');
}

static BOOLEAN cmOscSegOk(const UCHAR *p)
{
    ULONG i;

    if (!cmOscLead(p[0])) {
        return FALSE;
    }
    for (i = 1; i < 4UL; i++) {
        if (!cmOscLead(p[i]) && !(p[i] >= '0' && p[i] <= '9')) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN cmOscIs(const UCHAR *seg, const char *name)
{
    return (BOOLEAN)(seg != NULL && seg[0] == (UCHAR)name[0] &&
                     seg[1] == (UCHAR)name[1] && seg[2] == (UCHAR)name[2] &&
                     seg[3] == (UCHAR)name[3]);
}

/* An integer constant at p: its value (below 4 GB) and its bytes, or 0. */
static ULONG cmOscConst(const UCHAR *p, ULONG avail, PULONG value)
{
    if (avail == 0) {
        return 0;
    }
    switch (p[0]) {
    case 0x00:
        *value = 0;
        return 1;
    case 0x01:
        *value = 1;
        return 1;
    case 0x0A:
        if (avail < 2UL) {
            return 0;
        }
        *value = p[1];
        return 2;
    case 0x0B:
        if (avail < 3UL) {
            return 0;
        }
        *value = (ULONG)p[1] | ((ULONG)p[2] << 8);
        return 3;
    case 0x0C:
        if (avail < 5UL) {
            return 0;
        }
        *value = cmOscLe32(p + 1);
        return 5;
    case 0x0E:
        if (avail < 9UL || cmOscLe32(p + 5) != 0) {
            return 0;
        }
        *value = cmOscLe32(p + 1);
        return 9;
    default:
        return 0;
    }
}

/* A Field's list (p, avail bytes to the package's end): M620's and M251's
 * byte offsets when each is a 32-bit field on a byte boundary (found bit 0
 * and bit 1), the list's end in bits; FALSE when an element is not known
 * or a position overflows. */
static BOOLEAN cmOscFieldList(const UCHAR *p, ULONG avail, PULONG off620,
                              PULONG off251, PULONG found, PULONG endBits)
{
    ULONG at;
    ULONG bit;
    ULONG len;
    ULONG n;
    ULONG k;

    at = 0;
    bit = 0;
    *found = 0;
    while (at < avail) {
        if (p[at] == 0x00) {
            n = cmOscPkg(p + at + 1, avail - at - 1, &len);
            if (n == 0) {
                return FALSE;
            }
            at += 1UL + n;
        } else if (p[at] == 0x01) {
            at += 3;
            continue;
        } else if (p[at] == 0x03) {
            at += 4;
            continue;
        } else if (at + 4UL < avail && cmOscSegOk(p + at)) {
            n = cmOscPkg(p + at + 4, avail - at - 4, &len);
            if (n == 0) {
                return FALSE;
            }
            if (cmOscIs(p + at, "M620") || cmOscIs(p + at, "M251")) {
                k = cmOscIs(p + at, "M620") ? 1UL : 2UL;
                if ((*found & (k << 4)) != 0) {
                    return FALSE;       /* the name twice in one list */
                }
                *found |= k << 4;
                if (len == 32UL && (bit & 7UL) == 0) {
                    if (k == 1UL) {
                        *off620 = bit >> 3;
                    } else {
                        *off251 = bit >> 3;
                    }
                    *found |= k;
                }
            }
            at += 4UL + n;
        } else {
            return FALSE;
        }
        if (bit + len < bit) {
            return FALSE;
        }
        bit += len;
    }
    *endBits = bit;
    return (BOOLEAN)(at == avail);
}

/* ---- the AML walk (record 32 section 12.8.3 steps 4 and 5) ------------ */

#define CM_OSC_TERMS    20000UL /* terms walked in one table             */

typedef struct _CM_OSC_CTX {
    CM_OSC_WALK *W;
    ULONG Terms;
    const UCHAR *Base;          /* the table's AML, for the offsets kept */
    const CM_OSC_SCOPE *Find;   /* CmOscFind's path, or NULL             */
    ULONG FindBits;
    ULONG FindArgs;
} CM_OSC_CTX;

/* TRUE when two canonical paths are the same. */
static BOOLEAN cmOscSame(const CM_OSC_SCOPE *a, const CM_OSC_SCOPE *b)
{
    ULONG i;

    if (a->Segs != b->Segs) {
        return FALSE;
    }
    for (i = 0; i < a->Segs; i++) {
        if (a->Seg[i][0] != b->Seg[i][0] || a->Seg[i][1] != b->Seg[i][1] ||
            a->Seg[i][2] != b->Seg[i][2] || a->Seg[i][3] != b->Seg[i][3]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* CmOscFind: a declaration of `path` seen; `method` with `args`. */
static VOID cmOscDecl(CM_OSC_CTX *c, const CM_OSC_SCOPE *path,
                      BOOLEAN method, ULONG args)
{
    if (c->Find == NULL || !cmOscSame(path, c->Find)) {
        return;
    }
    if (args == 0xFFFFFFFFUL) {
        c->FindBits |= CM_OSC_FIND_ALIAS;
        return;
    }
    if (!method) {
        c->FindBits |= CM_OSC_FIND_VALUE;
        return;
    }
    if ((c->FindBits & CM_OSC_FIND_METHOD) != 0 && c->FindArgs != args) {
        c->FindBits |= CM_OSC_FIND_ARGS;
    }
    c->FindBits |= CM_OSC_FIND_METHOD;
    c->FindArgs = args;
}

/* A NameString resolved against `scope` into *out: root and parent
 * prefixes, dual and multiple name prefixes. Its length, or 0 when it is
 * not one or does not fit; *single TRUE for one plain NameSeg (a reference
 * that searches upward). */
static ULONG cmOscPath(const UCHAR *p, ULONG avail, const CM_OSC_SCOPE *scope,
                       CM_OSC_SCOPE *out, BOOLEAN *single)
{
    ULONG at;
    ULONG segs;
    ULONG ups;
    ULONG i;
    BOOLEAN root;

    at = 0;
    root = FALSE;
    ups = 0;
    *single = FALSE;
    if (at < avail && p[at] == 0x5C) {
        root = TRUE;
        at++;
    } else {
        while (at < avail && p[at] == 0x5E) {
            ups++;
            at++;
        }
    }
    if (at >= avail) {
        return 0;
    }
    if (root) {
        out->Segs = 0;
    } else {
        if (ups > scope->Segs) {
            return 0;
        }
        *out = *scope;
        out->Segs -= ups;
    }
    if (p[at] == 0x00) {
        return at + 1UL;
    }
    if (p[at] == 0x2E) {
        segs = 2;
        at++;
    } else if (p[at] == 0x2F) {
        if (at + 1UL >= avail) {
            return 0;
        }
        segs = p[at + 1];
        at += 2;
    } else {
        segs = 1;
        *single = (BOOLEAN)(!root && ups == 0);
    }
    if (segs == 0 || segs > avail || at + 4UL * segs > avail ||
        out->Segs + segs > CM_OSC_DEPTH) {
        return 0;
    }
    for (i = 0; i < segs; i++) {
        if (!cmOscSegOk(p + at)) {
            return 0;
        }
        out->Seg[out->Segs][0] = p[at];
        out->Seg[out->Segs][1] = p[at + 1];
        out->Seg[out->Segs][2] = p[at + 2];
        out->Seg[out->Segs][3] = p[at + 3];
        out->Segs++;
        at += 4;
    }
    return at;
}

/* TRUE when `s` is the root path of the names given (one or two). */
static BOOLEAN cmOscPathIs(const CM_OSC_SCOPE *s, const char *a,
                           const char *b)
{
    if (b == NULL) {
        return (BOOLEAN)(s->Segs == 1 && cmOscIs(s->Seg[0], a));
    }
    return (BOOLEAN)(s->Segs == 2 && cmOscIs(s->Seg[0], a) &&
                     cmOscIs(s->Seg[1], b));
}

/* FNV-1a over a method's flags byte and body (record 32 section 12.8.3
 * step 5): the identity of the firmware code the contract was read from. */
ULONG CmOscHash(const UCHAR *p, ULONG n)
{
    ULONG h;
    ULONG i;

    h = 0x811C9DC5UL;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h = (h * 0x01000193UL) & 0xFFFFFFFFUL;
    }
    return h;
}

/* The methods whose code the handover rests on, by canonical path. */
static const char *const cmOscMethodPath[CM_OSC_METHODS][2] = {
    { "_SB_", "M625" },     /* the mailbox exchange                     */
    { "_SB_", "_OSC" },     /* the two _OSCs: the commands and the bytes */
    { "M049", NULL },       /* a byte read at a pointer and offset       */
    { "M624", NULL },       /* the roots table's bus for an index        */
    { "M011", NULL }        /* a byte read through M049                  */
};

static VOID cmOscMethod(CM_OSC_CTX *c, const CM_OSC_SCOPE *path,
                        const UCHAR *flagsAndBody, ULONG n)
{
    ULONG i;

    cmOscDecl(c, path, TRUE, flagsAndBody[0] & 7UL);
    if (c->Find != NULL) {
        return;
    }
    for (i = 0; i < CM_OSC_METHODS; i++) {
        if (cmOscPathIs(path, cmOscMethodPath[i][0],
                        cmOscMethodPath[i][1])) {
            c->W->MethodSeen[i]++;
            c->W->MethodHash[i] = CmOscHash(flagsAndBody, n);
            c->W->MethodTable[i] = c->W->Table;
            c->W->MethodAt[i] = (ULONG)(flagsAndBody - c->Base);
            c->W->MethodLen[i] = n;
        }
    }
}

/* A Field's elements' names declared in `scope` (CmOscFind alone): FALSE
 * when the list is not one 12.8.3 step 4 decodes. */
static BOOLEAN cmOscFieldNames(CM_OSC_CTX *c, const UCHAR *p, ULONG avail,
                               const CM_OSC_SCOPE *scope)
{
    CM_OSC_SCOPE path;
    ULONG at;
    ULONG len;
    ULONG n;

    at = 0;
    while (at < avail) {
        if (p[at] == 0x00) {
            n = cmOscPkg(p + at + 1, avail - at - 1, &len);
            if (n == 0) {
                return FALSE;
            }
            at += 1UL + n;
        } else if (p[at] == 0x01) {
            at += 3;
        } else if (p[at] == 0x03) {
            at += 4;
        } else if (at + 4UL < avail && cmOscSegOk(p + at) &&
                   scope->Segs < CM_OSC_DEPTH) {
            n = cmOscPkg(p + at + 4, avail - at - 4, &len);
            if (n == 0) {
                return FALSE;
            }
            path = *scope;
            path.Seg[path.Segs][0] = p[at];
            path.Seg[path.Segs][1] = p[at + 1];
            path.Seg[path.Segs][2] = p[at + 2];
            path.Seg[path.Segs][3] = p[at + 3];
            path.Segs++;
            cmOscDecl(c, &path, FALSE, 0);
            at += 4UL + n;
        } else {
            return FALSE;
        }
    }
    return (BOOLEAN)(at == avail);
}

/* A package's extent at p (the opcode's bytes already passed): the end of
 * the package, its PkgLength's bytes in *pl; 0 when it does not fit. */
static ULONG cmOscPkgEnd(const UCHAR *p, ULONG avail, PULONG pl)
{
    ULONG v;
    ULONG n;

    n = cmOscPkg(p, avail, &v);
    if (n == 0 || v < n || v > avail) {
        return 0;
    }
    *pl = n;
    return v;
}

static BOOLEAN cmOscTerms(CM_OSC_CTX *c, const UCHAR *p, ULONG n,
                          const CM_OSC_SCOPE *scope, ULONG depth)
{
    CM_OSC_SCOPE path;
    CM_OSC_REF *r;
    ULONG at;
    ULONG k;
    ULONG end;
    ULONG pl;
    ULONG v;
    ULONG base;
    ULONG len;
    ULONG o620;
    ULONG o251;
    ULONG found;
    ULONG bits;
    BOOLEAN single;
    UCHAR op;

    at = 0;
    while (at < n) {
        if (++c->Terms > CM_OSC_TERMS) {
            return FALSE;
        }
        op = p[at];
        if (op == 0x10 || (op == 0x5B && at + 1UL < n && p[at + 1] == 0x82)) {
            /* Scope, Device: descended. */
            k = at + (op == 0x10 ? 1UL : 2UL);
            end = cmOscPkgEnd(p + k, n - k, &pl);
            if (end == 0 || depth >= CM_OSC_DEPTH) {
                return FALSE;
            }
            v = cmOscPath(p + k + pl, end - pl, scope, &path, &single);
            if (v == 0) {
                return FALSE;
            }
            cmOscDecl(c, &path, FALSE, 0);
            if (!cmOscTerms(c, p + k + pl + v, end - pl - v, &path,
                            depth + 1UL)) {
                return FALSE;
            }
            at = k + end;
        } else if (op == 0x14) {
            /* Method: fingerprinted, skipped. */
            k = at + 1UL;
            end = cmOscPkgEnd(p + k, n - k, &pl);
            if (end == 0) {
                return FALSE;
            }
            v = cmOscPath(p + k + pl, end - pl, scope, &path, &single);
            if (v == 0 || pl + v >= end) {
                return FALSE;
            }
            cmOscMethod(c, &path, p + k + pl + v, end - pl - v);
            at = k + end;
        } else if (op == 0xA0 || op == 0xA1 || op == 0xA2 || op == 0x11 ||
                   op == 0x12 || op == 0x13) {
            /* If, Else, While, and data packages: skipped whole. */
            k = at + 1UL;
            end = cmOscPkgEnd(p + k, n - k, &pl);
            if (end == 0) {
                return FALSE;
            }
            at = k + end;
        } else if (op == 0x5B && at + 1UL < n &&
                   (p[at + 1] == 0x83 || p[at + 1] == 0x84 ||
                    p[at + 1] == 0x85 || p[at + 1] == 0x86 ||
                    p[at + 1] == 0x87)) {
            /* Processor, PowerResource, ThermalZone, IndexField,
             * BankField: skipped whole (the first three's own names are
             * declarations for CmOscFind). */
            k = at + 2UL;
            end = cmOscPkgEnd(p + k, n - k, &pl);
            if (end == 0) {
                return FALSE;
            }
            if (c->Find != NULL && p[at + 1] <= 0x85) {
                v = cmOscPath(p + k + pl, end - pl, scope, &path, &single);
                if (v == 0) {
                    return FALSE;
                }
                cmOscDecl(c, &path, FALSE, 0);
            }
            at = k + end;
        } else if (op == 0x08) {
            /* Name and its data. */
            k = at + 1UL;
            v = cmOscPath(p + k, n - k, scope, &path, &single);
            if (v == 0 || k + v >= n) {
                return FALSE;
            }
            cmOscDecl(c, &path, FALSE, 0);
            k += v;
            v = cmOscConst(p + k, n - k, &base);
            if (v == 0) {
                if (p[k] == 0x0D) {
                    for (v = 1; k + v < n && p[k + v] != 0; v++) {
                    }
                    if (k + v >= n) {
                        return FALSE;
                    }
                    v++;
                } else if (p[k] == 0x11 || p[k] == 0x12 || p[k] == 0x13) {
                    end = cmOscPkgEnd(p + k + 1, n - k - 1, &pl);
                    if (end == 0) {
                        return FALSE;
                    }
                    v = 1UL + end;
                } else {
                    return FALSE;
                }
            }
            at = k + v;
        } else if (op == 0x15) {
            /* External: a name, a type and an argument count. */
            k = at + 1UL;
            v = cmOscPath(p + k, n - k, scope, &path, &single);
            if (v == 0 || k + v + 2UL > n) {
                return FALSE;
            }
            /* Type 8 is a method (ACPI's ObjectType). */
            cmOscDecl(c, &path, (BOOLEAN)(p[k + v] == 0x08),
                      p[k + v + 1] & 7UL);
            at = k + v + 2UL;
        } else if (op == 0x06) {
            /* Alias: two names. */
            k = at + 1UL;
            v = cmOscPath(p + k, n - k, scope, &path, &single);
            if (v == 0) {
                return FALSE;
            }
            k += v;
            v = cmOscPath(p + k, n - k, scope, &path, &single);
            if (v == 0) {
                return FALSE;
            }
            cmOscDecl(c, &path, FALSE, 0xFFFFFFFFUL);    /* an Alias */
            at = k + v;
        } else if (op == 0x5B && at + 1UL < n &&
                   (p[at + 1] == 0x01 || p[at + 1] == 0x02)) {
            /* Mutex (a name and a flags byte), Event (a name). */
            k = at + 2UL;
            v = cmOscPath(p + k, n - k, scope, &path, &single);
            if (v == 0) {
                return FALSE;
            }
            cmOscDecl(c, &path, FALSE, 0);
            at = k + v + (p[at + 1] == 0x01 ? 1UL : 0UL);
            if (at > n) {
                return FALSE;
            }
        } else if (op == 0x5B && at + 1UL < n && p[at + 1] == 0x80) {
            /* OperationRegion. */
            k = at + 2UL;
            v = cmOscPath(p + k, n - k, scope, &path, &single);
            if (v == 0 || k + v >= n) {
                return FALSE;
            }
            cmOscDecl(c, &path, FALSE, 0);
            k += v;
            op = p[k];
            k++;
            /* Literal constants alone: anything else (a name, which may be
             * a method call with arguments, or an expression) cannot be
             * passed over safely, and the table is not used. */
            v = cmOscConst(p + k, n - k, &base);
            if (v == 0) {
                return FALSE;
            }
            k += v;
            v = cmOscConst(p + k, n - k, &len);
            if (v == 0) {
                return FALSE;
            }
            at = k + v;
            if (c->Find == NULL && path.Segs >= 1 &&
                cmOscIs(path.Seg[path.Segs - 1], "CPNV")) {
                c->W->CpnvAnywhere++;
                if (path.Segs == 1) {
                    /* Sticky: once unusable or ambiguous, never 1 again. */
                    if (op != 0x00 || c->W->Regions == 2 ||
                        (c->W->Regions == 1 &&
                         (c->W->Base != base || c->W->Length != len))) {
                        c->W->Regions = 2;
                    } else {
                        c->W->Regions = 1;
                        c->W->Base = base;
                        c->W->Length = len;
                    }
                }
            }
        } else if (op == 0x5B && at + 1UL < n && p[at + 1] == 0x81) {
            /* Field. */
            k = at + 2UL;
            end = cmOscPkgEnd(p + k, n - k, &pl);
            if (end == 0) {
                return FALSE;
            }
            v = cmOscPath(p + k + pl, end - pl, scope, &path, &single);
            if (v == 0 || pl + v + 1UL > end) {
                return FALSE;
            }
            if (c->Find != NULL) {
                if (!cmOscFieldNames(c, p + k + pl + v + 1,
                                     end - pl - v - 1, scope)) {
                    return FALSE;
                }
            } else if (path.Segs >= 1 &&
                       cmOscIs(path.Seg[path.Segs - 1], "CPNV")) {
                if (c->W->Refs >= CM_OSC_REFS) {
                    return FALSE;
                }
                r = &c->W->Ref[c->W->Refs++];
                r->Path = path;
                r->Scope = *scope;
                r->Single = single;
                r->Ok = 0;
                o620 = 0;
                o251 = 0;
                if (cmOscFieldList(p + k + pl + v + 1, end - pl - v - 1,
                                   &o620, &o251, &found, &bits) &&
                    (found & 3UL) == 3UL) {
                    r->Ok = 1;
                    r->Off620 = o620;
                    r->Off251 = o251;
                    r->EndBits = bits;
                }
            }
            at = k + end;
        } else {
            /* Anything else: this table is not used (never a search for
             * the next opcode). */
            return FALSE;
        }
    }
    return (BOOLEAN)(at == n);
}

BOOLEAN CmOscAmlWalk(const UCHAR *aml, ULONG length, CM_OSC_WALK *w)
{
    CM_OSC_CTX c;
    CM_OSC_SCOPE root;
    ULONG regions;
    ULONG base;
    ULONG len;
    ULONG anywhere;
    ULONG refs;
    ULONG seen[CM_OSC_METHODS];
    ULONG hash[CM_OSC_METHODS];
    ULONG mt[CM_OSC_METHODS];
    ULONG ma[CM_OSC_METHODS];
    ULONG ml[CM_OSC_METHODS];
    ULONG i;

    for (i = 0; i < CM_OSC_METHODS; i++) {
        seen[i] = w->MethodSeen[i];
        hash[i] = w->MethodHash[i];
        mt[i] = w->MethodTable[i];
        ma[i] = w->MethodAt[i];
        ml[i] = w->MethodLen[i];
    }
    regions = w->Regions;
    base = w->Base;
    len = w->Length;
    anywhere = w->CpnvAnywhere;
    refs = w->Refs;
    c.W = w;
    c.Terms = 0;
    c.Base = aml;
    c.Find = NULL;
    c.FindBits = 0;
    c.FindArgs = 0;
    root.Segs = 0;
    if (cmOscTerms(&c, aml, length, &root, 0)) {
        return TRUE;
    }
    /* Not walked whole: nothing of this table is kept. */
    w->Regions = regions;
    w->Base = base;
    w->Length = len;
    w->CpnvAnywhere = anywhere;
    w->Refs = refs;
    for (i = 0; i < CM_OSC_METHODS; i++) {
        w->MethodSeen[i] = seen[i];
        w->MethodHash[i] = hash[i];
        w->MethodTable[i] = mt[i];
        w->MethodAt[i] = ma[i];
        w->MethodLen[i] = ml[i];
    }
    return FALSE;
}

ULONG CmOscFind(const UCHAR *aml, ULONG length, const CM_OSC_SCOPE *path,
                CM_OSC_WALK *scratch, PULONG args)
{
    CM_OSC_CTX c;
    CM_OSC_SCOPE root;
    ULONG i;
    UCHAR *z;

    z = (UCHAR *)scratch;
    for (i = 0; i < sizeof(*scratch); i++) {
        z[i] = 0;
    }
    c.W = scratch;
    c.Terms = 0;
    c.Base = aml;
    c.Find = path;
    c.FindBits = 0;
    c.FindArgs = 0;
    root.Segs = 0;
    *args = 0;
    if (!cmOscTerms(&c, aml, length, &root, 0)) {
        return CM_OSC_FIND_FAILED;
    }
    *args = c.FindArgs;
    return c.FindBits;
}

ULONG CmOscCpnvPlace(const CM_OSC_WALK *w, PULONG base, PULONG length,
                     PULONG off620, PULONG off251)
{
    const CM_OSC_REF *r;
    ULONG placed;
    ULONG o620;
    ULONG o251;
    ULONG i;
    BOOLEAN toRoot;

    if (w->Regions != 1) {
        return w->Regions == 0 ? CM_OSC_NO_CPNV : CM_OSC_CPNV_TWICE;
    }
    placed = 0;
    o620 = 0;
    o251 = 0;
    for (i = 0; i < w->Refs; i++) {
        r = &w->Ref[i];
        /* \CPNV itself, or a single name searched upward with no other
         * region of that name anywhere to find first. */
        /* The names a Field declares are in its own scope (ACPI): the
         * methods read \M620 and \M251, so only a Field at the root
         * places them. */
        if (r->Scope.Segs != 0) {
            continue;
        }
        toRoot = (BOOLEAN)((r->Path.Segs == 1 &&
                            cmOscIs(r->Path.Seg[0], "CPNV")) ||
                           (r->Single && w->CpnvAnywhere == 1));
        if (!toRoot) {
            continue;
        }
        if (!r->Ok || r->EndBits / 8UL > w->Length ||
            r->Off620 > w->Length - 4UL || r->Off251 > w->Length - 4UL ||
            w->Length < 4UL) {
            return CM_OSC_NO_CPNV;
        }
        if (placed != 0 && (r->Off620 != o620 || r->Off251 != o251)) {
            return CM_OSC_CPNV_TWICE;
        }
        placed = 1;
        o620 = r->Off620;
        o251 = r->Off251;
    }
    if (placed == 0) {
        return CM_OSC_NO_CPNV;
    }
    *base = w->Base;
    *length = w->Length;
    *off620 = o620;
    *off251 = o251;
    return CM_OSC_OK;
}

ULONG CmOscDecide(const UCHAR *dusb4, const UCHAR *entry, ULONG rootBdf,
                  PULONG data)
{
    ULONG bus;

    if (dusb4[CM_OSC_D_FITTED] == 0) {
        return CM_OSC_NOT_FITTED;
    }
    if (dusb4[CM_OSC_D_GRANT] == 0) {
        return CM_OSC_NOT_GRANTED;
    }
    if (entry[0] == 0xFF) {
        return CM_OSC_NO_ROOT_ENTRY;
    }
    bus = (ULONG)entry[3] | ((ULONG)entry[4] << 8);
    if (bus > 0xFFUL || dusb4[CM_OSC_D_ROOT_DEV] > 0x1FU ||
        dusb4[CM_OSC_D_ROOT_FN] > 7U ||
        ((bus << 8) | ((ULONG)dusb4[CM_OSC_D_ROOT_DEV] << 3) |
         dusb4[CM_OSC_D_ROOT_FN]) != rootBdf) {
        return CM_OSC_OTHER_ROOT;
    }
    *data = dusb4[CM_OSC_D_DATA];
    return CM_OSC_OK;
}

/*
 * The firmware builds whose code was read (record 32 section 12.8.3 step
 * 5), each the hashes of \_SB.M625, \_SB._OSC, \M049, \M624 and \M011 in
 * that order: the build read on 2026-11-02 (the same in the board's two
 * captures, its LAN on and off). The qemu flavour adds usb4-emu's own
 * methods (its scripts/asm-fw-ssdt.py: the trivial bodies and --build 1),
 * which no firmware has.
 */
const ULONG CmOscKnownCode[][CM_OSC_METHODS] = {
    { 0x7D572C5DUL, 0x9C8B7A07UL, 0xA4C061BAUL, 0x0031B85CUL, 0x15A47C69UL },
#if defined(XHCI_FLAVOUR_QEMU)
    { 0xE5BC260EUL, 0xF8435882UL, 0xE6BC27A1UL, 0xABE3DC80UL, 0x1F4395E7UL },
    /* usb4-emu's --build 1 (record 32 section 12.8.8). */
    { 0x43EB7CDCUL, 0xCCD47116UL, 0xCDA93C5FUL, 0xFE35E156UL, 0x408891DFUL },
    /* usb4-emu's --build 1 --osi, whose _OSC sends nothing before Windows
     * 11, as the board's (record 32 section 12.8.15.17; --externals-ifzero
     * leaves the methods' bytes). */
    { 0x43EB7CDCUL, 0x2FF061F8UL, 0xCDA93C5FUL, 0xFE35E156UL, 0x408891DFUL },
#endif
};

const ULONG CmOscKnownCount =
    sizeof(CmOscKnownCode) / sizeof(CmOscKnownCode[0]);

ULONG CmOscCodeKnown(const CM_OSC_WALK *w)
{
    ULONG v;
    ULONG i;

    for (v = 0; v < CmOscKnownCount; v++) {
        for (i = 0; i < CM_OSC_METHODS; i++) {
            if (w->MethodSeen[i] != 1 ||
                w->MethodHash[i] != CmOscKnownCode[v][i]) {
                break;
            }
        }
        if (i == CM_OSC_METHODS) {
            return CM_OSC_OK;
        }
    }
    return CM_OSC_UNKNOWN_CODE;
}
