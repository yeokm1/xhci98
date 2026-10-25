/*
 * test_trred.c - XHCITR's red tray icon (xhcitr\tr_red.c; design record 25
 * section 7.2.1, revision 18; roadmap-usb4 task 43.57): each condition
 * alone, holding and then clearing; the states that never count, so a
 * sleep never turns the icon red; one NHI counted once; the ranking; and
 * the tooltip at the shell's two sizes, never longer than its room.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_status.h"
#include "../xhcitr/tr_red.h"
#include "test_harness.h"

#define LIVE_OFF        0UL
#define LIVE_ACTIVE     1UL
#define LIVE_SUSPECT    2UL
#define LIVE_ABSENT     3UL
#define LIVE_RECOVERING 5UL
#define LIVE_RFAILED    6UL
#define LIVE_CONTAINED  7UL
#define LIVE_REVOKED    8UL
#define LIVE_PREPARING  9UL

static TR_SNAP snap;
static TR_U32 stage[TR_SNAP_NHI];
static TR_REDHC hc[4];
static TR_REDDEV dev[4];
static TR_RED_IN in;
static TR_RED r;

static void reset(void)
{
    int i;

    memset(&snap, 0, sizeof(snap));
    snap.Valid = 1;
    snap.NhiCount = 2;
    snap.NhiKey[0] = 0x1111UL;
    snap.NhiKey[1] = 0x2222UL;
    snap.Live[0] = LIVE_ACTIVE;
    snap.Live[1] = LIVE_ACTIVE;
    for (i = 0; i < TR_SNAP_NHI; i++) {
        stage[i] = CM_ST_STAGE_ACTIVE;
    }
    memset(hc, 0, sizeof(hc));
    memset(dev, 0, sizeof(dev));
    memset(&in, 0, sizeof(in));
    in.Snap = &snap;
    in.Stage = stage;
    in.Hc = hc;
    in.HcCount = 0;
    in.Dev = dev;
    in.DevCount = 0;
}

static void router(TR_U32 kind, TR_U32 approval, const char *name)
{
    TR_SROUTER *e = &snap.R[snap.Count++];

    memset(e, 0, sizeof(*e));
    e->Nhi = 0x1111UL;
    e->Kind = kind;
    e->Approval = approval;
    e->Place = (TR_U32)snap.Count;
    strcpy(e->Name, name);
}

static void state(void)
{
    tr_red_state(&in, &r);
}

static void test_nothing(void)
{
    reset();
    state();
    CHECK_EQ(r.Count, 0, "all well: not red");
    CHECK_EQ(r.Kind, TR_RED_NONE, "no kind");
    in.Snap = NULL;
    in.Stage = NULL;
    state();
    CHECK_EQ(r.Count, 0, "no snapshot: not red");
    reset();
    snap.Valid = 0;
    snap.Live[0] = LIVE_ABSENT;
    state();
    CHECK_EQ(r.Count, 0, "an invalid snapshot is not read");
}

static void test_liveness(void)
{
    static const TR_U32 lost[] = { LIVE_SUSPECT, LIVE_ABSENT, 4UL, LIVE_RECOVERING,
                                   LIVE_RFAILED, LIVE_CONTAINED, LIVE_PREPARING };
    static const TR_U32 kept[] = { LIVE_OFF, LIVE_ACTIVE, LIVE_REVOKED, 10UL, 0xFFFFFFFFUL };
    size_t i;

    for (i = 0; i < sizeof(lost) / sizeof(lost[0]); i++) {
        reset();
        snap.Live[1] = lost[i];
        state();
        CHECK_EQ(r.Count, 1, "a loss state is red");
        CHECK_EQ(r.Kind, TR_RED_HC, "as a controller");
        CHECK_EQ(r.Nhi, 1, "an NHI");
        CHECK_EQ(r.Index, 1, "the second NHI");
        snap.Live[1] = LIVE_ACTIVE;
        state();
        CHECK_EQ(r.Count, 0, "back to active: cleared");
    }
    for (i = 0; i < sizeof(kept) / sizeof(kept[0]); i++) {
        reset();
        snap.Live[0] = kept[i];
        snap.Live[1] = kept[i];
        state();
        CHECK_EQ(r.Count, 0, "OFF (a sleep), revoked, unknown: never red");
    }
}

static void test_stage(void)
{
    reset();
    snap.Live[0] = LIVE_OFF;        /* a failed resume leaves it OFF */
    stage[0] = CM_ST_STAGE_FAILED;
    state();
    CHECK_EQ(r.Count, 1, "a failed stage is red");
    CHECK_EQ(r.Nhi, 1, "the NHI");
    CHECK_EQ(r.Index, 0, "the first NHI");
    snap.Live[0] = LIVE_RFAILED;
    state();
    CHECK_EQ(r.Count, 1, "lost and failed at once: counted once");
    stage[0] = CM_ST_STAGE_ACTIVE;
    snap.Live[0] = LIVE_ACTIVE;
    state();
    CHECK_EQ(r.Count, 0, "back to active: cleared");
    stage[0] = CM_ST_STAGE_INERT;
    stage[1] = CM_ST_STAGE_STOPPED;
    state();
    CHECK_EQ(r.Count, 0, "inert and stopped: not red");
    stage[0] = CM_ST_STAGE_FAILED;
    in.Stage = NULL;
    state();
    CHECK_EQ(r.Count, 0, "no stage read: the stage is not counted");

    /* As the tool wires it: the snapshot's own stage (TR_SNAP.Stage). */
    reset();
    snap.Stage[1] = CM_ST_STAGE_FAILED;
    in.Stage = snap.Stage;
    state();
    CHECK_EQ(r.Count, 1, "the snapshot's failed stage is red");
    CHECK_EQ(r.Index, 1, "the second NHI");
}

/* The liveness test is the "stopped answering" balloon's (tr_live_lost):
 * for every value, red exactly when that test says lost. */
static void test_live_same(void)
{
    TR_U32 v;

    for (v = 0; v < 16; v++) {
        reset();
        snap.Live[0] = v;
        state();
        CHECK_EQ(r.Count, tr_live_lost(v) ? 1 : 0, "red as the balloon's test");
    }
}

static void test_hc(void)
{
    reset();
    strcpy(hc[0].Name, "Intel USB 3.0 eXtensible Host Controller");
    strcpy(hc[1].Name, "Second");
    in.HcCount = 2;
    state();
    CHECK_EQ(r.Count, 0, "two controllers running");
    hc[1].Failed = 1;
    state();
    CHECK_EQ(r.Count, 1, "one failed");
    CHECK_EQ(r.Kind, TR_RED_HC, "a controller");
    CHECK_EQ(r.Nhi, 0, "an xhci98 controller");
    CHECK_EQ(r.Index, 1, "the second");
    hc[1].Failed = 0;
    state();
    CHECK_EQ(r.Count, 0, "the flag cleared: cleared");
}

static void test_wait(void)
{
    reset();
    router(CM_ST_KIND_ICM, CM_ST_APPR_APPROVED, "Approved Dock");
    router(CM_ST_KIND_ICM, CM_ST_APPR_FAILED, "Failed Dock");
    router(CM_ST_KIND_ICM, CM_ST_APPR_REJECTED, "Rejected Dock");
    router(CM_ST_KIND_USB4_PORT, CM_ST_APPR_HELD, "USB4 Held");
    router(CM_ST_KIND_USB4_LINK, CM_ST_APPR_PENDING, "USB4 Pending");
    router(CM_ST_KIND_ICM, CM_ST_APPR_BOOT, "Boot Dock");
    state();
    CHECK_EQ(r.Count, 0, "approved, failed, rejected, USB4 held/pending, boot: not red");
    router(CM_ST_KIND_ICM, CM_ST_APPR_PENDING, "Pending Dock");
    router(CM_ST_KIND_ICM, CM_ST_APPR_UNANSWERED, "Unanswered Dock");
    state();
    CHECK_EQ(r.Count, 2, "pending and unanswered: red");
    CHECK_EQ(r.Kind, TR_RED_WAIT, "waiting");
    CHECK_EQ(r.Index, 6, "the first waiting entry");
    snap.R[6].Approval = CM_ST_APPR_APPROVED;
    snap.R[7].Approval = CM_ST_APPR_APPROVED;
    state();
    CHECK_EQ(r.Count, 0, "approved: cleared");
    snap.R[7].Approval = CM_ST_APPR_PENDING;
    snap.Count = 7;                 /* gone from the snapshot: cleared */
    state();
    CHECK_EQ(r.Count, 0, "unplugged: cleared");
}

static void test_slow(void)
{
    reset();
    dev[0].Kind = TR_RED_SLOW2;
    strcpy(dev[0].Name, "HS stick");
    dev[1].Kind = 99;
    strcpy(dev[1].Name, "unknown kind");
    dev[2].Kind = TR_RED_SLOW3;
    strcpy(dev[2].Name, "(E:) SS stick");
    in.DevCount = 2;
    state();
    CHECK_EQ(r.Count, 1, "a slowed High Speed device; an unknown kind not");
    CHECK_EQ(r.Kind, TR_RED_SLOW2, "slow 2");
    CHECK_EQ(r.Index, 0, "the first device");
    in.DevCount = 3;
    state();
    CHECK_EQ(r.Count, 2, "both");
    CHECK_EQ(r.Kind, TR_RED_SLOW3, "USB 3 below its speed ranks first");
    CHECK_EQ(r.Index, 2, "the third entry");
    in.DevCount = 0;
    state();
    CHECK_EQ(r.Count, 0, "the set emptied: cleared");
}

static void test_rank(void)
{
    reset();
    dev[0].Kind = TR_RED_SLOW3;
    strcpy(dev[0].Name, "SS stick");
    in.DevCount = 1;
    router(CM_ST_KIND_ICM, CM_ST_APPR_PENDING, "Dock");
    hc[0].Failed = 1;
    strcpy(hc[0].Name, "USB controller");
    in.HcCount = 1;
    snap.Live[1] = LIVE_ABSENT;
    state();
    CHECK_EQ(r.Count, 4, "every kind at once");
    CHECK_EQ(r.Kind, TR_RED_HC, "a controller first");
    CHECK_EQ(r.Nhi, 1, "an NHI before an xhci98 controller");
    snap.Live[1] = LIVE_ACTIVE;
    state();
    CHECK_EQ(r.Count, 3, "three");
    CHECK_EQ(r.Nhi, 0, "then the xhci98 controller");
    hc[0].Failed = 0;
    state();
    CHECK_EQ(r.Kind, TR_RED_WAIT, "then waiting");
    snap.Count = 0;
    state();
    CHECK_EQ(r.Kind, TR_RED_SLOW3, "then slow");
    CHECK_EQ(r.Count, 1, "one");
}

static void tip_check(const TR_RED *rr, int prefix, size_t cap, const char *want)
{
    char out[256];

    memset(out, 'Z', sizeof(out));
    tr_red_tip(&in, rr, prefix, out, cap);
    CHECK(strlen(out) <= cap - 1, "within the room");
    if (want != NULL) {
        CHECK(strcmp(out, want) == 0, want);
        if (strcmp(out, want) != 0) {
            printf("    got \"%s\"\n", out);
        }
    }
}

static void test_tip(void)
{
    TR_RED t;
    int n;
    size_t caps[2];
    int c;

    caps[0] = 64;
    caps[1] = 128;
    reset();
    t.Count = 0;
    t.Kind = TR_RED_NONE;
    t.Index = -1;
    t.Nhi = 0;
    tip_check(&t, 1, 128, "");

    snap.Live[0] = LIVE_ABSENT;
    state();
    tip_check(&r, 0, 64, "Thunderbolt/USB4 controller stopped answering");
    tip_check(&r, 1, 128, "xHCI98 USB Tree: Thunderbolt/USB4 controller stopped answering");
    t = r;
    t.Count = 10000;
    tip_check(&t, 0, 64, "Thunderbolt/USB4 controller stopped answering and 9999 more");

    reset();
    strcpy(hc[0].Name, "Renesas USB 3.0 eXtensible Host Controller");
    hc[0].Failed = 1;
    in.HcCount = 1;
    state();
    tip_check(&r, 0, 64, "Renesas USB 3.0 eXtensible Host Controller stopped answering");
    tip_check(&r, 1, 64, NULL);
    hc[0].Name[0] = '\0';
    tip_check(&r, 0, 64, "A USB controller stopped answering");

    reset();
    router(CM_ST_KIND_ICM, CM_ST_APPR_PENDING, "Lenovo ThinkPad Thunderbolt 3 Dock");
    state();
    tip_check(&r, 0, 64, "Lenovo ThinkPad Thunderbolt 3 Dock is waiting to be approved");
    dev[0].Kind = TR_RED_SLOW3;
    strcpy(dev[0].Name, "(E:) SanDisk Extreme");
    in.DevCount = 1;
    state();
    tip_check(&r, 0, 64, "Lenovo ThinkPad Thunder... is waiting to be approved and 1 more");
    tip_check(&r, 1, 128,
              "xHCI98 USB Tree: Lenovo ThinkPad Thunderbolt 3 Dock is waiting to be approved and 1 more");

    reset();
    dev[0].Kind = TR_RED_SLOW3;
    strcpy(dev[0].Name, "(E:) SanDisk Extreme");
    dev[1].Kind = TR_RED_SLOW2;
    strcpy(dev[1].Name, "Kingston DataTraveler 2.0");
    in.DevCount = 2;
    state();
    tip_check(&r, 0, 64, "(E:) SanDisk Extreme runs below USB 3 speed and 1 more");
    in.DevCount = 1;
    dev[0] = dev[1];
    state();
    tip_check(&r, 1, 64, "xHCI98 USB Tree: Kingston DataTra... is slowed by a USB 1.1 hub");
    tip_check(&r, 0, 64, "Kingston DataTraveler 2.0 is slowed by a USB 1.1 hub");

    /* A name as long as a name gets, every count, both rooms: never past
     * the room, the suffix always whole. */
    reset();
    memset(dev[0].Name, 'N', TR_NAME_CHARS - 1);
    dev[0].Name[TR_NAME_CHARS - 1] = '\0';
    dev[0].Kind = TR_RED_SLOW2;
    in.DevCount = 1;
    state();
    for (c = 0; c < 2; c++) {
        for (n = 1; n <= 10000; n *= 10) {
            char out[256];
            char suf[32];
            t = r;
            t.Count = n;
            tr_red_tip(&in, &t, c, out, caps[c]);
            CHECK(strlen(out) <= caps[c] - 1, "a long name within the room");
            if (n > 1) {
                sprintf(suf, " and %d more", n - 1);
                CHECK(strlen(out) >= strlen(suf) &&
                      strcmp(out + strlen(out) - strlen(suf), suf) == 0,
                      "the suffix whole");
            }
            CHECK((strstr(out, "...") != NULL) ==
                  (strlen(TR_RED_PREFIX) * c + 27 + strlen(n > 1 ? suf : "") +
                   TR_NAME_CHARS - 1 > caps[c] - 1),
                  "the name cut only when it does not fit");
            CHECK(strstr(out, "is slowed by a USB 1.1 hub") != NULL, "the words kept");
        }
    }
    /* A room too small for a name: the generic word. */
    t = r;
    t.Count = 2;
    {
        char out[40];
        tr_red_tip(&in, &t, 0, out, 40);
        CHECK(strcmp(out, "A device is slowed by a USB 1.1 hub and") == 0,
              "no room for a name: the generic word, cut at the room");
    }
    {
        char out[1];
        out[0] = 'X';
        tr_red_tip(&in, &t, 0, out, 1);
        CHECK_EQ(out[0], 0, "a room of one is empty");
    }
}

int main(void)
{
    test_nothing();
    test_liveness();
    test_stage();
    test_live_same();
    test_hc();
    test_wait();
    test_slow();
    test_rank();
    test_tip();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
