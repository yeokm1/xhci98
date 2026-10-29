/*
 * pf_s98.h - Stor98's counter answer as XHCIPERF reads it (design record 29
 * section 6; Stor98's include\stor98pf.h, its decision MD-8): the answer and
 * the identity page checked, byte by byte at the header's offsets (the
 * window's build holds these offsets to the header with compile-time
 * checks, xhciperf.c), two answers subtracted, and the Stor98 page's text.
 * Pure: test\test_pfs98.c links it on the host.
 */

#ifndef XHCIPERF_PF_S98_H
#define XHCIPERF_PF_S98_H

#include "pf_core.h"

/* Offsets in the answer (stor98pf.h, STOR98PF_ANSWER) */
#define S98_OFS_SIGNATURE       0x00
#define S98_OFS_VERSION         0x04
#define S98_OFS_LENGTH          0x08
#define S98_OFS_EPOCH           0x0C
#define S98_OFS_BUILD           0x10
#define S98_OFS_PCI             0x14
#define S98_OFS_VENDORDEVICE    0x18
#define S98_OFS_STATE           0x1C
#define S98_OFS_DRIVERVERSION   0x20
#define S98_OFS_BLOCKSIZE       0x30
#define S98_OFS_BLOCKSLO        0x34
#define S98_OFS_BLOCKSHI        0x38
#define S98_OFS_MAXTRANSFER     0x3C
#define S98_OFS_BREAKS          0x40
#define S98_OFS_MDTS            0x44
#define S98_OFS_MAXCHILD        0x48
#define S98_OFS_CHILDLIMIT      0x4C
#define S98_OFS_CHILDRENIO      0x50
#define S98_OFS_WT              0x54
#define S98_OFS_VWC             0x58
#define S98_OFS_POLLING         0x5C
#define S98_OFS_DCBSEEN         0x60
#define S98_OFS_DCBMAXXFER      0x64
#define S98_OFS_DCBMAXSG        0x68
#define S98_OFS_DCBDMD          0x6C
#define S98_OFS_EVENTS          0x70
#define S98_OFS_TOTALS          0x90
#define S98_EVENTS              8
#define S98_ANSWER_BYTES        (0x90 + 8 * 121)

#define S98_SIGNATURE           0x50383953u
#define S98_VERSION             1
#define S98_VPD_PAGE            0xC9
#define S98_VPD_LEN             16
#define S98_BUFFER_ID           0x53
#define S98_DCB_PHYS_SGD        0x00000800u

/* Totals: index of each in the totals block (8 bytes each) */
#define S98_SIZES               13
#define S98_T_SRBS              0                       /* [2] */
#define S98_T_SECTORS           2                       /* [2] */
#define S98_T_SRBSIZE           4                       /* [2][13] */
#define S98_T_CMDSIZE           30                      /* [2][13] */
#define S98_T_SPLITS            56
#define S98_T_BOUNCED           57
#define S98_T_FLUSHES           58
#define S98_T_SRBERRORS         59
#define S98_T_DEPTH             60                      /* [7] */
#define S98_T_RUNS              67                      /* [5][10] */
#define S98_T_SG9X              117                     /* [3] */
#define S98_T_DOUBLEBUF         120
#define S98_TOTALS              121
#define S98_DEPTHS              7
#define S98_RUN_CLASSES         5
#define S98_RUN_BUCKETS         10
#define S98_RUN_NOT_SEEN        9
#define S98_RUN_CLASS_FIRST     7

typedef struct s98_answer {
    pf_u32 version;
    pf_u32 length;
    pf_u32 epoch;
    pf_u32 build;
    pf_u32 pci;
    pf_u32 vendorDevice;
    pf_u32 state;
    char driverVersion[17];
    pf_u32 blockSize;
    pf_u64 blocks;
    pf_u32 maxTransfer, breaks, mdts, maxChild, childLimit, childrenIo;
    pf_u32 writeThrough, vwc, polling;
    pf_u32 dcbSeen, dcbMaxXfer, dcbMaxSg, dcbDmd;
    pf_u32 events[S98_EVENTS];
    pf_u64 tot[S98_TOTALS];
} S98_ANSWER;

/* 1 and *a filled when Bytes (Len of them) are a Stor98 answer this reader
 * knows; 0 otherwise. */
int s98_parse(const unsigned char *bytes, pf_u32 len, S98_ANSWER *a);

/* 1 when an INQUIRY C9h answer is Stor98's identity page; *pci its
 * location. */
int s98_identity(const unsigned char *page, pf_u32 len, pf_u32 *pci);

/* 1 when page 00h (len bytes) lists page C9h. */
int s98_lists_identity(const unsigned char *page, pf_u32 len);

/* The difference between two answers: 1, or 0 when they cannot be
 * subtracted (another epoch, a total that went down). */
typedef struct s98_delta {
    pf_u64 tot[S98_TOTALS];
    pf_u32 events[S98_EVENTS];
} S98_DELTA;
int s98_diff(const S98_ANSWER *before, const S98_ANSWER *after, S98_DELTA *d);

/* The scattered conclusion (record 29 section 3.4) for a test of Size
 * bytes, direction Write */
#define S98_SCAT_UNKNOWN        0
#define S98_SCAT_YES            1
#define S98_SCAT_NO             2
#define S98_SCAT_HIDDEN         3
int s98_scattered(const S98_DELTA *d, pf_u32 size, int write);
const char *s98_scattered_text(int conclusion);

/* The size bucket of a request (stor98pf.h's edges) and its name */
int s98_size_bucket(pf_u32 bytes);
const char *s98_size_name(int bucket);

/* The Stor98 page's text for one test (record 29 section 5.5). Is9x picks
 * the 9x lines. TestReqs and TestSize: what the test issued. */
void s98_page(PF_TEXT *t, const S98_ANSWER *after, const S98_DELTA *d, int is9x, pf_u64 testReqs,
              pf_u32 testSize, int write);
/* The facts alone (no test chosen) */
void s98_facts(PF_TEXT *t, const S98_ANSWER *a, int is9x);

/* What the search for the disk's Stor98 adapter found (record 29
 * section 6.2) */
#define S98_ST_NONE     0   /* not Stor98's disk */
#define S98_ST_OK       1
#define S98_ST_ADMIN    2   /* NT: needs administrator rights */
#define S98_ST_NOASPI   3   /* 9x: no ASPI layer */
#define S98_ST_AMBIG    4   /* 9x: the disk's adapter could not be told apart */
#define S98_ST_DEAD     5   /* did not answer in time */
#define S98_ST_LETTER   6   /* 9x: several disks claim the letter, none told present */
/* 9x: where a search that found no Stor98 disk ended (record 29 section
 * 16.5) */
#define S98_WHY_NONE        0   /* not said (NT) */
#define S98_WHY_NOLETTER    1   /* no disk under Enum\SCSI holds the letter */
#define S98_WHY_NOTARGET    2   /* no ASPI target answers as Stor98; n: ASPI adapters */
#define S98_WHY_NOMATCH     3   /* n Stor98 targets answer, none the letter's disk */
typedef struct s98_why {
    int why;                /* S98_WHY_* */
    char letter;
    int n;
    int models;             /* NOMATCH: Stor98 targets whose model was read */
    int cmAnswered;         /* NOLETTER: the configuration manager answered */
    char id[64];            /* the letter's disk id, cut to 60 characters */
} S98_WHY;
void s98_why_set(S98_WHY *w, int why, char letter, int n, const char *id);
/* The Stor98 page's sentence for a state that is not OK (OK: empty);
 * matches and ids are a 9x search's counts, for an ambiguity (for
 * S98_ST_LETTER, ids is how many disks claim the letter); w (may be NULL)
 * where a 9x search ended, for S98_ST_NONE */
void s98_state_sentence(PF_TEXT *t, int state, int matches, int ids, const S98_WHY *w);
/* The report's line when it carries no test's Stor98 page: "Stor98: "
 * and the state's sentence, or for OK that no test's counters were taken */
void s98_report_line(PF_TEXT *t, int state, int matches, int ids, const S98_WHY *w);

#endif /* XHCIPERF_PF_S98_H */
