/*
 * tbs_core.c - TBSPEED's pure half (tbs_core.h). C89, no Windows call.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tbs_core.h"

static const unsigned char reqMagic[4] = { 'T', 'B', 'S', '1' };
static const unsigned char resMagic[4] = { 'T', 'B', 'S', 'R' };

static void put32(unsigned char *b, unsigned long v)
{
    b[0] = (unsigned char)((v >> 24) & 0xFF);
    b[1] = (unsigned char)((v >> 16) & 0xFF);
    b[2] = (unsigned char)((v >> 8) & 0xFF);
    b[3] = (unsigned char)(v & 0xFF);
}

static unsigned long get32(const unsigned char *b)
{
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16) |
           ((unsigned long)b[2] << 8) | (unsigned long)b[3];
}

static void seterr(char *err, size_t len, const char *a, const char *b)
{
    if (err == NULL || len == 0)
        return;
    if (b == NULL)
        b = "";
    if (strlen(a) + strlen(b) + 1 > len) {
        err[0] = '\0';
        strncat(err, a, len - 1);
        return;
    }
    strcpy(err, a);
    strcat(err, b);
}

/* a decimal number within [lo, hi]; 0 = good */
static int number(const char *s, unsigned long lo, unsigned long hi,
                  unsigned long *v)
{
    unsigned long n = 0;
    if (s == NULL || *s == '\0')
        return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        if (n > (hi - (unsigned long)(*s - '0')) / 10)
            return -1;
        n = n * 10 + (unsigned long)(*s - '0');
    }
    if (n < lo)
        return -1;
    *v = n;
    return 0;
}

int tbs_parse(int argc, char **argv, TBS_OPTS *o, char *err, size_t errLen)
{
    int i;
    int haveT = 0, haveN = 0, clientOnly = 0;
    unsigned long v;

    memset(o, 0, sizeof(*o));
    o->port = TBS_PORT;
    o->seconds = TBS_DEF_SECONDS;
    o->lenKb = TBS_DEF_LEN_KB;
    o->interval = 1;
    if (err != NULL && errLen > 0)
        err[0] = '\0';

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        char c;
        if ((a[0] != '-' && a[0] != '/') || a[1] == '\0' || a[2] != '\0') {
            seterr(err, errLen, "Unknown option: ", a);
            return -1;
        }
        c = a[1];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        switch (c) {
        case 'h':
        case '?':
            return 1;
        case 's':
            if (o->mode != TBS_MODE_NONE) {
                seterr(err, errLen, "Give -s or -c, not both", NULL);
                return -1;
            }
            o->mode = TBS_MODE_SERVER;
            break;
        case '1':
            o->once = 1;
            break;
        case 'r':
            o->reverse = 1;
            clientOnly = 1;
            break;
        case 'c':
            if (o->mode != TBS_MODE_NONE) {
                seterr(err, errLen, "Give -s or -c, not both", NULL);
                return -1;
            }
            if (val == NULL || val[0] == '\0' || val[0] == '-') {
                seterr(err, errLen, "-c needs the other computer's address", NULL);
                return -1;
            }
            if (strlen(val) > TBS_HOST_MAX) {
                seterr(err, errLen, "The address is too long", NULL);
                return -1;
            }
            strcpy(o->host, val);
            o->mode = TBS_MODE_CLIENT;
            i++;
            break;
        case 'o':
            if (val == NULL || val[0] == '\0') {
                seterr(err, errLen, "-o needs a file name", NULL);
                return -1;
            }
            if (strlen(val) > TBS_PATH_MAX) {
                seterr(err, errLen, "The file name is too long", NULL);
                return -1;
            }
            strcpy(o->out, val);
            i++;
            break;
        case 'p':
        case 't':
        case 'n':
        case 'l':
        case 'w':
        case 'i':
            {
                unsigned long lo = 1, hi;
                const char *what;
                switch (c) {
                case 'p': hi = 65535; what = "-p wants a port, 1 to 65535"; break;
                case 't': hi = TBS_MAX_SECONDS; what = "-t wants seconds, 1 to 3600"; break;
                case 'n': hi = TBS_MAX_MB; what = "-n wants MB, 1 to 1000000"; break;
                case 'l': hi = TBS_MAX_LEN_KB; what = "-l wants KB, 1 to 1024"; break;
                case 'w': hi = TBS_MAX_WIN_KB; what = "-w wants KB, 1 to 8192"; break;
                default: lo = 0; hi = TBS_MAX_INTERVAL; what = "-i wants seconds, 0 to 60"; break;
                }
                if (number(val, lo, hi, &v) != 0) {
                    seterr(err, errLen, what, NULL);
                    return -1;
                }
                i++;
                switch (c) {
                case 'p': o->port = (unsigned)v; break;
                case 't': o->seconds = v; haveT = 1; clientOnly = 1; break;
                case 'n': o->megabytes = v; haveN = 1; clientOnly = 1; break;
                case 'l': o->lenKb = (unsigned)v; break;
                case 'w': o->winKb = (unsigned)v; break;
                default: o->interval = (unsigned)v; break;
                }
            }
            break;
        default:
            seterr(err, errLen, "Unknown option: ", a);
            return -1;
        }
    }

    if (o->mode == TBS_MODE_NONE) {
        seterr(err, errLen, "Give -s (wait for the other computer) or -c <address>", NULL);
        return -1;
    }
    if (haveT && haveN) {
        seterr(err, errLen, "Give -t or -n, not both", NULL);
        return -1;
    }
    if (o->mode == TBS_MODE_SERVER && clientOnly) {
        seterr(err, errLen, "-t, -n and -r are given to the -c side", NULL);
        return -1;
    }
    if (o->mode == TBS_MODE_CLIENT && o->once) {
        seterr(err, errLen, "-1 is given to the -s side", NULL);
        return -1;
    }
    if (haveN)
        o->seconds = 0;
    return 0;
}

void tbs_req_put(unsigned char *b, const TBS_REQ *r)
{
    memcpy(b, reqMagic, 4);
    b[4] = 1;
    b[5] = (unsigned char)(r->dir == TBS_DIR_SERVER_SENDS ? 1 : 0);
    b[6] = 0;
    b[7] = 0;
    put32(b + 8, r->seconds);
    put32(b + 12, r->megabytes);
}

int tbs_req_get(const unsigned char *b, TBS_REQ *r)
{
    if (memcmp(b, reqMagic, 4) != 0 || b[4] != 1 || b[5] > 1)
        return -1;
    r->dir = b[5];
    r->seconds = get32(b + 8);
    r->megabytes = get32(b + 12);
    if ((r->seconds == 0) == (r->megabytes == 0))
        return -1;
    if (r->seconds > TBS_MAX_SECONDS || r->megabytes > (unsigned long)TBS_MAX_MB)
        return -1;
    return 0;
}

void tbs_res_put(unsigned char *b, const TBS_RES *r)
{
    double hi = floor(r->bytes / 4294967296.0);
    double lo = r->bytes - hi * 4294967296.0;
    memcpy(b, resMagic, 4);
    put32(b + 4, (unsigned long)hi);
    put32(b + 8, (unsigned long)lo);
    put32(b + 12, r->ms);
}

int tbs_res_get(const unsigned char *b, TBS_RES *r)
{
    if (memcmp(b, resMagic, 4) != 0)
        return -1;
    r->bytes = (double)get32(b + 4) * 4294967296.0 + (double)get32(b + 8);
    r->ms = get32(b + 12);
    return 0;
}

double tbs_mbits(double bytes, unsigned long ms)
{
    if (ms == 0)
        return 0.0;
    return bytes * 8.0 / ((double)ms * 1000.0);
}

void tbs_figure(char *buf, size_t len, double bytes, unsigned long ms)
{
    char t[96];
    sprintf(t, "%9.2f MB in %6.2f s = %8.2f Mbit/s",
            bytes / 1000000.0, (double)ms / 1000.0, tbs_mbits(bytes, ms));
    if (len == 0)
        return;
    buf[0] = '\0';
    strncat(buf, t, len - 1);
}
