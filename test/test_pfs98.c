/*
 * test_pfs98.c - host vectors for XHCIPERF's reader of Stor98's counter
 * answer (xhciperf\pf_s98.c; design record 29 sections 5.5, 6 and 9;
 * Stor98's include\stor98pf.h, decision MD-8): an answer built byte by byte
 * at the header's offsets, a truncated one, a longer one, a wrong
 * signature, a low word's carry between two answers, a changed epoch, the
 * 63 KiB bucket, the identity page, and the page's sentences.
 */

#include <stdio.h>
#include <string.h>
#include "../xhciperf/pf_s98.h"
#include "test_harness.h"

static unsigned char ans[S98_ANSWER_BYTES + 64];

static void put32(unsigned char *p, pf_u32 v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put_total(int idx, pf_u64 v)
{
    put32(ans + S98_OFS_TOTALS + 8 * idx, (pf_u32)(v & 0xFFFFFFFFu));
    put32(ans + S98_OFS_TOTALS + 8 * idx + 4, (pf_u32)(v >> 32));
}

static void build(pf_u32 epoch)
{
    memset(ans, 0, sizeof(ans));
    put32(ans + S98_OFS_SIGNATURE, S98_SIGNATURE);
    put32(ans + S98_OFS_VERSION, S98_VERSION);
    put32(ans + S98_OFS_LENGTH, S98_ANSWER_BYTES);
    put32(ans + S98_OFS_EPOCH, epoch);
    put32(ans + S98_OFS_BUILD, 1);
    put32(ans + S98_OFS_PCI, (5u << 16) | (1u << 3) | 2u);
    memcpy(ans + S98_OFS_DRIVERVERSION, "3.2.0.0", 7);
    put32(ans + S98_OFS_MAXTRANSFER, 0x80000);
    put32(ans + S98_OFS_DCBSEEN, 1);
    put32(ans + S98_OFS_DCBMAXXFER, 0x80000);
    put32(ans + S98_OFS_DCBMAXSG, 17);
    put32(ans + S98_OFS_DCBDMD, 0x00000E09);
}

static void test_parse(void)
{
    S98_ANSWER a;

    build(0x1234);
    CHECK_EQ(s98_parse(ans, S98_ANSWER_BYTES, &a), 1, "an answer parses");
    CHECK_EQ(a.epoch, 0x1234, "its epoch");
    CHECK_EQ(a.pci, (5u << 16) | (1u << 3) | 2u, "its location");
    CHECK(strcmp(a.driverVersion, "3.2.0.0") == 0, "its version text");
    CHECK_EQ(a.dcbMaxSg, 17, "the DCB's elements");
    CHECK_EQ(s98_parse(ans, S98_ANSWER_BYTES - 1, &a), 0, "a truncated answer is refused");
    put32(ans + S98_OFS_LENGTH, S98_ANSWER_BYTES + 64);
    CHECK_EQ(s98_parse(ans, S98_ANSWER_BYTES + 64, &a), 1, "a longer answer of version 1 is read");
    build(0x1234);
    put32(ans + S98_OFS_SIGNATURE, 0x50383952u);
    CHECK_EQ(s98_parse(ans, S98_ANSWER_BYTES, &a), 0, "a wrong signature");
    build(0x1234);
    put32(ans + S98_OFS_VERSION, 2);
    CHECK_EQ(s98_parse(ans, S98_ANSWER_BYTES, &a), 0, "another version");
    build(0);
    CHECK_EQ(s98_parse(ans, S98_ANSWER_BYTES, &a), 0, "a zero epoch");
    build(0x1234);
    put32(ans + S98_OFS_LENGTH, 16);
    CHECK_EQ(s98_parse(ans, S98_ANSWER_BYTES, &a), 0, "a Length shorter than the layout");
}

static void test_diff(void)
{
    S98_ANSWER a, b;
    S98_DELTA d;

    build(7);
    put_total(S98_T_SRBS + 1, 0xFFFFFFF0u);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_SRBS + 1, (pf_u64)0x100000010u);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    CHECK_EQ(s98_diff(&b, &a, &d), 1, "two answers of one epoch");
    CHECK_EQ(d.tot[S98_T_SRBS + 1], 0x20, "the low word's carry is subtracted exactly");
    build(8);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    CHECK_EQ(s98_diff(&b, &a, &d), 0, "another epoch is not subtracted");
    build(7);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    CHECK_EQ(s98_diff(&b, &a, &d), 0, "a total that went down is not subtracted");
}

static void test_identity(void)
{
    unsigned char p[16];
    unsigned char l[8];
    pf_u32 pci;

    memset(p, 0, sizeof(p));
    p[1] = 0xC9;
    p[3] = 12;
    put32(p + 4, S98_SIGNATURE);
    put32(p + 8, 1);
    put32(p + 12, 0x5000A);
    CHECK_EQ(s98_identity(p, 16, &pci), 1, "the identity page");
    CHECK_EQ(pci, 0x5000A, "its location");
    CHECK_EQ(s98_identity(p, 15, &pci), 0, "a short page");
    p[4] = 0;
    CHECK_EQ(s98_identity(p, 16, &pci), 0, "no signature");
    memset(l, 0, sizeof(l));
    l[3] = 4;
    l[4] = 0x00;
    l[5] = 0x80;
    l[6] = 0x83;
    l[7] = 0xC9;
    CHECK_EQ(s98_lists_identity(l, 8), 1, "page 00h lists C9h");
    l[7] = 0xB0;
    CHECK_EQ(s98_lists_identity(l, 8), 0, "a list without C9h");
    l[7] = 0xC9;
    CHECK_EQ(s98_lists_identity(l, 7), 0, "C9h past the bytes returned");
}

static void test_buckets_page(void)
{
    S98_ANSWER a, b;
    S98_DELTA d;
    PF_TEXT t;
    static char buf[4096];

    CHECK_EQ(s98_size_bucket(64512u), 6, "63 KiB");
    CHECK_EQ(s98_size_bucket(64513u), 7, "just above 63 KiB");
    CHECK_EQ(s98_size_bucket(524288u), 10, "512 KiB");
    CHECK_EQ(s98_size_bucket(1048577u), 12, "above 1 MiB");
    CHECK(strcmp(s98_size_name(6), "63 KiB") == 0, "its name");

    /* 98 SE without the VSD: 1 MiB writes cut into 63 KiB pieces */
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_SRBSIZE + 13 + 6, 4096);
    put_total(S98_T_SRBSIZE + 13 + 3, 256);
    put_total(S98_T_CMDSIZE + 13 + 6, 4096);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 1048576u, 1), S98_SCAT_HIDDEN, "cut into pieces: the layout is hidden");
    pf_tx_init(&t, buf, sizeof(buf));
    s98_page(&t, &a, &d, 1, 256, 1048576u, 1);
    CHECK(strstr(buf, "and 4096 of 63 KiB") != NULL, "the page counts the 63 KiB pieces");
    CHECK(strstr(buf, "demands physical scatter/gather: yes") != NULL, "phys_sgd demanded");

    /* with the VSD: 512 KiB whole, scattered */
    build(9);
    put32(ans + S98_OFS_DCBDMD, 0x00040600);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_SRBSIZE + 13 + 10, 512);
    put_total(S98_T_CMDSIZE + 13 + 10, 512);
    put_total(S98_T_RUNS + 3 * 10 + 8, 500);
    put_total(S98_T_RUNS + 3 * 10 + 0, 12);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_YES, "whole and scattered");
    pf_tx_init(&t, buf, sizeof(buf));
    s98_page(&t, &a, &d, 1, 512, 524288u, 1);
    CHECK(strstr(buf, "received 512 requests") != NULL, "whole at Stor98");
    CHECK(strstr(buf, "demands physical scatter/gather: no") != NULL, "phys_sgd removed");
    CHECK(strstr(buf, "if no other program") != NULL, "the counts are qualified");

    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 3 * 10 + 0, 512);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_NO, "one run each: not scattered");
    CHECK_EQ(s98_scattered(&d, 4096u, 1), S98_SCAT_UNKNOWN, "no class for 4 KiB");

    /* two runs in 512 KiB are not more than one per 64 KiB; nine are */
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 3 * 10 + 1, 512);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_NO, "two runs in 512 KiB: not scattered");
    CHECK_EQ(s98_scattered(&d, 65536u, 1), S98_SCAT_UNKNOWN, "another class has no requests");
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 3 * 10 + 4, 512);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_YES, "9-16 runs in 512 KiB: scattered");
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 0 * 10 + 1, 512);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 65536u, 1), S98_SCAT_YES, "two runs in 64 KiB: scattered");
    /* double-buffered: the runs Stor98 saw are IOS's buffer, not the caller's */
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 3 * 10 + 4, 512);
    put_total(S98_T_DOUBLEBUF, 512);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_HIDDEN, "double-buffered with runs: hidden");
    /* one whole scattered request among many cut ones: hidden, not scattered */
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 3 * 10 + 4, 1);
    put_total(S98_T_SRBSIZE + 13 + 6, 900);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_HIDDEN, "a whole minority: hidden");
    /* nine whole scattered requests and ten cut ones (eight full pieces each) */
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 3 * 10 + 4, 9);
    put_total(S98_T_SRBSIZE + 13 + 6, 80);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_HIDDEN, "nine whole, ten cut: hidden");
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    put_total(S98_T_RUNS + 3 * 10 + 4, 11);
    put_total(S98_T_SRBSIZE + 13 + 6, 80);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    CHECK_EQ(s98_scattered(&d, 524288u, 1), S98_SCAT_YES, "eleven whole, ten cut: scattered");
    /* nothing reached the drive (a read from the cache): no depth claimed */
    build(9);
    s98_parse(ans, S98_ANSWER_BYTES, &b);
    s98_parse(ans, S98_ANSWER_BYTES, &a);
    s98_diff(&b, &a, &d);
    pf_tx_init(&t, buf, sizeof(buf));
    s98_page(&t, &a, &d, 1, 0, 524288u, 0);
    CHECK(strstr(buf, "at once: none") != NULL, "an empty depth histogram says none");
}

int main(void)
{
    test_parse();
    test_diff();
    test_identity();
    test_buckets_page();
    printf("test_pfs98: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
