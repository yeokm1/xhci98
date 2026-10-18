/*
 * test_regsrc.c - host vectors for where a registry value is taken from
 * (src\xhci_regsrc.c; roadmap-usb4.md task 42-A.17, design record 13
 * section 5.5): absent, per-device only, machine-wide only, both, and a
 * value of another type in either key. Every expectation was worked by
 * hand from the owner's rule of 2026-10-18 - the machine-wide value wins
 * when present - not from the code under test.
 */

#include <stdio.h>
#include "../src/xhci_regsrc.h"
#include "test_harness.h"

static void test_state(void)
{
    CHECK_EQ(XhciRegState(0, 0), XHCI_REG_ABSENT, "not seen: absent");
    CHECK_EQ(XhciRegState(1, 0), XHCI_REG_OTHER,
             "seen and refused: another type");
    CHECK_EQ(XhciRegState(1, 1), XHCI_REG_TAKEN, "seen and taken");
    CHECK_EQ(XhciRegState(0, 1), XHCI_REG_TAKEN,
             "taken is taken, whatever seen says");
    CHECK_EQ(XhciRegState(7, 0), XHCI_REG_OTHER, "any nonzero seen");
}

static void test_pick(void)
{
    /* Absent. */
    CHECK_EQ(XhciRegPick(XHCI_REG_ABSENT, XHCI_REG_ABSENT),
             XHCI_REG_FROM_NONE, "absent from both: the default");
    CHECK_EQ(XhciRegPickedState(XHCI_REG_ABSENT, XHCI_REG_ABSENT),
             XHCI_REG_ABSENT, "and absent stands");

    /* Per-device only: today's reading. */
    CHECK_EQ(XhciRegPick(XHCI_REG_ABSENT, XHCI_REG_TAKEN),
             XHCI_REG_FROM_DEVICE, "per-device only: the driver key");
    CHECK_EQ(XhciRegPickedState(XHCI_REG_ABSENT, XHCI_REG_TAKEN),
             XHCI_REG_TAKEN, "its DWORD stands");
    CHECK_EQ(XhciRegPick(XHCI_REG_ABSENT, XHCI_REG_OTHER),
             XHCI_REG_FROM_DEVICE, "per-device of another type");
    CHECK_EQ(XhciRegPickedState(XHCI_REG_ABSENT, XHCI_REG_OTHER),
             XHCI_REG_OTHER, "its refusal stands, as before");

    /* Machine-wide only. */
    CHECK_EQ(XhciRegPick(XHCI_REG_TAKEN, XHCI_REG_ABSENT),
             XHCI_REG_FROM_MACHINE, "machine-wide only");
    CHECK_EQ(XhciRegPickedState(XHCI_REG_TAKEN, XHCI_REG_ABSENT),
             XHCI_REG_TAKEN, "its DWORD stands");

    /* Both: the machine-wide value wins. */
    CHECK_EQ(XhciRegPick(XHCI_REG_TAKEN, XHCI_REG_TAKEN),
             XHCI_REG_FROM_MACHINE, "both: machine-wide wins");
    CHECK_EQ(XhciRegPick(XHCI_REG_TAKEN, XHCI_REG_OTHER),
             XHCI_REG_FROM_MACHINE, "over a per-device of another type");
    CHECK_EQ(XhciRegPickedState(XHCI_REG_TAKEN, XHCI_REG_OTHER),
             XHCI_REG_TAKEN, "its DWORD stands");

    /* Present machine-wide but of another type still wins: the default
     * stands, not the per-device value. */
    CHECK_EQ(XhciRegPick(XHCI_REG_OTHER, XHCI_REG_TAKEN),
             XHCI_REG_FROM_MACHINE, "machine-wide of another type wins");
    CHECK_EQ(XhciRegPickedState(XHCI_REG_OTHER, XHCI_REG_TAKEN),
             XHCI_REG_OTHER, "so its refusal stands");
    CHECK_EQ(XhciRegPick(XHCI_REG_OTHER, XHCI_REG_ABSENT),
             XHCI_REG_FROM_MACHINE, "machine-wide of another type alone");

    /* A state outside the three is absent. */
    CHECK_EQ(XhciRegPick(3, XHCI_REG_TAKEN), XHCI_REG_FROM_DEVICE,
             "an unknown machine state is absent");
    CHECK_EQ(XhciRegPick(0xFFFFFFFFUL, 0xFFFFFFFFUL), XHCI_REG_FROM_NONE,
             "unknown in both");
    CHECK_EQ(XhciRegPickedState(XHCI_REG_ABSENT, 9), XHCI_REG_ABSENT,
             "an unknown device state is absent");
}

int main(void)
{
    test_state();
    test_pick();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
