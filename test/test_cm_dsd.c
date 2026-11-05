/*
 * test_cm_dsd.c - the ACPI-named tunnel-port source's _DSD reader and name
 * normalisation (src/cm/cm_dsd.c; design record 22 section 7.6.1, task
 * 46.23). The answers are made up here in ACPI_EVAL_OUTPUT_BUFFER's
 * layout, in the shape a Strix Halo machine's static _DSD gives (its
 * firmware not copied: the names and values are this file's).
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_dsd.h"
#include "test_harness.h"

static const UCHAR uuidProps[16] = {
    0x14, 0xD8, 0xFF, 0xDA, 0xBA, 0x6E, 0x8C, 0x4D,
    0x8A, 0x91, 0xBC, 0x9B, 0xBF, 0x4A, 0xA3, 0x01
};
static const UCHAR uuidOther[16] = {
    0x6C, 0x50, 0x1A, 0x22, 0x4C, 0x5A, 0x46, 0x4E,
    0xA9, 0x3F, 0x22, 0x10, 0x44, 0x9A, 0xE3, 0x8E
};

/* A byte builder: arguments appended, packages closed over their
 * elements. */
typedef struct {
    UCHAR b[2048];
    ULONG n;
} BLD;

static void put16(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFF);
    p[1] = (UCHAR)((v >> 8) & 0xFF);
}

static void put32(UCHAR *p, ULONG v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, (v >> 16) & 0xFFFF);
}

static void arg(BLD *d, ULONG type, const void *data, ULONG len)
{
    ULONG step = 4 + (len > 4 ? len : 4);

    put16(d->b + d->n, type);
    put16(d->b + d->n + 2, len);
    memset(d->b + d->n + 4, 0, step - 4);
    if (len != 0) {
        memcpy(d->b + d->n + 4, data, len);
    }
    d->n += step;
}

static void str(BLD *d, const char *s)
{
    arg(d, CM_DSD_ARG_STRING, s, (ULONG)strlen(s) + 1);
}

static void num(BLD *d, ULONG v)
{
    UCHAR x[4];

    put32(x, v);
    arg(d, CM_DSD_ARG_INTEGER, x, 4);
}

static void pkg(BLD *d, const BLD *inner)
{
    arg(d, CM_DSD_ARG_PACKAGE, inner->b, inner->n);
}

static void pair_s(BLD *props, const char *key, const char *val)
{
    BLD p;

    p.n = 0;
    str(&p, key);
    str(&p, val);
    pkg(props, &p);
}

static void pair_n(BLD *props, const char *key, ULONG val)
{
    BLD p;

    p.n = 0;
    str(&p, key);
    num(&p, val);
    pkg(props, &p);
}

/* The answer: header, then `count` top arguments from `body`. */
static ULONG answer(UCHAR *out, const BLD *body, ULONG count)
{
    put32(out, CM_DSD_OUT_SIGNATURE);
    put32(out + 4, 12 + body->n);
    put32(out + 8, count);
    memcpy(out + 12, body->b, body->n);
    return 12 + body->n;
}

static UCHAR buf[4096];
static CM_DSD dsd;

/* The usual shape: UUID, the properties package (host interface, port
 * number), as the answer's two top arguments. */
static ULONG usual(const char *ref, ULONG port, int withPort)
{
    BLD props;
    BLD body;

    props.n = 0;
    pair_s(&props, "usb4-host-interface", ref);
    if (withPort) {
        pair_n(&props, "usb4-port-number", port);
    }
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    return answer(buf, &body, 2);
}

static void test_read(void)
{
    ULONG len;
    BLD props;
    BLD body;
    BLD whole;

    len = usual("\\_SB_.PCI0.GPPC.NHI0", 1, 1);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_OK, "the usual shape");
    CHECK(strcmp(dsd.Ref, "\\_SB_.PCI0.GPPC.NHI0") == 0 && dsd.RefLen == 20,
          "the reference normalised");
    CHECK(dsd.HavePort && dsd.Port == 1, "the port number read");

    len = usual("\\_SB.PCI0.GPPC.NHI1", 0, 1);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_OK, "port number zero");
    CHECK(dsd.HavePort && dsd.Port == 0 &&
          strcmp(dsd.Ref, "\\_SB_.PCI0.GPPC.NHI1") == 0,
          "zero is read and is no error; _SB padded");
    len = usual("\\_SB.PCI0.GPPC.NHI1", 0, 0);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_OK, "no port number");
    CHECK_EQ(dsd.HavePort, 0, "none read");

    /* The package as one argument. */
    props.n = 0;
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.NHI0");
    pair_n(&props, "usb4-port-number", 2);
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    whole.n = 0;
    pkg(&whole, &body);
    len = answer(buf, &whole, 1);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_OK, "the package as one");
    CHECK(dsd.Port == 2, "and its port");

    /* Another UUID's package before ours, with keys of its own. */
    props.n = 0;
    pair_n(&props, "ExternalFacingPort", 1);
    pair_n(&props, "UID", 0);
    whole.n = 0;
    arg(&whole, CM_DSD_ARG_BUFFER, uuidOther, 16);
    pkg(&whole, &props);
    props.n = 0;
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.GPPC.NHI0");
    pair_n(&props, "usb4-port-number", 1);
    pair_n(&props, "ExternalFacingPort", 1);
    arg(&whole, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&whole, &props);
    len = answer(buf, &whole, 4);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_OK,
             "another UUID first, other keys beside");

    /* No device-properties UUID. */
    props.n = 0;
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.GPPC.NHI0");
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidOther, 16);
    pkg(&body, &props);
    len = answer(buf, &body, 2);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_NO_UUID, "a wrong UUID");

    /* The UUID twice. */
    props.n = 0;
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.GPPC.NHI0");
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    len = answer(buf, &body, 4);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_DUPLICATE, "the UUID twice");

    /* A key missing, a key twice, a repeated port number. */
    props.n = 0;
    pair_n(&props, "usb4-port-number", 1);
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    len = answer(buf, &body, 2);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_NO_KEY, "no host interface");
    props.n = 0;
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.GPPC.NHI0");
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.GPPC.NHI1");
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    len = answer(buf, &body, 2);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_DUPLICATE,
             "the host interface twice");
    props.n = 0;
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.GPPC.NHI0");
    pair_n(&props, "usb4-port-number", 1);
    pair_n(&props, "usb4-port-number", 2);
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    len = answer(buf, &body, 2);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_DUPLICATE,
             "the port number twice");

    /* A reference that is no name string, a bad name, a port number that
     * is no integer. */
    props.n = 0;
    pair_n(&props, "usb4-host-interface", 5);
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    len = answer(buf, &body, 2);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_REF,
             "an integer for the reference");
    len = usual("^NHI0", 1, 1);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_REF, "a relative name");
    len = usual("\\_SB.PCI0.GPPCX.NHI0", 1, 1);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_REF, "a five-char segment");
    props.n = 0;
    pair_s(&props, "usb4-host-interface", "\\_SB.PCI0.GPPC.NHI0");
    pair_s(&props, "usb4-port-number", "1");
    body.n = 0;
    arg(&body, CM_DSD_ARG_BUFFER, uuidProps, 16);
    pkg(&body, &props);
    len = answer(buf, &body, 2);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_PORT,
             "a string for the port number");

    /* The header and the bounds. */
    len = usual("\\_SB.PCI0.GPPC.NHI0", 1, 1);
    CHECK_EQ(CmDsdRead(buf, 11, &dsd), CM_DSD_BAD_HEADER, "short");
    CHECK_EQ(CmDsdRead(NULL, len, &dsd), CM_DSD_BAD_HEADER, "no buffer");
    buf[0] ^= 1;
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_HEADER, "a wrong signature");
    buf[0] ^= 1;
    CHECK_EQ(CmDsdRead(buf, len - 1, &dsd), CM_DSD_BAD_HEADER,
             "Length past what was returned");
    put32(buf + 8, 3);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_SHAPE,
             "Count past the arguments");
    put32(buf + 8, 0);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_HEADER, "Count 0");
    put32(buf + 8, 2);
    /* An inner package's length running past its parent. */
    put16(buf + 12 + 20 + 2, 0xFFF0);
    CHECK_EQ(CmDsdRead(buf, len, &dsd), CM_DSD_BAD_SHAPE,
             "a package past the answer");
}

static void test_names(void)
{
    char out[CM_DSD_NAME_MAX + 1];
    char n0[CM_DSD_NAME_MAX + 1];
    char n1[CM_DSD_NAME_MAX + 1];
    static const USHORT w0[] = { '\\', '_', 'S', 'B', '.', 'P', 'C', 'I',
                                 '0', '.', 'G', 'P', 'P', 'C', '.', 'N',
                                 'H', 'I', '0', 0 };
    static const USHORT wbad[] = { '\\', '_', 'S', 0x00E9, 0 };
    char longName[200];
    ULONG i;

    CHECK_EQ(CmDsdNormName("\\_SB.PCI0.GPPC.NHI0", 19, out, sizeof(out)), 20,
             "a full name");
    CHECK(strcmp(out, "\\_SB_.PCI0.GPPC.NHI0") == 0, "padded");
    CHECK_EQ(CmDsdNormName("_sb_.pci0.nhi0", 14, out, sizeof(out)), 15,
             "no backslash, lower case");
    CHECK(strcmp(out, "\\_SB_.PCI0.NHI0") == 0, "taken as upper, rooted");
    CHECK_EQ(CmDsdNormName("\\_SB.PCI0\0\0", 11, out, sizeof(out)), 10,
             "trailing NULs");
    CHECK_EQ(CmDsdNormName("\\_SB.PCI0\0X", 11, out, sizeof(out)), 0,
             "something past the NUL");
    CHECK_EQ(CmDsdNormName("\\_SB..PCI0", 10, out, sizeof(out)), 0,
             "an empty segment");
    CHECK_EQ(CmDsdNormName("\\_SB.", 5, out, sizeof(out)), 0, "a trailing dot");
    CHECK_EQ(CmDsdNormName("\\", 1, out, sizeof(out)), 0, "the root alone");
    CHECK_EQ(CmDsdNormName("\\_SB.PC-0", 9, out, sizeof(out)), 0,
             "a character ACPI names do not use");
    CHECK_EQ(CmDsdNormName("\\_SB.PCI0", 9, out, 6), 0, "no room");
    for (i = 0; i < 17 * 5; i++) {
        longName[i] = (i % 5) == 4 ? '.' : 'A';
    }
    longName[17 * 5 - 1] = 0;
    CHECK_EQ(CmDsdNormName(longName, (ULONG)strlen(longName), out, sizeof(out)),
             0, "seventeen segments");

    CHECK(CmDsdNormNameW(w0, 32, n0, sizeof(n0)) == 20 &&
          strcmp(n0, "\\_SB_.PCI0.GPPC.NHI0") == 0,
          "the NHI's own name, UTF-16, as Windows gives it");
    CHECK_EQ(CmDsdNormNameW(wbad, 5, n1, sizeof(n1)), 0, "above 0x7F refused");
    (VOID)CmDsdNormName("\\_SB_.PCI0.GPPC.NHI0", 20, n1, sizeof(n1));
    CHECK(CmDsdSameName(n0, n1), "the reference and the NHI's name agree");
    (VOID)CmDsdNormName("\\_SB.PCI0.GPPC.NHI1", 19, n1, sizeof(n1));
    CHECK(!CmDsdSameName(n0, n1), "the other NHI's does not");
    CHECK(!CmDsdSameName(n0, ""), "an empty name matches nothing");
    CHECK(!CmDsdSameName("", ""), "nor two");
}

/* Record 22 section 7.6.1: one ACPI-named port per NHI, or none. */
static void test_taken(void)
{
    CHECK_EQ(CmDsdClaimsTaken(0), 0, "no claim: none");
    CHECK_EQ(CmDsdClaimsTaken(1), 1, "one claim: taken");
    CHECK_EQ(CmDsdClaimsTaken(2), 0, "two ports name one NHI: none taken");
    CHECK_EQ(CmDsdClaimsTaken(5), 0, "five: none taken");
}

/* A root port's filter restarted alone, operation 30h unavailable: its new
 * claim, the same NHI's and the only one, renews the member. */
static void test_renewed(void)
{
    CHECK(CmDsdRenewed(7, 9, TRUE), "a new publication's sole claim renews");
    CHECK(!CmDsdRenewed(7, 7, TRUE), "the same publication: nothing to do");
    CHECK(!CmDsdRenewed(7, 9, FALSE),
          "not the only claim naming the NHI: no renewal");
    CHECK(!CmDsdRenewed(7, 0, TRUE), "no claim serial: none");
}

int main(void)
{
    test_read();
    test_names();
    test_taken();
    test_renewed();
    printf("test_cm_dsd: %d checks, %d failures\n", checks, failures);
    return failures;
}
