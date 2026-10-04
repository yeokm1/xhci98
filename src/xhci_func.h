/*
 * xhci_func.h - the pure half of composite splitting in xhci98.sys
 * (roadmap-hcd.md task 26-A.7; design record 13 sections 10.7 to 10.9).
 *
 * Six computations, none touching a register, a lock or a kernel service,
 * so the host suite drives each (test\test_func.c):
 *
 *   - whether a device is split, and its functions: IADs first, then the
 *     positional audio rule Microsoft's parent applies, then one interface
 *     per function (section 10.8);
 *   - a function's own configuration descriptor, as its PDO answers
 *     GET_DESCRIPTOR(CONFIGURATION) (section 10.9);
 *   - whether a SETUP packet may leave a function PDO (an interface
 *     recipient must be one of its interfaces);
 *   - the function PDO's ids (section 10.7), as ASCII multi-strings;
 *   - every PDO's instance id: the serial number string when it is
 *     usable, else the location (task 33.2);
 *   - and every PDO's device text: which string it is named by, and that
 *     string made fit to show (task 33.6).
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

/*
 * Instance ids from the serial number (roadmap-hcd.md task 33.2; design
 * record 13 section 10.7). A string descriptor holds at most 126 UTF-16
 * characters ((255 - 2) / 2), so a serial id is at most 126 ASCII
 * characters and its NUL.
 */
#define XHCI_SERIAL_ID_CHARS    126UL
#define XHCI_SERIAL_ID_BYTES    127UL
#define XHCI_FUNC_BAD_SERIAL    5UL /* a string, but no usable instance id */
#define XHCI_INSTANCE_NO_MI     0xFFFFFFFFUL

/*
 * The serial id of the string descriptor `desc`, `bytes` of it read, into
 * `out` (at least XHCI_SERIAL_ID_BYTES), NUL-terminated. XHCI_FUNC_OK with
 * the characters copied; XHCI_FUNC_MALFORMED (out empty) when it is not a
 * string descriptor: fewer than 2 bytes, bDescriptorType not 3, or a
 * bLength below 2 or past `bytes`; XHCI_FUNC_BAD_SERIAL (out empty) for an
 * empty string, or any character an instance id may not carry - only 0x21
 * to 0x7E pass, less ',' and '\' (Microsoft's usbhub refuses below 0x20,
 * above 0x7F and ','; this rule is narrower by the space, DEL and the
 * backslash, which separates the parts of a device instance path). An odd
 * bLength's last byte is ignored. Nothing is truncated: a valid descriptor
 * always fits.
 */
ULONG XhciFuncSerialId(const UCHAR *desc, ULONG bytes, char *out,
                       ULONG capacity);

/* 1 when serial ids `a` and `b` are both non-empty and equal ignoring
 * ASCII case - the registry, where an instance id becomes a key name,
 * does not tell case apart. */
ULONG XhciFuncSerialSame(const char *a, const char *b);

/*
 * A device or function PDO's instance id, NUL-terminated ASCII. With a
 * non-empty `serial`: the serial id, then for a function (`mi` its
 * MI_nn, not XHCI_INSTANCE_NO_MI) '&' and nn. Without: the location key
 * (XhciHubInstanceKey) in decimal, then for a function nn - section 10.7's
 * location form. nn is two upper-case hex digits. *used is the whole
 * length, the NUL included, even when it did not fit.
 */
ULONG XhciFuncInstanceId(const char *serial, ULONG location, ULONG mi,
                         char *out, ULONG capacity, PULONG used);

/*
 * Device text (roadmap-hcd.md task 33.6; design record 13 section 10.7):
 * the DeviceTextDescription a PDO answers, from a string descriptor. At
 * most 126 UTF-16 units, as for a serial, and the NUL.
 */
#define XHCI_TEXT_CHARS         126UL
#define XHCI_TEXT_WCHARS        127UL
#define XHCI_TEXT_PICKS         3UL
#define XHCI_FUNC_BAD_TEXT      6UL /* a string, but nothing to show       */
#define XHCI_TEXT_FOLD_ASCII    1UL /* XhciFuncText flag: see below        */

/*
 * The string indexes a PDO's text is tried from, in order, into
 * `indexes` (XHCI_TEXT_PICKS of them); returns how many, zeros and
 * repeats left out. A device PDO (`func` NULL): iProduct. A function PDO:
 * its IAD's iFunction, then its first interface's iInterface (alternate
 * 0, found in the device's whole configuration `config` of `length`
 * bytes), then the device's iProduct. 0 when there is none: the caller's
 * "USB Device".
 */
ULONG XhciFuncTextIndexes(const UCHAR *device, const XHCI_FUNC *func,
                          const UCHAR *config, ULONG length, PULONG indexes);

/*
 * The text of the string descriptor `desc`, `bytes` of it read, into
 * `out` (at least XHCI_TEXT_WCHARS), NUL-terminated, *chars its length.
 * The string ends at its first NUL unit. A C0 or C1 control or DEL is a
 * space; a run of spaces is one; leading and trailing spaces go. A
 * surrogate that is not half of a pair, U+FFFE and U+FFFF are '?'. With
 * XHCI_TEXT_FOLD_ASCII every character above U+007E, a pair included, is
 * '?' too (Windows 98 and ME: see design record 13 section 10.7).
 * XHCI_FUNC_OK; XHCI_FUNC_MALFORMED (out empty) as XhciFuncSerialId's;
 * XHCI_FUNC_BAD_TEXT (out empty) when nothing but '?' and spaces would
 * remain, an empty string included.
 */
ULONG XhciFuncText(const UCHAR *desc, ULONG bytes, ULONG flags, WCHAR *out,
                   ULONG capacity, PULONG chars);

#endif /* XHCI_FUNC_H */
