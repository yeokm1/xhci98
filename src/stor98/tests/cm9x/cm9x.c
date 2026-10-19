/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026, Yeo Kheng Meng
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * cm9x.sys - test stand-in for xhci98's connection manager on Windows 98 SE
 * and Me (stor98 docs/gates/Q-9x.md; include/stor98cm.h interface 3, rules
 * R1-R12). WDM function driver of the usb4-emu usb4-host devnode
 * (DevLoader=*ntkern), test only, i440FX generic profile, one or two tunnel
 * ports (tunnel bridges at BridgeDev and BridgeDev1).
 *
 * What it does, all from one PASSIVE_LEVEL system thread (the one CM
 * writer, R6b):
 *  - allocates the never-freed STOR98LS_TABLE (non-paged pool) with a
 *    per-boot cookie, creates the current-boot proof (dynamic key
 *    HKEY_DYN_DATA\Stor98LinkState with REG_DWORD BootCookie and REG_BINARY
 *    TableLinear {linear, 272, cookie}, answered by its own resident provider;
 *    refused if the key already exists) and then the locator
 *    (HKLM\System\CurrentControlSet\Services\stor98 "LinkState"), R3;
 *  - per port: finds the tunnel bridge with CONFIGMG and polls its SHPC slot
 *    status through CONFIGMG_Call_Enumerator_Function (PCI.H). usb4-emu marks
 *    the slot empty in stage 3 of a tunnel down, after stage 2 (endpoint
 *    reset and drain), so "slot empty" stands for "the CM has completed the
 *    teardown";
 *  - slot occupied: LinkGeneration++, Flags = IN_USE | UP, then
 *    Reenumerate_DevNode on the bridge, but only once the port's previous
 *    endpoint devnode is retired (R8c; a veto defers the new UP);
 *  - slot empty: Flags = IN_USE | DOWN_QUIESCED, wait for AckGeneration (R12
 *    S4: LinkAckMs, default 3000, never below STOR98LS9X_CM_LINK_WAIT_MS),
 *    then Query_Remove_SubTree / Remove_SubTree of the endpoint devnode,
 *    retried every 2 s while vetoed (never forced, never blocking the thread);
 *  - eject request (HKLM\Software\stor98\Cm9x "EjectRequest" for port 0,
 *    "EjectRequest1" for port 1, set by cm9xctl.exe): R11 (query, request,
 *    validated acknowledgement, Remove_SubTree only on CLEAN; D2 recovery
 *    otherwise). The CM's own path teardown is debug-port stage 0x1CF (port 0)
 *    or 0x1B5 (port 1), which a harness rule "at=stage code=.. act=surprise"
 *    turns into a tunnel down.
 *  - boot-resident (R4): no DriverUnload; IRP_MN_QUERY_REMOVE of its device
 *    is refused while the table is published; a forced removal deletes the
 *    device object but leaves the table, provider and thread running.
 *
 * VMM, CONFIGMG and PCI-enumerator services are documented Windows 98 DDK
 * VxD services, called through the INT 20h dynalink from the system thread
 * (decision D1).
 *
 * Parameters (HKLM\Software\stor98\Cm9x, REG_DWORD, read at each event):
 * BridgeDev (5), BridgeDev1 (6; 0 = one port), EndpointDev (1), PollMs (10),
 * Mode (1 = early statement at stage 1, negative test), NoFlag (1 = DOWN
 * without QUIESCED), DelayMs, LinkAckMs (3000), AckTimeoutMs (0 = R11 bound),
 * CancelAfterAck (treat a CLEAN acknowledgement as cancelled: D2 recovery),
 * NoRemove, Teardown (1), SkipQuery (1 = REMOVE_PENDING without
 * Query_Remove_SubTree: I/O-after-acknowledgement test), DelayRemoveMs,
 * Overlap (1 = a second request 300 ms after the first; 2 = the same request
 * re-published after the acknowledgement), Hostile (malformed rendezvous or
 * entry for the next UP: see ApplyHostile), NoProof (1 = publish the table and
 * the locator but no current-boot proof key, as xhci98's 9x CM 3.0.0.0 does:
 * every stor98 load then runs without the link-state channel, read once at
 * DriverEntry). Results: EjectResult[1], LastCr.
 *
 * Every step is logged to the usb4-emu debug port (stage codes 0x1C7-0x1CF,
 * 0x1B5-0x1B6 of stor98 docs/TESTING.md, with TSC markers) and as a text line
 * to the QEMU debugcon (0xE9): "cm9x <TSC> <code> <arg> <text> <a> <b>".
 */

#include <wdm.h>
#include "..\..\include\stor98cm.h"

#define DBG_BASE        0x5A0
#define DBGCON          0xE9

/*
 * Stage codes 0x1C7-0x1CF and 0x1B5-0x1B6 (int-0.2 renumbering: the request
 * engine owns 0xB5-0xBF and 0xC7-0xDC of the unified table, so the CM
 * stand-in moved up by 0x100; docs/TESTING.md, docs/INTEGRATION-0.2.md).
 * The debug port takes 32-bit codes. Traces made before the move use
 * 0xC7-0xCF and 0xB5-0xB6.
 */
#define STG_CM_ERROR        0x1C7
#define STG_CM_START        0x1C8
#define STG_CM_SLOT         0x1C9   /* arg: slot status */
#define STG_CM_UP           0x1CA   /* arg: port << 24 | bus << 16 | generation (15:0) */
#define STG_CM_DOWN         0x1CB   /* arg: port << 24 | generation | flags << 16 */
#define STG_CM_ACK          0x1CC   /* arg: 1 acked | (ms & 0xFFFFF) << 8 | port << 28 */
#define STG_CM_CONFIGMG     0x1CD   /* arg: call << 24 | port << 16 | cr (15:0) */
#define STG_CM_REMOVE       0x1CE   /* arg: step << 24 | outcome << 16 | sequence (15:0); */
                                    /* step 3: step << 24 | port << 20 | sequence (19:0) */
#define STG_CM_TEARDOWN     0x1CF   /* harness: take tunnel 0 down */
#define STG_CM_TEARDOWN1    0x1B5   /* harness: take tunnel 1 down */
#define STG_CM_DIAG         0x1B6   /* hostile modes, dumps, lifetime */

#define SHPC_CAP_ID             0x0C
#define SHPC_SLOT1_STATUS_DW    9       /* SHPC working register 0x24 (slot 1) */

#define CR_SUCCESS              0
#define CM_QUERY_REMOVE_UI_NOT_OK   1
#define CM_REMOVE_UI_NOT_OK     1
#define PCI_ENUM_FUNC_GET_DEVICE_INFO   0
#define PCI_ENUM_FUNC_SET_DEVICE_INFO   1

#define VMM_ID          0x0001
#define CONFIGMG_ID     0x0033

#define HKLM    0x80000002
#define HKDD    0x80000006
#define PR_SYSTEM   0x80080000

typedef ULONG (__cdecl *VXDFN)();

/* One dynalink thunk per service: CD 20 <service | 8000h> <device>. */
typedef struct _THUNKS {
    UCHAR GetSystemTime[8];
    UCHAR RegCreateKey[8];
    UCHAR RegOpenKey[8];
    UCHAR RegCloseKey[8];
    UCHAR RegQueryValueEx[8];
    UCHAR RegSetValueEx[8];
    UCHAR RegCreateDynKey[8];
    UCHAR PageReserve[8];
    UCHAR CmGetVersion[8];
    UCHAR CmLocate[8];
    UCHAR CmGetChild[8];
    UCHAR CmGetSibling[8];
    UCHAR CmGetDeviceId[8];
    UCHAR CmQueryRemove[8];
    UCHAR CmRemove[8];
    UCHAR CmReenumerate[8];
    UCHAR CmGetStatus[8];
    UCHAR CmCallEnum[8];
} THUNKS;

typedef struct _PORT {
    ULONG Index;
    ULONG BridgeDev;
    ULONG BridgeDn;
    ULONG SecBus;
    ULONG ShpcCap;
    PSTOR98LS_ENTRY E;
    BOOLEAN Up;             /* a tunnel is published UP */
    BOOLEAN DevnodeLive;    /* an endpoint devnode may exist (not yet retired) */
    BOOLEAN RemovePending;  /* removal of that devnode in progress (vetoes retried) */
    BOOLEAN Early;
    BOOLEAN DeferLogged;
    ULONG NextTryMs;
    ULONG Tries;
    ULONG LastSt;
    ULONG EjectSeen;
    ULONG ReqSeq;           /* 24-bit request counter (R9a) */
    ULONG St;               /* PS_* per-port state (R6e) */
    ULONG T0;               /* state entry, VMM ms */
    ULONG Deadline;
    ULONG Seq, Seq2;        /* request in flight (Seq2: overlap/retry test) */
    ULONG Gen;              /* generation the statement or request names */
    ULONG Dn;               /* endpoint devnode of the eject */
    ULONG Outcome;          /* acknowledged outcome, 0xFF none */
    ULONG Mode;             /* EJ_CLEAN / EJ_ISOLATED: meaning of Remove_SubTree success */
    ULONG H10Phase, H10At;  /* hostile mode 10 timing */
    ULONG StatusAt;         /* when to log the endpoint devnode status (diagnostic), 0 none */
    ULONG SavedSlot;        /* Overlap 3: the entry's SlotNumber before the identity change */
    ULONG RmPhase;          /* ProcessRemove: 0 query next, 1 Remove_SubTree of RmDn running */
    ULONG RmDn;
    BOOLEAN RmLate;         /* a Remove_SubTree result taken after the O6 deadline (RmPhase 2) */
    ULONG RmLateCr;
    ULONG Request;          /* eject request value being started (PS_EJ_QUERY) */
    /* Per-port CONFIGMG worker (R6e): the calls that can block (Query_Remove_SubTree,
     * Remove_SubTree, Reenumerate_DevNode wait on the OS and other drivers) run
     * on this port's own thread, never on the writer. A system-modal message
     * stops both threads alike (R6e residual). Busy/Done: locked exchanges. */
    volatile LONG CallState;    /* CS_*: the only handoff word (R6f) */
    ULONG CallId;               /* operation id of CallOp/CallDn/CallFlags (writer) */
    ULONG DoneId;               /* operation id the worker completed (worker) */
    ULONG DoneAt;               /* VMM ms when the worker completed it (worker) */
    BOOLEAN StallDone;          /* WriterStallMs test applied for this eject */
    ULONG NextId;
    ULONG CallOp, CallDn, CallFlags, CallCr, CallAt;
    BOOLEAN CallLogged;
    ULONG O6At;             /* O6: elapsed 30 s deadline of the removal attempts */
} PORT;

static THUNKS *gT;
static PSTOR98LS_TABLE gTable;      /* never freed (R4) */
static ULONG gCookie, gProofCookie;
static STOR98LS9X_PROOF_TABLE gProofTable;  /* proof value "TableLinear" (R3a, option A) */
static BOOLEAN gPublished;
static PORT gPort[2];
static ULONG gPorts;
static ULONG gEndpointDev = 1, gPollMs = 10, gMode, gNoFlag, gDelayMs, gLinkAckMs = 3000, gAckTimeoutMs;
static ULONG gCancelAfterAck, gNoRemove, gTeardown = 1, gSkipQuery, gDelayRemoveMs, gOverlap, gHostile;
static ULONG gNoProof;
static ULONG gDumpSeen, gHostileActive, gFailRemove, gSlowRemoveMs, gWriterStallMs, gWriterStall2Ms;
static BOOLEAN gThreadStarted;
static volatile LONG gParked;          /* system shutdown seen: no more CONFIGMG polling */
static PDEVICE_OBJECT gPin;            /* non-PnP device object: keeps the image referenced (R4) */

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

static VOID OutB(USHORT port, UCHAR v)
{
    __asm {
        mov dx, port
        mov al, v
        out dx, al
    }
}

static VOID OutD(USHORT port, ULONG v)
{
    __asm {
        mov dx, port
        mov eax, v
        out dx, eax
    }
}

static ULONG InD(USHORT port)
{
    ULONG v;
    __asm {
        mov dx, port
        in eax, dx
        mov v, eax
    }
    return v;
}

static ULONGLONG Tsc(VOID)
{
    ULONG lo, hi;
    __asm {
        _emit 0x0F
        _emit 0x31
        mov lo, eax
        mov hi, edx
    }
    return ((ULONGLONG)hi << 32) | lo;
}

static VOID ConS(const char *s)
{
    while (*s) {
        OutB(DBGCON, (UCHAR)*s++);
    }
}

static VOID ConHex(ULONG v, ULONG digits)
{
    static const char hex[] = "0123456789abcdef";
    ULONG sh = digits * 4;
    while (sh) {
        sh -= 4;
        OutB(DBGCON, (UCHAR)hex[(v >> sh) & 0xF]);
    }
}

/* Stage code (+ optional TSC marker) and a debugcon line with up to two values. */
static VOID Log(ULONG code, ULONG arg, BOOLEAN mark, const char *text, ULONG a, ULONG b)
{
    ULONGLONG t = Tsc();

    OutD(DBG_BASE + 4, arg);
    OutD(DBG_BASE + 0, code);
    if (mark) {
        OutD(DBG_BASE + 8, (ULONG)t);
        OutD(DBG_BASE + 0xC, (ULONG)(t >> 32));
    }
    ConS("cm9x ");
    ConHex((ULONG)(t >> 32), 8);
    ConHex((ULONG)t, 8);
    ConS(" ");
    ConHex(code, 3);
    ConS(" ");
    ConHex(arg, 8);
    ConS(" ");
    ConS(text);
    ConS(" ");
    ConHex(a, 8);
    ConS(" ");
    ConHex(b, 8);
    ConS("\n");
}

static VOID LogStr(const char *text, const char *s)
{
    ULONGLONG t = Tsc();
    ConS("cm9x ");
    ConHex((ULONG)(t >> 32), 8);
    ConHex((ULONG)t, 8);
    ConS(" -- ");
    ConS(text);
    ConS(" ");
    ConS(s);
    ConS("\n");
}

/* ------------------------------------------------------------------ */
/* Atomic helpers (R6b): locked instructions, also compiler barriers    */
/* ------------------------------------------------------------------ */

static VOID LockedInc(volatile ULONG *p)
{
    __asm {
        mov ecx, p
        lock inc dword ptr [ecx]
    }
}

static ULONG Load(volatile ULONG *p)
{
    ULONG v;
    __asm {
        mov ecx, p
        mov eax, [ecx]
        mov v, eax
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* VxD services                                                        */
/* ------------------------------------------------------------------ */

static VOID MakeThunk(UCHAR *t, ULONG device, ULONG service)
{
    t[0] = 0xCD;
    t[1] = 0x20;
    t[2] = (UCHAR)(service & 0xFF);
    t[3] = (UCHAR)(((service >> 8) & 0xFF) | 0x80);
    t[4] = (UCHAR)(device & 0xFF);
    t[5] = (UCHAR)((device >> 8) & 0xFF);
    t[6] = 0x90;
    t[7] = 0x90;
}

#define VMMCALL(f)  ((VXDFN)(PVOID)gT->f)

static BOOLEAN ThunksInit(VOID)
{
    gT = (THUNKS *)ExAllocatePoolWithTag(NonPagedPool, sizeof(THUNKS), '9mcN');
    if (gT == NULL) {
        return FALSE;
    }
    MakeThunk(gT->GetSystemTime, VMM_ID, 0x003F);
    MakeThunk(gT->RegOpenKey, VMM_ID, 0x0148);
    MakeThunk(gT->RegCloseKey, VMM_ID, 0x0149);
    MakeThunk(gT->RegCreateKey, VMM_ID, 0x014A);
    MakeThunk(gT->RegQueryValueEx, VMM_ID, 0x0151);
    MakeThunk(gT->RegSetValueEx, VMM_ID, 0x0152);
    MakeThunk(gT->RegCreateDynKey, VMM_ID, 0x0176);
    MakeThunk(gT->PageReserve, VMM_ID, 0x011D);
    MakeThunk(gT->CmGetVersion, CONFIGMG_ID, 0x0000);
    MakeThunk(gT->CmLocate, CONFIGMG_ID, 0x0002);
    MakeThunk(gT->CmGetChild, CONFIGMG_ID, 0x0004);
    MakeThunk(gT->CmGetSibling, CONFIGMG_ID, 0x0005);
    MakeThunk(gT->CmGetDeviceId, CONFIGMG_ID, 0x0007);
    MakeThunk(gT->CmQueryRemove, CONFIGMG_ID, 0x000C);
    MakeThunk(gT->CmRemove, CONFIGMG_ID, 0x000D);
    MakeThunk(gT->CmReenumerate, CONFIGMG_ID, 0x0033);
    MakeThunk(gT->CmGetStatus, CONFIGMG_ID, 0x0036);
    MakeThunk(gT->CmCallEnum, CONFIGMG_ID, 0x0054);
    return TRUE;
}

static ULONG NowMs(VOID)
{
    return VMMCALL(GetSystemTime)();
}

static ULONG RegDword(ULONG hk, char *name, ULONG def)
{
    ULONG v = 0, type = 0, cb = sizeof(v);
    if (hk && VMMCALL(RegQueryValueEx)(hk, name, NULL, &type, &v, &cb) == 0 && cb == sizeof(v)) {
        return v;
    }
    return def;
}

static VOID RegPutDword(char *name, ULONG v)
{
    ULONG hk = 0;
    if (VMMCALL(RegCreateKey)(HKLM, "Software\\stor98\\Cm9x", &hk) == 0) {
        VMMCALL(RegSetValueEx)(hk, name, 0, 4, &v, sizeof(v));
        VMMCALL(RegCloseKey)(hk);
    }
}

static ULONG ParamDword(char *name, ULONG def)
{
    ULONG hk = 0, v;
    if (VMMCALL(RegOpenKey)(HKLM, "Software\\stor98\\Cm9x", &hk) != 0) {
        return def;
    }
    v = RegDword(hk, name, def);
    VMMCALL(RegCloseKey)(hk);
    return v;
}

static VOID ReadParams(VOID)
{
    ULONG hk = 0;
    if (VMMCALL(RegOpenKey)(HKLM, "Software\\stor98\\Cm9x", &hk) != 0) {
        hk = 0;
    }
    gEndpointDev = RegDword(hk, "EndpointDev", 1);
    gPollMs = RegDword(hk, "PollMs", 10);
    gMode = RegDword(hk, "Mode", 0);
    gNoFlag = RegDword(hk, "NoFlag", 0);
    gDelayMs = RegDword(hk, "DelayMs", 0);
    gLinkAckMs = RegDword(hk, "LinkAckMs", 3000);
    gAckTimeoutMs = RegDword(hk, "AckTimeoutMs", 0);
    gCancelAfterAck = RegDword(hk, "CancelAfterAck", 0);
    gNoRemove = RegDword(hk, "NoRemove", 0);
    gTeardown = RegDword(hk, "Teardown", 1);
    gSkipQuery = RegDword(hk, "SkipQuery", 0);
    gDelayRemoveMs = RegDword(hk, "DelayRemoveMs", 0);
    gOverlap = RegDword(hk, "Overlap", 0);
    gHostile = RegDword(hk, "Hostile", 0);
    gSlowRemoveMs = RegDword(hk, "SlowRemoveMs", 0);    /* test: worker delays each Remove_SubTree */
    gWriterStallMs = RegDword(hk, "WriterStallMs", 0);  /* test: writer stalls across the O6 deadline */
    gWriterStall2Ms = RegDword(hk, "WriterStall2Ms", 0); /* test: same, after the deadline was sampled */
    if (RegDword(hk, "FailRemove", 0)) {
        gFailRemove = RegDword(hk, "FailRemove", 0);    /* consumed by the next ejects */
        if (hk) {
            ULONG z = 0;
            VMMCALL(RegSetValueEx)(hk, "FailRemove", 0, 4, &z, sizeof(z));
        }
    }
    if (hk) {
        VMMCALL(RegCloseKey)(hk);
    }
    if (gLinkAckMs < STOR98LS9X_CM_LINK_WAIT_MS) {
        gLinkAckMs = STOR98LS9X_CM_LINK_WAIT_MS;        /* R12 S4 floor */
    }
    if (gAckTimeoutMs == 0) {
        gAckTimeoutMs = STOR98LS9X_CM_ACK_WAIT_MS;      /* R11 O4 */
    }
    Log(STG_CM_START, gMode | (gNoFlag << 4) | (gCancelAfterAck << 8) | (gNoRemove << 12) | (gSkipQuery << 16) |
        (gOverlap << 20) | (gHostile << 24), FALSE, "params; link ack ms, request ack ms", gLinkAckMs, gAckTimeoutMs);
}

static VOID SleepMs(ULONG ms)
{
    LARGE_INTEGER d;
    d.QuadPart = -(LONGLONG)ms * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &d);
}

/* ------------------------------------------------------------------ */
/* Table, proof and locator (R3)                                       */
/* ------------------------------------------------------------------ */

typedef struct _DYN_VALCTX {           /* VMMREG.H struct val_context */
    int valuelen;
    PVOID value_context;
    PVOID val_buff_ptr;
} DYN_VALCTX;

typedef struct _DYN_PVALUE {           /* VMMREG.H struct pvalue */
    char *pv_valuename;
    ULONG pv_valuelen;
    PVOID pv_value_context;
    ULONG pv_type;
} DYN_PVALUE;

typedef ULONG (__cdecl *DYN_QUERY)(PVOID, DYN_VALCTX *, ULONG, PVOID, ULONG *, ULONG);

typedef struct _DYN_PROVIDER {         /* VMMREG.H struct provider_info */
    DYN_QUERY pi_R0_1val;
    DYN_QUERY pi_R0_allvals;
    DYN_QUERY pi_R3_1val;
    DYN_QUERY pi_R3_allvals;
    ULONG pi_flags;
} DYN_PROVIDER;

#define PROVIDER_KEEPS_VALUE_LENGTH 1

static DYN_PVALUE gDynValue[2];
static DYN_PROVIDER gDynProvider;
static char gDynValueName[] = STOR98LS9X_PROOF_VALUE;
static char gDynTableName[] = STOR98LS9X_PROOF_TABLE_VALUE;
static char gDynKeyName[] = STOR98LS9X_PROOF_KEY;
static volatile ULONG gDynQueries;

static ULONG __cdecl DynQuery(PVOID keyctx, DYN_VALCTX *vc, ULONG n, PVOID data, ULONG *cb, ULONG reserved)
{
    ULONG i, need = 0;
    PUCHAR out = (PUCHAR)data;

    UNREFERENCED_PARAMETER(keyctx);
    UNREFERENCED_PARAMETER(reserved);
    for (i = 0; i < n; i++) {
        vc[i].valuelen = vc[i].value_context == (PVOID)&gProofCookie ? (int)sizeof(gProofCookie) :
                         vc[i].value_context == (PVOID)&gProofTable ? (int)sizeof(gProofTable) : 0;
        need += (ULONG)vc[i].valuelen;
    }
    gDynQueries++;
    if (data == NULL || *cb < need) {
        *cb = need;
        return data == NULL ? 0 : 234;  /* ERROR_MORE_DATA */
    }
    for (i = 0; i < n; i++) {
        RtlCopyMemory(out, vc[i].value_context, vc[i].valuelen);
        vc[i].val_buff_ptr = out;
        out += vc[i].valuelen;
    }
    *cb = need;
    return 0;
}

/* Writes the locator (R3 b); Len/Type/Linear/Cookie overridable (hostile tests). */
static ULONG WriteLocator(ULONG Linear, ULONG Cookie, ULONG Len, ULONG Type)
{
    STOR98LS9X_LOCATOR loc;
    ULONG hk = 0, rc;

    loc.Signature = STOR98LS_SIGNATURE;
    loc.Version = STOR98LS9X_LOCATOR_VERSION;
    loc.TableLinear = Linear;
    loc.TableSize = sizeof(STOR98LS_TABLE);
    loc.Cookie = Cookie;
    rc = VMMCALL(RegCreateKey)(HKLM, STOR98LS9X_LOCATOR_KEY, &hk);
    if (rc == 0) {
        rc = VMMCALL(RegSetValueEx)(hk, STOR98LS9X_LOCATOR_VALUE, 0, Type, &loc, Len);
        VMMCALL(RegCloseKey)(hk);
    }
    return rc;
}

static BOOLEAN TableInit(VOID)
{
    ULONG hk = 0, rc1, rc2;
    ULONGLONG t = Tsc();

    gTable = (PSTOR98LS_TABLE)ExAllocatePoolWithTag(NonPagedPool, sizeof(STOR98LS_TABLE), 'XS9N');
    if (gTable == NULL) {
        Log(STG_CM_ERROR, 1, FALSE, "table allocation failed", 0, 0);
        return FALSE;
    }
    RtlZeroMemory(gTable, sizeof(*gTable));
    gCookie = (ULONG)t ^ (ULONG)(t >> 32) ^ 0x5A5A1234;
    if (gCookie == 0) {
        gCookie = 1;
    }
    gTable->Signature = STOR98LS_SIGNATURE;
    gTable->Version = STOR98LS_VERSION;
    gTable->Count = STOR98LS_ENTRIES;
    gTable->Cookie = gCookie;
    gProofCookie = gCookie;
    /* One publisher per boot: an existing proof key belongs to another one. */
    if (VMMCALL(RegOpenKey)(HKDD, gDynKeyName, &hk) == 0) {
        VMMCALL(RegCloseKey)(hk);
        Log(STG_CM_ERROR, 0xD0, TRUE, "proof key already exists: another publisher; not publishing", 0, 0);
        return FALSE;
    }
    gProofTable.TableLinear = (ULONG)gTable;
    gProofTable.TableSize = sizeof(STOR98LS_TABLE);
    gProofTable.Cookie = gCookie;
    gDynValue[0].pv_valuename = gDynValueName;
    gDynValue[0].pv_valuelen = sizeof(gProofCookie);
    gDynValue[0].pv_value_context = &gProofCookie;
    gDynValue[0].pv_type = 4;               /* REG_DWORD */
    gDynValue[1].pv_valuename = gDynTableName;
    gDynValue[1].pv_valuelen = sizeof(gProofTable);
    gDynValue[1].pv_value_context = &gProofTable;
    gDynValue[1].pv_type = 3;               /* REG_BINARY */
    gDynProvider.pi_R0_1val = DynQuery;
    gDynProvider.pi_R0_allvals = DynQuery;
    gDynProvider.pi_R3_1val = DynQuery;
    gDynProvider.pi_R3_allvals = DynQuery;
    gDynProvider.pi_flags = PROVIDER_KEEPS_VALUE_LENGTH;
    hk = 0;
    gNoProof = ParamDword("NoProof", 0);
    if (gNoProof) {
        /* Locator only (test): no proof key, so no stor98 load accepts the
         * table (R5b); the table is still written as usual. */
        rc2 = WriteLocator((ULONG)gTable, gCookie, sizeof(STOR98LS9X_LOCATOR), 3);
        gPublished = (BOOLEAN)(rc2 == 0);
        Log(STG_CM_START, (ULONG)gTable, TRUE, "table; NoProof: no proof key, locator rc", 0xFFFFFFFF, rc2);
        Log(STG_CM_START, gCookie, FALSE, "cookie; table size, now ms", sizeof(STOR98LS_TABLE), NowMs());
        return gPublished;
    }
    rc1 = VMMCALL(RegCreateDynKey)(gDynKeyName, NULL, &gDynProvider, gDynValue, 2, &hk);
    rc2 = rc1 == 0 ? WriteLocator((ULONG)gTable, gCookie, sizeof(STOR98LS9X_LOCATOR), 3) : 0xFFFFFFFF;
    gPublished = (BOOLEAN)(rc1 == 0 && rc2 == 0);
    Log(STG_CM_START, (ULONG)gTable, TRUE, "table; proof dynamic key rc, locator rc", rc1, rc2);
    Log(STG_CM_START, gCookie, FALSE, "cookie; table size, now ms", sizeof(STOR98LS_TABLE), NowMs());
    return gPublished;
}

/* Entry update in one odd/even Sequence window (R6b). */
static VOID EntryBegin(PORT *p)
{
    LockedInc(&p->E->Sequence);
}

static VOID EntryEnd(PORT *p)
{
    LockedInc(&p->E->Sequence);
}

static VOID DumpTable(const char *why)
{
    ULONG i;
    PSTOR98LS_ENTRY e;

    LogStr("table dump:", why);
    Log(STG_CM_DIAG, 0xD1, FALSE, "header sig, cookie", gTable->Signature, gTable->Cookie);
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        e = &gTable->Entry[i];
        if (e->Flags == 0 && e->BusNumber == 0) {
            continue;
        }
        Log(STG_CM_DIAG, 0xD200 | i, FALSE, "entry seq|flags, bus|slot", (e->Sequence << 16) | e->Flags,
            (e->BusNumber << 16) | e->SlotNumber);
        Log(STG_CM_DIAG, 0xD300 | i, FALSE, "entry gen|ack, remseq|remack", (e->LinkGeneration << 16) | e->AckGeneration,
            e->RemoveSequence);
        Log(STG_CM_DIAG, 0xD400 | i, FALSE, "entry RemoveAck, -", e->RemoveAck, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Devnodes and config space                                           */
/* ------------------------------------------------------------------ */

static ULONG DevId(ULONG dn, char *buf, ULONG len)
{
    buf[0] = 0;
    return VMMCALL(CmGetDeviceId)(dn, buf, len, 0);
}

static BOOLEAN StrHas(const char *s, const char *sub)
{
    ULONG n = 0, i;
    while (sub[n]) n++;
    for (; *s; s++) {
        for (i = 0; i < n && s[i] && (s[i] | 0x20) == (sub[i] | 0x20); i++)
            ;
        if (i == n) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Depth-first search of the devnode tree for an ID containing both strings. */
static ULONG FindDevnode(ULONG dn, const char *a, const char *b, ULONG depth)
{
    char id[200];
    ULONG child = 0, found;

    if (depth > 8) {
        return 0;
    }
    if (DevId(dn, id, sizeof(id)) == CR_SUCCESS && StrHas(id, a) && (b == NULL || StrHas(id, b))) {
        return dn;
    }
    if (VMMCALL(CmGetChild)(&child, dn, 0) != CR_SUCCESS) {
        return 0;
    }
    while (child) {
        found = FindDevnode(child, a, b, depth + 1);
        if (found) {
            return found;
        }
        if (VMMCALL(CmGetSibling)(&child, child, 0) != CR_SUCCESS) {
            break;
        }
    }
    return 0;
}

/* The NVMe devnode under the port's bridge (vendor 1B36 device 0010), or 0. */
static ULONG EndpointDevnode(PORT *p)
{
    ULONG child = 0;
    char id[200];

    if (VMMCALL(CmGetChild)(&child, p->BridgeDn, 0) != CR_SUCCESS) {
        return 0;
    }
    while (child) {
        if (DevId(child, id, sizeof(id)) == CR_SUCCESS && StrHas(id, "VEN_1B36&DEV_0010")) {
            return child;
        }
        if (VMMCALL(CmGetSibling)(&child, child, 0) != CR_SUCCESS) {
            break;
        }
    }
    return 0;
}

static ULONG BrRead(PORT *p, ULONG off, ULONG len)
{
    ULONG v = 0xFFFFFFFF, cr;
    cr = VMMCALL(CmCallEnum)(p->BridgeDn, PCI_ENUM_FUNC_GET_DEVICE_INFO, off, &v, len, 0);
    if (cr != CR_SUCCESS) {
        return 0xFFFFFFFF;
    }
    if (len == 1) v &= 0xFF;
    if (len == 2) v &= 0xFFFF;
    return v;
}

static ULONG BrWrite(PORT *p, ULONG off, ULONG v, ULONG len)
{
    return VMMCALL(CmCallEnum)(p->BridgeDn, PCI_ENUM_FUNC_SET_DEVICE_INFO, off, &v, len, 0);
}

/* Endpoint vendor ID through configuration mechanism #1 (Mode 1 only). */
static ULONG EndpointVid(PORT *p)
{
    ULONG addr = 0x80000000 | (p->SecBus << 16) | (gEndpointDev << 11);
    ULONG v;
    __asm cli
    OutD(0xCF8, addr);
    v = InD(0xCFC);
    OutD(0xCF8, 0);
    __asm sti
    return v & 0xFFFF;
}

static ULONG ShpcSlotStatus(PORT *p)
{
    BrWrite(p, p->ShpcCap + 2, SHPC_SLOT1_STATUS_DW, 1);
    return BrRead(p, p->ShpcCap + 4, 4);
}

/* TRUE when the slot reports empty (presence bits 11:10 = 11b). */
static BOOLEAN ShpcEmpty(ULONG st)
{
    return (BOOLEAN)(st != 0xFFFFFFFF && ((st >> 10) & 3) == 3);
}

static BOOLEAN BridgeInit(PORT *p, ULONG root)
{
    ULONG cap, guard = 0;
    char id[200], dev[24];

    /* PCI\VEN_1B36&DEV_0001...\BUS_00&DEV_0n&FUNC_00 */
    dev[0] = 'D'; dev[1] = 'E'; dev[2] = 'V'; dev[3] = '_';
    dev[4] = (char)('0' + p->BridgeDev / 10); dev[5] = (char)('0' + p->BridgeDev % 10);
    dev[6] = '&'; dev[7] = 'F'; dev[8] = 'U'; dev[9] = 'N'; dev[10] = 'C'; dev[11] = 0;
    p->BridgeDn = FindDevnode(root, "VEN_1B36&DEV_0001", dev, 0);
    if (!p->BridgeDn) {
        Log(STG_CM_ERROR, 2, FALSE, "tunnel bridge devnode not found; port, dev", p->Index, p->BridgeDev);
        return FALSE;
    }
    DevId(p->BridgeDn, id, sizeof(id));
    LogStr("bridge devnode", id);
    p->SecBus = BrRead(p, 0x19, 1);
    cap = BrRead(p, 0x34, 1);
    while (cap && cap != 0xFF && guard++ < 48) {
        if (BrRead(p, cap, 1) == SHPC_CAP_ID) {
            p->ShpcCap = cap;
            break;
        }
        cap = BrRead(p, cap + 1, 1);
    }
    Log(STG_CM_START, p->BridgeDn, FALSE, "bridge devnode; secondary bus, SHPC cap", p->SecBus, p->ShpcCap);
    if (!p->ShpcCap || p->SecBus == 0xFF || p->SecBus == 0) {
        Log(STG_CM_ERROR, 3, FALSE, "no SHPC capability or secondary bus via CONFIGMG", p->SecBus, p->ShpcCap);
        return FALSE;
    }
    /* R8a: one entry per port, identity fixed for the boot. */
    p->E = &gTable->Entry[p->Index];
    EntryBegin(p);
    p->E->BusNumber = p->SecBus;
    p->E->SlotNumber = gEndpointDev;        /* PCI_SLOT_NUMBER: device in bits 4:0, function 0 */
    p->E->Flags = STOR98LS_IN_USE;
    EntryEnd(p);
    p->LastSt = 0xFFFFFFFE;
    p->DevnodeLive = (BOOLEAN)(EndpointDevnode(p) != 0);
    if (p->DevnodeLive) {
        /* An endpoint enumerated before the CM: retire it before any UP (R8c). */
        p->RemovePending = TRUE;
        p->NextTryMs = NowMs();
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Hostile rendezvous and entry variants (test only)                   */
/* ------------------------------------------------------------------ */

static PVOID gReserved;     /* reserved, never committed system-arena page */

static VOID ApplyHostile(PORT *p)
{
    ULONG lin = (ULONG)gTable, rc = 0;
    PSTOR98LS_ENTRY d;

    if (gHostile == gHostileActive) {
        return;
    }
    if (gHostileActive) {
        /* restore before applying another mode (modes never accumulate) */
        gProofCookie = gCookie;
        gProofTable.TableLinear = lin;
        gTable->Count = STOR98LS_ENTRIES;
        d = &gTable->Entry[7];
        LockedInc(&d->Sequence);
        d->Flags = 0;
        d->BusNumber = d->SlotNumber = d->LinkGeneration = 0;
        LockedInc(&d->Sequence);
        rc = WriteLocator(lin, gCookie, sizeof(STOR98LS9X_LOCATOR), 3);
        Log(STG_CM_DIAG, 0xA0, TRUE, "hostile mode restored; previous, locator rc", gHostileActive, rc);
        gHostileActive = 0;
    }
    if (gHostile == 0) {
        return;
    }
    switch (gHostile) {
    case 1:                 /* table address not 4-byte aligned */
        rc = WriteLocator(lin + 2, gCookie, sizeof(STOR98LS9X_LOCATOR), 3);
        break;
    case 2:                 /* table address in the system arena, not present */
        if (gReserved == NULL) {
            gReserved = (PVOID)VMMCALL(PageReserve)(PR_SYSTEM, 1, 0);
        }
        rc = WriteLocator((ULONG)gReserved, gCookie, sizeof(STOR98LS9X_LOCATOR), 3);
        Log(STG_CM_DIAG, 0xA2, FALSE, "reserved page (not committed)", (ULONG)gReserved, 0);
        break;
    case 3:                 /* table address in the private arena */
        rc = WriteLocator(0x00400000, gCookie, sizeof(STOR98LS9X_LOCATOR), 3);
        break;
    case 4:                 /* locator too short */
        rc = WriteLocator(lin, gCookie, 16, 3);
        break;
    case 5:                 /* locator of the wrong type */
        rc = WriteLocator(lin, gCookie, sizeof(STOR98LS9X_LOCATOR), 4);
        break;
    case 6:                 /* proof does not match (another boot's locator) */
        gProofCookie = gCookie ^ 0x00010000;
        break;
    case 7:                 /* stale locator: cookie of another boot */
        rc = WriteLocator(lin, gCookie ^ 0x00020000, sizeof(STOR98LS9X_LOCATOR), 3);
        break;
    case 8:                 /* table header wrong */
        gTable->Count = STOR98LS_ENTRIES - 1;
        break;
    case 9:                 /* duplicate entry for the same function */
        d = &gTable->Entry[7];
        LockedInc(&d->Sequence);
        d->BusNumber = p->SecBus;
        d->SlotNumber = gEndpointDev;
        d->LinkGeneration = 1;
        d->Flags = STOR98LS_IN_USE | STOR98LS_UP;
        LockedInc(&d->Sequence);
        break;
    case 11:                /* the provider's table address disagrees with the locator */
        gProofTable.TableLinear = lin + 0x1000;
        break;
    case 12:                /* locator rewritten with the right cookie but another address */
        rc = WriteLocator(lin + 0x1000, gCookie, sizeof(STOR98LS9X_LOCATOR), 3);
        break;
    case 10:                /* illegal flags after binding: applied after UP, see Publish */
        break;
    }
    gHostileActive = gHostile;
    Log(STG_CM_DIAG, 0xA000 | gHostile, TRUE, "hostile mode active; locator rc", rc, 0);
}

/* ------------------------------------------------------------------ */
/* Link events and the eject transaction: per-port states (R6e)        */
/* ------------------------------------------------------------------ */

/*
 * Every wait is a per-port state advanced by PollThread's loop (the one
 * writer); nothing below sleeps longer than the loop's own poll, so one
 * port's acknowledgement wait, removal retries or teardown never delays
 * another port's DOWN_QUIESCED (R6e).
 */
enum {
    PS_IDLE = 0,
    PS_LINK_DELAY,      /* DelayMs test: statement not yet published */
    PS_LINK_ACK,        /* waiting for AckGeneration (R12 S4) */
    PS_EJ_ACK,          /* waiting for RemoveAck (R11 O4) */
    PS_EJ_POST,         /* overlap/retry test observation window */
    PS_EJ_DELAY,        /* DelayRemoveMs test window before Remove_SubTree */
    PS_EJ_REMOVE,       /* Remove_SubTree attempts every 2 s for 30 s (O5/O6) */
    PS_EJ_REENUM,       /* D2: re-enumerate after a short pause */
    PS_EJ_TEARDOWN,     /* waiting for the slot to empty after a teardown request */
    PS_EJ_QUERY         /* eject: Query_Remove_SubTree on the worker (R11 O1) */
};

#define CALL_QUERY  0x0C    /* CONFIGMG call codes, as in the 0xCD stage argument */
#define CALL_REMOVE 0x0D
#define CALL_REENUM 0x33

#define EJ_CLEAN    1       /* what a successful Remove_SubTree leads to */
#define EJ_ISOLATED 2

static VOID Xchg(volatile LONG *p, LONG v)
{
    __asm {
        mov ecx, p
        mov eax, v
        xchg [ecx], eax
    }
}

/* Locked compare-exchange: *p = v if *p == expect; returns the old value. */
static LONG CmpXchg(volatile LONG *p, LONG expect, LONG v)
{
    LONG old;
    __asm {
        mov ecx, p
        mov eax, expect
        mov edx, v
        lock cmpxchg [ecx], edx
        mov old, eax
    }
    return old;
}

/*
 * Worker handoff (R6f): one state word, each transition owned by one side.
 *   IDLE -> SUBMITTED   writer, after writing op, devnode, flags and a new id
 *   SUBMITTED -> RUNNING worker (locked compare-exchange)
 *   RUNNING -> DONE     worker, after writing the result and the id it ran
 *   DONE -> IDLE        writer, after taking (or discarding) the result
 * The worker never runs an operation it did not move to RUNNING itself, so
 * no operation runs twice; the writer accepts a result only if its id is the
 * id it submitted for that operation and devnode.
 */
#define CS_IDLE         0
#define CS_SUBMITTED    1
#define CS_RUNNING      2
#define CS_DONE         3

/* The port's CONFIGMG worker: one call at a time, as handed over by Call(). */
static VOID CallThread(PVOID ctx)
{
    PORT *p = (PORT *)ctx;
    ULONG cr;

    ULONG id;

    for (;;) {
        if (gParked || CmpXchg(&p->CallState, CS_SUBMITTED, CS_RUNNING) != CS_SUBMITTED) {
            SleepMs(5);
            continue;
        }
        id = p->CallId;
        if (p->CallOp == CALL_REMOVE && gSlowRemoveMs) {
            SleepMs(gSlowRemoveMs);         /* test: a slow Remove_SubTree */
        }
        switch (p->CallOp) {
        case CALL_QUERY:
            cr = VMMCALL(CmQueryRemove)(p->CallDn, p->CallFlags);
            break;
        case CALL_REMOVE:
            cr = VMMCALL(CmRemove)(p->CallDn, p->CallFlags);
            break;
        default:
            cr = VMMCALL(CmReenumerate)(p->CallDn, p->CallFlags);
            break;
        }
        p->CallCr = cr;
        p->DoneAt = NowMs();
        p->DoneId = id;
        Xchg(&p->CallState, CS_DONE);
    }
}

/*
 * Non-blocking CONFIGMG call for the writer (R6e). Returns TRUE with *cr when
 * the result of exactly this call (op, devnode) is available; otherwise hands
 * it to the port's worker if that is idle and returns FALSE (the caller's
 * state retries on its next poll). A finished result of a different call (its
 * state was superseded, e.g. by a link loss) is discarded.
 */
static BOOLEAN Call(PORT *p, ULONG op, ULONG dn, ULONG flags, ULONG *cr)
{
    LONG st = (LONG)Load((volatile ULONG *)&p->CallState);

    if (st == CS_SUBMITTED || st == CS_RUNNING) {
        if (!p->CallLogged && NowMs() - p->CallAt >= 5000) {
            p->CallLogged = TRUE;
            Log(STG_CM_DIAG, 0xB9, TRUE, "CONFIGMG call still running after 5 s (other ports continue); op, port",
                p->CallOp, p->Index);
        }
        return FALSE;
    }
    if (st == CS_DONE) {
        if (p->DoneId == p->CallId && p->CallOp == op && p->CallDn == dn) {
            *cr = p->CallCr;
            Xchg(&p->CallState, CS_IDLE);
            if (p->CallLogged) {
                Log(STG_CM_DIAG, 0xBA, TRUE, "CONFIGMG call returned after; ms, port", NowMs() - p->CallAt, p->Index);
            }
            return TRUE;
        }
        Log(STG_CM_DIAG, 0xBB, TRUE, "CONFIGMG result discarded (superseded); op, id", p->CallOp, p->DoneId);
        Xchg(&p->CallState, CS_IDLE);
    }
    p->CallOp = op;
    p->CallDn = dn;
    p->CallFlags = flags;
    p->CallId = ++p->NextId;
    p->CallAt = NowMs();
    p->CallLogged = FALSE;
    Xchg(&p->CallState, CS_SUBMITTED);
    return FALSE;
}

static VOID SetState(PORT *p, ULONG st, ULONG ms)
{
    p->St = st;
    p->T0 = NowMs();
    p->Deadline = p->T0 + ms;
}

static BOOLEAN Due(PORT *p)
{
    return (BOOLEAN)((LONG)(NowMs() - p->Deadline) >= 0);
}

/* A fire-and-forget call's result (Reenumerate_DevNode after UP), taken while idle. */
static VOID DrainCall(PORT *p)
{
    ULONG cr;

    if (p->CallState == CS_DONE && p->CallOp == CALL_REENUM && Call(p, CALL_REENUM, p->CallDn, 0, &cr)) {
        Log(STG_CM_CONFIGMG, (0x33UL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE,
            "Reenumerate_DevNode(bridge); port, now ms", p->Index, NowMs());
    }
}

static VOID PublishUp(PORT *p)
{
    ULONG cr;

    ReadParams();
    ApplyHostile(p);
    EntryBegin(p);
    p->E->LinkGeneration++;
    p->E->Flags = STOR98LS_IN_USE | STOR98LS_UP;
    EntryEnd(p);
    p->Up = TRUE;
    p->Early = FALSE;
    Log(STG_CM_UP, (p->Index << 24) | ((p->E->BusNumber & 0xFF) << 16) | (p->E->LinkGeneration & 0xFFFF), TRUE,
        "UP published; bus, slot", p->E->BusNumber, p->E->SlotNumber);
    p->DevnodeLive = TRUE;
    p->StatusAt = NowMs() + 3000;
    (VOID)Call(p, CALL_REENUM, p->BridgeDn, 0, &cr);    /* result logged by DrainCall */
    if (gHostile == 10) {
        p->H10Phase = 1;                    /* illegal flags 15 s later (timed in PollPort) */
        p->H10At = NowMs() + 15000;
    }
}

/* Diagnostic: the endpoint devnode's CONFIGMG status and problem number. */
static VOID LogDevStatus(PORT *p, const char *why)
{
    ULONG dn = EndpointDevnode(p), st = 0, pr = 0, cr = 0xFF;

    if (dn) {
        cr = VMMCALL(CmGetStatus)(&st, &pr, dn, 0);
    }
    Log(STG_CM_DIAG, 0xF0 | p->Index, FALSE, "endpoint devnode status, problem", st, (cr << 24) | pr);
    LogStr("  devnode status:", why);
}

/* Devnode retired (Remove_SubTree succeeded or no devnode): R8c. */
static VOID Retire(PORT *p, const char *why)
{
    p->DevnodeLive = FALSE;
    p->RemovePending = FALSE;
    p->DeferLogged = FALSE;
    p->Tries = 0;
    Log(STG_CM_DIAG, 0xC0 | p->Index, TRUE, "endpoint devnode retired; port, generation", p->Index,
        p->E->LinkGeneration);
    LogStr("  retired:", why);
}

/* One attempt of the pending removal after a link-down or an O6 failure
 * (vetoes retried every 2 s, never forced). */
static VOID ProcessRemove(PORT *p)
{
    ULONG dn, cr;

    if (!p->RemovePending || p->St != PS_IDLE || (LONG)(NowMs() - p->NextTryMs) < 0) {
        return;
    }
    if (p->RmPhase == 2) {
        /* the late result EjRemoveStep took at O6 */
        p->RmPhase = 0;
        p->RmLate = FALSE;
        cr = p->RmLateCr;
        Log(STG_CM_CONFIGMG, (0x0DUL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE,
            "Remove_SubTree(endpoint) (late result, applied after O6); devnode, port", p->Dn, p->Index);
        if (cr == CR_SUCCESS) {
            Retire(p, "removed after link down");
            return;
        }
        p->Tries++;
        p->NextTryMs = NowMs() + 2000;
        return;
    }
    if (p->RmPhase == 1) {
        /* the devnode leaves the tree while Remove_SubTree runs: take its result first */
        if (!Call(p, CALL_REMOVE, p->RmDn, CM_REMOVE_UI_NOT_OK, &cr)) {
            return;
        }
        Log(STG_CM_DIAG, 0xAE00 | p->Index, TRUE, "Remove_SubTree result taken; DoneAt, O6At", p->DoneAt, p->O6At);
        p->RmPhase = 0;
        Log(STG_CM_CONFIGMG, (0x0DUL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE,
            "Remove_SubTree(endpoint); devnode, port", p->RmDn, p->Index);
        if (cr == CR_SUCCESS) {
            Retire(p, "removed after link down");
            return;
        }
        p->Tries++;
        p->NextTryMs = NowMs() + 2000;
        return;
    }
    dn = EndpointDevnode(p);
    if (!dn) {
        if (p->CallState == CS_IDLE || p->CallState == CS_DONE) {
            Retire(p, "no endpoint devnode");
        }
        return;
    }
    if (!Call(p, CALL_QUERY, dn, CM_QUERY_REMOVE_UI_NOT_OK, &cr)) {
        return;
    }
    if (p->Tries < 3 || (p->Tries % 30) == 0) {
        Log(STG_CM_CONFIGMG, (0x0CUL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE,
            "Query_Remove_SubTree(endpoint); devnode, try", dn, p->Tries);
    }
    RegPutDword("LastCr", cr);
    if (cr == CR_SUCCESS) {
        p->RmPhase = 1;
        p->RmDn = dn;
        (VOID)Call(p, CALL_REMOVE, dn, CM_REMOVE_UI_NOT_OK, &cr);
        return;
    }
    p->Tries++;
    p->NextTryMs = NowMs() + 2000;
}

static VOID SetResult(PORT *p, ULONG v)
{
    RegPutDword(p->Index ? "EjectResult1" : "EjectResult", v);
}

static VOID ClearPending(PORT *p)
{
    EntryBegin(p);
    p->E->Flags &= ~STOR98LS_REMOVE_PENDING;
    EntryEnd(p);
}

static VOID TeardownRequest(PORT *p)
{
    Log(p->Index ? STG_CM_TEARDOWN1 : STG_CM_TEARDOWN, p->Seq, TRUE, "teardown requested (harness stage rule); port",
        p->Index, 0);
}

/* R12 S2: the statement (after the DelayMs test window, if any), then S4. */
static VOID PublishDown(PORT *p)
{
    p->Gen = p->E->LinkGeneration;
    EntryBegin(p);
    p->E->Flags = STOR98LS_IN_USE | (gNoFlag ? 0 : STOR98LS_DOWN_QUIESCED);
    EntryEnd(p);
    p->Up = FALSE;
    Log(STG_CM_DOWN, (p->Index << 24) | ((p->E->Flags & 0xFF) << 16) | (p->Gen & 0xFFFF), TRUE,
        "DOWN published; ack before, endpoint devnode", p->E->AckGeneration, EndpointDevnode(p));
    SetState(p, PS_LINK_ACK, gLinkAckMs);
}

/* Slot empty while UP (or Mode 1's early statement). Supersedes any eject
 * state of the port: the statement clears REMOVE_PENDING. */
static VOID LinkDown(PORT *p)
{
    ReadParams();
    if (p->St == PS_EJ_QUERY) {
        Log(STG_CM_DIAG, 0xB8, TRUE, "link down during an eject; port, eject state", p->Index, p->St);
        SetResult(p, 0xE700 | p->St);       /* superseded before REMOVE_PENDING */
    } else if (p->St >= PS_EJ_ACK && p->St <= PS_EJ_TEARDOWN) {
        Log(STG_CM_DIAG, 0xB8, TRUE, "link down during an eject; port, eject state", p->Index, p->St);
        if (p->St != PS_EJ_TEARDOWN) {
            SetResult(p, 0xE700 | p->St);   /* eject ended by the link loss: not clean */
        }
    }
    if (gDelayMs) {
        p->Up = FALSE;
        SetState(p, PS_LINK_DELAY, gDelayMs);
        return;
    }
    PublishDown(p);
}

static VOID LinkAckStep(PORT *p)
{
    ULONG ms = NowMs() - p->T0;
    BOOLEAN due = Due(p);                   /* deadline first: an ack first seen after it is late */
    ULONG a1 = Load(&p->E->AckGeneration), a2;
    BOOLEAN acked = FALSE;

    /* R6d: read the acknowledgement, check it, and read it once more
       (unchanged, the statement's generation still current) before acting,
       as EjAckStep does for RemoveAck. A second read that differs is not an
       acknowledgement yet: the next poll looks again. */
    if (a1 == p->Gen) {
        a2 = Load(&p->E->AckGeneration);
        acked = (BOOLEAN)(a2 == a1 && Load(&p->E->LinkGeneration) == p->Gen);
    }
    if (due && acked) {
        Log(STG_CM_DIAG, 0xBC00 | p->Index, TRUE, "late link-down ack (first seen after the wait): timeout; port, ms",
            p->Index, ms);
        acked = FALSE;
    }
    /* No endpoint devnode: no instance can acknowledge. */
    if (!acked && EndpointDevnode(p) && !due) {
        return;
    }
    Log(STG_CM_ACK, acked | ((ms & 0xFFFFF) << 8) | (p->Index << 28), TRUE, "link-down ack (1) or timeout; gen, ms",
        p->Gen, ms);
    p->St = PS_IDLE;
    if (!gNoRemove) {
        p->RemovePending = TRUE;
        p->NextTryMs = NowMs();
        p->Tries = 0;
        /* RmPhase stays 1 if O6 left a Remove_SubTree running: its result is taken first */
        ProcessRemove(p);
    }
}

static VOID EjectPublish(PORT *p);

/* R11 O1-O2 (the start of an eject). */
static VOID EjectStart(PORT *p, ULONG request)
{
    ULONG dn, cr;

    ReadParams();
    Log(STG_CM_REMOVE, (1UL << 24) | (request & 0xFFFF), TRUE, "eject requested; port, generation", p->Index,
        p->E->LinkGeneration);
    dn = EndpointDevnode(p);
    if (!p->Up || !dn) {
        Log(STG_CM_ERROR, 4, FALSE, "eject: no tunnel or no endpoint devnode", p->Up, dn);
        SetResult(p, 0xE0);
        return;
    }
    p->Dn = dn;
    p->Request = request;
    if (!gSkipQuery) {
        (VOID)Call(p, CALL_QUERY, dn, CM_QUERY_REMOVE_UI_NOT_OK, &cr);
        SetState(p, PS_EJ_QUERY, 0);        /* EjQueryStep takes the result */
        return;
    }
    Log(STG_CM_DIAG, 0xB0, TRUE, "test: SkipQuery: request without Query_Remove_SubTree", 0, 0);
    EjectPublish(p);
}

/* O1 result: a veto is final; success -> O2. */
static VOID EjQueryStep(PORT *p)
{
    ULONG cr;

    if (!Call(p, CALL_QUERY, p->Dn, CM_QUERY_REMOVE_UI_NOT_OK, &cr)) {
        return;
    }
    Log(STG_CM_CONFIGMG, (0x0CUL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE,
        "eject: Query_Remove_SubTree; devnode", p->Dn, 0);
    if (cr != CR_SUCCESS) {
        Log(STG_CM_REMOVE, (2UL << 24) | (p->Request & 0xFFFF), TRUE, "eject: vetoed, no REMOVE_PENDING; cr", cr, 0);
        SetResult(p, 0xE100 | cr);
        p->St = PS_IDLE;
        return;
    }
    EjectPublish(p);
}

/* O2: REMOVE_PENDING with a new request sequence. */
static VOID EjectPublish(PORT *p)
{
    p->ReqSeq = (p->ReqSeq + 1) & STOR98LS9X_SEQ_MASK;
    if (p->ReqSeq == 0) {
        p->ReqSeq = 1;
    }
    p->Seq = p->ReqSeq;
    p->Seq2 = 0;
    p->Gen = p->E->LinkGeneration;
    EntryBegin(p);
    p->E->RemoveSequence = p->Seq;
    p->E->Flags |= STOR98LS_REMOVE_PENDING;
    EntryEnd(p);
    Log(STG_CM_REMOVE, (3UL << 24) | (p->Index << 20) | (p->Seq & 0xFFFFF), TRUE,
        "REMOVE_PENDING set; generation, ack before", p->Gen, p->E->RemoveAck);
    SetState(p, PS_EJ_ACK, gAckTimeoutMs);
}

/* O5/O6: Remove_SubTree, one attempt per call; Mode says what success means. */
static VOID EjRemoveStep(PORT *p)
{
    ULONG cr = 0xFF;                        /* 0xFF: the call is still running at O6 */
    BOOLEAN late = (BOOLEAN)((LONG)(NowMs() - p->O6At) >= 0);

    if (!late && gWriterStallMs && !p->StallDone && (LONG)(NowMs() + 200 - p->O6At) >= 0) {
        p->StallDone = TRUE;                /* test: the writer stalls across the deadline */
        Log(STG_CM_DIAG, 0xBF, TRUE, "test: writer stall across the O6 deadline; ms, port", gWriterStallMs, p->Index);
        SleepMs(gWriterStallMs);
        late = TRUE;
    }
    /* Elapsed 30 s, whatever the attempt count. The expired deadline wins: only a
     * result the worker completed before it may still be taken as this attempt's;
     * one completed later (or still running) belongs to the O6 retries. */
    if (late && !(p->CallState == CS_DONE && p->CallOp == CALL_REMOVE && (LONG)(p->DoneAt - p->O6At) < 0)) {
        goto o6;
    }
    if (!Due(p)) {
        return;
    }
    if (gWriterStall2Ms && !p->StallDone && p->CallState != CS_IDLE && (LONG)(NowMs() + 200 - p->O6At) >= 0) {
        p->StallDone = TRUE;                /* test: preempted between the deadline check and taking the result */
        Log(STG_CM_DIAG, 0xBF, TRUE, "test: writer stall after the deadline check; ms, port", gWriterStall2Ms, p->Index);
        SleepMs(gWriterStall2Ms);
    }
    if (gFailRemove) {
        gFailRemove--;
        cr = 0x17;                          /* test: a failing Remove_SubTree */
        Log(STG_CM_DIAG, 0xB5, TRUE, "test: Remove_SubTree failure injected; left", gFailRemove, 0);
    } else if (!Call(p, CALL_REMOVE, p->Dn, CM_REMOVE_UI_NOT_OK, &cr)) {
        return;
    } else {
        /* validated when taken: completed at or after the deadline -> O6, never clean */
        Log(STG_CM_DIAG, 0xAE00 | p->Index, TRUE, "Remove_SubTree result taken; DoneAt, O6At", p->DoneAt, p->O6At);
        if ((LONG)(p->DoneAt - p->O6At) >= 0) {
            Log(STG_CM_CONFIGMG, (0x0DUL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE,
                "Remove_SubTree(endpoint) completed after the O6 deadline; devnode, try", p->Dn, p->Tries);
            p->RmLateCr = cr;
            p->RmLate = TRUE;
            cr = 0xFF;
            goto o6;
        }
    }
    Log(STG_CM_CONFIGMG, (0x0DUL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE, "Remove_SubTree(endpoint); devnode, try",
        p->Dn, p->Tries);
    if (cr == CR_SUCCESS) {
        ClearPending(p);
        if (p->Mode == EJ_CLEAN) {
            Retire(p, "clean eject");
            Log(STG_CM_REMOVE, (7UL << 24) | (p->Outcome << 16) | (p->Seq & 0xFFFF), TRUE,
                "clean eject; RemoveAck after Remove", p->E->RemoveAck, 0);
            SetResult(p, 0xC1EA);
            if (gTeardown) {
                TeardownRequest(p);
                SetState(p, PS_EJ_TEARDOWN, 5000);
                return;
            }
            p->St = PS_IDLE;
            return;
        }
        Retire(p, "isolated, not clean (or cancelled): D2 recovery");
        SetResult(p, 0xE200 | p->Outcome);
        SetState(p, PS_EJ_REENUM, 500);
        return;
    }
    p->Tries++;
    if ((LONG)(NowMs() + 2000 - p->O6At) < 0) {
        p->Deadline = NowMs() + 2000;
        return;
    }
o6:
    /* O6, common to every outcome that reached Remove_SubTree: keep the
     * devnode, tear the tunnel down, keep retrying the removal (vetoes
     * honoured), no UP until it succeeds. A Remove_SubTree still running
     * stays this port's: ProcessRemove takes its result first (RmPhase 1). */
    ClearPending(p);
    Log(STG_CM_REMOVE, (8UL << 24) | (p->Seq & 0xFFFF), TRUE, "O6: Remove_SubTree failing for 30 s: eject failed; cr", cr, 0);
    SetResult(p, 0xE500 | (cr & 0xFF));
    p->RemovePending = TRUE;
    p->NextTryMs = NowMs() + 2000;
    p->Tries = 0;
    if (p->RmLate) {
        p->RmPhase = 2;                     /* a late result already taken: ProcessRemove applies it */
    } else if (p->CallState != CS_IDLE && p->CallOp == CALL_REMOVE) {
        p->RmPhase = 1;
        p->RmDn = p->CallDn;
        Log(STG_CM_DIAG, 0xBE, TRUE, "O6 with Remove_SubTree still running: kept for the retries; port, id", p->Index,
            p->CallId);
    } else {
        p->RmPhase = 0;
    }
    TeardownRequest(p);
    SetState(p, PS_EJ_TEARDOWN, 5000);
}

/* The verdict on a validated acknowledgement (or none: 0xFF). */
static VOID EjVerdict(PORT *p)
{
    if ((p->Outcome == STOR98RM_CLEAN && !gCancelAfterAck) || p->Outcome == STOR98RM_ISOLATED_NOT_CLEAN ||
        p->Outcome == STOR98RM_CLEAN) {
        p->Mode = (p->Outcome == STOR98RM_CLEAN && !gCancelAfterAck) ? EJ_CLEAN : EJ_ISOLATED;
        p->Tries = 0;
        if (p->Mode == EJ_CLEAN && gDelayRemoveMs) {
            Log(STG_CM_DIAG, 0xB4, TRUE, "test: delay before Remove_SubTree; ms", gDelayRemoveMs, 0);
            SetState(p, PS_EJ_DELAY, gDelayRemoveMs);
            return;
        }
        SetState(p, PS_EJ_REMOVE, 0);
        p->O6At = NowMs() + 30000;
        p->StallDone = FALSE;
        return;
    }
    /* FAILED or no valid acknowledgement: the controller may still DMA.
     * Isolate first (teardown -> slot empty -> R12 path), then remove. */
    ClearPending(p);
    Log(STG_CM_REMOVE, (6UL << 24) | (p->Outcome << 16) | (p->Seq & 0xFFFF), TRUE,
        "eject not clean, not isolated: CM isolates first; RemoveAck", p->E->RemoveAck, 0);
    SetResult(p, 0xE300 | p->Outcome);
    if (gTeardown) {
        TeardownRequest(p);
        SetState(p, PS_EJ_TEARDOWN, 5000);
    } else {
        p->St = PS_IDLE;
    }
}

static VOID EjAckStep(PORT *p)
{
    ULONG a1, a2, ms;

    if (gOverlap == 1 && p->Seq2 == 0 && NowMs() - p->T0 >= 300) {
        p->Seq2 = (p->Seq + 1) & STOR98LS9X_SEQ_MASK;
        EntryBegin(p);
        p->E->RemoveSequence = p->Seq2;
        EntryEnd(p);
        p->ReqSeq = p->Seq2;
        Log(STG_CM_DIAG, 0xB1, TRUE, "test: overlapping request published; first, second", p->Seq, p->Seq2);
    }
    if (gOverlap == 3 && p->Seq2 == 0 && NowMs() - p->T0 >= 300) {
        /* test (int-0.2): the entry's identity changes while the request
           is open; the receiver must stop acting on it and never
           acknowledge (R5e, R8d). Restored when the wait ends. */
        p->Seq2 = 1;
        p->SavedSlot = p->E->SlotNumber;
        EntryBegin(p);
        p->E->SlotNumber = p->SavedSlot ^ 0x1F;
        EntryEnd(p);
        Log(STG_CM_DIAG, 0xF8, TRUE, "test: entry identity changed during the request; slot before, after",
            p->SavedSlot, p->E->SlotNumber);
    }
    if (Due(p)) {
        /* deadline checked first (R11): an acknowledgement first seen now is late */
        a1 = Load(&p->E->RemoveAck);
        if ((a1 >> 8) == p->Seq) {
            Log(STG_CM_DIAG, 0xBD00 | p->Index, TRUE, "late acknowledgement (first seen after T_ack): ignored; RemoveAck, ms", a1,
                NowMs() - p->T0);
        }
        p->Outcome = 0xFF;
    } else {
        a1 = Load(&p->E->RemoveAck);
        if ((a1 >> 8) != p->Seq) {
            return;
        }
        a2 = Load(&p->E->RemoveAck);
        if (a2 != a1 || Load(&p->E->LinkGeneration) != p->Gen) {
            return;
        }
        p->Outcome = a1 & 0xFF;
    }
    ms = NowMs() - p->T0;
    Log(STG_CM_REMOVE, (4UL << 24) | (p->Outcome << 16) | (p->Seq & 0xFFFF), TRUE,
        "acknowledgement (FF none): ms, RemoveAck", ms, p->E->RemoveAck);
    if (gOverlap == 3 && p->Seq2 != 0) {
        EntryBegin(p);
        p->E->SlotNumber = p->SavedSlot;
        EntryEnd(p);
        Log(STG_CM_DIAG, 0xF9, TRUE, "test: entry identity restored; slot, RemoveAck", p->E->SlotNumber, p->E->RemoveAck);
        p->Seq2 = 0;
    }
    if (gOverlap == 1 || (gOverlap == 2 && p->Outcome != 0xFF)) {
        if (gOverlap == 2) {
            p->Seq2 = p->E->RemoveAck;
            EntryBegin(p);
            p->E->RemoveSequence = p->Seq;       /* the same request re-published */
            p->E->Flags |= STOR98LS_REMOVE_PENDING;
            EntryEnd(p);
        }
        SetState(p, PS_EJ_POST, gOverlap == 1 ? 2000 : 1500);
        return;
    }
    EjVerdict(p);
}

static VOID EjPostStep(PORT *p)
{
    if (gOverlap == 1 && p->Seq2 == 0) {
        /* the acknowledgement came before 300 ms: overlap it now */
        p->Seq2 = (p->Seq + 1) & STOR98LS9X_SEQ_MASK;
        EntryBegin(p);
        p->E->RemoveSequence = p->Seq2;
        EntryEnd(p);
        p->ReqSeq = p->Seq2;
        Log(STG_CM_DIAG, 0xB1, TRUE, "test: overlapping request published; first, second", p->Seq, p->Seq2);
    }
    if (!Due(p)) {
        return;
    }
    if (gOverlap == 1) {
        Log(STG_CM_DIAG, 0xB2, TRUE, "test: after overlap, RemoveAck (must name the first), second", p->E->RemoveAck,
            p->Seq2);
    } else {
        Log(STG_CM_DIAG, 0xB3, TRUE, "test: retry, RemoveAck before, after (must be equal)", p->Seq2, p->E->RemoveAck);
    }
    EjVerdict(p);
}

static VOID PortStep(PORT *p)
{
    ULONG cr;

    switch (p->St) {
    case PS_LINK_DELAY:
        if (Due(p)) {
            PublishDown(p);
        }
        break;
    case PS_LINK_ACK:
        LinkAckStep(p);
        break;
    case PS_EJ_ACK:
        EjAckStep(p);
        break;
    case PS_EJ_POST:
        EjPostStep(p);
        break;
    case PS_EJ_DELAY:
        if (Due(p)) {
            SetState(p, PS_EJ_REMOVE, 0);
            p->O6At = NowMs() + 30000;
            p->StallDone = FALSE;
        }
        break;
    case PS_EJ_REMOVE:
        EjRemoveStep(p);
        break;
    case PS_EJ_QUERY:
        EjQueryStep(p);
        break;
    case PS_EJ_REENUM:
        if (Due(p) && Call(p, CALL_REENUM, p->BridgeDn, 0, &cr)) {
            p->DevnodeLive = TRUE;
            Log(STG_CM_CONFIGMG, (0x33UL << 24) | (p->Index << 16) | (cr & 0xFFFF), TRUE,
                "D2: Reenumerate_DevNode(bridge); port", p->Index, 0);
            p->St = PS_IDLE;
        }
        break;
    case PS_EJ_TEARDOWN:
        if (Due(p)) {
            Log(STG_CM_ERROR, 5, TRUE, "teardown did not empty the slot in 5 s (no harness rule?); port", p->Index, 0);
            p->St = PS_IDLE;
        }
        break;
    }
}

static VOID PollPort(PORT *p)
{
    ULONG st, vid;
    BOOLEAN empty;

    st = ShpcSlotStatus(p);
    if (st != p->LastSt) {
        Log(STG_CM_SLOT, st, TRUE, "SHPC slot status changed; port, empty", p->Index, ShpcEmpty(st));
        p->LastSt = st;
    }
    empty = ShpcEmpty(st);
    if (!empty && st != 0xFFFFFFFF && !p->Up && p->St == PS_IDLE) {
        if (p->DevnodeLive || p->RemovePending) {
            if (!p->DeferLogged) {
                p->DeferLogged = TRUE;
                Log(STG_CM_DIAG, 0xD0 | p->Index, TRUE, "UP deferred: previous endpoint devnode not retired (R8c)", p->Index,
                    p->E->LinkGeneration);
            }
        } else {
            PublishUp(p);
        }
    } else if (p->Up && gMode == 1 && !p->Early) {
        vid = EndpointVid(p);
        if (vid == 0xFFFF) {
            p->Early = TRUE;
            Log(STG_CM_DOWN, 0xEA, TRUE, "Mode 1: endpoint vendor ID all-ones (stage 1): early statement", vid, 0);
            LinkDown(p);
        }
    } else if (p->Up && empty) {
        LinkDown(p);
    }
    if (p->H10Phase == 1 && (LONG)(NowMs() - p->H10At) >= 0) {
        EntryBegin(p);
        p->E->Flags = STOR98LS_IN_USE | STOR98LS_UP | STOR98LS_DOWN_QUIESCED;   /* illegal */
        EntryEnd(p);
        Log(STG_CM_DIAG, 0xA00A, TRUE, "hostile: illegal flags written; flags", p->E->Flags, 0);
        p->H10Phase = 2;
        p->H10At = NowMs() + 2000;
    } else if (p->H10Phase == 2 && (LONG)(NowMs() - p->H10At) >= 0) {
        if (p->Up) {
            EntryBegin(p);
            p->E->Flags = STOR98LS_IN_USE | STOR98LS_UP;
            EntryEnd(p);
        }
        p->H10Phase = 0;
    }
    if (p->StatusAt && (LONG)(NowMs() - p->StatusAt) >= 0) {
        p->StatusAt = 0;
        LogDevStatus(p, "3 s after enumeration");
    }
    PortStep(p);
    ProcessRemove(p);
    if (p->St == PS_IDLE && !p->RemovePending) {
        DrainCall(p);
    }
}

static VOID PollThread(PVOID ctx)
{
    ULONG i, n = 0, ej, root = 0, cr, dump;
    PORT *p;

    UNREFERENCED_PARAMETER(ctx);
    ReadParams();
    if (!TableInit()) {
        Log(STG_CM_ERROR, 0xFE, TRUE, "not published: stor98 will see the CM channel unavailable", 0, 0);
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }
    cr = VMMCALL(CmLocate)(&root, NULL, 0);
    Log(STG_CM_START, VMMCALL(CmGetVersion)(), FALSE, "CONFIGMG version; locate root cr, root", cr, root);
    gPort[0].BridgeDev = ParamDword("BridgeDev", 5);
    gPort[1].BridgeDev = ParamDword("BridgeDev1", 6);
    for (i = 0; i < 2; i++) {
        gPort[i].Index = i;
        if (gPort[i].BridgeDev && BridgeInit(&gPort[i], root)) {
            gPort[i].EjectSeen = ParamDword(i ? "EjectRequest1" : "EjectRequest", 0);
            gPorts |= 1 << i;
        } else if (i == 1) {
            /* test layouts put the second bridge at 00:06 or 00:09 */
            gPort[1].BridgeDev = gPort[1].BridgeDev == 6 ? 9 : 6;
            if (BridgeInit(&gPort[1], root)) {
                gPort[1].EjectSeen = ParamDword("EjectRequest1", 0);
                gPorts |= 2;
            }
        }
    }
    gDumpSeen = ParamDword("DumpRequest", 0);
    for (i = 0; i < 2; i++) {
        HANDLE th;
        if ((gPorts & (1 << i)) &&
            !NT_SUCCESS(PsCreateSystemThread(&th, THREAD_ALL_ACCESS, NULL, NULL, NULL, CallThread, &gPort[i]))) {
            Log(STG_CM_ERROR, 0xFD, TRUE, "CONFIGMG worker not created: port disabled; port", i, 0);
            gPorts &= ~(1UL << i);
        }
    }
    Log(STG_CM_START, gPorts, TRUE, "ports active (bit mask); cookie", gCookie, 0);
    if (!gPorts) {
        Log(STG_CM_ERROR, 0xFF, TRUE, "no tunnel port: stand-in idle (table stays published)", 0, 0);
    }
    for (;;) {
        if (gParked) {
            SleepMs(100);                   /* shutting down: no CONFIGMG calls any more */
            continue;
        }
        for (i = 0; i < 2; i++) {
            if (gPorts & (1 << i)) {
                PollPort(&gPort[i]);
            }
        }
        if ((n++ % (200 / (gPollMs ? gPollMs : 10) + 1)) == 0) {
            for (i = 0; i < 2; i++) {
                p = &gPort[i];
                if (!(gPorts & (1 << i)) || p->St != PS_IDLE || p->CallState != CS_IDLE || p->RemovePending) {
                    continue;                   /* a busy port takes its request later */
                }
                ej = ParamDword(i ? "EjectRequest1" : "EjectRequest", p->EjectSeen);
                if (ej != p->EjectSeen) {
                    p->EjectSeen = ej;
                    EjectStart(p, ej);
                }
            }
            dump = ParamDword("DumpRequest", gDumpSeen);
            if (dump != gDumpSeen) {
                gDumpSeen = dump;
                DumpTable("on request");
            }
        }
        if ((n % (30000 / (gPollMs ? gPollMs : 10) + 1)) == 0) {
            Log(STG_CM_DIAG, 0xEE, FALSE, "heartbeat; loops, now ms", n, NowMs());
        }
        SleepMs(gPollMs);
    }
}

/* ------------------------------------------------------------------ */
/* WDM plumbing (function driver of the usb4-host devnode). No          */
/* DriverUnload: the publisher is boot-resident and independent of this */
/* device object (R4).                                                  */
/* ------------------------------------------------------------------ */

typedef struct _FDO_EXT {
    PDEVICE_OBJECT Lower;
} FDO_EXT;

static NTSTATUS PassDown(PDEVICE_OBJECT Dev, PIRP Irp)
{
    FDO_EXT *x = (FDO_EXT *)Dev->DeviceExtension;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(x->Lower, Irp);
}

/* IRPs to the pin device (no lower device, no extension) end here. */
static NTSTATUS PinComplete(PIRP Irp, BOOLEAN Power)
{
    static ULONG logged;

    if (logged++ < 8) {
        Log(STG_CM_DIAG, 0xE3, TRUE, "IRP to the pin device completed; major, minor", IoGetCurrentIrpStackLocation(Irp)->MajorFunction,
            IoGetCurrentIrpStackLocation(Irp)->MinorFunction);
    }
    if (Power) {
        PoStartNextPowerIrp(Irp);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS DispatchPower(PDEVICE_OBJECT Dev, PIRP Irp)
{
    FDO_EXT *x = (FDO_EXT *)Dev->DeviceExtension;
    PIO_STACK_LOCATION s = IoGetCurrentIrpStackLocation(Irp);

    if (s->MinorFunction == IRP_MN_SET_POWER && s->Parameters.Power.Type == SystemPowerState &&
        s->Parameters.Power.State.SystemState >= PowerSystemShutdown && !gParked) {
        gParked = 1;                        /* stop CONFIGMG polling while the system shuts down */
        Log(STG_CM_DIAG, 0xE4, TRUE, "system shutdown power IRP: polling parked; state", s->Parameters.Power.State.SystemState, 0);
    }
    if (Dev == gPin) {
        return PinComplete(Irp, TRUE);
    }
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(x->Lower, Irp);
}

static NTSTATUS DispatchPnp(PDEVICE_OBJECT Dev, PIRP Irp)
{
    FDO_EXT *x = (FDO_EXT *)Dev->DeviceExtension;
    PIO_STACK_LOCATION s = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS st;

    if (Dev == gPin) {
        return PinComplete(Irp, FALSE);
    }
    if (s->MinorFunction == IRP_MN_QUERY_REMOVE_DEVICE && gPublished) {
        Log(STG_CM_DIAG, 0xE1, TRUE, "IRP_MN_QUERY_REMOVE_DEVICE refused: table published (R4)", 0, 0);
        Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_UNSUCCESSFUL;
    }
    if (s->MinorFunction == IRP_MN_REMOVE_DEVICE) {
        Log(STG_CM_DIAG, 0xE2, TRUE, "IRP_MN_REMOVE_DEVICE: device object deleted; table, proof and thread stay", 0, 0);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(Irp);
        st = IoCallDriver(x->Lower, Irp);
        IoDetachDevice(x->Lower);
        IoDeleteDevice(Dev);
        return st;
    }
    if (s->MinorFunction == IRP_MN_START_DEVICE || s->MinorFunction == IRP_MN_QUERY_STOP_DEVICE ||
        s->MinorFunction == IRP_MN_QUERY_REMOVE_DEVICE || s->MinorFunction == IRP_MN_STOP_DEVICE ||
        s->MinorFunction == IRP_MN_CANCEL_STOP_DEVICE || s->MinorFunction == IRP_MN_CANCEL_REMOVE_DEVICE) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
    }
    return PassDown(Dev, Irp);
}

static NTSTATUS DispatchOther(PDEVICE_OBJECT Dev, PIRP Irp)
{
    if (IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_SHUTDOWN && !gParked) {
        gParked = 1;
        Log(STG_CM_DIAG, 0xE5, TRUE, "IRP_MJ_SHUTDOWN: polling parked", 0, 0);
    }
    if (Dev == gPin) {
        return PinComplete(Irp, FALSE);
    }
    return PassDown(Dev, Irp);
}

static NTSTATUS AddDevice(PDRIVER_OBJECT Drv, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT fdo;
    FDO_EXT *x;
    NTSTATUS st;
    HANDLE th;

    st = IoCreateDevice(Drv, sizeof(FDO_EXT), NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &fdo);
    if (!NT_SUCCESS(st)) {
        return st;
    }
    x = (FDO_EXT *)fdo->DeviceExtension;
    x->Lower = IoAttachDeviceToDeviceStack(fdo, Pdo);
    fdo->Flags |= DO_POWER_PAGABLE;
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    /* One publisher per boot, whatever the number of device objects. */
    if (!gThreadStarted && gPin != NULL) {
        gThreadStarted = TRUE;
        st = PsCreateSystemThread(&th, THREAD_ALL_ACCESS, NULL, NULL, NULL, PollThread, NULL);
        if (NT_SUCCESS(st)) {
            ZwClose(th);
        } else {
            Log(STG_CM_ERROR, st, FALSE, "PsCreateSystemThread failed", 0, 0);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT Drv, PUNICODE_STRING Reg)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Reg);
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        Drv->MajorFunction[i] = DispatchOther;
    }
    Drv->MajorFunction[IRP_MJ_PNP] = DispatchPnp;
    Drv->MajorFunction[IRP_MJ_POWER] = DispatchPower;
    Drv->DriverExtension->AddDevice = AddDevice;
    /* No DriverUnload (R4), and a device object that no PnP removal deletes.
     * Without it the publisher's lifetime is not guaranteed (Q-9x r3), so the
     * driver does not load at all: no proof, no table (R4). */
    if (!ThunksInit()) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (ParamDword("FailPin", 0) ||
        !NT_SUCCESS(IoCreateDevice(Drv, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &gPin))) {
        gPin = NULL;
        Log(STG_CM_ERROR, 0xD1, TRUE, "pin device not created (or FailPin test): DriverEntry fails, nothing published", 0, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    gPin->Flags &= ~DO_DEVICE_INITIALIZING;
    ConS("cm9x DriverEntry (interface 3 revision, pin device)\n");
    return STATUS_SUCCESS;
}
