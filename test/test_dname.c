/*
 * test_dname.c - host vectors for the controller's \DosDevices name
 * (src\xhci_dname.c; roadmap-usb4 task 44.81; design record 13 section
 * 8.8): the form per system, the driver-wide record of numbers held, what
 * a probe's status means and when a delete gives a number back.
 */

#include <stdio.h>
#include "../src/xhci_dname.h"
#include "test_harness.h"

static void test_form(void)
{
    CHECK_EQ(XhciDnameForm(0), XHCI_DNAME_FORM_HCD,
             "98 SE and ME keep HCD<n>");
    CHECK_EQ(XhciDnameForm(1), XHCI_DNAME_FORM_PROJECT,
             "NT takes XHCI98HC<k>");
    CHECK_EQ(XhciDnameForm(7), XHCI_DNAME_FORM_PROJECT,
             "any nonzero answer is NT");
    CHECK_EQ(XhciDnameAlias(0, 0), 0UL, "98 SE and ME: no alias");
    CHECK_EQ(XhciDnameAlias(1, 0), 1UL, "2000, XP, XP x64: the alias");
    CHECK_EQ(XhciDnameAlias(1, 1), 0UL, "Vista and later: no alias");
    CHECK_EQ(XhciDnameAlias(0, 1), 0UL, "an impossible pair: none");
}

static void test_claim(void)
{
    ULONG taken[XHCI_DNAME_WORDS];
    ULONG n;
    ULONG i;

    XhciDnameInit(taken);
    for (i = 0; i < XHCI_DNAME_MAX; i++) {
        CHECK_EQ(XhciDnameHeld(taken, i), 0UL, "all free at the start");
    }
    CHECK_EQ(XhciDnameClaim(taken, 0), 0UL, "the first controller: 0");
    CHECK_EQ(XhciDnameClaim(taken, 0), 1UL,
             "a second never tries the first one's number");
    CHECK_EQ(XhciDnameHeld(taken, 0), 1UL, "0 held");
    CHECK_EQ(XhciDnameHeld(taken, 1), 1UL, "1 held");
    XhciDnameRelease(taken, 0);
    CHECK_EQ(XhciDnameHeld(taken, 0), 0UL, "0 given back");
    CHECK_EQ(XhciDnameClaim(taken, 0), 0UL, "a restart takes 0 again");
    /* A probe or create that failed at 2: the caller gives 2 back and
     * claims from 3, past a number held by its own record. */
    CHECK_EQ(XhciDnameClaim(taken, 2), 2UL, "2");
    XhciDnameRelease(taken, 2);
    CHECK_EQ(XhciDnameClaim(taken, 3), 3UL, "the next after a refusal");
    CHECK_EQ(XhciDnameHeld(taken, 2), 0UL, "the refused number not held");
    /* Across the word boundary. */
    XhciDnameInit(taken);
    for (i = 0; i < 31; i++) {
        (VOID)XhciDnameClaim(taken, 0);
    }
    CHECK_EQ(XhciDnameClaim(taken, 0), 31UL, "31, the first word's last");
    CHECK_EQ(XhciDnameClaim(taken, 0), 32UL, "32, the second word's first");
    CHECK_EQ(XhciDnameHeld(taken, 32), 1UL, "32 held");
    XhciDnameRelease(taken, 32);
    CHECK_EQ(XhciDnameHeld(taken, 31), 1UL, "31 untouched by 32's release");
    /* Exhausted: bounded, never a loop. */
    XhciDnameInit(taken);
    for (i = 0; i < XHCI_DNAME_MAX; i++) {
        n = XhciDnameClaim(taken, 0);
        CHECK_EQ(n, i, "in order");
    }
    CHECK_EQ(XhciDnameClaim(taken, 0), XHCI_DNAME_MAX, "none left");
    CHECK_EQ(XhciDnameClaim(taken, XHCI_DNAME_MAX), XHCI_DNAME_MAX,
             "from past the end");
    CHECK_EQ(XhciDnameClaim(taken, 0xFFFFFFFFUL), XHCI_DNAME_MAX,
             "from far past the end");
    XhciDnameRelease(taken, XHCI_DNAME_MAX);
    XhciDnameRelease(taken, 0xFFFFFFFFUL);
    CHECK_EQ(XhciDnameHeld(taken, XHCI_DNAME_MAX), 0UL,
             "out of range reads free");
    CHECK_EQ(XhciDnameHeld(taken, XHCI_DNAME_MAX - 1), 1UL,
             "an out-of-range release touches nothing");
}

static void test_claim_down(void)
{
    ULONG taken[XHCI_DNAME_WORDS];
    ULONG i;

    XhciDnameInit(taken);
    CHECK_EQ(XhciDnameClaimDown(taken, XHCI_DNAME_ALIAS_TOP), 9UL,
             "the first alias: HCD9");
    CHECK_EQ(XhciDnameClaimDown(taken, XHCI_DNAME_ALIAS_TOP), 8UL,
             "a second controller's: HCD8");
    /* A probe that found 7 taken (usbport's): given back, then down. */
    CHECK_EQ(XhciDnameClaimDown(taken, XHCI_DNAME_ALIAS_TOP), 7UL, "7");
    XhciDnameRelease(taken, 7);
    CHECK_EQ(XhciDnameClaimDown(taken, 6), 6UL, "past the refused 7");
    CHECK_EQ(XhciDnameHeld(taken, 7), 0UL, "7 not held");
    XhciDnameRelease(taken, 9);
    CHECK_EQ(XhciDnameClaimDown(taken, XHCI_DNAME_ALIAS_TOP), 9UL,
             "a restart takes 9 again");
    /* Exhausted: bounded. */
    XhciDnameInit(taken);
    for (i = 0; i <= XHCI_DNAME_ALIAS_TOP; i++) {
        CHECK_EQ(XhciDnameClaimDown(taken, XHCI_DNAME_ALIAS_TOP),
                 XHCI_DNAME_ALIAS_TOP - i, "in order, down");
    }
    CHECK_EQ(XhciDnameClaimDown(taken, XHCI_DNAME_ALIAS_TOP),
             XHCI_DNAME_MAX, "none left in 0 to 9");
    CHECK_EQ(XhciDnameHeld(taken, 10), 0UL, "nothing above the top held");
    CHECK_EQ(XhciDnameClaimDown(taken, 0), XHCI_DNAME_MAX, "0 held: none");
    /* A top past the record starts at its last number. */
    XhciDnameInit(taken);
    CHECK_EQ(XhciDnameClaimDown(taken, 0xFFFFFFFFUL), XHCI_DNAME_MAX - 1,
             "top clamped");
    CHECK_EQ(XhciDnameClaimDown(taken, 31), 31UL, "a word's last");
    CHECK_EQ(XhciDnameClaimDown(taken, 32), 32UL, "the next word's first");
}

static void test_probe(void)
{
    CHECK_EQ(XhciDnameProbeFree(0xC0000034UL), 1UL,
             "STATUS_OBJECT_NAME_NOT_FOUND: free");
    CHECK_EQ(XhciDnameProbeFree(0xC000003AUL), 1UL,
             "STATUS_OBJECT_PATH_NOT_FOUND: free");
    CHECK_EQ(XhciDnameProbeFree(0x00000000UL), 0UL,
             "it opened: something holds it");
    CHECK_EQ(XhciDnameProbeFree(0xC0000022UL), 0UL,
             "STATUS_ACCESS_DENIED: held");
    CHECK_EQ(XhciDnameProbeFree(0xC0000043UL), 0UL,
             "STATUS_SHARING_VIOLATION: held");
    CHECK_EQ(XhciDnameProbeFree(0xC0000056UL), 0UL,
             "STATUS_DELETE_PENDING: held");
    CHECK_EQ(XhciDnameProbeFree(0xC0000033UL), 0UL,
             "STATUS_OBJECT_NAME_INVALID: not taken for free");
    CHECK_EQ(XhciDnameProbeFree(0xC0000035UL), 0UL,
             "STATUS_OBJECT_NAME_COLLISION: held");
}

static void test_delete(void)
{
    CHECK_EQ(XhciDnameDeleteReleases(0x00000000UL), 1UL, "deleted");
    CHECK_EQ(XhciDnameDeleteReleases(0xC0000034UL), 1UL, "already gone");
    CHECK_EQ(XhciDnameDeleteReleases(0xC0000022UL), 0UL,
             "refused: the number stays held");
    CHECK_EQ(XhciDnameDeleteReleases(0xC000009AUL), 0UL,
             "no resources: stays held");
}

int main(void)
{
    test_form();
    test_claim();
    test_claim_down();
    test_probe();
    test_delete();
    printf("test_dname: %d checks, %d failures\n", checks, failures);
    return failures;
}
