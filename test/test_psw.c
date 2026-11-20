/*
 * test_psw.c - host vectors for the Intel port switchover's pure half
 * (src\xhci_psw.c; roadmap-hcd.md task 34.3): the device-id gate, the
 * board exemption, the XhciIntelPortSwitch rule, and the route and release
 * over a modelled configuration space - the order of the writes, what is
 * written, and where each refusal stops. Since task 35.5 (design record 16
 * revision 3): value 2's bypass, the accepted-write set, a release of only
 * that set, and the lifetime that carries it. Since task 50.34 (design
 * record 16 section 21): the step that holds the switchover while Windows
 * Setup runs, over a modelled registry - every reading's classification,
 * the 1296 Setup combinations against a rule stated in this file, the PE
 * tests, the image state, the setting and its source, the first-session
 * guard (Alternative A) through every failure and recovery within a boot,
 * a held lifetime's no access, the shutdown predicate, the latch and the
 * record XhciIntelPortSwitchResult.
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
    /* Section 21 (task 50.34): the two settings' sources and the setting,
     * read through the same Value callback. */
    ULONG SwitchSource;
    ULONG SetState;     /* XHCI_REG_* of XhciIntelPortSwitchSetup           */
    ULONG SetValue;
    ULONG SetSource;
    ULONG SetReads;
    /* Each query's preset observation, and its text (options, image). */
    XHCI_PSW_RAW Raw[10];
    const char *Sz[10];
    ULONG Asked[10];
    /* The guard's modelled registry: G in the driver key, and the volatile
     * session key with ReadyAtStart. */
    ULONG GState;       /* XHCI_REG_*; OTHER is a REG_SZ                    */
    ULONG GValue;
    ULONG Routine;      /* the routine there                                */
    ULONG KeyThere;
    ULONG KeyVolatile;
    ULONG KeySState;    /* XHCI_REG_* of ReadyAtStart                       */
    ULONG KeySValue;
    ULONG OpenStatus;   /* injected: not 0 fails the create                 */
    ULONG OpenDisp;     /* injected disposition, 0 for the model's          */
    ULONG ProbeStatus;  /* injected, 0 for the model's                      */
    ULONG WriteSStatus; /* injected                                         */
    ULONG WriteGStatus; /* injected                                         */
    ULONG SessionRawOn; /* the read-back's observation injected             */
    XHCI_PSW_RAW SessionRaw;
    ULONG Opened;       /* handles: the session key's                       */
    ULONG Closed;
    ULONG Children;     /* stable children made (a non-volatile key)        */
    ULONG WritesS;
    ULONG WritesG;
    ULONG GuardOps;
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

/* XhciIntelPortSwitch is logged as an access ('v'); the setting is only
 * counted, so the route's access orders read as before task 50.34. */
static ULONG fakeValue(PVOID context, ULONG which, PULONG value,
                       PULONG source)
{
    FAKE_CFG *f;

    f = (FAKE_CFG *)context;
    if (which == XHCI_PSW_WHICH_SETUP) {
        f->SetReads++;
        *source = (f->SetState == XHCI_REG_ABSENT) ? XHCI_REG_FROM_NONE
                                                   : f->SetSource;
        if (f->SetState == XHCI_REG_TAKEN) {
            *value = f->SetValue;
        }
        return f->SetState;
    }
    f->ValueReads++;
    fakeLog(f, 'v', FAKE_VALUE, 0);
    if (!f->ValueFound) {
        *source = XHCI_REG_FROM_NONE;
        return XHCI_REG_ABSENT;
    }
    *source = (f->SwitchSource != 0) ? f->SwitchSource
                                     : XHCI_REG_FROM_DEVICE;
    *value = f->Value;
    return XHCI_REG_TAKEN;
}

/* A Lynx Point with four SuperSpeed and fourteen USB 2.0 connectors
 * switchable, all on EHCI, as firmware on Auto leaves it; a Lenovo
 * subsystem id, and XhciIntelPortSwitch at 1 (the INF's value since task
 * 49.47). */
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
    io->Query = NULL;
    io->Guard = NULL;
    io->Text = NULL;
    io->Context = f;
    /* The vectors before task 50.34 read a 98 SE start, which section 21
     * leaves as it was; ntInit gives the NT starts. */
    io->System = XHCI_PSW_SYS_9X;
}

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
    /* A listed controller: on for a found nonzero value, off when absent
     * (task 36.42; kept by task 49.47 for text-mode Setup); 2 is not a
     * bypass. */
    CHECK_EQ(XhciPswMode(ID_LISTED, 0, 0), XHCI_PSW_MODE_OFF,
             "listed, absent is off");
    CHECK_EQ(XhciPswMode(ID_LISTED, 0, 5), XHCI_PSW_MODE_OFF,
             "listed, absent is off whatever was left");
    CHECK_EQ(XhciPswMode(ID_LISTED, 0, 1), XHCI_PSW_MODE_OFF,
             "listed, absent is off with a 1 left over");
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
             "Sunrise Point, 1 writes nothing");
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

    /* Two failures: the first is the one reported. */
    fakeInit(&f, &io);
    f.FailKind = 'r';
    f.FailOffset = XHCI_PSW_USB3_PSSEN;
    f.Usb2Mask = 0xFFFFFFFFUL;
    CHECK_EQ(XhciPswRoute(&io, &st), XHCI_PSW_STEP_PSSEN_READ,
             "a read-back failure, then an all-ones XUSB2PRM");
    CHECK_EQ(f.Count, 4, "XUSB2PR still not written");

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

    /* Absent, or of another type (the executor's reader refuses both the
     * same way): off, on a listed controller too (task 36.42; kept by task
     * 49.47, owner 2026-11-14: text-mode Setup writes no value). */
    fakeInit(&f, &io);
    f.ValueFound = 0;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "listed, value absent");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_OFF, "absent is off");
    CHECK_EQ(life.Mode, XHCI_PSW_MODE_OFF, "mode off");
    CHECK_EQ(life.Found, 0, "the value not found");
    CHECK_EQ(life.Value, 0, "the value logged as 0");
    CHECK_EQ(life.On, 0, "not on");
    CHECK_EQ(life.Written, 0, "nothing written");
    CHECK_EQ(f.Count, 2, "the id and the value, no subsystem read");
    CHECK_EQ(fakeWrites(&f, XHCI_PSW_USB3_PSSEN) +
             fakeWrites(&f, XHCI_PSW_XUSB2PR), 0, "no write when absent");
    CHECK_EQ(XhciPswLifeEnd(&io, &life, &st), 0, "no release");

    fakeInit(&f, &io);
    f.Id = ID_SPT;
    f.ValueFound = 0;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "Sunrise Point, absent");
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_OFF, "unlisted absent is off");
    CHECK_EQ(life.On, 0, "not on");
    CHECK_EQ(f.Count, 2, "the id and the value, no subsystem read");
    CHECK_EQ(fakeWrites(&f, XHCI_PSW_USB3_PSSEN) +
             fakeWrites(&f, XHCI_PSW_XUSB2PR), 0, "no write when absent");
    CHECK_EQ(XhciPswLifeResume(&io, &life, &st), 0, "no resume route");
    CHECK_EQ(XhciPswLifeEnd(&io, &life, &st), 0, "no release");
    CHECK_EQ(f.Count, 2, "the resume and the end touch nothing");

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

/* ------------------------------------------------------------------ */
/* Section 21 (task 50.34): the step, over a modelled registry          */
/* ------------------------------------------------------------------ */

static ULONG HandleOpen;

static void rawSet(XHCI_PSW_RAW *r, ULONG calls, ULONG type, ULONG length,
                   ULONG status, ULONG dword)
{
    r->Calls = calls;
    r->Type = type;
    r->Length = length;
    r->Status = status;
    r->Dword = dword;
}

static void rawDword(XHCI_PSW_RAW *r, ULONG v)
{
    rawSet(r, 1, XHCI_PSW_REG_DWORD, 4, XHCI_PSW_ST_SUCCESS, v);
}

static void rawAbsent(XHCI_PSW_RAW *r)
{
    rawSet(r, 0, 0, 0, XHCI_PSW_ST_NAME_NOT_FOUND, 0);
}

static ULONG szLen(const char *s)
{
    ULONG n;

    n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

static void fakeSz(FAKE_CFG *f, ULONG which, const char *s)
{
    rawSet(&f->Raw[which], 1, XHCI_PSW_REG_SZ, (szLen(s) + 1) * 2,
           XHCI_PSW_ST_SUCCESS, 0);
    f->Sz[which] = s;
}

static void fakeQuery(PVOID context, ULONG which, PXHCI_PSW_RAW raw,
                      PUSHORT text, ULONG capChars)
{
    FAKE_CFG *f;
    const char *s;
    ULONG i;
    ULONG n;

    f = (FAKE_CFG *)context;
    if (which < 10) {
        f->Asked[which]++;
    }
    if (which == XHCI_PSW_Q_READY) {
        if (f->GState == XHCI_REG_TAKEN) {
            rawDword(raw, f->GValue);
        } else if (f->GState == XHCI_REG_OTHER) {
            rawSet(raw, 1, XHCI_PSW_REG_SZ, 4, XHCI_PSW_ST_SUCCESS, 0x31UL);
        } else {
            rawAbsent(raw);
        }
        return;
    }
    if (which == XHCI_PSW_Q_SESSION) {
        if (f->SessionRawOn) {
            *raw = f->SessionRaw;
        } else if (!f->KeyThere || f->KeySState == XHCI_REG_ABSENT) {
            rawAbsent(raw);
        } else if (f->KeySState == XHCI_REG_TAKEN) {
            rawDword(raw, f->KeySValue);
        } else {
            rawSet(raw, 1, XHCI_PSW_REG_SZ, 4, XHCI_PSW_ST_SUCCESS, 0);
        }
        return;
    }
    if (which >= 10) {
        return;
    }
    *raw = f->Raw[which];
    s = f->Sz[which];
    if (s != NULL && text != NULL) {
        n = szLen(s) + 1;
        for (i = 0; i < n && i < capChars; i++) {
            text[i] = (USHORT)(UCHAR)s[i];
        }
    }
}

static ULONG fakeGuard(PVOID context, ULONG op, ULONG arg, PULONG out)
{
    FAKE_CFG *f;

    f = (FAKE_CFG *)context;
    f->GuardOps++;
    switch (op) {
    case XHCI_PSW_OP_ROUTINE:
        return f->Routine;
    case XHCI_PSW_OP_OPEN:
        if (f->OpenStatus != 0) {
            return f->OpenStatus;
        }
        if (!f->KeyThere) {
            f->KeyThere = 1;
            f->KeyVolatile = 1;
            f->KeySState = XHCI_REG_ABSENT;
            *out = XHCI_PSW_KEY_CREATED;
        } else {
            *out = XHCI_PSW_KEY_OPENED;
        }
        if (f->OpenDisp != 0) {
            *out = f->OpenDisp;
        }
        f->Opened++;
        HandleOpen = 1;
        return XHCI_PSW_ST_SUCCESS;
    case XHCI_PSW_OP_PROBE:
        if (f->ProbeStatus != 0) {
            return f->ProbeStatus;
        }
        if (f->KeyVolatile) {
            return XHCI_PSW_ST_CHILD_VOLATILE;
        }
        f->Children++;
        return XHCI_PSW_ST_SUCCESS;
    case XHCI_PSW_OP_WRITE_S:
        f->WritesS++;
        if (f->WriteSStatus != 0) {
            return f->WriteSStatus;
        }
        f->KeySState = XHCI_REG_TAKEN;
        f->KeySValue = arg;
        return XHCI_PSW_ST_SUCCESS;
    case XHCI_PSW_OP_WRITE_G:
        f->WritesG++;
        if (f->WriteGStatus != 0) {
            return f->WriteGStatus;
        }
        f->GState = XHCI_REG_TAKEN;
        f->GValue = arg;
        return XHCI_PSW_ST_SUCCESS;
    case XHCI_PSW_OP_CLOSE:
        if (HandleOpen) {
            f->Closed++;
            HandleOpen = 0;
        }
        return XHCI_PSW_ST_SUCCESS;
    }
    return 0xC0000001UL;
}

/* A Windows restart: the volatile key is gone, a stable one stays. */
static void fakeReboot(FAKE_CFG *f)
{
    if (f->KeyVolatile) {
        f->KeyThere = 0;
        f->KeySState = XHCI_REG_ABSENT;
    }
    HandleOpen = 0;
}

static USHORT TextBuf[XHCI_PSW_OPTIONS_CHARS];

/* An NT start of a listed controller with the switchover on, in a session
 * Windows Setup has finished: Setup's primary and SetupType 0, the other
 * two absent, not PE, the hive loaded and the image complete, the routine
 * there, no session key, and G absent - a first session, so the guard
 * holds. fakeArmed makes it a later session. */
static void ntInit(FAKE_CFG *f, XHCI_PSW_IO *io, ULONG system)
{
    fakeInit(f, io);
    io->System = system;
    io->Query = fakeQuery;
    io->Guard = fakeGuard;
    io->Text = TextBuf;
    HandleOpen = 0;
    rawDword(&f->Raw[XHCI_PSW_Q_SSIP], 0);
    rawDword(&f->Raw[XHCI_PSW_Q_TYPE], 0);
    rawAbsent(&f->Raw[XHCI_PSW_Q_OOBE]);
    rawAbsent(&f->Raw[XHCI_PSW_Q_MINI]);
    rawAbsent(&f->Raw[XHCI_PSW_Q_MININT]);
    fakeSz(f, XHCI_PSW_Q_OPTIONS, " NOEXECUTE=OPTIN");
    rawSet(&f->Raw[XHCI_PSW_Q_HIVE], 1, XHCI_PSW_REG_NONE, 0,
           XHCI_PSW_ST_SUCCESS, 0);
    fakeSz(f, XHCI_PSW_Q_IMAGE, "IMAGE_STATE_COMPLETE");
    f->Routine = 1;
    f->GState = XHCI_REG_ABSENT;
}

static void fakeArmed(FAKE_CFG *f)
{
    f->GState = XHCI_REG_TAKEN;
    f->GValue = 1;
}

static ULONG routeWrites(FAKE_CFG *f)
{
    ULONG i;
    ULONG n;

    n = 0;
    for (i = 0; i < f->Count && i < FAKE_LOG; i++) {
        if (f->LogKind[i] == 'w' ||
            (f->LogKind[i] == 'r' && f->LogOffset[i] >= XHCI_PSW_XUSB2PR &&
             f->LogOffset[i] <= XHCI_PSW_USB3PRM)) {
            n++;
        }
    }
    return n;
}

/* A held lifetime: no access at D0h to DCh at its start, its resumes, a
 * shutdown's release or its end, and the hand-back refused. */
static void checkHeldLife(FAKE_CFG *f, XHCI_PSW_IO *io, XHCI_PSW_LIFE *life,
                          const char *what)
{
    XHCI_PSW_STATE st;

    CHECK_EQ(life->On, 0, what);
    CHECK_EQ(life->Decision, XHCI_PSW_DECIDE_SETUP_HELD, what);
    CHECK_EQ(routeWrites(f), 0, what);
    CHECK_EQ(XhciPswLifeResume(io, life, &st), 0, what);
    CHECK_EQ(XhciPswLifeRelease(io, life, &st), 0, what);
    CHECK_EQ(XhciPswShutdownAllowed(life), 0, what);
    CHECK_EQ(XhciPswLifeEnd(io, life, &st), 0, what);
    CHECK_EQ(routeWrites(f), 0, what);
    CHECK_EQ(f->Usb2Route, 0, what);
    CHECK_EQ(f->Usb3Enable, 0, what);
}

static void test_classify_dword(void)
{
    XHCI_PSW_RAW r;
    ULONG v;
    static const ULONG fails[] = {
        XHCI_PSW_ST_NAME_NOT_FOUND, XHCI_PSW_ST_PATH_NOT_FOUND,
        0xC0000022UL, 0xC000009AUL, 0x80000005UL, XHCI_PSW_ST_NOT_RUN
    };
    ULONG i;

    /* The table's first row: one four-byte REG_DWORD. */
    rawDword(&r, 7);
    v = 0;
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_TAKEN, "taken");
    CHECK_EQ(v, 7, "the value whole");
    rawDword(&r, 0xFFFFFFFFUL);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_TAKEN, "all-ones");
    CHECK_EQ(v, 0xFFFFFFFFUL, "all-ones whole");
    for (i = 0; i < sizeof(fails) / sizeof(fails[0]); i++) {
        rawSet(&r, 1, XHCI_PSW_REG_DWORD, 4, fails[i], 0);
        CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_UNREAD,
                 "a DWORD taken by a query that failed is unread");
    }
    /* An empty REG_NONE, whatever the status. */
    rawSet(&r, 1, XHCI_PSW_REG_NONE, 0, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "an empty REG_NONE, success");
    for (i = 0; i < sizeof(fails) / sizeof(fails[0]); i++) {
        rawSet(&r, 1, XHCI_PSW_REG_NONE, 0, fails[i], 0);
        CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
                 "an empty REG_NONE, a failure");
    }
    /* Another type or length, or more than one call. */
    rawSet(&r, 1, XHCI_PSW_REG_SZ, 4, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED, "REG_SZ");
    rawSet(&r, 1, 3, 4, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "REG_BINARY of four");
    rawSet(&r, 1, XHCI_PSW_REG_DWORD, 2, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "a short DWORD");
    rawSet(&r, 1, XHCI_PSW_REG_DWORD, 8, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "a long DWORD");
    rawSet(&r, 1, 11, 8, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "REG_QWORD");
    rawSet(&r, 2, XHCI_PSW_REG_DWORD, 4, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "two calls");
    rawSet(&r, 1, XHCI_PSW_REG_SZ, 4, XHCI_PSW_ST_NAME_NOT_FOUND, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "another type, not found");
    rawSet(&r, 3, XHCI_PSW_REG_DWORD, 4, 0xC0000022UL, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_MALFORMED,
             "three calls, a failure");
    /* Nothing seen. */
    rawSet(&r, 0, 0, 0, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_UNREAD,
             "no call and success is unread");
    rawSet(&r, 0, 0, 0, XHCI_PSW_ST_NAME_NOT_FOUND, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_ABSENT,
             "no call, name not found");
    rawSet(&r, 0, 0, 0, XHCI_PSW_ST_PATH_NOT_FOUND, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_ABSENT,
             "no call, path not found");
    rawSet(&r, 0, 0, 0, 0xC0000022UL, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_UNREAD,
             "no call, access denied");
    rawSet(&r, 0, 0, 0, 0x80000005UL, 0);
    CHECK_EQ(XhciPswClassifyDword(&r, &v), XHCI_PSW_RD_UNREAD,
             "no call, a warning status");
    CHECK_EQ(XhciPswClassifyDword(NULL, &v), XHCI_PSW_RD_UNREAD, "no raw");
}

static ULONG sz(const char *s, USHORT *buf, ULONG cap, XHCI_PSW_RAW *r)
{
    ULONG n;
    ULONG i;

    n = szLen(s) + 1;
    for (i = 0; i < n && i < cap; i++) {
        buf[i] = (USHORT)(UCHAR)s[i];
    }
    rawSet(r, 1, XHCI_PSW_REG_SZ, n * 2, XHCI_PSW_ST_SUCCESS, 0);
    return XhciPswClassifyString(r, buf, cap);
}

static void test_classify_string(void)
{
    XHCI_PSW_RAW r;
    USHORT b[XHCI_PSW_OPTIONS_CHARS];
    ULONG i;

    CHECK_EQ(sz("IMAGE_STATE_COMPLETE", b, 64, &r), XHCI_PSW_SZ_VALID,
             "a valid string");
    r.Status = 0xC0000022UL;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "a string taken by a query that then failed");
    r.Status = XHCI_PSW_ST_NAME_NOT_FOUND;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "a call and not found");
    (VOID)sz("X", b, 64, &r);
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_VALID, "one char");
    r.Calls = 2;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "two calls");
    r.Calls = 1;
    r.Type = 2;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "REG_EXPAND_SZ kept its type by NOEXPAND");
    r.Type = 7;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "REG_MULTI_SZ");
    r.Type = 3;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "REG_BINARY");
    r.Type = XHCI_PSW_REG_DWORD;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "REG_DWORD");
    r.Type = XHCI_PSW_REG_SZ;
    r.Length = 3;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "an odd length");
    r.Length = 0;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "no data");
    r.Length = 1;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "one byte");
    /* The empty string: a terminator alone is valid, and is no token. */
    CHECK_EQ(sz("", b, 64, &r), XHCI_PSW_SZ_VALID, "the empty string");
    /* The buffer's edge. */
    for (i = 0; i < 63; i++) {
        b[i] = 'A';
    }
    b[63] = 0;
    rawSet(&r, 1, XHCI_PSW_REG_SZ, 128, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_VALID,
             "63 characters and the terminator fill 64");
    r.Length = 130;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "oversized by one character");
    r.Length = 0x10000UL;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "oversized");
    /* Terminators. */
    (VOID)sz("ABCD", b, 64, &r);
    b[4] = 'E';
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "no terminator at the end");
    (VOID)sz("ABCD", b, 64, &r);
    b[1] = 0;
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "an embedded terminator");
    (VOID)sz("ABCD", b, 64, &r);
    r.Length = 12;
    b[5] = 'X';
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "data after the string");
    (VOID)sz("ABCD", b, 64, &r);
    CHECK_EQ(XhciPswClassifyString(&r, NULL, 64), XHCI_PSW_SZ_MALFORMED,
             "no buffer");
    /* Absent, and the other no-call outcomes. */
    rawAbsent(&r);
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_ABSENT, "absent");
    rawSet(&r, 0, 0, 0, XHCI_PSW_ST_PATH_NOT_FOUND, 0);
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_ABSENT,
             "the key absent");
    rawSet(&r, 0, 0, 0, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "no call and success");
    rawSet(&r, 0, 0, 0, 0xC0000022UL, 0);
    CHECK_EQ(XhciPswClassifyString(&r, b, 64), XHCI_PSW_SZ_MALFORMED,
             "no call, access denied");
    CHECK_EQ(XhciPswClassifyString(NULL, b, 64), XHCI_PSW_SZ_MALFORMED,
             "no raw");
}

static ULONG minint(const char *s)
{
    USHORT b[XHCI_PSW_OPTIONS_CHARS];
    ULONG n;
    ULONG i;

    n = szLen(s);
    for (i = 0; i <= n; i++) {
        b[i] = (USHORT)(UCHAR)s[i];
    }
    return XhciPswOptionsMinint(b, n);
}

static ULONG complete(const char *s)
{
    USHORT b[XHCI_PSW_IMAGE_CHARS];
    ULONG n;
    ULONG i;

    n = szLen(s);
    for (i = 0; i <= n; i++) {
        b[i] = (USHORT)(UCHAR)s[i];
    }
    return XhciPswImageComplete(b, n);
}

static void test_tokens(void)
{
    CHECK_EQ(minint("/NOEXECUTE=OPTIN"), 0, "Vista's options");
    CHECK_EQ(minint(" NOEXECUTE=OPTIN"), 0, "7's options");
    CHECK_EQ(minint(" TESTSIGNING  NOEXECUTE=OPTIN"), 0, "8 and later");
    CHECK_EQ(minint("MININT"), 1, "the token alone");
    CHECK_EQ(minint("/MININT"), 1, "with a slash");
    CHECK_EQ(minint(" MININT"), 1, "a leading space");
    CHECK_EQ(minint(" /minint "), 1, "case ignored, trailing space");
    CHECK_EQ(minint("/NOEXECUTE=OPTIN /MININT"), 1, "Vista's spelling");
    CHECK_EQ(minint(" NOEXECUTE=OPTIN  MININT"), 1, "7's spelling");
    CHECK_EQ(minint("NOEXECUTE=OPTIN\tMININT\t"), 1, "tabs");
    CHECK_EQ(minint("MININT=1"), 0, "MININT=1 is not the token");
    CHECK_EQ(minint("XMININT"), 0, "XMININT");
    CHECK_EQ(minint("MININTX"), 0, "MININTX");
    CHECK_EQ(minint("//MININT"), 0, "only one slash removed");
    CHECK_EQ(minint("/"), 0, "a slash alone");
    CHECK_EQ(minint(""), 0, "nothing");
    CHECK_EQ(minint("   "), 0, "spaces");
    CHECK_EQ(minint("MININ"), 0, "short");
    CHECK_EQ(XhciPswOptionsMinint(NULL, 4), 0, "no text");

    CHECK_EQ(complete("IMAGE_STATE_COMPLETE"), 1, "complete");
    CHECK_EQ(complete("image_state_complete"), 1, "case ignored");
    CHECK_EQ(complete("IMAGE_STATE_UNDEPLOYABLE"), 0, "undeployable");
    CHECK_EQ(complete("IMAGE_STATE_SPECIALIZE_RESEAL_TO_OOBE"), 0,
             "resealed to OOBE");
    CHECK_EQ(complete(" IMAGE_STATE_COMPLETE"), 0, "a space before");
    CHECK_EQ(complete("IMAGE_STATE_COMPLETE "), 0, "a space after");
    CHECK_EQ(complete("IMAGE_STATE_COMPLET"), 0, "short");
    CHECK_EQ(complete("IMAGE_STATE_COMPLETEX"), 0, "long");
    CHECK_EQ(complete(""), 0, "empty");
    CHECK_EQ(XhciPswImageComplete(NULL, 20), 0, "no text");
}

static void test_exists(void)
{
    XHCI_PSW_RAW r;

    rawSet(&r, 1, XHCI_PSW_REG_NONE, 0, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyExists(&r, 1), XHCI_PSW_EX_THERE, "there");
    CHECK_EQ(XhciPswClassifyExists(&r, 0), XHCI_PSW_EX_THERE, "hive there");
    rawAbsent(&r);
    CHECK_EQ(XhciPswClassifyExists(&r, 1), XHCI_PSW_EX_NOT, "MiniNT absent");
    CHECK_EQ(XhciPswClassifyExists(&r, 0), XHCI_PSW_EX_NOT,
             "the hive not loaded yet");
    rawSet(&r, 0, 0, 0, XHCI_PSW_ST_PATH_NOT_FOUND, 0);
    CHECK_EQ(XhciPswClassifyExists(&r, 1), XHCI_PSW_EX_NOT,
             "MiniNT's path absent");
    CHECK_EQ(XhciPswClassifyExists(&r, 0), XHCI_PSW_EX_UNREAD,
             "the hive's test takes only name-not-found");
    rawSet(&r, 2, XHCI_PSW_REG_NONE, 0, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyExists(&r, 1), XHCI_PSW_EX_UNREAD, "two calls");
    rawSet(&r, 1, XHCI_PSW_REG_NONE, 0, 0xC0000022UL, 0);
    CHECK_EQ(XhciPswClassifyExists(&r, 1), XHCI_PSW_EX_UNREAD,
             "a call and a failure");
    rawSet(&r, 1, XHCI_PSW_REG_NONE, 0, XHCI_PSW_ST_NAME_NOT_FOUND, 0);
    CHECK_EQ(XhciPswClassifyExists(&r, 1), XHCI_PSW_EX_UNREAD,
             "a call and not found");
    rawSet(&r, 0, 0, 0, XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswClassifyExists(&r, 1), XHCI_PSW_EX_UNREAD,
             "no call and success");
    rawSet(&r, 0, 0, 0, 0xC0000022UL, 0);
    CHECK_EQ(XhciPswClassifyExists(&r, 0), XHCI_PSW_EX_UNREAD,
             "no call, access denied");
    CHECK_EQ(XhciPswClassifyExists(NULL, 0), XHCI_PSW_EX_UNREAD, "no raw");
}

/* The six representative states of section 21.10, as raws. */
#define RS_TAKEN0   0
#define RS_TAKEN1   1
#define RS_TAKEN2   2
#define RS_ABSENT   3
#define RS_MALFORMED 4
#define RS_UNREAD   5

static void rsRaw(XHCI_PSW_RAW *r, ULONG rs)
{
    switch (rs) {
    case RS_TAKEN0: rawDword(r, 0); break;
    case RS_TAKEN1: rawDword(r, 1); break;
    case RS_TAKEN2: rawDword(r, 2); break;
    case RS_ABSENT: rawAbsent(r); break;
    case RS_MALFORMED:
        rawSet(r, 1, XHCI_PSW_REG_NONE, 0, XHCI_PSW_ST_SUCCESS, 0);
        break;
    default:
        rawSet(r, 1, XHCI_PSW_REG_DWORD, 4, 0xC0000022UL, 1);
        break;
    }
}

/* The rule stated apart from xhci_psw.c's, as section 21.3 and the
 * owner's answers read: in progress for SSIP, OOBE or MINI taken non-zero
 * (the first in that order), else SetupType taken non-zero, else unread
 * when any is malformed or unread or the primary absent; otherwise no
 * objection. */
static ULONG ruleExpect(const ULONG *rs, PULONG which)
{
    ULONG nonzero[4];
    ULONG bad[4];
    ULONG q;

    for (q = 0; q < 4; q++) {
        nonzero[q] = (rs[q] == RS_TAKEN1 || rs[q] == RS_TAKEN2);
        bad[q] = (rs[q] == RS_MALFORMED || rs[q] == RS_UNREAD ||
                  (q == 0 && rs[q] == RS_ABSENT));
    }
    if (nonzero[0]) { *which = 0; return XHCI_PSW_V_HELD_SETUP; }
    if (nonzero[2]) { *which = 2; return XHCI_PSW_V_HELD_SETUP; }
    if (nonzero[3]) { *which = 3; return XHCI_PSW_V_HELD_SETUP; }
    if (nonzero[1]) { *which = 1; return XHCI_PSW_V_HELD_SETUPTYPE; }
    for (q = 0; q < 4; q++) {
        if (bad[q]) {
            *which = q;
            return XHCI_PSW_V_HELD_SETUP_UNREAD;
        }
    }
    *which = 0;
    return XHCI_PSW_V_ROUTE;
}

static void test_setup_matrix(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;
    ULONG rs[4];
    ULONG n;
    ULONG q;
    ULONG want;
    ULONG which;
    ULONG wrong;
    ULONG holds;
    ULONG routes;

    wrong = 0;
    holds = 0;
    routes = 0;
    for (n = 0; n < 1296; n++) {
        rs[0] = n % 6;
        rs[1] = (n / 6) % 6;
        rs[2] = (n / 36) % 6;
        rs[3] = (n / 216) % 6;
        ntInit(&f, &io, (n & 1) ? XHCI_PSW_SYS_NT5 : XHCI_PSW_SYS_NT6);
        fakeArmed(&f);
        for (q = 0; q < 4; q++) {
            rsRaw(&f.Raw[q], rs[q]);
        }
        want = ruleExpect(rs, &which);
        (VOID)XhciPswLifeStart(&io, &life, &st);
        if (life.Verdict != want ||
            (want != XHCI_PSW_V_ROUTE && life.VerdictValue != which) ||
            (life.On != 0) != (want == XHCI_PSW_V_ROUTE) ||
            (want != XHCI_PSW_V_ROUTE && routeWrites(&f) != 0) ||
            f.Opened != f.Closed) {
            wrong++;
        }
        if (want == XHCI_PSW_V_ROUTE) {
            routes++;
        } else {
            holds++;
        }
    }
    CHECK_EQ(wrong, 0, "the 1296 combinations against the rule");
    CHECK_EQ(routes, 2 * 2 * 2, "routes only with the primary taken 0, "
             "SetupType taken 0 or absent, the others taken 0 or absent");
    CHECK_EQ(holds + routes, 1296, "every combination read");

    /* The pure part alone, over the same states. */
    wrong = 0;
    for (n = 0; n < 1296; n++) {
        ULONG state[4];
        ULONG value[4];
        ULONG w2;

        for (q = 0; q < 4; q++) {
            rs[q] = (n / (q == 0 ? 1 : q == 1 ? 6 : q == 2 ? 36 : 216)) % 6;
            rsRaw(&f.Raw[q], rs[q]);
            value[q] = 0xFFFFFFFFUL;
            state[q] = XhciPswClassifyDword(&f.Raw[q], &value[q]);
        }
        want = ruleExpect(rs, &which);
        w2 = 0;
        if (XhciPswSetupHold(state, value, &w2) !=
                (want == XHCI_PSW_V_ROUTE ? XHCI_PSW_V_NONE : want) ||
            (want != XHCI_PSW_V_ROUTE && w2 != which)) {
            wrong++;
        }
    }
    CHECK_EQ(wrong, 0, "XhciPswSetupHold over the 1296");
}

/* The trial's readings (record 16 section 21.2) through the step. */
static void test_setup_routes(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;

    /* XP and 2000 GUI mode, and Windows PE: SSIP 1, SetupType 0. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    fakeArmed(&f);
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "GUI-mode Setup holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETUP, "Setup in progress");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_SSIP, "the primary named");
    checkHeldLife(&f, &io, &life, "GUI mode's lifetime");

    /* Vista and 7's OOBE boot: SSIP 0, SetupType 2, OOBE 1. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawDword(&f.Raw[XHCI_PSW_Q_TYPE], 2);
    rawDword(&f.Raw[XHCI_PSW_Q_OOBE], 1);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "the OOBE boot holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETUP, "in progress");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_OOBE, "OOBEInProgress named");

    /* SetupType alone: the veto. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawDword(&f.Raw[XHCI_PSW_Q_TYPE], 2);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "SetupType 2 vetoes");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETUPTYPE, "the veto");

    /* 7's first boots after Setup: 0, 0, 0, absent. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawDword(&f.Raw[XHCI_PSW_Q_OOBE], 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "after Setup it routes");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_ROUTE, "route");
    CHECK_EQ(life.Written, PSW_BOTH, "both written");
    CHECK_EQ(XhciPswShutdownAllowed(&life), 1, "a routed hand-back stands");

    /* A DWORD primary taken by a failed query, and an empty REG_NONE
     * primary and secondary. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_SSIP], 1, XHCI_PSW_REG_DWORD, 4,
           XHCI_PSW_ST_NAME_NOT_FOUND, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "a failed query holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETUP_UNREAD, "unread");
    CHECK_EQ(life.Setup[0].Status, XHCI_PSW_ST_NAME_NOT_FOUND,
             "the status kept");
    CHECK_EQ(life.Setup[0].Calls, 1, "the call kept");
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_SSIP], 1, XHCI_PSW_REG_NONE, 0,
           XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "an empty REG_NONE SSIP");
    CHECK_EQ(life.Setup[0].State, XHCI_PSW_RD_MALFORMED, "malformed");
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_MINI], 1, XHCI_PSW_REG_NONE, 0,
           XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0,
             "an empty REG_NONE secondary");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETUP_UNREAD, "unread");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_MINI, "MiniSetupInProgress named");
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    fakeArmed(&f);
    rawAbsent(&f.Raw[XHCI_PSW_Q_SSIP]);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "the primary absent");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETUP_UNREAD, "holds as unread");
    CHECK_EQ(life.Setup[0].State, XHCI_PSW_RD_ABSENT, "recorded absent");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawAbsent(&f.Raw[XHCI_PSW_Q_TYPE]);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1,
             "the three secondaries absent route");

    /* Every read is made whatever an earlier one said. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
    f.SetState = XHCI_REG_TAKEN;
    f.SetValue = 2;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(f.Asked[XHCI_PSW_Q_MINI] + f.Asked[XHCI_PSW_Q_MININT] +
             f.Asked[XHCI_PSW_Q_OPTIONS] + f.Asked[XHCI_PSW_Q_HIVE] +
             f.Asked[XHCI_PSW_Q_IMAGE] + f.Asked[XHCI_PSW_Q_READY], 6,
             "every reading made under the setting's 2");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETTING, "held by the setting");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_FIRST, "the guard ran");
    CHECK_EQ(f.GState, XHCI_REG_TAKEN, "and armed G");
    checkHeldLife(&f, &io, &life, "the setting's 2");
}

static void test_pe(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;

    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_MININT], 1, XHCI_PSW_REG_NONE, 0,
           XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "MiniNT holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_PE, "Windows PE");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_MININT, "by the key");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    fakeSz(&f, XHCI_PSW_Q_OPTIONS, "/NOEXECUTE=OPTIN /MININT");
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "the token holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_PE, "Windows PE");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_OPTIONS, "by the boot option");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_MININT], 0, 0, 0, XHCI_PSW_ST_PATH_NOT_FOUND, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "path not found: not PE");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_MININT], 1, XHCI_PSW_REG_NONE, 0,
           0xC0000022UL, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "a key test unread");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_PE_UNREAD, "PE test unread");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_MININT, "the key named");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_MININT], 2, XHCI_PSW_REG_NONE, 0,
           XHCI_PSW_ST_SUCCESS, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "two calls hold");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_PE_UNREAD, "unread");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawAbsent(&f.Raw[XHCI_PSW_Q_OPTIONS]);
    f.Sz[XHCI_PSW_Q_OPTIONS] = NULL;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "no boot options: not PE");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.Raw[XHCI_PSW_Q_OPTIONS].Type = 2;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "a REG_EXPAND_SZ holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_PE_UNREAD, "unread");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_OPTIONS, "the options named");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.Raw[XHCI_PSW_Q_OPTIONS].Length = 2 * XHCI_PSW_OPTIONS_CHARS + 2;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "oversized options hold");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    io.Text = NULL;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "no buffer holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_PE_UNREAD, "the options unread");
    CHECK_EQ(f.Asked[XHCI_PSW_Q_OPTIONS], 0, "and not asked");

    /* PE's own reading wins over an unread one, the key first. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_MININT], 1, XHCI_PSW_REG_NONE, 0,
           0xC0000022UL, 0);
    fakeSz(&f, XHCI_PSW_Q_OPTIONS, "MININT");
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_PE, "PE named over unread");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_Q_OPTIONS, "by the option");
}

static void test_image(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawAbsent(&f.Raw[XHCI_PSW_Q_HIVE]);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "the hive not loaded");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_IMAGE_NOT_LOADED, "not yet");
    CHECK_EQ(f.Asked[XHCI_PSW_Q_IMAGE], 0, "ImageState not read");
    checkHeldLife(&f, &io, &life, "not loaded");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_HIVE], 0, 0, 0, XHCI_PSW_ST_PATH_NOT_FOUND, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "the hive's path absent");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_IMAGE_UNREAD, "unread");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawSet(&f.Raw[XHCI_PSW_Q_HIVE], 1, XHCI_PSW_REG_NONE, 0,
           0xC0000022UL, 0);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "the hive test failed");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_IMAGE_UNREAD, "unread");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    fakeSz(&f, XHCI_PSW_Q_IMAGE, "IMAGE_STATE_UNDEPLOYABLE");
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "undeployable holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_IMAGE, "not complete");
    CHECK(life.ImageText[0] == 'I' && life.ImageText[12] == 'U' &&
          life.ImageText[24] == 0, "the text kept");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawAbsent(&f.Raw[XHCI_PSW_Q_IMAGE]);
    f.Sz[XHCI_PSW_Q_IMAGE] = NULL;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "ImageState absent");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_IMAGE, "not complete");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.Raw[XHCI_PSW_Q_IMAGE].Calls = 2;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "ImageState malformed");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_IMAGE_UNREAD, "unread");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    fakeSz(&f, XHCI_PSW_Q_IMAGE, "image_state_complete");
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "case ignored");

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.Raw[XHCI_PSW_Q_IMAGE].Length = 2 * XHCI_PSW_IMAGE_CHARS + 2;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "oversized holds");

    /* Before Vista neither is read, whatever they would say. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    fakeArmed(&f);
    rawAbsent(&f.Raw[XHCI_PSW_Q_HIVE]);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "2000 and XP route");
    CHECK_EQ(f.Asked[XHCI_PSW_Q_HIVE] + f.Asked[XHCI_PSW_Q_IMAGE], 0,
             "the hive and the image not read");
    CHECK_EQ(life.Image.Value, XHCI_PSW_IMG_NOT_READ, "recorded not read");
}

static void test_setting(void)
{
    static const ULONG systems[3] = {
        XHCI_PSW_SYS_9X, XHCI_PSW_SYS_NT5, XHCI_PSW_SYS_NT6
    };
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;
    ULONG s;

    for (s = 0; s < 3; s++) {
        /* A first session with SSIP 1: everything holds but the bypass. */
        ntInit(&f, &io, systems[s]);
        rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
        CHECK_EQ(XhciPswLifeStart(&io, &life, &st),
                 systems[s] == XHCI_PSW_SYS_9X, "absent: automatic");
        CHECK_EQ(life.Setting, XHCI_PSW_SETTING_AUTO, "automatic");
        CHECK_EQ(life.SettingSource, XHCI_REG_FROM_NONE, "from no key");

        ntInit(&f, &io, systems[s]);
        rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
        f.SetState = XHCI_REG_OTHER;
        f.SetSource = XHCI_REG_FROM_MACHINE;
        CHECK_EQ(XhciPswLifeStart(&io, &life, &st),
                 systems[s] == XHCI_PSW_SYS_9X, "another type: automatic");
        CHECK_EQ(life.SettingSource, XHCI_REG_FROM_MACHINE,
                 "the machine-wide key masks");
        CHECK_EQ(life.SettingState, XHCI_REG_OTHER, "recorded");

        ntInit(&f, &io, systems[s]);
        rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
        f.SetState = XHCI_REG_TAKEN;
        f.SetSource = XHCI_REG_FROM_DEVICE;
        f.SetValue = 0;
        CHECK_EQ(XhciPswLifeStart(&io, &life, &st),
                 systems[s] == XHCI_PSW_SYS_9X, "0: automatic");

        ntInit(&f, &io, systems[s]);
        rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
        f.SetState = XHCI_REG_TAKEN;
        f.SetSource = XHCI_REG_FROM_DEVICE;
        f.SetValue = 1;
        CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "1: bypass routes");
        CHECK_EQ(life.Verdict, systems[s] == XHCI_PSW_SYS_9X
                                   ? XHCI_PSW_V_ROUTE_9X
                                   : XHCI_PSW_V_BYPASS,
                 "bypass on NT, nothing to bypass on 9x");
        CHECK_EQ(life.SettingSource, XHCI_REG_FROM_DEVICE, "this key's");

        ntInit(&f, &io, systems[s]);
        f.SetState = XHCI_REG_TAKEN;
        f.SetSource = XHCI_REG_FROM_MACHINE;
        f.SetValue = 2;
        fakeArmed(&f);
        CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "2 always holds");
        CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETTING, "by the setting");
        checkHeldLife(&f, &io, &life, "2's lifetime");

        ntInit(&f, &io, systems[s]);
        rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
        f.SetState = XHCI_REG_TAKEN;
        f.SetValue = 3;
        CHECK_EQ(XhciPswLifeStart(&io, &life, &st),
                 systems[s] == XHCI_PSW_SYS_9X, "3: automatic");

        ntInit(&f, &io, systems[s]);
        rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
        f.SetState = XHCI_REG_TAKEN;
        f.SetValue = 0xFFFFFFFFUL;
        CHECK_EQ(XhciPswLifeStart(&io, &life, &st),
                 systems[s] == XHCI_PSW_SYS_9X, "all-ones: automatic");
        CHECK_EQ(life.SettingValue, 0xFFFFFFFFUL, "recorded whole");
    }

    /* 98 SE and ME: nothing but the setting is read. */
    ntInit(&f, &io, XHCI_PSW_SYS_9X);
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "98 SE routes");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_ROUTE_9X, "no hold, no guard");
    CHECK_EQ(f.Asked[0] + f.Asked[1] + f.Asked[2] + f.Asked[3] +
             f.Asked[4] + f.Asked[5] + f.Asked[6] + f.Asked[7] +
             f.Asked[8] + f.Asked[9], 0, "no reading on 98 SE");
    CHECK_EQ(f.GuardOps, 0, "no guard operation");
    CHECK_EQ(f.SetReads, 1, "the setting read once");

    CHECK_EQ(XhciPswSettingInForce(XHCI_REG_TAKEN, 1, XHCI_PSW_SYS_NT6),
             XHCI_PSW_SETTING_BYPASS, "1 on NT");
    CHECK_EQ(XhciPswSettingInForce(XHCI_REG_TAKEN, 1, XHCI_PSW_SYS_9X),
             XHCI_PSW_SETTING_AUTO, "1 on 9x");
    CHECK_EQ(XhciPswSettingInForce(XHCI_REG_OTHER, 2, XHCI_PSW_SYS_NT6),
             XHCI_PSW_SETTING_AUTO, "another type");
    CHECK_EQ(XhciPswSettingInForce(XHCI_REG_ABSENT, 2, XHCI_PSW_SYS_9X),
             XHCI_PSW_SETTING_AUTO, "absent");
}

/* XhciIntelPortSwitch over the controllers: the step's callbacks only
 * where the mode is on and the board passes. */
static void test_step_reach(void)
{
    static const ULONG ids[3] = { ID_LISTED, ID_SPT, ID_QEMU };
    static const ULONG values[4] = { 0xFFFFFFFFUL, 0, 1, 2 };
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;
    ULONG i;
    ULONG v;
    ULONG on;
    ULONG asked;

    for (i = 0; i < 3; i++) {
        for (v = 0; v < 4; v++) {
            ntInit(&f, &io, XHCI_PSW_SYS_NT6);
            fakeArmed(&f);
            f.Id = ids[i];
            f.ValueFound = (values[v] != 0xFFFFFFFFUL);
            f.Value = f.ValueFound ? values[v] : 0;
            (VOID)XhciPswLifeStart(&io, &life, &st);
            on = (ids[i] == ID_LISTED && f.ValueFound && values[v] != 0) ||
                 (ids[i] == ID_SPT && f.ValueFound && values[v] == 2);
            asked = f.Asked[XHCI_PSW_Q_SSIP] + f.Asked[XHCI_PSW_Q_READY] +
                    f.GuardOps + f.SetReads;
            CHECK_EQ(asked != 0, on, "the step only where the mode is on");
            CHECK_EQ(life.On, on, "routed where on");
            CHECK_EQ(life.Step, on, "Step says so");
            if (ids[i] == ID_LISTED && f.ValueFound) {
                CHECK_EQ(life.SwitchSource, XHCI_REG_FROM_DEVICE,
                         "the switch's key");
            }
        }
    }
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.SwitchSource = XHCI_REG_FROM_MACHINE;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(life.SwitchSource, XHCI_REG_FROM_MACHINE, "machine-wide");

    /* The Sony board and an unread subsystem: no step. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.Subsystem = 0x90A8104DUL;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(life.Decision, XHCI_PSW_DECIDE_BOARD_REFUSED, "Sony");
    CHECK_EQ(f.GuardOps + f.SetReads + f.Asked[XHCI_PSW_Q_READY], 0,
             "no step for the Sony board");
    CHECK_EQ(XhciPswShutdownAllowed(&life), 1, "its hand-back stands");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.Value = 0;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(XhciPswShutdownAllowed(&life), 1, "off keeps the hand-back");
    CHECK_EQ(XhciPswShutdownAllowed(NULL), 1, "no lifetime");

    /* An incomplete io holds. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    io.Query = NULL;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "no query: held");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SETUP_UNREAD, "unread");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    io.Guard = NULL;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "no guard: held");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SESSION, "session unknown");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_GUARD_NO_ROUTINE, "no routine");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    io.Query = NULL;
    io.Guard = NULL;
    io.System = 0;
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "an io of zeros holds");
}

/* The guard, Alternative A (record 16 section 21.5.1). */
static ULONG ntStart(FAKE_CFG *f, XHCI_PSW_IO *io, XHCI_PSW_LIFE *life)
{
    XHCI_PSW_STATE st;
    ULONG r;

    f->Count = 0;
    r = XhciPswLifeStart(io, life, &st);
    CHECK_EQ(f->Opened, f->Closed, "every session handle closed");
    return r;
}

static void test_guard(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_LIFE life;

    /* A first session: the key created with S = G = 0, G armed, held. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the first session holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_FIRST, "first session");
    CHECK_EQ(life.Guard.Disposition, XHCI_PSW_KEY_CREATED, "created");
    CHECK_EQ(life.Guard.S, 0, "S 0");
    CHECK_EQ(f.GValue, 1, "G armed");
    CHECK_EQ(f.WritesG, 1, "one G write");
    CHECK_EQ(life.Guard.WriteG, XHCI_PSW_ST_SUCCESS, "its status");
    /* A later start of the same session: the key opened, S 0, held. */
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the same session holds again");
    CHECK_EQ(life.Guard.Disposition, XHCI_PSW_KEY_OPENED, "opened");
    CHECK_EQ(life.Guard.S, 0, "S 0 still");
    CHECK_EQ(f.WritesG, 1, "G not written again");
    CHECK_EQ(f.WritesS, 1, "S not written again");
    /* The next boot: a new key with S = G = 1. */
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "the next boot routes");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_ALLOWS, "allows");
    CHECK_EQ(life.Guard.S, 1, "S 1");
    CHECK_EQ(life.Guard.WriteG, XHCI_PSW_ST_NOT_RUN, "no G write");
    CHECK_EQ(ntStart(&f, &io, &life), 1, "and its later starts");

    /* G's readings: absent, 0, another number and another type are 0. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    f.GState = XHCI_REG_TAKEN;
    f.GValue = 0;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "G 0 holds");
    CHECK_EQ(f.GValue, 1, "and arms");
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    f.GState = XHCI_REG_TAKEN;
    f.GValue = 5;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "G 5 reads as 0");
    CHECK_EQ(life.Guard.G, 0, "G 0");
    CHECK_EQ(f.GValue, 1, "written 1");
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    f.GState = XHCI_REG_OTHER;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "G of another type reads as 0");
    CHECK_EQ(life.Guard.Ready.State, XHCI_PSW_RD_MALFORMED, "recorded");

    /* G's write failing: held, and the next boot begins unarmed. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.WriteGStatus = 0xC0000022UL;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "held");
    CHECK_EQ(life.Guard.WriteG, 0xC0000022UL, "the failed write recorded");
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the next boot holds again");
    CHECK_EQ(life.Guard.S, 0, "S 0");
    f.WriteGStatus = 0;
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the write succeeds: still held");
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "and the boot after routes");

    /* Step 2: the routine not there - every start holds, no arming. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT5);
    f.Routine = 0;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "no routine holds");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_SESSION, "session unknown");
    CHECK_EQ(life.VerdictValue, XHCI_PSW_GUARD_NO_ROUTINE, "no routine");
    CHECK_EQ(f.WritesG, 0, "G not armed");
    CHECK_EQ(f.Opened, 0, "no key");

    /* Step 3: every failure of the create, and a disposition of neither. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.OpenStatus = 0xC0000022UL;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the create refused");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_KEY_FAILED, "key failed");
    CHECK_EQ(life.Guard.OpenStatus, 0xC0000022UL, "its status");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.OpenDisp = 3;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "an unknown disposition");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_KEY_FAILED, "key failed");
    CHECK_EQ(f.Closed, 1, "its handle closed");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.OpenDisp = 3;
    (VOID)ntStart(&f, &io, &life);
    CHECK_EQ(f.WritesG, 0, "no arming on a key that failed");

    /* Step 4: the volatility test's every outcome. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.ProbeStatus = 0xC0000022UL;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the probe refused otherwise");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_NOT_VOLATILE, "not proven");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.ProbeStatus = 0xC000009AUL;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "out of resources");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_NOT_VOLATILE, "not proven");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.KeyThere = 1;
    f.KeyVolatile = 0;
    f.KeySState = XHCI_REG_TAKEN;
    f.KeySValue = 1;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "a stable key holding 1 holds");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_NOT_VOLATILE, "not volatile");
    CHECK_EQ(life.Guard.ProbeStatus, XHCI_PSW_ST_SUCCESS, "the child made");
    CHECK_EQ(f.Children, 1, "one stray child");
    CHECK_EQ(f.Closed, 1, "closed");

    /* Step 5, a created key: S's write or its read-back failing. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.WriteSStatus = 0xC0000022UL;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "S's write failing holds");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_S_UNKNOWN, "unknown");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.SessionRawOn = 1;
    rawSet(&f.SessionRaw, 1, XHCI_PSW_REG_DWORD, 4, 0xC0000022UL, 1);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the read-back unread holds");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_S_UNKNOWN, "unknown");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.SessionRawOn = 1;
    rawDword(&f.SessionRaw, 0);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "a read-back of another value");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_S_UNKNOWN, "unknown");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.SessionRawOn = 1;
    rawAbsent(&f.SessionRaw);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "a read-back absent");
    CHECK_EQ(f.WritesG, 0, "no arming on unknown evidence");

    /* Step 5, an existing key: S taken 0 or 1, else unknown. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.KeyThere = 1;
    f.KeyVolatile = 1;
    f.KeySState = XHCI_REG_TAKEN;
    f.KeySValue = 1;
    CHECK_EQ(ntStart(&f, &io, &life), 1,
             "an existing volatile key with S 1 permits, G not consulted");
    CHECK_EQ(f.GValue, 1, "and arms G");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.KeyThere = 1;
    f.KeyVolatile = 1;
    f.KeySState = XHCI_REG_TAKEN;
    f.KeySValue = 0;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "S 0 holds with G 1");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_FIRST, "first session");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.KeyThere = 1;
    f.KeyVolatile = 1;
    f.KeySState = XHCI_REG_ABSENT;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "S absent holds");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_S_UNKNOWN, "unknown");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.KeyThere = 1;
    f.KeyVolatile = 1;
    f.KeySState = XHCI_REG_OTHER;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "S malformed holds");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.KeyThere = 1;
    f.KeyVolatile = 1;
    f.KeySState = XHCI_REG_TAKEN;
    f.KeySValue = 2;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "S 2 holds");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_S_UNKNOWN, "unknown");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.KeyThere = 1;
    f.KeyVolatile = 1;
    f.SessionRawOn = 1;
    rawSet(&f.SessionRaw, 0, 0, 0, 0xC0000022UL, 0);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "S unread holds");
}

/* Failure, then recovery, within one boot: every start of that boot
 * holds, and G stays 0 until a start with proven evidence, which itself
 * holds; the next boot permits. */
static void test_guard_recovery(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_LIFE life;
    ULONG k;

    for (k = 0; k < 3; k++) {
        ntInit(&f, &io, k == 0 ? XHCI_PSW_SYS_NT5 : XHCI_PSW_SYS_NT6);
        if (k == 0) {
            f.Routine = 0;
        } else if (k == 1) {
            f.OpenStatus = 0xC0000022UL;
        } else {
            f.SessionRawOn = 1;
            rawAbsent(&f.SessionRaw);
        }
        CHECK_EQ(ntStart(&f, &io, &life), 0, "the failing start holds");
        CHECK_EQ(f.GState, XHCI_REG_ABSENT, "G not armed");
        CHECK_EQ(ntStart(&f, &io, &life), 0, "a second failing start");
        CHECK_EQ(f.GState, XHCI_REG_ABSENT, "G still not armed");
        f.Routine = 1;
        f.OpenStatus = 0;
        f.SessionRawOn = 0;
        CHECK_EQ(ntStart(&f, &io, &life), 0, "the recovering start holds");
        CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_FIRST, "S 0");
        CHECK_EQ(f.GValue, 1, "and arms G");
        CHECK_EQ(ntStart(&f, &io, &life), 0, "the same boot stays held");
        fakeReboot(&f);
        CHECK_EQ(ntStart(&f, &io, &life), 1, "the next boot permits");
    }

    /* The read-back's failure on a key already created: the key now holds
     * an S that disagrees or nothing, so that boot never permits. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    f.WriteSStatus = 0xC0000022UL;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "S's write failed");
    f.WriteSStatus = 0;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the key exists, S absent: held");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_S_UNKNOWN, "unknown");
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "the next boot permits");

    /* The helper never answering (x86): every start, every boot holds and
     * G stays 0; the bypass routes. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.Routine = 0;
    for (k = 0; k < 4; k++) {
        CHECK_EQ(ntStart(&f, &io, &life), 0, "no helper: held");
        fakeReboot(&f);
    }
    CHECK_EQ(f.GState, XHCI_REG_ABSENT, "G never armed");
    f.SetState = XHCI_REG_TAKEN;
    f.SetValue = 1;
    CHECK_EQ(ntStart(&f, &io, &life), 1, "the bypass routes");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_BYPASS, "bypass");
    CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_NO_ROUTINE,
             "the guard recorded, not obeyed");

    /* A stable key of the name, holding 0, 1 or nothing, with G 0 and
     * G 1: every start holds, G is not armed; deleted and restarted, it
     * recovers. */
    for (k = 0; k < 6; k++) {
        ntInit(&f, &io, XHCI_PSW_SYS_NT6);
        if (k >= 3) {
            fakeArmed(&f);
        }
        f.KeyThere = 1;
        f.KeyVolatile = 0;
        f.KeySState = (k % 3 == 2) ? XHCI_REG_ABSENT : XHCI_REG_TAKEN;
        f.KeySValue = k % 3;
        CHECK_EQ(ntStart(&f, &io, &life), 0, "a stable key holds");
        CHECK_EQ(life.Guard.Outcome, XHCI_PSW_GUARD_NOT_VOLATILE,
                 "not volatile");
        CHECK_EQ(f.WritesG, 0, "G not armed");
        fakeReboot(&f);
        CHECK_EQ(ntStart(&f, &io, &life), 0, "and after a restart");
        CHECK_EQ(f.WritesS, 0, "S never written into it");
        f.KeyThere = 0;
        fakeReboot(&f);
        CHECK_EQ(ntStart(&f, &io, &life), k >= 3,
                 "deleted and restarted: G 1 permits, G 0 arms");
        if (k < 3) {
            fakeReboot(&f);
            CHECK_EQ(ntStart(&f, &io, &life), 1, "and then permits");
        }
    }
}

/* Section 21.5.3's rows and section 21.10's other cases for A. */
static void test_guard_rows(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_LIFE life;

    /* Arming, then a further installation pass in the same session, with
     * Setup's values active and then complete: held both times. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "armed");
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "a pass with Setup active");
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 0);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "a pass with Setup complete");
    CHECK_EQ(life.Verdict, XHCI_PSW_V_HELD_FIRST, "the guard holds it");

    /* A crash or power loss after arming: G persisted permits at the next
     * boot; G's write lost holds that boot and arms. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    (VOID)ntStart(&f, &io, &life);
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "G persisted: permits");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    (VOID)ntStart(&f, &io, &life);
    f.GState = XHCI_REG_ABSENT;
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "G lost: held again");
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "and the boot after permits");

    /* A deployed image: G 0 holds its first session, G 1 permits. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.GState = XHCI_REG_TAKEN;
    f.GValue = 0;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "deployed with G 0: held");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "deployed with G 1: permits");

    /* An update over a release without the value: the INF adds it at 0,
     * the update's start arms and holds, the next boot permits. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.GState = XHCI_REG_TAKEN;
    f.GValue = 0;
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the update's start holds");
    fakeReboot(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "the next boot permits");

    /* An update over an armed install: no session key yet - created with
     * S 1, permits at once; an existing key with S 0 holds. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "no key: S 1 at once");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    (VOID)ntStart(&f, &io, &life);
    CHECK_EQ(ntStart(&f, &io, &life), 0, "the armed session's own update");

    /* Sleep, hibernation and Fast Startup are returns to D0, not starts:
     * a held lifetime's resume makes no access; only a restart ends the
     * session. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    (VOID)ntStart(&f, &io, &life);
    checkHeldLife(&f, &io, &life, "a held lifetime across D0");

    /* Two controllers, one session: each its own G and key; the second
     * controller's first start holds whatever the first did. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    CHECK_EQ(ntStart(&f, &io, &life), 1, "controller A, armed earlier");
    {
        FAKE_CFG g;
        XHCI_PSW_IO io2;
        XHCI_PSW_LIFE life2;

        ntInit(&g, &io2, XHCI_PSW_SYS_NT6);
        CHECK_EQ(ntStart(&g, &io2, &life2), 0, "controller B, new: held");
        CHECK_EQ(ntStart(&f, &io, &life), 1, "A again: still permits");
    }
}

/* The latch: nothing re-decides until the next start. */
static void test_latch(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;

    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    fakeArmed(&f);
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 0, "held");
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 0);
    f.Count = 0;
    CHECK_EQ(XhciPswLifeResume(&io, &life, &st), 0,
             "Setup ending changes nothing at a resume");
    CHECK_EQ(f.Count, 0, "no access");
    CHECK_EQ(f.Asked[XHCI_PSW_Q_SSIP], 1, "and no re-read");
    CHECK_EQ(XhciPswLifeStart(&io, &life, &st), 1, "the next start routes");

    /* A routed lifetime is not undone by a later reading. */
    rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1);
    f.Count = 0;
    CHECK_EQ(XhciPswLifeResume(&io, &life, &st), 1, "it routes again at D0");
    CHECK_EQ(XhciPswShutdownAllowed(&life), 1, "and hands back");
}

/* The record: built from each verdict's lifetime and read back. */
static void test_record(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;
    XHCI_PSW_LIFE back;
    XHCI_PSW_REC_IN in;
    XHCI_PSW_REC_IN inBack;
    ULONG w[XHCI_PSW_REC_WORDS];
    UCHAR b[XHCI_PSW_REC_BYTES + 8];
    ULONG i;
    ULONG k;
    ULONG wrong;

    in.TimeLo = 0x11223344UL;
    in.TimeHi = 0x01D9ABCDUL;
    in.DriverVersion = 0x03050100UL;
    in.Flavour = 2;
    wrong = 0;
    for (k = 0; k < 12; k++) {
        ntInit(&f, &io, XHCI_PSW_SYS_NT6);
        fakeArmed(&f);
        switch (k) {
        case 0: break;
        case 1: f.SetState = XHCI_REG_TAKEN; f.SetValue = 1;
                rawDword(&f.Raw[XHCI_PSW_Q_SSIP], 1); break;
        case 2: f.SetState = XHCI_REG_TAKEN; f.SetValue = 2;
                f.SetSource = XHCI_REG_FROM_MACHINE; break;
        case 3: rawDword(&f.Raw[XHCI_PSW_Q_OOBE], 1); break;
        case 4: rawDword(&f.Raw[XHCI_PSW_Q_TYPE], 2); break;
        case 5: rawSet(&f.Raw[XHCI_PSW_Q_SSIP], 1, XHCI_PSW_REG_DWORD, 4,
                       0xC0000022UL, 0); break;
        case 6: fakeSz(&f, XHCI_PSW_Q_OPTIONS, "/MININT"); break;
        case 7: fakeSz(&f, XHCI_PSW_Q_IMAGE, "IMAGE_STATE_UNDEPLOYABLE");
                break;
        case 8: rawAbsent(&f.Raw[XHCI_PSW_Q_HIVE]); break;
        case 9: f.GState = XHCI_REG_ABSENT; break;
        case 10: f.Routine = 0; break;
        default: io.System = XHCI_PSW_SYS_9X; break;
        }
        (VOID)XhciPswLifeStart(&io, &life, &st);
        XhciPswRecordBuild(&life, &in, w);
        for (i = 0; i < XHCI_PSW_REC_WORDS; i++) {
            b[i * 4] = (UCHAR)(w[i] & 0xFF);
            b[i * 4 + 1] = (UCHAR)((w[i] >> 8) & 0xFF);
            b[i * 4 + 2] = (UCHAR)((w[i] >> 16) & 0xFF);
            b[i * 4 + 3] = (UCHAR)((w[i] >> 24) & 0xFF);
        }
        if (XhciPswRecordRead(1, 1, b, XHCI_PSW_REC_BYTES, &back,
                              &inBack) != XHCI_PSW_REC_OK ||
            back.Decision != life.Decision || back.Verdict != life.Verdict ||
            back.VerdictValue != life.VerdictValue ||
            back.Setting != life.Setting ||
            back.SettingSource != life.SettingSource ||
            back.SwitchSource != life.SwitchSource ||
            back.Setup[0].Value != life.Setup[0].Value ||
            back.Setup[0].Status != life.Setup[0].Status ||
            back.Setup[0].State != life.Setup[0].State ||
            back.PeOptions.Value != life.PeOptions.Value ||
            back.Image.Value != life.Image.Value ||
            back.Hive.State != life.Hive.State ||
            back.Guard.Outcome != life.Guard.Outcome ||
            back.Guard.G != life.Guard.G || back.Guard.S != life.Guard.S ||
            back.Guard.OpenStatus != life.Guard.OpenStatus ||
            back.Guard.WriteG != life.Guard.WriteG ||
            back.On != life.On || back.System != life.System ||
            back.RouteWritten != life.RouteWritten ||
            back.ImageText[0] != life.ImageText[0] ||
            back.ImageText[23] != life.ImageText[23] ||
            inBack.TimeHi != in.TimeHi || inBack.Flavour != in.Flavour) {
            wrong++;
        }
    }
    CHECK_EQ(wrong, 0, "every verdict's record reads back");
    CHECK_EQ(w[0], XHCI_PSW_REC_MAGIC, "the magic");
    CHECK_EQ(w[1], XHCI_PSW_REC_VERSION | (XHCI_PSW_REC_WORDS << 16),
             "the version and length");

    CHECK_EQ(XhciPswRecordRead(0, 1, b, XHCI_PSW_REC_BYTES, &back, NULL),
             XHCI_PSW_REC_ABSENT, "absent");
    CHECK_EQ(XhciPswRecordRead(1, 0, b, XHCI_PSW_REC_BYTES, &back, NULL),
             XHCI_PSW_REC_FOREIGN, "not binary");
    CHECK_EQ(XhciPswRecordRead(1, 1, b, XHCI_PSW_REC_BYTES - 1, &back, NULL),
             XHCI_PSW_REC_FOREIGN, "short");
    CHECK_EQ(XhciPswRecordRead(1, 1, b, XHCI_PSW_REC_BYTES + 8, &back, NULL),
             XHCI_PSW_REC_OK, "longer is read");
    b[0] ^= 1;
    CHECK_EQ(XhciPswRecordRead(1, 1, b, XHCI_PSW_REC_BYTES, &back, NULL),
             XHCI_PSW_REC_FOREIGN, "another magic");
    CHECK_EQ(back.Verdict, XHCI_PSW_V_NONE, "a foreign record clears");
    b[0] ^= 1;
    b[4] = 2;
    CHECK_EQ(XhciPswRecordRead(1, 1, b, XHCI_PSW_REC_BYTES, &back, NULL),
             XHCI_PSW_REC_FOREIGN, "another version");
    CHECK_EQ(XhciPswRecordRead(1, 1, NULL, 0, &back, NULL),
             XHCI_PSW_REC_FOREIGN, "no data");

    /* An off start's record: the step not reached. */
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.Value = 0;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    XhciPswRecordBuild(&life, &in, w);
    CHECK_EQ(w[6] & 0xFFUL, XHCI_PSW_DECIDE_OFF, "off recorded");
    CHECK_EQ((w[6] >> 8) & 0xFFUL, XHCI_PSW_V_NONE, "no verdict");
    CHECK_EQ((w[5] >> 16) & 0xFFUL, 0, "the step not reached");
    XhciPswRecordBuild(NULL, NULL, w);
    CHECK_EQ(w[0], XHCI_PSW_REC_MAGIC, "a header alone");
}

/* The record's scope (record 16 section 21.3): a start that did not reach
 * the step - not Intel, an unlisted Intel controller without 2, the
 * switchover off, the excluded board, an unread identity - has no
 * registry record, and its step's callbacks were never called. */
static void test_record_scope(void)
{
    FAKE_CFG f;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    XHCI_PSW_LIFE life;
    ULONG k;

    for (k = 0; k < 7; k++) {
        ntInit(&f, &io, XHCI_PSW_SYS_NT6);
        switch (k) {
        case 0: f.Id = ID_QEMU; break;
        case 1: f.Id = ID_SPT; f.Value = 1; break;
        case 2: f.Value = 0; break;
        case 3: f.ValueFound = 0; break;
        case 4: f.Subsystem = 0x90A8104DUL; break;
        case 5: f.FailKind = 'r'; f.FailOffset = XHCI_PSW_PCI_SUBSYSTEM; break;
        default: f.FailKind = 'r'; f.FailOffset = XHCI_PSW_PCI_ID; break;
        }
        (VOID)XhciPswLifeStart(&io, &life, &st);
        CHECK_EQ(XhciPswRecordApplies(&life), 0,
                 "out of scope: no registry record");
        CHECK_EQ(f.SetReads + f.GuardOps + f.Asked[XHCI_PSW_Q_SSIP] +
                 f.Asked[XHCI_PSW_Q_READY] + f.Asked[XHCI_PSW_Q_SESSION], 0,
                 "out of scope: no reading, no key, no write of the step");
        CHECK_EQ(f.Opened + f.WritesG + f.WritesS, 0, "nothing created");
    }
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(XhciPswRecordApplies(&life), 1, "a listed controller at 1");
    ntInit(&f, &io, XHCI_PSW_SYS_NT6);
    f.Id = ID_SPT;
    f.Value = 2;
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(XhciPswRecordApplies(&life), 1, "an unlisted one at 2");
    ntInit(&f, &io, XHCI_PSW_SYS_9X);
    (VOID)XhciPswLifeStart(&io, &life, &st);
    CHECK_EQ(XhciPswRecordApplies(&life), 1, "98 SE, the switchover on");
    CHECK_EQ(XhciPswRecordApplies(NULL), 0, "no lifetime");
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
    test_classify_dword();
    test_classify_string();
    test_tokens();
    test_exists();
    test_setup_matrix();
    test_setup_routes();
    test_pe();
    test_image();
    test_setting();
    test_step_reach();
    test_guard();
    test_guard_recovery();
    test_guard_rows();
    test_latch();
    test_record();
    test_record_scope();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
