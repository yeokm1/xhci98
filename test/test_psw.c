/*
 * test_psw.c - host vectors for the Intel port switchover's pure half
 * (src\xhci_psw.c; roadmap-hcd.md task 34.3): the device-id gate, the
 * board exemption, the XhciIntelPortSwitch rule, and the route and release
 * over a modelled configuration space - the order of the writes, what is
 * written, and where each refusal stops. Since task 35.5 (design record 16
 * revision 3): value 2's bypass, the accepted-write set, a release of only
 * that set, and the lifetime that carries it.
 */

#include <stdio.h>
#include "../src/xhci_psw.h"
#include "test_harness.h"

#define FAKE_LOG 16
#define PSW_BOTH (XHCI_PSW_WROTE_PSSEN | XHCI_PSW_WROTE_XUSB2PR)
/* The registry value's place in the access log ('v'); no config offset. */
#define FAKE_VALUE 0xFFFFFFFFUL

/* The four registers, the accesses made in order, and one access that
 * fails: kind 'r' or 'w' at failOffset (0 for none). */
typedef struct _FAKE_CFG {
    ULONG Id;
    ULONG Subsystem;
    ULONG ValueFound;   /* XhciIntelPortSwitch, through the Value callback */
    ULONG Value;
    ULONG ValueReads;
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
    case XHCI_PSW_PCI_ID:       return &f->Id;
    case XHCI_PSW_PCI_SUBSYSTEM: return &f->Subsystem;
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

static ULONG fakeValue(PVOID context, PULONG value)
{
    FAKE_CFG *f;

    f = (FAKE_CFG *)context;
    f->ValueReads++;
    fakeLog(f, 'v', FAKE_VALUE, 0);
    if (!f->ValueFound) {
        return 0;
    }
    *value = f->Value;
    return 1;
}

/* A Lynx Point with four SuperSpeed and fourteen USB 2.0 connectors
 * switchable, all on EHCI, as firmware on Auto leaves it; a Lenovo
 * subsystem id, and XhciIntelPortSwitch at the INF's 1. */
static void fakeInit(FAKE_CFG *f, XHCI_PSW_IO *io)
{
    ULONG i;
    PUCHAR p;

    p = (PUCHAR)f;
    for (i = 0; i < sizeof(*f); i++) {
        p[i] = 0;
    }
    f->Id = 0x8C318086UL;
    f->Subsystem = 0x220017AAUL;
    f->ValueFound = 1;
    f->Value = 1;
    f->Usb3Mask = 0x0000000FUL;
    f->Usb2Mask = 0x00003FFFUL;
    io->Read = fakeRead;
    io->Write = fakeWrite;
    io->Value = fakeValue;
    io->Context = f;
}

/* How many writes the log holds at one offset. */
static ULONG fakeWrites(FAKE_CFG *f, ULONG offset)
{
    ULONG i;
    ULONG n;

    n = 0;
    for (i = 0; i < f->Count && i < FAKE_LOG; i++) {
        if (f->LogKind[i] == 'w' && f->LogOffset[i] == offset) {
            n++;
        }
    }
    return n;
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

#define ID_LISTED   0x1E318086UL    /* Panther Point                     */
#define ID_SPT      0x9D2F8086UL    /* Sunrise Point-LP, no EHCI         */
#define ID_CML      0x02ED8086UL    /* Comet Lake-LP, no EHCI            */
#define ID_QEMU     0x000D1B36UL

static void test_mode(void)
{
    /* A listed controller: on unless an explicit 0; 2 is not a bypass. */
    CHECK_EQ(XhciPswMode(ID_LISTED, 0, 0), XHCI_PSW_MODE_GATED,
             "listed, absent is on");
    CHECK_EQ(XhciPswMode(ID_LISTED, 0, 5), XHCI_PSW_MODE_GATED,
             "listed, absent is on whatever was left");
    CHECK_EQ(XhciPswMode(ID_LISTED, 1, 0), XHCI_PSW_MODE_OFF,
             "listed, an explicit 0 is off");
    CHECK_EQ(XhciPswMode(ID_LISTED, 1, 1), XHCI_PSW_MODE_GATED, "listed, 1");
    CHECK_EQ(XhciPswMode(ID_LISTED, 1, 2), XHCI_PSW_MODE_GATED,
             "listed, 2 routes as 1 and bypasses nothing");
    CHECK_EQ(XhciPswMode(ID_LISTED, 1, 3), XHCI_PSW_MODE_GATED, "listed, 3");
    CHECK_EQ(XhciPswMode(ID_LISTED, 1, 0xFFFFFFFFUL), XHCI_PSW_MODE_GATED,
             "listed, all-ones");

    /* An unlisted Intel controller: only an exact 2. */
    CHECK_EQ(XhciPswMode(ID_SPT, 0, 0), XHCI_PSW_MODE_OFF,
             "Sunrise Point, absent");
    CHECK_EQ(XhciPswMode(ID_SPT, 0, 2), XHCI_PSW_MODE_OFF,
             "Sunrise Point, absent with a 2 left over");
    CHECK_EQ(XhciPswMode(ID_SPT, 1, 0), XHCI_PSW_MODE_OFF,
             "Sunrise Point, 0");
    CHECK_EQ(XhciPswMode(ID_SPT, 1, 1), XHCI_PSW_MODE_OFF,
             "Sunrise Point, the default 1 writes nothing");
    CHECK_EQ(XhciPswMode(ID_SPT, 1, 2), XHCI_PSW_MODE_BYPASS,
             "Sunrise Point, 2 bypasses the list");
    CHECK_EQ(XhciPswMode(ID_SPT, 1, 3), XHCI_PSW_MODE_OFF,
             "Sunrise Point, 3 is not 2");
    CHECK_EQ(XhciPswMode(ID_SPT, 1, 0xFFFFFFFFUL), XHCI_PSW_MODE_OFF,
             "Sunrise Point, all-ones");
    CHECK_EQ(XhciPswMode(ID_CML, 1, 2), XHCI_PSW_MODE_BYPASS,
             "Comet Lake-LP, 2");
    CHECK_EQ(XhciPswMode(ID_CML, 1, 1), XHCI_PSW_MODE_OFF,
             "Comet Lake-LP, 1");
    CHECK_EQ(XhciPswMode(ID_CML, 0, 0), XHCI_PSW_MODE_OFF,
             "Comet Lake-LP, absent");
    CHECK_EQ(XhciPswMode(ID_CML, 1, 0), XHCI_PSW_MODE_OFF,
             "Comet Lake-LP, 0");
    CHECK_EQ(XhciPswMode(ID_CML, 1, 3), XHCI_PSW_MODE_OFF,
             "Comet Lake-LP, 3");
    CHECK_EQ(XhciPswMode(ID_CML, 1, 0xFFFFFFFFUL), XHCI_PSW_MODE_OFF,
             "Comet Lake-LP, all-ones");

    /* Another vendor: never. */
    CHECK_EQ(XhciPswMode(ID_QEMU, 0, 0), XHCI_PSW_MODE_OFF, "QEMU, absent");
    CHECK_EQ(XhciPswMode(ID_QEMU, 1, 0), XHCI_PSW_MODE_OFF, "QEMU, 0");
    CHECK_EQ(XhciPswMode(ID_QEMU, 1, 1), XHCI_PSW_MODE_OFF, "QEMU, 1");
    CHECK_EQ(XhciPswMode(ID_QEMU, 1, 3), XHCI_PSW_MODE_OFF, "QEMU, 3");
    CHECK_EQ(XhciPswMode(ID_QEMU, 1, 0xFFFFFFFFUL), XHCI_PSW_MODE_OFF,
             "QEMU, all-ones");
    CHECK_EQ(XhciPswMode(ID_QEMU, 1, 2), XHCI_PSW_MODE_OFF,
             "QEMU, 2 is no bypass off Intel");
    CHECK_EQ(XhciPswMode(0x1E311B21UL, 1, 2), XHCI_PSW_MODE_OFF,
             "a listed device id under another vendor, 2");
    CHECK_EQ(XhciPswMode(0xFFFFFFFFUL, 1, 2), XHCI_PSW_MODE_OFF,
             "an all-ones id, 2");

    CHECK_EQ(XhciPswIntel(ID_LISTED), 1, "Intel, listed");
    CHECK_EQ(XhciPswIntel(ID_SPT), 1, "Intel, unlisted");
    CHECK_EQ(XhciPswIntel(ID_CML), 1, "Intel, Comet Lake-LP");
    CHECK_EQ(XhciPswIntel(ID_QEMU), 0, "QEMU");
    CHECK_EQ(XhciPswIntel(0x80861E31UL), 0, "vendor and device swapped");
    CHECK_EQ(XhciPswIntel(0xFFFFFFFFUL), 0, "an all-ones id");
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
    CHECK_EQ(XhciPswRelease(&io, PSW_BOTH, &st), XHCI_PSW_DONE, "a clean release");
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
    CHECK_EQ(XhciPswRelease(&io, PSW_BOTH, &st), XHCI_PSW_STEP_PSSEN_WRITE,
             "the first write refused is the step reported");
    CHECK_EQ(f.Count, 3, "XUSB2PR is still written and read back");
    CHECK_EQ(f.Usb2Route, 0, "pairs back on EHCI regardless");

    fakeInit(&f, &io);
    (VOID)XhciPswRoute(&io, &st);
    f.Count = 0;
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3_PSSEN;
    CHECK_EQ(XhciPswRelease(&io, PSW_BOTH, &st), XHCI_PSW_STEP_PSSEN_READ,
             "a failed read-back is reported");
    CHECK_EQ(f.Count, 4, "and the release goes on");

    fakeInit(&f, &io);
    (VOID)XhciPswRoute(&io, &st);
    f.Count = 0;
    f.FailKind = 'w';
    f.FailOffset = XHCI_PSW_XUSB2PR;
    CHECK_EQ(XhciPswRelease(&io, PSW_BOTH, &st), XHCI_PSW_STEP_XUSB2PR_WRITE,
             "XUSB2PR refused");
    CHECK_EQ(f.Usb3Enable, 0, "terminations off regardless");

    fakeInit(&f, &io);
    (VOID)XhciPswRoute(&io, &st);
    f.Count = 0;
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_XUSB2PR;
    CHECK_EQ(XhciPswRelease(&io, PSW_BOTH, &st), XHCI_PSW_STEP_XUSB2PR_READ,
             "XUSB2PR's read-back failed");
    CHECK_EQ(f.Count, 4, "every access was made");
    CHECK_EQ(st.Usb2Now, 0xFFFFFFFFUL, "reported unread");
    CHECK_EQ(f.Usb2Route, 0, "pairs back on EHCI");

    CHECK_EQ(XhciPswRelease(NULL, PSW_BOTH, &st), XHCI_PSW_STEP_BAD_PARAM,
             "no io");
    CHECK_EQ(XhciPswRelease(&io, PSW_BOTH, NULL), XHCI_PSW_STEP_BAD_PARAM,
             "no state");
}

/* Route with one access failing (kind 0 for none), and its Written. */
static ULONG routeWritten(char kind, ULONG offset, ULONG *step)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    fakeInit(&f, &io);
    f.FailKind = kind;
    f.FailOffset = offset;
    *step = XhciPswRoute(&io, &st);
    return st.Written;
}

static void test_written(void)
{
    ULONG step;
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    CHECK_EQ(routeWritten(0, 0, &step), PSW_BOTH, "a clean route, both");
    CHECK_EQ(routeWritten('r', XHCI_PSW_USB3PRM, &step), 0,
             "USB3PRM unreadable, nothing");
    CHECK_EQ(routeWritten('w', XHCI_PSW_USB3_PSSEN, &step), 0,
             "USB3_PSSEN refused: not confirmed, not recorded");
    CHECK_EQ(routeWritten('r', XHCI_PSW_USB3_PSSEN, &step), PSW_BOTH,
             "USB3_PSSEN's read-back failed and the route went on, both");
    CHECK_EQ(step, XHCI_PSW_STEP_PSSEN_READ, "the read-back still reported");
    CHECK_EQ(routeWritten('r', XHCI_PSW_XUSB2PRM, &step),
             XHCI_PSW_WROTE_PSSEN, "the D4h read failed, USB3_PSSEN only");
    CHECK_EQ(routeWritten('w', XHCI_PSW_XUSB2PR, &step),
             XHCI_PSW_WROTE_PSSEN, "XUSB2PR refused, USB3_PSSEN only");
    CHECK_EQ(routeWritten('r', XHCI_PSW_XUSB2PR, &step), PSW_BOTH,
             "XUSB2PR's read-back failed, both");

    fakeInit(&f, &io);
    f.Usb3Mask = 0xFFFFFFFFUL;
    (VOID)XhciPswRoute(&io, &st);
    CHECK_EQ(st.Written, 0, "an all-ones USB3PRM, nothing");
}

static void test_release_sets(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    fakeInit(&f, &io);
    f.Usb3Enable = 0x0FUL;
    f.Usb2Route = 0x3FFFUL;
    CHECK_EQ(XhciPswRelease(&io, 0, &st), XHCI_PSW_DONE, "an empty set");
    CHECK_EQ(f.Count, 0, "no access at all");
    CHECK_EQ(st.Usb3Now, 0xFFFFFFFFUL, "nothing read back");

    fakeInit(&f, &io);
    f.Usb3Enable = 0x0FUL;
    f.Usb2Route = 0x3FFFUL;
    CHECK_EQ(XhciPswRelease(&io, XHCI_PSW_WROTE_PSSEN, &st), XHCI_PSW_DONE,
             "USB3_PSSEN only");
    CHECK_EQ(f.Count, 2, "two accesses");
    checkAccess(&f, 0, 'w', XHCI_PSW_USB3_PSSEN, 0, "D8h written 0");
    checkAccess(&f, 1, 'r', XHCI_PSW_USB3_PSSEN, 0, "and read back");
    CHECK_EQ(f.Usb2Route, 0x3FFFUL, "D0h untouched");
    CHECK_EQ(st.Usb2Now, 0xFFFFFFFFUL, "D0h not read");

    fakeInit(&f, &io);
    f.Usb3Enable = 0x0FUL;
    f.Usb2Route = 0x3FFFUL;
    CHECK_EQ(XhciPswRelease(&io, XHCI_PSW_WROTE_XUSB2PR, &st),
             XHCI_PSW_DONE, "XUSB2PR only");
    CHECK_EQ(f.Count, 2, "two accesses");
    checkAccess(&f, 0, 'w', XHCI_PSW_XUSB2PR, 0, "D0h written 0");
    CHECK_EQ(f.Usb3Enable, 0x0FUL, "D8h untouched");
}

static void test_life_start(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;

    fakeInit(&f, &io);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "a listed start routes");
    CHECK_EQ(life.On, 1, "on");
    CHECK_EQ(life.Mode, XHCI_PSW_MODE_GATED, "gated");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_ON, "decided on");
    CHECK_EQ(life.Written, PSW_BOTH, "both written");
    CHECK_EQ(life.Subsystem, 0x220017AAUL, "the subsystem kept");
    CHECK_EQ(f.Count, 9, "the id, the value, the subsystem, the route's six");
    checkAccess(&f, 0, 'r', XHCI_PSW_PCI_ID, 0, "the id first");
    checkAccess(&f, 1, 'v', FAKE_VALUE, 0, "the value second");
    checkAccess(&f, 2, 'r', XHCI_PSW_PCI_SUBSYSTEM, 0, "the subsystem third");
    checkAccess(&f, 3, 'r', XHCI_PSW_USB3PRM, 0, "then the route");

    fakeInit(&f, &io);
    f.Id = ID_QEMU;
    f.Value = 2;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "QEMU under 2");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_NOT_INTEL, "not Intel");
    CHECK_EQ(f.ValueReads, 0, "the value is not read off Intel");
    CHECK_EQ(f.Count, 1, "offset 0 and nothing else");
    CHECK_EQ(st.Usb3Mask, 0xFFFFFFFFUL, "st initialised");
    CHECK_EQ(st.Written, 0, "st's set empty");

    fakeInit(&f, &io);
    f.Value = 0;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "listed at 0");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_OFF, "off");
    CHECK_EQ(life.Found, 1, "the value found");
    CHECK_EQ(f.Count, 2, "the id and the value, no subsystem read when off");
    checkAccess(&f, 1, 'v', FAKE_VALUE, 0, "the value after the id");

    fakeInit(&f, &io);
    f.Id = ID_SPT;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "Sunrise Point at 1");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_OFF, "off");
    CHECK_EQ(f.ValueReads, 1, "the value is read on every Intel part");
    CHECK_EQ(fakeWrites(&f, XHCI_PSW_USB3_PSSEN) +
             fakeWrites(&f, XHCI_PSW_XUSB2PR), 0, "no write");

    fakeInit(&f, &io);
    f.Id = ID_SPT;
    f.Value = 2;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "Sunrise Point at 2");
    CHECK_EQ(life.Mode, XHCI_PSW_MODE_BYPASS, "the list bypassed");
    CHECK_EQ(life.Written, PSW_BOTH, "both written");

    fakeInit(&f, &io);
    f.Id = ID_SPT;
    f.Value = 2;
    f.Subsystem = 0x90A8104DUL;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "the Sony board under 2");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_BOARD_REFUSED, "refused");
    CHECK_EQ(life.On, 0, "off");
    CHECK_EQ(f.Count, 3, "the id, the value and the subsystem, no write");
    checkAccess(&f, 2, 'r', XHCI_PSW_PCI_SUBSYSTEM, 0, "the subsystem last");

    fakeInit(&f, &io);
    f.Id = ID_SPT;
    f.Value = 2;
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_PCI_SUBSYSTEM;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0,
             "an unreadable subsystem under 2");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_BOARD_UNREAD, "refused");
    CHECK_EQ(f.Count, 3, "no write");

    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_PCI_ID;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "an unreadable id");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_ID_UNREAD, "refused");
    CHECK_EQ(f.ValueReads, 0, "no value read");
    CHECK_EQ(f.Count, 1, "no subsystem read");

    fakeInit(&f, &io);
    io.Value = NULL;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "no value reader");
    CHECK_EQ(f.Count, 0, "no access");
    CHECK_EQ(XhciPswLifeStart(&io, NULL, &st), 0, "no lifetime");
}

static void test_life(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;

    /* A good start, then a resume route that fails at USB3PRM: the stop
     * still releases both. */
    fakeInit(&f, &io);
    (VOID)XhciPswLifeStart(&io, &life, &st);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3PRM;
    CHECK_EQ(XhciPswLifeResume(&io, &life, &st), 1, "the resume route ran");
    CHECK_EQ(st.Written, 0, "and wrote nothing");
    CHECK_EQ(life.Written, PSW_BOTH, "the start's writes kept");
    f.FailKind = 0;
    f.Count = 0;
    CHECK_EQ(XhciPswLifeEnd(&io, &life, &st), 1, "the stop releases");
    CHECK_EQ(fakeWrites(&f, XHCI_PSW_USB3_PSSEN), 1, "D8h released");
    CHECK_EQ(fakeWrites(&f, XHCI_PSW_XUSB2PR), 1, "D0h released");
    CHECK_EQ(life.On, 0, "off after the stop");
    CHECK_EQ(life.Written, 0, "the set emptied");

    /* A start that wrote USB3_PSSEN only, then a good resume: both. */
    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_XUSB2PRM;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(life.Written, XHCI_PSW_WROTE_PSSEN, "the start wrote D8h");
    f.FailKind = 0;
    (VOID)XhciPswLifeResume(&io, &life, &st);
    CHECK_EQ(life.Written, PSW_BOTH, "the resume added D0h");

    /* A start that wrote USB3_PSSEN only, stopped: D8h alone. */
    fakeInit(&f, &io);
    f.Usb2Route = 0x5UL;
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_XUSB2PRM;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    f.FailKind = 0;
    f.Count = 0;
    CHECK_EQ(XhciPswLifeEnd(&io, &life, &st), 1, "a partial set released");
    CHECK_EQ(fakeWrites(&f, XHCI_PSW_USB3_PSSEN), 1, "D8h released");
    CHECK_EQ(fakeWrites(&f, XHCI_PSW_XUSB2PR), 0, "D0h left to firmware");
    CHECK_EQ(f.Usb2Route, 0x5UL, "firmware's routing kept");

    /* A start whose route wrote nothing: the stop writes nothing. */
    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3PRM;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "the route ran");
    CHECK_EQ(life.On, 1, "on");
    CHECK_EQ(life.Written, 0, "nothing written");
    f.FailKind = 0;
    f.Count = 0;
    CHECK_EQ(XhciPswLifeEnd(&io, &life, &st), 0, "nothing to release");
    CHECK_EQ(f.Count, 0, "no access");
    CHECK_EQ(life.On, 0, "off all the same");
}

static void test_life_edges(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;

    /* Start, a shutdown's release, then the stop: released twice. */
    fakeInit(&f, &io);
    (VOID)XhciPswLifeStart(&io, &life, &st);
    f.Count = 0;
    CHECK_EQ(XhciPswLifeRelease(&io, &life, &st), 1, "the shutdown releases");
    CHECK_EQ(f.Count, 4, "both registers");
    CHECK_EQ(life.On, 1, "still on");
    CHECK_EQ(life.Written, PSW_BOTH, "the set kept");
    f.Count = 0;
    CHECK_EQ(XhciPswLifeEnd(&io, &life, &st), 1, "the stop releases again");
    CHECK_EQ(f.Count, 4, "both registers again");

    /* A refused zero at the shutdown is retried by the stop. */
    fakeInit(&f, &io);
    (VOID)XhciPswLifeStart(&io, &life, &st);
    f.FailKind = 'w';
    f.FailOffset = XHCI_PSW_XUSB2PR;
    CHECK_EQ(XhciPswLifeRelease(&io, &life, &st), 1, "the shutdown releases");
    CHECK_EQ(st.Step, XHCI_PSW_STEP_XUSB2PR_WRITE, "D0h refused");
    CHECK_EQ(f.Usb2Route, 0x3FFFUL, "still on xHCI");
    f.FailKind = 0;
    (VOID)XhciPswLifeEnd(&io, &life, &st);
    CHECK_EQ(f.Usb2Route, 0, "the stop's retry took");

    /* A refused start's end, then a second end. */
    fakeInit(&f, &io);
    (VOID)XhciPswLifeStart(&io, &life, &st);
    (VOID)XhciPswLifeEnd(&io, &life, &st);
    CHECK_EQ(life.On, 0, "the end clears On");
    CHECK_EQ(life.Written, 0, "and the set");
    f.Count = 0;
    CHECK_EQ(XhciPswLifeEnd(&io, &life, &st), 0, "a second end");
    CHECK_EQ(f.Count, 0, "makes no access");
    CHECK_EQ(XhciPswLifeResume(&io, &life, &st), 0, "nor does a resume");
    CHECK_EQ(f.Count, 0, "after the end");

    /* A fresh start inherits no bit. */
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3PRM;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(life.Written, 0, "nothing inherited");

    /* A start that decides off after a lifetime that wrote clears it. */
    fakeInit(&f, &io);
    (VOID)XhciPswLifeStart(&io, &life, &st);
    f.Value = 0;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(life.On, 0, "off");
    CHECK_EQ(life.Written, 0, "the earlier set gone");
}

int main(void)
{
    test_gate();
    test_board();
    test_mode();
    test_route();
    test_route_refusals();
    test_release();
    test_written();
    test_release_sets();
    test_life_start();
    test_life();
    test_life_edges();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
