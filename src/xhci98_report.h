/*
 * xhci98_report.h - the private client-report interface of xhci98.sys, the
 * bus driver (roadmap-hcd.md task 36.2, issue 12), for a function driver of
 * this project's own: xhciuas.sys, which includes this header beside
 * xhci98_streams.h and nothing else of the bus.
 *
 * Self-contained: it needs only the DDK's base types and CTL_CODE, so a
 * driver includes it after <wdm.h> or <ntddk.h>. C89. No Microsoft driver
 * sends or answers this request; a bus that is not xhci98.sys completes it
 * with an error, and the caller carries on - a report is a diagnostic, never
 * a condition of anything.
 *
 *   IOCTL_XHCI98_CLIENT_REPORT  IRP_MJ_INTERNAL_DEVICE_CONTROL to the
 *                               device's PDO, Parameters.Others.Argument1 =
 *                               a PXHCI98_CLIENT_REPORT. <= DISPATCH_LEVEL to
 *                               send; the bus answers it before returning
 *                               and never pends it.
 *
 * WHY. A dump taken with XHCISNAP reads the bus, not the class drivers
 * above it: issue 12's reader bound xhciuas and showed no disk, and nothing
 * in the dump said whether REPORT LUNS or INQUIRY had failed, how, or which
 * pipe stalled. The bus keeps the last few reports, one per device, in the
 * snapshot's HCD region (src/xhci.h, XHCI_SNAPSHOT_HCD_CLIENT_*), stamped
 * with the device's slot, root port and route, and XHCISNAP decodes them by
 * Kind. A newer report from the same device replaces the older.
 *
 * Words are the client's own, laid out per Kind; the bus copies them and
 * reads none. A report holds no pointer.
 */

#ifndef XHCI98_REPORT_H
#define XHCI98_REPORT_H

/* FILE_DEVICE_UNKNOWN, as the streams requests (xhci98_streams.h, 0xF98
 * and 0xF99); 0xF9A is the next private function code. */
#define XHCI98_REPORT_DEVICE_TYPE   0x00000022UL
#define IOCTL_XHCI98_CLIENT_REPORT                                            \
    CTL_CODE(XHCI98_REPORT_DEVICE_TYPE, 0xF9A, METHOD_NEITHER,                \
             FILE_ANY_ACCESS)

#define XHCI98_REPORT_SIGNATURE     0x52383958UL    /* 'X98R' */
#define XHCI98_REPORT_VERSION       1UL
#define XHCI98_REPORT_WORDS         28UL

/* Kinds. */
#define XHCI98_REPORT_KIND_UAS      0x31534155UL    /* 'UAS1' */

typedef struct _XHCI98_CLIENT_REPORT {
    ULONG Signature;                /* in: XHCI98_REPORT_SIGNATURE       */
    ULONG Version;                  /* in: XHCI98_REPORT_VERSION         */
    ULONG Size;                     /* in: sizeof(XHCI98_CLIENT_REPORT)  */
    ULONG Kind;                     /* in: XHCI98_REPORT_KIND_*          */
    ULONG Words[XHCI98_REPORT_WORDS];
} XHCI98_CLIENT_REPORT, *PXHCI98_CLIENT_REPORT;

/*
 * XHCI98_REPORT_KIND_UAS: xhciuas.sys (src/uas/uas_fdo.c, uasReport). Sent
 * at the end of every START, after every port reset it asks for, and at
 * STOP. A "result" word packs an SRB's outcome: bits 0-7 the SRB status
 * (SRB_STATUS_*, flags included), 8-15 the SCSI status, 16-23 the tries
 * spent, 24-31 the LUN. A "sense" word: bits 0-7 the sense key, 8-15 the
 * ASC, 16-23 the ASCQ, 24-31 the response code (0: no sense came back).
 */
#define XHCI98_UAS_STAGE            0   /* XHCI98_UAS_STAGE_*: reached    */
#define XHCI98_UAS_STATUS           1   /* START's NTSTATUS              */
#define XHCI98_UAS_INFO             2   /* XHCI98_UAS_INFO_* bits, below */
#define XHCI98_UAS_PIPES            3   /* endpoint address by pipe id:
                                         * command, status, data-in,
                                         * data-out in bytes 0 to 3      */
#define XHCI98_UAS_STREAMS          4   /* wanted | granted << 16        */
#define XHCI98_UAS_RLUNS            5   /* REPORT LUNS: result           */
#define XHCI98_UAS_RLUNS_SENSE      6   /* REPORT LUNS: sense            */
#define XHCI98_UAS_LUNS             7   /* reported | inquired << 8 |
                                         * found << 16 | INQUIRYs failed
                                         * outright << 24                */
#define XHCI98_UAS_INQ              8   /* the last INQUIRY: result      */
#define XHCI98_UAS_INQ_SENSE        9   /* the last INQUIRY: sense       */
#define XHCI98_UAS_INQ_BYTE0        10  /* that INQUIRY's byte 0 | the
                                         * bytes it returned << 8        */
#define XHCI98_UAS_FAIL_FIRST       11  /* the first transfer failure:
                                         * USBD status                   */
#define XHCI98_UAS_FAIL_FIRST_WHO   12  /* its kind (UAS_XFER_*, 0 to 2)
                                         * | its pipe id << 8 | its
                                         * stream or tag << 16           */
#define XHCI98_UAS_FAIL_LAST        13  /* the latest: USBD status       */
#define XHCI98_UAS_FAIL_LAST_WHO    14  /* the latest: as _FIRST_WHO     */
#define XHCI98_UAS_FAIL_COMMAND     15  /* failures on the command pipe  */
#define XHCI98_UAS_FAIL_STATUS      16  /* ... on the status pipe        */
#define XHCI98_UAS_FAIL_DATA        17  /* ... on a data pipe            */
#define XHCI98_UAS_COUNTERS         18  /* 8 words: UAS_FDO.Counters, in
                                         * UAS_CTR_* order (src/uas/uas.h) */
#define XHCI98_UAS_REASON           26  /* XHCI98_UAS_REASON_*           */
#define XHCI98_UAS_SEQUENCE         27  /* this FDO's reports, counted   */

#define XHCI98_UAS_STAGE_NONE       0
#define XHCI98_UAS_STAGE_DESCRIPTOR 1   /* the device descriptor read    */
#define XHCI98_UAS_STAGE_CONFIG     2   /* the configuration parsed      */
#define XHCI98_UAS_STAGE_SELECT     3   /* the UAS alternate selected    */
#define XHCI98_UAS_STAGE_STREAMS    4   /* streams open (or streamless)  */
#define XHCI98_UAS_STAGE_ENGINE     5   /* the engine started            */
#define XHCI98_UAS_STAGE_LUNS       6   /* REPORT LUNS asked             */
#define XHCI98_UAS_STAGE_DONE       7   /* every LUN inquired            */

#define XHCI98_UAS_INFO_SUPERSPEED  0x00000001UL
#define XHCI98_UAS_INFO_STREAMED    0x00000002UL
#define XHCI98_UAS_INFO_DEAD        0x00000004UL
/* bits 8-15 the interface number, 16-23 its alternate, 24-31 its
 * endpoints. */

#define XHCI98_UAS_REASON_START     1
#define XHCI98_UAS_REASON_RESET     2
#define XHCI98_UAS_REASON_STOP      3

#endif /* XHCI98_REPORT_H */
