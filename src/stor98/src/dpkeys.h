// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// dpkeys.h - every key the driver looks for in the NT DriverParameter
// string (HwFindAdapter's ArgumentString), in one place. The parsers
// (lifecycle.c LcFind, platform.c, cmlinknt.c) find a key anywhere in the
// string, case-insensitively, so no key may occur inside another one (a key
// "idle=" inside a "pollidle=" would set both). The host unit test
// (tests/unit/xfer_ut.c) checks DPK_ALL pairwise; a key added to a parser
// must be added here, and the parsers use only these names.
//

#ifndef _STOR98_DPKEYS_H_
#define _STOR98_DPKEYS_H_

// Product tunables (lifecycle.c). Every product key starts "stor" and
// names its 9x value (StorGoneMs, StorWriteIdleMs, ...) since xhci98
// 3.3.0.0; the names before it (gone=, idle=, pollmode=, ...) are ignored.
#define DPK_GONE            "storgonems="
#define DPK_IDLE            "storwriteidlems="
// Test builds only (STOR98_DBGPORT, lifecycle.c). hold9x= sets the 9x hold
// bound (Hold9xMs, PLAN 0.3; the product setting is the 9x value
// StorHold9xMs, platform.h) but is read from the NT string, which 9x never
// passes, so it is a test key and no product setting (xhci98 3.3.0.0).
#define DPK_HOLD9X          "hold9x="
#define DPK_FRAG            "frag="
#define DPK_BOUNCE          "bounce="
#define DPK_CALX            "calx="
#define DPK_SHNFLUSH        "shnflush="
#define DPK_CAPTO           "capto="
#define DPK_STORMTEST       "stormtest="
#define DPK_STORMINTS       "stormints="
#define DPK_IDLETO          "idleto="
// Polling mode (lifecycle.c, polltun.h)
#define DPK_POLLMODE        "storpollmode="
#define DPK_POLLBUSYUS      "storpollbusyus="
#define DPK_POLLIDLEMS      "storpollidlems="
#define DPK_POLLSPINUS      "storpollspinus="
// Maximum transfer size (lifecycle.c, xfertun.h)
#define DPK_MAXTRANSFERKB   "stormaxtransferkb="
// Write-through (platform.c)
#define DPK_WRITETHROUGH    "storwritethrough="
// The dump stack's own indication (platform.c, cmlinknt.c)
#define DPK_DUMP            "dump=1"

// Every key above, for the overlap check.
#define DPK_ALL \
    DPK_GONE, DPK_IDLE, DPK_HOLD9X, DPK_FRAG, DPK_BOUNCE, DPK_CALX, DPK_SHNFLUSH, DPK_CAPTO, \
    DPK_STORMTEST, DPK_STORMINTS, DPK_IDLETO, DPK_POLLMODE, DPK_POLLBUSYUS, DPK_POLLIDLEMS, \
    DPK_POLLSPINUS, DPK_MAXTRANSFERKB, DPK_WRITETHROUGH, DPK_DUMP

#endif // _STOR98_DPKEYS_H_
