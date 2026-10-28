// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// polltun.h - the polling mode's settings (PLAN "Polling mode", 0.4.0.0):
// defaults, bounds and the clamping rule. Pure arithmetic with no port or OS
// call, so the host unit test (tests/unit/poll_ut.c) includes it unchanged.
//
// Where the values come from (read once in HwFindAdapter, kept in the device
// extension, which survives an NT resume that brings no ArgumentString):
//   NT  DriverParameter (REG_SZ) in HKLM\System\CurrentControlSet\Services\
//       stor98\Parameters\Device: storpollmode=<n>;storpollbusyus=<n>;
//       storpollidlems=<n>;storpollspinus=<n> (decimal; pollmode= and the
//       other names before xhci98 3.3.0.0 are ignored). No key may occur
//       inside another (dpkeys.h): the parser finds a key anywhere in the
//       string.
//   9x  REG_SZ (decimal) or REG_DWORD values StorPollMode, StorPollBusyUs,
//       StorPollIdleMs, StorPollSpinUs in HKLM\System\CurrentControlSet\
//       Services\stor98, the key StorWriteThrough also uses (PLAN D3).
//
// A value that is absent takes its default. A present value outside its
// bounds is clamped to the nearer bound, and the clamp is recorded (one bit
// per value) for the driver's log (debug-port stage STG_POLL_TUNABLES).
//

#ifndef _STOR98_POLLTUN_H_
#define _STOR98_POLLTUN_H_

// PollMode (9x value StorPollMode, NT key storpollmode=).
// 0 = automatic (poll only when the OS gives the controller no
// usable interrupt), 1 = always poll (testing; the interrupt is not used even
// where one exists). Anything above 1 is clamped to 1.
#define POLL_MODE_AUTO          0
#define POLL_MODE_FORCE         1

// PollBusyUs (9x value StorPollBusyUs, NT key storpollbusyus=).
// the timer period, in microseconds, while any command is issued
// or any request is held. 0 = the default. Bounds: 1 ms (the finest period
// the 9x port expresses; 98 SE delivered 1 ms periods on time, PLAN
// "Polling mode") to 100 ms (the engine's watchdog period: polling never
// runs slower than the interrupt mode's lost-interrupt check). NT uses at
// least the 10 ms fast tick (lifecycle.c, LcPollBusyUs): its port rounds
// every period up to the OS clock tick, so a shorter value changes nothing
// there. Default 1 ms, from the 98 SE measurements (PLAN).
#define POLL_BUSY_US_DEFAULT    1000
#define POLL_BUSY_US_MIN        1000
#define POLL_BUSY_US_MAX        100000

// PollIdleMs (9x value StorPollIdleMs, NT key storpollidlems=).
// the timer period, in milliseconds, while nothing is issued or
// held and nothing is unflushed. 0 = no idle polling: the timer stops when
// idle, as it does with an interrupt, and the next request restarts it.
// Bounds otherwise 100 ms to 60 s.
#define POLL_IDLE_MS_DEFAULT    1000
#define POLL_IDLE_MS_MIN        100
#define POLL_IDLE_MS_MAX        60000

// PollSpinUs (9x value StorPollSpinUs, NT key storpollspinus=).
// after HwStartIo has submitted work, poll the completion queue
// for at most this many microseconds, in stalls of POLL_SPIN_STEP_US, so a
// short command completes in the same callback instead of at the next tick.
// 0 = no spin. At most 500 us: half the per-callback stall budget (PLAN
// 0.10), the rest stays for the callback's other work. Default per family
// (maintainer, 2026-10-16): 500 us on NT, whose timer cannot go below the
// OS clock tick (without the spin forced polling ran at about a tenth of the
// interrupt throughput on 2000, with it about the same); 0 on 9x, where 1 ms
// periods work and the CPU is scarce.
#ifdef NVME2K_W9X
#define POLL_SPIN_US_DEFAULT    0
#else
#define POLL_SPIN_US_DEFAULT    500
#endif
#define POLL_SPIN_US_MIN        0
#define POLL_SPIN_US_MAX        500
#define POLL_SPIN_STEP_US       10

// Clamp record (POLL_SETTINGS.Clamped)
#define POLL_CLAMP_MODE         0x01
#define POLL_CLAMP_BUSY         0x02
#define POLL_CLAMP_IDLE         0x04
#define POLL_CLAMP_SPIN         0x08

typedef struct _POLL_SETTINGS {
    ULONG Mode;             // POLL_MODE_*
    ULONG BusyUs;           // busy timer period (us)
    ULONG IdleMs;           // idle timer period (ms), 0 = timer stops when idle
    ULONG SpinUs;           // HwStartIo completion spin (us), 0 = none
    ULONG Clamped;          // POLL_CLAMP_* of the values clamped by the last PollSettingsApply
} POLL_SETTINGS;

// Raw values as read: Present is a POLL_CLAMP_* mask of the values found.
typedef struct _POLL_RAW {
    ULONG Present;
    ULONG Mode;
    ULONG BusyUs;
    ULONG IdleMs;
    ULONG SpinUs;
} POLL_RAW;

static ULONG PollClampRange(ULONG v, ULONG lo, ULONG hi, ULONG Bit, ULONG *Clamped)
{
    if (v < lo) {
        *Clamped |= Bit;
        return lo;
    }
    if (v > hi) {
        *Clamped |= Bit;
        return hi;
    }
    return v;
}

static VOID PollSettingsDefault(POLL_SETTINGS *s)
{
    s->Mode = POLL_MODE_AUTO;
    s->BusyUs = POLL_BUSY_US_DEFAULT;
    s->IdleMs = POLL_IDLE_MS_DEFAULT;
    s->SpinUs = POLL_SPIN_US_DEFAULT;
    s->Clamped = 0;
}

// Defaults first, then every present value with its rule for 0 and its
// bounds.
static VOID PollSettingsApply(POLL_SETTINGS *s, const POLL_RAW *r)
{
    PollSettingsDefault(s);
    if (r->Present & POLL_CLAMP_MODE) {
        s->Mode = PollClampRange(r->Mode, POLL_MODE_AUTO, POLL_MODE_FORCE, POLL_CLAMP_MODE, &s->Clamped);
    }
    if ((r->Present & POLL_CLAMP_BUSY) && r->BusyUs != 0) {
        s->BusyUs = PollClampRange(r->BusyUs, POLL_BUSY_US_MIN, POLL_BUSY_US_MAX, POLL_CLAMP_BUSY, &s->Clamped);
    }
    if (r->Present & POLL_CLAMP_IDLE) {
        s->IdleMs = (r->IdleMs == 0) ? 0 :
                    PollClampRange(r->IdleMs, POLL_IDLE_MS_MIN, POLL_IDLE_MS_MAX, POLL_CLAMP_IDLE, &s->Clamped);
    }
    if (r->Present & POLL_CLAMP_SPIN) {
        s->SpinUs = PollClampRange(r->SpinUs, POLL_SPIN_US_MIN, POLL_SPIN_US_MAX, POLL_CLAMP_SPIN, &s->Clamped);
    }
}

// Decimal digits of a REG_SZ value or a DriverParameter field, up to the
// first non-digit. FALSE when there is no digit; a value too large for 32
// bits saturates (and is then clamped to its maximum).
static BOOLEAN PollParseDec(const char *p, ULONG *Out)
{
    ULONG v = 0;
    BOOLEAN any = FALSE;

    if (p == NULL) {
        return FALSE;
    }
    while (*p == ' ') {
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        ULONG d = (ULONG)(*p - '0');

        v = (v > (0xFFFFFFFFUL - d) / 10) ? 0xFFFFFFFFUL : v * 10 + d;
        any = TRUE;
        p++;
    }
    if (any) {
        *Out = v;
    }
    return any;
}

//
// Timer period selection (lifecycle.c; one timer serves every periodic
// duty, so each duty's period is combined, never overwritten).
//
// PollEnsurePeriod (EngTimerEnsure): while polling, a request between the
// busy period and the watchdog tick is shortened to the busy period, but
// only while work is outstanding; an idle period, a dirty-idle watchdog
// tick or a CM link poll period with nothing outstanding is kept as asked.
static ULONG PollEnsurePeriod(ULONG RequestUs, BOOLEAN Polling, BOOLEAN Outstanding, ULONG BusyUs)
{
    if (Polling && Outstanding && RequestUs > BusyUs && RequestUs <= POLL_BUSY_US_MAX) {
        return BusyUs;
    }
    return RequestUs;
}

// PollRunningPeriod (LcWantedPeriodEngine, state running): busy -> the busy
// period when polling (no interrupt, or after a storm), else the watchdog
// tick; unflushed writes -> the watchdog tick (idle flush); idle -> the idle
// period in the polling mode (0: the timer stops), else 0.
static ULONG PollRunningPeriod(BOOLEAN Busy, BOOLEAN Dirty, BOOLEAN Polling, BOOLEAN Always, ULONG BusyUs,
                               ULONG IdleMs, ULONG WatchUs)
{
    if (Busy) {
        return Polling ? BusyUs : WatchUs;
    }
    if (Dirty) {
        return WatchUs;
    }
    return Always ? IdleMs * 1000 : 0;
}

// PollCombine (LcWantedPeriod): the engine's period and another duty's (the
// CM link-state poll, nt-link / pool9x), 0 meaning "none": the shorter one.
static ULONG PollCombine(ULONG EngineUs, ULONG OtherUs)
{
    if (OtherUs != 0 && (EngineUs == 0 || EngineUs > OtherUs)) {
        return OtherUs;
    }
    return EngineUs;
}

//
// The interrupt decision at HwFindAdapter (PLAN "Polling mode", detection).
// The adapter polls exactly when the interrupt it would report to the port
// is none, or when PollMode forces it. A natural reason wins over "forced",
// so the report says why the OS gave no interrupt even on a forcing system.
//   Pin       Interrupt Pin from config space (0: the function has no INTx,
//             for example an MSI-only controller).
//   Line      Interrupt Line (0 or FFh: no IRQ routed; the value the base
//             code reported to the port, so it is checked on every OS).
//   PinLine   FALSE when config space was too short to read them.
//   PortGives TRUE where the port fills ConfigInfo's interrupt from the OS's
//             resource assignment before HwFindAdapter (NT 2000 and later);
//             then PortLevel = PortVector = 0 means no interrupt resource.
//
#define POLL_WHY_INTERRUPT      0   // interrupt-driven
#define POLL_WHY_FORCED         1   // PollMode = 1
#define POLL_WHY_NO_PIN         2   // Interrupt Pin 0
#define POLL_WHY_NO_PORT_IRQ    3   // the OS assigned no interrupt resource
#define POLL_WHY_NO_LINE        4   // Interrupt Line 0 or FFh
#define POLL_WHY_NO_CONFIG      5   // Pin and Line unreadable

static ULONG PollDecide(ULONG Mode, BOOLEAN PinLine, UCHAR Pin, UCHAR Line, BOOLEAN PortGives,
                        ULONG PortLevel, ULONG PortVector)
{
    if (!PinLine) {
        return POLL_WHY_NO_CONFIG;
    }
    if (Pin == 0) {
        return POLL_WHY_NO_PIN;
    }
    if (PortGives && PortLevel == 0 && PortVector == 0) {
        return POLL_WHY_NO_PORT_IRQ;
    }
    if (Line == 0 || Line == 0xFF) {
        return POLL_WHY_NO_LINE;
    }
    if (Mode == POLL_MODE_FORCE) {
        return POLL_WHY_FORCED;
    }
    return POLL_WHY_INTERRUPT;
}

#endif // _STOR98_POLLTUN_H_
