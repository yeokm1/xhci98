/*
 * test_pirq.c - host vectors for the Intel PCH interrupt routing quirk's
 * pure half (src\xhci_pirq.c; roadmap-hcd.md task 36.3): the value rule,
 * the reviewed pairs and the verified E7250, the target PIRQ in APIC and
 * PIC mode with every reserved PIRQx_ROUT encoding, the delivery, and the
 * run over a modelled machine - which reads it makes, where each check
 * stops it, the one 16-bit write it may make and its read-back.
 */

#include <stdio.h>
#include "../src/xhci_pirq.h"
#include "test_harness.h"

#define FAKE_LOG 32

/* The E7250 (issue 12 sections 2 and 3.5): the xHCI at 00:14.0, the LPC
 * at 00:1F.0 with RCBA FED1C000, D20IP INTA, D20IR 3210 (INTA to PIRQA)
 * while the OS was told IRQ 21. */
#define E7250_XHCI      0x9CB18086UL
#define E7250_SUBSYSTEM 0x062D1028UL
#define E7250_LPC       0x9CC38086UL
#define E7250_RCBA      0xFED1C001UL
#define E7250_BAR0      0xF7E00004UL
#define E7250_D20IR     0x3210U

typedef struct _FAKE_LOG_ENTRY {
    char Kind;          /* 'o' own, 's' slot, 'm' map, 'r' 32, 'h' 16,
                         * 'w' write 16, 'u' unmap                           */
    ULONG Where;        /* offset, device or physical address              */
    ULONG Value;
} FAKE_LOG_ENTRY;

typedef struct _FAKE_PC {
    ULONG OwnId;
    ULONG OwnBar0;
    ULONG OwnBar1;
    ULONG OwnSubsystem;
    ULONG OwnInterrupt;     /* 3Ch: line 7:0, pin 15:8                     */
    UCHAR Slot14[XHCI_PIRQ_SLOT_BYTES];
    UCHAR Slot1F[XHCI_PIRQ_LPC_BYTES];
    ULONG Slot14Bytes;      /* what the bus-data read returns              */
    ULONG Slot1FBytes;
    ULONG D20ip;
    USHORT D20ir;
    USHORT IarStuck;        /* IAR bits that do not take a write           */
    ULONG Mapped;           /* the physical address mapped, 0 none         */
    ULONG Maps;
    ULONG Unmaps;
    char FailKind;          /* one access refused: 'o' 'm' 'r' 'h' 'w'     */
    ULONG FailWhere;
    ULONG ReadBackAllOnes;
    FAKE_LOG_ENTRY Log[FAKE_LOG];
    ULONG Count;
} FAKE_PC;

static void fakeLog(FAKE_PC *f, char kind, ULONG where, ULONG value)
{
    if (f->Count < FAKE_LOG) {
        f->Log[f->Count].Kind = kind;
        f->Log[f->Count].Where = where;
        f->Log[f->Count].Value = value;
    }
    f->Count++;
}

static ULONG fakeCount(FAKE_PC *f, char kind)
{
    ULONG i;
    ULONG n;

    n = 0;
    for (i = 0; i < f->Count && i < FAKE_LOG; i++) {
        if (f->Log[i].Kind == kind) {
            n++;
        }
    }
    return n;
}

static void put32(UCHAR *b, ULONG offset, ULONG v)
{
    b[offset] = (UCHAR)(v & 0xFF);
    b[offset + 1] = (UCHAR)((v >> 8) & 0xFF);
    b[offset + 2] = (UCHAR)((v >> 16) & 0xFF);
    b[offset + 3] = (UCHAR)((v >> 24) & 0xFF);
}

static ULONG fakeOwn(PVOID context, ULONG offset, PULONG value)
{
    FAKE_PC *f;

    f = (FAKE_PC *)context;
    fakeLog(f, 'o', offset, 0);
    if (f->FailKind == 'o' && f->FailWhere == offset) {
        return 0;
    }
    switch (offset) {
    case XHCI_PIRQ_CFG_ID:          *value = f->OwnId; return 1;
    case XHCI_PIRQ_CFG_BAR0:        *value = f->OwnBar0; return 1;
    case XHCI_PIRQ_CFG_BAR1:        *value = f->OwnBar1; return 1;
    case XHCI_PIRQ_CFG_SUBSYSTEM:   *value = f->OwnSubsystem; return 1;
    case XHCI_PIRQ_CFG_INTERRUPT:   *value = f->OwnInterrupt; return 1;
    }
    return 0;
}

static ULONG fakeSlot(PVOID context, ULONG device, ULONG function,
                      PUCHAR buffer, ULONG length)
{
    FAKE_PC *f;
    const UCHAR *from;
    ULONG have;
    ULONG i;

    f = (FAKE_PC *)context;
    fakeLog(f, 's', device, length);
    if (function != 0) {
        return 0;
    }
    if (device == XHCI_PIRQ_XHCI_DEVICE) {
        from = f->Slot14;
        have = f->Slot14Bytes;
    } else if (device == XHCI_PIRQ_LPC_DEVICE) {
        from = f->Slot1F;
        have = f->Slot1FBytes;
    } else {
        return 0;
    }
    if (have > length) {
        have = length;
    }
    for (i = 0; i < have; i++) {
        buffer[i] = from[i];
    }
    return have;
}

static ULONG fakeMap(PVOID context, ULONG physical)
{
    FAKE_PC *f;

    f = (FAKE_PC *)context;
    fakeLog(f, 'm', physical, 0);
    if (f->FailKind == 'm') {
        return 0;
    }
    f->Mapped = physical;
    f->Maps++;
    return 1;
}

static ULONG fakeRead32(PVOID context, ULONG offset, PULONG value)
{
    FAKE_PC *f;

    f = (FAKE_PC *)context;
    fakeLog(f, 'r', offset, 0);
    if (f->Mapped == 0 || (f->FailKind == 'r' && f->FailWhere == offset)) {
        return 0;
    }
    *value = (offset == XHCI_PIRQ_D20IP) ? f->D20ip : 0;
    return 1;
}

static ULONG fakeRead16(PVOID context, ULONG offset, PUSHORT value)
{
    FAKE_PC *f;

    f = (FAKE_PC *)context;
    fakeLog(f, 'h', offset, 0);
    if (f->Mapped == 0 || (f->FailKind == 'h' && f->FailWhere == offset)) {
        return 0;
    }
    if (f->ReadBackAllOnes && fakeCount(f, 'w') != 0) {
        *value = 0xFFFFU;
        return 1;
    }
    *value = (offset == XHCI_PIRQ_D20IR) ? f->D20ir : 0;
    return 1;
}

static ULONG fakeWrite16(PVOID context, ULONG offset, USHORT value)
{
    FAKE_PC *f;

    f = (FAKE_PC *)context;
    fakeLog(f, 'w', offset, value);
    if (f->Mapped == 0 || (f->FailKind == 'w' && f->FailWhere == offset)) {
        return 0;
    }
    if (offset == XHCI_PIRQ_D20IR) {
        f->D20ir = (USHORT)((value & (USHORT)~f->IarStuck) |
                            (f->D20ir & f->IarStuck));
    }
    return 1;
}

static VOID fakeUnmap(PVOID context)
{
    FAKE_PC *f;

    f = (FAKE_PC *)context;
    fakeLog(f, 'u', f->Mapped, 0);
    f->Mapped = 0;
    f->Unmaps++;
}

/* The slot copies follow the own registers: set those, then call this. */
static void fakeSlots(FAKE_PC *f)
{
    put32(f->Slot14, XHCI_PIRQ_CFG_ID, f->OwnId);
    put32(f->Slot14, XHCI_PIRQ_CFG_BAR0, f->OwnBar0);
    put32(f->Slot14, XHCI_PIRQ_CFG_BAR1, f->OwnBar1);
    put32(f->Slot14, XHCI_PIRQ_CFG_SUBSYSTEM, f->OwnSubsystem);
    put32(f->Slot14, XHCI_PIRQ_CFG_INTERRUPT, f->OwnInterrupt);
}

static void fakeLpc(FAKE_PC *f, ULONG id, ULONG rcba, ULONG ad, ULONG eh)
{
    put32(f->Slot1F, XHCI_PIRQ_CFG_ID, id);
    put32(f->Slot1F, XHCI_PIRQ_LPC_RCBA, rcba);
    put32(f->Slot1F, XHCI_PIRQ_LPC_PIRQ_AD, ad);
    put32(f->Slot1F, XHCI_PIRQ_LPC_PIRQ_EH, eh);
}

/* The E7250 in APIC mode, with the PIRQ routes BIOS leaves for PIC mode
 * disabled (IRQEN set), and the raw resource Windows gave: IRQ 21,
 * level-triggered, shared. */
static void fakeInit(FAKE_PC *f, XHCI_PIRQ_IO *io, XHCI_PIRQ_IRQ *irq)
{
    ULONG i;
    PUCHAR p;

    p = (PUCHAR)f;
    for (i = 0; i < sizeof(*f); i++) {
        p[i] = 0;
    }
    f->OwnId = E7250_XHCI;
    f->OwnBar0 = E7250_BAR0;
    f->OwnBar1 = 0;
    f->OwnSubsystem = E7250_SUBSYSTEM;
    f->OwnInterrupt = 0x00000115UL;
    for (i = 0; i < XHCI_PIRQ_SLOT_BYTES; i++) {
        f->Slot14[i] = 0;
    }
    fakeSlots(f);
    for (i = 0; i < XHCI_PIRQ_LPC_BYTES; i++) {
        f->Slot1F[i] = 0;
    }
    fakeLpc(f, E7250_LPC, E7250_RCBA, 0x80808080UL, 0x80808080UL);
    f->Slot14Bytes = XHCI_PIRQ_SLOT_BYTES;
    f->Slot1FBytes = XHCI_PIRQ_LPC_BYTES;
    f->D20ip = 0x00000001UL;
    f->D20ir = E7250_D20IR;

    io->Own = fakeOwn;
    io->Slot = fakeSlot;
    io->Map = fakeMap;
    io->Read32 = fakeRead32;
    io->Read16 = fakeRead16;
    io->Write16 = fakeWrite16;
    io->Unmap = fakeUnmap;
    io->Context = f;
    io->Steerable = 1;

    irq->Count = 1;
    irq->Level = 21;
    irq->Vector = 21;
    irq->Flags = 0;
    irq->Share = 3;
}

static ULONG run(FAKE_PC *f, XHCI_PIRQ_IO *io, XHCI_PIRQ_IRQ *irq,
                 PXHCI_PIRQ_STATE st, ULONG found, ULONG value,
                 ULONG delivery)
{
    XhciPirqLatch(st, found, value);
    f->Count = 0;
    return XhciPirqRun(io, XHCI_PIRQ_WHEN_START, delivery, irq, st);
}

static void zeroState(PXHCI_PIRQ_STATE st)
{
    ULONG i;
    PUCHAR p;

    p = (PUCHAR)st;
    for (i = 0; i < sizeof(*st); i++) {
        p[i] = 0;
    }
}

/* ---------------------------------------------------------------------- */

static void test_mode(void)
{
    CHECK_EQ(XhciPirqMode(1, 0), XHCI_PIRQ_MODE_OFF, "0 is off");
    CHECK_EQ(XhciPirqMode(1, 1), XHCI_PIRQ_MODE_VERIFIED, "1 verified");
    CHECK_EQ(XhciPirqMode(1, 2), XHCI_PIRQ_MODE_REVIEWED, "2 reviewed");
    CHECK_EQ(XhciPirqMode(1, 3), XHCI_PIRQ_MODE_VERIFIED, "3 is 1");
    CHECK_EQ(XhciPirqMode(1, 0xFFFFFFFFUL), XHCI_PIRQ_MODE_VERIFIED,
             "all-ones is 1");
    CHECK_EQ(XhciPirqMode(0, 0), XHCI_PIRQ_MODE_VERIFIED, "missing is 1");
    CHECK_EQ(XhciPirqMode(0, 2), XHCI_PIRQ_MODE_VERIFIED,
             "a value not found does not count");
}

static void test_ids(void)
{
    static const ULONG reviewed[] = {
        0x1E318086UL, 0x8C318086UL, 0x9C318086UL, 0x8CB18086UL,
        0x9CB18086UL, 0x8D318086UL
    };
    static const ULONG pairs[][2] = {
        { 0x1E318086UL, 0x1E448086UL }, { 0x1E318086UL, 0x1E5B8086UL },
        { 0x8C318086UL, 0x8C448086UL }, { 0x8C318086UL, 0x8C5C8086UL },
        { 0x9C318086UL, 0x9C438086UL }, { 0x9C318086UL, 0x9C458086UL },
        { 0x8CB18086UL, 0x8CC38086UL }, { 0x8CB18086UL, 0x8CC68086UL },
        { 0x9CB18086UL, 0x9CC38086UL }, { 0x8D318086UL, 0x8D448086UL },
        { 0x8D318086UL, 0x8D478086UL }
    };
    /* Engineering samples, coreboot-only ids, range members outside the
     * reviewed list, Y-line LPCs for 9CB1, a pair crossed with another
     * chipset's LPC, and the right id under another vendor. */
    static const ULONG unlisted[][2] = {
        { 0x8C318086UL, 0x8C418086UL }, { 0x8C318086UL, 0x8C428086UL },
        { 0x9C318086UL, 0x9C418086UL }, { 0x9C318086UL, 0x9C478086UL },
        { 0x8CB18086UL, 0x8CC18086UL }, { 0x8CB18086UL, 0x8CC28086UL },
        { 0x1E318086UL, 0x1E408086UL }, { 0x1E318086UL, 0x1E5F8086UL },
        { 0x1E318086UL, 0x1E458086UL }, { 0x9CB18086UL, 0x9CC18086UL },
        { 0x9CB18086UL, 0x9CC68086UL }, { 0x9CB18086UL, 0x8CC38086UL },
        { 0x8CB18086UL, 0x9CC38086UL }, { 0x9CB18086UL, 0x9CC31022UL },
        { 0x9D2F8086UL, 0x9D488086UL }
    };
    ULONG i;

    for (i = 0; i < sizeof(reviewed) / sizeof(reviewed[0]); i++) {
        CHECK_EQ(XhciPirqReviewed(reviewed[i]), 1, "a reviewed xHCI");
    }
    CHECK_EQ(XhciPirqReviewed(0x9D2F8086UL), 0, "Sunrise Point-LP is not");
    CHECK_EQ(XhciPirqReviewed(0xA12F8086UL), 0, "Sunrise Point-H is not");
    CHECK_EQ(XhciPirqReviewed(0x000D1B36UL), 0, "QEMU is not");
    CHECK_EQ(XhciPirqReviewed(0x9CB11022UL), 0, "the id under AMD is not");
    CHECK_EQ(XhciPirqReviewed(0x1E328086UL), 0, "a neighbouring id is not");
    for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        CHECK_EQ(XhciPirqPairListed(pairs[i][0], pairs[i][1]), 1,
                 "a reviewed pair");
    }
    for (i = 0; i < sizeof(unlisted) / sizeof(unlisted[0]); i++) {
        CHECK_EQ(XhciPirqPairListed(unlisted[i][0], unlisted[i][1]), 0,
                 "not a reviewed pair");
    }
    CHECK_EQ(XhciPirqVerified(E7250_XHCI, E7250_SUBSYSTEM, E7250_LPC), 1,
             "the E7250");
    CHECK_EQ(XhciPirqVerified(E7250_XHCI, 0x220017AAUL, E7250_LPC), 0,
             "another board on the same silicon");
    CHECK_EQ(XhciPirqVerified(E7250_XHCI, E7250_SUBSYSTEM, 0x9CC18086UL), 0,
             "another LPC");
    CHECK_EQ(XhciPirqVerified(0x8CB18086UL, E7250_SUBSYSTEM, E7250_LPC), 0,
             "another xHCI");
}

static void test_pic_irq(void)
{
    static const UCHAR routable[16] = {
        0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1
    };
    ULONG i;

    for (i = 0; i < 16; i++) {
        CHECK_EQ(XhciPirqPicIrq(i), routable[i], "PIRQx_ROUT's encodings");
    }
    CHECK_EQ(XhciPirqPicIrq(16), 0, "16 is no 8259 input");
    CHECK_EQ(XhciPirqPicIrq(0xFFFFFFFFUL), 0, "nor all-ones");
}

static void test_target(void)
{
    ULONG mode;
    ULONG target;
    ULONG i;

    for (i = 16; i <= 23; i++) {
        CHECK_EQ(XhciPirqTarget(i, 0, 0, 0, &mode, &target),
                 XHCI_PIRQ_D_NONE, "APIC");
        CHECK_EQ(mode, XHCI_PIRQ_IRQ_APIC, "APIC mode");
        CHECK_EQ(target, i - 16, "PIRQ = IRQ - 16");
    }
    CHECK_EQ(XhciPirqTarget(21, 0, 0, 5, &mode, &target), XHCI_PIRQ_D_NONE,
             "the E7250's 21");
    CHECK_EQ(target, 5, "PIRQF");
    CHECK_EQ(XhciPirqTarget(24, 0, 0, 0, &mode, &target),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "24 is past PIRQH");
    CHECK_EQ(target, XHCI_PIRQ_UNREAD, "no target");
    CHECK_EQ(XhciPirqTarget(0xFFFFFFFFUL, 0, 0, 0, &mode, &target),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "all-ones");

    /* Every reserved encoding is report-only, even when a PIRQx_ROUT
     * register holds it with IRQEN clear. */
    CHECK_EQ(XhciPirqTarget(0, 0x00000000UL, 0, 0, &mode, &target),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "0 reserved");
    CHECK_EQ(XhciPirqTarget(1, 0x00000001UL, 0, 0, &mode, &target),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "1 reserved");
    CHECK_EQ(XhciPirqTarget(2, 0x00000002UL, 0, 0, &mode, &target),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "2 reserved");
    CHECK_EQ(XhciPirqTarget(8, 0x00000008UL, 0, 0, &mode, &target),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "8 reserved");
    CHECK_EQ(XhciPirqTarget(13, 0x0000000DUL, 0, 0, &mode, &target),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "13 reserved");
    CHECK_EQ(mode, XHCI_PIRQ_IRQ_NONE, "no mode for a reserved IRQ");

    /* PIC: A disabled, B and F routed to 11, H to 10. */
    CHECK_EQ(XhciPirqTarget(11, 0x80800B80UL, 0x0A800B80UL, 0, &mode,
                            &target), XHCI_PIRQ_D_NONE, "PIC 11");
    CHECK_EQ(mode, XHCI_PIRQ_IRQ_PIC, "PIC mode");
    CHECK_EQ(target, 1, "the lowest routed to it, PIRQB");
    CHECK_EQ(XhciPirqTarget(11, 0x80800B80UL, 0x0A800B80UL, 5, &mode,
                            &target), XHCI_PIRQ_D_NONE, "PIC 11 at F");
    CHECK_EQ(target, 5, "the current one kept");
    CHECK_EQ(XhciPirqTarget(10, 0x80800B80UL, 0x0A800B80UL, 0, &mode,
                            &target), XHCI_PIRQ_D_NONE, "PIC 10");
    CHECK_EQ(target, 7, "PIRQH");
    CHECK_EQ(XhciPirqTarget(5, 0x80800B80UL, 0x0A800B80UL, 0, &mode,
                            &target), XHCI_PIRQ_D_PIC_NO_PIRQ,
             "no PIRQ routed to 5");
    CHECK_EQ(target, XHCI_PIRQ_UNREAD, "no target");
    /* IRQEN set: the field names 11, but the PIRQ goes nowhere. */
    CHECK_EQ(XhciPirqTarget(11, 0x8B8B8B8BUL, 0x8B8B8B8BUL, 0, &mode,
                            &target), XHCI_PIRQ_D_PIC_NO_PIRQ,
             "routes disabled");
    /* Bits 6:4 are reserved and not read. */
    CHECK_EQ(XhciPirqTarget(11, 0x80807B80UL, 0x80808080UL, 0, &mode,
                            &target), XHCI_PIRQ_D_NONE, "reserved bits");
    CHECK_EQ(target, 1, "still PIRQB");
    CHECK_EQ(XhciPirqTarget(15, 0x80808080UL, 0x0F808080UL, 0, &mode,
                            &target), XHCI_PIRQ_D_NONE, "PIC 15");
    CHECK_EQ(target, 7, "PIRQH");
    CHECK_EQ(XhciPirqTarget(3, 0x80808003UL, 0x80808080UL, 3, &mode,
                            &target), XHCI_PIRQ_D_NONE, "PIC 3");
    CHECK_EQ(target, 0, "PIRQA");
}

/* The E7250 at each value. */
static void test_e7250(void)
{
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;
    ULONG i;
    ULONG writes;

    zeroState(&st);
    fakeInit(&f, &io, &irq);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "value 1 repairs the E7250");
    CHECK_EQ(f.D20ir, 0x3215U, "INTA to PIRQF, 3215 as written by hand");
    CHECK_EQ(st.D20ir, 0x3210UL, "before");
    CHECK_EQ(st.D20irAfter, 0x3215UL, "read back");
    CHECK_EQ(st.Target, 5, "PIRQF");
    CHECK_EQ(st.IrqMode, XHCI_PIRQ_IRQ_APIC, "APIC");
    CHECK_EQ(st.Writes, 1, "one write");
    CHECK_EQ(st.WriteFailures, 0, "it took");
    CHECK_EQ(st.Runs, 1, "one run");
    CHECK_EQ(st.Rcba, E7250_RCBA, "RCBA recorded");
    CHECK_EQ(st.LpcId, E7250_LPC, "LPC recorded");
    CHECK_EQ(st.SlotId, E7250_XHCI, "00:14.0 recorded");
    CHECK_EQ(st.XhciPin, 1, "INTA");
    CHECK_EQ(st.D20ip, 1, "D20IP");
    CHECK_EQ(st.IrqLevel, 21, "raw level");
    CHECK_EQ(st.IrqShare, 3, "share kept");
    CHECK_EQ(f.Maps, 1, "one page mapped");
    CHECK_EQ(f.Unmaps, 1, "and unmapped");
    CHECK_EQ(f.Mapped, 0, "nothing left mapped");
    CHECK_EQ(f.Log[f.Count - 1].Kind, 'u', "the unmap is last");
    for (i = 0; i < f.Count && i < FAKE_LOG; i++) {
        if (f.Log[i].Kind == 'm') {
            CHECK_EQ(f.Log[i].Where, 0xFED1F000UL, "RCBA+3000h");
        }
    }
    /* Exactly one write anywhere, to D20IR, 16 bits. */
    writes = 0;
    for (i = 0; i < f.Count && i < FAKE_LOG; i++) {
        if (f.Log[i].Kind == 'w') {
            writes++;
            CHECK_EQ(f.Log[i].Where, XHCI_PIRQ_D20IR, "D20IR only");
            CHECK_EQ(f.Log[i].Value, 0x3215UL, "(old & ~7) | 5");
        }
    }
    CHECK_EQ(writes, 1, "one write in the log");
    CHECK_EQ(fakeCount(&f, 's'), 2, "00:14.0 and 00:1F.0 read");
    CHECK_EQ(fakeCount(&f, 'o'), 5, "five own dwords");

    /* A second run finds it routed and writes nothing. */
    f.Count = 0;
    CHECK_EQ(XhciPirqRun(&io, XHCI_PIRQ_WHEN_RESUME, XHCI_PIRQ_DELIVERY_LINE,
                         &irq, &st), XHCI_PIRQ_D_ROUTED, "already routed");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    CHECK_EQ(st.When, XHCI_PIRQ_WHEN_RESUME, "a resume's run");
    CHECK_EQ(st.Runs, 2, "counted");
    CHECK_EQ(st.Writes, 1, "the earlier write still counted");
    CHECK_EQ(st.D20irAfter, XHCI_PIRQ_UNREAD, "nothing read back");

    /* The BIOS puts it back across a sleep; the D0 run repairs it. */
    f.D20ir = E7250_D20IR;
    f.Count = 0;
    CHECK_EQ(XhciPirqRun(&io, XHCI_PIRQ_WHEN_RESUME, XHCI_PIRQ_DELIVERY_LINE,
                         &irq, &st), XHCI_PIRQ_D_WROTE, "repaired at D0");
    CHECK_EQ(f.D20ir, 0x3215U, "again");
    CHECK_EQ(st.Writes, 2, "a second write");

    /* Value 2 repairs it too. */
    fakeInit(&f, &io, &irq);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "value 2");
    CHECK_EQ(f.D20ir, 0x3215U, "written");

    /* Value 0 reads everything, finds the target, writes nothing. */
    fakeInit(&f, &io, &irq);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 0, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_OFF, "value 0");
    CHECK_EQ(f.D20ir, E7250_D20IR, "untouched");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    CHECK_EQ(st.Target, 5, "the target it would have written");
    CHECK_EQ(st.D20ir, 0x3210UL, "and what it found");
    CHECK_EQ(st.Mode, XHCI_PIRQ_MODE_OFF, "mode 0");
    CHECK_EQ(f.Unmaps, 1, "the page unmapped");

    /* Missing, of another type (the executor reports it not found) or out
     * of range: 1. */
    fakeInit(&f, &io, &irq);
    CHECK_EQ(run(&f, &io, &irq, &st, 0, 0, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "missing means 1");
    CHECK_EQ(st.ValueFound, 0, "recorded as not found");
    CHECK_EQ(st.Value, XHCI_PIRQ_UNREAD, "no value");
    fakeInit(&f, &io, &irq);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 7, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "7 means 1");
    CHECK_EQ(st.Value, 7, "the value recorded");
    CHECK_EQ(st.Mode, XHCI_PIRQ_MODE_VERIFIED, "mode 1");
}

/* A reviewed pair that is not the E7250. */
static void test_admission(void)
{
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;

    zeroState(&st);
    fakeInit(&f, &io, &irq);
    f.OwnSubsystem = 0x220017AAUL;
    fakeSlots(&f);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_NOT_ADMITTED, "value 1, another board");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    CHECK_EQ(st.Target, 5, "the target found");
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "value 2 admits it");

    /* A Lynx Point desktop at value 2: IRQ 19, PIRQD. */
    fakeInit(&f, &io, &irq);
    f.OwnId = 0x8C318086UL;
    f.OwnSubsystem = 0x05D71028UL;
    fakeSlots(&f);
    fakeLpc(&f, 0x8C4E8086UL, 0xFED1C001UL, 0x80808080UL, 0x80808080UL);
    f.D20ir = 0x3210U;
    irq.Level = 19;
    irq.Vector = 19;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_NOT_ADMITTED, "value 1: not the E7250");
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "value 2");
    CHECK_EQ(f.D20ir, 0x3213U, "PIRQD");
}

/* Each check that stops the run, and what it has not read by then. */
static void test_refusals(void)
{
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;

    zeroState(&st);
    CHECK_EQ(XhciPirqRun(NULL, XHCI_PIRQ_WHEN_START, XHCI_PIRQ_DELIVERY_LINE,
                         NULL, NULL), XHCI_PIRQ_D_BAD_PARAM, "no state");
    fakeInit(&f, &io, &irq);
    io.Write16 = NULL;
    CHECK_EQ(XhciPirqRun(&io, XHCI_PIRQ_WHEN_START, XHCI_PIRQ_DELIVERY_LINE,
                         &irq, &st), XHCI_PIRQ_D_BAD_PARAM, "a callback");
    CHECK_EQ(f.Count, 0, "no access");

    /* QEMU's xHCI: nothing past its own space. */
    fakeInit(&f, &io, &irq);
    f.OwnId = 0x000D1B36UL;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_NOT_REVIEWED, "not a reviewed xHCI");
    CHECK_EQ(fakeCount(&f, 's'), 0, "no bus-data read");
    CHECK_EQ(fakeCount(&f, 'm'), 0, "no map");
    CHECK_EQ(st.XhciId, 0x000D1B36UL, "its id recorded");
    CHECK_EQ(st.SlotId, XHCI_PIRQ_UNREAD, "no slot read");
    /* Sunrise Point, which has no RCBA, the same. */
    f.OwnId = 0x9D2F8086UL;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_NOT_REVIEWED, "100 series and later");
    CHECK_EQ(fakeCount(&f, 's'), 0, "no bus-data read");

    fakeInit(&f, &io, &irq);
    f.FailKind = 'o';
    f.FailWhere = XHCI_PIRQ_CFG_INTERRUPT;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_OWN_UNREAD, "own config unread");
    CHECK_EQ(fakeCount(&f, 's'), 0, "nothing further");
    f.FailWhere = XHCI_PIRQ_CFG_ID;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_OWN_UNREAD, "own id unread");
    CHECK_EQ(st.XhciId, XHCI_PIRQ_UNREAD, "not recorded");

    fakeInit(&f, &io, &irq);
    f.Slot14Bytes = 2;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_SLOT_UNREAD, "00:14.0 empty (HalGetBusData's 2)");
    CHECK_EQ(fakeCount(&f, 's'), 1, "the LPC not read");
    f.Slot14Bytes = 0;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_SLOT_UNREAD, "no such bus (0)");

    /* Another function at 00:14.0: an add-in card's xHCI of the same id,
     * told apart by its BAR. */
    fakeInit(&f, &io, &irq);
    f.Slot14[XHCI_PIRQ_CFG_BAR0 + 3] = 0xE0;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_NOT_AT_SLOT, "BAR0 differs");
    CHECK_EQ(st.SlotId, E7250_XHCI, "its id recorded");
    CHECK_EQ(fakeCount(&f, 's'), 1, "the LPC not read");
    fakeInit(&f, &io, &irq);
    f.Slot14[XHCI_PIRQ_CFG_SUBSYSTEM] = 0xAA;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_NOT_AT_SLOT, "subsystem differs");
    fakeInit(&f, &io, &irq);
    f.Slot14[XHCI_PIRQ_CFG_BAR1] = 0x01;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_NOT_AT_SLOT, "BAR1 differs");

    fakeInit(&f, &io, &irq);
    f.Slot1FBytes = 0x40;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_LPC_UNREAD, "F0h not reached");
    CHECK_EQ(st.Rcba, XHCI_PIRQ_UNREAD, "RCBA not recorded");
    CHECK_EQ(fakeCount(&f, 'm'), 0, "no map");

    fakeInit(&f, &io, &irq);
    fakeLpc(&f, 0x9CC18086UL, E7250_RCBA, 0x80808080UL, 0x80808080UL);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_LPC_UNLISTED, "not this xHCI's LPC");
    CHECK_EQ(st.LpcId, 0x9CC18086UL, "recorded");
    CHECK_EQ(fakeCount(&f, 'm'), 0, "no map");
    fakeLpc(&f, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_LPC_UNLISTED, "nothing at 00:1F.0");

    fakeInit(&f, &io, &irq);
    fakeLpc(&f, E7250_LPC, 0xFED1C000UL, 0x80808080UL, 0x80808080UL);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_RCBA_INVALID, "RCBA not enabled");
    fakeLpc(&f, E7250_LPC, 0x00000001UL, 0x80808080UL, 0x80808080UL);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_RCBA_INVALID, "enabled at zero");
    fakeLpc(&f, E7250_LPC, 0xFFFFFFFFUL, 0x80808080UL, 0x80808080UL);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_RCBA_INVALID, "all-ones");
    CHECK_EQ(fakeCount(&f, 'm'), 0, "no map");

    fakeInit(&f, &io, &irq);
    f.FailKind = 'm';
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_MAP_FAILED, "the map refused");
    CHECK_EQ(f.Unmaps, 0, "nothing to unmap");

    /* The pins. */
    fakeInit(&f, &io, &irq);
    f.D20ip = 2;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_PIN, "D20IP INTB");
    CHECK_EQ(st.D20ip, 2, "recorded");
    CHECK_EQ(fakeCount(&f, 'h'), 0, "D20IR not read");
    CHECK_EQ(f.Unmaps, 1, "unmapped");
    f.D20ip = 0;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_PIN, "D20IP no interrupt");
    f.D20ip = 0x00000011UL;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "other devices' fields in D20IP's dword");
    fakeInit(&f, &io, &irq);
    f.OwnInterrupt = 0x00000215UL;
    fakeSlots(&f);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_PIN, "the xHCI's pin INTB");
    CHECK_EQ(st.XhciPin, 2, "recorded");
    fakeInit(&f, &io, &irq);
    f.FailKind = 'r';
    f.FailWhere = XHCI_PIRQ_D20IP;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_PIN, "D20IP unread");
    CHECK_EQ(st.D20ip, XHCI_PIRQ_UNREAD, "recorded unread");

    fakeInit(&f, &io, &irq);
    f.D20ir = 0xFFFFU;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_D20IR_UNREAD, "D20IR all-ones");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    fakeInit(&f, &io, &irq);
    f.FailKind = 'h';
    f.FailWhere = XHCI_PIRQ_D20IR;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_D20IR_UNREAD, "D20IR unread");
    CHECK_EQ(st.D20ir, XHCI_PIRQ_UNREAD, "recorded unread");
}

/* The delivery: only a line is repaired; the rest report what they find. */
static void test_delivery(void)
{
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;

    zeroState(&st);
    fakeInit(&f, &io, &irq);
    irq.Flags = 2;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_MESSAGE),
             XHCI_PIRQ_D_MESSAGE, "MSI");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    CHECK_EQ(st.D20ir, 0x3210UL, "but D20IR reported");
    CHECK_EQ(st.IrqFlags, 2, "the message flag recorded");
    CHECK_EQ(st.Delivery, XHCI_PIRQ_DELIVERY_MESSAGE, "the delivery");
    CHECK_EQ(f.Unmaps, 1, "unmapped");

    fakeInit(&f, &io, &irq);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_POLL),
             XHCI_PIRQ_D_POLL, "polled");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    CHECK_EQ(f.D20ir, E7250_D20IR, "left as the BIOS set it");

    fakeInit(&f, &io, &irq);
    irq.Count = 0;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_NONE),
             XHCI_PIRQ_D_NO_INTERRUPT, "no interrupt");
    CHECK_EQ(st.IrqLevel, XHCI_PIRQ_UNREAD, "no raw resource recorded");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, 7), XHCI_PIRQ_D_NO_INTERRUPT,
             "an unknown delivery");

    /* A line later, on the same machine: repaired then. */
    fakeInit(&f, &io, &irq);
    (VOID)run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_POLL);
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "a later start on the line");

    /* The raw resource. */
    fakeInit(&f, &io, &irq);
    irq.Count = 2;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_IRQ_AMBIGUOUS, "two interrupt resources");
    CHECK_EQ(st.IrqCount, 2, "recorded");
    fakeInit(&f, &io, &irq);
    irq.Vector = 0xB2;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_IRQ_AMBIGUOUS, "level and vector differ");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    fakeInit(&f, &io, &irq);
    irq.Level = 24;
    irq.Vector = 24;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_IRQ_UNROUTABLE, "GSI 24");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
}

/* PIC mode: Windows 98, ME, or NT without the APIC. */
static void test_pic(void)
{
    static const ULONG reserved[] = { 0, 1, 2, 8, 13 };
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;
    ULONG i;

    zeroState(&st);
    /* PIRQF routed to 11 by the OS, D20IR still at PIRQA. */
    fakeInit(&f, &io, &irq);
    fakeLpc(&f, E7250_LPC, E7250_RCBA, 0x8080800AUL, 0x80800B80UL);
    irq.Level = 11;
    irq.Vector = 11;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "PIC 11");
    CHECK_EQ(st.IrqMode, XHCI_PIRQ_IRQ_PIC, "PIC mode");
    CHECK_EQ(st.Target, 5, "PIRQF");
    CHECK_EQ(f.D20ir, 0x3215U, "written");
    CHECK_EQ(st.PirqAd, 0x8080800AUL, "routes recorded");
    CHECK_EQ(st.PirqEh, 0x80800B80UL, "routes recorded");
    /* PIRQx_ROUT is read through the LPC's header only, never written. */
    CHECK_EQ(fakeCount(&f, 'w'), 1, "the one write");
    CHECK_EQ(f.Log[0].Kind, 'o', "own space first");

    /* PIRQA routed to 10, and the OS assigned 10: already right. */
    fakeInit(&f, &io, &irq);
    fakeLpc(&f, E7250_LPC, E7250_RCBA, 0x8080800AUL, 0x80800B80UL);
    irq.Level = 10;
    irq.Vector = 10;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_ROUTED, "PIRQA delivers 10");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");

    /* The OS's IRQ on no routed PIRQ: report. */
    fakeInit(&f, &io, &irq);
    irq.Level = 11;
    irq.Vector = 11;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_PIC_NO_PIRQ, "every route disabled");
    CHECK_EQ(st.IrqMode, XHCI_PIRQ_IRQ_PIC, "PIC mode");
    CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");

    /* Each reserved encoding, even with a register holding it. */
    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        fakeInit(&f, &io, &irq);
        fakeLpc(&f, E7250_LPC, E7250_RCBA, 0x80808000UL | reserved[i],
                0x80808080UL);
        irq.Level = reserved[i];
        irq.Vector = reserved[i];
        CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
                 XHCI_PIRQ_D_IRQ_UNROUTABLE, "a reserved IRQ");
        CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    }

    /* On 2000 to 7 an IRQ below 16 is reported at every value, the
     * target still derived; 16-23 is written there as on 98 (owner,
     * 2026-10-09). */
    for (i = 0; i <= 2; i++) {
        fakeInit(&f, &io, &irq);
        io.Steerable = 0;
        fakeLpc(&f, E7250_LPC, E7250_RCBA, 0x8080800AUL, 0x80800B80UL);
        irq.Level = 11;
        irq.Vector = 11;
        CHECK_EQ(run(&f, &io, &irq, &st, 1, i, XHCI_PIRQ_DELIVERY_LINE),
                 XHCI_PIRQ_D_STEERED_NT, "below 16 on NT");
        CHECK_EQ(st.IrqMode, XHCI_PIRQ_IRQ_PIC, "the steered path");
        CHECK_EQ(st.Target, 5, "its target still named");
        CHECK_EQ(st.D20irAfter, XHCI_PIRQ_UNREAD, "nothing after");
        CHECK_EQ(fakeCount(&f, 'w'), 0, "no write");
    }
    fakeInit(&f, &io, &irq);
    io.Steerable = 0;
    fakeLpc(&f, E7250_LPC, E7250_RCBA, 0x8080800AUL, 0x80800B80UL);
    irq.Level = 10;
    irq.Vector = 10;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 2, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_ROUTED, "already right on NT: ROUTED first");
    fakeInit(&f, &io, &irq);
    io.Steerable = 0;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "21 on NT: the direct path is written");
    CHECK_EQ(f.D20ir, 0x3215U, "written");
}

/* The write and its read-back. */
static void test_write(void)
{
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;

    zeroState(&st);
    /* INTB-D's routes and the reserved bit 3 written back as read. */
    fakeInit(&f, &io, &irq);
    f.D20ir = 0x765AU;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WROTE, "other fields set");
    CHECK_EQ(f.D20ir, 0x765DU, "only 2:0 changed");

    /* A register that does not take the write. */
    fakeInit(&f, &io, &irq);
    f.IarStuck = 0x0007U;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WRITE_FAILED, "read back unchanged");
    CHECK_EQ(st.D20irAfter, 0x3210UL, "recorded");
    CHECK_EQ(st.Writes, 2, "the write counted, after the first test's");
    CHECK_EQ(st.WriteFailures, 1, "and its failure");
    CHECK_EQ(f.Unmaps, 1, "unmapped");

    fakeInit(&f, &io, &irq);
    f.ReadBackAllOnes = 1;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WRITE_FAILED, "read back all-ones");
    CHECK_EQ(st.D20irAfter, 0xFFFFUL, "recorded");
    CHECK_EQ(st.WriteFailures, 2, "counted");

    fakeInit(&f, &io, &irq);
    f.FailKind = 'w';
    f.FailWhere = XHCI_PIRQ_D20IR;
    CHECK_EQ(run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE),
             XHCI_PIRQ_D_WRITE_FAILED, "the write refused");
    CHECK_EQ(st.Writes, 4, "counted");
    CHECK_EQ(st.WriteFailures, 3, "counted");
}

/* The latch keeps the counts and clears the last run. */
static void test_latch(void)
{
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;

    zeroState(&st);
    fakeInit(&f, &io, &irq);
    (VOID)run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE);
    XhciPirqLatch(&st, 1, 0);
    CHECK_EQ(st.Runs, 1, "runs kept");
    CHECK_EQ(st.Writes, 1, "writes kept");
    CHECK_EQ(st.Decision, XHCI_PIRQ_D_NONE, "the last run cleared");
    CHECK_EQ(st.When, XHCI_PIRQ_WHEN_NONE, "no run yet");
    CHECK_EQ(st.D20ir, XHCI_PIRQ_UNREAD, "nothing read");
    CHECK_EQ(st.Mode, XHCI_PIRQ_MODE_OFF, "the new value");
    XhciPirqLatch(NULL, 1, 1);
}

/* A start attempt resets the record before anything can refuse it: a
 * refused start shows no earlier start's run or value. */
static void test_reset(void)
{
    FAKE_PC f;
    XHCI_PIRQ_IO io;
    XHCI_PIRQ_IRQ irq;
    XHCI_PIRQ_STATE st;

    zeroState(&st);
    fakeInit(&f, &io, &irq);
    (VOID)run(&f, &io, &irq, &st, 1, 1, XHCI_PIRQ_DELIVERY_LINE);
    CHECK_EQ(st.Decision, XHCI_PIRQ_D_WROTE, "a run that wrote");
    XhciPirqReset(&st);
    CHECK_EQ(st.Runs, 1, "runs kept");
    CHECK_EQ(st.Writes, 1, "writes kept");
    CHECK_EQ(st.Decision, XHCI_PIRQ_D_NONE, "its decision gone");
    CHECK_EQ(st.When, XHCI_PIRQ_WHEN_NONE, "no run this start");
    CHECK_EQ(st.D20ir, XHCI_PIRQ_UNREAD, "its readings gone");
    CHECK_EQ(st.Target, XHCI_PIRQ_UNREAD, "its target gone");
    CHECK_EQ(st.ValueFound, 0, "no value found this start");
    CHECK_EQ(st.Value, XHCI_PIRQ_UNREAD, "no value read");
    CHECK_EQ(st.Mode, XHCI_PIRQ_UNREAD, "no mode until the latch");
    XhciPirqLatch(&st, 0, 0);
    CHECK_EQ(st.Mode, XHCI_PIRQ_MODE_VERIFIED, "the latch then decides");
    XhciPirqReset(NULL);
}

int main(void)
{
    test_mode();
    test_ids();
    test_pic_irq();
    test_target();
    test_e7250();
    test_admission();
    test_refusals();
    test_delivery();
    test_pic();
    test_write();
    test_latch();
    test_reset();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
