/*
 * hcd_acpi.c - xhci98.sys's firmware tables request, IOCTL_XHCI98_ACPI
 * (xhci_acpi.h; design record 13 section 8.13, design record 32 section
 * 12.8.16; roadmap-usb4 task 46.21, the owner's option B): on 98 SE and
 * ME, the ACPI root pointer, root table, DSDT and every SSDT whole, read
 * on the caller's thread when asked (xhci_acpi.c, XhaReadSet, over this
 * file's physical reader), held as one allocation until its last part has
 * been fetched, a release, or the controller's stop or removal. Nothing of
 * it runs at a start.
 *
 * THE SET. hc->AcpiSet under hc->AcpiLock (a spin lock held for the
 * pointer and the counts alone): the set's head with its reader count, its
 * generation and whether it is off the controller (Gone); the body after
 * the head. A request takes a reader under the lock and copies with the
 * lock released; whoever leaves a set that is Gone with no reader frees
 * it. hc->AcpiEpoch, bumped by every forget, keeps a reading made across a
 * stop from being published. hc->AcpiGate (a synchronization event) lets
 * one request at a time read.
 *
 * IRQL: HcdAcpiInit at AddDevice; HcdAcpiForget in the PnP path; the
 * request at PASSIVE_LEVEL on the caller's thread. Physical memory is read
 * through MmMapIoSpace (MmNonCached), mapped for each piece and unmapped
 * after, a byte at a time, only on 98 SE and ME.
 */

#include "hcd.h"
#include "xhci_acpib.h"

typedef struct _HCD_ACPI_SET {
    ULONG Readers;
    ULONG Gone;
    ULONG Gen;
    ULONG Bytes;
    ULONG Count;
    ULONG Flags;
    UCHAR *Body;
} HCD_ACPI_SET, *PHCD_ACPI_SET;

VOID HcdAcpiInit(PHCD_CONTROLLER hc)
{
    KeInitializeSpinLock(&hc->AcpiLock);
    KeInitializeEvent(&hc->AcpiGate, SynchronizationEvent, TRUE);
    hc->AcpiSet = NULL;
    hc->AcpiGen = 0;
    hc->AcpiEpoch = 0;
    hc->AcpiClosed = 1;
}

/* A start admits requests again (hcdReadValues, at each start). */
VOID HcdAcpiOpen(PHCD_CONTROLLER hc)
{
    KIRQL irql;

    KeAcquireSpinLock(&hc->AcpiLock, &irql);
    hc->AcpiClosed = 0;
    KeReleaseSpinLock(&hc->AcpiLock, irql);
}

static VOID hcdAcpiFreeSet(PHCD_ACPI_SET set)
{
    HcdPoolFreeAcpi(set->Body);
    HcdPoolFreeAcpi(set);
}

/* The set off the controller, under AcpiLock: returned to be freed when
 * no request reads it, else NULL (its last reader frees it). */
static PHCD_ACPI_SET hcdAcpiDetach(PHCD_CONTROLLER hc)
{
    PHCD_ACPI_SET set;

    set = (PHCD_ACPI_SET)hc->AcpiSet;
    hc->AcpiSet = NULL;
    if (set == NULL) {
        return NULL;
    }
    set->Gone = 1;
    return set->Readers == 0 ? set : NULL;
}

VOID HcdAcpiForget(PHCD_CONTROLLER hc)
{
    PHCD_ACPI_SET set;
    KIRQL irql;

    KeAcquireSpinLock(&hc->AcpiLock, &irql);
    set = hcdAcpiDetach(hc);
    hc->AcpiEpoch++;
    hc->AcpiClosed = 1;
    KeReleaseSpinLock(&hc->AcpiLock, irql);
    if (set != NULL) {
        hcdAcpiFreeSet(set);
    }
}

static VOID hcdAcpiPut(PHCD_CONTROLLER hc, PHCD_ACPI_SET set)
{
    KIRQL irql;
    BOOLEAN free;

    KeAcquireSpinLock(&hc->AcpiLock, &irql);
    set->Readers--;
    free = (BOOLEAN)(set->Readers == 0 && set->Gone);
    KeReleaseSpinLock(&hc->AcpiLock, irql);
    if (free) {
        hcdAcpiFreeSet(set);
    }
}

/* ---- the reader's host ------------------------------------------------ */

static BOOLEAN hcdAcpiRead(PVOID ctx, ULONG phys, UCHAR *out, ULONG bytes)
{
    PHYSICAL_ADDRESS pa;
    volatile UCHAR *va;
    ULONG base;
    ULONG span;
    ULONG i;

    UNREFERENCED_PARAMETER(ctx);
    if (bytes == 0 || phys > 0xFFFFFFFFUL - bytes) {
        return FALSE;
    }
    base = phys & ~0xFFFUL;
    span = ((phys - base) + bytes + 0xFFFUL) & ~0xFFFUL;
    if (span > XHA_MAP_MAX) {
        return FALSE;
    }
    pa.LowPart = base;
    pa.HighPart = 0;
    va = (volatile UCHAR *)MmMapIoSpace(pa, span, MmNonCached);
    if (va == NULL) {
        return FALSE;
    }
    for (i = 0; i < bytes; i++) {
        out[i] = va[(phys - base) + i];
    }
    MmUnmapIoSpace((PVOID)va, span);
    return TRUE;
}

static PVOID hcdAcpiAlloc(PVOID ctx, ULONG bytes)
{
    UNREFERENCED_PARAMETER(ctx);
    return HcdPoolAllocAcpi(bytes);
}

static VOID hcdAcpiFree(PVOID ctx, PVOID p)
{
    UNREFERENCED_PARAMETER(ctx);
    HcdPoolFreeAcpi(p);
}

/* Milliseconds from the system time, wrapping; for the reading's 20 s
 * bound only (the high word counts 2^32 x 100 ns, 429,497 ms; no 64-bit
 * division). */
static ULONG hcdAcpiNowMs(PVOID ctx)
{
    LARGE_INTEGER now;

    UNREFERENCED_PARAMETER(ctx);
    KeQuerySystemTime(&now);
    return (ULONG)now.HighPart * 429497UL + now.LowPart / 10000UL;
}

/* ---- the request ------------------------------------------------------ */

NTSTATUS HcdAcpiRequest(PHCD_CONTROLLER hc, PUCHAR buf, ULONG inLen,
                        ULONG outLen, PULONG_PTR info)
{
    LARGE_INTEGER wait;
    XHA_HOST host;
    XHA_IN in;
    PHCD_ACPI_SET set;
    PHCD_ACPI_SET drop;
    PHCD_ACPI_SET made;
    UCHAR *body;
    ULONG from;
    ULONG flags;
    ULONG bytes;
    ULONG count;
    ULONG setFlags;
    ULONG outcome;
    ULONG state;
    ULONG epoch;
    ULONG written;
    BOOLEAN last;
    KIRQL irql;
    NTSTATUS st;

    *info = 0;
    epoch = 0;
    if (buf == NULL || inLen != 2UL * sizeof(ULONG)) {
        return STATUS_INVALID_PARAMETER;
    }
    /* METHOD_BUFFERED: the input is read before the answer overwrites it. */
    from = (ULONG)buf[0] | ((ULONG)buf[1] << 8) | ((ULONG)buf[2] << 16) |
           ((ULONG)buf[3] << 24);
    flags = (ULONG)buf[4] | ((ULONG)buf[5] << 8) | ((ULONG)buf[6] << 16) |
            ((ULONG)buf[7] << 24);
    if (flags != XHA_IN_READ && flags != XHA_IN_RELEASE) {
        return STATUS_INVALID_PARAMETER;
    }
    if (outLen < XHA_HDR_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    {
        PUCHAR z = (PUCHAR)&in;
        ULONG i;
        for (i = 0; i < sizeof(in); i++) {
            z[i] = 0;
        }
    }
    in.State = XHA_S_READ;
    set = NULL;
    drop = NULL;
    KeAcquireSpinLock(&hc->AcpiLock, &irql);
    /* A stopped controller admits nothing; the epoch taken now is the
     * one a reading must still find to publish. */
    if (hc->AcpiClosed) {
        KeReleaseSpinLock(&hc->AcpiLock, irql);
        return STATUS_DEVICE_NOT_READY;
    }
    epoch = hc->AcpiEpoch;
    if (flags == XHA_IN_RELEASE) {
        drop = hcdAcpiDetach(hc);
    } else if (hc->AcpiSet != NULL) {
        set = (PHCD_ACPI_SET)hc->AcpiSet;
        set->Readers++;
    }
    KeReleaseSpinLock(&hc->AcpiLock, irql);
    if (drop != NULL) {
        hcdAcpiFreeSet(drop);
    }
    if (flags == XHA_IN_RELEASE) {
        goto answer;
    }
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        in.State = XHA_S_NOT_OS;
        goto answer;
    }
    if (!hc->AcpiTablesOn) {
        in.State = XHA_S_OFF;
        goto answer;
    }
    if (set == NULL && from == 0) {
        /* One reading at a time; only a wait that succeeded holds the
         * gate. 10 s relative, in 100 ns units. */
        wait.LowPart = 0xFA0A1F00UL;
        wait.HighPart = -1;
        st = KeWaitForSingleObject(&hc->AcpiGate, Executive, KernelMode,
                                   FALSE, &wait);
        if (st != STATUS_SUCCESS) {
            in.State = XHA_S_BUSY;
            goto answer;
        }
        KeAcquireSpinLock(&hc->AcpiLock, &irql);
        if (hc->AcpiClosed || hc->AcpiEpoch != epoch) {
            KeReleaseSpinLock(&hc->AcpiLock, irql);
            (VOID)KeSetEvent(&hc->AcpiGate, IO_NO_INCREMENT, FALSE);
            return STATUS_DEVICE_NOT_READY;
        }
        if (hc->AcpiSet != NULL) {
            set = (PHCD_ACPI_SET)hc->AcpiSet;
            set->Readers++;
        }
        KeReleaseSpinLock(&hc->AcpiLock, irql);
        if (set == NULL) {
            host.Read = hcdAcpiRead;
            host.Alloc = hcdAcpiAlloc;
            host.Free = hcdAcpiFree;
            host.NowMs = hcdAcpiNowMs;
            host.Context = hc;
            body = NULL;
            state = XhaReadSet(&host, &body, &bytes, &count, &setFlags,
                               &outcome);
            made = NULL;
            if (state == XHA_S_READ) {
                made = (PHCD_ACPI_SET)HcdPoolAllocAcpi(sizeof(HCD_ACPI_SET));
                if (made == NULL) {
                    HcdPoolFreeAcpi(body);
                    state = XHA_S_NOMEM;
                } else {
                    made->Readers = 1;
                    made->Gone = 0;
                    made->Bytes = bytes;
                    made->Count = count;
                    made->Flags = setFlags;
                    made->Body = body;
                }
            }
            if (made != NULL) {
                KeAcquireSpinLock(&hc->AcpiLock, &irql);
                if (!hc->AcpiClosed && hc->AcpiEpoch == epoch &&
                    hc->AcpiSet == NULL) {
                    hc->AcpiGen++;
                    if (hc->AcpiGen == 0) {
                        hc->AcpiGen = 1;
                    }
                    made->Gen = hc->AcpiGen;
                    hc->AcpiSet = made;
                    set = made;
                    made = NULL;
                }
                KeReleaseSpinLock(&hc->AcpiLock, irql);
                if (made != NULL) {
                    /* Stopped meanwhile: never published. */
                    hcdAcpiFreeSet(made);
                    (VOID)KeSetEvent(&hc->AcpiGate, IO_NO_INCREMENT, FALSE);
                    return STATUS_DEVICE_NOT_READY;
                }
            } else {
                in.State = state;
                in.Outcome = outcome;
            }
        }
        (VOID)KeSetEvent(&hc->AcpiGate, IO_NO_INCREMENT, FALSE);
    }
answer:
    if (set != NULL) {
        in.Body = set->Body;
        in.BodyBytes = set->Bytes;
        in.Count = set->Count;
        in.Flags = set->Flags;
        in.SetGen = set->Gen;
    }
    (VOID)XhaAnswer(buf, outLen, &in, set != NULL ? from : 0, &written);
    *info = written;
    if (set != NULL) {
        /* The answer that carried the last byte lets the set go. */
        last = (BOOLEAN)(from <= set->Bytes && written > XHA_HDR_BYTES &&
                         from + (written - XHA_HDR_BYTES) == set->Bytes);
        if (last) {
            KeAcquireSpinLock(&hc->AcpiLock, &irql);
            if (hc->AcpiSet == set) {
                hc->AcpiSet = NULL;
                set->Gone = 1;
            }
            KeReleaseSpinLock(&hc->AcpiLock, irql);
        }
        hcdAcpiPut(hc, set);
    }
    return STATUS_SUCCESS;
}
