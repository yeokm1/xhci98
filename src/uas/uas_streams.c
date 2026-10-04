/*
 * uas_streams.c - the streams adapter (uas_streams.h): the bus's
 * IOCTL_XHCI98_OPEN_STREAMS and IOCTL_XHCI98_CLOSE_STREAMS
 * (src\xhci98_streams.h) behind the three calls the engine makes.
 *
 * IRQL: as uas_streams.h states per function.
 */

#include "uas.h"

static NTSTATUS uasStreamsSignal(PDEVICE_OBJECT device, PIRP irp,
                                 PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* PASSIVE_LEVEL. One request to the bus, waited for. The request lives in
 * nonpaged pool for the IRP's lifetime, as the interface requires. */
static NTSTATUS uasStreamsIoctl(PDEVICE_OBJECT lower, ULONG code,
                                PXHCI98_STREAMS_REQUEST req)
{
    PIO_STACK_LOCATION next;
    KEVENT done;
    PIRP irp;
    NTSTATUS status;

    irp = IoAllocateIrp(lower->StackSize, FALSE);
    if (irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode = code;
    next->Parameters.Others.Argument1 = req;
    UAS_SET_COMPLETION_ALWAYS(irp, uasStreamsSignal, &done);
    (VOID)IoCallDriver(lower, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);
    if (NT_SUCCESS(status) && req->Status != XHCI98_STREAMS_SUCCESS) {
        status = STATUS_UNSUCCESSFUL;
    }
    return status;
}

static PXHCI98_STREAMS_REQUEST uasStreamsRequest(USBD_PIPE_HANDLE pipe,
                                                 ULONG count)
{
    PXHCI98_STREAMS_REQUEST req;

    req = (PXHCI98_STREAMS_REQUEST)UasAlloc(sizeof(XHCI98_STREAMS_REQUEST));
    if (req != NULL) {
        req->Signature = XHCI98_STREAMS_SIGNATURE;
        req->Version = XHCI98_STREAMS_VERSION;
        req->Size = sizeof(XHCI98_STREAMS_REQUEST);
        req->PipeHandle = pipe;
        req->StreamsRequested = count;
        req->Status = XHCI98_STREAMS_FAILED;
    }
    return req;
}

/*
 * PASSIVE_LEVEL. The same count on all three pipes, since a tag is a stream
 * id on each: a pipe granting fewer than asked bounds the others, so
 * everything is closed and opened again at that count (once; a second
 * shortfall fails).
 */
NTSTATUS UasStreamsOpen(PUAS_STREAMS s, PDEVICE_OBJECT lower,
                        USBD_PIPE_HANDLE status, USBD_PIPE_HANDLE dataIn,
                        USBD_PIPE_HANDLE dataOut, ULONG wanted)
{
    PXHCI98_STREAMS_REQUEST req;
    ULONG count;
    ULONG pass;
    ULONG i;
    ULONG n;
    BOOLEAN shrunk;
    NTSTATUS st;

    UasStreamsClose(s);
    s->Lower = lower;
    s->Base[UAS_STREAM_STATUS] = status;
    s->Base[UAS_STREAM_DATA_IN] = dataIn;
    s->Base[UAS_STREAM_DATA_OUT] = dataOut;
    if (wanted > UAS_STREAMS_MAX) {
        wanted = UAS_STREAMS_MAX;
    }
    if (wanted == 0) {
        return STATUS_NOT_SUPPORTED;
    }
    count = wanted;
    st = STATUS_NOT_SUPPORTED;
    shrunk = FALSE;
    for (pass = 0; pass < 2; pass++) {
        shrunk = FALSE;
        for (i = 0; i < 3; i++) {
            req = uasStreamsRequest(s->Base[i], count);
            if (req == NULL) {
                st = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            st = uasStreamsIoctl(lower, IOCTL_XHCI98_OPEN_STREAMS, req);
            if (NT_SUCCESS(st) &&
                (req->StreamsGranted == 0 || req->StreamsGranted > count)) {
                st = STATUS_UNSUCCESSFUL;
            }
            if (NT_SUCCESS(st)) {
                s->Open[i] = TRUE;
                for (n = 0; n <= UAS_STREAMS_MAX; n++) {
                    s->Pipe[i][n] = (n >= 1 && n <= req->StreamsGranted)
                                        ? req->StreamPipeHandle[n] : NULL;
                }
                if (req->StreamsGranted < count) {
                    count = req->StreamsGranted;
                    shrunk = TRUE;
                }
            }
            UasFree(req);
            if (!NT_SUCCESS(st) || shrunk) {
                break;
            }
        }
        if (!NT_SUCCESS(st) || !shrunk) {
            break;
        }
        UasStreamsClose(s);
    }
    if (!NT_SUCCESS(st) || shrunk) {
        UasStreamsClose(s);
        return NT_SUCCESS(st) ? STATUS_UNSUCCESSFUL : st;
    }
    s->Granted = count;
    return STATUS_SUCCESS;
}

/* <= DISPATCH_LEVEL. Stream n's handle; the base handle when streams are
 * not open (the streamless transport). */
USBD_PIPE_HANDLE UasStreamsPipe(PUAS_STREAMS s, ULONG which, ULONG stream)
{
    if (which > UAS_STREAM_DATA_OUT || !s->Open[which] || stream == 0 ||
        stream > s->Granted) {
        return s->Base[which];
    }
    return s->Pipe[which][stream];
}

/* PASSIVE_LEVEL, nothing outstanding on any stream. */
VOID UasStreamsClose(PUAS_STREAMS s)
{
    PXHCI98_STREAMS_REQUEST req;
    ULONG i;

    for (i = 0; i < 3; i++) {
        if (s->Open[i] && s->Lower != NULL) {
            req = uasStreamsRequest(s->Base[i], 0);
            if (req != NULL) {
                (VOID)uasStreamsIoctl(s->Lower, IOCTL_XHCI98_CLOSE_STREAMS,
                                      req);
                UasFree(req);
            }
        }
        s->Open[i] = FALSE;
    }
    s->Granted = 0;
}
