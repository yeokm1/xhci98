/*
 * xhci_func.h - the pure half of composite splitting in xhci98.sys
 * (roadmap-hcd.md task 26-A.7; design record 13 sections 10.7 to 10.9).
 *
 * Four computations, none touching a register, a lock or a kernel service,
 * so the host suite drives each (test\test_func.c):
 *
 *   - whether a device is split, and its functions: IADs first, then the
 *     positional audio rule Microsoft's parent applies, then one interface
 *     per function (section 10.8);
 *   - a function's own configuration descriptor, as its PDO answers
 *     GET_DESCRIPTOR(CONFIGURATION) (section 10.9);
 *   - whether a SETUP packet may leave a function PDO (an interface
 *     recipient must be one of its interfaces);
 *   - the function PDO's ids (section 10.7), as ASCII multi-strings.
 *
 * DDK-free: part of the pure core.
 */

#ifndef XHCI_FUNC_H
#define XHCI_FUNC_H

#include "xhci_compat.h"

#define XHCI_FUNC_OK            0UL
#define XHCI_FUNC_NO_SPLIT      1UL /* sound, but the device stays whole  */
#define XHCI_FUNC_MALFORMED     2UL
#define XHCI_FUNC_BAD_PARAM     3UL
#define XHCI_FUNC_TOO_SMALL     4UL /* the id did not fit; *used = needed */

/* USB 2.0 Table 9-5 and the ECN's IAD (the Windows 2000 DDK has no IAD;
 * WDK usb200.h per design record 13 section 10.8). */
#define XHCI_FUNC_DT_DEVICE     0x01UL
#define XHCI_FUNC_DT_CONFIG     0x02UL
#define XHCI_FUNC_DT_INTERFACE  0x04UL
#define XHCI_FUNC_DT_IAD        0x0BUL
#define XHCI_FUNC_DEVICE_BYTES  18UL
#define XHCI_FUNC_CONFIG_BYTES  9UL
#define XHCI_FUNC_IFACE_BYTES   9UL
#define XHCI_FUNC_IAD_BYTES     8UL

/* Functions (and IADs) per device; past it the device is not split. */
#define XHCI_FUNC_MAX           16UL

typedef struct _XHCI_FUNC {
    ULONG FirstInterface;       /* MI_nn                                 */
    ULONG InterfaceMask;        /* bit n: bInterfaceNumber n             */
    ULONG InterfaceCount;       /* the filtered bNumInterfaces           */
    ULONG Class;                /* the IAD's bFunction triple, else the  */
                                /* first interface's (alternate 0)       */
    ULONG SubClass;
    ULONG Protocol;
    ULONG IadOffset;            /* of its IAD; 0 when grouped without    */
    ULONG StringIndex;          /* iFunction, else iInterface            */
    ULONG DeviceMask;           /* every interface of the configuration  */
} XHCI_FUNC, *PXHCI_FUNC;

typedef struct _XHCI_FUNC_SET {
    ULONG Count;
    XHCI_FUNC Func[XHCI_FUNC_MAX];
} XHCI_FUNC_SET, *PXHCI_FUNC_SET;

/*
 * The functions of a device with the 18-byte `device` descriptor and the
 * whole configuration `config` of `length` bytes. Split only as Microsoft's
 * composite parent is: bNumConfigurations 1, at least two interfaces, and
 * bDeviceClass 0 or class/subclass/protocol EF/02/01. Grouped by IAD; with
 * no IAD in the configuration at all, by the positional audio rule; else
 * one interface per function. XHCI_FUNC_OK with Count >= 1 when the bus
 * splits it, in the order of their first interfaces' descriptors;
 * XHCI_FUNC_NO_SPLIT (Count 0) when it stays one device PDO;
 * XHCI_FUNC_MALFORMED for a header that is not a configuration descriptor,
 * a wTotalLength below 9 or above `length`, a descriptor whose bLength is
 * below 2 or runs past wTotalLength, an interface descriptor below 9 or an
 * IAD below 8 bytes, or two alternate-0 descriptors of one interface.
 */
ULONG XhciFuncSplit(const UCHAR *device, const UCHAR *config, ULONG length,
                    PXHCI_FUNC_SET set);

/*
 * The function's configuration descriptor: `config`'s header with
 * wTotalLength and bNumInterfaces rewritten, then the function's IAD and its
 * interfaces' descriptors (every alternate, with what follows each up to the
 * next interface or IAD), in device order. *total is its whole length;
 * min(capacity, *total) bytes are written. out may be NULL with capacity 0.
 */
ULONG XhciFuncConfig(const UCHAR *config, ULONG length, const XHCI_FUNC *func,
                     UCHAR *out, ULONG capacity, PULONG total);

/* 1 when the 8-byte SETUP may go to the device from the function whose
 * interfaces are `interfaceMask`, of the device's `deviceMask`: a standard
 * or class request with interface recipient may not name a sibling's
 * interface in wIndex's low byte (the high byte is the class's own - UAC
 * 1.0 puts an entity ID there). One naming no interface of the device is
 * the device's to answer. */
ULONG XhciFuncSetupAllowed(const UCHAR *setup, ULONG interfaceMask,
                           ULONG deviceMask);

#define XHCI_FUNC_ID_DEVICE     0UL
#define XHCI_FUNC_ID_HARDWARE   1UL
#define XHCI_FUNC_ID_COMPATIBLE 2UL
#define XHCI_FUNC_ID_INSTANCE   3UL

/* One id query's answer, NUL-terminated strings with a second NUL closing a
 * multi-string (hardware and compatible ids), upper-case hex. *used is the
 * whole answer's length, NULs included, even when it did not fit. */
ULONG XhciFuncId(const UCHAR *device, const XHCI_FUNC *func, ULONG port,
                 ULONG which, char *out, ULONG capacity, PULONG used);

#endif /* XHCI_FUNC_H */
