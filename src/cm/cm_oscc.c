/*
 * cm_oscc.c - the second tier of the firmware's recognition for the
 * platform's USB4 handover on 98 SE and ME (cm_osc.h; design record 32
 * section 12.8.10, task 46.12): each of the five methods' bodies parsed
 * whole as AML [acpi] into a canonical form, the form fingerprinted, and
 * the five fingerprints compared with the builds read. Pure, host-tested
 * (test\test_cm_osc.c); cm_oscos.c runs it on the thread.
 *
 * The form is a token string, one token per parsed term in the order the
 * AML holds them, lists bracketed; with section 12.8.10's rewrites alone:
 * a Noop dropped, an integer by its value, a listed operator with a target
 * in statement position written as a Store of the operator, the debug
 * print's text ignored, the Locals renumbered by first appearance. Names
 * are their spelling; the three tables' declarations (CmOscFind) only tell
 * a call from a value and give a call's argument count.
 *
 * C89, no allocation (the caller's CM_OSCC), no lock. IRQL: any.
 */

#include "cm_osc.h"

/* Tokens of the form; AML opcodes stand for themselves. */
#define TK_LIST     0xF0    /* a term list begins                      */
#define TK_END      0xF1    /* it ends                                 */
#define TK_INT      0xF2    /* and 8 bytes, the value low dword first  */
#define TK_ONES     0xF3
#define TK_STR      0xF4    /* and the characters and a 0              */
#define TK_DSTR     0xF5    /* the debug print's text, not compared    */
#define TK_BUF      0xF6    /* after the size: 4 bytes length, bytes   */
#define TK_NAME     0xF7    /* and the spelling                        */
#define TK_CALL     0xF8    /* and the spelling, the count, the args   */
#define TK_NULL     0xF9    /* no target                               */
#define TK_LOCAL    0xFA    /* and the renumbered index                */
#define TK_ARG      0xFB    /* and its number                          */
#define TK_FRES     0xFC    /* a reserved field: 4 bytes width         */
#define TK_FACC     0xFD    /* an access field: 2 bytes                */
#define TK_FXACC    0xFE    /* an extended access field: 3 bytes       */
#define TK_FNAMED   0xEF    /* a named field: the spelling, 4 bytes    */

/* Kinds of a looked-up spelling. */
#define CC_UNDECLARED   0UL
#define CC_VALUE        1UL
#define CC_METHOD       2UL
#define CC_OWN          3UL     /* declared by this body               */

static VOID ccFail(CM_OSCC *c, ULONG why, ULONG at)
{
    if (c->Why == 0) {
        c->Why = why;
        c->WhyAt = at;
    }
}

static VOID ccPut(CM_OSCC *c, UCHAR b)
{
    if (c->Len >= CM_OSCC_FORM) {
        ccFail(c, CM_OSCC_W_FORM, 0);
        return;
    }
    c->Form[c->Len++] = b;
}

static VOID ccPut32(CM_OSCC *c, ULONG v)
{
    ccPut(c, (UCHAR)v);
    ccPut(c, (UCHAR)(v >> 8));
    ccPut(c, (UCHAR)(v >> 16));
    ccPut(c, (UCHAR)(v >> 24));
}

/* Inserts byte b at form offset `at` (the Store rewrite). */
static VOID ccInsert(CM_OSCC *c, ULONG at, UCHAR b)
{
    ULONG i;

    if (c->Len >= CM_OSCC_FORM || at > c->Len) {
        ccFail(c, CM_OSCC_W_FORM, 0);
        return;
    }
    for (i = c->Len; i > at; i--) {
        c->Form[i] = c->Form[i - 1];
    }
    c->Form[at] = b;
    c->Len++;
}

static ULONG ccLe32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

/* A PkgLength at p[*at] (end the bound): the package's end as an offset,
 * *at past the PkgLength; FALSE when it does not fit. */
static BOOLEAN ccPkg(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                     PULONG pkgEnd)
{
    ULONG start;
    ULONG n;
    ULONG v;
    ULONG i;

    start = *at;
    if (start >= end) {
        ccFail(c, CM_OSCC_W_LENGTH, start);
        return FALSE;
    }
    n = (ULONG)(p[start] >> 6);
    if (n == 0) {
        v = p[start] & 0x3FUL;
    } else {
        if ((p[start] & 0x30) != 0 || start + n >= end) {
            ccFail(c, CM_OSCC_W_LENGTH, start);
            return FALSE;
        }
        v = p[start] & 0x0FUL;
        for (i = 0; i < n; i++) {
            v |= (ULONG)p[start + 1 + i] << (4UL + 8UL * i);
        }
    }
    if (v < n + 1UL || v > end - start) {
        ccFail(c, CM_OSCC_W_LENGTH, start);
        return FALSE;
    }
    *at = start + n + 1UL;
    *pkgEnd = start + v;
    return TRUE;
}

/* A field's width, a PkgLength's encoding of a number (not a package's
 * extent): its value, *at past it. */
static BOOLEAN ccWidth(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                       PULONG width)
{
    ULONG k;
    ULONG n;
    ULONG v;
    ULONG i;

    k = *at;
    if (k >= end) {
        ccFail(c, CM_OSCC_W_LENGTH, k);
        return FALSE;
    }
    n = (ULONG)(p[k] >> 6);
    if (n == 0) {
        v = p[k] & 0x3FUL;
    } else {
        if ((p[k] & 0x30) != 0 || k + n >= end) {
            ccFail(c, CM_OSCC_W_LENGTH, k);
            return FALSE;
        }
        v = p[k] & 0x0FUL;
        for (i = 0; i < n; i++) {
            v |= (ULONG)p[k + 1 + i] << (4UL + 8UL * i);
        }
    }
    *at = k + n + 1UL;
    *width = v;
    return TRUE;
}

static BOOLEAN ccLead(UCHAR ch)
{
    return (BOOLEAN)((ch >= 'A' && ch <= 'Z') || ch == '_');
}

static BOOLEAN ccSeg(const UCHAR *p)
{
    ULONG i;

    if (!ccLead(p[0])) {
        return FALSE;
    }
    for (i = 1; i < 4UL; i++) {
        if (!ccLead(p[i]) && !(p[i] >= '0' && p[i] <= '9')) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN ccNameLead(UCHAR ch)
{
    return (BOOLEAN)(ch == 0x5C || ch == 0x5E || ch == 0x2E || ch == 0x2F ||
                     ccLead(ch));
}

/* A NameString at p[*at] into *sp: the spelling (root, parent prefixes,
 * segments). FALSE when it is not one (a NullName included). */
static BOOLEAN ccName(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                      CM_OSCC_SPELL *sp)
{
    ULONG k;
    ULONG segs;
    ULONG i;

    k = *at;
    sp->Root = 0;
    sp->Ups = 0;
    sp->Segs = 0;
    if (k < end && p[k] == 0x5C) {
        sp->Root = 1;
        k++;
    } else {
        while (k < end && p[k] == 0x5E && sp->Ups < CM_OSC_DEPTH) {
            sp->Ups++;
            k++;
        }
    }
    if (k >= end || p[k] == 0x00 || p[k] == 0x5E) {
        ccFail(c, CM_OSCC_W_NAME, k);
        return FALSE;
    }
    if (p[k] == 0x2E) {
        segs = 2;
        k++;
    } else if (p[k] == 0x2F) {
        if (k + 1UL >= end) {
            ccFail(c, CM_OSCC_W_NAME, k);
            return FALSE;
        }
        segs = p[k + 1];
        k += 2;
    } else {
        segs = 1;
    }
    if (segs == 0 || segs > CM_OSC_DEPTH || k + 4UL * segs > end) {
        ccFail(c, CM_OSCC_W_NAME, k);
        return FALSE;
    }
    for (i = 0; i < segs; i++) {
        if (!ccSeg(p + k)) {
            ccFail(c, CM_OSCC_W_NAME, k);
            return FALSE;
        }
        sp->Seg[i][0] = p[k];
        sp->Seg[i][1] = p[k + 1];
        sp->Seg[i][2] = p[k + 2];
        sp->Seg[i][3] = p[k + 3];
        k += 4;
    }
    sp->Segs = segs;
    *at = k;
    return TRUE;
}

static BOOLEAN ccSpellSame(const CM_OSCC_SPELL *a, const CM_OSCC_SPELL *b)
{
    ULONG i;

    if (a->Root != b->Root || a->Ups != b->Ups || a->Segs != b->Segs) {
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

static BOOLEAN ccSegIs(const UCHAR *s, const char *n)
{
    return (BOOLEAN)(s[0] == (UCHAR)n[0] && s[1] == (UCHAR)n[1] &&
                     s[2] == (UCHAR)n[2] && s[3] == (UCHAR)n[3]);
}

/* The spelling as tokens: TK_NAME or TK_CALL's operand. */
static VOID ccPutSpell(CM_OSCC *c, const CM_OSCC_SPELL *sp)
{
    ULONG i;

    ccPut(c, (UCHAR)sp->Root);
    ccPut(c, (UCHAR)sp->Ups);
    ccPut(c, (UCHAR)sp->Segs);
    for (i = 0; i < sp->Segs; i++) {
        ccPut(c, sp->Seg[i][0]);
        ccPut(c, sp->Seg[i][1]);
        ccPut(c, sp->Seg[i][2]);
        ccPut(c, sp->Seg[i][3]);
    }
}

/* The declarations of `path` in the three tables: CC_* and *args. */
static ULONG ccDeclared(CM_OSCC *c, const CM_OSC_SCOPE *path, PULONG args,
                        ULONG at)
{
    ULONG t;
    ULONG bits;
    ULONG all;
    ULONG a;
    ULONG margs;

    all = 0;
    margs = 0;
    for (t = 0; t < c->Tables; t++) {
        bits = CmOscFind(c->Tbl[t].Aml, c->Tbl[t].Length, path, &c->Scratch,
                         &a);
        if ((bits & (CM_OSC_FIND_FAILED | CM_OSC_FIND_ARGS |
                     CM_OSC_FIND_ALIAS)) != 0) {
            ccFail(c, CM_OSCC_W_KIND, at);
            return CC_UNDECLARED;
        }
        if ((bits & CM_OSC_FIND_METHOD) != 0) {
            if ((all & CM_OSC_FIND_METHOD) != 0 && a != margs) {
                ccFail(c, CM_OSCC_W_KIND, at);
                return CC_UNDECLARED;
            }
            margs = a;
        }
        all |= bits;
    }
    if ((all & CM_OSC_FIND_METHOD) != 0 && (all & CM_OSC_FIND_VALUE) != 0) {
        ccFail(c, CM_OSCC_W_KIND, at);
        return CC_UNDECLARED;
    }
    *args = margs;
    if ((all & CM_OSC_FIND_METHOD) != 0) {
        return CC_METHOD;
    }
    return (all & CM_OSC_FIND_VALUE) != 0 ? CC_VALUE : CC_UNDECLARED;
}

/* The body's own name `seg`, or -1. */
static LONG ccOwn(const CM_OSCC *c, const UCHAR *seg)
{
    ULONG i;

    for (i = 0; i < c->Owns; i++) {
        if (c->Own[i][0] == seg[0] && c->Own[i][1] == seg[1] &&
            c->Own[i][2] == seg[2] && c->Own[i][3] == seg[3]) {
            return (LONG)i;
        }
    }
    return -1;
}

/* Looks a spelling up (section 12.8.10, "Names"): its kind, argument count
 * and whether it is found at the root (the debug print's test). */
static ULONG ccLookup(CM_OSCC *c, const CM_OSCC_SPELL *sp, PULONG args,
                      PULONG rootSeg, ULONG at)
{
    CM_OSC_SCOPE path;
    CM_OSCC_NAME *e;
    ULONG kind;
    ULONG keep;
    ULONG i;

    *args = 0;
    *rootSeg = 0;
    if (!sp->Root && sp->Ups == 0 && sp->Segs == 1 &&
        ccOwn(c, sp->Seg[0]) >= 0) {
        return CC_OWN;
    }
    for (i = 0; i < c->Names; i++) {
        if (ccSpellSame(&c->Name[i].Spell, sp)) {
            *args = c->Name[i].Args;
            *rootSeg = c->Name[i].RootSeg;
            return c->Name[i].Kind;
        }
    }
    if (c->Names >= CM_OSCC_NAMES) {
        ccFail(c, CM_OSCC_W_NAMES, at);
        return CC_UNDECLARED;
    }
    kind = CC_UNDECLARED;
    if (sp->Root) {
        path.Segs = 0;
    } else {
        if (sp->Ups > c->Method.Segs) {
            ccFail(c, CM_OSCC_W_NAME, at);
            return CC_UNDECLARED;
        }
        path = c->Method;
        path.Segs -= sp->Ups;
    }
    if (path.Segs + sp->Segs > CM_OSC_DEPTH) {
        ccFail(c, CM_OSCC_W_NAME, at);
        return CC_UNDECLARED;
    }
    if (!sp->Root && sp->Ups == 0 && sp->Segs == 1) {
        /* A single segment: the method's scope, then each parent. */
        keep = path.Segs + 1UL;
        do {
            keep--;
            path.Segs = keep;
            path.Seg[keep][0] = sp->Seg[0][0];
            path.Seg[keep][1] = sp->Seg[0][1];
            path.Seg[keep][2] = sp->Seg[0][2];
            path.Seg[keep][3] = sp->Seg[0][3];
            path.Segs = keep + 1UL;
            kind = ccDeclared(c, &path, args, at);
            if (c->Why != 0) {
                return CC_UNDECLARED;
            }
        } while (kind == CC_UNDECLARED && keep > 0);
        *rootSeg = (ULONG)(kind != CC_UNDECLARED && keep == 0);
    } else {
        for (i = 0; i < sp->Segs; i++) {
            path.Seg[path.Segs][0] = sp->Seg[i][0];
            path.Seg[path.Segs][1] = sp->Seg[i][1];
            path.Seg[path.Segs][2] = sp->Seg[i][2];
            path.Seg[path.Segs][3] = sp->Seg[i][3];
            path.Segs++;
        }
        kind = ccDeclared(c, &path, args, at);
        if (c->Why != 0) {
            return CC_UNDECLARED;
        }
        *rootSeg = (ULONG)(path.Segs == 1UL);
    }
    e = &c->Name[c->Names++];
    e->Spell = *sp;
    e->Kind = kind;
    e->Args = *args;
    e->RootSeg = *rootSeg;
    return kind;
}

/* The four undeclared names the build read evaluates (section 12.8.10's
 * assumed-value exemption), spelled with the root prefix. */
static BOOLEAN ccExempt(const CM_OSCC_SPELL *sp)
{
    if (!sp->Root || sp->Segs != 1) {
        return FALSE;
    }
    return (BOOLEAN)(ccSegIs(sp->Seg[0], "DIMS") ||
                     ccSegIs(sp->Seg[0], "DDEP") ||
                     ccSegIs(sp->Seg[0], "PSIG") ||
                     ccSegIs(sp->Seg[0], "PWOC"));
}

/* A declaration by the body (section 12.8.10): one unprefixed segment,
 * once, never referenced before, not declared in the three tables
 * anywhere the method's upward search reaches. */
static BOOLEAN ccDeclare(CM_OSCC *c, const CM_OSCC_SPELL *sp, ULONG at)
{
    CM_OSCC_SPELL one;
    ULONG args;
    ULONG rootSeg;
    ULONG i;

    if (sp->Root || sp->Ups != 0 || sp->Segs != 1 ||
        ccOwn(c, sp->Seg[0]) >= 0 || c->Owns >= CM_OSCC_OWN) {
        ccFail(c, CM_OSCC_W_OWN, at);
        return FALSE;
    }
    for (i = 0; i < c->Names; i++) {
        one = c->Name[i].Spell;
        if (!one.Root && one.Ups == 0 && one.Segs == 1 &&
            ccSegIs(one.Seg[0], (const char *)sp->Seg[0])) {
            ccFail(c, CM_OSCC_W_OWN, at);
            return FALSE;
        }
    }
    if (ccLookup(c, sp, &args, &rootSeg, at) != CC_UNDECLARED ||
        c->Why != 0) {
        ccFail(c, CM_OSCC_W_OWN, at);
        return FALSE;
    }
    /* The lookup above was cached under a reference's key: drop it, so a
     * later reference finds the body's own name. */
    c->Names--;
    c->Own[c->Owns][0] = sp->Seg[0][0];
    c->Own[c->Owns][1] = sp->Seg[0][1];
    c->Own[c->Owns][2] = sp->Seg[0][2];
    c->Own[c->Owns][3] = sp->Seg[0][3];
    c->Owns++;
    ccPut(c, TK_NAME);
    ccPutSpell(c, sp);
    return TRUE;
}

static BOOLEAN ccBudget(CM_OSCC *c, ULONG depth, ULONG limit, ULONG at)
{
    if (++c->Terms > CM_OSCC_TERMS || depth >= limit) {
        ccFail(c, CM_OSCC_W_BUDGET, at);
        return FALSE;
    }
    return TRUE;
}

static BOOLEAN ccArg(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                     ULONG depth);

/* A Local, renumbered by first appearance. */
static VOID ccLocal(CM_OSCC *c, ULONG n)
{
    if (c->LocalMap[n] == 0xFF) {
        c->LocalMap[n] = (UCHAR)c->LocalNext++;
    }
    ccPut(c, TK_LOCAL);
    ccPut(c, c->LocalMap[n]);
}

/* A target (SuperName or none): a name never called, a Local, an Arg.
 * `evaluated`: the operand's object is read as well (Increment, Decrement,
 * Acquire, Release), so an undeclared name is one of ccExempt's alone. */
static BOOLEAN ccTargetEx(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                          BOOLEAN *null, BOOLEAN evaluated);

static BOOLEAN ccTarget(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                        BOOLEAN *null)
{
    return ccTargetEx(c, p, end, at, null, FALSE);
}

static BOOLEAN ccTargetEx(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                          BOOLEAN *null, BOOLEAN evaluated)
{
    CM_OSCC_SPELL sp;
    ULONG kind;
    ULONG args;
    ULONG rootSeg;
    UCHAR op;

    *null = FALSE;
    if (*at >= end) {
        ccFail(c, CM_OSCC_W_LENGTH, *at);
        return FALSE;
    }
    op = p[*at];
    if (op == 0x00) {
        *null = TRUE;
        (*at)++;
        ccPut(c, TK_NULL);
        return TRUE;
    }
    if (op >= 0x60 && op <= 0x67) {
        (*at)++;
        ccLocal(c, op - 0x60UL);
        return TRUE;
    }
    if (op >= 0x68 && op <= 0x6E) {
        (*at)++;
        ccPut(c, TK_ARG);
        ccPut(c, (UCHAR)(op - 0x68));
        return TRUE;
    }
    if (ccNameLead(op)) {
        if (!ccName(c, p, end, at, &sp)) {
            return FALSE;
        }
        kind = ccLookup(c, &sp, &args, &rootSeg, *at);
        if (c->Why != 0 || kind == CC_METHOD ||
            (kind == CC_UNDECLARED &&
             (!sp.Root || (evaluated && !ccExempt(&sp))))) {
            ccFail(c, CM_OSCC_W_KIND, *at);
            return FALSE;
        }
        ccPut(c, TK_NAME);
        ccPutSpell(c, &sp);
        return TRUE;
    }
    ccFail(c, CM_OSCC_W_TERM, *at);
    return FALSE;
}

/* The operators with a target that statement position rewrites (section
 * 12.8.10 rewrite 3): their operand count, else 0. */
static ULONG ccTargetOp(UCHAR op)
{
    switch (op) {
    case 0x72: case 0x74: case 0x77: case 0x79: case 0x7A: case 0x7B:
    case 0x7D: case 0x7F:
        return 2;       /* Add, Subtract, Multiply, ShiftLeft, ShiftRight,
                           And, Or, Xor */
    case 0x80:
        return 1;       /* Not */
    default:
        return 0;
    }
}

/* A string at p[*at] (its prefix passed): its characters and the 0. */
static BOOLEAN ccString(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                        BOOLEAN keep)
{
    ULONG k;

    for (k = *at; k < end && p[k] != 0; k++) {
        if (p[k] > 0x7F) {
            ccFail(c, CM_OSCC_W_TERM, k);
            return FALSE;
        }
        if (keep) {
            ccPut(c, p[k]);
        }
    }
    if (k >= end) {
        ccFail(c, CM_OSCC_W_LENGTH, *at);
        return FALSE;
    }
    if (keep) {
        ccPut(c, 0);
    }
    *at = k + 1UL;
    return TRUE;
}

/* An expression (TermArg). */
static BOOLEAN ccArg(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                     ULONG depth)
{
    CM_OSCC_SPELL sp;
    BOOLEAN null;
    ULONG k;
    ULONG e;
    ULONG n;
    ULONG kind;
    ULONG args;
    ULONG rootSeg;
    ULONG i;
    UCHAR op;

    if (!ccBudget(c, depth, CM_OSCC_EXPR_DEPTH, *at)) {
        return FALSE;
    }
    if (*at >= end) {
        ccFail(c, CM_OSCC_W_LENGTH, *at);
        return FALSE;
    }
    k = *at;
    op = p[k];
    switch (op) {
    case 0x00:
    case 0x01:
        ccPut(c, TK_INT);
        ccPut32(c, op);
        ccPut32(c, 0);
        *at = k + 1UL;
        return TRUE;
    case 0xFF:
        ccPut(c, TK_ONES);
        *at = k + 1UL;
        return TRUE;
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0E:
        n = op == 0x0A ? 1UL : op == 0x0B ? 2UL : op == 0x0C ? 4UL : 8UL;
        if (k + n >= end) {
            ccFail(c, CM_OSCC_W_LENGTH, k);
            return FALSE;
        }
        ccPut(c, TK_INT);
        if (n == 8UL) {
            ccPut32(c, ccLe32(p + k + 1));
            ccPut32(c, ccLe32(p + k + 5));
        } else {
            e = 0;
            for (i = 0; i < n; i++) {
                e |= (ULONG)p[k + 1 + i] << (8UL * i);
            }
            ccPut32(c, e);
            ccPut32(c, 0);
        }
        *at = k + 1UL + n;
        return TRUE;
    case 0x0D:
        ccPut(c, TK_STR);
        *at = k + 1UL;
        return ccString(c, p, end, at, TRUE);
    case 0x11:
        /* Buffer: PkgLength, its size (an expression), the bytes. */
        *at = k + 1UL;
        if (!ccPkg(c, p, end, at, &e)) {
            return FALSE;
        }
        ccPut(c, 0x11);
        if (!ccArg(c, p, e, at, depth + 1UL)) {
            return FALSE;
        }
        ccPut(c, TK_BUF);
        ccPut32(c, e - *at);
        for (i = *at; i < e; i++) {
            ccPut(c, p[i]);
        }
        *at = e;
        return TRUE;
    default:
        break;
    }
    if (op >= 0x60 && op <= 0x67) {
        *at = k + 1UL;
        ccLocal(c, op - 0x60UL);
        return TRUE;
    }
    if (op >= 0x68 && op <= 0x6E) {
        *at = k + 1UL;
        ccPut(c, TK_ARG);
        ccPut(c, (UCHAR)(op - 0x68));
        return TRUE;
    }
    if (op == 0x70) {
        /* Store (TermArg, SuperName). */
        *at = k + 1UL;
        ccPut(c, 0x70);
        return (BOOLEAN)(ccArg(c, p, end, at, depth + 1UL) &&
                         ccTarget(c, p, end, at, &null));
    }
    n = ccTargetOp(op);
    if (n != 0) {
        *at = k + 1UL;
        ccPut(c, op);
        for (i = 0; i < n; i++) {
            if (!ccArg(c, p, end, at, depth + 1UL)) {
                return FALSE;
            }
        }
        return ccTarget(c, p, end, at, &null);
    }
    if (op == 0x75 || op == 0x76) {
        /* Increment, Decrement (SuperName, read and written). */
        *at = k + 1UL;
        ccPut(c, op);
        if (!ccTargetEx(c, p, end, at, &null, TRUE)) {
            return FALSE;
        }
        if (null) {
            ccFail(c, CM_OSCC_W_TERM, k);
            return FALSE;
        }
        return TRUE;
    }
    if (op == 0x92) {
        *at = k + 1UL;
        ccPut(c, op);
        return ccArg(c, p, end, at, depth + 1UL);
    }
    if (op == 0x90 || op == 0x91 || op == 0x93 || op == 0x94 || op == 0x95) {
        /* LAnd, LOr, LEqual, LGreater, LLess. */
        *at = k + 1UL;
        ccPut(c, op);
        return (BOOLEAN)(ccArg(c, p, end, at, depth + 1UL) &&
                         ccArg(c, p, end, at, depth + 1UL));
    }
    if (op == 0x5B && k + 1UL < end && p[k + 1] == 0x12) {
        /* CondRefOf (SuperName, Target): the source is never evaluated;
         * an undeclared name there needs only the root prefix. */
        *at = k + 2UL;
        ccPut(c, 0x5B);
        ccPut(c, 0x12);
        if (*at < end && ccNameLead(p[*at])) {
            if (!ccName(c, p, end, at, &sp)) {
                return FALSE;
            }
            kind = ccLookup(c, &sp, &args, &rootSeg, *at);
            if (c->Why != 0 || (kind == CC_UNDECLARED && !sp.Root)) {
                ccFail(c, CM_OSCC_W_KIND, *at);
                return FALSE;
            }
            ccPut(c, TK_NAME);
            ccPutSpell(c, &sp);
        } else if (!ccTarget(c, p, end, at, &null) || null) {
            ccFail(c, CM_OSCC_W_TERM, k);
            return FALSE;
        }
        return ccTarget(c, p, end, at, &null);
    }
    if (op == 0x5B && k + 1UL < end && p[k + 1] == 0x23) {
        /* Acquire (SuperName, WordData). */
        *at = k + 2UL;
        ccPut(c, 0x5B);
        ccPut(c, 0x23);
        if (!ccTargetEx(c, p, end, at, &null, TRUE) || null ||
            *at + 2UL > end) {
            ccFail(c, CM_OSCC_W_TERM, k);
            return FALSE;
        }
        ccPut(c, p[*at]);
        ccPut(c, p[*at + 1]);
        *at += 2;
        return TRUE;
    }
    if (ccNameLead(op)) {
        if (!ccName(c, p, end, at, &sp)) {
            return FALSE;
        }
        kind = ccLookup(c, &sp, &args, &rootSeg, k);
        if (c->Why != 0) {
            return FALSE;
        }
        if (kind == CC_UNDECLARED && !ccExempt(&sp)) {
            ccFail(c, CM_OSCC_W_KIND, k);
            return FALSE;
        }
        if (kind != CC_METHOD) {
            ccPut(c, TK_NAME);
            ccPutSpell(c, &sp);
            return TRUE;
        }
        ccPut(c, TK_CALL);
        ccPutSpell(c, &sp);
        ccPut(c, (UCHAR)args);
        for (i = 0; i < args; i++) {
            /* The debug print's text (rewrite 4): M460 or \M460, declared
             * at the root, its first argument a string. */
            if (i == 0 && rootSeg && sp.Segs == 1 && sp.Ups == 0 &&
                ccSegIs(sp.Seg[0], "M460") && *at < end &&
                p[*at] == 0x0D) {
                ccPut(c, TK_DSTR);
                (*at)++;
                if (!ccString(c, p, end, at, FALSE) ||
                    !ccBudget(c, depth + 1UL, CM_OSCC_EXPR_DEPTH, *at)) {
                    return FALSE;
                }
                continue;
            }
            if (!ccArg(c, p, end, at, depth + 1UL)) {
                return FALSE;
            }
        }
        return TRUE;
    }
    ccFail(c, CM_OSCC_W_TERM, k);
    return FALSE;
}

static BOOLEAN ccList(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                      ULONG depth);

/* One statement (TermObj) of a body. */
static BOOLEAN ccStmt(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                      ULONG depth)
{
    CM_OSCC_SPELL sp;
    BOOLEAN null;
    ULONG k;
    ULONG e;
    ULONG e2;
    ULONG start;
    ULONG tgt;
    ULONG kind;
    ULONG args;
    ULONG rootSeg;
    ULONG n;
    ULONG i;
    UCHAR op;

    if (!ccBudget(c, depth, CM_OSCC_STMT_DEPTH, *at)) {
        return FALSE;
    }
    k = *at;
    op = p[k];
    if (op == 0xA0 || op == 0xA2) {
        /* If, While: PkgLength, predicate, term list. */
        *at = k + 1UL;
        if (!ccPkg(c, p, end, at, &e)) {
            return FALSE;
        }
        ccPut(c, op);
        if (!ccArg(c, p, e, at, 0) || !ccList(c, p, e, at, depth + 1UL)) {
            return FALSE;
        }
        *at = e;
        if (op == 0xA0 && e < end && p[e] == 0xA1) {
            *at = e + 1UL;
            if (!ccPkg(c, p, end, at, &e2)) {
                return FALSE;
            }
            ccPut(c, 0xA1);
            if (!ccList(c, p, e2, at, depth + 1UL)) {
                return FALSE;
            }
            *at = e2;
        }
        return TRUE;
    }
    if (op == 0xA1) {
        ccFail(c, CM_OSCC_W_TERM, k);   /* an Else with no If before it */
        return FALSE;
    }
    if (op == 0xA3) {
        *at = k + 1UL;                  /* Noop: dropped (rewrite 1) */
        return TRUE;
    }
    if (op == 0xA4) {
        *at = k + 1UL;
        ccPut(c, op);
        return ccArg(c, p, end, at, 0);
    }
    if (op == 0xA5) {
        *at = k + 1UL;
        ccPut(c, op);
        return TRUE;
    }
    if (op == 0x08) {
        /* Name (NameString, DataRefObject). */
        *at = k + 1UL;
        ccPut(c, op);
        if (!ccName(c, p, end, at, &sp) || !ccDeclare(c, &sp, k)) {
            return FALSE;
        }
        if (*at < end && (p[*at] == 0x12 || p[*at] == 0x13)) {
            ccFail(c, CM_OSCC_W_TERM, *at);
            return FALSE;
        }
        return ccArg(c, p, end, at, 0);
    }
    if (op == 0x8A || op == 0x8B || op == 0x8C || op == 0x8D || op == 0x8F) {
        /* Create...Field (SourceBuff, Index, NameString). */
        *at = k + 1UL;
        ccPut(c, op);
        if (!ccArg(c, p, end, at, 0) || !ccArg(c, p, end, at, 0) ||
            !ccName(c, p, end, at, &sp)) {
            return FALSE;
        }
        return ccDeclare(c, &sp, k);
    }
    if (op == 0x5B && k + 1UL < end) {
        switch (p[k + 1]) {
        case 0x80:
            /* OperationRegion (NameString, space, offset, length). */
            *at = k + 2UL;
            ccPut(c, 0x5B);
            ccPut(c, 0x80);
            if (!ccName(c, p, end, at, &sp) || !ccDeclare(c, &sp, k)) {
                return FALSE;
            }
            if (*at >= end) {
                ccFail(c, CM_OSCC_W_LENGTH, *at);
                return FALSE;
            }
            ccPut(c, p[*at]);
            (*at)++;
            return (BOOLEAN)(ccArg(c, p, end, at, 0) &&
                             ccArg(c, p, end, at, 0));
        case 0x81:
            /* Field (PkgLength, region, flags, elements). */
            *at = k + 2UL;
            if (!ccPkg(c, p, end, at, &e)) {
                return FALSE;
            }
            ccPut(c, 0x5B);
            ccPut(c, 0x81);
            if (!ccName(c, p, e, at, &sp)) {
                return FALSE;
            }
            kind = ccLookup(c, &sp, &args, &rootSeg, k);
            if (c->Why != 0 || kind == CC_METHOD || kind == CC_UNDECLARED) {
                ccFail(c, CM_OSCC_W_KIND, k);
                return FALSE;
            }
            ccPut(c, TK_NAME);
            ccPutSpell(c, &sp);
            if (*at >= e) {
                ccFail(c, CM_OSCC_W_LENGTH, *at);
                return FALSE;
            }
            ccPut(c, p[(*at)++]);
            ccPut(c, TK_LIST);
            while (*at < e && c->Why == 0) {
                start = *at;
                if (p[start] == 0x00) {
                    ccPut(c, TK_FRES);
                    *at = start + 1UL;
                    if (!ccWidth(c, p, e, at, &e2)) {
                        return FALSE;
                    }
                    ccPut32(c, e2);
                } else if (p[start] == 0x01 && start + 3UL <= e) {
                    ccPut(c, TK_FACC);
                    ccPut(c, p[start + 1]);
                    ccPut(c, p[start + 2]);
                    *at = start + 3UL;
                } else if (p[start] == 0x03 && start + 4UL <= e) {
                    ccPut(c, TK_FXACC);
                    ccPut(c, p[start + 1]);
                    ccPut(c, p[start + 2]);
                    ccPut(c, p[start + 3]);
                    *at = start + 4UL;
                } else if (start + 4UL < e && ccSeg(p + start)) {
                    sp.Root = 0;
                    sp.Ups = 0;
                    sp.Segs = 1;
                    sp.Seg[0][0] = p[start];
                    sp.Seg[0][1] = p[start + 1];
                    sp.Seg[0][2] = p[start + 2];
                    sp.Seg[0][3] = p[start + 3];
                    ccPut(c, TK_FNAMED);
                    if (!ccDeclare(c, &sp, start)) {
                        return FALSE;
                    }
                    *at = start + 4UL;
                    if (!ccWidth(c, p, e, at, &e2)) {
                        return FALSE;
                    }
                    ccPut32(c, e2);
                } else {
                    ccFail(c, CM_OSCC_W_TERM, start);   /* a connection */
                    return FALSE;
                }
            }
            if (*at != e) {
                ccFail(c, CM_OSCC_W_LENGTH, *at);
                return FALSE;
            }
            ccPut(c, TK_END);
            return TRUE;
        case 0x27:
            /* Release (SuperName). */
            *at = k + 2UL;
            ccPut(c, 0x5B);
            ccPut(c, 0x27);
            if (!ccTargetEx(c, p, end, at, &null, TRUE) || null) {
                ccFail(c, CM_OSCC_W_TERM, k);
                return FALSE;
            }
            return TRUE;
        case 0x21:
        case 0x22:
            /* Stall, Sleep (TermArg). */
            *at = k + 2UL;
            ccPut(c, 0x5B);
            ccPut(c, p[k + 1]);
            return ccArg(c, p, end, at, 0);
        default:
            break;
        }
    }
    /* An expression in statement position; rewrite 3. */
    n = ccTargetOp(op);
    if (n != 0) {
        start = c->Len;
        *at = k + 1UL;
        ccPut(c, op);
        for (i = 0; i < n; i++) {
            if (!ccArg(c, p, end, at, 0)) {
                return FALSE;
            }
        }
        tgt = c->Len;
        if (!ccTarget(c, p, end, at, &null)) {
            return FALSE;
        }
        if (!null) {
            /* op (a, b, T) as Store (op (a, b, none), T). */
            ccInsert(c, tgt, TK_NULL);
            ccInsert(c, start, 0x70);
        }
        return (BOOLEAN)(c->Why == 0);
    }
    return ccArg(c, p, end, at, 0);
}

static BOOLEAN ccList(CM_OSCC *c, const UCHAR *p, ULONG end, PULONG at,
                      ULONG depth)
{
    ccPut(c, TK_LIST);
    while (*at < end && c->Why == 0) {
        if (!ccStmt(c, p, end, at, depth)) {
            return FALSE;
        }
    }
    if (*at != end) {
        ccFail(c, CM_OSCC_W_LENGTH, *at);
        return FALSE;
    }
    ccPut(c, TK_END);
    return (BOOLEAN)(c->Why == 0);
}

/* CRC-32 (the reflected 04C11DB7h polynomial), bit by bit. */
ULONG CmOscCrc(const UCHAR *p, ULONG n)
{
    ULONG crc;
    ULONG i;
    ULONG b;

    crc = 0xFFFFFFFFUL;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8UL; b++) {
            crc = (crc & 1UL) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* The methods' canonical paths (cm_osc.c's order). */
static const char *const ccMethodPath[CM_OSC_METHODS][2] = {
    { "_SB_", "M625" }, { "_SB_", "_OSC" }, { "M049", NULL },
    { "M624", NULL }, { "M011", NULL }
};

ULONG CmOscCanon(CM_OSCC *c, const CM_OSC_TBL *tbl, ULONG tables,
                 const CM_OSC_WALK *w, ULONG m, PULONG fnv, PULONG crc)
{
    const UCHAR *p;
    ULONG at;
    ULONG end;
    ULONG i;

    c->Tbl = tbl;
    c->Tables = tables;
    c->Len = 0;
    c->Names = 0;
    c->Owns = 0;
    c->Terms = 0;
    c->Why = 0;
    c->WhyAt = 0;
    c->LocalNext = 0;
    for (i = 0; i < 8UL; i++) {
        c->LocalMap[i] = 0xFF;
    }
    *fnv = 0;
    *crc = 0;
    if (m >= CM_OSC_METHODS || w->MethodSeen[m] != 1 ||
        w->MethodTable[m] >= tables || w->MethodLen[m] == 0 ||
        w->MethodAt[m] > tbl[w->MethodTable[m]].Length ||
        w->MethodLen[m] > tbl[w->MethodTable[m]].Length - w->MethodAt[m]) {
        ccFail(c, CM_OSCC_W_LENGTH, 0);
        return c->Why;
    }
    c->Method.Segs = 0;
    for (i = 0; i < 2UL && ccMethodPath[m][i] != NULL; i++) {
        c->Method.Seg[i][0] = (UCHAR)ccMethodPath[m][i][0];
        c->Method.Seg[i][1] = (UCHAR)ccMethodPath[m][i][1];
        c->Method.Seg[i][2] = (UCHAR)ccMethodPath[m][i][2];
        c->Method.Seg[i][3] = (UCHAR)ccMethodPath[m][i][3];
        c->Method.Segs++;
    }
    p = tbl[w->MethodTable[m]].Aml;
    at = w->MethodAt[m];
    end = at + w->MethodLen[m];
    ccPut(c, p[at]);                    /* the flags byte */
    at++;
    if (!ccList(c, p, end, &at, 0) || c->Why != 0) {
        if (c->Why == 0) {
            ccFail(c, CM_OSCC_W_TERM, at);
        }
        return c->Why;
    }
    *fnv = CmOscHash(c->Form, c->Len);
    *crc = CmOscCrc(c->Form, c->Len);
    return 0;
}

/*
 * The canonical forms read (record 32 section 12.8.3 step 5, second
 * tier), FNV-1a and CRC-32 of each method's form in CM_OSC_METHODS' order:
 * the build read on 2026-11-02 (computed off the tree from the board's
 * two captures, the same in both); the qemu flavour adds usb4-emu's build
 * 1 (its scripts/asm-fw-ssdt.py), which no firmware has.
 */
const ULONG CmOscKnownForm[][CM_OSC_METHODS][2] = {
    { { 0x82BD6D36UL, 0x3F47FA00UL }, { 0xFFF2B597UL, 0xFCFD732CUL },
      { 0x4D07F24CUL, 0x98232472UL }, { 0x608E006CUL, 0xF02EDCEDUL },
      { 0x32FEEDAAUL, 0x1391BA4AUL } },
#if defined(XHCI_FLAVOUR_QEMU)
    { { 0x921E64CCUL, 0x0C073A4BUL }, { 0x6E06DFA5UL, 0x8FA9DD74UL },
      { 0x4383C182UL, 0x78D55DC3UL }, { 0x5238222EUL, 0xE0E054ACUL },
      { 0xA4FF774BUL, 0x7BAE4E78UL } },
    /* usb4-emu's --build 1 --osi (record 32 section 12.8.15.17): its
     * _OSC's form alone differs. */
    { { 0x921E64CCUL, 0x0C073A4BUL }, { 0x9616AF17UL, 0x652CB991UL },
      { 0x4383C182UL, 0x78D55DC3UL }, { 0x5238222EUL, 0xE0E054ACUL },
      { 0xA4FF774BUL, 0x7BAE4E78UL } },
#endif
};

const ULONG CmOscKnownFormCount =
    sizeof(CmOscKnownForm) / sizeof(CmOscKnownForm[0]);

ULONG CmOscFormKnown(const ULONG fnv[CM_OSC_METHODS],
                     const ULONG crc[CM_OSC_METHODS])
{
    ULONG v;
    ULONG i;

    for (v = 0; v < CmOscKnownFormCount; v++) {
        for (i = 0; i < CM_OSC_METHODS; i++) {
            if (fnv[i] != CmOscKnownForm[v][i][0] ||
                crc[i] != CmOscKnownForm[v][i][1]) {
                break;
            }
        }
        if (i == CM_OSC_METHODS) {
            return CM_OSC_OK;
        }
    }
    return CM_OSC_UNKNOWN_CODE;
}
