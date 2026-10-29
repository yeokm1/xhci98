/*
 * tr_diag.c - the pure half of XHCITR's "PCI and bridge diagnostics" node
 * (design record 25 section 20; tr_diag.h). No Windows call; C89.
 *
 * The layouts are the drivers' own: XHCIPFW's values design record 32
 * sections 6 and 11.2 (src\pfw\pfw_scan.h and pfw_ntdec.h),
 * XhciCmBridgeDiag src\cm\cm_statb.h with src\cm\tbt_brnum.h's outcomes,
 * XhciCmNoIrqDiag src\cm\cm_noirq.h, and task 44.53's log records record
 * 21 section 6.2.1 (src\cm\cm_filt.c, cm_irq.h). Multi-byte values are
 * little-endian, as the registry holds them on both architectures.
 *
 * [BR] PCI-to-PCI Bridge Architecture Specification 1.2: 3.2 (the type 1
 *      header: bus numbers, the memory and prefetchable windows).
 */

#include <string.h>
#include "tr_diag.h"

/* ---- the names (one table: a rename touches only it) ------------------ */

typedef struct _TR_DNAME {
    const char *Name;
    TR_U32 Type;
    int NtOnly;
    const char *What;           /* a count's meaning; NULL otherwise      */
} TR_DNAME;

static const TR_DNAME trPfw[TR_PFW_COUNT] = {
    { "XhciPfwVersion",    TR_REG_SZ,     0, NULL },
    { "XhciPfwSkipped",    TR_REG_DWORD,  1, NULL },
    { "XhciPfwMode",       TR_REG_DWORD,  1, NULL },
    { "XhciPfwHoldTicks",  TR_REG_DWORD,  1, NULL },
    { "XhciPfwTickHz",     TR_REG_DWORD,  1, NULL },
    { "XhciPfwResult",     TR_REG_DWORD,  0, NULL },
    { "XhciPfwClosed",     TR_REG_DWORD,  0, "windows closed" },
    { "XhciPfwRefused",    TR_REG_DWORD,  0, "bridges whose write was refused" },
    { "XhciPfwParked",     TR_REG_DWORD,  0, "bridges left part-way" },
    { "XhciPfwAcross",     TR_REG_DWORD,  0, "windows across 4 GB, left" },
    { "XhciPfwBarsInside", TR_REG_DWORD,  0, "BARs inside a window to close" },
    { "XhciPfwFunctions",  TR_REG_DWORD,  0, "PCI functions found" },
    { "XhciPfwXhciHigh",   TR_REG_DWORD,  0, "xHCI BARs above 4 GB" },
    { "XhciPfwBridges",    TR_REG_BINARY, 0, NULL },
    { "XhciPfwBars",       TR_REG_BINARY, 0, NULL },
    { "XhciPfwXhci",       TR_REG_BINARY, 0, NULL }
};

/* Task 44.53's records in xhcicm's log (cm_filt.c, branch p44-w2kirq):
 * how each value is laid out. */
#define IRQ_V_LINE      0   /* Line 31:16, location 15:0              */
#define IRQ_V_WHY       1   /* reason 31:16, location 15:0            */
#define IRQ_V_BDF       2   /* location 15:0                          */

static const struct {
    const char *Label;
    int Kind;
    const char *What;
} trIrq[] = {
    { "cm.chain.irq",               IRQ_V_LINE, "added to its requirements" },
    { "cm.chain.irq.filter",        IRQ_V_LINE, "added at the filter request" },
    { "cm.chain.irq.no",            IRQ_V_WHY,  "no interrupt added" },
    { "cm.chain.irq.nomem",         IRQ_V_BDF,  "not added: no memory" },
    { "cm.chain.irq.start.refused", IRQ_V_BDF,  "its start refused" },
    { "cm.chain.irq.start.nomem",   IRQ_V_BDF,  "its start failed: no memory" },
    { "cm.chain.irq.start.stripped", IRQ_V_LINE,
      "the added interrupt taken out of its start" }
};

#define IRQ_LABELS ((int)(sizeof(trIrq) / sizeof(trIrq[0])))

const char *tr_pfw_name(int i)
{
    return i >= 0 && i < TR_PFW_COUNT ? trPfw[i].Name : NULL;
}

TR_U32 tr_pfw_type(int i)
{
    return i >= 0 && i < TR_PFW_COUNT ? trPfw[i].Type : 0;
}

int tr_pfw_nt_only(int i)
{
    return i >= 0 && i < TR_PFW_COUNT ? trPfw[i].NtOnly : 0;
}

const char *tr_irq_label(int i)
{
    return i >= 0 && i < IRQ_LABELS ? trIrq[i].Label : NULL;
}

/* ---- small helpers ----------------------------------------------------- */

static TR_U32 rd32(const unsigned char *b)
{
    return (TR_U32)b[0] | ((TR_U32)b[1] << 8) | ((TR_U32)b[2] << 16) |
           ((TR_U32)b[3] << 24);
}

void tr_diag_bdf(TR_U32 bdf, char *out, size_t cap)
{
    if (bdf == 0xFFFFFFFFUL) {
        tr_copy(out, "none", cap);
        return;
    }
    tr_sprintf(out, cap, "PCI %02lX:%02lX.%lu", (unsigned long)((bdf >> 8) & 0xFFUL),
               (unsigned long)((bdf >> 3) & 0x1FUL), (unsigned long)(bdf & 7UL));
}

/* [BR] 3.2: memory base 15:4 and limit 31:20 hold address bits
 * 31:20; the limit's low 20 bits are all ones. */
void tr_diag_memwin(TR_U32 v, char *out, size_t cap)
{
    TR_U32 base = (v & 0xFFF0UL) << 16;
    TR_U32 limit = (v & 0xFFF00000UL) | 0x000FFFFFUL;

    if (base > limit) {
        tr_copy(out, "closed", cap);
        return;
    }
    tr_sprintf(out, cap, "%08lX-%08lX", (unsigned long)base,
               (unsigned long)limit);
}

/* [BR] 3.2: the prefetchable base and limit, with the
 * upper halves (28h, 2Ch) where 24h's type nibble says 64-bit. */
void tr_diag_pfwin(TR_U32 r24, TR_U32 r28, TR_U32 r2c, char *out,
                   size_t cap)
{
    TR_U32 bLo = (r24 & 0xFFF0UL) << 16;
    TR_U32 lLo = (r24 & 0xFFF00000UL) | 0x000FFFFFUL;
    TR_U32 bHi = 0;
    TR_U32 lHi = 0;

    if ((r24 & 0xFUL) == 1UL) {
        bHi = r28;
        lHi = r2c;
    }
    if (bHi > lHi || (bHi == lHi && bLo > lLo)) {
        tr_copy(out, "closed", cap);
        return;
    }
    tr_sprintf(out, cap, "%08lX_%08lX-%08lX_%08lX", (unsigned long)bHi,
               (unsigned long)bLo, (unsigned long)lHi, (unsigned long)lLo);
}

static const char *pin_name(TR_U32 pin)
{
    switch (pin) {
    case 0: return "none";
    case 1: return "INTA";
    case 2: return "INTB";
    case 3: return "INTC";
    case 4: return "INTD";
    default: return "invalid";
    }
}

static void line_text(TR_U32 line, char *out, size_t cap)
{
    if (line == 0xFFUL) {
        tr_copy(out, "unset (FFh)", cap);
    } else {
        tr_sprintf(out, cap, "%lu", (unsigned long)line);
    }
}

static void pad(TR_TEXT *t, int n)
{
    while (n-- > 0) {
        tr_put(t, " ");
    }
}

/* Raw dwords, five a line, under `indent` spaces. */
static void raw_dwords(TR_TEXT *t, const unsigned char *b, TR_U32 words,
                       int indent)
{
    TR_U32 i;

    for (i = 0; i < words; i++) {
        if (i % 5 == 0) {
            pad(t, indent);
            tr_printf(t, "%2lu:", (unsigned long)i);
        }
        tr_printf(t, " %08lX", (unsigned long)rd32(b + i * 4));
        if (i % 5 == 4 || i + 1 == words) {
            tr_put(t, "\n");
        }
    }
}

/* A value this tool does not read the shape of: its type, length and
 * bytes. */
static void raw_value(TR_TEXT *t, const TR_DVAL *v, int indent)
{
    pad(t, indent);
    if (v->State == TR_DV_LONG) {
        tr_printf(t, "type %lu, longer than %d bytes: not a layout this "
                     "tool reads\n", (unsigned long)v->Type, TR_DV_MAX);
        return;
    }
    tr_printf(t, "type %lu, %lu bytes: not a layout this tool reads\n",
              (unsigned long)v->Type, (unsigned long)v->Len);
    if (v->Len > 0) {
        tr_hex(t, v->Data, (size_t)v->Len, indent + 2);
    }
}

/* A dword value's raw form; 0 when it is not a dword (said in t). */
static int dword_of(TR_TEXT *t, const TR_DVAL *v, const char *name,
                    TR_U32 *out)
{
    if (v->State == TR_DV_ABSENT) {
        tr_printf(t, "  %-18s not recorded\n", name);
        return 0;
    }
    if (v->State != TR_DV_OK || v->Type != TR_REG_DWORD || v->Len != 4) {
        tr_printf(t, "  %-18s\n", name);
        raw_value(t, v, 4);
        return 0;
    }
    *out = rd32(v->Data);
    return 1;
}

/* ---- XHCIPFW ------------------------------------------------------------ */

static const char *const pfwResultBits[7] = {
    "the scan finished",
    "a bridge read otherwise at the close, left as it was",
    "a write was refused (undone)",
    "a bridge was parked",
    "a window across 4 GB was found",
    "a table was full",
    "an xHCI controller's BAR lies above 4 GB"
};

static const char *const pfwSkipBits[4] = {
    "not a system it acts on",
    "a Safe Mode start",
    "not at the boot drivers' time",
    "several processors and no way to hold them"
};

static const char *pfw_action(TR_U32 a)
{
    switch (a) {
    case 1: return "closed";
    case 2: return "refused (undone)";
    case 3: return "parked (the undo did not hold)";
    case 4: return "across 4 GB, left";
    default: return NULL;
    }
}

static void pfw_bits(TR_TEXT *t, TR_U32 v, const char *const *names,
                     int count, const char *none)
{
    int b;
    int any = 0;

    for (b = 0; b < 32; b++) {
        if ((v & (1UL << b)) == 0) {
            continue;
        }
        if (b < count) {
            tr_printf(t, "    bit %d: %s\n", b, names[b]);
        } else {
            tr_printf(t, "    bit %d: not one this tool knows\n", b);
        }
        any = 1;
    }
    if (!any) {
        tr_printf(t, "    %s\n", none);
    }
}

/* A binary value of `size`-byte records, at most `max`: the count, or -1
 * when its shape is not one this tool reads (said in t). */
static int records_of(TR_TEXT *t, const TR_DVAL *v, const char *name,
                      TR_U32 size, TR_U32 max)
{
    if (v->State == TR_DV_ABSENT) {
        tr_printf(t, "  %-18s not recorded\n", name);
        return -1;
    }
    if (v->State != TR_DV_OK || v->Type != TR_REG_BINARY ||
        v->Len % size != 0 || v->Len / size > max) {
        tr_printf(t, "  %-18s\n", name);
        raw_value(t, v, 4);
        return -1;
    }
    tr_printf(t, "  %-18s %lu record%s (%lu bytes)%s\n", name,
              (unsigned long)(v->Len / size), tr_plural((long)(v->Len / size)),
              (unsigned long)v->Len, v->Len == 0 ? ": none" : "");
    return (int)(v->Len / size);
}

static void pfw_bridges(TR_TEXT *t, const TR_DVAL *v)
{
    char loc[24];
    char w[48];
    const char *act;
    const unsigned char *r;
    int n;
    int i;

    n = records_of(t, v, trPfw[TR_PFW_BRIDGES].Name, TR_PFW_BRIDGE_BYTES,
                   TR_PFW_MAX_BRIDGES);
    for (i = 0; i < n; i++) {
        r = v->Data + i * TR_PFW_BRIDGE_BYTES;
        tr_diag_bdf(((TR_U32)r[0] << 8) | r[1], loc, sizeof(loc));
        act = pfw_action(r[2]);
        if (act != NULL) {
            tr_printf(t, "    %s: %s\n", loc, act);
        } else {
            tr_printf(t, "    %s: action %u, not one this tool knows\n", loc,
                      (unsigned int)r[2]);
        }
        tr_printf(t, "      before: 24h %08lX 28h %08lX 2Ch %08lX\n",
                  (unsigned long)rd32(r + 4), (unsigned long)rd32(r + 8),
                  (unsigned long)rd32(r + 12));
        tr_diag_pfwin(rd32(r + 4), rd32(r + 8), rd32(r + 12), w, sizeof(w));
        tr_printf(t, "              prefetchable window %s\n", w);
        tr_printf(t, "      after:  24h %08lX 28h %08lX 2Ch %08lX\n",
                  (unsigned long)rd32(r + 16), (unsigned long)rd32(r + 20),
                  (unsigned long)rd32(r + 24));
        tr_diag_pfwin(rd32(r + 16), rd32(r + 20), rd32(r + 24), w, sizeof(w));
        tr_printf(t, "              prefetchable window %s\n", w);
    }
}

static void pfw_bars(TR_TEXT *t, const TR_DVAL *v)
{
    char loc[24];
    const unsigned char *r;
    int n;
    int i;

    n = records_of(t, v, trPfw[TR_PFW_BARS].Name, TR_PFW_BAR_BYTES,
                   TR_PFW_MAX_BARS);
    for (i = 0; i < n; i++) {
        r = v->Data + i * TR_PFW_BAR_BYTES;
        tr_diag_bdf(((TR_U32)r[0] << 8) | r[1], loc, sizeof(loc));
        tr_printf(t, "    %s BAR %02Xh, upper dword %08lX\n", loc,
                  (unsigned int)r[2], (unsigned long)rd32(r + 4));
    }
}

static void pfw_xhci(TR_TEXT *t, const TR_DVAL *v)
{
    char loc[24];
    const unsigned char *r;
    TR_U32 id;
    int n;
    int i;

    n = records_of(t, v, trPfw[TR_PFW_XHCI].Name, TR_PFW_XHCI_BYTES,
                   TR_PFW_MAX_XHCI);
    for (i = 0; i < n; i++) {
        r = v->Data + i * TR_PFW_XHCI_BYTES;
        tr_diag_bdf(((TR_U32)r[0] << 8) | r[1], loc, sizeof(loc));
        id = rd32(r + 4);
        tr_printf(t, "    %s %04lX:%04lX BAR %02Xh at %08lX_%08lX\n", loc,
                  (unsigned long)(id & 0xFFFFUL),
                  (unsigned long)((id >> 16) & 0xFFFFUL), (unsigned int)r[2],
                  (unsigned long)rd32(r + 8), (unsigned long)rd32(r + 12));
    }
}

static void pfw_version(TR_TEXT *t, const TR_DVAL *v)
{
    const char *name = trPfw[TR_PFW_VERSION].Name;
    TR_U32 i;

    if (v->State == TR_DV_ABSENT) {
        tr_printf(t, "  %-18s not recorded\n", name);
        return;
    }
    if (v->State != TR_DV_OK || v->Type != TR_REG_SZ || v->Len > 64) {
        tr_printf(t, "  %-18s\n", name);
        raw_value(t, v, 4);
        return;
    }
    /* Up to 64 characters: on the name's line where they fit in 79
     * columns, else on a line of their own. */
    for (i = 0; i < v->Len && v->Data[i] != 0; i++) {
        ;
    }
    if (i > 55) {
        tr_printf(t, "  %s\n    \"", name);
    } else {
        tr_printf(t, "  %-18s \"", name);
    }
    for (i = 0; i < v->Len && v->Data[i] != 0; i++) {
        unsigned char c = v->Data[i];
        char s[2];
        s[0] = (char)(c >= 0x20 && c < 0x7F ? c : '.');
        s[1] = 0;
        tr_put(t, s);
    }
    tr_put(t, "\"\n");
}

void tr_pfw_text(TR_TEXT *t, int key, TR_U32 err, const TR_DVAL *v, int nt)
{
    TR_U32 d;
    TR_U32 ticks = 0;
    TR_U32 hz = 0;
    int haveTicks = 0;
    int haveHz = 0;
    int i;

    tr_put(t, "XHCIPFW: PCI bridges' prefetchable windows above 4 GB, closed "
              "at the start\n");
    tr_printf(t, "  From HKEY_LOCAL_MACHINE\\\n    %s\n",
              nt ? TR_PFW_KEY_NT : TR_PFW_KEY_9X);
    if (key == TR_DK_ABSENT) {
        tr_put(t, "  Not recorded: XHCIPFW is not installed, or has not run "
                  "since it was\n  installed.\n");
        return;
    }
    if (key != TR_DK_OPEN) {
        tr_printf(t, "  The key could not be read (error %lu).\n",
                  (unsigned long)err);
        return;
    }
    pfw_version(t, &v[TR_PFW_VERSION]);
    for (i = TR_PFW_SKIPPED; i <= TR_PFW_TICKHZ; i++) {
        if (!nt && v[i].State == TR_DV_ABSENT) {
            continue;
        }
        if (!dword_of(t, &v[i], trPfw[i].Name, &d)) {
            continue;
        }
        tr_printf(t, "  %-18s %08lX", trPfw[i].Name, (unsigned long)d);
        switch (i) {
        case TR_PFW_SKIPPED:
            tr_put(t, d == 0 ? "  it ran\n" : "  it did nothing:\n");
            if (d != 0) {
                pfw_bits(t, d, pfwSkipBits, 4, "");
            }
            break;
        case TR_PFW_MODE:
            tr_put(t, d == 0 ? "  nothing done\n"
                    : d == 1 ? "  run with one processor\n"
                    : d == 2 ? "  run inside KeIpiGenericCall\n"
                             : "  not a mode this tool knows\n");
            break;
        case TR_PFW_HOLDTICKS:
            ticks = d;
            haveTicks = 1;
            tr_printf(t, "  the step took %lu ticks\n", (unsigned long)d);
            break;
        default:
            hz = d;
            haveHz = 1;
            tr_printf(t, "  the counter's frequency, %lu Hz\n",
                      (unsigned long)d);
            break;
        }
    }
    if (haveTicks && haveHz && hz != 0) {
        tr_printf(t, "  %-18s about %lu microseconds\n", "",
                  (unsigned long)((double)ticks * 1000000.0 / (double)hz +
                                  0.5));
    }
    if (dword_of(t, &v[TR_PFW_RESULT], trPfw[TR_PFW_RESULT].Name, &d)) {
        tr_printf(t, "  %-18s %08lX\n", trPfw[TR_PFW_RESULT].Name,
                  (unsigned long)d);
        pfw_bits(t, d, pfwResultBits, 7, "nothing set: the scan did not run");
    }
    for (i = TR_PFW_CLOSED; i <= TR_PFW_XHCIHIGH; i++) {
        if (dword_of(t, &v[i], trPfw[i].Name, &d)) {
            tr_printf(t, "  %-18s %08lX  %s: %lu\n", trPfw[i].Name,
                      (unsigned long)d, trPfw[i].What, (unsigned long)d);
        }
    }
    pfw_bridges(t, &v[TR_PFW_BRIDGES]);
    pfw_bars(t, &v[TR_PFW_BARS]);
    pfw_xhci(t, &v[TR_PFW_XHCI]);
}

/* ---- XhciCmBridgeDiag ---------------------------------------------------- */

const char *tr_brdiag_outcome(TR_U32 o)
{
    switch (o) {
    case 0: return "no bridge on the port's secondary bus";
    case 1: return "the chain numbered";
    case 2: return "a bridge already numbered: nothing written";
    case 3: return "the port's own bus range unreadable or not usable";
    case 4: return "more bridges than buses: undone";
    case 5: return "deeper than 6 bridges: undone";
    case 6: return "more than 16 bridges: undone";
    case 7: return "a read or write failed: undone";
    case 8: return "no writer: nothing read";
    case 9: return "a numbered bridge below one in reset state: undone";
    case 10: return "a function answered Retry Status: undone";
    case 11: return "no longer current: undone";
    case 12: return "a stopped chain given back its windows and lines";
    default: return "not one this tool knows";
    }
}

const char *tr_brdiag_win(TR_U32 o)
{
    switch (o) {
    case 0: return "not asked, or nothing numbered";
    case 1: return "every bridge's window written";
    case 2: return "the port has no memory window";
    case 3: return "the chain needs more than the port's window";
    case 4: return "a function below already decodes";
    case 5: return "a read or write failed";
    case 6: return "no longer current";
    default: return "not one this tool knows";
    }
}

const char *tr_brdiag_irq(TR_U32 o)
{
    switch (o) {
    case 0: return "not asked, or nothing set up";
    case 1: return "every eligible line written";
    case 2: return "the port has no line of its own";
    case 3: return "a read failed, a function not ready or a line not held";
    case 4: return "no longer current: every write undone";
    default: return "not one this tool knows";
    }
}

/* Each record's opening: absent, an unreadable shape, or 1 to decode. */
static int diag_open(TR_TEXT *t, const TR_DVAL *v, const char *name,
                     const char *what, TR_U32 words, int nt)
{
    tr_printf(t, "  %s (%s)\n", name, what);
    if (v->State == TR_DV_ABSENT) {
        tr_put(t, nt ? "    Not recorded (written on Windows 98 SE and ME "
                       "only).\n"
                     : "    Not recorded: no attempt since the driver was "
                       "installed.\n");
        return 0;
    }
    if (v->State != TR_DV_OK || v->Type != TR_REG_BINARY ||
        v->Len != words * 4) {
        raw_value(t, v, 4);
        return 0;
    }
    return 1;
}

void tr_brdiag_text(TR_TEXT *t, const TR_DVAL *v, int nt)
{
    const unsigned char *b = v->Data;
    char s1[48];
    char s2[48];
    TR_U32 seen;
    TR_U32 flags;
    TR_U32 bridges;
    TR_U32 k;
    TR_U32 irq;
    TR_U32 cnt;

    if (!diag_open(t, v, TR_BRDIAG_VALUE, "the bridge numbering's last "
                   "attempt", TR_BRDIAG_WORDS, nt)) {
        return;
    }
    flags = rd32(b + 64);
    tr_diag_bdf(rd32(b + 4), s1, sizeof(s1));
    tr_printf(t, "    Attempt %lu, at the tunnel port %s\n",
              (unsigned long)rd32(b), s1);
    if (flags & 1UL) {
        tr_put(t, "    Not attempted: a device below the port was still "
                  "listed (or the list\n    could not be read whole).\n");
    } else if (flags & 2UL) {
        tr_printf(t, "    Not attempted: PCI.VXD's configuration route "
                     "failed its check (read\n    %08lX).\n",
                  (unsigned long)rd32(b + 68));
    } else {
        seen = rd32(b + 12);
        bridges = (seen >> 8) & 0xFFUL;
        tr_printf(t, "    Outcome %lu: %s\n", (unsigned long)rd32(b + 8),
                  tr_brdiag_outcome(rd32(b + 8)));
        tr_printf(t, "    Seen: %lu function%s on the port's bus, %lu "
                     "bridge%s, %lu tr%s\n",
                  (unsigned long)(seen >> 16), tr_plural((long)(seen >> 16)),
                  (unsigned long)bridges, tr_plural((long)bridges),
                  (unsigned long)(seen & 0xFFUL),
                  (seen & 0xFFUL) == 1 ? "y" : "ies");
        k = rd32(b + 16);
        tr_printf(t, "    The port's buses (18h %08lX): primary %02lX, "
                     "secondary %02lX,\n      subordinate %02lX\n",
                  (unsigned long)k, (unsigned long)(k & 0xFFUL),
                  (unsigned long)((k >> 8) & 0xFFUL),
                  (unsigned long)((k >> 16) & 0xFFUL));
        /* tbt_brnum.c brWindows: the port's 20h is read before every
         * outcome but 0 (the stage not run) and a failed read (5, which
         * may also come later); the need is whole only after 1 (brNeed
         * can stop part-way with 3, 4 or 6). */
        k = rd32(b + 20);
        tr_printf(t, "    Windows %lu: %s\n", (unsigned long)k,
                  tr_brdiag_win(k));
        if (k != 0 && k != 5) {
            tr_diag_memwin(rd32(b + 24), s1, sizeof(s1));
            tr_printf(t, "      the port's memory window (20h %08lX): %s\n",
                      (unsigned long)rd32(b + 24), s1);
        }
        if (k == 1) {
            tr_printf(t, "      needed %08lX bytes below the port\n",
                      (unsigned long)rd32(b + 28));
        } else if (k == 3 || k == 4 || k == 6) {
            tr_put(t, "      the need: not recorded whole (see Raw, 7)\n");
        } else if (k == 5) {
            tr_put(t, "      the port's window and the need: not recorded "
                      "(see Raw, 6 and 7)\n");
        }
        tr_diag_bdf(rd32(b + 32), s1, sizeof(s1));
        tr_printf(t, "    Where it stopped: %s\n", s1);
        tr_printf(t, "    Bridges written back: %lu; could not be: %lu\n",
                  (unsigned long)(rd32(b + 36) & 0xFFFFUL),
                  (unsigned long)(rd32(b + 36) >> 16));
        for (k = 0; k < 2 && k < bridges; k++) {
            tr_diag_bdf(rd32(b + 40 + k * 4), s1, sizeof(s1));
            tr_diag_memwin(rd32(b + 56 + k * 4), s2, sizeof(s2));
            tr_printf(t, "    Bridge %lu, %s: 18h %08lX, 20h %08lX\n",
                      (unsigned long)(k + 1), s1,
                      (unsigned long)rd32(b + 48 + k * 4),
                      (unsigned long)rd32(b + 56 + k * 4));
            tr_printf(t, "      buses %02lX/%02lX/%02lX, memory window %s\n",
                      (unsigned long)(rd32(b + 48 + k * 4) & 0xFFUL),
                      (unsigned long)((rd32(b + 48 + k * 4) >> 8) & 0xFFUL),
                      (unsigned long)((rd32(b + 48 + k * 4) >> 16) & 0xFFUL),
                      s2);
        }
        irq = rd32(b + 72);
        cnt = rd32(b + 76);
        line_text((irq >> 8) & 0xFFUL, s1, sizeof(s1));
        tr_printf(t, "    Interrupt lines %lu: %s\n",
                  (unsigned long)(irq & 0xFFUL), tr_brdiag_irq(irq & 0xFFUL));
        /* tbt_brnum.c brIrq: the port's line and pin are read before
         * outcomes 1, 2 and 4; 3 can be that read failing; the functions
         * are counted for 1 and 4; 0 is the stage not run. */
        k = irq & 0xFFUL;
        if (k == 1 || k == 2 || k == 4) {
            tr_printf(t, "      the port's line %s, pin %s\n", s1,
                      pin_name((irq >> 16) & 0xFFUL));
        }
        if (k == 1 || k == 4) {
            tr_printf(t, "      functions with a pin %lu: given the port's "
                         "line %lu, already set %lu,\n      on another pin "
                         "%lu\n",
                      (unsigned long)(cnt & 0xFFUL),
                      (unsigned long)((cnt >> 8) & 0xFFUL),
                      (unsigned long)((cnt >> 16) & 0xFFUL),
                      (unsigned long)((cnt >> 24) & 0xFFUL));
        }
        if (k == 3) {
            tr_put(t, "      the port's line and the counts: not recorded "
                      "(see Raw, 18 and 19)\n");
        }
    }
    tr_printf(t, "    The port read up: %s\n", (flags & 4UL) ? "yes" : "no");
    tr_put(t, "    Raw:\n");
    raw_dwords(t, b, TR_BRDIAG_WORDS, 6);
}

/* ---- XhciCmNoIrqDiag ---------------------------------------------------- */

const char *tr_ni_irq(TR_U32 v)
{
    switch (v) {
    case 0: return "not read";
    case 1: return "the port's line written";
    case 2: return "a line already set";
    case 3: return "unroutable";
    case 4: return "the port has no line";
    case 5: return "a write not held";
    case 6: return "a read failed";
    case 7: return "routable";
    default: return "not one this tool knows";
    }
}

const char *tr_ni_state(TR_U32 v)
{
    switch (v) {
    case 0: return "free";
    case 1: return "pending";
    case 2: return "excluded";
    case 3: return "active";
    case 4: return "attempted";
    case 5: return "done (started)";
    case 6: return "failed";
    default: return "not one this tool knows";
    }
}

const char *tr_ni_why(TR_U32 v)
{
    switch (v) {
    case 0: return "none";
    case 1: return "XhciCmNoIrqStart is 0";
    case 2: return "started, or another problem";
    case 3: return "its header (class, pin, line or I/O)";
    case 4: return "not Stor98's";
    case 5: return "its interrupt is routable or routed";
    case 6: return "its configurations' shape";
    case 7: return "a left-over configuration";
    case 8: return "attempts used up";
    case 9: return "not in time";
    case 10: return "no interrupt pin";
    case 11: return "its handle's place changed";
    case 12: return "a timeout";
    case 13: return "the controller (lost, gated or claimed)";
    case 14: return "a step refused";
    case 15: return "no longer admitted";
    default: return "not one this tool knows";
    }
}

const char *tr_ni_op(TR_U32 v)
{
    switch (v) {
    case 0: return "none";
    case 1: return "the header read";
    case 2: return "the status read";
    case 3: return "the binding read";
    case 4: return "the port's line read";
    case 5: return "the configurations listed";
    case 6: return "a descriptor read";
    case 7: return "Add_Empty_Log_Conf";
    case 8: return "the configuration placed";
    case 9: return "Add_Res_Des";
    case 10: return "the priority set";
    case 11: return "Reprocess_DevNode";
    case 12: return "Setup_DevNode";
    case 13: return "the line read";
    default: return "not one this tool knows";
    }
}

void tr_nidiag_text(TR_TEXT *t, const TR_DVAL *v, int nt)
{
    const unsigned char *b = v->Data;
    char s1[48];
    char s2[48];
    TR_U32 w;
    TR_U32 k;
    TR_U32 n;
    int any;

    if (!diag_open(t, v, TR_NIDIAG_VALUE, "the no-interrupt start's last "
                   "attempt", TR_NIDIAG_WORDS, nt)) {
        return;
    }
    tr_diag_bdf(rd32(b + 4), s1, sizeof(s1));
    tr_diag_bdf(rd32(b + 8), s2, sizeof(s2));
    tr_printf(t, "    Record %lu, the tunnel port %s, the function %s\n",
              (unsigned long)rd32(b), s1, s2);
    w = rd32(b + 12);
    line_text((w >> 8) & 0xFFUL, s1, sizeof(s1));
    tr_printf(t, "    Its pin %s, line %s, as read\n", pin_name(w & 0xFFUL),
              s1);
    w = rd32(b + 16);
    tr_printf(t, "    Interrupt %lu: %s\n", (unsigned long)(w & 0xFFUL),
              tr_ni_irq(w & 0xFFUL));
    tr_printf(t, "    State %lu: %s; reason %lu: %s\n",
              (unsigned long)((w >> 8) & 0xFFUL), tr_ni_state((w >> 8) & 0xFFUL),
              (unsigned long)((w >> 16) & 0xFFUL),
              tr_ni_why((w >> 16) & 0xFFUL));
    tr_printf(t, "    Problem before: %lu\n", (unsigned long)rd32(b + 20));
    w = rd32(b + 24);
    tr_printf(t, "    Step reached %lu: %s, CONFIGRET %lu\n",
              (unsigned long)(w >> 16), tr_ni_op(w >> 16),
              (unsigned long)(w & 0xFFFFUL));
    w = rd32(b + 28);
    if (w == 0 && rd32(b + 32) == 0) {
        /* An exclusion records no status after (cm_noirq.c niExclude). */
        tr_put(t, "    After: not recorded (status 00000000, problem 0)\n");
    } else {
        tr_printf(t, "    After: status %08lX (%s%s), problem %lu\n",
                  (unsigned long)w, (w & 0x8UL) ? "started" : "not started",
                  (w & 0x400UL) ? ", has a problem" : "",
                  (unsigned long)rd32(b + 32));
    }
    w = rd32(b + 36);
    tr_printf(t, "    Configurations created %lu; candidates %lu\n",
              (unsigned long)(w & 0xFFFFUL), (unsigned long)(w >> 16));
    w = rd32(b + 40);
    tr_put(t, "    Refused, by reason (each count held at 15):");
    any = 0;
    for (k = 1; k <= 8; k++) {
        n = (w >> ((k - 1) * 4)) & 0xFUL;
        if (n != 0) {
            tr_printf(t, "\n      %lu, %s: %lu", (unsigned long)k, tr_ni_why(k),
                      (unsigned long)n);
            any = 1;
        }
    }
    tr_put(t, any ? "\n" : " none\n");
    w = rd32(b + 44);
    tr_printf(t, "    Functions with no pin: %lu seen, %lu started\n",
              (unsigned long)(w & 0xFFFFUL), (unsigned long)(w >> 16));
    tr_put(t, "    Raw:\n");
    raw_dwords(t, b, TR_NIDIAG_WORDS, 6);
}

/* ---- task 44.53's records ------------------------------------------------ */

static const char *irq_why(TR_U32 v)
{
    switch (v) {
    case 0: return "given";
    case 1: return "the function's pin is not 1 to 4";
    case 2: return "the port's pin is not 1 to 4";
    case 3: return "the port's line is not usable";
    case 4: return "it arrives on another of the port's pins";
    case 5: return "the path to the port is not one it follows";
    case 6: return "no active tunnel port above it";
    case 7: return "the function or a bridge not read";
    case 8: return "a bridge's secondary bus moved";
    default: return "not one this tool knows";
    }
}

static int hexval(const unsigned char *p, TR_U32 n, TR_U32 *out)
{
    TR_U32 v = 0;
    TR_U32 i;
    unsigned char c;

    if (n != 8) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (TR_U32)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            v |= (TR_U32)(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            v |= (TR_U32)(c - 'a' + 10);
        } else {
            return 0;
        }
    }
    *out = v;
    return 1;
}

/* One pass over the log: each record of the table's, printed when its
 * ordinal is at least `from` (t NULL: counted only). */
static int irqPass(TR_TEXT *t, const unsigned char *log, TR_U32 n,
                   int indent, int from)
{
    TR_U32 at = 0;
    TR_U32 end;
    TR_U32 eq;
    TR_U32 v;
    TR_U32 i;
    char loc[24];
    char raw[80];
    int k;
    int found = 0;
    size_t len;

    while (at < n) {
        end = at;
        while (end < n && log[end] != '\r' && log[end] != '\n') {
            end++;
        }
        for (eq = at; eq < end && log[eq] != '='; eq++) {
            ;
        }
        for (k = 0; k < IRQ_LABELS && eq < end; k++) {
            len = strlen(trIrq[k].Label);
            if ((TR_U32)len != eq - at ||
                memcmp(log + at, trIrq[k].Label, len) != 0 ||
                !hexval(log + eq + 1, end - eq - 1, &v)) {
                continue;
            }
            if (t == NULL || found < from) {
                found++;
                break;
            }
            for (i = 0; i < end - at && i < sizeof(raw) - 1; i++) {
                raw[i] = (char)log[at + i];
            }
            raw[i] = 0;
            tr_diag_bdf(v & 0xFFFFUL, loc, sizeof(loc));
            pad(t, indent);
            tr_printf(t, "%s\n", raw);
            pad(t, indent + 2);
            if (trIrq[k].Kind == IRQ_V_LINE) {
                tr_printf(t, "%s: line %lu, %s\n", loc,
                          (unsigned long)(v >> 16), trIrq[k].What);
            } else if (trIrq[k].Kind == IRQ_V_WHY) {
                tr_printf(t, "%s: %s:\n", loc, trIrq[k].What);
                pad(t, indent + 4);
                tr_printf(t, "%s\n", irq_why(v >> 16));
            } else {
                tr_printf(t, "%s: %s\n", loc, trIrq[k].What);
            }
            found++;
            break;
        }
        at = end;
        while (at < n && (log[at] == '\r' || log[at] == '\n')) {
            at++;
        }
    }
    return found;
}

int tr_irq_lines(TR_TEXT *t, const unsigned char *log, TR_U32 n, int indent,
                 int max)
{
    int all = irqPass(NULL, log, n, indent, 0);
    int from = all > max ? all - max : 0;

    if (from > 0) {
        pad(t, indent);
        tr_printf(t, "(the last %d of %d records)\n", max, all);
    }
    (void)irqPass(t, log, n, indent, from);
    return all;
}

/* ---- the node ------------------------------------------------------------ */

void tr_diag_head(TR_TEXT *t)
{
    tr_put(t, "PCI and bridge diagnostics\n\n"
              "What this package's drivers recorded about PCI bridges and "
              "interrupts,\nfrom the registry and the connection manager's "
              "log. Reading it changes\nnothing. Edit > Copy with nothing "
              "selected copies all of it.\n\n");
}

void tr_diag_wrap(TR_TEXT *t, int indent, const char *s)
{
    size_t n = strlen(s);
    size_t at = 0;
    size_t k;
    size_t room = (size_t)(79 - indent);
    char part[80];

    while (at < n) {
        k = n - at > room ? room : n - at;
        memcpy(part, s + at, k);
        part[k] = 0;
        pad(t, indent);
        tr_printf(t, "%s\n", part);
        at += k;
    }
}

void tr_diag_nhi(TR_TEXT *t, const char *name, const char *id)
{
    tr_diag_wrap(t, 2, name);
    tr_diag_wrap(t, 4, id);
}
