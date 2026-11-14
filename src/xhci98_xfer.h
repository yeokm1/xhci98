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
 *                                 for under the caller's lock. Since
 *                                 3.4.2.0 (issue 26) the same request also
 *                                 carries a PXHCI98_FAILURES, told apart by
 *                                 its first word; a bus before then refuses
 *                                 it STATUS_INVALID_PARAMETER.
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

/* ---- the class driver's last failures (issue 26) ---------------------- */

/*
 * The last XHCI98_FAIL_RECORDS commands that did not end GOOD, oldest
 * overwritten, as IOCTL_XHCI98_TELEMETRY's second record: a header, then
 * the ring. COUNT is how many have been recorded since the class driver's
 * start; the newest is entry (COUNT - 1) % XHCI98_FAIL_RECORDS. Each entry
 * is XHCI98_FAIL_WORDS words:
 *   word 0  event 31:24 (XHCI98_FAIL_E_*), origin 23:16
 *           (XHCI98_FAIL_O_*), tag 15:8, the CDB's operation code 7:0
 *   word 1  SRB status 31:24 (without its flag bits), SCSI status 23:16,
 *           ASC 15:8, ASCQ 7:0
 *   word 2  sense key 31:24 (0xFF: no sense), the command's slot state
 *           23:16, its transfers 15:8 (XHCI98_FAIL_X_*), the timeout in
 *           seconds 7:0 (255 and above as 255)
 *   word 3  the command's data length, in bytes
 */
#define XHCI98_FAIL_SIGNATURE       0x46383958UL    /* 'X98F' */
#define XHCI98_FAIL_VERSION         1UL
#define XHCI98_FAIL_RECORDS         8UL
#define XHCI98_FAIL_WORDS           4UL

#define XHCI98_FAIL_SIG             0
#define XHCI98_FAIL_VER             1
#define XHCI98_FAIL_SIZE            2
#define XHCI98_FAIL_COUNT           3
#define XHCI98_FAIL_AT              4
#define XHCI98_FAIL_TOTAL_WORDS     (XHCI98_FAIL_AT + XHCI98_FAIL_RECORDS * \
                                     XHCI98_FAIL_WORDS)

/* Events. */
#define XHCI98_FAIL_E_SENSE         1UL /* ended CHECK CONDITION            */
#define XHCI98_FAIL_E_TIMEOUT       2UL /* its timeout ran out (recorded
                                         * then, before any recovery)       */
#define XHCI98_FAIL_E_ENDED         3UL /* ended by recovery or teardown:
                                         * the SRB status says how          */
#define XHCI98_FAIL_E_REFUSED       4UL /* a RESPONSE IU refused it         */
#define XHCI98_FAIL_E_LOCAL         5UL /* answered by the class driver,
                                         * never sent (XhciUasNoAtaPass-
                                         * Through)                         */
#define XHCI98_FAIL_E_OTHER         6UL /* another status from the device
                                         * (BUSY, a short transfer's error) */
#define XHCI98_FAIL_E_FAST_RESET    7UL /* XhciUasFastReset: the device
                                         * reset at once - a second timeout
                                         * after an answered ABORT TASK, or
                                         * a data cancel that never came
                                         * back (its transfers say which)   */
#define XHCI98_FAIL_E_FINAL_TIMEOUT 8UL /* task 49.39: timed out with its
                                         * final IU in and its data
                                         * transfer outstanding - the
                                         * device reset, no ABORT TASK      */
#define XHCI98_FAIL_E_REPLAYED      9UL /* task 49.39: answered by the class
                                         * driver with the drive's own
                                         * earlier refusal (5/20/00) of
                                         * that ATA PASS-THROUGH, never
                                         * sent                             */
#define XHCI98_FAIL_E_GIVEN_UP     10UL /* task 49.39: due for event 8's
                                         * reset after 3 of them with no
                                         * data delivered between: the
                                         * drive given up until replugged */

/* Origins. */
#define XHCI98_FAIL_O_CLASS         0UL /* the storage class driver's SRB   */
#define XHCI98_FAIL_O_PROGRAM       1UL /* a program's pass-through or SMART */
#define XHCI98_FAIL_O_INTERNAL      2UL /* the class driver's own command   */

/* Transfers, as the event found them. */
#define XHCI98_FAIL_X_STATUS        0x01UL  /* the status read in flight    */
#define XHCI98_FAIL_X_DATA          0x02UL  /* the data transfer in flight  */
#define XHCI98_FAIL_X_COMMAND       0x04UL  /* the COMMAND IU in flight     */
#define XHCI98_FAIL_X_CANCELLING    0x08UL  /* one of them being cancelled  */
#define XHCI98_FAIL_X_FINAL         0x10UL  /* its SENSE or RESPONSE IU in  */
#define XHCI98_FAIL_X_DATA_POSTED   0x20UL  /* a data transfer was posted   */

typedef struct _XHCI98_FAILURES {
    ULONG Word[XHCI98_FAIL_TOTAL_WORDS];
} XHCI98_FAILURES, *PXHCI98_FAILURES;

/*
 * Task 49.39 (design record 17 section 4.12.9): version 2, the same header
 * and ring, then each entry's time - the class driver's own seconds since
 * its start, entry i's at XHCI98_FAIL_TIMES_AT + i - and the LUNs' ATA
 * refusal bits (design record 13 section 16.16; LUN n's two bits at 2n,
 * bit 0 for (12), bit 1 for (16), LUNs 0 to 15). A bus before 3.5.0.0
 * refuses it; the class driver then sends version 1.
 */
#define XHCI98_FAIL_VERSION2        2UL
#define XHCI98_FAIL_TIMES_AT        XHCI98_FAIL_TOTAL_WORDS
#define XHCI98_FAIL_ATA_AT          (XHCI98_FAIL_TIMES_AT + XHCI98_FAIL_RECORDS)
#define XHCI98_FAIL_TOTAL_WORDS2    (XHCI98_FAIL_ATA_AT + 1)

typedef struct _XHCI98_FAILURES2 {
    ULONG Word[XHCI98_FAIL_TOTAL_WORDS2];
} XHCI98_FAILURES2, *PXHCI98_FAILURES2;

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
#define XHCI98_RESUME_OP_CYCLE      4UL /* the function's own device reset
                                         * failed: the device is cycled, the
                                         * cycle charged to its location as
                                         * a failed RESET_PORT's (task
                                         * 45.41); a bus before 3.4.0.0
                                         * refuses the op as INVALID        */

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
