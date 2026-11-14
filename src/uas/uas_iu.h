/*
 * uas_iu.h - the pure core of xhciuas.sys, the project's USB Attached SCSI
 * class driver (roadmap-hcd.md task 31-A.2).
 *
 * Everything here is DDK-free and host-tested (test\test_uas.c): the
 * information units the UAS class specification defines (COMMAND, SENSE,
 * RESPONSE, TASK MANAGEMENT, READ READY, WRITE READY), the SAM LUN encoding
 * they carry, the tag allocator, the walk of a configuration descriptor for
 * the UAS alternate setting and its four Pipe Usage descriptors, the REPORT
 * LUNS parse, the SCSI status to SRB status fold, and the storage id strings
 * built from INQUIRY data. The facts are the UAS specification's (USB
 * Attached SCSI Protocol 1.0, and T10's UAS) and SAM's; nothing here is taken
 * from another driver.
 *
 * All multi-byte IU fields are big-endian on the wire.
 *
 * IRQL: every function is pure and callable at any IRQL; none touches a
 * lock. Callers hold whatever lock guards the structure they pass.
 */

#ifndef UAS_IU_H
#define UAS_IU_H

#include "../xhci_compat.h"

/* Information unit ids (UAS 1.0 section 6.2). */
#define UAS_IU_COMMAND              0x01
#define UAS_IU_SENSE                0x03
#define UAS_IU_RESPONSE             0x04
#define UAS_IU_TASK_MGMT            0x05
#define UAS_IU_READ_READY           0x06
#define UAS_IU_WRITE_READY          0x07

/* Fixed IU lengths. A COMMAND IU is 32 bytes for a CDB of up to 16. */
#define UAS_IU_COMMAND_LENGTH       32
#define UAS_IU_SENSE_HEADER         16
#define UAS_IU_RESPONSE_LENGTH      8
#define UAS_IU_TASK_MGMT_LENGTH     16
#define UAS_IU_READY_LENGTH         4
#define UAS_SENSE_MAX               252

/* Task attributes, COMMAND IU byte 4 bits 2:0. */
#define UAS_TASK_SIMPLE             0
#define UAS_TASK_HEAD_OF_QUEUE      1
#define UAS_TASK_ORDERED            2

/* Task management functions (TASK MANAGEMENT IU byte 4). */
#define UAS_TMF_ABORT_TASK          0x01
#define UAS_TMF_ABORT_TASK_SET      0x02
#define UAS_TMF_CLEAR_TASK_SET      0x04
#define UAS_TMF_LOGICAL_UNIT_RESET  0x08
#define UAS_TMF_I_T_NEXUS_RESET     0x10
#define UAS_TMF_QUERY_TASK          0x80

/* RESPONSE IU response codes (byte 7). */
#define UAS_RC_TMF_COMPLETE         0x00
#define UAS_RC_INVALID_IU           0x02
#define UAS_RC_TMF_NOT_SUPPORTED    0x04
#define UAS_RC_TMF_FAILED           0x05
#define UAS_RC_TMF_SUCCEEDED        0x08
#define UAS_RC_INCORRECT_LUN        0x09
#define UAS_RC_OVERLAPPED_TAG       0x0A

/* Pipe Usage descriptor (UAS 1.0 section 5.3.3): bLength 4, type 0x24. */
#define UAS_DESC_PIPE_USAGE         0x24
#define UAS_PIPE_COMMAND            1
#define UAS_PIPE_STATUS             2
#define UAS_PIPE_DATA_IN            3
#define UAS_PIPE_DATA_OUT           4
#define UAS_PIPES                   4

/* The interface the driver binds: Mass Storage, SCSI, UAS. */
#define UAS_CLASS                   0x08
#define UAS_SUBCLASS                0x06
#define UAS_PROTOCOL                0x62

/* SCSI status bytes the fold distinguishes. */
#define UAS_SCSI_GOOD               0x00
#define UAS_SCSI_CHECK_CONDITION    0x02
#define UAS_SCSI_BUSY               0x08
#define UAS_SCSI_TASK_SET_FULL      0x28

/*
 * Tags. A tag is 16 bits on the wire; with streams it is also the stream id,
 * which is why tag 0 is never handed out (stream 0 is reserved). The
 * allocator holds at most UAS_MAX_TAGS, which bounds the queue depth.
 */
#define UAS_MAX_TAGS                32

typedef struct _UAS_TAGS {
    ULONG Count;    /* tags 1..Count may be handed out */
    ULONG Next;     /* where the next search starts, so a freed tag rests */
    ULONG Map;      /* bit (tag - 1) set while that tag is in use */
} UAS_TAGS, *PUAS_TAGS;

VOID UasTagInit(PUAS_TAGS tags, ULONG count);
ULONG UasTagAlloc(PUAS_TAGS tags);
BOOLEAN UasTagFree(PUAS_TAGS tags, ULONG tag);
BOOLEAN UasTagBusy(const UAS_TAGS *tags, ULONG tag);
ULONG UasTagsInUse(const UAS_TAGS *tags);

/* IU encoders: the length written, or 0 when the buffer or input cannot
 * make a valid IU. */
ULONG UasIuBuildCommand(PUCHAR iu, ULONG size, ULONG tag, ULONG lun,
                        ULONG attribute, const UCHAR *cdb, ULONG cdbLength);
ULONG UasIuBuildTaskMgmt(PUCHAR iu, ULONG size, ULONG tag, ULONG function,
                         ULONG taskTag, ULONG lun);

/* The decoded fields of one received IU. */
typedef struct _UAS_IU_INFO {
    ULONG Id;
    ULONG Tag;
    ULONG Status;           /* SENSE: SCSI status */
    ULONG Qualifier;        /* SENSE: status qualifier */
    ULONG SenseLength;      /* SENSE: sense bytes present in the buffer */
    ULONG ResponseCode;     /* RESPONSE */
    ULONG ResponseInfo;     /* RESPONSE: the three additional bytes */
} UAS_IU_INFO, *PUAS_IU_INFO;

#define UAS_PARSE_OK            0
#define UAS_PARSE_SHORT         1   /* shorter than its IU type requires */
#define UAS_PARSE_UNKNOWN       2   /* an IU id the host never receives */

ULONG UasIuParse(const UCHAR *buf, ULONG length, PUAS_IU_INFO info);

/* SAM LUN encoding, the 8-byte field of COMMAND and TASK MANAGEMENT IUs and
 * of REPORT LUNS entries. Peripheral addressing below 256, flat to 16383. */
BOOLEAN UasLunEncode(ULONG lun, PUCHAR out);
BOOLEAN UasLunDecode(const UCHAR *in, PULONG lun);

/* The UAS interface found in a configuration descriptor. */
typedef struct _UAS_CONFIG_INFO {
    ULONG InterfaceNumber;
    ULONG AlternateSetting;
    ULONG EndpointCount;            /* endpoints in that alternate setting */
    ULONG EndpointAddress[UAS_PIPES];   /* by pipe id - 1 */
    ULONG MaxPacket[UAS_PIPES];
    ULONG MaxStreams[UAS_PIPES];    /* 2^MaxStreams from the companion, 0 none */
    BOOLEAN SuperSpeed;             /* endpoint companions were present */
} UAS_CONFIG_INFO, *PUAS_CONFIG_INFO;

#define UAS_CFG_OK              0
#define UAS_CFG_NO_INTERFACE    1   /* no 08/06/62 alternate setting */
#define UAS_CFG_BAD_PIPES       2   /* the four pipe usages are not all there */
#define UAS_CFG_MALFORMED       3

ULONG UasParseConfig(const UCHAR *cfg, ULONG length, PUAS_CONFIG_INFO info);

/* REPORT LUNS: the LUNs listed, at most max, each decodable and below 256.
 * Returns the count; an unparseable list returns 0. */
ULONG UasParseReportLuns(const UCHAR *buf, ULONG length, PUCHAR luns,
                         ULONG max);

/* SRB status values the fold returns (srb.h's numbers, repeated here so the
 * fold can be host-tested without the DDK). */
#define UAS_SRB_SUCCESS         0x01
#define UAS_SRB_ERROR           0x04
#define UAS_SRB_BUSY            0x05
#define UAS_SRB_DATA_OVERRUN    0x12

ULONG UasSrbStatus(ULONG scsiStatus, ULONG requested, ULONG transferred);

/* CDB data direction by opcode, for an SRB that names none. */
#define UAS_DIR_NONE            0
#define UAS_DIR_IN              1
#define UAS_DIR_OUT             2

ULONG UasCdbDirection(UCHAR opcode);
/* The same for a whole CDB: ATA PASS-THROUGH (12) and (16) carry their
 * direction in the CDB (T_DIR), every other opcode is UasCdbDirection's. */
ULONG UasCdbDataDirection(const UCHAR *cdb, ULONG cdbLength);

/*
 * Storage ids from the INQUIRY data of a LUN, as the class INFs on each
 * target match them (design in uas_pdo.c). Each builder writes ASCII into
 * out, NUL-separated and NUL-terminated (a MULTI_SZ for the list kinds),
 * returning the bytes written including the final NUL, or 0 when it does not
 * fit or the INQUIRY data is too short.
 */
#define UAS_ID_DEVICE           0
#define UAS_ID_HARDWARE         1
#define UAS_ID_COMPATIBLE       2
#define UAS_ID_INSTANCE         3
#define UAS_ID_TEXT             4

#define UAS_INQUIRY_LENGTH      36

ULONG UasBuildId(ULONG kind, const UCHAR *inquiry, ULONG inquiryLength,
                 const char *serial, ULONG lun, char *out, ULONG size);

/*
 * Task 36.21's telemetry (xhci98_xfer.h): the bucket of a command's data
 * length - up to 4 KiB, 16, 64, 128, 256, 512 KiB, 1 MiB, 2 MiB, above -
 * and of the commands in flight as one starts - 1, 2, 3-4, 5-8, 9-16, 17
 * and more (0 reads as 1). The bus's own buckets are the same
 * (xhci_xstat.c). And a sum's carry by hand.
 */
#define UAS_TELE_BUCKETS        9UL
#define UAS_TELE_DEPTHS         6UL

ULONG UasTeleBucket(ULONG bytes);
ULONG UasTeleDepthBucket(ULONG active);
VOID UasTeleAdd64(PULONG lo, PULONG hi, ULONG v);

/*
 * Task 36.22: the command size from the bus's answer to
 * IOCTL_XHCI98_TRANSFER_POLICY (xhci98_xfer.h). `sent` says how the
 * request itself went; `reply` is the request structure as it came back
 * (read only when sent is UAS_POLICY_SENT_OK). Returns the size in bytes
 * - the bus's when its reply keeps the contract, 64 KiB otherwise - and
 * sets *source to the XHCI98_TELE_SRC_* that says which.
 */
#define UAS_POLICY_SENT_OK          0UL /* the IRP succeeded              */
#define UAS_POLICY_SENT_UNSUPPORTED 1UL /* NOT_SUPPORTED, INVALID_DEVICE_
                                         * REQUEST: an older bus          */
#define UAS_POLICY_SENT_REFUSED     2UL /* INVALID_PARAMETER: the bus
                                         * refused the request's header  */
#define UAS_POLICY_SENT_FAILED      3UL /* anything else: removal, no
                                         * memory, no IRP                 */
#define UAS_POLICY_FALLBACK_BYTES   0x10000UL

struct _XHCI98_TRANSFER_POLICY;
ULONG UasPolicyValue(ULONG sent, const struct _XHCI98_TRANSFER_POLICY *reply,
                     PULONG source);

/*
 * Task 36.22, owner 2026-10-09: Microsoft's per-device limit, the
 * MaximumTransferLength value (bytes) under
 * HKLM\SYSTEM\CurrentControlSet\Control\usbstor\VVVVPPPP (KB 2581464),
 * honoured for this one device over the bus's answer. `busBytes` and
 * `busSource` are what UasPolicyValue gave; `found` says whether the value
 * exists, `type` and `length` what it is, `value` its four bytes. Returns
 * the command size and sets *source (XHCI98_TELE_SRC_DEVICE when the value
 * was taken, else busSource) and *why (UAS_DEVLIM_*). A value is taken only
 * when it is a four-byte REG_DWORD from 65535 to 2097152 - KB 2581464's
 * range, 65535 to 2097120, with its top widened to 2097152, which is what
 * the existing tools write for 2 MiB (owner, 2026-10-10) - and only when
 * the bus answered (XHCI98_TELE_SRC_BUS): an older
 * bus, or a question that failed, keeps 64 KiB whatever the value says. A
 * taken value is rounded to the nearest 4 KiB, ties up, so it lands on the
 * contract's grid of 64 KiB to 2 MiB in 4 KiB steps: nearest moves a value
 * least (never by more than 2048 bytes, a tie going up), and takes
 * 65535 and 2097120 - Microsoft's spellings of 64 KiB and 2 MiB - and
 * 2097152 to 64 KiB and 2 MiB. It is a normalisation onto the grid, not a
 * byte ceiling: the size taken can exceed the value by up to 2048 bytes.
 * The bridge caps in use - 64 KiB, 120 KiB (240 sectors), 512 KiB, 1 MiB -
 * are on the grid already and do not move.
 */
#define UAS_DEVLIM_ABSENT           0UL /* no value: the bus's answer     */
#define UAS_DEVLIM_USED             1UL /* taken for this device          */
#define UAS_DEVLIM_TYPE             2UL /* not a four-byte REG_DWORD      */
#define UAS_DEVLIM_RANGE            3UL /* outside 65535 to 2097152       */
#define UAS_DEVLIM_NO_BUS           4UL /* valid, but the bus did not
                                         * answer: 64 KiB stands          */
#define UAS_DEVLIM_MIN_BYTES        65535UL
#define UAS_DEVLIM_MAX_BYTES        2097152UL
#define UAS_DEVLIM_REG_DWORD        4UL /* REG_DWORD, for the pure core   */

ULONG UasDeviceLimit(ULONG busBytes, ULONG busSource, ULONG found,
                     ULONG type, ULONG length, ULONG value, PULONG source,
                     PULONG why);

/*
 * Task 45.23 (design record 20 section 4.3.3): the wake check and the
 * medium check of a drive kept across a resume's re-initialization.
 *
 * UasSenseClass folds the sense data of a CHECK CONDITION, fixed (70h,
 * 71h) or descriptor (72h, 73h) format (SPC), into what the checks act on:
 * a UNIT ATTENTION, one saying the medium may have changed (ASC 28h), NOT
 * READY with no medium (ASC 3Ah) or becoming ready / needing a start
 * (ASC 04h), ILLEGAL REQUEST, or anything else.
 */
#define UAS_SENSE_OTHER             0UL
#define UAS_SENSE_UA                1UL
#define UAS_SENSE_UA_MEDIUM         2UL
#define UAS_SENSE_NO_MEDIUM         3UL
#define UAS_SENSE_BECOMING          4UL
#define UAS_SENSE_ILLEGAL           5UL

ULONG UasSenseClass(const UCHAR *sense, ULONG length);

/* Vital product data page 0x00 as `length` bytes of it came: 1 when it is
 * page 0x00 and its whole advertised list came (UasVpdListComplete); 1
 * when such a complete list names `page` (UasVpdListHas, 0 for an
 * incomplete one). */
ULONG UasVpdListComplete(const UCHAR *page0, ULONG length);
ULONG UasVpdListHas(const UCHAR *page0, ULONG length, UCHAR page);

/*
 * One LUN's identity as the medium check records and compares it: the
 * removable-medium bit of INQUIRY, the unit serial number (page 0x80, its
 * bytes after the header) and the device identification page (0x83, its
 * designators), each read whole (a page longer than these bounds, or one
 * that did not come in full, is a failed read) and empty when page 0x00
 * does not list it, and READ
 * CAPACITY's answer - the last LBA (eight bytes, big-endian; READ CAPACITY
 * (16)'s when (10) reports FFFFFFFFh) and the block length - or that the
 * LUN reported no medium. Read is 1 when every read the record needs
 * succeeded; MediumChanged when one met a UNIT ATTENTION with ASC 28h.
 */
#define UAS_ID_SERIAL_MAX           251UL
#define UAS_ID_DEVID_MAX            251UL
#define UAS_ID_CAP_FAILED           0UL
#define UAS_ID_CAP_VALID            1UL
#define UAS_ID_CAP_NO_MEDIUM        2UL

typedef struct _UAS_LUN_ID {
    ULONG Read;
    ULONG Removable;
    ULONG MediumChanged;
    ULONG SerialLength;
    UCHAR Serial[UAS_ID_SERIAL_MAX];
    ULONG DevIdLength;
    UCHAR DevId[UAS_ID_DEVID_MAX];
    ULONG CapState;
    UCHAR Cap[12];
} UAS_LUN_ID, *PUAS_LUN_ID;

/* 1 when `now` is the medium `was` recorded: both read, no medium change
 * reported, the same removable bit, and either removable with no medium
 * both times, or not removable with a serial or a device identification
 * (non-empty), each equal, and the same capacity. Anything else is 0. */
ULONG UasIdMatch(const UAS_LUN_ID *was, const UAS_LUN_ID *now);

/*
 * The wake check's next step. busKnown: the bus answered the sleep's
 * withdrawal (not STATUS_NOT_SUPPORTED); withdrawOk: that withdrawal
 * succeeded; declareTried: a quiet declaration was sent at the sleep
 * (whatever its outcome); queryOk, state and epoch: the bus's answer to the
 * query now (XHCI98_RESUME_*); verified: the epoch last verified;
 * pastDeadline: the check's thirty seconds have run out.
 */
#define UAS_WAKE_RELEASE            0UL /* nothing to check               */
#define UAS_WAKE_SAME               1UL /* not re-initialized: release    */
#define UAS_WAKE_CHECK              2UL /* kept across one: the medium    */
#define UAS_WAKE_FAIL               3UL /* Dead, the port cycled          */
#define UAS_WAKE_WAIT               4UL /* the bus's pass still to end    */

ULONG UasWakeDecide(ULONG busKnown, ULONG withdrawOk, ULONG declareTried,
                    ULONG queryOk, ULONG state, ULONG epoch, ULONG verified,
                    ULONG pastDeadline);

/*
 * Task 45.52 (design record 13 section 16): a program's SCSI and ATA
 * pass-through and the SMART requests the disk class driver sends down,
 * served by the LUN's PDO through the engine.
 *
 * The request headers are read and written at byte offsets, one layout
 * per pointer size, so one host build checks all three callers: the x86
 * driver's (UAS_PT_LAYOUT_32), a 64-bit process on the amd64 driver
 * (UAS_PT_LAYOUT_64) and a 32-bit process there (WOW64, the ..._32
 * structures: UAS_PT_LAYOUT_32 again). Offsets and sizes are the WDK 7.1
 * declarations' (ntddscsi.h); the driver asserts them against the DDK
 * structures where its headers carry them.
 *
 *   SCSI_PASS_THROUGH(_DIRECT)  32: 44 bytes, DataBufferOffset or
 *                               DataBuffer at 20 (4), SenseInfoOffset 24,
 *                               Cdb 28.  64: 56, at 24 (8), 32, Cdb 36.
 *   ATA_PASS_THROUGH_EX(_DIRECT) 32: 40, DataBufferOffset or DataBuffer
 *                               at 20 (4), PreviousTaskFile 24,
 *                               CurrentTaskFile 32.  64: 48, at 24 (8),
 *                               32, 40.
 */
#define UAS_PT_LAYOUT_32            0UL
#define UAS_PT_LAYOUT_64            1UL

#define UAS_PT_SCSI                 0UL /* IOCTL_SCSI_PASS_THROUGH        */
#define UAS_PT_SCSI_DIRECT          1UL /* IOCTL_SCSI_PASS_THROUGH_DIRECT */
#define UAS_PT_ATA                  2UL /* IOCTL_ATA_PASS_THROUGH         */
#define UAS_PT_ATA_DIRECT           3UL /* IOCTL_ATA_PASS_THROUGH_DIRECT  */

/* The checks' answers; the driver maps them to NTSTATUS. */
#define UAS_PT_OK                   0UL
#define UAS_PT_INVALID              1UL /* STATUS_INVALID_PARAMETER       */
#define UAS_PT_TOO_SMALL            2UL /* STATUS_BUFFER_TOO_SMALL        */
#define UAS_PT_UNKNOWN              3UL /* not this driver's: answered as
                                         * an unknown IOCTL               */

#define UAS_PT_SENSE_MAX            UAS_SENSE_MAX
#define UAS_PT_TIMEOUT_DEFAULT      30UL    /* seconds: 0 asks for this   */
#define UAS_PT_TIMEOUT_MAX          120UL   /* seconds: the clamp         */

/* ATA_PASS_THROUGH_EX AtaFlags (ntddscsi.h) the driver accepts. */
#define UAS_ATA_DRDY_REQUIRED       0x01UL
#define UAS_ATA_DATA_IN             0x02UL
#define UAS_ATA_DATA_OUT            0x04UL
#define UAS_ATA_48BIT               0x08UL
#define UAS_ATA_USE_DMA             0x10UL

/*
 * One request as the checks planned it. For the buffered forms the data
 * and sense regions are offsets in the system buffer; for the direct
 * forms DataPtrLo/Hi is the caller's pointer, which the driver locks.
 * The ATA forms keep the task files they sent, for the registers back.
 */
typedef struct _UAS_PT_PLAN {
    ULONG Kind;
    ULONG Layout;
    ULONG HeaderSize;
    ULONG Dir;                      /* UAS_DIR_*                          */
    ULONG DataOffset;
    ULONG DataLength;
    ULONG DataPtrLo;
    ULONG DataPtrHi;
    ULONG SenseOffset;
    ULONG SenseLength;
    ULONG Timeout;                  /* seconds, clamped                   */
    ULONG CdbLength;
    UCHAR Cdb[16];
    UCHAR Prev[8];                  /* ATA: as sent                       */
    UCHAR Cur[8];
    ULONG Extend;                   /* ATA: 48-bit                        */
} UAS_PT_PLAN, *PUAS_PT_PLAN;

ULONG UasPtHeaderSize(ULONG kind, ULONG layout);

/*
 * Checks a request header in buf (the system buffer: inLength bytes of the
 * caller's input, max(inLength, outLength) long) and plans it, or refuses
 * it. maxTransfer is the LUN's MaximumTransferLength.
 */
ULONG UasPtPlan(ULONG kind, ULONG layout, const UCHAR *buf, ULONG inLength,
                ULONG outLength, ULONG maxTransfer, PUAS_PT_PLAN plan);

/* The timeout clamp: 0 is the default, the rest at most the maximum. */
ULONG UasPtTimeout(ULONG seconds);

/* 1 for a command that addresses other targets (COPY, COMPARE, COPY AND
 * VERIFY, every service action of 83h), which pass-through refuses. */
ULONG UasPtMultiTarget(UCHAR opcode);

/*
 * The ATA PASS-THROUGH (16) CDB for a task file (prev and cur, eight bytes
 * each as ATA_PASS_THROUGH_EX carries them), AtaFlags and a data length;
 * UAS_PT_OK or UAS_PT_INVALID. ckCond asks the drive's registers back; it
 * is honoured for a command with no data only.
 */
ULONG UasAtaCdb(ULONG flags, const UCHAR *prev, const UCHAR *cur,
                ULONG length, ULONG ckCond, PUCHAR cdb);

/*
 * The drive's registers from sense data: an ATA Status Return descriptor
 * (09h) in descriptor format, or fixed format with ASC/ASCQ 00h/1Dh.
 * prev and cur receive eight bytes each, in ATA_PASS_THROUGH_EX's order,
 * output meaning: [0] Error, [1] Count, [2..4] LBA, [5] Device, [6]
 * Status. UAS_ATAREG_PARTIAL: fixed format said an upper byte is non-zero,
 * which that format cannot carry.
 */
#define UAS_ATAREG_NONE             0UL
#define UAS_ATAREG_OK               1UL
#define UAS_ATAREG_PARTIAL          2UL

ULONG UasAtaRegsFromSense(const UCHAR *sense, ULONG length, PUCHAR prev,
                          PUCHAR cur);

/* SRB status values the outcome reads (srb.h). */
#define UAS_SRB_ABORTED             0x02
#define UAS_SRB_INVALID_REQUEST     0x06
#define UAS_SRB_NO_DEVICE           0x08
#define UAS_SRB_TIMEOUT             0x09

/* How a pass-through ends; the driver maps it to NTSTATUS. */
#define UAS_PTR_SUCCESS             0UL
#define UAS_PTR_TIMEOUT             1UL /* STATUS_IO_TIMEOUT              */
#define UAS_PTR_NO_DEVICE           2UL /* STATUS_NO_SUCH_DEVICE          */
#define UAS_PTR_BUSY                3UL /* STATUS_DEVICE_BUSY             */
#define UAS_PTR_INVALID             4UL /* STATUS_INVALID_PARAMETER       */
#define UAS_PTR_IO_ERROR            5UL /* STATUS_IO_DEVICE_ERROR         */
#define UAS_PTR_NOT_SUPPORTED       6UL /* STATUS_NOT_SUPPORTED           */

/*
 * Writes a finished request's outcome into buf (outLength bytes may be
 * returned) and returns UAS_PTR_*, with *information the bytes to return
 * (0 unless UAS_PTR_SUCCESS). srbStatus is the SRB's status without its
 * flag bits; sense and senseLength the autosense the engine copied (0 when
 * none); moved the bytes the data phase moved.
 */
ULONG UasPtResult(const UAS_PT_PLAN *plan, PUCHAR buf, ULONG outLength,
                  ULONG srbStatus, ULONG scsiStatus, ULONG moved,
                  const UCHAR *sense, ULONG senseLength,
                  PULONG information);

/*
 * The SMART requests as disk.sys sends them: IOCTL_SCSI_MINIPORT with an
 * SRB_IO_CONTROL (28 bytes: HeaderLength 0, Signature 4, Timeout 12,
 * ControlCode 16, ReturnCode 20, Length 24; ntddscsi.h) whose signature is
 * "SCSIDISK", and the program's SENDCMDINPARAMS after it (design record 13
 * section 16.9).
 */
#define UAS_SIOC_LENGTH             28UL
#define UAS_SIOC_SMART_VERSION      0x1B0500UL
#define UAS_SIOC_IDENTIFY           0x1B0501UL
#define UAS_SIOC_READ_ATTRIBS       0x1B0502UL
#define UAS_SIOC_READ_THRESHOLDS    0x1B0503UL
#define UAS_SIOC_ENABLE_SMART       0x1B0504UL
#define UAS_SIOC_DISABLE_SMART      0x1B0505UL
#define UAS_SIOC_RETURN_STATUS      0x1B0506UL
#define UAS_SIOC_AUTOSAVE           0x1B0507UL
#define UAS_SIOC_SAVE_ATTRIBS       0x1B0508UL
#define UAS_SIOC_OFFLINE_DIAGS      0x1B0509UL
#define UAS_SIOC_AUTO_OFFLINE       0x1B050AUL
#define UAS_SIOC_READ_LOG           0x1B050BUL
#define UAS_SIOC_WRITE_LOG          0x1B050CUL

#define UAS_SMART_IN_LENGTH         32UL    /* SENDCMDINPARAMS less bBuffer */
#define UAS_SMART_OUT_HEADER        16UL    /* SENDCMDOUTPARAMS less bBuffer */
#define UAS_SMART_VERSION_LENGTH    24UL    /* GETVERSIONINPARAMS            */

/* DRIVERSTATUS.bDriverError (ntdddisk.h). */
#define UAS_SMART_NO_ERROR          0UL
#define UAS_SMART_IDE_ERROR         1UL
#define UAS_SMART_INVALID_COMMAND   3UL
#define UAS_SMART_INVALID_BUFFER    4UL
#define UAS_SMART_NOT_SUPPORTED     9UL

/*
 * Checks the envelope in buf (inLength in, outLength out) and returns
 * UAS_PT_OK with *control, *timeout (seconds as sent) and *capacity (the
 * payload room the sender declared), or UAS_PT_INVALID for anything not a
 * SCSIDISK request (the dispatch then answers it as an unknown IOCTL) or
 * UAS_PT_TOO_SMALL.
 */
ULONG UasSiocCheck(const UCHAR *buf, ULONG inLength, ULONG outLength,
                   PULONG control, PULONG timeout, PULONG capacity);

typedef struct _UAS_SMART_PLAN {
    ULONG Control;
    ULONG DataLength;               /* data in, bytes; 0: none            */
    ULONG OutLength;                /* SENDCMDOUTPARAMS bytes returned on
                                     * success: 16 + data or 16 + 8       */
    ULONG ReturnRegs;               /* RETURN STATUS: IDEREGS back        */
    UCHAR Cur[8];                   /* the task file sent                 */
    UCHAR Cdb[16];
} UAS_SMART_PLAN, *PUAS_SMART_PLAN;

/*
 * Plans one SMART control code other than SMART_VERSION from the payload
 * (payloadIn bytes of SENDCMDINPARAMS) for a reply of at most replyRoom
 * bytes. Returns UAS_SMART_NO_ERROR, or the DRIVERSTATUS error to answer
 * with.
 */
ULONG UasSmartPlan(ULONG control, const UCHAR *payload, ULONG payloadIn,
                   ULONG replyRoom, PUAS_SMART_PLAN plan);

/* GETVERSIONINPARAMS, 24 bytes: version 1.1, one device, ATA IDENTIFY and
 * SMART; reserved bytes 0. */
VOID UasSmartVersion(PUCHAR out);

/*
 * Writes a SMART request's SENDCMDOUTPARAMS into out (room bytes) from its
 * outcome and returns UAS_PTR_*; *outBytes the SENDCMDOUTPARAMS bytes
 * written, *driverError the bDriverError given. data is the data-in the
 * drive sent (moved bytes).
 */
ULONG UasSmartResult(const UAS_SMART_PLAN *plan, PUCHAR out, ULONG room,
                     ULONG srbStatus, ULONG scsiStatus, ULONG moved,
                     const UCHAR *data, const UCHAR *sense,
                     ULONG senseLength, PULONG outBytes,
                     PULONG driverError);

/*
 * Issue 26 (2026-11-07): ATA PASS-THROUGH answered by this driver for a
 * device whose XhciUasNoAtaPassThrough is set, never sent to it, as
 * Linux's uas.c does under US_FL_NO_ATA_1X. UasNoAtaCdb is 1 for a CDB
 * this answers: ATA PASS-THROUGH (16), 85h, and (12), A1h, by the
 * operation code alone, as Linux tests cmnd[0]. UasInvalidCdbSense writes
 * the answer's sense into sense (room bytes): fixed format, ILLEGAL
 * REQUEST, INVALID FIELD IN CDB (5/24/00), Linux's
 * usb_stor_sense_invalidCDB; returns the bytes written, at most
 * UAS_INVALID_CDB_SENSE.
 */
#define UAS_INVALID_CDB_SENSE       18UL

ULONG UasNoAtaCdb(const UCHAR *cdb, ULONG cdbLength);
ULONG UasInvalidCdbSense(PUCHAR sense, ULONG room);

/*
 * Task 49.39 (design record 13 section 16.16): a drive's own refusal of ATA
 * PASS-THROUGH replayed. UasAtaBit: the LUN's bit for a CDB's operation
 * code - UAS_ATA_REFUSED_12 for A1h, UAS_ATA_REFUSED_16 for 85h, 0 for any
 * other. UasAtaRefusal: that bit when the drive answered such a command
 * CHECK CONDITION with sense key 5, ASC 20h, ASCQ 00h (INVALID COMMAND
 * OPERATION CODE) in fixed (70h/71h, at least 14 bytes) or descriptor
 * (72h/73h, at least 4) sense; 0 otherwise. UasInvalidOpcodeSense writes
 * that answer's fixed sense into sense (room bytes), returning the bytes
 * written, at most UAS_INVALID_CDB_SENSE.
 */
#define UAS_ATA_REFUSED_12          0x01UL
#define UAS_ATA_REFUSED_16          0x02UL

ULONG UasAtaBit(const UCHAR *cdb, ULONG cdbLength);
ULONG UasAtaRefusal(const UCHAR *cdb, ULONG cdbLength, ULONG scsiStatus,
                    const UCHAR *sense, ULONG senseLength);
ULONG UasInvalidOpcodeSense(PUCHAR sense, ULONG room);

/*
 * Issue 26: one failure's four words for the record the bus keeps
 * (xhci98_xfer.h, XHCI98_FAILURES): w[0..3] as that header lays them out.
 * The sense key, ASC and ASCQ are read from sense (senseLength bytes,
 * fixed 70h/71h or descriptor 72h/73h format); no sense, or one too short
 * to hold them, gives key FFh and ASC and ASCQ 0. The timeout is held at
 * 255; each other field is cut to its width.
 */
/*
 * Issue 26, XhciUasFastReset (owner, 2026-11-07; default 0, and at 0
 * nothing below is asked). UasFastResetDue: 1 when a command's timeout goes
 * straight to the device reset instead of another ABORT TASK - an ABORT
 * TASK the device answered (armed) at second abortAt, no command ended GOOD
 * since (progress 0), now within UAS_FAST_WINDOW seconds of it, and the
 * command started at or after it (startedAt). UasFinalCancelDue: 1 when a
 * command whose final IU is in has waited UAS_FAST_CANCEL_WAIT seconds for
 * its data transfer's cancel, which then asks for transport recovery, not
 * an ABORT TASK. Seconds are the class driver's own one-second count;
 * each comparison is modular, so the count may wrap.
 */
#define UAS_FAST_WINDOW             120UL
#define UAS_FAST_CANCEL_WAIT        5UL

ULONG UasFastResetDue(ULONG fast, ULONG armed, ULONG progress, ULONG now,
                      ULONG abortAt, ULONG startedAt);
ULONG UasFinalCancelDue(ULONG fast, ULONG final, ULONG dataOutstanding,
                        ULONG waited);

/*
 * Issue 26 section 8 (task 49.39; design record 13 section 16.15): what a
 * command whose own timeout has run out does next. UAS_FINAL_TIMEOUT_NONE:
 * the ordinary timeout (ABORT TASK) - no final IU yet, or nothing of its
 * data still outstanding. UAS_FINAL_TIMEOUT_WAIT: its final IU came less
 * than UAS_FINAL_TIMEOUT_AGE seconds ago (finalAge, the class driver's
 * seconds since the final IU): held at the deadline and asked again at the
 * next tick, since status and data complete on separate pipes in either
 * order. UAS_FINAL_TIMEOUT_RESET: the final IU is in and old enough and the
 * data transfer is still outstanding (busy, under the driver's cancel, or
 * holding a cancel reference): the device reset, not ABORT TASK - the
 * device holds no task for a command it has answered.
 */
#define UAS_FINAL_TIMEOUT_AGE       2UL
#define UAS_FINAL_TIMEOUT_NONE      0UL
#define UAS_FINAL_TIMEOUT_WAIT      1UL
#define UAS_FINAL_TIMEOUT_RESET     2UL

ULONG UasFinalTimeoutDue(ULONG final, ULONG dataOutstanding,
                         ULONG finalAge);

/*
 * Task 49.39 (design record 13 section 16.17): a command due for
 * UAS_FINAL_TIMEOUT_RESET with resets (the device resets already taken for
 * it since data was last delivered) at UAS_FINAL_RESET_LIMIT or more gives
 * the drive up instead (1). UasDataDelivered: what clears that count - a
 * data transfer that completed successfully with at least one byte; a
 * command with no data phase, GOOD or not, never does.
 */
#define UAS_FINAL_RESET_LIMIT       3UL
#define UAS_FINAL_DIR_IN            1UL
#define UAS_FINAL_DIR_OUT           2UL

ULONG UasFinalGiveUpDue(ULONG resets);
ULONG UasDataDelivered(ULONG ok, ULONG bytes);

/*
 * The count is kept per direction (`counts`, UAS_FINAL_DIRS words: [0] for
 * data-in, [1] for data-out). UasFinalCount: one reset taken for commands of
 * the directions in `dirs` (a mask of UAS_FINAL_DIR_*) adds one to each of
 * them. UasFinalClear: data delivered in `dir` clears that direction alone -
 * writes that land never clear a count of reads that never came back, nor
 * the other way round. UasFinalDue: 1 when a reset asked for by commands of
 * `dirs` finds any of those directions at UAS_FINAL_RESET_LIMIT: the drive
 * is given up instead.
 */
#define UAS_FINAL_DIRS              2UL

VOID UasFinalCount(PULONG counts, ULONG dirs);
VOID UasFinalClear(PULONG counts, ULONG dir);
ULONG UasFinalDue(const ULONG *counts, ULONG dirs);

VOID UasFailPack(PULONG w, ULONG event, ULONG origin, ULONG tag,
                 ULONG opcode, ULONG srbStatus, ULONG scsiStatus,
                 const UCHAR *sense, ULONG senseLength, ULONG slotState,
                 ULONG xfers, ULONG timeout, ULONG length);

#endif /* UAS_IU_H */
