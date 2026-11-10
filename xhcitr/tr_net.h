/*
 * tr_net.h - XHCITR's "Thunderbolt networking" nodes (design record 35
 * section 17; roadmap-usb4.md task 48.18): xhcicm.sys's networking answer
 * (src\cm\cm_qnet.h CM_QN_*) read as bytes, the far side's kind, the
 * labels and the details text. tr_net.c is pure (test\test_trnet.c links
 * it under /Za); tr_netw.c is the half that calls Windows: the request,
 * the IP address and the snapshot's section. C89.
 */

#ifndef TR_NET_H
#define TR_NET_H

#include "tr_core.h"
#include "../src/cm/cm_qnet.h"

/* ---- the answer --------------------------------------------------------- */

typedef struct _TR_QNET {
    TR_U32 Bytes;               /* the answer's, or the size to ask for    */
    TR_U32 Flags;               /* CM_QN_F_*                               */
    TR_U32 EpochLo;
    TR_U32 EpochHi;
    TR_U32 Pass;
    TR_U32 AgeMs;
    TR_U32 NetValue;
    TR_U32 E2EValue;
    TR_U32 Cm;                  /* CM_ST_CM_*                              */
    const unsigned char *Svc;   /* SvcBytes, into the caller's bytes */
    unsigned long SvcBytes;     /* CM_QN_SVC_MIN_BYTES or more */
    int LinkCount;
    const unsigned char *Link[CM_QN_MAX_LINKS];   /* CM_QN_LINK_BYTES each */
    const unsigned char *Client;    /* NULL for none                       */
    TR_U32 ClientBytes;         /* at least 4 where Client is set          */
} TR_QNET;

/* The answer of n bytes into q (pointers into b): 1 read; -1 a header
 * alone or an answer cut short whose CM_QN_O_BYTES is larger (ask again
 * with q->Bytes); 0 not one this tool reads (a wrong magic or version, an
 * offset, shape or size past the bytes, a service block smaller than
 * CM_QN_SVC_BYTES, entries smaller than CM_QN_LINK_BYTES or more than
 * CM_QN_MAX_LINKS, a client block under 4 bytes or past the answer). */
int tr_qnet_parse(const unsigned char *b, size_t n, TR_QNET *q);

/* 1 when the NHI gets the node: XhciCmNet is on, or a link is listed. */
int tr_qnet_show(const TR_QNET *q);

/* ---- the far side ------------------------------------------------------- */

#define TR_NETK_UNKNOWN         0
#define TR_NETK_LINUX           1       /* vendorid 1D6Bh, "Linux"          */
#define TR_NETK_XHCI98          2       /* "xHCI98", "xhcicm"               */
#define TR_NETK_NONE            3       /* no block read yet                */
#define TR_NETK_WINDOWS         4       /* vendorid 045Eh                   */

/* The kind from the link entry's block alone, and its text into kindText
 * ("Linux (thunderbolt_net)", "xHCI98", "Windows (USB4 P2P network
 * adapter)", "not known", "not read yet");
 * the other computer's name into name ("" where the block gives none:
 * Linux's device text is its host name). */
int tr_qnet_kind(const unsigned char *link, char *kindText, size_t kindCap,
                 char *name, size_t nameCap);

/* ---- labels and text ---------------------------------------------------- */

/* "Thunderbolt networking - up, 40 Gb/s" and the like. */
void tr_qnet_label(const TR_QNET *q, char *out, size_t cap);

/* "Link on port 1 - connected: myhost" and the like, for link i. */
void tr_qnet_link_label(const TR_QNET *q, int i, char *out, size_t cap);

/* The details of the node (link -1: the service and every link; else that
 * link alone), each line within 79 columns. ip: the adapter's addresses
 * as tr_netw.c read them (NULL: not shown). pub: the snapshot's form,
 * which withholds the MACs, the UUIDs, the IP address and the peer's
 * texts (the snapshot is pasted publicly; design record 35 section 17.5). */
void tr_qnet_text(TR_TEXT *t, const TR_QNET *q, int link, const char *ip,
                  int pub);

/* The index in `now` of the link entry `was` (an entry of an earlier
 * answer of the same start): the same link number and, where both know
 * it, the same peer UUID; -1 when the link is gone or another peer is on
 * it. */
int tr_qnet_link_match(const TR_QNET *now, const unsigned char *was);

/* A MAC as ipconfig writes it, "02-1D-6B-81-87-4F". */
void tr_qnet_mac(const unsigned char *mac6, char *out, size_t cap);

/* A 64-bit count of two words in decimal. */
void tr_qnet_u64(TR_U32 lo, TR_U32 hi, char *out, size_t cap);

/* ---- tr_netw.c (Windows) ------------------------------------------------ */

#ifndef XHCI_HOST_TEST
/* The answer of query device qname ("XHCICM0", or a path) into *buf
 * (allocated; the caller frees), parsed into q: 1 read, 0 not served (a
 * driver before task 48.18), -1 failed (*err the error where there was
 * one). nt: 0 on 98 SE and ME, where an open with no access is tried
 * again with read and write access. */
int tr_net_read(const char *qname, int nt, unsigned char **buf, TR_QNET *q,
                unsigned long *err);

/* The IPv4 addresses Windows gives the adapter with that MAC, "a.b.c.d
 * (mask)" each, comma-separated, into out; "none" where it has none,
 * "not read (...)" where it could not be asked. */
void tr_net_ip(const unsigned char *mac6, char *out, size_t cap);

/* The snapshot's section: every query device XHCICM0 to XHCICM15 that
 * answers, in the public form. */
void tr_net_dev_text(TR_TEXT *t, int nt);
#endif

#endif /* TR_NET_H */
