/*
 * xhci_acpi.c - the firmware tables xhci98.sys reads on request on 98 SE
 * and ME (xhci_acpib.h, xhci_acpi.h; design record 13 section 8.13,
 * design record 32 section 12.8.16; roadmap-usb4 task 46.21, the owner's
 * option B). Written for this project. A walk of its own, beside
 * xhcicm.sys's discovery (src\cm\cm_osc.c, cm_oscos.c), which it does not
 * change.
 *
 * WHAT IS READ, in this order, every read held to the bounds (above 1 MB,
 * below FEC00000h, no wrap; the BIOS areas below 1 MB for the root
 * pointer alone) and, once MCFG is read, outside every MCFG window: the
 * root pointer (the EBDA's first KB, then E0000h to FFFFFh); the root
 * table it gives; each root entry's 36-byte header (the bootstrap pass,
 * to find MCFG); MCFG whole; each entry's header again; FACP and every
 * SSDT whole; the DSDT the FADT names, whole. No address or length comes
 * from anywhere but the tables. The ACPI Specification lays out the root
 * pointer (signature "RSD PTR ", checksum over 20 bytes, revision 15, RSDT
 * 16, length 20, XSDT 24, extended checksum), the description header (36
 * bytes: signature 0, length 4) and the FADT's DSDT (40) and X_DSDT (140);
 * the PCI Firmware Specification MCFG's allocations (44 on, 16 bytes each:
 * base 0, segment 8, first bus 10, last bus 11).
 *
 * CHECKED BEFORE A TABLE IS KEPT WHOLE: its signature and length those
 * of the header read for it, its length within its cap and its own
 * checksum zero; else it is kept as that header with the reason.
 *
 * IRQL: any the host's routines allow; no lock, no DDK call.
 */

#include "xhci_acpib.h"

#define XHA_SIG(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | \
                             ((ULONG)(UCHAR)(c) << 16) | \
                             ((ULONG)(UCHAR)(d) << 24))

static ULONG xhaGet(const UCHAR *b, ULONG o)
{
    return (ULONG)b[o] | ((ULONG)b[o + 1] << 8) | ((ULONG)b[o + 2] << 16) |
           ((ULONG)b[o + 3] << 24);
}

static VOID xhaPut(UCHAR *b, ULONG o, ULONG v)
{
    b[o] = (UCHAR)(v & 0xFFUL);
    b[o + 1] = (UCHAR)((v >> 8) & 0xFFUL);
    b[o + 2] = (UCHAR)((v >> 16) & 0xFFUL);
    b[o + 3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static ULONG xhaRound(ULONG n)
{
    return (n + 3UL) & ~3UL;
}

static BOOLEAN xhaSumZero(const UCHAR *p, ULONG n)
{
    ULONG s;
    ULONG i;

    s = 0;
    for (i = 0; i < n; i++) {
        s += p[i];
    }
    return (BOOLEAN)((s & 0xFFUL) == 0);
}

/* ---- the pure parts -------------------------------------------------- */

BOOLEAN XhaFadtDsdt(const UCHAR *fadt, ULONG len, PULONG phys)
{
    *phys = 0;
    if (fadt == NULL) {
        return FALSE;
    }
    if (len >= 148UL && xhaGet(fadt, 144) == 0 && xhaGet(fadt, 140) != 0) {
        *phys = xhaGet(fadt, 140);
        return TRUE;
    }
    if (len >= 44UL && xhaGet(fadt, 40) != 0) {
        *phys = xhaGet(fadt, 40);
        return TRUE;
    }
    return FALSE;
}

BOOLEAN XhaMcfgWindows(const UCHAR *mcfg, ULONG len, PULONG lo, PULONG hi,
                       PULONG n)
{
    ULONG count;
    ULONG i;
    ULONG base;
    ULONG first;
    ULONG last;
    const UCHAR *a;

    *n = 0;
    if (mcfg == NULL || len < 44UL || ((len - 44UL) % 16UL) != 0) {
        return FALSE;
    }
    count = (len - 44UL) / 16UL;
    if (count > XHA_MCFG_WINDOWS) {
        return FALSE;
    }
    for (i = 0; i < count; i++) {
        a = mcfg + 44UL + 16UL * i;
        base = xhaGet(a, 0);
        first = a[10];
        last = a[11];
        if (xhaGet(a, 4) != 0 || (base & 0xFFFFFUL) != 0 || first > last ||
            base > 0xFFFFFFFFUL - ((last + 1UL) << 20)) {
            return FALSE;
        }
        lo[i] = base + (first << 20);
        hi[i] = base + ((last + 1UL) << 20);
    }
    *n = count;
    return TRUE;
}

ULONG XhaFindRsdp(const UCHAR *area, ULONG bytes)
{
    static const char sig[8] = { 'R', 'S', 'D', ' ', 'P', 'T', 'R', ' ' };
    const UCHAR *p;
    ULONG at;
    ULONG len;
    ULONG i;

    for (at = 0; at + 20UL <= bytes; at += 16UL) {
        p = area + at;
        for (i = 0; i < 8UL && p[i] == (UCHAR)sig[i]; i++) {
        }
        if (i < 8UL || !xhaSumZero(p, 20UL)) {
            continue;
        }
        if (p[15] < 2) {
            return at;
        }
        if (bytes - at < 36UL) {
            continue;
        }
        len = xhaGet(p, 20);
        if (len >= 36UL && len <= 255UL && len <= bytes - at &&
            xhaSumZero(p, len)) {
            return at;
        }
    }
    return 0xFFFFFFFFUL;
}

ULONG XhaAnswer(UCHAR *out, ULONG room, const XHA_IN *in, ULONG from,
                PULONG info)
{
    ULONG part;
    ULONG flags;
    ULONG gen;
    ULONG i;

    *info = 0;
    if (out == NULL || room < XHA_HDR_BYTES) {
        return 1;
    }
    for (i = 0; i < XHA_HDR_BYTES; i++) {
        out[i] = 0;
    }
    flags = in->Flags | XHA_F_FULL;
    gen = in->SetGen;
    part = 0;
    if (in->Body == NULL || from > in->BodyBytes) {
        flags |= XHA_F_PAST;
        gen = 0;
    } else {
        part = in->BodyBytes - from;
        if (part > room - XHA_HDR_BYTES) {
            part = room - XHA_HDR_BYTES;
        }
        if (part > XHA_PART_MAX) {
            part = XHA_PART_MAX;
        }
        for (i = 0; i < part; i++) {
            out[XHA_HDR_BYTES + i] = in->Body[from + i];
        }
    }
    xhaPut(out, XHA_O_MAGIC, XHA_MAGIC);
    xhaPut(out, XHA_O_VERSION, XHA_VERSION);
    xhaPut(out, XHA_O_BYTES, XHA_HDR_BYTES + part);
    xhaPut(out, XHA_O_FLAGS, flags);
    xhaPut(out, XHA_O_STATE, in->State);
    xhaPut(out, XHA_O_OUTCOME, in->Outcome);
    xhaPut(out, XHA_O_BODY_BYTES, in->Body != NULL ? in->BodyBytes : 0);
    xhaPut(out, XHA_O_ENT_SHAPE,
           ((in->Body != NULL ? in->Count : 0) & 0xFFFFUL) |
               (XHA_ENT_BYTES << 16));
    xhaPut(out, XHA_O_PART_AT, (flags & XHA_F_PAST) ? 0 : from);
    xhaPut(out, XHA_O_PART_BYTES, part);
    xhaPut(out, XHA_O_SET_GEN, gen);
    *info = XHA_HDR_BYTES + part;
    return 0;
}

/* ---- the reading ----------------------------------------------------- */

typedef struct _XHA_ENT {
    ULONG Sig;
    ULONG Kind;             /* XHA_K_* | index << 16                      */
    ULONG Length;           /* the header's                               */
    ULONG Phys;
    ULONG How;              /* XHA_H_* planned, then kept                 */
    ULONG Reason;
    ULONG Cap;              /* the whole's cap, 0: a header by its kind  */
    ULONG Held;
    ULONG Offset;           /* in the body                                */
    UCHAR Hdr[XHA_TBL_HDR];
} XHA_ENT;

typedef struct _XHA_WORK {
    const XHA_HOST *H;
    ULONG Start;
    ULONG Windows;
    ULONG WinLo[XHA_MCFG_WINDOWS];
    ULONG WinHi[XHA_MCFG_WINDOWS];
    BOOLEAN UseWindows;
    ULONG Count;
    XHA_ENT E[XHA_MAX_ENTRIES];
    UCHAR Rsdp[256];
    ULONG RsdpLen;
} XHA_WORK;

/* Whether [phys, phys + bytes) may be read as a table. */
static BOOLEAN xhaBounds(const XHA_WORK *w, ULONG phys, ULONG bytes)
{
    ULONG end;
    ULONG i;

    if (bytes == 0 || phys > 0xFFFFFFFFUL - bytes) {
        return FALSE;
    }
    end = phys + bytes;
    if (phys < 0x100000UL || end > 0xFEC00000UL) {
        return FALSE;
    }
    if (w->UseWindows) {
        for (i = 0; i < w->Windows; i++) {
            if (phys < w->WinHi[i] && end > w->WinLo[i]) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* `bytes` at `phys` in pieces that each stay within XHA_MAP_MAX counted
 * from their first page; `bios` the areas below 1 MB instead of the
 * bounds. */
static BOOLEAN xhaRead(XHA_WORK *w, ULONG phys, UCHAR *out, ULONG bytes,
                       BOOLEAN bios)
{
    ULONG done;
    ULONG piece;
    ULONG at;

    if (bios) {
        if (bytes == 0 || phys > 0x100000UL || bytes > 0x100000UL - phys) {
            return FALSE;
        }
    } else if (!xhaBounds(w, phys, bytes)) {
        return FALSE;
    }
    for (done = 0; done < bytes; done += piece) {
        at = phys + done;
        piece = XHA_MAP_MAX - (at & 0xFFFUL);
        if (piece > bytes - done) {
            piece = bytes - done;
        }
        if (!w->H->Read(w->H->Context, at, out + done, piece)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The root pointer into w->Rsdp: TRUE when found. */
static BOOLEAN xhaRsdp(XHA_WORK *w, UCHAR *area)
{
    UCHAR seg[2];
    ULONG ebda;
    ULONG at;
    ULONG len;

    at = 0xFFFFFFFFUL;
    if (xhaRead(w, 0x40EUL, seg, 2, TRUE)) {
        ebda = ((ULONG)seg[0] | ((ULONG)seg[1] << 8)) << 4;
        /* Its first 1 KB, but never past 0xA0000, where the
         * EBDA's conventional memory ends and the video window begins. */
        if (ebda >= 0x80000UL && ebda < 0xA0000UL) {
            len = 0xA0000UL - ebda;
            if (len > 1024UL) {
                len = 1024UL;
            }
            if (xhaRead(w, ebda, area, len, TRUE)) {
                at = XhaFindRsdp(area, len);
            }
        }
    }
    if (at == 0xFFFFFFFFUL && xhaRead(w, 0xE0000UL, area, 0x20000UL, TRUE)) {
        at = XhaFindRsdp(area, 0x20000UL);
    }
    if (at == 0xFFFFFFFFUL) {
        return FALSE;
    }
    len = area[at + 15] < 2 ? 20UL : xhaGet(area, at + 20);
    for (w->RsdpLen = 0; w->RsdpLen < len; w->RsdpLen++) {
        w->Rsdp[w->RsdpLen] = area[at + w->RsdpLen];
    }
    return TRUE;
}

/* Whether the reading's 20 s have passed. */
static BOOLEAN xhaLate(XHA_WORK *w)
{
    return (BOOLEAN)(w->H->NowMs(w->H->Context) - w->Start > XHA_TIMEOUT_MS);
}

/* A table's header at `phys` into hdr: TRUE when read. */
static BOOLEAN xhaHeader(XHA_WORK *w, ULONG phys, UCHAR *hdr)
{
    return (BOOLEAN)(phys != 0 && xhaRead(w, phys, hdr, XHA_TBL_HDR, FALSE));
}

/* A whole table at `phys` of `len` bytes into a new allocation, checked:
 * its header's signature `sig` and length `len`, its checksum. NULL with
 * *reason. */
static UCHAR *xhaWhole(XHA_WORK *w, ULONG phys, ULONG sig, ULONG len,
                       PULONG reason)
{
    UCHAR *t;

    if (!xhaBounds(w, phys, len)) {
        *reason = XHA_R_BOUNDS;
        return NULL;
    }
    t = (UCHAR *)w->H->Alloc(w->H->Context, len);
    if (t == NULL) {
        *reason = XHA_R_NOMEM;
        return NULL;
    }
    if (!xhaRead(w, phys, t, len, FALSE)) {
        *reason = XHA_R_READ;
    } else if (xhaGet(t, 0) != sig) {
        *reason = XHA_R_SIGNATURE;
    } else if (xhaGet(t, 4) != len) {
        *reason = XHA_R_LENGTH;
    } else if (!xhaSumZero(t, len)) {
        *reason = XHA_R_CHECKSUM;
    } else {
        *reason = XHA_R_NONE;
        return t;
    }
    w->H->Free(w->H->Context, t);
    return NULL;
}

/* The root table into a new allocation (*len, *xs): the XSDT below 4 GB,
 * else the RSDT, its checksum enforced. NULL when refused. */
static UCHAR *xhaRoot(XHA_WORK *w, PULONG len, BOOLEAN *xs)
{
    UCHAR hdr[XHA_TBL_HDR];
    UCHAR *t;
    ULONG phys;
    ULONG l;
    ULONG reason;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        *xs = (BOOLEAN)(pass == 0);
        if (pass == 0) {
            if (w->Rsdp[15] < 2 || w->RsdpLen < 36UL ||
                xhaGet(w->Rsdp, 28) != 0 || xhaGet(w->Rsdp, 24) == 0) {
                continue;
            }
            phys = xhaGet(w->Rsdp, 24);
        } else {
            phys = xhaGet(w->Rsdp, 16);
        }
        if (!xhaHeader(w, phys, hdr)) {
            continue;
        }
        l = xhaGet(hdr, 4);
        if (xhaGet(hdr, 0) != (*xs ? XHA_SIG('X', 'S', 'D', 'T')
                                   : XHA_SIG('R', 'S', 'D', 'T')) ||
            l < XHA_TBL_HDR ||
            l > XHA_TBL_HDR + (*xs ? 8UL : 4UL) * XHA_MAX_ROOT ||
            ((l - XHA_TBL_HDR) % (*xs ? 8UL : 4UL)) != 0) {
            continue;
        }
        t = xhaWhole(w, phys, xhaGet(hdr, 0), l, &reason);
        if (t != NULL) {
            *len = l;
            return t;
        }
    }
    return NULL;
}

/* Root entry n's address: FALSE past the table or above 4 GB (*phys 0). */
static BOOLEAN xhaEntry(const UCHAR *root, ULONG len, BOOLEAN xs, ULONG n,
                        PULONG phys)
{
    ULONG at;

    *phys = 0;
    at = XHA_TBL_HDR + n * (xs ? 8UL : 4UL);
    if (at + (xs ? 8UL : 4UL) > len) {
        return FALSE;
    }
    if (xs && xhaGet(root, at + 4) != 0) {
        return TRUE;
    }
    *phys = xhaGet(root, at);
    return TRUE;
}

/* Entry i planned: its header kept, its whole if the budget allows. */
static VOID xhaPlan(XHA_WORK *w, ULONG i, PULONG budget, PULONG flags)
{
    XHA_ENT *e;

    e = &w->E[i];
    if (e->How != XHA_H_WHOLE) {
        return;
    }
    if (e->Length < XHA_TBL_HDR) {
        e->How = XHA_H_HEADER;
        e->Reason = XHA_R_LENGTH;
        return;
    }
    if (e->Length > e->Cap) {
        e->How = XHA_H_HEADER;
        e->Reason = XHA_R_LIMIT;
        return;
    }
    /* Its header is reserved already: the rest of it. */
    if (e->Length - XHA_TBL_HDR > *budget) {
        e->How = XHA_H_HEADER;
        e->Reason = XHA_R_LIMIT;
        *flags |= XHA_F_CUT;
        return;
    }
    *budget -= e->Length - XHA_TBL_HDR;
}

ULONG XhaReadSet(const XHA_HOST *h, UCHAR **body, PULONG bytes,
                 PULONG count, PULONG flags, PULONG outcome)
{
    XHA_WORK *w;
    XHA_ENT *e;
    UCHAR *area;
    UCHAR *root;
    UCHAR *t;
    UCHAR *out;
    UCHAR hdr[XHA_TBL_HDR];
    ULONG rootLen;
    ULONG n;
    ULONG i;
    ULONG k;
    ULONG phys;
    ULONG mcfg;
    ULONG mcfgAt;
    ULONG fadtAt;
    ULONG dsdt;
    ULONG budget;
    ULONG size;
    ULONG at;
    ULONG reason;
    ULONG state;
    BOOLEAN xs;

    *body = NULL;
    *bytes = 0;
    *count = 0;
    *flags = 0;
    *outcome = 0;
    w = (XHA_WORK *)h->Alloc(h->Context, sizeof(XHA_WORK));
    area = (UCHAR *)h->Alloc(h->Context, 0x20000UL);
    root = NULL;
    state = XHA_S_NOMEM;
    if (w == NULL || area == NULL) {
        goto done;
    }
    for (i = 0; i < sizeof(XHA_WORK); i++) {
        ((UCHAR *)w)[i] = 0;
    }
    w->H = h;
    w->Start = h->NowMs(h->Context);
    /* The root pointer and the root table. */
    if (!xhaRsdp(w, area)) {
        state = XHA_S_NO_RSDP;
        goto done;
    }
    root = xhaLate(w) ? NULL : xhaRoot(w, &rootLen, &xs);
    if (root == NULL && xhaLate(w)) {
        state = XHA_S_REFUSED;
        *outcome = 4;
        goto done;
    }
    if (root == NULL) {
        state = XHA_S_REFUSED;
        *outcome = 1;
        goto done;
    }
    w->E[0].Sig = XHA_SIG('R', 'S', 'D', 'P');
    w->E[0].Kind = XHA_K_RSDP;
    w->E[0].Length = w->RsdpLen;
    w->E[0].How = XHA_H_WHOLE;
    w->E[1].Sig = xhaGet(root, 0);
    w->E[1].Kind = XHA_K_ROOT;
    w->E[1].Length = rootLen;
    w->E[1].How = XHA_H_WHOLE;
    /* The bootstrap pass: the headers, to find MCFG. */
    mcfg = 0;
    mcfgAt = 0;
    /* Every entry must be classified: one whose header cannot be read
     * could be an MCFG, and no window would then be known. */
    k = 0;
    for (n = 0; xhaEntry(root, rootLen, xs, n, &phys); n++) {
        if (xhaLate(w)) {
            state = XHA_S_REFUSED;
            *outcome = 4;
            goto done;
        }
        if (!xhaHeader(w, phys, hdr)) {
            k++;
        } else if (xhaGet(hdr, 0) == XHA_SIG('M', 'C', 'F', 'G')) {
            mcfg++;
            mcfgAt = n;
        }
    }
    w->Count = 2UL + n;
    if (k != 0) {
        state = XHA_S_REFUSED;
        *outcome = 3;
        goto done;
    }
    if (mcfg > 1) {
        state = XHA_S_REFUSED;
        *outcome = 2;
        goto done;
    }
    /* The windows are not known yet: past the bound, refused. */
    if (xhaLate(w)) {
        state = XHA_S_REFUSED;
        *outcome = 4;
        goto done;
    }
    if (mcfg == 1) {
        (VOID)xhaEntry(root, rootLen, xs, mcfgAt, &phys);
        t = NULL;
        if (xhaHeader(w, phys, hdr) &&
            xhaGet(hdr, 0) == XHA_SIG('M', 'C', 'F', 'G') &&
            xhaGet(hdr, 4) >= 44UL && xhaGet(hdr, 4) <= XHA_MCFG_CAP) {
            t = xhaWhole(w, phys, XHA_SIG('M', 'C', 'F', 'G'),
                         xhaGet(hdr, 4), &reason);
        }
        if (t == NULL ||
            !XhaMcfgWindows(t, xhaGet(hdr, 4), w->WinLo, w->WinHi,
                            &w->Windows)) {
            if (t != NULL) {
                h->Free(h->Context, t);
            }
            state = XHA_S_REFUSED;
            *outcome = 2;
            goto done;
        }
        h->Free(h->Context, t);
    }
    w->UseWindows = TRUE;
    /* Each entry's header again, now outside every window too. */
    fadtAt = 0xFFFFFFFFUL;
    for (n = 0; n + 2UL < w->Count; n++) {
        e = &w->E[2UL + n];
        e->Kind = XHA_K_ENTRY | (n << 16);
        (VOID)xhaEntry(root, rootLen, xs, n, &phys);
        e->Phys = phys;
        if (xhaLate(w)) {
            e->How = XHA_H_NONE;
            e->Reason = XHA_R_TIMEOUT;
            continue;
        }
        if (!xhaHeader(w, phys, e->Hdr)) {
            e->How = XHA_H_NONE;
            e->Reason = phys == 0 || !xhaBounds(w, phys, XHA_TBL_HDR)
                            ? XHA_R_BOUNDS
                            : XHA_R_READ;
            continue;
        }
        e->Sig = xhaGet(e->Hdr, 0);
        e->Length = xhaGet(e->Hdr, 4);
        e->How = XHA_H_HEADER;
        if (e->Sig == XHA_SIG('F', 'A', 'C', 'P')) {
            e->Cap = XHA_FACP_CAP;
            e->How = XHA_H_WHOLE;
            if (fadtAt == 0xFFFFFFFFUL) {
                fadtAt = n;
            }
        } else if (e->Sig == XHA_SIG('S', 'S', 'D', 'T')) {
            e->Cap = XHA_SSDT_CAP;
            e->How = XHA_H_WHOLE;
        } else if (e->Sig == XHA_SIG('M', 'C', 'F', 'G')) {
            e->Cap = XHA_MCFG_CAP;
            e->How = XHA_H_WHOLE;
        }
    }
    /* The DSDT the FADT names: its header now, its whole with the rest. */
    if (fadtAt != 0xFFFFFFFFUL && !xhaLate(w)) {
        e = &w->E[2UL + fadtAt];
        reason = XHA_R_LENGTH;
        t = NULL;
        if (e->Length >= XHA_TBL_HDR && e->Length <= e->Cap) {
            t = xhaWhole(w, e->Phys, e->Sig, e->Length, &reason);
        }
        dsdt = 0;
        if (t != NULL) {
            (VOID)XhaFadtDsdt(t, e->Length, &dsdt);
            h->Free(h->Context, t);
        }
        if (dsdt != 0 && !xhaLate(w)) {
            e = &w->E[w->Count];
            w->Count++;
            e->Kind = XHA_K_DSDT | (fadtAt << 16);
            e->Phys = dsdt;
            if (!xhaHeader(w, dsdt, e->Hdr)) {
                e->How = XHA_H_NONE;
                e->Reason = !xhaBounds(w, dsdt, XHA_TBL_HDR) ? XHA_R_BOUNDS
                                                             : XHA_R_READ;
            } else {
                e->Sig = xhaGet(e->Hdr, 0);
                e->Length = xhaGet(e->Hdr, 4);
                e->Cap = XHA_DSDT_CAP;
                e->How = XHA_H_WHOLE;
                if (e->Sig != XHA_SIG('D', 'S', 'D', 'T')) {
                    e->How = XHA_H_HEADER;
                    e->Reason = XHA_R_SIGNATURE;
                }
            }
        }
    }
    /* The budget: the pointer, the root, every header, then the wholes in
     * order. */
    budget = XHA_DATA_MAX - w->RsdpLen - rootLen;
    for (i = 2; i < w->Count; i++) {
        if (w->E[i].How != XHA_H_NONE) {
            budget -= XHA_TBL_HDR;
        }
    }
    for (i = 2; i < w->Count; i++) {
        xhaPlan(w, i, &budget, flags);
    }
    /* The body: the entries, then each piece held at its own place. */
    size = w->Count * XHA_ENT_BYTES;
    for (i = 0; i < w->Count; i++) {
        e = &w->E[i];
        e->Offset = size;
        e->Held = e->How == XHA_H_WHOLE ? e->Length
                : e->How == XHA_H_HEADER ? XHA_TBL_HDR
                                         : 0;
        size += xhaRound(e->Held);
    }
    out = (UCHAR *)h->Alloc(h->Context, size);
    if (out == NULL) {
        state = XHA_S_NOMEM;
        goto done;
    }
    for (i = 0; i < size; i++) {
        out[i] = 0;
    }
    for (i = 0; i < w->RsdpLen; i++) {
        out[w->E[0].Offset + i] = w->Rsdp[i];
    }
    for (i = 0; i < rootLen; i++) {
        out[w->E[1].Offset + i] = root[i];
    }
    for (i = 2; i < w->Count; i++) {
        e = &w->E[i];
        if (e->How == XHA_H_WHOLE) {
            if (xhaLate(w)) {
                e->How = XHA_H_HEADER;
                e->Reason = XHA_R_TIMEOUT;
            } else {
                t = xhaWhole(w, e->Phys, e->Sig, e->Length, &reason);
                if (t != NULL) {
                    for (k = 0; k < e->Length; k++) {
                        out[e->Offset + k] = t[k];
                    }
                    h->Free(h->Context, t);
                } else {
                    e->How = XHA_H_HEADER;
                    e->Reason = reason;
                }
            }
        }
        if (e->How == XHA_H_HEADER) {
            /* A whole planned and not kept leaves its place, zeroed, with
             * the header at its start. */
            for (k = 0; k < XHA_TBL_HDR; k++) {
                out[e->Offset + k] = e->Hdr[k];
            }
            e->Held = XHA_TBL_HDR;
        }
        if (e->How == XHA_H_NONE) {
            *flags |= XHA_F_INCOMPLETE;
        }
        if (e->Cap != 0 && e->How != XHA_H_WHOLE) {
            *flags |= XHA_F_INCOMPLETE;
        }
        if (e->Reason == XHA_R_NOMEM) {
            *flags |= XHA_F_NOMEM;
        }
    }
    /* Flags from what is kept. */
    for (i = 2, k = 0; i < w->Count; i++) {
        if ((w->E[i].Kind & 0xFFUL) == XHA_K_DSDT &&
            w->E[i].How == XHA_H_WHOLE) {
            k = 1;
        }
    }
    if (k == 0) {
        *flags |= XHA_F_NO_DSDT;
    }
    for (i = 0; i < w->Count; i++) {
        e = &w->E[i];
        at = i * XHA_ENT_BYTES;
        xhaPut(out, at + XHA_E_SIG, e->Sig);
        xhaPut(out, at + XHA_E_KIND, e->Kind | (e->How << 8));
        xhaPut(out, at + XHA_E_LENGTH, e->Length);
        xhaPut(out, at + XHA_E_OFFSET, e->Held != 0 ? e->Offset : 0);
        xhaPut(out, at + XHA_E_HELD, e->Held);
        xhaPut(out, at + XHA_E_REASON, e->Reason);
    }
    *body = out;
    *bytes = size;
    *count = w->Count;
    state = XHA_S_READ;
done:
    if (root != NULL) {
        h->Free(h->Context, root);
    }
    if (area != NULL) {
        h->Free(h->Context, area);
    }
    if (w != NULL) {
        h->Free(h->Context, w);
    }
    return state;
}
