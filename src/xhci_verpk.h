/*
 * xhci_verpk.h - the package version as one word, a byte a field from
 * the most significant (3.3.0.0 is 0x03030000), for the query answers
 * that say which driver is running (roadmap-usb4 task 44.8; design record
 * 25 section 18): xhci98.sys's XHQ_O_DRIVER_VERSION and xhcicm.sys's
 * CM_Q_RM_DRIVER.
 *
 * Made at compile time from xhci_version.h's XHCI_VER_CSV, the number the
 * version resources carry, so it cannot drift from them; xhci_version.h
 * itself stays the #define lines its three toolchains accept (its own
 * header), which is why the packing lives here. The host suite holds it
 * to XHCI_VER_STR (test_xhq).
 *
 * The argument of XHCI_VER_PACK_ARGS is XHCI_VER_CSV in parentheses: the
 * argument is expanded before it is substituted, so the rescan meets
 * XHCI_VER_PACK4 followed by (a,b,c,d).
 */

#ifndef XHCI_VERPK_H
#define XHCI_VERPK_H

#include "xhci_version.h"

#define XHCI_VER_PACK4(a, b, c, d)                                          \
    (((((ULONG)(a)) & 0xFFUL) << 24) | ((((ULONG)(b)) & 0xFFUL) << 16) |    \
     ((((ULONG)(c)) & 0xFFUL) << 8) | (((ULONG)(d)) & 0xFFUL))
#define XHCI_VER_PACK_ARGS(args) XHCI_VER_PACK4 args
#define XHCI_VER_PACKED XHCI_VER_PACK_ARGS((XHCI_VER_CSV))

#endif /* XHCI_VERPK_H */
