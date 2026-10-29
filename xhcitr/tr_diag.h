/*
 * tr_diag.h - the pure half of XHCITR's "PCI and bridge diagnostics" node
 * (design record 25 section 20, revision 23; roadmap-usb4 task 44.62):
 * the values the package's drivers leave in the registry about PCI
 * bridges and interrupts, read by the system half (xhcitr.c) and decoded
 * here to text, each beside its raw form. No Windows call:
 * test\test_trdiag.c links it under /Za. C89.
 *
 *   - XHCIPFW's log (design record 32 sections 6 and 11.2);
 *   - XhciCmBridgeDiag (record 21 section 6.1.1, src\cm\cm_statb.h);
 *   - XhciCmNoIrqDiag (record 28 section 7, src\cm\cm_noirq.h);
 *   - task 44.53's records in xhcicm's log (record 21 section 6.2.1).
 *
 * Every name the tool reads is in one table in tr_diag.c, so a rename
 * touches one place. No line of the text passes 79 columns.
 */

#ifndef TR_DIAG_H
#define TR_DIAG_H

#include "tr_core.h"

/* The registry's value types (winnt.h's numbers). */
#define TR_REG_SZ               1UL
#define TR_REG_BINARY           3UL
#define TR_REG_DWORD            4UL

/* One registry value as the system half read it. */
#define TR_DV_MAX               512
#define TR_DV_ABSENT            0   /* not there (or not readable)        */
#define TR_DV_OK                1   /* Type, Len and Data as read         */
#define TR_DV_LONG              2   /* longer than TR_DV_MAX: Type alone  */

typedef struct _TR_DVAL {
    int State;
    TR_U32 Type;
    TR_U32 Len;
    unsigned char Data[TR_DV_MAX];
} TR_DVAL;

/* A key as the system half opened it. */
#define TR_DK_ABSENT            0
#define TR_DK_OPEN              1
#define TR_DK_ERROR             2   /* it could not be opened: the error  */

/* ---- XHCIPFW's log (design record 32 sections 6 and 11.2) ------------- */

/* Where it is, under HKEY_LOCAL_MACHINE: 98 SE and ME, and NT (the
 * service xhcipfw's Parameters). */
#define TR_PFW_KEY_9X   "System\\CurrentControlSet\\Services\\VxD\\XHCIPFW"
#define TR_PFW_KEY_NT \
    "SYSTEM\\CurrentControlSet\\Services\\xhcipfw\\Parameters"

/* The values, in the order they are shown (tr_pfw_name). */
#define TR_PFW_VERSION          0
#define TR_PFW_SKIPPED          1   /* NT only, as the next three         */
#define TR_PFW_MODE             2
#define TR_PFW_HOLDTICKS        3
#define TR_PFW_TICKHZ           4
#define TR_PFW_RESULT           5
#define TR_PFW_CLOSED           6
#define TR_PFW_REFUSED          7
#define TR_PFW_PARKED           8
#define TR_PFW_ACROSS           9
#define TR_PFW_BARSINSIDE       10
#define TR_PFW_FUNCTIONS        11
#define TR_PFW_XHCIHIGH         12
#define TR_PFW_BRIDGES          13
#define TR_PFW_BARS             14
#define TR_PFW_XHCI             15
#define TR_PFW_COUNT            16

/* Record sizes and the tables' bounds (pfw_scan.h). */
#define TR_PFW_BRIDGE_BYTES     28
#define TR_PFW_BAR_BYTES        8
#define TR_PFW_XHCI_BYTES       16
#define TR_PFW_MAX_BRIDGES      16
#define TR_PFW_MAX_BARS         16
#define TR_PFW_MAX_XHCI         8

/* Value i's name and its type; NULL past the table. */
const char *tr_pfw_name(int i);
TR_U32 tr_pfw_type(int i);
/* 1 for a value XHCIPFW.SYS writes and XHCIPFW.VXD does not. */
int tr_pfw_nt_only(int i);

/* The part's text: `key` TR_DK_*, `err` the open's error for
 * TR_DK_ERROR, `v` TR_PFW_COUNT values (read only when the key is
 * open). nt: 2000 and later. */
void tr_pfw_text(TR_TEXT *t, int key, TR_U32 err, const TR_DVAL *v, int nt);

/* ---- the connection manager's two records (98 SE and ME) -------------- */

#define TR_BRDIAG_VALUE         "XhciCmBridgeDiag"
#define TR_NIDIAG_VALUE         "XhciCmNoIrqDiag"
#define TR_BRDIAG_WORDS         20  /* cm_statb.h CM_BRDIAG_WORDS          */
#define TR_NIDIAG_WORDS         12  /* cm_noirq.h CM_NI_DIAG_WORDS         */

/* Each record's text from its value (TR_DV_ABSENT: "not recorded", with
 * on NT why). nt: 2000 and later. */
void tr_brdiag_text(TR_TEXT *t, const TR_DVAL *v, int nt);
void tr_nidiag_text(TR_TEXT *t, const TR_DVAL *v, int nt);

/* Short forms the text is made of, for the host suite. */
const char *tr_brdiag_outcome(TR_U32 outcome);
const char *tr_brdiag_win(TR_U32 outcome);
const char *tr_brdiag_irq(TR_U32 outcome);
const char *tr_ni_irq(TR_U32 v);
const char *tr_ni_state(TR_U32 v);
const char *tr_ni_why(TR_U32 v);
const char *tr_ni_op(TR_U32 v);
/* "PCI 05:01.0" for a location in bus 15:8, device 7:3, function 2:0;
 * "none" for FFFFFFFFh. */
void tr_diag_bdf(TR_U32 bdf, char *out, size_t cap);
/* A type 1 header's 20h (memory window) as "E0000000-E00FFFFF" or
 * "closed (20h ...)" */
void tr_diag_memwin(TR_U32 v, char *out, size_t cap);
/* A prefetchable window from 24h, 28h and 2Ch as
 * "00000001_00000000-00000040_000FFFFF" or "closed". */
void tr_diag_pfwin(TR_U32 r24, TR_U32 r28, TR_U32 r2c, char *out,
                   size_t cap);

/* ---- task 44.53's records in xhcicm's log ------------------------------ */

/* The labels (record 25 section 20 item 5), NULL past the table. */
const char *tr_irq_label(int i);

/* The records of the log text `log` (n bytes, oldest first, lines
 * "label=XXXXXXXX" ending CR LF; the first may be cut) whose label is one
 * of the table's, each decoded with its raw line, indented by `indent`:
 * the last `max` of them, with a line saying so when there are more.
 * Returns how many there are. */
#define TR_IRQ_SHOWN            16
int tr_irq_lines(TR_TEXT *t, const unsigned char *log, TR_U32 n,
                 int indent, int max);

/* ---- the node --------------------------------------------------------- */

/* The node's opening lines. */
void tr_diag_head(TR_TEXT *t);

/* s at `indent`, cut into lines of at most 79 columns. */
void tr_diag_wrap(TR_TEXT *t, int indent, const char *s);

/* An NHI's heading: its name and instance id (wrapped at 79 columns). */
void tr_diag_nhi(TR_TEXT *t, const char *name, const char *id);

#endif /* TR_DIAG_H */
