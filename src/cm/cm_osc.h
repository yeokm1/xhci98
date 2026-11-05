/*
 * cm_osc.h - the pure half of the platform's USB4 handover on 98 SE and
 * ME (roadmap-usb4.md task 46.12; design record 32 section 12.8): the
 * ACPI tables found in memory and checked, MCFG's ECAM, the firmware's
 * data area found by a bounded parse of its AML, the firmware's two
 * tables decoded, and the match that decides whether the command is sent.
 * DDK-free and host-tested (test\test_cm_osc.c); cm_oscos.c is the OS
 * half.
 *
 * C89, no allocation, no lock. IRQL: any (the reader decides).
 */

#ifndef CM_OSC_H
#define CM_OSC_H

#include "../xhci_compat.h"

/* Reasons (the first refusal of section 12.8.4), logged as they are. */
#define CM_OSC_OK               0UL
#define CM_OSC_NO_RSDP          1UL   /* no root pointer that checks      */
#define CM_OSC_NO_ROOT_TABLE    2UL   /* no RSDT or XSDT that checks      */
#define CM_OSC_NO_MCFG          3UL   /* no MCFG allocation for the buses */
#define CM_OSC_NO_DISCUSB4      4UL   /* the firmware's USB4 table absent  */
#define CM_OSC_NO_CPNV          5UL   /* no data region with both fields  */
#define CM_OSC_CPNV_TWICE       6UL   /* two placements that disagree     */
#define CM_OSC_NO_POINTER       7UL   /* a pointer zero or unreadable     */
#define CM_OSC_NOT_FITTED       8UL   /* byte 10h zero                     */
#define CM_OSC_NOT_GRANTED      9UL   /* byte 4Dh zero                     */
#define CM_OSC_NO_ROOT_ENTRY    10UL  /* the roots entry absent (FFh)     */
#define CM_OSC_OTHER_ROOT       11UL  /* names another root port          */
#define CM_OSC_BAD_ADDRESS      12UL  /* a table above 4 GB or in ECAM    */
#define CM_OSC_UNKNOWN_CODE     13UL  /* the methods are not a known build */
#define CM_OSC_SAME_OFF         14UL  /* the same handover code in another
                                         build, XhciCmAsm4242Structural 0 */

/* The tiers of the firmware's recognition, as the status record reports
 * them (record 32 section 12.8.11). */
#define CM_OSC_TIER_NONE        0UL   /* not looked for                   */
#define CM_OSC_TIER_UNKNOWN     1UL   /* looked for, not recognised       */
#define CM_OSC_TIER_KNOWN       2UL   /* a known build                    */
#define CM_OSC_TIER_SAME        3UL   /* the same handover code, another
                                         build                            */

/* Reads `bytes` of physical memory at `phys` into `out`; returns the bytes
 * read (fewer: unreadable). */
typedef ULONG (*CM_OSC_READ)(PVOID context, ULONG phys, UCHAR *out,
                             ULONG bytes);

/* TRUE when the `n` bytes sum to zero modulo 256. */
BOOLEAN CmOscSumZero(const UCHAR *p, ULONG n);

/*
 * The root pointer in `area` (`bytes` long, starting on a 16-byte boundary
 * of physical memory): the offset of the first "RSD PTR " on a 16-byte
 * boundary whose first 20 bytes sum to zero and, for revision 2 or later,
 * whose whole length (at most 36..255 and inside `area`) sums to zero; or
 * 0xFFFFFFFF.
 */
ULONG CmOscFindRsdp(const UCHAR *area, ULONG bytes);

/* From a root pointer that CmOscFindRsdp accepted: the RSDT's address,
 * and the XSDT's when revision 2 or later gives one below 4 GB (else 0). */
VOID CmOscRsdpTables(const UCHAR *rsdp, PULONG rsdt, PULONG xsdt);

/* The header fields a table's 36-byte header gives. */
typedef struct _CM_OSC_HDR {
    ULONG Signature;    /* the four characters, the first in bits 7:0  */
    ULONG Length;
    UCHAR OemId[6];
    UCHAR OemTableId[8];
    ULONG OemRevision;
} CM_OSC_HDR;

#define CM_OSC_SIG(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | \
                                ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

/* The header of a table at `p` (36 bytes at least). */
VOID CmOscHeader(const UCHAR *p, CM_OSC_HDR *h);

/*
 * The n-th entry of a root table (`t`, its whole length checked by the
 * caller): an RSDT entry is 4 bytes, an XSDT entry 8 (`xsdt`), an entry
 * whose high dword is not zero is returned as 0 (not reachable). FALSE past
 * the end.
 */
BOOLEAN CmOscRootEntry(const UCHAR *t, ULONG length, BOOLEAN xsdt, ULONG n,
                       PULONG phys);

/*
 * MCFG (`t`, `length` bytes, checked): the base of the one allocation for
 * segment 0 whose bus range holds busLo to busHi, the address of bus 0,
 * 1 MB aligned and below 4 GB, with its last bus; FALSE when none, when
 * two cover them, or when the one that does is malformed.
 */
BOOLEAN CmOscMcfg(const UCHAR *t, ULONG length, ULONG busLo, ULONG busHi,
                  PULONG base, PULONG busEnd);

/* The ECAM address of `bdf` (XHCI_TBT_BDF form) at `offset` (a dword):
 * base + (bus << 20) + (device << 15) + (function << 12) + offset, the bus
 * at most busEnd, the function's page below 4 GB. */
BOOLEAN CmOscEcam(ULONG base, ULONG busEnd, ULONG bdf, ULONG offset,
                  PULONG phys);

/* Whether `bytes` at `phys` lie in RAM (record 32 section 12.8.3 step 0):
 * below A0000h, in E0000h to FFFFFh, or from 1 MB up to topMem. */
BOOLEAN CmOscInRam(ULONG phys, ULONG bytes, ULONG topMem);

/* The AML walk's limits and state (record 32 section 12.8.3 steps 4 and
 * 5), accumulated over every SSDT with OEM ID AMD. */
#define CM_OSC_DEPTH    16UL
#define CM_OSC_REFS     8UL
#define CM_OSC_METHODS  5UL     /* \_SB.M625, \_SB._OSC, \M049, \M624, \M011 */

typedef struct _CM_OSC_SCOPE {
    ULONG Segs;
    UCHAR Seg[CM_OSC_DEPTH][4];
} CM_OSC_SCOPE;

/* A Field whose region name ends in CPNV. */
typedef struct _CM_OSC_REF {
    CM_OSC_SCOPE Path;  /* the region's name as resolved from its scope */
    CM_OSC_SCOPE Scope;
    BOOLEAN Single;     /* one plain NameSeg (searched upward)          */
    ULONG Ok;           /* both pointers placed                         */
    ULONG Off620;       /* byte offsets of M620 and M251                */
    ULONG Off251;
    ULONG EndBits;      /* the list's end                               */
} CM_OSC_REF;

typedef struct _CM_OSC_WALK {
    ULONG Table;        /* the caller's index of the table walked next  */
    ULONG Regions;      /* \CPNV: 0 none, 1 one usable, 2 not usable    */
    ULONG Base;
    ULONG Length;
    ULONG CpnvAnywhere; /* regions named CPNV at any path               */
    ULONG Refs;
    CM_OSC_REF Ref[CM_OSC_REFS];
    ULONG MethodSeen[CM_OSC_METHODS];   /* each method found, times    */
    ULONG MethodHash[CM_OSC_METHODS];   /* CmOscHash of its code        */
    ULONG MethodTable[CM_OSC_METHODS];  /* where its code is: the table  */
    ULONG MethodAt[CM_OSC_METHODS];     /* (Table as walked), the offset */
    ULONG MethodLen[CM_OSC_METHODS];    /* of its flags byte in that     */
                                        /* table's AML, flags and body   */
} CM_OSC_WALK;

/*
 * One SSDT's AML (`aml`, `length` bytes after its header) walked as a term
 * list from the root into *w (zeroed by the caller once for every table):
 * Scope and Device descended, Method fingerprinted and skipped, the other
 * known terms decoded or skipped by their lengths. FALSE when the table
 * cannot be walked whole (an unknown term, a length past its end, a depth
 * or work bound); nothing of it is then kept.
 */
BOOLEAN CmOscAmlWalk(const UCHAR *aml, ULONG length, CM_OSC_WALK *w);

/*
 * After every table: \CPNV's address and length and the byte offsets of
 * its two pointers, CM_OSC_OK; or CM_OSC_NO_CPNV (no region, no field
 * placing both, a field past the region's end) or CM_OSC_CPNV_TWICE (two
 * regions or two placements that disagree, or a region not usable).
 */
ULONG CmOscCpnvPlace(const CM_OSC_WALK *w, PULONG base, PULONG length,
                     PULONG off620, PULONG off251);

/*
 * A declaration lookup (record 32 section 12.8.10, "Names"): every
 * declaration of the canonical path `path` at the root walk of one table
 * (`aml`, `length` bytes after its header), Field elements, Externals and
 * Methods included; `scratch` the caller's, overwritten. Returns
 * CM_OSC_FIND_* bits; *args the argument count of a method found.
 */
#define CM_OSC_FIND_VALUE   0x1UL   /* declared as anything but a method */
#define CM_OSC_FIND_METHOD  0x2UL   /* a Method or an External method    */
#define CM_OSC_FIND_ARGS    0x4UL   /* two methods with other arg counts */
#define CM_OSC_FIND_FAILED  0x8UL   /* the table not walked whole        */
#define CM_OSC_FIND_ALIAS   0x10UL  /* an Alias: its object's kind is not
                                       established, the lookup declines */
ULONG CmOscFind(const UCHAR *aml, ULONG length, const CM_OSC_SCOPE *path,
                CM_OSC_WALK *scratch, PULONG args);

/* The firmware's discrete USB4 table, its first CM_OSC_DUSB4_BYTES. */
#define CM_OSC_DUSB4_BYTES  0x50UL
#define CM_OSC_D_FITTED     0x10UL
#define CM_OSC_D_ROOT_INDEX 0x12UL
#define CM_OSC_D_ROOT_DEV   0x13UL
#define CM_OSC_D_ROOT_FN    0x14UL
#define CM_OSC_D_DATA       0x4BUL
#define CM_OSC_D_GRANT      0x4DUL

/* The roots table's entry for index i, at 10h + 8 x i, 8 bytes. */
#define CM_OSC_ROOT_ENTRY(i) (0x10UL + 8UL * (ULONG)(i))

/*
 * The match of section 12.8.4 from the decoded parts: `dusb4` the discrete
 * USB4 table's first CM_OSC_DUSB4_BYTES, `entry` the roots table's 8-byte
 * entry for its index, `rootBdf` the root port found by bus (XHCI_TBT_BDF
 * form: bus 15:8, device 7:3, function 2:0). Returns a CM_OSC_* reason, and
 * the command's data byte in *data on CM_OSC_OK.
 */
ULONG CmOscDecide(const UCHAR *dusb4, const UCHAR *entry, ULONG rootBdf,
                  PULONG data);

/* The two commands (record 32 section 12.8.1). */
#define CM_OSC_CMD_HANDOVER     0x0102UL
#define CM_OSC_CMD_QUERY        0x0180UL
#define CM_OSC_MB_CMD           0x420UL
#define CM_OSC_MB_REPLY         0x430UL
#define CM_OSC_MB_BUSY(v)       ((((v) >> 8) & 0xFUL) == 1UL)
#define CM_OSC_MB_READY(v)      ((((v) >> 8) & 0xFUL) != 0UL)

/* FNV-1a 32 of `n` bytes. */
ULONG CmOscHash(const UCHAR *p, ULONG n);

/* The firmware builds whose code was read (cm_osc.c), each a hash per
 * method in CM_OSC_METHODS' order. */
extern const ULONG CmOscKnownCode[][CM_OSC_METHODS];
extern const ULONG CmOscKnownCount;

/* CM_OSC_OK when each method was found once and its code is one known
 * build's, else CM_OSC_UNKNOWN_CODE. */
ULONG CmOscCodeKnown(const CM_OSC_WALK *w);

/* ---- the second tier: the canonical form (cm_oscc.c; record 32 section
 * 12.8.10) -------------------------------------------------------------- */

/* One of the three tables, its AML after the header. */
typedef struct _CM_OSC_TBL {
    const UCHAR *Aml;
    ULONG Length;
} CM_OSC_TBL;

#define CM_OSCC_FORM        0x8000UL    /* a form's bytes at most        */
#define CM_OSCC_NAMES       64UL        /* distinct spellings a method   */
#define CM_OSCC_OWN         32UL        /* names a body declares         */
#define CM_OSCC_TERMS       20000UL     /* terms a method                */
#define CM_OSCC_STMT_DEPTH  16UL
#define CM_OSCC_EXPR_DEPTH  32UL

/* Why a method has no form (logged with the offset in its table). */
#define CM_OSCC_W_TERM      1UL     /* a term outside the parser's set   */
#define CM_OSCC_W_LENGTH    2UL     /* a length past its package or end  */
#define CM_OSCC_W_BUDGET    3UL     /* terms or nesting                  */
#define CM_OSCC_W_FORM      4UL     /* the form full                     */
#define CM_OSCC_W_NAME      5UL     /* not a name string                 */
#define CM_OSCC_W_KIND      6UL     /* a name's declaration refused      */
#define CM_OSCC_W_OWN       7UL     /* a declaration by the body refused */
#define CM_OSCC_W_NAMES     8UL     /* more spellings than CM_OSCC_NAMES */

/* A name as written: the root prefix, parent prefixes, segments. */
typedef struct _CM_OSCC_SPELL {
    ULONG Root;
    ULONG Ups;
    ULONG Segs;
    UCHAR Seg[CM_OSC_DEPTH][4];
} CM_OSCC_SPELL;

typedef struct _CM_OSCC_NAME {
    CM_OSCC_SPELL Spell;
    ULONG Kind;         /* cm_oscc.c's CC_*                              */
    ULONG Args;
    ULONG RootSeg;      /* found declared at the root, one segment       */
} CM_OSCC_NAME;

/* The canonicaliser's state, the caller's (about 40 KB). */
typedef struct _CM_OSCC {
    const CM_OSC_TBL *Tbl;
    ULONG Tables;
    CM_OSC_SCOPE Method;
    ULONG Len;
    ULONG Names;
    ULONG Owns;
    ULONG Terms;
    ULONG Why;          /* CM_OSCC_W_*, 0 while the parse holds          */
    ULONG WhyAt;
    ULONG LocalNext;
    UCHAR LocalMap[8];
    UCHAR Own[CM_OSCC_OWN][4];
    CM_OSCC_NAME Name[CM_OSCC_NAMES];
    CM_OSC_WALK Scratch;
    UCHAR Form[CM_OSCC_FORM];
} CM_OSCC;

/*
 * Method `m` (CM_OSC_METHODS' order) of the walk `w`, whose code lies in
 * tbl[w->MethodTable[m]], parsed whole into its canonical form in c->Form
 * (c->Len bytes), its FNV-1a and CRC-32 in *fnv and *crc. Returns 0, or a
 * CM_OSCC_W_* reason (c->WhyAt the offset in that table's AML).
 */
ULONG CmOscCanon(CM_OSCC *c, const CM_OSC_TBL *tbl, ULONG tables,
                 const CM_OSC_WALK *w, ULONG m, PULONG fnv, PULONG crc);

/* CRC-32 of `n` bytes. */
ULONG CmOscCrc(const UCHAR *p, ULONG n);

/* The canonical forms read (cm_oscc.c): CM_OSC_OK when the five pairs are
 * one entry's, else CM_OSC_UNKNOWN_CODE. */
extern const ULONG CmOscKnownForm[][CM_OSC_METHODS][2];
extern const ULONG CmOscKnownFormCount;
ULONG CmOscFormKnown(const ULONG fnv[CM_OSC_METHODS],
                     const ULONG crc[CM_OSC_METHODS]);

#endif /* CM_OSC_H */
