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

#endif /* UAS_IU_H */
