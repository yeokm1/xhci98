/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * inqid_ut - host unit test of src/inqid.c, the INQUIRY vendor/product
 * split of the NVMe model number (docs/RENAME-STOR98.md, "Disk identity
 * change"), the standard INQUIRY data and the vital product data pages
 * 00h, 80h and 83h. Built by tests\unit\build.cmd; the exit code is the
 * number of failed checks.
 */

#include <stdio.h>
#include <string.h>
#include "nvme2k.h"
#include "inqid.c"

static int failures;

/* mn: the model number as the controller reports it (padded to 40 with
 * spaces here, as Identify Controller does); want: 24 bytes expected. */
static void expect(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static void check(const char *mn, const char *vendor, const char *product)
{
    UCHAR m[41], out[25], want[25];
    size_t n = strlen(mn);

    memset(m, ' ', 40);
    memcpy(m, mn, n > 40 ? 40 : n);
    m[40] = 0;
    memset(out, 0xCC, sizeof(out));
    out[24] = 0;
    InqIdentity(m, out);
    sprintf((char *)want, "%-8.8s%-16.16s", vendor, product);
    if (memcmp(out, want, 24) == 0) {
        printf("  ok    \"%s\" -> [%.8s][%.16s]\n", mn, out, out + 8);
    } else {
        printf("  FAIL  \"%s\" -> [%.8s][%.16s], want [%.8s][%.16s]\n", mn, out, out + 8, want, want + 8);
        failures++;
    }
}

int main(void)
{
    UCHAR m[41], out[25];

    printf("inqid_ut\n");
    check("QEMU NVMe Ctrl", "QEMU", "NVMe Ctrl");
    check("Samsung SSD 970 EVO Plus 1TB", "Samsung", "SSD 970 EVO Plus");
    check("WDC WDS500G2B0C-00PXH0", "WDC", "WDS500G2B0C-00PX");
    check("  INTEL SSDPEKNW010T8", "INTEL", "SSDPEKNW010T8");
    check("KINGSTON SA2000M8500G", "KINGSTON", "SA2000M8500G");      /* 8-character word */
    check("SAMSUNGXY MZVLB512HAJQ", "NVMe", "SAMSUNGXY MZVLB5");     /* 9-character word */
    check("SingleWord", "NVMe", "SingleWord");                       /* nothing after the word */
    check("Vendor    Spaced   Product", "Vendor", "Spaced   Product");
    check("", "NVMe", "");
    check("1234567890123456789012345678901234567890", "NVMe", "1234567890123456");
    /* 0.3.0.1: an underscore ends the first word too (xhci98, real SK hynix). */
    check("SKHynix_HFS256GD4QNFN-8A", "SKHynix", "HFS256GD4QNFN-8A");
    check("SKHynix_HFS256GD4QNFN-8A10A", "SKHynix", "HFS256GD4QNFN-8A");
    check("Samsung SSD 980 PRO 1TB", "Samsung", "SSD 980 PRO 1TB");
    check("WD_BLACK SN770 1TB", "WD", "BLACK SN770 1TB");
    check("Vendor_Product Name", "Vendor", "Product Name");
    check("ACME _ Product", "ACME", "Product");                      /* mixed delimiters skipped */
    check("VeryLongVendor_Product", "NVMe", "VeryLongVendor_P");     /* first word over 8 */
    check("ACME_", "NVMe", "ACME_");                                  /* nothing after the delimiter */

    /* A NUL inside the field ends it. */
    memset(m, 0, sizeof(m));
    memcpy(m, "ACME", 4);
    memset(out, 0xCC, sizeof(out));
    InqIdentity(m, out);
    if (memcmp(out, "NVMe    ACME            ", 24) != 0) {
        printf("  FAIL  NUL-terminated one word\n");
        failures++;
    } else {
        printf("  ok    NUL-terminated one word\n");
    }

    /* Standard INQUIRY data: header, identity, revision cut at 4. */
    {
        UCHAR fr[9], d[INQ_STD_LEN + 4];

        memset(m, ' ', 40);
        memcpy(m, "QEMU NVMe Ctrl", 14);
        m[40] = 0;
        memcpy(fr, "1.0\0\0\0\0\0", 9);
        memset(d, 0xCC, sizeof(d));
        InqStandardData(m, fr, d);
        expect(d[0] == 0 && d[1] == 0 && d[2] == 5 && d[3] == 2 && d[4] == 31 && d[5] == 0 &&
               d[6] == 0 && d[7] == 2, "standard data header (SPC-3, additional length 31, CmdQue)");
        expect(memcmp(d + 8, "QEMU    NVMe Ctrl       1.0 ", 28) == 0, "standard data identity and revision");
        expect(d[INQ_STD_LEN] == 0xCC, "standard data writes exactly 36 bytes");
        memcpy(fr, "ABCDEFGH", 9);
        InqStandardData(m, fr, d);
        expect(memcmp(d + 32, "ABCD", 4) == 0, "revision is the first 4 characters");
    }

    /* Vital product data. */
    {
        UCHAR sn[21], v[INQ_VPD_MAX + 4];
        ULONG n;

        memset(m, ' ', 40);
        memcpy(m, "Samsung SSD 970 EVO Plus 1TB", 28);
        m[40] = 0;
        memset(sn, ' ', 20);
        memcpy(sn, "S4EWNX0R123456", 14);
        sn[20] = 0;

        memset(v, 0xCC, sizeof(v));
        n = InqVpdPage(0x00, sn, m, v);
        expect(n == 7 && v[0] == 0 && v[1] == 0x00 && v[2] == 0 && v[3] == 3 && v[4] == 0x00 &&
               v[5] == 0x80 && v[6] == 0x83, "VPD 00h lists 00h, 80h, 83h");

        n = InqVpdPage(0x80, sn, m, v);
        expect(n == 24 && v[1] == 0x80 && v[2] == 0 && v[3] == 20 && memcmp(v + 4, "S4EWNX0R123456      ", 20) == 0,
               "VPD 80h is the 20-character serial number");

        n = InqVpdPage(0x83, sn, m, v);
        expect(n == 76 && v[1] == 0x83 && v[2] == 0 && v[3] == 72, "VPD 83h page length 72");
        expect(v[4] == 0x02 && v[5] == 0x01 && v[6] == 0 && v[7] == 68,
               "VPD 83h designator: ASCII, LU, T10 vendor ID, length 68");
        expect(memcmp(v + 8, "NVMe    ", 8) == 0 && memcmp(v + 16, m, 40) == 0 && memcmp(v + 56, sn, 20) == 0,
               "VPD 83h designator: vendor, model, serial");
        expect(v[INQ_VPD_MAX] == 0xCC, "VPD 83h writes at most INQ_VPD_MAX bytes");

        /* A NUL ends the serial number; the rest is spaces. */
        memset(sn, 0, sizeof(sn));
        memcpy(sn, "SN1", 3);
        n = InqVpdPage(0x80, sn, m, v);
        expect(n == 24 && memcmp(v + 4, "SN1                 ", 20) == 0, "VPD 80h NUL-terminated serial padded");

        expect(InqVpdPage(0xB0, sn, m, v) == 0, "VPD B0h not supported");
        expect(InqVpdPage(0xB1, sn, m, v) == 0, "VPD B1h not supported");
        expect(InqVpdPage(0x81, sn, m, v) == 0, "VPD 81h not supported");
        expect(InqVpdPage(0x01, sn, m, v) == 0, "VPD 01h not supported");
    }

    /* The INQUIRY CDB: EVPD in byte 1 bit 0, page code in byte 2. */
    {
        UCHAR sn[21], fr[9], r[INQ_RESP_MAX];
        UCHAR cdb[6];

        memset(m, ' ', 40);
        memcpy(m, "QEMU NVMe Ctrl", 14);
        m[40] = 0;
        memcpy(sn, "deadbeef            ", 21);
        memcpy(fr, "1.0     ", 9);

#define CDB(b1, b2, b3, b4) (cdb[0] = 0x12, cdb[1] = (b1), cdb[2] = (b2), cdb[3] = (b3), cdb[4] = (b4), cdb[5] = 0)
        CDB(0x00, 0x00, 0, 36);
        expect(InqResponse(cdb, sn, m, fr, r) == 36 && r[2] == 5 && memcmp(r + 8, "QEMU", 4) == 0,
               "standard INQUIRY, 36 bytes");
        CDB(0x00, 0x00, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == 36, "standard INQUIRY, allocation 255 -> 36");
        CDB(0x00, 0x00, 0, 5);
        expect(InqResponse(cdb, sn, m, fr, r) == 5 && r[4] == 31, "standard INQUIRY, allocation 5 -> 5");
        CDB(0x00, 0x00, 0, 0);
        expect(InqResponse(cdb, sn, m, fr, r) == 0, "standard INQUIRY, allocation 0 -> no data");
        CDB(0x01, 0x00, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == 7 && r[1] == 0x00 && r[5] == 0x80, "EVPD page 00h");
        CDB(0x01, 0x80, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == 24 && r[1] == 0x80 && memcmp(r + 4, "deadbeef", 8) == 0,
               "EVPD page 80h");
        CDB(0x01, 0x83, 0x01, 0x00);
        expect(InqResponse(cdb, sn, m, fr, r) == 76 && r[1] == 0x83, "EVPD page 83h, 16-bit allocation 256");
        CDB(0x01, 0x83, 0, 8);
        expect(InqResponse(cdb, sn, m, fr, r) == 8 && r[3] == 72, "EVPD page 83h cut at allocation 8");
        CDB(0x01, 0xB0, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == INQ_REFUSE, "EVPD page B0h refused");
        CDB(0x01, 0xB2, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == INQ_REFUSE, "EVPD page B2h refused");
        /* The defect this replaces tested bit 0 of the page code. */
        CDB(0x00, 0x83, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == INQ_REFUSE, "page code 83h with EVPD=0 refused");
        CDB(0x00, 0x80, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == INQ_REFUSE, "page code 80h with EVPD=0 refused");
        CDB(0x02, 0x00, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == INQ_REFUSE, "CmdDt refused");
        CDB(0x03, 0x00, 0, 0xFF);
        expect(InqResponse(cdb, sn, m, fr, r) == INQ_REFUSE, "CmdDt with EVPD refused");
        CDB(0xE0, 0x00, 0, 36);
        expect(InqResponse(cdb, sn, m, fr, r) == 36, "LUN bits of byte 1 ignored (SCSI-2 initiators)");
#undef CDB
    }

    printf("%d failure(s)\n", failures);
    return failures;
}
