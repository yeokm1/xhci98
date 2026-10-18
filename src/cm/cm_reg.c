/*
 * cm_reg.c - the registry values of record 21 section 8, read once per
 * start from the NHI's driver (software) key: the key every install path
 * of xhcicm.inf and xhcicm-amd64.inf writes them to (HKR in the install
 * section's AddReg) at the defaults cm_vals.c holds, under
 * FLG_ADDREG_NOCLOBBER. The one file that opens a registry key, so the
 * import allowlists hold those imports to it (SITES=cm_reg.obj).
 *
 * The rules each value is held to are cm_vals.c's; this file only fetches.
 * A value absent, unreadable or of another type is "not found" and its
 * default stands, which is also what the written default reads as. The
 * two values no INF writes, XhciCmTunnelBridge and XhciCmBridgeNumbering,
 * are read from the same key.
 *
 * From 3.1.0.0 every value is read first from the machine-wide key
 * HKLM\System\CurrentControlSet\Services\USB, the one xhci98.sys reads
 * too, and from the driver key only when it is not there in any type: the
 * machine-wide value wins when present (task 42-A.17, ..\xhci_regsrc.h).
 * The machine-wide key is read even when the driver key cannot be opened.
 *
 * IRQL: PASSIVE_LEVEL, in the system thread PnP's start runs in, as
 * IoOpenDeviceRegistryKey needs.
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
    L"XhciCmLogDebugView"
};

/* Not written by any INF (record 21 section 8): absent is no override. */
#define CM_VALUE_TUNNEL_BRIDGE L"XhciCmTunnelBridge"
/* Not written by any INF either (record 21 section 6.1.1): absent is the
 * default, CM_BRIDGE_NUMBERING_DEFAULT. */
#define CM_VALUE_BRIDGE_NUMBERING L"XhciCmBridgeNumbering"

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

VOID CmReadValues(PCM_NHI nhi)
{
    HANDLE key;
    CM_TAKE take;
    ULONG i;

    CmValuesDefault(&nhi->Values);
    nhi->ValuesRead = 1;
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
    if (key != NULL) {
        ZwClose(key);
    }
}
