/*
 * xhci_psw.c - the Intel EHCI-to-xHCI port switchover, the pure half
 * (xhci_psw.h; roadmap-hcd.md tasks 34.3 and 35.5).
 *
 * DDK-free: part of the pure core. C89. IRQL: any for the functions that
 * take no XHCI_PSW_IO; those that do run at their callbacks' IRQL, which
 * with hcd_ctl.c's is PASSIVE_LEVEL under no spin lock.
 */

#include "xhci_psw.h"

#define XHCI_PSW_VENDOR_INTEL   0x8086UL
#define XHCI_PSW_VENDOR_SONY    0x104DUL
#define XHCI_PSW_SONY_VAIO_T    0x90A8UL
#define XHCI_PSW_UNREAD         0xFFFFFFFFUL
#define XHCI_PSW_ABSENT_ROUTES  0UL

/* Panther Point, Lynx Point, Lynx Point-LP, Wildcat Point, Wildcat
 * Point-LP: the 7-, 8- and 9-series PCH xHCI (Linux xhci-pci.c's
 * PCI_DEVICE_ID_INTEL_* for the -LP parts; xhciqual's quirks.c rows); and
 * Wellsburg, the C610/X99 PCH, which ships an EHCI beside its xHCI
 * (xhciqual's QF_XUSB2PR row; owner, 2026-10-05). For Wildcat Point and
 * Wellsburg the support is Linux's one sequence for every Intel xHCI with
 * an EHCI and xhciqual's reading of them as such parts, not a datasheet or
 * reading of their own (design record 16 section 4). Linux's rule would
 * also reach other Intel parts with an EHCI; none is listed here. */
static const USHORT xhciPswDevices[] = {
    0x1E31, 0x8C31, 0x9C31, 0x8CB1, 0x9CB1, 0x8D31
};

ULONG XhciPswGate(ULONG vendorDevice)
{
    ULONG device;
    ULONG i;

    if ((vendorDevice & 0xFFFFUL) != XHCI_PSW_VENDOR_INTEL) {
        return 0;
    }
    device = (vendorDevice >> 16) & 0xFFFFUL;
    for (i = 0; i < sizeof(xhciPswDevices) / sizeof(xhciPswDevices[0]);
         i++) {
        if (device == (ULONG)xhciPswDevices[i]) {
            return 1;
        }
    }
    return 0;
}

ULONG XhciPswBoardRefused(ULONG subsystem)
{
    return (subsystem & 0xFFFFUL) == XHCI_PSW_VENDOR_SONY &&
           ((subsystem >> 16) & 0xFFFFUL) == XHCI_PSW_SONY_VAIO_T;
}

ULONG XhciPswIntel(ULONG vendorDevice)
{
    return (vendorDevice & 0xFFFFUL) == XHCI_PSW_VENDOR_INTEL;
}

/* Only an exact 2 reaches unlisted silicon, so it has to be typed on
 * purpose; any other nonzero number keeps the gated meaning (record 16
 * section 5). On a listed controller a value not found (absent, of another
 * type or unreadable) is XHCI_PSW_ABSENT_ROUTES: off, so text-mode Setup,
 * which writes no value, keeps the ports where the firmware put them
 * (owner, 2026-11-14, task 49.47; off since task 36.42, on before it). */
ULONG XhciPswMode(ULONG vendorDevice, ULONG found, ULONG value)
{
    if (!XhciPswIntel(vendorDevice)) {
        return XHCI_PSW_MODE_OFF;
    }
    if (XhciPswGate(vendorDevice)) {
        return (found ? value != 0 : XHCI_PSW_ABSENT_ROUTES != 0)
                   ? XHCI_PSW_MODE_GATED
                   : XHCI_PSW_MODE_OFF;
    }
    return (found && value == 2) ? XHCI_PSW_MODE_BYPASS : XHCI_PSW_MODE_OFF;
}

static VOID xhciPswReset(PXHCI_PSW_STATE st)
{
    st->Usb3Mask = XHCI_PSW_UNREAD;
    st->Usb3Now = XHCI_PSW_UNREAD;
    st->Usb2Mask = XHCI_PSW_UNREAD;
    st->Usb2Now = XHCI_PSW_UNREAD;
    st->Step = XHCI_PSW_DONE;
    st->Written = 0;
}

/* A mask is refused when it could not be read or reads all-ones, which is
 * what an access nothing decodes returns: no PCH has 32 switchable ports,
 * and writing it would claim every bit of the register. */
static ULONG xhciPswMask(const XHCI_PSW_IO *io, ULONG offset, PULONG mask)
{
    ULONG value;

    value = XHCI_PSW_UNREAD;
    if (!io->Read(io->Context, offset, &value)) {
        *mask = XHCI_PSW_UNREAD;
        return 0;
    }
    *mask = value;
    return value != XHCI_PSW_UNREAD;
}

/* Record the first failure only. */
static VOID xhciPswFail(PXHCI_PSW_STATE st, ULONG step)
{
    if (st->Step == XHCI_PSW_DONE) {
        st->Step = step;
    }
}

ULONG XhciPswRoute(const XHCI_PSW_IO *io, PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return XHCI_PSW_STEP_BAD_PARAM;
    }
    xhciPswReset(st);
    if (io == NULL || io->Read == NULL || io->Write == NULL) {
        st->Step = XHCI_PSW_STEP_BAD_PARAM;
        return st->Step;
    }

    if (!xhciPswMask(io, XHCI_PSW_USB3PRM, &st->Usb3Mask)) {
        st->Step = XHCI_PSW_STEP_USB3PRM;
        return st->Step;
    }
    if (!io->Write(io->Context, XHCI_PSW_USB3_PSSEN, st->Usb3Mask)) {
        st->Step = XHCI_PSW_STEP_PSSEN_WRITE;
        return st->Step;
    }
    st->Written |= XHCI_PSW_WROTE_PSSEN;
    /* The read-back is the log's, not a gate: the write was accepted, so
     * the USB 2.0 pairs follow the terminations. */
    if (!io->Read(io->Context, XHCI_PSW_USB3_PSSEN, &st->Usb3Now)) {
        st->Usb3Now = XHCI_PSW_UNREAD;
        st->Step = XHCI_PSW_STEP_PSSEN_READ;
    }

    /* From here a PSSEN read-back failure may already be recorded, and it
     * stays the reported step: the first failure, as the header says. */
    if (!xhciPswMask(io, XHCI_PSW_XUSB2PRM, &st->Usb2Mask)) {
        xhciPswFail(st, XHCI_PSW_STEP_XUSB2PRM);
        return st->Step;
    }
    if (!io->Write(io->Context, XHCI_PSW_XUSB2PR, st->Usb2Mask)) {
        xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_WRITE);
        return st->Step;
    }
    st->Written |= XHCI_PSW_WROTE_XUSB2PR;
    if (!io->Read(io->Context, XHCI_PSW_XUSB2PR, &st->Usb2Now)) {
        st->Usb2Now = XHCI_PSW_UNREAD;
        xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_READ);
    }
    return st->Step;
}

ULONG XhciPswRelease(const XHCI_PSW_IO *io, ULONG written,
                     PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return XHCI_PSW_STEP_BAD_PARAM;
    }
    xhciPswReset(st);
    if (io == NULL || io->Read == NULL || io->Write == NULL) {
        st->Step = XHCI_PSW_STEP_BAD_PARAM;
        return st->Step;
    }

    if (written & XHCI_PSW_WROTE_PSSEN) {
        if (!io->Write(io->Context, XHCI_PSW_USB3_PSSEN, 0)) {
            xhciPswFail(st, XHCI_PSW_STEP_PSSEN_WRITE);
        } else if (!io->Read(io->Context, XHCI_PSW_USB3_PSSEN,
                             &st->Usb3Now)) {
            st->Usb3Now = XHCI_PSW_UNREAD;
            xhciPswFail(st, XHCI_PSW_STEP_PSSEN_READ);
        }
    }
    if (written & XHCI_PSW_WROTE_XUSB2PR) {
        if (!io->Write(io->Context, XHCI_PSW_XUSB2PR, 0)) {
            xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_WRITE);
        } else if (!io->Read(io->Context, XHCI_PSW_XUSB2PR,
                             &st->Usb2Now)) {
            st->Usb2Now = XHCI_PSW_UNREAD;
            xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_READ);
        }
    }
    return st->Step;
}

/* ------------------------------------------------------------------------ */
/* The lifetime (record 16 section 7a)                                       */
/* ------------------------------------------------------------------------ */

static VOID xhciPswObsClear(PXHCI_PSW_OBS obs)
{
    obs->State = XHCI_PSW_RD_NOT_READ;
    obs->Calls = 0;
    obs->Type = 0;
    obs->Length = 0;
    obs->Status = XHCI_PSW_ST_NOT_RUN;
    obs->Value = XHCI_PSW_UNREAD;
}

static VOID xhciPswGuardClear(PXHCI_PSW_GUARD g)
{
    g->G = 0;
    xhciPswObsClear(&g->Ready);
    g->Routine = 0;
    g->OpenStatus = XHCI_PSW_ST_NOT_RUN;
    g->Disposition = 0;
    g->ProbeStatus = XHCI_PSW_ST_NOT_RUN;
    g->WriteS = XHCI_PSW_ST_NOT_RUN;
    xhciPswObsClear(&g->Session);
    g->S = XHCI_PSW_S_UNKNOWN;
    g->WriteG = XHCI_PSW_ST_NOT_RUN;
    g->Outcome = XHCI_PSW_GUARD_NOT_RUN;
}

static VOID xhciPswLifeClear(PXHCI_PSW_LIFE life)
{
    ULONG i;

    life->On = 0;
    life->Mode = XHCI_PSW_MODE_OFF;
    life->Written = 0;
    life->Decision = XHCI_PSW_DECIDE_NONE;
    life->Id = XHCI_PSW_UNREAD;
    life->Found = 0;
    life->Value = 0;
    life->Subsystem = XHCI_PSW_UNREAD;
    life->System = XHCI_PSW_SYS_NT6;
    life->SwitchState = XHCI_REG_ABSENT;
    life->SwitchSource = XHCI_REG_FROM_NONE;
    life->Step = 0;
    life->SettingState = XHCI_REG_ABSENT;
    life->SettingValue = 0;
    life->SettingSource = XHCI_REG_FROM_NONE;
    life->Setting = XHCI_PSW_SETTING_AUTO;
    for (i = 0; i < XHCI_PSW_SETUP_VALUES; i++) {
        xhciPswObsClear(&life->Setup[i]);
    }
    xhciPswObsClear(&life->PeKey);
    xhciPswObsClear(&life->PeOptions);
    xhciPswObsClear(&life->Hive);
    xhciPswObsClear(&life->Image);
    for (i = 0; i < XHCI_PSW_IMAGE_TEXT; i++) {
        life->ImageText[i] = 0;
    }
    xhciPswGuardClear(&life->Guard);
    life->Verdict = XHCI_PSW_V_NONE;
    life->VerdictValue = 0;
    life->RouteStep = XHCI_PSW_DONE;
    life->RouteWritten = 0;
}

static VOID xhciPswStep(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life);

ULONG XhciPswLifeStart(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                       PXHCI_PSW_STATE st)
{
    ULONG value;
    ULONG source;

    if (st != NULL) {
        xhciPswReset(st);
    }
    if (life == NULL) {
        return 0;
    }
    xhciPswLifeClear(life);
    if (st == NULL || io == NULL || io->Read == NULL || io->Write == NULL ||
        io->Value == NULL) {
        return 0;
    }
    life->System = io->System;

    if (!io->Read(io->Context, XHCI_PSW_PCI_ID, &life->Id)) {
        life->Id = XHCI_PSW_UNREAD;
        life->Decision = XHCI_PSW_DECIDE_ID_UNREAD;
        return 0;
    }
    if (!XhciPswIntel(life->Id)) {
        life->Decision = XHCI_PSW_DECIDE_NOT_INTEL;
        return 0;
    }
    value = 0;
    source = XHCI_REG_FROM_NONE;
    life->SwitchState = io->Value(io->Context, XHCI_PSW_WHICH_SWITCH, &value,
                                  &source);
    life->SwitchSource = source;
    life->Found = (life->SwitchState == XHCI_REG_TAKEN) ? 1 : 0;
    life->Value = life->Found ? value : 0;
    life->Mode = XhciPswMode(life->Id, life->Found, life->Value);
    if (life->Mode == XHCI_PSW_MODE_OFF) {
        life->Decision = XHCI_PSW_DECIDE_OFF;
        return 0;
    }

    /* The exemption holds only on a reading: a subsystem id that could not
     * be read is no evidence the board is not the one that cannot switch
     * (record 16 section 4); value 2 keeps it (section 4a). */
    if (!io->Read(io->Context, XHCI_PSW_PCI_SUBSYSTEM, &life->Subsystem)) {
        life->Subsystem = XHCI_PSW_UNREAD;
        life->Decision = XHCI_PSW_DECIDE_BOARD_UNREAD;
        return 0;
    }
    if (XhciPswBoardRefused(life->Subsystem)) {
        life->Decision = XHCI_PSW_DECIDE_BOARD_REFUSED;
        return 0;
    }

    /* Section 21's step: a held start ends here with On clear, so nothing
     * of this lifetime ever writes a routing register. */
    xhciPswStep(io, life);
    if (life->Verdict != XHCI_PSW_V_ROUTE &&
        life->Verdict != XHCI_PSW_V_BYPASS &&
        life->Verdict != XHCI_PSW_V_ROUTE_9X) {
        life->Decision = XHCI_PSW_DECIDE_SETUP_HELD;
        return 0;
    }

    life->On = 1;
    life->Decision = XHCI_PSW_DECIDE_ON;
    (VOID)XhciPswRoute(io, st);
    life->Written |= st->Written;
    life->RouteStep = st->Step;
    life->RouteWritten = st->Written;
    return 1;
}

/* A later route that fails does not clear what an earlier one wrote: the
 * register may still hold the earlier value (record 16 section 6). */
ULONG XhciPswLifeResume(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                        PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return 0;
    }
    xhciPswReset(st);
    if (life == NULL || !life->On) {
        return 0;
    }
    (VOID)XhciPswRoute(io, st);
    life->Written |= st->Written;
    return 1;
}

/* Written is kept: a refused zero at a shutdown's D3 is retried by the
 * stop that may follow it. */
ULONG XhciPswLifeRelease(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                         PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return 0;
    }
    xhciPswReset(st);
    if (life == NULL || life->Written == 0) {
        return 0;
    }
    (VOID)XhciPswRelease(io, life->Written, st);
    return 1;
}

ULONG XhciPswLifeEnd(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                     PXHCI_PSW_STATE st)
{
    ULONG released;

    released = XhciPswLifeRelease(io, life, st);
    if (life != NULL) {
        life->On = 0;
        life->Written = 0;
    }
    return released;
}

/* ------------------------------------------------------------------------ */
/* Section 21: the readings, the verdict, the guard, the record              */
/* ------------------------------------------------------------------------ */

static ULONG xhciPswNotFound(ULONG status, ULONG pathCounts)
{
    return status == XHCI_PSW_ST_NAME_NOT_FOUND ||
           (pathCounts && status == XHCI_PSW_ST_PATH_NOT_FOUND);
}

/* A value taken requires both observations: the routine took one
 * four-byte REG_DWORD and the query succeeded. An empty REG_NONE is a
 * value that is there, malformed: with a zeroed default and REQUIRED a
 * missing value calls no routine (record 16 section 21.3). */
ULONG XhciPswClassifyDword(const XHCI_PSW_RAW *raw, PULONG value)
{
    if (raw == NULL) {
        return XHCI_PSW_RD_UNREAD;
    }
    if (raw->Calls == 0) {
        return xhciPswNotFound(raw->Status, 1) ? XHCI_PSW_RD_ABSENT
                                               : XHCI_PSW_RD_UNREAD;
    }
    if (raw->Calls != 1 || raw->Type != XHCI_PSW_REG_DWORD ||
        raw->Length != 4) {
        return XHCI_PSW_RD_MALFORMED;
    }
    if (raw->Status != XHCI_PSW_ST_SUCCESS) {
        return XHCI_PSW_RD_UNREAD;
    }
    if (value != NULL) {
        *value = raw->Dword;
    }
    return XHCI_PSW_RD_TAKEN;
}

ULONG XhciPswClassifyString(const XHCI_PSW_RAW *raw, const USHORT *text,
                            ULONG capChars)
{
    ULONG n;
    ULONG i;

    if (raw == NULL) {
        return XHCI_PSW_SZ_MALFORMED;
    }
    if (raw->Calls == 0) {
        return xhciPswNotFound(raw->Status, 1) ? XHCI_PSW_SZ_ABSENT
                                               : XHCI_PSW_SZ_MALFORMED;
    }
    /* NOEXPAND keeps a REG_EXPAND_SZ its own type, so it fails here and is
     * never expanded. */
    if (raw->Status != XHCI_PSW_ST_SUCCESS || raw->Calls != 1 ||
        raw->Type != XHCI_PSW_REG_SZ || (raw->Length & 1UL) != 0 ||
        raw->Length < 2 || raw->Length / 2 > capChars || text == NULL) {
        return XHCI_PSW_SZ_MALFORMED;
    }
    n = raw->Length / 2;
    if (text[n - 1] != 0) {
        return XHCI_PSW_SZ_MALFORMED;
    }
    for (i = 0; i + 1 < n; i++) {
        if (text[i] == 0) {
            return XHCI_PSW_SZ_MALFORMED;
        }
    }
    return XHCI_PSW_SZ_VALID;
}

ULONG XhciPswClassifyExists(const XHCI_PSW_RAW *raw, ULONG pathCounts)
{
    if (raw == NULL) {
        return XHCI_PSW_EX_UNREAD;
    }
    if (raw->Calls == 1 && raw->Status == XHCI_PSW_ST_SUCCESS) {
        return XHCI_PSW_EX_THERE;
    }
    if (raw->Calls == 0 && xhciPswNotFound(raw->Status, pathCounts)) {
        return XHCI_PSW_EX_NOT;
    }
    return XHCI_PSW_EX_UNREAD;
}

static USHORT xhciPswUpper(USHORT c)
{
    return (c >= 'a' && c <= 'z') ? (USHORT)(c - ('a' - 'A')) : c;
}

/* 1 when text[0..chars) is word, case ignored. */
static ULONG xhciPswSame(const USHORT *text, ULONG chars, const char *word)
{
    ULONG i;

    for (i = 0; i < chars; i++) {
        if (word[i] == 0 || xhciPswUpper(text[i]) != (USHORT)word[i]) {
            return 0;
        }
    }
    return word[chars] == 0;
}

ULONG XhciPswOptionsMinint(const USHORT *text, ULONG chars)
{
    ULONG i;
    ULONG start;

    if (text == NULL) {
        return 0;
    }
    i = 0;
    while (i < chars) {
        while (i < chars && (text[i] == ' ' || text[i] == '\t')) {
            i++;
        }
        start = i;
        while (i < chars && text[i] != ' ' && text[i] != '\t' &&
               text[i] != 0) {
            i++;
        }
        if (i > start && text[start] == '/') {
            start++;
        }
        if (i > start && xhciPswSame(text + start, i - start, "MININT")) {
            return 1;
        }
        if (i < chars && text[i] == 0) {
            break;
        }
    }
    return 0;
}

ULONG XhciPswImageComplete(const USHORT *text, ULONG chars)
{
    if (text == NULL) {
        return 0;
    }
    return xhciPswSame(text, chars, "IMAGE_STATE_COMPLETE");
}

ULONG XhciPswSetupHold(const ULONG *state, const ULONG *value, PULONG which)
{
    static const ULONG progress[3] = {
        XHCI_PSW_Q_SSIP, XHCI_PSW_Q_OOBE, XHCI_PSW_Q_MINI
    };
    ULONG i;
    ULONG q;

    for (i = 0; i < 3; i++) {
        q = progress[i];
        if (state[q] == XHCI_PSW_RD_TAKEN && value[q] != 0) {
            *which = q;
            return XHCI_PSW_V_HELD_SETUP;
        }
    }
    if (state[XHCI_PSW_Q_TYPE] == XHCI_PSW_RD_TAKEN &&
        value[XHCI_PSW_Q_TYPE] != 0) {
        *which = XHCI_PSW_Q_TYPE;
        return XHCI_PSW_V_HELD_SETUPTYPE;
    }
    /* The secondaries' absence is normal; the primary's is not. */
    for (q = 0; q < XHCI_PSW_SETUP_VALUES; q++) {
        if (state[q] != XHCI_PSW_RD_TAKEN &&
            (state[q] != XHCI_PSW_RD_ABSENT || q == XHCI_PSW_Q_SSIP)) {
            *which = q;
            return XHCI_PSW_V_HELD_SETUP_UNREAD;
        }
    }
    *which = 0;
    return XHCI_PSW_V_NONE;
}

ULONG XhciPswSettingInForce(ULONG state, ULONG value, ULONG system)
{
    if (state != XHCI_REG_TAKEN) {
        return XHCI_PSW_SETTING_AUTO;
    }
    if (value == 2) {
        return XHCI_PSW_SETTING_HOLD;
    }
    if (value == 1 && system != XHCI_PSW_SYS_9X) {
        return XHCI_PSW_SETTING_BYPASS;
    }
    return XHCI_PSW_SETTING_AUTO;
}

static VOID xhciPswObsSet(PXHCI_PSW_OBS obs, const XHCI_PSW_RAW *raw)
{
    obs->Calls = raw->Calls;
    obs->Type = raw->Type;
    obs->Length = raw->Length;
    obs->Status = raw->Status;
}

/* One query through io->Query, or, with none (or no buffer for a string),
 * the observation of a call never made: no call and XHCI_PSW_ST_NOT_RUN,
 * which every classification reads as unread or malformed. */
static VOID xhciPswAsk(const XHCI_PSW_IO *io, ULONG which, PXHCI_PSW_RAW raw,
                       ULONG capChars)
{
    raw->Calls = 0;
    raw->Type = 0;
    raw->Length = 0;
    raw->Status = XHCI_PSW_ST_NOT_RUN;
    raw->Dword = 0;
    if (io->Query == NULL || (capChars != 0 && io->Text == NULL)) {
        return;
    }
    io->Query(io->Context, which, raw, capChars != 0 ? io->Text : NULL,
              capChars);
}

static VOID xhciPswAskDword(const XHCI_PSW_IO *io, ULONG which,
                            PXHCI_PSW_OBS obs)
{
    XHCI_PSW_RAW raw;
    ULONG value;

    xhciPswAsk(io, which, &raw, 0);
    xhciPswObsSet(obs, &raw);
    value = XHCI_PSW_UNREAD;
    obs->State = XhciPswClassifyDword(&raw, &value);
    obs->Value = (obs->State == XHCI_PSW_RD_TAKEN) ? value : XHCI_PSW_UNREAD;
}

/* A string reading; returns its classification, the text left in
 * io->Text. */
static ULONG xhciPswAskString(const XHCI_PSW_IO *io, ULONG which,
                              ULONG capChars, PXHCI_PSW_OBS obs)
{
    XHCI_PSW_RAW raw;

    xhciPswAsk(io, which, &raw, capChars);
    xhciPswObsSet(obs, &raw);
    obs->State = XhciPswClassifyString(&raw, io->Text, capChars);
    return obs->State;
}

static VOID xhciPswAskExists(const XHCI_PSW_IO *io, ULONG which,
                             ULONG pathCounts, PXHCI_PSW_OBS obs)
{
    XHCI_PSW_RAW raw;

    xhciPswAsk(io, which, &raw, 0);
    xhciPswObsSet(obs, &raw);
    obs->State = XhciPswClassifyExists(&raw, pathCounts);
    obs->Value = obs->State;
}

ULONG XhciPswGuardRun(const XHCI_PSW_IO *io, PXHCI_PSW_GUARD g)
{
    ULONG out;
    ULONG failed;

    if (g == NULL) {
        return 0;
    }
    xhciPswGuardClear(g);
    if (io == NULL) {
        g->Outcome = XHCI_PSW_GUARD_NO_ROUTINE;
        return 0;
    }

    /* Step 1: G, only an exact REG_DWORD 1 armed. */
    xhciPswAskDword(io, XHCI_PSW_Q_READY, &g->Ready);
    g->G = (g->Ready.State == XHCI_PSW_RD_TAKEN && g->Ready.Value == 1)
               ? 1UL
               : 0UL;

    /* Step 2: the routine. */
    out = 0;
    if (io->Guard == NULL ||
        !io->Guard(io->Context, XHCI_PSW_OP_ROUTINE, 0, &out)) {
        g->Outcome = XHCI_PSW_GUARD_NO_ROUTINE;
        return 0;
    }
    g->Routine = 1;

    /* Steps 3 to 5, with the session key closed on every path. */
    failed = XHCI_PSW_GUARD_NOT_RUN;
    out = 0;
    g->OpenStatus = io->Guard(io->Context, XHCI_PSW_OP_OPEN, 0, &out);
    g->Disposition = out;
    if (g->OpenStatus != XHCI_PSW_ST_SUCCESS ||
        (out != XHCI_PSW_KEY_CREATED && out != XHCI_PSW_KEY_OPENED)) {
        failed = XHCI_PSW_GUARD_KEY_FAILED;
        goto close;
    }
    /* Step 4: only the status Windows gives a stable child under a
     * volatile parent proves the key volatile. */
    out = 0;
    g->ProbeStatus = io->Guard(io->Context, XHCI_PSW_OP_PROBE, 0, &out);
    if (g->ProbeStatus != XHCI_PSW_ST_CHILD_VOLATILE) {
        failed = XHCI_PSW_GUARD_NOT_VOLATILE;
        goto close;
    }
    if (g->Disposition == XHCI_PSW_KEY_CREATED) {
        out = 0;
        g->WriteS = io->Guard(io->Context, XHCI_PSW_OP_WRITE_S, g->G, &out);
        xhciPswAskDword(io, XHCI_PSW_Q_SESSION, &g->Session);
        if (g->WriteS == XHCI_PSW_ST_SUCCESS &&
            g->Session.State == XHCI_PSW_RD_TAKEN &&
            g->Session.Value == g->G) {
            g->S = g->G;
        }
    } else {
        /* An existing key: G is not consulted on this path. */
        xhciPswAskDword(io, XHCI_PSW_Q_SESSION, &g->Session);
        if (g->Session.State == XHCI_PSW_RD_TAKEN &&
            (g->Session.Value == 0 || g->Session.Value == 1)) {
            g->S = g->Session.Value;
        }
    }

close:
    out = 0;
    (VOID)io->Guard(io->Context, XHCI_PSW_OP_CLOSE, 0, &out);
    if (failed != XHCI_PSW_GUARD_NOT_RUN) {
        g->Outcome = failed;
        return 0;
    }
    if (g->S == XHCI_PSW_S_UNKNOWN) {
        g->Outcome = XHCI_PSW_GUARD_S_UNKNOWN;
        return 0;
    }

    /* Step 6: arming, only on proven session evidence. A failed write
     * leaves G 0, and the next session begins unarmed again. */
    if (g->G == 0) {
        out = 0;
        g->WriteG = io->Guard(io->Context, XHCI_PSW_OP_WRITE_G, 1, &out);
    }
    g->Outcome = (g->S == 1) ? XHCI_PSW_GUARD_ALLOWS : XHCI_PSW_GUARD_FIRST;
    return g->S == 1;
}

/* The image state's text, ASCII, for the record and the Tree. */
static VOID xhciPswKeepImage(PXHCI_PSW_LIFE life, const USHORT *text,
                             ULONG chars)
{
    ULONG i;
    USHORT c;

    for (i = 0; i < XHCI_PSW_IMAGE_TEXT; i++) {
        life->ImageText[i] = 0;
    }
    for (i = 0; i < chars && i < XHCI_PSW_IMAGE_TEXT; i++) {
        c = text[i];
        life->ImageText[i] = (UCHAR)((c >= 0x20 && c < 0x7F) ? c : '?');
    }
}

/*
 * Every reading of the step is made whatever an earlier one said (they
 * have no side effects), so the record is complete; the guard's writes are
 * its own. On 98 SE and ME only the setting is read: 2 holds, and nothing
 * else exists there to hold. IRQL: the callbacks'.
 */
static VOID xhciPswStep(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life)
{
    ULONG state[XHCI_PSW_SETUP_VALUES];
    ULONG value[XHCI_PSW_SETUP_VALUES];
    ULONG setting;
    ULONG source;
    ULONG which;
    ULONG hold;
    ULONG i;

    life->Step = 1;
    setting = 0;
    source = XHCI_REG_FROM_NONE;
    life->SettingState = io->Value(io->Context, XHCI_PSW_WHICH_SETUP,
                                   &setting, &source);
    life->SettingSource = source;
    life->SettingValue =
        (life->SettingState == XHCI_REG_TAKEN) ? setting : 0;
    life->Setting = XhciPswSettingInForce(life->SettingState,
                                          life->SettingValue, life->System);
    if (life->System == XHCI_PSW_SYS_9X) {
        if (life->Setting == XHCI_PSW_SETTING_HOLD) {
            life->Verdict = XHCI_PSW_V_HELD_SETTING;
        } else {
            life->Verdict = XHCI_PSW_V_ROUTE_9X;
        }
        return;
    }

    for (i = 0; i < XHCI_PSW_SETUP_VALUES; i++) {
        xhciPswAskDword(io, i, &life->Setup[i]);
        state[i] = life->Setup[i].State;
        value[i] = life->Setup[i].Value;
    }

    /* Windows PE: the MiniNT key, then the boot options' MININT token. */
    xhciPswAskExists(io, XHCI_PSW_Q_MININT, 1, &life->PeKey);
    life->PeKey.Value = (life->PeKey.State == XHCI_PSW_EX_THERE)
                            ? XHCI_PSW_PE_YES
                            : (life->PeKey.State == XHCI_PSW_EX_NOT)
                                  ? XHCI_PSW_PE_NOT
                                  : XHCI_PSW_PE_UNREAD;
    switch (xhciPswAskString(io, XHCI_PSW_Q_OPTIONS, XHCI_PSW_OPTIONS_CHARS,
                             &life->PeOptions)) {
    case XHCI_PSW_SZ_VALID:
        life->PeOptions.Value =
            XhciPswOptionsMinint(io->Text, life->PeOptions.Length / 2 - 1)
                ? XHCI_PSW_PE_YES
                : XHCI_PSW_PE_NOT;
        break;
    case XHCI_PSW_SZ_ABSENT:
        life->PeOptions.Value = XHCI_PSW_PE_NOT;
        break;
    default:
        life->PeOptions.Value = XHCI_PSW_PE_UNREAD;
        break;
    }

    /* The image state, Vista and later: the hive first. */
    if (life->System == XHCI_PSW_SYS_NT6) {
        xhciPswAskExists(io, XHCI_PSW_Q_HIVE, 0, &life->Hive);
        if (life->Hive.State == XHCI_PSW_EX_THERE) {
            switch (xhciPswAskString(io, XHCI_PSW_Q_IMAGE,
                                     XHCI_PSW_IMAGE_CHARS, &life->Image)) {
            case XHCI_PSW_SZ_VALID:
                xhciPswKeepImage(life, io->Text,
                                 life->Image.Length / 2 - 1);
                life->Image.Value =
                    XhciPswImageComplete(io->Text,
                                         life->Image.Length / 2 - 1)
                        ? XHCI_PSW_IMG_COMPLETE
                        : XHCI_PSW_IMG_NOT_COMPLETE;
                break;
            case XHCI_PSW_SZ_ABSENT:
                life->Image.Value = XHCI_PSW_IMG_NOT_COMPLETE;
                break;
            default:
                life->Image.Value = XHCI_PSW_IMG_UNREAD;
                break;
            }
        } else {
            life->Image.Value = (life->Hive.State == XHCI_PSW_EX_NOT)
                                    ? XHCI_PSW_IMG_NOT_LOADED
                                    : XHCI_PSW_IMG_UNREAD;
        }
    } else {
        life->Image.Value = XHCI_PSW_IMG_NOT_READ;
    }

    (VOID)XhciPswGuardRun(io, &life->Guard);

    /* The verdict, the first that applies (record 16 section 21.3). No
     * check permits against another. */
    life->VerdictValue = 0;
    if (life->Setting == XHCI_PSW_SETTING_HOLD) {
        life->Verdict = XHCI_PSW_V_HELD_SETTING;
        return;
    }
    if (life->Setting == XHCI_PSW_SETTING_BYPASS) {
        life->Verdict = XHCI_PSW_V_BYPASS;
        return;
    }
    which = 0;
    hold = XhciPswSetupHold(state, value, &which);
    if (hold != XHCI_PSW_V_NONE) {
        life->Verdict = hold;
        life->VerdictValue = which;
        return;
    }
    if (life->PeKey.Value == XHCI_PSW_PE_YES ||
        life->PeOptions.Value == XHCI_PSW_PE_YES) {
        life->Verdict = XHCI_PSW_V_HELD_PE;
        life->VerdictValue = (life->PeKey.Value == XHCI_PSW_PE_YES)
                                 ? XHCI_PSW_Q_MININT
                                 : XHCI_PSW_Q_OPTIONS;
        return;
    }
    if (life->PeKey.Value != XHCI_PSW_PE_NOT ||
        life->PeOptions.Value != XHCI_PSW_PE_NOT) {
        life->Verdict = XHCI_PSW_V_HELD_PE_UNREAD;
        life->VerdictValue = (life->PeKey.Value != XHCI_PSW_PE_NOT)
                                 ? XHCI_PSW_Q_MININT
                                 : XHCI_PSW_Q_OPTIONS;
        return;
    }
    if (life->System == XHCI_PSW_SYS_NT6 &&
        life->Image.Value != XHCI_PSW_IMG_COMPLETE) {
        life->Verdict =
            (life->Image.Value == XHCI_PSW_IMG_NOT_COMPLETE)
                ? XHCI_PSW_V_HELD_IMAGE
                : (life->Image.Value == XHCI_PSW_IMG_NOT_LOADED)
                      ? XHCI_PSW_V_HELD_IMAGE_NOT_LOADED
                      : XHCI_PSW_V_HELD_IMAGE_UNREAD;
        return;
    }
    if (life->Guard.Outcome != XHCI_PSW_GUARD_ALLOWS) {
        life->Verdict = (life->Guard.Outcome == XHCI_PSW_GUARD_FIRST)
                            ? XHCI_PSW_V_HELD_FIRST
                            : XHCI_PSW_V_HELD_SESSION;
        life->VerdictValue = life->Guard.Outcome;
        return;
    }
    life->Verdict = XHCI_PSW_V_ROUTE;
}

ULONG XhciPswRecordApplies(const XHCI_PSW_LIFE *life)
{
    return (life != NULL && life->Step != 0) ? 1UL : 0UL;
}

ULONG XhciPswShutdownAllowed(const XHCI_PSW_LIFE *life)
{
    if (life == NULL) {
        return 1;
    }
    return life->Decision != XHCI_PSW_DECIDE_SETUP_HELD;
}

/* ---- the record --------------------------------------------------------- */

static ULONG xhciPswByte(ULONG v)
{
    return (v > 0xFFUL) ? 0xFFUL : v;
}

/* State, calls, type and a fourth byte (a length or an answer), each held
 * to a byte. */
static ULONG xhciPswPack(ULONG a, ULONG b, ULONG c, ULONG d)
{
    return xhciPswByte(a) | (xhciPswByte(b) << 8) | (xhciPswByte(c) << 16) |
           (xhciPswByte(d) << 24);
}

VOID XhciPswRecordBuild(const XHCI_PSW_LIFE *life, const XHCI_PSW_REC_IN *in,
                        PULONG words)
{
    const XHCI_PSW_OBS *o;
    const XHCI_PSW_GUARD *g;
    ULONG i;
    ULONG at;

    for (i = 0; i < XHCI_PSW_REC_WORDS; i++) {
        words[i] = 0;
    }
    words[0] = XHCI_PSW_REC_MAGIC;
    words[1] = XHCI_PSW_REC_VERSION | (XHCI_PSW_REC_WORDS << 16);
    if (in != NULL) {
        words[2] = in->TimeLo;
        words[3] = in->TimeHi;
        words[4] = in->DriverVersion;
        words[5] = (in->Flavour & 0xFFUL) << 8;
    }
    if (life == NULL) {
        return;
    }
    words[5] |= (life->System & 0xFFUL) | ((life->Step & 0xFFUL) << 16);
    words[6] = (life->Decision & 0xFFUL) | ((life->Verdict & 0xFFUL) << 8) |
               ((life->VerdictValue & 0xFFFFUL) << 16);
    words[7] = life->Id;
    words[8] = life->Subsystem;
    words[9] = life->Value;
    words[10] = xhciPswPack(life->SwitchState, life->SwitchSource,
                            life->Mode, life->On);
    words[11] = life->SettingValue;
    words[12] = xhciPswPack(life->SettingState, life->SettingSource,
                            life->Setting, 0);
    for (i = 0; i < XHCI_PSW_SETUP_VALUES; i++) {
        o = &life->Setup[i];
        at = XHCI_PSW_REC_AT_SETUP + i * 3;
        words[at] = xhciPswPack(o->State, o->Calls, o->Type, o->Length);
        words[at + 1] = o->Value;
        words[at + 2] = o->Status;
    }
    at = XHCI_PSW_REC_AT_PE;
    o = &life->PeKey;
    words[at] = xhciPswPack(o->State, o->Calls, o->Type, o->Value);
    words[at + 1] = o->Status;
    o = &life->PeOptions;
    words[at + 2] = xhciPswPack(o->State, o->Calls, o->Type, o->Value);
    words[at + 3] = o->Status;
    words[at + 4] = o->Length;
    at = XHCI_PSW_REC_AT_IMAGE;
    o = &life->Hive;
    words[at] = xhciPswPack(o->State, o->Calls, o->Type, o->Value);
    words[at + 1] = o->Status;
    o = &life->Image;
    words[at + 2] = xhciPswPack(o->State, o->Calls, o->Type, o->Value);
    words[at + 3] = o->Status;
    words[at + 4] = o->Length;
    for (i = 0; i < XHCI_PSW_IMAGE_TEXT; i++) {
        words[XHCI_PSW_REC_AT_TEXT + i / 4] |=
            (ULONG)life->ImageText[i] << ((i % 4) * 8);
    }
    g = &life->Guard;
    at = XHCI_PSW_REC_AT_GUARD;
    words[at] = xhciPswPack(g->G, g->Outcome, g->Disposition, g->S);
    words[at + 1] = g->Routine;
    words[at + 2] = xhciPswPack(g->Ready.State, g->Ready.Calls,
                                g->Ready.Type, g->Ready.Length);
    words[at + 3] = g->Ready.Value;
    words[at + 4] = g->Ready.Status;
    words[at + 5] = g->OpenStatus;
    words[at + 6] = g->ProbeStatus;
    words[at + 7] = g->WriteS;
    words[at + 8] = xhciPswPack(g->Session.State, g->Session.Calls,
                                g->Session.Type, g->Session.Length);
    words[at + 9] = g->Session.Value;
    words[at + 10] = g->Session.Status;
    words[at + 11] = g->WriteG;
    words[XHCI_PSW_REC_AT_ROUTE] = (life->RouteStep & 0xFFUL) |
                                   ((life->RouteWritten & 0xFFUL) << 8);
}

static ULONG xhciPswLe32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

/* The four bytes of a packed word back into an observation's fields: the
 * fourth into Length (lengthByte) or into Value, where 0xFF is the
 * all-ones of a value not read. */
static VOID xhciPswUnpack(ULONG w, PXHCI_PSW_OBS o, ULONG lengthByte)
{
    o->State = w & 0xFFUL;
    o->Calls = (w >> 8) & 0xFFUL;
    o->Type = (w >> 16) & 0xFFUL;
    if (lengthByte) {
        o->Length = (w >> 24) & 0xFFUL;
    } else {
        o->Value = (w >> 24) & 0xFFUL;
        if (o->Value == 0xFFUL) {
            o->Value = XHCI_PSW_UNREAD;
        }
    }
}

ULONG XhciPswRecordRead(ULONG present, ULONG binary, const UCHAR *data,
                        ULONG length, PXHCI_PSW_LIFE life,
                        XHCI_PSW_REC_IN *in)
{
    ULONG w[XHCI_PSW_REC_WORDS];
    PXHCI_PSW_OBS o;
    PXHCI_PSW_GUARD g;
    ULONG i;
    ULONG at;

    if (life != NULL) {
        xhciPswLifeClear(life);
    }
    if (in != NULL) {
        in->TimeLo = 0;
        in->TimeHi = 0;
        in->DriverVersion = 0;
        in->Flavour = 0;
    }
    if (!present) {
        return XHCI_PSW_REC_ABSENT;
    }
    if (!binary || data == NULL || length < XHCI_PSW_REC_BYTES ||
        xhciPswLe32(data) != XHCI_PSW_REC_MAGIC ||
        (xhciPswLe32(data + 4) & 0xFFFFUL) != XHCI_PSW_REC_VERSION) {
        return XHCI_PSW_REC_FOREIGN;
    }
    for (i = 0; i < XHCI_PSW_REC_WORDS; i++) {
        w[i] = xhciPswLe32(data + i * 4);
    }
    if (in != NULL) {
        in->TimeLo = w[2];
        in->TimeHi = w[3];
        in->DriverVersion = w[4];
        in->Flavour = (w[5] >> 8) & 0xFFUL;
    }
    if (life == NULL) {
        return XHCI_PSW_REC_OK;
    }
    life->System = w[5] & 0xFFUL;
    life->Step = (w[5] >> 16) & 0xFFUL;
    life->Decision = w[6] & 0xFFUL;
    life->Verdict = (w[6] >> 8) & 0xFFUL;
    life->VerdictValue = (w[6] >> 16) & 0xFFFFUL;
    life->Id = w[7];
    life->Subsystem = w[8];
    life->Value = w[9];
    life->SwitchState = w[10] & 0xFFUL;
    life->SwitchSource = (w[10] >> 8) & 0xFFUL;
    life->Mode = (w[10] >> 16) & 0xFFUL;
    life->On = (w[10] >> 24) & 0xFFUL;
    life->Found = (life->SwitchState == XHCI_REG_TAKEN) ? 1UL : 0UL;
    life->SettingValue = w[11];
    life->SettingState = w[12] & 0xFFUL;
    life->SettingSource = (w[12] >> 8) & 0xFFUL;
    life->Setting = (w[12] >> 16) & 0xFFUL;
    for (i = 0; i < XHCI_PSW_SETUP_VALUES; i++) {
        o = &life->Setup[i];
        at = XHCI_PSW_REC_AT_SETUP + i * 3;
        xhciPswUnpack(w[at], o, 1);
        o->Value = w[at + 1];
        o->Status = w[at + 2];
    }
    at = XHCI_PSW_REC_AT_PE;
    xhciPswUnpack(w[at], &life->PeKey, 0);
    life->PeKey.Status = w[at + 1];
    xhciPswUnpack(w[at + 2], &life->PeOptions, 0);
    life->PeOptions.Status = w[at + 3];
    life->PeOptions.Length = w[at + 4];
    at = XHCI_PSW_REC_AT_IMAGE;
    xhciPswUnpack(w[at], &life->Hive, 0);
    life->Hive.Status = w[at + 1];
    xhciPswUnpack(w[at + 2], &life->Image, 0);
    life->Image.Status = w[at + 3];
    life->Image.Length = w[at + 4];
    for (i = 0; i < XHCI_PSW_IMAGE_TEXT; i++) {
        life->ImageText[i] =
            (UCHAR)((w[XHCI_PSW_REC_AT_TEXT + i / 4] >> ((i % 4) * 8)) &
                    0xFFUL);
    }
    g = &life->Guard;
    at = XHCI_PSW_REC_AT_GUARD;
    g->G = w[at] & 0xFFUL;
    g->Outcome = (w[at] >> 8) & 0xFFUL;
    g->Disposition = (w[at] >> 16) & 0xFFUL;
    g->S = (w[at] >> 24) & 0xFFUL;
    g->Routine = w[at + 1];
    xhciPswUnpack(w[at + 2], &g->Ready, 1);
    g->Ready.Value = w[at + 3];
    g->Ready.Status = w[at + 4];
    g->OpenStatus = w[at + 5];
    g->ProbeStatus = w[at + 6];
    g->WriteS = w[at + 7];
    xhciPswUnpack(w[at + 8], &g->Session, 1);
    g->Session.Value = w[at + 9];
    g->Session.Status = w[at + 10];
    g->WriteG = w[at + 11];
    life->RouteStep = w[XHCI_PSW_REC_AT_ROUTE] & 0xFFUL;
    life->RouteWritten = (w[XHCI_PSW_REC_AT_ROUTE] >> 8) & 0xFFUL;
    return XHCI_PSW_REC_OK;
}
