/*
 * test_quirk.c - host vectors for Linux's xHCI controller quirks, their pure
 * half (src\xhci_quirk.c; roadmap-hcd task 38.10, design record 20): each
 * value's reading and normalization, value 2 offered or read as 1, the
 * match of every listed id with its neighbours and the other vendors' same
 * device numbers refused, VIA's vendor-wide row, the effective policy, the
 * packed word, XhciMsiAvoid's decision with an unknown identity,
 * XhciStreamsOff's reading of HCCPARAMS1, and XhciAsmediaFlowControl's
 * bounded wait for the control byte (task 44.65). The quirks' executors
 * carry their own vectors in test_ring, test_iso, test_link, test_stream,
 * test_tol and test_poll.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_compat.h"
#include "../src/xhci_quirk.h"
#include "test_harness.h"

#define CHECK_TRUE(cond, what) CHECK_EQ((cond) ? 1UL : 0UL, 1UL, what)
#define ID(v, d)    ((((ULONG)(d)) << 16) | (ULONG)(v))

/* Record 20 section 3.1's "Controllers" column, per quirk, 0-terminated. */
static const ULONG msiAvoid[] = { ID(0x1B73, 0x1000), ID(0x1B73, 0x1400), 0 };
static const ULONG streamsOff[] = {
    ID(0x1B73, 0x1000), ID(0x1B73, 0x1009), ID(0x1B6F, 0x7023),
    ID(0x1B6F, 0x7052), ID(0x1106, 0x3432), ID(0x1B21, 0x1042),
    ID(0x17CD, 0x0200), 0
};
static const ULONG resumeReinit[] = {
    ID(0x1022, 0x15E5), ID(0x1B6F, 0x7023), ID(0x1B6F, 0x7052),
    ID(0x1912, 0x0015), ID(0x1DB7, 0xDC27), ID(0x1B21, 0x3042),
    ID(0x1D17, 0x9202), ID(0x1106, 0x3483), ID(0x1106, 0x0001),
    ID(0x1106, 0xFFFE), 0
};
static const ULONG shutToEhci[] = {
    ID(0x8086, 0x1E31), ID(0x8086, 0x9C31), ID(0x8086, 0x9CB1), 0
};
static const ULONG shutReset[] = { ID(0x8086, 0x9C31), ID(0x8086, 0x9CB1), 0 };
static const ULONG haltDelay[] = {
    ID(0x1022, 0x145C), ID(0x1022, 0x15E0), ID(0x1022, 0x15E1),
    ID(0x1022, 0x43BB), 0
};
static const ULONG haltTimeout[] = { ID(0x1B73, 0x1000), 0 };
static const ULONG asmFlow[] = { ID(0x1B21, 0x1142), 0 };
static const ULONG tiCap[] = { ID(0x104C, 0x8241), 0 };
static const ULONG stuckLink[] = {
    ID(0x8086, 0x22B5), ID(0x8086, 0x9D2F), ID(0x8086, 0xA12F),
    ID(0x8086, 0x5AA8), ID(0x8086, 0x19D0), 0
};
static const ULONG ringPad[] = {
    ID(0x1106, 0x3483), ID(0x1D17, 0x9202), ID(0x1D17, 0x9203), 0
};
static const ULONG etron[] = { ID(0x1B6F, 0x7023), ID(0x1B6F, 0x7052), 0 };
static const ULONG amdBit[] = { ID(0x1022, 0x15E5), 0 };

static const ULONG *const lists[XHCI_QK_COUNT] = {
    msiAvoid, streamsOff, resumeReinit, shutToEhci, shutReset, haltDelay,
    haltTimeout, asmFlow, tiCap, stuckLink, ringPad, etron, etron, amdBit
};

/* Every vendor any list names, for the cross-vendor refusals. */
static const ULONG vendors[] = {
    0x1B73, 0x1B6F, 0x1106, 0x1B21, 0x17CD, 0x1022, 0x1912, 0x1DB7, 0x1D17,
    0x8086, 0x104C, 0x1002, 0x1033, 0x1B36, 0
};

static ULONG listed(ULONG q, ULONG id)
{
    const ULONG *p;

    if (q == XHCI_QK_RESUME_REINIT && (id & 0xFFFFUL) == 0x1106UL) {
        return 1;
    }
    for (p = lists[q]; *p != 0; p++) {
        if (*p == id) {
            return 1;
        }
    }
    return 0;
}

static void test_names(void)
{
    ULONG q;

    for (q = 0; q < XHCI_QK_COUNT; q++) {
        CHECK_TRUE(XhciQuirkValueName(q) != NULL, "a value name");
        CHECK_TRUE(XhciQuirkLinuxFlag(q) != NULL, "a Linux flag");
        CHECK_TRUE(XhciQuirkLabel(q) != NULL &&
                       strncmp(XhciQuirkLabel(q), "quirk.", 6) == 0,
                   "a quirk.* label");
    }
    CHECK_TRUE(XhciQuirkValueName(XHCI_QK_COUNT) == NULL, "past the end");
    CHECK_TRUE(strcmp(XhciQuirkValueName(XHCI_QK_MSI_AVOID),
                      "XhciMsiAvoid") == 0, "Q1's name");
    CHECK_TRUE(strcmp(XhciQuirkValueName(XHCI_QK_AMD_BIT),
                      "XhciAmdSuspendBitClear") == 0, "Q16's name");
    CHECK_TRUE(strcmp(XhciQuirkLinuxFlag(XHCI_QK_SHUT_RESET),
                      "XHCI_SPURIOUS_WAKEUP") == 0, "Q5's flag");
}

static void test_modes(void)
{
    ULONG q;
    ULONG norm;
    ULONG def;

    for (q = 0; q < XHCI_QK_COUNT; q++) {
        def = (q == XHCI_QK_SHUT_RESET) ? 0UL : 1UL;
        CHECK_EQ(XhciQuirkDefault(q), def, "the default");
        CHECK_EQ(XhciQuirkMode(q, XHCI_QK_FOUND_NONE, 0, &norm), def,
                 "absent: the default");
        CHECK_EQ(norm, XHCI_QK_NORM_ABSENT, "absent noted");
        CHECK_EQ(XhciQuirkMode(q, XHCI_QK_FOUND_OTHER, 0, &norm), def,
                 "another type: the default");
        CHECK_EQ(norm, XHCI_QK_NORM_TYPE, "type noted");
        CHECK_EQ(XhciQuirkMode(q, XHCI_QK_FOUND_DWORD, 0, &norm), 0,
                 "0 off");
        CHECK_EQ(norm, XHCI_QK_NORM_READ, "0 read");
        CHECK_EQ(XhciQuirkMode(q, XHCI_QK_FOUND_DWORD, 1, &norm), 1, "1");
        CHECK_EQ(norm, XHCI_QK_NORM_READ, "1 read");
        CHECK_EQ(XhciQuirkMode(q, XHCI_QK_FOUND_DWORD, 3, &norm), def,
                 "3: the default");
        CHECK_EQ(norm, XHCI_QK_NORM_NUMBER, "3 noted");
        CHECK_EQ(XhciQuirkMode(q, XHCI_QK_FOUND_DWORD, 0xFFFFFFFFUL, &norm),
                 def, "all ones: the default");
    }
    /* Value 2: every controller on the generic quirks, 1 on the vendor
     * ones (owner, 2026-10-13). */
    for (q = 0; q < XHCI_QK_COUNT; q++) {
        ULONG vendorOnly;

        vendorOnly = (q == XHCI_QK_SHUT_TO_EHCI || q == XHCI_QK_ASM_FLOW ||
                      q == XHCI_QK_AMD_BIT) ? 1UL : 0UL;
        CHECK_EQ(XhciQuirkTwoOffered(q), vendorOnly ? 0UL : 1UL,
                 "2 offered on the generic quirks only");
        CHECK_EQ(XhciQuirkMode(q, XHCI_QK_FOUND_DWORD, 2, &norm),
                 vendorOnly ? XHCI_QK_MODE_LISTED : XHCI_QK_MODE_EVERY,
                 "2's mode");
        CHECK_EQ(norm, vendorOnly ? XHCI_QK_NORM_TWO_AS_ONE
                                  : XHCI_QK_NORM_READ, "2's note");
    }
    CHECK_EQ(XhciQuirkMode(XHCI_QK_COUNT, XHCI_QK_FOUND_DWORD, 1, &norm), 0,
             "an unknown quirk is off");
}

static void test_match(void)
{
    ULONG q;
    ULONG v;
    const ULONG *p;
    ULONG id;
    ULONG dev;

    for (q = 0; q < XHCI_QK_COUNT; q++) {
        for (p = lists[q]; *p != 0; p++) {
            CHECK_EQ(XhciQuirkMatches(q, *p), 1, "a listed id matches");
            /* The neighbours of each listed device, same vendor. */
            dev = *p >> 16;
            id = ((dev + 1UL) & 0xFFFFUL) << 16 | (*p & 0xFFFFUL);
            CHECK_EQ(XhciQuirkMatches(q, id), listed(q, id),
                     "the next device number");
            id = ((dev - 1UL) & 0xFFFFUL) << 16 | (*p & 0xFFFFUL);
            CHECK_EQ(XhciQuirkMatches(q, id), listed(q, id),
                     "the previous device number");
            /* The same device number under every other vendor. */
            for (v = 0; vendors[v] != 0; v++) {
                id = (dev << 16) | vendors[v];
                CHECK_EQ(XhciQuirkMatches(q, id), listed(q, id),
                         "the device number under another vendor");
            }
        }
        CHECK_EQ(XhciQuirkMatches(q, 0), 0, "no identity");
        CHECK_EQ(XhciQuirkMatches(q, 0xFFFFFFFFUL), 0, "all ones");
        CHECK_EQ(XhciQuirkMatches(q, ID(0x1B36, 0x000D)), 0,
                 "QEMU's xHCI is on no list");
    }
    /* VIA vendor-wide for XhciResumeReinit alone. */
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_RESUME_REINIT, ID(0x1106, 0x1234)), 1,
             "every VIA device resumes reinitialized");
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_STREAMS_OFF, ID(0x1106, 0x1234)), 0,
             "VIA is vendor-wide for no other quirk");
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_RING_PAD, ID(0x1106, 0x3432)), 0,
             "3432 is not the VL805");
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_RESUME_REINIT, ID(0x1107, 0x3483)), 0,
             "the next vendor id is not VIA");
    /* The project's machines. */
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_SHUT_TO_EHCI, ID(0x8086, 0x1E31)), 1,
             "B490: Q4");
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_SHUT_RESET, ID(0x8086, 0x1E31)), 0,
             "B490: not Q5");
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_SHUT_RESET, ID(0x8086, 0x9CB1)), 1,
             "E7250: Q5's list");
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_STUCK_LINK, ID(0x8086, 0x9D2F)), 1,
             "E460: Q12");
    CHECK_EQ(XhciQuirkMatches(XHCI_QK_STUCK_LINK, ID(0x8086, 0x02ED)), 0,
             "P14s Gen 1: none");
}

static void test_effective(void)
{
    CHECK_EQ(XhciQuirkEffective(XHCI_QK_MODE_OFF, 1), 0, "0 off, listed");
    CHECK_EQ(XhciQuirkEffective(XHCI_QK_MODE_LISTED, 1), 1, "1 listed");
    CHECK_EQ(XhciQuirkEffective(XHCI_QK_MODE_LISTED, 0), 0, "1 not listed");
    CHECK_EQ(XhciQuirkEffective(XHCI_QK_MODE_EVERY, 0), 1, "2 every");
    CHECK_EQ(XhciQuirkEffective(3, 1), 0, "a raw 3 is not a mode");
}

static void test_state(void)
{
    XHCI_QUIRK_STATE st;
    ULONG w;
    ULONG q;

    XhciQuirkInit(&st);
    for (q = 0; q < XHCI_QK_COUNT; q++) {
        CHECK_EQ(st.Raw[q], XHCI_QK_RAW_NONE, "unread");
        CHECK_EQ(st.Mode[q], XhciQuirkDefault(q), "the default mode");
        CHECK_EQ(XhciQuirkOn(&st, q), 0, "nothing applies before a match");
    }
    CHECK_EQ(st.PciRevision, 0xFFFFFFFFUL, "revision unread");

    /* QEMU's controller: every value absent, nothing applies; value 2 on
     * a generic quirk applies, on a vendor one does not. */
    XhciQuirkSetValue(&st, XHCI_QK_STREAMS_OFF, XHCI_QK_FOUND_DWORD, 2);
    XhciQuirkSetValue(&st, XHCI_QK_ASM_FLOW, XHCI_QK_FOUND_DWORD, 2);
    XhciQuirkSetValue(&st, XHCI_QK_SHUT_RESET, XHCI_QK_FOUND_NONE, 0);
    XhciQuirkSetValue(&st, XHCI_QK_RING_PAD, XHCI_QK_FOUND_DWORD, 0);
    XhciQuirkMatchAll(&st, ID(0x1B36, 0x000D));
    CHECK_EQ(st.Identity, ID(0x1B36, 0x000D), "the identity kept");
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_STREAMS_OFF), 1, "2 on QEMU applies");
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_ASM_FLOW), 0,
             "a vendor quirk's 2 is 1: not on QEMU");
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_SHUT_RESET), 0, "Q5 off by default");
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_RESUME_REINIT), 0, "1, not listed");
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_MSI_AVOID), 0,
             "Q1 not decided by the match");

    w = XhciQuirkPacked(&st, XHCI_QK_STREAMS_OFF);
    CHECK_EQ(XHCI_QK_PACK_MODE(w), XHCI_QK_MODE_EVERY, "packed mode");
    CHECK_EQ(XHCI_QK_PACK_NORM(w), XHCI_QK_NORM_READ, "packed norm");
    CHECK_EQ(w & XHCI_QK_PACK_MATCHED, 0, "packed: not listed");
    CHECK_TRUE((w & XHCI_QK_PACK_EFFECTIVE) != 0, "packed: applies");
    CHECK_EQ(XHCI_QK_PACK_RAW(w), 2, "packed raw");
    w = XhciQuirkPacked(&st, XHCI_QK_ASM_FLOW);
    CHECK_EQ(XHCI_QK_PACK_NORM(w), XHCI_QK_NORM_TWO_AS_ONE, "2 read as 1");
    CHECK_EQ(XHCI_QK_PACK_MODE(w), XHCI_QK_MODE_LISTED, "...as 1");
    w = XhciQuirkPacked(&st, XHCI_QK_SHUT_RESET);
    CHECK_EQ(XHCI_QK_PACK_RAW(w), 0xFFFF, "absent raw");
    CHECK_EQ(XHCI_QK_PACK_NORM(w), XHCI_QK_NORM_ABSENT, "absent norm");

    XhciQuirkOutcome(&st, XHCI_QK_AMD_BIT, XHCI_QK_OUT_SKIPPED,
                     XHCI_QK_WHY_WINDOW_SMALL);
    XhciQuirkSetEffective(&st, XHCI_QK_MSI_AVOID, 1, 0, 1);
    w = XhciQuirkPacked(&st, XHCI_QK_AMD_BIT);
    CHECK_EQ(XHCI_QK_PACK_OUT(w), XHCI_QK_OUT_SKIPPED, "packed outcome");
    CHECK_EQ(XHCI_QK_PACK_WHY(w), XHCI_QK_WHY_WINDOW_SMALL, "packed why");
    w = XhciQuirkPacked(&st, XHCI_QK_MSI_AVOID);
    CHECK_TRUE((w & XHCI_QK_PACK_OVERRIDDEN) != 0, "packed: overridden");
    CHECK_TRUE((w & XHCI_QK_PACK_MATCHED) != 0, "packed: listed");
    CHECK_EQ(w & XHCI_QK_PACK_EFFECTIVE, 0, "packed: not applied");

    /* A listed controller: the E7250, everything at its default. */
    XhciQuirkInit(&st);
    XhciQuirkMatchAll(&st, ID(0x8086, 0x9CB1));
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_SHUT_TO_EHCI), 1, "E7250: Q4 on");
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_SHUT_RESET), 0,
             "E7250: Q5 listed, off by default");
    CHECK_TRUE((XhciQuirkPacked(&st, XHCI_QK_SHUT_RESET) &
                XHCI_QK_PACK_MATCHED) != 0, "E7250: Q5 listed");
    XhciQuirkSetValue(&st, XHCI_QK_SHUT_RESET, XHCI_QK_FOUND_DWORD, 1);
    XhciQuirkMatchAll(&st, ID(0x8086, 0x9CB1));
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_SHUT_RESET), 1, "E7250: Q5 at 1");
    XhciQuirkSetValue(&st, XHCI_QK_SHUT_TO_EHCI, XHCI_QK_FOUND_DWORD, 0);
    XhciQuirkMatchAll(&st, ID(0x8086, 0x9CB1));
    CHECK_EQ(XhciQuirkOn(&st, XHCI_QK_SHUT_TO_EHCI), 0, "E7250: Q4 at 0");

    CHECK_EQ(sizeof(XHCI_QUIRK_STATE) % (2 * sizeof(ULONG)), 0,
             "an even number of ULONGs");
}

static void test_msi_avoid(void)
{
    ULONG fl;

    fl = ID(0x1B73, 0x1000);
    CHECK_EQ(XhciQuirkMsiAvoidDecide(0, 1, fl), 0, "0: never");
    CHECK_EQ(XhciQuirkMsiAvoidDecide(1, 1, fl), 1, "1: the FL1000");
    CHECK_EQ(XhciQuirkMsiAvoidDecide(1, 1, ID(0x1B73, 0x1400)), 1,
             "1: the FL1400");
    CHECK_EQ(XhciQuirkMsiAvoidDecide(1, 1, ID(0x1B73, 0x1009)), 0,
             "1: not the FL1009");
    CHECK_EQ(XhciQuirkMsiAvoidDecide(1, 0, 0), 1,
             "1 with no identity ever read: avoided");
    CHECK_EQ(XhciQuirkMsiAvoidDecide(0, 0, 0), 0,
             "0 with no identity: never");
    CHECK_EQ(XhciQuirkMsiAvoidDecide(2, 1, ID(0x8086, 0x9CB1)), 1,
             "2: every controller");
}

static void test_streams(void)
{
    ULONG hcc1;

    hcc1 = 0x0050F5E5UL;
    CHECK_EQ(XhciQuirkStreamsHcc1(0, hcc1), hcc1, "off: as read");
    CHECK_EQ(XhciQuirkStreamsHcc1(1, hcc1), 0x005005E5UL,
             "on: MaxPSASize cleared, nothing else");
    CHECK_EQ(XhciQuirkStreamsHcc1(1, 0), 0, "a streamless one stays so");
}

/* Q9's wait: a scripted control byte. `script` gives the byte read at each
 * read in turn, the last one repeated once the script runs out; the reads
 * and stalls are counted, and a stall that does not follow a read is
 * counted as out of order (two stalls in a row, or one before any read). */
typedef struct _ASM_FAKE {
    const UCHAR *Script;
    ULONG Length;
    ULONG Reads;
    ULONG Stalls;
    ULONG OutOfOrder;
} ASM_FAKE;

static UCHAR asmRead(PVOID context)
{
    ASM_FAKE *f;
    ULONG i;

    f = (ASM_FAKE *)context;
    i = (f->Reads < f->Length) ? f->Reads : f->Length - 1;
    f->Reads++;
    return f->Script[i];
}

static void asmStall(PVOID context)
{
    ASM_FAKE *f;

    f = (ASM_FAKE *)context;
    f->Stalls++;
    if (f->Stalls != f->Reads) {
        f->OutOfOrder++;
    }
}

static ULONG asmRun(ASM_FAKE *f, const UCHAR *script, ULONG length,
                    ULONG tries)
{
    f->Script = script;
    f->Length = length;
    f->Reads = 0;
    f->Stalls = 0;
    f->OutOfOrder = 0;
    return XhciQuirkAsmWait(asmRead, asmStall, f, 0x02, tries);
}

static void test_asm_wait(void)
{
    static const UCHAR clear[] = { 0x00 };
    static const UCHAR otherBits[] = { 0xFD };
    static const UCHAR afterThree[] = { 0x02, 0x03, 0x06, 0x01 };
    static const UCHAR busy[] = { 0x02 };
    static const UCHAR ff[] = { 0xFF };
    static const UCHAR busyThenFf[] = { 0x02, 0x02, 0xFF, 0x00 };
    static const UCHAR lastTry[] = {
        0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00
    };
    ASM_FAKE f;

    CHECK_EQ(asmRun(&f, clear, 1, 1000), XHCI_QK_WHY_NONE,
             "ready at once");
    CHECK_EQ(f.Reads, 1, "ready at once: one read");
    CHECK_EQ(f.Stalls, 0, "ready at once: no stall");

    CHECK_EQ(asmRun(&f, otherBits, 1, 1000), XHCI_QK_WHY_NONE,
             "only the write bit is read");
    CHECK_EQ(f.Reads, 1, "other bits set: one read");

    CHECK_EQ(asmRun(&f, afterThree, 4, 1000), XHCI_QK_WHY_NONE,
             "ready after three busy reads");
    CHECK_EQ(f.Reads, 4, "ready after three: four reads");
    CHECK_EQ(f.Stalls, 3, "ready after three: a stall after each busy read");
    CHECK_EQ(f.OutOfOrder, 0, "ready after three: each stall after its read");

    CHECK_EQ(asmRun(&f, busy, 1, 1000), XHCI_QK_WHY_BUSY,
             "never ready: the bound's result");
    CHECK_EQ(f.Reads, 1000, "never ready: 1000 reads");
    CHECK_EQ(f.Stalls, 1000, "never ready: 1000 stalls of 50 us");
    CHECK_EQ(f.OutOfOrder, 0, "never ready: read, stall, read, stall");

    CHECK_EQ(asmRun(&f, lastTry, 10, 10), XHCI_QK_WHY_NONE,
             "clear at the last read the bound allows");
    CHECK_EQ(f.Reads, 10, "the last read: ten reads");
    CHECK_EQ(f.Stalls, 9, "the last read: nine stalls");
    CHECK_EQ(asmRun(&f, lastTry, 10, 9), XHCI_QK_WHY_BUSY,
             "clear one read past the bound: busy");
    CHECK_EQ(f.Reads, 9, "one past the bound: never read");
    CHECK_EQ(f.Stalls, 9, "one past the bound: a stall after the last read");

    CHECK_EQ(asmRun(&f, ff, 1, 1000), XHCI_QK_WHY_READ_FF,
             "FFh at once (or a failed read)");
    CHECK_EQ(f.Reads, 1, "FFh at once: one read");
    CHECK_EQ(f.Stalls, 0, "FFh at once: no stall");

    CHECK_EQ(asmRun(&f, busyThenFf, 4, 1000), XHCI_QK_WHY_READ_FF,
             "FFh after two busy reads ends the wait");
    CHECK_EQ(f.Reads, 3, "FFh after two busy: three reads");
    CHECK_EQ(f.Stalls, 2, "FFh after two busy: two stalls");
    CHECK_EQ(f.OutOfOrder, 0, "FFh after two busy: each stall after its read");

    CHECK_EQ(asmRun(&f, clear, 1, 0), XHCI_QK_WHY_BUSY,
             "no tries: busy, nothing read");
    CHECK_EQ(f.Reads, 0, "no tries: no read");
    CHECK_EQ(f.Stalls, 0, "no tries: no stall");
}

int main(void)
{
    test_names();
    test_modes();
    test_match();
    test_effective();
    test_state();
    test_msi_avoid();
    test_streams();
    test_asm_wait();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
