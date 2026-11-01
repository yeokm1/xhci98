/*
 * xhci_cmiso.h - the isolation statement xhcicm.sys gives xhci98.sys
 * (task 45.35; design record 17 section 4.13.6, design record 22 section
 * 10.5). Shared by both drivers; this file defines only the interface's
 * shape and its GUID.
 *
 * xhci98.sys asks IRP_MN_QUERY_INTERFACE for GUID_XHCI_CM_ISOLATION,
 * version XHCI_CM_ISO_VERSION, at its controller's start, of the object
 * IoAttachDeviceToDeviceStack returned. xhcicm.sys's kernel-attached child
 * filter on the controller's PDO answers it where it can bind the
 * controller to a USB4 port's PCIe tunnel (record 22 section 10.5).
 * Isolated() then answers 1 only while that tunnel's path is read back
 * closed. Called at PASSIVE_LEVEL; no lock held by the caller.
 *
 * The header compiles in both drivers' builds; the interface structure
 * is left out of the host build.
 */

#ifndef XHCI_CMISO_H
#define XHCI_CMISO_H

#include "xhci_compat.h"

/* {412072B0-1DC5-4D93-9E46-35FD4854F1F0} */
#define XHCI_CM_ISO_GUID_DATA1  0x412072B0UL
#define XHCI_CM_ISO_GUID_DATA2  0x1DC5
#define XHCI_CM_ISO_GUID_DATA3  0x4D93
#define XHCI_CM_ISO_GUID_DATA4 \
    { 0x9E, 0x46, 0x35, 0xFD, 0x48, 0x54, 0xF1, 0xF0 }

#define XHCI_CM_ISO_VERSION     1

typedef ULONG (*XHCI_CM_ISO_ISOLATED)(PVOID Context);

#if !defined(XHCI_HOST_TEST)
/* The INTERFACE header (Size, Version, Context, InterfaceReference,
 * InterfaceDereference), then the one function. */
typedef struct _XHCI_CM_ISO_INTERFACE {
    INTERFACE Header;
    XHCI_CM_ISO_ISOLATED Isolated;
} XHCI_CM_ISO_INTERFACE, *PXHCI_CM_ISO_INTERFACE;
#endif

/* An initializer for a `static const GUID` of each driver's own. */
#define XHCI_CM_ISO_GUID_INIT \
    { XHCI_CM_ISO_GUID_DATA1, XHCI_CM_ISO_GUID_DATA2, \
      XHCI_CM_ISO_GUID_DATA3, XHCI_CM_ISO_GUID_DATA4 }

#endif /* XHCI_CMISO_H */
