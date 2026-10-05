/*
 * test_psw.c - host vectors for the Intel port switchover's pure half
 * (src\xhci_psw.c; roadmap-hcd.md task 34.4): the device-id gate, the
 * board exemption, the XhciIntelPortSwitch rule, and the route and release
 * over a modelled configuration space - the order of the writes, what is
 * written, and where each refusal stops.
 */

#include <stdio.h>
#include "../src/xhci_psw.h"
#include "test_harness.h"

#define FAKE_LOG 16

/* The four registers, the accesses made in order, and one access that
 * fails: kind 'r' or 'w' at failOffset (0 for none). */
typedef struct _FAKE_CFG {
    ULONG Usb2Route;
    ULONG Usb2Mask;
    ULONG Usb3Enable;
    ULONG Usb3Mask;
    ULONG LogOffset[FAKE_LOG];
    char LogKind[FAKE_LOG];
    ULONG LogValue[FAKE_LOG];
    ULONG Count;
    char FailKind;
    ULONG FailOffset;
} FAKE_CFG;

static PULONG fakeReg(FAKE_CFG *f, ULONG offset)
{
    switch (offset) {
    case XHCI_PSW_XUSB2PR:      return &f->Usb2Route;
    case XHCI_PSW_XUSB2PRM:     return &f->Usb2Mask;
    case XHCI_PSW_USB3_PSSEN:   return &f->Usb3Enable;
    case XHCI_PSW_USB3PRM:      return &f->Usb3Mask;
    }
    return NULL;
}

static void fakeLog(FAKE_CFG *f, char kind, ULONG offset, ULONG value)
{
    if (f->Count < FAKE_LOG) {
        f->LogKind[f->Count] = kind;
        f->LogOffset[f->Count] = offset;
        f->LogValue[f->Count] = value;
    }
    f->Count++;
}

static ULONG fakeRead(PVOID context, ULONG offset, PULONG value)
{
    FAKE_CFG *f;
    PULONG reg;

    f = (FAKE_CFG *)context;
    reg = fakeReg(f, offset);
    fakeLog(f, 'r', offset, 0);
    if (reg == NULL || (f->FailKind == 'r' && f->FailOffset == offset)) {
        return 0;
    }
    *value = *reg;
    return 1;
}

/* The write-only bits outside the mask read back 0, as a routing register
 * whose unswitchable bits are reserved would. */
static ULONG fakeWrite(PVOID context, ULONG offset, ULONG value)
{
    FAKE_CFG *f;
    PULONG reg;

    f = (FAKE_CFG *)context;
    reg = fakeReg(f, offset);
    fakeLog(f, 'w', offset, value);
    if (reg == NULL || (f->FailKind == 'w' && f->FailOffset == offset)) {
        return 0;
    }
    if (offset == XHCI_PSW_USB3_PSSEN) {
        *reg = value & f->Usb3Mask;
    } else if (offset == XHCI_PSW_XUSB2PR) {
        *reg = value & f->Usb2Mask;
    } else {
        *reg = value;
    }
    return 1;
}

/* A Lynx Point with four SuperSpeed and fourteen USB 2.0 connectors
 * switchable, all on EHCI, as firmware on Auto leaves it. */
static void fakeInit(FAKE_CFG *f, XHCI_PSW_IO *io)
{
    ULONG i;
    PUCHAR p;

    p = (PUCHAR)f;
    for (i = 0; i < sizeof(*f); i++) {
        p[i] = 0;
    }
    f->Usb3Mask = 0x0000000FUL;
    f->Usb2Mask = 0x00003FFFUL;
    io->Read = fakeRead;
    io->Write = fakeWrite;
    io->Context = f;
}

static void checkAccess(FAKE_CFG *f, ULONG n, char kind, ULONG offset,
                        ULONG value, const char *what)
{
    CHECK(n < f->Count, what);
    if (n >= f->Count || n >= FAKE_LOG) {
        return;
    }
    CHECK(f->LogKind[n] == kind, what);
    CHECK_EQ(f->LogOffset[n], offset, what);
    if (kind == 'w') {
        CHECK_EQ(f->LogValue[n], value, what);
    }
}

static void test_gate(void)
{
    CHECK_EQ(XhciPswGate(0x1E318086UL), 1, "Panther Point (7-series)");
    CHECK_EQ(XhciPswGate(0x8C318086UL), 1, "Lynx Point (8-series)");
    CHECK_EQ(XhciPswGate(0x9C318086UL), 1, "Lynx Point-LP");
    CHECK_EQ(XhciPswGate(0x8CB18086UL), 1, "Wildcat Point (9-series)");
    CHECK_EQ(XhciPswGate(0x9CB18086UL), 1, "Wildcat Point-LP");

    CHECK_EQ(XhciPswGate(0x000D1B36UL), 0, "QEMU's xHCI writes nothing");
    CHECK_EQ(XhciPswGate(0x8D318086UL), 1, "Wellsburg (C610/X99)");
    CHECK_EQ(XhciPswGate(0x02ED8086UL), 0,
             "Comet Lake-LP has no EHCI to switch from");
    CHECK_EQ(XhciPswGate(0x9D2F8086UL), 0,
             "Sunrise Point-LP has no EHCI to switch from");
    CHECK_EQ(XhciPswGate(0xA12F8086UL), 0, "Sunrise Point-H");
    CHECK_EQ(XhciPswGate(0x1E311B21UL), 0,
             "the device id under another vendor");
    CHECK_EQ(XhciPswGate(0x80861E31UL), 0, "vendor and device swapped");
    CHECK_EQ(XhciPswGate(0xFFFFFFFFUL), 0, "an all-ones read");
    CHECK_EQ(XhciPswGate(0), 0, "an unread id");
}

static void test_board(void)
{
    CHECK_EQ(XhciPswBoardRefused(0x90A8104DUL), 1,
             "Sony VAIO T-series cannot switch");
    CHECK_EQ(XhciPswBoardRefused(0x90A9104DUL), 0, "another Sony board");
    CHECK_EQ(XhciPswBoardRefused(0x90A817AAUL), 0, "the id under Lenovo");
    CHECK_EQ(XhciPswBoardRefused(0), 0, "no subsystem id");
    CHECK_EQ(XhciPswBoardRefused(0xFFFFFFFFUL), 0, "an all-ones read");
}

static void test_enabled(void)
{
    CHECK_EQ(XhciPswEnabled(0, 0), 1, "absent is on");
    CHECK_EQ(XhciPswEnabled(0, 5), 1, "absent is on, whatever was left");
    CHECK_EQ(XhciPswEnabled(1, 0), 0, "an explicit 0 is off");
    CHECK_EQ(XhciPswEnabled(1, 1), 1, "1 is on");
    CHECK_EQ(XhciPswEnabled(1, 2), 1, "any other number is on");
    CHECK_EQ(XhciPswEnabled(1, 0xFFFFFFFFUL), 1, "all-ones is on");
}

static void test_route(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    fakeInit(&f, &io);
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_DONE, "a clean route");
    CHECK_EQ(f.Count, 6, "six accesses");
    checkAccess(&f, 0, 'r', XHCI_PSW_USB3PRM, 0, "USB3PRM read first");
    checkAccess(&f, 1, 'w', XHCI_PSW_USB3_PSSEN, 0x0FUL,
                "SuperSpeed terminations second, at USB3PRM");
    checkAccess(&f, 2, 'r', XHCI_PSW_USB3_PSSEN, 0, "USB3_PSSEN read back");
    checkAccess(&f, 3, 'r', XHCI_PSW_XUSB2PRM, 0, "XUSB2PRM read");
    checkAccess(&f, 4, 'w', XHCI_PSW_XUSB2PR, 0x3FFFUL,
                "USB 2.0 pairs last, at XUSB2PRM");
    checkAccess(&f, 5, 'r', XHCI_PSW_XUSB2PR, 0, "XUSB2PR read back");
    CHECK_EQ(st.Usb3Mask, 0x0FUL, "USB3PRM reported");
    CHECK_EQ(st.Usb3Now, 0x0FUL, "USB3_PSSEN reported");
    CHECK_EQ(st.Usb2Mask, 0x3FFFUL, "XUSB2PRM reported");
    CHECK_EQ(st.Usb2Now, 0x3FFFUL, "XUSB2PR reported");
    CHECK_EQ(f.Usb3Enable, 0x0FUL, "terminations on");
    CHECK_EQ(f.Usb2Route, 0x3FFFUL, "pairs on xHCI");

    /* A second route, as at every D0, writes the same values again. */
    f.Count = 0;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_DONE, "routing again");
    CHECK_EQ(f.Count, 6, "the same six accesses");
    CHECK_EQ(f.Usb2Route, 0x3FFFUL, "still on xHCI");

    /* Firmware that locks every connector: masks of 0, writes of 0. */
    fakeInit(&f, &io);
    f.Usb3Mask = 0;
    f.Usb2Mask = 0;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_DONE, "locked masks");
    checkAccess(&f, 1, 'w', XHCI_PSW_USB3_PSSEN, 0, "an empty mask written");
    checkAccess(&f, 4, 'w', XHCI_PSW_XUSB2PR, 0, "an empty mask written");
}

static void test_route_refusals(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3PRM;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_USB3PRM,
             "USB3PRM unreadable");
    CHECK_EQ(f.Count, 1, "nothing written");
    CHECK_EQ(st.Usb3Mask, 0xFFFFFFFFUL, "the mask reported unread");

    fakeInit(&f, &io);
    f.Usb3Mask = 0xFFFFFFFFUL;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_USB3PRM,
             "an all-ones USB3PRM is nothing decoding");
    CHECK_EQ(f.Count, 1, "nothing written");

    fakeInit(&f, &io);
    f.FailKind = 'w';
    f.FailOffset = XHCI_PSW_USB3_PSSEN;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_PSSEN_WRITE,
             "the terminations refused");
    CHECK_EQ(f.Count, 2, "the pairs never move without the terminations");
    CHECK_EQ(f.Usb2Route, 0, "still on EHCI");

    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3_PSSEN;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_PSSEN_READ,
             "a failed read-back is reported");
    CHECK_EQ(f.Count, 6, "and the route goes on");
    CHECK_EQ(st.Usb3Now, 0xFFFFFFFFUL, "the read-back reported unread");
    CHECK_EQ(f.Usb2Route, 0x3FFFUL, "the pairs moved");

    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_XUSB2PRM;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_XUSB2PRM,
             "XUSB2PRM unreadable");
    CHECK_EQ(f.Count, 4, "XUSB2PR not written");
    CHECK_EQ(f.Usb3Enable, 0x0FUL, "the terminations stay on");

    fakeInit(&f, &io);
    f.Usb2Mask = 0xFFFFFFFFUL;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_XUSB2PRM,
             "an all-ones XUSB2PRM");
    CHECK_EQ(f.Count, 4, "XUSB2PR not written");

    fakeInit(&f, &io);
    f.FailKind = 'w';
    f.FailOffset = XHCI_PSW_XUSB2PR;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_XUSB2PR_WRITE,
             "the pairs refused");
    CHECK_EQ(f.Count, 5, "no read-back after a refused write");

    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_XUSB2PR;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_XUSB2PR_READ,
             "XUSB2PR's read-back failed");
    CHECK_EQ(st.Usb2Now, 0xFFFFFFFFUL, "reported unread");

    CHECK_EQ(XhciPswRoute(NULL, &st), XHCI_PSW_STEP_BAD_PARAM, "no io");
    CHECK_EQ(st.Usb3Mask, 0xFFFFFFFFUL, "state reset on a refusal");
    CHECK_EQ(XhciPswRoute(&io, NULL), XHCI_PSW_STEP_BAD_PARAM, "no state");
    io.Write = NULL;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_BAD_PARAM, "no writer");
}

static void test_release(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    fakeInit(&f, &io);
    (VOID)XhciPswRoute(&io, &st);
    f.Count = 0;
    CHECK_EQ(XhciPswRelease(&io, &st), XHCI_PSW_DONE, "a clean release");
    CHECK_EQ(f.Count, 4, "four accesses");
    checkAccess(&f, 0, 'w', XHCI_PSW_USB3_PSSEN, 0,
                "USB3_PSSEN written 0 first, as Linux");
    checkAccess(&f, 1, 'r', XHCI_PSW_USB3_PSSEN, 0, "and read back");
    checkAccess(&f, 2, 'w', XHCI_PSW_XUSB2PR, 0, "XUSB2PR written 0");
    checkAccess(&f, 3, 'r', XHCI_PSW_XUSB2PR, 0, "and read back");
    CHECK_EQ(f.Usb3Enable, 0, "terminations off");
    CHECK_EQ(f.Usb2Route, 0, "pairs back on EHCI");
    CHECK_EQ(st.Usb3Now, 0, "USB3_PSSEN reported");
    CHECK_EQ(st.Usb2Now, 0, "XUSB2PR reported");
    CHECK_EQ(st.Usb3Mask, 0xFFFFFFFFUL, "no mask is read by a release");

    fakeInit(&f, &io);
    (VOID)XhciPswRoute(&io, &st);
    f.Count = 0;
    f.FailKind = 'w';
    f.FailOffset = XHCI_PSW_USB3_PSSEN;
    CHECK_EQ(XhciPswRelease(&io, &st), XHCI_PSW_STEP_PSSEN_WRITE,
             "the first write refused is the step reported");
    CHECK_EQ(f.Count, 3, "XUSB2PR is still written and read back");
    CHECK_EQ(f.Usb2Route, 0, "pairs back on EHCI regardless");

    fakeInit(&f, &io);
    (VOID)XhciPswRoute(&io, &st);
    f.Count = 0;
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3_PSSEN;
    CHECK_EQ(XhciPswRelease(&io, &st), XHCI_PSW_STEP_PSSEN_READ,
             "a failed read-back is reported");
    CHECK_EQ(f.Count, 4, "and the release goes on");

    fakeInit(&f, &io);
    (VOID)XhciPswRoute(&io, &st);
    f.Count = 0;
    f.FailKind = 'w';
    f.FailOffset = XHCI_PSW_XUSB2PR;
    CHECK_EQ(XhciPswRelease(&io, &st), XHCI_PSW_STEP_XUSB2PR_WRITE,
             "XUSB2PR refused");
    CHECK_EQ(f.Usb3Enable, 0, "terminations off regardless");

    CHECK_EQ(XhciPswRelease(NULL, &st), XHCI_PSW_STEP_BAD_PARAM, "no io");
    CHECK_EQ(XhciPswRelease(&io, NULL), XHCI_PSW_STEP_BAD_PARAM,
             "no state");
}

int main(void)
{
    test_gate();
    test_board();
    test_enabled();
    test_route();
    test_route_refusals();
    test_release();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
