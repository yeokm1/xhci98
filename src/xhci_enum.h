/*
 * xhci_enum.h - the enumeration state machine of xhci98.sys, one per port
 * (design record 13 section 5.3; roadmap-hcd.md tasks 26-A.3, 26-A.4 and
 * 26-A.9).
 *
 * A pure transition function over a port record and an event: it returns
 * the one action the caller must carry out and changes nothing else. The
 * caller - the controller thread (hcd_enum.c) - performs the action, waits
 * for its outcome, and feeds the outcome back as the next event. The machine
 * touches no register, calls no kernel service and holds no lock, so the
 * host suite drives it with no controller at all (test\test_enum.c), the
 * split xhci_vhub.c made for the virtual hub in the miniport.
 *
 * DDK-free: part of the pure core.
 */

#ifndef XHCI_ENUM_H
#define XHCI_ENUM_H

#include "xhci_compat.h"

/* States (section 5.3's table). */
#define XHCI_ENUM_EMPTY          0UL
#define XHCI_ENUM_DEBOUNCE       1UL
#define XHCI_ENUM_RESET          2UL
#define XHCI_ENUM_ENABLE_SLOT    3UL
#define XHCI_ENUM_ADDRESS        4UL
#define XHCI_ENUM_DESC8          5UL
#define XHCI_ENUM_EVALUATE       6UL
#define XHCI_ENUM_DESC18         7UL
#define XHCI_ENUM_CONFIG9        8UL
#define XHCI_ENUM_CONFIG_FULL    9UL
#define XHCI_ENUM_PRESENT        10UL
#define XHCI_ENUM_BOUND          11UL
#define XHCI_ENUM_GONE           12UL
#define XHCI_ENUM_FAILED         13UL
#define XHCI_ENUM_STATE_COUNT    14UL

/* Events. Each carries the fields its outcome needs in XHCI_ENUM_EVENT. */
#define XHCI_ENUM_EV_CONNECT       1UL  /* CCS seen set on a change        */
#define XHCI_ENUM_EV_DISCONNECT    2UL  /* CCS seen clear on a change      */
#define XHCI_ENUM_EV_DEBOUNCED     3UL  /* Connected: still connected      */
#define XHCI_ENUM_EV_RESET_DONE    4UL  /* Ok, Speed                       */
#define XHCI_ENUM_EV_COMMAND_DONE  5UL  /* Ok, SlotId (Enable Slot)        */
#define XHCI_ENUM_EV_TRANSFER_DONE 6UL  /* Ok, Bytes, Value (see below)    */
#define XHCI_ENUM_EV_PDO_CREATED   7UL  /* Ok                              */
#define XHCI_ENUM_EV_PDO_STARTED   8UL
#define XHCI_ENUM_EV_PDO_REMOVED   9UL

/* Actions. */
#define XHCI_ENUM_ACT_NONE          0UL
#define XHCI_ENUM_ACT_DEBOUNCE      1UL /* wait the attach debounce, re-read  */
#define XHCI_ENUM_ACT_RESET         2UL /* reset the port, read its speed     */
#define XHCI_ENUM_ACT_ENABLE_SLOT   3UL
#define XHCI_ENUM_ACT_ADDRESS       4UL /* Address Device, EP0 at Mps0        */
#define XHCI_ENUM_ACT_GET_DEVICE    5UL /* GET_DESCRIPTOR(DEVICE), Length     */
#define XHCI_ENUM_ACT_EVALUATE      6UL /* Evaluate Context, EP0 at Mps0      */
#define XHCI_ENUM_ACT_GET_CONFIG    7UL /* GET_DESCRIPTOR(CONFIGURATION), Len */
#define XHCI_ENUM_ACT_CREATE_PDO    8UL
#define XHCI_ENUM_ACT_DISABLE_SLOT  9UL /* and drop what the slot owned       */
#define XHCI_ENUM_ACT_REPORT_GONE   10UL /* report the PDO missing            */

/* Why a port reached XHCI_ENUM_FAILED (XHCI_ENUM_PORT.FailCause). */
#define XHCI_ENUM_FAIL_NONE         0UL
#define XHCI_ENUM_FAIL_RESET        1UL
#define XHCI_ENUM_FAIL_NO_SLOT      2UL
#define XHCI_ENUM_FAIL_ADDRESS      3UL
#define XHCI_ENUM_FAIL_DESCRIPTOR   4UL
#define XHCI_ENUM_FAIL_CONFIG       5UL
#define XHCI_ENUM_FAIL_PDO          6UL
#define XHCI_ENUM_FAIL_SPEED        7UL

/* PORTSC speed values (xHCI Table 7-13 default PSIV, USB 2.0 ports). */
#define XHCI_ENUM_SPEED_FULL        1UL
#define XHCI_ENUM_SPEED_LOW         2UL
#define XHCI_ENUM_SPEED_HIGH        3UL

/* The length of a USB device descriptor, and of a configuration descriptor's
 * own header (USB 2.0 9.6.1, 9.6.3). */
#define XHCI_ENUM_DEVICE_DESC_BYTES 18UL
#define XHCI_ENUM_CONFIG_HEAD_BYTES 9UL

/* One retry of the whole reset-to-descriptors sequence on a failure, as the
 * targets' own hub drivers do (section 5.3's Failed row). */
#define XHCI_ENUM_RETRIES           1UL

typedef struct _XHCI_ENUM_PORT {
    ULONG State;
    ULONG Speed;            /* XHCI_ENUM_SPEED_*, from the reset        */
    ULONG SlotId;           /* 0 = none enabled                         */
    ULONG Mps0;             /* EP0 max packet size in use               */
    ULONG ConfigLength;     /* wTotalLength, from the 9-byte read       */
    ULONG Retries;          /* used of XHCI_ENUM_RETRIES                */
    ULONG FailCause;        /* XHCI_ENUM_FAIL_*                         */
    ULONG PdoExists;        /* a PDO has been created and not removed   */
} XHCI_ENUM_PORT, *PXHCI_ENUM_PORT;

typedef struct _XHCI_ENUM_EVENT {
    ULONG Kind;             /* XHCI_ENUM_EV_*                           */
    ULONG Ok;               /* the step succeeded                       */
    ULONG Speed;            /* RESET_DONE                               */
    ULONG SlotId;           /* COMMAND_DONE after Enable Slot           */
    ULONG Bytes;            /* TRANSFER_DONE: bytes received            */
    ULONG Value;            /* TRANSFER_DONE: bMaxPacketSize0 after the
                             * 8-byte read, wTotalLength after the
                             * 9-byte configuration read                */
} XHCI_ENUM_EVENT, *PXHCI_ENUM_EVENT;

typedef struct _XHCI_ENUM_ACTION {
    ULONG Kind;             /* XHCI_ENUM_ACT_*                          */
    ULONG Length;           /* GET_DEVICE / GET_CONFIG: wLength         */
    ULONG Mps0;             /* ADDRESS / EVALUATE                       */
} XHCI_ENUM_ACTION, *PXHCI_ENUM_ACTION;

/* The EP0 packet size Address Device uses for a speed before the device
 * has said (section 10.2 step 7; the miniport's Finding 2 fix for Full
 * Speed). 0 for a speed the machine does not take. */
ULONG XhciEnumInitialMps0(ULONG speed);

/* Put a port in XHCI_ENUM_EMPTY with nothing owned. */
VOID XhciEnumReset(PXHCI_ENUM_PORT port);

/* One transition. Returns the new state; *action says what to do next. An
 * event that does not apply to the current state changes nothing and asks
 * for nothing. */
ULONG XhciEnumStep(PXHCI_ENUM_PORT port, const XHCI_ENUM_EVENT *event,
                   PXHCI_ENUM_ACTION action);

/* After a failure: one more attempt from Reset while retries remain
 * (XHCI_ENUM_RETRIES), otherwise nothing. The caller sends it once the slot
 * the failed step owned has been disabled and the port still reads
 * connected. */
ULONG XhciEnumRetry(PXHCI_ENUM_PORT port, PXHCI_ENUM_ACTION action);

/* The same with the caller's limit in place of XHCI_ENUM_RETRIES: a hub's
 * port is given XHCI_HUB_PORT_ATTEMPTS attempts in all (xhci_hub.h). */
ULONG XhciEnumRetryUpTo(PXHCI_ENUM_PORT port, ULONG retries,
                        PXHCI_ENUM_ACTION action);

#endif /* XHCI_ENUM_H */
