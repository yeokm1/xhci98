/*
 * tr_core.c - the pure half of XHCITR.EXE and XHCITR64.EXE (design record
 * 25 section 10; roadmap-usb4.md task 41-A.2): text, the USB answers'
 * bytes, descriptor decoding, xhcicm.sys's query, the status record's
 * texts, the tray's event diff, the tree's keys and the command line.
 * No Windows call; test\test_tr.c links it under /Za. C89.
 */

#include <stdarg.h>
#include <string.h>
#include "tr_core.h"
#include "../xhciinst/inst_core.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"

/* cm_live.h's states, which that header cannot give a tool: it includes
 * the driver's compatibility header. The host suite checks them against
 * it (test_tr.c). */
#define CM_LIVE_OFF              0UL
#define CM_LIVE_ACTIVE           1UL
#define CM_LIVE_SUSPECT          2UL
#define CM_LIVE_ABSENT           3UL
#define CM_LIVE_CONFIG_LOST      4UL
#define CM_LIVE_RECOVERING       5UL
#define CM_LIVE_RECOVERY_FAILED  6UL
#define CM_LIVE_CONTAINED        7UL
#define CM_LIVE_REVOKED          8UL

/* ======================================================================== */
/* Text                                                                      */
/* ======================================================================== */

void tr_text_init(TR_TEXT *t, char *buf, size_t cap)
{
    t->Buf = buf;
    t->Cap = cap;
    t->Len = 0;
    t->Overflow = 0;
    if (cap > 0) {
        buf[0] = '\0';
    }
}

static void tr_ch(TR_TEXT *t, char c)
{
    if (t->Cap == 0) {
        t->Overflow = 1;
        return;
    }
    if (c == '\n') {
        if (t->Len + 2 >= t->Cap) {
            t->Overflow = 1;
            return;
        }
        t->Buf[t->Len++] = '\r';
    } else if (t->Len + 1 >= t->Cap) {
        t->Overflow = 1;
        return;
    }
    t->Buf[t->Len++] = c;
    t->Buf[t->Len] = '\0';
}

void tr_put(TR_TEXT *t, const char *s)
{
    while (*s != '\0') {
        tr_ch(t, *s++);
    }
}

static void tr_num(TR_TEXT *t, unsigned long v, int neg, unsigned int base,
                   int upper, int width, int zero, int left)
{
    char digits[24];
    int n = 0;
    int len;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        digits[n++] = set[v % base];
        v /= base;
    } while (v != 0 && n < (int)sizeof(digits));
    len = n + (neg ? 1 : 0);
    if (!left && !zero) {
        while (width > len) {
            tr_ch(t, ' ');
            width--;
        }
    }
    if (neg) {
        tr_ch(t, '-');
    }
    if (!left && zero) {
        while (width > len) {
            tr_ch(t, '0');
            width--;
        }
    }
    while (n > 0) {
        tr_ch(t, digits[--n]);
    }
    if (left) {
        while (width > len) {
            tr_ch(t, ' ');
            width--;
        }
    }
}

static void tr_vprintf(TR_TEXT *t, const char *fmt, va_list ap)
{
    while (*fmt != '\0') {
        int left = 0;
        int zero = 0;
        int width = 0;
        int isLong = 0;

        if (*fmt != '%') {
            tr_ch(t, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '-') {
            left = 1;
            fmt++;
        }
        if (*fmt == '0') {
            zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') {
            isLong = 1;
            fmt++;
        }
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len;
            if (s == NULL) {
                s = "(null)";
            }
            len = (int)strlen(s);
            if (!left) {
                while (width > len) {
                    tr_ch(t, ' ');
                    width--;
                }
            }
            tr_put(t, s);
            while (left && width > len) {
                tr_ch(t, ' ');
                width--;
            }
            break;
        }
        case 'c':
            tr_ch(t, (char)va_arg(ap, int));
            break;
        case 'd': {
            long v = isLong ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) {
                tr_num(t, (unsigned long)(-(v + 1)) + 1UL, 1, 10, 0, width,
                       zero, left);
            } else {
                tr_num(t, (unsigned long)v, 0, 10, 0, width, zero, left);
            }
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long v = isLong ? va_arg(ap, unsigned long)
                                     : (unsigned long)va_arg(ap, unsigned int);
            tr_num(t, v, 0, *fmt == 'u' ? 10U : 16U, *fmt == 'X', width, zero,
                   left);
            break;
        }
        case '%':
            tr_ch(t, '%');
            break;
        case '\0':
            return;
        default:
            tr_ch(t, '%');
            tr_ch(t, *fmt);
            break;
        }
        fmt++;
    }
}

void tr_printf(TR_TEXT *t, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    tr_vprintf(t, fmt, ap);
    va_end(ap);
}

void tr_sprintf(char *out, size_t cap, const char *fmt, ...)
{
    TR_TEXT t;
    va_list ap;

    tr_text_init(&t, out, cap);
    va_start(ap, fmt);
    tr_vprintf(&t, fmt, ap);
    va_end(ap);
}

void tr_hex(TR_TEXT *t, const unsigned char *b, size_t n, int indent)
{
    size_t i;
    int k;

    for (i = 0; i < n; i += 16) {
        size_t j;
        for (k = 0; k < indent; k++) {
            tr_ch(t, ' ');
        }
        tr_printf(t, "%04x:", (unsigned int)i);
        for (j = i; j < n && j < i + 16; j++) {
            tr_printf(t, " %02X", (unsigned int)b[j]);
        }
        tr_ch(t, '\n');
    }
}

void tr_copy(char *dst, const char *src, size_t cap)
{
    size_t i;

    if (cap == 0) {
        return;
    }
    for (i = 0; i + 1 < cap && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int tr_up(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

int tr_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tr_up((unsigned char)*a) != tr_up((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static unsigned int tr_w16(const unsigned char *b)
{
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8);
}

static TR_U32 tr_d32(const unsigned char *b)
{
    return (TR_U32)b[0] | ((TR_U32)b[1] << 8) | ((TR_U32)b[2] << 16) |
           ((TR_U32)b[3] << 24);
}

/* ======================================================================== */
/* The USB answers                                                           */
/* ======================================================================== */

int tr_wide_name(const unsigned char *b, size_t n, size_t offset, char *out,
                 size_t cap)
{
    size_t i;
    size_t o = 0;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    for (i = offset; i + 1 < n && o + 1 < cap; i += 2) {
        unsigned int c = tr_w16(b + i);
        if (c == 0) {
            break;
        }
        out[o++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    out[o] = '\0';
    return o > 0;
}

int tr_conn_parse(const unsigned char *b, size_t n, int ex, TR_CONN *c)
{
    TR_U32 i;

    memset(c, 0, sizeof(*c));
    if (n < TR_CI_BYTES) {
        return 0;
    }
    c->Port = tr_d32(b);
    memcpy(c->Dev, b + 4, 18);
    c->Config = b[22];
    if (ex) {
        c->Speed = b[23];
    } else {
        /* LowSpeed alone: Full and High look the same here. */
        c->Speed = b[23] ? TR_SPEED_LOW : TR_SPEED_UNKNOWN;
    }
    c->IsHub = b[24] != 0;
    c->Address = tr_w16(b + 25);
    c->OpenPipes = tr_d32(b + 27);
    c->Status = tr_d32(b + 31);
    for (i = 0; i < c->OpenPipes && c->PipeCount < TR_MAX_PIPES; i++) {
        size_t o = TR_CI_BYTES + (size_t)i * TR_CI_PIPE_BYTES;
        if (o + TR_CI_PIPE_BYTES > n) {
            break;
        }
        memcpy(c->Pipe[c->PipeCount++], b + o, 7);
    }
    return 1;
}

const char *tr_conn_status_text(TR_U32 status)
{
    switch (status) {
    case 0:
        return "no device connected";
    case 1:
        return "device connected";
    case 2:
        return "the device failed enumeration";
    case 3:
        return "general failure";
    case 4:
        return "the device caused an over-current";
    case 5:
        return "not enough power for the device";
    case 6:
        return "not enough bandwidth for the device";
    case 7:
        return "hubs nested too deeply";
    case 8:
        return "the device is in a legacy hub";
    case 9:
        return "the device is enumerating";
    case 10:
        return "the device is being reset";
    default:
        return "status not known";
    }
}

int tr_speed(unsigned int exSpeed, int haveV2, TR_U32 v2Flags)
{
    if (haveV2 && (v2Flags & TR_V2_F_SSP_NOW)) {
        return TR_SPEED_SUPERPLUS;
    }
    if (haveV2 && (v2Flags & TR_V2_F_SS_NOW)) {
        return TR_SPEED_SUPER;
    }
    if (exSpeed <= TR_SPEED_SUPER) {
        return (int)exSpeed;
    }
    return -1;
}

const char *tr_speed_name(int speed)
{
    switch (speed) {
    case TR_SPEED_LOW:
        return "Low Speed (1.5 Mbit/s)";
    case TR_SPEED_FULL:
        return "Full Speed (12 Mbit/s)";
    case TR_SPEED_HIGH:
        return "High Speed (480 Mbit/s)";
    case TR_SPEED_SUPER:
        return "SuperSpeed (5 Gbit/s)";
    case TR_SPEED_SUPERPLUS:
        return "SuperSpeedPlus";
    default:
        return "speed not reported";
    }
}

int tr_node_parse(const unsigned char *b, size_t n, unsigned int *ports,
                  int *busPowered)
{
    if (n < TR_NI_BYTES || tr_d32(b) != 0) {
        return 0;
    }
    *ports = b[6];
    *busPowered = b[75] != 0;
    return 1;
}

/* ======================================================================== */
/* Descriptors                                                               */
/* ======================================================================== */

const char *tr_class_name(unsigned int cls)
{
    switch (cls) {
    case 0x00:
        return "defined per interface";
    case 0x01:
        return "Audio";
    case 0x02:
        return "Communications";
    case 0x03:
        return "Human Interface Device";
    case 0x05:
        return "Physical";
    case 0x06:
        return "Image";
    case 0x07:
        return "Printer";
    case 0x08:
        return "Mass Storage";
    case 0x09:
        return "Hub";
    case 0x0A:
        return "CDC Data";
    case 0x0B:
        return "Smart Card";
    case 0x0D:
        return "Content Security";
    case 0x0E:
        return "Video";
    case 0x0F:
        return "Personal Healthcare";
    case 0x10:
        return "Audio/Video";
    case 0x11:
        return "Billboard";
    case 0x12:
        return "Type-C Bridge";
    case 0xDC:
        return "Diagnostic";
    case 0xE0:
        return "Wireless Controller";
    case 0xEF:
        return "Miscellaneous";
    case 0xFE:
        return "Application Specific";
    case 0xFF:
        return "Vendor Specific";
    default:
        return "class not known";
    }
}

static void tr_bcd(TR_TEXT *t, const char *what, unsigned int v)
{
    tr_printf(t, "  %-20s %x.%02x\n", what, (v >> 8) & 0xFFU, v & 0xFFU);
}

void tr_dec_device(TR_TEXT *t, const unsigned char *d, size_t n)
{
    tr_put(t, "Device descriptor\n");
    if (n < 18 || d[0] < 18 || d[1] != 0x01) {
        tr_put(t, "  (short or not a device descriptor)\n");
        tr_hex(t, d, n, 2);
        return;
    }
    tr_bcd(t, "bcdUSB", tr_w16(d + 2));
    tr_printf(t, "  %-20s 0x%02X (%s)\n", "bDeviceClass", (unsigned int)d[4],
              tr_class_name(d[4]));
    tr_printf(t, "  %-20s 0x%02X\n", "bDeviceSubClass", (unsigned int)d[5]);
    tr_printf(t, "  %-20s 0x%02X\n", "bDeviceProtocol", (unsigned int)d[6]);
    tr_printf(t, "  %-20s %u\n", "bMaxPacketSize0", (unsigned int)d[7]);
    tr_printf(t, "  %-20s 0x%04X\n", "idVendor", tr_w16(d + 8));
    tr_printf(t, "  %-20s 0x%04X\n", "idProduct", tr_w16(d + 10));
    tr_bcd(t, "bcdDevice", tr_w16(d + 12));
    tr_printf(t, "  %-20s %u\n", "iManufacturer", (unsigned int)d[14]);
    tr_printf(t, "  %-20s %u\n", "iProduct", (unsigned int)d[15]);
    tr_printf(t, "  %-20s %u\n", "iSerialNumber", (unsigned int)d[16]);
    tr_printf(t, "  %-20s %u\n", "bNumConfigurations", (unsigned int)d[17]);
    tr_hex(t, d, 18, 2);
}

static const char *tr_ep_type(unsigned int attr)
{
    switch (attr & 3U) {
    case 0:
        return "Control";
    case 1:
        return "Isochronous";
    case 2:
        return "Bulk";
    default:
        return "Interrupt";
    }
}

static void tr_one_desc(TR_TEXT *t, const unsigned char *d, unsigned int len)
{
    unsigned int type = d[1];

    switch (type) {
    case 0x02:
        if (len < 9) {
            break;
        }
        tr_put(t, "Configuration descriptor\n");
        tr_printf(t, "  %-20s %u\n", "wTotalLength", tr_w16(d + 2));
        tr_printf(t, "  %-20s %u\n", "bNumInterfaces", (unsigned int)d[4]);
        tr_printf(t, "  %-20s %u\n", "bConfigurationValue", (unsigned int)d[5]);
        tr_printf(t, "  %-20s %u\n", "iConfiguration", (unsigned int)d[6]);
        tr_printf(t, "  %-20s 0x%02X (%s%s)\n", "bmAttributes",
                  (unsigned int)d[7],
                  (d[7] & 0x40) ? "self-powered" : "bus-powered",
                  (d[7] & 0x20) ? ", remote wakeup" : "");
        tr_printf(t, "  %-20s %u (units of 2 mA, 8 mA at SuperSpeed)\n",
                  "bMaxPower", (unsigned int)d[8]);
        return;
    case 0x04:
        if (len < 9) {
            break;
        }
        tr_put(t, "Interface descriptor\n");
        tr_printf(t, "  %-20s %u\n", "bInterfaceNumber", (unsigned int)d[2]);
        tr_printf(t, "  %-20s %u\n", "bAlternateSetting", (unsigned int)d[3]);
        tr_printf(t, "  %-20s %u\n", "bNumEndpoints", (unsigned int)d[4]);
        tr_printf(t, "  %-20s 0x%02X (%s)\n", "bInterfaceClass",
                  (unsigned int)d[5], tr_class_name(d[5]));
        tr_printf(t, "  %-20s 0x%02X\n", "bInterfaceSubClass", (unsigned int)d[6]);
        tr_printf(t, "  %-20s 0x%02X\n", "bInterfaceProtocol", (unsigned int)d[7]);
        tr_printf(t, "  %-20s %u\n", "iInterface", (unsigned int)d[8]);
        return;
    case 0x05: {
        unsigned int mps;
        if (len < 7) {
            break;
        }
        mps = tr_w16(d + 4);
        tr_put(t, "Endpoint descriptor\n");
        tr_printf(t, "  %-20s 0x%02X (%s %u)\n", "bEndpointAddress",
                  (unsigned int)d[2], (d[2] & 0x80) ? "IN" : "OUT",
                  (unsigned int)(d[2] & 0x0F));
        tr_printf(t, "  %-20s 0x%02X (%s)\n", "bmAttributes", (unsigned int)d[3],
                  tr_ep_type(d[3]));
        tr_printf(t, "  %-20s 0x%04X (%u bytes", "wMaxPacketSize", mps,
                  mps & 0x7FFU);
        if ((mps >> 11) & 3U) {
            tr_printf(t, ", %u extra per microframe", (mps >> 11) & 3U);
        }
        tr_put(t, ")\n");
        tr_printf(t, "  %-20s %u\n", "bInterval", (unsigned int)d[6]);
        return;
    }
    case 0x0B:
        if (len < 8) {
            break;
        }
        tr_put(t, "Interface association descriptor\n");
        tr_printf(t, "  %-20s %u\n", "bFirstInterface", (unsigned int)d[2]);
        tr_printf(t, "  %-20s %u\n", "bInterfaceCount", (unsigned int)d[3]);
        tr_printf(t, "  %-20s 0x%02X (%s)\n", "bFunctionClass",
                  (unsigned int)d[4], tr_class_name(d[4]));
        tr_printf(t, "  %-20s 0x%02X\n", "bFunctionSubClass", (unsigned int)d[5]);
        tr_printf(t, "  %-20s 0x%02X\n", "bFunctionProtocol", (unsigned int)d[6]);
        tr_printf(t, "  %-20s %u\n", "iFunction", (unsigned int)d[7]);
        return;
    case 0x30:
        if (len < 6) {
            break;
        }
        tr_put(t, "SuperSpeed endpoint companion descriptor\n");
        tr_printf(t, "  %-20s %u\n", "bMaxBurst", (unsigned int)d[2]);
        tr_printf(t, "  %-20s 0x%02X\n", "bmAttributes", (unsigned int)d[3]);
        tr_printf(t, "  %-20s %u\n", "wBytesPerInterval", tr_w16(d + 4));
        return;
    case 0x31:
        if (len < 8) {
            break;
        }
        tr_put(t, "SuperSpeedPlus isochronous endpoint companion descriptor\n");
        tr_printf(t, "  %-20s %lu\n", "dwBytesPerInterval",
                  (unsigned long)tr_d32(d + 4));
        return;
    case 0x21:
        if (len < 6) {
            break;
        }
        tr_put(t, "HID descriptor\n");
        tr_bcd(t, "bcdHID", tr_w16(d + 2));
        tr_printf(t, "  %-20s %u\n", "bCountryCode", (unsigned int)d[4]);
        tr_printf(t, "  %-20s %u\n", "bNumDescriptors", (unsigned int)d[5]);
        if (len >= 9) {
            tr_printf(t, "  %-20s 0x%02X, %u bytes\n", "report descriptor",
                      (unsigned int)d[6], tr_w16(d + 7));
        }
        return;
    case 0x24:
        tr_printf(t, "Class-specific interface descriptor (subtype 0x%02X)\n",
                  len >= 3 ? (unsigned int)d[2] : 0U);
        return;
    case 0x25:
        tr_printf(t, "Class-specific endpoint descriptor (subtype 0x%02X)\n",
                  len >= 3 ? (unsigned int)d[2] : 0U);
        return;
    default:
        tr_printf(t, "Descriptor of type 0x%02X\n", type);
        return;
    }
    tr_printf(t, "Descriptor of type 0x%02X, too short (%u bytes)\n", type, len);
}

void tr_dec_config(TR_TEXT *t, const unsigned char *d, size_t n)
{
    size_t o = 0;
    size_t total;

    if (n < 9 || d[1] != 0x02 || d[0] < 9 || d[0] > n) {
        tr_put(t, "Configuration descriptor: short or malformed\n");
        tr_hex(t, d, n, 2);
        return;
    }
    total = tr_w16(d + 2);
    if (total > n) {
        tr_printf(t, "(wTotalLength %u, only %u bytes read)\n",
                  (unsigned int)total, (unsigned int)n);
        total = n;
    }
    while (o + 2 <= total) {
        unsigned int len = d[o];
        if (len < 2 || o + len > total) {
            tr_printf(t, "Malformed descriptor at offset %u (length %u)\n",
                      (unsigned int)o, len);
            break;
        }
        tr_one_desc(t, d + o, len);
        tr_hex(t, d + o, len, 2);
        o += len;
    }
}

static void tr_lse(TR_TEXT *t, TR_U32 attr)
{
    static const char *const unit[4] = { "bit/s", "kbit/s", "Mbit/s", "Gbit/s" };
    static const char *const st[4] = {
        "symmetric", "asymmetric receive", "symmetric (transmit)",
        "asymmetric transmit"
    };
    tr_printf(t, "    sublink speed id %lu: %lu %s, %s, %s\n",
              (unsigned long)(attr & 0xFUL), (unsigned long)(attr >> 16),
              unit[(attr >> 4) & 3UL], st[(attr >> 6) & 3UL],
              ((attr >> 14) & 3UL) == 1 ? "SuperSpeedPlus" : "SuperSpeed");
}

void tr_dec_bos(TR_TEXT *t, const unsigned char *d, size_t n)
{
    size_t o;
    size_t total;

    if (n < 5 || d[1] != 0x0F || d[0] < 5 || d[0] > n) {
        tr_put(t, "BOS descriptor: short or malformed\n");
        tr_hex(t, d, n, 2);
        return;
    }
    total = tr_w16(d + 2);
    tr_put(t, "BOS descriptor\n");
    tr_printf(t, "  %-20s %u\n", "wTotalLength", (unsigned int)total);
    tr_printf(t, "  %-20s %u\n", "bNumDeviceCaps", (unsigned int)d[4]);
    tr_hex(t, d, d[0], 2);
    if (total > n) {
        tr_printf(t, "(only %u bytes read)\n", (unsigned int)n);
        total = n;
    }
    o = d[0];
    while (o + 3 <= total) {
        unsigned int len = d[o];
        const unsigned char *c = d + o;
        if (len < 3 || o + len > total) {
            tr_printf(t, "Malformed capability at offset %u (length %u)\n",
                      (unsigned int)o, len);
            break;
        }
        if (c[1] != 0x10) {
            tr_printf(t, "Descriptor of type 0x%02X in the BOS\n",
                      (unsigned int)c[1]);
        } else if (c[2] == 0x02 && len >= 7) {
            TR_U32 a = tr_d32(c + 3);
            tr_put(t, "USB 2.0 extension capability\n");
            tr_printf(t, "  %-20s 0x%08lX (%s)\n", "bmAttributes",
                      (unsigned long)a, (a & 2UL) ? "LPM" : "no LPM");
        } else if (c[2] == 0x03 && len >= 10) {
            unsigned int s = tr_w16(c + 4);
            tr_put(t, "SuperSpeed capability\n");
            tr_printf(t, "  %-20s 0x%02X\n", "bmAttributes", (unsigned int)c[3]);
            tr_printf(t, "  %-20s 0x%04X (%s%s%s%s)\n", "wSpeedsSupported", s,
                      (s & 1U) ? "low " : "", (s & 2U) ? "full " : "",
                      (s & 4U) ? "high " : "", (s & 8U) ? "5 Gbit/s" : "");
            tr_printf(t, "  %-20s %u\n", "bFunctionalitySupport",
                      (unsigned int)c[6]);
            tr_printf(t, "  %-20s %u us\n", "bU1DevExitLat", (unsigned int)c[7]);
            tr_printf(t, "  %-20s %u us\n", "wU2DevExitLat", tr_w16(c + 8));
        } else if (c[2] == 0x04 && len >= 20) {
            int k;
            tr_put(t, "Container ID capability\n  ");
            for (k = 0; k < 16; k++) {
                tr_printf(t, "%02X", (unsigned int)c[4 + k]);
                if (k == 3 || k == 5 || k == 7 || k == 9) {
                    tr_ch(t, '-');
                }
            }
            tr_ch(t, '\n');
        } else if (c[2] == 0x0A && len >= 12) {
            TR_U32 a = tr_d32(c + 4);
            unsigned int f = tr_w16(c + 8);
            TR_U32 count = (a & 0x1FUL) + 1;
            TR_U32 i;
            tr_put(t, "SuperSpeedPlus capability\n");
            tr_printf(t, "  %-20s 0x%08lX (%lu sublink speed attributes)\n",
                      "bmAttributes", (unsigned long)a, (unsigned long)count);
            tr_printf(t, "  %-20s 0x%04X (minimum id %u, rx lanes %u, tx lanes %u)\n",
                      "wFunctionalitySupport", f, f & 0xFU, (f >> 8) & 0xFU,
                      (f >> 12) & 0xFU);
            for (i = 0; i < count; i++) {
                if (12 + 4 * i + 4 > len) {
                    tr_put(t, "    (attributes cut short)\n");
                    break;
                }
                tr_lse(t, tr_d32(c + 12 + 4 * i));
            }
        } else {
            tr_printf(t, "Device capability 0x%02X\n", (unsigned int)c[2]);
        }
        tr_hex(t, c, len, 2);
        o += len;
    }
}

int tr_bos_ssp_text(const unsigned char *d, size_t n, char *out, size_t cap)
{
    size_t o;
    size_t total;
    TR_TEXT t;
    int found = 0;

    tr_text_init(&t, out, cap);
    if (n < 5 || d[1] != 0x0F || d[0] < 5 || d[0] > n) {
        return 0;
    }
    total = tr_w16(d + 2);
    if (total > n) {
        total = n;
    }
    for (o = d[0]; o + 3 <= total;) {
        unsigned int len = d[o];
        const unsigned char *c = d + o;
        if (len < 3 || o + len > total) {
            break;
        }
        if (c[1] == 0x10 && c[2] == 0x0A && len >= 12) {
            TR_U32 count = (tr_d32(c + 4) & 0x1FUL) + 1;
            TR_U32 i;
            for (i = 0; i < count && 12 + 4 * i + 4 <= len; i++) {
                TR_U32 a = tr_d32(c + 12 + 4 * i);
                TR_U32 st = (a >> 6) & 3UL;
                TR_U32 lse = (a >> 4) & 3UL;
                if (st != 0 && st != 1) {
                    continue;   /* the transmit half repeats the receive one */
                }
                tr_printf(&t, "%s%lu %s", found ? ", " : "",
                          (unsigned long)(a >> 16),
                          lse == 3 ? "Gbit/s" : lse == 2 ? "Mbit/s"
                                   : lse == 1 ? "kbit/s" : "bit/s");
                found = 1;
            }
            if (found) {
                tr_put(&t, " per lane");
            }
        }
        o += len;
    }
    return found;
}

int tr_config_power(const unsigned char *d, size_t n, int speed,
                    unsigned int *ma, int *selfPowered)
{
    if (n < 9 || d[1] != 0x02) {
        return 0;
    }
    *ma = (unsigned int)d[8] * (speed >= TR_SPEED_SUPER ? 8U : 2U);
    *selfPowered = (d[7] & 0x40) != 0;
    return 1;
}

void tr_dec_hub(TR_TEXT *t, const unsigned char *d, size_t n)
{
    unsigned int ch;

    if (n < 7 || (d[1] != 0x29 && d[1] != 0x2A) || d[0] < 7 || d[0] > n) {
        tr_put(t, "Hub descriptor: short or not reported\n");
        tr_hex(t, d, n, 2);
        return;
    }
    ch = tr_w16(d + 3);
    tr_printf(t, "%s hub descriptor\n", d[1] == 0x2A ? "SuperSpeed" : "USB 2.0");
    tr_printf(t, "  %-20s %u\n", "bNbrPorts", (unsigned int)d[2]);
    tr_printf(t, "  %-20s 0x%04X\n", "wHubCharacteristics", ch);
    tr_printf(t, "    power switching: %s\n",
              (ch & 2U) ? "none" : (ch & 1U) ? "per port" : "ganged");
    tr_printf(t, "    compound device: %s\n", (ch & 4U) ? "yes" : "no");
    tr_printf(t, "    over-current protection: %s\n",
              (ch & 0x10U) ? "none" : (ch & 8U) ? "per port" : "global");
    if (d[1] == 0x29) {
        tr_printf(t, "    port indicators: %s\n", (ch & 0x80U) ? "yes" : "no");
    }
    tr_printf(t, "  %-20s %u ms\n", "bPwrOn2PwrGood", (unsigned int)d[5] * 2U);
    tr_printf(t, "  %-20s %u mA\n", "bHubContrCurrent", (unsigned int)d[6]);
    tr_hex(t, d, (size_t)d[0], 2);
}

int tr_dec_string(const unsigned char *d, size_t n, char *out, size_t cap)
{
    size_t len;

    if (cap > 0) {
        out[0] = '\0';
    }
    if (n < 2 || d[1] != 0x03 || d[0] < 2) {
        return 0;
    }
    len = d[0] < n ? d[0] : n;
    tr_wide_name(d, len, 2, out, cap);
    return 1;
}

/* ======================================================================== */
/* xhcicm.sys's query                                                        */
/* ======================================================================== */

int tr_query_parse(const unsigned char *b, size_t n, TR_QUERY *q)
{
    TR_U32 shape;
    TR_U32 recOff;
    TR_U32 routers;
    TR_U32 idOff;
    TR_U32 idLen;
    TR_U32 i;

    memset(q, 0, sizeof(*q));
    if (n < CM_Q_HDR_BYTES || tr_d32(b + CM_Q_O_MAGIC) != CM_Q_MAGIC ||
        tr_d32(b + CM_Q_O_VERSION) != CM_Q_VERSION) {
        return 0;
    }
    q->Base = b;
    q->Bytes = tr_d32(b + CM_Q_O_BYTES);
    q->Pass = tr_d32(b + CM_Q_O_PASS);
    q->AgeMs = tr_d32(b + CM_Q_O_PASS_AGE_MS);
    q->Live = tr_d32(b + CM_Q_O_LIVE);
    q->Flags = tr_d32(b + CM_Q_O_FLAGS);
    q->EpochLo = tr_d32(b + CM_Q_O_EPOCH_LO);
    q->EpochHi = tr_d32(b + CM_Q_O_EPOCH_HI);
    q->NhiBdf = tr_d32(b + CM_Q_O_NHI_BDF);
    q->ContentGen = tr_d32(b + CM_Q_O_CONTENT_GEN);
    if (q->Bytes < CM_Q_HDR_BYTES + CM_ST_HDR_BYTES ||
        q->Bytes > CM_Q_MAX_BYTES || (q->EpochLo == 0 && q->EpochHi == 0) ||
        q->ContentGen == 0) {
        return 0;
    }
    if (q->Bytes > n) {
        return -1;
    }
    recOff = tr_d32(b + CM_Q_O_REC_OFFSET);
    q->ExtOffset = tr_d32(b + CM_Q_O_EXT_OFFSET);
    shape = tr_d32(b + CM_Q_O_EXT_SHAPE);
    q->ExtCount = shape & 0xFFFFUL;
    q->ExtBytes = shape >> 16;
    idOff = tr_d32(b + CM_Q_O_ID_OFFSET);
    idLen = tr_d32(b + CM_Q_O_ID_BYTES);
    if (recOff < CM_Q_HDR_BYTES || recOff > q->Bytes ||
        q->Bytes - recOff < CM_ST_HDR_BYTES) {
        return 0;
    }
    q->Rec = b + recOff;
    q->RecBytes = tr_d32(q->Rec + CM_ST_O_BYTES);
    if (q->RecBytes > q->Bytes - recOff ||
        !inst_st_check(q->Rec, (size_t)q->RecBytes)) {
        return 0;
    }
    routers = tr_d32(q->Rec + CM_ST_O_ROUTER_COUNT);
    if (q->ExtCount != routers || q->ExtBytes < CM_Q_EXT_BYTES ||
        q->ExtOffset < recOff + q->RecBytes || q->ExtOffset > q->Bytes ||
        (q->Bytes - q->ExtOffset) / q->ExtBytes < q->ExtCount) {
        return 0;
    }
    if (idLen == 0 || idLen > TR_ID_CHARS - 8 ||
        idOff < q->ExtOffset + q->ExtCount * q->ExtBytes ||
        idOff > q->Bytes || q->Bytes - idOff < idLen) {
        return 0;
    }
    for (i = 0; i < idLen; i++) {
        unsigned char c = b[idOff + i];
        if (c < 0x20 || c >= 0x7F) {
            return 0;
        }
        q->Id[i] = (char)c;
    }
    q->Id[idLen] = '\0';
    return 1;
}

int tr_query_current(const TR_QUERY *q)
{
    return q->AgeMs != CM_Q_UNKNOWN && q->AgeMs < CM_Q_CURRENT_MS &&
           (q->Flags & CM_Q_F_INERT) == 0 && q->Live == CM_LIVE_ACTIVE;
}

int tr_query_eject_ready(const TR_QUERY *q, const char **why)
{
    const char *w = NULL;

    if (q->Flags & CM_Q_F_STOPPING) {
        w = "The Thunderbolt/USB4 controller is stopping.";
    } else if (q->Flags & CM_Q_F_INERT) {
        w = "The Thunderbolt driver is idle on this controller.";
    } else if (tr_d32(q->Rec + CM_ST_O_STAGE) != CM_ST_STAGE_ACTIVE) {
        w = "The Thunderbolt driver's connection manager is not active.";
    } else if (q->Live != CM_LIVE_ACTIVE) {
        w = "The Thunderbolt/USB4 controller is not answering.";
    } else if (q->Pass == 0) {
        w = "The Thunderbolt driver has not published its device list yet.";
    }
    if (why != NULL) {
        *why = w;
    }
    return w == NULL;
}

int tr_query_new_pass(const TR_QUERY *a, const TR_QUERY *b)
{
    TR_U32 moved;

    if (a->EpochLo != b->EpochLo || a->EpochHi != b->EpochHi || b->Pass == 0) {
        return 0;
    }
    moved = (b->Pass - a->Pass) & 0xFFFFFFFFUL;
    if (b->Pass < a->Pass && a->Pass != 0) {
        moved--;    /* the counter skips 0 when it wraps */
    }
    return moved >= CM_Q_NEW_PASSES && moved < 0x80000000UL;
}

int tr_query_same_content(const TR_QUERY *a, const TR_QUERY *b)
{
    return a->EpochLo == b->EpochLo && a->EpochHi == b->EpochHi &&
           a->ContentGen == b->ContentGen;
}

int tr_query_ext(const TR_QUERY *q, TR_U32 index, TR_EXT *x)
{
    const unsigned char *e;

    memset(x, 0, sizeof(*x));
    if (index >= q->ExtCount) {
        return 0;
    }
    e = q->Base + q->ExtOffset + index * q->ExtBytes;
    x->Rate = tr_d32(e + CM_Q_X_RATE);
    x->Width = tr_d32(e + CM_Q_X_WIDTH);
    x->LinkState = tr_d32(e + CM_Q_X_LINK_STATE);
    x->GenRaw = tr_d32(e + CM_Q_X_GEN_RAW);
    x->Ids = tr_d32(e + CM_Q_X_IDS);
    x->Flags = tr_d32(e + CM_Q_X_FLAGS);
    return 1;
}

/* ======================================================================== */
/* The status record's texts                                                 */
/* ======================================================================== */

const char *tr_tun_text(TR_U32 v)
{
    switch (v & 0xFUL) {
    case CM_ST_TUN_NONE:
        return "none";
    case CM_ST_TUN_UP:
        return "up";
    case CM_ST_TUN_PRESERVED:
        return "up (found at start)";
    case CM_ST_TUN_FAILED:
        return "failed";
    case CM_ST_TUN_EJECTED:
        return "closed after an eject";
    default:
        return "not known";
    }
}

const char *tr_appr_text(TR_U32 v)
{
    switch (v) {
    case CM_ST_APPR_APPROVED:
        return "approved";
    case CM_ST_APPR_BOOT:
        return "approved before Windows started";
    case CM_ST_APPR_REJECTED:
        return "rejected by the controller";
    case CM_ST_APPR_FAILED:
        return "approval refused by the controller";
    case CM_ST_APPR_UNANSWERED:
        return "approval not answered";
    case CM_ST_APPR_PENDING:
        return "not approved";
    case CM_ST_APPR_HELD:
        return "connection held or failed";
    default:
        return "approval not known";
    }
}

const char *tr_cm_text(TR_U32 cm)
{
    switch (cm) {
    case CM_ST_CM_ICM:
        return "ICM";
    case CM_ST_CM_USB4:
        return "USB4 software CM";
    default:
        return "no connection manager";
    }
}

const char *tr_stage_text(TR_U32 stage)
{
    switch (stage) {
    case CM_ST_STAGE_STOPPED:
        return "stopped";
    case CM_ST_STAGE_INERT:
        return "idle (inert)";
    case CM_ST_STAGE_ACTIVE:
        return "active";
    case CM_ST_STAGE_FAILED:
        return "failed";
    default:
        return "stage not known";
    }
}

const char *tr_live_text(TR_U32 live)
{
    switch (live) {
    case CM_LIVE_OFF:
        return "not monitored";
    case CM_LIVE_ACTIVE:
        return "answering";
    case CM_LIVE_SUSPECT:
        return "not answering (suspect)";
    case CM_LIVE_ABSENT:
        return "gone";
    case CM_LIVE_CONFIG_LOST:
        return "configuration lost";
    case CM_LIVE_RECOVERING:
        return "recovering";
    case CM_LIVE_RECOVERY_FAILED:
        return "recovery failed";
    case CM_LIVE_CONTAINED:
        return "contained";
    case CM_LIVE_REVOKED:
        return "access revoked";
    default:
        return "state not known";
    }
}

void tr_gen_text(TR_U32 gen, char *out, size_t cap)
{
    if ((gen & 0xFFUL) == CM_ST_GEN_TBT3) {
        tr_copy(out, "Thunderbolt 3", cap);
    } else if ((gen & 0xFFUL) == CM_ST_GEN_USB4) {
        if (((gen >> 8) & 0xFFUL) != 0) {
            tr_sprintf(out, cap, "USB4 v%lu.%lu",
                       (unsigned long)((gen >> 12) & 0xFUL),
                       (unsigned long)((gen >> 8) & 0xFUL));
        } else {
            tr_copy(out, "USB4", cap);
        }
    } else {
        tr_copy(out, "generation not known", cap);
    }
}

void tr_link_text(TR_U32 kind, TR_U32 lanes, const TR_EXT *x, char *out,
                  size_t cap)
{
    TR_U32 width = 0;

    if (x != NULL && x->Width != 0) {
        width = x->Width;
    } else if (lanes == CM_ST_LANES_BONDED) {
        width = 2;
    } else if (lanes == CM_ST_LANES_SINGLE) {
        width = 1;
    }
    if (x != NULL && x->Rate != 0 && kind == CM_ST_KIND_ICM) {
        /* The ICM's device-connected event carries the rate and width; the
         * lane adapter is read only where the flag says so. */
        const char *src = (x->Flags & CM_Q_XF_RATE_READ)
                              ? "read from the lane adapter"
                              : "reported by the firmware";
        if (width != 0) {
            tr_sprintf(out, cap, "%lu Gbit/s per lane, x%lu (%s)",
                       (unsigned long)x->Rate, (unsigned long)width, src);
        } else {
            tr_sprintf(out, cap, "%lu Gbit/s per lane, lanes not known (%s)",
                       (unsigned long)x->Rate, src);
        }
        return;
    }
    if (x != NULL && x->Rate != 0) {
        const char *gen = x->Rate == 10 ? "Gen 2 " : x->Rate == 20 ? "Gen 3 "
                          : x->Rate == 40 ? "Gen 4 " : "";
        const char *src = (x->Flags & CM_Q_XF_RATE_READ)
                              ? " (read from the lane adapter)" : "";
        if (width != 0) {
            tr_sprintf(out, cap, "USB4 %sx%lu (%lu Gbit/s)%s", gen,
                       (unsigned long)width,
                       (unsigned long)(x->Rate * width), src);
        } else {
            tr_sprintf(out, cap, "USB4 %s(%lu Gbit/s per lane), lanes not known%s",
                       gen, (unsigned long)x->Rate, src);
        }
        return;
    }
    if (kind == CM_ST_KIND_ICM) {
        tr_sprintf(out, cap, "%s; speed not reported by the firmware",
                   width == 2 ? "two lanes bonded" : width == 1 ? "one lane"
                                                             : "lanes not known");
        return;
    }
    tr_sprintf(out, cap, "%s; speed not known",
               width == 2 ? "two lanes" : width == 1 ? "one lane"
                                                     : "lanes not known");
}

void tr_router_name(const unsigned char *entry, char *out, size_t cap)
{
    char vendor[CM_ST_NAME_BYTES];
    char model[CM_ST_NAME_BYTES];
    size_t i;

    for (i = 0; i < CM_ST_NAME_BYTES - 1 && entry[CM_ST_E_VENDOR + i] != 0; i++) {
        unsigned char c = entry[CM_ST_E_VENDOR + i];
        vendor[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    vendor[i] = '\0';
    for (i = 0; i < CM_ST_NAME_BYTES - 1 && entry[CM_ST_E_MODEL + i] != 0; i++) {
        unsigned char c = entry[CM_ST_E_MODEL + i];
        model[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    model[i] = '\0';
    if (vendor[0] == '\0' && model[0] == '\0') {
        tr_copy(out, "(no name given)", cap);
    } else {
        tr_sprintf(out, cap, "%s%s%s", vendor, vendor[0] && model[0] ? " " : "",
                   model);
    }
}

/* ======================================================================== */
/* The tray's events                                                         */
/* ======================================================================== */

void tr_snap_init(TR_SNAP *s)
{
    memset(s, 0, sizeof(*s));
}

int tr_snap_add(TR_SNAP *s, TR_U32 nhiKey, const TR_QUERY *q)
{
    TR_U32 n = tr_d32(q->Rec + CM_ST_O_ROUTER_COUNT);
    TR_U32 r;

    if (s->NhiCount >= TR_SNAP_NHI) {
        s->Truncated = 1;
        return 0;
    }
    s->NhiKey[s->NhiCount] = nhiKey;
    s->Live[s->NhiCount] = q->Live;
    s->EpochLo[s->NhiCount] = q->EpochLo;
    s->EpochHi[s->NhiCount] = q->EpochHi;
    s->NhiCount++;
    for (r = 0; r < n; r++) {
        const unsigned char *e = q->Rec + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
        TR_SROUTER *x;
        if (s->Count >= TR_SNAP_ROUTERS) {
            s->Truncated = 1;
            return 0;
        }
        x = &s->R[s->Count++];
        memset(x, 0, sizeof(*x));
        x->Nhi = nhiKey;
        x->EpochLo = q->EpochLo;
        x->EpochHi = q->EpochHi;
        x->Kind = tr_d32(e + CM_ST_E_KIND);
        x->RouteLo = tr_d32(e + CM_ST_E_ROUTE_LO);
        x->RouteHi = tr_d32(e + CM_ST_E_ROUTE_HI);
        x->Place = tr_d32(e + CM_ST_E_PLACE);
        x->Ids = tr_d32(e + CM_ST_E_IDS);
        x->ShortId = tr_d32(e + CM_ST_E_SHORT_ID);
        x->Approval = tr_d32(e + CM_ST_E_APPROVAL);
        x->Tunnels = tr_d32(e + CM_ST_E_TUNNELS);
        x->Bridge = tr_d32(e + CM_ST_E_BRIDGE);
        x->Gen = tr_d32(e + CM_ST_E_GEN);
        tr_router_name(e, x->Name, sizeof(x->Name));
    }
    return 1;
}

int tr_same_router(const TR_SROUTER *a, const TR_SROUTER *b)
{
    if (a->Nhi != b->Nhi || a->EpochLo != b->EpochLo ||
        a->EpochHi != b->EpochHi || a->Kind != b->Kind) {
        return 0;
    }
    if (a->Kind == CM_ST_KIND_ICM) {
        return a->Place == b->Place && a->Ids == b->Ids &&
               a->ShortId == b->ShortId;
    }
    return a->RouteLo == b->RouteLo && a->RouteHi == b->RouteHi;
}

/* The approval events read the ICM's states alone (record 25 section
 * 7.2). */
static int tr_waiting(const TR_SROUTER *r)
{
    return r->Kind == CM_ST_KIND_ICM &&
           (r->Approval == CM_ST_APPR_PENDING ||
            r->Approval == CM_ST_APPR_UNANSWERED);
}

static int tr_failed(const TR_SROUTER *r)
{
    return r->Kind == CM_ST_KIND_ICM &&
           (r->Approval == CM_ST_APPR_FAILED ||
            r->Approval == CM_ST_APPR_REJECTED);
}

/* How many entries of s are the same router as r, and the first. */
static int tr_count(const TR_SNAP *s, const TR_SROUTER *r,
                    const TR_SROUTER **first)
{
    int i;
    int n = 0;

    *first = NULL;
    for (i = 0; i < s->Count; i++) {
        if (tr_same_router(&s->R[i], r)) {
            if (n == 0) {
                *first = &s->R[i];
            }
            n++;
        }
    }
    return n;
}

/* 1 when s holds the NHI at this epoch (absent, or another start: 0). */
static int tr_nhi_at(const TR_SNAP *s, TR_U32 key, TR_U32 lo, TR_U32 hi)
{
    int i;

    for (i = 0; i < s->NhiCount; i++) {
        if (s->NhiKey[i] == key) {
            return s->EpochLo[i] == lo && s->EpochHi[i] == hi;
        }
    }
    return 0;
}

static int tr_ev(TR_EVENT *ev, int cap, int count, int kind,
                 const TR_SROUTER *r, int named)
{
    if (count >= cap) {
        return count;
    }
    memset(&ev[count], 0, sizeof(ev[count]));
    ev[count].Kind = kind;
    if (r != NULL) {
        ev[count].Gen = r->Gen;
        tr_copy(ev[count].Name, named ? r->Name : "A Thunderbolt device",
                sizeof(ev[count].Name));
    }
    return count + 1;
}

/* Whether entry i is the first of its key in its snapshot: a group is
 * handled once. */
static int tr_first_of(const TR_SNAP *s, int i)
{
    int k;

    for (k = 0; k < i; k++) {
        if (tr_same_router(&s->R[k], &s->R[i])) {
            return 0;
        }
    }
    return 1;
}

int tr_diff(const TR_SNAP *old, const TR_SNAP *now, TR_EVENT *ev, int cap)
{
    int count = 0;
    int i;
    int k;

    if (!old->Valid || !now->Valid) {
        return 0;
    }
    for (i = 0; i < now->NhiCount; i++) {
        for (k = 0; k < old->NhiCount; k++) {
            if (old->NhiKey[k] == now->NhiKey[i] &&
                old->Live[k] == CM_LIVE_ACTIVE &&
                now->Live[i] != CM_LIVE_ACTIVE) {
                count = tr_ev(ev, cap, count, TR_EV_STOPPED, NULL, 0);
            }
        }
    }
    for (i = 0; i < now->Count; i++) {
        const TR_SROUTER *r = &now->R[i];
        const TR_SROUTER *o;
        const TR_SROUTER *f;
        int nNow;
        int nOld;
        if (!tr_first_of(now, i) ||
            !tr_nhi_at(old, r->Nhi, r->EpochLo, r->EpochHi)) {
            continue;
        }
        nNow = tr_count(now, r, &f);
        nOld = tr_count(old, r, &o);
        if (nNow > 1 || nOld > 1) {
            if (nNow > nOld) {
                count = tr_ev(ev, cap, count, TR_EV_CONNECTED, r, 0);
            }
            continue;
        }
        if (nOld == 0) {
            count = tr_ev(ev, cap, count,
                          tr_waiting(r) ? TR_EV_WAITING
                          : tr_failed(r) ? TR_EV_FAILED
                                                   : TR_EV_CONNECTED, r, 1);
        } else if (tr_waiting(r) && !tr_waiting(o)) {
            count = tr_ev(ev, cap, count, TR_EV_WAITING, r, 1);
        } else if (tr_failed(r) && !tr_failed(o)) {
            count = tr_ev(ev, cap, count, TR_EV_FAILED, r, 1);
        }
    }
    for (i = 0; i < old->Count; i++) {
        const TR_SROUTER *o = &old->R[i];
        const TR_SROUTER *f;
        int nNow;
        int nOld;
        if (!tr_first_of(old, i) ||
            !tr_nhi_at(now, o->Nhi, o->EpochLo, o->EpochHi)) {
            continue;
        }
        nNow = tr_count(now, o, &f);
        nOld = tr_count(old, o, &f);
        if (nNow >= nOld) {
            continue;
        }
        count = tr_ev(ev, cap, count, TR_EV_LEFT, o, nOld == 1);
    }
    return count;
}

void tr_event_text(const TR_EVENT *e, char *out, size_t cap)
{
    char gen[32];

    tr_gen_text(e->Gen, gen, sizeof(gen));
    switch (e->Kind) {
    case TR_EV_CONNECTED:
        if ((e->Gen & 0xFFUL) == CM_ST_GEN_UNKNOWN) {
            tr_sprintf(out, cap, "%s connected.", e->Name);
        } else {
            tr_sprintf(out, cap, "%s connected (%s).", e->Name, gen);
        }
        break;
    case TR_EV_WAITING:
        tr_sprintf(out, cap, "%s is waiting to be approved.", e->Name);
        break;
    case TR_EV_FAILED:
        tr_sprintf(out, cap, "%s could not be approved.", e->Name);
        break;
    case TR_EV_STOPPED:
        tr_copy(out, "The Thunderbolt/USB4 controller stopped answering.", cap);
        break;
    case TR_EV_LEFT:
        tr_sprintf(out, cap, "%s was disconnected.", e->Name);
        break;
    default:
        tr_copy(out, "", cap);
        break;
    }
}

const char *tr_event_title(int kind)
{
    switch (kind) {
    case TR_EV_STOPPED:
    case TR_EV_FAILED:
        return "Thunderbolt/USB4 warning";
    default:
        return "Thunderbolt/USB4";
    }
}

/* ======================================================================== */
/* Keys, the command line, the report                                        */
/* ======================================================================== */

void tr_key(char *out, size_t cap, const char *prefix, const char *a,
            const char *b)
{
    tr_sprintf(out, cap, "%s:%s:%s", prefix, a != NULL ? a : "",
               b != NULL ? b : "");
}

void tr_port_path(char *out, size_t cap, const char *parent, TR_U32 port)
{
    if (parent == NULL || parent[0] == '\0') {
        tr_sprintf(out, cap, "%lu", (unsigned long)port);
    } else {
        tr_sprintf(out, cap, "%s.%lu", parent, (unsigned long)port);
    }
}

static int tr_switch(const char *a, const char *name)
{
    return (a[0] == '/' || a[0] == '-') && tr_eq(a + 1, name);
}

int tr_parse_args(int argc, char **argv, TR_OPTS *o, char *err, size_t cap)
{
    int i;
    int alone = 0;

    memset(o, 0, sizeof(*o));
    if (cap > 0) {
        err[0] = '\0';
    }
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (tr_switch(a, "tray")) {
            o->Tray = 1;
        } else if (tr_switch(a, "selftest")) {
            o->SelfTest = 1;
            alone++;
        } else if (tr_switch(a, "report")) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                strlen(argv[i + 1]) >= TR_PATH_CHARS) {
                tr_copy(err, "/report needs the name of a file.", cap);
                return 1;
            }
            o->Report = 1;
            tr_copy(o->ReportPath, argv[++i], sizeof(o->ReportPath));
            alone++;
        } else if (tr_switch(a, "eject")) {
            const char *id;
            if (i + 1 >= argc) {
                tr_copy(err, "/eject needs id=<instance id>.", cap);
                return 1;
            }
            id = argv[++i];
            if (!(tr_up((unsigned char)id[0]) == 'I' &&
                  tr_up((unsigned char)id[1]) == 'D' && id[2] == '=') ||
                id[3] == '\0' || strlen(id + 3) >= TR_ID_CHARS) {
                tr_copy(err, "/eject needs id=<instance id>.", cap);
                return 1;
            }
            o->Eject = 1;
            tr_copy(o->EjectId, id + 3, sizeof(o->EjectId));
            alone++;
        } else {
            tr_sprintf(err, cap, "Unknown switch %s. Use: XHCITR [/tray] "
                       "[/eject id=<instance id>] [/report <file>]", a);
            return 1;
        }
    }
    if (alone > 1 || (alone == 1 && o->Tray)) {
        tr_copy(err, "/eject, /report and /selftest are each used alone.", cap);
        return 1;
    }
    return 0;
}

void tr_report_node(TR_TEXT *t, int depth, const char *label,
                    const char *details)
{
    int k;
    int bol = 1;

    for (k = 0; k < depth; k++) {
        tr_put(t, "  ");
    }
    tr_put(t, label);
    tr_ch(t, '\n');
    if (details == NULL) {
        return;
    }
    for (; *details != '\0'; details++) {
        if (*details == '\r') {
            continue;
        }
        if (bol) {
            for (k = 0; k < depth + 2; k++) {
                tr_put(t, "  ");
            }
            tr_put(t, "| ");
            bol = 0;
        }
        tr_ch(t, *details);
        if (*details == '\n') {
            bol = 1;
        }
    }
    if (!bol) {
        tr_ch(t, '\n');
    }
}
