/*
 * xhci98_streams.h - the private bulk-streams interface of xhci98.sys, the
 * bus driver (roadmap-hcd.md task 31-A.1), for a function driver of this
 * project's own: the UAS class driver of 31-A.2, a separate binary that
 * includes this header and nothing else of the bus.
 *
 * Self-contained: it needs only the DDK's base types and CTL_CODE, so a
 * driver includes it after <wdm.h> or <ntddk.h>. C89. No Microsoft driver
 * sends or answers these requests; a bus that is not xhci98.sys completes
 * them with an error (STATUS_NOT_SUPPORTED or STATUS_INVALID_DEVICE_REQUEST
 * on every target's own stack), which is how the caller learns that streams
 * are not available and takes its streamless path.
 *
 * THE MODEL. Streams are opened on a pipe handle the caller already holds -
 * a bulk endpoint of a SuperSpeed device, as SELECT_CONFIGURATION or
 * SELECT_INTERFACE returned it - and each stream comes back as a pipe handle
 * of its own. A transfer on stream n is an ordinary
 * URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER whose PipeHandle is stream n's
 * handle: the stream ID rides on the handle, so no URB carries a private
 * field and every URB the caller already builds works unchanged. Stream
 * handles are opaque, like every USBD_PIPE_HANDLE.
 *
 *   IOCTL_XHCI98_OPEN_STREAMS   IRP_MJ_INTERNAL_DEVICE_CONTROL to the
 *                               device's PDO, Parameters.Others.Argument1 =
 *                               a PXHCI98_STREAMS_REQUEST. PASSIVE_LEVEL to
 *                               send (the bus answers it from its own
 *                               thread, so it may pend); the request
 *                               structure must stay valid until the IRP
 *                               completes.
 *   IOCTL_XHCI98_CLOSE_STREAMS  the same, with PipeHandle the endpoint's own
 *                               handle (not a stream's); the other fields
 *                               but Status are not read.
 *
 * What the bus does with each request on an endpoint with streams open:
 *
 *   a transfer on a stream handle   queued on that stream's ring, its
 *                                   doorbell rung with the Stream ID;
 *   a transfer on the endpoint's    refused, USBD_STATUS_INVALID_PIPE_HANDLE
 *     own handle                    (a streams endpoint has no stream 0);
 *   ABORT_PIPE on a stream handle   that stream's requests complete as
 *                                   cancelled; the other streams run on and
 *                                   every handle stays valid;
 *   ABORT_PIPE on the endpoint's    every stream's requests complete as
 *     own handle                    cancelled and the streams are CLOSED:
 *                                   the stream handles go invalid and the
 *                                   endpoint is an ordinary bulk pipe again
 *                                   (open them again to continue);
 *   RESET_PIPE on either            the whole endpoint: every stream's
 *                                   requests cancelled, the endpoint reset
 *                                   on both sides (CLEAR_FEATURE(
 *                                   ENDPOINT_HALT) to the device), the
 *                                   streams left open, every handle valid;
 *   CLOSE_STREAMS                   as ABORT_PIPE on the endpoint's handle;
 *   IOCTL_INTERNAL_USB_RESET_PORT   the device restored with its streams
 *                                   open, every handle valid, what was in
 *                                   flight cancelled;
 *   SELECT_CONFIGURATION,           the endpoint's pipe closes and its
 *     SELECT_INTERFACE of its         streams with it; their handles go
 *     interface, device removal       invalid.
 *
 * OPEN_STREAMS cancels anything pending on the endpoint's own handle first.
 * A request on a controller or endpoint without streams fails with Status
 * saying which, and the endpoint is left as it was.
 */

#ifndef XHCI98_STREAMS_H
#define XHCI98_STREAMS_H

/* FILE_DEVICE_UNKNOWN, as FILE_DEVICE_USB is (usbioctl.h); function codes
 * from 0x800 are the private range, and 0xF98 is no USB IOCTL's. */
#define XHCI98_STREAMS_DEVICE_TYPE  0x00000022UL
#define IOCTL_XHCI98_OPEN_STREAMS                                             \
    CTL_CODE(XHCI98_STREAMS_DEVICE_TYPE, 0xF98, METHOD_NEITHER,               \
             FILE_ANY_ACCESS)
#define IOCTL_XHCI98_CLOSE_STREAMS                                            \
    CTL_CODE(XHCI98_STREAMS_DEVICE_TYPE, 0xF99, METHOD_NEITHER,               \
             FILE_ANY_ACCESS)

#define XHCI98_STREAMS_SIGNATURE    0x53383958UL    /* 'X98S' */
#define XHCI98_STREAMS_VERSION      1UL

/* Streams per endpoint, at most: Stream IDs 1..31. The bus grants what the
 * endpoint's companion descriptor, the controller's array size and this
 * bound all allow, and never more than was asked. */
#define XHCI98_STREAMS_MAX          31UL

/* Status: the bus's answer, beside the IRP's NTSTATUS. */
#define XHCI98_STREAMS_SUCCESS          0UL /* STATUS_SUCCESS              */
#define XHCI98_STREAMS_NO_CONTROLLER    1UL /* MaxPSASize 0: this xHC has no
                                             * streams; STATUS_NOT_SUPPORTED */
#define XHCI98_STREAMS_NO_ENDPOINT      2UL /* not a SuperSpeed bulk
                                             * endpoint with MaxStreams > 0;
                                             * STATUS_NOT_SUPPORTED        */
#define XHCI98_STREAMS_INVALID_PIPE     3UL /* not an open pipe of this PDO,
                                             * or a stream's handle;
                                             * STATUS_INVALID_PARAMETER    */
#define XHCI98_STREAMS_INVALID_REQUEST  4UL /* signature, version, size or
                                             * count; STATUS_INVALID_PARAMETER */
#define XHCI98_STREAMS_ALREADY_OPEN     5UL /* STATUS_DEVICE_BUSY          */
#define XHCI98_STREAMS_NOT_OPEN         6UL /* CLOSE with none open;
                                             * STATUS_INVALID_PARAMETER    */
#define XHCI98_STREAMS_NO_RESOURCES     7UL /* memory, common buffer, or the
                                             * controller's Resource Error;
                                             * STATUS_INSUFFICIENT_RESOURCES */
#define XHCI98_STREAMS_FAILED           8UL /* a command or the device failed,
                                             * or the device left;
                                             * STATUS_UNSUCCESSFUL or
                                             * STATUS_NO_SUCH_DEVICE       */

typedef struct _XHCI98_STREAMS_REQUEST {
    ULONG Signature;            /* in:  XHCI98_STREAMS_SIGNATURE          */
    ULONG Version;              /* in:  XHCI98_STREAMS_VERSION            */
    ULONG Size;                 /* in:  sizeof(XHCI98_STREAMS_REQUEST)    */
    PVOID PipeHandle;           /* in:  the endpoint's own pipe handle    */
    ULONG StreamsRequested;     /* in:  1..XHCI98_STREAMS_MAX (OPEN)      */
    ULONG StreamsGranted;       /* out: 1..StreamsRequested (OPEN)        */
    ULONG Status;               /* out: XHCI98_STREAMS_*                  */
    ULONG Reserved;             /* in:  0                                 */
    /* out (OPEN): StreamPipeHandle[n] is Stream ID n's handle for
     * n = 1..StreamsGranted; [0] and the rest are NULL. */
    PVOID StreamPipeHandle[XHCI98_STREAMS_MAX + 1];
} XHCI98_STREAMS_REQUEST, *PXHCI98_STREAMS_REQUEST;

#endif /* XHCI98_STREAMS_H */
