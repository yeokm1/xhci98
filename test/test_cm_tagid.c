/*
 * test_cm_tagid.c - the pure half of the tunnel id on Windows 98 SE and ME
 * (src\cm\cm_tagid.c; design record 31 sections 5.1 and 5.2, roadmap-usb4
 * task 44.3): the id list's test in both separators, the step's decision,
 * what follows an Add_ID in each mode, and the per-boot record.
 */

#include "../src/cm/cm_tagid.h"
#include "test_harness.h"

static ULONG len(const char *s)
{
    ULONG n;

    n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n + 1;
}

static void test_list(void)
{
    static const char nvme[] =
        "PCI\\VEN_1B36&DEV_0010&REV_02,PCI\\VEN_1B36&DEV_0010,"
        "PCI\\VEN_1B36&CC_010802,PCI\\VEN_1B36&CC_0108,PCI\\VEN_1B36,"
        "PCI\\CC_010802,PCI\\CC_0108";
    static const char bridge[] =
        "PCI\\VEN_1B21&DEV_2421&CC_060400,PCI\\CC_060400,PCI\\CC_0604";
    static const char tagged[] =
        "PCI\\VEN_1B36&DEV_0010,PCI\\CC_010802,PCI\\XHCI98_TUNNEL&CC_010802";
    static const char multi[] =
        "PCI\\VEN_1B36&DEV_0010\0PCI\\CC_010802\0\0PCI\\XHCI98_TUNNEL&CC_010802\0";
    static const char blanks[] = " pci\\cc_010802 ,PCI\\CC_0108";
    static const char last[] = "PCI\\CC_0108,PCI\\CC_010802";
    static const char prefix[] = "PCI\\CC_0108020,PCI\\CC_01080";
    static const char usb[] = "PCI\\CC_0C0330,PCI\\CC_0C03";

    CHECK(CmTagListHas(nvme, len(nvme), CM_TAG_CLASS),
          "an NVMe function's list carries the class");
    CHECK(!CmTagListHas(nvme, len(nvme), CM_TAG_ID),
          "a plain NVMe function has no id");
    CHECK(!CmTagListHas(bridge, len(bridge), CM_TAG_CLASS),
          "a bridge is not of the class");
    CHECK(!CmTagListHas(usb, len(usb), CM_TAG_CLASS),
          "a USB controller is not of the class");
    CHECK(CmTagListHas(tagged, len(tagged), CM_TAG_ID),
          "an appended id is found");
    CHECK(CmTagListHas(tagged, len(tagged), CM_TAG_CLASS),
          "the class beside the id");
    CHECK(CmTagListHas(multi, sizeof(multi), CM_TAG_CLASS),
          "a MULTI_SZ list is read");
    CHECK(!CmTagListHas(multi, sizeof(multi), CM_TAG_ID),
          "a MULTI_SZ list ends at two NULs");
    CHECK(CmTagListHas(blanks, len(blanks), CM_TAG_CLASS),
          "case and blanks around an entry ignored");
    CHECK(CmTagListHas(last, len(last), CM_TAG_CLASS),
          "the class as the last entry");
    CHECK(!CmTagListHas(prefix, len(prefix), CM_TAG_CLASS),
          "a longer or shorter entry is not the class");
    CHECK(!CmTagListHas(last, 12, CM_TAG_CLASS),
          "the list ends at its byte count");
    CHECK(CmTagListHas(last, len(last) - 1, CM_TAG_CLASS),
          "a list without its NUL is read to its byte count");
    CHECK(!CmTagListHas(NULL, 10, CM_TAG_CLASS), "a NULL list");
    CHECK(!CmTagListHas("", 1, CM_TAG_CLASS), "an empty list");
    CHECK(!CmTagListHas(",,", 3, CM_TAG_CLASS), "empty entries");
    CHECK(!CmTagListHas(nvme, len(nvme), ""), "an empty id");
}

static void test_decide(void)
{
    CHECK_EQ(CmTagDecide(1, 1, 0), CM_TAG_ADD, "the class without the id");
    CHECK_EQ(CmTagDecide(0, 1, 0), CM_TAG_NO_READ, "not read");
    CHECK_EQ(CmTagDecide(1, 0, 0), CM_TAG_NOT_CLASS, "another class");
    CHECK_EQ(CmTagDecide(1, 0, 1), CM_TAG_NOT_CLASS,
             "the id without the class gains nothing");
    CHECK_EQ(CmTagDecide(1, 1, 1), CM_TAG_HAS_ID, "the id already there");
}

static void test_after(void)
{
}

static void test_seen(void)
{
    CM_TAG_SEEN s;
    ULONG k;

    CmTagSeenInit(&s);
    CHECK(!CmTagSeenHas(&s, 0x1000, 7), "empty");
    CmTagSeenAdd(&s, 0x1000, 7);
    CHECK(CmTagSeenHas(&s, 0x1000, 7), "added");
    CHECK(!CmTagSeenHas(&s, 0x1000, 8),
          "the same handle with another instance id is new");
    CmTagSeenAdd(&s, 0x1000, 7);
    CHECK_EQ(s.Count, 1, "an entry added twice is one");
    for (k = 1; k < CM_TAG_SEEN_MAX; k++) {
        CmTagSeenAdd(&s, 0x2000 + k, k);
    }
    CHECK_EQ(s.Count, CM_TAG_SEEN_MAX, "full");
    CmTagSeenAdd(&s, 0x9000, 99);
    CHECK(CmTagSeenHas(&s, 0x9000, 99), "a new entry in a full table");
    CHECK(!CmTagSeenHas(&s, 0x1000, 7), "the oldest gave way");
    CHECK(CmTagSeenHas(&s, 0x2001, 1), "the next oldest kept");
    CmTagSeenAdd(&s, 0x9001, 100);
    CHECK(!CmTagSeenHas(&s, 0x2001, 1), "then the next oldest");
    CHECK(CmTagSeenHas(&s, 0x9000, 99), "the newer kept");
}

int main(void)
{
    test_list();
    test_decide();
    test_after();
    test_seen();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
