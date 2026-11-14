/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Yeo Kheng Meng */
/*
 * msi4nt.h - message-signaled interrupts for Windows NT drivers before
 * Vista, with an API shaped like Vista's IoConnectInterruptEx.
 *
 * The contract is docs/using/api.md; the delivery path is design record 01
 * (docs/contributing/design/01-fake-irq.md), the hosted build and amd64
 * design record 03, the capability programming design record 04.
 *
 * Include it after ntddk.h (or wdm.h in a hosted build). In exactly one .c
 * file define MSI4NT_IMPLEMENTATION before the include to compile the code
 * in. Configuration macros (MSI4NT_HOSTED, MSI4NT_NO_NATIVE,
 * MSI4NT_LEGACY_NT, MSI4NT_POOL_TAG, MSI4NT_TRACE, and the experimental
 * MSI4NT_ISA_LINE of design record 05) go before every include.
 *
 * Copyright (c) 2026 Yeo Kheng Meng
 */

#ifndef MSI4NT_H
#define MSI4NT_H

#define MSI4NT_VERSION 1

/* The signature every image that compiles the implementation carries
 * (docs/using/api.md, "The signature"): the library's name, the header
 * version above and the copyright holder as in LICENSE. */
#define MSI4NT_STR2(x) #x
#define MSI4NT_STR(x) MSI4NT_STR2(x)
#define MSI4NT_SIGNATURE "msi4nt " MSI4NT_STR(MSI4NT_VERSION) " (c) 2026 Yeo Kheng Meng"

#if defined(MSI4NT_LEGACY_NT) && defined(MSI4NT_HOSTED)
#error "msi4nt: MSI4NT_LEGACY_NT and MSI4NT_HOSTED are separate builds"
#endif
#if defined(MSI4NT_LEGACY_NT) && defined(MSI4NT_ISA_LINE)
#error "msi4nt: MSI4NT_ISA_LINE is for the WDM builds (2000 and later)"
#endif

#if defined(_M_AMD64) || defined(_AMD64_)
#define MSI4NT_AMD64 1
#elif defined(_M_IX86) || defined(_X86_)
#define MSI4NT_X86 1
#else
#error "msi4nt: x86 or amd64 only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define MSGIRQ_CONNECT_MESSAGE_BASED    3

#define MSGIRQ_MESSAGE_ANY              0
#define MSGIRQ_MESSAGE_MSI              1
#define MSGIRQ_MESSAGE_MSIX             2

#define MSGIRQ_CAP_MSI                  0x01
#define MSGIRQ_CAP_MSIX                 0x02
#define MSGIRQ_CAP_64BIT                0x04
#define MSGIRQ_CAP_PER_VECTOR_MASK      0x08

#define MSGIRQ_CONNECT_FLAG_ATTACH      0x1
#define MSGIRQ_CONNECT_FLAG_PIN_PRIVATE 0x2
#if defined(MSI4NT_ISA_LINE)
/* Experimental (design record 05): connect through the function's ISA
 * claim (MsgIrqIsaClaim); LineInterrupt must be all zero. */
#define MSGIRQ_CONNECT_FLAG_ISA_LINE    0x4
#endif

/* MsgIrqDisconnect Flags 0x1 is withdrawn and never reused. */
#define MSGIRQ_DISCONNECT_QUARANTINE    0x2
#define MSGIRQ_DISCONNECT_ABSENT_OK     0x4
/* Release only a function proven absent; otherwise write nothing and
 * return MSGIRQ_STATUS_CONFIG_ACCESS (design record 01, section 2). */
#define MSGIRQ_DISCONNECT_ABSENT_ONLY   0x8

#define MSGIRQ_MODE_NONE                0
#define MSGIRQ_MODE_FAKE_IRQ            1
#define MSGIRQ_MODE_NATIVE              2

#define MSGIRQ_STATUS_OK                0
#define MSGIRQ_STATUS_VERSION           1
#define MSGIRQ_STATUS_NO_MSI_CAP        2
#define MSGIRQ_STATUS_HAL_UNSUPPORTED   3
#define MSGIRQ_STATUS_NO_VECTORS        4
#define MSGIRQ_STATUS_UNSUPPORTED       5
#define MSGIRQ_STATUS_IN_USE            6
#define MSGIRQ_STATUS_NOT_INITIALIZED   7
#define MSGIRQ_STATUS_NO_MEMORY         8
#define MSGIRQ_STATUS_INVALID_PARAMETER 9
#define MSGIRQ_STATUS_CONFIG_ACCESS     10
/* 11 is reserved for msi4vxd's NO_CORE. 12-14 are returned only by an
 * MSI4NT_ISA_LINE build, and defined in every build so that the numbers
 * mean the same everywhere (a published snapshot carries them, record 07). */
/* The function's ISA claim is kept for the rest of the boot (record 05). */
#define MSGIRQ_STATUS_CLAIM_KEPT        12
/* The claim's stray-entry brake has tripped (record 05, section 2.7). */
#define MSGIRQ_STATUS_BRAKED            13
/* Only IOAPIC inputs above 23 were asked for and the machine has none in
 * the call's scope (record 05, section 2.9). */
#define MSGIRQ_STATUS_NO_INPUTS         14

/* ------------------------------------------------------------------ */
/* Structures                                                          */
/* ------------------------------------------------------------------ */

#if defined(MSI4NT_X86)
#pragma pack(push, 4)
#endif

typedef BOOLEAN (__stdcall *MSGIRQ_MESSAGE_SERVICE_ROUTINE)(PKINTERRUPT Interrupt,
                                                           PVOID Context,
                                                           ULONG MessageID);
typedef BOOLEAN (__stdcall *MSGIRQ_LINE_SERVICE_ROUTINE)(PKINTERRUPT Interrupt,
                                                        PVOID Context);

typedef PVOID (__stdcall *MSGIRQ_RESOLVE_ROUTINE)(PVOID HostContext, PCWSTR Name);
typedef PVOID (__stdcall *MSGIRQ_ALLOCATE_ROUTINE)(PVOID HostContext, ULONG Bytes);
typedef VOID (__stdcall *MSGIRQ_FREE_ROUTINE)(PVOID HostContext, PVOID Block);
typedef ULONG (__stdcall *MSGIRQ_CONFIG_ROUTINE)(PVOID ConfigContext, ULONG Offset,
                                                PVOID Buffer, ULONG Length);

typedef struct _MSGIRQ_HOST {
    ULONG Size;
    PVOID HostContext;
    MSGIRQ_RESOLVE_ROUTINE Resolve;
    MSGIRQ_ALLOCATE_ROUTINE Allocate;
    MSGIRQ_FREE_ROUTINE Free;
} MSGIRQ_HOST;

typedef struct _MSGIRQ_CONFIG_ACCESS {
    MSGIRQ_CONFIG_ROUTINE Read;
    MSGIRQ_CONFIG_ROUTINE Write;
    PVOID ConfigContext;
} MSGIRQ_CONFIG_ACCESS;

typedef struct _MSGIRQ_CAPABILITIES {
    ULONG MsiMessages;
    ULONG MsixMessages;
    ULONG Flags;
} MSGIRQ_CAPABILITIES;

typedef struct _MSGIRQ_LINE_RESOURCE {
    ULONG Vector;
    KIRQL Irql;
    KAFFINITY Affinity;
    KINTERRUPT_MODE Mode;
    UCHAR ShareDisposition;
} MSGIRQ_LINE_RESOURCE;

typedef struct _MSGIRQ_CONNECT_PARAMS {
    ULONG Version;
    struct {
        PDEVICE_OBJECT PhysicalDeviceObject;
        MSGIRQ_LINE_RESOURCE LineInterrupt;
        ULONG Bus, Dev, Func;
        const MSGIRQ_CONFIG_ACCESS *ConfigAccess;
        ULONG MessageType;
        ULONG MessagesWanted;
        ULONG Flags;
        MSGIRQ_MESSAGE_SERVICE_ROUTINE MessageServiceRoutine;
        PVOID ServiceContext;
        MSGIRQ_LINE_SERVICE_ROUTINE FallBackServiceRoutine;
        BOOLEAN FloatingSave;
        PKSPIN_LOCK SpinLock;
        KIRQL SynchronizeIrql;
    } MessageBased;
} MSGIRQ_CONNECT_PARAMS;

typedef struct _MSGIRQ_MESSAGE_INFO_ENTRY {
    ULONG Vector;
    KAFFINITY Affinity;
    KIRQL Irql;
    ULONG MessageAddress;
    ULONG MessageData;
    PKINTERRUPT InterruptObject;
} MSGIRQ_MESSAGE_INFO_ENTRY;

typedef struct _MSGIRQ_MESSAGE_INFO {
    KIRQL UnifiedIrql;
    ULONG MessageCount;
    MSGIRQ_MESSAGE_INFO_ENTRY MessageInfo[1];
} MSGIRQ_MESSAGE_INFO, *PMSGIRQ_MESSAGE_INFO;

#if defined(MSI4NT_ISA_LINE)
/* MsgIrqIsaClaim's choices and outcome (design record 05, section 2.1). */
#define MSGIRQ_ISA_MAX_CANDIDATES 16
#define MSGIRQ_ISA_FLAG_OVERRIDE  0x1   /* the candidates are the user's choice */
#define MSGIRQ_ISA_FLAG_SHARE     0x2   /* joinable; joins another when no IRQ of its own is left */
#define MSGIRQ_ISA_FLAG_JOIN_FIRST 0x4  /* with SHARE only: join the driver's fullest claim with
                                           room before an IRQ of its own (record 05, 2.12) */
#define MSGIRQ_ISA_BRAKE_OFF      0xFFFFFFFFUL
/* The pools a claim may take its interrupt from (record 05, section 2.9):
 * the ISA IRQs, the IOAPIC inputs above 23 that nothing declares, or both
 * (then the ISA IRQs first). */
#define MSGIRQ_POOL_ISA           0x1
#define MSGIRQ_POOL_HIGH          0x2
#define MSGIRQ_HIGH_MIN           24UL  /* the lowest and the highest upper input */
#define MSGIRQ_HIGH_MAX           255UL
#define MSGIRQ_HIGH_MAX_EXCLUDES  4
typedef struct _MSGIRQ_INPUT_RANGE {
    ULONG First;                        /* inclusive */
    ULONG Last;
} MSGIRQ_INPUT_RANGE;
typedef struct _MSGIRQ_ISA_PARAMS {
    ULONG Size;                         /* sizeof(MSGIRQ_ISA_PARAMS) */
    PDRIVER_OBJECT DriverObject;        /* the client's: owns each claim's owner object */
    ULONG Flags;                        /* MSGIRQ_ISA_FLAG_* */
    ULONG CandidateCount;               /* 0: the library's order */
    UCHAR Candidates[MSGIRQ_ISA_MAX_CANDIDATES];  /* ISA IRQs and, with MSGIRQ_POOL_HIGH,
                                           inputs 24..255, in the order tried */
    ULONG QuietMilliseconds;            /* the listen on each candidate; 0: 500; 25..5000 */
    ULONG BrakeEntries;                 /* 0: 1000; MSGIRQ_ISA_BRAKE_OFF; else >= 16 */
    ULONG Exclude;                      /* a bit per ISA IRQ the client rules out; 0: none */
    ULONG MaxMembers;                   /* with MSGIRQ_ISA_FLAG_SHARE, the most functions on
                                           one claim; 0: 2; 2..16 */
    /* Since the second pool; a caller whose Size ends before Pools has
     * left them all zero. */
    ULONG Pools;                        /* MSGIRQ_POOL_*; 0: the ISA IRQs only */
    ULONG HighFirst;                    /* the upper inputs considered, inclusive; */
    ULONG HighLast;                     /* both 0: 24..255 */
    ULONG HighIoApics;                  /* bit n: the n-th I/O APIC entry of the MADT; 0: all */
    ULONG HighExcludeCount;             /* ranges of upper inputs the client rules out */
    MSGIRQ_INPUT_RANGE HighExclude[MSGIRQ_HIGH_MAX_EXCLUDES];
    ULONG HighBudget;                   /* the different upper inputs the driver may have
                                           translated in one boot; 0: 16; 1..32 */
    const UNICODE_STRING *RegistryPath; /* as DriverEntry received it; required with
                                           MSGIRQ_POOL_HIGH, else NULL. Read during the
                                           call only */
} MSGIRQ_ISA_PARAMS;
#define MSGIRQ_ISA_PARAMS_V1_SIZE ((ULONG)FIELD_OFFSET(MSGIRQ_ISA_PARAMS, Pools))

/* Per-IRQ outcome (record 05, section 2.3). */
#define MSGIRQ_ISA_NOT_TRIED   0
#define MSGIRQ_ISA_CHOSEN      1
#define MSGIRQ_ISA_NEVER       2
#define MSGIRQ_ISA_SCI         3
#define MSGIRQ_ISA_OVERRIDE    4
#define MSGIRQ_ISA_LEVEL       5
#define MSGIRQ_ISA_CONFIGURED  6
#define MSGIRQ_ISA_POSSIBLE    7
#define MSGIRQ_ISA_PIRQ        8
#define MSGIRQ_ISA_CONFLICT    9
#define MSGIRQ_ISA_TRANSLATE   10
#define MSGIRQ_ISA_HELD        11
#define MSGIRQ_ISA_HEARD       12
#define MSGIRQ_ISA_JOINED      13      /* the IRQ of the claim this call joined */
#define MSGIRQ_ISA_FULL        14      /* a claim that could be joined has all its members */
#define MSGIRQ_ISA_EXCLUDED    15      /* ruled out by the client's Exclude */
#define MSGIRQ_ISA_BUDGET      16      /* an upper input this copy may no longer translate:
                                          its budget for the boot is spent (section 2.9) */

#define MSGIRQ_ISA_SOURCE_FADT     0x01
#define MSGIRQ_ISA_SOURCE_MADT     0x02
#define MSGIRQ_ISA_SOURCE_ELCR     0x04
#define MSGIRQ_ISA_SOURCE_PIRQ     0x08
#define MSGIRQ_ISA_SOURCE_REGISTRY 0x10

typedef struct _MSGIRQ_ISA_RESULT {
    ULONG Size;                         /* sizeof(MSGIRQ_ISA_RESULT) */
    ULONG Irq;                          /* 0: none */
    ULONG Sci;                          /* the FADT's SCI; 0xFFFFFFFF: no FADT read */
    ULONG Sources;                      /* MSGIRQ_ISA_SOURCE_* */
    ULONG Members;                      /* functions on the claim after the call */
    UCHAR Reason[16];                   /* MSGIRQ_ISA_* per IRQ */
    ULONG Detail[16];
    /* Since the second pool; written only when Size reaches them. */
    ULONG PoolsScanned;                 /* MSGIRQ_POOL_*: the pools this call evaluated */
    ULONG HighFirst;                    /* the range as used; 0: the upper pool not chosen */
    ULONG HighLast;
    ULONG HighIoApics;                  /* the IOAPICs in scope, as used */
    ULONG HighIoApicCount;              /* the MADT's I/O APIC entries */
    ULONG HighEnd;                      /* the highest input in scope the HAL translated;
                                           0xFFFFFFFF: none, or the pool was not evaluated */
    ULONG HighLastInput;                /* the upper input HighDetail belongs to; 0: none */
    ULONG HighDetail;                   /* the claimed upper input's vector, else the detail
                                           of the last upper input tried */
    UCHAR HighReason[256];              /* MSGIRQ_ISA_* per input; entries 0..23 unused */
    ULONG HighBudget;                   /* the budget as used; 0: the upper pool not chosen */
    ULONG HighNew;                      /* the upper inputs this call newly charged to the
                                           driver (record 05, section 2.9) */
    ULONG HighSpent;                    /* the upper inputs charged to the driver for the
                                           boot after the call */
} MSGIRQ_ISA_RESULT;
#define MSGIRQ_ISA_RESULT_V1_SIZE ((ULONG)FIELD_OFFSET(MSGIRQ_ISA_RESULT, PoolsScanned))

typedef struct _MSGIRQ_ISA_STATE {
    ULONG Size;                         /* sizeof(MSGIRQ_ISA_STATE) */
    ULONG Braked;                       /* 0, or 1: the brake has tripped */
    ULONG Entries;
    ULONG Stray;                        /* entries the client did not claim */
    ULONG Run;                          /* unclaimed entries since the last claimed one */
    ULONG MemberEntries;                /* the calling connection's own, since its connect */
    ULONG MemberStray;
} MSGIRQ_ISA_STATE;
#define MSGIRQ_ISA_STATE_V1_SIZE 20     /* without the two member counters */
#endif

/* The event history (design record 08): one event, as MsgIrqReadHistory
 * copies it and as the published History value holds it. 32 bytes, the
 * same on x86 and amd64 and in msi4vxd. */
typedef struct _MSGIRQ_EVENT {
    ULONG Sequence;                     /* 1, 2, ...; a fold takes a new one; 0: none */
    ULONG TimeLow;                      /* the time, in MSGIRQ_HISTORY.TimeUnit */
    ULONG TimeHigh;
    USHORT Kind;                        /* MSGIRQ_EVENT_* in bits 0-7; MSGIRQ_EVENT_COPY */
    USHORT Location;                    /* bus << 8 | dev << 3 | func; 0 with MSGIRQ_EVENT_COPY */
    ULONG Arg[4];                       /* as the kind says (record 08, section 3) */
} MSGIRQ_EVENT;

typedef struct _MSGIRQ_HISTORY {
    ULONG Size;                         /* sizeof(MSGIRQ_HISTORY) */
    ULONG Capacity;                     /* the ring's records */
    ULONG Newest;                       /* the newest event's Sequence; 0: none yet */
    ULONG Count;                        /* records copied */
    ULONG Skipped;                      /* records after After the ring overwrote (at most) */
    ULONG TimeUnit;                     /* MSGIRQ_TIME_* or nanoseconds per unit */
    ULONG Overwritten;                  /* records the ring overwrote since the copy started */
    ULONG Written;                      /* records written since the copy started */
} MSGIRQ_HISTORY;

#define MSGIRQ_EVENT_KIND(k)     ((k) & 0xFF)   /* the kind, without the flags */
#define MSGIRQ_EVENT_COPY        0x8000         /* Kind: the copy's own event, no function */
#define MSGIRQ_TIME_NONE         0UL            /* no time stamps */
#define MSGIRQ_TIME_CYCLES       0xFFFFFFFFUL   /* processor cycles, rate not known (msi4vxd) */
#define MSGIRQ_TIME_NT           100UL          /* 100 ns: the interrupt time */

/* Kinds 1-63 are shared with msi4vxd; 64-127 are msi4vxd's own, 128-191
 * msi4nt's own, 192-255 reserved (record 08, section 3). */
#define MSGIRQ_EVENT_INIT        1
#define MSGIRQ_EVENT_CONNECT     2
#define MSGIRQ_EVENT_DISCONNECT  3
#define MSGIRQ_EVENT_REARM       4
#define MSGIRQ_EVENT_CLAIM       5
#define MSGIRQ_EVENT_CLAIM_WHY   6
#define MSGIRQ_EVENT_CLAIM_HIGH  7
#define MSGIRQ_EVENT_RELEASE     8
#define MSGIRQ_EVENT_REFUSED     9
#define MSGIRQ_EVENT_KEPT        10
#define MSGIRQ_EVENT_BRAKE       11
#define MSGIRQ_EVENT_STRAY       12
#define MSGIRQ_EVENT_SWALLOWED   13
#define MSGIRQ_EVENT_PUBFAIL     14
#define MSGIRQ_EVENT_CLIENT      15     /* msi4vxd's; msi4nt writes none */
#define MSGIRQ_EVENT_CLAIM_STOP  16     /* msi4nt: where a claim stopped, and why */
#define MSGIRQ_EVENT_ABSENT_ONLY 128    /* msi4nt: a disconnect with MSGIRQ_DISCONNECT_ABSENT_ONLY */

/* REFUSED: the call (Arg1) and the detail kind (Arg2). */
#define MSGIRQ_CALL_CONNECT      1
#define MSGIRQ_CALL_DISCONNECT   2
#define MSGIRQ_CALL_REARM        3
#define MSGIRQ_CALL_ISA_CLAIM    4
#define MSGIRQ_CALL_ISA_RELEASE  5
#define MSGIRQ_CALL_QUERY        6
#define MSGIRQ_CALL_ISA_QUERY    7
#define MSGIRQ_CALL_READ_HISTORY 8
#define MSGIRQ_CALL_INITIALIZE   9
#define MSGIRQ_CALL_ARM          10
#define MSGIRQ_CALL_DISARM       11

#define MSGIRQ_DETAIL_NONE       0
#define MSGIRQ_DETAIL_NTSTATUS   1
#define MSGIRQ_DETAIL_CONFIG     2      /* the configuration offset that failed */
#define MSGIRQ_DETAIL_MAP        3      /* the physical address (low 32 bits) not mapped */
#define MSGIRQ_DETAIL_HAL        4      /* a HAL reason (record 07, section 6) */
#define MSGIRQ_DETAIL_IRQL       5      /* the caller's IRQL */
#define MSGIRQ_DETAIL_VERSION    6
#define MSGIRQ_DETAIL_CHECK      7
#define MSGIRQ_DETAIL_STEP       8

#if defined(MSI4NT_X86)
#pragma pack(pop)
#endif

/* Layout checks (docs/using/api.md, "Binary layout"). */
#define MSI4NT_C_ASSERT(n, e) typedef char MsgIrqAssert_##n[(e) ? 1 : -1]
MSI4NT_C_ASSERT(event_size, sizeof(MSGIRQ_EVENT) == 32);
MSI4NT_C_ASSERT(event_kind, FIELD_OFFSET(MSGIRQ_EVENT, Kind) == 12);
MSI4NT_C_ASSERT(event_arg, FIELD_OFFSET(MSGIRQ_EVENT, Arg) == 16);
MSI4NT_C_ASSERT(history_size, sizeof(MSGIRQ_HISTORY) == 32);
#if defined(MSI4NT_X86)
MSI4NT_C_ASSERT(entry_size, sizeof(MSGIRQ_MESSAGE_INFO_ENTRY) == 24);
MSI4NT_C_ASSERT(entry_object, FIELD_OFFSET(MSGIRQ_MESSAGE_INFO_ENTRY, InterruptObject) == 20);
MSI4NT_C_ASSERT(info_entries, FIELD_OFFSET(MSGIRQ_MESSAGE_INFO, MessageInfo) == 8);
MSI4NT_C_ASSERT(line_size, sizeof(MSGIRQ_LINE_RESOURCE) == 20);
MSI4NT_C_ASSERT(params_size, sizeof(MSGIRQ_CONNECT_PARAMS) == 80);
MSI4NT_C_ASSERT(host_size, sizeof(MSGIRQ_HOST) == 20);
MSI4NT_C_ASSERT(access_size, sizeof(MSGIRQ_CONFIG_ACCESS) == 12);
#if defined(MSI4NT_ISA_LINE)
MSI4NT_C_ASSERT(isa_size_v1, FIELD_OFFSET(MSGIRQ_ISA_PARAMS, Pools) == 48);
MSI4NT_C_ASSERT(isa_size, sizeof(MSGIRQ_ISA_PARAMS) == 108);
MSI4NT_C_ASSERT(isa_result_v1, FIELD_OFFSET(MSGIRQ_ISA_RESULT, PoolsScanned) == 100);
MSI4NT_C_ASSERT(isa_result, sizeof(MSGIRQ_ISA_RESULT) == 400);
MSI4NT_C_ASSERT(isa_state, sizeof(MSGIRQ_ISA_STATE) == 28);
MSI4NT_C_ASSERT(isa_state_v1, FIELD_OFFSET(MSGIRQ_ISA_STATE, MemberEntries) == MSGIRQ_ISA_STATE_V1_SIZE);
#endif
#else
MSI4NT_C_ASSERT(entry_size, sizeof(MSGIRQ_MESSAGE_INFO_ENTRY) == 40);
MSI4NT_C_ASSERT(entry_object, FIELD_OFFSET(MSGIRQ_MESSAGE_INFO_ENTRY, InterruptObject) == 32);
MSI4NT_C_ASSERT(info_entries, FIELD_OFFSET(MSGIRQ_MESSAGE_INFO, MessageInfo) == 8);
#if defined(MSI4NT_ISA_LINE)
MSI4NT_C_ASSERT(isa_size_v1, FIELD_OFFSET(MSGIRQ_ISA_PARAMS, Pools) == 56);
MSI4NT_C_ASSERT(isa_result_v1, FIELD_OFFSET(MSGIRQ_ISA_RESULT, PoolsScanned) == 100);
#endif
#endif

/* ------------------------------------------------------------------ */
/* Functions (docs/using/api.md, "Functions")                          */
/* ------------------------------------------------------------------ */

ULONG MsgIrqInitialize(const MSGIRQ_HOST *Host);
ULONG MsgIrqGetMode(VOID);
ULONG MsgIrqQuery(const MSGIRQ_CONNECT_PARAMS *Params, MSGIRQ_CAPABILITIES *Caps);
ULONG MsgIrqConnect(const MSGIRQ_CONNECT_PARAMS *Params, PMSGIRQ_MESSAGE_INFO *Info);
ULONG MsgIrqArm(PMSGIRQ_MESSAGE_INFO Info);
ULONG MsgIrqDisarm(PMSGIRQ_MESSAGE_INFO Info);
ULONG MsgIrqTranslateLine(ULONG Bus, ULONG BusInterruptLevel,
                          ULONG BusInterruptVector,
                          MSGIRQ_LINE_RESOURCE *LineInterrupt);
ULONG MsgIrqDisconnect(PMSGIRQ_MESSAGE_INFO Info, ULONG Flags);
ULONG MsgIrqReArm(PMSGIRQ_MESSAGE_INFO Info);
/* The event history (design record 08): any IRQL. */
ULONG MsgIrqReadHistory(ULONG After, MSGIRQ_EVENT *Events, ULONG MaxEvents,
                        MSGIRQ_HISTORY *History);

#if defined(MSI4NT_ISA_LINE)
/* Experimental (design record 05, section 2): a vector for a function
 * with no private INTx line, from a free ISA interrupt claimed for it. */
ULONG MsgIrqIsaClaim(const MSGIRQ_CONNECT_PARAMS *Params, const MSGIRQ_ISA_PARAMS *Options,
                     MSGIRQ_ISA_RESULT *Result);
ULONG MsgIrqIsaRelease(const MSGIRQ_CONNECT_PARAMS *Params);
ULONG MsgIrqIsaQuery(PMSGIRQ_MESSAGE_INFO Info, MSGIRQ_ISA_STATE *State);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MSI4NT_H */

/* ==================================================================== */
/* Implementation                                                        */
/* ==================================================================== */

#if defined(MSI4NT_IMPLEMENTATION) && !defined(MSI4NT_IMPLEMENTED)
#define MSI4NT_IMPLEMENTED

#ifdef __cplusplus
extern "C" {
#endif

/* The signature: one constant string per image, in read-only data, that
 * nothing reads or prints. The toolchain is told to keep it although
 * nothing refers to it: the Microsoft linker's /include, written into the
 * object file; with any other compiler a volatile pointer to it that
 * MsgIrqInitialize, which every client links, reads (and the "used"
 * attribute where GCC or Clang has it). There is no macro to leave it out. */
extern const char MsgIrqSignature[];
#if defined(__GNUC__) && !defined(_MSC_VER)
const char MsgIrqSignature[] __attribute__((used)) = MSI4NT_SIGNATURE;
#else
const char MsgIrqSignature[] = MSI4NT_SIGNATURE;
#endif
#if defined(_MSC_VER)
#if defined(MSI4NT_X86)
#pragma comment(linker, "/include:_MsgIrqSignature")
#else
#pragma comment(linker, "/include:MsgIrqSignature")
#endif
#else
static const char *volatile MsgIrqpSignatureRef = MsgIrqSignature;
#endif

#ifndef MSI4NT_POOL_TAG
#define MSI4NT_POOL_TAG 'qIsM'
#endif

#ifndef MSI4NT_TRACE
#define MSI4NT_TRACE(args)
#endif

/* ------------------------------------------------------------------ */
/* Part A: the pure core. Configuration-space and MSI-X table access go */
/* through MSGIRQP_IO, so the host tests run these exact functions      */
/* against a simulated function (test/host). Design record 04.          */
/* ------------------------------------------------------------------ */

#define MSGIRQP_CFG_VENDOR      0x00
#define MSGIRQP_CFG_COMMAND     0x04
#define MSGIRQP_CFG_STATUS      0x06
#define MSGIRQP_CFG_HEADER      0x0E
#define MSGIRQP_CFG_BAR0        0x10
#define MSGIRQP_CFG_CAPPTR      0x34

#define MSGIRQP_CMD_MSE         0x0002
#define MSGIRQP_CMD_BME         0x0004
#define MSGIRQP_CMD_INTD        0x0400
#define MSGIRQP_STATUS_CAPLIST  0x0010

#define MSGIRQP_CAPID_PM        0x01
#define MSGIRQP_CAPID_MSI       0x05
#define MSGIRQP_CAPID_MSIX      0x11

#define MSGIRQP_PM_PMCSR        4       /* PMCSR: bits 1:0 PowerState, 0 = D0 */
#define MSGIRQP_PM_STATE_MASK   0x0003

#define MSGIRQP_MSI_ENABLE      0x0001
#define MSGIRQP_MSI_MMC_MASK    0x000E
#define MSGIRQP_MSI_MME_MASK    0x0070
#define MSGIRQP_MSI_64BIT       0x0080
#define MSGIRQP_MSI_PVM         0x0100

#define MSGIRQP_MSIX_SIZE_MASK  0x07FF
#define MSGIRQP_MSIX_FMASK      0x4000
#define MSGIRQP_MSIX_ENABLE     0x8000

#define MSGIRQP_MSIX_ENTRY_BYTES 16
#define MSGIRQP_MSIX_VCTRL_MASK  0x00000001UL

/* Configuration access: each routine returns TRUE on success. */
typedef struct _MSGIRQP_IO {
    PVOID Context;
    BOOLEAN (*Read)(PVOID Context, ULONG Offset, PVOID Buffer, ULONG Length);
    BOOLEAN (*Write)(PVOID Context, ULONG Offset, PVOID Buffer, ULONG Length);
} MSGIRQP_IO;

/* What the capability walk found (design record 04, section 1). */
typedef struct _MSGIRQP_CAPS {
    ULONG MsiOffset;            /* 0 = none */
    ULONG MsixOffset;           /* 0 = none */
    ULONG PmOffset;             /* the power management capability; 0 = none */
    USHORT MsiControl;
    USHORT MsixControl;
    USHORT Command;
    USHORT Vendor;
} MSGIRQP_CAPS;

static BOOLEAN MsgIrqpRead8(const MSGIRQP_IO *Io, ULONG Off, UCHAR *V)
{
    return Io->Read(Io->Context, Off, V, 1);
}

static BOOLEAN MsgIrqpRead16(const MSGIRQP_IO *Io, ULONG Off, USHORT *V)
{
    return Io->Read(Io->Context, Off, V, 2);
}

static BOOLEAN MsgIrqpRead32(const MSGIRQP_IO *Io, ULONG Off, ULONG *V)
{
    return Io->Read(Io->Context, Off, V, 4);
}

static BOOLEAN MsgIrqpWrite16(const MSGIRQP_IO *Io, ULONG Off, USHORT V)
{
    return Io->Write(Io->Context, Off, &V, 2);
}

static BOOLEAN MsgIrqpWrite32(const MSGIRQP_IO *Io, ULONG Off, ULONG V)
{
    return Io->Write(Io->Context, Off, &V, 4);
}

/*
 * The capability walk: the status register's Capabilities List bit, the
 * pointer at 34h, each pointer's low two bits masked, to a next pointer of
 * zero. Returns MSGIRQ_STATUS_OK with Caps filled (offsets 0 when absent)
 * only for a complete walk; MSGIRQ_STATUS_CONFIG_ACCESS when a read fails
 * or the list is malformed: a nonzero pointer below 40h, or more than 48
 * entries (a loop). An offset of 0 therefore means "not in the list",
 * never "not reached": the power-state rule's "no PM capability counts as
 * D0" and the choice of MSI or MSI-X rely on it (design record 04,
 * section 7). A function that reads FFFFh as its vendor ID is reported
 * through Caps->Vendor; the caller decides.
 */
static ULONG MsgIrqpWalk(const MSGIRQP_IO *Io, MSGIRQP_CAPS *Caps)
{
    USHORT status;
    UCHAR ptr;
    UCHAR id;
    ULONG count;

    Caps->MsiOffset = 0;
    Caps->MsixOffset = 0;
    Caps->PmOffset = 0;
    Caps->MsiControl = 0;
    Caps->MsixControl = 0;
    Caps->Command = 0;
    if (!MsgIrqpRead16(Io, MSGIRQP_CFG_VENDOR, &Caps->Vendor)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (Caps->Vendor == 0xFFFF) {
        return MSGIRQ_STATUS_OK;
    }
    if (!MsgIrqpRead16(Io, MSGIRQP_CFG_COMMAND, &Caps->Command) ||
        !MsgIrqpRead16(Io, MSGIRQP_CFG_STATUS, &status)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if ((status & MSGIRQP_STATUS_CAPLIST) == 0) {
        return MSGIRQ_STATUS_OK;
    }
    if (!MsgIrqpRead8(Io, MSGIRQP_CFG_CAPPTR, &ptr)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    ptr &= 0xFC;
    for (count = 0; count < 48 && ptr >= 0x40; count++) {
        if (!MsgIrqpRead8(Io, ptr, &id)) {
            return MSGIRQ_STATUS_CONFIG_ACCESS;
        }
        if (id == MSGIRQP_CAPID_MSI && Caps->MsiOffset == 0) {
            Caps->MsiOffset = ptr;
            if (!MsgIrqpRead16(Io, ptr + 2UL, &Caps->MsiControl)) {
                return MSGIRQ_STATUS_CONFIG_ACCESS;
            }
        } else if (id == MSGIRQP_CAPID_MSIX && Caps->MsixOffset == 0) {
            Caps->MsixOffset = ptr;
            if (!MsgIrqpRead16(Io, ptr + 2UL, &Caps->MsixControl)) {
                return MSGIRQ_STATUS_CONFIG_ACCESS;
            }
        } else if (id == MSGIRQP_CAPID_PM && Caps->PmOffset == 0) {
            Caps->PmOffset = ptr;
        }
        if (!MsgIrqpRead8(Io, ptr + 1UL, &ptr)) {
            return MSGIRQ_STATUS_CONFIG_ACCESS;
        }
        ptr &= 0xFC;
    }
    if (ptr != 0) {
        MSI4NT_TRACE(("msi4nt: capability list malformed at %02x\n", (ULONG)ptr));
        return MSGIRQ_STATUS_CONFIG_ACCESS;     /* fail closed: not a proof of absence */
    }
    return MSGIRQ_STATUS_OK;
}

static VOID MsgIrqpCapsToPublic(const MSGIRQP_CAPS *Caps, MSGIRQ_CAPABILITIES *Out)
{
    Out->MsiMessages = 0;
    Out->MsixMessages = 0;
    Out->Flags = 0;
    if (Caps->MsiOffset != 0) {
        Out->MsiMessages = 1UL << ((Caps->MsiControl & MSGIRQP_MSI_MMC_MASK) >> 1);
        if (Out->MsiMessages > 32) {
            Out->MsiMessages = 32;
        }
        Out->Flags |= MSGIRQ_CAP_MSI;
        if (Caps->MsiControl & MSGIRQP_MSI_64BIT) {
            Out->Flags |= MSGIRQ_CAP_64BIT;
        }
        if (Caps->MsiControl & MSGIRQP_MSI_PVM) {
            Out->Flags |= MSGIRQ_CAP_PER_VECTOR_MASK;
        }
    }
    if (Caps->MsixOffset != 0) {
        Out->MsixMessages = (Caps->MsixControl & MSGIRQP_MSIX_SIZE_MASK) + 1UL;
        Out->Flags |= MSGIRQ_CAP_MSIX;
    }
}

/* The message encoding (docs/msi-info/background.md): fixed delivery,
 * physical destination, edge. */
static ULONG MsgIrqpAddress(ULONG ApicId)
{
    return 0xFEE00000UL | ((ApicId & 0xFFUL) << 12);
}

static ULONG MsgIrqpData(ULONG Vector)
{
    return Vector & 0xFFUL;
}

/* Offset of the MSI data register and of the mask bits (0 = none). */
static ULONG MsgIrqpMsiDataOffset(ULONG Cap, USHORT Control)
{
    return Cap + ((Control & MSGIRQP_MSI_64BIT) ? 0x0CUL : 0x08UL);
}

static ULONG MsgIrqpMsiMaskOffset(ULONG Cap, USHORT Control)
{
    if ((Control & MSGIRQP_MSI_PVM) == 0) {
        return 0;
    }
    return Cap + ((Control & MSGIRQP_MSI_64BIT) ? 0x10UL : 0x0CUL);
}

/*
 * The Interrupt Disable probe (design record 04, section 4): set it and
 * read it back. Returns TRUE when both worked; *Capable says whether the
 * bit held. The bit is not restored: the arm sets it before the enable and
 * the teardown keeps it set (owner, 2026-11-10), so from the probe on it
 * stays set on a function that implements it.
 */
static BOOLEAN MsgIrqpProbeIntd(const MSGIRQP_IO *Io, BOOLEAN *Capable)
{
    USHORT cmd;

    *Capable = FALSE;
    if (!MsgIrqpRead16(Io, MSGIRQP_CFG_COMMAND, &cmd)) {
        return FALSE;
    }
    if ((cmd & MSGIRQP_CMD_INTD) == 0) {
        if (!MsgIrqpWrite16(Io, MSGIRQP_CFG_COMMAND, (USHORT)(cmd | MSGIRQP_CMD_INTD)) ||
            !MsgIrqpRead16(Io, MSGIRQP_CFG_COMMAND, &cmd)) {
            return FALSE;
        }
    }
    *Capable = (cmd & MSGIRQP_CMD_INTD) ? TRUE : FALSE;
    return TRUE;
}

/* Sets or clears command-register bits with a read-modify-write of the
 * 16-bit register only (never the status register beside it). */
static BOOLEAN MsgIrqpCommandBits(const MSGIRQP_IO *Io, USHORT Set, USHORT Clear)
{
    USHORT cmd;
    USHORT next;

    if (!MsgIrqpRead16(Io, MSGIRQP_CFG_COMMAND, &cmd)) {
        return FALSE;
    }
    next = (USHORT)((cmd | Set) & ~Clear);
    if (next == cmd) {
        return TRUE;
    }
    return MsgIrqpWrite16(Io, MSGIRQP_CFG_COMMAND, next);
}

/* One aligned 32-bit access to device memory or to the local APIC (design
 * record 04, section 3): the PCI specification asks for DWORD or QWORD
 * accesses to the MSI-X table, and a C expression on a volatile ULONG may
 * still be narrowed to a byte (Visual C++ 6.0 turns a test of bit 0 into a
 * one-byte test). On x86 the access is an inline 32-bit mov; on amd64 a
 * volatile doubleword moved through a volatile local. The value is then
 * worked on in a register. A host test may supply its own accessors. */
#if !defined(MSGIRQP_MMIO_READ)
#if defined(MSI4NT_X86) && defined(_MSC_VER)
static ULONG MsgIrqpMmioRead(volatile ULONG *Register)
{
    ULONG v;

    __asm {
        mov ecx, Register
        mov eax, dword ptr [ecx]
        mov v, eax
    }
    return v;
}

static VOID MsgIrqpMmioWrite(volatile ULONG *Register, ULONG Value)
{
    __asm {
        mov ecx, Register
        mov eax, Value
        mov dword ptr [ecx], eax
    }
}
#else
static ULONG MsgIrqpMmioRead(volatile ULONG *Register)
{
    volatile ULONG v = *Register;

    return v;
}

static VOID MsgIrqpMmioWrite(volatile ULONG *Register, ULONG Value)
{
    *Register = Value;
}
#endif
#define MSGIRQP_MMIO_READ(r)     MsgIrqpMmioRead(r)
#define MSGIRQP_MMIO_WRITE(r, v) MsgIrqpMmioWrite((r), (v))
#endif

/* The arm/disarm target: which capability, where, and the MSI-X table. */
typedef struct _MSGIRQP_TARGET {
    ULONG Type;                 /* MSGIRQ_MESSAGE_MSI or _MSIX */
    ULONG Cap;                  /* capability offset */
    USHORT Control;             /* Message Control as read at connect */
    BOOLEAN IntdCapable;        /* the function implements Interrupt Disable */
    volatile ULONG *Table;      /* MSI-X: the mapped table */
    ULONG TableEntries;
    ULONG PmCap;                /* the power management capability; 0 = none */
} MSGIRQP_TARGET;

/* Is the function in D0 (design record 04, section 7)? The power
 * management capability's PowerState, read now; a function without the
 * capability supports only D0 and D3cold and answers configuration reads
 * only in D0, so it counts as in D0; a failed read counts as not in D0. */
static BOOLEAN MsgIrqpInD0(const MSGIRQP_IO *Io, ULONG PmCap)
{
    USHORT pmcsr;

    if (PmCap == 0) {
        return TRUE;
    }
    if (!MsgIrqpRead16(Io, PmCap + MSGIRQP_PM_PMCSR, &pmcsr)) {
        return FALSE;
    }
    return ((pmcsr & MSGIRQP_PM_STATE_MASK) == 0) ? TRUE : FALSE;
}

/* May the MSI-X table be touched now: Memory Space Enable set and the
 * function in D0 (record 04, sections 3 and 7). A failed read: no. */
static BOOLEAN MsgIrqpTableReachable(const MSGIRQP_IO *Io, const MSGIRQP_TARGET *T)
{
    USHORT cmd;

    if (T->Table == NULL || !MsgIrqpRead16(Io, MSGIRQP_CFG_COMMAND, &cmd) || (cmd & MSGIRQP_CMD_MSE) == 0) {
        return FALSE;
    }
    return MsgIrqpInD0(Io, T->PmCap);
}

/* An arm refused before any write because the table cannot be reached
 * (record 04, section 7): the caller does not disarm, and reports
 * MSGIRQ_STATUS_CONFIG_ACCESS. Never returned by a public call. */
#define MSGIRQP_STATUS_UNREACHED 0x100UL

/* The command bits an armed function carries: Bus Master Enable, and
 * Interrupt Disable where the function implements it. */
static USHORT MsgIrqpArmedCommand(const MSGIRQP_TARGET *T)
{
    return (USHORT)(MSGIRQP_CMD_BME | (T->IntdCapable ? MSGIRQP_CMD_INTD : 0));
}

/* Do the armed command bits read back set? */
static BOOLEAN MsgIrqpCommandArmed(const MSGIRQP_IO *Io, const MSGIRQP_TARGET *T)
{
    USHORT cmd;
    USHORT want = MsgIrqpArmedCommand(T);

    if (!MsgIrqpRead16(Io, MSGIRQP_CFG_COMMAND, &cmd)) {
        return FALSE;
    }
    return ((cmd & want) == want) ? TRUE : FALSE;
}

/* Interrupt Disable set (it stays set: owner, 2026-11-10), where the
 * function implements it, before an enable is cleared; confirmed by
 * read-back, so a disarm never succeeds with the bit clear. */
static BOOLEAN MsgIrqpKeepIntd(const MSGIRQP_IO *Io, const MSGIRQP_TARGET *T)
{
    USHORT cmd;

    if (!T->IntdCapable) {
        return TRUE;
    }
    if (!MsgIrqpCommandBits(Io, MSGIRQP_CMD_INTD, 0) ||
        !MsgIrqpRead16(Io, MSGIRQP_CFG_COMMAND, &cmd)) {
        return FALSE;
    }
    return (cmd & MSGIRQP_CMD_INTD) ? TRUE : FALSE;
}

/* MSI disarm (design record 04, section 2): Interrupt Disable kept set,
 * Enable cleared, then the read that flushes the function's posted
 * writes. Returns TRUE when Enable read back clear. */
static BOOLEAN MsgIrqpMsiDisarm(const MSGIRQP_IO *Io, MSGIRQP_TARGET *T)
{
    USHORT ctl;

    if (!MsgIrqpKeepIntd(Io, T) || !MsgIrqpRead16(Io, T->Cap + 2UL, &ctl)) {
        return FALSE;
    }
    ctl = (USHORT)(ctl & ~(MSGIRQP_MSI_ENABLE | MSGIRQP_MSI_MME_MASK));
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, ctl)) {
        return FALSE;
    }
    if (!MsgIrqpRead16(Io, T->Cap + 2UL, &ctl)) {
        return FALSE;
    }
    return (ctl & MSGIRQP_MSI_ENABLE) ? FALSE : TRUE;
}

/* MSI-X disarm (design record 04, section 3). */
static BOOLEAN MsgIrqpMsixDisarm(const MSGIRQP_IO *Io, MSGIRQP_TARGET *T)
{
    USHORT ctl;

    if (!MsgIrqpKeepIntd(Io, T) || !MsgIrqpRead16(Io, T->Cap + 2UL, &ctl)) {
        return FALSE;
    }
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, (USHORT)(ctl | MSGIRQP_MSIX_FMASK))) {
        return FALSE;
    }
    ctl = (USHORT)(ctl & ~(MSGIRQP_MSIX_ENABLE | MSGIRQP_MSIX_FMASK));
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, ctl)) {
        return FALSE;
    }
    if (!MsgIrqpRead16(Io, T->Cap + 2UL, &ctl) || (ctl & MSGIRQP_MSIX_ENABLE)) {
        return FALSE;
    }
    /* Tidiness for the next owner, only where the table decodes and the
     * function is in D0; it does not affect the result (design record 04,
     * sections 3 and 7). */
    if (MsgIrqpTableReachable(Io, T)) {
        MSGIRQP_MMIO_WRITE(&T->Table[3], MSGIRQP_MMIO_READ(&T->Table[3]) | MSGIRQP_MSIX_VCTRL_MASK);
    } else {
        MSI4NT_TRACE(("msi4nt: MSI-X table not reachable, its tidiness write left out\n"));
    }
    return TRUE;
}

static BOOLEAN MsgIrqpDisarm(const MSGIRQP_IO *Io, MSGIRQP_TARGET *T)
{
    if (T->Type == MSGIRQ_MESSAGE_MSIX) {
        return MsgIrqpMsixDisarm(Io, T);
    }
    return MsgIrqpMsiDisarm(Io, T);
}

/*
 * MSI arm (design record 04, section 2), from whatever state the function
 * is in (connect, or a re-arm after D3 with a reset capability and a
 * command register restored by the bus driver): MSI Enable cleared, the
 * address, the upper address (zero) where the capability is 64-bit, the
 * data, the mask bits, Interrupt Disable and Bus Master Enable, then MSI
 * Enable, then the read-backs. Returns MSGIRQ_STATUS_OK, or
 * MSGIRQ_STATUS_CONFIG_ACCESS for a failed write, a capability that is not
 * the one found at connect (an all-ones function among them; checked
 * before any write), or a read-back that did not take (the caller
 * disarms).
 */
#define MSGIRQP_MSI_FIXED_BITS  (MSGIRQP_MSI_64BIT | MSGIRQP_MSI_PVM)
static ULONG MsgIrqpMsiArm(const MSGIRQP_IO *Io, MSGIRQP_TARGET *T,
                           ULONG Address, ULONG Data)
{
    USHORT ctl;
    USHORT data16;
    ULONG addr;
    ULONG maskOff;
    UCHAR id;

    if (!MsgIrqpRead8(Io, T->Cap, &id) || id != MSGIRQP_CAPID_MSI ||
        !MsgIrqpRead16(Io, T->Cap + 2UL, &ctl) ||
        (ctl & MSGIRQP_MSI_FIXED_BITS) != (T->Control & MSGIRQP_MSI_FIXED_BITS)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    ctl = (USHORT)(ctl & ~(MSGIRQP_MSI_ENABLE | MSGIRQP_MSI_MME_MASK));
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, ctl) ||
        !MsgIrqpWrite32(Io, T->Cap + 4UL, Address)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if ((ctl & MSGIRQP_MSI_64BIT) && !MsgIrqpWrite32(Io, T->Cap + 8UL, 0)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (!MsgIrqpWrite16(Io, MsgIrqpMsiDataOffset(T->Cap, ctl), (USHORT)Data)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    maskOff = MsgIrqpMsiMaskOffset(T->Cap, ctl);
    if (maskOff != 0 && !MsgIrqpWrite32(Io, maskOff, 0xFFFFFFFEUL)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (!MsgIrqpCommandBits(Io, MsgIrqpArmedCommand(T), 0)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, (USHORT)(ctl | MSGIRQP_MSI_ENABLE))) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (!MsgIrqpRead16(Io, T->Cap + 2UL, &ctl) ||
        !MsgIrqpRead32(Io, T->Cap + 4UL, &addr) ||
        !MsgIrqpRead16(Io, MsgIrqpMsiDataOffset(T->Cap, ctl), &data16)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (ctl & MSGIRQP_MSI_64BIT) {
        ULONG upper;

        if (!MsgIrqpRead32(Io, T->Cap + 8UL, &upper) || upper != 0) {
            return MSGIRQ_STATUS_CONFIG_ACCESS;
        }
    }
    if ((ctl & MSGIRQP_MSI_ENABLE) == 0 || (ctl & MSGIRQP_MSI_MME_MASK) != 0 ||
        addr != Address || data16 != (USHORT)Data || !MsgIrqpCommandArmed(Io, T)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (maskOff != 0) {
        ULONG mask;

        /* Message 0 must have been left unmasked by the write above. */
        if (!MsgIrqpRead32(Io, maskOff, &mask) || (mask & 1UL) != 0) {
            return MSGIRQ_STATUS_CONFIG_ACCESS;
        }
    }
    return MSGIRQ_STATUS_OK;
}

/*
 * MSI-X arm (design record 04, section 3): MSI-X Enable cleared under
 * Function Mask, then Enable with Function Mask (nothing can be sent), every
 * entry masked, entry 0 written and unmasked and read back, Interrupt
 * Disable and Bus Master Enable, then Function Mask cleared (the effective
 * enable), read-backs.
 */
static ULONG MsgIrqpMsixArm(const MSGIRQP_IO *Io, MSGIRQP_TARGET *T,
                            ULONG Address, ULONG Data)
{
    USHORT ctl;
    ULONG i;
    ULONG vctl;
    UCHAR id;

    if (!MsgIrqpTableReachable(Io, T)) {
        MSI4NT_TRACE(("msi4nt: MSI-X table not reachable, nothing written\n"));
        return MSGIRQP_STATUS_UNREACHED;                /* nothing written (record 04, section 7) */
    }
    if (!MsgIrqpRead8(Io, T->Cap, &id) || id != MSGIRQP_CAPID_MSIX ||
        !MsgIrqpRead16(Io, T->Cap + 2UL, &ctl)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    ctl = (USHORT)((ctl | MSGIRQP_MSIX_FMASK) & ~MSGIRQP_MSIX_ENABLE);
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, ctl)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    ctl = (USHORT)(ctl | MSGIRQP_MSIX_ENABLE);
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, ctl)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    for (i = 0; i < T->TableEntries; i++) {
        vctl = MSGIRQP_MMIO_READ(&T->Table[i * 4 + 3]);
        MSGIRQP_MMIO_WRITE(&T->Table[i * 4 + 3], vctl | MSGIRQP_MSIX_VCTRL_MASK);
    }
    MSGIRQP_MMIO_WRITE(&T->Table[0], Address);
    MSGIRQP_MMIO_WRITE(&T->Table[1], 0);
    MSGIRQP_MMIO_WRITE(&T->Table[2], Data);
    vctl = MSGIRQP_MMIO_READ(&T->Table[3]);
    MSGIRQP_MMIO_WRITE(&T->Table[3], vctl & ~MSGIRQP_MSIX_VCTRL_MASK);
    vctl = MSGIRQP_MMIO_READ(&T->Table[3]);
    if ((vctl & MSGIRQP_MSIX_VCTRL_MASK) || MSGIRQP_MMIO_READ(&T->Table[0]) != Address ||
        MSGIRQP_MMIO_READ(&T->Table[1]) != 0 || MSGIRQP_MMIO_READ(&T->Table[2]) != Data) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (!MsgIrqpCommandBits(Io, MsgIrqpArmedCommand(T), 0)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    ctl = (USHORT)(ctl & ~MSGIRQP_MSIX_FMASK);
    if (!MsgIrqpWrite16(Io, T->Cap + 2UL, ctl) ||
        !MsgIrqpRead16(Io, T->Cap + 2UL, &ctl)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if ((ctl & MSGIRQP_MSIX_ENABLE) == 0 || (ctl & MSGIRQP_MSIX_FMASK) != 0 ||
        !MsgIrqpCommandArmed(Io, T)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    return MSGIRQ_STATUS_OK;
}

/* The arm for the target's type. Bus Master Enable and Interrupt Disable
 * are written by the arm itself, in one write before the enable, and the
 * command register is among its read-backs (design record 04, sections 2
 * and 3). */
static ULONG MsgIrqpArm(const MSGIRQP_IO *Io, MSGIRQP_TARGET *T,
                        ULONG Address, ULONG Data)
{
    if (T->Type == MSGIRQ_MESSAGE_MSIX) {
        return MsgIrqpMsixArm(Io, T, Address, Data);
    }
    return MsgIrqpMsiArm(Io, T, Address, Data);
}

/*
 * The absence proof (design record 01, section 2; owner, 2026-11-10): the
 * function's first dword (vendor and device ID) read MSGIRQP_ABSENT_READS
 * times through the connection's route, with Pause between reads, every
 * read succeeding and returning all ones. A failed read proves nothing.
 */
#define MSGIRQP_ABSENT_READS    3
#define MSGIRQP_ABSENT_PAUSE_MS 10
typedef VOID (*MSGIRQP_PAUSE)(VOID);
static BOOLEAN MsgIrqpProveAbsentId(const MSGIRQP_IO *Io, MSGIRQP_PAUSE Pause, ULONG *Last,
                                    BOOLEAN *Failed)
{
    ULONG i;
    ULONG id;

    *Failed = FALSE;
    for (i = 0; i < MSGIRQP_ABSENT_READS; i++) {
        if (i != 0) {
            Pause();
        }
        if (!MsgIrqpRead32(Io, MSGIRQP_CFG_VENDOR, &id)) {
            *Failed = TRUE;
            return FALSE;
        }
        *Last = id;
        if (id != 0xFFFFFFFFUL) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN MsgIrqpProveAbsent(const MSGIRQP_IO *Io, MSGIRQP_PAUSE Pause)
{
    ULONG last = 0;
    BOOLEAN failed;

    return MsgIrqpProveAbsentId(Io, Pause, &last, &failed);
}

/* Which MsgIrqDisconnect Flags are valid: QUARANTINE, ABSENT_OK or
 * ABSENT_ONLY, one at a time (0x1 is withdrawn). */
static BOOLEAN MsgIrqpDisconnectFlagsValid(ULONG Flags)
{
    if ((Flags & ~(ULONG)(MSGIRQ_DISCONNECT_QUARANTINE | MSGIRQ_DISCONNECT_ABSENT_OK |
                          MSGIRQ_DISCONNECT_ABSENT_ONLY)) != 0) {
        return FALSE;
    }
    return (Flags == 0 || Flags == MSGIRQ_DISCONNECT_QUARANTINE || Flags == MSGIRQ_DISCONNECT_ABSENT_OK ||
            Flags == MSGIRQ_DISCONNECT_ABSENT_ONLY) ? TRUE : FALSE;
}

/* An ABSENT_ONLY disconnect's outcome (record 08, kind 128). */
#define MSGIRQP_AO_ABSENT    0          /* proven absent, released */
#define MSGIRQP_AO_PRESENT   1          /* the function answered */
#define MSGIRQP_AO_READFAIL  2          /* a read failed */
#define MSGIRQP_AO_UNPROVEN  3          /* the proof did not hold */

/* ABSENT_ONLY's device side (record 01, section 2): configuration reads
 * only, never a write. TRUE when the function is proven absent; *Outcome
 * and *Last say what was found. */
static BOOLEAN MsgIrqpAbsentOnly(const MSGIRQP_IO *Io, MSGIRQP_PAUSE Pause, ULONG *Outcome,
                                 ULONG *Last)
{
    ULONG id;
    BOOLEAN failed;

    *Last = 0;
    if (!MsgIrqpRead32(Io, MSGIRQP_CFG_VENDOR, &id)) {
        *Outcome = MSGIRQP_AO_READFAIL;
        return FALSE;
    }
    *Last = id;                                         /* the last first dword read that succeeded */
    if ((id & 0xFFFFUL) != 0xFFFFUL) {
        *Outcome = MSGIRQP_AO_PRESENT;
        return FALSE;
    }
    if (MsgIrqpProveAbsentId(Io, Pause, Last, &failed)) {
        *Outcome = MSGIRQP_AO_ABSENT;
        return TRUE;
    }
    *Outcome = failed ? MSGIRQP_AO_READFAIL : MSGIRQP_AO_UNPROVEN;
    return FALSE;
}

/*
 * The device side of a disconnect without QUARANTINE (design record 01,
 * section 2). MSGIRQP_OFF_DISABLED: the function answered and its Enable
 * read back clear. MSGIRQP_OFF_ABSENT: its vendor ID read FFFFh,
 * ABSENT_OK was given and the absence proof held. MSGIRQP_OFF_NONE:
 * neither; the connection stays. A function that answered with a real
 * vendor ID is present, so a disarm of it that fails never leads to the
 * absence proof.
 */
#define MSGIRQP_OFF_NONE        0
#define MSGIRQP_OFF_DISABLED    1
#define MSGIRQP_OFF_ABSENT      2
static ULONG MsgIrqpTeardown(const MSGIRQP_IO *Io, MSGIRQP_TARGET *T, ULONG Flags,
                             MSGIRQP_PAUSE Pause)
{
    USHORT vendor;

    if (!MsgIrqpRead16(Io, MSGIRQP_CFG_VENDOR, &vendor)) {
        return MSGIRQP_OFF_NONE;
    }
    if (vendor != 0xFFFF) {
        return MsgIrqpDisarm(Io, T) ? MSGIRQP_OFF_DISABLED : MSGIRQP_OFF_NONE;
    }
    /* A single FFFFh is what a failed transaction returns too: not proof
     * of anything without the repeated reads. */
    if ((Flags & MSGIRQ_DISCONNECT_ABSENT_OK) != 0 && MsgIrqpProveAbsent(Io, Pause)) {
        return MSGIRQP_OFF_ABSENT;
    }
    return MSGIRQP_OFF_NONE;
}

/* Is either enable set? Returns FALSE on a failed read (with *Set TRUE,
 * the conservative answer). */
static BOOLEAN MsgIrqpEnabled(const MSGIRQP_IO *Io, const MSGIRQP_CAPS *Caps,
                              BOOLEAN *Set)
{
    USHORT ctl;

    *Set = TRUE;
    if (Caps->MsiOffset != 0) {
        if (!MsgIrqpRead16(Io, Caps->MsiOffset + 2UL, &ctl)) {
            return FALSE;
        }
        if (ctl & MSGIRQP_MSI_ENABLE) {
            return TRUE;
        }
    }
    if (Caps->MsixOffset != 0) {
        if (!MsgIrqpRead16(Io, Caps->MsixOffset + 2UL, &ctl)) {
            return FALSE;
        }
        if (ctl & MSGIRQP_MSIX_ENABLE) {
            return TRUE;
        }
    }
    *Set = FALSE;
    return TRUE;
}

/* Chooses the capability for MessageType (design record 04, section 1):
 * ANY takes MSI first. Returns 0 when none fits. */
static ULONG MsgIrqpChoose(const MSGIRQP_CAPS *Caps, ULONG MessageType)
{
    if ((MessageType == MSGIRQ_MESSAGE_ANY || MessageType == MSGIRQ_MESSAGE_MSI) &&
        Caps->MsiOffset != 0) {
        return MSGIRQ_MESSAGE_MSI;
    }
    if ((MessageType == MSGIRQ_MESSAGE_ANY || MessageType == MSGIRQ_MESSAGE_MSIX) &&
        Caps->MsixOffset != 0) {
        return MSGIRQ_MESSAGE_MSIX;
    }
    return 0;
}

/* The share-disposition rule (design record 01, section 2, step 3). The
 * CmResourceShare* values are the DDK's; restated so the pure core needs
 * no DDK header. */
#define MSGIRQP_SHARE_DEVICE_EXCLUSIVE 1
#if defined(MSI4NT_ISA_LINE)
#define MSGIRQP_FLAG_ISA MSGIRQ_CONNECT_FLAG_ISA_LINE
#else
#define MSGIRQP_FLAG_ISA 0
#endif
#define MSGIRQP_CONNECT_FLAGS \
    (MSGIRQ_CONNECT_FLAG_ATTACH | MSGIRQ_CONNECT_FLAG_PIN_PRIVATE | MSGIRQP_FLAG_ISA)
static BOOLEAN MsgIrqpPinAccepted(UCHAR ShareDisposition, ULONG Flags)
{
    if (Flags & MSGIRQ_CONNECT_FLAG_PIN_PRIVATE) {
        return TRUE;
    }
    return (ShareDisposition == MSGIRQP_SHARE_DEVICE_EXCLUSIVE) ? TRUE : FALSE;
}

/* The per-line test of the HAL-mode probe (design record 01, section 1):
 * a PIC-mode HAL's device vectors are 30h-3Fh; an APIC-mode HAL's are at
 * or above 50h. */
#define MSGIRQP_MIN_APIC_VECTOR 0x50

/* The IDT vector a line's system vector is dispatched through (design
 * record 01, section 2, step 1): its low byte. An x86 MP HAL hands out
 * system vectors above FFh (halmacpi: 183h for IDT vector 83h); the
 * connect confirms the mapping against the IDT on x86. */
static ULONG MsgIrqpIdtVector(ULONG SystemVector)
{
    return SystemVector & 0xFFUL;
}
static BOOLEAN MsgIrqpVectorIsApic(ULONG Vector)
{
    return (Vector >= MSGIRQP_MIN_APIC_VECTOR && Vector <= 0xFF) ? TRUE : FALSE;
}

/* The lowest CPU in an affinity mask; returns FALSE for an empty mask. */
static BOOLEAN MsgIrqpLowestCpu(KAFFINITY Affinity, ULONG *Cpu)
{
    ULONG i;

    for (i = 0; i < sizeof(KAFFINITY) * 8; i++) {
        if (Affinity & (((KAFFINITY)1) << i)) {
            *Cpu = i;
            return TRUE;
        }
    }
    return FALSE;
}

/* IA32_APIC_BASE (design record 01, section 1): the local APIC is usable
 * only when it is globally enabled (bit 11) and in xAPIC mode. With bit 10
 * set it runs in x2APIC mode, where its registers are not memory mapped
 * and a destination ID need not fit the message address: refused. */
#define MSGIRQP_APICBASE_ENABLE 0x800UL
#define MSGIRQP_APICBASE_X2APIC 0x400UL
static BOOLEAN MsgIrqpApicBaseUsable(ULONG Low)
{
    return ((Low & MSGIRQP_APICBASE_ENABLE) != 0 &&
            (Low & MSGIRQP_APICBASE_X2APIC) == 0) ? TRUE : FALSE;
}

/* A locked compare-exchange with no import: the brake's (record 05) and
 * the event history's flag (record 08). Returns the value it found. */
#if defined(MSI4NT_X86) && defined(_MSC_VER)
static LONG MsgIrqpCas(volatile LONG *Target, LONG New, LONG Old)
{
    LONG r;

    __asm {
        mov ecx, Target
        mov edx, New
        mov eax, Old
        lock cmpxchg [ecx], edx
        mov r, eax
    }
    return r;
}
#elif defined(MSI4NT_HOST_TEST)
/* A host test built with another compiler runs on one thread. */
static LONG MsgIrqpCas(volatile LONG *Target, LONG New, LONG Old)
{
    LONG r = *Target;

    if (r == Old) {
        *Target = New;
    }
    return r;
}
#else
#define MsgIrqpCas(t, n, o) InterlockedCompareExchange((t), (n), (o))
#endif

/* An atomic exchange with no import that a 386 has (xchg locks by
 * itself; cmpxchg needs a 486): the event history's flag (record 08),
 * which every build takes, the legacy one on any processor NT runs on.
 * Returns the value it found. */
#if defined(MSI4NT_X86) && defined(_MSC_VER)
static LONG MsgIrqpXchg(volatile LONG *Target, LONG New)
{
    LONG r;

    __asm {
        mov ecx, Target
        mov eax, New
        xchg [ecx], eax
        mov r, eax
    }
    return r;
}
#elif defined(MSI4NT_HOST_TEST)
static LONG MsgIrqpXchg(volatile LONG *Target, LONG New)
{
    LONG r = *Target;

    *Target = New;
    return r;
}
#else
#define MsgIrqpXchg(t, n) InterlockedExchange((t), (n))
#endif

#if defined(MSI4NT_ISA_LINE)
/* ------------------------------------------------------------------ */
/* Part A, the ISA claim's pure decisions (design record 05): the      */
/* brake's counting and threshold, the candidate list, and what the    */
/* firmware tables and the ELCR say about IRQs 0-15.                   */
/* ------------------------------------------------------------------ */

#define MSGIRQP_BRAKE_MAX 0x7FFFFFFFUL

/* Between a read of Run and its compare-exchange: nothing, except in the
 * host tests, which put another CPU's entry there. */
#ifndef MSGIRQP_BRAKE_HOOK
#define MSGIRQP_BRAKE_HOOK(b)
#endif

/* The stray-entry brake's state (section 2.7), one per claim. */
typedef struct _MSGIRQP_BRAKE {
    volatile ULONG At;                  /* the threshold; 0: the brake is off */
    volatile ULONG Entries;             /* statistics, plain counters */
    volatile ULONG Stray;
    volatile LONG Run;                  /* compare-exchange only */
    volatile LONG Braked;               /* compare-exchange only */
} MSGIRQP_BRAKE;

/* The threshold for a membership: BrakeEntries (0: off) times the number
 * of members, saturated. */
static ULONG MsgIrqpBrakeThreshold(ULONG BrakeEntries, ULONG Members)
{
    if (BrakeEntries == 0 || Members == 0) {
        return 0;
    }
    return (Members > MSGIRQP_BRAKE_MAX / BrakeEntries) ? MSGIRQP_BRAKE_MAX
                                                         : BrakeEntries * Members;
}

/* Counts one entry the client has answered (section 2.7). A claimed entry
 * exchanges Run for zero; an unclaimed one replaces Run with the value it
 * read plus one only if it is still that value (so it cannot overwrite a
 * reset made meanwhile on another CPU), and the entry whose replacement
 * takes Run to or past the threshold, read once, sets Braked. Run stops
 * at its maximum, so it cannot wrap with the brake off. TRUE only for the
 * one entry whose compare-exchange set Braked (record 08: it notes BRAKE). */
static BOOLEAN MsgIrqpBrakeCount(MSGIRQP_BRAKE *B, BOOLEAN Claimed)
{
    LONG old;
    LONG run;
    ULONG at;

    if (Claimed) {
        for (;;) {
            old = B->Run;
            MSGIRQP_BRAKE_HOOK(B);
            if (old == 0 || MsgIrqpCas(&B->Run, 0, old) == old) {
                break;
            }
        }
        return FALSE;
    }
    B->Stray++;
    for (;;) {
        old = B->Run;
        if ((ULONG)old >= MSGIRQP_BRAKE_MAX) {
            run = old;                          /* stays at the maximum */
            break;
        }
        run = old + 1;
        MSGIRQP_BRAKE_HOOK(B);
        if (MsgIrqpCas(&B->Run, run, old) == old) {
            break;
        }
    }
    at = B->At;
    if (at != 0 && (ULONG)run >= at) {
        return (BOOLEAN)(MsgIrqpCas(&B->Braked, 1, 0) == 0);
    }
    return FALSE;
}

/* The never set (section 2.3): timer, keyboard, cascade, RTC, SCI, PS/2
 * mouse, FPU error, the two IDE channels. */
#define MSGIRQP_ISA_NEVER                                                  \
    ((1UL << 0) | (1UL << 1) | (1UL << 2) | (1UL << 8) | (1UL << 9) |    \
     (1UL << 12) | (1UL << 13) | (1UL << 14) | (1UL << 15))

/* The library's order (section 2.3). */
static const UCHAR MsgIrqpIsaDefault[] = { 10, 11, 5, 7, 6, 3, 4 };

/* The candidates in the order tried: the client's, or the library's for a
 * count of 0. FALSE for more than 16, an IRQ above 15, one of the never
 * set, or one listed twice. */
static BOOLEAN MsgIrqpIsaOrder(const UCHAR *Candidates, ULONG CandidateCount, UCHAR *Order,
                               ULONG *Count)
{
    ULONG seen = 0;
    ULONG k;

    if (CandidateCount > MSGIRQ_ISA_MAX_CANDIDATES) {
        return FALSE;
    }
    if (CandidateCount == 0) {
        Candidates = MsgIrqpIsaDefault;
        CandidateCount = (ULONG)sizeof(MsgIrqpIsaDefault);
    }
    for (k = 0; k < CandidateCount; k++) {
        if (Candidates[k] > 15 || (MSGIRQP_ISA_NEVER & (1UL << Candidates[k])) ||
            (seen & (1UL << Candidates[k]))) {
            return FALSE;
        }
        seen |= 1UL << Candidates[k];
        Order[k] = Candidates[k];
    }
    *Count = CandidateCount;
    return TRUE;
}

/* The most functions on one joinable claim (sections 2.8 and 2.11): the
 * claim's maker chooses within these bounds. */
#define MSGIRQP_ISA_DEFAULT_MEMBERS 2UL
#define MSGIRQP_ISA_MIN_MEMBERS     2UL
#define MSGIRQP_ISA_MAX_MEMBERS     16UL

/* The member cap a claim call asks for; FALSE when out of range. */
static BOOLEAN MsgIrqpIsaCap(ULONG MaxMembers, ULONG *Cap)
{
    if (MaxMembers == 0) {
        MaxMembers = MSGIRQP_ISA_DEFAULT_MEMBERS;
    }
    if (MaxMembers < MSGIRQP_ISA_MIN_MEMBERS || MaxMembers > MSGIRQP_ISA_MAX_MEMBERS) {
        return FALSE;
    }
    *Cap = MaxMembers;
    return TRUE;
}

/* The flags a claim call may pass (sections 2.8 and 2.12): the known
 * ones, and JOIN_FIRST only with SHARE. */
static BOOLEAN MsgIrqpIsaFlagsOk(ULONG Flags)
{
    if ((Flags & ~(ULONG)(MSGIRQ_ISA_FLAG_OVERRIDE | MSGIRQ_ISA_FLAG_SHARE |
                          MSGIRQ_ISA_FLAG_JOIN_FIRST)) != 0) {
        return FALSE;
    }
    return (BOOLEAN)((Flags & MSGIRQ_ISA_FLAG_JOIN_FIRST) == 0 || (Flags & MSGIRQ_ISA_FLAG_SHARE) != 0);
}

/* Which claim a function joins (sections 2.8 and 2.12). Members[irq] is
 * the member count of this driver's joinable claim on that IRQ (HELD,
 * brake not tripped), 0 for none; Reason[] is the caller's own picker's
 * verdict; Caps[irq] is the most members that claim may have for this
 * caller (the lower of the claim's cap and the caller's). A claim is
 * joined only on a candidate IRQ and only with room. Own line first
 * (First FALSE: the picker found no IRQ of its own): only where the
 * picker's only reason is the arbiter's conflict, the fewest members
 * first. Shared line first (First TRUE: before anything is translated or
 * reported): only where no source excluded the IRQ (NOT_TRIED), the most
 * members first. Ties go to the caller's order. Returns the IRQ, or 0 for
 * none; *Full receives the first candidate IRQ whose claim was passed
 * over only for being full (0: none). */
static ULONG MsgIrqpIsaJoinChoice(const UCHAR *Order, ULONG Count, const UCHAR *Reason,
                                  const ULONG *Members, const ULONG *Caps, BOOLEAN First,
                                  ULONG *Full)
{
    UCHAR want = (UCHAR)(First ? MSGIRQ_ISA_NOT_TRIED : MSGIRQ_ISA_CONFLICT);
    ULONG best = 0;
    ULONG k;
    ULONG irq;

    *Full = 0;
    for (k = 0; k < Count; k++) {
        irq = Order[k];
        if (irq > 15 || Members[irq] == 0 || Reason[irq] != want) {
            continue;
        }
        if (Members[irq] >= Caps[irq]) {
            if (*Full == 0) {
                *Full = irq;
            }
            continue;
        }
        if (best == 0 || (First ? Members[irq] > Members[best] : Members[irq] < Members[best])) {
            best = irq;
        }
    }
    return best;
}

/* What the sources say about IRQs 0-15, and each candidate's translation. */
typedef struct _MSGIRQP_ISA_PICK {
    ULONG Sci;
    ULONG Sources;
    ULONG Configured;
    ULONG Possible;
    ULONG Override;
    ULONG Level;
    ULONG Pirq;
    ULONG Elcr;
    ULONG OverrideGsi[16];
    UCHAR PirqOffset[16];
    UCHAR Reason[16];
    ULONG Detail[16];
    ULONG Vector[16];
    KIRQL Irql[16];
    KAFFINITY Affinity[16];
} MSGIRQP_ISA_PICK;

static ULONG MsgIrqpLe16(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static ULONG MsgIrqpLe32(const UCHAR *p)
{
    return MsgIrqpLe16(p) | (MsgIrqpLe16(p + 2) << 16);
}

static BOOLEAN MsgIrqpSumZero(const UCHAR *p, ULONG n)
{
    UCHAR s = 0;

    while (n-- != 0) {
        s = (UCHAR)(s + *p++);
    }
    return (s == 0) ? TRUE : FALSE;
}

/* The MADT's interrupt source overrides (ACPI 5.2.12.5): type 2, length
 * 10, bus at 2, source IRQ at 3, GSI at 4. */
static VOID MsgIrqpIsaMadt(MSGIRQP_ISA_PICK *K, const UCHAR *T, ULONG Len)
{
    ULONG off;
    ULONG el;
    ULONG src;
    ULONG gsi;

    for (off = 44; off + 2 <= Len; off += el) {
        el = T[off + 1];
        if (el < 2 || el > Len - off) {
            break;
        }
        if (T[off] == 2 && el >= 10) {
            src = T[off + 3];
            gsi = MsgIrqpLe32(T + off + 4);
            if (src < 16) {
                K->Override |= 1UL << src;
                K->OverrideGsi[src] = gsi;
            }
            if (gsi < 16 && gsi != src) {
                K->Override |= 1UL << gsi;
                K->OverrideGsi[gsi] = gsi;
            }
        }
    }
}

/* ELCR1/ELCR2 as read (ICH9 datasheet 13.4.10-11). IRQ 0-2, 8 and 13
 * cannot be level; a set bit there, or FFh in both, means no ELCR. */
static VOID MsgIrqpIsaElcrValue(MSGIRQP_ISA_PICK *K, ULONG Low, ULONG High)
{
    K->Elcr = Low | (High << 8);
    if ((Low == 0xFF && High == 0xFF) || (Low & 0x07) != 0 || (High & 0x21) != 0) {
        return;
    }
    K->Level = K->Elcr;
    K->Sources |= MSGIRQ_ISA_SOURCE_ELCR;
}

/* Gives each IRQ the first reason that applies (section 2.3); the client's
 * own exclusion comes after every source, so that the result still says
 * what the board says about an IRQ the client ruled out. */
static VOID MsgIrqpIsaReasons(MSGIRQP_ISA_PICK *K, BOOLEAN Override, ULONG Exclude)
{
    ULONG k;
    ULONG bit;

    for (k = 0; k < 16; k++) {
        bit = 1UL << k;
        if (MSGIRQP_ISA_NEVER & bit) {
            K->Reason[k] = MSGIRQ_ISA_NEVER;
        } else if (K->Sci == k) {
            K->Reason[k] = MSGIRQ_ISA_SCI;
        } else if (K->Override & bit) {
            K->Reason[k] = MSGIRQ_ISA_OVERRIDE;
            K->Detail[k] = K->OverrideGsi[k];
        } else if (K->Level & bit) {
            K->Reason[k] = MSGIRQ_ISA_LEVEL;
            K->Detail[k] = K->Elcr;
        } else if (K->Configured & bit) {
            K->Reason[k] = MSGIRQ_ISA_CONFIGURED;
        } else if ((K->Possible & bit) && !Override) {
            K->Reason[k] = MSGIRQ_ISA_POSSIBLE;
        } else if (K->Pirq & bit) {
            K->Reason[k] = MSGIRQ_ISA_PIRQ;
            K->Detail[k] = K->PirqOffset[k];
        } else if (Exclude & bit) {
            K->Reason[k] = MSGIRQ_ISA_EXCLUDED;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The second pool's pure decisions (design record 05, section 2.9):   */
/* the call's choice of pools, range and candidates; the MADT's I/O    */
/* APIC entries; what names an input above 23; the claim loop over the */
/* four operations the kernel side supplies; the order of the sources. */
/* ------------------------------------------------------------------ */

#define MSGIRQP_HIGH_APICS 32UL         /* what HighIoApics can name */
#define MSGIRQP_HIGH_NONE  0xFFFFFFFFUL

static BOOLEAN MsgIrqpBit(const ULONG *Map, ULONG n)
{
    return (BOOLEAN)((Map[n >> 5] >> (n & 31)) & 1UL);
}

static VOID MsgIrqpBitSet(ULONG *Map, ULONG n)
{
    Map[n >> 5] |= 1UL << (n & 31);
}

/* A call's pools, range, exclusions and candidates, checked, with the
 * defaults filled in. */
typedef struct _MSGIRQP_POOLS {
    ULONG Pools;                        /* MSGIRQ_POOL_*, never 0 */
    ULONG First;                        /* the upper range; 0 without MSGIRQ_POOL_HIGH */
    ULONG Last;
    ULONG IoApics;                      /* as given; 0: all */
    ULONG ExcludeCount;
    MSGIRQ_INPUT_RANGE Exclude[MSGIRQ_HIGH_MAX_EXCLUDES];
    ULONG IsaCount;                     /* the ISA IRQs to try, in order */
    ULONG HighCount;                    /* the listed upper inputs, in order */
    ULONG Budget;                       /* the upper budget, 1..32; 0 without MSGIRQ_POOL_HIGH */
    ULONG NameStart;                    /* the service name in RegistryPath: its first */
    ULONG NameChars;                    /* character and its length */
    BOOLEAN Listed;                     /* the call gave a candidate list */
    UCHAR IsaOrder[MSGIRQ_ISA_MAX_CANDIDATES];
    UCHAR HighOrder[MSGIRQ_ISA_MAX_CANDIDATES];
} MSGIRQP_POOLS;

static BOOLEAN MsgIrqpHighExcluded(const MSGIRQP_POOLS *P, ULONG n)
{
    ULONG k;

    for (k = 0; k < P->ExcludeCount; k++) {
        if (n >= P->Exclude[k].First && n <= P->Exclude[k].Last) {
            return TRUE;
        }
    }
    return FALSE;
}

/* The sizes a caller's structures may have (section 2.9, "Structure
 * versions"): exactly the size before the second pool (an earlier
 * client's), or the present size or more. Any size between is no released
 * layout and is refused, never read as the smaller one: a caller that
 * asked for the upper pool is never given an ISA claim because of its
 * structure's size. Result may be 0 (no result). */
static BOOLEAN MsgIrqpIsaSizes(ULONG Options, ULONG Result, BOOLEAN HaveResult)
{
    if (Options != MSGIRQ_ISA_PARAMS_V1_SIZE && Options < sizeof(MSGIRQ_ISA_PARAMS)) {
        return FALSE;
    }
    return (BOOLEAN)(!HaveResult || Result == MSGIRQ_ISA_RESULT_V1_SIZE ||
                     Result >= sizeof(MSGIRQ_ISA_RESULT));
}

/* The default and the largest budget of upper translations a driver may
 * have in one boot (section 2.9, "A translation allocates"). */
#define MSGIRQP_HIGH_BUDGET     16UL
#define MSGIRQP_HIGH_MAX_BUDGET 32UL
#define MSGIRQP_SERVICE_MAX     255UL   /* the longest registry key name, in characters */

/* The driver's service name in its RegistryPath: the last component.
 * FALSE for a NULL or empty string, an odd length, a length beyond the
 * buffer, a path that ends in a separator, or a name longer than a key
 * name may be. */
static BOOLEAN MsgIrqpServiceName(const UNICODE_STRING *Path, ULONG *Start, ULONG *Chars)
{
    ULONG n;
    ULONG k;

    *Start = 0;
    *Chars = 0;
    if (Path == NULL || Path->Buffer == NULL || Path->Length == 0 || (Path->Length & 1) != 0 ||
        Path->Length > Path->MaximumLength) {
        return FALSE;
    }
    n = Path->Length / 2;
    for (k = n; k > 0 && Path->Buffer[k - 1] != L'\\'; k--) {
    }
    if (n - k == 0 || n - k > MSGIRQP_SERVICE_MAX) {
        return FALSE;
    }
    *Start = k;
    *Chars = n - k;
    return TRUE;
}

/* Checks everything of a call's pool choice that needs no machine
 * (sections 2.9 and 2.11) and splits the candidates by pool. A caller
 * whose structure ends before Pools has left the new fields zero. FALSE
 * is MSGIRQ_STATUS_INVALID_PARAMETER: an unknown pool; a range, an IOAPIC
 * choice, an exclusion, a budget or a RegistryPath given without the
 * upper pool; the upper pool without a RegistryPath that names a service,
 * or with a budget above 32; a range that is empty, inverted or outside
 * 24..255; more than four exclusions or one that is inverted or outside
 * 24..255; more than 16 candidates; a candidate that is one of 16..23,
 * one of the never set, listed twice, in a pool the call did not choose,
 * or an upper one outside the call's own range or inside its own
 * exclusions. */
static BOOLEAN MsgIrqpPoolCheck(const MSGIRQ_ISA_PARAMS *O, MSGIRQP_POOLS *P)
{
    UCHAR isa[MSGIRQ_ISA_MAX_CANDIDATES];
    ULONG seen[8];
    ULONG first = 0;
    ULONG last = 0;
    ULONG count = 0;
    ULONG budget = 0;
    const UNICODE_STRING *path = NULL;
    ULONG n = 0;
    ULONG k;
    ULONG c;
    UCHAR *b = (UCHAR *)P;

    for (k = 0; k < sizeof(*P); k++) {
        b[k] = 0;
    }
    for (k = 0; k < 8; k++) {
        seen[k] = 0;
    }
    if (O->Size >= sizeof(MSGIRQ_ISA_PARAMS)) {
        P->Pools = O->Pools;
        first = O->HighFirst;
        last = O->HighLast;
        P->IoApics = O->HighIoApics;
        count = O->HighExcludeCount;
        budget = O->HighBudget;
        path = O->RegistryPath;
    }
    if ((P->Pools & ~(ULONG)(MSGIRQ_POOL_ISA | MSGIRQ_POOL_HIGH)) != 0) {
        return FALSE;
    }
    if (P->Pools == 0) {
        P->Pools = MSGIRQ_POOL_ISA;
    }
    if ((P->Pools & MSGIRQ_POOL_HIGH) == 0) {
        if (first != 0 || last != 0 || P->IoApics != 0 || count != 0 || budget != 0 ||
            path != NULL) {
            return FALSE;
        }
    } else {
        if (first == 0 && last == 0) {
            first = MSGIRQ_HIGH_MIN;
            last = MSGIRQ_HIGH_MAX;
        }
        if (budget == 0) {
            budget = MSGIRQP_HIGH_BUDGET;
        }
        if (first < MSGIRQ_HIGH_MIN || last > MSGIRQ_HIGH_MAX || first > last ||
            count > MSGIRQ_HIGH_MAX_EXCLUDES || budget > MSGIRQP_HIGH_MAX_BUDGET ||
            !MsgIrqpServiceName(path, &P->NameStart, &P->NameChars)) {
            return FALSE;
        }
        P->Budget = budget;
        for (k = 0; k < count; k++) {
            if (O->HighExclude[k].First < MSGIRQ_HIGH_MIN ||
                O->HighExclude[k].Last > MSGIRQ_HIGH_MAX ||
                O->HighExclude[k].First > O->HighExclude[k].Last) {
                return FALSE;
            }
            P->Exclude[k] = O->HighExclude[k];
        }
        P->First = first;
        P->Last = last;
        P->ExcludeCount = count;
    }
    if (O->CandidateCount > MSGIRQ_ISA_MAX_CANDIDATES) {
        return FALSE;
    }
    if (O->CandidateCount == 0) {
        /* The library's order in each chosen pool: section 2.3's for the
         * ISA IRQs, ascending from First for the upper inputs. */
        return (BOOLEAN)((P->Pools & MSGIRQ_POOL_ISA) == 0 ||
                         MsgIrqpIsaOrder(O->Candidates, 0, P->IsaOrder, &P->IsaCount));
    }
    P->Listed = TRUE;
    for (k = 0; k < O->CandidateCount; k++) {
        c = O->Candidates[k];
        if (c < 16) {
            if ((P->Pools & MSGIRQ_POOL_ISA) == 0) {
                return FALSE;
            }
            isa[n++] = (UCHAR)c;
        } else {
            if (c < MSGIRQ_HIGH_MIN || (P->Pools & MSGIRQ_POOL_HIGH) == 0 || c < first ||
                c > last || MsgIrqpHighExcluded(P, c) || MsgIrqpBit(seen, c)) {
                return FALSE;
            }
            MsgIrqpBitSet(seen, c);
            P->HighOrder[P->HighCount++] = (UCHAR)c;
        }
    }
    return (BOOLEAN)(n == 0 || MsgIrqpIsaOrder(isa, n, P->IsaOrder, &P->IsaCount));
}

/* What is known about the inputs above 23 during one claim. */
typedef struct _MSGIRQP_HIGH {
    ULONG First;                        /* the range as used */
    ULONG Last;
    ULONG IoApics;                      /* the IOAPICs in scope, as used */
    ULONG ApicCount;                    /* every I/O APIC entry of the MADT */
    ULONG ApicBase[MSGIRQP_HIGH_APICS];
    ULONG Scope[8];                     /* a bit per input: in the range, on a chosen IOAPIC,
                                           not excluded by the client */
    ULONG Client[8];                    /* in the range and ruled out by the client's choices */
    ULONG Override[8];                  /* a MADT override's target, or an NMI source */
    ULONG Configured[8];
    ULONG Possible[8];
    ULONG End;                          /* the highest input this call translated;
                                           MSGIRQP_HIGH_NONE */
    ULONG LastInput;                    /* the input Detail belongs to; 0: none */
    ULONG Detail;
    ULONG Budget;                       /* the call's budget, and what it charged: */
    ULONG New;                          /* the charges it made and left, */
    ULONG Spent;                        /* the driver's charges after it */
    UCHAR Reason[256];
} MSGIRQP_HIGH;

/* The MADT's I/O APIC entries (ACPI 5.2.12.3: type 1, length 12, the
 * Global System Interrupt Base at 8), and the inputs above 23 that an
 * interrupt source override (type 2, the GSI at 4) or an NMI source
 * (type 3, length 8, the GSI at 4) names. Every I/O APIC entry is
 * counted; the bases of the first 32 are kept. */
static VOID MsgIrqpHighMadt(MSGIRQP_HIGH *H, const UCHAR *T, ULONG Len)
{
    ULONG off;
    ULONG el;
    ULONG gsi;

    for (off = 44; off + 2 <= Len; off += el) {
        el = T[off + 1];
        if (el < 2 || el > Len - off) {
            break;
        }
        if (T[off] == 1 && el >= 12) {
            if (H->ApicCount < MSGIRQP_HIGH_APICS) {
                H->ApicBase[H->ApicCount] = MsgIrqpLe32(T + off + 8);
            }
            H->ApicCount++;
        } else if ((T[off] == 2 && el >= 10) || (T[off] == 3 && el >= 8)) {
            gsi = MsgIrqpLe32(T + off + 4);
            if (gsi >= MSGIRQ_HIGH_MIN && gsi <= MSGIRQ_HIGH_MAX) {
                MsgIrqpBitSet(H->Override, gsi);
            }
        }
    }
}

/* The IOAPIC an input belongs to: the entry with the greatest base at or
 * below it (the first of equals). MSGIRQP_HIGH_NONE when it lies below
 * every base, and for a MADT with no I/O APIC entry or with more than
 * HighIoApics can name, which is not served. */
static ULONG MsgIrqpHighOwner(const MSGIRQP_HIGH *H, ULONG n)
{
    ULONG best = MSGIRQP_HIGH_NONE;
    ULONG k;

    if (H->ApicCount > MSGIRQP_HIGH_APICS) {
        return MSGIRQP_HIGH_NONE;
    }
    for (k = 0; k < H->ApicCount; k++) {
        if (H->ApicBase[k] <= n && (best == MSGIRQP_HIGH_NONE || H->ApicBase[k] > H->ApicBase[best])) {
            best = k;
        }
    }
    return best;
}

/* The call's scope on this machine, once the MADT is read: the range, on
 * the chosen IOAPICs, less the client's exclusions. FALSE is
 * MSGIRQ_STATUS_INVALID_PARAMETER: an IOAPIC bit beyond the MADT's
 * entries, or a listed candidate that is not in scope. With no I/O APIC
 * entry, or more than 32, nothing is in scope (and a listed candidate
 * then contradicts nothing). */
static BOOLEAN MsgIrqpHighScope(MSGIRQP_HIGH *H, const MSGIRQP_POOLS *P)
{
    ULONG n;
    ULONG k;
    ULONG owner;

    H->First = P->First;
    H->Last = P->Last;
    H->IoApics = 0;
    H->End = MSGIRQP_HIGH_NONE;
    for (k = 0; k < 8; k++) {
        H->Scope[k] = 0;
        H->Client[k] = 0;
    }
    if (H->ApicCount < MSGIRQP_HIGH_APICS && (P->IoApics >> H->ApicCount) != 0) {
        return FALSE;
    }
    if (H->ApicCount == 0 || H->ApicCount > MSGIRQP_HIGH_APICS) {
        return TRUE;
    }
    H->IoApics = P->IoApics;
    if (H->IoApics == 0) {
        H->IoApics = (H->ApicCount == MSGIRQP_HIGH_APICS) ? 0xFFFFFFFFUL
                                                          : (1UL << H->ApicCount) - 1;
    }
    for (n = H->First; n <= H->Last; n++) {
        owner = MsgIrqpHighOwner(H, n);
        if (owner == MSGIRQP_HIGH_NONE || ((H->IoApics >> owner) & 1UL) == 0 ||
            MsgIrqpHighExcluded(P, n)) {
            MsgIrqpBitSet(H->Client, n);
        } else {
            MsgIrqpBitSet(H->Scope, n);
        }
    }
    for (k = 0; k < P->HighCount; k++) {
        if (!MsgIrqpBit(H->Scope, P->HighOrder[k])) {
            return FALSE;
        }
    }
    return TRUE;
}

/* An interrupt descriptor of a boot or allocated configuration: its
 * level and its vector, each an input when it lies in 24..255. */
static VOID MsgIrqpHighMarkCm(MSGIRQP_HIGH *H, ULONG Level, ULONG Vector)
{
    if (Level >= MSGIRQ_HIGH_MIN && Level <= MSGIRQ_HIGH_MAX) {
        MsgIrqpBitSet(H->Configured, Level);
    }
    if (Vector >= MSGIRQ_HIGH_MIN && Vector <= MSGIRQ_HIGH_MAX) {
        MsgIrqpBitSet(H->Configured, Vector);
    }
}

/* An interrupt range of a possible configuration. A range that begins at
 * 0 and reaches 255 or beyond is a requirement for any interrupt and
 * names no input (every PCI device has one); any other range names every
 * input of 24..255 it covers. */
static VOID MsgIrqpHighMarkIo(MSGIRQP_HIGH *H, ULONG Min, ULONG Max)
{
    ULONG n;

    if (Min > Max || (Min == 0 && Max >= MSGIRQ_HIGH_MAX)) {
        return;
    }
    for (n = (Min < MSGIRQ_HIGH_MIN) ? MSGIRQ_HIGH_MIN : Min; n <= Max && n <= MSGIRQ_HIGH_MAX; n++) {
        MsgIrqpBitSet(H->Possible, n);
    }
}

/* Gives each input above 23 the first reason that applies (section 2.9).
 * The sources speak for every input, in the range or not; the client's
 * own exclusion comes after them and only inside the range. */
static VOID MsgIrqpHighReasons(MSGIRQP_HIGH *H, BOOLEAN Override)
{
    ULONG n;

    for (n = 0; n < 256; n++) {
        H->Reason[n] = MSGIRQ_ISA_NOT_TRIED;
    }
    for (n = MSGIRQ_HIGH_MIN; n <= MSGIRQ_HIGH_MAX; n++) {
        if (MsgIrqpBit(H->Override, n)) {
            H->Reason[n] = MSGIRQ_ISA_OVERRIDE;
        } else if (MsgIrqpBit(H->Configured, n)) {
            H->Reason[n] = MSGIRQ_ISA_CONFIGURED;
        } else if (MsgIrqpBit(H->Possible, n) && !Override) {
            H->Reason[n] = MSGIRQ_ISA_POSSIBLE;
        } else if (MsgIrqpBit(H->Client, n)) {
            H->Reason[n] = MSGIRQ_ISA_EXCLUDED;
        }
    }
}

/* The four operations of the upper claim, as the kernel side (or a host
 * test's machine) performs them. Each returns one of these; *Detail
 * receives the vector (translate), the status (report, a failed listen
 * connect) or the count heard. A report is made for the claim's owner
 * object, which the kernel side creates at the first report of a call
 * (MSGIRQP_OP_NO_MEMORY when it cannot). */
#define MSGIRQP_OP_OK        0
#define MSGIRQP_OP_NONE      1          /* translate: the HAL gives no usable line */
#define MSGIRQP_OP_NOT_APIC  2          /* translate: not a sound APIC-mode line */
#define MSGIRQP_OP_CONFLICT  3          /* report: the arbiter's conflict */
#define MSGIRQP_OP_HELD      4          /* report: the conflict, with the report in place;
                                           listen: the exclusive connect failed */
#define MSGIRQP_OP_NO_MEMORY 5
#define MSGIRQP_OP_REFUSED   6          /* report: the list itself refused; withdraw: failed */
#define MSGIRQP_OP_HEARD     7          /* listen: the input was driven */
typedef ULONG (*MSGIRQP_HIGH_OP)(PVOID Context, ULONG Input, ULONG *Detail);

/* The upper inputs charged to a driver for the boot (section 2.9, "A
 * translation allocates"): on the APIC-mode HALs of 2000 and XP a
 * translation gives an input that has none a vector for the rest of the
 * boot, and translating some 75 new inputs was measured to overrun the
 * HAL's priority bands, after which it returns wrong IRQLs to everyone.
 * So a driver has at most its budget of different inputs translated to a
 * vector in one boot; an input charged before costs nothing more. The
 * charges are kept, written ahead of each new translation, in the
 * driver's record in the volatile HARDWARE hive, so that a reload or a
 * reinstall of the driver does not give them back. */
typedef struct _MSGIRQP_HIGH_SPENT {
    ULONG Count;                        /* the bits set in Inputs */
    ULONG Inputs[8];                    /* a bit per input charged */
} MSGIRQP_HIGH_SPENT;
#define MSGIRQP_LEDGER_BYTES 32UL       /* the record's value: bit n % 8 of byte n / 8 */

/* Persist writes the whole set as the driver's record: MSGIRQP_OP_OK, or
 * MSGIRQP_OP_REFUSED with *Detail the status. */
typedef ULONG (*MSGIRQP_HIGH_PERSIST)(PVOID Context, const MSGIRQP_HIGH_SPENT *Spent, ULONG *Detail);
typedef struct _MSGIRQP_HIGH_OPS {
    PVOID Context;
    MSGIRQP_HIGH_OP Translate;
    MSGIRQP_HIGH_OP Report;
    MSGIRQP_HIGH_OP Withdraw;
    MSGIRQP_HIGH_OP Listen;
    MSGIRQP_HIGH_PERSIST Persist;
} MSGIRQP_HIGH_OPS;

/* The record's value read into a set: FALSE (a malformed record) when a
 * bit below 24 is set. */
static BOOLEAN MsgIrqpLedgerLoad(MSGIRQP_HIGH_SPENT *S, const UCHAR *Bytes)
{
    ULONG n;

    S->Count = 0;
    for (n = 0; n < 8; n++) {
        S->Inputs[n] = 0;
    }
    for (n = 0; n < 256; n++) {
        if ((Bytes[n >> 3] >> (n & 7)) & 1) {
            if (n < MSGIRQ_HIGH_MIN) {
                return FALSE;
            }
            MsgIrqpBitSet(S->Inputs, n);
            S->Count++;
        }
    }
    return TRUE;
}

static VOID MsgIrqpLedgerStore(const MSGIRQP_HIGH_SPENT *S, UCHAR *Bytes)
{
    ULONG n;

    for (n = 0; n < MSGIRQP_LEDGER_BYTES; n++) {
        Bytes[n] = 0;
    }
    for (n = 0; n < 256; n++) {
        if (MsgIrqpBit(S->Inputs, n)) {
            Bytes[n >> 3] |= (UCHAR)(1 << (n & 7));
        }
    }
}

/* Into becomes the union of the two sets; TRUE when it gained a bit. */
static BOOLEAN MsgIrqpLedgerUnion(MSGIRQP_HIGH_SPENT *Into, const MSGIRQP_HIGH_SPENT *From)
{
    BOOLEAN gained = FALSE;
    ULONG n;

    for (n = MSGIRQ_HIGH_MIN; n <= MSGIRQ_HIGH_MAX; n++) {
        if (MsgIrqpBit(From->Inputs, n) && !MsgIrqpBit(Into->Inputs, n)) {
            MsgIrqpBitSet(Into->Inputs, n);
            Into->Count++;
            gained = TRUE;
        }
    }
    return gained;
}

static VOID MsgIrqpBitClear(ULONG *Map, ULONG n)
{
    Map[n >> 5] &= ~(1UL << (n & 31));
}

/* Whether a translation's vector and IRQL agree, as the HALs measured
 * give them (section 2.9): on x86 the IRQL is the vector's priority class
 * (its high four bits) less one, on amd64 it is the class. A pair that
 * does not agree is what a HAL whose bands have run over returns, and an
 * interrupt connected with it is entered above the IRQL its driver
 * synchronises at. */
static BOOLEAN MsgIrqpHighAgrees(ULONG IdtVector, ULONG Irql, BOOLEAN Amd64)
{
    ULONG cls = (IdtVector >> 4) & 0xFUL;

    return (BOOLEAN)(Amd64 ? (Irql == cls) : (Irql + 1 == cls));
}

/* The k-th candidate of the upper pool: the call's list, or ascending
 * from First. 0 past the end. */
static ULONG MsgIrqpHighCandidate(const MSGIRQP_HIGH *H, const UCHAR *Order, ULONG Count, ULONG k)
{
    if (Count != 0) {
        return (k < Count) ? Order[k] : 0;
    }
    return (H->First >= MSGIRQ_HIGH_MIN && H->First + k <= H->Last &&
            H->First + k <= MSGIRQ_HIGH_MAX) ? H->First + k : 0;
}

/* The upper pool's attempt (section 2.9; section 2.2, step 5, with the
 * upper pool's operations). The reasons first; then each candidate in
 * scope that no source excludes is translated, reported only if that
 * gave an APIC-mode vector, listened on, and withdrawn when rejected. An
 * input is translated only here, immediately before its report, and
 * only while the driver's budget lasts (Spent holds Budget inputs at the
 * most) or the input is one it has been charged for before. A new input
 * is charged, and the set written (Ops->Persist), before it is
 * translated; the charge is taken back, and the set written again, only
 * when the translation gave no vector, and stays when that write fails.
 * *New counts the charges this call made and left. MSGIRQ_STATUS_OK with
 * *Input the input kept (its translation is the last one made);
 * NO_INPUTS when no candidate in scope was given a vector and none was
 * passed over for a source's reason or for the budget; NO_VECTORS;
 * HAL_UNSUPPORTED for a translation that is not a sound APIC-mode line,
 * that input not reported; NO_MEMORY or UNSUPPORTED with nothing reported
 * any more (UNSUPPORTED also when a write ahead failed: H->LastInput is
 * that input, not translated, and H->Detail the status); CLAIM_KEPT with
 * *Input the input whose withdrawal failed. */
static ULONG MsgIrqpHighClaim(MSGIRQP_HIGH *H, const MSGIRQP_HIGH_OPS *Ops, MSGIRQP_HIGH_SPENT *Spent,
                              ULONG Budget, BOOLEAN Override, const UCHAR *Order, ULONG Count,
                              ULONG *Input, ULONG *New)
{
    ULONG k;
    ULONG n;
    ULONG r;
    ULONG vector;
    ULONG detail;
    ULONG status;
    BOOLEAN some = FALSE;
    BOOLEAN charged;

    *Input = 0;
    *New = 0;
    MsgIrqpHighReasons(H, Override);
    H->End = MSGIRQP_HIGH_NONE;
    H->LastInput = 0;
    H->Detail = 0;
    for (k = 0; (n = MsgIrqpHighCandidate(H, Order, Count, k)) != 0; k++) {
        if (!MsgIrqpBit(H->Scope, n)) {
            continue;                                   /* not this call's to consider */
        }
        if (H->Reason[n] != MSGIRQ_ISA_NOT_TRIED) {
            some = TRUE;                                /* an input a source names is there */
            continue;
        }
        charged = FALSE;
        if (!MsgIrqpBit(Spent->Inputs, n)) {
            if (Spent->Count >= Budget) {
                H->Reason[n] = MSGIRQ_ISA_BUDGET;       /* not translated: it may be there */
                some = TRUE;
                continue;
            }
            /* Charged ahead: the record never lacks an input the HAL may
             * have given a vector for. */
            MsgIrqpBitSet(Spent->Inputs, n);
            Spent->Count++;
            detail = 0;
            if (Ops->Persist(Ops->Context, Spent, &detail) != MSGIRQP_OP_OK) {
                MsgIrqpBitClear(Spent->Inputs, n);
                Spent->Count--;
                H->LastInput = n;
                H->Detail = detail;
                return MSGIRQ_STATUS_UNSUPPORTED;       /* n not translated */
            }
            charged = TRUE;
            (*New)++;
        }
        vector = 0;
        r = Ops->Translate(Ops->Context, n, &vector);
        H->LastInput = n;
        H->Detail = vector;
        if (vector != 0) {
            /* The HAL gave a vector, usable or not: the input is there,
             * and it has cost the HAL one if it had none. */
            some = TRUE;
            H->End = (H->End == MSGIRQP_HIGH_NONE || n > H->End) ? n : H->End;
        } else if (charged) {
            /* No such input: the charge is taken back, unless the record
             * cannot be written, which leaves it charged (the safe side). */
            MsgIrqpBitClear(Spent->Inputs, n);
            Spent->Count--;
            if (Ops->Persist(Ops->Context, Spent, &detail) == MSGIRQP_OP_OK) {
                (*New)--;
            } else {
                MsgIrqpBitSet(Spent->Inputs, n);
                Spent->Count++;
            }
        }
        if (r == MSGIRQP_OP_NOT_APIC) {
            H->Reason[n] = MSGIRQ_ISA_TRANSLATE;
            return MSGIRQ_STATUS_HAL_UNSUPPORTED;
        }
        if (r != MSGIRQP_OP_OK) {
            /* An input the HAL does not translate does not exist, and is
             * never reported. */
            H->Reason[n] = MSGIRQ_ISA_TRANSLATE;
            continue;
        }
        detail = 0;
        r = Ops->Report(Ops->Context, n, &detail);
        if (r == MSGIRQP_OP_CONFLICT || r == MSGIRQP_OP_HELD) {
            H->Reason[n] = MSGIRQ_ISA_CONFLICT;
            H->Detail = detail;
            if (r == MSGIRQP_OP_CONFLICT) {
                continue;                               /* a failed report holds nothing */
            }
        } else if (r != MSGIRQP_OP_OK) {
            H->Detail = detail;
            return (r == MSGIRQP_OP_NO_MEMORY) ? MSGIRQ_STATUS_NO_MEMORY
                                               : MSGIRQ_STATUS_UNSUPPORTED;
        } else {
            detail = 0;
            r = Ops->Listen(Ops->Context, n, &detail);
            if (r == MSGIRQP_OP_OK) {
                H->Reason[n] = MSGIRQ_ISA_CHOSEN;
                H->Detail = vector;
                *Input = n;
                return MSGIRQ_STATUS_OK;
            }
            H->Detail = detail;
            if (r == MSGIRQP_OP_NO_MEMORY) {
                status = MSGIRQ_STATUS_NO_MEMORY;
                if (Ops->Withdraw(Ops->Context, n, &detail) != MSGIRQP_OP_OK) {
                    *Input = n;
                    status = MSGIRQ_STATUS_CLAIM_KEPT;
                }
                return status;
            }
            H->Reason[n] = (UCHAR)((r == MSGIRQP_OP_HEARD) ? MSGIRQ_ISA_HEARD : MSGIRQ_ISA_HELD);
        }
        if (Ops->Withdraw(Ops->Context, n, &detail) != MSGIRQP_OP_OK) {
            *Input = n;
            return MSGIRQ_STATUS_CLAIM_KEPT;            /* section 2.2, step 5.4 */
        }
    }
    return some ? MSGIRQ_STATUS_NO_VECTORS : MSGIRQ_STATUS_NO_INPUTS;
}

/* The order of the sources in one call (sections 2.8, 2.9, 2.11 and
 * 2.12): with the share flag, shared line first (FirstStep not NULL) and
 * the ISA pool to scan, first the join of an ISA claim with room; then an
 * ISA IRQ of the function's own, then an upper input of its own, then,
 * with the share flag and only when the ISA pool was scanned, the join of
 * an ISA claim (for shared-first it finds no room the first join did not,
 * and names a full claim); each only when the one before ended with
 * NO_VECTORS (the upper pool's NO_INPUTS counts as that unless the upper
 * pool is the only one chosen; the first join's end is only "none").
 * Any other status ends the call. Scan names the pools the call has
 * candidates for. */
#define MSGIRQP_FROM_NONE 0
#define MSGIRQP_FROM_ISA  1
#define MSGIRQP_FROM_HIGH 2
#define MSGIRQP_FROM_JOIN 3
typedef ULONG (*MSGIRQP_STEP)(PVOID Context);
static ULONG MsgIrqpPoolRun(ULONG Pools, ULONG Scan, BOOLEAN Share, MSGIRQP_STEP FirstStep,
                            MSGIRQP_STEP IsaStep, MSGIRQP_STEP HighStep, MSGIRQP_STEP JoinStep,
                            PVOID Context, ULONG *Scanned, ULONG *From)
{
    ULONG status = MSGIRQ_STATUS_NO_VECTORS;

    *Scanned = 0;
    *From = MSGIRQP_FROM_NONE;
    if (Share && FirstStep != NULL && (Scan & MSGIRQ_POOL_ISA)) {
        *Scanned |= MSGIRQ_POOL_ISA;
        if (FirstStep(Context) == MSGIRQ_STATUS_OK) {
            *From = MSGIRQP_FROM_JOIN;
            return MSGIRQ_STATUS_OK;
        }
    }
    if (Scan & MSGIRQ_POOL_ISA) {
        *Scanned |= MSGIRQ_POOL_ISA;
        status = IsaStep(Context);
        if (status == MSGIRQ_STATUS_OK) {
            *From = MSGIRQP_FROM_ISA;
        }
        if (status != MSGIRQ_STATUS_NO_VECTORS) {
            return status;
        }
    }
    if (Scan & MSGIRQ_POOL_HIGH) {
        *Scanned |= MSGIRQ_POOL_HIGH;
        status = HighStep(Context);
        if (status == MSGIRQ_STATUS_OK) {
            *From = MSGIRQP_FROM_HIGH;
            return status;
        }
        if (status == MSGIRQ_STATUS_NO_INPUTS && Pools != MSGIRQ_POOL_HIGH) {
            status = MSGIRQ_STATUS_NO_VECTORS;
        }
        if (status != MSGIRQ_STATUS_NO_VECTORS) {
            return status;
        }
    }
    if (Share && (*Scanned & MSGIRQ_POOL_ISA) && JoinStep(Context) == MSGIRQ_STATUS_OK) {
        *From = MSGIRQP_FROM_JOIN;
        return MSGIRQ_STATUS_OK;
    }
    return status;
}
#endif /* MSI4NT_ISA_LINE */

/* ------------------------------------------------------------------ */
/* Part A: the published state's pure decisions (design record 07): the */
/* snapshot, its transition at each of the five calls, the table of     */
/* snapshots, and the key names.                                         */
/* ------------------------------------------------------------------ */

#if !defined(MSI4NT_NO_PUBLISH) && !defined(MSI4NT_LEGACY_NT)
#define MSGIRQP_PUBLISH 1
#endif
#if !defined(MSI4NT_NO_HISTORY)
#define MSGIRQP_HISTORY 1
#endif
/* What a call captures (its detail, a claim's digest) and the lock that
 * serializes the calls: for the published state and for the history. */
#if defined(MSGIRQP_PUBLISH) || defined(MSGIRQP_HISTORY)
#define MSGIRQP_CALLS 1
#endif

#if defined(MSGIRQP_CALLS)
#define MSGIRQP_PUB_VERSION     1
#define MSGIRQP_PUB_SLOTS       32
#define MSGIRQP_PUB_FREE        0xFFFFFFFFUL    /* a table entry's Bus when unused */

#define MSGIRQP_PUB_CONNECT     1               /* LastCall */
#define MSGIRQP_PUB_DISCONNECT  2
#define MSGIRQP_PUB_REARM       3
#define MSGIRQP_PUB_CLAIM       4
#define MSGIRQP_PUB_RELEASE     5

#define MSGIRQP_CONN_NONE       0               /* ConnState */
#define MSGIRQP_CONN_CONNECTED  1
#define MSGIRQP_CONN_LIVE       2               /* live, not proven safe */
#define MSGIRQP_CONN_DOWN       3
#define MSGIRQP_CONN_QUARANTINE 4
#define MSGIRQP_CONN_PREPARED   6

#define MSGIRQP_CLAIM_NONE      0               /* ClaimState */
#define MSGIRQP_CLAIM_HELD      1
#define MSGIRQP_CLAIM_KEPT      2
#define MSGIRQP_CLAIM_RELEASED  3

#define MSGIRQP_DETAIL_NTSTATUS 1               /* DetailKind */
#define MSGIRQP_DETAIL_CONFIG   2
#define MSGIRQP_DETAIL_MAP      3
#define MSGIRQP_DETAIL_HAL      4

#define MSGIRQP_HAL_PIC         1               /* HAL reasons (InitReason, detail kind 4) */
#define MSGIRQP_HAL_UNKNOWN     2
#define MSGIRQP_HAL_X2APIC      3
#define MSGIRQP_HAL_VECTOR      4
#define MSGIRQP_HAL_IDT         5               /* the IDT gate leads elsewhere (record 08) */
#define MSGIRQP_HAL_CHANGED     6               /* the claim's line is no longer the HAL's */

#define MSGIRQP_PUB_INCOMPLETE  0x1UL           /* Flags: earlier calls may be missing */

/* The snapshot of record 07, section 3: version 1, 200 bytes, all ULONG
 * but the two byte arrays, so its layout is the same on x86 and amd64. */
typedef struct _MSGIRQP_PUB_SNAP {
    ULONG Size;
    ULONG Version;
    ULONG Header;
    ULONG Build;
    ULONG LoadTimeLow;
    ULONG LoadTimeHigh;
    ULONG Generation;
    ULONG ConnState;
    ULONG ClaimState;
    ULONG LastCall;
    ULONG LastStatus;
    ULONG DetailKind;
    ULONG Detail;
    ULONG Mode;
    ULONG InitReason;
    ULONG Type;
    ULONG Messages;
    ULONG Vector;
    ULONG Irql;
    ULONG Affinity;
    ULONG MessageAddress;
    ULONG MessageData;
    ULONG Source;
    ULONG LineVector;
    ULONG LineIrql;
    ULONG Irq;
    ULONG Joinable;
    ULONG Members;
    ULONG Budget;
    ULONG BudgetUsed;
    ULONG Counted;
    ULONG Calls;
    ULONG NotMine;
    UCHAR DriverName[16];
    ULONG IsaResult[8];         /* Irq, Sci, Sources, Members, PoolsScanned, HighEnd,
                                   HighLastInput, HighDetail */
    UCHAR IsaReason[16];
    ULONG Flags;                /* MSGIRQP_PUB_INCOMPLETE */
} MSGIRQP_PUB_SNAP;
MSI4NT_C_ASSERT(pub_size, sizeof(MSGIRQP_PUB_SNAP) == 200);
MSI4NT_C_ASSERT(pub_flags, FIELD_OFFSET(MSGIRQP_PUB_SNAP, Flags) == 196);
MSI4NT_C_ASSERT(pub_name, FIELD_OFFSET(MSGIRQP_PUB_SNAP, DriverName) == 132);
MSI4NT_C_ASSERT(pub_result, FIELD_OFFSET(MSGIRQP_PUB_SNAP, IsaResult) == 148);

/* What one call tells the transition (record 07, section 4). The facts
 * of the function's claim "now" are read after the call, under the
 * library's lock. */
typedef struct _MSGIRQP_PUB_FACTS {
    ULONG Call;
    ULONG Status;
    ULONG DetailKind;
    ULONG Detail;
    ULONG Mode;
    BOOLEAN Attach;             /* connect: attach mode (prepare) */
    BOOLEAN Live;               /* connect: Info is live after the call */
    BOOLEAN Quarantine;         /* disconnect: the quarantine */
    BOOLEAN Absent;             /* disconnect: ended with the function absent (record 08) */
    BOOLEAN AbsentOnly;         /* disconnect: with MSGIRQ_DISCONNECT_ABSENT_ONLY */
    ULONG AoOutcome;            /* ... MSGIRQP_AO_* */
    ULONG AoId;                 /* ... the last first dword read */
    BOOLEAN OnClaim;            /* disconnect: the connection was on a claim */
    BOOLEAN HaveCounts;
    BOOLEAN Fresh;              /* claim: CLAIM_KEPT for a claim made in this call */
    BOOLEAN Reached;            /* claim: the attempt reached the pools */
    BOOLEAN HoldsClaim;         /* the function holds a claim after the call */
    BOOLEAN KeptNow;            /* ... and it is kept for the boot */
    ULONG MembersNow;
    ULONG MembersLeft;          /* release OK: the claim's members left */
    ULONG Calls;
    ULONG NotMine;
    ULONG Type;                 /* the connection fields (connect) */
    ULONG Messages;
    ULONG Vector;
    ULONG Irql;
    ULONG Affinity;
    ULONG MessageAddress;
    ULONG MessageData;
    ULONG Source;
    ULONG LineVector;
    ULONG LineIrql;
    ULONG Counted;
    ULONG Irq;                  /* the claim fields (claim) */
    ULONG Joinable;
    UCHAR DriverName[16];
    ULONG Budget;
    ULONG BudgetUsed;
    ULONG IsaResult[8];
    UCHAR IsaReason[16];
} MSGIRQP_PUB_FACTS;

static VOID MsgIrqpPubZero(PVOID P, ULONG Bytes)
{
    UCHAR *b = (UCHAR *)P;
    ULONG k;

    for (k = 0; k < Bytes; k++) {
        b[k] = 0;
    }
}

static VOID MsgIrqpPubConnFields(MSGIRQP_PUB_SNAP *S, const MSGIRQP_PUB_FACTS *F)
{
    S->Type = F->Type;
    S->Messages = F->Messages;
    S->Vector = F->Vector;
    S->Irql = F->Irql;
    S->Affinity = F->Affinity;
    S->MessageAddress = F->MessageAddress;
    S->MessageData = F->MessageData;
    S->Source = F->Source;
    S->LineVector = F->LineVector;
    S->LineIrql = F->LineIrql;
    S->Counted = F->Counted;
    S->Calls = 0;
    S->NotMine = 0;
}

static VOID MsgIrqpPubCounts(MSGIRQP_PUB_SNAP *S, const MSGIRQP_PUB_FACTS *F)
{
    if (F->HaveCounts) {
        S->Calls = F->Calls;
        S->NotMine = F->NotMine;
    }
}

static VOID MsgIrqpPubAttempt(MSGIRQP_PUB_SNAP *S, const MSGIRQP_PUB_FACTS *F)
{
    ULONG k;

    if (!F->Reached) {
        return;
    }
    S->Budget = F->Budget;
    S->BudgetUsed = F->BudgetUsed;
    for (k = 0; k < 8; k++) {
        S->IsaResult[k] = F->IsaResult[k];
    }
    for (k = 0; k < 16; k++) {
        S->IsaReason[k] = F->IsaReason[k];
    }
}

/* The transition of record 07, section 4, for one call. Identity,
 * Generation and the copy's fields are the caller's. */
static VOID MsgIrqpPubApply(MSGIRQP_PUB_SNAP *S, const MSGIRQP_PUB_FACTS *F)
{
    ULONG k;

    S->LastCall = F->Call;
    S->LastStatus = F->Status;
    S->DetailKind = F->DetailKind;
    S->Detail = F->Detail;
    S->Mode = F->Mode;
    switch (F->Call) {
    case MSGIRQP_PUB_CONNECT:
        if (F->Status == MSGIRQ_STATUS_OK && F->Live) {
            S->ConnState = F->Attach ? MSGIRQP_CONN_PREPARED : MSGIRQP_CONN_CONNECTED;
            MsgIrqpPubConnFields(S, F);
        } else if (F->Status == MSGIRQ_STATUS_CONFIG_ACCESS && F->Live) {
            S->ConnState = MSGIRQP_CONN_LIVE;
            MsgIrqpPubConnFields(S, F);
        }
        break;
    case MSGIRQP_PUB_DISCONNECT:
        if (F->Status == MSGIRQ_STATUS_OK && F->Quarantine) {
            S->ConnState = MSGIRQP_CONN_QUARANTINE;
            if (F->OnClaim) {
                S->ClaimState = MSGIRQP_CLAIM_KEPT;
            }
            MsgIrqpPubCounts(S, F);
        } else if (F->Status == MSGIRQ_STATUS_OK) {
            S->ConnState = MSGIRQP_CONN_DOWN;
            MsgIrqpPubCounts(S, F);
        } else if (F->Status == MSGIRQ_STATUS_CONFIG_ACCESS) {
            S->ConnState = MSGIRQP_CONN_LIVE;
            MsgIrqpPubCounts(S, F);
        }
        break;
    case MSGIRQP_PUB_REARM:
        if (F->Status == MSGIRQ_STATUS_OK) {
            S->ConnState = MSGIRQP_CONN_CONNECTED;
            S->Vector = F->Vector;
            S->MessageAddress = F->MessageAddress;
            S->MessageData = F->MessageData;
            MsgIrqpPubCounts(S, F);
        } else if (F->Status == MSGIRQ_STATUS_CONFIG_ACCESS || F->Status == MSGIRQ_STATUS_BRAKED) {
            S->ConnState = MSGIRQP_CONN_LIVE;
            MsgIrqpPubCounts(S, F);
        }
        break;
    case MSGIRQP_PUB_CLAIM:
        if (F->Status == MSGIRQ_STATUS_OK || (F->Status == MSGIRQ_STATUS_CLAIM_KEPT && F->Fresh)) {
            if (S->ConnState == MSGIRQP_CONN_DOWN) {
                /* An ended connection's view does not survive a new claim. */
                S->ConnState = MSGIRQP_CONN_NONE;
                S->Type = S->Messages = S->Vector = S->Irql = S->Affinity = 0;
                S->MessageAddress = S->MessageData = S->Source = 0;
                S->LineVector = S->LineIrql = S->Counted = S->Calls = S->NotMine = 0;
            }
            S->ClaimState = (F->Status == MSGIRQ_STATUS_OK) ? MSGIRQP_CLAIM_HELD : MSGIRQP_CLAIM_KEPT;
            S->Irq = F->Irq;
            S->Joinable = F->Joinable;
            for (k = 0; k < 16; k++) {
                S->DriverName[k] = F->DriverName[k];
            }
            MsgIrqpPubAttempt(S, F);
        } else if (F->Status == MSGIRQ_STATUS_CLAIM_KEPT) {
            S->ClaimState = MSGIRQP_CLAIM_KEPT;          /* kept already: Last* only */
        } else if (F->Status != MSGIRQ_STATUS_IN_USE) {
            if (!F->HoldsClaim) {
                S->ClaimState = MSGIRQP_CLAIM_NONE;
            }
            MsgIrqpPubAttempt(S, F);
        }
        break;
    case MSGIRQP_PUB_RELEASE:
        if (F->Status == MSGIRQ_STATUS_OK) {
            S->ClaimState = MSGIRQP_CLAIM_RELEASED;
            S->Members = F->MembersLeft;
        } else if (F->Status == MSGIRQ_STATUS_CLAIM_KEPT) {
            S->ClaimState = MSGIRQP_CLAIM_KEPT;
        }
        break;
    default:
        break;
    }
    /* The claim the function holds now, read afresh at every publication
     * (so an entry taken after a lost claim call still shows it). */
    if (F->HoldsClaim) {
        S->Irq = F->Irq;
        S->Joinable = F->Joinable;
        S->Members = F->MembersNow;
        S->ClaimState = F->KeptNow ? MSGIRQP_CLAIM_KEPT : MSGIRQP_CLAIM_HELD;
    }
}

/* One entry of the copy's table (record 07, section 4). */
typedef struct _MSGIRQP_PUB_SLOT {
    ULONG Bus;                  /* MSGIRQP_PUB_FREE: unused */
    ULONG Dev;
    ULONG Func;
    ULONG Stamp;                /* the copy's count when the entry last changed */
    MSGIRQP_PUB_SNAP Snap;
} MSGIRQP_PUB_SLOT;

static VOID MsgIrqpPubTableInit(MSGIRQP_PUB_SLOT *T, ULONG N)
{
    ULONG k;

    for (k = 0; k < N; k++) {
        MsgIrqpPubZero(&T[k], sizeof(T[k]));
        T[k].Bus = MSGIRQP_PUB_FREE;
    }
}

/* The function's entry: its own, a free one, or the one longest unchanged
 * whose connection and claim are over (ConnState 0 or 3, ClaimState 0 or
 * 3), cleared. -1 when none is left. *Lost counts the history the copy
 * has lost: a replaced entry here, a call kept nowhere in MsgIrqpPubEntry.
 * A new entry taken while it is not 0 (this replacement included) is
 * marked incomplete for its life: the copy does not know whose history
 * was lost, and the function may be the one it was. */
static LONG MsgIrqpPubSlotFor(MSGIRQP_PUB_SLOT *T, ULONG N, ULONG Bus, ULONG Dev, ULONG Func,
                              ULONG *Lost)
{
    ULONG k;
    LONG best = -1;
    BOOLEAN replaced = FALSE;

    for (k = 0; k < N; k++) {
        if (T[k].Bus == Bus && T[k].Dev == Dev && T[k].Func == Func) {
            return (LONG)k;
        }
    }
    for (k = 0; k < N && best < 0; k++) {
        if (T[k].Bus == MSGIRQP_PUB_FREE) {
            best = (LONG)k;
        }
    }
    if (best < 0) {
        for (k = 0; k < N; k++) {
            const MSGIRQP_PUB_SNAP *s = &T[k].Snap;

            if ((s->ConnState == MSGIRQP_CONN_NONE || s->ConnState == MSGIRQP_CONN_DOWN) &&
                (s->ClaimState == MSGIRQP_CLAIM_NONE || s->ClaimState == MSGIRQP_CLAIM_RELEASED) &&
                (best < 0 || T[k].Stamp < T[best].Stamp)) {
                best = (LONG)k;
                replaced = TRUE;
            }
        }
    }
    if (best < 0) {
        return -1;
    }
    if (replaced && *Lost != 0xFFFFFFFFUL) {
        (*Lost)++;                      /* saturates: never back to 0 */
    }
    MsgIrqpPubZero(&T[best], sizeof(T[best]));
    T[best].Bus = Bus;
    T[best].Dev = Dev;
    T[best].Func = Func;
    if (*Lost != 0) {
        T[best].Snap.Flags = MSGIRQP_PUB_INCOMPLETE;
    }
    return best;
}

/* Where one call's snapshot is built (record 07, section 4): the
 * function's entry (*Index), or, with none left, Scratch, cleared and
 * marked incomplete, to be written and not kept (*Index -1, the call
 * counted in *Lost). */
static MSGIRQP_PUB_SNAP *MsgIrqpPubEntry(MSGIRQP_PUB_SLOT *T, ULONG N, ULONG Bus, ULONG Dev, ULONG Func,
                                         ULONG *Lost, MSGIRQP_PUB_SNAP *Scratch, LONG *Index)
{
    *Index = MsgIrqpPubSlotFor(T, N, Bus, Dev, Func, Lost);
    if (*Index >= 0) {
        return &T[*Index].Snap;
    }
    if (*Lost != 0xFFFFFFFFUL) {
        (*Lost)++;
    }
    MsgIrqpPubZero(Scratch, sizeof(*Scratch));
    Scratch->Flags = MSGIRQP_PUB_INCOMPLETE;
    return Scratch;
}

/* Key names (record 07, section 2): the copy as hex digits of its block's
 * address, the function as bus_dev_func in decimal. Return the length in
 * characters; the buffer gets a terminating zero. */
static ULONG MsgIrqpPubHex(WCHAR *Out, ULONG_PTR Value, ULONG Digits)
{
    ULONG k;

    for (k = 0; k < Digits; k++) {
        ULONG d = (ULONG)((Value >> (4 * (Digits - 1 - k))) & 0xF);

        Out[k] = (WCHAR)(d < 10 ? L'0' + d : L'A' + d - 10);
    }
    Out[Digits] = 0;
    return Digits;
}

static ULONG MsgIrqpPubDec(WCHAR *Out, ULONG Value)
{
    WCHAR t[10];
    ULONG n = 0;
    ULONG k;

    do {
        t[n++] = (WCHAR)(L'0' + Value % 10);
        Value /= 10;
    } while (Value != 0 && n < 10);
    for (k = 0; k < n; k++) {
        Out[k] = t[n - 1 - k];
    }
    Out[n] = 0;
    return n;
}

static ULONG MsgIrqpPubFunctionName(WCHAR *Out, ULONG Bus, ULONG Dev, ULONG Func)
{
    ULONG n;

    n = MsgIrqpPubDec(Out, Bus);
    Out[n++] = L'_';
    n += MsgIrqpPubDec(Out + n, Dev);
    Out[n++] = L'_';
    n += MsgIrqpPubDec(Out + n, Func);
    return n;
}

/* The first 15 characters of a driver object's name after "\Driver\"
 * (or the whole name when it has no such prefix), as ASCII. */
static VOID MsgIrqpPubDriverName(UCHAR *Out, const WCHAR *Name, ULONG Chars)
{
    static const WCHAR prefix[] = L"\\Driver\\";
    ULONG p = sizeof(prefix) / sizeof(prefix[0]) - 1;
    ULONG k;
    ULONG n = 0;

    MsgIrqpPubZero(Out, 16);
    if (Name == NULL) {
        return;
    }
    for (k = 0; k < p && k < Chars && Name[k] == prefix[k]; k++) {
    }
    k = (k == p) ? p : 0;
    for (; k < Chars && n < 15; k++) {
        Out[n++] = (UCHAR)((Name[k] >= 0x20 && Name[k] < 0x7F) ? Name[k] : '?');
    }
}
#endif /* MSGIRQP_PUBLISH */

/* ------------------------------------------------------------------ */
/* Part A: the event history's ring (design record 08): its notes, the  */
/* fold, one position's copy and the registry export, as pure functions */
/* over a ring; Part B takes the ring's flag around each at HIGH_LEVEL.  */
/* ------------------------------------------------------------------ */

#if defined(MSGIRQP_HISTORY)
#ifndef MSI4NT_HISTORY_EVENTS
#define MSI4NT_HISTORY_EVENTS 64
#endif
MSI4NT_C_ASSERT(history_events, MSI4NT_HISTORY_EVENTS >= 16 && MSI4NT_HISTORY_EVENTS <= 256 &&
                                (MSI4NT_HISTORY_EVENTS & (MSI4NT_HISTORY_EVENTS - 1)) == 0);
#define MSGIRQP_RING_MASK ((ULONG)MSI4NT_HISTORY_EVENTS - 1UL)

#define MSGIRQP_HIST_VERSION    1
#define MSGIRQP_HIST_HEADER     56UL
#define MSGIRQP_HIST_REPEAT_MAX 0xFFFFUL

/* Where an event belongs (MsgIrqpRingNote's Where): a function's
 * location, or the copy itself (the kind then carries
 * MSGIRQ_EVENT_COPY and the location is 0). */
#define MSGIRQP_EV_COPY 0x10000UL

/* The published History value's header (record 08, section 5). */
typedef struct _MSGIRQP_HIST_HEAD {
    ULONG Size;
    ULONG Version;
    ULONG HeaderSize;
    ULONG RecordSize;
    ULONG Capacity;
    ULONG Count;
    ULONG Newest;
    ULONG Lost;
    ULONG Dropped;
    ULONG TimeUnit;
    ULONG NowLow;
    ULONG NowHigh;
    ULONG LoadTimeLow;                  /* record 07's LoadTime: which load wrote it */
    ULONG LoadTimeHigh;
} MSGIRQP_HIST_HEAD;
MSI4NT_C_ASSERT(hist_head, sizeof(MSGIRQP_HIST_HEAD) == MSGIRQP_HIST_HEADER);

typedef struct _MSGIRQP_RING {
    volatile LONG Flag;                 /* the ring's spin flag (Part B, HIGH_LEVEL) */
    ULONG Written;                      /* positions filled since the copy started */
    ULONG Held;                         /* records the ring holds, at most its size */
    ULONG Sequence;                     /* the last number given; 0: none */
    ULONG Overwritten;                  /* saturating */
    ULONG LastOverwritten;              /* the number of the last record overwritten */
    ULONG Changes;                      /* notes and folds */
    ULONG PubLost;                      /* records overwritten before any value had them (saturating) */
    ULONG PubPending;                   /* staged records overwritten while their value is written */
    ULONG PubChanges;                   /* Changes when the last value written was staged */
    BOOLEAN PubAny;                     /* a value has been written */
    MSGIRQ_EVENT Slot[MSI4NT_HISTORY_EVENTS];
    UCHAR Mark[MSI4NT_HISTORY_EVENTS];  /* MSGIRQP_MARK_*: the record's place in the registry */
} MSGIRQP_RING;

#define MSGIRQP_MARK_DONE   0           /* a value written has had the record */
#define MSGIRQP_MARK_NEW    1           /* no value has had it yet */
#define MSGIRQP_MARK_STAGED 2           /* in the value being written */

/* What one staging took, committed only when its value was written. */
typedef struct _MSGIRQP_RING_STAGE {
    ULONG Changes;
} MSGIRQP_RING_STAGE;

/* Folds are made only while the oldest record held is within this many
 * numbers of the newest; past it, repeats are new records, which push the
 * old ones out, so no record held is ever more than the horizon plus the
 * ring's size behind the newest, well inside the 2^31 that a comparison
 * with an After near the newest needs (record 08, section 2). */
#define MSGIRQP_FOLD_HORIZON 0x40000000UL

/* Whether sequence number A comes after B. Numbers wrap at 2^32; the
 * comparison holds while they are less than 2^31 apart (record 08,
 * section 4). */
static BOOLEAN MsgIrqpSeqAfter(ULONG A, ULONG B)
{
    return (BOOLEAN)((LONG)(A - B) > 0);
}

static ULONG MsgIrqpSatAdd(ULONG A, ULONG B)
{
    return (A + B < A) ? 0xFFFFFFFFUL : A + B;
}

/* One note (record 08, section 2); the ring's flag held. Foldable: a
 * re-arm or a refusal, folded into the newest record while it is the
 * newest and the same: Kind, Location, status (Arg0 bits 0-15) and Arg1,
 * and for a refusal Arg2 and Arg3 too; a re-arm's Arg2 and Arg3 (its
 * counts) are taken from the latest. A fold takes a new number. */
static VOID MsgIrqpRingNote(MSGIRQP_RING *R, ULONG Kind, ULONG Where, const ULONG *Arg,
                            ULONG TimeLow, ULONG TimeHigh, BOOLEAN Foldable)
{
    MSGIRQ_EVENT *e;
    ULONG location = 0;
    ULONG seq;
    ULONG k;

    if (Where & MSGIRQP_EV_COPY) {
        Kind |= MSGIRQ_EVENT_COPY;
    } else {
        location = Where & 0xFFFFUL;
    }
    seq = R->Sequence + 1;
    if (seq == 0) {
        seq = 1;                                        /* 0 marks no event */
    }
    R->Changes++;
    if (Foldable && R->Held != 0 &&
        seq - R->Slot[(R->Written - R->Held) & MSGIRQP_RING_MASK].Sequence < MSGIRQP_FOLD_HORIZON) {
        e = &R->Slot[(R->Written - 1) & MSGIRQP_RING_MASK];
        if (e->Kind == (USHORT)Kind && e->Location == (USHORT)location &&
            (e->Arg[0] & 0xFFFFUL) == (Arg[0] & 0xFFFFUL) && e->Arg[1] == Arg[1] &&
            ((Kind & 0xFFUL) == MSGIRQ_EVENT_REARM || (e->Arg[2] == Arg[2] && e->Arg[3] == Arg[3]))) {
            ULONG repeats = e->Arg[0] >> 16;

            if (repeats < MSGIRQP_HIST_REPEAT_MAX) {
                repeats++;
            }
            e->Arg[0] = (Arg[0] & 0xFFFFUL) | (repeats << 16);
            e->Arg[2] = Arg[2];
            e->Arg[3] = Arg[3];
            e->Sequence = seq;
            e->TimeLow = TimeLow;
            e->TimeHigh = TimeHigh;
            R->Sequence = seq;
            return;
        }
    }
    k = R->Written & MSGIRQP_RING_MASK;
    e = &R->Slot[k];
    if (R->Held == (ULONG)MSI4NT_HISTORY_EVENTS) {
        R->Overwritten = MsgIrqpSatAdd(R->Overwritten, 1);
        R->LastOverwritten = e->Sequence;
        if (R->Mark[k] == MSGIRQP_MARK_NEW) {
            R->PubLost = MsgIrqpSatAdd(R->PubLost, 1);  /* no value has had it */
        } else if (R->Mark[k] == MSGIRQP_MARK_STAGED) {
            R->PubPending = MsgIrqpSatAdd(R->PubPending, 1);   /* lost unless that value is written */
        }
    } else {
        R->Held++;
    }
    R->Mark[k] = MSGIRQP_MARK_NEW;
    e->Sequence = seq;
    e->TimeLow = TimeLow;
    e->TimeHigh = TimeHigh;
    e->Kind = (USHORT)Kind;
    e->Location = (USHORT)location;
    for (k = 0; k < 4; k++) {
        e->Arg[k] = Arg[k];
    }
    if (Foldable) {
        e->Arg[0] &= 0xFFFFUL;                          /* no repeats yet */
    }
    R->Sequence = seq;
    R->Written++;
}

/* The record at position P, when the ring still holds it; the ring's
 * flag held. */
static BOOLEAN MsgIrqpRingAt(const MSGIRQP_RING *R, ULONG P, MSGIRQ_EVENT *E)
{
    if (R->Written - P - 1 >= R->Held) {
        return FALSE;                                   /* not yet, or overwritten */
    }
    *E = R->Slot[P & MSGIRQP_RING_MASK];
    return TRUE;
}

/* The records after After the ring overwrote, at most (record 08,
 * section 4): exact unless a fold took a number among them. */
static ULONG MsgIrqpRingSkipped(ULONG Overwritten, ULONG LastOverwritten, ULONG After)
{
    ULONG gap;

    if (Overwritten == 0 || !MsgIrqpSeqAfter(LastOverwritten, After)) {
        return 0;
    }
    gap = LastOverwritten - After;
    if (LastOverwritten < After) {
        gap--;                                          /* the wrap skipped 0 */
    }
    return (gap < Overwritten) ? gap : Overwritten;
}

/* Whether the ring changed since the last value written. */
static BOOLEAN MsgIrqpRingDirty(const MSGIRQP_RING *R)
{
    return (BOOLEAN)(!R->PubAny || R->Changes != R->PubChanges);
}

/* Stages the value (record 08, section 5): the header and the held
 * records, oldest first, into Out (room for the header and the ring's
 * size); the records not yet in a value written are marked staged, and
 * what the write's outcome settles goes into *S. Returns the bytes. The
 * ring's flag held; one staging at a time (PubLock). */
static ULONG MsgIrqpRingStage(MSGIRQP_RING *R, ULONG *Out, ULONG TimeUnit, ULONG NowLow,
                              ULONG NowHigh, ULONG LoadLow, ULONG LoadHigh, MSGIRQP_RING_STAGE *S)
{
    MSGIRQP_HIST_HEAD *h = (MSGIRQP_HIST_HEAD *)Out;
    MSGIRQ_EVENT *e = (MSGIRQ_EVENT *)((UCHAR *)Out + MSGIRQP_HIST_HEADER);
    ULONG first = R->Written - R->Held;
    ULONG k;
    ULONG s;

    for (k = 0; k < R->Held; k++) {
        s = (first + k) & MSGIRQP_RING_MASK;
        e[k] = R->Slot[s];
        if (R->Mark[s] == MSGIRQP_MARK_NEW) {
            R->Mark[s] = MSGIRQP_MARK_STAGED;
        }
    }
    R->PubPending = 0;
    S->Changes = R->Changes;
    h->Size = MSGIRQP_HIST_HEADER + R->Held * (ULONG)sizeof(MSGIRQ_EVENT);
    h->Version = MSGIRQP_HIST_VERSION;
    h->HeaderSize = MSGIRQP_HIST_HEADER;
    h->RecordSize = (ULONG)sizeof(MSGIRQ_EVENT);
    h->Capacity = (ULONG)MSI4NT_HISTORY_EVENTS;
    h->Count = R->Held;
    h->Newest = R->Sequence;
    h->Lost = R->PubLost;
    h->Dropped = 0;
    h->TimeUnit = TimeUnit;
    h->NowLow = NowLow;
    h->NowHigh = NowHigh;
    h->LoadTimeLow = LoadLow;
    h->LoadTimeHigh = LoadHigh;
    return h->Size;
}

/* The staged value's write ended. Written: the staged records the ring
 * still holds are done, and the staged ones overwritten meanwhile were
 * in the value, so they are not lost. Not written: the staged records
 * are new again, and those overwritten meanwhile are lost after all.
 * Records noted since the staging stay new either way. The ring's flag
 * held. */
static VOID MsgIrqpRingSettle(MSGIRQP_RING *R, const MSGIRQP_RING_STAGE *S, BOOLEAN Written)
{
    ULONG k;

    for (k = 0; k < (ULONG)MSI4NT_HISTORY_EVENTS; k++) {
        if (R->Mark[k] == MSGIRQP_MARK_STAGED) {
            R->Mark[k] = (UCHAR)(Written ? MSGIRQP_MARK_DONE : MSGIRQP_MARK_NEW);
        }
    }
    if (Written) {
        R->PubChanges = S->Changes;
        R->PubAny = TRUE;
    } else {
        R->PubLost = MsgIrqpSatAdd(R->PubLost, R->PubPending);
    }
    R->PubPending = 0;
}

/* An event's place from the function: its location, or the copy's own
 * when the function is not a valid one. */
static ULONG MsgIrqpEvLocation(ULONG Bus, ULONG Dev, ULONG Func)
{
    if (Bus > 255 || Dev > 31 || Func > 7) {
        return MSGIRQP_EV_COPY;
    }
    return (Bus << 8) | (Dev << 3) | Func;
}

/* A count that has just become a power of two (1, 2, 4, ...): STRAY and
 * SWALLOWED are noted only then (record 08, section 3). */
static BOOLEAN MsgIrqpPowerOfTwo(ULONG N)
{
    return (BOOLEAN)(N != 0 && (N & (N - 1)) == 0);
}
#endif /* MSGIRQP_HISTORY */

#if !defined(MSI4NT_HOST_TEST)

/* ------------------------------------------------------------------ */
/* Part B: the kernel side.                                            */
/* ------------------------------------------------------------------ */

#if defined(MSI4NT_AMD64)
void __cpuid(int Info[4], int Leaf);
unsigned __int64 __readmsr(unsigned long Register);
#pragma intrinsic(__cpuid)
#pragma intrinsic(__readmsr)
#endif

#if defined(MSGIRQP_PUBLISH)
/* The published state's routines (design record 07, section 8): imported
 * in a default build, resolved (and optional) in a hosted one. */
typedef NTSTATUS (NTAPI *MSGIRQP_PUB_CREATE)(PHANDLE Key, ACCESS_MASK Access,
                                             POBJECT_ATTRIBUTES Attributes, ULONG TitleIndex,
                                             PUNICODE_STRING Class, ULONG CreateOptions,
                                             PULONG Disposition);
typedef NTSTATUS (NTAPI *MSGIRQP_PUB_SET)(HANDLE Key, PUNICODE_STRING Name, ULONG TitleIndex,
                                          ULONG Type, PVOID Data, ULONG DataSize);
typedef NTSTATUS (NTAPI *MSGIRQP_PUB_CLOSE)(HANDLE Handle);
typedef VOID (NTAPI *MSGIRQP_PUB_TIME)(PLARGE_INTEGER Time);
#endif
#if defined(MSGIRQP_HISTORY) && defined(MSI4NT_HOSTED) && defined(MSI4NT_X86)
/* The history's clock in a hosted x86 build, resolved (record 08, section 3). */
typedef ULONGLONG (NTAPI *MSGIRQP_HIST_TIME)(VOID);
#endif

typedef VOID (NTAPI *MSGIRQP_SET_AFFINITY)(KAFFINITY Affinity);
typedef VOID (NTAPI *MSGIRQP_REVERT_AFFINITY)(VOID);

#define MSGIRQP_PATH_FAKE   1
#define MSGIRQP_PATH_NATIVE 2

#define MSGIRQP_MAGIC 0x7149734DUL

/* The trampoline's context: kept for good after a quarantine. */
typedef struct _MSGIRQP_TRAMP {
    volatile LONG Quarantined;
    MSGIRQ_MESSAGE_SERVICE_ROUTINE Routine;
    PVOID Context;
    volatile ULONG Entries;             /* the client's routine called (record 07: Calls) */
    volatile ULONG Stray;               /* ... and it returned FALSE (NotMine) */
    volatile ULONG Swallowed;           /* entries answered without calling it (record 08; saturating) */
    volatile ULONG StrayNoted;          /* Stray for the history: saturating, so STRAY stops */
    ULONG Location;                     /* the function, as an event names it (record 08) */
    ULONG IdtVector;
#if defined(MSI4NT_ISA_LINE)
    struct _MSGIRQP_ISA *Isa;           /* the claim of a record-05 connection */
#endif
} MSGIRQP_TRAMP;

/* The configuration route of one connection (design record 01, section 5). */
typedef struct _MSGIRQP_ROUTE {
    MSGIRQ_CONFIG_ACCESS Access;        /* Read NULL = the default route */
    PDEVICE_OBJECT Pdo;
    ULONG Bus;
    ULONG Dev;
    ULONG Func;
} MSGIRQP_ROUTE;

#if defined(MSI4NT_ISA_LINE)
/* One ISA claim (design record 05, section 2): one function's, or a
 * shared claim's with several members. HELD until its last member's
 * MsgIrqIsaRelease; KEPT for the rest of the boot after a quarantine of
 * a connection on it or a withdrawal that failed. */
#define MSGIRQP_ISA_HELD 1
#define MSGIRQP_ISA_KEPT 2
typedef struct _MSGIRQP_ISA {
    struct _MSGIRQP_ISA *Next;
    ULONG State;
    BOOLEAN Shared;
    ULONG Members;
    ULONG Irq;
    MSGIRQ_LINE_RESOURCE Line;          /* the translated vector */
    PDRIVER_OBJECT Driver;
    PDEVICE_OBJECT Owner;               /* the claim's scope */
    volatile LONG Heard;                /* the listen's count */
    ULONG BrakeEntries;                 /* per member; 0: off */
    ULONG MaxMembers;                   /* the lowest cap its maker and joiners asked for */
    MSGIRQP_BRAKE Brake;                /* section 2.7 */
} MSGIRQP_ISA;

/* A function's membership of a claim. */
typedef struct _MSGIRQP_ISA_MEMBER {
    struct _MSGIRQP_ISA_MEMBER *Next;
    ULONG Bus;
    ULONG Dev;
    ULONG Func;
    MSGIRQP_ISA *Claim;
} MSGIRQP_ISA_MEMBER;

typedef NTSTATUS (NTAPI *MSGIRQP_REPORT_DETECTION)(PDRIVER_OBJECT DriverObject,
                                                   PCM_RESOURCE_LIST DriverList,
                                                   ULONG DriverListSize,
                                                   PDEVICE_OBJECT DeviceObject,
                                                   PCM_RESOURCE_LIST DeviceList,
                                                   ULONG DeviceListSize,
                                                   PBOOLEAN ConflictDetected);
typedef NTSTATUS (NTAPI *MSGIRQP_CREATE_DEVICE)(PDRIVER_OBJECT DriverObject,
                                                ULONG DeviceExtensionSize,
                                                PUNICODE_STRING DeviceName,
                                                DEVICE_TYPE DeviceType,
                                                ULONG DeviceCharacteristics,
                                                BOOLEAN Exclusive,
                                                PDEVICE_OBJECT *DeviceObject);
typedef VOID (NTAPI *MSGIRQP_DELETE_DEVICE)(PDEVICE_OBJECT DeviceObject);
typedef ULONG (NTAPI *MSGIRQP_GET_VECTOR)(INTERFACE_TYPE InterfaceType, ULONG BusNumber,
                                          ULONG BusInterruptLevel, ULONG BusInterruptVector,
                                          PKIRQL Irql, PKAFFINITY Affinity);
typedef ULONG (NTAPI *MSGIRQP_GET_BUS_DATA)(ULONG BusDataType, ULONG BusNumber,
                                            ULONG SlotNumber, PVOID Buffer, ULONG Offset,
                                            ULONG Length);
typedef NTSTATUS (NTAPI *MSGIRQP_OPEN_KEY)(PHANDLE Key, ACCESS_MASK Access,
                                           POBJECT_ATTRIBUTES Attributes);
typedef NTSTATUS (NTAPI *MSGIRQP_ENUM_KEY)(HANDLE Key, ULONG Index, ULONG Class, PVOID Info,
                                           ULONG Length, PULONG ResultLength);
typedef NTSTATUS (NTAPI *MSGIRQP_QUERY_VALUE)(HANDLE Key, PUNICODE_STRING Name, ULONG Class,
                                              PVOID Info, ULONG Length, PULONG ResultLength);
typedef NTSTATUS (NTAPI *MSGIRQP_CLOSE)(HANDLE Handle);
typedef PEPROCESS (NTAPI *MSGIRQP_CURRENT_PROCESS)(VOID);
typedef NTSTATUS (NTAPI *MSGIRQP_CREATE_KEY)(PHANDLE Key, ACCESS_MASK Access,
                                             POBJECT_ATTRIBUTES Attributes, ULONG TitleIndex,
                                             PUNICODE_STRING Class, ULONG CreateOptions,
                                             PULONG Disposition);
typedef NTSTATUS (NTAPI *MSGIRQP_SET_VALUE)(HANDLE Key, PUNICODE_STRING Name, ULONG TitleIndex,
                                            ULONG Type, PVOID Data, ULONG DataSize);
#endif

typedef struct _MSGIRQP_CONN {
    ULONG Magic;
    struct _MSGIRQP_CONN *Next;
    PMSGIRQ_MESSAGE_INFO Info;          /* what the client holds; follows this header */
    ULONG Path;
    MSGIRQP_ROUTE Route;
    MSGIRQP_TARGET Target;
    PVOID TableVa;                      /* MSI-X mapping */
    ULONG TableBytes;
    ULONG Cpu;
    ULONG Vector;
    KIRQL Irql;
    KAFFINITY LineAffinity;
    PKINTERRUPT Interrupt;
    MSGIRQP_TRAMP *Tramp;
    PVOID NativeContext;
    ULONG NativeVersion;
    PVOID NativeTable;
#if defined(MSI4NT_ISA_LINE)
    MSGIRQP_ISA *Isa;                   /* the claim this connection uses, or NULL */
#endif
} MSGIRQP_CONN;

static struct {
    BOOLEAN Initialized;
    ULONG Mode;
    ULONG NoneStatus;                   /* what a connect returns in MODE_NONE */
    MSGIRQ_HOST Host;
    BOOLEAN HaveHost;
    MSGIRQP_SET_AFFINITY SetAffinity;
    MSGIRQP_REVERT_AFFINITY RevertAffinity;
    PVOID ConnectEx;
    PVOID DisconnectEx;
    PVOID ApicVa;
    KEVENT Lock;
#if defined(MSGIRQP_CALLS)
    /* The published state (design record 07) and the history (record 08):
     * the calls of this copy that publish or note take PubLock, then Lock. */
    KEVENT PubLock;
    ULONG PubInitReason;
    ULONG PubDetailKind;                /* the current call's detail, under PubLock */
    ULONG PubDetail;
    ULONG PubMembersLeft;               /* a release's, under PubLock */
    MSGIRQP_PUB_FACTS PubClaim;         /* a claim attempt's facts, under PubLock */
#endif
#if defined(MSGIRQP_HISTORY)
    MSGIRQP_RING Ring;                  /* record 08 */
#if defined(MSI4NT_HOSTED) && defined(MSI4NT_X86)
    MSGIRQP_HIST_TIME HistTime;
#endif
    ULONG HistChecks;                   /* the connect's agreement checks, under PubLock */
    ULONG HistFrom;                     /* the claim's pool: 1 ISA, 2 upper, 3 joined */
    ULONG HistFlags;                    /* the claim's caller flags */
    ULONG HistNew;                      /* the claim's HighNew */
    ULONG HistWithdraw;                 /* the NTSTATUS of a withdrawal that failed */
    ULONG HistIrq;                      /* a release's IRQ or input */
    ULONG HistStopIrq;                  /* where a claim stopped on a refused report */
    ULONG HistStopStatus;               /* ... and the report's NTSTATUS; 0: none */
#if defined(MSGIRQP_PUBLISH)
    ULONG HistBuf[(MSGIRQP_HIST_HEADER + MSI4NT_HISTORY_EVENTS * 32UL) / 4];
#endif
#endif
#if defined(MSGIRQP_PUBLISH)
    BOOLEAN PubOn;
    ULONG PubGeneration;
    ULONG PubStamp;
    ULONG PubLost;                      /* history lost: entries replaced, calls kept nowhere */
    ULONG PubLoadLow;
    ULONG PubLoadHigh;
    MSGIRQP_PUB_CREATE PubCreate;
    MSGIRQP_PUB_SET PubSet;
    MSGIRQP_PUB_CLOSE PubClose;
    MSGIRQP_PUB_SLOT Pub[MSGIRQP_PUB_SLOTS];
#endif
    MSGIRQP_CONN *List;
#if defined(MSI4NT_ISA_LINE)
    MSGIRQP_ISA *IsaList;               /* every claim, HELD or KEPT */
    MSGIRQP_ISA_MEMBER *IsaMembers;     /* every function's membership */
    MSGIRQP_HIGH_SPENT HighSpent;       /* the upper inputs this image has been charged for
                                           since initialisation (its record holds them too) */
    MSGIRQP_CREATE_KEY IsaCreateKey;    /* the upper pool's record only */
    MSGIRQP_SET_VALUE IsaSetValue;
    MSGIRQP_REPORT_DETECTION IsaReport;
    MSGIRQP_CREATE_DEVICE IsaCreate;
    MSGIRQP_DELETE_DEVICE IsaDelete;
    MSGIRQP_GET_VECTOR IsaVector;
    MSGIRQP_GET_BUS_DATA IsaBusData;
    MSGIRQP_OPEN_KEY IsaOpenKey;
    MSGIRQP_ENUM_KEY IsaEnumKey;
    MSGIRQP_QUERY_VALUE IsaQueryValue;
    MSGIRQP_CLOSE IsaClose;
    MSGIRQP_CURRENT_PROCESS IsaProcess;
    PEPROCESS *IsaSystemProcess;
#endif
} MsgIrqpG;

/* The build's bits (record 07's Build, record 08's INIT). */
#define MSGIRQP_BUILD_BITS (0UL                                                       \
                            | MSGIRQP_BUILD_HOSTED | MSGIRQP_BUILD_LEGACY | MSGIRQP_BUILD_ISA \
                            | MSGIRQP_BUILD_NO_NATIVE)
#if defined(MSI4NT_HOSTED)
#define MSGIRQP_BUILD_HOSTED 0x1UL
#else
#define MSGIRQP_BUILD_HOSTED 0
#endif
#if defined(MSI4NT_LEGACY_NT)
#define MSGIRQP_BUILD_LEGACY 0x2UL
#else
#define MSGIRQP_BUILD_LEGACY 0
#endif
#if defined(MSI4NT_ISA_LINE)
#define MSGIRQP_BUILD_ISA 0x4UL
#else
#define MSGIRQP_BUILD_ISA 0
#endif
#if defined(MSI4NT_NO_NATIVE)
#define MSGIRQP_BUILD_NO_NATIVE 0x8UL
#else
#define MSGIRQP_BUILD_NO_NATIVE 0
#endif

#if defined(MSGIRQP_CALLS)
static VOID MsgIrqpPubBegin(VOID);
static VOID MsgIrqpPubEnd(VOID);
#endif
#if defined(MSGIRQP_HISTORY)
#define MSGIRQP_CLAIM_STOP(irq, ns) (MsgIrqpG.HistStopIrq = (irq), MsgIrqpG.HistStopStatus = (ULONG)(ns))
#else
#define MSGIRQP_CLAIM_STOP(irq, ns) ((VOID)0)
#endif
#if defined(MSGIRQP_HISTORY) && defined(MSGIRQP_PUBLISH)
static VOID MsgIrqpHistPublish(VOID);
static VOID MsgIrqpHistCalm(VOID);
#define MSGIRQP_HIST_CALM() MsgIrqpHistCalm()
#else
#define MSGIRQP_HIST_CALM() ((VOID)0)
#endif

/* The copy's HAL judgement at MsgIrqInitialize (record 07, section 6). */
#if defined(MSGIRQP_CALLS)
#define MSGIRQP_INIT_REASON(r) (MsgIrqpG.PubInitReason = (r))
#else
#define MSGIRQP_INIT_REASON(r) ((VOID)0)
#endif

#define MSGIRQP_APIC_VERSION 0x30
#define MSGIRQP_APIC_ID      0x20
#define MSGIRQP_APIC_SVR     0xF0

static ULONG MsgIrqpApicRead(ULONG Offset)
{
    return MSGIRQP_MMIO_READ((volatile ULONG *)((PUCHAR)MsgIrqpG.ApicVa + Offset));
}

/* ------------------------------ pool ------------------------------ */

static PVOID MsgIrqpAlloc(ULONG Bytes)
{
    PUCHAR p;
    ULONG i;

#if defined(MSI4NT_HOSTED)
    p = (PUCHAR)MsgIrqpG.Host.Allocate(MsgIrqpG.Host.HostContext, Bytes);
#else
    if (MsgIrqpG.HaveHost && MsgIrqpG.Host.Allocate != NULL) {
        p = (PUCHAR)MsgIrqpG.Host.Allocate(MsgIrqpG.Host.HostContext, Bytes);
    } else {
        p = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, Bytes, MSI4NT_POOL_TAG);
    }
#endif
    if (p != NULL) {
        for (i = 0; i < Bytes; i++) {
            p[i] = 0;
        }
    }
    return p;
}

static VOID MsgIrqpFree(PVOID Block)
{
#if defined(MSI4NT_HOSTED)
    MsgIrqpG.Host.Free(MsgIrqpG.Host.HostContext, Block);
#else
    if (MsgIrqpG.HaveHost && MsgIrqpG.Host.Allocate != NULL) {
        MsgIrqpG.Host.Free(MsgIrqpG.Host.HostContext, Block);
    } else {
        ExFreePool(Block);
    }
#endif
}

/* ------------------------------ lock ------------------------------ */

static VOID MsgIrqpLock(VOID)
{
    (VOID)KeWaitForSingleObject(&MsgIrqpG.Lock, Executive, KernelMode, FALSE, NULL);
}

static VOID MsgIrqpUnlock(VOID)
{
    (VOID)KeSetEvent(&MsgIrqpG.Lock, 0, FALSE);
}

/* ------------------------- configuration -------------------------- */

#if !defined(MSI4NT_HOSTED) && !defined(MSI4NT_LEGACY_NT)
/* The WDM default route: IRP_MN_READ_CONFIG / IRP_MN_WRITE_CONFIG to the
 * top of the PDO's stack, synchronously, at PASSIVE_LEVEL. */
static BOOLEAN MsgIrqpPnpConfig(PDEVICE_OBJECT Pdo, BOOLEAN Read, ULONG Offset,
                                PVOID Buffer, ULONG Length)
{
    PDEVICE_OBJECT top;
    PIRP irp;
    PIO_STACK_LOCATION sp;
    IO_STATUS_BLOCK iosb;
    KEVENT event;
    NTSTATUS status;

    if (Pdo == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return FALSE;
    }
    top = IoGetAttachedDeviceReference(Pdo);
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, top, NULL, 0, NULL, &event, &iosb);
    if (irp == NULL) {
        ObDereferenceObject(top);
        return FALSE;
    }
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    sp = IoGetNextIrpStackLocation(irp);
    sp->MinorFunction = (UCHAR)(Read ? IRP_MN_READ_CONFIG : IRP_MN_WRITE_CONFIG);
    sp->Parameters.ReadWriteConfig.WhichSpace = PCI_WHICHSPACE_CONFIG;
    sp->Parameters.ReadWriteConfig.Buffer = Buffer;
    sp->Parameters.ReadWriteConfig.Offset = Offset;
    sp->Parameters.ReadWriteConfig.Length = Length;
    status = IoCallDriver(top, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }
    ObDereferenceObject(top);
    return (NT_SUCCESS(status) && iosb.Information == Length) ? TRUE : FALSE;
}
#endif

#if !defined(MSI4NT_HOSTED)
/* The legacy default route: HalGetBusData / HalSetBusData. */
static BOOLEAN MsgIrqpHalConfig(const MSGIRQP_ROUTE *R, BOOLEAN Read, ULONG Offset,
                                PVOID Buffer, ULONG Length)
{
    PCI_SLOT_NUMBER slot;
    ULONG done;

    slot.u.AsULONG = 0;
    slot.u.bits.DeviceNumber = R->Dev;
    slot.u.bits.FunctionNumber = R->Func;
    if (Read) {
        done = HalGetBusDataByOffset(PCIConfiguration, R->Bus, slot.u.AsULONG,
                                     Buffer, Offset, Length);
    } else {
        done = HalSetBusDataByOffset(PCIConfiguration, R->Bus, slot.u.AsULONG,
                                     Buffer, Offset, Length);
    }
    return (done == Length) ? TRUE : FALSE;
}
#endif

static BOOLEAN MsgIrqpRouteAccess(const MSGIRQP_ROUTE *R, BOOLEAN Read, ULONG Offset,
                                  PVOID Buffer, ULONG Length)
{
    MSGIRQ_CONFIG_ROUTINE fn;

    if (Offset > 255 || Length == 0 || Offset + Length > 256) {
        return FALSE;
    }
    fn = Read ? R->Access.Read : R->Access.Write;
    if (fn != NULL) {
        return (fn(R->Access.ConfigContext, Offset, Buffer, Length) == Length) ? TRUE : FALSE;
    }
#if defined(MSI4NT_HOSTED)
    return FALSE;
#elif defined(MSI4NT_LEGACY_NT)
    return MsgIrqpHalConfig(R, Read, Offset, Buffer, Length);
#else
    if (R->Pdo != NULL) {
        return MsgIrqpPnpConfig(R->Pdo, Read, Offset, Buffer, Length);
    }
    return MsgIrqpHalConfig(R, Read, Offset, Buffer, Length);
#endif
}

/* A configuration access that failed is the current call's detail
 * (record 07, section 3: kind 2, the offset), unless it has one already. */
static BOOLEAN MsgIrqpIoNote(BOOLEAN Ok, ULONG Offset)
{
#if defined(MSGIRQP_CALLS)
    if (!Ok && MsgIrqpG.PubDetailKind == 0) {
        MsgIrqpG.PubDetailKind = MSGIRQP_DETAIL_CONFIG;
        MsgIrqpG.PubDetail = Offset;
    }
#else
    UNREFERENCED_PARAMETER(Offset);
#endif
    return Ok;
}

static BOOLEAN MsgIrqpIoRead(PVOID Context, ULONG Offset, PVOID Buffer, ULONG Length)
{
    return MsgIrqpIoNote(MsgIrqpRouteAccess((const MSGIRQP_ROUTE *)Context, TRUE, Offset, Buffer,
                                            Length), Offset);
}

static BOOLEAN MsgIrqpIoWrite(PVOID Context, ULONG Offset, PVOID Buffer, ULONG Length)
{
    return MsgIrqpIoNote(MsgIrqpRouteAccess((const MSGIRQP_ROUTE *)Context, FALSE, Offset, Buffer,
                                            Length), Offset);
}

static VOID MsgIrqpIoFor(const MSGIRQP_ROUTE *R, MSGIRQP_IO *Io)
{
    Io->Context = (PVOID)R;
    Io->Read = MsgIrqpIoRead;
    Io->Write = MsgIrqpIoWrite;
}

/* The same route without the note of a failure: for MsgIrqQuery, which
 * publishes nothing and may run beside a call that does (record 07). */
static BOOLEAN MsgIrqpIoReadQuiet(PVOID Context, ULONG Offset, PVOID Buffer, ULONG Length)
{
    return MsgIrqpRouteAccess((const MSGIRQP_ROUTE *)Context, TRUE, Offset, Buffer, Length);
}

static BOOLEAN MsgIrqpIoWriteQuiet(PVOID Context, ULONG Offset, PVOID Buffer, ULONG Length)
{
    return MsgIrqpRouteAccess((const MSGIRQP_ROUTE *)Context, FALSE, Offset, Buffer, Length);
}

/* Builds a route from the parameter block; FALSE for an invalid one. */
static BOOLEAN MsgIrqpRouteFrom(const MSGIRQ_CONNECT_PARAMS *P, MSGIRQP_ROUTE *R)
{
    R->Pdo = P->MessageBased.PhysicalDeviceObject;
    R->Bus = P->MessageBased.Bus;
    R->Dev = P->MessageBased.Dev;
    R->Func = P->MessageBased.Func;
    R->Access.Read = NULL;
    R->Access.Write = NULL;
    R->Access.ConfigContext = NULL;
    if (R->Bus > 255 || R->Dev > 31 || R->Func > 7) {
        return FALSE;
    }
    if (P->MessageBased.ConfigAccess != NULL) {
        R->Access = *P->MessageBased.ConfigAccess;
        if (R->Access.Read == NULL || R->Access.Write == NULL) {
            return FALSE;
        }
    } else {
#if defined(MSI4NT_HOSTED)
        return FALSE;
#endif
    }
    return TRUE;
}

/* ----------------------------- CPU -------------------------------- */

#if defined(MSI4NT_X86)
static BOOLEAN MsgIrqpHaveCpuid(VOID)
{
    ULONG before;
    ULONG after;

    __asm {
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 0x00200000
        push eax
        popfd
        pushfd
        pop eax
        push ecx
        popfd
        mov before, ecx
        mov after, eax
    }
    return ((before ^ after) & 0x00200000UL) ? TRUE : FALSE;
}

static VOID MsgIrqpCpuid(ULONG Leaf, ULONG *Eax, ULONG *Edx)
{
    ULONG a;
    ULONG d;

    __asm {
        push ebx
        mov eax, Leaf
        xor ecx, ecx
        _emit 0x0F
        _emit 0xA2
        mov a, eax
        mov d, edx
        pop ebx
    }
    *Eax = a;
    *Edx = d;
}

static VOID MsgIrqpReadApicBaseMsr(ULONG *Lo, ULONG *Hi)
{
    ULONG lo;
    ULONG hi;

    __asm {
        mov ecx, 0x1B
        _emit 0x0F
        _emit 0x32
        mov lo, eax
        mov hi, edx
    }
    *Lo = lo;
    *Hi = hi;
}
#endif

/*
 * Steps 1 and 2 of the HAL-mode probe (design record 01, section 1): the
 * LAPIC's physical base, or FALSE when the processor has none enabled.
 */
static BOOLEAN MsgIrqpApicBase(PHYSICAL_ADDRESS *Base)
{
    ULONG family;
    ULONG lo;
    ULONG hi;
#if defined(MSI4NT_X86)
    ULONG eax;
    ULONG edx;

    if (!MsgIrqpHaveCpuid()) {
        MSGIRQP_INIT_REASON(MSGIRQP_HAL_UNKNOWN);
        return FALSE;
    }
    MsgIrqpCpuid(1, &eax, &edx);
    if ((edx & 0x200UL) == 0) {
        MSGIRQP_INIT_REASON(MSGIRQP_HAL_PIC);           /* the LAPIC is off */
        return FALSE;
    }
    family = (eax >> 8) & 0xFUL;
    if (family < 5) {
        MSGIRQP_INIT_REASON(MSGIRQP_HAL_UNKNOWN);
        return FALSE;
    }
    if (family == 5) {
        Base->LowPart = 0xFEE00000UL;
        Base->HighPart = 0;
        return TRUE;
    }
    if ((edx & 0x20UL) == 0) {          /* no MSR instruction */
        MSGIRQP_INIT_REASON(MSGIRQP_HAL_UNKNOWN);
        return FALSE;
    }
    MsgIrqpReadApicBaseMsr(&lo, &hi);
#else
    ULONGLONG msr;
    int info[4];

    __cpuid(info, 1);
    if ((info[3] & 0x200) == 0) {
        MSGIRQP_INIT_REASON(MSGIRQP_HAL_PIC);
        return FALSE;
    }
    family = ((ULONG)info[0] >> 8) & 0xFUL;
    msr = __readmsr(0x1B);
    lo = (ULONG)msr;
    hi = (ULONG)(msr >> 32);
#endif
    MSI4NT_TRACE(("msi4nt: family %lu apicbase %08lx%08lx\n", family, hi, lo));
    if (!MsgIrqpApicBaseUsable(lo)) {
        MSI4NT_TRACE(("msi4nt: local APIC disabled or in x2APIC mode\n"));
        MSGIRQP_INIT_REASON((lo & MSGIRQP_APICBASE_X2APIC) ? MSGIRQP_HAL_X2APIC : MSGIRQP_HAL_PIC);
        return FALSE;
    }
    Base->LowPart = lo & 0xFFFFF000UL;
    Base->HighPart = (LONG)(hi & 0x000FFFFFUL);
    return TRUE;
}

/* The LAPIC ID of one CPU, read there from its ID register (design record
 * 01, section 4): the hardware's own value, never the processor number. */
static ULONG MsgIrqpApicIdOn(ULONG Cpu)
{
    ULONG id;

    MsgIrqpG.SetAffinity(((KAFFINITY)1) << Cpu);
    id = (MsgIrqpApicRead(MSGIRQP_APIC_ID) >> 24) & 0xFFUL;
    MsgIrqpG.RevertAffinity();
    return id;
}

/* The drain (design record 01, section 2): run at PASSIVE_LEVEL on the
 * target CPU once, so every interrupt it had pending above this level has
 * been dispatched. */
static VOID MsgIrqpDrain(ULONG Cpu)
{
    MsgIrqpG.SetAffinity(((KAFFINITY)1) << Cpu);
    MsgIrqpG.RevertAffinity();
}

#if defined(MSI4NT_X86)
/*
 * Does the IDT gate for this vector lead into the interrupt object just
 * connected? Read with SIDT on the current CPU (every CPU's IDT carries a
 * connected vector's gate); nothing is written. The bound is a sanity
 * window, not a structure layout (design record 01, section 2, step 4).
 */
#define MSGIRQP_IDT_WINDOW 0x400
static BOOLEAN MsgIrqpIdtConfirms(ULONG Vector, PKINTERRUPT Object, ULONG Cpu)
{
    UCHAR idtr[6];
    PUCHAR gate;
    ULONG target;
    ULONG obj = (ULONG)(ULONG_PTR)Object;
    ULONG limit;
    KIRQL old;

    /* On the target CPU, whose interrupt object is the first of the set
     * IoConnectInterrupt returned (the lowest CPU of the line's affinity). */
    MsgIrqpG.SetAffinity(((KAFFINITY)1) << Cpu);
    KeRaiseIrql(DISPATCH_LEVEL, &old);
    __asm { sidt idtr }
    limit = *(USHORT *)&idtr[0];
    gate = (PUCHAR)(ULONG_PTR)(*(ULONG *)&idtr[2]) + Vector * 8;
    target = 0;
    if (Vector * 8 + 7 <= limit) {
        target = *(USHORT *)gate | ((ULONG)*(USHORT *)(gate + 6) << 16);
    }
    KeLowerIrql(old);
    MsgIrqpG.RevertAffinity();
    return (target >= obj && target < obj + MSGIRQP_IDT_WINDOW) ? TRUE : FALSE;
}
#endif

/* The pause between the absence proof's reads: a timed wait on an event
 * nobody sets, at PASSIVE_LEVEL (the import set has no stall routine). */
static VOID MsgIrqpPause(VOID)
{
    KEVENT event;
    LARGE_INTEGER t;

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    t.QuadPart = -(LONGLONG)MSGIRQP_ABSENT_PAUSE_MS * 10000;
    (VOID)KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &t);
}

/* ----------------------------- history ---------------------------- */

#if defined(MSGIRQP_HISTORY)
/* The ring's flag (record 08, section 2): the processor at HIGH_LEVEL, so
 * no note on it can interrupt another, then an atomic exchange against
 * the other processors. Nothing done under it waits or calls out. */
static KIRQL MsgIrqpRingEnter(VOID)
{
    KIRQL old;

    KeRaiseIrql(HIGH_LEVEL, &old);
    while (MsgIrqpXchg(&MsgIrqpG.Ring.Flag, 1) != 0) {
        while (MsgIrqpG.Ring.Flag != 0) {
        }
    }
    return old;
}

static VOID MsgIrqpRingLeave(KIRQL Old)
{
    (VOID)MsgIrqpXchg(&MsgIrqpG.Ring.Flag, 0);
    KeLowerIrql(Old);
}

/* The interrupt time (record 08, section 3), callable at any IRQL; 0 in
 * the legacy build and in a hosted x86 build that could not resolve it. */
static VOID MsgIrqpHistNow(ULONG *Low, ULONG *High)
{
#if defined(MSI4NT_LEGACY_NT)
    *Low = 0;
    *High = 0;
#else
    ULARGE_INTEGER t;

#if defined(MSI4NT_HOSTED) && defined(MSI4NT_X86)
    t.QuadPart = (MsgIrqpG.HistTime != NULL) ? MsgIrqpG.HistTime() : 0;
#else
    t.QuadPart = KeQueryInterruptTime();
#endif
    *Low = t.LowPart;
    *High = t.HighPart;
#endif
}

static ULONG MsgIrqpHistUnit(VOID)
{
#if defined(MSI4NT_LEGACY_NT)
    return MSGIRQ_TIME_NONE;
#elif defined(MSI4NT_HOSTED) && defined(MSI4NT_X86)
    return (MsgIrqpG.HistTime != NULL) ? MSGIRQ_TIME_NT : MSGIRQ_TIME_NONE;
#else
    return MSGIRQ_TIME_NT;
#endif
}

/* One event, at any IRQL. */
static VOID MsgIrqpEv(ULONG Kind, ULONG Location, ULONG A0, ULONG A1, ULONG A2, ULONG A3,
                      BOOLEAN Foldable)
{
    ULONG arg[4];
    ULONG lo;
    ULONG hi;
    KIRQL old;

    arg[0] = A0;
    arg[1] = A1;
    arg[2] = A2;
    arg[3] = A3;
    old = MsgIrqpRingEnter();
    MsgIrqpHistNow(&lo, &hi);
    MsgIrqpRingNote(&MsgIrqpG.Ring, Kind, Location, arg, lo, hi, Foldable);
    MsgIrqpRingLeave(old);
}

/* A call refused (REFUSED, folded). */
static VOID MsgIrqpEvRefused(ULONG Location, ULONG Call, ULONG Status, ULONG DetailKind,
                             ULONG Detail)
{
    MsgIrqpEv(MSGIRQ_EVENT_REFUSED, Location, Status, Call, DetailKind, Detail, TRUE);
}

/* A call refused before it named a function: its IRQL as the detail
 * when that was the reason; at PASSIVE_LEVEL also a calm point that never
 * waits (record 08, section 5). Never called with MsgIrqpG.Lock held; a
 * caller that holds PubLock publishes at its own end. */
static VOID MsgIrqpEvEarly(ULONG Call, ULONG Status)
{
    KIRQL irql = KeGetCurrentIrql();

    if (irql != PASSIVE_LEVEL) {
        MsgIrqpEvRefused(MSGIRQP_EV_COPY, Call, Status, MSGIRQ_DETAIL_IRQL, irql);
    } else {
        MsgIrqpEvRefused(MSGIRQP_EV_COPY, Call, Status, 0, 0);
        MSGIRQP_HIST_CALM();
    }
}
#define MSGIRQP_EV_EARLY(call, status) MsgIrqpEvEarly((call), (status))
#else
#define MSGIRQP_EV_EARLY(call, status) ((VOID)0)
#endif

/* --------------------------- trampoline --------------------------- */

#if defined(MSGIRQP_HISTORY)
/* STRAY and SWALLOWED from a trampoline, at DIRQL with the interrupt
 * object's lock held: their counts saturate, so each kind is noted at
 * most 32 times per connection (record 08, section 3). */
static VOID MsgIrqpEvSwallowed(MSGIRQP_TRAMP *T, ULONG Why)
{
    if (T->Swallowed != 0xFFFFFFFFUL && MsgIrqpPowerOfTwo(++T->Swallowed)) {
        MsgIrqpEv(MSGIRQ_EVENT_SWALLOWED, T->Location, T->Swallowed, T->IdtVector, Why, 0, FALSE);
    }
}

static VOID MsgIrqpEvStray(MSGIRQP_TRAMP *T)
{
    if (T->StrayNoted != 0xFFFFFFFFUL && MsgIrqpPowerOfTwo(++T->StrayNoted)) {
        MsgIrqpEv(MSGIRQ_EVENT_STRAY, T->Location, T->Entries, T->Stray, T->IdtVector, 0, FALSE);
    }
}
#endif

/* The line trampoline. The two counts (record 07, section 7) are plain
 * stores to this connection's non-paged block, at the line's DIRQL with
 * the interrupt object's lock held: no lock, no call, no IRQL change.
 * STRAY and SWALLOWED are noted when their counts reach a power of two
 * (record 08, section 3). */
static BOOLEAN __stdcall MsgIrqpTrampoline(PKINTERRUPT Interrupt, PVOID Context)
{
    MSGIRQP_TRAMP *t = (MSGIRQP_TRAMP *)Context;
    BOOLEAN mine;

    if (t->Quarantined) {
#if defined(MSGIRQP_HISTORY)
        MsgIrqpEvSwallowed(t, 1);
#endif
        return TRUE;
    }
    t->Entries++;
    mine = t->Routine(Interrupt, t->Context, 0);
    if (!mine) {
        t->Stray++;
#if defined(MSGIRQP_HISTORY)
        MsgIrqpEvStray(t);
#endif
    }
    return mine;
}

#if defined(MSI4NT_ISA_LINE)
/* The trampoline of a connection on a claimed line (record 05, sections
 * 2.7 and 2.8): counts for the brake, and once it has tripped (or after a
 * quarantine) returns at once without calling the client - TRUE on an
 * exclusive vector, FALSE on a shared one so that it never ends a chain.
 * The counting is MsgIrqpBrakeCount's (Part A). */
static BOOLEAN __stdcall MsgIrqpIsaTrampoline(PKINTERRUPT Interrupt, PVOID Context)
{
    MSGIRQP_TRAMP *t = (MSGIRQP_TRAMP *)Context;
    struct _MSGIRQP_ISA *i = t->Isa;
    BOOLEAN claimed;

    if (t->Quarantined || i->Brake.Braked) {
#if defined(MSGIRQP_HISTORY)
        MsgIrqpEvSwallowed(t, t->Quarantined ? 1UL : 2UL);
#endif
        return i->Shared ? FALSE : TRUE;
    }
    i->Brake.Entries++;
    t->Entries++;
    claimed = t->Routine(Interrupt, t->Context, 0);
    if (!claimed) {
        t->Stray++;
#if defined(MSGIRQP_HISTORY)
        MsgIrqpEvStray(t);
#endif
    }
    if (MsgIrqpBrakeCount(&i->Brake, claimed)) {
#if defined(MSGIRQP_HISTORY)
        MsgIrqpEv(MSGIRQ_EVENT_BRAKE, t->Location, i->Irq, i->Brake.Entries, i->Brake.Stray,
                  i->Brake.At, FALSE);
#endif
    }
    return claimed;
}
#endif

/* The counts of a live connection read together, and for a quarantine
 * read in the same synchronized step that stops the client's calls
 * (record 07, sections 4 and 7). */
typedef struct _MSGIRQP_COUNT_SYNC {
    MSGIRQP_TRAMP *Tramp;
    BOOLEAN Quarantine;
    ULONG Calls;
    ULONG NotMine;
} MSGIRQP_COUNT_SYNC;

static BOOLEAN __stdcall MsgIrqpCountSync(PVOID Context)
{
    MSGIRQP_COUNT_SYNC *s = (MSGIRQP_COUNT_SYNC *)Context;

    s->Calls = s->Tramp->Entries;
    s->NotMine = s->Tramp->Stray;
    if (s->Quarantine) {
        s->Tramp->Quarantined = 1;
    }
    return TRUE;
}

/* ------------------------- native forwarding ---------------------- */

#if !defined(MSI4NT_NO_NATIVE) && !defined(MSI4NT_LEGACY_NT)
/*
 * IoConnectInterruptEx's parameter block, CONNECT_MESSAGE_BASED member,
 * and its message table, restated from the routine's published
 * documentation (field names and order are the interface). The union is
 * padded to the size of the largest member (CONNECT_FULLY_SPECIFIED) so
 * the kernel never reads past the block.
 */
typedef struct _MSGIRQP_NATIVE_MESSAGE_BASED {
    PDEVICE_OBJECT PhysicalDeviceObject;
    PVOID *ConnectionContext;
    MSGIRQ_MESSAGE_SERVICE_ROUTINE MessageServiceRoutine;
    PVOID ServiceContext;
    PKSPIN_LOCK SpinLock;
    KIRQL SynchronizeIrql;
    BOOLEAN FloatingSave;
    MSGIRQ_LINE_SERVICE_ROUTINE FallBackServiceRoutine;
} MSGIRQP_NATIVE_MESSAGE_BASED;

typedef struct _MSGIRQP_NATIVE_PARAMS {
    ULONG Version;
    union {
        MSGIRQP_NATIVE_MESSAGE_BASED MessageBased;
        PVOID Pad[16];
    } u;
} MSGIRQP_NATIVE_PARAMS;

typedef struct _MSGIRQP_NATIVE_ENTRY {
    PHYSICAL_ADDRESS MessageAddress;
    KAFFINITY TargetProcessorSet;
    PKINTERRUPT InterruptObject;
    ULONG MessageData;
    ULONG Vector;
    KIRQL Irql;
    KINTERRUPT_MODE Mode;
    ULONG Polarity;
} MSGIRQP_NATIVE_ENTRY;

typedef struct _MSGIRQP_NATIVE_TABLE {
    KIRQL UnifiedIrql;
    ULONG MessageCount;
    MSGIRQP_NATIVE_ENTRY MessageInfo[1];
} MSGIRQP_NATIVE_TABLE;

typedef struct _MSGIRQP_NATIVE_DISCONNECT {
    ULONG Version;
    PVOID ConnectionContext;
} MSGIRQP_NATIVE_DISCONNECT;

typedef NTSTATUS (NTAPI *MSGIRQP_CONNECT_EX)(MSGIRQP_NATIVE_PARAMS *Params);
typedef VOID (NTAPI *MSGIRQP_DISCONNECT_EX)(MSGIRQP_NATIVE_DISCONNECT *Params);

#if defined(MSI4NT_X86)
MSI4NT_C_ASSERT(native_entry, sizeof(MSGIRQP_NATIVE_ENTRY) == 0x28);
MSI4NT_C_ASSERT(native_object, FIELD_OFFSET(MSGIRQP_NATIVE_ENTRY, InterruptObject) == 0x0C);
#else
MSI4NT_C_ASSERT(native_entry, sizeof(MSGIRQP_NATIVE_ENTRY) == 0x30);
MSI4NT_C_ASSERT(native_object, FIELD_OFFSET(MSGIRQP_NATIVE_ENTRY, InterruptObject) == 0x10);
#endif
MSI4NT_C_ASSERT(native_entries, FIELD_OFFSET(MSGIRQP_NATIVE_TABLE, MessageInfo) == 8);
#endif

/* --------------------------- the list ----------------------------- */

static MSGIRQP_CONN *MsgIrqpFind(PMSGIRQ_MESSAGE_INFO Info)
{
    MSGIRQP_CONN *c;

    for (c = MsgIrqpG.List; c != NULL; c = c->Next) {
        if (c->Info == Info && c->Magic == MSGIRQP_MAGIC) {
            return c;
        }
    }
    return NULL;
}

static BOOLEAN MsgIrqpListed(ULONG Bus, ULONG Dev, ULONG Func)
{
    MSGIRQP_CONN *c;

    for (c = MsgIrqpG.List; c != NULL; c = c->Next) {
        if (c->Route.Bus == Bus && c->Route.Dev == Dev && c->Route.Func == Func) {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID MsgIrqpUnlist(MSGIRQP_CONN *Conn)
{
    MSGIRQP_CONN **pp;

    for (pp = &MsgIrqpG.List; *pp != NULL; pp = &(*pp)->Next) {
        if (*pp == Conn) {
            *pp = Conn->Next;
            return;
        }
    }
}

/* One block: the hidden header, then the public MSGIRQ_MESSAGE_INFO with
 * Count entries. */
#if defined(MSI4NT_ISA_LINE)
static MSGIRQP_ISA_MEMBER *MsgIrqpIsaMember(ULONG Bus, ULONG Dev, ULONG Func)
{
    MSGIRQP_ISA_MEMBER *m;

    for (m = MsgIrqpG.IsaMembers; m != NULL; m = m->Next) {
        if (m->Bus == Bus && m->Dev == Dev && m->Func == Func) {
            return m;
        }
    }
    return NULL;
}

static MSGIRQP_ISA *MsgIrqpIsaFind(ULONG Bus, ULONG Dev, ULONG Func)
{
    MSGIRQP_ISA_MEMBER *m = MsgIrqpIsaMember(Bus, Dev, Func);

    return (m != NULL) ? m->Claim : NULL;
}

static BOOLEAN MsgIrqpLineSound(ULONG Vector, KIRQL Irql);

/* Record 05, section 2.4: the claim's interrupt translated again at a
 * connect, under the interface it was claimed under (an input above 23
 * under PCIBus). An interrupt that has its vector is given no other and
 * costs nothing. TRUE only when the HAL still returns the claim's own
 * vector and IRQL, the pair agrees, and the processor the connection
 * targets (the lowest of the claim's affinity) is still among those it
 * gives: once another driver has run the HAL's vectors over, the IRQL it
 * returns changes and the line is not connected. The affinity is not
 * compared whole, so bits for processors that are not there do not
 * matter. */
static BOOLEAN MsgIrqpIsaLineStill(const MSGIRQP_ISA *I)
{
    KIRQL irql = 0;
    KAFFINITY affinity = 0;
    ULONG vector;
    ULONG cpu;

    vector = MsgIrqpG.IsaVector((I->Irq < 16) ? Isa : PCIBus, 0, I->Irq, I->Irq, &irql, &affinity);
    MSI4NT_TRACE(("msi4nt: isa line again: irq %lu vector %lx irql %lu affinity %lx (claimed %lx %lu %lx)\n",
                  I->Irq, vector, (ULONG)irql, (ULONG)affinity, I->Line.Vector, (ULONG)I->Line.Irql,
                  (ULONG)I->Line.Affinity));
    return (BOOLEAN)(vector == I->Line.Vector && irql == I->Line.Irql &&
                     MsgIrqpLowestCpu(I->Line.Affinity, &cpu) &&
                     (affinity & (((KAFFINITY)1) << cpu)) != 0 && MsgIrqpLineSound(vector, irql));
}

#if defined(MSI4NT_X86)
/* A shared claim's IDT gate leads into its first connected member's
 * object (record 05, section 2.8). */
static BOOLEAN MsgIrqpIsaGateLeadsToMember(MSGIRQP_ISA *I, ULONG Vector, ULONG Cpu)
{
    MSGIRQP_CONN *c;

    for (c = MsgIrqpG.List; c != NULL; c = c->Next) {
        if (c->Isa == I && c->Interrupt != NULL && MsgIrqpIdtConfirms(Vector, c->Interrupt, Cpu)) {
            return TRUE;
        }
    }
    return FALSE;
}
#endif
#endif

static MSGIRQP_CONN *MsgIrqpNewConn(ULONG Count)
{
    ULONG head;
    ULONG bytes;
    MSGIRQP_CONN *c;

    if (Count == 0 || Count > 2048) {
        return NULL;
    }
    head = (sizeof(MSGIRQP_CONN) + 15UL) & ~15UL;
    bytes = head + sizeof(MSGIRQ_MESSAGE_INFO) +
            (Count - 1) * sizeof(MSGIRQ_MESSAGE_INFO_ENTRY);
    c = (MSGIRQP_CONN *)MsgIrqpAlloc(bytes);
    if (c == NULL) {
        return NULL;
    }
    c->Magic = MSGIRQP_MAGIC;
    c->Info = (PMSGIRQ_MESSAGE_INFO)((PUCHAR)c + head);
    return c;
}

/* ------------------------- MSI-X mapping -------------------------- */

/* Maps the MSI-X table (design record 04, section 3). Returns a status. */
static ULONG MsgIrqpMapTable(const MSGIRQP_IO *Io, MSGIRQP_CONN *C, ULONG Cap,
                             USHORT Control)
{
    ULONG tbl;
    ULONG bir;
    ULONG bar;
    ULONG barHi;
    PHYSICAL_ADDRESS pa;
    ULONG entries;

    if (!MsgIrqpRead32(Io, Cap + 4UL, &tbl)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    bir = tbl & 7UL;
    if (bir > 5) {
        return MSGIRQ_STATUS_NO_MSI_CAP;
    }
    if (!MsgIrqpRead32(Io, MSGIRQP_CFG_BAR0 + bir * 4UL, &bar)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (bar & 1UL) {
        return MSGIRQ_STATUS_NO_MSI_CAP;        /* an I/O BAR */
    }
    barHi = 0;
    if ((bar & 6UL) == 4UL) {                   /* 64-bit */
        if (bir == 5 || !MsgIrqpRead32(Io, MSGIRQP_CFG_BAR0 + (bir + 1) * 4UL, &barHi)) {
            return (bir == 5) ? MSGIRQ_STATUS_NO_MSI_CAP : MSGIRQ_STATUS_CONFIG_ACCESS;
        }
    }
    pa.HighPart = (LONG)barHi;
    pa.LowPart = (bar & ~0xFUL) + (tbl & ~7UL);
    if ((bar & ~0xFUL) == 0 && barHi == 0) {
        return MSGIRQ_STATUS_NO_MSI_CAP;
    }
    entries = (Control & MSGIRQP_MSIX_SIZE_MASK) + 1UL;
    C->TableBytes = entries * MSGIRQP_MSIX_ENTRY_BYTES;
    C->TableVa = MmMapIoSpace(pa, C->TableBytes, MmNonCached);
    if (C->TableVa == NULL) {
#if defined(MSGIRQP_CALLS)
        MsgIrqpG.PubDetailKind = MSGIRQP_DETAIL_MAP;
        MsgIrqpG.PubDetail = pa.LowPart;
#endif
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    C->Target.Table = (volatile ULONG *)C->TableVa;
    C->Target.TableEntries = entries;
    return MSGIRQ_STATUS_OK;
}

static VOID MsgIrqpUnmapTable(MSGIRQP_CONN *C)
{
    if (C->TableVa != NULL) {
        MmUnmapIoSpace(C->TableVa, C->TableBytes);
        C->TableVa = NULL;
        C->Target.Table = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The API                                                             */
/* ------------------------------------------------------------------ */

#if defined(MSI4NT_HOSTED)
static PVOID MsgIrqpResolve(PCWSTR Name)
{
    return MsgIrqpG.Host.Resolve(MsgIrqpG.Host.HostContext, Name);
}
#elif !defined(MSI4NT_LEGACY_NT)
static PVOID MsgIrqpResolve(PCWSTR Name)
{
    UNICODE_STRING s;

    if (MsgIrqpG.HaveHost && MsgIrqpG.Host.Resolve != NULL) {
        return MsgIrqpG.Host.Resolve(MsgIrqpG.Host.HostContext, Name);
    }
    RtlInitUnicodeString(&s, Name);
    return MmGetSystemRoutineAddress(&s);
}
#endif

#if defined(MSI4NT_LEGACY_NT)
/* The legacy build imports the pair directly (roadmap Q9 checks NT 3.5x);
 * not every DDK declares them, so they are declared here. */
VOID NTAPI KeSetSystemAffinityThread(KAFFINITY Affinity);
VOID NTAPI KeRevertToUserAffinityThread(VOID);

static VOID NTAPI MsgIrqpSetAffinityDirect(KAFFINITY Affinity)
{
    KeSetSystemAffinityThread(Affinity);
}

static VOID NTAPI MsgIrqpRevertAffinityDirect(VOID)
{
    KeRevertToUserAffinityThread();
}
#endif

#if defined(MSGIRQP_PUBLISH)
/* The published state's start (record 07): its lock, its table, the
 * copy's load time. Nothing is published until the routines are known. */
static VOID MsgIrqpPubInit(VOID)
{
    LARGE_INTEGER t;
#if defined(MSI4NT_HOSTED) && defined(MSI4NT_X86)
    MSGIRQP_PUB_TIME query;
#endif

    MsgIrqpPubTableInit(MsgIrqpG.Pub, MSGIRQP_PUB_SLOTS);
    MsgIrqpG.PubOn = FALSE;
    MsgIrqpG.PubGeneration = 0;
    MsgIrqpG.PubStamp = 0;
    MsgIrqpG.PubLost = 0;
    t.QuadPart = 0;
#if defined(MSI4NT_HOSTED) && defined(MSI4NT_X86)
    /* A hosted image imports nothing it did not ask for; amd64 reads the
     * time without a call. */
    query = (MSGIRQP_PUB_TIME)MsgIrqpResolve(L"KeQuerySystemTime");
    if (query != NULL) {
        query(&t);
    }
#else
    KeQuerySystemTime(&t);
#endif
    MsgIrqpG.PubLoadLow = t.LowPart;
    MsgIrqpG.PubLoadHigh = (ULONG)t.HighPart;
}

/* The registry routines: imported in a default build, resolved (and
 * optional: without one nothing is published) in a hosted one. */
static VOID MsgIrqpPubRoutines(VOID)
{
#if defined(MSI4NT_HOSTED)
    MsgIrqpG.PubCreate = (MSGIRQP_PUB_CREATE)MsgIrqpResolve(L"ZwCreateKey");
    MsgIrqpG.PubSet = (MSGIRQP_PUB_SET)MsgIrqpResolve(L"ZwSetValueKey");
    MsgIrqpG.PubClose = (MSGIRQP_PUB_CLOSE)MsgIrqpResolve(L"ZwClose");
#else
    MsgIrqpG.PubCreate = ZwCreateKey;
    MsgIrqpG.PubSet = ZwSetValueKey;
    MsgIrqpG.PubClose = ZwClose;
#endif
    MsgIrqpG.PubOn = (BOOLEAN)(MsgIrqpG.PubCreate != NULL && MsgIrqpG.PubSet != NULL &&
                               MsgIrqpG.PubClose != NULL);
}
#endif

/* Which system: 0 = 9x, 5 = NT before Vista, 6 = Vista or later. */
static ULONG MsgIrqpSystem(VOID)
{
#if defined(MSI4NT_LEGACY_NT)
    ULONG major;

    major = 0;
    (VOID)PsGetVersion(&major, NULL, NULL, NULL);
    return (major >= 6) ? 6UL : 5UL;
#else
    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        return 0;
    }
    return IoIsWdmVersionAvailable(6, 0) ? 6UL : 5UL;
#endif
}

static ULONG MsgIrqpInitializeCall(const MSGIRQ_HOST *Host);

/* The copy's start, then (record 08) its INIT event and the history's
 * first copy into the registry. */
ULONG MsgIrqInitialize(const MSGIRQ_HOST *Host)
{
    ULONG status;

    if (MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_OK;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    status = MsgIrqpInitializeCall(Host);
#if defined(MSGIRQP_HISTORY)
    if (MsgIrqpG.Initialized) {
        MsgIrqpPubBegin();
        MsgIrqpEv(MSGIRQ_EVENT_INIT, MSGIRQP_EV_COPY, MsgIrqpG.Mode, MsgIrqpG.PubInitReason,
                  MSI4NT_VERSION, MSGIRQP_BUILD_BITS, FALSE);
        MsgIrqpPubEnd();
    }
#endif
    return status;
}

static ULONG MsgIrqpInitializeCall(const MSGIRQ_HOST *Host)
{
    ULONG system;
    PHYSICAL_ADDRESS base;

    MsgIrqpG.Mode = MSGIRQ_MODE_NONE;
    MsgIrqpG.NoneStatus = MSGIRQ_STATUS_UNSUPPORTED;
    MsgIrqpG.List = NULL;
#if !defined(_MSC_VER)
    if (MsgIrqpSignatureRef == NULL) {      /* a volatile read: keeps the string in the image */
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
#endif
#if defined(MSI4NT_ISA_LINE)
    MsgIrqpG.IsaList = NULL;
    MsgIrqpG.IsaMembers = NULL;
    {
        ULONG k;

        MsgIrqpG.HighSpent.Count = 0;
        for (k = 0; k < 8; k++) {
            MsgIrqpG.HighSpent.Inputs[k] = 0;
        }
    }
#endif
    MsgIrqpG.HaveHost = FALSE;
    if (Host != NULL) {
        if (Host->Size < sizeof(MSGIRQ_HOST) ||
            (Host->Allocate == NULL) != (Host->Free == NULL)) {
            return MSGIRQ_STATUS_INVALID_PARAMETER;
        }
        MsgIrqpG.Host = *Host;
        MsgIrqpG.HaveHost = TRUE;
    }
#if defined(MSI4NT_HOSTED)
    if (!MsgIrqpG.HaveHost || MsgIrqpG.Host.Resolve == NULL ||
        MsgIrqpG.Host.Allocate == NULL) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
#endif
    KeInitializeEvent(&MsgIrqpG.Lock, SynchronizationEvent, TRUE);
#if defined(MSGIRQP_CALLS)
    KeInitializeEvent(&MsgIrqpG.PubLock, SynchronizationEvent, TRUE);
    MsgIrqpG.PubInitReason = 0;
#endif
#if defined(MSGIRQP_PUBLISH)
    MsgIrqpPubInit();
#endif
#if defined(MSGIRQP_HISTORY) && defined(MSI4NT_HOSTED) && defined(MSI4NT_X86)
    MsgIrqpG.HistTime = (MSGIRQP_HIST_TIME)MsgIrqpResolve(L"KeQueryInterruptTime");
#endif
    MsgIrqpG.Initialized = TRUE;

    system = MsgIrqpSystem();
#if defined(MSGIRQP_PUBLISH)
    if (system != 0) {
        MsgIrqpPubRoutines();                           /* 98 / ME: nothing is published */
    }
#endif
    MSI4NT_TRACE(("msi4nt: version %u system %lu\n", MSI4NT_VERSION, system));
    if (system == 0) {
        return MSGIRQ_STATUS_OK;                        /* 98 / ME */
    }
    if (system == 6) {
#if !defined(MSI4NT_NO_NATIVE) && !defined(MSI4NT_LEGACY_NT)
        MsgIrqpG.ConnectEx = MsgIrqpResolve(L"IoConnectInterruptEx");
        MsgIrqpG.DisconnectEx = MsgIrqpResolve(L"IoDisconnectInterruptEx");
        if (MsgIrqpG.ConnectEx != NULL && MsgIrqpG.DisconnectEx != NULL) {
            MsgIrqpG.Mode = MSGIRQ_MODE_NATIVE;
        }
#endif
        return MSGIRQ_STATUS_OK;
    }

    /* 2000, XP, 2003: the fake-IRQ path, if the HAL is in APIC mode. */
#if !defined(MSI4NT_LEGACY_NT)
    MsgIrqpG.SetAffinity = (MSGIRQP_SET_AFFINITY)MsgIrqpResolve(L"KeSetSystemAffinityThread");
    MsgIrqpG.RevertAffinity =
        (MSGIRQP_REVERT_AFFINITY)MsgIrqpResolve(L"KeRevertToUserAffinityThread");
    if (MsgIrqpG.SetAffinity == NULL || MsgIrqpG.RevertAffinity == NULL) {
        MSGIRQP_INIT_REASON(MSGIRQP_HAL_UNKNOWN);
        return MSGIRQ_STATUS_OK;                        /* UNSUPPORTED */
    }
#else
    MsgIrqpG.SetAffinity = MsgIrqpSetAffinityDirect;
    MsgIrqpG.RevertAffinity = MsgIrqpRevertAffinityDirect;
#endif
    MsgIrqpG.NoneStatus = MSGIRQ_STATUS_HAL_UNSUPPORTED;
#if defined(MSI4NT_ISA_LINE)
    /* Record 05, section 2.6: resolved, never imported, so the build's
     * import set does not change; a missing one makes MsgIrqIsaClaim
     * return MSGIRQ_STATUS_UNSUPPORTED. */
    MsgIrqpG.IsaReport = (MSGIRQP_REPORT_DETECTION)MsgIrqpResolve(L"IoReportResourceForDetection");
    MsgIrqpG.IsaCreate = (MSGIRQP_CREATE_DEVICE)MsgIrqpResolve(L"IoCreateDevice");
    MsgIrqpG.IsaDelete = (MSGIRQP_DELETE_DEVICE)MsgIrqpResolve(L"IoDeleteDevice");
    MsgIrqpG.IsaVector = (MSGIRQP_GET_VECTOR)MsgIrqpResolve(L"HalGetInterruptVector");
    MsgIrqpG.IsaBusData = (MSGIRQP_GET_BUS_DATA)MsgIrqpResolve(L"HalGetBusDataByOffset");
    MsgIrqpG.IsaOpenKey = (MSGIRQP_OPEN_KEY)MsgIrqpResolve(L"ZwOpenKey");
    MsgIrqpG.IsaEnumKey = (MSGIRQP_ENUM_KEY)MsgIrqpResolve(L"ZwEnumerateKey");
    MsgIrqpG.IsaQueryValue = (MSGIRQP_QUERY_VALUE)MsgIrqpResolve(L"ZwQueryValueKey");
    MsgIrqpG.IsaClose = (MSGIRQP_CLOSE)MsgIrqpResolve(L"ZwClose");
    MsgIrqpG.IsaProcess = (MSGIRQP_CURRENT_PROCESS)MsgIrqpResolve(L"IoGetCurrentProcess");
    MsgIrqpG.IsaSystemProcess = (PEPROCESS *)MsgIrqpResolve(L"PsInitialSystemProcess");
    /* Only a claim that chooses the upper pool needs these (its record of
     * translations, section 2.9); without them such a claim is refused. */
    MsgIrqpG.IsaCreateKey = (MSGIRQP_CREATE_KEY)MsgIrqpResolve(L"ZwCreateKey");
    MsgIrqpG.IsaSetValue = (MSGIRQP_SET_VALUE)MsgIrqpResolve(L"ZwSetValueKey");
#endif
    if (!MsgIrqpApicBase(&base)) {
        return MSGIRQ_STATUS_OK;
    }
    MsgIrqpG.ApicVa = MmMapIoSpace(base, 0x1000, MmNonCached);
    if (MsgIrqpG.ApicVa == NULL) {
        MSGIRQP_INIT_REASON(MSGIRQP_HAL_UNKNOWN);
        return MSGIRQ_STATUS_OK;
    }
    {
        ULONG ver = MsgIrqpApicRead(MSGIRQP_APIC_VERSION);
        ULONG svr = MsgIrqpApicRead(MSGIRQP_APIC_SVR);

        MSI4NT_TRACE(("msi4nt: lapic version %08lx svr %08lx\n", ver, svr));
        if (ver == 0 || ver == 0xFFFFFFFFUL || (svr & 0x100UL) == 0) {
            MSGIRQP_INIT_REASON((ver == 0 || ver == 0xFFFFFFFFUL) ? MSGIRQP_HAL_UNKNOWN : MSGIRQP_HAL_PIC);
            MmUnmapIoSpace(MsgIrqpG.ApicVa, 0x1000);
            MsgIrqpG.ApicVa = NULL;
            return MSGIRQ_STATUS_OK;
        }
    }
    MsgIrqpG.Mode = MSGIRQ_MODE_FAKE_IRQ;
    return MSGIRQ_STATUS_OK;
}

ULONG MsgIrqGetMode(VOID)
{
    return MsgIrqpG.Initialized ? MsgIrqpG.Mode : MSGIRQ_MODE_NONE;
}

ULONG MsgIrqQuery(const MSGIRQ_CONNECT_PARAMS *Params, MSGIRQ_CAPABILITIES *Caps)
{
    MSGIRQP_ROUTE route;
    MSGIRQP_IO io;
    MSGIRQP_CAPS caps;
    ULONG status;

    if (!MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
    if (Params == NULL || Caps == NULL || !MsgIrqpRouteFrom(Params, &route) ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) {
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_QUERY, MSGIRQ_STATUS_INVALID_PARAMETER);
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    MsgIrqpIoFor(&route, &io);
    io.Read = MsgIrqpIoReadQuiet;
    io.Write = MsgIrqpIoWriteQuiet;
    status = MsgIrqpWalk(&io, &caps);
    if (status == MSGIRQ_STATUS_OK && caps.Vendor == 0xFFFF) {
        status = MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (status == MSGIRQ_STATUS_OK) {
        MsgIrqpCapsToPublic(&caps, Caps);
    }
#if defined(MSGIRQP_HISTORY)
    else {
        MsgIrqpEvRefused(MsgIrqpEvLocation(route.Bus, route.Dev, route.Func), MSGIRQ_CALL_QUERY, status,
                         0, 0);
    }
#endif
    MSGIRQP_HIST_CALM();                           /* a calm point that never waits */
    return status;
}

#if !defined(MSI4NT_NO_NATIVE) && !defined(MSI4NT_LEGACY_NT)
static ULONG MsgIrqpConnectNative(const MSGIRQ_CONNECT_PARAMS *P, PMSGIRQ_MESSAGE_INFO *Info)
{
    MSGIRQP_NATIVE_PARAMS np;
    MSGIRQP_NATIVE_TABLE *table;
    MSGIRQP_NATIVE_DISCONNECT dp;
    MSGIRQP_CONN *c;
    PVOID context;
    NTSTATUS status;
    ULONG i;
    PUCHAR b;

    if (P->MessageBased.PhysicalDeviceObject == NULL ||
        (P->MessageBased.Flags & MSGIRQ_CONNECT_FLAG_ATTACH)) {
        return MSGIRQ_STATUS_UNSUPPORTED;
    }
    if (P->MessageBased.Bus > 255 || P->MessageBased.Dev > 31 || P->MessageBased.Func > 7) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    if (MsgIrqpListed(P->MessageBased.Bus, P->MessageBased.Dev, P->MessageBased.Func)) {
        return MSGIRQ_STATUS_IN_USE;                    /* one connection per function */
    }
    b = (PUCHAR)&np;
    for (i = 0; i < sizeof(np); i++) {
        b[i] = 0;
    }
    context = NULL;
    np.Version = MSGIRQ_CONNECT_MESSAGE_BASED;
    np.u.MessageBased.PhysicalDeviceObject = P->MessageBased.PhysicalDeviceObject;
    np.u.MessageBased.ConnectionContext = &context;
    np.u.MessageBased.MessageServiceRoutine = P->MessageBased.MessageServiceRoutine;
    np.u.MessageBased.ServiceContext = P->MessageBased.ServiceContext;
    np.u.MessageBased.SpinLock = P->MessageBased.SpinLock;
    np.u.MessageBased.SynchronizeIrql = P->MessageBased.SynchronizeIrql;
    np.u.MessageBased.FloatingSave = P->MessageBased.FloatingSave;
    np.u.MessageBased.FallBackServiceRoutine = NULL;
    status = ((MSGIRQP_CONNECT_EX)MsgIrqpG.ConnectEx)(&np);
    if (!NT_SUCCESS(status) || context == NULL) {
        return MSGIRQ_STATUS_NO_MSI_CAP;
    }
    if (np.Version != MSGIRQ_CONNECT_MESSAGE_BASED) {
        dp.Version = np.Version;
        dp.ConnectionContext = context;
        ((MSGIRQP_DISCONNECT_EX)MsgIrqpG.DisconnectEx)(&dp);
        return MSGIRQ_STATUS_NO_MSI_CAP;
    }
    table = (MSGIRQP_NATIVE_TABLE *)context;
    c = MsgIrqpNewConn(table->MessageCount);
    if (c == NULL) {
        dp.Version = MSGIRQ_CONNECT_MESSAGE_BASED;
        dp.ConnectionContext = context;
        ((MSGIRQP_DISCONNECT_EX)MsgIrqpG.DisconnectEx)(&dp);
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    c->Path = MSGIRQP_PATH_NATIVE;
    c->NativeContext = context;
    c->NativeVersion = MSGIRQ_CONNECT_MESSAGE_BASED;
    c->Route.Bus = P->MessageBased.Bus;
    c->Route.Dev = P->MessageBased.Dev;
    c->Route.Func = P->MessageBased.Func;
    c->Info->UnifiedIrql = table->UnifiedIrql;
    c->Info->MessageCount = table->MessageCount;
    for (i = 0; i < table->MessageCount; i++) {
        c->Info->MessageInfo[i].Vector = table->MessageInfo[i].Vector;
        c->Info->MessageInfo[i].Affinity = table->MessageInfo[i].TargetProcessorSet;
        c->Info->MessageInfo[i].Irql = table->MessageInfo[i].Irql;
        c->Info->MessageInfo[i].MessageAddress = table->MessageInfo[i].MessageAddress.LowPart;
        c->Info->MessageInfo[i].MessageData = table->MessageInfo[i].MessageData;
        c->Info->MessageInfo[i].InterruptObject = table->MessageInfo[i].InterruptObject;
    }
    c->Next = MsgIrqpG.List;
    MsgIrqpG.List = c;
    *Info = c->Info;
    return MSGIRQ_STATUS_OK;
}
#endif

/* Ends a fake-IRQ connection whose device writes are done (or skipped):
 * drain, disconnect, unmap, free. Counts (optional) receives the
 * trampoline's counts once dispatch has stopped (record 07, section 4).
 * Lock held. */
static VOID MsgIrqpRelease(MSGIRQP_CONN *C, MSGIRQP_COUNT_SYNC *Counts)
{
    if (C->Interrupt != NULL) {
        MsgIrqpDrain(C->Cpu);
        IoDisconnectInterrupt(C->Interrupt);
        C->Interrupt = NULL;
    }
    MsgIrqpUnmapTable(C);
    MsgIrqpUnlist(C);
    if (C->Tramp != NULL) {
        if (Counts != NULL) {
            Counts->Calls = C->Tramp->Entries;
            Counts->NotMine = C->Tramp->Stray;
        }
        MsgIrqpFree(C->Tramp);
    }
    C->Magic = 0;
    MsgIrqpFree(C);
}

static ULONG MsgIrqpConnectFake(const MSGIRQ_CONNECT_PARAMS *P, PMSGIRQ_MESSAGE_INFO *Info)
{
    const MSGIRQ_LINE_RESOURCE *line = &P->MessageBased.LineInterrupt;
#if defined(MSI4NT_ISA_LINE)
    MSGIRQP_ISA *isa = NULL;
#endif
    MSGIRQP_ROUTE route;
    MSGIRQP_IO io;
    MSGIRQP_CAPS caps;
    MSGIRQP_CONN *c;
    MSGIRQP_TARGET target;
    ULONG type;
    ULONG cpu;
    ULONG apicId;
    ULONG status;
    ULONG idtVector;
    BOOLEAN set;
    BOOLEAN share = FALSE;
    PKSERVICE_ROUTINE routine = (PKSERVICE_ROUTINE)MsgIrqpTrampoline;
    KIRQL sync;
    NTSTATUS ns;

    if (P->MessageBased.Flags & MSGIRQ_CONNECT_FLAG_ATTACH) {
        return MSGIRQ_STATUS_UNSUPPORTED;               /* roadmap Q12 */
    }
#if defined(MSI4NT_ISA_LINE)
    if (P->MessageBased.Flags & MSGIRQ_CONNECT_FLAG_ISA_LINE) {
        /* Record 05, section 2.4: the line is the function's own claim. */
        isa = MsgIrqpIsaFind(P->MessageBased.Bus, P->MessageBased.Dev, P->MessageBased.Func);
        if (isa == NULL) {
            MSI4NT_TRACE(("msi4nt: no ISA claim\n"));
            return MSGIRQ_STATUS_NO_VECTORS;
        }
        if (isa->State == MSGIRQP_ISA_KEPT) {
            return MSGIRQ_STATUS_CLAIM_KEPT;
        }
        if (isa->Brake.Braked) {
            return MSGIRQ_STATUS_BRAKED;
        }
        if (!MsgIrqpIsaLineStill(isa)) {
            MSI4NT_TRACE(("msi4nt: the claim's line is no longer what the HAL gives\n"));
#if defined(MSGIRQP_CALLS)
            MsgIrqpG.PubDetailKind = MSGIRQP_DETAIL_HAL;
            MsgIrqpG.PubDetail = MSGIRQP_HAL_CHANGED;
#endif
            return MSGIRQ_STATUS_HAL_UNSUPPORTED;
        }
#if defined(MSGIRQP_HISTORY)
        MsgIrqpG.HistChecks |= 0x2UL;                   /* the line translated again, agreed */
#endif
        line = &isa->Line;
    }
#endif
    if (line->Vector == 0) {
        MSI4NT_TRACE(("msi4nt: no line\n"));
        return MSGIRQ_STATUS_NO_VECTORS;
    }
    idtVector = MsgIrqpIdtVector(line->Vector);
    /* A claimed ISA line (record 05) is the library's own, exclusive or
     * shared by its claim's members; the share rule is for a client's line. */
    if (
#if defined(MSI4NT_ISA_LINE)
        isa == NULL &&
#endif
        !MsgIrqpPinAccepted(line->ShareDisposition, P->MessageBased.Flags)) {
        MSI4NT_TRACE(("msi4nt: share disposition %lu refused\n", (ULONG)line->ShareDisposition));
        return MSGIRQ_STATUS_NO_VECTORS;
    }
    if (!MsgIrqpLowestCpu(line->Affinity, &cpu)) {
        MSI4NT_TRACE(("msi4nt: empty affinity\n"));
        return MSGIRQ_STATUS_NO_VECTORS;
    }
    if (!MsgIrqpRouteFrom(P, &route)) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    if (MsgIrqpListed(route.Bus, route.Dev, route.Func)) {
        return MSGIRQ_STATUS_IN_USE;
    }
    MsgIrqpIoFor(&route, &io);
    status = MsgIrqpWalk(&io, &caps);
    if (status != MSGIRQ_STATUS_OK) {
        return status;
    }
    if (caps.Vendor == 0xFFFF) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    type = MsgIrqpChoose(&caps, P->MessageBased.MessageType);
    if (type == 0) {
        return MSGIRQ_STATUS_NO_MSI_CAP;
    }
    if (!MsgIrqpEnabled(&io, &caps, &set)) {
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    if (set) {
        return MSGIRQ_STATUS_IN_USE;
    }
    if (!MsgIrqpVectorIsApic(idtVector) || line->Irql <= DISPATCH_LEVEL) {
        MSI4NT_TRACE(("msi4nt: vector %lx irql %u is not an APIC-mode line\n",
                      line->Vector, (ULONG)line->Irql));
#if defined(MSGIRQP_CALLS)
        MsgIrqpG.PubDetailKind = MSGIRQP_DETAIL_HAL;
        MsgIrqpG.PubDetail = MSGIRQP_HAL_VECTOR;
#endif
        return MSGIRQ_STATUS_HAL_UNSUPPORTED;
    }
#if defined(MSGIRQP_HISTORY)
    MsgIrqpG.HistChecks |= 0x4UL;                       /* an APIC-mode vector and IRQL */
#endif

    /* Everything allocated and mapped before the first device write. */
    c = MsgIrqpNewConn(1);
    if (c == NULL) {
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    c->Tramp = (MSGIRQP_TRAMP *)MsgIrqpAlloc(sizeof(MSGIRQP_TRAMP));
    if (c->Tramp == NULL) {
        MsgIrqpFree(c);
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    c->Path = MSGIRQP_PATH_FAKE;
    c->Route = route;
    c->Cpu = cpu;
    c->Vector = idtVector;
    c->Irql = line->Irql;
    c->LineAffinity = line->Affinity;
    c->Tramp->Quarantined = 0;
    c->Tramp->Routine = P->MessageBased.MessageServiceRoutine;
    c->Tramp->Context = P->MessageBased.ServiceContext;
#if defined(MSGIRQP_HISTORY)
    c->Tramp->Location = MsgIrqpEvLocation(route.Bus, route.Dev, route.Func);
    c->Tramp->IdtVector = idtVector;
#endif
#if defined(MSI4NT_ISA_LINE)
    c->Isa = isa;
    c->Tramp->Isa = isa;
    if (isa != NULL) {
        routine = (PKSERVICE_ROUTINE)MsgIrqpIsaTrampoline;
        share = isa->Shared;
    }
#endif
    target.Type = type;
    target.Cap = (type == MSGIRQ_MESSAGE_MSIX) ? caps.MsixOffset : caps.MsiOffset;
    target.Control = (type == MSGIRQ_MESSAGE_MSIX) ? caps.MsixControl : caps.MsiControl;
    target.IntdCapable = FALSE;
    target.Table = NULL;
    target.TableEntries = 0;
    target.PmCap = caps.PmOffset;
    c->Target = target;
    /* The route copy inside c is what the device I/O uses from here on. */
    MsgIrqpIoFor(&c->Route, &io);
    if (type == MSGIRQ_MESSAGE_MSIX) {
        if (!MsgIrqpInD0(&io, target.PmCap)) {
            /* Record 04, section 7: out of D0 (or the state unreadable),
             * before the decoding and BAR checks, any write and anything
             * connected; not NO_MSI_CAP, which would send the client to
             * INTx. */
            MSI4NT_TRACE(("msi4nt: MSI-X function not in D0\n"));
            status = MSGIRQ_STATUS_CONFIG_ACCESS;
        } else {
            status = ((caps.Command & MSGIRQP_CMD_MSE) == 0)
                         ? MSGIRQ_STATUS_NO_MSI_CAP
                         : MsgIrqpMapTable(&io, c, target.Cap, target.Control);
        }
        if (status != MSGIRQ_STATUS_OK) {
            MsgIrqpFree(c->Tramp);
            MsgIrqpFree(c);
            return status;
        }
    }
    apicId = MsgIrqpApicIdOn(cpu);

    /* Step 4: the exclusive connect. */
    sync = P->MessageBased.SynchronizeIrql;
    if (sync < line->Irql) {
        sync = line->Irql;
    }
    ns = IoConnectInterrupt(&c->Interrupt, routine, c->Tramp,
                            P->MessageBased.SpinLock, line->Vector, line->Irql, sync,
                            line->Mode, share, line->Affinity, FALSE);
    if (!NT_SUCCESS(ns)) {
        MSI4NT_TRACE(("msi4nt: IoConnectInterrupt %08lx\n", (ULONG)ns));
#if defined(MSGIRQP_CALLS)
        MsgIrqpG.PubDetailKind = MSGIRQP_DETAIL_NTSTATUS;
        MsgIrqpG.PubDetail = (ULONG)ns;
#endif
        c->Interrupt = NULL;
        status = MSGIRQ_STATUS_NO_VECTORS;
        if (!MsgIrqpEnabled(&io, &caps, &set)) {
            status = MSGIRQ_STATUS_CONFIG_ACCESS;
        } else if (set) {
            status = MSGIRQ_STATUS_IN_USE;
        }
        MsgIrqpUnmapTable(c);
        MsgIrqpFree(c->Tramp);
        MsgIrqpFree(c);
        return status;
    }

#if defined(MSI4NT_X86)
    if (!MsgIrqpIdtConfirms(idtVector, c->Interrupt, cpu)
#if defined(MSI4NT_ISA_LINE)
        && !(isa != NULL && isa->Shared && MsgIrqpIsaGateLeadsToMember(isa, idtVector, cpu))
#endif
        ) {
        MSI4NT_TRACE(("msi4nt: IDT gate %02lx does not lead to the line's object\n", idtVector));
#if defined(MSGIRQP_CALLS)
        MsgIrqpG.PubDetailKind = MSGIRQP_DETAIL_HAL;
        MsgIrqpG.PubDetail = MSGIRQP_HAL_IDT;
#endif
        IoDisconnectInterrupt(c->Interrupt);
        c->Interrupt = NULL;
        MsgIrqpUnmapTable(c);
        MsgIrqpFree(c->Tramp);
        MsgIrqpFree(c);
        return MSGIRQ_STATUS_HAL_UNSUPPORTED;
    }
#if defined(MSGIRQP_HISTORY)
    MsgIrqpG.HistChecks |= 0x1UL;                       /* the IDT gate confirmed */
#endif
#endif

    /* Step 5: program and enable (design record 04). */
    if (!MsgIrqpProbeIntd(&io, &c->Target.IntdCapable)) {
        status = MSGIRQ_STATUS_CONFIG_ACCESS;
    } else {
        status = MsgIrqpArm(&io, &c->Target, MsgIrqpAddress(apicId), MsgIrqpData(idtVector));
        if (status == MSGIRQP_STATUS_UNREACHED) {
            status = MSGIRQ_STATUS_CONFIG_ACCESS;       /* left D0 since the check: rolled back below */
        }
    }
    c->Info->UnifiedIrql = sync;
    c->Info->MessageCount = 1;
    c->Info->MessageInfo[0].Vector = line->Vector;
    c->Info->MessageInfo[0].Affinity = ((KAFFINITY)1) << cpu;
    c->Info->MessageInfo[0].Irql = line->Irql;
    c->Info->MessageInfo[0].MessageAddress = MsgIrqpAddress(apicId);
    c->Info->MessageInfo[0].MessageData = MsgIrqpData(idtVector);
    c->Info->MessageInfo[0].InterruptObject = c->Interrupt;
    c->Next = MsgIrqpG.List;
    MsgIrqpG.List = c;
    if (status != MSGIRQ_STATUS_OK) {
        /* Roll back: a confirmed disable ends the connection; otherwise it
         * stays live and is returned with the status (record 01, section 2). */
        if (MsgIrqpDisarm(&io, &c->Target)) {
            MsgIrqpRelease(c, NULL);
            return MSGIRQ_STATUS_CONFIG_ACCESS;
        }
        *Info = c->Info;
        return MSGIRQ_STATUS_CONFIG_ACCESS;
    }
    MSI4NT_TRACE(("msi4nt: connected %lu:%lu.%lu type %lu vector %02lx cpu %lu apic %lu%s\n",
                  route.Bus, route.Dev, route.Func, type, idtVector, cpu, apicId,
                  (P->MessageBased.Flags & MSGIRQP_FLAG_ISA) ? " isa" : ""));
    *Info = c->Info;
    return MSGIRQ_STATUS_OK;
}

#if defined(MSGIRQP_CALLS)
/* ------------------------------------------------------------------ */
/* The published state (design record 07): each of the five calls takes */
/* PubLock (before the library's lock), the facts are captured and the  */
/* function's entry changed under the library's lock, and the snapshot  */
/* is written after it, still under PubLock. Nothing here allocates,     */
/* and its outcome never changes a call's result.                        */
/* ------------------------------------------------------------------ */

/* The call's start: PubLock, then this call's detail and history facts
 * cleared. */
static VOID MsgIrqpPubBegin(VOID)
{
    (VOID)KeWaitForSingleObject(&MsgIrqpG.PubLock, Executive, KernelMode, FALSE, NULL);
    MsgIrqpG.PubDetailKind = 0;
    MsgIrqpG.PubDetail = 0;
#if defined(MSGIRQP_HISTORY)
    MsgIrqpG.HistChecks = 0;
    MsgIrqpG.HistFrom = 0;
    MsgIrqpG.HistFlags = 0;
    MsgIrqpG.HistNew = 0;
    MsgIrqpG.HistWithdraw = 0;
    MsgIrqpG.HistIrq = 0;
    MsgIrqpG.HistStopIrq = 0;
    MsgIrqpG.HistStopStatus = 0;
#endif
}

/* The call's end: the history copied into the registry when it changed
 * (record 08, section 5), then PubLock given back. PASSIVE_LEVEL, the
 * library's lock not held. */
static VOID MsgIrqpPubEnd(VOID)
{
#if defined(MSGIRQP_HISTORY) && defined(MSGIRQP_PUBLISH)
    MsgIrqpHistPublish();
#endif
    KeSetEvent(&MsgIrqpG.PubLock, 0, FALSE);
}

static BOOLEAN MsgIrqpPubFunction(ULONG Bus, ULONG Dev, ULONG Func)
{
    return (BOOLEAN)(Bus <= 255 && Dev <= 31 && Func <= 7);
}

/* A fresh set of facts for one call: its status and this call's detail. */
static VOID MsgIrqpPubFacts(MSGIRQP_PUB_FACTS *F, ULONG Call, ULONG Status)
{
    MsgIrqpPubZero(F, sizeof(*F));
    F->Call = Call;
    F->Status = Status;
    F->DetailKind = MsgIrqpG.PubDetailKind;
    F->Detail = MsgIrqpG.PubDetail;
    F->Mode = MsgIrqpG.Mode;
    if (Status == MSGIRQ_STATUS_HAL_UNSUPPORTED && F->DetailKind == 0 &&
        MsgIrqpG.Mode == MSGIRQ_MODE_NONE && MsgIrqpG.PubInitReason != 0) {
        F->DetailKind = MSGIRQP_DETAIL_HAL;             /* refused because of InitReason */
        F->Detail = MsgIrqpG.PubInitReason;
    }
}

#if defined(MSGIRQP_HISTORY)
/* The events of one call, from the facts it gives the snapshot (record 08,
 * section 3). Lock held; the ring's flag is taken per event. */
static VOID MsgIrqpHistFacts(ULONG Bus, ULONG Dev, ULONG Func, const MSGIRQP_PUB_FACTS *F)
{
    ULONG loc = MsgIrqpEvLocation(Bus, Dev, Func);
    ULONG msg = (F->MessageData & 0xFFFFUL) | (((F->MessageAddress >> 12) & 0xFFUL) << 16);
    ULONG calls = F->HaveCounts ? F->Calls : 0;
    ULONG notMine = F->HaveCounts ? F->NotMine : 0;
    ULONG why[4];
    ULONG k;

    switch (F->Call) {
    case MSGIRQP_PUB_CONNECT:
        if (F->Live) {
            MsgIrqpEv(MSGIRQ_EVENT_CONNECT, loc, F->Status, F->Vector,
                      (F->Irql & 0xFFUL) | ((F->Type & 0xFFUL) << 8) | ((F->Source & 0xFFUL) << 16) |
                          ((MsgIrqpG.HistChecks & 0xFFUL) << 24),
                      msg, FALSE);
        } else {
            MsgIrqpEvRefused(loc, MSGIRQ_CALL_CONNECT, F->Status, F->DetailKind, F->Detail);
        }
        break;
    case MSGIRQP_PUB_DISCONNECT:
        if (F->Status == MSGIRQ_STATUS_UNSUPPORTED) {
            MsgIrqpEvRefused(loc, MSGIRQ_CALL_DISCONNECT, F->Status, 0, 0);   /* ABSENT_ONLY, native */
            break;
        }
        MsgIrqpEv(MSGIRQ_EVENT_DISCONNECT, loc, F->Status,
                  F->Quarantine ? 1UL : (F->Absent ? 2UL : 0UL), calls, notMine, FALSE);
        if (F->AbsentOnly) {
            MsgIrqpEv(MSGIRQ_EVENT_ABSENT_ONLY, loc, F->Status, F->AoOutcome, F->AoId, 0, FALSE);
        }
        if (F->Status == MSGIRQ_STATUS_OK && F->Quarantine && F->OnClaim) {
            MsgIrqpEv(MSGIRQ_EVENT_KEPT, loc, F->Irq, 1, 0, F->MembersNow, FALSE);
        }
        break;
    case MSGIRQP_PUB_REARM:
        MsgIrqpEv(MSGIRQ_EVENT_REARM, loc, F->Status, msg, calls, notMine, TRUE);
        break;
    case MSGIRQP_PUB_CLAIM:
        if (!F->Reached) {
            MsgIrqpEvRefused(loc, MSGIRQ_CALL_ISA_CLAIM, F->Status, F->DetailKind, F->Detail);
            break;
        }
        MsgIrqpEv(MSGIRQ_EVENT_CLAIM, loc, F->Status,
                  (F->IsaResult[0] & 0xFFFFUL) | ((F->IsaResult[3] & 0xFFUL) << 16) |
                      ((MsgIrqpG.HistFrom & 0xFFUL) << 24),
                  (F->IsaResult[4] & 0xFFUL) | ((F->IsaResult[2] & 0xFFUL) << 8) |
                      ((MsgIrqpG.HistFlags & 0xFFFFUL) << 16),
                  F->IsaResult[1], FALSE);
        for (k = 0; k < 4; k++) {
            why[k] = (ULONG)F->IsaReason[4 * k] | ((ULONG)F->IsaReason[4 * k + 1] << 8) |
                     ((ULONG)F->IsaReason[4 * k + 2] << 16) | ((ULONG)F->IsaReason[4 * k + 3] << 24);
        }
        MsgIrqpEv(MSGIRQ_EVENT_CLAIM_WHY, loc, why[0], why[1], why[2], why[3], FALSE);
        if (MsgIrqpG.HistStopStatus != 0) {
            MsgIrqpEv(MSGIRQ_EVENT_CLAIM_STOP, loc, MsgIrqpG.HistStopIrq, MSGIRQ_DETAIL_NTSTATUS,
                      MsgIrqpG.HistStopStatus, 0, FALSE);
        }
        if (F->IsaResult[4] & 0x2UL) {                  /* the upper pool was scanned */
            MsgIrqpEv(MSGIRQ_EVENT_CLAIM_HIGH, loc, F->IsaResult[5], F->IsaResult[6], F->IsaResult[7],
                      (F->Budget & 0xFFUL) | ((F->BudgetUsed & 0xFFUL) << 8) |
                          ((MsgIrqpG.HistNew & 0xFFUL) << 16),
                      FALSE);
        }
        if (F->Fresh) {
            MsgIrqpEv(MSGIRQ_EVENT_KEPT, loc, F->Irq, 2, MsgIrqpG.HistWithdraw, F->MembersNow, FALSE);
        }
        break;
    case MSGIRQP_PUB_RELEASE:
        if (F->Status == MSGIRQ_STATUS_OK) {
            MsgIrqpEv(MSGIRQ_EVENT_RELEASE, loc, F->Status, MsgIrqpG.HistIrq, F->MembersLeft, 0, FALSE);
        } else if (F->Status == MSGIRQ_STATUS_CLAIM_KEPT && MsgIrqpG.HistWithdraw != 0) {
            MsgIrqpEv(MSGIRQ_EVENT_RELEASE, loc, F->Status, MsgIrqpG.HistIrq, F->MembersNow, 0, FALSE);
            MsgIrqpEv(MSGIRQ_EVENT_KEPT, loc, MsgIrqpG.HistIrq, 3, MsgIrqpG.HistWithdraw, F->MembersNow,
                      FALSE);
        } else {
            MsgIrqpEvRefused(loc, MSGIRQ_CALL_ISA_RELEASE, F->Status, F->DetailKind, F->Detail);
        }
        break;
    default:
        break;
    }
}
#endif

/* The claim the function holds now (record 07, section 4). Lock held. */
static VOID MsgIrqpPubClaimNow(MSGIRQP_PUB_FACTS *F, ULONG Bus, ULONG Dev, ULONG Func)
{
#if defined(MSI4NT_ISA_LINE)
    MSGIRQP_ISA_MEMBER *m = MsgIrqpIsaMember(Bus, Dev, Func);

    if (m != NULL) {
        F->HoldsClaim = TRUE;
        F->KeptNow = (BOOLEAN)(m->Claim->State == MSGIRQP_ISA_KEPT);
        F->MembersNow = m->Claim->Members;
        F->Irq = m->Claim->Irq;
        F->Joinable = m->Claim->Shared ? 1UL : 0UL;
    }
#else
    UNREFERENCED_PARAMETER(F);
    UNREFERENCED_PARAMETER(Bus);
    UNREFERENCED_PARAMETER(Dev);
    UNREFERENCED_PARAMETER(Func);
#endif
}

/* Applies one call's facts to the function's entry and takes a copy of
 * the result for the write. FALSE: nothing to publish. Lock held. */
static BOOLEAN MsgIrqpPubCommit(ULONG Bus, ULONG Dev, ULONG Func, const MSGIRQP_PUB_FACTS *F,
                                MSGIRQP_PUB_SNAP *Out)
{
#if defined(MSGIRQP_PUBLISH)
    MSGIRQP_PUB_SNAP *s;
    LONG i;
#endif

#if defined(MSGIRQP_HISTORY)
    MsgIrqpHistFacts(Bus, Dev, Func, F);
#endif
#if !defined(MSGIRQP_PUBLISH)
    UNREFERENCED_PARAMETER(Out);
    return FALSE;
#else
    if (!MsgIrqpG.PubOn || !MsgIrqpPubFunction(Bus, Dev, Func)) {
        return FALSE;
    }
    /* With no entry left the snapshot is built in Out, marked incomplete
     * and not kept, so the key never shows an older state (record 07). */
    s = MsgIrqpPubEntry(MsgIrqpG.Pub, MSGIRQP_PUB_SLOTS, Bus, Dev, Func, &MsgIrqpG.PubLost, Out, &i);
    MsgIrqpPubApply(s, F);
    s->Size = sizeof(MSGIRQP_PUB_SNAP);
    s->Version = MSGIRQP_PUB_VERSION;
    s->Header = MSI4NT_VERSION;
    s->Build = MSGIRQP_BUILD_BITS;
    s->LoadTimeLow = MsgIrqpG.PubLoadLow;
    s->LoadTimeHigh = MsgIrqpG.PubLoadHigh;
    s->InitReason = MsgIrqpG.PubInitReason;
    s->Generation = ++MsgIrqpG.PubGeneration;
    if (i >= 0) {
        MsgIrqpG.Pub[i].Stamp = ++MsgIrqpG.PubStamp;
        *Out = *s;
    }
    return TRUE;
#endif
}

#if defined(MSGIRQP_PUBLISH)
static NTSTATUS MsgIrqpPubKey(HANDLE *Key, HANDLE Parent, const WCHAR *Name, ULONG Chars,
                              ACCESS_MASK Access)
{
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    ULONG disposition;

    u.Buffer = (PWSTR)Name;
    u.Length = (USHORT)(Chars * sizeof(WCHAR));
    u.MaximumLength = u.Length;
    InitializeObjectAttributes(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
    *Key = NULL;
    return MsgIrqpG.PubCreate(Key, Access, &oa, 0, NULL, REG_OPTION_VOLATILE, &disposition);
}

/* Writes one snapshot: the root, the copy's key and the function's key,
 * each volatile, created or opened, then the one value. PubLock held,
 * the library's lock not held, PASSIVE_LEVEL. */
static VOID MsgIrqpPubWrite(ULONG Bus, ULONG Dev, ULONG Func, MSGIRQP_PUB_SNAP *S)
{
    static const WCHAR root[] = L"\\Registry\\Machine\\HARDWARE\\msi4nt-state";
    static const WCHAR value[] = L"Snapshot";
    WCHAR copy[2 * sizeof(ULONG_PTR) + 1];
    WCHAR fn[16];
    HANDLE r = NULL;
    HANDLE c = NULL;
    HANDLE f = NULL;
    UNICODE_STRING u;
    ULONG n;
    NTSTATUS ns;

    ns = MsgIrqpPubKey(&r, NULL, root, sizeof(root) / sizeof(WCHAR) - 1, KEY_CREATE_SUB_KEY);
    if (NT_SUCCESS(ns)) {
        n = MsgIrqpPubHex(copy, (ULONG_PTR)&MsgIrqpG, 2 * sizeof(ULONG_PTR));
        ns = MsgIrqpPubKey(&c, r, copy, n, KEY_CREATE_SUB_KEY);
    }
    if (NT_SUCCESS(ns)) {
        n = MsgIrqpPubFunctionName(fn, Bus, Dev, Func);
        ns = MsgIrqpPubKey(&f, c, fn, n, KEY_SET_VALUE);
    }
    if (NT_SUCCESS(ns)) {
        u.Buffer = (PWSTR)value;
        u.Length = (USHORT)(sizeof(value) - sizeof(WCHAR));
        u.MaximumLength = (USHORT)sizeof(value);
        ns = MsgIrqpG.PubSet(f, &u, 0, REG_BINARY, S, sizeof(*S));
    }
    if (!NT_SUCCESS(ns)) {
        MSI4NT_TRACE(("msi4nt: publish %lu:%lu.%lu %08lx\n", Bus, Dev, Func, (ULONG)ns));
#if defined(MSGIRQP_HISTORY)
        MsgIrqpEv(MSGIRQ_EVENT_PUBFAIL, MsgIrqpEvLocation(Bus, Dev, Func), (ULONG)ns, 1, 0, 0, FALSE);
#endif
    }
    if (f != NULL) {
        MsgIrqpG.PubClose(f);
    }
    if (c != NULL) {
        MsgIrqpG.PubClose(c);
    }
    if (r != NULL) {
        MsgIrqpG.PubClose(r);
    }
}

#if defined(MSGIRQP_HISTORY)
/* The history's copy into the registry (record 08, section 5): the ring
 * staged at HIGH_LEVEL into the staging buffer when it changed since the
 * last value written, then one value in the copy's key, and the staging
 * committed only when that write succeeded. A second round when events
 * arrived during the first write. PubLock held, the library's lock not
 * held, PASSIVE_LEVEL. */
static VOID MsgIrqpHistPublish(VOID)
{
    static const WCHAR root[] = L"\\Registry\\Machine\\HARDWARE\\msi4nt-state";
    static const WCHAR value[] = L"History";
    WCHAR copy[2 * sizeof(ULONG_PTR) + 1];
    HANDLE r;
    HANDLE c;
    UNICODE_STRING u;
    ULONG n;
    ULONG bytes;
    ULONG lo;
    ULONG hi;
    ULONG round;
    MSGIRQP_RING_STAGE stage;
    KIRQL old;
    NTSTATUS ns;

    if (!MsgIrqpG.PubOn || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return;
    }
    for (round = 0; round < 2; round++) {
        old = MsgIrqpRingEnter();
        if (!MsgIrqpRingDirty(&MsgIrqpG.Ring)) {
            MsgIrqpRingLeave(old);
            return;
        }
        MsgIrqpHistNow(&lo, &hi);
        bytes = MsgIrqpRingStage(&MsgIrqpG.Ring, MsgIrqpG.HistBuf, MsgIrqpHistUnit(), lo, hi,
                                 MsgIrqpG.PubLoadLow, MsgIrqpG.PubLoadHigh, &stage);
        MsgIrqpRingLeave(old);
        r = NULL;
        c = NULL;
        ns = MsgIrqpPubKey(&r, NULL, root, sizeof(root) / sizeof(WCHAR) - 1, KEY_CREATE_SUB_KEY);
        if (NT_SUCCESS(ns)) {
            n = MsgIrqpPubHex(copy, (ULONG_PTR)&MsgIrqpG, 2 * sizeof(ULONG_PTR));
            ns = MsgIrqpPubKey(&c, r, copy, n, KEY_SET_VALUE);
        }
        if (NT_SUCCESS(ns)) {
            u.Buffer = (PWSTR)value;
            u.Length = (USHORT)(sizeof(value) - sizeof(WCHAR));
            u.MaximumLength = (USHORT)sizeof(value);
            ns = MsgIrqpG.PubSet(c, &u, 0, REG_BINARY, MsgIrqpG.HistBuf, bytes);
        }
        if (c != NULL) {
            MsgIrqpG.PubClose(c);
        }
        if (r != NULL) {
            MsgIrqpG.PubClose(r);
        }
        old = MsgIrqpRingEnter();
        MsgIrqpRingSettle(&MsgIrqpG.Ring, &stage, (BOOLEAN)NT_SUCCESS(ns));
        MsgIrqpRingLeave(old);
        if (!NT_SUCCESS(ns)) {
            MSI4NT_TRACE(("msi4nt: history %08lx\n", (ULONG)ns));
            MsgIrqpEv(MSGIRQ_EVENT_PUBFAIL, MSGIRQP_EV_COPY, (ULONG)ns, 2, 0, 0, FALSE);
            return;                                     /* the next calm point tries again */
        }
    }
}

/* A calm point outside the five calls (record 08, section 5): a query,
 * or the client's read. It never waits: it takes PubLock only when it is
 * free (a query can be called from a client's configuration routine
 * while a call of this copy holds PubLock), so it is best effort. */
static VOID MsgIrqpHistCalm(VOID)
{
    LARGE_INTEGER zero;

    if (!MsgIrqpG.PubOn || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return;
    }
    zero.QuadPart = 0;
    if (KeWaitForSingleObject(&MsgIrqpG.PubLock, Executive, KernelMode, FALSE, &zero) !=
        STATUS_SUCCESS) {
        return;                                         /* best effort: a later calm point */
    }
    MsgIrqpHistPublish();
    KeSetEvent(&MsgIrqpG.PubLock, 0, FALSE);
}
#endif
#else
static VOID MsgIrqpPubWrite(ULONG Bus, ULONG Dev, ULONG Func, MSGIRQP_PUB_SNAP *S)
{
    UNREFERENCED_PARAMETER(Bus);
    UNREFERENCED_PARAMETER(Dev);
    UNREFERENCED_PARAMETER(Func);
    UNREFERENCED_PARAMETER(S);
}
#endif /* MSGIRQP_PUBLISH */

/* Commits under the library's lock (taken here) and writes after it. */
static VOID MsgIrqpPubPost(ULONG Bus, ULONG Dev, ULONG Func, MSGIRQP_PUB_FACTS *F)
{
    MSGIRQP_PUB_SNAP snap;
    BOOLEAN have;

    MsgIrqpLock();
    MsgIrqpPubClaimNow(F, Bus, Dev, Func);
    have = MsgIrqpPubCommit(Bus, Dev, Func, F, &snap);
    MsgIrqpUnlock();
    if (have) {
        MsgIrqpPubWrite(Bus, Dev, Func, &snap);
    }
}

/* A connect's facts (record 07, sections 3 and 4). */
static VOID MsgIrqpPubConnect(const MSGIRQ_CONNECT_PARAMS *P, PMSGIRQ_MESSAGE_INFO Info, ULONG Status)
{
    MSGIRQP_PUB_FACTS f;
    MSGIRQP_PUB_SNAP snap;
    MSGIRQP_CONN *c;
    BOOLEAN have;
    ULONG bus = P->MessageBased.Bus;
    ULONG dev = P->MessageBased.Dev;
    ULONG func = P->MessageBased.Func;

    MsgIrqpPubFacts(&f, MSGIRQP_PUB_CONNECT, Status);
    f.Attach = (BOOLEAN)((P->MessageBased.Flags & MSGIRQ_CONNECT_FLAG_ATTACH) != 0);
    MsgIrqpLock();
    c = (Info != NULL) ? MsgIrqpFind(Info) : NULL;
    if (c != NULL && (Status == MSGIRQ_STATUS_OK || Status == MSGIRQ_STATUS_CONFIG_ACCESS)) {
        f.Live = TRUE;
        f.Messages = Info->MessageCount;
        f.Vector = Info->MessageInfo[0].Vector;
        f.Irql = Info->MessageInfo[0].Irql;
        f.Affinity = (ULONG)Info->MessageInfo[0].Affinity;
        f.MessageAddress = Info->MessageInfo[0].MessageAddress;
        f.MessageData = Info->MessageInfo[0].MessageData;
        if (c->Path == MSGIRQP_PATH_FAKE) {
            f.Type = c->Target.Type;
            f.Counted = 1;
            f.Source = 1;
#if defined(MSI4NT_ISA_LINE)
            if (c->Isa != NULL) {
                f.Source = (c->Isa->Irq >= MSGIRQ_HIGH_MIN) ? 3UL : 2UL;
            }
#endif
            if (f.Source == 1) {
                f.LineVector = P->MessageBased.LineInterrupt.Vector;
                f.LineIrql = P->MessageBased.LineInterrupt.Irql;
            }
        } else {
            f.Type = 0;                                 /* native: the mechanism is Windows' */
            f.Source = 4;
        }
    }
    MsgIrqpPubClaimNow(&f, bus, dev, func);
    have = MsgIrqpPubCommit(bus, dev, func, &f, &snap);
    MsgIrqpUnlock();
    if (have) {
        MsgIrqpPubWrite(bus, dev, func, &snap);
    }
}
#endif /* MSGIRQP_PUBLISH */

static ULONG MsgIrqpConnectCall(const MSGIRQ_CONNECT_PARAMS *Params, PMSGIRQ_MESSAGE_INFO *Info);

ULONG MsgIrqConnect(const MSGIRQ_CONNECT_PARAMS *Params, PMSGIRQ_MESSAGE_INFO *Info)
{
#if defined(MSGIRQP_CALLS)
    ULONG status;
#endif

    if (Info != NULL) {
        *Info = NULL;
    }
    if (!MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
    if (Params == NULL || Info == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_CONNECT, MSGIRQ_STATUS_INVALID_PARAMETER);
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
#if defined(MSGIRQP_CALLS)
    if (MsgIrqpPubFunction(Params->MessageBased.Bus, Params->MessageBased.Dev,
                           Params->MessageBased.Func)) {
        MsgIrqpPubBegin();
        status = MsgIrqpConnectCall(Params, Info);
        MsgIrqpPubConnect(Params, *Info, status);
        MsgIrqpPubEnd();
        return status;
    }
    status = MsgIrqpConnectCall(Params, Info);          /* a function that cannot be */
    MSGIRQP_EV_EARLY(MSGIRQ_CALL_CONNECT, status);
    return status;
#else
    return MsgIrqpConnectCall(Params, Info);
#endif
}

static ULONG MsgIrqpConnectCall(const MSGIRQ_CONNECT_PARAMS *Params, PMSGIRQ_MESSAGE_INFO *Info)
{
    ULONG status;
    ULONG type;

    if (Params->Version != MSGIRQ_CONNECT_MESSAGE_BASED) {
        return MSGIRQ_STATUS_VERSION;
    }
    type = Params->MessageBased.MessageType;
    if (type > MSGIRQ_MESSAGE_MSIX || Params->MessageBased.MessagesWanted == 0 ||
        (Params->MessageBased.Flags & ~(ULONG)MSGIRQP_CONNECT_FLAGS) != 0) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
#if defined(MSI4NT_ISA_LINE)
    /* The claimed line is a connect-mode line the library holds: neither a
     * port driver's, nor a pin the client vouches for, nor a line the
     * client passes in (record 05, section 2.1). */
    if ((Params->MessageBased.Flags & MSGIRQ_CONNECT_FLAG_ISA_LINE) &&
        ((Params->MessageBased.Flags &
          (MSGIRQ_CONNECT_FLAG_ATTACH | MSGIRQ_CONNECT_FLAG_PIN_PRIVATE)) ||
         Params->MessageBased.LineInterrupt.Vector != 0 ||
         Params->MessageBased.LineInterrupt.Irql != 0 ||
         Params->MessageBased.LineInterrupt.Affinity != 0 ||
         Params->MessageBased.LineInterrupt.Mode != 0 ||
         Params->MessageBased.LineInterrupt.ShareDisposition != 0)) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
#endif
    if ((Params->MessageBased.Flags & MSGIRQ_CONNECT_FLAG_ATTACH) == 0 &&
        Params->MessageBased.MessageServiceRoutine == NULL) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    if (MsgIrqpG.Mode == MSGIRQ_MODE_NONE) {
        return MsgIrqpG.NoneStatus;
    }
#if defined(MSI4NT_ISA_LINE)
    if (MsgIrqpG.Mode != MSGIRQ_MODE_FAKE_IRQ &&
        (Params->MessageBased.Flags & MSGIRQ_CONNECT_FLAG_ISA_LINE)) {
        return MSGIRQ_STATUS_UNSUPPORTED;               /* record 05: before Vista only */
    }
#endif
    MsgIrqpLock();
#if !defined(MSI4NT_NO_NATIVE) && !defined(MSI4NT_LEGACY_NT)
    if (MsgIrqpG.Mode == MSGIRQ_MODE_NATIVE) {
        status = MsgIrqpConnectNative(Params, Info);
        MsgIrqpUnlock();
        return status;
    }
#endif
    if (Params->MessageBased.FloatingSave) {
        status = MSGIRQ_STATUS_INVALID_PARAMETER;
    } else {
        status = MsgIrqpConnectFake(Params, Info);
    }
    MsgIrqpUnlock();
    return status;
}

ULONG MsgIrqArm(PMSGIRQ_MESSAGE_INFO Info)
{
    UNREFERENCED_PARAMETER(Info);
    return MSGIRQ_STATUS_UNSUPPORTED;                   /* attach mode: roadmap Q12 */
}

ULONG MsgIrqDisarm(PMSGIRQ_MESSAGE_INFO Info)
{
    UNREFERENCED_PARAMETER(Info);
    return MSGIRQ_STATUS_UNSUPPORTED;
}

ULONG MsgIrqTranslateLine(ULONG Bus, ULONG BusInterruptLevel, ULONG BusInterruptVector,
                          MSGIRQ_LINE_RESOURCE *LineInterrupt)
{
    UNREFERENCED_PARAMETER(Bus);
    UNREFERENCED_PARAMETER(BusInterruptLevel);
    UNREFERENCED_PARAMETER(BusInterruptVector);
    UNREFERENCED_PARAMETER(LineInterrupt);
    return MSGIRQ_STATUS_UNSUPPORTED;                   /* attach mode: roadmap Q12 */
}

ULONG MsgIrqDisconnect(PMSGIRQ_MESSAGE_INFO Info, ULONG Flags)
{
    MSGIRQP_CONN *c;
    MSGIRQP_IO io;
    ULONG off;
    ULONG status;
    MSGIRQP_COUNT_SYNC counts;
#if defined(MSGIRQP_CALLS)
    MSGIRQP_PUB_FACTS f;
    MSGIRQP_PUB_SNAP snap;
    BOOLEAN have = FALSE;
#endif
    ULONG bus;
    ULONG dev;
    ULONG func;
    BOOLEAN quarantine = FALSE;
    BOOLEAN onClaim = FALSE;
    BOOLEAN haveCounts = FALSE;
    BOOLEAN absent = FALSE;
    BOOLEAN absentOnly = FALSE;
    ULONG aoOutcome = 0;
    ULONG aoId = 0;

    if (!MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
    if (Info == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !MsgIrqpDisconnectFlagsValid(Flags)) {
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_DISCONNECT, MSGIRQ_STATUS_INVALID_PARAMETER);
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
#if defined(MSGIRQP_CALLS)
    MsgIrqpPubBegin();
#endif
    MsgIrqpLock();
    c = MsgIrqpFind(Info);
    if (c == NULL) {
        MsgIrqpUnlock();
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_DISCONNECT, MSGIRQ_STATUS_INVALID_PARAMETER);
#if defined(MSGIRQP_CALLS)
        MsgIrqpPubEnd();                                /* no function identified */
#endif
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    bus = c->Route.Bus;
    dev = c->Route.Dev;
    func = c->Route.Func;
    counts.Tramp = c->Tramp;
    counts.Quarantine = FALSE;
    counts.Calls = 0;
    counts.NotMine = 0;
    status = MSGIRQ_STATUS_OK;
#if !defined(MSI4NT_NO_NATIVE) && !defined(MSI4NT_LEGACY_NT)
    if (c->Path == MSGIRQP_PATH_NATIVE && (Flags & MSGIRQ_DISCONNECT_ABSENT_ONLY)) {
        status = MSGIRQ_STATUS_UNSUPPORTED;             /* record 01: the fake-IRQ path only; Info kept */
        goto done;
    }
    if (c->Path == MSGIRQP_PATH_NATIVE) {
        MSGIRQP_NATIVE_DISCONNECT dp;

        dp.Version = c->NativeVersion;
        dp.ConnectionContext = c->NativeContext;
        ((MSGIRQP_DISCONNECT_EX)MsgIrqpG.DisconnectEx)(&dp);
        MsgIrqpUnlist(c);
        c->Magic = 0;
        MsgIrqpFree(c);
        goto done;
    }
#endif
    if (Flags & MSGIRQ_DISCONNECT_QUARANTINE) {
        /* Record 01, section 2: the trampoline claims and stops calling the
         * client; the interrupt object, the trampoline record and the MSI-X
         * mapping stay for the rest of the boot. The counts are read in the
         * same synchronized step (record 07). */
        counts.Quarantine = TRUE;
        (VOID)KeSynchronizeExecution(c->Interrupt, MsgIrqpCountSync, &counts);
        haveCounts = TRUE;
        quarantine = TRUE;
        MSI4NT_TRACE(("msi4nt: quarantined vector %02lx\n", c->Vector));
#if defined(MSI4NT_ISA_LINE)
        if (c->Isa != NULL) {
            onClaim = TRUE;
            c->Isa->State = MSGIRQP_ISA_KEPT;          /* record 05: with its vector */
        }
#endif
        MsgIrqpUnlist(c);
        c->Magic = 0;
        MsgIrqpFree(c);
        goto done;
    }
    MsgIrqpIoFor(&c->Route, &io);
    if (Flags & MSGIRQ_DISCONNECT_ABSENT_ONLY) {
        /* Record 01, section 2: reads only; released only when proven absent. */
        absentOnly = TRUE;
        off = MsgIrqpAbsentOnly(&io, MsgIrqpPause, &aoOutcome, &aoId) ? MSGIRQP_OFF_ABSENT : MSGIRQP_OFF_NONE;
    } else {
        off = MsgIrqpTeardown(&io, &c->Target, Flags, MsgIrqpPause);
    }
    if (off == MSGIRQP_OFF_ABSENT) {
        absent = TRUE;
        /* The client permitted absence as proof that its MSI is off
         * (owner, 2026-11-10). */
        MSI4NT_TRACE(("msi4nt: %lu:%lu.%lu absent, released\n", c->Route.Bus, c->Route.Dev,
                      c->Route.Func));
    }
    if (off == MSGIRQP_OFF_NONE) {
        if (c->Interrupt != NULL) {
            (VOID)KeSynchronizeExecution(c->Interrupt, MsgIrqpCountSync, &counts);
            haveCounts = TRUE;
        }
        status = MSGIRQ_STATUS_CONFIG_ACCESS;
        goto done;
    }
    MsgIrqpRelease(c, &counts);
    haveCounts = TRUE;
done:
#if defined(MSGIRQP_CALLS)
    MsgIrqpPubFacts(&f, MSGIRQP_PUB_DISCONNECT, status);
    f.Quarantine = quarantine;
    f.Absent = absent;
    f.AbsentOnly = absentOnly;
    f.AoOutcome = aoOutcome;
    f.AoId = aoId;
    f.OnClaim = onClaim;
    f.HaveCounts = haveCounts;
    f.Calls = counts.Calls;
    f.NotMine = counts.NotMine;
    MsgIrqpPubClaimNow(&f, bus, dev, func);
    have = MsgIrqpPubCommit(bus, dev, func, &f, &snap);
#endif
    MsgIrqpUnlock();
#if defined(MSGIRQP_CALLS)
    if (have) {
        MsgIrqpPubWrite(bus, dev, func, &snap);
    }
    MsgIrqpPubEnd();
#else
    UNREFERENCED_PARAMETER(bus);
    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(func);
    UNREFERENCED_PARAMETER(quarantine);
    UNREFERENCED_PARAMETER(onClaim);
    UNREFERENCED_PARAMETER(haveCounts);
    UNREFERENCED_PARAMETER(absent);
    UNREFERENCED_PARAMETER(absentOnly);
    UNREFERENCED_PARAMETER(aoOutcome);
    UNREFERENCED_PARAMETER(aoId);
#endif
    return status;
}

ULONG MsgIrqReArm(PMSGIRQ_MESSAGE_INFO Info)
{
    MSGIRQP_CONN *c;
    MSGIRQP_IO io;
    ULONG apicId;
    ULONG status;
#if defined(MSGIRQP_CALLS)
    MSGIRQP_COUNT_SYNC counts;
    MSGIRQP_PUB_FACTS f;
    MSGIRQP_PUB_SNAP snap;
    BOOLEAN have = FALSE;
    ULONG bus;
    ULONG dev;
    ULONG func;
#endif

    if (!MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
    if (Info == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_REARM, MSGIRQ_STATUS_INVALID_PARAMETER);
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
#if defined(MSGIRQP_CALLS)
    MsgIrqpPubBegin();
#endif
    MsgIrqpLock();
    c = MsgIrqpFind(Info);
    if (c == NULL) {
        MsgIrqpUnlock();
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_REARM, MSGIRQ_STATUS_INVALID_PARAMETER);
#if defined(MSGIRQP_CALLS)
        MsgIrqpPubEnd();                                /* no function identified */
#endif
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    if (c->Path != MSGIRQP_PATH_FAKE) {
        status = MSGIRQ_STATUS_OK;
        goto done;
    }
#if defined(MSI4NT_ISA_LINE)
    if (c->Isa != NULL && c->Isa->Brake.Braked) {
        status = MSGIRQ_STATUS_BRAKED;                  /* record 05, section 2.7 */
        goto done;
    }
#endif
    apicId = MsgIrqpApicIdOn(c->Cpu);
    MsgIrqpIoFor(&c->Route, &io);
    status = MsgIrqpArm(&io, &c->Target, MsgIrqpAddress(apicId), MsgIrqpData(c->Vector));
    if (status == MSGIRQ_STATUS_OK) {
        c->Info->MessageInfo[0].MessageAddress = MsgIrqpAddress(apicId);
    } else if (status == MSGIRQP_STATUS_UNREACHED) {
        /* Record 04, section 7: refused before any write, so not disarmed
         * either: the function is as it was. */
        status = MSGIRQ_STATUS_CONFIG_ACCESS;
    } else {
        /* A failed arm is disarmed, as at connect; the connection stays
         * whether or not the disable is confirmed (record 04, section 6). */
        (VOID)MsgIrqpDisarm(&io, &c->Target);
    }
done:
#if defined(MSGIRQP_CALLS)
    MsgIrqpPubFacts(&f, MSGIRQP_PUB_REARM, status);
    if (c->Path == MSGIRQP_PATH_FAKE && c->Interrupt != NULL) {
        counts.Tramp = c->Tramp;
        counts.Quarantine = FALSE;
        (VOID)KeSynchronizeExecution(c->Interrupt, MsgIrqpCountSync, &counts);
        f.HaveCounts = TRUE;
        f.Calls = counts.Calls;
        f.NotMine = counts.NotMine;
    }
    f.Vector = c->Info->MessageInfo[0].Vector;
    f.MessageAddress = c->Info->MessageInfo[0].MessageAddress;
    f.MessageData = c->Info->MessageInfo[0].MessageData;
    bus = c->Route.Bus;
    dev = c->Route.Dev;
    func = c->Route.Func;
    MsgIrqpPubClaimNow(&f, bus, dev, func);
    have = MsgIrqpPubCommit(bus, dev, func, &f, &snap);
#endif
    MsgIrqpUnlock();
#if defined(MSGIRQP_CALLS)
    if (have) {
        MsgIrqpPubWrite(bus, dev, func, &snap);
    }
    MsgIrqpPubEnd();
#endif
    return status;
}


#if defined(MSI4NT_ISA_LINE)
/* ------------------------------------------------------------------ */
/* Experimental: a claimed ISA interrupt (design record 05, section 2). */
/* The picker reads what the firmware and the system say about IRQs    */
/* 0-15 and writes nothing; the claim reserves one IRQ in the PnP       */
/* manager's arbiter at a scope of its own (an owner device object per  */
/* claim) and listens on its vector before it is kept.                  */
/* ------------------------------------------------------------------ */

#define MSGIRQP_ISA_RELEASE        0xFFFFFFFFUL
#define MSGIRQP_ISA_DEFAULT_QUIET  500UL
#define MSGIRQP_ISA_MIN_QUIET      25UL
#define MSGIRQP_ISA_MAX_QUIET      5000UL
/* The listen's timeout is computed in 32 bits (MsgIrqpIsaListenOn). */
MSI4NT_C_ASSERT(isa_quiet_fits, MSGIRQP_ISA_MAX_QUIET <= 0x7FFFFFFFUL / 10000UL);
#define MSGIRQP_ISA_DEFAULT_BRAKE  1000UL
#define MSGIRQP_ISA_MIN_BRAKE      16UL
#define MSGIRQP_ISA_MAX_BRAKE      1000000UL
/* A claim never has more members than the cap: the brake's threshold at
 * the largest BrakeEntries and the most members is exact, never the
 * saturated value (MsgIrqpBrakeThreshold), and the member count fits the
 * eight bits the CLAIM record gives it (record 08). */
MSI4NT_C_ASSERT(isa_brake_fits, MSGIRQP_ISA_MAX_MEMBERS <= MSGIRQP_BRAKE_MAX / MSGIRQP_ISA_MAX_BRAKE);
MSI4NT_C_ASSERT(isa_members_fit, MSGIRQP_ISA_MAX_MEMBERS <= 0xFFUL);

/* The interface type and bus number that lead CM_FULL_RESOURCE_DESCRIPTOR
 * and follow IO_RESOURCE_REQUIREMENTS_LIST's ListSize: the 2000 DDK's
 * wdm.h names them DoNotUse1 and DoNotUse2, its ntddk.h and later kits
 * InterfaceType and BusNumber, at the same offsets. Reached through these
 * restatements so that the code compiles with either header. */
typedef struct _MSGIRQP_FULL_HEAD {
    INTERFACE_TYPE InterfaceType;
    ULONG BusNumber;
} MSGIRQP_FULL_HEAD;
typedef struct _MSGIRQP_REQ_HEAD {
    ULONG ListSize;
    INTERFACE_TYPE InterfaceType;
} MSGIRQP_REQ_HEAD;
MSI4NT_C_ASSERT(isa_full_head,
                FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) == sizeof(MSGIRQP_FULL_HEAD));
MSI4NT_C_ASSERT(isa_req_head, FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, AlternativeLists) ==
                              sizeof(MSGIRQP_REQ_HEAD) + 2 * sizeof(ULONG) + 3 * sizeof(ULONG));
#define MSGIRQP_PCI_CONFIGURATION  4        /* BUS_DATA_TYPE PCIConfiguration */
#define MSGIRQP_PIRQ_VALID         0xDEF8U  /* IRQ 3-7, 9-12, 14, 15 */
#define MSGIRQP_REG_NAME_BYTES     600UL
#define MSGIRQP_REG_VALUE_BYTES    0x4000UL
#define MSGIRQP_REG_MAX_KEYS       4096UL

/* ------------------------- the ACPI tables ------------------------ */

static PUCHAR MsgIrqpMapPhys(ULONGLONG Pa, ULONG Bytes)
{
    PHYSICAL_ADDRESS a;

    a.QuadPart = (LONGLONG)Pa;
    return (PUCHAR)MmMapIoSpace(a, Bytes, MmCached);
}

/* Finds the RSDP in [Pa, Pa + Bytes) (ACPI 5.2.5.1): 16-byte aligned,
 * "RSD PTR ", the first 20 bytes summing to zero. */
static BOOLEAN MsgIrqpRsdpIn(ULONGLONG Pa, ULONG Bytes, ULONG *Rsdt, ULONGLONG *Xsdt,
                             BOOLEAN *Short)
{
    static const UCHAR sig[8] = { 'R', 'S', 'D', ' ', 'P', 'T', 'R', ' ' };
    PUCHAR m;
    PUCHAR p;
    ULONG k;
    ULONG j;
    ULONG len;
    BOOLEAN found = FALSE;

    m = MsgIrqpMapPhys(Pa, Bytes);
    if (m == NULL) {
        *Short = TRUE;
        return FALSE;
    }
    for (k = 0; k + 20 <= Bytes && !found; k += 16) {
        p = m + k;
        for (j = 0; j < 8 && p[j] == sig[j]; j++) {
        }
        if (j < 8 || !MsgIrqpSumZero(p, 20)) {
            continue;
        }
        *Rsdt = MsgIrqpLe32(p + 16);
        *Xsdt = 0;
        if (p[15] >= 2 && k + 36 <= Bytes) {
            len = MsgIrqpLe32(p + 20);
            if (len >= 36 && len <= Bytes - k && MsgIrqpSumZero(p, len)) {
                *Xsdt = (ULONGLONG)MsgIrqpLe32(p + 24) | ((ULONGLONG)MsgIrqpLe32(p + 28) << 32);
            }
        }
        found = TRUE;
    }
    MmUnmapIoSpace(m, Bytes);
    return found;
}

/* A copy in pool of the table at Pa when its signature is Sig, its length
 * sane and its checksum good; NULL otherwise. *Short is set when a mapping
 * or the copy's pool could not be had: the table may be there, unread. */
static PUCHAR MsgIrqpAcpiTable(ULONGLONG Pa, const char *Sig, ULONG *Length, BOOLEAN *Short)
{
    PUCHAR h;
    PUCHAR t;
    PUCHAR copy;
    ULONG len;
    ULONG k;
    BOOLEAN match;

    if (Pa == 0) {
        return NULL;
    }
    h = MsgIrqpMapPhys(Pa, 36);
    if (h == NULL) {
        *Short = TRUE;
        return NULL;
    }
    match = (h[0] == (UCHAR)Sig[0] && h[1] == (UCHAR)Sig[1] && h[2] == (UCHAR)Sig[2] &&
             h[3] == (UCHAR)Sig[3]) ? TRUE : FALSE;
    len = MsgIrqpLe32(h + 4);
    MmUnmapIoSpace(h, 36);
    if (!match || len < 36 || len > 0x10000UL) {
        return NULL;
    }
    t = MsgIrqpMapPhys(Pa, len);
    if (t == NULL) {
        *Short = TRUE;
        return NULL;
    }
    copy = (PUCHAR)MsgIrqpAlloc(len);
    if (copy != NULL) {
        for (k = 0; k < len; k++) {
            copy[k] = t[k];
        }
    } else {
        *Short = TRUE;
    }
    MmUnmapIoSpace(t, len);
    if (copy != NULL && !MsgIrqpSumZero(copy, len)) {
        MsgIrqpFree(copy);
        copy = NULL;
    }
    *Length = len;
    return copy;
}

/* The FADT's SCI and the MADT's overrides and, for the upper pool (H not
 * NULL), its I/O APIC entries. Returns FALSE when a mapping or pool ran
 * short, so that a table that is there is never taken as absent. */
static BOOLEAN MsgIrqpIsaAcpi(MSGIRQP_ISA_PICK *K, MSGIRQP_HIGH *H)
{
    PUCHAR bda;
    PUCHAR root;
    PUCHAR t;
    ULONG rsdt = 0;
    ULONGLONG xsdt = 0;
    ULONGLONG pa;
    ULONG ebda;
    ULONG len;
    ULONG tlen;
    ULONG step;
    ULONG n;
    ULONG k;
    BOOLEAN found = FALSE;
    BOOLEAN isShort = FALSE;

    bda = MsgIrqpMapPhys(0x40EUL, 2);
    if (bda == NULL) {
        return FALSE;
    }
    ebda = MsgIrqpLe16(bda) << 4;
    MmUnmapIoSpace(bda, 2);
    if (ebda >= 0x80000UL && ebda < 0xA0000UL) {
        found = MsgIrqpRsdpIn(ebda, 1024, &rsdt, &xsdt, &isShort);
    }
    if (!found) {
        found = MsgIrqpRsdpIn(0xE0000UL, 0x20000UL, &rsdt, &xsdt, &isShort);
    }
    if (isShort) {
        return FALSE;
    }
    if (!found) {
        MSI4NT_TRACE(("msi4nt: isa no RSDP\n"));
        return TRUE;
    }
    root = NULL;
    step = 4;
    if (rsdt != 0) {
        root = MsgIrqpAcpiTable(rsdt, "RSDT", &len, &isShort);
    }
    if (root == NULL && xsdt != 0) {
        root = MsgIrqpAcpiTable(xsdt, "XSDT", &len, &isShort);
        step = 8;
    }
    if (root == NULL) {
        MSI4NT_TRACE(("msi4nt: isa no RSDT/XSDT\n"));
        return (BOOLEAN)!isShort;
    }
    isShort = FALSE;                    /* an RSDT that was short, an XSDT that was read */
    n = (len - 36) / step;
    for (k = 0; k < n && !isShort; k++) {
        pa = MsgIrqpLe32(root + 36 + k * step);
        if (step == 8) {
            pa |= (ULONGLONG)MsgIrqpLe32(root + 40 + k * step) << 32;
        }
        t = MsgIrqpAcpiTable(pa, "FACP", &tlen, &isShort);
        if (t != NULL) {
            if (tlen >= 48) {
                K->Sci = MsgIrqpLe16(t + 46);
                K->Sources |= MSGIRQ_ISA_SOURCE_FADT;
            }
            MsgIrqpFree(t);
            continue;
        }
        t = MsgIrqpAcpiTable(pa, "APIC", &tlen, &isShort);
        if (t != NULL) {
            MsgIrqpIsaMadt(K, t, tlen);
            if (H != NULL) {
                MsgIrqpHighMadt(H, t, tlen);
            }
            K->Sources |= MSGIRQ_ISA_SOURCE_MADT;
            MsgIrqpFree(t);
        }
    }
    MsgIrqpFree(root);
    return (BOOLEAN)!isShort;
}

/* ----------------------------- the ELCR --------------------------- */

#if defined(MSI4NT_AMD64)
unsigned char __inbyte(unsigned short Port);
#pragma intrinsic(__inbyte)
#endif

static UCHAR MsgIrqpInByte(USHORT Port)
{
#if defined(MSI4NT_X86)
    UCHAR v;

    __asm {
        mov dx, Port
        in al, dx
        mov v, al
    }
    return v;
#else
    return __inbyte(Port);
#endif
}

/* ELCR1/ELCR2 (ports 4D0h and 4D1h): read only. */
static VOID MsgIrqpIsaElcr(MSGIRQP_ISA_PICK *K)
{
    ULONG lo = MsgIrqpInByte(0x4D0);
    ULONG hi = MsgIrqpInByte(0x4D1);

    MsgIrqpIsaElcrValue(K, lo, hi);
    if ((K->Sources & MSGIRQ_ISA_SOURCE_ELCR) == 0) {
        MSI4NT_TRACE(("msi4nt: isa ELCR %04lx taken as absent\n", K->Elcr));
    }
}

/* ------------------------- Intel PIRQx_ROUT ----------------------- */

/* The Intel ISA bridge on bus 0 (8086h, class 0601h): PIRQA-D at 60h-63h
 * and, at device 31, PIRQE-H at 68h-6Bh; bit 7 clear routes the PIRQ to
 * the IRQ in bits 3:0 (ICH9 datasheet 13.1.17, 13.1.19). */
static VOID MsgIrqpIsaPirq(MSGIRQP_ISA_PICK *K)
{
    UCHAR h[12];
    UCHAR r[4];
    ULONG dev;
    ULONG pass;
    ULONG k;
    ULONG irq;
    ULONG base;

    for (dev = 0; dev < 32; dev++) {
        if (MsgIrqpG.IsaBusData(MSGIRQP_PCI_CONFIGURATION, 0, dev, h, 0, 12) != 12) {
            continue;
        }
        if (MsgIrqpLe16(h) != 0x8086 || h[11] != 0x06 || h[10] != 0x01) {
            continue;
        }
        for (pass = 0; pass < ((dev == 31) ? 2UL : 1UL); pass++) {
            base = pass ? 0x68UL : 0x60UL;
            if (MsgIrqpG.IsaBusData(MSGIRQP_PCI_CONFIGURATION, 0, dev, r, base, 4) != 4) {
                continue;
            }
            for (k = 0; k < 4; k++) {
                irq = r[k] & 0x0FUL;
                if ((r[k] & 0x80) == 0 && (MSGIRQP_PIRQ_VALID & (1UL << irq))) {
                    K->Pirq |= 1UL << irq;
                    K->PirqOffset[irq] = (UCHAR)(base + k);
                }
            }
        }
        K->Sources |= MSGIRQ_ISA_SOURCE_PIRQ;
        MSI4NT_TRACE(("msi4nt: isa PIRQ bridge 0:%lu.0 routed %04lx\n", dev, K->Pirq));
        return;
    }
}

/* --------------------------- the registry ------------------------- */

static ULONG MsgIrqpWideBytes(PCWSTR s)
{
    ULONG n = 0;

    while (s[n] != 0) {
        n++;
    }
    return n * sizeof(WCHAR);
}

static NTSTATUS MsgIrqpRegOpen(HANDLE Parent, PCWSTR Name, ULONG Bytes, HANDLE *Key)
{
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;

    u.Buffer = (PWSTR)Name;
    u.Length = (USHORT)Bytes;
    u.MaximumLength = (USHORT)Bytes;
    InitializeObjectAttributes(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
    *Key = NULL;
    return MsgIrqpG.IsaOpenKey(Key, KEY_READ, &oa);
}

/* Opens the Index-th subkey of Parent (Name: MSGIRQP_REG_NAME_BYTES of
 * scratch). STATUS_NO_MORE_ENTRIES ends an enumeration. */
static NTSTATUS MsgIrqpRegChild(HANDLE Parent, ULONG Index, PUCHAR Name, HANDLE *Key)
{
    PKEY_BASIC_INFORMATION b = (PKEY_BASIC_INFORMATION)Name;
    ULONG got = 0;
    NTSTATUS ns;

    *Key = NULL;
    ns = MsgIrqpG.IsaEnumKey(Parent, Index, KeyBasicInformation, b,
                             MSGIRQP_REG_NAME_BYTES, &got);
    if (!NT_SUCCESS(ns)) {
        return ns;
    }
    if (b->NameLength > MSGIRQP_REG_NAME_BYTES - FIELD_OFFSET(KEY_BASIC_INFORMATION, Name)) {
        return STATUS_UNSUCCESSFUL;
    }
    return MsgIrqpRegOpen(Parent, b->Name, b->NameLength, Key);
}

/* Marks the interrupts of a CM_RESOURCE_LIST (raw: level and vector are
 * the IRQ) below 16 in *Mask and, for the upper pool (H not NULL), those
 * in 24..255 as configured. */
static VOID MsgIrqpMarkCm(const UCHAR *Data, ULONG Bytes, ULONG *Mask, MSGIRQP_HIGH *H)
{
    const CM_PARTIAL_RESOURCE_DESCRIPTOR *d;
    ULONG head = (ULONG)FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,
                                     PartialResourceList.PartialDescriptors);
    ULONG count;
    ULONG off;
    ULONG n;
    ULONG k;
    ULONG j;
    ULONG ds;
    ULONG iface;

    if (Bytes < 4) {
        return;
    }
    count = MsgIrqpLe32(Data);
    off = 4;
    for (k = 0; k < count; k++) {
        if (Bytes - off < head) {
            return;
        }
        n = ((const CM_FULL_RESOURCE_DESCRIPTOR *)(Data + off))->PartialResourceList.Count;
        iface = (ULONG)((const MSGIRQP_FULL_HEAD *)(Data + off))->InterfaceType;
        off += head;
        for (j = 0; j < n; j++) {
            if (Bytes - off < sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR)) {
                return;
            }
            d = (const CM_PARTIAL_RESOURCE_DESCRIPTOR *)(Data + off);
            off += sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
            if (d->Type == CmResourceTypeInterrupt && iface != (ULONG)Internal) {
                /* Internal: the HAL's own reservations, whose numbers are
                 * IDT vectors, not ISA inputs (record 05, section 2.3). */
                MSI4NT_TRACE(("msi4nt: isa cm iface %lu level %lx vector %lx\n", iface,
                              d->u.Interrupt.Level, d->u.Interrupt.Vector));
                if (d->u.Interrupt.Level < 16) {
                    *Mask |= 1UL << d->u.Interrupt.Level;
                }
                if (d->u.Interrupt.Vector < 16) {
                    *Mask |= 1UL << d->u.Interrupt.Vector;
                }
                if (H != NULL) {
                    MsgIrqpHighMarkCm(H, d->u.Interrupt.Level, d->u.Interrupt.Vector);
                }
            } else if (d->Type == CmResourceTypeDeviceSpecific) {
                ds = d->u.DeviceSpecificData.DataSize;
                if (ds > Bytes - off) {
                    return;
                }
                off += ds;
            }
        }
    }
}

/* Marks every IRQ below 16 that an IO_RESOURCE_REQUIREMENTS_LIST's
 * interrupt ranges allow, alternatives included, and for the upper pool
 * (H not NULL) every input above 23 a range names (MsgIrqpHighMarkIo). */
static VOID MsgIrqpMarkIo(const UCHAR *Data, ULONG Bytes, ULONG *Mask, MSGIRQP_HIGH *H)
{
    const IO_RESOURCE_DESCRIPTOR *d;
    ULONG head = (ULONG)FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List);
    ULONG lhead = (ULONG)FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors);
    ULONG lists;
    ULONG off;
    ULONG n;
    ULONG k;
    ULONG j;
    ULONG irq;
    ULONG iface;

    if (Bytes < head) {
        return;
    }
    lists = ((const IO_RESOURCE_REQUIREMENTS_LIST *)Data)->AlternativeLists;
    iface = (ULONG)((const MSGIRQP_REQ_HEAD *)Data)->InterfaceType;
    off = head;
    for (k = 0; k < lists; k++) {
        if (Bytes - off < lhead) {
            return;
        }
        n = ((const IO_RESOURCE_LIST *)(Data + off))->Count;
        off += lhead;
        for (j = 0; j < n; j++) {
            if (Bytes - off < sizeof(IO_RESOURCE_DESCRIPTOR)) {
                return;
            }
            d = (const IO_RESOURCE_DESCRIPTOR *)(Data + off);
            off += sizeof(IO_RESOURCE_DESCRIPTOR);
            /* Only a range within the ISA IRQs names them: a requirement
             * for any interrupt (0-FFh) says nothing about one input. */
            if (d->Type == CmResourceTypeInterrupt && d->u.Interrupt.MaximumVector < 16 &&
                iface != (ULONG)Internal) {
                MSI4NT_TRACE(("msi4nt: isa io min %lx max %lx\n", d->u.Interrupt.MinimumVector,
                              d->u.Interrupt.MaximumVector));
                for (irq = d->u.Interrupt.MinimumVector;
                     irq <= d->u.Interrupt.MaximumVector && irq < 16; irq++) {
                    *Mask |= 1UL << irq;
                }
            }
            if (H != NULL && d->Type == CmResourceTypeInterrupt && iface != (ULONG)Internal) {
                MsgIrqpHighMarkIo(H, d->u.Interrupt.MinimumVector, d->u.Interrupt.MaximumVector);
            }
        }
    }
}

/* One named value of Key, of the given type, into the marks. */
static VOID MsgIrqpRegMark(HANDLE Key, PCWSTR Name, ULONG Type, PUCHAR Buf, ULONG *Mask,
                           MSGIRQP_HIGH *H)
{
    PKEY_VALUE_PARTIAL_INFORMATION v = (PKEY_VALUE_PARTIAL_INFORMATION)Buf;
    UNICODE_STRING u;
    ULONG got = 0;
    ULONG room = MSGIRQP_REG_VALUE_BYTES - FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);

    u.Buffer = (PWSTR)Name;
    u.Length = (USHORT)MsgIrqpWideBytes(Name);
    u.MaximumLength = u.Length;
    if (!NT_SUCCESS(MsgIrqpG.IsaQueryValue(Key, &u, KeyValuePartialInformation, v,
                                           MSGIRQP_REG_VALUE_BYTES, &got)) ||
        v->Type != Type || v->DataLength > room) {
        return;
    }
    if (Type == REG_RESOURCE_LIST) {
        MsgIrqpMarkCm(v->Data, v->DataLength, Mask, H);
    } else {
        MsgIrqpMarkIo(v->Data, v->DataLength, Mask, H);
    }
}

/* The devnodes' configurations under Enum (section 2.3); the resource
 * map is not read. Returns FALSE when its scratch could not be allocated. */
static BOOLEAN MsgIrqpIsaRegistry(MSGIRQP_ISA_PICK *K, MSGIRQP_HIGH *H)
{
    static const WCHAR enumPath[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum";
    PUCHAR name;
    PUCHAR buf;
    HANDLE root;
    HANDLE k1;
    HANDLE k2;
    HANDLE k3;
    HANDLE sub;
    ULONG i;
    ULONG j;
    ULONG m;
    BOOLEAN read = FALSE;

    name = (PUCHAR)MsgIrqpAlloc(MSGIRQP_REG_NAME_BYTES);
    buf = (PUCHAR)MsgIrqpAlloc(MSGIRQP_REG_VALUE_BYTES);
    if (name == NULL || buf == NULL) {
        if (name != NULL) {
            MsgIrqpFree(name);
        }
        if (buf != NULL) {
            MsgIrqpFree(buf);
        }
        return FALSE;
    }
    if (NT_SUCCESS(MsgIrqpRegOpen(NULL, enumPath, MsgIrqpWideBytes(enumPath), &root))) {
        read = TRUE;
        for (i = 0; i < MSGIRQP_REG_MAX_KEYS; i++) {
            NTSTATUS ns = MsgIrqpRegChild(root, i, name, &k1);
            if (ns == STATUS_NO_MORE_ENTRIES) {
                break;
            }
            if (!NT_SUCCESS(ns)) {
                continue;
            }
            for (j = 0; j < MSGIRQP_REG_MAX_KEYS; j++) {
                ns = MsgIrqpRegChild(k1, j, name, &k2);
                if (ns == STATUS_NO_MORE_ENTRIES) {
                    break;
                }
                if (!NT_SUCCESS(ns)) {
                    continue;
                }
                for (m = 0; m < MSGIRQP_REG_MAX_KEYS; m++) {
                    ns = MsgIrqpRegChild(k2, m, name, &k3);
                    if (ns == STATUS_NO_MORE_ENTRIES) {
                        break;
                    }
                    if (!NT_SUCCESS(ns)) {
                        continue;
                    }
                    if (NT_SUCCESS(MsgIrqpRegOpen(k3, L"LogConf", 14, &sub))) {
                        MsgIrqpRegMark(sub, L"BootConfig", REG_RESOURCE_LIST, buf,
                                       &K->Configured, H);
                        MsgIrqpRegMark(sub, L"BasicConfigVector", REG_RESOURCE_REQUIREMENTS_LIST,
                                       buf, &K->Possible, H);
                        MsgIrqpG.IsaClose(sub);
                    }
                    if (NT_SUCCESS(MsgIrqpRegOpen(k3, L"Control", 14, &sub))) {
                        MsgIrqpRegMark(sub, L"AllocConfig", REG_RESOURCE_LIST, buf,
                                       &K->Configured, H);
                        MsgIrqpG.IsaClose(sub);
                    }
                    MsgIrqpG.IsaClose(k3);
                }
                MsgIrqpG.IsaClose(k2);
            }
            MsgIrqpG.IsaClose(k1);
        }
        MsgIrqpG.IsaClose(root);
    }
    if (read) {
        K->Sources |= MSGIRQ_ISA_SOURCE_REGISTRY;
    }
    MsgIrqpFree(name);
    MsgIrqpFree(buf);
    return TRUE;
}

/* Reads every source and gives each IRQ its first reason (section 2.3).
 * With the upper pool chosen (H not NULL) the same reading of the MADT
 * and the same registry walk fill H; a call for the upper pool alone
 * reads neither the ELCR nor the bridge and gives no ISA IRQ a reason
 * (section 2.9: the ISA IRQs are not evaluated). Returns FALSE when pool
 * or a mapping for the ACPI tables, or the registry walk's scratch, could
 * not be had. */
static BOOLEAN MsgIrqpIsaSources(MSGIRQP_ISA_PICK *K, BOOLEAN Override, ULONG Exclude,
                                 MSGIRQP_HIGH *H, ULONG Pools)
{
    PUCHAR b = (PUCHAR)K;
    ULONG k;

    for (k = 0; k < sizeof(*K); k++) {
        b[k] = 0;
    }
    K->Sci = 0xFFFFFFFFUL;
    if (!MsgIrqpIsaAcpi(K, H)) {
        return FALSE;
    }
    if (Pools & MSGIRQ_POOL_ISA) {
        MsgIrqpIsaElcr(K);
        MsgIrqpIsaPirq(K);
    }
    if (!MsgIrqpIsaRegistry(K, H)) {
        return FALSE;
    }
    if ((Pools & MSGIRQ_POOL_ISA) == 0) {
        MSI4NT_TRACE(("msi4nt: isa pool not chosen; sources %lx\n", K->Sources));
        return TRUE;
    }
    MsgIrqpIsaReasons(K, Override, Exclude);
    for (k = 0; k < 16; k++) {
        MSI4NT_TRACE(("msi4nt: isa irq %lu reason %lu detail %lx\n", k, (ULONG)K->Reason[k],
                      K->Detail[k]));
    }
    MSI4NT_TRACE(("msi4nt: isa sources %lx sci %lx configured %04lx possible %04lx override "
                  "%04lx level %04lx pirq %04lx\n", K->Sources, K->Sci, K->Configured,
                  K->Possible, K->Override, K->Level, K->Pirq));
    return TRUE;
}

/* ---------------------------- the claim --------------------------- */

/* The listen: counts whatever arrives on a candidate's vector while
 * nothing of the client's is pointed at it. One interrupt object, one
 * spin lock: the count needs no interlock. */
static BOOLEAN __stdcall MsgIrqpIsaListen(PKINTERRUPT Interrupt, PVOID Context)
{
    UNREFERENCED_PARAMETER(Interrupt);
    ((MSGIRQP_ISA *)Context)->Heard++;
    return TRUE;
}

/* Reports Irq (or, with MSGIRQP_ISA_RELEASE, nothing: the withdrawal) at
 * the claim's own scope: an ISA IRQ under Isa, an input above 23 under
 * PCIBus, the only interface type the arbiter grants there (section 2.9). */
static NTSTATUS MsgIrqpIsaReport(MSGIRQP_ISA *I, ULONG Irq, BOOLEAN *Conflict)
{
    CM_RESOURCE_LIST list;              /* one full and one partial descriptor */
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    PUCHAR b = (PUCHAR)&list;
    ULONG k;

    for (k = 0; k < sizeof(list); k++) {
        b[k] = 0;
    }
    if (Irq != MSGIRQP_ISA_RELEASE) {
        list.Count = 1;
        ((MSGIRQP_FULL_HEAD *)&list.List[0])->InterfaceType = (Irq < 16) ? Isa : PCIBus;
        ((MSGIRQP_FULL_HEAD *)&list.List[0])->BusNumber = 0;
        list.List[0].PartialResourceList.Version = 1;
        list.List[0].PartialResourceList.Revision = 1;
        list.List[0].PartialResourceList.Count = 1;
        d = &list.List[0].PartialResourceList.PartialDescriptors[0];
        d->Type = CmResourceTypeInterrupt;
        d->ShareDisposition = CmResourceShareDeviceExclusive;
        d->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
        d->u.Interrupt.Level = Irq;
        d->u.Interrupt.Vector = Irq;
        d->u.Interrupt.Affinity = (KAFFINITY)-1;
    }
    *Conflict = FALSE;
#if defined(MSGIRQP_HISTORY)
    {
        NTSTATUS ns = MsgIrqpG.IsaReport(I->Driver, NULL, 0, I->Owner, &list, (ULONG)sizeof(list),
                                         Conflict);

        if (Irq == MSGIRQP_ISA_RELEASE && !NT_SUCCESS(ns)) {
            MsgIrqpG.HistWithdraw = (ULONG)ns;          /* a KEPT event's status (record 08) */
        }
        return ns;
    }
#else
    return MsgIrqpG.IsaReport(I->Driver, NULL, 0, I->Owner, &list, (ULONG)sizeof(list), Conflict);
#endif
}

static VOID MsgIrqpIsaUnlist(MSGIRQP_ISA *I)
{
    MSGIRQP_ISA **pp;

    for (pp = &MsgIrqpG.IsaList; *pp != NULL; pp = &(*pp)->Next) {
        if (*pp == I) {
            *pp = I->Next;
            return;
        }
    }
}

static VOID MsgIrqpIsaDropMember(MSGIRQP_ISA_MEMBER *M)
{
    MSGIRQP_ISA_MEMBER **pp;

    for (pp = &MsgIrqpG.IsaMembers; *pp != NULL; pp = &(*pp)->Next) {
        if (*pp == M) {
            *pp = M->Next;
            break;
        }
    }
    MsgIrqpFree(M);
}

/* Links the function's membership, allocated before anything was
 * reported (so that a claim is never left without one). Lock held. */
static VOID MsgIrqpIsaAddMember(MSGIRQP_ISA *I, MSGIRQP_ISA_MEMBER *m,
                                const MSGIRQ_CONNECT_PARAMS *P)
{
    m->Bus = P->MessageBased.Bus;
    m->Dev = P->MessageBased.Dev;
    m->Func = P->MessageBased.Func;
    m->Claim = I;
    m->Next = MsgIrqpG.IsaMembers;
    MsgIrqpG.IsaMembers = m;
    I->Members++;
}

/* The brake's threshold for the current membership: one aligned store,
 * under the lock (section 2.7). */
static VOID MsgIrqpIsaThreshold(MSGIRQP_ISA *I)
{
    I->Brake.At = MsgIrqpBrakeThreshold(I->BrakeEntries, I->Members);
}

/* A candidate's translation is an APIC-mode one (record 01, section 1). */
static BOOLEAN MsgIrqpIsaApicLine(ULONG Vector, KIRQL Irql)
{
    return (MsgIrqpVectorIsApic(MsgIrqpIdtVector(Vector)) && Irql > DISPATCH_LEVEL) ? TRUE : FALSE;
}

/* A translation the library may use, in either pool, at the claim and
 * at every connect (section 2.9, "A translation allocates"): an APIC-mode
 * vector, and an IRQL that agrees with the vector's priority class as
 * this architecture's HALs give it. */
static BOOLEAN MsgIrqpLineSound(ULONG Vector, KIRQL Irql)
{
    return (BOOLEAN)(MsgIrqpIsaApicLine(Vector, Irql) &&
                     MsgIrqpHighAgrees(MsgIrqpIdtVector(Vector), (ULONG)Irql,
#if defined(MSI4NT_AMD64)
                                       TRUE
#else
                                       FALSE
#endif
                                       ));
}

/* The listen on one translated line (section 2.2, step 5.3): an exclusive,
 * latched connect for QuietMs to a routine that only counts. One of
 * MSGIRQP_OP_OK (quiet), MSGIRQP_OP_NO_MEMORY, MSGIRQP_OP_HELD (the
 * connect failed otherwise: *Detail its status) or MSGIRQP_OP_HEARD
 * (*Detail the count). Number is the IRQ or input, for the trace. */
static ULONG MsgIrqpIsaListenOn(MSGIRQP_ISA *I, ULONG Number, ULONG Vector, KIRQL Irql,
                                KAFFINITY Affinity, ULONG QuietMs, ULONG *Detail)
{
    PKINTERRUPT object;
    KEVENT never;
    LARGE_INTEGER wait;
    NTSTATUS ns;

    I->Heard = 0;
    ns = IoConnectInterrupt(&object, (PKSERVICE_ROUTINE)MsgIrqpIsaListen, I, NULL, Vector, Irql,
                            Irql, Latched, FALSE, Affinity, FALSE);
    if (!NT_SUCCESS(ns)) {
        MSI4NT_TRACE(("msi4nt: isa irq %lu listen connect %08lx\n", Number, (ULONG)ns));
        *Detail = (ULONG)ns;
        return (ns == STATUS_INSUFFICIENT_RESOURCES || ns == STATUS_NO_MEMORY)
                   ? MSGIRQP_OP_NO_MEMORY : MSGIRQP_OP_HELD;
    }
    KeInitializeEvent(&never, NotificationEvent, FALSE);
    /* In 32 bits, then widened: a 64-bit multiply by a variable is a call
     * to the compiler's _allmul on x86, an import a client's audit refuses.
     * QuietMs is validated to at most MSGIRQP_ISA_MAX_QUIET, so the product
     * fits a LONG (asserted below the limits). */
    wait.QuadPart = -(LONGLONG)(LONG)(QuietMs * 10000UL);
    (VOID)KeWaitForSingleObject(&never, Executive, KernelMode, FALSE, &wait);
    IoDisconnectInterrupt(object);
    MSI4NT_TRACE(("msi4nt: isa irq %lu heard %lu in %lu ms\n", Number, (ULONG)I->Heard, QuietMs));
    if (I->Heard != 0) {
        *Detail = (ULONG)I->Heard;
        return MSGIRQP_OP_HEARD;
    }
    return MSGIRQP_OP_OK;
}

/* The listen on one translated ISA candidate. Returns MSGIRQ_STATUS_OK
 * when quiet, NO_VECTORS when held or heard, NO_MEMORY when the interrupt
 * object could not be allocated. */
static ULONG MsgIrqpIsaHear(MSGIRQP_ISA *I, MSGIRQP_ISA_PICK *K, ULONG Irq, ULONG QuietMs)
{
    ULONG detail = 0;
    ULONG r = MsgIrqpIsaListenOn(I, Irq, K->Vector[Irq], K->Irql[Irq], K->Affinity[Irq], QuietMs,
                                 &detail);

    if (r == MSGIRQP_OP_OK) {
        return MSGIRQ_STATUS_OK;
    }
    K->Detail[Irq] = detail;
    if (r == MSGIRQP_OP_NO_MEMORY) {
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    K->Reason[Irq] = (UCHAR)((r == MSGIRQP_OP_HEARD) ? MSGIRQ_ISA_HEARD : MSGIRQ_ISA_HELD);
    return MSGIRQ_STATUS_NO_VECTORS;
}

static VOID MsgIrqpIsaResult(MSGIRQ_ISA_RESULT *R, const MSGIRQP_ISA_PICK *K, ULONG Irq,
                             ULONG Members)
{
    ULONG k;

    if (R == NULL) {
        return;
    }
    R->Irq = Irq;
    R->Members = Members;
    if (K != NULL) {
        R->Sci = K->Sci;
        R->Sources = K->Sources;
        for (k = 0; k < 16; k++) {
            R->Reason[k] = K->Reason[k];
            R->Detail[k] = K->Detail[k];
        }
    }
}

/* The second pool's part of the result, for a caller whose structure has
 * it (section 2.9). H is NULL when the upper pool was not chosen. */
static VOID MsgIrqpHighResult(MSGIRQ_ISA_RESULT *R, const MSGIRQP_HIGH *H, ULONG Scanned)
{
    ULONG k;

    if (R == NULL || R->Size < sizeof(MSGIRQ_ISA_RESULT)) {
        return;
    }
    R->PoolsScanned = Scanned;
    if (H == NULL) {
        return;
    }
    R->HighFirst = H->First;
    R->HighLast = H->Last;
    R->HighIoApics = H->IoApics;
    R->HighIoApicCount = H->ApicCount;
    if (Scanned & MSGIRQ_POOL_HIGH) {
        R->HighBudget = H->Budget;
        R->HighNew = H->New;
        R->HighSpent = H->Spent;
        R->HighEnd = H->End;
        R->HighLastInput = H->LastInput;
        R->HighDetail = H->Detail;
        for (k = 0; k < 256; k++) {
            R->HighReason[k] = H->Reason[k];
        }
    }
}

static BOOLEAN MsgIrqpIsaSystemThread(VOID)
{
    return (MsgIrqpG.IsaProcess() == *MsgIrqpG.IsaSystemProcess) ? TRUE : FALSE;
}

/* Checks shared by claim and release; MSGIRQ_STATUS_OK to go on. */
static ULONG MsgIrqpIsaContext(VOID)
{
    if (MsgIrqpG.Mode == MSGIRQ_MODE_NONE) {
        return MsgIrqpG.NoneStatus;                     /* a PIC-mode HAL: HAL_UNSUPPORTED */
    }
    if (MsgIrqpG.Mode != MSGIRQ_MODE_FAKE_IRQ || MsgIrqpG.IsaReport == NULL ||
        MsgIrqpG.IsaCreate == NULL || MsgIrqpG.IsaDelete == NULL || MsgIrqpG.IsaVector == NULL ||
        MsgIrqpG.IsaBusData == NULL || MsgIrqpG.IsaOpenKey == NULL || MsgIrqpG.IsaEnumKey == NULL ||
        MsgIrqpG.IsaQueryValue == NULL ||
        MsgIrqpG.IsaClose == NULL || MsgIrqpG.IsaProcess == NULL ||
        MsgIrqpG.IsaSystemProcess == NULL) {
        return MSGIRQ_STATUS_UNSUPPORTED;
    }
    if (!MsgIrqpIsaSystemThread()) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;         /* section 2.6 */
    }
    return MSGIRQ_STATUS_OK;
}

/* Section 2.8: the function's own picker found no IRQ (First FALSE), or
 * section 2.12: shared line first, before anything is translated or
 * reported (First TRUE). With the flag, it joins a joinable claim of the
 * same driver on a candidate IRQ, if one has room; the picker's findings
 * stay in Pick, with the joined IRQ JOINED or, after the picker only, a
 * full claim's IRQ FULL (the first join leaves every other reason as the
 * sources set it, for the IRQ of the function's own that may follow).
 * Returns the claim joined, or NULL. Lock held. */
static MSGIRQP_ISA *MsgIrqpIsaJoin(const MSGIRQ_ISA_PARAMS *Options, ULONG Cap, const UCHAR *Order,
                                   ULONG Count, MSGIRQP_ISA_PICK *Pick, BOOLEAN First)
{
    ULONG members[16];
    ULONG caps[16];
    MSGIRQP_ISA *I;
    ULONG k;
    ULONG irq;
    ULONG full;

    if ((Options->Flags & MSGIRQ_ISA_FLAG_SHARE) == 0) {
        return NULL;
    }
    for (k = 0; k < 16; k++) {
        members[k] = 0;
        caps[k] = 0;
    }
    for (I = MsgIrqpG.IsaList; I != NULL; I = I->Next) {
        if (I->Shared && I->State == MSGIRQP_ISA_HELD && !I->Brake.Braked &&
            I->Driver == Options->DriverObject && I->Irq < 16) {
            members[I->Irq] = I->Members;
            caps[I->Irq] = (I->MaxMembers < Cap) ? I->MaxMembers : Cap;
        }
    }
    irq = MsgIrqpIsaJoinChoice(Order, Count, Pick->Reason, members, caps, First, &full);
    if (irq == 0) {
        if (full != 0 && !First) {
            Pick->Reason[full] = MSGIRQ_ISA_FULL;      /* only when nothing is joined */
            Pick->Detail[full] = members[full];
        }
        return NULL;
    }
    for (I = MsgIrqpG.IsaList; I != NULL; I = I->Next) {
        if (I->Shared && I->State == MSGIRQP_ISA_HELD && !I->Brake.Braked &&
            I->Driver == Options->DriverObject && I->Irq == irq) {
            Pick->Reason[irq] = MSGIRQ_ISA_JOINED;
            Pick->Detail[irq] = I->Line.Vector;
            return I;
        }
    }
    return NULL;
}

/* One MsgIrqIsaClaim call, for its three steps (MsgIrqpPoolRun). */
typedef struct _MSGIRQP_CLAIM_RUN {
    const MSGIRQ_ISA_PARAMS *Options;
    const MSGIRQP_POOLS *Pools;
    MSGIRQP_ISA_PICK *Pick;
    MSGIRQP_HIGH *High;                 /* NULL: the upper pool was not chosen */
    ULONG Quiet;
    ULONG Brake;
    ULONG Cap;
    MSGIRQP_ISA *Claim;                 /* the claim a step made, kept or joined */
    /* the upper pool's operations */
    ULONG Vector;                       /* the last translation made */
    KIRQL Irql;
    KAFFINITY Affinity;
    HANDLE Ledger;                      /* the driver's record, open during the upper step */
} MSGIRQP_CLAIM_RUN;

/* A claim record with its owner object (section 2.2, step 4), nothing
 * reported yet. NULL: no pool, or the owner could not be created. */
static MSGIRQP_ISA *MsgIrqpIsaNew(const MSGIRQP_CLAIM_RUN *Run, BOOLEAN Shared)
{
    MSGIRQP_ISA *I;
    NTSTATUS ns;

    I = (MSGIRQP_ISA *)MsgIrqpAlloc(sizeof(MSGIRQP_ISA));
    if (I == NULL) {
        return NULL;
    }
    I->Driver = Run->Options->DriverObject;
    I->Shared = Shared;
    I->BrakeEntries = Run->Brake;
    I->MaxMembers = Run->Cap;
    ns = MsgIrqpG.IsaCreate(I->Driver, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &I->Owner);
    if (!NT_SUCCESS(ns) || I->Owner == NULL) {
        MsgIrqpFree(I);
        return NULL;
    }
    return I;
}

/* The ISA pool's attempt (sections 2.2 and 2.3): an ISA IRQ of the
 * function's own. MSGIRQ_STATUS_OK with Run->Claim reported, listened to
 * and holding its IRQ and line; NO_VECTORS with nothing held (every
 * candidate excluded by a source or untranslated, or each rejected by the
 * arbiter or the listen); HAL_UNSUPPORTED, NO_MEMORY or UNSUPPORTED with
 * nothing held; CLAIM_KEPT with Run->Claim the claim whose withdrawal
 * failed. It never joins. Lock held. */
static ULONG MsgIrqpIsaStep(PVOID Context)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;
    MSGIRQP_ISA_PICK *pick = run->Pick;
    const UCHAR *order = run->Pools->IsaOrder;
    ULONG count = run->Pools->IsaCount;
    ULONG k;
    ULONG irq = 0;
    ULONG status;
    BOOLEAN conflict;
    BOOLEAN reported;
    BOOLEAN kept;
    MSGIRQP_ISA *I;
    NTSTATUS ns;

    /* Section 2.2, step 2: every surviving candidate is translated before
     * anything is reported; a translation that is not an APIC-mode one,
     * or whose vector and IRQL do not agree, refuses the claim. */
    for (k = 0; k < count; k++) {
        KIRQL irql = 0;
        KAFFINITY affinity = 0;
        ULONG vector;

        irq = order[k];
        if (pick->Reason[irq] != MSGIRQ_ISA_NOT_TRIED) {
            continue;
        }
        vector = MsgIrqpG.IsaVector(Isa, 0, irq, irq, &irql, &affinity);
        MSI4NT_TRACE(("msi4nt: isa irq %lu vector %lx irql %lu affinity %lx\n", irq, vector,
                      (ULONG)irql, (ULONG)affinity));
        pick->Vector[irq] = vector;
        pick->Irql[irq] = irql;
        pick->Affinity[irq] = affinity;
        if (vector == 0 || affinity == 0) {
            pick->Reason[irq] = MSGIRQ_ISA_TRANSLATE;
            pick->Detail[irq] = vector;
            continue;
        }
        if (!MsgIrqpLineSound(vector, irql)) {
            pick->Reason[irq] = MSGIRQ_ISA_TRANSLATE;
            pick->Detail[irq] = vector;
            return MSGIRQ_STATUS_HAL_UNSUPPORTED;
        }
    }
    for (k = 0; k < count && pick->Reason[order[k]] != MSGIRQ_ISA_NOT_TRIED; k++) {
    }
    if (k == count) {
        return MSGIRQ_STATUS_NO_VECTORS;
    }

    I = MsgIrqpIsaNew(run, (BOOLEAN)((run->Options->Flags & MSGIRQ_ISA_FLAG_SHARE) != 0));
    if (I == NULL) {
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    status = MSGIRQ_STATUS_NO_VECTORS;
    reported = FALSE;
    kept = FALSE;
    for (k = 0; k < count; k++) {
        irq = order[k];
        if (pick->Reason[irq] != MSGIRQ_ISA_NOT_TRIED) {
            continue;
        }
        ns = MsgIrqpIsaReport(I, irq, &conflict);
        MSI4NT_TRACE(("msi4nt: isa claim irq %lu status %08lx conflict %lu\n", irq, (ULONG)ns,
                      (ULONG)conflict));
        if (conflict || ns == STATUS_CONFLICTING_ADDRESSES) {
            /* A failed report with the conflict flag holds nothing. */
            if (NT_SUCCESS(ns)) {
                reported = TRUE;                        /* withdrawn below */
            }
            pick->Reason[irq] = MSGIRQ_ISA_CONFLICT;
            pick->Detail[irq] = (ULONG)ns;
        } else if (ns == STATUS_INSUFFICIENT_RESOURCES || ns == STATUS_NO_MEMORY) {
            pick->Detail[irq] = (ULONG)ns;
            status = MSGIRQ_STATUS_NO_MEMORY;
            MSGIRQP_CLAIM_STOP(irq, ns);
            break;
        } else if (!NT_SUCCESS(ns)) {
            pick->Detail[irq] = (ULONG)ns;
            status = MSGIRQ_STATUS_UNSUPPORTED;         /* the list itself refused */
            MSGIRQP_CLAIM_STOP(irq, ns);
            break;
        } else {
            reported = TRUE;
            status = MsgIrqpIsaHear(I, pick, irq, run->Quiet);
            if (status == MSGIRQ_STATUS_OK) {
                pick->Reason[irq] = MSGIRQ_ISA_CHOSEN;
                pick->Detail[irq] = pick->Vector[irq];
                I->Irq = irq;
                break;
            }
            if (status == MSGIRQ_STATUS_NO_MEMORY) {
                break;
            }
            status = MSGIRQ_STATUS_NO_VECTORS;
        }
        if (reported) {
            ns = MsgIrqpIsaReport(I, MSGIRQP_ISA_RELEASE, &conflict);
            MSI4NT_TRACE(("msi4nt: isa withdraw irq %lu status %08lx\n", irq, (ULONG)ns));
            if (!NT_SUCCESS(ns)) {
                I->Irq = irq;
                kept = TRUE;                            /* section 2.2, step 5.4 */
                break;
            }
            reported = FALSE;
        }
    }
    if (status != MSGIRQ_STATUS_OK && reported && !kept) {
        /* The claim stopped early (NO_MEMORY, UNSUPPORTED) with its last
         * report still in place: withdraw it. */
        ns = MsgIrqpIsaReport(I, MSGIRQP_ISA_RELEASE, &conflict);
        if (NT_SUCCESS(ns)) {
            reported = FALSE;
        } else {
            I->Irq = irq;
        }
    }
    if (status != MSGIRQ_STATUS_OK && reported) {
        /* A withdrawal that failed: the IRQ stays reserved by its owner
         * for the rest of the boot, and the function gets no other claim. */
        run->Claim = I;
        return MSGIRQ_STATUS_CLAIM_KEPT;
    }
    if (status != MSGIRQ_STATUS_OK) {
        MsgIrqpG.IsaDelete(I->Owner);
        MsgIrqpFree(I);
        return status;
    }
    I->Line.Vector = pick->Vector[I->Irq];
    I->Line.Irql = pick->Irql[I->Irq];
    I->Line.Affinity = pick->Affinity[I->Irq];
    run->Claim = I;
    return MSGIRQ_STATUS_OK;
}

/* The upper pool's four operations (section 2.9), for MsgIrqpHighClaim. */
static ULONG MsgIrqpHighTranslate(PVOID Context, ULONG Input, ULONG *Detail)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;
    KIRQL irql = 0;
    KAFFINITY affinity = 0;
    ULONG vector;

    vector = MsgIrqpG.IsaVector(PCIBus, 0, Input, Input, &irql, &affinity);
    *Detail = vector;
    run->Vector = 0;
    if (vector == 0) {
        return MSGIRQP_OP_NONE;                         /* no such input */
    }
    MSI4NT_TRACE(("msi4nt: high input %lu vector %lx irql %lu affinity %lx\n", Input, vector,
                  (ULONG)irql, (ULONG)affinity));
    /* The soundness of the vector first: it says what state the HAL is
     * in, whether or not a processor is given for this input. */
    if (!MsgIrqpLineSound(vector, irql)) {
        return MSGIRQP_OP_NOT_APIC;
    }
    if (affinity == 0) {
        return MSGIRQP_OP_NONE;
    }
    run->Vector = vector;
    run->Irql = irql;
    run->Affinity = affinity;
    return MSGIRQP_OP_OK;
}

static ULONG MsgIrqpHighReport(PVOID Context, ULONG Input, ULONG *Detail)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;
    BOOLEAN conflict;
    NTSTATUS ns;

    if (run->Claim == NULL) {
        /* The claim's record and owner, at the first input that exists:
         * never joinable, whatever the flag (section 2.9). */
        run->Claim = MsgIrqpIsaNew(run, FALSE);
        if (run->Claim == NULL) {
            *Detail = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
            return MSGIRQP_OP_NO_MEMORY;
        }
    }
    ns = MsgIrqpIsaReport(run->Claim, Input, &conflict);
    MSI4NT_TRACE(("msi4nt: high claim input %lu status %08lx conflict %lu\n", Input, (ULONG)ns,
                  (ULONG)conflict));
    *Detail = (ULONG)ns;
    if (conflict || ns == STATUS_CONFLICTING_ADDRESSES) {
        /* A failed report with the conflict flag holds nothing. */
        return NT_SUCCESS(ns) ? MSGIRQP_OP_HELD : MSGIRQP_OP_CONFLICT;
    }
    if (ns == STATUS_INSUFFICIENT_RESOURCES || ns == STATUS_NO_MEMORY) {
        MSGIRQP_CLAIM_STOP(Input, ns);
        return MSGIRQP_OP_NO_MEMORY;
    }
    if (!NT_SUCCESS(ns)) {
        MSGIRQP_CLAIM_STOP(Input, ns);
        return MSGIRQP_OP_REFUSED;
    }
    return MSGIRQP_OP_OK;
}

static ULONG MsgIrqpHighWithdraw(PVOID Context, ULONG Input, ULONG *Detail)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;
    BOOLEAN conflict;
    NTSTATUS ns;

    ns = MsgIrqpIsaReport(run->Claim, MSGIRQP_ISA_RELEASE, &conflict);
    MSI4NT_TRACE(("msi4nt: high withdraw input %lu status %08lx\n", Input, (ULONG)ns));
    *Detail = (ULONG)ns;
    return NT_SUCCESS(ns) ? MSGIRQP_OP_OK : MSGIRQP_OP_REFUSED;
}

static ULONG MsgIrqpHighListen(PVOID Context, ULONG Input, ULONG *Detail)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;

    /* The line is the one translation MsgIrqpHighClaim made of this
     * input, just before its report. */
    return MsgIrqpIsaListenOn(run->Claim, Input, run->Vector, run->Irql, run->Affinity, run->Quiet,
                              Detail);
}

/* The driver's record of upper translations (section 2.9): the key
 * \Registry\Machine\HARDWARE\msi4nt\<service>, value HighInputs. */
static const WCHAR MsgIrqpLedgerRoot[] = L"\\Registry\\Machine\\HARDWARE\\msi4nt";
static const WCHAR MsgIrqpLedgerValue[] = L"HighInputs";

static ULONG MsgIrqpHighPersist(PVOID Context, const MSGIRQP_HIGH_SPENT *Spent, ULONG *Detail)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;
    UCHAR bytes[MSGIRQP_LEDGER_BYTES];
    UNICODE_STRING name;
    NTSTATUS ns;

    MsgIrqpLedgerStore(Spent, bytes);
    name.Buffer = (PWSTR)MsgIrqpLedgerValue;
    name.Length = (USHORT)(sizeof(MsgIrqpLedgerValue) - sizeof(WCHAR));
    name.MaximumLength = (USHORT)sizeof(MsgIrqpLedgerValue);
    ns = MsgIrqpG.IsaSetValue(run->Ledger, &name, 0, REG_BINARY, bytes, MSGIRQP_LEDGER_BYTES);
    *Detail = (ULONG)ns;
    if (!NT_SUCCESS(ns)) {
        MSI4NT_TRACE(("msi4nt: high record write %08lx\n", (ULONG)ns));
        return MSGIRQP_OP_REFUSED;
    }
    return MSGIRQP_OP_OK;
}

/* Opens (creating, volatile) the driver's record and reads it into
 * *Spent, united with what this image holds; a record that lacks a bit
 * of the union is written first. MSGIRQ_STATUS_OK with Run->Ledger open,
 * or MSGIRQ_STATUS_UNSUPPORTED with *Detail the status and nothing open
 * (a malformed record: STATUS_DATA_ERROR). */
static ULONG MsgIrqpHighLedgerOpen(MSGIRQP_CLAIM_RUN *Run, MSGIRQP_HIGH_SPENT *Spent, ULONG *Detail)
{
    const UNICODE_STRING *path = Run->Options->RegistryPath;
    ULONG info[16];                     /* aligned; the value info and 32 bytes fit */
    KEY_VALUE_PARTIAL_INFORMATION *v = (KEY_VALUE_PARTIAL_INFORMATION *)info;
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    HANDLE root = NULL;
    ULONG disposition;
    ULONG got = 0;
    ULONG k;
    NTSTATUS ns;

    Run->Ledger = NULL;
    u.Buffer = (PWSTR)MsgIrqpLedgerRoot;
    u.Length = (USHORT)(sizeof(MsgIrqpLedgerRoot) - sizeof(WCHAR));
    u.MaximumLength = (USHORT)sizeof(MsgIrqpLedgerRoot);
    InitializeObjectAttributes(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    ns = MsgIrqpG.IsaCreateKey(&root, KEY_CREATE_SUB_KEY, &oa, 0, NULL, REG_OPTION_VOLATILE,
                               &disposition);
    if (NT_SUCCESS(ns)) {
        u.Buffer = path->Buffer + Run->Pools->NameStart;
        u.Length = (USHORT)(Run->Pools->NameChars * sizeof(WCHAR));
        u.MaximumLength = u.Length;
        InitializeObjectAttributes(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, root, NULL);
        ns = MsgIrqpG.IsaCreateKey(&Run->Ledger, KEY_QUERY_VALUE | KEY_SET_VALUE, &oa, 0, NULL,
                                   REG_OPTION_VOLATILE, &disposition);
        MsgIrqpG.IsaClose(root);
    }
    if (!NT_SUCCESS(ns)) {
        Run->Ledger = NULL;
        *Detail = (ULONG)ns;
        MSI4NT_TRACE(("msi4nt: high record open %08lx\n", (ULONG)ns));
        return MSGIRQ_STATUS_UNSUPPORTED;
    }
    u.Buffer = (PWSTR)MsgIrqpLedgerValue;
    u.Length = (USHORT)(sizeof(MsgIrqpLedgerValue) - sizeof(WCHAR));
    u.MaximumLength = (USHORT)sizeof(MsgIrqpLedgerValue);
    for (k = 0; k < sizeof(info) / sizeof(info[0]); k++) {
        info[k] = 0;
    }
    ns = MsgIrqpG.IsaQueryValue(Run->Ledger, &u, KeyValuePartialInformation, v,
                                FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + MSGIRQP_LEDGER_BYTES,
                                &got);
    if (ns == STATUS_OBJECT_NAME_NOT_FOUND) {
        for (k = 0; k < MSGIRQP_LEDGER_BYTES; k++) {
            v->Data[k] = 0;                             /* no record yet: nothing charged */
        }
        ns = STATUS_SUCCESS;
    } else if (NT_SUCCESS(ns) &&
               (v->Type != REG_BINARY || v->DataLength != MSGIRQP_LEDGER_BYTES)) {
        ns = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(ns) && !MsgIrqpLedgerLoad(Spent, v->Data)) {
        ns = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(ns) && MsgIrqpLedgerUnion(Spent, &MsgIrqpG.HighSpent) &&
        MsgIrqpHighPersist(Run, Spent, Detail) != MSGIRQP_OP_OK) {
        ns = (NTSTATUS)*Detail;                         /* the union could not be written */
    }
    MSI4NT_TRACE(("msi4nt: high record status %08lx, %lu inputs charged\n", (ULONG)ns, Spent->Count));
    if (!NT_SUCCESS(ns)) {
        MsgIrqpG.IsaClose(Run->Ledger);
        Run->Ledger = NULL;
        *Detail = (ULONG)ns;
        return MSGIRQ_STATUS_UNSUPPORTED;
    }
    return MSGIRQ_STATUS_OK;
}

/* The upper pool's attempt (section 2.9): an input above 23 of the
 * function's own, always exclusive. MSGIRQ_STATUS_OK with Run->Claim
 * holding the input and its line; NO_INPUTS when the pool has no input
 * for this call; NO_VECTORS, HAL_UNSUPPORTED, NO_MEMORY or UNSUPPORTED
 * with nothing held (UNSUPPORTED also when the driver's record could not
 * be opened, read or written); CLAIM_KEPT with Run->Claim the claim whose
 * withdrawal failed. Lock held. */
static ULONG MsgIrqpHighStep(PVOID Context)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;
    MSGIRQP_HIGH *H = run->High;
    MSGIRQP_HIGH_OPS ops;
    MSGIRQP_HIGH_SPENT spent;
    MSGIRQP_ISA *I;
    ULONG input = 0;
    ULONG fresh = 0;
    ULONG detail = 0;
    ULONG status;

    ops.Context = run;
    ops.Translate = MsgIrqpHighTranslate;
    ops.Report = MsgIrqpHighReport;
    ops.Withdraw = MsgIrqpHighWithdraw;
    ops.Listen = MsgIrqpHighListen;
    ops.Persist = MsgIrqpHighPersist;
    run->Claim = NULL;
    H->Budget = run->Pools->Budget;
    H->New = 0;
    H->Spent = 0;
    spent.Count = 0;
    for (input = 0; input < 8; input++) {
        spent.Inputs[input] = 0;
    }
    input = 0;
    status = MsgIrqpHighLedgerOpen(run, &spent, &detail);
    if (status != MSGIRQ_STATUS_OK) {
        /* Nothing of the upper pool is translated; the result still says
         * what the sources say. */
        MsgIrqpHighReasons(H, (BOOLEAN)((run->Options->Flags & MSGIRQ_ISA_FLAG_OVERRIDE) != 0));
        H->End = MSGIRQP_HIGH_NONE;
        H->LastInput = 0;
        H->Detail = detail;
        H->Spent = MsgIrqpG.HighSpent.Count;
        return status;
    }
    status = MsgIrqpHighClaim(H, &ops, &spent, run->Pools->Budget,
                              (BOOLEAN)((run->Options->Flags & MSGIRQ_ISA_FLAG_OVERRIDE) != 0),
                              run->Pools->HighOrder, run->Pools->HighCount, &input, &fresh);
    MsgIrqpG.IsaClose(run->Ledger);
    run->Ledger = NULL;
    MsgIrqpG.HighSpent = spent;         /* a superset of what the image held */
    H->New = fresh;
    H->Spent = spent.Count;
    MSI4NT_TRACE(("msi4nt: high range %lu-%lu ioapics %lx of %lu end %lx input %lu status %lu; "
                  "budget %lu, %lu newly charged, %lu charged for the boot\n", H->First, H->Last,
                  H->IoApics, H->ApicCount, H->End, input, status, H->Budget, fresh, spent.Count));
    I = run->Claim;
    if (I == NULL) {
        return status;                      /* nothing was reported */
    }
    I->Irq = input;
    if (status == MSGIRQ_STATUS_OK) {
        I->Line.Vector = run->Vector;
        I->Line.Irql = run->Irql;
        I->Line.Affinity = run->Affinity;
        return status;
    }
    if (status == MSGIRQ_STATUS_CLAIM_KEPT) {
        return status;
    }
    run->Claim = NULL;
    MsgIrqpG.IsaDelete(I->Owner);
    MsgIrqpFree(I);
    return status;
}

/* The join (section 2.8), when no interrupt of the function's own was to
 * be had. Lock held. */
static ULONG MsgIrqpJoinStep(PVOID Context)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;

    run->Claim = MsgIrqpIsaJoin(run->Options, run->Cap, run->Pools->IsaOrder, run->Pools->IsaCount,
                                run->Pick, FALSE);
    return (run->Claim != NULL) ? MSGIRQ_STATUS_OK : MSGIRQ_STATUS_NO_VECTORS;
}

/* Shared line first (section 2.12): the join before an interrupt of the
 * function's own is tried. Lock held. */
static ULONG MsgIrqpJoinFirstStep(PVOID Context)
{
    MSGIRQP_CLAIM_RUN *run = (MSGIRQP_CLAIM_RUN *)Context;

    run->Claim = MsgIrqpIsaJoin(run->Options, run->Cap, run->Pools->IsaOrder, run->Pools->IsaCount,
                                run->Pick, TRUE);
    return (run->Claim != NULL) ? MSGIRQ_STATUS_OK : MSGIRQ_STATUS_NO_VECTORS;
}

static ULONG MsgIrqpIsaClaimCall(const MSGIRQ_CONNECT_PARAMS *Params, const MSGIRQ_ISA_PARAMS *Options,
                                 MSGIRQ_ISA_RESULT *Result)
{
    MSGIRQP_ISA_PICK pick;
    MSGIRQP_POOLS pools;
    MSGIRQP_CLAIM_RUN run;
    MSGIRQP_HIGH *high = NULL;
    ULONG quiet;
    ULONG brake;
    ULONG cap;
    ULONG k;
    ULONG status;
    ULONG scan;
    ULONG scanned = 0;
    ULONG from = MSGIRQP_FROM_NONE;
    MSGIRQP_ISA *I;
    MSGIRQP_ISA_MEMBER *m;
    MSGIRQP_ISA_MEMBER *nm;

    if (!MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
    /* A structure that ends before the second pool's fields is an earlier
     * client's: its claim is for the ISA IRQs, and nothing is written
     * past the result it has (section 2.9). */
    if (Params == NULL || Options == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !MsgIrqpIsaSizes(Options->Size, (Result != NULL) ? Result->Size : 0,
                         (BOOLEAN)(Result != NULL)) ||
        Options->DriverObject == NULL || !MsgIrqpIsaFlagsOk(Options->Flags) ||
        !MsgIrqpPoolCheck(Options, &pools) ||
        (Options->QuietMilliseconds != 0 &&
         (Options->QuietMilliseconds < MSGIRQP_ISA_MIN_QUIET ||
          Options->QuietMilliseconds > MSGIRQP_ISA_MAX_QUIET)) ||
        (Options->BrakeEntries != 0 && Options->BrakeEntries != MSGIRQ_ISA_BRAKE_OFF &&
         (Options->BrakeEntries < MSGIRQP_ISA_MIN_BRAKE ||
          Options->BrakeEntries > MSGIRQP_ISA_MAX_BRAKE)) ||
        (Options->Exclude & ~0xFFFFUL) != 0 || !MsgIrqpIsaCap(Options->MaxMembers, &cap) ||
        Params->MessageBased.Bus > 255 || Params->MessageBased.Dev > 31 ||
        Params->MessageBased.Func > 7) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    if (Result != NULL) {
        Result->Irq = 0;
        Result->Sci = 0xFFFFFFFFUL;
        Result->Sources = 0;
        Result->Members = 0;
        for (k = 0; k < 16; k++) {
            Result->Reason[k] = MSGIRQ_ISA_NOT_TRIED;
            Result->Detail[k] = 0;
        }
        if (Result->Size >= sizeof(MSGIRQ_ISA_RESULT)) {
            Result->PoolsScanned = 0;
            Result->HighFirst = 0;
            Result->HighLast = 0;
            Result->HighIoApics = 0;
            Result->HighIoApicCount = 0;
            Result->HighEnd = MSGIRQP_HIGH_NONE;
            Result->HighLastInput = 0;
            Result->HighDetail = 0;
            for (k = 0; k < 256; k++) {
                Result->HighReason[k] = MSGIRQ_ISA_NOT_TRIED;
            }
            Result->HighBudget = 0;
            Result->HighNew = 0;
            Result->HighSpent = 0;
        }
    }
    status = MsgIrqpIsaContext();
    if (status != MSGIRQ_STATUS_OK) {
        return status;
    }
    if ((pools.Pools & MSGIRQ_POOL_HIGH) &&
        (MsgIrqpG.IsaCreateKey == NULL || MsgIrqpG.IsaSetValue == NULL)) {
        return MSGIRQ_STATUS_UNSUPPORTED;               /* no record of translations: section 2.6 */
    }
    quiet = Options->QuietMilliseconds ? Options->QuietMilliseconds : MSGIRQP_ISA_DEFAULT_QUIET;
    brake = Options->BrakeEntries ? Options->BrakeEntries : MSGIRQP_ISA_DEFAULT_BRAKE;
    if (brake == MSGIRQ_ISA_BRAKE_OFF) {
        brake = 0;
    }

    MsgIrqpLock();
    m = MsgIrqpIsaMember(Params->MessageBased.Bus, Params->MessageBased.Dev,
                         Params->MessageBased.Func);
    if (m != NULL) {
        status = (m->Claim->State == MSGIRQP_ISA_KEPT) ? MSGIRQ_STATUS_CLAIM_KEPT
                                                       : MSGIRQ_STATUS_IN_USE;
        MsgIrqpUnlock();
        return status;
    }
    /* The membership before anything is reported: a claim is never kept
     * without one, and NO_MEMORY here holds nothing. The upper pool's
     * scratch likewise, from pool rather than this thread's stack. */
    nm = (MSGIRQP_ISA_MEMBER *)MsgIrqpAlloc(sizeof(MSGIRQP_ISA_MEMBER));
    if (nm == NULL) {
        MsgIrqpUnlock();
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    if (pools.Pools & MSGIRQ_POOL_HIGH) {
        high = (MSGIRQP_HIGH *)MsgIrqpAlloc(sizeof(MSGIRQP_HIGH));
        if (high == NULL) {
            MsgIrqpUnlock();
            MsgIrqpFree(nm);
            return MSGIRQ_STATUS_NO_MEMORY;
        }
        high->End = MSGIRQP_HIGH_NONE;
    }
    if (!MsgIrqpIsaSources(&pick, (BOOLEAN)((Options->Flags & MSGIRQ_ISA_FLAG_OVERRIDE) != 0),
                           Options->Exclude, high, pools.Pools)) {
        MsgIrqpUnlock();
        MsgIrqpFree(nm);
        if (high != NULL) {
            MsgIrqpFree(high);
        }
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    /* The upper pool's scope on this machine, and the last of the
     * parameter checks, which need the MADT: before anything is reported
     * in either pool. */
    if (high != NULL && !MsgIrqpHighScope(high, &pools)) {
        MSI4NT_TRACE(("msi4nt: high ioapics %lx or a candidate outside the %lu the MADT has\n",
                      pools.IoApics, high->ApicCount));
        MsgIrqpUnlock();
        MsgIrqpFree(nm);
        MsgIrqpFree(high);
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }

    run.Options = Options;
    run.Pools = &pools;
    run.Pick = &pick;
    run.High = high;
    run.Quiet = quiet;
    run.Brake = brake;
    run.Cap = cap;
    run.Claim = NULL;
    run.Vector = 0;
    run.Irql = 0;
    run.Affinity = 0;
    run.Ledger = NULL;
    /* A list that is not empty is the whole list across both pools: a
     * chosen pool with no entry in it is not scanned. */
    scan = pools.Pools;
    if (pools.Listed) {
        scan = (pools.IsaCount ? MSGIRQ_POOL_ISA : 0) | (pools.HighCount ? MSGIRQ_POOL_HIGH : 0);
    }
    status = MsgIrqpPoolRun(pools.Pools, scan,
                            (BOOLEAN)((Options->Flags & MSGIRQ_ISA_FLAG_SHARE) != 0),
                            (Options->Flags & MSGIRQ_ISA_FLAG_JOIN_FIRST) ? MsgIrqpJoinFirstStep : NULL,
                            MsgIrqpIsaStep, MsgIrqpHighStep, MsgIrqpJoinStep, &run, &scanned, &from);
    I = run.Claim;
    if (status == MSGIRQ_STATUS_OK && from == MSGIRQP_FROM_JOIN) {
        /* Nothing is reported or listened to again: the claim's own report
         * and owner stay what they were. */
        MsgIrqpIsaAddMember(I, nm, Params);
        if (cap < I->MaxMembers) {
            I->MaxMembers = cap;            /* no member ever sees more than it allowed */
        }
        MsgIrqpIsaThreshold(I);
        MSI4NT_TRACE(("msi4nt: isa joined irq %lu members %lu\n", I->Irq, I->Members));
        MsgIrqpIsaResult(Result, &pick, I->Irq, I->Members);
    } else if (status == MSGIRQ_STATUS_OK) {
        MsgIrqpIsaAddMember(I, nm, Params);
        I->Line.Mode = Latched;
        I->Line.ShareDisposition = I->Shared ? CmResourceShareShared : CmResourceShareDeviceExclusive;
        I->State = MSGIRQP_ISA_HELD;
        MsgIrqpIsaThreshold(I);
        I->Next = MsgIrqpG.IsaList;
        MsgIrqpG.IsaList = I;
        MsgIrqpIsaResult(Result, &pick, I->Irq, I->Members);
    } else if (status == MSGIRQ_STATUS_CLAIM_KEPT && I != NULL) {
        /* A withdrawal that failed: the interrupt stays reserved by its
         * owner for the rest of the boot, and the function gets no other
         * claim. */
        I->State = MSGIRQP_ISA_KEPT;
        I->Next = MsgIrqpG.IsaList;
        MsgIrqpG.IsaList = I;
        MsgIrqpIsaAddMember(I, nm, Params);
        MsgIrqpIsaResult(Result, &pick, 0, I->Members);
    } else {
        MsgIrqpIsaResult(Result, &pick, 0, 0);
        MsgIrqpFree(nm);
    }
    MsgIrqpHighResult(Result, high, scanned);
#if defined(MSGIRQP_CALLS)
    {
        /* The attempt's own digest (record 07, section 5), independent of
         * the caller's Result: it reached the pools. */
        MSGIRQP_PUB_FACTS *d = &MsgIrqpG.PubClaim;

        d->Reached = TRUE;
        d->IsaResult[0] = (status == MSGIRQ_STATUS_OK && I != NULL) ? I->Irq : 0;
        d->IsaResult[1] = pick.Sci;
        d->IsaResult[2] = pick.Sources;
        d->IsaResult[3] = (I != NULL && (status == MSGIRQ_STATUS_OK || status == MSGIRQ_STATUS_CLAIM_KEPT))
                              ? I->Members : 0;
        d->IsaResult[4] = scanned;
        d->IsaResult[5] = (high != NULL && (scanned & MSGIRQ_POOL_HIGH)) ? high->End : MSGIRQP_HIGH_NONE;
        d->IsaResult[6] = (high != NULL && (scanned & MSGIRQ_POOL_HIGH)) ? high->LastInput : 0;
        d->IsaResult[7] = (high != NULL && (scanned & MSGIRQ_POOL_HIGH)) ? high->Detail : 0;
        for (k = 0; k < 16; k++) {
            d->IsaReason[k] = pick.Reason[k];
        }
        d->Budget = (high != NULL && (scanned & MSGIRQ_POOL_HIGH)) ? high->Budget : 0;
        d->BudgetUsed = (high != NULL && (scanned & MSGIRQ_POOL_HIGH)) ? high->Spent : 0;
#if defined(MSGIRQP_HISTORY)
        MsgIrqpG.HistFrom = (status == MSGIRQ_STATUS_OK) ? from : 0;
        MsgIrqpG.HistFlags = Options->Flags;
        MsgIrqpG.HistNew = (high != NULL && (scanned & MSGIRQ_POOL_HIGH)) ? high->New : 0;
#endif
    }
#endif
    MsgIrqpUnlock();
    if (high != NULL) {
        MsgIrqpFree(high);
    }
    return status;
}

#if defined(MSGIRQP_CALLS)
/* A claim's facts (record 07, section 4), after the call. */
static VOID MsgIrqpPubClaimFacts(const MSGIRQ_CONNECT_PARAMS *P, const MSGIRQ_ISA_PARAMS *O, ULONG Status)
{
    MSGIRQP_PUB_FACTS f;
    const MSGIRQP_PUB_FACTS *d = &MsgIrqpG.PubClaim;
    ULONG k;

    MsgIrqpPubFacts(&f, MSGIRQP_PUB_CLAIM, Status);
    f.Reached = d->Reached;
    f.Fresh = (BOOLEAN)(Status == MSGIRQ_STATUS_CLAIM_KEPT && d->Reached);
    f.Budget = d->Budget;
    f.BudgetUsed = d->BudgetUsed;
    for (k = 0; k < 8; k++) {
        f.IsaResult[k] = d->IsaResult[k];
    }
    for (k = 0; k < 16; k++) {
        f.IsaReason[k] = d->IsaReason[k];
    }
    /* The driver's name only from options a claim was installed with:
     * those were validated (a rejected caller's may be anything). */
    if ((Status == MSGIRQ_STATUS_OK || f.Fresh) && O != NULL && O->DriverObject != NULL) {
        MsgIrqpPubDriverName(f.DriverName, O->DriverObject->DriverName.Buffer,
                             O->DriverObject->DriverName.Length / sizeof(WCHAR));
    }
    MsgIrqpPubPost(P->MessageBased.Bus, P->MessageBased.Dev, P->MessageBased.Func, &f);
}
#endif

ULONG MsgIrqIsaClaim(const MSGIRQ_CONNECT_PARAMS *Params, const MSGIRQ_ISA_PARAMS *Options,
                     MSGIRQ_ISA_RESULT *Result)
{
#if defined(MSGIRQP_CALLS)
    ULONG status;

    if (MsgIrqpG.Initialized && Params != NULL && KeGetCurrentIrql() == PASSIVE_LEVEL &&
        MsgIrqpPubFunction(Params->MessageBased.Bus, Params->MessageBased.Dev,
                           Params->MessageBased.Func)) {
        MsgIrqpPubBegin();
        MsgIrqpPubZero(&MsgIrqpG.PubClaim, sizeof(MsgIrqpG.PubClaim));
        status = MsgIrqpIsaClaimCall(Params, Options, Result);
        MsgIrqpPubClaimFacts(Params, Options, status);
        MsgIrqpPubEnd();
        return status;
    }
    status = MsgIrqpIsaClaimCall(Params, Options, Result);
    if (MsgIrqpG.Initialized) {
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_ISA_CLAIM, status);
    }
    return status;
#else
    return MsgIrqpIsaClaimCall(Params, Options, Result);
#endif
}

static ULONG MsgIrqpIsaReleaseCall(const MSGIRQ_CONNECT_PARAMS *Params)
{
    MSGIRQP_ISA_MEMBER *m;
    MSGIRQP_ISA *I;
    MSGIRQP_CONN *c;
    BOOLEAN conflict;
    NTSTATUS ns;
    ULONG status;

    if (!MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
    if (Params == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    status = MsgIrqpIsaContext();
    if (status != MSGIRQ_STATUS_OK) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;         /* no claim can exist here; section 2.6 */
    }
    MsgIrqpLock();
    m = MsgIrqpIsaMember(Params->MessageBased.Bus, Params->MessageBased.Dev,
                         Params->MessageBased.Func);
    if (m == NULL) {
        MsgIrqpUnlock();
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    I = m->Claim;
#if defined(MSGIRQP_HISTORY)
    MsgIrqpG.HistIrq = I->Irq;
#endif
    if (I->State == MSGIRQP_ISA_KEPT) {
        MsgIrqpUnlock();
        return MSGIRQ_STATUS_CLAIM_KEPT;
    }
    for (c = MsgIrqpG.List; c != NULL; c = c->Next) {
        if (c->Isa == I && c->Route.Bus == m->Bus && c->Route.Dev == m->Dev &&
            c->Route.Func == m->Func) {
            MsgIrqpUnlock();
            return MSGIRQ_STATUS_IN_USE;                /* disconnect first */
        }
    }
    if (I->Members > 1) {
        MsgIrqpIsaDropMember(m);
        I->Members--;
        MsgIrqpIsaThreshold(I);
        MSI4NT_TRACE(("msi4nt: isa left irq %lu members %lu\n", I->Irq, I->Members));
#if defined(MSGIRQP_CALLS)
        MsgIrqpG.PubMembersLeft = I->Members;
#endif
        MsgIrqpUnlock();
        return MSGIRQ_STATUS_OK;
    }
    ns = MsgIrqpIsaReport(I, MSGIRQP_ISA_RELEASE, &conflict);
    MSI4NT_TRACE(("msi4nt: isa release irq %lu status %08lx\n", I->Irq, (ULONG)ns));
    if (!NT_SUCCESS(ns)) {
        I->State = MSGIRQP_ISA_KEPT;
        MsgIrqpUnlock();
        return MSGIRQ_STATUS_CLAIM_KEPT;
    }
    MsgIrqpIsaDropMember(m);
    MsgIrqpIsaUnlist(I);
    MsgIrqpG.IsaDelete(I->Owner);
    MsgIrqpFree(I);
    MsgIrqpUnlock();
    return MSGIRQ_STATUS_OK;
}

ULONG MsgIrqIsaRelease(const MSGIRQ_CONNECT_PARAMS *Params)
{
#if defined(MSGIRQP_CALLS)
    MSGIRQP_PUB_FACTS f;
    ULONG status;

    if (MsgIrqpG.Initialized && Params != NULL && KeGetCurrentIrql() == PASSIVE_LEVEL &&
        MsgIrqpPubFunction(Params->MessageBased.Bus, Params->MessageBased.Dev,
                           Params->MessageBased.Func)) {
        MsgIrqpPubBegin();
        MsgIrqpG.PubMembersLeft = 0;
        status = MsgIrqpIsaReleaseCall(Params);
        MsgIrqpPubFacts(&f, MSGIRQP_PUB_RELEASE, status);
        f.MembersLeft = MsgIrqpG.PubMembersLeft;
        MsgIrqpPubPost(Params->MessageBased.Bus, Params->MessageBased.Dev, Params->MessageBased.Func,
                       &f);
        MsgIrqpPubEnd();
        return status;
    }
    status = MsgIrqpIsaReleaseCall(Params);
    if (MsgIrqpG.Initialized) {
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_ISA_RELEASE, status);
    }
    return status;
#else
    return MsgIrqpIsaReleaseCall(Params);
#endif
}

ULONG MsgIrqIsaQuery(PMSGIRQ_MESSAGE_INFO Info, MSGIRQ_ISA_STATE *State)
{
    MSGIRQP_CONN *c;

    if (!MsgIrqpG.Initialized) {
        return MSGIRQ_STATUS_NOT_INITIALIZED;
    }
    if (Info == NULL || State == NULL || State->Size < MSGIRQ_ISA_STATE_V1_SIZE ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) {
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_ISA_QUERY, MSGIRQ_STATUS_INVALID_PARAMETER);
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    MsgIrqpLock();
    c = MsgIrqpFind(Info);
    if (c == NULL || c->Path != MSGIRQP_PATH_FAKE || c->Isa == NULL) {
        MsgIrqpUnlock();
        MSGIRQP_EV_EARLY(MSGIRQ_CALL_ISA_QUERY, MSGIRQ_STATUS_INVALID_PARAMETER);
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    State->Braked = c->Isa->Brake.Braked ? 1UL : 0UL;
    State->Entries = c->Isa->Brake.Entries;
    State->Stray = c->Isa->Brake.Stray;
    State->Run = (ULONG)c->Isa->Brake.Run;
    if (State->Size >= sizeof(MSGIRQ_ISA_STATE)) {
        State->MemberEntries = c->Tramp->Entries;
        State->MemberStray = c->Tramp->Stray;
    }
    MsgIrqpUnlock();
    MSGIRQP_HIST_CALM();                           /* a calm point that never waits */
    return MSGIRQ_STATUS_OK;
}
#endif /* MSI4NT_ISA_LINE */

/* The client's read of the history (record 08, section 4). Any IRQL: each
 * record is copied under the ring's flag into a local one and stored into
 * the caller's buffer after the flag is given back, at the caller's IRQL. */
ULONG MsgIrqReadHistory(ULONG After, MSGIRQ_EVENT *Events, ULONG MaxEvents, MSGIRQ_HISTORY *History)
{
#if defined(MSGIRQP_HISTORY)
    MSGIRQ_EVENT e;
    ULONG written;
    ULONG held;
    ULONG newest;
    ULONG over;
    ULONG last;
    ULONG skipped = 0;
    ULONG count = 0;
    ULONG p;
    BOOLEAN have;
    KIRQL old;

    if (History == NULL || History->Size < sizeof(MSGIRQ_HISTORY) ||
        (Events == NULL && MaxEvents != 0)) {
        if (MsgIrqpG.Initialized) {
            MSGIRQP_EV_EARLY(MSGIRQ_CALL_READ_HISTORY, MSGIRQ_STATUS_INVALID_PARAMETER);
        }
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    if (MsgIrqpG.Initialized) {
        MSGIRQP_HIST_CALM();                       /* a calm point that does not wait */
    }
    old = MsgIrqpRingEnter();
    written = MsgIrqpG.Ring.Written;
    held = MsgIrqpG.Ring.Held;
    newest = MsgIrqpG.Ring.Sequence;
    over = MsgIrqpG.Ring.Overwritten;
    last = MsgIrqpG.Ring.LastOverwritten;
    MsgIrqpRingLeave(old);
    if (After != 0) {
        skipped = MsgIrqpRingSkipped(over, last, After);
    } else {
        skipped = over;                                 /* everything before the oldest */
    }
    for (p = written - held; p != written && count < MaxEvents; p++) {
        old = MsgIrqpRingEnter();
        have = MsgIrqpRingAt(&MsgIrqpG.Ring, p, &e);
        MsgIrqpRingLeave(old);
        if (!have) {
            skipped = MsgIrqpSatAdd(skipped, 1);        /* overwritten while this read ran */
            continue;
        }
        if (After == 0 || MsgIrqpSeqAfter(e.Sequence, After)) {
            Events[count++] = e;
        }
    }
    History->Capacity = (ULONG)MSI4NT_HISTORY_EVENTS;
    History->Newest = newest;
    History->Count = count;
    History->Skipped = skipped;
    History->TimeUnit = MsgIrqpHistUnit();
    History->Overwritten = over;
    History->Written = written;
    return MSGIRQ_STATUS_OK;
#else
    UNREFERENCED_PARAMETER(After);
    UNREFERENCED_PARAMETER(Events);
    UNREFERENCED_PARAMETER(MaxEvents);
    UNREFERENCED_PARAMETER(History);
    return MSGIRQ_STATUS_UNSUPPORTED;
#endif
}

#endif /* !MSI4NT_HOST_TEST */

#ifdef __cplusplus
}
#endif

#endif /* MSI4NT_IMPLEMENTATION */
