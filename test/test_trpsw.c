/*
 * test_trpsw.c - host vectors for the xHCI98 USB Tree's words for the
 * Intel port switchover's decision (xhcitr\tr_pswt.c; roadmap-usb4 task
 * 50.34, design record 16 section 21.7): each decision and verdict's line
 * from the record's words, where the values in force were set, the
 * readings line, the snapshot's unsaved-write note, a record that is not
 * one, and a buffer too small.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_psw.h"
#include "../xhcitr/tr_pswt.h"
#include "test_harness.h"

static XHCI_PSW_LIFE Life;
static UCHAR Bytes[XHCI_PSW_REC_BYTES];
static char Out[4096];

static void fresh(ULONG decision, ULONG verdict, ULONG value)
{
    (VOID)XhciPswLifeStart(NULL, &Life, NULL);
    Life.Decision = decision;
    Life.Verdict = verdict;
    Life.VerdictValue = value;
    Life.Id = 0x1E318086UL;
    Life.SwitchState = XHCI_REG_TAKEN;
    Life.SwitchSource = XHCI_REG_FROM_DEVICE;
    Life.Found = 1;
    Life.Value = 1;
    Life.Step = (decision == XHCI_PSW_DECIDE_ON ||
                 decision == XHCI_PSW_DECIDE_SETUP_HELD);
}

static int lines(ULONG writeStatus, int fromSnapshot)
{
    ULONG w[XHCI_PSW_REC_WORDS];
    XHCI_PSW_REC_IN in;
    ULONG i;

    in.TimeLo = 0x12345678UL;
    in.TimeHi = 0x01DB0000UL;
    in.DriverVersion = 0x03050100UL;
    in.Flavour = 1;
    XhciPswRecordBuild(&Life, &in, w);
    for (i = 0; i < XHCI_PSW_REC_WORDS; i++) {
        Bytes[i * 4] = (UCHAR)(w[i] & 0xFF);
        Bytes[i * 4 + 1] = (UCHAR)((w[i] >> 8) & 0xFF);
        Bytes[i * 4 + 2] = (UCHAR)((w[i] >> 16) & 0xFF);
        Bytes[i * 4 + 3] = (UCHAR)((w[i] >> 24) & 0xFF);
    }
    return tr_psw_lines(Bytes, XHCI_PSW_REC_BYTES, writeStatus, fromSnapshot,
                        Out, sizeof(Out));
}

static void has(const char *s, const char *what)
{
    if (strstr(Out, s) == NULL) {
        printf("missing \"%s\" in:\n%s\n", s, Out);
    }
    CHECK(strstr(Out, s) != NULL, what);
}

static void hasNot(const char *s, const char *what)
{
    CHECK(strstr(Out, s) == NULL, what);
}

int main(void)
{
    unsigned long lo;
    unsigned long hi;
    char small[24];

    fresh(XHCI_PSW_DECIDE_OFF, XHCI_PSW_V_NONE, 0);
    Life.Value = 0;
    CHECK_EQ(lines(0, 0), 1, "off");
    has("Port switchover: off (XhciIntelPortSwitch 0)", "off's line");
    has("XhciIntelShutdownToEhci", "the shutdown exception named");
    has("XhciIntelPortSwitch is set in this controller's key", "its key");
    hasNot("Read at this start", "no readings when off");
    fresh(XHCI_PSW_DECIDE_OFF, XHCI_PSW_V_NONE, 0);
    Life.SwitchState = XHCI_REG_ABSENT;
    Life.SwitchSource = XHCI_REG_FROM_NONE;
    (VOID)lines(0, 0);
    has("(XhciIntelPortSwitch not set)", "off, not set");
    has("XhciIntelPortSwitch is not set.", "no key");

    fresh(XHCI_PSW_DECIDE_ON, XHCI_PSW_V_ROUTE, 0);
    Life.On = 1;
    Life.Setup[0].State = XHCI_PSW_RD_TAKEN;
    Life.Setup[0].Value = 0;
    Life.Guard.Outcome = XHCI_PSW_GUARD_ALLOWS;
    Life.Image.Value = XHCI_PSW_IMG_COMPLETE;
    Life.System = XHCI_PSW_SYS_NT6;
    Life.RouteStep = XHCI_PSW_DONE;
    Life.RouteWritten = XHCI_PSW_WROTE_PSSEN | XHCI_PSW_WROTE_XUSB2PR;
    CHECK_EQ(lines(0, 1), 1, "routed");
    has("Port switchover: on. The shared (usually blue) connectors were "
        "moved", "on's line");
    has("Read at this start: Setup values SystemSetupInProgress 0",
        "the readings");
    has("image state complete", "the image");
    has("first-session check a later session", "the guard");
    hasNot("not saved", "a saved write says nothing");

    fresh(XHCI_PSW_DECIDE_ON, XHCI_PSW_V_ROUTE_9X, 0);
    Life.System = XHCI_PSW_SYS_9X;
    (VOID)lines(0, 0);
    has("Port switchover: on.", "98 SE routed");
    hasNot("Read at this start", "nothing read on 98 SE");

    /* The route's recorded outcome, not the decision alone. */
    fresh(XHCI_PSW_DECIDE_ON, XHCI_PSW_V_ROUTE, 0);
    Life.RouteStep = XHCI_PSW_STEP_USB3PRM;
    Life.RouteWritten = 0;
    (VOID)lines(0, 1);
    has("Port switchover: on. Moving the shared connectors was allowed at "
        "this start, but no write", "a route that wrote nothing");
    has("(step 1), so they were not moved", "its step");
    hasNot("were moved", "no move claimed");
    fresh(XHCI_PSW_DECIDE_ON, XHCI_PSW_V_ROUTE, 0);
    Life.RouteStep = XHCI_PSW_STEP_XUSB2PR_WRITE;
    Life.RouteWritten = XHCI_PSW_WROTE_PSSEN;
    (VOID)lines(0, 1);
    has("not every step of it succeeded (step 5), so some of them may still "
        "be on the USB 2.0 controller", "a partly written route");
    hasNot("were moved", "no move claimed, partly");

    fresh(XHCI_PSW_DECIDE_ON, XHCI_PSW_V_BYPASS, 0);
    Life.SettingSource = XHCI_REG_FROM_MACHINE;
    (VOID)lines(0xC0000022UL, 1);
    has("Windows' Setup check bypassed (XhciIntelPortSwitchSetup 1)",
        "bypass");
    has("XhciIntelPortSwitchSetup is set in the machine-wide key",
        "the setting's key");
    has("(not saved in the registry: 0xC0000022)", "the unsaved write");

    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SETTING, 0);
    (VOID)lines(0, 0);
    has("held by XhciIntelPortSwitchSetup 2", "always hold");
    hasNot("XhciIntelPortSwitchSetup 1 (bypass) moves", "no bypass advice");

    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SETUP, XHCI_PSW_Q_OOBE);
    Life.Setup[XHCI_PSW_Q_OOBE].State = XHCI_PSW_RD_TAKEN;
    Life.Setup[XHCI_PSW_Q_OOBE].Value = 1;
    (VOID)lines(0, 0);
    has("Windows says Setup is in progress (OOBEInProgress = 1)", "OOBE");
    has("XhciIntelPortSwitchSetup 1 (bypass) moves them at the controller's "
        "next start", "the waiting line's end");

    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SETUPTYPE,
          XHCI_PSW_Q_TYPE);
    Life.Setup[XHCI_PSW_Q_TYPE].State = XHCI_PSW_RD_TAKEN;
    Life.Setup[XHCI_PSW_Q_TYPE].Value = 2;
    (VOID)lines(0, 0);
    has("SetupType is 2. While it is not 0", "the veto");

    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SETUP_UNREAD,
          XHCI_PSW_Q_SSIP);
    Life.Setup[0].State = XHCI_PSW_RD_UNREAD;
    Life.Setup[0].Status = 0xC0000022UL;
    (VOID)lines(0, 0);
    has("(SystemSetupInProgress: status 0xC0000022)", "unread with status");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SETUP_UNREAD,
          XHCI_PSW_Q_SSIP);
    Life.Setup[0].State = XHCI_PSW_RD_ABSENT;
    (VOID)lines(0, 0);
    has("(SystemSetupInProgress: absent)", "absent");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SETUP_UNREAD,
          XHCI_PSW_Q_MINI);
    Life.Setup[XHCI_PSW_Q_MINI].State = XHCI_PSW_RD_MALFORMED;
    (VOID)lines(0, 0);
    has("(MiniSetupInProgress: malformed)", "malformed");

    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_PE, XHCI_PSW_Q_MININT);
    (VOID)lines(0, 0);
    has("running as Windows PE (the MiniNT key)", "PE by the key");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_PE, XHCI_PSW_Q_OPTIONS);
    (VOID)lines(0, 0);
    has("(the boot option MININT)", "PE by the option");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_PE_UNREAD,
          XHCI_PSW_Q_OPTIONS);
    (VOID)lines(0, 0);
    has("could not be read (the boot options)", "PE unread");

    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_IMAGE, 0);
    memcpy(Life.ImageText, "IMAGE_STATE_UNDEPLOYABLE", 24);
    (VOID)lines(0, 0);
    has("image state is IMAGE_STATE_UNDEPLOYABLE, not complete", "image");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_IMAGE, 0);
    (VOID)lines(0, 0);
    has("image state is not set, not complete", "image absent");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_IMAGE_UNREAD, 0);
    (VOID)lines(0, 0);
    has("image state could not be read, so", "image unread");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_IMAGE_NOT_LOADED, 0);
    (VOID)lines(0, 0);
    has("Windows had not loaded it yet", "not loaded");
    has("Do not rely on either for a drive Windows starts from",
        "the operating restriction");

    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_FIRST,
          XHCI_PSW_GUARD_FIRST);
    (VOID)lines(0, 0);
    has("turned on for this controller in this Windows session", "first");
    has("(Restart, not Shut down)", "Restart named");
    hasNot("bypass", "no bypass advice for the guard");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SESSION,
          XHCI_PSW_GUARD_NO_ROUTINE);
    (VOID)lines(0, 0);
    has("could not tell this Windows session from an earlier one (the "
        "helper xhcihlpr.sys did not answer)", "no routine");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SESSION,
          XHCI_PSW_GUARD_KEY_FAILED);
    (VOID)lines(0, 0);
    has("from an earlier one, so", "key failed");
    fresh(XHCI_PSW_DECIDE_SETUP_HELD, XHCI_PSW_V_HELD_SESSION,
          XHCI_PSW_GUARD_NOT_VOLATILE);
    (VOID)lines(0, 0);
    has("is not one the driver made: delete it, then restart Windows",
        "a key not the driver's");

    fresh(XHCI_PSW_DECIDE_BOARD_REFUSED, XHCI_PSW_V_NONE, 0);
    (VOID)lines(0, 0);
    has("this board cannot switch", "Sony");

    fresh(XHCI_PSW_DECIDE_NOT_INTEL, XHCI_PSW_V_NONE, 0);
    CHECK_EQ(lines(0, 1), 0, "not Intel: no line");
    CHECK_EQ(Out[0], 0, "and nothing written");

    fresh(XHCI_PSW_DECIDE_ON, XHCI_PSW_V_ROUTE, 0);
    (VOID)lines(0, 0);
    Bytes[0] ^= 1;
    CHECK_EQ(tr_psw_lines(Bytes, XHCI_PSW_REC_BYTES, 0, 0, Out, sizeof(Out)),
             0, "another magic");
    Bytes[0] ^= 1;
    CHECK_EQ(tr_psw_lines(Bytes, XHCI_PSW_REC_BYTES - 4, 0, 0, Out,
                          sizeof(Out)), 0, "short");
    CHECK_EQ(tr_psw_lines(NULL, 0, 0, 0, Out, sizeof(Out)), 0, "none");
    CHECK_EQ(tr_psw_time(Bytes, XHCI_PSW_REC_BYTES, &lo, &hi), 1, "the time");
    CHECK_EQ(lo, 0x12345678UL, "its low word");
    CHECK_EQ(hi, 0x01DB0000UL, "its high word");
    memset(small, 'Z', sizeof(small));
    CHECK_EQ(tr_psw_lines(Bytes, XHCI_PSW_REC_BYTES, 0, 0, small,
                          sizeof(small)), 1, "a small buffer");
    CHECK_EQ(small[sizeof(small) - 1], 0, "held and terminated");
    CHECK_EQ(strlen(small), sizeof(small) - 1, "filled to its end");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
