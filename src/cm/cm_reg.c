/*
 * cm_reg.c - the registry values of record 21 section 8, read once per
 * start from the NHI's driver (software) key: the key every install path
 * of xhcicm.inf and xhcicm-amd64.inf writes them to (HKR in the install
 * section's AddReg) at the defaults cm_vals.c holds, under
 * FLG_ADDREG_NOCLOBBER. One of the two files that open a registry key
 * through the kernel's imports, with cm_statos.c (the status record and
 * the eject's values), so the import allowlists hold those imports to
 * them (SITES=cm_reg.obj,cm_statos.obj); cm_nvlinkos.c's 9x keys go
 * through the VMM's registry services, which are no import.
 *
 * The rules each value is held to are cm_vals.c's; this file only fetches.
 * A value absent, unreadable or of another type is "not found" and its
 * default stands, which is also what the written default reads as.
 * XhciCmTunnelBridge (written as "auto" since task 45.50), the two values
 * only the 98 SE and ME install writes, XhciCmBridgeNumbering and
 * XhciCmNoIrqStart (task 44.46), and the values outside the rule table
 * below are read from the same key.
 *
 * From 3.1.0.0 every value is read first from the machine-wide key
 * HKLM\System\CurrentControlSet\Services\USB, the one xhci98.sys reads
 * too, and from the driver key only when it is not there in any type: the
 * machine-wide value wins when present (task 42-A.17, ..\xhci_regsrc.h).
 * The machine-wide key is read even when the driver key cannot be opened.
 *
 * Since task 44.53 it also reads, once in DriverEntry on the NT role, the
 * gate of design record 21 section 6.2.1 (CmReadIrqGate): the HAL's
 * hardware id, from the machine's enumeration key. Since task 45.12 the
 * gate of design record 33 (CmReadBreqGate, NT 5.x alone from the shared
 * data page) and, where it is open, XhciCmBridgeReq.
 *
 * IRQL: PASSIVE_LEVEL, in the system thread PnP's start runs in, as
 * IoOpenDeviceRegistryKey needs (DriverEntry's thread for the gate).
 */

#include "cm.h"
#include "../xhci_regsrc.h"

/* Relative to RTL_REGISTRY_SERVICES (hcd_ctl.c's HCD_MACHINE_KEY). */
#define CM_MACHINE_KEY L"USB"

static const PCWSTR cmValueNames[CM_VAL_COUNT] = {
    L"XhciCmMode",
    L"XhciCmApprove",
    L"XhciCmReenum",
    L"XhciCmPollIdleMs",
    L"XhciCmCmdTimeoutMs",
    L"XhciCmApproveTimeoutMs",
    L"XhciCmSafeEject",
    L"XhciCmLogVerbosity",
    L"XhciCmUsb3Tunnel",
    L"XhciCmLaneBonding",
    L"XhciCmPreOsCleanup",
    L"XhciCmLogDebugView",
    L"XhciCmNet",
    L"XhciCmNetPollMs",
    L"XhciCmNetRxFrames",
    L"XhciCmNetTxFrames",
    L"XhciCmNetE2E"
};

/* Written as "auto" since task 45.50 (record 21 section 8.1): "auto",
 * absent and empty are no override. */
#define CM_VALUE_TUNNEL_BRIDGE L"XhciCmTunnelBridge"
/* Written by the 98 SE and ME install alone (record 21 section 6.1.1,
 * task 44.46): absent is the default, CM_BRIDGE_NUMBERING_DEFAULT, as is
 * XhciCmNoIrqStart's absence CM_NOIRQ_START_DEFAULT. */
#define CM_VALUE_BRIDGE_NUMBERING L"XhciCmBridgeNumbering"
#define CM_VALUE_NOIRQ_START L"XhciCmNoIrqStart"
/* Record 22 section 18.8: absent is 0, as written since task 45.50. */
#define CM_VALUE_ASM_PCIE7 L"XhciCmAsmediaPcie7Credits"
/* Record 22 section 12, decision U30: absent is 1, as written since task
 * 45.50. */
#define CM_VALUE_USB4_V2 L"XhciCmUsb4V2"
#define CM_VALUE_GEN4_ELR L"XhciCmGen4LinkRecovery"
/* Record 33 section 9.1: absent is 1, as the NT 5.x install paths write it
 * since task 45.50; read on NT 5.x alone. XhciCmRingInterrupt: absent is
 * 0, as the NT install paths write it. */
#define CM_VALUE_BRIDGE_REQ L"XhciCmBridgeReq"
#define CM_VALUE_RING_IRQ L"XhciCmRingInterrupt"
#define CM_VALUE_PLAT_HANDOVER L"XhciCmAsm4242Handover"
#define CM_VALUE_HO_STRUCTURAL L"XhciCmAsm4242Structural"
#define CM_VALUE_HO_READS      L"XhciCmAsm4242Reads"

typedef struct _CM_TAKE {
    ULONG Seen;                 /* the routine was called with a value    */
    ULONG Found;                /* of the type taken, in Dword or Text    */
    ULONG Dword;
    WCHAR Text[CM_TUNNEL_BRIDGE_CHARS + 1];
    ULONG TextChars;
} CM_TAKE, *PCM_TAKE;

/* A REG_DWORD, and nothing else, is taken. */
static NTSTATUS NTAPI cmTakeDword(PWSTR ValueName, ULONG ValueType,
                                  PVOID ValueData, ULONG ValueLength,
                                  PVOID Context, PVOID EntryContext)
{
    PCM_TAKE take;
    PUCHAR from;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    take = (PCM_TAKE)EntryContext;
    if (!XhciRegCallSeen(ValueType, ValueLength)) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    take->Seen = 1;
    if (ValueType != REG_DWORD || ValueLength != sizeof(ULONG) ||
        ValueData == NULL) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    from = (PUCHAR)ValueData;
    take->Dword = (ULONG)from[0] | ((ULONG)from[1] << 8) |
                  ((ULONG)from[2] << 16) | ((ULONG)from[3] << 24);
    take->Found = 1;
    return STATUS_SUCCESS;
}

/* A REG_SZ, copied up to one character past what CmValuesTakeBridge
 * accepts, so that an over-long value reaches it as over-long. */
static NTSTATUS NTAPI cmTakeText(PWSTR ValueName, ULONG ValueType,
                                 PVOID ValueData, ULONG ValueLength,
                                 PVOID Context, PVOID EntryContext)
{
    PCM_TAKE take;
    const WCHAR *from;
    ULONG chars;
    ULONG i;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    take = (PCM_TAKE)EntryContext;
    if (!XhciRegCallSeen(ValueType, ValueLength)) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    take->Seen = 1;
    if (ValueType != REG_SZ || ValueData == NULL) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    from = (const WCHAR *)ValueData;
    chars = ValueLength / sizeof(WCHAR);
    while (chars != 0 && from[chars - 1] == 0) {
        chars--;
    }
    if (chars > CM_TUNNEL_BRIDGE_CHARS) {
        chars = CM_TUNNEL_BRIDGE_CHARS;
    }
    for (i = 0; i < chars; i++) {
        take->Text[i] = from[i];
    }
    take->Text[chars] = 0;
    take->TextChars = chars;
    take->Found = 1;
    return STATUS_SUCCESS;
}

/* One value from one key, `relativeTo` and `path` as RtlQueryRegistryValues
 * takes them; Seen and Found are the routine's, whatever status the query
 * returns. */
static VOID cmQueryAt(ULONG relativeTo, PWSTR path, PCWSTR name,
                      PCM_TAKE take, ULONG text)
{
    RTL_QUERY_REGISTRY_TABLE table[2];

    CmZero(table, sizeof(table));
    CmZero(take, sizeof(CM_TAKE));
    table[0].QueryRoutine = text ? cmTakeText : cmTakeDword;
    /* NOEXPAND: a value mistakenly stored as REG_EXPAND_SZ reaches the
     * type check as it is (hcd_ctl.c, hcdReadDword). */
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = (PWSTR)name;
    table[0].EntryContext = take;
    (VOID)RtlQueryRegistryValues(relativeTo, path, table, NULL, NULL);
}

/* One value as it stands: the machine-wide key's reading when the value is
 * there in any type, else the driver key's (`key` NULL when that did not
 * open). Found only for the type taken. */
static VOID cmQuery(HANDLE key, PCWSTR name, PCM_TAKE take, ULONG text)
{
    ULONG machineState;
    ULONG deviceState;

    cmQueryAt(RTL_REGISTRY_SERVICES, (PWSTR)CM_MACHINE_KEY, name, take, text);
    machineState = XhciRegState(take->Seen, take->Found);
    deviceState = XHCI_REG_ABSENT;
    if (machineState == XHCI_REG_ABSENT) {
        if (key != NULL) {
            cmQueryAt(RTL_REGISTRY_HANDLE, (PWSTR)key, name, take, text);
        } else {
            CmZero(take, sizeof(CM_TAKE));
        }
        deviceState = XhciRegState(take->Seen, take->Found);
    }
    if (XhciRegPickedState(machineState, deviceState) != XHCI_REG_TAKEN) {
        take->Found = 0;
    }
}

/* ---- The interrupt gate (design record 21 section 6.2.1) ---- */

ULONG CmIrqGate;

/* The HAL devnode's hardware ids: a REG_MULTI_SZ (read whole, with
 * RTL_QUERY_REGISTRY_NOEXPAND) or a REG_SZ; the first string tested. */
#define CM_HAL_KEY \
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\Root\\PCI_HAL\\0000"
#define CM_HAL_CHARS 16UL

static NTSTATUS NTAPI cmTakeHal(PWSTR ValueName, ULONG ValueType,
                                PVOID ValueData, ULONG ValueLength,
                                PVOID Context, PVOID EntryContext)
{
    PULONG std;
    USHORT ids[CM_HAL_CHARS];
    const USHORT *from;
    ULONG chars;
    ULONG i;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    std = (PULONG)EntryContext;
    if ((ValueType != REG_MULTI_SZ && ValueType != REG_SZ) ||
        ValueData == NULL) {
        return STATUS_SUCCESS;
    }
    from = (const USHORT *)ValueData;
    chars = ValueLength / sizeof(USHORT);
    if (chars > CM_HAL_CHARS) {
        chars = CM_HAL_CHARS;
    }
    for (i = 0; i < chars; i++) {
        ids[i] = from[i];
    }
    *std = (ULONG)CmIrqHalStdPc(ids, chars);
    return STATUS_SUCCESS;
}

/* ---- The bridge requirements' gate (design record 33 section 4.4) ---- */

ULONG CmBreqGate;

VOID CmReadBreqGate(VOID)
{
    CmBreqGate = 0;
    if (SharedUserData->NtMajorVersion == 5) {
        /* The minor version plus one: 1 on 2000, 2 on XP, 3 on XP x64 and
         * Server 2003. */
        CmBreqGate = SharedUserData->NtMinorVersion + 1UL;
    }
}

VOID CmReadIrqGate(VOID)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    ULONG std;

    CmIrqGate = 0;
    if (SharedUserData->NtMajorVersion != 5 ||
        SharedUserData->NtMinorVersion != 0) {
        return;
    }
    std = 0;
    CmZero(table, sizeof(table));
    table[0].QueryRoutine = cmTakeHal;
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = L"HardwareID";
    table[0].EntryContext = &std;
    (VOID)RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, (PWSTR)CM_HAL_KEY,
                                 table, NULL, NULL);
    CmIrqGate = std;
}

VOID CmReadValues(PCM_NHI nhi)
{
    HANDLE key;
    CM_TAKE take;
    ULONG i;
    BOOLEAN admitted;

    CmValuesDefault(&nhi->Values);
    nhi->ValuesRead = 1;
    /* 98 SE and ME away from S0 (task 46.14): no registry access, the
     * defaults stand (such a start goes no further than inert). */
    admitted = CmRegEnter(nhi);
    if (!admitted) {
        /* Not read: this start activates nothing (CmNhiStartWork). */
        nhi->ValuesRead = 0;
        CmLogNote(nhi, "cm.values.deferred", (ULONG)nhi->SystemPower);
        return;
    }
    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(nhi->Common.Pdo,
                                            PLUGPLAY_REGKEY_DRIVER, KEY_READ,
                                            &key))) {
        key = NULL;
    }
    for (i = 0; i < CM_VAL_COUNT; i++) {
        cmQuery(key, cmValueNames[i], &take, 0);
        CmValuesTake(&nhi->Values, i, take.Found, take.Dword);
    }
    cmQuery(key, CM_VALUE_TUNNEL_BRIDGE, &take, 1);
    if (take.Found) {
        CmValuesTakeBridge(&nhi->Values, take.Text, take.TextChars);
    }
    cmQuery(key, CM_VALUE_BRIDGE_NUMBERING, &take, 0);
    CmValuesTakeBridgeNumbering(&nhi->Values, take.Found, take.Dword);
    cmQuery(key, CM_VALUE_NOIRQ_START, &take, 0);
    CmValuesTakeNoIrqStart(&nhi->Values, take.Found, take.Dword);
    cmQuery(key, CM_VALUE_ASM_PCIE7, &take, 0);
    CmValuesTakeAsmPcie7(&nhi->Values, take.Found, take.Dword);
    cmQuery(key, CM_VALUE_RING_IRQ, &take, 0);
    CmValuesTakeRingIrq(&nhi->Values, take.Found, take.Dword);
    /* The platform's USB4 handover's values (record 32 sections 12.8.6,
     * 12.8.15.10 and 12.8.15.17): the master switch and the structural
     * tier on every target (on NT the tier applies to the read path
     * alone); the read path's opt-in on NT alone. */
    cmQuery(key, CM_VALUE_PLAT_HANDOVER, &take, 0);
    CmValuesTakePlatformHandover(&nhi->Values, take.Found, take.Dword);
    cmQuery(key, CM_VALUE_HO_STRUCTURAL, &take, 0);
    CmValuesTakeHandoverStructural(&nhi->Values, take.Found, take.Dword);
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        cmQuery(key, CM_VALUE_HO_READS, &take, 0);
        CmValuesTakeHandoverReads(&nhi->Values, take.Found, take.Dword);
    } else {
        CmValuesTakeHandoverReads(&nhi->Values, 0, 0);
    }
#if defined(XHCI_FLAVOUR_QEMU)
    cmQuery(key, L"XhciCmQemuIrqDead", &take, 0);
    nhi->Values.QemuIrqDead = take.Found ? take.Dword : 0;
    cmQuery(key, L"XhciCmQemuFailD0", &take, 0);
    nhi->QemuFailD0 = take.Found ? take.Dword : 0;
    cmQuery(key, L"XhciCmQemuFailReenum", &take, 0);
    nhi->QemuFailReenum = take.Found ? take.Dword : 0;
    cmQuery(key, L"XhciCmQemuOscNoClear", &take, 0);
    nhi->QemuOscNoClear = take.Found ? take.Dword : 0;
    cmQuery(key, L"XhciCmQemuEcamFault", &take, 0);
    nhi->QemuEcamFault = take.Found ? take.Dword : 0;
#endif
    cmQuery(key, CM_VALUE_USB4_V2, &take, 0);
    CmValuesTakeUsb4V2(&nhi->Values, take.Found, take.Dword);
    cmQuery(key, CM_VALUE_GEN4_ELR, &take, 0);
    CmValuesTakeGen4Elr(&nhi->Values, take.Found, take.Dword);
    if (CmBreqGate != 0) {
        cmQuery(key, CM_VALUE_BRIDGE_REQ, &take, 0);
        CmValuesTakeBridgeReq(&nhi->Values, take.Found, take.Dword);
    }
    if (key != NULL) {
        ZwClose(key);
    }
    CmRegLeave(nhi);
}
