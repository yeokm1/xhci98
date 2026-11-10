/*
 * tr_net.c - the pure half of XHCITR's "Thunderbolt networking" nodes
 * (tr_net.h; design record 35 section 17, task 48.18). No Windows call:
 * test\test_trnet.c links it under /Za. C89.
 *
 * The answer is read as bytes at cm_qnet.h's offsets, never overlaid. The
 * peer's texts arrive printable already (the driver writes any other byte
 * as '?'); they are read only within their 33 bytes.
 */

#include <string.h>
#include "tr_net.h"

static TR_U32 qn32(const unsigned char *b, TR_U32 at)
{
    return (TR_U32)b[at] | ((TR_U32)b[at + 1] << 8) |
           ((TR_U32)b[at + 2] << 16) | ((TR_U32)b[at + 3] << 24);
}

int tr_qnet_parse(const unsigned char *b, size_t n, TR_QNET *q)
{
    TR_U32 bytes;
    TR_U32 at;
    TR_U32 sz;
    TR_U32 shape;
    TR_U32 count;
    TR_U32 esz;
    TR_U32 i;

    memset(q, 0, sizeof(*q));
    if (b == NULL || n < CM_QN_HDR_BYTES || qn32(b, CM_QN_O_MAGIC) != CM_QN_MAGIC ||
        qn32(b, CM_QN_O_VERSION) != CM_QN_VERSION) {
        return 0;
    }
    bytes = qn32(b, CM_QN_O_BYTES);
    q->Bytes = bytes;
    if (bytes < CM_QN_HDR_BYTES) {
        return 0;
    }
    if ((size_t)bytes > n) {
        return -1;
    }
    q->Flags = qn32(b, CM_QN_O_FLAGS);
    q->EpochLo = qn32(b, CM_QN_O_EPOCH_LO);
    q->EpochHi = qn32(b, CM_QN_O_EPOCH_HI);
    q->Pass = qn32(b, CM_QN_O_PASS);
    q->AgeMs = qn32(b, CM_QN_O_PASS_AGE_MS);
    q->NetValue = qn32(b, CM_QN_O_NET_VALUE);
    q->E2EValue = qn32(b, CM_QN_O_E2E_VALUE);
    q->Cm = qn32(b, CM_QN_O_CM);

    /* The service block: whole, inside the answer. */
    at = qn32(b, CM_QN_O_SVC_OFFSET);
    sz = qn32(b, CM_QN_O_SVC_BYTES);
    /* A driver before task 48.38 wrote CM_QN_SVC_MIN_BYTES. */
    if (sz < CM_QN_SVC_MIN_BYTES || at < CM_QN_HDR_BYTES || at > bytes ||
        sz > bytes - at) {
        return 0;
    }
    q->Svc = b + at;
    q->SvcBytes = sz;

    /* The links. */
    at = qn32(b, CM_QN_O_LINK_OFFSET);
    shape = qn32(b, CM_QN_O_LINK_SHAPE);
    count = shape & 0xFFFFUL;
    esz = shape >> 16;
    if (count > CM_QN_MAX_LINKS) {
        return 0;
    }
    if (count != 0) {
        if (esz < CM_QN_LINK_MIN_BYTES || at < CM_QN_HDR_BYTES || at > bytes ||
            count * esz > bytes - at) {
            return 0;
        }
        for (i = 0; i < count; i++) {
            q->Link[i] = b + at + i * esz;
            /* The whole texts claimed in an entry too short to hold them. */
            if ((qn32(q->Link[i], CM_QN_L_FLAGS) & CM_QN_LF_TEXT_L) &&
                esz < CM_QN_LINK_BYTES) {
                return 0;
            }
        }
    }
    q->LinkCount = (int)count;

    /* The client block: its first word its bytes. */
    at = qn32(b, CM_QN_O_CLIENT_OFFSET);
    if (at != 0) {
        if (at < CM_QN_HDR_BYTES || at > bytes || bytes - at < 4) {
            return 0;
        }
        sz = qn32(b, at);
        if (sz < 4 || sz > bytes - at) {
            return 0;
        }
        q->Client = b + at;
        q->ClientBytes = sz;
    }
    return 1;
}

int tr_qnet_show(const TR_QNET *q)
{
    return q->NetValue != 0 || q->LinkCount > 0;
}

/* A peer's text of a link entry (`device` its device text, else its
 * vendor text) into out: the whole text where the entry has it
 * (CM_QN_LF_TEXT_L), else the 32 characters a driver before revision 23
 * wrote. */
static void qn_text(const unsigned char *link, int device, char *out, size_t cap)
{
    size_t i;
    size_t chars = CM_QN_TEXT_CHARS;
    TR_U32 at = device ? CM_QN_L_DEVICE_TEXT : CM_QN_L_VENDOR_TEXT;

    if (qn32(link, CM_QN_L_FLAGS) & CM_QN_LF_TEXT_L) {
        chars = CM_QN_TEXT_L_CHARS;
        at = device ? CM_QN_L_DEVICE_TEXT_L : CM_QN_L_VENDOR_TEXT_L;
    }
    for (i = 0; i + 1 < cap && i < chars && link[at + i] != 0; i++) {
        unsigned char c = link[at + i];
        out[i] = (char)((c >= 0x20 && c <= 0x7E) ? c : '?');
    }
    if (cap > 0) {
        out[i] = '\0';
    }
}

int tr_qnet_kind(const unsigned char *link, char *kindText, size_t kindCap,
                 char *name, size_t nameCap)
{
    char vt[CM_QN_TEXT_L_BYTES + 1];
    char dt[CM_QN_TEXT_L_BYTES + 1];
    int kind;

    if (nameCap > 0) {
        name[0] = '\0';
    }
    if ((qn32(link, CM_QN_L_FLAGS) & CM_QN_LF_BLOCK) == 0) {
        tr_copy(kindText, "not read yet", kindCap);
        return TR_NETK_NONE;
    }
    qn_text(link, 0, vt, sizeof(vt));
    qn_text(link, 1, dt, sizeof(dt));
    /* Record 35 section 2.3: Linux publishes vendorid 1D6Bh, "Linux", and
     * its host name as the device text; section 3.7: this driver's own. */
    if (qn32(link, CM_QN_L_VENDOR_ID) == 0x1D6BUL && strcmp(vt, "Linux") == 0) {
        kind = TR_NETK_LINUX;
        tr_copy(kindText, "Linux (thunderbolt_net)", kindCap);
        tr_copy(name, dt, nameCap);
    } else if (strcmp(vt, "xHCI98") == 0 && strcmp(dt, "xhcicm") == 0) {
        kind = TR_NETK_XHCI98;
        tr_copy(kindText, "xHCI98", kindCap);
    } else if (qn32(link, CM_QN_L_VENDOR_ID) == 0x045EUL) {
        /* Record 35 section 3.12: Windows 11's block, read on the owner's
         * bench of 2026-11-09; its texts were not read. */
        kind = TR_NETK_WINDOWS;
        tr_copy(kindText, "Windows (USB4 P2P network adapter)", kindCap);
        tr_copy(name, dt, nameCap);
    } else {
        kind = TR_NETK_UNKNOWN;
        tr_copy(kindText, "not known", kindCap);
        tr_copy(name, dt[0] != '\0' ? dt : vt, nameCap);
    }
    return kind;
}

/* The discovery state (record 35 section 3.5), TBT_XDP_S_*. */
static const char *qn_disc(TR_U32 s)
{
    switch (s) {
    case 0: return "idle";
    case 1: return "asking for its UUID";
    case 2: return "reading the link";
    case 3:
    case 4: return "bonding the lanes";
    case 5: return "reading its properties";
    case 6: return "found";
    case 7: return "failed";
    case 8: return "cable loops back to this computer";
    default: return "unknown";
    }
}

/* The service state (record 35 section 4), TBT_NET_S_*. */
static const char *qn_net(TR_U32 s)
{
    switch (s) {
    case 0: return "not logged in";
    case 1: return "logging in";
    case 2: return "logged in";
    case 3: return "logging out";
    case 4: return "no free path to log in with";
    default: return "unknown";
    }
}

static void qn_rate(TR_U32 mbps, TR_U32 lanes, char *out, size_t cap)
{
    if (mbps == 0) {
        tr_copy(out, "not known", cap);
    } else if (mbps % 1000UL == 0) {
        tr_sprintf(out, cap, "%lu Gb/s", (unsigned long)(mbps / 1000UL));
    } else {
        tr_sprintf(out, cap, "%lu Mb/s", (unsigned long)mbps);
    }
    if (mbps != 0 && lanes != 0) {
        size_t n = strlen(out);
        tr_sprintf(out + n, cap - n, ", %lu lane%s", (unsigned long)lanes,
                   lanes == 1 ? "" : "s");
    }
}

void tr_qnet_label(const TR_QNET *q, char *out, size_t cap)
{
    char rate[40];
    TR_U32 st = qn32(q->Svc, CM_QN_S_STATE);

    if (q->NetValue == 0) {
        tr_copy(out, "Thunderbolt networking - off", cap);
    } else if (st == CM_QN_STATE_NO_DRIVER) {
        tr_copy(out, "Thunderbolt networking - no network driver", cap);
    } else if (st == CM_QN_STATE_UP) {
        if (qn32(q->Svc, CM_QN_S_MBPS) != 0) {
            qn_rate(qn32(q->Svc, CM_QN_S_MBPS), 0, rate, sizeof(rate));
            tr_sprintf(out, cap, "Thunderbolt networking - up, %s", rate);
        } else {
            tr_copy(out, "Thunderbolt networking - up", cap);
        }
    } else if (st == CM_QN_STATE_WAITING) {
        int i;
        int found = 0;

        for (i = 0; i < q->LinkCount; i++) {
            if (!(qn32(q->Link[i], CM_QN_L_FLAGS) & CM_QN_LF_UNANNOUNCED)) {
                break;
            }
            found++;
        }
        if (found > 0 && found == q->LinkCount) {
            tr_copy(out, "Thunderbolt networking - not connected", cap);
        } else {
            tr_copy(out, q->LinkCount > 0 ? "Thunderbolt networking - connecting"
                                          : "Thunderbolt networking - no other "
                                            "computer connected", cap);
        }
    } else {
        tr_copy(out, "Thunderbolt networking - off", cap);
    }
}

/* A link's state in a few words. */
static const char *qn_link_state(const unsigned char *l)
{
    TR_U32 flags = qn32(l, CM_QN_L_FLAGS);
    TR_U32 disc = qn32(l, CM_QN_L_DISC_STATE);

    if (flags & CM_QN_LF_SERVICE) {
        return "connected";
    }
    if (flags & CM_QN_LF_UNANNOUNCED) {
        return "found, not connected";
    }
    if (qn32(l, CM_QN_L_NET_STATE) == 1) {
        return "logging in";
    }
    if (disc == 6) {
        return (flags & CM_QN_LF_USB4NET) ? "found, not connected"
                                          : "found, no networking offered";
    }
    return qn_disc(disc);
}

void tr_qnet_link_label(const TR_QNET *q, int i, char *out, size_t cap)
{
    const unsigned char *l;
    char kind[40];
    char name[CM_QN_TEXT_L_BYTES * 2 + 4];
    int k;

    if (i < 0 || i >= q->LinkCount) {
        tr_copy(out, "Link", cap);
        return;
    }
    l = q->Link[i];
    k = tr_qnet_kind(l, kind, sizeof(kind), name, sizeof(name));
    if (name[0] == '\0' && k == TR_NETK_XHCI98) {
        tr_copy(name, "a computer with xHCI98", sizeof(name));
    }
    if (name[0] != '\0') {
        tr_sprintf(out, cap, "Link on port %lu - %s: %s",
                   (unsigned long)qn32(l, CM_QN_L_LINK), qn_link_state(l), name);
    } else {
        tr_sprintf(out, cap, "Link on port %lu - %s",
                   (unsigned long)qn32(l, CM_QN_L_LINK), qn_link_state(l));
    }
}

int tr_qnet_link_match(const TR_QNET *now, const unsigned char *was)
{
    int i;

    for (i = 0; i < now->LinkCount; i++) {
        const unsigned char *l = now->Link[i];

        if (qn32(l, CM_QN_L_LINK) != qn32(was, CM_QN_L_LINK)) {
            continue;
        }
        if ((qn32(l, CM_QN_L_FLAGS) & CM_QN_LF_UUID) &&
            (qn32(was, CM_QN_L_FLAGS) & CM_QN_LF_UUID) &&
            memcmp(l + CM_QN_L_UUID, was + CM_QN_L_UUID, 16) != 0) {
            return -1;
        }
        return i;
    }
    return -1;
}

void tr_qnet_mac(const unsigned char *m, char *out, size_t cap)
{
    tr_sprintf(out, cap, "%02X-%02X-%02X-%02X-%02X-%02X", (unsigned int)m[0],
               (unsigned int)m[1], (unsigned int)m[2], (unsigned int)m[3],
               (unsigned int)m[4], (unsigned int)m[5]);
}

void tr_qnet_u64(TR_U32 lo, TR_U32 hi, char *out, size_t cap)
{
    char digits[24];
    int n = 0;
    int i;

    /* Repeated division by 10 in 16-bit steps: no 64-bit arithmetic. */
    do {
        TR_U32 r;
        TR_U32 part;

        part = hi >> 16;
        r = part % 10UL;
        hi = ((part / 10UL) << 16) | (hi & 0xFFFFUL);
        part = (r << 16) | (hi & 0xFFFFUL);
        r = part % 10UL;
        hi = (hi & 0xFFFF0000UL) | (part / 10UL);
        part = (r << 16) | (lo >> 16);
        r = part % 10UL;
        lo = ((part / 10UL) << 16) | (lo & 0xFFFFUL);
        part = (r << 16) | (lo & 0xFFFFUL);
        r = part % 10UL;
        lo = (lo & 0xFFFF0000UL) | (part / 10UL);
        digits[n++] = (char)('0' + (int)r);
    } while ((lo != 0 || hi != 0) && n < (int)sizeof(digits) - 1);
    i = 0;
    while (n > 0 && (size_t)i + 1 < cap) {
        out[i++] = digits[--n];
    }
    if (cap > 0) {
        out[i] = '\0';
    }
}

static void qn_stns(TR_U32 s, char *out, size_t cap)
{
    out[0] = '\0';
    if (s & 0x1UL) {
        tr_copy(out, "end-to-end flow control", cap);
    }
    if (s & 0x2UL) {
        size_t n = strlen(out);
        tr_sprintf(out + n, cap - n, "%sframe IDs", n ? ", " : "");
    }
    if (s & 0x4UL) {
        size_t n = strlen(out);
        tr_sprintf(out + n, cap - n, "%slarge frames", n ? ", " : "");
    }
    if (out[0] == '\0') {
        tr_copy(out, "none", cap);
    }
}

static void qn_uuid(const unsigned char *l, char *out, size_t cap)
{
    unsigned char u[16];
    int i;

    /* TBT_UUID's word n is the canonical bytes 4n to 4n+3, little-endian. */
    for (i = 0; i < 16; i++) {
        u[i] = l[CM_QN_L_UUID + i];
    }
    tr_sprintf(out, cap, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-"
               "%02X%02X%02X%02X%02X%02X",
               u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9],
               u[10], u[11], u[12], u[13], u[14], u[15]);
}

/* One details line, "<label><head><q><text><q>", within 79 columns: a
 * text too long for the line goes on the next one, indented 4. */
static void qn_line(TR_TEXT *t, const char *label, const char *head,
                    const char *text, const char *q)
{
    if (strlen(label) + strlen(head) + strlen(text) + 2 * strlen(q) <= 79) {
        tr_printf(t, "%s%s%s%s%s\n", label, head, q, text, q);
    } else {
        tr_printf(t, "%s%s\n    %s%s%s\n", label, head, q, text, q);
    }
}

static void qn_link_text(TR_TEXT *t, const unsigned char *l, int pub)
{
    TR_U32 flags = qn32(l, CM_QN_L_FLAGS);
    TR_U32 logins = qn32(l, CM_QN_L_LOGINS);
    TR_U32 peer = qn32(l, CM_QN_L_PEER_LOGINS);
    char kind[40];
    char name[CM_QN_TEXT_L_BYTES * 2 + 4];
    char text[CM_QN_TEXT_L_BYTES + 1];
    char buf[96];

    tr_printf(t, "Link on port %lu\n", (unsigned long)qn32(l, CM_QN_L_LINK));
    tr_printf(t, "  State:                %s%s\n",
              qn_net(qn32(l, CM_QN_L_NET_STATE)),
              (flags & CM_QN_LF_SERVICE) ? " (carries the network)" : "");
    tr_printf(t, "  Discovery:            %s\n",
              qn_disc(qn32(l, CM_QN_L_DISC_STATE)));
    if (flags & CM_QN_LF_UNANNOUNCED) {
        tr_put(t, "  Not connected:        the cable was plugged in again and "
                  "the Thunderbolt\n"
                  "                        firmware did not report the other "
                  "computer.\n"
                  "                        Restart this computer with the "
                  "cable in.\n");
    }
    (void)tr_qnet_kind(l, kind, sizeof(kind), name, sizeof(name));
    if (name[0] != '\0') {
        qn_line(t, "  Other computer:       ", "",
                pub ? "(its name is not included in the snapshot)" : name, "");
    }
    tr_printf(t, "  Kind:                 %s\n", kind);
    if (flags & CM_QN_LF_BLOCK) {
        qn_text(l, 0, text, sizeof(text));
        tr_sprintf(buf, sizeof(buf), "%04lX ",
                   (unsigned long)qn32(l, CM_QN_L_VENDOR_ID));
        qn_line(t, "  Its vendor:           ", buf, pub ? "..." : text, "\"");
        qn_text(l, 1, text, sizeof(text));
        tr_sprintf(buf, sizeof(buf), "%04lX ",
                   (unsigned long)qn32(l, CM_QN_L_DEVICE_ID));
        qn_line(t, "  Its device:           ", buf, pub ? "..." : text, "\"");
        tr_printf(t, "  Its revision:         %08lX\n",
                  (unsigned long)qn32(l, CM_QN_L_DEVICE_REV));
    }
    if (flags & CM_QN_LF_UUID) {
        if (pub) {
            tr_put(t, "  Its UUID:             (not included in the snapshot)\n");
        } else {
            qn_uuid(l, buf, sizeof(buf));
            tr_printf(t, "  Its UUID:             %s\n", buf);
        }
    }
    if (flags & CM_QN_LF_FIRMWARE) {
        tr_put(t, "  Speed:                not known (the firmware runs the "
                  "link)\n");
    } else {
        qn_rate(qn32(l, CM_QN_L_MBPS), qn32(l, CM_QN_L_LANES), buf, sizeof(buf));
        tr_printf(t, "  Speed:                %s\n", buf);
    }
    if (flags & CM_QN_LF_BLOCK) {
        if (flags & CM_QN_LF_USB4NET) {
            qn_stns(qn32(l, CM_QN_L_PRTCSTNS), buf, sizeof(buf));
            tr_printf(t, "  Networking offered:   yes (%s)\n", buf);
        } else {
            tr_put(t, "  Networking offered:   no\n");
        }
    }
    tr_printf(t, "  Inter-domain packets: %lu sent, %lu received,\n",
              (unsigned long)qn32(l, CM_QN_L_SENT),
              (unsigned long)qn32(l, CM_QN_L_RECEIVED));
    tr_printf(t, "                        %lu resent, %lu malformed\n",
              (unsigned long)qn32(l, CM_QN_L_RESENDS),
              (unsigned long)qn32(l, CM_QN_L_MALFORMED));
    tr_printf(t, "  Logins:               %lu sent, %lu failed; %lu received, "
                 "%lu refused\n",
              (unsigned long)(logins & 0xFFFFUL), (unsigned long)(logins >> 16),
              (unsigned long)(peer & 0xFFFFUL), (unsigned long)(peer >> 16));
    tr_printf(t, "  Services:             %lu up, %lu failed\n",
              (unsigned long)qn32(l, CM_QN_L_SERVICES_UP),
              (unsigned long)qn32(l, CM_QN_L_SERVICE_FAILS));
    tr_printf(t, "  Discovery restarts:   %lu\n",
              (unsigned long)qn32(l, CM_QN_L_RESTARTS));
}

static void qn_client_text(TR_TEXT *t, const TR_QNET *q)
{
    const unsigned char *c = q->Client;
    char a[24];
    char b[24];
    TR_U32 f;

    if (c == NULL || q->ClientBytes < CM_QN_C_KNOWN_BYTES) {
        tr_put(t, "  Sent and received:    not given by the network driver\n");
        return;
    }
    f = qn32(c, CM_QN_C_FLAGS);
    tr_qnet_u64(qn32(c, CM_QN_C_TX_BYTES_LO), qn32(c, CM_QN_C_TX_BYTES_HI), a,
                sizeof(a));
    tr_printf(t, "  Sent:                 %lu packets, %s bytes\n",
              (unsigned long)qn32(c, CM_QN_C_TX_PACKETS), a);
    tr_qnet_u64(qn32(c, CM_QN_C_RX_BYTES_LO), qn32(c, CM_QN_C_RX_BYTES_HI), b,
                sizeof(b));
    tr_printf(t, "  Received:             %lu packets, %s bytes\n",
              (unsigned long)qn32(c, CM_QN_C_RX_PACKETS), b);
    if (f & CM_QN_CF_BYTES32) {
        tr_put(t, "                        (the byte counts start again at "
                  "4 GB)\n");
    }
    tr_printf(t, "  Errors:               %lu sending, %lu receiving\n",
              (unsigned long)qn32(c, CM_QN_C_TX_ERRORS),
              (unsigned long)qn32(c, CM_QN_C_RX_ERRORS));
    tr_printf(t, "  Dropped sending:      %lu (link down)\n",
              (unsigned long)qn32(c, CM_QN_C_TX_DOWN));
    tr_printf(t, "  Dropped receiving:    %lu (no buffer), %lu (link down)\n",
              (unsigned long)qn32(c, CM_QN_C_RX_NO_BUFFER),
              (unsigned long)qn32(c, CM_QN_C_RX_NOT_UP));
    tr_printf(t, "  Network driver:       %s, MTU %lu%s\n",
              qn32(c, CM_QN_C_DRIVER) == CM_QN_DRV_NDIS6 ? "xhcinet6.sys"
                                                          : "xhcinet.sys",
              (unsigned long)qn32(c, CM_QN_C_MTU),
              (f & CM_QN_CF_ASLEEP) ? " (asleep)" : "");
}

void tr_qnet_text(TR_TEXT *t, const TR_QNET *q, int link, const char *ip,
                  int pub)
{
    const unsigned char *s = q->Svc;
    TR_U32 st = qn32(s, CM_QN_S_STATE);
    TR_U32 mbps;
    char buf[96];
    int i;

    if (link >= 0) {
        if (link < q->LinkCount) {
            qn_link_text(t, q->Link[link], pub);
        }
        return;
    }
    tr_put(t, "Thunderbolt networking\n");
    tr_printf(t, "  XhciCmNet:            %lu (%s)\n", (unsigned long)q->NetValue,
              q->NetValue != 0 ? "on" : "off");
    tr_printf(t, "  State:                %s\n",
              st == CM_QN_STATE_UP ? "up"
              : st == CM_QN_STATE_WAITING
                  ? (q->LinkCount > 0 ? "connecting"
                                      : "no other computer connected")
              : st == CM_QN_STATE_NO_DRIVER ? "no network driver loaded"
                                            : "off");
    if (q->Flags & CM_QN_F_MAC) {
        if (pub) {
            tr_put(t, "  Adapter MAC:          (not included in the "
                      "snapshot)\n");
        } else {
            tr_qnet_mac(s + CM_QN_S_MAC, buf, sizeof(buf));
            tr_printf(t, "  Adapter MAC:          %s\n", buf);
        }
    }
    if (ip != NULL && !pub) {
        /* One address a line: tr_netw.c separates them with ", ". */
        const char *a = ip;
        const char *c;
        char one[80];
        int first = 1;

        for (;;) {
            size_t len;

            c = strstr(a, ", ");
            len = c != NULL ? (size_t)(c - a) : strlen(a);
            if (len >= sizeof(one)) {
                len = sizeof(one) - 1;
            }
            memcpy(one, a, len);
            one[len] = '\0';
            tr_printf(t, "%s%s\n", first ? "  IP address:           "
                                         : "                        ", one);
            first = 0;
            if (c == NULL) {
                break;
            }
            a = c + 2;
        }
    }
    if (st == CM_QN_STATE_UP) {
        mbps = qn32(s, CM_QN_S_MBPS);
        if (q->Cm == CM_ST_CM_ICM) {
            tr_printf(t, "  Link:                 port %lu, speed not known "
                         "(the firmware runs it)\n",
                      (unsigned long)qn32(s, CM_QN_S_LINK));
        } else {
            qn_rate(mbps, qn32(s, CM_QN_S_LANES), buf, sizeof(buf));
            tr_printf(t, "  Link:                 port %lu, %s\n",
                      (unsigned long)qn32(s, CM_QN_S_LINK), buf);
        }
        tr_printf(t, "  Flow control:         %s\n",
                  qn32(s, CM_QN_S_E2E) ? "end-to-end" : "none");
        if (!pub) {
            tr_qnet_mac(s + CM_QN_S_PEER_MAC, buf, sizeof(buf));
            tr_printf(t, "  Other computer's MAC: %s\n", buf);
        }
    }
    tr_printf(t, "  Largest frame:        %lu bytes\n",
              (unsigned long)qn32(s, CM_QN_S_MAX_FRAME));
    qn_client_text(t, q);
    tr_printf(t, "  USB4 frames in:       %lu, %lu with errors, %lu dropped\n",
              (unsigned long)qn32(s, CM_QN_S_RX_FRAMES),
              (unsigned long)qn32(s, CM_QN_S_RX_ERRORS),
              (unsigned long)qn32(s, CM_QN_S_RX_DROPPED));
    tr_printf(t, "  USB4 frames out:      %lu, %lu refused\n",
              (unsigned long)qn32(s, CM_QN_S_TX_FRAMES),
              (unsigned long)qn32(s, CM_QN_S_TX_REFUSED));
    tr_printf(t, "  Link changes:         %lu up, %lu down\n",
              (unsigned long)qn32(s, CM_QN_S_LINK_UPS),
              (unsigned long)qn32(s, CM_QN_S_LINK_DOWNS));
    /* Record 35 section 5.5: word 92 was 0 (the poll alone) before task
     * 48.38, and the kicks came with it. */
    switch (qn32(s, CM_QN_S_IRQ_STATE)) {
    case CM_QN_IRQ_OPEN:
        tr_printf(t, "  Ring interrupt:       on (%lu wake-ups)\n",
                  (unsigned long)(q->SvcBytes >= CM_QN_S_IRQ_KICKS + 4
                                      ? qn32(s, CM_QN_S_IRQ_KICKS)
                                      : 0));
        break;
    case CM_QN_IRQ_LATCHED:
        tr_put(t, "  Ring interrupt:       off (fell back to checking every "
                  "clock tick)\n");
        break;
    default:
        tr_put(t, "  Ring interrupt:       off (checked every clock tick)\n");
        break;
    }
    if (q->AgeMs == CM_QN_UNKNOWN) {
        tr_printf(t, "  As of:                pass %lu\n", (unsigned long)q->Pass);
    } else {
        tr_printf(t, "  As of:                pass %lu, %lu ms ago\n",
                  (unsigned long)q->Pass, (unsigned long)q->AgeMs);
    }
    if (q->Flags & CM_QN_F_STOPPING) {
        tr_put(t, "  The controller is stopping.\n");
    }
    if (q->LinkCount == 0) {
        tr_put(t, "  No other computer is connected.\n");
    }
    for (i = 0; i < q->LinkCount; i++) {
        tr_put(t, "\n");
        qn_link_text(t, q->Link[i], pub);
    }
}
