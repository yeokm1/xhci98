/*
 * tr_pci.h - the pure half of XHCITR's "PCI configuration" nodes (design
 * record 25 sections 3.6 and 4.7, revision 12): xhcicm.sys's PCI answer
 * (src\cm\cm_query.h CM_QP_*) and xhci98.sys's PCI region (src\xhci.h
 * XHCI_SNAPSHOT_REGION_PCI) read as bytes, and a function's configuration
 * space, offsets 00h to FFh as read, decoded to text. No Windows call:
 * test\test_trpci.c links it under /Za. C89.
 *
 * The capability ids, register offsets and bits are this file's own
 * constants (tr_pci.c), each cited to the public specification that
 * defines it; nothing is taken from a Microsoft header. The capability
 * list is walked only where the Status register's Capabilities List bit
 * is set, with each pointer masked to a DWORD boundary, never below 40h,
 * never visited twice and at most TR_PCI_CAP_MAX entries; a capability
 * whose registers would lie past the 256 bytes read is named and not
 * decoded.
 */

#ifndef TR_PCI_H
#define TR_PCI_H

#include "tr_core.h"
#include "tr_log.h"

#define TR_PCI_CFG_BYTES        256

/* ---- the capability walk ------------------------------------------------ */

#define TR_PCI_CAP_MAX          48

typedef struct _TR_PCICAP {
    unsigned int Id;
    unsigned int At;            /* the capability's offset */
} TR_PCICAP;

/* How a walk ended: 0 the list ended with a null pointer. */
#define TR_PCI_WALK_NONE        0x01    /* the Capabilities List bit clear */
#define TR_PCI_WALK_BAD_PTR     0x02    /* a pointer below 40h (or past FCh)*/
#define TR_PCI_WALK_LOOP        0x04    /* a pointer visited twice         */
#define TR_PCI_WALK_CUT         0x08    /* more than `max` entries         */

/* The capabilities of cfg (TR_PCI_CFG_BYTES) into out (max entries).
 * Returns the count; *how the TR_PCI_WALK_* bits, *badAt the offending
 * pointer for BAD_PTR or LOOP. */
int tr_pci_caps(const unsigned char *cfg, TR_PCICAP *out, int max, int *how,
                unsigned int *badAt);

/* ---- decoding ----------------------------------------------------------- */

/* A function's text, every line indented by `indent` spaces, none over
 * 79 columns: identity, class, command and status, interrupt pin and
 * line, the BARs raw, for a type 1 header the buses and the three
 * windows, then the capabilities (MSI, MSI-X, PCI Express with the link,
 * power management; any other by id). */
void tr_pci_text(TR_TEXT *t, const unsigned char *cfg, int indent);

/* Short forms the text is made of, for the host suite. */
void tr_pci_pin(unsigned int pin, unsigned int line, char *out, size_t cap);
void tr_pci_msi(const unsigned char *cfg, unsigned int at, char *out,
                size_t cap);
void tr_pci_msix(const unsigned char *cfg, unsigned int at, char *out,
                 size_t cap);
/* "Gen 3 x4 (capable Gen 3 x4)"; "down" where the width reads 0. */
void tr_pci_link(const unsigned char *cfg, unsigned int at, char *out,
                 size_t cap);
const char *tr_pci_port_type(unsigned int type);
const char *tr_pci_class_text(unsigned long classCode);

/* ---- xhcicm.sys: the PCI answer (CM_QP_*) ------------------------------- */

typedef struct _TR_QPCI {
    TR_U32 Bytes;
    TR_U32 Flags;               /* CM_QP_F_*                                */
    TR_U32 EpochLo;
    TR_U32 EpochHi;
    TR_U32 Count;               /* entries                                  */
    TR_U32 NhiBdf;
    TR_U32 Ports;
    TR_U32 Reads;
    TR_U32 Fails;
    const unsigned char *Ent;   /* the first entry                          */
} TR_QPCI;

typedef struct _TR_QPENT {
    TR_U32 Bdf;
    TR_U32 Role;                /* CM_QP_R_*                                */
    TR_U32 State;               /* CM_QP_S_*                                */
    TR_U32 Port;
    TR_U32 Parent;
    TR_U32 Depth;
    TR_U32 Info;
    const unsigned char *Cfg;   /* TR_PCI_CFG_BYTES                         */
} TR_QPENT;

/* An answer of n bytes (no more than the call returned). 1 a whole,
 * consistent answer; -1 the header alone, or a size asking for more than
 * n (q->Bytes says how much, at most CM_QP_MAX_BYTES); 0 anything else:
 * the wrong magic or version, an entry shape not version 1's, entries
 * past the answer or more than CM_QP_MAX_ENTRIES. */
int tr_qpci_parse(const unsigned char *b, size_t n, TR_QPCI *q);

/* Entry i (< q->Count) of a parsed answer. */
void tr_qpci_entry(const TR_QPCI *q, TR_U32 i, TR_QPENT *e);

/* The node's text from a parsed answer: the NHI, each tunnel port, the
 * functions below each (or why they were not read). nt: 2000 and later. */
void tr_qpci_text(TR_TEXT *t, const TR_QPCI *q, int nt);

/* ---- xhci98.sys: its query request (src\xhci_qry.h) --------------------- */

/* The node's text from a parsed IOCTL_XHCI98_QUERY answer (tr_log.h's
 * tr_xhq_parse), or NULL for a driver that refused the request: "not
 * served by this xhci98.sys". A configuration read the driver could not
 * make reads "unread". Returns 1 when decoded. */
int tr_xhq_pci_text(TR_TEXT *t, const TR_XHQ *x);

#endif /* TR_PCI_H */
