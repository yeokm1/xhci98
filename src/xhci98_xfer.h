/*
 * xhci98_xfer.h - the private transfer-size interface of xhci98.sys, the bus
 * driver, for a function driver of this project's own: xhciuas.sys
 * (roadmap-hcd.md tasks 36.21 and 36.22). Beside xhci98_streams.h and
 * separate from it: a High-Speed UAS device opens no streams, and still
 * asks this.
 *
 * Self-contained: it needs only the DDK's base types and CTL_CODE, so a
 * driver includes it after <wdm.h> or <ntddk.h>. C89. No Microsoft driver
 * sends or answers these requests; a bus that is not xhci98.sys, or an
 * xhci98.sys before 2.3.0.0, completes them STATUS_NOT_SUPPORTED (the
 * default of its internal device control, hcd_urb.c), which the caller
 * takes as "64 KiB" and "no telemetry".
 *
 *   IOCTL_XHCI98_TRANSFER_POLICY  IRP_MJ_INTERNAL_DEVICE_CONTROL to the
 *                                 device's PDO, Parameters.Others.Argument1
 *                                 = a PXHCI98_TRANSFER_POLICY. Pointer-free;
 *                                 answered synchronously at <= DISPATCH_LEVEL
 *                                 from what the controller's start read
 *                                 (XhciMaxTransferKB), never from the map
 *                                 registers or the streams of the moment.
 *   IOCTL_XHCI98_TELEMETRY        the same, Argument1 = a
 *                                 PXHCI98_TELEMETRY: the caller's bounded
 *                                 record, copied whole into the bus's own
 *                                 storage before the IRP completes, so the
 *                                 bus never holds a pointer into the caller.
 *                                 Sent at <= DISPATCH_LEVEL, never waited
 *                                 for under the caller's lock.
 *   IOCTL_XHCI98_RESUME_STATE     the same, Argument1 = a
 *                                 PXHCI98_RESUME_STATE (task 45.23, design
 *                                 record 20 section 4.3.3): the function's
 *                                 quiet declaration before a sleep, its
 *                                 withdrawal, the controller's resume state
 *                                 and the device's resume epoch after one,
 *                                 and the wake check's outcome for the
 *                                 bus's log. Pointer-free; answered
 *                                 synchronously at <= DISPATCH_LEVEL. A bus
 *                                 before 3.4.0.0 completes it
 *                                 STATUS_NOT_SUPPORTED and never keeps a
 *                                 device across a re-initialization.
 */

#ifndef XHCI98_XFER_H
#define XHCI98_XFER_H

/* xhci98_streams.h's device type; the next three function codes. */
#define XHCI98_XFER_DEVICE_TYPE     0x00000022UL
#define IOCTL_XHCI98_TRANSFER_POLICY                                          \
    CTL_CODE(XHCI98_XFER_DEVICE_TYPE, 0xF9A, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_XHCI98_TELEMETRY                                                \
    CTL_CODE(XHCI98_XFER_DEVICE_TYPE, 0xF9B, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_XHCI98_RESUME_STATE                                             \
    CTL_CODE(XHCI98_XFER_DEVICE_TYPE, 0xF9C, METHOD_NEITHER, FILE_ANY_ACCESS)

/* ---- the transfer policy (36.22) ------------------------------------- */

#define XHCI98_POLICY_SIGNATURE     0x50383958UL    /* 'X98P' */
#define XHCI98_POLICY_VERSION       1UL

/* The command size's range and step, in bytes: 64 KiB to 2 MiB in 4 KiB
 * steps, 1 MiB when the value is absent or not usable. */
#define XHCI98_POLICY_MIN_BYTES     0x00010000UL
#define XHCI98_POLICY_MAX_BYTES     0x00200000UL
#define XHCI98_POLICY_STEP_BYTES    0x00001000UL
#define XHCI98_POLICY_DEFAULT_BYTES 0x00100000UL

/* Status: the bus's answer, beside the IRP's NTSTATUS. */
#define XHCI98_POLICY_SUCCESS       0UL     /* STATUS_SUCCESS              */
#define XHCI98_POLICY_INVALID       1UL     /* signature, version or size;
                                             * STATUS_INVALID_PARAMETER    */

typedef struct _XHCI98_TRANSFER_POLICY {
    ULONG Signature;            /* in:  XHCI98_POLICY_SIGNATURE           */
    ULONG Version;              /* in:  XHCI98_POLICY_VERSION             */
    ULONG Size;                 /* in:  sizeof(XHCI98_TRANSFER_POLICY)    */
    ULONG Status;               /* out: XHCI98_POLICY_*                   */
    ULONG CommandBytes;         /* out: the command size in effect        */
    ULONG MinBytes;             /* out: XHCI98_POLICY_MIN_BYTES           */
    ULONG MaxBytes;             /* out: XHCI98_POLICY_MAX_BYTES           */
    ULONG StepBytes;            /* out: XHCI98_POLICY_STEP_BYTES          */
    ULONG Reserved[4];          /* in: 0; out: 0                          */
} XHCI98_TRANSFER_POLICY, *PXHCI98_TRANSFER_POLICY;

/* ---- the class driver's telemetry (36.21) ----------------------------- */

#define XHCI98_TELE_SIGNATURE       0x54383958UL    /* 'X98T' */
#define XHCI98_TELE_VERSION         1UL

/* Command lengths are counted in these buckets: up to 4 KiB, 16 KiB,
 * 64 KiB, 128 KiB, 256 KiB, 512 KiB, 1 MiB, 2 MiB, and above (the bus's
 * own data URBs use the same nine, XhciXstatBucket). */
#define XHCI98_TELE_BUCKETS         9UL
/* Commands outstanding as each one starts, this one included: 1, 2, 3-4,
 * 5-8, 9-16, 17 and more. */
#define XHCI98_TELE_DEPTHS          6UL

/* The record's words. A reader walks by Size, so a later version appends. */
#define XHCI98_TELE_SIG             0   /* XHCI98_TELE_SIGNATURE            */
#define XHCI98_TELE_VER             1   /* XHCI98_TELE_VERSION              */
#define XHCI98_TELE_SIZE            2   /* sizeof(XHCI98_TELEMETRY)         */
#define XHCI98_TELE_SEQUENCE        3   /* each send, from 1                */
#define XHCI98_TELE_COMMAND_BYTES   4   /* the command size it advertises   */
#define XHCI98_TELE_POLICY_SOURCE   5   /* XHCI98_TELE_SRC_*                */
#define XHCI98_TELE_STREAMED        6   /* streams open                     */
#define XHCI98_TELE_QUEUE_DEPTH     7   /* commands in flight at most       */
#define XHCI98_TELE_ACTIVE          8   /* commands in flight now           */
#define XHCI98_TELE_ACTIVE_MAX      9   /* ... at most, since its start     */
#define XHCI98_TELE_COMMANDS        10  /* commands started                 */
#define XHCI98_TELE_DATA_COMMANDS   11  /* ... with a data phase            */
#define XHCI98_TELE_DATA_LO         12  /* their data lengths' sum          */
#define XHCI98_TELE_DATA_HI         13
#define XHCI98_TELE_LENGTH_MAX      14  /* the longest                      */
#define XHCI98_TELE_HIST            15  /* XHCI98_TELE_BUCKETS words        */
#define XHCI98_TELE_DEPTH_HIST      24  /* XHCI98_TELE_DEPTHS words         */
#define XHCI98_TELE_COUNTERS        30  /* UAS_CTR_*: 8 words (uas.h)       */
/* Starts whose transfer-policy question (36.22) went each way but the
 * bus's valid answer, since the driver loaded. */
#define XHCI98_TELE_UNSUPPORTED     38  /* an older bus                     */
#define XHCI98_TELE_MALFORMED       39  /* a reply out of contract          */
#define XHCI98_TELE_TRANSPORT       40  /* the request itself failed        */
#define XHCI98_TELE_WORDS           41UL

/* Where its command size came from. */
#define XHCI98_TELE_SRC_UNASKED     0UL /* not asked yet: 64 KiB            */
#define XHCI98_TELE_SRC_BUS         1UL /* the bus's valid reply            */
#define XHCI98_TELE_SRC_UNSUPPORTED 2UL /* an older bus: 64 KiB             */
#define XHCI98_TELE_SRC_MALFORMED   3UL /* a reply out of contract: 64 KiB  */
#define XHCI98_TELE_SRC_TRANSPORT   4UL /* removal or no memory: 64 KiB     */
#define XHCI98_TELE_SRC_DEVICE      5UL /* the device's own usbstor limit  */

typedef struct _XHCI98_TELEMETRY {
    ULONG Word[XHCI98_TELE_WORDS];
} XHCI98_TELEMETRY, *PXHCI98_TELEMETRY;

/* ---- the resume state (45.23) ---------------------------------------- */

#define XHCI98_RESUME_SIGNATURE     0x52383958UL    /* 'X98R' */
#define XHCI98_RESUME_VERSION       1UL

/* Op. */
#define XHCI98_RESUME_OP_QUERY      0UL /* State and Epoch only             */
#define XHCI98_RESUME_OP_DECLARE    1UL /* the function is quiet: nothing in
                                         * flight, nothing sent until its
                                         * wake check (void at its next
                                         * transfer and at every resume's
                                         * end)                             */
#define XHCI98_RESUME_OP_WITHDRAW   2UL /* any declaration void             */
#define XHCI98_RESUME_OP_REPORT     3UL /* Value, the wake check's outcome,
                                         * for the bus's log only           */

/* Status, beside the IRP's NTSTATUS. */
#define XHCI98_RESUME_SUCCESS       0UL /* STATUS_SUCCESS                   */
#define XHCI98_RESUME_INVALID       1UL /* signature, version, size or op;
                                         * STATUS_INVALID_PARAMETER         */

/* State: the controller's, as the request finds it. */
#define XHCI98_RESUME_RUNNING       0UL /* its last resume finished: none
                                         * re-initialized it, or the
                                         * thread's pass that followed one
                                         * has kept or dropped the devices  */
#define XHCI98_RESUME_PENDING       1UL /* suspended, or resumed with that
                                         * pass still to end                */
#define XHCI98_RESUME_FAILED        2UL /* a resume failed, or the
                                         * controller is contained or
                                         * failed                           */

/* REPORT's Value: the wake check's outcome (xhciuas.sys). */
#define XHCI98_RESUME_RPT_RELEASED  1UL /* released, nothing to check       */
#define XHCI98_RESUME_RPT_SAME      2UL /* released, the epoch unchanged    */
#define XHCI98_RESUME_RPT_VERIFIED  3UL /* released, every LUN's medium
                                         * checked                          */
#define XHCI98_RESUME_RPT_FAILED    4UL /* failed: the port cycled          */

typedef struct _XHCI98_RESUME_STATE {
    ULONG Signature;            /* in:  XHCI98_RESUME_SIGNATURE           */
    ULONG Version;              /* in:  XHCI98_RESUME_VERSION             */
    ULONG Size;                 /* in:  sizeof(XHCI98_RESUME_STATE)       */
    ULONG Op;                   /* in:  XHCI98_RESUME_OP_*                */
    ULONG Value;                /* in:  REPORT's value; else 0            */
    ULONG Status;               /* out: XHCI98_RESUME_SUCCESS / _INVALID  */
    ULONG State;                /* out: XHCI98_RESUME_RUNNING etc.        */
    ULONG Epoch;                /* out: the device's resume epoch: one
                                 * more each time a re-initialization
                                 * kept it                                */
    ULONG Reserved[2];          /* in: 0; out: 0                          */
} XHCI98_RESUME_STATE, *PXHCI98_RESUME_STATE;

#endif /* XHCI98_XFER_H */
