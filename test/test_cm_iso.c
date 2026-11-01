/*
 * test_cm_iso.c - the isolation statement's decision (src/cm/cm_iso.c;
 * design record 22 section 10.5, roadmap-usb4.md task 45.35): the one yes,
 * every condition refused alone in the order the record lists them, and
 * every combination's verdict the first failing condition's.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_iso.h"
#include "test_harness.h"

static void yesInput(CM_ISO_IN *in)
{
    memset(in, 0, sizeof(*in));
    in->Bound = 1;
    in->Gone = 0;
    in->DomainListed = 1;
    in->StartsSame = 1;
    in->U4On = 1;
    in->StageActive = 1;
    in->LiveActive = 1;
    in->LossDrain = 0;
    in->Corresponds = 1;
    in->Isolated = 1;
}

/* The verdict the record's order gives for a 10-bit pattern: bit k set
 * means condition k is spoiled (Bound, Gone, DomainListed, StartsSame,
 * U4On, StageActive, LiveActive, LossDrain, Corresponds, Isolated). */
static ULONG expected(ULONG bits)
{
    if (bits & 0x001UL) {
        return CM_ISO_UNBOUND;
    }
    if (bits & 0x002UL) {
        return CM_ISO_GONE;
    }
    if (bits & 0x00CUL) {
        return CM_ISO_DOMAIN;
    }
    if (bits & 0x030UL) {
        return CM_ISO_PROFILE;
    }
    if (bits & 0x0C0UL) {
        return CM_ISO_LOST;
    }
    if (bits & 0x100UL) {
        return CM_ISO_MAP;
    }
    if (bits & 0x200UL) {
        return CM_ISO_OPEN;
    }
    return CM_ISO_YES;
}

static void spoil(CM_ISO_IN *in, ULONG bits)
{
    yesInput(in);
    if (bits & 0x001UL) in->Bound = 0;
    if (bits & 0x002UL) in->Gone = 1;
    if (bits & 0x004UL) in->DomainListed = 0;
    if (bits & 0x008UL) in->StartsSame = 0;
    if (bits & 0x010UL) in->U4On = 0;
    if (bits & 0x020UL) in->StageActive = 0;
    if (bits & 0x040UL) in->LiveActive = 0;
    if (bits & 0x080UL) in->LossDrain = 1;
    if (bits & 0x100UL) in->Corresponds = 0;
    if (bits & 0x200UL) in->Isolated = 0;
}

static void test_single(void)
{
    CM_ISO_IN in;

    yesInput(&in);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_YES, "every condition held: yes");
    CHECK_EQ(CmIsoDecide(NULL), CM_ISO_UNBOUND, "no input: unbound");
    spoil(&in, 0x001UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_UNBOUND, "no binding");
    spoil(&in, 0x002UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_GONE, "the child being removed");
    spoil(&in, 0x004UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_DOMAIN, "the domain not listed");
    spoil(&in, 0x008UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_DOMAIN, "the domain restarted");
    spoil(&in, 0x010UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_PROFILE, "not the USB4 software CM");
    spoil(&in, 0x020UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_PROFILE, "the domain not started");
    spoil(&in, 0x040UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_LOST, "the host interface lost");
    spoil(&in, 0x080UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_LOST, "a loss still draining");
    spoil(&in, 0x100UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_MAP, "the port's correspondence changed");
    spoil(&in, 0x200UL);
    CHECK_EQ(CmIsoDecide(&in), CM_ISO_OPEN, "the path not read back closed");
}

static void test_all(void)
{
    CM_ISO_IN in;
    ULONG bits;
    ULONG yes;
    char what[64];

    yes = 0;
    for (bits = 0; bits < 0x400UL; bits++) {
        spoil(&in, bits);
        sprintf(what, "pattern %03lx: the first failing condition",
                (unsigned long)bits);
        CHECK_EQ(CmIsoDecide(&in), expected(bits), what);
        if (CmIsoDecide(&in) == CM_ISO_YES) {
            yes++;
        }
    }
    CHECK_EQ(yes, 1, "exactly one of the 1024 patterns is a yes");
}

int main(void)
{
    test_single();
    test_all();
    printf("test_cm_iso: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
