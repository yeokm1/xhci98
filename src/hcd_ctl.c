/*
 * hcd_ctl.c - the controller FDO's start and stop, its interrupt, and its
 * PASSIVE-level thread (roadmap-hcd.md task 26-A.2; design record 13
 * sections 5.4 and 11).
 *
 * The controller sequence itself is the miniport's, kept in xhci_init.c,
 * xhci_cmd.c and xhci_evt.c. What this file replaces is what usbport did
 * around it: translate the PnP resources, map the BAR, connect the interrupt,
 * hand over a common buffer, and call the sequence in usbport's order -
 * StartController then EnableInterrupts at start, DisableInterrupts then
 * StopController at stop - and the 500 ms CheckController poll, which here is
 * the controller thread's.
 *
 * The thread (design record 13 section 5.4, layer 1) is the PASSIVE-level
 * context the HCD owns: a system thread rather than work items, because it
 * waits between steps. In this task it runs the health poll and the in-place
 * recovery; the enumeration machines of 26-A.4 joined it, and the log
 * flusher of 26-A.8 (hcd_log.c) runs at every wake.
 *
 * IRQL: each function carries its own.
 */

#include "hcd.h"
#include "xhci_dbg.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_usbport.h"

/* The resource mask XhciInitController holds the start to. The miniport
 * settled it from the registration packet's interface version, because NT 6.x
 * usbport named the two resources with different bits; the HCD fills the
 * USBPORT_RESOURCES copy itself, with the NT 5.x bits, on every target. */
ULONG XhciResourcesRequired = USBPORT_RESOURCES_MEMORY |
                              USBPORT_RESOURCES_INTERRUPT;

/* The device-key values the HCD reads (design record 13 section 5.5).
 * The miniport's three XhciVirtualHSHub* values are not read. The fourth,
 * XhciForceBulkOnly (31-A.3), is read at every enumeration rather than at
 * start: absent is 1 since 37.2, Bulk-Only where the device offers it, for
 * text-mode Setup (HcdCtlForceBulkOnly); the INFs' 0 keeps UAS.
 * XhciFastPollFsLs (33.8) is read at start: absent is 0, every interval by
 * Table 6-12. Since 34.1 the INFs write both, and the two below; the first
 * at 0, not at its absent value, since 37.2. Since 3.1.0.0 (task 42-A.17)
 * each is read from the machine-wide key first (hcdReadDword). */
#define HCD_VALUE_LOG_VERBOSITY L"XhciLogVerbosity"
#define HCD_VALUE_LOG_DEBUGVIEW L"XhciLogDebugView"
#define HCD_VALUE_IMOD          L"XhciImodInterval250ns"
#define HCD_VALUE_FORCE_BOT     L"XhciForceBulkOnly"
#define HCD_VALUE_FAST_POLL     L"XhciFastPollFsLs"
/* Task 33.3: the first answer's settle, in ms (xhci_enum.h). The INFs write
 * the defaults (34.1); text-mode Setup's txtsetup.oem writes none, so there
 * they are absent and the same defaults stand. */
#define HCD_VALUE_SETTLE_TOTAL  L"XhciFirstEnumWaitMs"
#define HCD_VALUE_SETTLE_PORT   L"XhciFirstEnumPortMs"
/* Task 34.3: the Intel port switchover, on a listed controller when the
 * value is a nonzero DWORD; absent, another type or 0 is off (task 36.42;
 * txtsetup.oem writes none); an exact 2 also on an unlisted Intel one
 * (task 35.5). Read at start, on every Intel controller and no other. */
#define HCD_VALUE_PORT_SWITCH   L"XhciIntelPortSwitch"
/* Task 36.3: the Intel PCH interrupt routing quirk; absent, another type
 * or another number than 0 to 2 is 1, the verified E7250 only
 * (xhci_pirq.h). Read at start; a return to D0 runs on the start's. */
#define HCD_VALUE_IRQ_ROUTE     L"XhciIntelIrqRoute"
/* 35-T (design record 17 sections 4.7 and 4.11): the tolerance off-switch,
 * the interrupt-interval cap and the Average TRB Length switch, read at
 * every start; absent, another type or another number takes the default. */
#define HCD_VALUE_TOLERANCE     L"XhciTolerance"
#define HCD_VALUE_INTERVAL_CAP  L"XhciIntervalCap"
#define HCD_VALUE_AVG_TRB_ESIT  L"XhciAvgTrbEsit"
/* Task 36.22: the command size xhciuas.sys advertises, in KiB, 64 to 2048
 * in steps of 4; anything else is 1024 (xhci_xstat.h, XhciXpolValue). Read
 * at every start; a change reaches the class driver at its next start, and
 * its storage stack at the next reboot. */
#define HCD_VALUE_MAX_TRANSFER  L"XhciMaxTransferKB"
/* Task 36.33: selective suspend, stages A and B (design record 14 section
 * 7). Default 0; the INFs write the 0 (task 36.51). */
#define HCD_VALUE_SELECTIVE_SUSPEND L"XhciSelectiveSuspend"
/* Task 36.24: stage 2's whole-URB mapping (design record 13 section 11.6,
 * point 2), written by the INFs at 0 (task 36.51): a DWORD, 0 off and
 * anything else on; absent or of another type is HCD_WIDE_MAP_DEFAULT.
 * Read at every start; on Windows 2000 and later only, and only past the
 * start's register-free check (hcd_dma.c, HcdDmaWideOpen). */
#define HCD_VALUE_WIDE_MAP      L"XhciWideMap"
/* Issue 18: the default speed IDs for a Full- or Low-Speed device behind a
 * hub on a USB 2.0 root port whose speed table names no such speed
 * (XhciPortPsivForHubChild), written by the INFs at 1 (task 36.51): only a
 * DWORD 0 turns it off; absent, another number or another type is on.
 * Read at every start. */
#define HCD_VALUE_HUB_SPEED     L"XhciUsb2HubSpeedFallback"
/* Task 38.11 (design record 17 section 4.14.4): the 0.96 floor, written by
 * the INFs at 1: only a DWORD 0 restores the 1.0 floor of 2.3.1.0; absent,
 * another number or another type is the 0.96 floor (XhciVersionFloor).
 * Read at every start and latched for its lifetime. */
#define HCD_VALUE_ACCEPT_096    L"XhciAccept096"
/* Task 38.2 (design record 13 section 6.11.3): the silent install's gate,
 * on Windows 98 SE and ME only. Only a DWORD of exactly 1 is on; absent,
 * 0, another number, another type or a failed read is off (O6). The INFs
 * write it at 0 on every path (owner, 2026-10-14; no INF wrote it before),
 * NOCLOBBER, so silent-on.bat's 1 survives an update. Read at every start.
 * The list it gates is a Services key, which no INF writes. */
#define HCD_VALUE_SILENT        L"XhciSilentInstall"
#define HCD_SILENT_LIST         L"USB\\SilentClasses"
#if defined(XHCI_FLAVOUR_QEMU)
/* Task 36.22, qemu flavour only and written by no INF: the map grant a
 * chunk may use, held to this many registers (1 to the real grant), so a
 * leg can constrain it; absent, 0 or larger leaves the grant as given. */
#define HCD_VALUE_QEMU_MAP_GRANT L"XhciQemuMapGrant"
#endif
/* Tasks 36.6 to 36.8 (design record 15 section 3): how events are
 * delivered - 0 automatic, 1 the line, 2 polling, 3 and above as 0 - and
 * the poll's idle and active periods, in ms. Read at every start, before
 * anything is connected; absent or of another type is the default. */
#define HCD_VALUE_INTR_MODE     L"XhciInterruptMode"
#define HCD_VALUE_POLL_IDLE     L"XhciPollIdleMs"
#define HCD_VALUE_POLL_ACTIVE   L"XhciPollActiveMs"
/* Task 38.3 (design record 15 section 7.2.4): the finer system timer while
 * polling - 1 is on; 0, absent, unreadable or not a DWORD is off; any
 * other value runs as 1 (off by default since the owner's change of
 * 2026-10-14). Read at every start; both INFs write 0 under
 * FLG_ADDREG_NOCLOBBER. Windows 98 SE and ME read it and do nothing. */
#define HCD_VALUE_POLL_FINE_TIMER L"XhciPollFineTimer"
#if defined(XHCI_FLAVOUR_QEMU)
/* Record 15 section 11's switches, qemu flavour only and written by no
 * INF, read at every start: no interrupt resource and pin 0; the pin's
 * read failing; and the dead line, N + 1 (0 off). */
#define HCD_VALUE_QEMU_NO_INTR  L"XhciQemuNoInterrupt"
#define HCD_VALUE_QEMU_PIN_FAIL L"XhciQemuPinUnreadable"
#define HCD_VALUE_QEMU_DEADLINE L"XhciQemuDeadLine"
/* Record 15 section 7.2.12's switches for legs T3 and T4, qemu only and
 * written by no INF, read at every start: the timer thread sleeps this many
 * ms before each call (held to 10 s); and 1 keeps the selective-suspend
 * executor from claiming a child's pended Dx (hcd_susp.c). */
#define HCD_VALUE_QEMU_TMR_DELAY L"XhciQemuTmrDelayMs"
#define HCD_VALUE_QEMU_SUSP_HOLD L"XhciQemuSuspHoldDx"
#endif
#if defined(XHCI_FLAVOUR_QEMU)
/* Task 35.4, qemu flavour only and written by no INF: 1 makes the USB 3
 * protocol's PSI table read as the E460's (xhci_caps.c); absent, another
 * type or any other number is off. Read at every start. */
#define HCD_VALUE_QEMU_PSI_E460 L"XhciQemuPsiE460"
/* Task 38.11, D7 (design record 17 section 4.14.10), qemu flavour only and
 * written by no INF: a DWORD 0x0001 to 0xFFFE is decoded as HCIVERSION in
 * place of the register's (xhci_init.c, xhciEffectiveCapDword0); absent, 0,
 * 0xFFFF and up, or another type applies nothing. Read at every start. */
#define HCD_VALUE_QEMU_HCIVERSION L"XhciQemuHciVersion"
#endif

/* The health poll's period. usbport's CheckController was nominally 500 ms
 * and measured at 36-80 ms on the E460 (run-13e, Finding V); the poll's
 * thresholds run on the frame clock, not on this period, which only bounds
 * how late a verdict is acted on. */
#define HCD_POLL_MS 100UL

/* --------------------------------------------------------------------- */
/* Registry                                                               */
/* --------------------------------------------------------------------- */

/* The machine-wide key every value is read from first (task 42-A.17,
 * xhci_regsrc.h), relative to RTL_REGISTRY_SERVICES: on every target
 * HKLM\System\CurrentControlSet\Services\USB, where the SilentClasses list
 * already lives (HCD_SILENT_LIST, read the same way on 98 SE and ME). */
#define HCD_MACHINE_KEY         L"USB"

/* What hcdTakeDword saw of one value in one key. */
typedef struct _HCD_REG_TAKE {
    ULONG Seen;                 /* called with a value of any type        */
    ULONG Taken;                /* a REG_DWORD of four bytes, in Value     */
    ULONG Value;
} HCD_REG_TAKE, *PHCD_REG_TAKE;

/*
 * The query routine for hcdQueryDword: a value is taken only when it is a
 * REG_DWORD of four bytes. RTL_QUERY_REGISTRY_DIRECT would instead write a
 * value of another type in that type's own form - a REG_SZ as a
 * UNICODE_STRING - over the four-byte destination (task 26-A.2), and the
 * flag that makes DIRECT check the type is
 * newer than both primary targets. Seen is set for a value of any type, a
 * stored REG_NONE with data included (XhciRegCallSeen), so the caller
 * tells "there, of another type" from "not there" without relying on the
 * status each target's RtlQueryRegistryValues returns for the two (task
 * 42-A.17). IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS NTAPI hcdTakeDword(PWSTR ValueName, ULONG ValueType,
                                   PVOID ValueData, ULONG ValueLength,
                                   PVOID Context, PVOID EntryContext)
{
    PHCD_REG_TAKE take;
    PUCHAR from;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);

    take = (PHCD_REG_TAKE)EntryContext;
    if (!XhciRegCallSeen(ValueType, ValueLength)) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    take->Seen = 1;
    if (ValueType != REG_DWORD || ValueLength != sizeof(ULONG) ||
        ValueData == NULL) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    from = (PUCHAR)ValueData;
    take->Value = (ULONG)from[0] | ((ULONG)from[1] << 8) |
                  ((ULONG)from[2] << 16) | ((ULONG)from[3] << 24);
    take->Taken = 1;
    return STATUS_SUCCESS;
}

/* One value from one key: `relativeTo` and `path` as RtlQueryRegistryValues
 * takes them. The reading is XhciRegState's, from what hcdTakeDword saw;
 * the query's own status is not consulted. IRQL: PASSIVE_LEVEL. */
static ULONG hcdQueryDword(ULONG relativeTo, PWSTR path, PCWSTR name,
                           PULONG value)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    HCD_REG_TAKE take;
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)table;
    for (i = 0; i < sizeof(table); i++) {
        p[i] = 0;
    }
    take.Seen = 0;
    take.Taken = 0;
    take.Value = 0;
    table[0].QueryRoutine = hcdTakeDword;
    /* NOEXPAND: a value mistakenly stored as REG_EXPAND_SZ is not expanded
     * before hcdTakeDword refuses its type (task 26-A.2). */
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = (PWSTR)name;
    table[0].EntryContext = &take;

    (VOID)RtlQueryRegistryValues(relativeTo, path, table, NULL, NULL);
    if (take.Taken) {
        *value = take.Value;
    }
    return XhciRegState(take.Seen, take.Taken);
}

/*
 * One REG_DWORD of the controller's: from the machine-wide key when it is
 * there in any type, else from the controller's driver (software) key, the
 * key the miniport's values lived in (task 42-A.17, xhci_regsrc.h).
 * Returns STATUS_SUCCESS with *value set for a REG_DWORD,
 * STATUS_OBJECT_TYPE_MISMATCH for a value of another type in the key that
 * stands, STATUS_OBJECT_NAME_NOT_FOUND for none - the caller's default
 * stands for both failures; *value is untouched on failure. A value taken
 * machine-wide is counted in RegMachineWon. IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdReadDword(PHCD_CONTROLLER hc, PCWSTR name, PULONG value)
{
    HANDLE key;
    ULONG machine;
    ULONG device;
    ULONG mValue;
    ULONG dValue;
    ULONG state;

    mValue = 0;
    dValue = 0;
    device = XHCI_REG_ABSENT;
    machine = hcdQueryDword(RTL_REGISTRY_SERVICES, (PWSTR)HCD_MACHINE_KEY,
                            name, &mValue);
    if (machine == XHCI_REG_ABSENT &&
        NT_SUCCESS(IoOpenDeviceRegistryKey(hc->Pdo, PLUGPLAY_REGKEY_DRIVER,
                                           KEY_READ, &key))) {
        device = hcdQueryDword(RTL_REGISTRY_HANDLE, (PWSTR)key, name,
                               &dValue);
        ZwClose(key);
    }
    state = XhciRegPickedState(machine, device);
    if (state == XHCI_REG_ABSENT) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (XhciRegPick(machine, device) == XHCI_REG_FROM_MACHINE) {
        hc->RegMachineWon++;
    }
    if (state != XHCI_REG_TAKEN) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    *value = (machine == XHCI_REG_TAKEN) ? mValue : dValue;
    return STATUS_SUCCESS;
}

/* The same read for hcd_msi86.c's mode cache, from AddDevice and the
 * start, both system-thread contexts as IoOpenDeviceRegistryKey needs.
 * IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdReadValueDword(PHCD_CONTROLLER hc, PCWSTR name, PULONG value)
{
    return hcdReadDword(hc, name, value);
}

/* The log's two switches, first of the start's values: a start the delivery
 * refuses reads these alone, to record its refusal (hcdLogRefused).
 * IRQL: PASSIVE_LEVEL. */
static VOID hcdReadLogSwitches(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG verbosity;
    ULONG debugView;
    NTSTATUS status;

    ext = &hc->Hc;
    verbosity = 0;
    debugView = 0;
    /* The read statuses travel in XHCISNAP's header (schema 5), so a reader
     * tells a zero somebody set from a value never found. */
    ext->Log.SwitchRead = 1;
    status = hcdReadDword(hc, HCD_VALUE_LOG_VERBOSITY, &verbosity);
    ext->Log.SwitchStatusVerbosity = NT_SUCCESS(status) ? MP_STATUS_SUCCESS
                                                        : MP_STATUS_FAILURE;
    /* An absent value is the default (xhci_log.h; 2 since the owner's
     * decision of 2026-10-16), not 0. */
    if (!NT_SUCCESS(status)) {
        verbosity = XHCI_LOG_VERBOSITY_DEFAULT;
    }
    status = hcdReadDword(hc, HCD_VALUE_LOG_DEBUGVIEW, &debugView);
    ext->Log.SwitchStatusDebugView = NT_SUCCESS(status) ? MP_STATUS_SUCCESS
                                                        : MP_STATUS_FAILURE;
    (VOID)XhciLogApplySwitches(&ext->Log, verbosity, debugView);
    XhciLogNote(ext, "log.verbosity", ext->Log.Verbosity);
    XhciLogNote(ext, "log.verbosity.read", ext->Log.VerbosityRead);
    XhciLogNote(ext, "log.verbosity.refused", ext->Log.VerbosityRefused);
    XhciLogNote(ext, "log.debugview", ext->Log.DebugViewEnabled);
}

/*
 * Task 38.11's three counter words (design record 17 section 4.14.7) from
 * what the start latched and step 0c recorded in the extension: the value
 * and the policy at hcdReadValues, the versions once XhciInitController has
 * read them, whatever it then decided. The start's writes, before the
 * thread exists and under the door gate. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdVersionCounters(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    if (ext->HciVersionRead) {
        hc->Counters.HciVersions =
            XhciVersionPairWord(ext->HciVersionRegister,
                                ext->HciVersionEffective);
    }
    hc->Counters.HciAccept096Value = ext->HciAccept096Value;
    hc->Counters.HciVersionPolicy =
        XhciVersionPolicyWord(ext->HciFloor, ext->HciTdSizeForm,
                              ext->HciOverrideApplied, ext->HciVersionRead);
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdReadValues(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG imod;
    ULONG fast;
    ULONG value;
    ULONG tolFound;
    ULONG tolValue;
    ULONG capFound;
    ULONG capValue;
    ULONG avgFound;
    ULONG avgValue;
    ULONG found;
    ULONG bytes;
    ULONG why;
    KIRQL oldIrql;
    NTSTATUS status;

    ext = &hc->Hc;

    hcdReadLogSwitches(hc);

    imod = 0;
    status = hcdReadDword(hc, HCD_VALUE_IMOD, &imod);
    ext->ImodStatus = NT_SUCCESS(status) ? MP_STATUS_SUCCESS
                                         : MP_STATUS_FAILURE;
    ext->ImodRequested = NT_SUCCESS(status) ? imod : 0;
    XhciLogNote(ext, "imod.status", ext->ImodStatus);
    XhciLogNote(ext, "imod.requested", ext->ImodRequested);

    /* Off unless a value of 1 to 4 is there (XhciPipeFastMode). */
    fast = 0;
    if (!NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_FAST_POLL, &fast))) {
        fast = 0;
    }
    hc->FastPollMode = XhciPipeFastMode(fast);
    XhciLogNote(ext, "fastpoll.value", fast);
    XhciLogNote(ext, "fastpoll.mode", hc->FastPollMode);

    /* Outside Hc, read at every start: a change takes effect at the next. */
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_SETTLE_TOTAL, &value);
    hc->SettleCapMs = XhciEnumSettleCap(NT_SUCCESS(status), value);
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_SETTLE_PORT, &value);
    hc->SettlePortMs = XhciEnumSettlePortCap(NT_SUCCESS(status), value,
                                             hc->SettleCapMs);
    XhciLogNote(ext, "settle.total.ms", hc->SettleCapMs);
    XhciLogNote(ext, "settle.port.ms", hc->SettlePortMs);

    /* Latched here, before admission reopens, for the whole started
     * lifetime (record 17 section 4.11); the cap's controller test waits
     * for XhciInitController's PCI id (hcdTolApplies). */
    tolValue = 0;
    tolFound = NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_TOLERANCE, &tolValue));
    capValue = 0;
    capFound = NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_INTERVAL_CAP,
                                       &capValue));
    avgValue = 0;
    avgFound = NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_AVG_TRB_ESIT,
                                       &avgValue));
    XhciTolStart(&ext->Tol, 0, tolFound, tolValue, capFound, capValue,
                 avgFound, avgValue);
    XhciLogNote(ext, "tol.value", tolFound ? tolValue : 0xFFFFFFFFUL);
    XhciLogNote(ext, "tol.mode", ext->Tol.Stats.Tolerance);
    XhciLogNote(ext, "tol.cap.value", capFound ? capValue : 0xFFFFFFFFUL);
    XhciLogNote(ext, "tol.cap.mode", ext->Tol.Stats.CapMode);
    XhciLogNote(ext, "tol.avgtrb.value", avgFound ? avgValue : 0xFFFFFFFFUL);
    XhciLogNote(ext, "tol.avgtrb.mode", ext->Tol.Stats.AvgTrbMode);

    /* 36.33: selective suspend's switch (design record 14 section 7),
     * read here, before XhciInitController, so stage B's DNCTRL follows
     * it too. Only a DWORD 1 turns stages A and B on; absent, 0, another
     * value or another type is off - today's behaviour bit for bit. The
     * INFs write the 0 (task 36.51). */
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_SELECTIVE_SUSPEND, &value);
    hc->Counters.SuspSwitchRead =
        NT_SUCCESS(status) ? 1UL
        : (status == STATUS_OBJECT_TYPE_MISMATCH) ? 2UL
                                                 : 0UL;
    hc->SuspOn = (NT_SUCCESS(status) && value == 1) ? 1UL : 0UL;
    hc->Counters.SuspSwitch = hc->SuspOn;
    XhciLogNote(ext, "susp.switch.value",
                NT_SUCCESS(status) ? value : 0xFFFFFFFFUL);
    XhciLogNote(ext, "susp.switch", hc->SuspOn);

    /* 36.22: absent, another type, out of range or off a step is the
     * default, each noted apart (the type refusal is hcdTakeDword's). */
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_MAX_TRANSFER, &value);
    found = NT_SUCCESS(status) ? XHCI_XPOL_FOUND_DWORD
            : (status == STATUS_OBJECT_TYPE_MISMATCH) ? XHCI_XPOL_FOUND_OTHER
                                                     : XHCI_XPOL_FOUND_NONE;
    bytes = XhciXpolValue(found, value, &why);
    XhciControllerLockAcquire(ext, &oldIrql);
    hc->Xstat.ValueWhy = why;
    hc->Xstat.ValueRaw = NT_SUCCESS(status) ? value : 0;
    hc->Xstat.CommandBytes = bytes;
    XhciControllerLockRelease(ext, oldIrql);
    XhciLogNote(ext, "xfer.max.value",
                NT_SUCCESS(status) ? value : 0xFFFFFFFFUL);
    XhciLogNote(ext, "xfer.max.why", why);
    XhciLogNote(ext, "xfer.max.bytes", bytes);

    /* 36.24. */
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_WIDE_MAP, &value);
    hc->WideSwitch = NT_SUCCESS(status) ? (value != 0 ? 1UL : 0UL)
                                        : HCD_WIDE_MAP_DEFAULT;
    XhciControllerLockAcquire(ext, &oldIrql);
    hc->Xpipe.Switch = NT_SUCCESS(status) ? value : 0xFFFFFFFFUL;
    XhciControllerLockRelease(ext, oldIrql);
    XhciLogNote(ext, "xfer.wide.value",
                NT_SUCCESS(status) ? value : 0xFFFFFFFFUL);
    XhciLogNote(ext, "xfer.wide.switch", hc->WideSwitch);

    /* Issue 18. */
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_HUB_SPEED, &value);
    hc->Counters.Usb2HubSpeedSwitchRead =
        NT_SUCCESS(status) ? 1UL
        : (status == STATUS_OBJECT_TYPE_MISMATCH) ? 2UL
                                                 : 0UL;
    hc->HubSpeedFallback = (NT_SUCCESS(status) && value == 0) ? 0UL : 1UL;
    hc->Counters.Usb2HubSpeedFallbackOn = hc->HubSpeedFallback;
    hc->HubSpeedNotes = 0;
    XhciLogNote(ext, "hub.speed.value",
                NT_SUCCESS(status) ? value : 0xFFFFFFFFUL);
    XhciLogNote(ext, "hub.speed.fallback", hc->HubSpeedFallback);

    /* Task 38.11: latched in the extension, which this start has zeroed
     * and which no in-place reinitialization zeroes again, so every version
     * check of the started lifetime uses this floor. */
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_ACCEPT_096, &value);
    ext->HciAccept096Value = NT_SUCCESS(status) ? value : 0xFFFFFFFFUL;
    ext->HciFloor = XhciVersionFloor(NT_SUCCESS(status) ? 1UL : 0UL, value);
    XhciLogNote(ext, "hc.accept096", ext->HciAccept096Value);
    XhciLogNote(ext, "hc.floor", ext->HciFloor);
#if defined(XHCI_FLAVOUR_QEMU)
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_QEMU_HCIVERSION, &value);
    ext->HciQemuVersion =
        (NT_SUCCESS(status) && value != 0 && value < 0xFFFFUL) ? value : 0;
    XhciLogNote(ext, "qemu.hciversion.value", ext->HciQemuVersion);
#endif
    hcdVersionCounters(hc);

#if defined(XHCI_FLAVOUR_QEMU)
    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_QEMU_MAP_GRANT, &value);
    hc->QemuMapGrant = NT_SUCCESS(status) ? value : 0;
    XhciLogNote(ext, "qemu.mapgrant", hc->QemuMapGrant);

    value = 0;
    status = hcdReadDword(hc, L"XhciQemuBigRefuse", &value);
    hc->QemuBigRefuse = (NT_SUCCESS(status) && value == 1) ? 1UL : 0UL;
    XhciLogNote(ext, "qemu.bigrefuse", hc->QemuBigRefuse);

    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_QEMU_PSI_E460, &value);
    hc->QemuPsiE460 = (NT_SUCCESS(status) && value == 1) ? 1UL : 0UL;
    XhciLogNote(ext, "qemu.psi.e460", hc->QemuPsiE460);
#endif
}

/* xhci_silent.h keeps REG_BINARY's number for the pure core; this is where
 * the two must agree. */
typedef char hcdSilentRegBinaryAgrees[(REG_BINARY == XHCI_SILENT_REG_BINARY)
                                          ? 1
                                          : -1];

/*
 * The query routine for hcdReadSilent: called once for every value of
 * USB\SilentClasses (the table's Name is NULL), each handed to the pure
 * admission, which keeps, ignores or drops it and counts it. Always
 * STATUS_SUCCESS, so one bad value ends nothing (design record 13 section
 * 6.11.3). IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS NTAPI hcdTakeSilent(PWSTR ValueName, ULONG ValueType,
                                    PVOID ValueData, ULONG ValueLength,
                                    PVOID Context, PVOID EntryContext)
{
    PHCD_CONTROLLER hc;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(EntryContext);

    hc = (PHCD_CONTROLLER)Context;
    (VOID)XhciSilentAdd(&hc->Silent, ValueType, (const UCHAR *)ValueData,
                        ValueLength);
    return STATUS_SUCCESS;
}

/*
 * The silent install's gate and list (task 38.2; design record 13 section
 * 6.11.3), once per start, after hcdReadValues and before the controller
 * thread exists, so every PDO of this start is decided against them
 * (hcd_pdo.c, hcdSilentDecide). On every target the table is emptied and
 * the gate cleared first; on Windows 98 SE and ME alone the gate is read,
 * and only with it on the list. The list's query names no value: NTKERN's
 * enumeration of every value for a NULL Name is O1, unread, and the four
 * counts noted here are how the first 98 SE leg reads it - kept, ignored
 * and dropped are every call the routine received. A query that does not
 * succeed leaves the table empty: a list read in part is not the list.
 * No lock: the previous start's thread has exited and this start's does
 * not exist yet. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdReadSilent(PHCD_CONTROLLER hc)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    PXHCI_EXTENSION ext;
    NTSTATUS status;
    ULONG value;
    PUCHAR p;
    ULONG i;

    ext = &hc->Hc;
    XhciSilentReset(&hc->Silent);
    hc->SilentGate = 0;
    hc->SilentStatus = STATUS_SUCCESS;
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        return;
    }

    value = 0;
    status = hcdReadDword(hc, HCD_VALUE_SILENT, &value);
    hc->SilentGate = XhciSilentGate(NT_SUCCESS(status) ? 1UL : 0UL, value);
    XhciLogNote(ext, "silent.gate.value",
                NT_SUCCESS(status) ? value : 0xFFFFFFFFUL);
    XhciLogNote(ext, "silent.gate", hc->SilentGate);
    if (!hc->SilentGate) {
        return;
    }

    p = (PUCHAR)table;
    for (i = 0; i < sizeof(table); i++) {
        p[i] = 0;
    }
    table[0].QueryRoutine = hcdTakeSilent;
    table[0].Flags = RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = NULL;
    status = RtlQueryRegistryValues(RTL_REGISTRY_SERVICES, HCD_SILENT_LIST,
                                    table, hc, NULL);
    hc->SilentStatus = status;
    XhciSilentEnd(&hc->Silent, (status == STATUS_SUCCESS) ? 1UL : 0UL);
    XhciLogNote(ext, "silent.status", (ULONG)status);
    XhciLogNote(ext, "silent.entries", hc->Silent.Count);
    XhciLogNote(ext, "silent.ignored", hc->Silent.Ignored);
    XhciLogNote(ext, "silent.dropped", hc->Silent.Dropped);
}

#if defined(XHCI_FLAVOUR_QEMU)
/* Task 35.4's switch as the start latched it, for xhciBuildPortMap.
 * IRQL: any. */
ULONG HcdSvcQemuPsiE460(PXHCI_EXTENSION ext)
{
    return HcdControllerFromExt(ext)->QemuPsiE460;
}

/* hcdReadDword for the injection layer's trigger (hcd_inj.c).
 * IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdCtlQemuReadDword(PHCD_CONTROLLER hc, PCWSTR name, PULONG value)
{
    return hcdReadDword(hc, name, value);
}
#endif

/* The interval cap's controller test, and QEMU's stream-context quirk's,
 * once XhciInitController has read the PCI id. IRQL: PASSIVE_LEVEL. */
static VOID hcdTolApplies(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    ext->Tol.Stats.CapApplied = XhciTolCapApplies(ext->Tol.Stats.CapMode,
                                                  ext->PciVendorDevice);
    XhciLogNote(ext, "tol.cap.applied", ext->Tol.Stats.CapApplied);
    /* 36.54: QEMU's stream-context quirk, on its PCI id alone (record 17
     * section 4.12.4); no setting reaches it. */
    hc->QemuStreamQuirk = XhciTolQemuStreamQuirk(ext->PciVendorDevice);
    hc->Counters.QemuStreamQuirk = hc->QemuStreamQuirk;
}

/* Issue 18: a controller's own firmware version where its configuration
 * space has one this driver knows of (XhciPciFirmwareOffset) - the VIA
 * VL805's, which decides a downstream TT quirk other drivers carry - read
 * once and noted, so a start's dump answers it. A read, never a write.
 * IRQL: PASSIVE_LEVEL. */
static VOID hcdFirmwareNote(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG offset;
    ULONG value;

    ext = &hc->Hc;
    offset = XhciPciFirmwareOffset(ext->PciVendorDevice);
    if (offset == 0) {
        return;
    }
    value = 0xFFFFFFFFUL;
    if (XhciReadPciConfig(ext, offset, &value, sizeof(value)) !=
        MP_STATUS_SUCCESS) {
        value = 0xFFFFFFFFUL;
    }
    XhciLogNote(ext, "pci.vl805.fw", value);
}
/*
 * The force-Bulk-Only value (roadmap-hcd.md 31-A.3): 1 when XhciForceBulkOnly
 * is a nonzero REG_DWORD in the controller's driver key, the key every other
 * value of this driver lives in; 0 when it is zero. When it cannot be read -
 * absent, of another type, or no driver key at all - it is 1 (roadmap-hcd
 * 37.2; issue 15). Every INF install path writes 0, so on an installed
 * system the value is unread only when it was deleted or mistyped by hand,
 * or a binary was copied over an install older than 34.1 - the conservative
 * transport is the right answer there too - and in text-mode Setup, before
 * any INF has run, it is always unread: there only usbstor's Bulk-Only ids
 * have a driver and xhciuas.sys is not loaded, so a PDO given UAS's ids is no
 * disk, and a boot stick behind it is STOP 0x7B. One value for every device
 * on this controller, read per enumeration so a change takes effect at the
 * next one (a devnode already installed on NT keeps its service until it is
 * uninstalled and re-plugged). IRQL: PASSIVE_LEVEL.
 */
ULONG HcdCtlForceBulkOnly(PHCD_CONTROLLER hc)
{
    ULONG value;

    value = 0;
    if (!NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_FORCE_BOT, &value))) {
        return 1;
    }
    return value != 0;
}

/* --------------------------------------------------------------------- */
/* The Intel port switchover (task 34.3)                                  */
/* --------------------------------------------------------------------- */

/*
 * The second writer of configuration space beside the quiesce path's Bus
 * Master Enable (xhci_hw.h): four Intel-specific registers above the PCI
 * header, on the controllers xhci_psw.h gates, through the same IRP
 * the reads take. IRQL: PASSIVE_LEVEL, as HcdSvcConfigSpace.
 */
static ULONG hcdPswRead(PVOID context, ULONG offset, PULONG value)
{
    return HcdSvcConfigSpace(&((PHCD_CONTROLLER)context)->Hc, TRUE, value,
                             offset, sizeof(ULONG)) == MP_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL, as HcdSvcConfigSpace. */
static ULONG hcdPswWrite(PVOID context, ULONG offset, ULONG value)
{
    ULONG data;

    data = value;
    return HcdSvcConfigSpace(&((PHCD_CONTROLLER)context)->Hc, FALSE, &data,
                             offset, sizeof(ULONG)) == MP_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL, under no spin lock, as hcdReadDword. */
static ULONG hcdPswValue(PVOID context, PULONG value)
{
    return NT_SUCCESS(hcdReadDword((PHCD_CONTROLLER)context,
                                   HCD_VALUE_PORT_SWITCH, value));
}

/* IRQL: any; the callbacks it installs are PASSIVE_LEVEL only. */
static VOID hcdPswIo(PHCD_CONTROLLER hc, PXHCI_PSW_IO io)
{
    io->Read = hcdPswRead;
    io->Write = hcdPswWrite;
    io->Value = hcdPswValue;
    io->Context = hc;
}

/* Only a route that ran is counted: the decision's own reads on a
 * controller that is off are not a route (record 16 section 7a).
 * IRQL: PASSIVE_LEVEL. */
static VOID hcdPswRouted(PHCD_CONTROLLER hc, const XHCI_PSW_STATE *st)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    hc->PswRoutes++;
    if (st->Step != XHCI_PSW_DONE) {
        hc->PswFailures++;
    }
    XhciLogNote(ext, "psw.usb3prm", st->Usb3Mask);
    XhciLogNote(ext, "psw.usb3pssen", st->Usb3Now);
    XhciLogNote(ext, "psw.xusb2prm", st->Usb2Mask);
    XhciLogNote(ext, "psw.xusb2pr", st->Usb2Now);
    XhciLogNote(ext, "psw.route.step", st->Step);
    XhciLogNote(ext, "psw.route.written", st->Written);
    XHCI_DBG_VALUE("hcd: port switchover, USB3_PSSEN now", st->Usb3Now);
    XHCI_DBG_VALUE("hcd: port switchover, XUSB2PR now", st->Usb2Now);
}

/* Only a release with a nonempty set is counted: one with nothing to hand
 * back made no access (record 16 section 6). IRQL: PASSIVE_LEVEL. */
static VOID hcdPswReleased(PHCD_CONTROLLER hc, const XHCI_PSW_STATE *st)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    hc->PswReleases++;
    if (st->Step != XHCI_PSW_DONE) {
        hc->PswFailures++;
    }
    XhciLogNote(ext, "psw.release.usb3pssen", st->Usb3Now);
    XhciLogNote(ext, "psw.release.xusb2pr", st->Usb2Now);
    XhciLogNote(ext, "psw.release.step", st->Step);
    XHCI_DBG_VALUE("hcd: port switchover released, step", st->Step);
}

/*
 * Every switchable connector to xHCI again, when this start decided to
 * route (PswLife.On): at each return to D0 before the resume, since
 * firmware reprograms the routing across standby. What it writes joins
 * the lifetime's set, and a failed route takes nothing out of it.
 * IRQL: PASSIVE_LEVEL.
 */
VOID HcdPswRoute(PHCD_CONTROLLER hc)
{
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    hcdPswIo(hc, &io);
    if (XhciPswLifeResume(&io, &hc->PswLife, &st)) {
        hcdPswRouted(hc, &st);
    }
}

/*
 * The registers this lifetime's routes wrote, and no other, back to
 * EHCI, as Linux's usb_disable_xhci_ports: at the D3 of a system shutdown,
 * where Linux does it against a Panther Point, Lynx Point-LP or Wildcat
 * Point-LP machine powering itself back on. The set is kept, so a stop
 * that follows releases it again. IRQL: PASSIVE_LEVEL.
 */
VOID HcdPswRelease(PHCD_CONTROLLER hc)
{
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    if (!hc->PswLife.On) {
        return;
    }
    XhciLogNote(&hc->Hc, "psw.release.written", hc->PswLife.Written);
    hcdPswIo(hc, &io);
    if (XhciPswLifeRelease(&io, &hc->PswLife, &st)) {
        hcdPswReleased(hc, &st);
    }
}

/*
 * The same release at the stop once the controller has halted, and at a
 * refused start, then the lifetime ends: so a stopped or removed driver
 * never leaves the connectors on an xHCI nothing drives, and the next
 * start inherits nothing. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdPswEnd(PHCD_CONTROLLER hc)
{
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;

    if (hc->PswLife.On) {
        XhciLogNote(&hc->Hc, "psw.release.written", hc->PswLife.Written);
    }
    hcdPswIo(hc, &io);
    if (XhciPswLifeEnd(&io, &hc->PswLife, &st)) {
        hcdPswReleased(hc, &st);
    }
}

/*
 * The start's decision, then its route, before XhciInitController as
 * Linux routes before the controller's reset. The decision reads PCI
 * offset 0 itself because XhciInitController, which records it, runs
 * after; on another vendor's controller (QEMU's 1B36:000D among them) no
 * register past the header is read or written and the value is not read,
 * and psw.gate 0 says so. psw.mode is the value in effect: 2 on an
 * unlisted Intel part is the bypass, at the user's own risk (record 16
 * section 4a). IRQL: PASSIVE_LEVEL.
 */
static VOID hcdPswStart(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_PSW_LIFE life;
    XHCI_PSW_IO io;
    XHCI_PSW_STATE st;
    ULONG routed;

    ext = &hc->Hc;
    life = &hc->PswLife;
    hcdPswIo(hc, &io);
    routed = XhciPswLifeStart(&io, life, &st);

    if (life->Decision == XHCI_PSW_DECIDE_ID_UNREAD) {
        XhciLogNote(ext, "psw.pci.unread", 1);
        return;
    }
    XhciLogNote(ext, "psw.gate", XhciPswGate(life->Id));
    if (life->Decision == XHCI_PSW_DECIDE_NOT_INTEL) {
        XHCI_DBG_TEXT("hcd: port switchover: not an Intel xHCI, no config "
                      "write");
        return;
    }
    XhciLogNote(ext, "psw.value.found", life->Found);
    XhciLogNote(ext, "psw.value", life->Value);
    XhciLogNote(ext, "psw.mode", life->Mode);
    if (life->Decision == XHCI_PSW_DECIDE_OFF) {
        XHCI_DBG_TEXT("hcd: port switchover: off for this controller, "
                      "routing left as firmware set it");
        return;
    }
    if (life->Decision == XHCI_PSW_DECIDE_BOARD_UNREAD) {
        XhciLogNote(ext, "psw.board.unread", 1);
        return;
    }
    if (life->Decision == XHCI_PSW_DECIDE_BOARD_REFUSED) {
        XhciLogNote(ext, "psw.board.refused", life->Subsystem);
        return;
    }
    if (life->Mode == XHCI_PSW_MODE_BYPASS) {
        XHCI_DBG_TEXT("hcd: port switchover: XhciIntelPortSwitch 2, the "
                      "device-id gate bypassed");
    }
    if (routed) {
        hcdPswRouted(hc, &st);
    }
}

/* --------------------------------------------------------------------- */
/* The Intel PCH interrupt routing quirk (task 36.3)                      */
/* --------------------------------------------------------------------- */

/* The Windows 2000 DDK predates message-signalled interrupts; the bit is
 * WDK 7.1 wdm.h's. */
#ifndef CM_RESOURCE_INTERRUPT_MESSAGE
#define CM_RESOURCE_INTERRUPT_MESSAGE 2
#endif

/* WDK 7.1's wdm.h defines NO_LEGACY_DRIVERS under _WIN64, which hides
 * ntddk.h's HalGetBusData prototype; XP x64's hal.dll still exports it
 * and WDK 7.1's amd64 hal.lib lists it, so the declaration is restated
 * here as ntddk.h has it. */
#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

/*
 * PCI configuration space of a function on bus 0 that is not this one's:
 * the PCH's 00:14.0 and 00:1F.0. HcdSvcConfigSpace reaches only this
 * device's own PDO, and the LPC bridge's PDO is not this driver's to
 * address, so the read goes through the HAL, the one route every target
 * offers: HalGetBusData is in every Windows 2000 SP4 HAL's export table,
 * in Vista's and 7's x86 HAL, in XP x64's, in WDK 7.1's Vista and 7
 * amd64 hal.lib, and in Windows 98 SE ntkern.vxd's hal.dll name block
 * beside KfAcquireSpinLock. The ...ByOffset form has no Windows 98
 * evidence, so the header is read from offset 0. Reads only: nothing is
 * written through this route. IRQL: PASSIVE_LEVEL.
 */
static ULONG hcdPirqSlot(PVOID context, ULONG device, ULONG function,
                         PUCHAR buffer, ULONG length)
{
    UNREFERENCED_PARAMETER(context);

    /* PCI_SLOT_NUMBER: device 4:0, function 7:5. */
    return HalGetBusData(PCIConfiguration, 0,
                         (device & 0x1FUL) | ((function & 0x7UL) << 5),
                         buffer, length);
}

/* One noncached page of the chipset's RCBA space, for the run only.
 * IRQL: PASSIVE_LEVEL. */
static ULONG hcdPirqMap(PVOID context, ULONG physical)
{
    PHCD_CONTROLLER hc;
    PHYSICAL_ADDRESS pa;

    hc = (PHCD_CONTROLLER)context;
    pa.LowPart = physical;
    pa.HighPart = 0;
    hc->PirqVa = MmMapIoSpace(pa, XHCI_PIRQ_WINDOW_BYTES, MmNonCached);
    return hc->PirqVa != NULL ? 1UL : 0UL;
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdPirqUnmap(PVOID context)
{
    PHCD_CONTROLLER hc;

    hc = (PHCD_CONTROLLER)context;
    if (hc->PirqVa != NULL) {
        MmUnmapIoSpace(hc->PirqVa, XHCI_PIRQ_WINDOW_BYTES);
        hc->PirqVa = NULL;
    }
}

/* IRQL: PASSIVE_LEVEL. */
static ULONG hcdPirqRead32(PVOID context, ULONG offset, PULONG value)
{
    PHCD_CONTROLLER hc;

    hc = (PHCD_CONTROLLER)context;
    if (hc->PirqVa == NULL || offset > XHCI_PIRQ_WINDOW_BYTES - 4) {
        return 0;
    }
    *value = READ_REGISTER_ULONG((PULONG)((PUCHAR)hc->PirqVa + offset));
    return 1;
}

/* D20IR is a 16-bit register, accessed at its own width. IRQL:
 * PASSIVE_LEVEL. */
static ULONG hcdPirqRead16(PVOID context, ULONG offset, PUSHORT value)
{
    PHCD_CONTROLLER hc;

    hc = (PHCD_CONTROLLER)context;
    if (hc->PirqVa == NULL || offset > XHCI_PIRQ_WINDOW_BYTES - 2) {
        return 0;
    }
    *value = READ_REGISTER_USHORT((PUSHORT)((PUCHAR)hc->PirqVa + offset));
    return 1;
}

/* IRQL: PASSIVE_LEVEL. */
static ULONG hcdPirqWrite16(PVOID context, ULONG offset, USHORT value)
{
    PHCD_CONTROLLER hc;

    hc = (PHCD_CONTROLLER)context;
    if (hc->PirqVa == NULL || offset > XHCI_PIRQ_WINDOW_BYTES - 2) {
        return 0;
    }
    WRITE_REGISTER_USHORT((PUSHORT)((PUCHAR)hc->PirqVa + offset), value);
    return 1;
}

/* IRQL: any; the callbacks it installs are PASSIVE_LEVEL only. The own
 * reads are the port switchover's. */
static VOID hcdPirqIo(PHCD_CONTROLLER hc, PXHCI_PIRQ_IO io)
{
    io->Own = hcdPswRead;
    io->Slot = hcdPirqSlot;
    io->Map = hcdPirqMap;
    io->Read32 = hcdPirqRead32;
    io->Read16 = hcdPirqRead16;
    io->Write16 = hcdPirqWrite16;
    io->Unmap = hcdPirqUnmap;
    io->Context = hc;
    /* WDM 1.10 is Windows 2000's: below it, 98 and ME, an IRQ below 16 is
     * the 8259 path (XHCI_PIRQ_IO.Steerable). */
    io->Steerable = IoIsWdmVersionAvailable(1, 0x10) ? 0UL : 1UL;
}

/*
 * One run and its record: the whole state is in the snapshot's HCD region
 * at every verbosity (hcd_door.c), and the note ring carries the decision
 * and what it turned on. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdPirqRun(PHCD_CONTROLLER hc, ULONG when)
{
    PXHCI_EXTENSION ext;
    PXHCI_PIRQ_STATE st;
    XHCI_PIRQ_IO io;

    ext = &hc->Hc;
    st = &hc->Pirq;
    hcdPirqIo(hc, &io);
    (VOID)XhciPirqRun(&io, when, hc->IntDelivery, &hc->IntRaw, st);
    hcdPirqUnmap(hc);

    XhciLogNote(ext, "pirq.decision", st->Decision);
    XhciLogNote(ext, "pirq.when", st->When);
    XhciLogNote(ext, "pirq.delivery", st->Delivery);
    XhciLogNote(ext, "pirq.irq", st->IrqLevel);
    XHCI_DBG_VALUE("hcd: interrupt routing, decision", st->Decision);
    if (st->Decision == XHCI_PIRQ_D_NOT_REVIEWED ||
        st->Decision == XHCI_PIRQ_D_OWN_UNREAD) {
        return;
    }
    XhciLogNote(ext, "pirq.slot.id", st->SlotId);
    XhciLogNote(ext, "pirq.lpc.id", st->LpcId);
    /* A physical address: level 4 alone (design record 25 revision 12). */
    XhciLogNoteAddress(ext, "pirq.rcba", st->Rcba);
    XhciLogNote(ext, "pirq.d20ip", st->D20ip);
    XhciLogNote(ext, "pirq.d20ir", st->D20ir);
    XhciLogNote(ext, "pirq.target", st->Target);
    XhciLogNote(ext, "pirq.d20ir.after", st->D20irAfter);
    XHCI_DBG_VALUE("hcd: interrupt routing, D20IR found", st->D20ir);
    XHCI_DBG_VALUE("hcd: interrupt routing, D20IR now", st->D20irAfter);
}

#if defined(XHCI_FLAVOUR_QEMU)
/* IRQL: any (a pure read of the caller's bytes). */
static ULONG hcdPirqLe32(const UCHAR *b, ULONG offset)
{
    return (ULONG)b[offset] | ((ULONG)b[offset + 1] << 8) |
           ((ULONG)b[offset + 2] << 16) | ((ULONG)b[offset + 3] << 24);
}

/*
 * No guest offers a reviewed PCH, so every guest's run stops at
 * NOT_REVIEWED before any bus-data read. This takes the run's own
 * HalGetBusData route over bus 0, finding this function by the dwords the
 * run identifies 00:14.0 by, and reads 00:1F.0 (absent on QEMU's pc
 * machine), so a guest shows that route answering on its OS, with the raw
 * interrupt resource the run would name the IRQ from. Reads only. IRQL:
 * PASSIVE_LEVEL.
 */
static VOID hcdPirqQemuProbe(PHCD_CONTROLLER hc)
{
    static const ULONG same[4] = {
        XHCI_PIRQ_CFG_ID, XHCI_PIRQ_CFG_BAR0, XHCI_PIRQ_CFG_BAR1,
        XHCI_PIRQ_CFG_SUBSYSTEM
    };
    UCHAR cfg[XHCI_PIRQ_SLOT_BYTES];
    ULONG own[4];
    ULONG bytes;
    ULONG found;
    ULONG device;
    ULONG i;

    for (i = 0; i < 4; i++) {
        if (!hcdPswRead(hc, same[i], &own[i])) {
            return;
        }
    }
    found = 0xFFFFFFFFUL;
    for (device = 0; device < 32 && found == 0xFFFFFFFFUL; device++) {
        bytes = hcdPirqSlot(hc, device, 0, cfg, XHCI_PIRQ_SLOT_BYTES);
        if (bytes < XHCI_PIRQ_SLOT_BYTES) {
            continue;
        }
        found = device;
        for (i = 0; i < 4; i++) {
            if (hcdPirqLe32(cfg, same[i]) != own[i]) {
                found = 0xFFFFFFFFUL;
            }
        }
    }
    bytes = hcdPirqSlot(hc, XHCI_PIRQ_LPC_DEVICE, 0, cfg,
                        XHCI_PIRQ_SLOT_BYTES);
    XhciLogNote(&hc->Hc, "pirq.qemu.device", found);
    XhciLogNote(&hc->Hc, "pirq.qemu.lpc.bytes", bytes);
    XHCI_DBG_VALUE("hcd: interrupt routing probe, this function at device",
                   found);
    XHCI_DBG_VALUE("hcd: interrupt routing probe, 00:1F.0 bytes", bytes);
    XHCI_DBG_VALUE("hcd: interrupt routing probe, raw count",
                   hc->IntRaw.Count);
    XHCI_DBG_VALUE("hcd: interrupt routing probe, raw level",
                   hc->IntRaw.Level);
    XHCI_DBG_VALUE("hcd: interrupt routing probe, raw vector",
                   hc->IntRaw.Vector);
}
#endif

/*
 * The start's value and run: after the interrupt is connected and
 * XhciInitController has run, before XhciEnableInterrupts, so the start's
 * No Op completes on a repaired line. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdPirqStart(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG value;
    ULONG found;

    ext = &hc->Hc;
    value = 0;
    found = NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_IRQ_ROUTE, &value)) ? 1UL
                                                                        : 0UL;
    XhciPirqLatch(&hc->Pirq, found, value);
    XhciLogNote(ext, "pirq.value", found ? value : 0xFFFFFFFFUL);
    XhciLogNote(ext, "pirq.mode", hc->Pirq.Mode);
    if (!found || value > XHCI_PIRQ_MODE_REVIEWED) {
        XhciLogNote(ext, "pirq.value.defaulted", 1);
    }
    hcdPirqRun(hc, XHCI_PIRQ_WHEN_START);
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->Pirq.Decision == XHCI_PIRQ_D_NOT_REVIEWED) {
        hcdPirqQemuProbe(hc);
    }
#endif
}

/*
 * A return to D0, before the controller resumes: firmware may have put
 * its route back across the sleep. Decided again on the start's value and
 * delivery. IRQL: PASSIVE_LEVEL, the power gate held.
 */
VOID HcdPirqResume(PHCD_CONTROLLER hc)
{
    hcdPirqRun(hc, XHCI_PIRQ_WHEN_RESUME);
}

/*
 * Every START_DEVICE, before it goes down the stack: a start the lower
 * driver refuses, or one refused before the routing step, then shows no
 * earlier start's run through a door handle still open. The counts are
 * kept. Under the door gate, which the
 * snapshot's copy holds; released before the IRP is forwarded.
 * IRQL: PASSIVE_LEVEL.
 */
VOID HcdPirqStartAttempt(PHCD_CONTROLLER hc)
{
    HcdDoorGateEnter(hc);
    XhciPirqReset(&hc->Pirq);
    HcdDoorGateLeave(hc);
}

/* --------------------------------------------------------------------- */
/* Resources                                                              */
/* --------------------------------------------------------------------- */

/* The first memory window and the first interrupt of the translated list,
 * the raw memory window's bus address, and the raw interrupt resource with
 * how many the raw list holds (task 36.3). IRQL: PASSIVE_LEVEL. */
static NTSTATUS hcdParseResources(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PCM_RESOURCE_LIST translated;
    PCM_RESOURCE_LIST raw;
    PCM_PARTIAL_RESOURCE_LIST list;
    PCM_PARTIAL_RESOURCE_LIST rawList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    ULONG rawMemory;
    ULONG i;

    hc->HaveMemory = 0;
    hc->HaveInterrupt = 0;
    hc->IntRaw.Count = 0;
    hc->IntRaw.Level = 0;
    hc->IntRaw.Vector = 0;
    hc->IntRaw.Flags = 0;
    hc->IntRaw.Share = 0;
    hc->IntDelivery = XHCI_PIRQ_DELIVERY_NONE;
    hc->MsgDescriptors = 0;
    hc->MsgRawCount = 0;
    rawMemory = 0;

    stack = IoGetCurrentIrpStackLocation(irp);
    translated = stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    raw = stack->Parameters.StartDevice.AllocatedResources;
    if (translated == NULL || raw == NULL || translated->Count == 0 ||
        raw->Count == 0) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    list = &translated->List[0].PartialResourceList;
    rawList = &raw->List[0].PartialResourceList;

    for (i = 0; i < list->Count; i++) {
        d = &list->PartialDescriptors[i];
        if (d->Type == CmResourceTypeMemory && !hc->HaveMemory) {
            hc->BarTranslated = d->u.Memory.Start;
            hc->BarLength = d->u.Memory.Length;
            hc->HaveMemory = 1;
        } else if (d->Type == CmResourceTypeInterrupt && !hc->HaveInterrupt) {
            hc->IntVector = d->u.Interrupt.Vector;
            hc->IntIrql = (KIRQL)d->u.Interrupt.Level;
            hc->IntAffinity = d->u.Interrupt.Affinity;
            hc->IntMode = (d->Flags & CM_RESOURCE_INTERRUPT_LATCHED) != 0
                              ? Latched
                              : LevelSensitive;
            hc->IntShared = (d->ShareDisposition == CmResourceShareShared)
                                ? TRUE
                                : FALSE;
            hc->HaveInterrupt = 1;
        }
    }
    for (i = 0; i < rawList->Count; i++) {
        d = &rawList->PartialDescriptors[i];
        if (d->Type == CmResourceTypeMemory && !rawMemory) {
            hc->BarRaw = d->u.Memory.Start;
            rawMemory = 1;
        } else if (d->Type == CmResourceTypeInterrupt) {
            if (hc->IntRaw.Count == 0) {
                /* Kept as given. Under CM_RESOURCE_INTERRUPT_MESSAGE the
                 * first word is WDK 7.1's u.MessageInterrupt.Raw, a
                 * reserved USHORT then MessageCount, so Level carries the
                 * count in 31:16; the run reports a message grant before
                 * it reads Level, and XHCISNAP decodes it so. */
                hc->IntRaw.Level = d->u.Interrupt.Level;
                hc->IntRaw.Vector = d->u.Interrupt.Vector;
                hc->IntRaw.Flags = d->Flags;
                hc->IntRaw.Share = d->ShareDisposition;
            }
            /* Record 15 section 4.3: the message descriptors, and the
             * MessageCount the first one carries (WDK 7.1's
             * u.MessageInterrupt.Raw: a reserved USHORT, then the count). */
            if ((d->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0) {
                if (hc->MsgDescriptors == 0) {
                    hc->MsgRawCount = (d->u.Interrupt.Level >> 16) & 0xFFFFUL;
                }
                hc->MsgDescriptors++;
            }
            hc->IntRaw.Count++;
        }
    }
    if (hc->IntRaw.Count != 0) {
        hc->IntDelivery = (hc->IntRaw.Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                              ? XHCI_PIRQ_DELIVERY_MESSAGE
                              : XHCI_PIRQ_DELIVERY_LINE;
    }

    /* An interrupt is no longer required here: the delivery's decision
     * says what its absence means (hcdSettleDelivery). */
    if (!hc->HaveMemory) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_SUCCESS;
}

/*
 * The delivery and its reason (design record 15 section 4), settled once
 * per start before anything is connected: the three values, the resource,
 * the message flag and the pin, read here from configuration space with
 * its status kept apart from its value. Returns a failure where the
 * delivery the mode names cannot be had (XhciPollDecide's NONE): mode 1
 * with no line - no interrupt resource, pin 0, or a message granted - and
 * mode 3 with no message to connect (record 15 revision 7). Logged later,
 * once the log's switches are read (hcdLogDelivery), or by hcdLogRefused
 * for a refusal. IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdSettleDelivery(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG modeValue;
    ULONG idleValue;
    ULONG activeValue;
    ULONG modeFound;
    ULONG idleFound;
    ULONG activeFound;
    ULONG haveInterrupt;
    ULONG message;
    UCHAR pin;
    ULONG delivery;
    ULONG reason;
    ULONG noLine;
    ULONG value2;
    ULONG msiAvoid;
    NTSTATUS status;
#if defined(XHCI_FLAVOUR_QEMU)
    ULONG value;
#endif

    ext = &hc->Hc;
    modeValue = 0;
    idleValue = 0;
    activeValue = 0;
    modeFound = NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_INTR_MODE, &modeValue))
                    ? 1UL
                    : 0UL;
    idleFound = NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_POLL_IDLE, &idleValue))
                    ? 1UL
                    : 0UL;
    activeFound = NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_POLL_ACTIVE,
                                          &activeValue))
                      ? 1UL
                      : 0UL;
    XhciPollValues(&hc->PollValues, modeFound, modeValue, idleFound,
                   idleValue, activeFound, activeValue);
    /* Record 15 section 7.2.4: the finer timer's switch and the value it
     * asks, the active period in effect in 100 ns. Off unless a non-zero
     * value is read (opt-in, the owner's change of 2026-10-14). */
    value2 = 0;
    if (NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_POLL_FINE_TIMER, &value2))) {
        hc->Counters.TmrSwitchRead = value2;
        hc->Tmr.Switch = (value2 == 0) ? 0UL : 1UL;
    } else {
        hc->Counters.TmrSwitchRead = XHCI_POLL_NOT_FOUND;
        hc->Tmr.Switch = 0;
    }
    hc->Counters.TmrSwitch = hc->Tmr.Switch;
    hc->Tmr.Desired = hc->PollValues.ActiveMs * XHCI_POLL_TICKS_PER_MS;
    hc->Counters.TmrDesired = hc->Tmr.Desired;
    hc->Tmr.DelayMs = 0;
#if defined(XHCI_FLAVOUR_QEMU)
    value2 = 0;
    if (NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_QEMU_TMR_DELAY, &value2))) {
        hc->Tmr.DelayMs = (value2 > 10000UL) ? 10000UL : value2;
    }
    value2 = 0;
    hc->QemuSuspHoldDx =
        (NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_QEMU_SUSP_HOLD, &value2)) &&
         value2 == 1) ? 1UL : 0UL;
#endif
    /* Record 18 section 4.2: the start's reading kept for a later filter
     * on this FDO; and on x86 a look for the helper while not bound
     * (section 4.1). Both NT 6 only. */
    HcdMsiStartValues(hc, modeFound, modeValue);
    HcdMsiStartLook(hc);

    haveInterrupt = hc->HaveInterrupt;
    message = (hc->IntDelivery == XHCI_PIRQ_DELIVERY_MESSAGE) ? 1UL : 0UL;
    hc->MsgRoutines = HCD_MSI_ROUTINES_NONE;
    hc->MsgConnectEx = NULL;
    hc->MsgDisconnectEx = NULL;
    hc->MsgConnected = 0;
    hc->MsgCapability = 0;
    hc->MsgConnectStatus = STATUS_SUCCESS;
    hc->QemuMsgFail = 0;
#if defined(XHCI_FLAVOUR_QEMU)
    value = 0;
    hc->QemuNoInterrupt =
        (NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_QEMU_NO_INTR, &value)) &&
         value == 1) ? 1UL : 0UL;
    value = 0;
    hc->QemuPinUnreadable =
        (NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_QEMU_PIN_FAIL, &value)) &&
         value == 1) ? 1UL : 0UL;
    value = 0;
    hc->QemuDeadLine =
        NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_QEMU_DEADLINE, &value)) ? value
                                                                      : 0UL;
    /* Held to 100 s: the latch's deadline is compared by signed
     * difference of the system time's low word (record 15 section 6.2). */
    if (hc->QemuDeadLine > 100001UL) {
        hc->QemuDeadLine = 100001UL;
    }
    hc->QemuDeadLatched = (hc->QemuDeadLine == 1) ? 1UL : 0UL;
    hc->QemuDeadAt = 0;
    hc->QemuDeadClaims = 0;
    if (hc->QemuNoInterrupt) {
        haveInterrupt = 0;
        message = 0;
        hc->HaveInterrupt = 0;
    }
    value = 0;
    hc->QemuMsgFail =
        (NT_SUCCESS(hcdReadDword(hc, HCD_VALUE_QEMU_MSG_FAIL, &value)) &&
         value == 1) ? 1UL : 0UL;
#endif
    /* Record 15 section 4.3: the IoConnectInterruptEx pair for a message
     * grant - amd64's from MmGetSystemRoutineAddress, x86's from the
     * helper - and, for the routines' record alone, whether no line was
     * offered to an unbound x86 (record 18 section 4.3). */
    noLine = 0;
    if (message) {
        hc->MsgRoutines = HcdMsiRoutines(hc, &hc->MsgConnectEx,
                                         &hc->MsgDisconnectEx);
        noLine = HcdMsiNoLineOffered(hc);
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_ROUTINES, hc->MsgRoutines, noLine);
    }

    pin = 0;
    hc->PollPin = 0xFFFFFFFFUL;
    hc->PollPinStatus = XHCI_POLL_PIN_UNREADABLE;
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuNoInterrupt) {
        hc->PollPin = 0;
        hc->PollPinStatus = XHCI_POLL_PIN_READ;
    } else if (!hc->QemuPinUnreadable &&
               XhciReadPciConfig(ext, XHCI_PCI_INTERRUPT_PIN, &pin,
                                 sizeof(UCHAR)) == MP_STATUS_SUCCESS) {
#else
    if (XhciReadPciConfig(ext, XHCI_PCI_INTERRUPT_PIN, &pin, sizeof(UCHAR)) ==
        MP_STATUS_SUCCESS) {
#endif
        hc->PollPin = (ULONG)pin;
        hc->PollPinStatus = XHCI_POLL_PIN_READ;
    }

    reason = XHCI_POLL_R_NONE;
    /* Task 38.10, XhciMsiAvoid (design record 20 section 4.1): its value
     * and this controller's identity, read before anything is mapped. */
    msiAvoid = HcdQuirkMsiAvoid(hc, hc->PollValues.Mode);
    /* A message whose routines were found is decided as connected; a
     * connection that then fails turns it to a poll, or under mode 3 fails
     * the start (hcdConnect). */
    delivery = XhciPollDecideEx(hc->PollValues.Mode, haveInterrupt, message,
                                (hc->MsgConnectEx != NULL) ? 1UL : 0UL,
                                hc->PollPinStatus, hc->PollPin,
                                HcdMsiSystem(), msiAvoid, &reason);
    if (reason == XHCI_POLL_R_MSI_AVOIDED) {
        HcdQuirkMsiRefused(hc);
    }
    hc->PollDelivery = delivery;
    hc->PollReason = reason;
    hc->Counters.IntrModeRead = hc->PollValues.ModeRead;
    hc->Counters.IntrMode = hc->PollValues.Mode;
    hc->Counters.IntrDelivery = delivery;
    hc->Counters.IntrReason = reason;
    hc->Counters.IntrPinStatus = hc->PollPinStatus;
    hc->Counters.IntrPin = hc->PollPin;
    hc->Counters.PollIdleMsRead = hc->PollValues.IdleRead;
    hc->Counters.PollIdleMs = hc->PollValues.IdleMs;
    hc->Counters.PollActiveMsRead = hc->PollValues.ActiveRead;
    hc->Counters.PollActiveMs = hc->PollValues.ActiveMs;
    hc->Counters.PollIntervalMinMs = XHCI_POLL_NOT_FOUND;
    hc->Counters.IntrValuesFound = hc->PollValues.Found;
    HcdMsiCountersStart(hc);
    /* Mode 1 with no usable line, and mode 3 with no message to connect,
     * fail here, before anything is connected, with the status a forced
     * line with no line always gave: no resource, the resources refused; a
     * resource (on pin 0, a message under mode 1, a line under mode 3, or
     * on a system with no message interrupts), the controller's
     * configuration refused (record 15 section 4). hcdStartBody records the
     * refusal; the start-decision event follows (record 18 section 9). */
    if (delivery == XHCI_POLL_DELIVERY_NONE) {
        status = haveInterrupt ? STATUS_DEVICE_CONFIGURATION_ERROR
                               : STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }
    return STATUS_SUCCESS;
}

/* The start's notes (record 15 section 8), once the log's switches are
 * read. IRQL: PASSIVE_LEVEL. */
static VOID hcdLogDelivery(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    XhciLogNote(ext, "intr.mode.read", hc->PollValues.ModeRead);
    XhciLogNote(ext, "intr.mode", hc->PollValues.Mode);
    XhciLogNote(ext, "intr.delivery", hc->PollDelivery);
    XhciLogNote(ext, "intr.reason", hc->PollReason);
    XhciLogNote(ext, "intr.pin", hc->PollPin);
    XhciLogNote(ext, "poll.idle.ms", hc->PollValues.IdleMs);
    XhciLogNote(ext, "poll.active.ms", hc->PollValues.ActiveMs);
    /* Record 15 section 4.3 and record 18 section 9. */
    XhciLogNote(ext, "msi.descriptors", hc->MsgDescriptors);
    XhciLogNote(ext, "msi.raw.count", hc->MsgRawCount);
    XhciLogNote(ext, "msi.routines", hc->MsgRoutines);
    XhciLogNote(ext, "msi86.state", hc->HlprBind.State);
    XhciLogNote(ext, "msi86.status", (ULONG)hc->HlprStatus);
    XhciLogNote(ext, "msi86.refusal", hc->HlprRefusal);
    XhciLogNote(ext, "msi86.helper.version", hc->HlprHelperVersion);
    XhciLogNote(ext, "hlpr.msi.verdict", hc->HlprMsiVerdict);
    XhciLogNote(ext, "hlpr.timer.verdict", hc->HlprTimerVerdict);
    /* Record 15 section 7.2.9. */
    XhciLogNote(ext, "poll.timer.switch.read", hc->Counters.TmrSwitchRead);
    XhciLogNote(ext, "poll.timer.switch", hc->Tmr.Switch);
    XhciLogNote(ext, "poll.timer.desired", hc->Tmr.Desired);
#if defined(XHCI_FLAVOUR_QEMU)
    XhciLogNote(ext, "qemu.tmr.delay", hc->Tmr.DelayMs);
    XhciLogNote(ext, "qemu.susp.holddx", hc->QemuSuspHoldDx);
#endif
    XhciLogNote(ext, "msi.filter.seq", hc->MsiFilter.Sequence);
    XhciLogNote(ext, "msi.filter.class", hc->MsiFilter.Class);
    XhciLogNote(ext, "msi.filter.outcome", hc->MsiFilter.Outcome);
    XhciLogNote(ext, "msi.filter.mode.raw", hc->FilterModeRaw);
    XhciLogNote(ext, "msi.filter.mode.from", hc->FilterModeFrom);
    XhciLogNote(ext, "msi.filter.lists", hc->FilterWalk.Lists);
    XhciLogNote(ext, "msi.filter.messages",
                hc->FilterClassify.MessageMembers);
    XhciLogNote(ext, "msi.filter.lines", hc->FilterClassify.LineMembers);
#if defined(XHCI_FLAVOUR_QEMU)
    XhciLogNote(ext, "qemu.inj.nointerrupt", hc->QemuNoInterrupt);
    XhciLogNote(ext, "qemu.inj.pinunreadable", hc->QemuPinUnreadable);
    XhciLogNote(ext, "qemu.inj.deadline.value", hc->QemuDeadLine);
    /* N = 0 latches before the connection (record 15 section 11): its
     * latch note here, as hcdPollQemuLatch notes a delayed one. */
    if (hc->QemuDeadLatched) {
        XhciLogNote(ext, "qemu.inj.deadline", 1);
    }
#endif
}

/*
 * A start the delivery refuses (record 15 section 8, revision 7): never
 * stopped, and its FDO removed next, so it records itself - the log's
 * switches and the start's notes where the start had not reached them, the
 * refusal's reason, and the ring flushed with the counter dump through the
 * stop's own flush (HcdLogFlush, no new DbgPrint site). From hcdStartBody,
 * on the PnP manager's start thread. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdLogRefused(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    if (ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        ext->Signature = XHCI_EXTENSION_SIGNATURE;
        ext->TrailingSignature = XHCI_EXTENSION_TRAILING;
        hcdReadLogSwitches(hc);
        hcdLogDelivery(hc);
    }
    XhciLogNote(ext, "start.refused.delivery", hc->PollReason);
    HcdLogFlush(hc, XHCI_LOG_REASON_FAILURE, 1);
}

/*
 * A leftover PCI Interrupt Disable (record 15 section 4.2, set by an
 * earlier start's containment) cleared before a line's interrupts are
 * enabled, after XhciInitController's HCRST, and read back.
 * IRQL: PASSIVE_LEVEL.
 */
static VOID hcdIntxEnable(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    USHORT command;

    ext = &hc->Hc;
    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command, sizeof(USHORT)) !=
            MP_STATUS_SUCCESS ||
        command == 0xFFFFU || (command & XHCI_PCI_COMMAND_INTX_DISABLE) == 0) {
        return;
    }
    command = (USHORT)(command & ~XHCI_PCI_COMMAND_INTX_DISABLE);
    (VOID)XhciWritePciConfig(ext, XHCI_PCI_COMMAND, &command,
                             sizeof(USHORT));
    command = 0xFFFFU;
    (VOID)XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command, sizeof(USHORT));
    if (command != 0xFFFFU && (command & XHCI_PCI_COMMAND_INTX_DISABLE) == 0) {
        hc->Counters.IntxDisableCleared++;
        XhciLogNote(ext, "intr.intxdisable.cleared", 1);
    } else {
        XhciLogNote(ext, "intr.intxdisable.stuck", command);
    }
}

/* --------------------------------------------------------------------- */
/* Interrupt                                                              */
/* --------------------------------------------------------------------- */

/* IRQL: DIRQL. XhciIsr is the miniport's one-shot ISR, unchanged: it claims
 * on USBSTS.EINT, acknowledges and returns, and the DPC drains. */
static BOOLEAN NTAPI hcdIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PHCD_CONTROLLER hc;

    UNREFERENCED_PARAMETER(Interrupt);

    hc = (PHCD_CONTROLLER)Context;
    if (!XhciIsr(&hc->Hc)) {
        return FALSE;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    /* 35-T.9's lost interrupt, and task 36.4's start hold: acknowledged as
     * ever, the drain not queued. Record 15 section 11's dead line: the
     * same, and the claim withheld from the watcher. */
    if (hc->QemuDeadLatched) {
        (VOID)InterlockedIncrement(&hc->QemuDeadClaims);
        return TRUE;
    }
    if (HcdInjIsrDrop(hc) || HcdInjStartHeld(hc)) {
        return TRUE;
    }
#endif
    /* Counted when queued, not when the DPC starts: a DPC another processor
     * has dequeued but not yet entered is then still in the count the
     * teardown waits on (task 26-A.2). */
    (VOID)InterlockedIncrement(&hc->DpcsInFlight);
    if (!KeInsertQueueDpc(&hc->IsrDpc, NULL, NULL)) {
        (VOID)InterlockedDecrement(&hc->DpcsInFlight);
    }
    return TRUE;
}

/* IRQL: DIRQL. The message ISR (record 15 section 4.3). Only Interrupter 0
 * is enabled, so only message 0 is ever written by this controller; any
 * other message ID a grant of several connects is declined before the
 * controller is touched, so it can neither claim another device's shared
 * message nor run the ISR body beside message 0's under another message's
 * lock (each message has its own, SpinLock being NULL; task 36.15).
 * Message 0 runs the line ISR's body and returns
 * XhciIsr's answer - FALSE for an interrupt this controller did not raise,
 * since Windows permits shared message interrupts. */
static BOOLEAN NTAPI hcdMsgIsr(PKINTERRUPT Interrupt, PVOID Context,
                               ULONG MessageId)
{
    if (MessageId != 0) {
        return FALSE;
    }
    return hcdIsr(Interrupt, Context);
}

/*
 * The seam (record 15 section 4.3): one function, handed the
 * IoConnectInterruptEx pair by either architecture's caller - amd64's
 * MmGetSystemRoutineAddress, the x86 helper's answer - connects the message
 * grant and keeps the disconnect for the release. CONNECT_MESSAGE_BASED
 * with the line ISR as FallBackServiceRoutine; SpinLock NULL, so each
 * message's interrupt object has its own lock, and SynchronizeIrql 0, so
 * each runs at its own message's IRQL - which is safe because only message
 * 0 runs the ISR body
 * (hcdMsgIsr) and nothing synchronizes with a message's ISR (the line's
 * KeSynchronizeExecution users run under a line delivery only). On success
 * MsgVersion says what was connected: a message, its table kept; a line
 * Windows fell back to, its interrupt object kept as the line's.
 * IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdMsgConnect(PHCD_CONTROLLER hc, PVOID connect,
                              PVOID disconnect)
{
    HCD_IO_CONNECT_PARAMETERS params;
    PHCD_IO_MESSAGE_INFO table;
    NTSTATUS status;
    PUCHAR p;
    ULONG i;

    if (connect == NULL || disconnect == NULL) {
        return STATUS_PROCEDURE_NOT_FOUND;
    }
    p = (PUCHAR)&params;
    for (i = 0; i < sizeof(params); i++) {
        p[i] = 0;
    }
    hc->MsgContext = NULL;
    params.Version = HCD_CONNECT_MESSAGE_BASED;
    params.u.MessageBased.PhysicalDeviceObject = hc->Pdo;
    params.u.MessageBased.ConnectionContext = &hc->MsgContext;
    params.u.MessageBased.MessageServiceRoutine = hcdMsgIsr;
    params.u.MessageBased.ServiceContext = hc;
    params.u.MessageBased.SpinLock = NULL;
    params.u.MessageBased.SynchronizeIrql = 0;
    params.u.MessageBased.FloatingSave = FALSE;
    params.u.MessageBased.FallBackServiceRoutine = hcdIsr;
    status = ((HCD_CONNECT_EX)connect)(&params);
    if (!NT_SUCCESS(status)) {
        hc->MsgContext = NULL;
        return status;
    }
    hc->MsgVersion = params.Version;
    hc->MsgDisconnectEx = disconnect;
    if (params.Version == HCD_CONNECT_MESSAGE_BASED) {
        table = (PHCD_IO_MESSAGE_INFO)hc->MsgContext;
        hc->MsgConnected = (table != NULL) ? table->MessageCount : 0UL;
    } else {
        /* A line Windows fell back to: its object serves the line's
         * KeSynchronizeExecution (record 15 section 4.2), and the release
         * disconnects it through IoDisconnectInterruptEx all the same. */
        hc->Interrupt = (PKINTERRUPT)hc->MsgContext;
        hc->MsgConnected = 0;
    }
    return STATUS_SUCCESS;
}

/* Which message capability is enabled, from the first 256 bytes of
 * configuration space (record 15 section 4.3). IRQL: PASSIVE_LEVEL. */
static ULONG hcdMsgCapability(PHCD_CONTROLLER hc)
{
    UCHAR config[256];
    ULONG i;

    for (i = 0; i < sizeof(config); i++) {
        config[i] = 0;
    }
    if (XhciReadPciConfig(&hc->Hc, 0, config, sizeof(config)) !=
        MP_STATUS_SUCCESS) {
        return XHCI_POLL_DELIVERY_MESSAGE;
    }
    return XhciMsiCapRead(config);
}

/*
 * The connection for the delivery settled at start (design record 15
 * section 4): the line through IoConnectInterrupt, nothing for a poll, and
 * a message through the seam, hcdMsgConnect. A message connection's
 * failure turns the delivery to a poll ("message not connected") before
 * anything else reads it; a line Windows fell back to is a line delivery
 * with the reason "automatic". Under mode 3 either is a refusal (record 15
 * revision 7): the delivery none, "forced message, not connected", and
 * STATUS_DEVICE_CONFIGURATION_ERROR, whatever was connected left for the
 * caller's release to disconnect. IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdConnect(PHCD_CONTROLLER hc)
{
    NTSTATUS status;

    if (hc->PollDelivery == XHCI_POLL_DELIVERY_LINE) {
        return IoConnectInterrupt(&hc->Interrupt, hcdIsr, hc, NULL,
                                  hc->IntVector, hc->IntIrql, hc->IntIrql,
                                  hc->IntMode, hc->IntShared,
                                  hc->IntAffinity, FALSE);
    }
    if (hc->PollDelivery != XHCI_POLL_DELIVERY_MESSAGE) {
        return STATUS_SUCCESS;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    status = hc->QemuMsgFail
                 ? STATUS_UNSUCCESSFUL
                 : hcdMsgConnect(hc, hc->MsgConnectEx, hc->MsgDisconnectEx);
#else
    status = hcdMsgConnect(hc, hc->MsgConnectEx, hc->MsgDisconnectEx);
#endif
    hc->MsgConnectStatus = status;
    hc->PollDelivery = XhciPollConnected(
        hc->PollValues.Mode, NT_SUCCESS(status) ? 1UL : 0UL,
        (NT_SUCCESS(status) && hc->MsgVersion != HCD_CONNECT_MESSAGE_BASED)
            ? 1UL
            : 0UL,
        &hc->PollReason);
    if (hc->PollDelivery == XHCI_POLL_DELIVERY_MESSAGE) {
        hc->MsgCapability = hcdMsgCapability(hc);
        hc->PollDelivery = hc->MsgCapability;
    }
    hc->Counters.IntrDelivery = hc->PollDelivery;
    hc->Counters.IntrReason = hc->PollReason;
    HcdMsiCountersStart(hc);
    HcdMsiTraceLife(hc, HCD_MSI_LIFE_CONNECT, hc->MsgVersion, (ULONG)status);
    XhciLogNote(&hc->Hc, "msi.connect.status", (ULONG)status);
    XhciLogNote(&hc->Hc, "msi.connect.version", hc->MsgVersion);
    XhciLogNote(&hc->Hc, "msi.connected", hc->MsgConnected);
    XhciLogNote(&hc->Hc, "intr.delivery", hc->PollDelivery);
    XhciLogNote(&hc->Hc, "intr.reason", hc->PollReason);
    if (hc->PollDelivery == XHCI_POLL_DELIVERY_NONE) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

/* IsrDpc's SystemArgument1 when hcdRearmPeek queued it; every other
 * insertion passes NULL. */
#define HCD_DRAIN_FROM_PEEK ((PVOID)(ULONG_PTR)1)

static VOID hcdRearmPeek(PHCD_CONTROLLER hc);

/* IRQL: DISPATCH_LEVEL. The event drain, with the interrupter re-armed as
 * usbport's InterruptDpc(ext, TRUE) asked. A port change wakes the thread,
 * which owns the ports from task 26-A.3. */
static VOID NTAPI hcdIsrDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    PHCD_CONTROLLER hc;
    ULONG portEvents;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg2);

    hc = (PHCD_CONTROLLER)Context;
    if (!hc->DpcClosed) {
        /* XhciEventDpc's own return is the Version 300 report route, which
         * the HCD never takes (RootHubReportThroughDpc is 0). Under a poll
         * delivery the drain re-arms nothing (design record 15 section 4),
         * and a peek follows for what a completion routine resubmitted
         * (section 7.1). */
        portEvents = hc->PortEvents;
        (VOID)XhciEventDpc(&hc->Hc,
                           (hc->Hc.EventDelivery != XHCI_POLL_DELIVERY_POLL)
                               ? TRUE
                               : FALSE);
        if (hc->PortEvents != portEvents) {
            HcdThreadWake(hc);
        }
        if (hc->Hc.EventDelivery == XHCI_POLL_DELIVERY_POLL) {
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            HcdPollPeekLocked(hc);
            XhciControllerLockRelease(&hc->Hc, oldIrql);
        } else if (Arg1 != HCD_DRAIN_FROM_PEEK) {
            hcdRearmPeek(hc);
        }
    }
    (VOID)InterlockedDecrement(&hc->DpcsInFlight);
}

/* --------------------------------------------------------------------- */
/* The controller thread                                                  */
/* --------------------------------------------------------------------- */

/* The objects start and stop reuse, initialised once so a start that fails
 * early can run the same release as a stop. IRQL: PASSIVE_LEVEL (AddDevice). */
VOID HcdControllerInitObjects(PHCD_CONTROLLER hc)
{
    KeInitializeSpinLock(&hc->ControllerLock);
    KeInitializeSpinLock(&hc->PdoListLock);
    HcdTimersInit(hc);
    HcdDmaInitObjects(hc);
    KeInitializeDpc(&hc->IsrDpc, hcdIsrDpc, hc);
    HcdPollInitObjects(hc);
    HcdTmrInitObjects(hc);
    hc->DpcsInFlight = 0;
    hc->DpcClosed = 1;
    KeInitializeEvent(&hc->WorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&hc->CmdDoneEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&hc->PowerGate, SynchronizationEvent, TRUE);
    KeInitializeEvent(&hc->XferDoneEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&hc->EnumDetachDone, NotificationEvent, TRUE);
    KeInitializeEvent(&hc->DoorGate, SynchronizationEvent, TRUE);
    InitializeListHead(&hc->DoorIrps);
    /* Task 38.10: the shutdown's log flush (hcd_quirk.c). */
    KeInitializeEvent(&hc->ShutFlushDone, NotificationEvent, FALSE);
    hc->ShutFlushWanted = 0;
    hc->ShutFlushAcked = 0;
    hc->ShutFlushSink = 0;
    /* Task 36.3: the routing record's unread marks, not zeros, before
     * any start (a zero Mode would read as value 0). */
    XhciPirqReset(&hc->Pirq);
    KeInitializeTimer(&hc->LinkRecoverTimer);
#if defined(XHCI_FLAVOUR_QEMU)
    KeInitializeSpinLock(&hc->InjLock);
    XhciInjRegsClear(&hc->InjRegs);
    XhciInjEpClear(&hc->InjEp);
    KeInitializeEvent(&hc->InjEvDone, NotificationEvent, FALSE);
#endif
}

/*
 * The power gate: a synchronization event used as a mutex - KeInitializeMutex
 * has no allowlist row - held by the in-place recovery and by every path that
 * suspends or resumes the controller, so the two never interleave on two
 * processors. IRQL: PASSIVE_LEVEL to enter.
 */
VOID HcdPowerGateEnter(PHCD_CONTROLLER hc)
{
    (VOID)KeWaitForSingleObject(&hc->PowerGate, Executive, KernelMode, FALSE,
                                NULL);
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID HcdPowerGateLeave(PHCD_CONTROLLER hc)
{
    (VOID)KeSetEvent(&hc->PowerGate, IO_NO_INCREMENT, FALSE);
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID HcdThreadWake(PHCD_CONTROLLER hc)
{
    (VOID)KeSetEvent(&hc->WorkEvent, IO_NO_INCREMENT, FALSE);
}

/*
 * The all-ones containment's admission (hcdContain): tolerance on, not yet
 * contained, a started controller in D0, outside a power transition, with
 * PnP saying it is present. HcInfoStatus is not asked: a recovery whose
 * reinitialization met the dead window set it bad at its top and never got
 * as far as decoding again, but the layout the start validated is still in
 * HcInfo, which only a whole validated decode replaces (XhciDeriveHcInfo),
 * and STARTED says the start validated one. Asking it would let a recovery
 * shut the containment out for the rest of the lifetime.
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
static ULONG hcdContainAdmitted(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    return (ext->Tol.Stats.Tolerance && !ext->Tol.Unreadable &&
            (ext->Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
            (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
            hc->Common.DevicePower == PowerDeviceD0 &&
            !hc->SuspendedInD0 &&
            hc->Common.PnpState != HCD_PNP_SURPRISE_REMOVED &&
            hc->Common.PnpState != HCD_PNP_REMOVED)
               ? 1UL
               : 0UL;
}

/*
 * Put back the Bus Master Enable this driver cleared (a containment's or an
 * unproven invalidation's proof, BusMasterCleared), and read it back set.
 * The recovery's own step for it (XHCI_INIT_STEP_BUS_MASTER_RESTORE)
 * refuses below PASSIVE_LEVEL, where the recovery runs; this is the same
 * write at PASSIVE_LEVEL, made only on a controller that sequence has just
 * halted and reset and not yet run (hcdRecover). IRQL: PASSIVE_LEVEL.
 */
static ULONG hcdRestoreBusMaster(PXHCI_EXTENSION ext)
{
    USHORT command;

    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command, sizeof(USHORT)) !=
            MP_STATUS_SUCCESS ||
        command == 0xFFFFU) {
        return 0;
    }
    if ((command & XHCI_PCI_COMMAND_BME) == 0) {
        command = (USHORT)(command | XHCI_PCI_COMMAND_BME);
        if (XhciWritePciConfig(ext, XHCI_PCI_COMMAND, &command,
                               sizeof(USHORT)) != MP_STATUS_SUCCESS) {
            return 0;
        }
        command = 0;
        if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                              sizeof(USHORT)) != MP_STATUS_SUCCESS ||
            command == 0xFFFFU || (command & XHCI_PCI_COMMAND_BME) == 0) {
            return 0;
        }
    }
    ext->BusMasterCleared = 0;
    return 1;
}

/*
 * The in-place recovery, as the miniport's recovery timer callback ran it,
 * but from this thread instead of a usbport timer DPC: the request is taken
 * under the lock, the recovery itself at DISPATCH_LEVEL, because
 * XhciRecoverController's contract is DISPATCH_LEVEL (it sets
 * InitBelowPassive and keeps every wait a stall). IRQL: PASSIVE_LEVEL.
 */
static VOID hcdRecover(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    KIRQL raised;
    ULONG go;
    ULONG ok;
    ULONG admitted;
    ULONG allOnes;

    ext = &hc->Hc;
    go = 0;

    /* The power gate excludes every suspend and resume for the whole
     * recovery, and the powered state is re-read under it: a recovery decided
     * before a power-down on another processor would otherwise reprogram a
     * controller the bus has just taken to D3 (task 26-A.2). */
    HcdPowerGateEnter(hc);
    XhciControllerLockAcquire(ext, &oldIrql);
    if (ext->RecoveryRequested && ext->ControllerFailed &&
        !ext->Tol.Unreadable && ext->Tol.Window.Refused == 0 &&
        (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
        hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0 &&
        ext->RecoveryFailuresConsecutive < XHCI_RECOVERY_MAX_ATTEMPTS) {
        go = 1;
        /* 35-T.6: a request raised on a window that stopped decoding - a
         * command that timed out on it - is left owed, uncharged, while
         * all-ones stands and the containment would be admitted: begun, it
         * would fail on the dead capability registers, and its new start
         * generation would restamp the all-ones episode on every pass, so
         * the containment would never run (XhciTolRecoverDefer). */
        admitted = hcdContainAdmitted(hc);
        allOnes = 0;
        if (admitted) {
            allOnes = (XhciReadOp(ext, XHCI_OP_USBSTS) == 0xFFFFFFFFUL)
                          ? 1UL
                          : 0UL;
        }
        if (XhciTolRecoverDefer(ext->Tol.Stats.Tolerance, admitted,
                                allOnes)) {
            go = 0;
        }
    }
    if (go) {
        ext->RecoveryRequested = 0;
        /* Selective suspend's epoch (design record 14 section 4.4): the
         * thread's next powered pass settles a Dx pended before it. */
        HcdSuspControllerDownLocked(hc);
        /* 35-T.6's window (record 17 section 4.6), charged as a recovery
         * begins, retries included: a fourth inside ten minutes is not
         * begun, and the request is dropped. Window.Refused, cleared only
         * by XhciTolStart, is the latch: no later request is acted on and
         * no resume reinitializes the controller (hcd_power.c), so it
         * stays failed until a stop and start - today's terminal after
         * three failures in a row, which keeps its own meaning. */
        if (!XhciTolWindowAdmit(&ext->Tol.Window, ext->Tol.Stats.Tolerance,
                                HcdTolNow(hc))) {
            go = 0;
            ext->Tol.Stats.WindowRefused++;
            XhciLogNoteLocked(ext, "ctrl.window.refused",
                              ext->Tol.Window.Refused);
        }
    }
    XhciControllerLockRelease(ext, oldIrql);
    if (!go) {
        HcdPowerGateLeave(hc);
        return;
    }

    /* Record 15 section 4.2: the poller's service revoked, then a moved
     * line's owed retirement contained, before the recovery resets. */
    HcdPollRevoke(hc);
    (VOID)HcdPollContain(hc);
    hc->TolStartGen++;
    KeRaiseIrql(DISPATCH_LEVEL, &raised);
    ok = XhciRecoverController(ext);
    KeLowerIrql(raised);
    /* A sequence refused at the Bus Master Enable restore had halted and
     * reset the controller and stopped short of Run/Stop only because the
     * write needs PASSIVE_LEVEL. Made here, under the same gate, and the
     * sequence run once more: the same recovery, not a new one, so the
     * window is not charged again. Without it, a recovery after a proof
     * that cleared the bit could never restore service. */
    if (!ok && ext->BusMasterCleared &&
        ext->RecoveryLastStep == XHCI_INIT_STEP_BUS_MASTER_RESTORE &&
        hcdRestoreBusMaster(ext)) {
        XhciLogNote(ext, "ctrl.recover.bme.restored", 1);
        hc->TolStartGen++;
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        ok = XhciRecoverController(ext);
        KeLowerIrql(raised);
    }
    /* The poller's service back on the final successful pass, inside the
     * power gate (record 15 section 5.3); before it, task 38.10's ASMedia
     * write, which the raised reset could not make (record 20 section
     * 4.8). */
    if (ok) {
        HcdQuirkAsmFlow(hc);
        HcdPollGrant(hc);
    }
    HcdPowerGateLeave(hc);

    if (!ok) {
        XhciControllerLockAcquire(ext, &oldIrql);
        if (ext->RecoveryFailuresConsecutive < XHCI_RECOVERY_MAX_ATTEMPTS) {
            ext->RecoveryRequested = 1;
        }
        XhciControllerLockRelease(ext, oldIrql);
    }
}

/*
 * A terminal no recovery acts on - the window refused one (35-T.6), or the
 * run of failures is spent - leaves the controller latched failed with the
 * transfers it was given still on its rings: the event drain and the
 * commands are refused, and only a recovery's own XhciSlotInvalidateAll
 * ever completed them. With none coming, no class driver's request ended,
 * no query-remove finished, and the stop that would drop them never came
 * (35-V, the Windows 2000 SMP guest: the disable hung after
 * ctrl.window.refused). So the thread raises that invalidation itself,
 * once per lifetime, and HcdEnumService drains as after a recovery: with
 * HCH read set (the controller stopped executing, the evidence the
 * recovery takes) every device is dropped and its transfers completed;
 * without it the invalidation is unproven, and HcdEnumService takes the
 * Bus Master Enable proof first and, failing it, pins the buffer and
 * contains, keeping every transfer and mapping. Admitted as that service
 * is, on a started controller in D0. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdTerminalRelease(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG terminal;
    ULONG devices;
    ULONG usbsts;
    ULONG i;

    ext = &hc->Hc;
    HcdPowerGateEnter(hc);
    XhciControllerLockAcquire(ext, &oldIrql);
    if ((ext->Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
        hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0) {
        terminal = XhciTolTerminal(ext->ControllerFailed, ext->Tol.Unreadable,
                                   ext->Tol.Window.Refused,
                                   ext->RecoveryFailuresConsecutive,
                                   XHCI_RECOVERY_MAX_ATTEMPTS);
        /* Every record with a slot is in the table, written under this
         * lock (hcdDeviceNew, hcdDeviceFree). */
        devices = 0;
        for (i = 1; i <= XHCI_MAX_SLOTS && !devices; i++) {
            devices = (hc->SlotDevice[i] != NULL) ? 1UL : 0UL;
        }
        if (XhciTolTerminalRelease(terminal, hc->SlotsInvalidated,
                                   hc->TerminalReleased, devices)) {
            hc->TerminalReleased = 1;
            /* The slots stay enabled without a Disable Slot, their buffers
             * about to be given back: no saved image may be restored over
             * them, and no new one is taken while failed (xhciSaveState). */
            ext->SavedStateValid = 0;
            usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
            XhciLogNoteLocked(ext, "ctrl.terminal.release", usbsts);
            XhciSlotInvalidateAll(ext, XhciTolHaltProven(usbsts));
        }
    }
    XhciControllerLockRelease(ext, oldIrql);
    HcdPowerGateLeave(hc);
}

/*
 * The drain's admission, the backstop's and the bus command path's: a good,
 * initialized, unfailed controller in D0 with the DPC open - what the health
 * poll's own admission does not check. Called with the controller lock
 * held. IRQL: DISPATCH_LEVEL.
 */
static ULONG hcdDrainAdmitted(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    return (ext->HcInfoStatus == XHCI_HC_OK &&
            (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0 &&
            (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
            !ext->ControllerFailed &&
            hc->Common.DevicePower == PowerDeviceD0 &&
            !hc->SuspendedInD0 && !hc->DpcClosed &&
            ext->EventRing.Base != NULL) ? 1UL : 0UL;
}

/* hcdDrainAdmitted for hcd_poll.c. IRQL: DISPATCH_LEVEL, controller lock
 * held. */
ULONG HcdCtlDrainAdmitted(PHCD_CONTROLLER hc)
{
    return hcdDrainAdmitted(hc);
}

/* hcdIsr's admission: counted before the insertion, rolled back on a
 * refusal, nothing once the DPC is closed (the stop ends the thread before
 * hcdRelease closes it). A drain already running on another processor
 * serializes on the lock and finds the ring empty. Returns whether it was
 * queued. IRQL: <= DISPATCH_LEVEL. */
static ULONG hcdQueueDrain(PHCD_CONTROLLER hc)
{
    (VOID)InterlockedIncrement(&hc->DpcsInFlight);
    if (hc->DpcClosed || !KeInsertQueueDpc(&hc->IsrDpc, NULL, NULL)) {
        (VOID)InterlockedDecrement(&hc->DpcsInFlight);
        return 0;
    }
    return 1;
}

/*
 * The event ring check after the re-arm (roadmap-hcd 37.4, design record 17
 * section 4.1, "Amended for 37.4"): hcdIsrDpc's, after an interrupt-
 * delivered drain released EHB and set IMAN.IE. An event the controller
 * wrote while IE was clear raised IP, which a compliant interrupter delivers
 * once IE is back; QEMU's MSI-X instead drops that message, since it raises
 * it while the vector is unused across the IE toggle, and the event waited
 * for the backstop. One peek under the controller lock, with the backstop's
 * admission and the re-arm's own condition (XHCI_EXT_FLAG_INTERRUPTS, the
 * flag the drain re-armed on), and at most one insertion through the ISR's
 * admission (hcdQueueDrain's, with HCD_DRAIN_FROM_PEEK) - never a drain
 * inline and never a chain: the drain this queues does not peek, so each
 * further drain waits for a moderated interrupt and no run of DPCs can hold
 * a processor at DISPATCH_LEVEL under sustained completions (task 37.4).
 * An event the second drain's own window strands waits for the
 * backstop, as before. An insertion while IsrDpc is still queued is
 * refused and counted as pending only. No register is written, so the
 * ISR's IE=0 (record 05, "The DIRQL exception") is untouched. IRQL:
 * DISPATCH_LEVEL, no lock held.
 */
static VOID hcdRearmPeek(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG pending;

    ext = &hc->Hc;
#if defined(XHCI_FLAVOUR_QEMU)
    /* Task 36.4's start hold keeps the start's No Op undrained. */
    if (HcdInjStartHeld(hc)) {
        return;
    }
#endif
    XhciControllerLockAcquire(ext, &oldIrql);
    pending = (hcdDrainAdmitted(hc) &&
               (ext->Flags & XHCI_EXT_FLAG_INTERRUPTS) != 0 &&
               XhciEventRingPending(&ext->EventRing)) ? 1UL : 0UL;
    if (pending) {
        ext->RearmPeekPending++;
    }
    XhciControllerLockRelease(ext, oldIrql);
    if (!pending) {
        return;
    }
    (VOID)InterlockedIncrement(&hc->DpcsInFlight);
    if (hc->DpcClosed ||
        !KeInsertQueueDpc(&hc->IsrDpc, HCD_DRAIN_FROM_PEEK, NULL)) {
        (VOID)InterlockedDecrement(&hc->DpcsInFlight);
        return;
    }
    XhciControllerLockAcquire(ext, &oldIrql);
    ext->RearmPeekDrains++;
    XhciControllerLockRelease(ext, oldIrql);
}

/*
 * The lost-interrupt backstop (35-T.1, design record 17 section 4.1),
 * admitted by hcdDrainAdmitted. Under the controller lock it peeks at the
 * event TRB at the software dequeue; an event pending at the same index,
 * cycle, drain pass (DpcCount, bumped under this lock by every admitted
 * drain) and start generation for the backstop interval was written and
 * never drained, and the drain is queued through the ISR's own admission.
 * It writes no register: ERDP, EHB and IE stay the drain's. A refused
 * admission drops the observation, so timing starts afresh.
 *
 * Task 36.55: called from the tolerance clock's DPC on every tick, no
 * longer from the controller thread, which Windows 98 SE was seen to leave
 * unscheduled while a disk request waited on this very drain (record 17
 * section 4.1, "Amended"). No power gate: what it queues is the ISR's own
 * DPC, which admits itself under the controller lock as an interrupt's
 * does at any point of a transition. One peek and at most one insertion;
 * never a drain inline. The caller runs it before it re-arms its timer, so
 * HcdTimersDrain, which waits for that re-arm or its refusal, waits for
 * this too. IRQL: DISPATCH_LEVEL, no lock held.
 */
VOID HcdCtlBackstopTick(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG admitted;
    ULONG pending;
    ULONG queue;

    ext = &hc->Hc;
    queue = 0;
#if defined(XHCI_FLAVOUR_QEMU)
    /* Task 36.4's start hold keeps the start's No Op undrained. */
    if (HcdInjStartHeld(hc)) {
        return;
    }
#endif
    XhciControllerLockAcquire(ext, &oldIrql);
    admitted = hcdDrainAdmitted(hc);
    pending = admitted ? XhciEventRingPending(&ext->EventRing) : 0UL;
    queue = XhciTolBackstop(&ext->Tol.Obs, ext->Tol.Stats.Tolerance, pending,
                            ext->EventRing.Dequeue, ext->EventRing.Ccs,
                            ext->DpcCount, hc->TolStartGen, HcdTolNow(hc));
    XhciControllerLockRelease(ext, oldIrql);
    if (!queue || !hcdQueueDrain(hc)) {
        return;
    }
    XhciControllerLockAcquire(ext, &oldIrql);
    ext->Tol.Stats.BackstopDrains++;
    XhciControllerLockRelease(ext, oldIrql);
}

/*
 * A drain under hcdDrainAdmitted, asked for only when an event stands at the
 * dequeue; the caller's own counting. Returns whether one was queued. IRQL:
 * PASSIVE_LEVEL, the power gate held by the caller or by nothing that could
 * be waiting on it.
 */
ULONG HcdDrainPending(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG queue;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    queue = (hcdDrainAdmitted(hc) &&
             XhciEventRingPending(&ext->EventRing)) ? 1UL : 0UL;
    XhciControllerLockRelease(ext, oldIrql);
    return (queue && hcdQueueDrain(hc)) ? 1UL : 0UL;
}

/*
 * Task 36.4 (issue 12, Finding 1): a drain for the bus's command path, which
 * met the engine busy with an earlier command whose completion may have
 * come with no interrupt - the start's No Op on the E7250, whose line
 * reached nothing, and which the backstop cannot drain while the thread
 * waits here. The backstop's admission under the controller lock, without
 * the power gate, which the thread may already hold (HcdEnumService), and
 * only when an event stands at the dequeue. Returns whether one was queued.
 * IRQL: PASSIVE_LEVEL.
 */
ULONG HcdCommandDrain(PHCD_CONTROLLER hc)
{
    if (!HcdDrainPending(hc)) {
        return 0;
    }
    hc->EnumCommandBusyDrains++;
    return 1;
}

/*
 * 35-T.6's proof that a contained controller masters nothing (design record
 * 17 section 4.6, step 2): configuration space answers, and Bus Master Enable
 * reads back clear - cleared here when it was set. Stricter than the
 * quiesce's own fallback (xhci_init.c, xhciTryClearBusMaster), which takes a
 * Command register of all ones as a device off the bus: here that is a
 * function that does not answer, and no proof. A clear this driver made is
 * recorded, so a later start puts the bit back (xhciRestoreBusMaster).
 * IRQL: PASSIVE_LEVEL (configuration space goes out to the bus driver).
 */
static ULONG hcdContainProve(PXHCI_EXTENSION ext)
{
    USHORT command;

    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command, sizeof(USHORT)) !=
            MP_STATUS_SUCCESS ||
        command == 0xFFFFU) {
        return 0;
    }
    if ((command & XHCI_PCI_COMMAND_BME) != 0) {
        command = (USHORT)(command & ~XHCI_PCI_COMMAND_BME);
        if (XhciWritePciConfig(ext, XHCI_PCI_COMMAND, &command,
                               sizeof(USHORT)) != MP_STATUS_SUCCESS) {
            return 0;
        }
        ext->BusMasterCleared = 1;
        command = 0;
        if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                              sizeof(USHORT)) != MP_STATUS_SUCCESS ||
            command == 0xFFFFU) {
            return 0;
        }
    }
#if defined(XHCI_FLAVOUR_QEMU)
    /* 35-T.9's all-ones without the proof: the read-back answers set. */
    command = HcdInjPciCommand(HcdControllerFromExt(ext), command);
#endif
    return (command & XHCI_PCI_COMMAND_BME) == 0 ? 1UL : 0UL;
}

/* The same proof for an invalidation no halt or HCRST has proven
 * (HcdEnumService). IRQL: PASSIVE_LEVEL. */
ULONG HcdCtlProveDmaStopped(PXHCI_EXTENSION ext)
{
    return hcdContainProve(ext);
}

/*
 * The all-ones containment (35-T.6, design record 17 section 4.6): a step of
 * its own after hcdRecover, under the power gate as that is, admitted only on
 * a started controller in D0, outside a power transition, with PnP saying it
 * is present - so no power transition or recovery runs beside it - and with
 * tolerance on. It reads USBSTS itself; the health poll's ungated samples are
 * not used. XhciTolDeadStep stamps the first all-ones read with the start
 * generation and clears the stamp on a good read, a refused admission, or a
 * new generation (a start, a resume, an in-place recovery). Once all-ones has
 * stood on every admitted pass for the containment interval, in order:
 *
 *   1 the controller latched failed and Unreadable, under the controller
 *     lock: the drain, commands, enumeration and the health poll stop, the
 *     recovery never acts on it (hcdRecover), and every submission path
 *     holds a new request on its PDO instead of refusing it. Nothing already
 *     submitted is completed;
 *   2 Bus Master Enable cleared and read back clear (hcdContainProve);
 *   3 with that proof, every device drained and dropped (HcdEnumContain);
 *     without it, the common buffer pinned first, then every device drained,
 *     which on a pinned buffer keeps every transfer and mapping.
 *
 * Once per lifetime: Unreadable refuses the admission until a start clears
 * it (XhciTolStart). IRQL: PASSIVE_LEVEL.
 */
static VOID hcdContain(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG admitted;
    ULONG allOnes;
    ULONG armed;
    ULONG contain;
    ULONG proof;

    ext = &hc->Hc;
    allOnes = 0;
    HcdPowerGateEnter(hc);
    XhciControllerLockAcquire(ext, &oldIrql);
    admitted = hcdContainAdmitted(hc);
    if (admitted) {
#if defined(XHCI_FLAVOUR_QEMU)
        allOnes = (HcdInjUsbsts(hc, XhciReadOp(ext, XHCI_OP_USBSTS)) ==
                   0xFFFFFFFFUL) ? 1UL : 0UL;
#else
        allOnes = (XhciReadOp(ext, XHCI_OP_USBSTS) == 0xFFFFFFFFUL) ? 1UL
                                                                     : 0UL;
#endif
    }
    armed = (ext->Tol.Dead.Armed && ext->Tol.Dead.StartGen == hc->TolStartGen)
                ? 1UL
                : 0UL;
    contain = XhciTolDeadStep(&ext->Tol.Dead, ext->Tol.Stats.Tolerance,
                              admitted, allOnes, hc->TolStartGen,
                              HcdTolNow(hc));
    if (!armed && ext->Tol.Dead.Armed) {
        ext->Tol.Stats.DeadEpisodes++;
        XhciLogNoteLocked(ext, "tol.dead.stamp", ext->Tol.Dead.Stamp);
    }
    if (contain) {
        /* Step 1. The reason is Unreadable itself: no recovery request is
         * raised, and one raised elsewhere is never acted on. The enables
         * are masked as any failure masks them, a write the dead window
         * may not take. */
        if (!ext->ControllerFailed &&
            (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0) {
            XhciMaskInterrupts(ext);
        }
        ext->ControllerFailed = 1;
        ext->Tol.Unreadable = XHCI_TOL_CONTAINED_UNREADABLE;
        XhciLogNoteLocked(ext, "ctrl.failed.unreadable", HcdTolNow(hc));
    }
    XhciControllerLockRelease(ext, oldIrql);
    if (!contain) {
        HcdPowerGateLeave(hc);
        return;
    }

    proof = hcdContainProve(ext);
    if (!proof) {
        HcdSvcDmaNotStopped(ext);
    }
    HcdEnumContain(hc, proof);
    XhciControllerLockAcquire(ext, &oldIrql);
    ext->Tol.Stats.Contained = proof ? XHCI_TOL_CONTAIN_RELEASED
                                     : XHCI_TOL_CONTAIN_PINNED;
    XhciLogNoteLocked(ext, "tol.contained", ext->Tol.Stats.Contained);
    XhciControllerLockRelease(ext, oldIrql);
    HcdPowerGateLeave(hc);
}

/*
 * Declare the controller failed and ask the thread for the in-place
 * recovery: the HCD's answer to a failed resume, which under usbport ended in
 * the stop/start a failed ResumeController asked usbport for (task
 * 26-A.2). IRQL: <= DISPATCH_LEVEL, controller lock released.
 */
VOID HcdControllerFail(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    if (!ext->ControllerFailed) {
        ext->ControllerFailed = 1;
        XhciLogNoteLocked(ext, "ctrl.failed.resume", 1);
    }
    ext->RecoveryRequested = 1;
    XhciControllerLockRelease(ext, oldIrql);
    HcdThreadWake(hc);
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdPoll(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL raised;
    ULONG escalate;

    ext = &hc->Hc;
#if defined(XHCI_FLAVOUR_QEMU)
    /* 35-T.9: a fault a guest script asked for, before the steps that
     * meet it. */
    HcdInjPoll(hc);
#endif
    if ((ext->Flags & XHCI_EXT_FLAG_STARTED) != 0) {
        ext->CheckCallbacks++;

        /* XhciControllerHealthPoll's contract is DISPATCH_LEVEL: usbport
         * called CheckController under its MiniportSpinLock. */
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        escalate = XhciControllerHealthPoll(ext);
        KeLowerIrql(raised);
        if (escalate) {
            XhciRequestControllerReset(ext);
        }
    }
    HcdCountersPoll(hc);
    /* Outside the STARTED gate: a failed resume (HcdControllerFail) is a
     * recovery request on a controller whose flags no longer say it runs. */
    hcdRecover(hc);
    hcdTerminalRelease(hc);
    /* The lost-interrupt backstop is the tolerance clock's DPC's since task
     * 36.55 (HcdCtlBackstopTick). Record 15 sections 4.2 and 7.1: a moved
     * line's containment, and a peek under a poll delivery. */
    HcdPollThreadPass(hc);
    hcdContain(hc);
}

/* IRQL: PASSIVE_LEVEL (a system thread). */
static VOID NTAPI hcdThread(PVOID Context)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;
    LONG ticket;
    ULONG sunk;

    hc = (PHCD_CONTROLLER)Context;
    for (;;) {
        HcdRelativeMs(&due, HCD_POLL_MS);
        (VOID)KeWaitForSingleObject(&hc->WorkEvent, Executive, KernelMode,
                                    FALSE, &due);
        if (hc->ThreadStop) {
            break;
        }
        hcdPoll(hc);
        /* The powered state is read under the gate, as the recovery's is: a
         * check before it could pass and then lose to a power-down (batch
         * (b)). */
        HcdPowerGateEnter(hc);
        HcdEnumService(hc, (hc->Hc.Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
                               hc->Common.DevicePower == PowerDeviceD0 &&
                               !hc->SuspendedInD0);
        HcdPowerGateLeave(hc);
        /* The continuous sink (26-A.8): whatever the producers recorded
         * since the last wake, emitted here at PASSIVE_LEVEL and nowhere
         * nearer a DPC. The shutdown's flush ticket is captured before it
         * and acknowledged after it (task 38.10, hcd_quirk.c). */
        ticket = hc->ShutFlushWanted;
        sunk = hc->Hc.Log.DebugViewBytes;
        HcdLogFlush(hc, XHCI_LOG_REASON_PERIODIC, 0);
        HcdQuirkFlushAck(hc, ticket, hc->Hc.Log.DebugViewBytes != sunk);
    }
    XHCI_DBG_TEXT("hcd: controller thread leaving its loop");
    /* No door request waits for a thread that is gone (task 36.45). */
    HcdDoorQueueClose(hc);
    /* The only arming party leaves: no timer of the controller's stays
     * queued past its storage (task 33.3). */
    (VOID)KeCancelTimer(&hc->LinkRecoverTimer);
    /* No root hub waits on a thread that is gone (HcdEnumDetach). */
    (VOID)KeSetEvent(&hc->EnumDetachDone, IO_NO_INCREMENT, FALSE);
    (VOID)KeSetEvent(&hc->ThreadExited, IO_NO_INCREMENT, FALSE);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * How the stop waits for the thread differs by target, and each half is the
 * one measured to work there (26-A.2, 2026-10-03, QEMU; runs/run-26.md):
 *
 *   - NT (WDM 1.10 and later; Windows 98 reports 1.00 and ME 1.05,
 *     win98-wdm.md): on the thread's object, referenced from the
 *     handle. A disable on Windows 2000 is QUERY_REMOVE then REMOVE, and the
 *     remove unloads the image; only the thread object's signal says the
 *     thread has left it. With the event wait instead, the trace ran through
 *     the whole teardown and the guest then rebooted, twice - read as the
 *     thread still executing its last instructions here when the image went
 *     (inferred; no dump was taken). With the thread-object wait the same
 *     disable passed two cycles.
 *   - Windows 98 and ME: on that event. A KeWaitForSingleObject on the
 *     thread's object faulted Windows 98 SE ("A fatal exception 0E has
 *     occurred at 0028:C00312EE"), between the traces either side of the
 *     wait. The event leaves the thread's call into PsTerminateSystemThread
 *     after it; a Windows 98 disable is QUERY_STOP and STOP, which unloads
 *     nothing, and the remove that would unload the image has not been
 *     measured against that window on Windows 98 (26-V.1's).
 *
 * IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdThreadStart(PHCD_CONTROLLER hc)
{
    HANDLE handle;
    NTSTATUS status;

    hc->ThreadStop = 0;
    hc->ThreadRunning = 0;
    hc->ThreadObject = NULL;
    KeInitializeEvent(&hc->WorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&hc->ThreadExited, NotificationEvent, FALSE);
    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  hcdThread, hc);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = STATUS_SUCCESS;
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        /* A NULL object type: PsThreadType is a data import, and the handle
         * came from the call above. */
        status = ObReferenceObjectByHandle(handle, THREAD_ALL_ACCESS, NULL,
                                           KernelMode, &hc->ThreadObject,
                                           NULL);
    }
    (VOID)ZwClose(handle);
    if (!NT_SUCCESS(status)) {
        /*
         * On NT the event alone cannot cover a remove's unload, so a start
         * that could not reference its thread does not proceed (task
         * 26-A.2). The thread is told to leave and its
         * event waited for; what it still runs after that is its call into
         * PsTerminateSystemThread from this image. **That window is not
         * closed**: with the object unreferenced there
         * is nothing to join, and a remove that unloaded the image inside it
         * would unload it under the thread. It is the residual of a failure
         * no run has seen - a reference to a handle PsCreateSystemThread has
         * just returned - and it is counted.
         */
        hc->ThreadReferenceFailures++;
        hc->ThreadObject = NULL;
        hc->ThreadStop = 1;
        HcdThreadWake(hc);
        (VOID)KeWaitForSingleObject(&hc->ThreadExited, Executive,
                                    KernelMode, FALSE, NULL);
        return status;
    }
    hc->ThreadRunning = 1;
    HcdDoorQueueOpen(hc);
    return STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdThreadStop(PHCD_CONTROLLER hc)
{
    if (!hc->ThreadRunning) {
        return;
    }
    XHCI_DBG_TEXT("hcd: stopping the controller thread");
    hc->ThreadStop = 1;
    HcdThreadWake(hc);
    if (hc->ThreadObject != NULL) {
        (VOID)KeWaitForSingleObject(hc->ThreadObject, Executive, KernelMode,
                                    FALSE, NULL);
        ObDereferenceObject(hc->ThreadObject);
        hc->ThreadObject = NULL;
    } else {
        (VOID)KeWaitForSingleObject(&hc->ThreadExited, Executive, KernelMode,
                                    FALSE, NULL);
    }
    XHCI_DBG_TEXT("hcd: controller thread has exited");
    hc->ThreadRunning = 0;
}

/* --------------------------------------------------------------------- */
/* Start and stop                                                         */
/* --------------------------------------------------------------------- */

/* IRQL: PASSIVE_LEVEL. Undo whatever hcdStartController got as far as. */
static VOID hcdRelease(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;

    /* Before the disconnect (design record 15 section 5.2): the move's
     * KeSynchronizeExecution never meets a disconnected interrupt. */
    XHCI_DBG_TEXT("hcd: release: poller");
    HcdPollClose(hc);
    XHCI_DBG_TEXT("hcd: release: interrupt");
    if (hc->MsgVersion != 0) {
        /* Whatever IoConnectInterruptEx connected - a message or the line
         * it fell back to - goes through IoDisconnectInterruptEx with the
         * Version and context it returned (record 15 section 4.3). */
        HCD_IO_DISCONNECT_PARAMETERS disconnect;

        disconnect.Version = hc->MsgVersion;
        disconnect.ConnectionContext = hc->MsgContext;
        ((HCD_DISCONNECT_EX)hc->MsgDisconnectEx)(&disconnect);
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_DISCONNECT, hc->MsgVersion, 0);
        hc->MsgVersion = 0;
        hc->MsgContext = NULL;
        hc->Interrupt = NULL;
    } else if (hc->Interrupt != NULL) {
        IoDisconnectInterrupt(hc->Interrupt);
        hc->Interrupt = NULL;
    }
    /* No ISR can queue it now. A DPC already queued is let run - closed, it
     * does nothing but retire its count - rather than dequeued:
     * KeRemoveQueueDpc's TRUE does not promise, before Vista SP1, that the
     * DPC will not run, and retiring its count here as well would retire it
     * twice (task 26-A.2).
     *
     * The tolerance clock's DPC still queues it until HcdTimersDrain below
     * (HcdCtlBackstopTick, task 36.55): it counts DpcsInFlight with an
     * interlocked increment and then reads DpcClosed (hcdQueueDrain), so
     * the close is an interlocked exchange before DpcsInFlight is read.
     * Each side orders its write before its read; either the producer sees
     * the close and rolls back, or this wait sees its count. */
    XHCI_DBG_TEXT("hcd: release: DPC");
    (VOID)InterlockedExchange((PLONG)&hc->DpcClosed, 1);
    while (hc->DpcsInFlight != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    XHCI_DBG_TEXT("hcd: release: timers");
    HcdTimersDrain(hc);
    XHCI_DBG_TEXT("hcd: release: DMA");
    HcdDmaMapDrain(hc);
    HcdDmaWideClose(hc);
    HcdDmaClose(hc);
    XHCI_DBG_TEXT("hcd: release: BAR");
    if (hc->BarVa != NULL) {
        MmUnmapIoSpace(hc->BarVa, hc->BarLength);
        hc->BarVa = NULL;
    }
}

static VOID hcdStopBody(PHCD_CONTROLLER hc);

/*
 * 36.21's readings start again with each start, the clocks' pair taken
 * now; the class drivers' records are kept (each says when it came). Under
 * the controller lock, which the door reads them under. IRQL:
 * PASSIVE_LEVEL.
 */
static VOID hcdXstatStart(PHCD_CONTROLLER hc)
{
    PXHCI_XSTAT s;
    LARGE_INTEGER now;
    KIRQL oldIrql;
    PUCHAR p;
    ULONG i;

    s = &hc->Xstat;
    KeQuerySystemTime(&now);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    p = (PUCHAR)s;
    for (i = 0; i < sizeof(*s); i++) {
        p[i] = 0;
    }
    /* 36.24's counters start again with it. */
    p = (PUCHAR)&hc->Xpipe;
    for (i = 0; i < sizeof(hc->Xpipe); i++) {
        p[i] = 0;
    }
    HcdTscRead(&s->TscStartLo, &s->TscStartHi);
    s->TimeStartLo = now.LowPart;
    s->TimeStartHi = (ULONG)now.HighPart;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* The grant IoGetDmaAdapter gave, and what one chunk may span of it
 * (hcd_io.c, hcdPlanChunk). IRQL: PASSIVE_LEVEL, after HcdDmaOpen. */
static VOID hcdXstatGrant(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

#if defined(XHCI_FLAVOUR_QEMU)
    /* 36.22's constrained grant: fewer registers than the adapter gave,
     * so every chunk is planned from it (hcd_io.c, hcdPlanChunk). */
    if (hc->QemuMapGrant != 0 && hc->QemuMapGrant < hc->MapRegisters) {
        hc->MapRegisters = hc->QemuMapGrant;
        XhciLogNote(&hc->Hc, "qemu.mapgrant.applied", hc->MapRegisters);
    }
#endif
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Xstat.MapGrant = hc->MapRegisters;
    hc->Xstat.ChunkPages = hc->MapRegisters < HCD_SG_ELEMENTS
                               ? hc->MapRegisters
                               : HCD_SG_ELEMENTS;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * Bring the controller up: resources, BAR, adapter and common buffer, the
 * interrupt, XhciInitController, the interrupt enables, the thread. Called
 * after the PCI stack has started (hcd_pnp.c). IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdStartBody(PHCD_CONTROLLER hc, PIRP irp)
{
    PXHCI_EXTENSION ext;
    KIRQL raised;
    USBPORT_RESOURCES res;
    PUCHAR p;
    ULONG i;
    MPSTATUS mp;
    NTSTATUS status;

    ext = &hc->Hc;

    /* usbport zeroed the miniport extension before every StartController,
     * and the kept sequence relies on it: XhciStopController's RH_CLOSED, for
     * one, is never cleared again because "a start rebuilds the extension from
     * the zero usbport writes over it". The HCD writes the zero itself. */
    p = (PUCHAR)ext;
    for (i = 0; i < sizeof(XHCI_EXTENSION); i++) {
        p[i] = 0;
    }
    /* Task 38.10: every quirk at its default and unread, before the
     * delivery reads XhciMsiAvoid (hcd_quirk.c). */
    HcdQuirkStartBegin(hc);
    /* Task 42-A.17: the start's values taken from the machine-wide key,
     * counted from the delivery's first read to the note after the
     * quirks and the route below. */
    hc->RegMachineWon = 0;
    /* The power fallback's marker describes the controller this start
     * rebuilds, so it is reset with it (task 26-A.2). */
    hc->SuspendedInD0 = 0;
    /* And the shutdown latch (37.3): a new lifetime serves its requests
     * (reviewed and revised after 4de0f999). Before the start
     * admits anything, so no lock is needed against an admission. */
    hc->ShutLatched = 0;
    /* Task 36.3's record was reset when the START_DEVICE arrived, before
     * the lower driver saw it (HcdPirqStartAttempt, hcd_pnp.c). */
    HcdCountersStart(hc);
    /* The finer timer's block (design record 15 section 7.2.5), before the
     * poller is opened and so before any of its producers can run. */
    HcdTmrStartReset(hc);
    HcdEnumInit(hc);
    hcdXstatStart(hc);
    /* This attempt's decision starts unknown, so a start refused before
     * hcdSettleDelivery reports none rather than the previous start's in
     * its start-decision record (task 36.15). */
    hc->PollValues.Mode = XHCI_POLL_NOT_FOUND;
    hc->PollDelivery = XHCI_POLL_DELIVERY_NONE;
    hc->PollReason = XHCI_POLL_R_NONE;

    status = hcdParseResources(hc, irp);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    /* Design record 15 section 4: the delivery, before anything is mapped
     * or connected. */
    status = hcdSettleDelivery(hc);
    if (!NT_SUCCESS(status)) {
        hcdLogRefused(hc);
        return status;
    }

    hc->BarVa = MmMapIoSpace(hc->BarTranslated, hc->BarLength, MmNonCached);
    if (hc->BarVa == NULL) {
        hcdRelease(hc);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = HcdDmaOpen(hc);
    if (!NT_SUCCESS(status)) {
        hcdRelease(hc);
        return status;
    }

    XhciCommandInit(ext);
    ext->DeliverUnderUsbportLockOnly = 0;
    ext->DeliverPerEndpointOnly = 0;
    ext->ArmThroughExOnly = 0;
    ext->RootHubReportThroughDpc = 0;
    ext->Signature = XHCI_EXTENSION_SIGNATURE;
    ext->TrailingSignature = XHCI_EXTENSION_TRAILING;
    hcdReadValues(hc);
    hcdReadSilent(hc);
    hcdLogDelivery(hc);
#if defined(XHCI_FLAVOUR_QEMU)
    HcdInjStart(hc);
#endif

    /* The interrupt bit only when an interrupt resource was given (record
     * 15 section 4: ResourcesTypes tells the truth); the core requires it
     * of a line delivery alone. */
    ext->ResourcesTypes =
        hc->HaveInterrupt
            ? XhciResourcesRequired
            : (XhciResourcesRequired & ~(ULONG)USBPORT_RESOURCES_INTERRUPT);
    ext->ResourceBase = (ULONG_PTR)hc->BarVa;
    ext->IoSpaceLength = hc->BarLength;
    ext->StartVA = (ULONG_PTR)hc->CommonVa;
    ext->StartPA = hc->CommonPa.LowPart;
    ext->InterruptVector = hc->IntVector;
    ext->InterruptLevel = (ULONG)hc->IntIrql;
    ext->InterruptMode = (ULONG)hc->IntMode;
    XhciLogNoteAddress(ext, "start.bar", hc->BarRaw.LowPart);
    /* A DMA address: recorded at level 4 alone, with the other addresses,
     * so levels 2 and 3 carry none (design record 25 revision 12). */
    XhciLogNoteAddress(ext, "start.common.pa", ext->StartPA);
    XhciLogNote(ext, "start.mapregs", hc->MapRegisters);
    hcdXstatGrant(hc);
    /* 36.24: after the grant, before anything is enumerated. */
    HcdDmaWideOpen(hc);
    /* The design record 13 section 11.4 reading, on the debug channel
     * too: the log ring has no reader until 26-A.8. */
    XHCI_DBG_VALUE("hcd: start, map registers granted", hc->MapRegisters);

    p = (PUCHAR)&res;
    for (i = 0; i < sizeof(res); i++) {
        p[i] = 0;
    }
    res.ResourcesTypes = ext->ResourcesTypes;
    res.InterruptVector = hc->IntVector;
    res.InterruptLevel = (UCHAR)hc->IntIrql;
    res.InterruptAffinity = (ULONG_PTR)hc->IntAffinity;
    res.ShareVector = (UCHAR)hc->IntShared;
    res.InterruptMode = (ULONG)hc->IntMode;
    res.ResourceBase = hc->BarVa;
    res.IoSpaceLength = hc->BarLength;
    res.StartVA = ext->StartVA;
    res.StartPA = ext->StartPA;

    HcdTimersOpen(hc);
    hc->DpcClosed = 0;

    /* Connected before the sequence, as usbport connected it before
     * StartController: XhciIsr claims nothing until the controller is
     * initialized, and the No Op self-test's completion needs the DPC.
     * Under a poll delivery nothing is connected (record 15 section 4); a
     * message's connection may still turn the delivery to a poll or a line
     * (hcdConnect), so what reads the delivery follows it. */
    status = hcdConnect(hc);
    if (!NT_SUCCESS(status)) {
        /* Mode 3's message not connected (record 15 revision 7) is
         * recorded first; a line connection's own failure is as before. */
        if (hc->PollDelivery == XHCI_POLL_DELIVERY_NONE) {
            hcdLogRefused(hc);
        }
        if (hc->MsgVersion == 0) {
            hc->Interrupt = NULL;
        }
        hcdRelease(hc);
        return status;
    }

    /* The delivery, after the start's zeroing (record 15 section 4); a
     * poll leaves the routing quirk nothing to repair (36.3), nor does a
     * message (the IntDelivery the resource gave). */
    ext->EventDelivery = hc->PollDelivery;
    if (hc->PollDelivery == XHCI_POLL_DELIVERY_POLL) {
        hc->IntDelivery = XHCI_PIRQ_DELIVERY_POLL;
    }

    /* 36.38: stage B's Function Wake notifications (design record 14
     * section 8.7), enabled only with XhciSelectiveSuspend 1, read above;
     * at 0 DNCTRL keeps its reset value and is never written. The in-place
     * reinitializations (a resume's, a recovery's HCRST) keep it: only this
     * start zeroes the extension. */
    /* Selective suspend's completion workers provisioned first (design
     * record 14 section 3.12): one not had turns the switch off here. */
    HcdSuspDoneStart(hc);
    ext->DnctrlEnables = hc->SuspOn ? XHCI_DNCTRL_FUNCTION_WAKE : 0UL;

    /* The poller beside the timers (record 15 section 5.2): served under a
     * poll delivery, watching under a line in mode 0 - never a message,
     * whose IP the message write clears (section 4.1) - closed otherwise.
     * The grant, after D0 is published, arms it (hcd_pnp.c). */
    if (hc->PollDelivery == XHCI_POLL_DELIVERY_POLL) {
        HcdPollOpen(hc, XHCI_POLL_ROLE_SERVE);
    } else if (hc->PollDelivery == XHCI_POLL_DELIVERY_LINE &&
               hc->PollValues.Mode == XHCI_POLL_MODE_AUTO) {
        HcdPollOpen(hc, XHCI_POLL_ROLE_WATCH);
    }

    /* Task 34.3: before the reset XhciInitController performs, so the
     * connectors are the xHCI's when it powers its ports and the root hub
     * first enumerates them. */
    hcdPswStart(hc);

    mp = XhciInitController(ext, &res);
    XhciLogNote(ext, "pci.cfg.status", hc->ConfigLastStatus);
    XhciLogNote(ext, "pci.cfg.info", hc->ConfigLastInformation);
    hcdVersionCounters(hc);
    if (mp != MP_STATUS_SUCCESS) {
        XhciLogNote(ext, "start.refused.step", ext->InitStep);
        XhciLogNote(ext, "start.refused.status", ext->InitStatus);
        if (!XhciStopController(ext)) {
            XhciFailClosedDma(ext);
        }
        /* A refused controller drives nothing: the connectors go back. */
        hcdPswEnd(hc);
        hcdRelease(hc);
        /* The pool HcdSuspDoneStart provisioned (hcd_susp.c). */
        HcdSuspDoneDrain(hc);
        /* No thread ever ran to poll, and the half-built start's fatal
         * status or its stop's mask failure is the reading a refused start
         * leaves; the mirrors are embedded scalars, so the poll is safe
         * after the release. */
        HcdCountersPoll(hc);
        /*
         * Task 38.11, D5 (design record 17 section 4.14.7): every refusal of
         * the initial start, at any step, flushes the ring with the counter
         * dump as a refused delivery does (hcdLogRefused), so with
         * XhciLogDebugView 1 DebugView shows why there was a Code 10 - the
         * version words included - at XhciLogVerbosity 0. Here and not in
         * xhciInitFailed: a resume's or a recovery's reinitialization shares
         * that and may run below PASSIVE_LEVEL, where HcdLogFlush emits
         * nothing, so those keep their reporting as it was.
         */
        HcdLogFlush(hc, XHCI_LOG_REASON_FAILURE, 1);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    hcdTolApplies(hc);
    hcdFirmwareNote(hc);
    /* Task 38.10: the quirks' values, the lists matched against the id just
     * read, and the ASMedia write before anything is enumerated. */
    HcdQuirkStart(hc);
    /* Task 36.3: the route repaired before the first interrupt is enabled. */
    hcdPirqStart(hc);
    XhciLogNote(ext, "reg.machine", hc->RegMachineWon);
    /* Before the thread runs, so no backstop pass reads it mid-write. */
    hc->TolStartGen++;
    (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_STARTED);
    /* Under a poll delivery no interrupt is ever enabled, so
     * XHCI_EXT_FLAG_INTERRUPTS is never set (record 15 section 4). A line
     * clears a leftover PCI Interrupt Disable first; a message has no use
     * for INTx. */
    if (hc->PollDelivery != XHCI_POLL_DELIVERY_POLL) {
        if (hc->PollDelivery == XHCI_POLL_DELIVERY_LINE) {
            hcdIntxEnable(hc);
        }
        ext->InterruptEnables++;
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        XhciEnableInterrupts(ext);
        KeLowerIrql(raised);
    }
    XhciLogNote(ext, "imod.interval", ext->ImodInterval);
    XhciLogNote(ext, "imod.readback", ext->ImodReadback);
    XhciLogNoteAddress(ext, "start.ok", (ULONG)ext->ResourceBase);

    /* Selective suspend's callback thread (design record 14 section 6.1,
     * task 36.32), while the switch is on, before the enum thread that
     * queues to it; without it no idle callback is called and stage A
     * otherwise runs. */
    if (!NT_SUCCESS(HcdSuspCbStart(hc))) {
        XhciLogNote(ext, "susp.cbthread.failed", 1);
    }
    status = hcdThreadStart(hc);
    if (!NT_SUCCESS(status)) {
        hcdStopBody(hc);
        return status;
    }
    HcdFrameTimerStart(hc);
    HcdTolClockStart(hc);
    hc->ControllerStarted = 1;
    return STATUS_SUCCESS;
}

/*
 * usbport's stop order: DisableInterrupts, then StopController (which halts,
 * proves DMA stopped or says it could not), then the resources. Safe to call
 * on a controller that is not started. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdStopBody(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL raised;
    KIRQL oldIrql;

    XHCI_DBG_TEXT("hcd: stop controller");
    ext = &hc->Hc;
#if defined(XHCI_FLAVOUR_QEMU)
    /* Leg T3's reading (record 15 section 7.2.12): each child Dx still
     * pended at the stop's entry, before the bar. */
    HcdSuspQemuStopNote(hc);
#endif
    /* The finer timer first of all (record 15 section 7.2.5): stopped and
     * barred, so its thread gives a held request back at once; joined
     * late, below. */
    HcdTmrStopEarly(hc);
    XhciControllerLockAcquire(ext, &oldIrql);
    /* Selective suspend's epoch and admission (design record 14 section
     * 4.4): the stop's device teardown settles every slot at its unlink. */
    HcdSuspControllerDownLocked(hc);
    XhciControllerLockRelease(ext, oldIrql);
    hcdThreadStop(hc);
    /* Record 15 section 4.2: the poller's service revoked and a moved
     * line's owed retirement contained by the stop itself, before anything
     * is disabled or disconnected; still owed, it is recorded uncontained. */
    HcdPowerGateEnter(hc);
    HcdPollRevoke(hc);
    /* Closed before the check, so no watcher can move behind it; a grant
     * meanwhile finds it CLOSED and arms nothing (record 15 section 5.2). */
    HcdPollClose(hc);
    (VOID)HcdPollContain(hc);
    HcdPowerGateLeave(hc);
    if (ext->LineMoved && !ext->LineRetired) {
        ext->LineRetireState = XHCI_POLL_RETIRED_UNCONTAINED;
        hc->Counters.LineRetireState = XHCI_POLL_RETIRED_UNCONTAINED;
        XhciLogNote(ext, "intr.retire.uncontained", 1);
    }
    /*
     * Whenever a register window is mapped, not only when INITIALIZED is
     * set: the quiesce and a failed in-place recovery clear INITIALIZED with
     * the controller possibly still running, and XhciStopController carries
     * the admission checks of its own steps (task 26-A.2).
     * XhciDisableInterrupts' contract is DISPATCH_LEVEL - the
     * level usbport called it at.
     */
    if (hc->BarVa != NULL && ext->ResourceBase != 0) {
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        XhciDisableInterrupts(ext);
        KeLowerIrql(raised);
        if (!XhciStopController(ext)) {
            XhciFailClosedDma(ext);
        }
    }
    /* Task 34.3: once the controller has halted, so the disconnects the
     * hand-back makes are seen by no running xHCI. */
    hcdPswEnd(hc);
    /*
     * The devices go once the controller has halted, not before: freeing a
     * record completes its URBs (hcd_io.c, HcdIoDeviceGone), and a client
     * buffer must not go back while a TD on a running ring can still DMA
     * into it. Before the DMA adapter goes, since completion gives map
     * registers back.
     */
    HcdEnumDrop(hc);
    /* The idle callback thread's handoff, once the enum thread has exited
     * and the devices' slots are settled at their unlink (design record 14
     * section 4.4). */
    HcdSuspCbStop(hc);
    /* The thread's deferred completions done before anything of the
     * controller goes (hcd_susp.c). */
    HcdSuspDoneDrain(hc);
    /* The finer timer's thread joined (record 15 section 7.2.5): every
     * child D-IRP this controller held is completed and every completion
     * worker has left, and no power path takes the door gate held here,
     * so the join waits behind no power IRP; before the release, outside
     * the power gate. */
    HcdTmrJoin(hc);
    (VOID)XhciControllerUpdateFlags(ext,
                                    XHCI_EXT_FLAG_STARTED |
                                        XHCI_EXT_FLAG_INTERRUPTS |
                                        XHCI_EXT_FLAG_INITIALIZED,
                                    0);
    if (hc->BarVa != NULL || hc->Dma != NULL || hc->Interrupt != NULL ||
        hc->MsgVersion != 0) {
        hcdRelease(hc);
    }
    /* The thread's last poll ran before the disable above. */
    HcdCountersPoll(hc);
    hc->ControllerStarted = 0;
}

/*
 * The start and the stop, each under the door gate (hcd_door.c), so an
 * XHCISNAP window or a controller-information request never reads an
 * extension the start is zeroing or a BAR the stop is unmapping. The
 * controller thread never takes the gate, so the stop may wait for it
 * there. IRQL: PASSIVE_LEVEL.
 */
NTSTATUS HcdStartController(PHCD_CONTROLLER hc, PIRP irp)
{
    NTSTATUS status;

    HcdDoorGateEnter(hc);
    status = hcdStartBody(hc, irp);
    /* The start-decision event on every outcome (design record 18 section
     * 9): the grant as the resources gave it (0 none, 1 line, 2 message),
     * the final delivery and reason, and the status. */
    HcdMsiTraceStart(hc,
                     (hc->MsgDescriptors != 0) ? 2UL
                                               : (hc->HaveInterrupt ? 1UL : 0UL),
                     status);
    HcdDoorGateLeave(hc);
    return status;
}

/* The stop's flush carries the counter block, and it is the last: the
 * thread has gone and the next start zeroes the ring. IRQL: PASSIVE_LEVEL. */
VOID HcdStopController(PHCD_CONTROLLER hc)
{
    HcdDoorGateEnter(hc);
    hcdStopBody(hc);
    HcdLogFlush(hc, XHCI_LOG_REASON_STOP, 1);
    HcdDoorGateLeave(hc);
}
