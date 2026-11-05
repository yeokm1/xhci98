/*
 * tr_acpi.h - the pure half of File > Save Firmware Tables... and XHCITR
 * /acpi <folder> (design record 25 section 21, revision 26; task 46.21):
 * xhcicm.sys's firmware tables answer (src\cm\cm_query.h CM_QA_*) read as
 * bytes, the tables Windows' interface gave added one by one, each
 * table's header read for the index, the files' names, the index's text
 * and the message. No Windows call: test\test_tracpi.c links it under
 * /Za. C89.
 *
 * A table's header is read as the ACPI Specification lays it out (the
 * system description table header: signature 0, length 4, revision 8,
 * checksum 9, OEM ID 10, OEM table ID 16; the root pointer: signature
 * "RSD PTR " 0, checksum 8, OEM ID 9, revision 15, length 20 from
 * revision 2; the FACS: signature and length, no checksum). Nothing else
 * of a table is interpreted.
 */

#ifndef TR_ACPI_H
#define TR_ACPI_H

#include "tr_core.h"

#define TR_ACPI_MAX             256     /* tables a save holds */
#define TR_ACPI_HDR             36

#define TR_ACPI_SRC_NT          1       /* GetSystemFirmwareTable */
#define TR_ACPI_SRC_CM          2       /* xhcicm.sys's request */
#define TR_ACPI_SRC_XHA         3       /* xhci98.sys's request (option B),
                                           xhcicm.sys's copies merged    */
#define TR_ACPI_SRC_REG         4       /* 2000 and XP: the registry      */

/* Why a table is not saved: xhci98.sys's XHA_R_* (1 to 8), and the
 * tool's own. */
#define TR_ACPI_R_NOT_KEPT      20      /* the registry has no copy       */
#define TR_ACPI_R_TYPE          21      /* a registry value not binary    */

#define TR_ACPI_HELD_NONE       0
#define TR_ACPI_HELD_WHOLE      1
#define TR_ACPI_HELD_HEADER     2

#define TR_ACPI_SUM_NONE        0       /* "-": no checksum to check */
#define TR_ACPI_SUM_OK          1
#define TR_ACPI_SUM_BAD         2

typedef struct _TR_ACPI_T {
    unsigned char Sig[4];       /* as held; the root pointer "RSDP"       */
    int Rsdp;                   /* the root pointer                       */
    int Held;                   /* TR_ACPI_HELD_*                         */
    const unsigned char *Data;  /* the caller's bytes, Bytes of them      */
    TR_U32 Bytes;
    TR_U32 Length;              /* the length its header gives            */
    int Listed;                 /* NT: how often Windows' list named it   */
    char File[16];              /* "SSDT2.DAT"; "" when not saved         */
    int Kind;                   /* the answer's kind (XHA_K_*); 0 NT/reg  */
    TR_U32 Index;               /* the root entry's index                 */
    TR_U32 Reason;              /* why not whole: XHA_R_*, TR_ACPI_R_*    */
    int From;                   /* TR_ACPI_SRC_* of this table            */
    int SameAsCm;               /* xhcicm.sys's copy of it is the same    */
    TR_U32 Addr;                /* the registry's RSDT entries: address   */
} TR_ACPI_T;

typedef struct _TR_ACPI {
    int Source;                 /* TR_ACPI_SRC_*                          */
    int Count;
    TR_ACPI_T T[TR_ACPI_MAX];
    /* xhcicm.sys's answer: */
    TR_U32 State;               /* CM_QA_S_*                              */
    TR_U32 Outcome;
    TR_U32 Tier;
    TR_U32 Flags;               /* CM_QA_F_*                              */
    int DsdtAsked;              /* NT: DSDT asked by name, not listed     */
    int RootAsked;              /* NT: XSDT or RSDT asked by name         */
    /* 98 SE and ME: xhci98.sys's state when the save is xhcicm.sys's
     * copies alone (TR_ACPI_SRC_CM): XHA_S_*, 0 not served, -1 not read,
     * -2 no controller of it. */
    long XhaState;
} TR_ACPI;

/* ---- xhcicm.sys's answer -------------------------------------------------- */

typedef struct _TR_QAHDR {
    TR_U32 SetGen;              /* xhci98.sys's answer: its generation    */
    TR_U32 Bytes;
    TR_U32 Flags;
    TR_U32 EpochLo;
    TR_U32 EpochHi;
    TR_U32 State;
    TR_U32 Outcome;
    TR_U32 Tier;
    TR_U32 BodyBytes;
    TR_U32 Count;
    TR_U32 EntBytes;
    TR_U32 PartAt;
    TR_U32 PartBytes;
} TR_QAHDR;

/* One answer of `got` bytes: 1 with *h filled when its magic, version and
 * sizes hold (its bytes field equal to got, its part inside the body and
 * within CM_QA_PART_MAX, the body within CM_QA_MAX_BODY), else 0. */
int tr_qa_header(const unsigned char *b, TR_U32 got, TR_QAHDR *h);

/* xhci98.sys's answer (src\xhci_acpi.h XHA_*, the same word offsets):
 * as tr_qa_header, its own magic, version and bounds. */
int tr_xha_header(const unsigned char *b, TR_U32 got, TR_QAHDR *h);

/* Whether a later part's header belongs with the first's: the same epoch,
 * generation, body size and entry shape, and the part at `want`. */
int tr_qa_same(const TR_QAHDR *first, const TR_QAHDR *h, TR_U32 want);

/* The whole body (bytes of it, count entries) into a: 1 when every entry
 * is inside it and holds no more than its kind allows, the entries
 * pointing into `body`; else 0 and a->Count 0. An entry of kind 0 (a root
 * entry not read) is skipped. State, Outcome, Tier and Flags are the
 * caller's to set from the header. */
int tr_qa_body(const unsigned char *body, TR_U32 bytes, TR_U32 count,
               TR_ACPI *a);

/* xhci98.sys's body: as tr_qa_body, the DSDT kind and each entry's reason
 * too; every table's From TR_ACPI_SRC_XHA. */
int tr_xha_body(const unsigned char *body, TR_U32 bytes, TR_U32 count,
                TR_ACPI *a);

/* xhcicm.sys's copies (cm, read by tr_qa_body) merged into xhci98.sys's
 * set a: a whole copy the same as a's whole table of the same kind and
 * root index marks it SameAsCm; one that differs, or has no whole table
 * to match, is added (From TR_ACPI_SRC_CM, saved as <SIG><n>C.DAT). */
void tr_acpi_merge(TR_ACPI *a, const TR_ACPI *cm);

/* ---- 2000 and 32-bit XP: the registry's copies ------------------------- */

/* The registry, as the caller reads it (KEY_READ): a subkey's name by
 * index under a path below HKLM (0 when none), a value's data (its type,
 * its bytes; 0 when absent). */
typedef struct _TR_REGOPS {
    int (*Subkey)(void *ctx, const char *path, int index, char *name,
                  size_t cap);
    int (*Value)(void *ctx, const char *path, const char *name,
                 unsigned char *buf, TR_U32 cap, TR_U32 *bytes,
                 TR_U32 *type);
    void *Ctx;
} TR_REGOPS;

#define TR_REG_BINARY           3UL     /* REG_BINARY                     */
#define TR_REG_SUBKEYS          8       /* a level's subkeys read         */
#define TR_REG_TABLE_MAX        0x100000UL

/* A registry value as a table of key `key` ("DSDT", "FADT", "RSDT",
 * "FACS"): 0 when it is a REG_BINARY of at least 36 bytes (the FACS, 64)
 * whose signature is the key's (FADT's "FACP"), whose length field is its
 * size, at most TR_REG_TABLE_MAX, and whose checksum is zero (the FACS
 * has none); else the reason (XHA_R_SIGNATURE, _LENGTH, _CHECKSUM,
 * TR_ACPI_R_TYPE). */
TR_U32 tr_acpi_reg_check(const char *key, TR_U32 type,
                         const unsigned char *data, TR_U32 bytes);

/* HKLM\HARDWARE\ACPI walked into a (TR_ACPI_SRC_REG): each of DSDT, FADT
 * and RSDT, every <OEM ID>\<table ID>\<revision> subkey's value
 * "00000000" (at most TR_REG_SUBKEYS a level, 4 tables a key), and FACS's
 * own; the data copied into store (cap bytes) the tables point into. A
 * value that fails tr_acpi_reg_check is listed, not saved, with its
 * reason. The RSDT's entries are not records of their own: the index
 * (tr_acpi_index) lists them by address, counted. Returns the tables
 * saved whole. */
int tr_acpi_reg_walk(const TR_REGOPS *ops, TR_ACPI *a, unsigned char *store,
                     TR_U32 cap);

/* Why a table is not saved, as text ("" for none). */
const char *tr_acpi_reason(TR_U32 r);

/* ---- the tables ------------------------------------------------------------- */

/* A table Windows gave (bytes of it at data) added whole: 0 when the
 * list is full. listed is how often the list named its signature. */
int tr_acpi_add(TR_ACPI *a, const unsigned char *data, TR_U32 bytes,
                int listed);

/* The checksum's state: TR_ACPI_SUM_NONE for a header alone and the FACS;
 * else OK when the bytes held are the length its header gives and sum to
 * zero (the root pointer: its first 20, and from revision 2 its whole
 * length as its length field gives it, 36 to 255). */
int tr_acpi_sum(const TR_ACPI_T *t);

/* Each whole table's file name, by its signature (a character outside
 * A-Z and 0-9 written '_', a-z as A-Z; the root pointer "RSDP") and its
 * index, from 1, among the whole tables whose name has that same stem, so
 * two signatures written alike never share a name: "SSDT2.DAT". Returns
 * the files named. */
int tr_acpi_names(TR_ACPI *a);

/* The signature for text: four characters, one outside printable ASCII
 * written '.'. out holds 5. */
void tr_acpi_sig(const TR_ACPI_T *t, char *out);

/* "RSDP, XSDT, FACP, DSDT, SSDT (3)": the whole tables' signatures in
 * the order held, a count after one held more than once. */
void tr_acpi_list(const TR_ACPI *a, char *out, size_t cap);

/* TABLES.TXT: the tables, a line each, then where they came from; every
 * line within 79 columns, ending CR LF. */
void tr_acpi_index(const TR_ACPI *a, TR_TEXT *t);

/* ---- the message -------------------------------------------------------- */

#define TR_ACPI_M_SAVED         0       /* written                       */
#define TR_ACPI_M_NO_API        1       /* NT with no interface          */
#define TR_ACPI_M_NO_CM         2       /* 98 SE / ME, no query device   */
#define TR_ACPI_M_CM_NONE       3       /* 98 SE / ME, the driver kept none */
#define TR_ACPI_M_NT_NONE       4       /* the interface gave none       */
#define TR_ACPI_M_EXISTS        5       /* a name exists in the folder   */
#define TR_ACPI_M_WRITE         6       /* a write failed                */
#define TR_ACPI_M_NO_98         7       /* 98 SE / ME, neither driver     */
#define TR_ACPI_M_XHA_NONE      8       /* 98 SE / ME, xhci98.sys's reason
                                           (a->XhaState), no copies      */

/* xhcicm.sys's reason for keeping none: the state (CM_QA_S_*); 0 for a
 * driver that does not serve the request, -1 for an answer that could
 * not be read. */
const char *tr_qa_why(long state);

/* xhci98.sys's reason for reading none (a->XhaState's values). */
const char *tr_xha_why(long state);

/* The message for `kind`: a->... for SAVED and CM_NONE (a->State, or
 * `why` when given), folder the one picked, name the file for EXISTS and
 * WRITE, n and m the files written and to write for WRITE. */
void tr_acpi_message(int kind, const TR_ACPI *a, const char *folder,
                     const char *name, const char *why, int n, int m,
                     char *out, size_t cap);

#endif /* TR_ACPI_H */
