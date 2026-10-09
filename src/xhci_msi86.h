/*
 * xhci_msi86.h - the channel between xhci98.sys and the NT 6 x86 MSI
 * helper, xhcimsi.sys (design record 18 section 3).
 *
 * The helper imports IoConnectInterruptEx and IoDisconnectInterruptEx and
 * hands their addresses to the HCD's x86 image, which cannot import them
 * because the same binary loads on Windows 98. The HCD opens the helper's
 * named device with IoGetDeviceObjectPointer and sends one internal device
 * control, METHOD_NEITHER, whose Type3InputBuffer is the block below, in
 * and out. This header is the one declaration both binaries compile.
 *
 * The pure halves of both ends live in xhci_msi86.c: the helper's
 * validation and fill (XhciMsi86Answer) and the HCD's acceptance
 * (XhciMsi86Accept), host-tested in test\test_msi.c.
 *
 * Built three ways: into xhci98.sys (MSVC 6 and the Windows 2000 DDK, or
 * WDK 7.1 for amd64, where nothing calls it), into xhcimsi.sys (WDK 7.1
 * x86, XHCI_MSI86_HELPER defined, ntddk.h included first), and into the
 * host suite (XHCI_HOST_TEST). The block's layout is x86's; the size and
 * offset asserts below hold for x86 only and are compiled there.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_MSI86_H
#define XHCI_MSI86_H

#ifdef XHCI_MSI86_HELPER
#include <ntddk.h>
#define XHCI_MSI86_ASSERT(name, cond) \
    typedef char xhci_msi86_assert_##name[(cond) ? 1 : -1]
#else
#include "xhci_compat.h"
#define XHCI_MSI86_ASSERT(name, cond) XHCI_C_ASSERT(name, cond)
#endif

/* \Device\Xhci98MsiHelper, and the helper's class GUID for
 * IoCreateDeviceSecure: {6B0C1E52-7A41-4E9C-9D3E-58C8A2F1B6A4}. */
#define XHCI_MSI86_DEVICE_NAME      L"\\Device\\Xhci98MsiHelper"

/* 'XMSI'. */
#define XHCI_MSI86_MAGIC            0x49534D58UL
#define XHCI_MSI86_MAJOR            1
#define XHCI_MSI86_MINOR            0

/* The whole V1 block, and the least any later minor may send. */
#define XHCI_MSI86_V1_MIN           0x28UL

/* CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900 + major, METHOD_NEITHER,
 * FILE_ANY_ACCESS): (0x22 << 16) | (0 << 14) | (0x901 << 2) | 3. A new
 * major takes a new function number, so an older helper refuses it. */
#define XHCI_MSI86_IOCTL_QUERY_V1   0x00222407UL

/* Flags the helper sets. */
#define XHCI_MSI86_F_INJECTED       0x00000001UL    /* qemu injection on */

typedef struct _XHCI_MSI86_QUERY {
    ULONG Size;             /* 0x00 caller: the block size it declares     */
    USHORT CallerMajor;     /* 0x04 caller                                 */
    USHORT CallerMinor;     /* 0x06 caller                                 */
    ULONG CallerVersion;    /* 0x08 caller: XHCI_VER packed a.b.c.d        */
    ULONG Magic;            /* 0x0C helper: XHCI_MSI86_MAGIC               */
    USHORT HelperMajor;     /* 0x10 helper                                 */
    USHORT HelperMinor;     /* 0x12 helper                                 */
    ULONG HelperVersion;    /* 0x14 helper: XHCI_VER packed                */
    ULONG Returned;         /* 0x18 helper: bytes it filled, from 0        */
    PVOID ConnectEx;        /* 0x1C helper: IoConnectInterruptEx           */
    PVOID DisconnectEx;     /* 0x20 helper: IoDisconnectInterruptEx        */
    ULONG Flags;            /* 0x24 helper: XHCI_MSI86_F_*                 */
} XHCI_MSI86_QUERY, *PXHCI_MSI86_QUERY;

#if !defined(_WIN64)
XHCI_MSI86_ASSERT(msi86_query_size,
                  sizeof(XHCI_MSI86_QUERY) == XHCI_MSI86_V1_MIN);
XHCI_MSI86_ASSERT(msi86_query_magic,
                  (ULONG)(ULONG_PTR)&((XHCI_MSI86_QUERY *)0)->Magic == 0x0C);
XHCI_MSI86_ASSERT(msi86_query_returned,
                  (ULONG)(ULONG_PTR)&((XHCI_MSI86_QUERY *)0)->Returned ==
                      0x18);
XHCI_MSI86_ASSERT(msi86_query_connect,
                  (ULONG)(ULONG_PTR)&((XHCI_MSI86_QUERY *)0)->ConnectEx ==
                      0x1C);
XHCI_MSI86_ASSERT(msi86_query_flags,
                  (ULONG)(ULONG_PTR)&((XHCI_MSI86_QUERY *)0)->Flags == 0x24);
#endif

/* The helper's answers, which it maps to an NTSTATUS. */
#define XHCI_MSI86_ANS_OK           0UL     /* STATUS_SUCCESS              */
#define XHCI_MSI86_ANS_TOO_SMALL    1UL     /* STATUS_BUFFER_TOO_SMALL     */
#define XHCI_MSI86_ANS_INVALID      2UL     /* STATUS_INVALID_PARAMETER    */
#define XHCI_MSI86_ANS_REVISION     3UL     /* STATUS_REVISION_MISMATCH    */

/* The qemu flavour's injections (record 18 section 2.1) that change the
 * answer; 1 (DriverEntry fails) and 2 (the query refused) are the
 * helper's own. */
#define XHCI_MSI86_INJ_NONE         0UL
#define XHCI_MSI86_INJ_MAJOR2       3UL
#define XHCI_MSI86_INJ_SHORT        4UL
#define XHCI_MSI86_INJ_NULL_DISC    5UL
#define XHCI_MSI86_INJ_MAGIC        6UL

/*
 * The helper's half (record 18 section 3.2): validates the request before it
 * reads any byte of the block - the buffer non-NULL, the two lengths equal
 * and at least V1_MIN - then the block's Size against the length and the
 * caller's major; fills its own fields for min(Size, its block) bytes and
 * returns that count through *information (0 on a refusal). `inject` is an
 * XHCI_MSI86_INJ_* value, XHCI_MSI86_INJ_NONE outside the qemu flavour.
 * Returns an XHCI_MSI86_ANS_*. IRQL: any.
 */
ULONG XhciMsi86Answer(PVOID buffer, ULONG inputLength, ULONG outputLength,
                      PVOID connectEx, PVOID disconnectEx,
                      ULONG helperVersion, ULONG inject,
                      PULONG information);

/* The HCD's acceptance tests, in order (record 18 section 3.2). */
#define XHCI_MSI86_ACCEPTED         0UL
#define XHCI_MSI86_REFUSE_STATUS    1UL     /* the IRP failed              */
#define XHCI_MSI86_REFUSE_LENGTH    2UL     /* Returned/Information/capacity */
#define XHCI_MSI86_REFUSE_IDENTITY  3UL     /* Magic or HelperMajor        */
#define XHCI_MSI86_REFUSE_ROUTINES  4UL     /* a NULL or equal pair        */

/*
 * The HCD's half: `succeeded` is NT_SUCCESS of the IRP's status,
 * `information` its IoStatus.Information, `capacity` the Size the HCD sent.
 * Returns XHCI_MSI86_ACCEPTED or the first test that failed. IRQL: any.
 */
ULONG XhciMsi86Accept(ULONG succeeded, ULONG information, ULONG capacity,
                      const XHCI_MSI86_QUERY *query);

/* "a.b.c.d" packed one byte a part (each part held to 255), for the two
 * ends' version fields. IRQL: any. */
ULONG XhciMsi86PackVersion(const char *text);

#endif /* XHCI_MSI86_H */
