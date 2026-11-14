/*
 * xhci_bot.h - the Bulk-Only transport's state as the bus sees it, the
 * pure half (roadmap-usb4 task 49.34; design record 20 section 4.3.4, M3
 * and M5).
 *
 * The bus carries every transfer of a Bulk-Only interface (Bulk-Only
 * Transport 1.0) and so can read the transport's own framing: each CBW the
 * class driver submits, the bytes of each data phase and each CSW. This
 * machine follows that framing per interface and answers one question for
 * the resume keep: is the drive between commands, with nothing begun and
 * not ended and no sense owed? It fails closed: every event it does not
 * list leads to UNKNOWN, and UNKNOWN is left only through an event that
 * defines the device as waiting for a CBW (a first configuration, the
 * Bulk-Only reset recovery in its order, the bus's own port reset).
 *
 * Beside that, it records what the medium check (M5) compares: the last
 * READ CAPACITY answer the class driver received, read only within its
 * usable length (Bulk-Only Transport 1.0 section 5.3.3), or that the LUN
 * answered "medium not present"; and it parses CBWs, CSWs, sense data and
 * capacity answers for the bus's own commands of that check.
 *
 * DDK-free: part of the pure core. C89. IRQL: any; the caller serializes
 * the calls for one machine (the bus holds its controller lock).
 */

#ifndef XHCI_BOT_H
#define XHCI_BOT_H

#include "xhci_compat.h"

/* The machine's states. Keepable: SYNC and IDLE (XhciBotKeepable). */
#define XHCI_BOT_UNKNOWN         0UL
#define XHCI_BOT_SYNC            1UL  /* defined to be waiting for a CBW */
#define XHCI_BOT_IDLE            2UL  /* a CSW validated with status 0 */
#define XHCI_BOT_SENSE           3UL  /* a CSW with status 1: sense owed */
#define XHCI_BOT_CBW_SENT        4UL  /* a CBW submitted, not completed */
#define XHCI_BOT_DATA            5UL
#define XHCI_BOT_DATA_STALL      6UL  /* a data transfer STALLed, clear owed */
#define XHCI_BOT_STATUS          7UL  /* the CSW is the next IN completion */
#define XHCI_BOT_STATUS_STALL    8UL  /* the CSW read STALLed, clear owed */
#define XHCI_BOT_RECOVERY        9UL  /* a Bulk-Only reset done, clears owed */

/* How a transfer ended, as the caller maps its completion. */
#define XHCI_BOT_XFER_OK         0UL
#define XHCI_BOT_XFER_STALL      1UL
#define XHCI_BOT_XFER_CANCEL     2UL  /* cancelled or aborted */
#define XHCI_BOT_XFER_ERROR      3UL  /* any other failure */

/* What the CBW, CSW and data are. */
#define XHCI_BOT_CBW_BYTES       31UL
#define XHCI_BOT_CSW_BYTES       13UL
#define XHCI_BOT_CBW_SIG         0x43425355UL
#define XHCI_BOT_CSW_SIG         0x53425355UL
#define XHCI_BOT_STASH_BYTES     32UL
#define XHCI_BOT_LUN_UNKNOWN     0xFFUL

/* The SCSI operation codes the machine and the medium check know. */
#define XHCI_BOT_OP_TEST_UNIT_READY  0x00UL
#define XHCI_BOT_OP_REQUEST_SENSE    0x03UL
#define XHCI_BOT_OP_READ_CAPACITY10  0x25UL
#define XHCI_BOT_OP_SERVICE_IN16     0x9EUL
#define XHCI_BOT_SA_READ_CAPACITY16  0x10UL

/* The Bulk-Only class requests (Bulk-Only Transport 1.0 sections 3.1, 3.2). */
#define XHCI_BOT_REQ_RESET       0xFFUL
#define XHCI_BOT_REQ_MAX_LUN     0xFEUL

/* Flags. */
#define XHCI_BOT_F_CARRIED       0x0001UL /* a CBW carried since the SYNC */
#define XHCI_BOT_F_SENSE_CMD     0x0002UL /* the command is a SENSE's REQUEST
                                           * SENSE */
#define XHCI_BOT_F_CSW_RETRIED   0x0004UL /* the CSW read was cleared once */
#define XHCI_BOT_F_RECOV_IN      0x0008UL /* recovery: Bulk-In cleared */
#define XHCI_BOT_F_CAP_FAILED    0x0010UL /* the command was a READ CAPACITY
                                           * that ended with status 1 */
#define XHCI_BOT_F_STASH_BAD     0x0020UL /* part of the answer unreadable */

/* The capacity record (M5). */
#define XHCI_BOT_CAP_NONE        0UL  /* nothing recorded */
#define XHCI_BOT_CAP_SIZE        1UL  /* last LBA and block length */
#define XHCI_BOT_CAP_NO_MEDIUM   2UL  /* NOT READY, medium not present */

typedef struct _XHCI_BOT_CAP {
    ULONG Kind;                 /* XHCI_BOT_CAP_* */
    ULONG LastLbaLo;
    ULONG LastLbaHi;
    ULONG BlockLength;
} XHCI_BOT_CAP;

typedef struct _XHCI_BOT {
    ULONG State;                /* XHCI_BOT_* */
    ULONG Flags;                /* XHCI_BOT_F_* */
    ULONG Tag;                  /* the open command's dCBWTag */
    ULONG Length;               /* its dCBWDataTransferLength */
    ULONG Moved;                /* data bytes moved in its data phase */
    ULONG DirIn;                /* its direction: 1 IN */
    ULONG Opcode;               /* its CDB's first byte */
    ULONG Service;              /* its CDB's second byte, low five bits */
    ULONG MaxLun;               /* Get Max LUN's answer, or LUN_UNKNOWN */
    ULONG MaxLunSeen;           /* a Get Max LUN completed since the first
                                 * configuration */
    ULONG StashLength;          /* bytes of the data phase stashed */
    UCHAR Stash[XHCI_BOT_STASH_BYTES];
    XHCI_BOT_CAP Cap;           /* the class driver's last capacity answer */
    ULONG Events;               /* events seen (diagnostic) */
    ULONG LastUnknown;          /* the reason code of the last UNKNOWN */
} XHCI_BOT, *PXHCI_BOT;

/* The reason codes LastUnknown records (diagnostic only; the bus logs it). */
#define XHCI_BOT_WHY_NONE        0UL
#define XHCI_BOT_WHY_INIT        1UL
#define XHCI_BOT_WHY_NOT_CBW     2UL  /* a submission that is not a CBW */
#define XHCI_BOT_WHY_BAD_CBW     3UL  /* malformed, unreadable or LUN high */
#define XHCI_BOT_WHY_EARLY       4UL  /* a transfer posted before the CBW
                                       * completed */
#define XHCI_BOT_WHY_CBW_END     5UL  /* the CBW's completion failed */
#define XHCI_BOT_WHY_DIRECTION   6UL
#define XHCI_BOT_WHY_OVERRUN     7UL  /* more data than the CBW asked */
#define XHCI_BOT_WHY_DATA_END    8UL  /* a data transfer failed */
#define XHCI_BOT_WHY_BAD_CSW     9UL
#define XHCI_BOT_WHY_PHASE      10UL  /* bCSWStatus 2 */
#define XHCI_BOT_WHY_CSW_END    11UL  /* the CSW read failed */
#define XHCI_BOT_WHY_SENSE      12UL  /* a command other than REQUEST SENSE
                                       * after a CHECK CONDITION, or the
                                       * REQUEST SENSE failed */
#define XHCI_BOT_WHY_CLEAR      13UL  /* a halt cleared where none was owed */
#define XHCI_BOT_WHY_RECOVERY   14UL  /* a reset recovery out of order */
#define XHCI_BOT_WHY_SELECT     15UL
#define XHCI_BOT_WHY_MAX_LUN    16UL  /* Get Max LUN failed or changed */
#define XHCI_BOT_WHY_OTHER      17UL  /* abort, reset pipe, cancel, a port
                                       * request before its replay */

/* A fresh machine: UNKNOWN, no LUN, nothing recorded. */
VOID XhciBotInit(PXHCI_BOT bot);

/* A SELECT_CONFIGURATION or SELECT_INTERFACE that selected the Bulk-Only
 * setting completed with success; `first` when it is the first
 * SELECT_CONFIGURATION after the bus enumerated the device. A failed select
 * is XhciBotOther. */
VOID XhciBotSelected(PXHCI_BOT bot, ULONG first, ULONG isInterface);

/* The bus's own port reset and replay finished (RESET_PORT, or the keep's
 * restore at its reopening): the device waits for a CBW. */
VOID XhciBotPortReset(PXHCI_BOT bot);

/* A bulk transfer submitted: `in` its direction, `length` its length,
 * `bytes` its buffer when the bus can read it without a mapping call, else
 * NULL. */
VOID XhciBotSubmit(PXHCI_BOT bot, ULONG in, ULONG length,
                   const UCHAR *bytes);

/* A bulk transfer completed: `how` an XHCI_BOT_XFER_*, `actual` the bytes
 * moved, `requested` its length, `bytes` its buffer (as for the submission;
 * read only for an IN that succeeded). */
VOID XhciBotComplete(PXHCI_BOT bot, ULONG in, ULONG how, ULONG actual,
                     ULONG requested, const UCHAR *bytes);

/* A CLEAR_FEATURE(ENDPOINT_HALT) or RESET_PIPE on one of the interface's
 * bulk endpoints completed: `in` the endpoint's direction, `ok` its
 * success. */
VOID XhciBotClearHalt(PXHCI_BOT bot, ULONG in, ULONG ok);

/* A class request to the interface completed: `request` its bRequest, `ok`
 * its success, `stalled` a STALL, `answer` the first byte returned (Get Max
 * LUN), `answered` how many bytes were. */
VOID XhciBotClassRequest(PXHCI_BOT bot, ULONG request, ULONG ok,
                         ULONG stalled, ULONG answer, ULONG answered);

/* Anything else that names the interface or its pipes: an ABORT_PIPE, a
 * RESET_PIPE the machine owes nothing for, a cancel, a RESET_PORT or
 * CYCLE_PORT before its replay, a failed select. */
VOID XhciBotOther(PXHCI_BOT bot);

/* 1 when the drive is between commands with no sense owed (SYNC or IDLE)
 * and its max LUN is 0. Pure. */
ULONG XhciBotKeepable(const XHCI_BOT *bot);

/* Parsers, shared by the machine and the medium check. Each returns 1 when
 * the bytes are valid as the function states, else 0. */

/* A CBW of exactly 31 readable bytes: signature, bCBWCBLength 1 to 16,
 * bCBWLUN no higher than maxLun (any when maxLun is LUN_UNKNOWN). */
ULONG XhciBotParseCbw(const UCHAR *bytes, ULONG length, ULONG maxLun,
                      PULONG tag, PULONG dataLength, PULONG dirIn,
                      PULONG opcode, PULONG service);

/* A CSW of exactly 13 readable bytes for `tag`: signature, tag, status 0
 * to 2, residue no larger than `dataLength`. */
ULONG XhciBotParseCsw(const UCHAR *bytes, ULONG length, ULONG tag,
                      ULONG dataLength, PULONG status, PULONG residue);

/* The usable length of a response (section 5.3.3): the smaller of the bytes
 * moved and the CBW's length less the CSW's residue. */
ULONG XhciBotUsable(ULONG moved, ULONG dataLength, ULONG residue);

/* Sense data within its usable length: fixed format (70h, 71h) with at
 * least 14 bytes, or descriptor format (72h, 73h) with at least 4. */
ULONG XhciBotParseSense(const UCHAR *bytes, ULONG usable, PULONG key,
                        PULONG asc, PULONG ascq);

/* A READ CAPACITY answer within its usable length: (10) at least 8 bytes,
 * (16) at least 12; `sixteen` names which. */
ULONG XhciBotParseCapacity(const UCHAR *bytes, ULONG usable, ULONG sixteen,
                           XHCI_BOT_CAP *cap);

/* Builds a CBW for the bus's own command (M5): `cdb` of `cdbLength` bytes
 * (1 to 16), `dataLength` bytes in direction `dirIn`, LUN 0, `tag`. Writes
 * 31 bytes. */
VOID XhciBotBuildCbw(UCHAR *cbw, ULONG tag, ULONG dataLength, ULONG dirIn,
                     const UCHAR *cdb, ULONG cdbLength);

/* The medium check's comparison (M5): 1 when `now` matches the class
 * driver's own `recorded` answer (the same size, or no medium both
 * times); 0 when either is NONE or they differ. */
ULONG XhciBotSameMedium(const XHCI_BOT_CAP *recorded,
                        const XHCI_BOT_CAP *now);

/* What the medium check does with sense data it read after a TEST UNIT
 * READY's CHECK CONDITION. */
#define XHCI_BOT_TUR_FAIL        0UL  /* drop the drive */
#define XHCI_BOT_TUR_RETRY       1UL  /* the reset's attention: again */
#define XHCI_BOT_TUR_BECOMING    2UL  /* becoming ready: again after 500 ms */
#define XHCI_BOT_TUR_NO_MEDIUM   3UL  /* medium not present */
ULONG XhciBotTurSense(ULONG key, ULONG asc, ULONG ascq);

/* XhciResumeKeepBot's bits (M9), and its value when none is set (M18):
 * after a hibernation on Windows 2000 (`isW2k`: WDM 1.10 and not 1.20),
 * nothing on any other Windows. The INFs write no value, so this is the
 * default an install runs with. */
#define XHCI_BOT_KEEP_HIBERNATE  1UL
#define XHCI_BOT_KEEP_STANDBY    2UL
#define XHCI_BOT_KEEP_MASK       3UL
ULONG XhciBotKeepDefault(ULONG isW2k);

#endif /* XHCI_BOT_H */
