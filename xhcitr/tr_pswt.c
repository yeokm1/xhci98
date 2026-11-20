/*
 * tr_pswt.c - the xHCI98 USB Tree's words for the Intel port switchover's
 * decision (roadmap-usb4 task 50.34; design record 16 section 21.7): what
 * held a controller's start, or that it routed, how each value read and
 * where the values in force were set, from the record XhciIntelPortSwitchResult
 * or the same words in the snapshot's HCD region. The record's reader is
 * the driver's own (src\xhci_psw.c, XhciPswRecordRead), so the two cannot
 * read the layout differently.
 *
 * The sentences are drafts for the owner to word (record 16 section 21.7,
 * Q8); they promise no move: each says only what held this start.
 *
 * Pure: built with XHCI_HOST_TEST beside ..\src\xhci_psw.c, no Windows
 * header, so test\test_trpsw.c links it as XHCITR does. C89.
 */

#include "../src/xhci_psw.h"
#include "tr_pswt.h"

typedef struct _TRP_OUT {
    char *Buf;
    unsigned long Cap;
    unsigned long Len;
} TRP_OUT;

static void trp_s(TRP_OUT *o, const char *s)
{
    while (*s != '\0') {
        if (o->Len + 1 < o->Cap) {
            o->Buf[o->Len] = *s;
            o->Len++;
            o->Buf[o->Len] = '\0';
        }
        s++;
    }
}

static void trp_u(TRP_OUT *o, unsigned long v)
{
    char t[12];
    int n;

    n = 0;
    do {
        t[n++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    } while (v != 0 && n < 11);
    while (n > 0) {
        char c[2];

        c[0] = t[--n];
        c[1] = '\0';
        trp_s(o, c);
    }
}

static void trp_x(TRP_OUT *o, unsigned long v)
{
    static const char hex[] = "0123456789ABCDEF";
    char t[11];
    int i;

    t[0] = '0';
    t[1] = 'x';
    for (i = 0; i < 8; i++) {
        t[2 + i] = hex[(v >> (28 - 4 * i)) & 0xFUL];
    }
    t[10] = '\0';
    trp_s(o, t);
}

static const char *const trpSetupName[XHCI_PSW_SETUP_VALUES] = {
    "SystemSetupInProgress", "SetupType", "OOBEInProgress",
    "MiniSetupInProgress"
};

static void trp_where(TRP_OUT *o, const char *name, unsigned long source)
{
    if (source == XHCI_REG_FROM_MACHINE) {
        trp_s(o, name);
        trp_s(o, " is set in the machine-wide key.");
    } else if (source == XHCI_REG_FROM_DEVICE) {
        trp_s(o, name);
        trp_s(o, " is set in this controller's key.");
    } else {
        trp_s(o, name);
        trp_s(o, " is not set.");
    }
}

/* How one Setup value read, for the unread line. */
static void trp_reading(TRP_OUT *o, const XHCI_PSW_OBS *v)
{
    if (v->State == XHCI_PSW_RD_ABSENT) {
        trp_s(o, "absent");
    } else if (v->State == XHCI_PSW_RD_MALFORMED) {
        trp_s(o, "malformed");
    } else {
        trp_s(o, "status ");
        trp_x(o, v->Status);
    }
}

static void trp_waiting_end(TRP_OUT *o)
{
    trp_s(o, "\nIf Windows Setup has finished and this does not change "
             "after you restart Windows (Restart, not Shut down), "
             "XhciIntelPortSwitchSetup 1 (bypass) moves them at the "
             "controller's next start; a device in use on them is "
             "disconnected then.");
}

int tr_psw_lines(const unsigned char *rec, unsigned long len,
                 unsigned long writeStatus, int fromSnapshot, char *out,
                 unsigned long cap)
{
    XHCI_PSW_LIFE life;
    TRP_OUT o;
    unsigned long v;
    unsigned long i;
    int waiting;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    o.Buf = out;
    o.Cap = cap;
    o.Len = 0;
    if (XhciPswRecordRead(rec != NULL ? 1UL : 0UL, 1, rec, len, &life,
                          NULL) != XHCI_PSW_REC_OK) {
        return 0;
    }
    waiting = 0;
    switch (life.Decision) {
    case XHCI_PSW_DECIDE_OFF:
        trp_s(&o, "Port switchover: off (XhciIntelPortSwitch ");
        if (life.SwitchState == XHCI_REG_TAKEN) {
            trp_u(&o, life.Value);
        } else {
            trp_s(&o, "not set");
        }
        trp_s(&o, "). This driver does not move the shared connectors "
                  "when it starts or when Windows wakes; at shutdown it may "
                  "still hand them back to the USB 2.0 controller "
                  "(XhciIntelShutdownToEhci).");
        break;
    case XHCI_PSW_DECIDE_BOARD_UNREAD:
        trp_s(&o, "Port switchover: not done: this board's identity could "
                  "not be read.");
        break;
    case XHCI_PSW_DECIDE_BOARD_REFUSED:
        trp_s(&o, "Port switchover: not done: this board cannot switch its "
                  "connectors.");
        break;
    case XHCI_PSW_DECIDE_ON:
        /* The decision is taken before the route runs and kept whatever
         * the route did, so what happened is the route's own outcome: its
         * step (XHCI_PSW_DONE when every access succeeded) and the writes
         * the chipset accepted. */
        if (life.Verdict == XHCI_PSW_V_BYPASS) {
            trp_s(&o, "Port switchover: on, Windows' Setup check bypassed "
                      "(XhciIntelPortSwitchSetup 1).");
        } else {
            trp_s(&o, "Port switchover: on.");
        }
        if (life.RouteStep == XHCI_PSW_DONE) {
            trp_s(&o, " The shared (usually blue) connectors were moved to "
                      "this controller at its start.");
        } else if (life.RouteWritten == 0) {
            trp_s(&o, " Moving the shared connectors was allowed at this "
                      "start, but no write to the chipset's switch "
                      "registers was accepted (step ");
            trp_u(&o, life.RouteStep);
            trp_s(&o, "), so they were not moved.");
        } else {
            trp_s(&o, " Moving the shared connectors was allowed at this "
                      "start, but not every step of it succeeded (step ");
            trp_u(&o, life.RouteStep);
            trp_s(&o, "), so some of them may still be on the USB 2.0 "
                      "controller.");
        }
        break;
    case XHCI_PSW_DECIDE_SETUP_HELD:
        switch (life.Verdict) {
        case XHCI_PSW_V_HELD_SETTING:
            trp_s(&o, "Port switchover: held by XhciIntelPortSwitchSetup 2. "
                      "The shared connectors are not moved.");
            break;
        case XHCI_PSW_V_HELD_SETUP:
            i = life.VerdictValue < XHCI_PSW_SETUP_VALUES ? life.VerdictValue
                                                          : 0;
            trp_s(&o, "Port switchover: waiting. Windows says Setup is in "
                      "progress (");
            trp_s(&o, trpSetupName[i]);
            trp_s(&o, " = ");
            trp_u(&o, life.Setup[i].Value);
            trp_s(&o, "), so the shared connectors were not moved at this "
                      "start.");
            waiting = 1;
            break;
        case XHCI_PSW_V_HELD_SETUPTYPE:
            trp_s(&o, "Port switchover: waiting. Windows' Setup value "
                      "SetupType is ");
            trp_u(&o, life.Setup[XHCI_PSW_Q_TYPE].Value);
            trp_s(&o, ". While it is not 0 the shared connectors are not "
                      "moved; on some systems it may stay set after Setup.");
            waiting = 1;
            break;
        case XHCI_PSW_V_HELD_SETUP_UNREAD:
            i = life.VerdictValue < XHCI_PSW_SETUP_VALUES ? life.VerdictValue
                                                          : 0;
            trp_s(&o, "Port switchover: waiting. A Windows Setup value could "
                      "not be read (");
            trp_s(&o, trpSetupName[i]);
            trp_s(&o, ": ");
            trp_reading(&o, &life.Setup[i]);
            trp_s(&o, "), so the shared connectors were not moved.");
            waiting = 1;
            break;
        case XHCI_PSW_V_HELD_PE:
            trp_s(&o, "Port switchover: waiting. Windows is running as "
                      "Windows PE (");
            trp_s(&o, life.VerdictValue == XHCI_PSW_Q_MININT
                          ? "the MiniNT key"
                          : "the boot option MININT");
            trp_s(&o, "), so the shared connectors were not moved.");
            waiting = 1;
            break;
        case XHCI_PSW_V_HELD_PE_UNREAD:
            trp_s(&o, "Port switchover: waiting. Whether Windows is running "
                      "as Windows PE could not be read (");
            trp_s(&o, life.VerdictValue == XHCI_PSW_Q_MININT
                          ? "the MiniNT key"
                          : "the boot options");
            trp_s(&o, "), so the shared connectors were not moved.");
            waiting = 1;
            break;
        case XHCI_PSW_V_HELD_IMAGE:
            trp_s(&o, "Port switchover: waiting. Windows' image state is ");
            if (life.ImageText[0] != 0) {
                char t[XHCI_PSW_IMAGE_TEXT + 1];

                for (i = 0; i < XHCI_PSW_IMAGE_TEXT; i++) {
                    t[i] = (char)life.ImageText[i];
                }
                t[XHCI_PSW_IMAGE_TEXT] = '\0';
                trp_s(&o, t);
            } else {
                trp_s(&o, "not set");
            }
            trp_s(&o, ", not complete, so the shared connectors were not "
                      "moved.");
            waiting = 1;
            break;
        case XHCI_PSW_V_HELD_IMAGE_UNREAD:
            trp_s(&o, "Port switchover: waiting. Windows' image state could "
                      "not be read, so the shared connectors were not "
                      "moved.");
            waiting = 1;
            break;
        case XHCI_PSW_V_HELD_IMAGE_NOT_LOADED:
            trp_s(&o, "Port switchover: waiting. Windows' image state could "
                      "not be read when this controller started: Windows had "
                      "not loaded it yet. It is read again at the "
                      "controller's next start (a restart of this "
                      "controller, or of Windows); the shared connectors "
                      "move then only if nothing else holds them. On this "
                      "computer that can differ from one start of Windows "
                      "to the next. XhciIntelPortSwitchSetup 1 (bypass) "
                      "moves them at every start, whatever Windows' Setup "
                      "state says; a device in use on them is disconnected "
                      "when they move. Do not rely on either for a drive "
                      "Windows starts from.");
            break;
        case XHCI_PSW_V_HELD_FIRST:
            trp_s(&o, "Port switchover: waiting. It was turned on for this "
                      "controller in this Windows session, so the shared "
                      "connectors are not moved before Windows is restarted "
                      "(Restart, not Shut down).");
            break;
        case XHCI_PSW_V_HELD_SESSION:
            if (life.VerdictValue == XHCI_PSW_GUARD_NOT_VOLATILE) {
                trp_s(&o, "Port switchover: waiting. The key "
                          "XhciIntelPortSwitchSession under this "
                          "controller's key is not one the driver made: "
                          "delete it, then restart Windows.");
            } else {
                trp_s(&o, "Port switchover: waiting. The driver could not "
                          "tell this Windows session from an earlier one");
                if (life.VerdictValue == XHCI_PSW_GUARD_NO_ROUTINE) {
                    trp_s(&o, " (the helper xhcihlpr.sys did not answer)");
                }
                trp_s(&o, ", so the shared connectors were not moved.");
                waiting = 1;
            }
            break;
        default:
            trp_s(&o, "Port switchover: waiting (reason ");
            trp_u(&o, life.Verdict);
            trp_s(&o, ").");
            break;
        }
        if (waiting) {
            trp_waiting_end(&o);
        }
        break;
    default:
        /* Not an Intel controller, or nothing decided: no line. */
        out[0] = '\0';
        return 0;
    }
    trp_s(&o, "\n");
    trp_where(&o, "XhciIntelPortSwitch", life.SwitchSource);
    if (life.Step) {
        trp_s(&o, " ");
        trp_where(&o, "XhciIntelPortSwitchSetup", life.SettingSource);
    }
    if (fromSnapshot && writeStatus != 0 &&
        writeStatus != XHCI_PSW_ST_NOT_RUN) {
        trp_s(&o, " (not saved in the registry: ");
        trp_x(&o, writeStatus);
        trp_s(&o, ")");
    }
    if (life.Step && life.System != XHCI_PSW_SYS_9X) {
        trp_s(&o, "\nRead at this start: Setup values");
        for (i = 0; i < XHCI_PSW_SETUP_VALUES; i++) {
            trp_s(&o, i == 0 ? " " : ", ");
            trp_s(&o, trpSetupName[i]);
            trp_s(&o, " ");
            if (life.Setup[i].State == XHCI_PSW_RD_TAKEN) {
                trp_u(&o, life.Setup[i].Value);
            } else {
                trp_reading(&o, &life.Setup[i]);
            }
        }
        trp_s(&o, "; Windows PE ");
        v = (life.PeKey.Value == XHCI_PSW_PE_YES ||
             life.PeOptions.Value == XHCI_PSW_PE_YES)
                ? 1
                : (life.PeKey.Value == XHCI_PSW_PE_NOT &&
                   life.PeOptions.Value == XHCI_PSW_PE_NOT)
                      ? 0
                      : 2;
        trp_s(&o, v == 1 ? "yes" : v == 0 ? "no" : "not read");
        if (life.System == XHCI_PSW_SYS_NT6) {
            trp_s(&o, "; image state ");
            v = life.Image.Value;
            trp_s(&o, v == XHCI_PSW_IMG_COMPLETE       ? "complete"
                      : v == XHCI_PSW_IMG_NOT_COMPLETE ? "not complete"
                      : v == XHCI_PSW_IMG_NOT_LOADED   ? "not loaded yet"
                                                       : "not read");
        }
        trp_s(&o, "; first-session check ");
        v = life.Guard.Outcome;
        trp_s(&o, v == XHCI_PSW_GUARD_ALLOWS        ? "a later session"
                  : v == XHCI_PSW_GUARD_FIRST       ? "the first session"
                  : v == XHCI_PSW_GUARD_NOT_VOLATILE
                      ? "a session key the driver did not make"
                      : "session not known");
        trp_s(&o, ".");
    }
    return 1;
}

int tr_psw_time(const unsigned char *rec, unsigned long len,
                unsigned long *lo, unsigned long *hi)
{
    XHCI_PSW_REC_IN in;

    *lo = 0;
    *hi = 0;
    if (XhciPswRecordRead(rec != NULL ? 1UL : 0UL, 1, rec, len, NULL,
                          &in) != XHCI_PSW_REC_OK) {
        return 0;
    }
    *lo = in.TimeLo;
    *hi = in.TimeHi;
    return 1;
}
