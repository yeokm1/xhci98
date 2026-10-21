/*
 * xhci_usbdx.h - Windows 8's client contract (the 0x602 contract of the
 * usbdex library its class drivers link) served by xhci98.sys, the pure half
 * (roadmap-usb4 task 43.29; design record 13 section 6.13).
 *
 * The bus answers GUID_USBD_INTERFACE_V1 at version 0x602 (its six routines:
 * unregister, the four URB allocators and the free), the capability request
 * 0x49104B, URB_FUNCTION_OPEN_STATIC_STREAMS and _CLOSE_STATIC_STREAMS on the
 * private streams machinery (xhci98_streams.h), and 0x220FB3 as RESET_PORT.
 * Every decision here that touches no kernel service is a pure function of
 * values the driver half reads, so test\test_usbdx.c drives it:
 *   - the interface's and the capability block's sizes per pointer width and
 *     the checks a query and a request must pass;
 *   - which capability a GUID names, and the answer to each;
 *   - the streams count the capability reports;
 *   - the sizes and offsets of the URBs the allocators build;
 *   - the checks of an OPEN_STATIC_STREAMS URB, and the URB status each of
 *     the private interface's answers maps to;
 *   - the IOCTL 0x220FB3 is served as.
 * Layouts and codes are design record 13 section 6.13.1's static reading of
 * usbdex and ucx01000.sys and Microsoft's documentation it cites.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_USBDX_H
#define XHCI_USBDX_H

#include "xhci_compat.h"
#include "xhci.h"

#define XHCI_USBDX_CONTRACT_602         0x0602UL

/* The interface's Size, which UCX requires exactly (section 6.13.1). */
#define XHCI_USBDX_V1_BYTES_32          0x58UL
#define XHCI_USBDX_V1_BYTES_64          0x98UL

/* The capability request (FILE_DEVICE_USBEX, function 0x412,
 * METHOD_NEITHER) and its block: version 1, size 0x20 / 0x28. */
#define XHCI_USBDX_IOCTL_CAPABILITY     0x0049104BUL
#define XHCI_USBDX_CAP_VERSION          1UL
#define XHCI_USBDX_CAP_BYTES_32         0x20UL
#define XHCI_USBDX_CAP_BYTES_64         0x28UL

/* IOCTL_INTERNAL_USB_RESET_PORT, and the code Windows 8's storage drivers
 * send in its place on the paging, hibernation and dump paths (function
 * 0x3EC), which Windows 8's usbhub.sys serves as a port reset. */
#define XHCI_USBDX_IOCTL_RESET_PORT     0x00220007UL
#define XHCI_USBDX_IOCTL_RESET_ALIAS    0x00220FB3UL

/* usb.h (Windows 8 kit): the stream URBs and the stream record's version. */
#define XHCI_USBDX_URB_OPEN_STREAMS     0x0035UL
#define XHCI_USBDX_URB_CLOSE_STREAMS    0x0036UL
#define XHCI_USBDX_STREAM_INFO_VERSION  0x0100UL

/* XHCI98_STREAMS_MAX (xhci98_streams.h): the most streams the bus grants an
 * endpoint; the driver half asserts the two are equal. */
#define XHCI_USBDX_MAX_STREAMS          31UL

/* The interfaces a select may name (the bus's select tracks interface
 * numbers 0..31), and the endpoints one interface may have (USB 3.2 9.6.5:
 * at most 15 IN and 15 OUT besides the default pipe). */
#define XHCI_USBDX_MAX_INTERFACES       32UL
#define XHCI_USBDX_MAX_ENDPOINTS        30UL

/* _URB_HEADER.Length is a USHORT: no URB is longer. */
#define XHCI_USBDX_MAX_URB_BYTES        0xFFFFUL

/* Results of the checks. */
#define XHCI_USBDX_OK                   0UL
#define XHCI_USBDX_NOT_OURS             1UL /* not this build's interface:
                                             * completed as it came       */
#define XHCI_USBDX_INVALID              2UL /* STATUS_INVALID_PARAMETER   */
#define XHCI_USBDX_NOT_SUPPORTED        3UL /* STATUS_NOT_SUPPORTED       */

/* The capabilities by name (section 6.13.2's table). */
#define XHCI_USBDX_CAP_UNKNOWN          0UL
#define XHCI_USBDX_CAP_SELECTIVE_SUSPEND 1UL
#define XHCI_USBDX_CAP_CHAINED_MDLS     2UL
#define XHCI_USBDX_CAP_STATIC_STREAMS   3UL
#define XHCI_USBDX_CAP_FUNCTION_SUSPEND 4UL
#define XHCI_USBDX_CAP_HIGH_SPEED       5UL
#define XHCI_USBDX_CAP_SUPER_SPEED      6UL
#define XHCI_USBDX_CAP_FULL_SPEED       7UL
#define XHCI_USBDX_CAP_LOW_SPEED        8UL
#define XHCI_USBDX_CAP_CLEAR_TT         9UL

/* USBD status values (usb.h, Windows 2000 DDK and WDK 7.1), as the URB
 * carries them. */
#define XHCI_USBDX_USBD_SUCCESS             0x00000000UL
#define XHCI_USBDX_USBD_INVALID_PARAMETER   0x80000300UL
#define XHCI_USBDX_USBD_INVALID_PIPE        0x80000600UL
#define XHCI_USBDX_USBD_INTERNAL_HC_ERROR   0x80000800UL
#define XHCI_USBDX_USBD_NOT_SUPPORTED       0xC0000E00UL
#define XHCI_USBDX_USBD_NO_RESOURCES        0xC0001000UL
#define XHCI_USBDX_USBD_DEVICE_GONE         0xC0007000UL

/* The private interface's answers (xhci98_streams.h's XHCI98_STREAMS_*,
 * the same values; the driver half asserts them). */
#define XHCI_USBDX_SA_SUCCESS           0UL
#define XHCI_USBDX_SA_NO_CONTROLLER     1UL
#define XHCI_USBDX_SA_NO_ENDPOINT       2UL
#define XHCI_USBDX_SA_INVALID_PIPE      3UL
#define XHCI_USBDX_SA_INVALID_REQUEST   4UL
#define XHCI_USBDX_SA_ALREADY_OPEN      5UL
#define XHCI_USBDX_SA_NOT_OPEN          6UL
#define XHCI_USBDX_SA_NO_RESOURCES      7UL
#define XHCI_USBDX_SA_FAILED            8UL

/* The interface's Size for a build whose pointers are `pointerBytes` (4 or
 * 8); 0 for any other. */
ULONG XhciUsbdxV1Bytes(ULONG pointerBytes);

/*
 * IRP_MN_QUERY_INTERFACE for GUID_USBD_INTERFACE_V1: XHCI_USBDX_NOT_OURS
 * when `size` is not this build's or `version` is not 0x602 (the client then
 * takes its 0x600 path); XHCI_USBDX_INVALID for a NULL client device object
 * (`deviceObjectPresent` 0) or a zero pool tag, as UCX; XHCI_USBDX_OK.
 */
ULONG XhciUsbdxV1Check(ULONG size, ULONG version, ULONG pointerBytes,
                       ULONG deviceObjectPresent, ULONG poolTag);

/* The capability block's size for a build; 0 for an unknown width. */
ULONG XhciUsbdxCapBlockBytes(ULONG pointerBytes);

/*
 * The capability request's block: version 1, this build's size, a context
 * (`contextPresent`), and an output buffer present exactly when its length
 * is nonzero. XHCI_USBDX_OK or XHCI_USBDX_INVALID.
 */
ULONG XhciUsbdxCapBlockCheck(ULONG version, ULONG size, ULONG pointerBytes,
                             ULONG contextPresent, ULONG outLength,
                             ULONG outPresent);

/* The capability `guid` names: its 16 bytes as they lie in memory (Data1,
 * Data2 and Data3 little-endian). XHCI_USBDX_CAP_UNKNOWN when none. */
ULONG XhciUsbdxCapabilityId(const UCHAR *guid);

/*
 * The answer to capability `id` for a PDO at `speedClass` (XHCI_SPEED_*)
 * whose controller grants `maxStreams` (XhciUsbdxMaxStreams; 0 none),
 * with an output buffer of `outLength` bytes: XHCI_USBDX_OK,
 * XHCI_USBDX_NOT_SUPPORTED or XHCI_USBDX_INVALID. On XHCI_USBDX_OK for
 * STATIC_STREAMS *count is the USHORT to write and *resultBytes 2; for the
 * rest *count and *resultBytes are 0. A capability that takes no output
 * refuses an output length other than 0, and STATIC_STREAMS one other than
 * 2, as XHCI_USBDX_INVALID.
 */
ULONG XhciUsbdxCapability(ULONG id, ULONG speedClass, ULONG maxStreams,
                          ULONG outLength, PULONG count, PULONG resultBytes);

/* The streams count the capability reports on a controller whose Primary
 * Stream Context Array takes `hcEntries` (XhciStreamHcEntries, after the
 * streams-off quirk): the most XhciStreamPlan grants any endpoint there,
 * min(XHCI_USBDX_MAX_STREAMS, hcEntries - 1); 0 below 4 entries. */
ULONG XhciUsbdxMaxStreams(ULONG hcEntries);

/*
 * An isochronous URB for `packets` packets: `isochBytes` (sizeof(struct
 * _URB_ISOCH_TRANSFER), which holds one packet) plus `packets` times
 * `packetBytes`, GET_ISO_URB_SIZE's rule. XHCI_USBDX_INVALID for 0 packets
 * or a total above XHCI_USBDX_MAX_URB_BYTES; XHCI_USBDX_OK with *bytes.
 */
ULONG XhciUsbdxIsochBytes(ULONG packets, ULONG isochBytes, ULONG packetBytes,
                          PULONG bytes);

/*
 * A select URB's layout: `count` interfaces (1..XHCI_USBDX_MAX_INTERFACES),
 * interface i with pipes[i] endpoints (0..XHCI_USBDX_MAX_ENDPOINTS), after a
 * header of `headerBytes` (FIELD_OFFSET of the URB's Interface member). Each
 * interface takes a whole USBD_INTERFACE_INFORMATION (`ifInfoBytes`, which
 * holds one pipe) plus (pipes - 1) * `pipeInfoBytes` when it has more than
 * one: HcdCompositeSelectDown's rule and USBD_CreateConfigurationRequestEx's.
 * offsets[i] is interface i's offset from the URB's start, and *total the
 * URB's size. XHCI_USBDX_INVALID for a count or pipe count out of range or a
 * total above XHCI_USBDX_MAX_URB_BYTES; nothing is written then.
 */
ULONG XhciUsbdxSelectLayout(const ULONG *pipes, ULONG count,
                            ULONG headerBytes, ULONG ifInfoBytes,
                            ULONG pipeInfoBytes, PULONG offsets,
                            PULONG total);

/* One interface's USBD_INTERFACE_INFORMATION length for `pipes` endpoints,
 * as XhciUsbdxSelectLayout sizes it. */
ULONG XhciUsbdxInterfaceBytes(ULONG pipes, ULONG ifInfoBytes,
                              ULONG pipeInfoBytes);

/*
 * The DDK's select structures, per pointer width (usb.h of the Windows 2000
 * DDK and of WDK 7.1; the driver half asserts its FIELD_OFFSETs and sizes
 * against these): USBD_PIPE_INFORMATION, USBD_INTERFACE_INFORMATION (which
 * holds one pipe), and where each select URB's Interface member begins.
 */
#define XHCI_USBDX_PIPE_BYTES_32        0x14UL
#define XHCI_USBDX_PIPE_BYTES_64        0x18UL
#define XHCI_USBDX_PIPE_MAXXFER_32      0x0CUL
#define XHCI_USBDX_PIPE_MAXXFER_64      0x10UL
#define XHCI_USBDX_IFINFO_BYTES_32      0x24UL
#define XHCI_USBDX_IFINFO_BYTES_64      0x30UL
#define XHCI_USBDX_IFINFO_PIPES_32      0x0CUL  /* NumberOfPipes          */
#define XHCI_USBDX_IFINFO_PIPES_64      0x10UL
#define XHCI_USBDX_IFINFO_PIPE0_32      0x10UL  /* Pipes[0]               */
#define XHCI_USBDX_IFINFO_PIPE0_64      0x18UL
#define XHCI_USBDX_SELCFG_IFACE_32      0x18UL
#define XHCI_USBDX_SELCFG_IFACE_64      0x28UL
#define XHCI_USBDX_SELIF_IFACE_32       0x14UL
#define XHCI_USBDX_SELIF_IFACE_64       0x20UL

/* URB_FUNCTION_SELECT_CONFIGURATION and _SELECT_INTERFACE. */
#define XHCI_USBDX_URB_SELECT_CONFIG    0x0000UL
#define XHCI_USBDX_URB_SELECT_IFACE     0x0001UL

/*
 * A select URB's every field but its pointers, into `urb` (`urbBytes`,
 * zeroed by the caller), for a build whose pointers are `pointerBytes`:
 * `function` (XHCI_USBDX_URB_SELECT_CONFIG or _IFACE, which also chooses
 * the header), the header's Length and Function, and per interface i
 * (numbers[i], alternates[i], pipes[i] endpoints) its Length,
 * InterfaceNumber, AlternateSetting and NumberOfPipes and every pipe's
 * MaximumTransferSize, `maxTransfer` (the build's
 * USBD_DEFAULT_MAXIMUM_TRANSFER_SIZE, as HcdCompositeSelectDown writes it:
 * PAGE_SIZE in the Windows 2000 DDK, 0xFFFFFFFF in WDK 7.1 for XP and
 * later). A select-interface URB has exactly one interface.
 * offsets[i] is interface i's offset, for the pointers the caller writes.
 * XHCI_USBDX_INVALID when the layout is (XhciUsbdxSelectLayout), when
 * `urbBytes` is not its total, for an unknown function or width, or a
 * select-interface of other than one interface; nothing is written then.
 */
ULONG XhciUsbdxSelectFill(UCHAR *urb, ULONG urbBytes, ULONG function,
                          ULONG pointerBytes, const ULONG *pipes,
                          const UCHAR *numbers, const UCHAR *alternates,
                          ULONG count, ULONG maxTransfer, PULONG offsets);

/* What XhciUsbdxSelectFill needs first: the URB's size for these
 * interfaces, or XHCI_USBDX_INVALID. */
ULONG XhciUsbdxSelectBytes(ULONG function, ULONG pointerBytes,
                           const ULONG *pipes, ULONG count, PULONG bytes);

/*
 * An OPEN_STATIC_STREAMS URB before it is queued: `length` at least
 * `structBytes` (sizeof(struct _URB_OPEN_STATIC_STREAMS)), `infoVersion`
 * 0x100, `infoSize` equal to `infoBytes` (sizeof(USBD_STREAM_INFORMATION)),
 * `streams` 1..XHCI_USBDX_MAX_STREAMS and a Streams array present.
 * XHCI_USBDX_USBD_SUCCESS or XHCI_USBDX_USBD_INVALID_PARAMETER.
 */
ULONG XhciUsbdxOpenCheck(ULONG length, ULONG structBytes, ULONG infoVersion,
                         ULONG infoSize, ULONG infoBytes, ULONG streams,
                         ULONG arrayPresent);

/* A CLOSE_STATIC_STREAMS URB before its PipeHandle is read: `length` at
 * least `pipeRequestBytes` (sizeof(struct _URB_PIPE_REQUEST)).
 * XHCI_USBDX_USBD_SUCCESS or XHCI_USBDX_USBD_INVALID_PARAMETER. */
ULONG XhciUsbdxCloseCheck(ULONG length, ULONG pipeRequestBytes);

/*
 * Whether a plan that grants `planned` streams serves a request for
 * `requested`: the public OPEN_STATIC_STREAMS URB has no granted field, so
 * it is served only exactly (`exact` nonzero); the private
 * IOCTL_XHCI98_OPEN_STREAMS takes any grant of 1..requested.
 * XHCI_USBDX_OK or XHCI_USBDX_INVALID.
 */
ULONG XhciUsbdxGrantCheck(ULONG requested, ULONG planned, ULONG exact);

/* The URB status for the private interface's `answer` (XHCI_USBDX_SA_*),
 * `gone` nonzero when the device has left. */
ULONG XhciUsbdxStreamsUsbd(ULONG answer, ULONG gone);

/* The internal IOCTL `code` is served as: 0x220FB3 as RESET_PORT, any other
 * as itself. */
ULONG XhciUsbdxIoctlAlias(ULONG code);

#endif /* XHCI_USBDX_H */
