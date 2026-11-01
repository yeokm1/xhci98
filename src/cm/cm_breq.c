/*
 * cm_breq.c - the requirement lists of hot-added bridges on Windows 2000
 * and XP (design record 33 sections 5.2, 6 and 9.2; roadmap-usb4.md task
 * 45.12). cm_breq.h has the contract.
 *
 * The templates are the closed lists read in task 44.63 (record 33
 * section 2.1, the evidence `xp\reqs-a.txt` and `w2k\reqs-w.txt`): every
 * field the PCI driver wrote is compared but the header's interface type,
 * bus and slot, so a list that differs by one field is left as it came.
 * XP x64's closed lists, read in task 45.55 (record 33 section 7.7), are
 * XP's byte for byte, so XP x64 takes XP's template.
 * The device-private descriptors are the PCI driver's own and are never
 * invented, changed or reordered: each follows its window group and goes
 * with it, kept or dropped whole.
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any.
 */

#include "cm_breq.h"
#include "cm_irq.h"

/* ---- bytes ------------------------------------------------------------ */

static ULONG bqGet32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static VOID bqPut32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static VOID bqCopy(UCHAR *to, const UCHAR *from, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        to[i] = from[i];
    }
}

/* ---- the templates (section 6.1) -------------------------------------- */

/* The IO_RESOURCE_DESCRIPTOR fields at their offsets (cm_irq.h has the
 * first four; the union at 8: Length, Alignment, MinimumAddress low and
 * high, MaximumAddress low and high; a private descriptor's Data[3]). */
#define BQ_D_OPTION     0UL
#define BQ_D_TYPE       1UL
#define BQ_D_LENGTH     8UL
#define BQ_D_ALIGN      12UL
#define BQ_D_MIN_LO     16UL
#define BQ_D_MAX_LO     24UL
#define BQ_D_MAX_HI     28UL

#define BQ_OPT_ALT      0x08UL          /* IO_RESOURCE_ALTERNATIVE        */
#define BQ_TYPE_PORT    1UL             /* CmResourceTypePort             */
#define BQ_TYPE_INTR    2UL             /* CmResourceTypeInterrupt        */
#define BQ_TYPE_MEM     3UL             /* CmResourceTypeMemory           */
#define BQ_TYPE_PRIV    0x81UL          /* CmResourceTypeDevicePrivate    */

/* The two templates' shapes: XP's 18 descriptors and 2000's 6. */
#define BQ_XP_BYTES     0x268UL
#define BQ_XP_COUNT     18UL
#define BQ_2K_BYTES     0xE8UL
#define BQ_2K_COUNT     6UL
#define BQ_LADDER_XP    7UL             /* 64 MB down to 1 MB             */
/* The bridge's own interrupt, after the template on a bridge whose
 * Interrupt Pin is set (a hot-plug capable switch port), as read on XP in
 * task 45.55: one descriptor, kept unchanged at the end. */
#define BQ_IRQ_SHARE    3UL             /* CmResourceShareShared          */
#define BQ_IRQ_MAXV     0xFFUL          /* MinimumVector 0, MaximumVector */

/* One expected descriptor, as 32 bytes. */
static VOID bqDesc(UCHAR *d, ULONG option, ULONG type, ULONG share,
                   ULONG flags, ULONG length, ULONG align, ULONG maxLo,
                   ULONG maxHi)
{
    ULONG i;

    for (i = 0; i < CM_IRQ_IOD_SIZE; i++) {
        d[i] = 0;
    }
    d[BQ_D_OPTION] = (UCHAR)option;
    d[BQ_D_TYPE] = (UCHAR)type;
    d[CM_IRQ_IOD_SHARE_AT] = (UCHAR)share;
    d[CM_IRQ_IOD_FLAGS_AT] = (UCHAR)(flags & 0xFFUL);
    d[CM_IRQ_IOD_FLAGS_AT + 1] = (UCHAR)((flags >> 8) & 0xFFUL);
    bqPut32(d + BQ_D_LENGTH, length);
    bqPut32(d + BQ_D_ALIGN, align);
    bqPut32(d + BQ_D_MAX_LO, maxLo);
    bqPut32(d + BQ_D_MAX_HI, maxHi);
}

/* The PCI driver's private descriptor after group k: (1, k + 2, 0). */
static VOID bqPriv(UCHAR *d, ULONG k)
{
    bqDesc(d, 0, BQ_TYPE_PRIV, 1, 0, 1, k + 2UL, 0, 0);
}

/* Descriptor `i` of the template `shape`, into d[32]. */
static VOID bqTemplate(ULONG shape, ULONG i, UCHAR *d)
{
    ULONG k;
    ULONG j;
    ULONG share;
    ULONG flags;
    ULONG maxHi;
    ULONG per;

    per = shape == CM_BREQ_SHAPE_XP ? BQ_LADDER_XP : 1UL;
    if (i == 0) {
        bqDesc(d, 0, BQ_TYPE_PORT, 1, 0xB1UL, 0x1000UL, 0x1000UL, 0xFFFFUL,
               0);
        return;
    }
    if (i == 1) {
        bqPriv(d, CM_BREQ_K_IO);
        return;
    }
    /* Then memory and prefetchable: `per` descriptors and a private one. */
    k = CM_BREQ_K_MEM + (i - 2UL) / (per + 1UL);
    j = (i - 2UL) % (per + 1UL);
    if (j == per) {
        bqPriv(d, k);
        return;
    }
    share = shape == CM_BREQ_SHAPE_XP ? 0UL : 1UL;
    flags = k == CM_BREQ_K_PREF ? 4UL : 0UL;
    maxHi = k == CM_BREQ_K_PREF ? 0xFFFFFFFFUL : 0UL;
    bqDesc(d, j == 0 ? 0UL : BQ_OPT_ALT, BQ_TYPE_MEM, share, flags,
           (shape == CM_BREQ_SHAPE_XP ? 0x4000000UL : 0x100000UL) >> j,
           0x100000UL, 0xFFFFFFFFUL, maxHi);
}

/* The trailing interrupt descriptor (BQ_IRQ_*): Option 0, level, shared,
 * vectors 0 to FFh, every other byte 0. */
static VOID bqIrq(UCHAR *d)
{
    bqDesc(d, 0, BQ_TYPE_INTR, BQ_IRQ_SHARE, 0, 0, BQ_IRQ_MAXV, 0, 0);
}

ULONG CmBreqTail(const UCHAR *list)
{
    if (list == NULL) {
        return 0;
    }
    return bqGet32(list) == BQ_XP_BYTES + CM_IRQ_IOD_SIZE ||
                   bqGet32(list) == BQ_2K_BYTES + CM_IRQ_IOD_SIZE
               ? 1UL
               : 0UL;
}

ULONG CmBreqShape(const UCHAR *list, ULONG minor)
{
    UCHAR want[CM_IRQ_IOD_SIZE];
    const UCHAR *at;
    ULONG shape;
    ULONG bytes;
    ULONG count;
    ULONG tail;
    ULONG i;
    ULONG b;

    if (list == NULL) {
        return CM_BREQ_SHAPE_NONE;
    }
    if (minor == 1 || minor == 2) {
        /* XP x64 (NT 5.2) answered XP's list byte for byte (task 45.55,
         * record 33 section 7.7, revision 5). */
        shape = CM_BREQ_SHAPE_XP;
        bytes = BQ_XP_BYTES;
        count = BQ_XP_COUNT;
    } else if (minor == 0) {
        shape = CM_BREQ_SHAPE_2000;
        bytes = BQ_2K_BYTES;
        count = BQ_2K_COUNT;
    } else {
        return CM_BREQ_SHAPE_NONE;
    }
    /* The template alone, or followed by the bridge's interrupt. */
    if (bqGet32(list) == bytes) {
        tail = 0;
    } else if (bqGet32(list) == bytes + CM_IRQ_IOD_SIZE) {
        tail = 1;
    } else {
        return CM_BREQ_SHAPE_NONE;
    }
    if (bqGet32(list + CM_IRQ_REQ_ALTS_AT) != 1) {
        return CM_BREQ_SHAPE_NONE;
    }
    /* Reserved[3], between the slot and the alternatives' count. */
    for (b = 16; b < CM_IRQ_REQ_ALTS_AT; b++) {
        if (list[b] != 0) {
            return CM_BREQ_SHAPE_NONE;
        }
    }
    at = list + CM_IRQ_REQ_HDR;
    if (at[0] != 1 || at[1] != 0 || at[2] != 1 || at[3] != 0 ||
        bqGet32(at + CM_IRQ_IOL_COUNT_AT) != count + tail) {
        return CM_BREQ_SHAPE_NONE;
    }
    at += CM_IRQ_IOL_HDR;
    for (i = 0; i < count + tail; i++) {
        if (i < count) {
            bqTemplate(shape, i, want);
        } else {
            bqIrq(want);
        }
        for (b = 0; b < CM_IRQ_IOD_SIZE; b++) {
            if (at[b] != want[b]) {
                return CM_BREQ_SHAPE_NONE;
            }
        }
        at += CM_IRQ_IOD_SIZE;
    }
    return shape;
}

/* Where group k starts in a template's descriptors, and how many window
 * descriptors it has (its private one follows them). */
static VOID bqGroup(ULONG shape, ULONG k, PULONG first, PULONG n)
{
    ULONG per;

    per = shape == CM_BREQ_SHAPE_XP ? BQ_LADDER_XP : 1UL;
    if (k == CM_BREQ_K_IO) {
        *first = 0;
        *n = 1;
        return;
    }
    *first = 2UL + (k - CM_BREQ_K_MEM) * (per + 1UL);
    *n = per;
}

/* ---- the parent's windows (section 6's arithmetic) -------------------- */

/* An open memory window's length from (base, limit) pairs, capped: D =
 * limit - base with a borrow, then CAP when D is 4 GB less 1 MB or more. */
static ULONG bqMemLen(ULONG baseHi, ULONG baseLo, ULONG limHi, ULONG limLo)
{
    ULONG dLo;
    ULONG dHi;
    ULONG borrow;

    if (baseHi > limHi || (baseHi == limHi && baseLo > limLo)) {
        return 0;
    }
    borrow = limLo < baseLo ? 1UL : 0UL;
    dLo = limLo - baseLo;
    dHi = limHi - baseHi - borrow;
    if (dHi != 0 || dLo >= CM_BREQ_CAP - 1UL) {
        return CM_BREQ_CAP;
    }
    return dLo + 1UL;
}

BOOLEAN CmBreqWindows(const ULONG *par, PULONG p)
{
    ULONG ioBase;
    ULONG ioLim;
    ULONG r;

    /* I/O: 1Ch base 7:4 and limit 15:12 give address bits 15:12; the upper
     * 16 bits at 30h where the window decodes 32 bits. */
    r = par[0];
    ioBase = (r & 0xF0UL) << 8;
    ioLim = (((r >> 8) & 0xF0UL) << 8) | 0xFFFUL;
    if ((r & 0x0FUL) == 1UL) {
        ioBase |= (par[5] & 0xFFFFUL) << 16;
        ioLim |= ((par[5] >> 16) & 0xFFFFUL) << 16;
    }
    if (ioBase > ioLim) {
        p[CM_BREQ_K_IO] = 0;
    } else if (ioLim > 0xFFFFUL) {
        return FALSE;
    } else {
        p[CM_BREQ_K_IO] = ioLim - ioBase + 1UL;
    }
    /* Memory: 20h base 15:4 and limit 31:20 are address bits 31:20. */
    r = par[1];
    p[CM_BREQ_K_MEM] = bqMemLen(0, (r & 0xFFF0UL) << 16, 0,
                                (r & 0xFFF00000UL) | 0xFFFFFUL);
    /* Prefetchable: 24h the same, with 28h and 2Ch where it decodes 64
     * bits. */
    r = par[2];
    if ((r & 0x0FUL) == 1UL) {
        p[CM_BREQ_K_PREF] = bqMemLen(par[3], (r & 0xFFF0UL) << 16, par[4],
                                     (r & 0xFFF00000UL) | 0xFFFFFUL);
    } else {
        p[CM_BREQ_K_PREF] = bqMemLen(0, (r & 0xFFF0UL) << 16, 0,
                                     (r & 0xFFF00000UL) | 0xFFFFFUL);
    }
    return TRUE;
}

/* ---- the claims (section 5.2) ----------------------------------------- */

BOOLEAN CmBreqBars(const ULONG *bars, PULONG io, PULONG pref)
{
    ULONG i;
    ULONG v;

    *io = 0;
    *pref = 0;
    for (i = 0; i < 6; i++) {
        v = bars[i];
        if ((v & 1UL) != 0) {
            *io = 1;
            continue;
        }
        if ((v & 8UL) != 0) {
            *pref = 1;
        }
        switch ((v >> 1) & 3UL) {
        case 0:
        case 1:
            break;
        case 2:
            if (i == 5) {
                return FALSE;
            }
            /* Its upper dword, never a BAR of its own. */
            i++;
            break;
        default:
            return FALSE;
        }
    }
    return TRUE;
}

VOID CmBreqClaims(const CM_BREQ_SIB *sib, ULONG prefWin, PULONG c)
{
    switch (sib->Behind) {
    case CM_BREQ_B_NONE:
        if (sib->HotPlug) {
            c[CM_BREQ_K_IO] = CM_BREQ_C_UNKNOWN;
            c[CM_BREQ_K_MEM] = CM_BREQ_C_YES;
            c[CM_BREQ_K_PREF] = CM_BREQ_C_YES;
        } else {
            c[CM_BREQ_K_IO] = CM_BREQ_C_NO;
            c[CM_BREQ_K_MEM] = CM_BREQ_C_GRANULE;
            c[CM_BREQ_K_PREF] = CM_BREQ_C_NO;
        }
        break;
    case CM_BREQ_B_TYPE0:
        c[CM_BREQ_K_IO] = sib->AnyIo ? CM_BREQ_C_YES : CM_BREQ_C_NO;
        c[CM_BREQ_K_MEM] = CM_BREQ_C_YES;
        c[CM_BREQ_K_PREF] = sib->AnyPref ? CM_BREQ_C_YES : CM_BREQ_C_NO;
        break;
    default:
        c[CM_BREQ_K_IO] = CM_BREQ_C_UNKNOWN;
        c[CM_BREQ_K_MEM] = CM_BREQ_C_YES;
        c[CM_BREQ_K_PREF] = CM_BREQ_C_YES;
        break;
    }
    if (!prefWin && c[CM_BREQ_K_PREF] != CM_BREQ_C_NO) {
        /* The PCI driver may place such a BAR in the memory window. */
        c[CM_BREQ_K_PREF] = CM_BREQ_C_NO;
        c[CM_BREQ_K_MEM] = CM_BREQ_C_YES;
    }
}

/* ---- the ladders ------------------------------------------------------- */

static ULONG bqGran(ULONG k)
{
    return k == CM_BREQ_K_IO ? CM_BREQ_G_IO : CM_BREQ_G_MEM;
}

static ULONG bqFloor(ULONG k, ULONG x)
{
    return x & ~(bqGran(k) - 1UL);
}

static VOID bqStep(PCM_BREQ_PLAN p, ULONG k, ULONG len)
{
    if (p->Steps[k] < CM_BREQ_STEPS_MAX) {
        p->Len[k][p->Steps[k]] = len;
        p->Steps[k]++;
    } else {
        /* Full: the smallest so far gives way to this smaller one, so the
         * ladder still ends at its least. */
        p->Len[k][CM_BREQ_STEPS_MAX - 1UL] = len;
    }
}

/* Rule 6.2.4: P(k) preferred, halved down to and including max(2g, the
 * OS's least), then the OS's least where smaller; I/O steps by 4 KB. */
static VOID bqUpLadder(PCM_BREQ_PLAN p, ULONG k)
{
    ULONG g;
    ULONG x;
    ULONG h;
    ULONG bound;
    ULONG last;

    g = bqGran(k);
    x = p->P[k];
    p->Steps[k] = 0;
    p->Sized[k] = 1;
    if (k == CM_BREQ_K_IO) {
        while (x >= g) {
            bqStep(p, k, x);
            x -= g;
        }
        return;
    }
    bound = p->OsMin[k] > 2UL * g ? p->OsMin[k] : 2UL * g;
    bqStep(p, k, x);
    last = x;
    for (;;) {
        h = bqFloor(k, x / 2UL);
        if (h <= bound) {
            if (x > bound) {
                bqStep(p, k, bound);
                last = bound;
            }
            break;
        }
        bqStep(p, k, h);
        last = h;
        x = h;
    }
    if (p->OsMin[k] < last && p->OsMin[k] >= g) {
        bqStep(p, k, p->OsMin[k]);
    }
}

/* Rule 6.3.3: a preferred length halved down to the granule. */
static VOID bqDownLadder(PCM_BREQ_PLAN p, ULONG k, ULONG pref)
{
    ULONG g;
    ULONG x;

    g = bqGran(k);
    p->Steps[k] = 0;
    p->Sized[k] = 1;
    x = pref;
    bqStep(p, k, x);
    while (x > g) {
        x = bqFloor(k, x / 2UL);
        if (x < g) {
            x = g;
        }
        bqStep(p, k, x);
    }
}

/* ---- the rules (sections 6.2 to 6.4) ---------------------------------- */

static VOID bqDrop(PCM_BREQ_PLAN p, ULONG k)
{
    p->Keep[k] = 0;
    p->Sized[k] = 0;
    p->Dropped |= 1UL << k;
}

/* Rule 6.2.1 (6.3.1 for a downstream port): a kind the parent lacks. */
static VOID bqDropAbsent(PCM_BREQ_PLAN p)
{
    ULONG k;

    for (k = 0; k < CM_BREQ_KINDS; k++) {
        if (p->P[k] == 0) {
            bqDrop(p, k);
        }
    }
}

/* Rule 6.3 for this port: every sibling's claims from the snapshot, the
 * order, the budget and the shares, per kind. */
static VOID bqDown(const CM_BREQ_SNAP *s, PCM_BREQ_PLAN p)
{
    ULONG claims[CM_BREQ_SIBS_MAX][CM_BREQ_KINDS];
    ULONG granted[CM_BREQ_SIBS_MAX];
    ULONG order[CM_BREQ_SIBS_MAX];
    ULONG classes[3];
    ULONG n;
    ULONG i;
    ULONG j;
    ULONG c;
    ULONG k;
    ULONG g;
    ULONG used;
    ULONG sharers;
    ULONG share;
    ULONG mine;

    for (i = 0; i < s->Sibs; i++) {
        CmBreqClaims(&s->Sib[i], (ULONG)(p->P[CM_BREQ_K_PREF] != 0),
                     claims[i]);
    }
    p->Claims = claims[s->Index][CM_BREQ_K_IO] |
                (claims[s->Index][CM_BREQ_K_MEM] << 2) |
                (claims[s->Index][CM_BREQ_K_PREF] << 4);
    classes[0] = CM_BREQ_C_YES;
    classes[1] = CM_BREQ_C_GRANULE;
    classes[2] = CM_BREQ_C_UNKNOWN;
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        mine = claims[s->Index][k];
        if (p->P[k] == 0) {
            /* Rule 6.3.1, already dropped: a "yes" is an unmet need. */
            if (mine == CM_BREQ_C_YES) {
                p->Unmet |= 1UL << k;
            }
            continue;
        }
        g = bqGran(k);
        n = 0;
        for (c = 0; c < 3; c++) {
            for (i = 0; i < s->Sibs; i++) {
                if (claims[i][k] == classes[c]) {
                    order[n++] = i;
                }
            }
        }
        for (i = 0; i < s->Sibs; i++) {
            granted[i] = 0;
        }
        used = 0;
        sharers = 0;
        for (j = 0; j < n; j++) {
            if (p->P[k] - used >= g) {
                granted[order[j]] = 1;
                used += g;
                if (claims[order[j]][k] != CM_BREQ_C_GRANULE) {
                    sharers++;
                }
            }
        }
        if (mine == CM_BREQ_C_NO || !granted[s->Index]) {
            if (mine == CM_BREQ_C_YES) {
                p->Unmet |= 1UL << k;
            }
            bqDrop(p, k);
            continue;
        }
        p->Keep[k] = 1;
        p->Sized[k] = 1;
        p->Steps[k] = 0;
        if (k == CM_BREQ_K_IO || mine == CM_BREQ_C_GRANULE) {
            bqStep(p, k, g);
            continue;
        }
        share = sharers != 0 ? bqFloor(k, (p->P[k] - used) / sharers) : 0;
        bqDownLadder(p, k, g + share);
    }
}

ULONG CmBreqPlan(const UCHAR *list, ULONG minor, const CM_BREQ_SNAP *s,
                 PCM_BREQ_PLAN p)
{
    ULONG k;
    ULONG i;
    ULONG first;
    ULONG n;
    ULONG count;
    ULONG allDsp;
    ULONG keep;
    ULONG changed;
    ULONG reason;
    ULONG sibsOk;
    const UCHAR *d;

    {
        UCHAR *z;
        ULONG b;

        z = (UCHAR *)p;
        for (b = 0; b < (ULONG)sizeof(*p); b++) {
            z[b] = 0;
        }
    }
    p->Role = s->Role;
    p->Shape = CmBreqShape(list, minor);
    if (p->Shape != CM_BREQ_SHAPE_NONE) {
        p->Tail = CmBreqTail(list);
    }
    /* The siblings read whole and within the snapshot's room; anything
     * else is no inventory to divide by. */
    sibsOk = (ULONG)(s->SibsRead && s->Sibs <= CM_BREQ_SIBS_MAX &&
                     s->Index < s->Sibs);
    if (s->Alone == CM_BREQ_YES) {
        p->Flags |= 1UL;
    }
    if (s->Role == CM_BREQ_ROLE_UP) {
        p->Count = s->N;
        if (s->SecNumbered) {
            p->Flags |= 2UL;
        }
    } else if (s->Role == CM_BREQ_ROLE_DOWN && sibsOk &&
               s->Index < s->Sibs) {
        p->Count = s->Sibs;
        p->Index = s->Index;
        if (s->Sib[s->Index].Numbered) {
            p->Flags |= 2UL;
        }
        if (s->Sib[s->Index].HotPlug) {
            p->Flags |= 4UL;
        }
        if (s->Sib[s->Index].Behind == CM_BREQ_B_TYPE1) {
            p->Flags |= 8UL;
        }
    }
    if (p->Shape == CM_BREQ_SHAPE_NONE) {
        p->Outcome = CM_BREQ_OUT_SHAPE;
        return p->Outcome;
    }
    if (!s->ParentRead || !CmBreqWindows(s->Par, p->P)) {
        p->Outcome = CM_BREQ_OUT_PARENT;
        return p->Outcome;
    }
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        bqGroup(p->Shape, k, &first, &n);
        d = list + CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR + first * CM_IRQ_IOD_SIZE;
        p->OsPref[k] = bqGet32(d + BQ_D_LENGTH);
        p->OsMin[k] = bqGet32(d + (n - 1UL) * CM_IRQ_IOD_SIZE + BQ_D_LENGTH);
        if (p->P[k] > CM_BREQ_CAP) {
            p->P[k] = CM_BREQ_CAP;
        }
        p->Keep[k] = 1;
    }
    bqDropAbsent(p);
    reason = CM_BREQ_OUT_LEFT;
    if (s->Role == CM_BREQ_ROLE_UP) {
        /* Rules 6.2.2 to 6.2.4. */
        if (s->N == CM_BREQ_N_UNKNOWN || s->N >= 2) {
            if (s->Alone != CM_BREQ_YES) {
                reason = CM_BREQ_OUT_NOTALONE;
            } else {
                for (k = 0; k < CM_BREQ_KINDS; k++) {
                    if (p->Keep[k]) {
                        bqUpLadder(p, k);
                    }
                }
            }
        }
    } else if (s->Role == CM_BREQ_ROLE_DOWN && sibsOk &&
               s->Index < s->Sibs && s->Sibs >= 2) {
        /* Rule 6.4's last sentence: only where every function on the
         * upstream port's bus is a downstream port. */
        allDsp = 1;
        for (i = 0; i < s->Sibs; i++) {
            if (!s->Sib[i].Dsp) {
                allDsp = 0;
            }
        }
        if (allDsp) {
            bqDown(s, p);
        }
    } else if (s->Role == CM_BREQ_ROLE_DOWN && sibsOk &&
               s->Index < s->Sibs) {
        /* Rule 6.3.2, one sibling: the OS list but for 6.3.1; its unmet
         * needs are still counted. */
        {
            ULONG c[CM_BREQ_KINDS];

            CmBreqClaims(&s->Sib[s->Index],
                         (ULONG)(p->P[CM_BREQ_K_PREF] != 0), c);
            p->Claims = c[0] | (c[1] << 2) | (c[2] << 4);
            for (k = 0; k < CM_BREQ_KINDS; k++) {
                if (p->P[k] == 0 && c[k] == CM_BREQ_C_YES) {
                    p->Unmet |= 1UL << k;
                }
            }
        }
    }
    /* No list without a window (section 6.1). */
    keep = 0;
    changed = 0;
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        if (p->Keep[k]) {
            keep = 1;
        }
        if (!p->Keep[k] || p->Sized[k]) {
            changed = 1;
        }
    }
    if (!keep) {
        for (k = 0; k < CM_BREQ_KINDS; k++) {
            p->Keep[k] = 1;
            p->Sized[k] = 0;
            p->Steps[k] = 0;
        }
        p->Dropped = 0;
        p->Outcome = CM_BREQ_OUT_NOWIN;
        return p->Outcome;
    }
    if (!changed) {
        p->Outcome = reason;
        return p->Outcome;
    }
    count = 0;
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        if (!p->Keep[k]) {
            continue;
        }
        bqGroup(p->Shape, k, &first, &n);
        count += (p->Sized[k] ? p->Steps[k] : n) + 1UL;
    }
    count += p->Tail;
    p->Bytes = CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR + count * CM_IRQ_IOD_SIZE;
    p->Outcome = CM_BREQ_OUT_REWRITTEN;
    return p->Outcome;
}

ULONG CmBreqWrite(const UCHAR *list, const CM_BREQ_PLAN *p, UCHAR *out,
                  ULONG room)
{
    const UCHAR *src;
    UCHAR *at;
    ULONG k;
    ULONG first;
    ULONG n;
    ULONG j;
    ULONG count;
    ULONG total;

    if (p->Outcome != CM_BREQ_OUT_REWRITTEN || p->Bytes > room ||
        CmBreqShape(list, p->Shape == CM_BREQ_SHAPE_XP ? 1UL : 0UL) !=
            p->Shape ||
        CmBreqTail(list) != p->Tail) {
        return 0;
    }
    src = list + CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR;
    at = out + CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR;
    count = 0;
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        if (!p->Keep[k]) {
            continue;
        }
        bqGroup(p->Shape, k, &first, &n);
        if (!p->Sized[k]) {
            bqCopy(at, src + first * CM_IRQ_IOD_SIZE,
                   (n + 1UL) * CM_IRQ_IOD_SIZE);
            at += (n + 1UL) * CM_IRQ_IOD_SIZE;
            count += n + 1UL;
            continue;
        }
        for (j = 0; j < p->Steps[k]; j++) {
            /* The group's first descriptor (Type, ShareDisposition, Flags,
             * MaximumAddress), with this step's Option and Length. */
            bqCopy(at, src + first * CM_IRQ_IOD_SIZE, CM_IRQ_IOD_SIZE);
            at[BQ_D_OPTION] = (UCHAR)(j == 0 ? 0UL : BQ_OPT_ALT);
            bqPut32(at + BQ_D_LENGTH, p->Len[k][j]);
            bqPut32(at + BQ_D_ALIGN, bqGran(k));
            bqPut32(at + BQ_D_MIN_LO, 0);
            bqPut32(at + BQ_D_MIN_LO + 4UL, 0);
            at += CM_IRQ_IOD_SIZE;
            count++;
        }
        bqCopy(at, src + (first + n) * CM_IRQ_IOD_SIZE, CM_IRQ_IOD_SIZE);
        at += CM_IRQ_IOD_SIZE;
        count++;
    }
    if (p->Tail != 0) {
        /* The bridge's interrupt, the OS list's last descriptor, as it
         * came. */
        total = bqGet32(list + CM_IRQ_REQ_HDR + CM_IRQ_IOL_COUNT_AT);
        bqCopy(at, src + (total - 1UL) * CM_IRQ_IOD_SIZE, CM_IRQ_IOD_SIZE);
        at += CM_IRQ_IOD_SIZE;
        count++;
    }
    bqCopy(out, list, CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR);
    bqPut32(out, p->Bytes);
    bqPut32(out + CM_IRQ_REQ_HDR + CM_IRQ_IOL_COUNT_AT, count);
    return p->Bytes;
}

/* ---- the diagnostic (section 9.2) ------------------------------------- */

static ULONG bqMib(ULONG bytes)
{
    ULONG m;

    m = bytes >> 20;
    return m > 0xFFFFUL ? 0xFFFFUL : m;
}

VOID CmBreqEntry(const CM_BREQ_PLAN *p, ULONG bdf, ULONG portBdf,
                 ULONG minor, ULONG outcome, ULONG times, PULONG e)
{
    ULONG io;
    ULONG osIo;
    ULONG mem;
    ULONG pref;

    e[0] = (bdf & 0xFFFFUL) | ((portBdf & 0xFFFFUL) << 16);
    e[1] = (p->Role & 0xFUL) | ((p->Shape & 0xFUL) << 4) |
           ((minor & 0xFUL) << 8) | ((outcome & 0xFUL) << 12) |
           ((p->Dropped & 7UL) << 16) | ((p->Unmet & 7UL) << 19) |
           ((p->Tail & 1UL) << 24);
    e[2] = (p->Count & 0xFFUL) | ((p->Index & 0xFFUL) << 8) |
           ((p->Flags & 0xFUL) << 16) | ((p->Claims & 0x3FUL) << 20);
    e[3] = p->P[CM_BREQ_K_IO];
    e[4] = bqMib(p->P[CM_BREQ_K_MEM]) | (bqMib(p->P[CM_BREQ_K_PREF]) << 16);
    io = 0;
    mem = 0;
    pref = 0;
    if (p->Keep[CM_BREQ_K_IO]) {
        io = p->Sized[CM_BREQ_K_IO] ? p->Len[CM_BREQ_K_IO][0]
                                    : p->OsPref[CM_BREQ_K_IO];
    }
    if (p->Keep[CM_BREQ_K_MEM]) {
        mem = p->Sized[CM_BREQ_K_MEM] ? p->Len[CM_BREQ_K_MEM][0]
                                      : p->OsPref[CM_BREQ_K_MEM];
    }
    if (p->Keep[CM_BREQ_K_PREF]) {
        pref = p->Sized[CM_BREQ_K_PREF] ? p->Len[CM_BREQ_K_PREF][0]
                                        : p->OsPref[CM_BREQ_K_PREF];
    }
    osIo = p->OsPref[CM_BREQ_K_IO] >> 12;
    io >>= 12;
    e[5] = (io > 0xFFUL ? 0xFFUL : io) | ((osIo > 0xFFUL ? 0xFFUL : osIo) << 8) |
           ((times > 0xFFUL ? 0xFFUL : times) << 16);
    e[6] = bqMib(mem) | (bqMib(pref) << 16);
}

/* A 16-bit half of a dword raised by one, saturating. */
static ULONG bqBump(ULONG v, ULONG shift)
{
    if (((v >> shift) & 0xFFFFUL) != 0xFFFFUL) {
        v += 1UL << shift;
    }
    return v;
}

VOID CmBreqDiagAdd(PULONG d, ULONG seq, const ULONG *e)
{
    ULONG i;
    ULONG j;
    ULONG outcome;

    /* The ring keeps the newest last: the oldest entry gives way. */
    for (i = 0; i + 1UL < CM_BQDIAG_ENTRIES; i++) {
        for (j = 0; j < CM_BQDIAG_ENTRY_WORDS; j++) {
            d[CM_BQDIAG_HDR + i * CM_BQDIAG_ENTRY_WORDS + j] =
                d[CM_BQDIAG_HDR + (i + 1UL) * CM_BQDIAG_ENTRY_WORDS + j];
        }
    }
    i = CM_BQDIAG_HDR + (CM_BQDIAG_ENTRIES - 1UL) * CM_BQDIAG_ENTRY_WORDS;
    d[i] = seq;
    for (j = 0; j < CM_BQDIAG_ENTRY_WORDS - 1UL; j++) {
        d[i + 1UL + j] = e[j];
    }
    d[0] = seq;
    outcome = (e[1] >> 12) & 0xFUL;
    if (outcome == CM_BREQ_OUT_REWRITTEN) {
        d[1] = bqBump(d[1], 0);
    } else if (outcome == CM_BREQ_OUT_NOMEM) {
        d[3] = bqBump(d[3], 0);
    } else if (outcome != CM_BREQ_OUT_OFF && outcome != CM_BREQ_OUT_RECORD &&
               outcome != CM_BREQ_OUT_LOWER) {
        d[1] = bqBump(d[1], 16);
    }
}

BOOLEAN CmBreqDiagStart(PULONG d, ULONG bdf, BOOLEAN ok)
{
    ULONG i;
    ULONG at;

    for (i = CM_BQDIAG_ENTRIES; i != 0; i--) {
        at = CM_BQDIAG_HDR + (i - 1UL) * CM_BQDIAG_ENTRY_WORDS;
        if (d[at] == 0 || (d[at + 1UL] & 0xFFFFUL) != (bdf & 0xFFFFUL)) {
            continue;
        }
        if (((d[at + 2UL] >> 12) & 0xFUL) != CM_BREQ_OUT_REWRITTEN ||
            ((d[at + 2UL] >> 22) & 3UL) != CM_BREQ_START_NONE) {
            return FALSE;
        }
        d[at + 2UL] |= (ok ? CM_BREQ_START_OK : CM_BREQ_START_FAILED) << 22;
        d[2] = bqBump(d[2], ok ? 0UL : 16UL);
        return TRUE;
    }
    return FALSE;
}

VOID CmBreqDiagNoBridge(PULONG d)
{
    d[3] = bqBump(d[3], 16);
}
