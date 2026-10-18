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
 * IRQL: PASSIVE_LEVEL, in the system thread PnP's start runs in, as
 * IoOpenDeviceRegistryKey needs.
 */

#include "cm.h"

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
    ULONG Found;
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
    if (ValueType != REG_DWORD || ValueLength != sizeof(ULONG)) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    take = (PCM_TAKE)EntryContext;
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
    if (ValueType != REG_SZ) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    take = (PCM_TAKE)EntryContext;
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

static VOID cmQuery(HANDLE key, PCWSTR name, PCM_TAKE take, ULONG text)
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
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_HANDLE, (PWSTR)key,
                                           table, NULL, NULL))) {
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
        return;
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
    ZwClose(key);
}
