/*
 * test_cm_net.c - the Thunderbolt networking service's data rings, their
 * pure half (src/cm/cm_netr.c; design record 35 sections 5.3 and 6.3; task
 * 45.59 step S5): the descriptors as the host interface reads them, the
 * receive ring's completion (EOF PDF and length decide, the SOF PDF never
 * does; CRC Error and Buffer Overflow as flags), the transmit ring's
 * posting, reaping and wrap, the one-empty-slot rule, and the reservation's
 * tokens (all or nothing, one at a time, a stale generation released
 * without a post).
 */

#include <stdio.h>
#include "../src/cm/cm_netr.h"
#include "test_harness.h"

#define N 32UL

static UCHAR desc[N * TBT_DESC_BYTES];
static ULONG pa[N];

static ULONG ld(ULONG i, ULONG off)
{
    const UCHAR *p = desc + i * TBT_DESC_BYTES + off;
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}

static void st(ULONG i, ULONG off, ULONG v)
{
    UCHAR *p = desc + i * TBT_DESC_BYTES + off;
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

static void setup(CM_NETR *r)
{
    ULONG i;

    for (i = 0; i < N; i++) {
        pa[i] = 0x10000000UL + 0x1000UL * i;
        st(i, TBT_DESC_DW2, 0xFFFFFFFFUL);
    }
    CmNetrInit(r, desc, N, pa);
}

static void test_init(void)
{
    CM_NETR r;
    ULONG i;
    int ok = 1;

    setup(&r);
    for (i = 0; i < N; i++) {
        if (ld(i, TBT_DESC_ADDR_LO) != pa[i] || ld(i, TBT_DESC_ADDR_HI) != 0 ||
            ld(i, TBT_DESC_DW2) != 0 || ld(i, TBT_DESC_DW3) != 0) {
            ok = 0;
        }
    }
    CHECK(ok, "each descriptor its buffer's address, DW2 and DW3 clear");
    CHECK_EQ(r.Head, 0, "Head 0");
    CHECK_EQ(r.Tail, 0, "Tail 0");
}

static void test_rx(void)
{
    CM_NETR r;
    ULONG len;
    ULONG flags;

    setup(&r);
    CmNetrRxPostAll(&r, TRUE);
    CHECK_EQ(ld(0, TBT_DESC_DW2), TBT_DESC_RS | TBT_DESC_IE, "posted: RS 1, IE 1, DD 0");
    CHECK_EQ(ld(N - 1, TBT_DESC_DW2), TBT_DESC_RS | TBT_DESC_IE, "every slot posted");
    CHECK_EQ(CmNetrRxConsumer(&r), N - 1, "Consumer one behind Head: N - 1 slots the HI's");
    CHECK_EQ(CmNetrRxTake(&r, 0, &len, &flags), CM_NETR_RX_NONE, "DD clear: not taken");

    /* A frame of 1514 + 12 bytes with SOF PDF 0 (a short frame, the
     * emulator's write-back) and EOF PDF 2. */
    st(0, TBT_DESC_DW2, TBT_DESC_DD | (2UL << TBT_DESC_EOF_SHIFT) | 1526UL);
    CHECK_EQ(CmNetrRxTake(&r, 0, &len, &flags), CM_NETR_RX_FRAME, "SOF PDF 0 accepted");
    CHECK_EQ(len, 1526, "its length");
    CHECK_EQ(flags, 0, "no flag");
    st(1, TBT_DESC_DW2, TBT_DESC_DD | (2UL << TBT_DESC_EOF_SHIFT) | (1UL << TBT_DESC_SOF_SHIFT));
    CHECK_EQ(CmNetrRxTake(&r, 1, &len, &flags), CM_NETR_RX_FRAME, "SOF PDF 1 accepted");
    CHECK_EQ(len, 4096, "length 0 is 4096");
    st(2, TBT_DESC_DW2, TBT_DESC_DD | (1UL << TBT_DESC_EOF_SHIFT) | 64UL);
    CHECK_EQ(CmNetrRxTake(&r, 2, &len, &flags), CM_NETR_RX_BAD_PDF, "EOF PDF 1: refused");
    st(3, TBT_DESC_DW2, TBT_DESC_DD | (2UL << TBT_DESC_EOF_SHIFT) | TBT_DESC_RX_CRC_ERR |
                            TBT_DESC_RX_BO | 100UL);
    CHECK_EQ(CmNetrRxTake(&r, 3, &len, &flags), CM_NETR_RX_FRAME, "a frame with errors");
    CHECK_EQ(flags, CM_NETR_FLAG_CRC | CM_NETR_FLAG_OVERFLOW, "CRC Error and Buffer Overflow");
    CHECK_EQ(CmNetrRxTake(&r, N + 3, &len, &flags), CM_NETR_RX_FRAME, "an index wraps");

    CmNetrRxRepost(&r, 0, FALSE);
    CHECK_EQ(ld(0, TBT_DESC_DW2), TBT_DESC_RS, "reposted: RS 1, IE as asked, DD 0");
    CHECK_EQ(ld(0, TBT_DESC_ADDR_LO), pa[0], "its address kept");
    r.Head = 5;
    CHECK_EQ(CmNetrRxConsumer(&r), 4, "Consumer the slot before Head");
    r.Head = 0;
    CHECK_EQ(CmNetrRxConsumer(&r), N - 1, "and across the wrap");
}

static void test_tx(void)
{
    CM_NETR r;
    ULONG i;

    setup(&r);
    CHECK_EQ(CmNetrTxUsed(&r), 0, "empty");
    CHECK_EQ(CmNetrTxFree(&r), N - 1, "N - 1 free: one slot always empty");
    CmNetrTxPost(&r, 0, 1526);
    CHECK_EQ(ld(0, TBT_DESC_DW2),
             1526UL | (2UL << TBT_DESC_EOF_SHIFT) | (1UL << TBT_DESC_SOF_SHIFT) | TBT_DESC_RS,
             "SOF PDF 1, EOF PDF 2, RS 1, IE 0, the length");
    CmNetrTxPost(&r, 1, 4096);
    CHECK_EQ(ld(1, TBT_DESC_DW2) & TBT_DESC_LEN_MASK, 0, "4096 written as 0");
    r.Head = 2;
    CHECK_EQ(CmNetrTxUsed(&r), 2, "two posted");
    CHECK_EQ(CmNetrTxReap(&r), 0, "none done");
    st(0, TBT_DESC_DW2, ld(0, TBT_DESC_DW2) | TBT_DESC_DD);
    CHECK_EQ(CmNetrTxReap(&r), 1, "the first done");
    CHECK_EQ(r.Tail, 1, "Tail past it");
    st(1, TBT_DESC_DW2, ld(1, TBT_DESC_DW2) | TBT_DESC_DD);
    CHECK_EQ(CmNetrTxReap(&r), 1, "the second");
    CHECK_EQ(CmNetrTxReap(&r), 0, "never past Head");
    CHECK_EQ(CmNetrTxFree(&r), N - 1, "free again");

    /* Around the wrap. */
    r.Head = N - 2;
    r.Tail = N - 2;
    for (i = 0; i < 4; i++) {
        CmNetrTxPost(&r, r.Head, 100);
        r.Head = (r.Head + 1) % N;
    }
    CHECK_EQ(r.Head, 2, "Head wrapped");
    CHECK_EQ(CmNetrTxUsed(&r), 4, "four used across the wrap");
    for (i = 0; i < 4; i++) {
        ULONG s = (N - 2 + i) % N;
        st(s, TBT_DESC_DW2, ld(s, TBT_DESC_DW2) | TBT_DESC_DD);
    }
    CHECK_EQ(CmNetrTxReap(&r), 4, "all four reaped");
    CHECK_EQ(r.Tail, 2, "Tail wrapped");
}

static void test_reserve(void)
{
    CM_NETR r;
    CM_NETR_RES res;
    ULONG first;

    setup(&r);
    res.Held = FALSE;
    CHECK_EQ(CmNetrReserve(&r, &res, 7, 0, &first), 0, "count 0 refused");
    CHECK_EQ(CmNetrReserve(&r, &res, 7, CM_NETR_MAX_RESERVE + 1, &first), 0, "18 refused");
    CHECK(!res.Held, "nothing held");
    CHECK_EQ(CmNetrReserve(&r, &res, 7, 17, &first), 17, "17 of 31 free");
    CHECK_EQ(first, 0, "from Head");
    CHECK_EQ(CmNetrReserve(&r, &res, 7, 1, &first), 0, "one at a time");
    CHECK_EQ(CmNetrRelease(&res, 7, 7, 1, 17), CM_NETR_REL_NONE, "another first: no such one");
    CHECK_EQ(CmNetrRelease(&res, 7, 6, 0, 17), CM_NETR_REL_NONE, "another generation's token");
    CHECK(res.Held, "still held");
    CHECK_EQ(CmNetrRelease(&res, 7, 7, 0, 17), CM_NETR_REL_POST, "the current one: post");
    CHECK(!res.Held, "released");
    CHECK_EQ(CmNetrRelease(&res, 7, 7, 0, 17), CM_NETR_REL_NONE, "twice: nothing");

    /* All or nothing against the free count. */
    r.Head = 20;
    r.Tail = 0;
    CHECK_EQ(CmNetrTxFree(&r), 11, "11 free");
    CHECK_EQ(CmNetrReserve(&r, &res, 8, 12, &first), 0, "12 of 11: nothing");
    CHECK(!res.Held, "nothing held");
    CHECK_EQ(CmNetrReserve(&r, &res, 8, 11, &first), 11, "11 of 11");
    CHECK_EQ(first, 20, "from Head");

    /* The service ended (generation 9) between the reserve and the commit:
     * released, nothing posted. */
    CHECK_EQ(CmNetrRelease(&res, 9, 8, 20, 11), CM_NETR_REL_STALE, "stale: released");
    CHECK(!res.Held, "no longer held");
    CHECK_EQ(CmNetrReserve(&r, &res, 9, 1, &first), 1, "the next reserves");
}

int main(void)
{
    test_init();
    test_rx();
    test_tx();
    test_reserve();
    printf("test_cm_net: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
