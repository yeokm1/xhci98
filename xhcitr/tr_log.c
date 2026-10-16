/*
 * tr_log.c - the pure half of XHCITR's "Driver log" nodes (tr_log.h;
 * design record 25 sections 3.5, 4.5 and 4.6, revision 9). No Windows
 * call; test\test_trlog.c links it under /Za. C89.
 */

#include <string.h>
#include "tr_log.h"
#include "../src/cm/cm_query.h"

static TR_U32 trlGet(const unsigned char *b, TR_U32 o)
{
    return (TR_U32)b[o] | ((TR_U32)b[o + 1] << 8) | ((TR_U32)b[o + 2] << 16) |
           ((TR_U32)b[o + 3] << 24);
}

static void trlPut(unsigned char *b, TR_U32 o, TR_U32 v)
{
    b[o] = (unsigned char)(v & 0xFFUL);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[o + 2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[o + 3] = (unsigned char)((v >> 24) & 0xFFUL);
}

/* One byte of driver text into t: CR dropped (t writes CR LF for LF),
 * anything outside printable ASCII as '.'. */
static void trlCh(TR_TEXT *t, unsigned char c)
{
    char s[2];

    if (c == '\r') {
        return;
    }
    s[0] = (c == '\n' || (c >= 0x20 && c <= 0x7E)) ? (char)c : '.';
    s[1] = '\0';
    tr_put(t, s);
}

/* ======================================================================== */
/* xhcicm.sys                                                                */
/* ======================================================================== */

int tr_qlog_parse(const unsigned char *b, size_t n, TR_QLOG *l)
{
    TR_U32 co;
    TR_U32 lo;

    memset(l, 0, sizeof(*l));
    if (b == NULL || n < CM_QL_HDR_BYTES ||
        trlGet(b, CM_QL_O_MAGIC) != CM_QL_MAGIC ||
        trlGet(b, CM_QL_O_VERSION) != CM_QL_VERSION) {
        return 0;
    }
    l->Bytes = trlGet(b, CM_QL_O_BYTES);
    if (l->Bytes < CM_QL_HDR_BYTES || l->Bytes > CM_QL_ASK_BYTES) {
        return 0;
    }
    if (l->Bytes > n) {
        return -1;
    }
    l->Flags = trlGet(b, CM_QL_O_FLAGS);
    l->Verbosity = trlGet(b, CM_QL_O_VERBOSITY);
    l->VerbosityRead = trlGet(b, CM_QL_O_VERBOSITY_READ);
    l->EpochLo = trlGet(b, CM_QL_O_EPOCH_LO);
    l->EpochHi = trlGet(b, CM_QL_O_EPOCH_HI);
    co = trlGet(b, CM_QL_O_CTR_OFFSET);
    l->CtrBytes = trlGet(b, CM_QL_O_CTR_BYTES);
    lo = trlGet(b, CM_QL_O_LOG_OFFSET);
    l->LogBytes = trlGet(b, CM_QL_O_LOG_BYTES);
    l->Appends = trlGet(b, CM_QL_O_APPENDS);
    l->Dropped = trlGet(b, CM_QL_O_DROPPED);
    l->Pass = trlGet(b, CM_QL_O_PASS);
    l->AgeMs = trlGet(b, CM_QL_O_PASS_AGE_MS);
    /* Two comparisons, never a sum that could wrap. */
    if (l->CtrBytes > CM_QL_CTR_MAX || l->LogBytes > CM_QL_LOG_MAX ||
        co < CM_QL_HDR_BYTES || co > l->Bytes ||
        l->CtrBytes > l->Bytes - co || lo < CM_QL_HDR_BYTES ||
        lo > l->Bytes || l->LogBytes > l->Bytes - lo) {
        memset(l, 0, sizeof(*l));
        return 0;
    }
    l->Ctr = b + co;
    l->Log = b + lo;
    return 1;
}

void tr_log_advice_cm(TR_TEXT *t, const char *keyPath, int nt)
{
    tr_put(t, "  To record the log, set the DWORD value XhciLogVerbosity to 2\n"
              "  under this key");
    tr_put(t, nt ? " (as an administrator)" : "");
    tr_put(t, ", then restart Windows:\n");
    tr_printf(t, "    HKEY_LOCAL_MACHINE\\%s\n",
              keyPath != NULL && keyPath[0] ? keyPath
                                            : "(the controller's driver key)");
    tr_put(t, "  Set it back to 0 once the log has been sent.\n");
}

/* The counters' text, "name=XXXXXXXX" a line: the nonzero ones, in
 * decimal. */
static void trlCounters(TR_TEXT *t, const unsigned char *c, TR_U32 n)
{
    TR_U32 i = 0;
    TR_U32 shown = 0;
    TR_U32 lines = 0;

    while (i < n) {
        char name[72];
        TR_U32 k = 0;
        TR_U32 v = 0;
        TR_U32 digits = 0;
        int ok = 1;
        while (i < n && c[i] != '=' && c[i] != '\n') {
            if (k + 1 < sizeof(name)) {
                name[k++] = (c[i] > 0x20 && c[i] < 0x7F) ? (char)c[i] : '.';
            }
            i++;
        }
        name[k] = '\0';
        if (i < n && c[i] == '=') {
            i++;
            while (i < n && c[i] != '\r' && c[i] != '\n') {
                unsigned char d = c[i++];
                if (d >= '0' && d <= '9') {
                    v = (v << 4) | (TR_U32)(d - '0');
                } else if (d >= 'A' && d <= 'F') {
                    v = (v << 4) | (TR_U32)(d - 'A' + 10);
                } else {
                    ok = 0;
                }
                digits++;
            }
        } else {
            ok = 0;
        }
        while (i < n && c[i] != '\n') {
            i++;
        }
        i++;
        if (k == 0) {
            continue;
        }
        lines++;
        if (!ok || digits != 8) {
            tr_printf(t, "    %s (unreadable)\n", name);
            shown++;
        } else if (v != 0) {
            tr_printf(t, "    %-36s %lu\n", name, (unsigned long)v);
            shown++;
        }
    }
    tr_printf(t, "    (%lu of %lu nonzero.)\n", (unsigned long)shown,
              (unsigned long)lines);
}

void tr_qlog_text(TR_TEXT *t, const TR_QLOG *l, const char *keyPath, int nt)
{
    TR_U32 i;

    tr_put(t, "xhcicm driver log\n");
    if (l->Flags & CM_QL_F_RECORDING) {
        tr_printf(t, "  Recording:  on (XhciLogVerbosity %lu)\n",
                  (unsigned long)l->Verbosity);
    } else if (l->Flags & CM_QL_F_REFUSED) {
        tr_printf(t, "  Recording:  off - XhciLogVerbosity is %lu, which the "
                     "driver refused\n              (0 to 4 only), so it "
                     "applied 0\n", (unsigned long)l->VerbosityRead);
        tr_log_advice_cm(t, keyPath, nt);
    } else {
        tr_printf(t, "  Recording:  off (XhciLogVerbosity %lu; the log needs "
                     "2 or more)\n", (unsigned long)l->Verbosity);
        tr_log_advice_cm(t, keyPath, nt);
    }
    if (l->Flags & CM_QL_F_STOPPING) {
        tr_put(t, "  The controller is stopping.\n");
    }
    tr_put(t, "\n  Counters");
    if (l->Pass == 0) {
        tr_put(t, " (before the driver's first pass)");
    } else if (l->AgeMs != CM_Q_UNKNOWN) {
        tr_printf(t, " (pass %lu, %lu ms ago%s)", (unsigned long)l->Pass,
                  (unsigned long)l->AgeMs,
                  (l->Flags & CM_QL_F_INERT) ? "; the driver is idle" : "");
    }
    tr_put(t, ":\n");
    trlCounters(t, l->Ctr, l->CtrBytes);
    if (l->Flags & CM_QL_F_CTR_CUT) {
        tr_put(t, "    (more counters than the answer holds)\n");
    }
    tr_printf(t, "\n  Log, oldest record first (%lu bytes; %lu records since "
                 "the driver\n  was loaded", (unsigned long)l->LogBytes,
              (unsigned long)l->Appends);
    if (l->Dropped != 0) {
        tr_printf(t, "; %lu older bytes overwritten", (unsigned long)l->Dropped);
    }
    tr_put(t, "):\n");
    if (l->Flags & CM_QL_F_EMITTED) {
        tr_put(t, "  (this build hands the log to a debugger as it goes: "
                  "only what is not\n  handed over yet is here)\n");
    }
    if (l->Flags & CM_QL_F_WITHHELD) {
        tr_put(t, "  (values shown as -------- are addresses, withheld)\n");
    }
    if (l->LogBytes == 0) {
        tr_put(t, "  (empty)\n");
        return;
    }
    for (i = 0; i < l->LogBytes; i++) {
        trlCh(t, l->Log[i]);
    }
    if (l->Log[l->LogBytes - 1] != '\n') {
        tr_put(t, "\n");
    }
}

/* ======================================================================== */
/* xhci98.sys                                                                */
/* ======================================================================== */

void tr_snap_request(unsigned char *buf, TR_U32 region, TR_U32 offset)
{
    unsigned char *block = buf + TR_UU_PARAMETERS;

    memset(buf, 0, TR_SNAP_BUFFER_BYTES);
    trlPut(buf, TR_UU_REQUEST, TR_SNAP_PASS_THRU);
    trlPut(buf, TR_UU_REQUEST_BYTES, TR_SNAP_BUFFER_BYTES);
    /* {34F57942-2D69-4066-9CBD-82148E44BC20}, as the driver compares it
     * (src\xhci.h XHCI_SNAPSHOT_GUID_*). */
    trlPut(buf, TR_UU_GUID + 0, 0x34F57942UL);
    trlPut(buf, TR_UU_GUID + 4, 0x40662D69UL);
    trlPut(buf, TR_UU_GUID + 8, 0x1482BD9CUL);
    trlPut(buf, TR_UU_GUID + 12, 0x20BC448EUL);
    trlPut(buf, TR_UU_PARAM_BYTES, TR_SNAP_PARAM_BYTES);
    trlPut(block, 0, TR_SNAP_REQ_SIGNATURE);
    trlPut(block, 4, region);
    trlPut(block, 8, offset);
}

int tr_snap_window(const unsigned char *buf, size_t returned, TR_SNAPW *w)
{
    const unsigned char *h = buf + TR_UU_PARAMETERS;

    memset(w, 0, sizeof(*w));
    if (returned < TR_UU_PARAMETERS) {
        return TR_SW_BAD;
    }
    w->UuStatus = trlGet(buf, TR_UU_STATUS);
    if (w->UuStatus == TR_SNAP_UU_NOT_SUPPORTED) {
        return TR_SW_OFF;
    }
    if (w->UuStatus != 0) {
        return TR_SW_REFUSED;
    }
    if (returned < TR_UU_PARAMETERS + TR_SNAP_HDR_BYTES ||
        trlGet(h, 0) != TR_SNAP_SIGNATURE ||
        trlGet(h, 4) != TR_SNAP_SCHEMA || trlGet(h, 8) != TR_SNAP_HDR_BYTES) {
        return TR_SW_BAD;
    }
    w->Status = trlGet(h, 12);
    w->Region = trlGet(h, 16);
    w->Offset = trlGet(h, 20);
    w->RegionBytes = trlGet(h, 24);
    w->PayloadBytes = trlGet(h, 28);
    w->Flavour = trlGet(h, 48);
    w->VerbosityRead = trlGet(h, 52);
    w->VerbosityApplied = trlGet(h, 56);
    w->RingOffset = trlGet(h, 72);
    w->RingBytes = trlGet(h, 76);
    w->RingHead = trlGet(h, 80);
    w->RingUsed = trlGet(h, 84);
    if (w->PayloadBytes > TR_SNAP_PARAM_BYTES - TR_SNAP_HDR_BYTES ||
        returned - TR_UU_PARAMETERS - TR_SNAP_HDR_BYTES < w->PayloadBytes) {
        return TR_SW_BAD;
    }
    w->Payload = h + TR_SNAP_HDR_BYTES;
    return TR_SW_OK;
}

void tr_log_advice_hcd(TR_TEXT *t, int nt)
{
    tr_put(t, "  To record the log, run SNAPON.BAT from the package's "
              "tools\\xhcisnap\n  folder");
    tr_put(t, nt ? " as an administrator (on Vista and 7: right-click it,\n"
                   "  Run as administrator)" : "");
    tr_put(t, ", then restart Windows. SNAPOFF.BAT turns it off\n"
              "  again once the log has been sent.\n");
}

void tr_hcd_off_text(TR_TEXT *t, int nt)
{
    tr_put(t, "xhci98 driver log\n"
              "  Recording:  off (XhciLogVerbosity 0: the driver does not "
              "answer for its\n              log or its counters)\n");
    tr_log_advice_hcd(t, nt);
}

static const char *trlFlavour(TR_U32 f)
{
    switch (f) {
    case 1: return "release";
    case 2: return "debug";
    case 3: return "qemu";
    case 4: return "host test";
    default: return "unknown";
    }
}

void tr_hcd_head_text(TR_TEXT *t, const TR_SNAPW *w, int nt)
{
    tr_put(t, "xhci98 driver log\n");
    tr_printf(t, "  Build:      %s\n", trlFlavour(w->Flavour));
    if (w->VerbosityApplied >= 2 && w->VerbosityApplied <= 3) {
        tr_printf(t, "  Recording:  on (XhciLogVerbosity %lu)\n",
                  (unsigned long)w->VerbosityApplied);
    } else if (w->VerbosityApplied == 4) {
        tr_put(t, "  Recording:  on (XhciLogVerbosity 4)\n"
                  "  Level 4 records internal addresses, so this tool does "
                  "not show the log.\n  Run SNAPON.BAT (level 2) and restart "
                  "Windows for a log it can show.\n");
    } else {
        tr_printf(t, "  Recording:  off (XhciLogVerbosity %lu: counters "
                     "only)\n", (unsigned long)w->VerbosityApplied);
        tr_log_advice_hcd(t, nt);
    }
}

int tr_hcd_ring_text(TR_TEXT *t, const TR_SNAPW *w, TR_U32 asked)
{
    TR_U32 mask;
    TR_U32 start;
    TR_U32 i;
    TR_U32 skip;

    if (w->VerbosityApplied == 4) {
        return TR_RING_WITHHELD;
    }
    if (w->VerbosityApplied < 2) {
        return TR_RING_OFF;
    }
    if (w->Region != TR_SNAP_REGION_EXT || w->Offset != asked ||
        w->RingOffset != asked || w->RingBytes == 0 ||
        (w->RingBytes & (w->RingBytes - 1)) != 0 ||
        w->RingUsed > w->RingBytes || w->PayloadBytes < w->RingBytes ||
        w->Payload == NULL) {
        return TR_RING_BAD;
    }
    tr_printf(t, "\n  Log, oldest record first (%lu bytes):\n",
              (unsigned long)w->RingUsed);
    if (w->RingUsed == 0) {
        tr_put(t, "  (empty)\n");
        return TR_RING_EMPTY;
    }
    mask = w->RingBytes - 1;
    start = (w->RingHead - w->RingUsed) & mask;
    skip = 0;
    /* A full ring may begin inside a record the wrap cut. */
    if (w->RingUsed == w->RingBytes) {
        while (skip < w->RingUsed &&
               w->Payload[(start + skip) & mask] != '\n') {
            skip++;
        }
        if (skip < w->RingUsed) {
            skip++;
        }
    }
    for (i = skip; i < w->RingUsed; i++) {
        trlCh(t, w->Payload[(start + i) & mask]);
    }
    if (w->Payload[(start + w->RingUsed - 1) & mask] != '\n') {
        tr_put(t, "\n");
    }
    return TR_RING_SHOWN;
}

/* The names, in field order. */
static const char *const trlHcNames[] = {
#define TR_HC(n) #n,
#include "tr_hcctr.h"
#undef TR_HC
    NULL
};

TR_U32 tr_hc_count(void)
{
    return (TR_U32)(sizeof(trlHcNames) / sizeof(trlHcNames[0]) - 1);
}

const char *tr_hc_name(TR_U32 i)
{
    return i < tr_hc_count() ? trlHcNames[i] : NULL;
}

int tr_hcd_counters_text(TR_TEXT *t, const unsigned char *img, size_t n)
{
    TR_U32 bytes;
    TR_U32 version;
    TR_U32 headBytes;
    TR_U32 ports;
    TR_U32 portBytes;
    TR_U32 portsAt;
    TR_U32 counters;
    TR_U32 countersAt;
    TR_U32 i;
    TR_U32 shown = 0;

    tr_put(t, "\n  Counters (nonzero):\n");
    if (img == NULL || n < 32 || n > TR_SNAP_HCD_MAX_BYTES) {
        tr_put(t, "    (the driver's counter block was not read)\n");
        return 0;
    }
    bytes = (TR_U32)n;
    version = trlGet(img, 0);
    headBytes = trlGet(img, 4);
    ports = trlGet(img, 8);
    portBytes = trlGet(img, 12);
    portsAt = trlGet(img, 16);
    counters = trlGet(img, 20);
    countersAt = trlGet(img, 24);
    /* xhcisnap.c's hcd_shape_ok. */
    if (version == 0 || version > 1 || headBytes < 32 || headBytes > bytes ||
        (headBytes & 3UL) != 0 || portBytes < 64 || (portBytes & 3UL) != 0 ||
        portsAt < headBytes || portsAt > bytes || ports > 255 ||
        ports > (bytes - portsAt) / portBytes || countersAt > bytes ||
        countersAt < headBytes || (countersAt & 3UL) != 0 ||
        countersAt < portsAt || countersAt - portsAt < ports * portBytes ||
        counters > (bytes - countersAt) / 4) {
        tr_printf(t, "    (a counter block of version %lu, in a shape this "
                     "tool does not read)\n", (unsigned long)version);
        return 0;
    }
    for (i = 0; i < counters; i++) {
        TR_U32 v = trlGet(img, countersAt + i * 4);
        const char *name;
        if (v == 0) {
            continue;
        }
        name = tr_hc_name(i);
        if (name != NULL) {
            tr_printf(t, "    %-36s %lu\n", name, (unsigned long)v);
        } else {
            tr_printf(t, "    counter %-28lu %lu\n", (unsigned long)i,
                      (unsigned long)v);
        }
        shown++;
    }
    tr_printf(t, "    (%lu of %lu nonzero.)\n", (unsigned long)shown,
              (unsigned long)counters);
    return 1;
}
