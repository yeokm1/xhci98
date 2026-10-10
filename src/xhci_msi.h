/*
 * xhci_msi.h - message-signalled interrupts on Vista and 7, the pure half
 * (design record 15 section 4.3; design record 18; roadmap-hcd.md tasks
 * 36.15 and 36.16).
 *
 * What needs no kernel:
 *
 *   - which capability Windows enabled, from the controller's
 *     configuration bytes (record 15 section 4.3, "MSI or MSI-X");
 *   - the IO_RESOURCE_REQUIREMENTS_LIST's checked walk, its classification
 *     and the strip that keeps only the configurations offering a line
 *     (record 18 section 4.2), over the list's bytes;
 *   - the qemu flavour's no-line rewrite (record 18 section 10, H1a);
 *   - the helper binding's per-FDO state machine (record 18 sections 4.1
 *     and 10, L2) and the filter record's class and sequence (section 4.2,
 *     step 0);
 *   - the checksummed trace record (record 18 section 9).
 *
 * The executor is hcd_msi86.c. The byte layouts are the DDK's, the same on
 * x86 and amd64, and hcd_msi86.c asserts them against the DDK's own types.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_MSI_H
#define XHCI_MSI_H

#include "xhci_compat.h"

/* ---- the controller's capability (record 15 section 4.3) -------------- */

/* Given the first 256 bytes of configuration space, which message
 * capability is enabled: XHCI_POLL_DELIVERY_MSIX, _MSI, or _MESSAGE when
 * neither is read as enabled. The walk is bounded at 48 entries, each
 * pointer in 40h-FCh and DWORD-aligned. */
ULONG XhciMsiCapRead(const UCHAR *config);

/* ---- the requirements list (record 18 section 4.2) -------------------- */

#define XHCI_MSI_RRL_HEADER         0x20UL  /* to List[0]                 */
#define XHCI_MSI_RRL_ALTERNATIVES   0x1CUL
#define XHCI_MSI_RL_HEADER          0x08UL  /* to Descriptors[0]          */
#define XHCI_MSI_RL_COUNT           0x04UL
#define XHCI_MSI_RD_SIZE            0x20UL
#define XHCI_MSI_RD_OPTION          0x00UL
#define XHCI_MSI_RD_TYPE            0x01UL
#define XHCI_MSI_RD_SHARE           0x02UL
#define XHCI_MSI_RD_FLAGS           0x04UL
#define XHCI_MSI_RD_U               0x08UL
#define XHCI_MSI_RD_U_BYTES         0x18UL

#define XHCI_MSI_TYPE_INTERRUPT     2UL     /* CmResourceTypeInterrupt    */
#define XHCI_MSI_TYPE_DEVPRIVATE    0x81UL  /* CmResourceTypeDevicePrivate */
#define XHCI_MSI_OPTION_ALTERNATIVE 0x08UL  /* IO_RESOURCE_ALTERNATIVE    */
#define XHCI_MSI_FLAG_MESSAGE       0x0002UL /* CM_RESOURCE_INTERRUPT_MESSAGE */

/* The walk's verdict and where it stopped (step 4). */
#define XHCI_MSI_WALK_OK            0UL
#define XHCI_MSI_WALK_SHORT_HEADER  1UL     /* ListSize below the header  */
#define XHCI_MSI_WALK_ALTERNATIVES  2UL     /* more lists than bytes      */
#define XHCI_MSI_WALK_LIST_HEADER   3UL     /* a list's header cut short  */
#define XHCI_MSI_WALK_COUNT         4UL     /* a Count past the bytes     */

typedef struct _XHCI_MSI_WALK {
    ULONG Verdict;          /* XHCI_MSI_WALK_*                            */
    ULONG ListSize;
    ULONG Lists;            /* AlternativeLists, as given                 */
    ULONG Descriptors;      /* in all the lists walked                    */
    ULONG Used;             /* bytes the lists occupy, header included    */
    ULONG FailList;         /* where it stopped, on a failure             */
} XHCI_MSI_WALK, *PXHCI_MSI_WALK;

/*
 * The checked walk: reads ListSize, then each list's header and Count and
 * spans each list's descriptors, against the bytes remaining, decrementing
 * a remaining-length counter and never forming an end from a count. Nothing
 * past a failure is read. `list` must address at least 4 readable bytes.
 */
VOID XhciMsiWalk(const UCHAR *list, PXHCI_MSI_WALK walk);

/* The list's class (section 4.2); the start reads it (section 4.3). */
#define XHCI_MSI_CLASS_UNKNOWN      0UL     /* not classified (step 0)    */
#define XHCI_MSI_CLASS_NO_INTERRUPT 1UL     /* no interrupt descriptor    */
#define XHCI_MSI_CLASS_NO_MESSAGE   2UL     /* interrupts, none a message */
#define XHCI_MSI_CLASS_REPLACEABLE  3UL     /* messages and a STRIPPABLE  */
#define XHCI_MSI_CLASS_NO_LINE      4UL     /* messages, none STRIPPABLE  */
#define XHCI_MSI_CLASS_UNSUPPORTED  5UL     /* a shape not handled        */
#define XHCI_MSI_CLASS_INCONSISTENT 6UL     /* the walk refused it        */

typedef struct _XHCI_MSI_CLASSIFY {
    ULONG Class;            /* XHCI_MSI_CLASS_*                           */
    ULONG Strippable;       /* configurations of each kind                */
    ULONG MessageBound;
    ULONG NoInterrupt;
    ULONG MessageMembers;   /* in all configurations                      */
    ULONG LineMembers;
    ULONG DevicePrivate;
    ULONG Unsupported;      /* runs with a head of another type, or a
                             * list opening on an alternative             */
} XHCI_MSI_CLASSIFY, *PXHCI_MSI_CLASSIFY;

/* Classifies a list the walk accepted (`walk->Verdict` OK). */
VOID XhciMsiClassify(const UCHAR *list, const XHCI_MSI_WALK *walk,
                     PXHCI_MSI_CLASSIFY classify);

/*
 * The strip of a REPLACEABLE list: the STRIPPABLE configurations alone, in
 * order, each without its message members, a removed head's run headed by
 * its next kept member. With `out` NULL returns the size the result needs;
 * otherwise writes it (at most `capacity` bytes) and returns its size, or 0
 * when it does not fit. Returns 0 for a list of any other class.
 */
ULONG XhciMsiStrip(const UCHAR *list, const XHCI_MSI_WALK *walk,
                   const XHCI_MSI_CLASSIFY *classify, PUCHAR out,
                   ULONG capacity);

/*
 * The qemu flavour's no-line rewrite (record 18 section 10, H1a): every
 * line member of an interrupt run that also has a message member removed,
 * and every configuration whose interrupt runs are all line-only dropped;
 * never a run or the list left empty. Sizing and writing as XhciMsiStrip;
 * 0 when there is nothing to rewrite.
 */
ULONG XhciMsiInjNoLine(const UCHAR *list, const XHCI_MSI_WALK *walk,
                       PUCHAR out, ULONG capacity);

/* The filter's outcome (section 4.2). */
#define XHCI_MSI_OUT_NONE           0UL     /* no filter yet              */
#define XHCI_MSI_OUT_LOWER_FAILED   1UL
#define XHCI_MSI_OUT_NO_LIST        2UL
#define XHCI_MSI_OUT_INCONSISTENT   3UL
#define XHCI_MSI_OUT_NOT_ASKED      4UL
#define XHCI_MSI_OUT_DISABLED       5UL
#define XHCI_MSI_OUT_NO_LINE        6UL
#define XHCI_MSI_OUT_NO_INTERRUPT   7UL
#define XHCI_MSI_OUT_NO_MESSAGE     8UL
#define XHCI_MSI_OUT_UNSUPPORTED    9UL
#define XHCI_MSI_OUT_REPLACED       10UL
#define XHCI_MSI_OUT_NO_MEMORY      11UL

/* The outcome for a classified list when the strip is asked for and
 * enabled: REPLACED for a REPLACEABLE list, else the kept outcome. */
ULONG XhciMsiOutcome(ULONG listClass);

/* The filter record (section 4.2, step 0): the class is UNKNOWN from
 * AddDevice and from every filter's entry until that filter publishes. */
typedef struct _XHCI_MSI_FILTER_RECORD {
    ULONG Sequence;
    ULONG Class;
    ULONG Outcome;
} XHCI_MSI_FILTER_RECORD, *PXHCI_MSI_FILTER_RECORD;

VOID XhciMsiFilterInit(PXHCI_MSI_FILTER_RECORD record);
VOID XhciMsiFilterBegin(PXHCI_MSI_FILTER_RECORD record);
VOID XhciMsiFilterPublish(PXHCI_MSI_FILTER_RECORD record, ULONG listClass,
                          ULONG outcome);

/* ---- the helper binding (record 18 section 4.1) ----------------------- */

#define XHCI_MSI86_STATE_UNSEEN     0UL
#define XHCI_MSI86_STATE_ABSENT     1UL
#define XHCI_MSI86_STATE_REFUSED    2UL
#define XHCI_MSI86_STATE_BOUND      3UL
#define XHCI_MSI86_STATE_REMOVED    4UL

#define XHCI_MSI86_EV_FILTER        1UL
#define XHCI_MSI86_EV_START         2UL
#define XHCI_MSI86_EV_STOP          3UL
#define XHCI_MSI86_EV_SURPRISE      4UL
#define XHCI_MSI86_EV_REMOVE        5UL
#define XHCI_MSI86_EV_LOOK_ABSENT   6UL     /* not opened: no reference   */
#define XHCI_MSI86_EV_LOOK_FAILED   7UL     /* opened, no IRP: reference  */
#define XHCI_MSI86_EV_LOOK_REFUSED  8UL     /* opened, refused: reference */
#define XHCI_MSI86_EV_LOOK_ACCEPTED 9UL     /* opened, accepted           */

#define XHCI_MSI86_ACT_LOOK         0x01UL  /* make a look                */
#define XHCI_MSI86_ACT_DROP         0x02UL  /* drop the held reference    */
#define XHCI_MSI86_ACT_DROP_LOOK    0x04UL  /* drop the look's reference  */

typedef struct _XHCI_MSI86_BIND {
    ULONG State;
    ULONG Held;             /* a reference kept for a BOUND answer        */
    ULONG Looks;
} XHCI_MSI86_BIND, *PXHCI_MSI86_BIND;

VOID XhciMsi86BindInit(PXHCI_MSI86_BIND bind);

/* One event; returns XHCI_MSI86_ACT_* for the executor to perform. */
ULONG XhciMsi86BindStep(PXHCI_MSI86_BIND bind, ULONG event);

/* ---- the trace (record 18 section 9) ---------------------------------- */

#define XHCI_MSI_TRACE_FIELDS       10UL
#define XHCI_MSI_TRACE_MAX          120UL   /* with its terminator        */

/* Record kinds. */
#define XHCI_MSI_TRACE_BEGIN        'B'
#define XHCI_MSI_TRACE_CONFIG       'C'
#define XHCI_MSI_TRACE_DESCRIPTOR   'D'
#define XHCI_MSI_TRACE_END          'E'
#define XHCI_MSI_TRACE_LIFE         'L'
#define XHCI_MSI_TRACE_START        'S'

/*
 * One record, "M86 K FFFF SSSS NNNN" then up to ten fields of eight hex
 * digits and " c" with two hex digits of the byte sum of everything before
 * it. NNNN is the record's number, one more than the previous record's
 * from the same driver image (1 after a load), so a lost record of any
 * kind shows as a gap. Returns its length, the terminator not counted. The
 * longest record is under XHCI_MSI_TRACE_MAX by construction, which the
 * debug channel's 128-byte line with its 8-byte prefix holds.
 */
ULONG XhciMsiTraceFormat(char *line, char kind, ULONG fdo, ULONG sequence,
                         ULONG number, const ULONG *fields, ULONG count);

#endif /* XHCI_MSI_H */
