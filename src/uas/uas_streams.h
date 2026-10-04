/*
 * uas_streams.h - the one place xhciuas.sys meets the bus's bulk streams
 * (roadmap-hcd.md task 31-A.1, src\xhci98_streams.h).
 *
 * The class driver needs three things from the bus and no more: open N
 * streams on a bulk pipe and learn how many were granted, address a bulk
 * transfer to stream N, and close the streams again. Every other file calls
 * the functions below and nothing else, so a change to the bus's interface
 * is a change to uas_streams.c alone.
 *
 * The bus's interface (src\xhci98_streams.h, included from src\ by path,
 * the same file the bus builds with): IOCTL_XHCI98_OPEN_STREAMS and
 * IOCTL_XHCI98_CLOSE_STREAMS, internal device controls to the device's PDO
 * with an XHCI98_STREAMS_REQUEST in Argument1, sent at PASSIVE_LEVEL and
 * possibly pended. A stream's transfers are ordinary bulk URBs on the
 * stream's own pipe handle. IOCTL_INTERNAL_USB_RESET_PORT leaves the
 * streams open and every handle valid, so recovery does not reopen them.
 * A bus that is not xhci98.sys fails the IOCTL: streams are unavailable,
 * High Speed UAS runs without them, and SuperSpeed UAS refuses to start
 * (31-A.3's send-back keeps such a device off this driver).
 *
 * IRQL: UasStreamsOpen and UasStreamsClose at PASSIVE_LEVEL; UasStreamsPipe
 * at <= DISPATCH_LEVEL.
 */

#ifndef UAS_STREAMS_H
#define UAS_STREAMS_H

#include "../xhci98_streams.h"

/* The most streams this driver asks for on a pipe. */
#define UAS_STREAMS_MAX     XHCI98_STREAMS_MAX

typedef struct _UAS_STREAMS {
    PDEVICE_OBJECT Lower;               /* the device's stack */
    ULONG Granted;                      /* streams open on every pipe */
    USBD_PIPE_HANDLE Base[3];           /* status, data-in, data-out */
    USBD_PIPE_HANDLE Pipe[3][UAS_STREAMS_MAX + 1];  /* [n]: stream n */
    BOOLEAN Open[3];
} UAS_STREAMS, *PUAS_STREAMS;

#define UAS_STREAM_STATUS   0
#define UAS_STREAM_DATA_IN  1
#define UAS_STREAM_DATA_OUT 2

NTSTATUS UasStreamsOpen(PUAS_STREAMS s, PDEVICE_OBJECT lower,
                        USBD_PIPE_HANDLE status, USBD_PIPE_HANDLE dataIn,
                        USBD_PIPE_HANDLE dataOut, ULONG wanted);
USBD_PIPE_HANDLE UasStreamsPipe(PUAS_STREAMS s, ULONG which, ULONG stream);
VOID UasStreamsClose(PUAS_STREAMS s);

#endif /* UAS_STREAMS_H */
