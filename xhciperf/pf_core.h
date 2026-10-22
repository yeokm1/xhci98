/*
 * pf_core.h - XHCIPERF's pure half (design record 29 sections 3 to 5 and
 * 9): the test plan, the presets, the generator and the verifiable
 * pattern, the generation allocator, the statistics, the test file's
 * header, the free-space and path rules, the workload key, and the text of
 * every result, report and saved file. No Windows call: test\test_pf.c
 * links it on the host. C89 with __int64 (record 29 section 3.6).
 */

#ifndef XHCIPERF_PF_CORE_H
#define XHCIPERF_PF_CORE_H

#include <stddef.h>

typedef unsigned __int64 pf_u64;
typedef unsigned long pf_u32;

/* System family */
#define PF_OS_9X            1
#define PF_OS_NT            2

/* Test kinds, operations, access, buffers, patterns */
#define PF_SEQ              0
#define PF_RND              1
#define PF_OP_READ          1
#define PF_OP_WRITE         2
#define PF_OP_BOTH          3
#define PF_ACC_UNBUF        0
#define PF_ACC_CACHED       1
#define PF_BUF_ALIGNED      0
#define PF_BUF_OFFSET       1
#define PF_BUF_SCATTER      2
#define PF_BUFS             3
#define PF_PAT_VERIFY       0
#define PF_PAT_RANDOM       1
#define PF_PAT_ZEROS        2
#define PF_SHOW_BEST        0
#define PF_SHOW_MEAN        1
#define PF_SHOW_MEDIAN      2

#define PF_UNIT             512u            /* the pattern's unit */
#define PF_HEADER_BYTES     1048576u        /* the file's ownership region */
#define PF_MAX_TESTS        16
#define PF_MAX_STEPS        96
#define PF_MAX_PASSES       9
#define PF_MAX_DEPTH        32
#define PF_MAX_THREADS      8
#define PF_MAX_RUN_SEC      7200u           /* the plan's limit on a run */
#define PF_MAP_MAX_BYTES    (16u * 1048576u)
#define PF_GEN_LAST         0xFFFFFFFEu     /* the allocator's last value */
#define PF_GEN_UNKNOWN      0xFFFFFFFFu     /* a block whose write did not complete */
#define PF_LAT_BUCKETS      64
#define PF_SIZES            15              /* 512 B to 8 MiB */

typedef struct pf_test {
    int kind;               /* PF_SEQ, PF_RND */
    pf_u32 size;            /* bytes */
    int depth;
    int threads;
} PF_TEST;

/* The parameters as the window, a preset or the command line set them. */
typedef struct pf_params {
    int os;                 /* PF_OS_* */
    int preset;             /* PF_PRESET_*, or PF_PRESET_CUSTOM */
    pf_u32 fileMiB;         /* the test region, MiB */
    int passes;
    int timeSec;            /* per fixed test */
    int pauseSec;
    int warmup;
    int ops;                /* PF_OP_* */
    int access;             /* PF_ACC_* */
    int writeThrough;
    int flushInWrite;       /* cached writes: the flush inside the time */
    int bufMask;            /* 1 << PF_BUF_* of each layout run */
    int pattern;            /* PF_PAT_* */
    int verify;
    int verifyDuring;
    int verifyEnd;
    int stopOnError;
    int sweep;              /* 1: a sweep, else the fixed tests */
    pf_u32 sweepMin;        /* bytes, a power of two from 512 to 8 MiB */
    pf_u32 sweepMax;
    int sweepTimed;         /* 1: time per size; 0: total bytes */
    pf_u32 sweepTotalMiB;   /* 0: the region's size */
    int sweepDepth;
    int ntests;
    PF_TEST tests[PF_MAX_TESTS];
    pf_u32 seed;
    int show;               /* PF_SHOW_* */
    int unitMiB;            /* 1: MiB/s, else MB/s */
} PF_PARAMS;

/* What the system says about the volume and the machine. */
typedef struct pf_env {
    int os;
    pf_u32 sector;          /* logical sector */
    pf_u32 physSector;      /* 0: not reported */
    pf_u32 alignMask;       /* the adapter's, 0: not reported */
    int fat;                /* FAT or FAT32 */
    int systemVolume;       /* the Windows or the 9x swap file's volume */
    pf_u64 freeUser;        /* bytes this user may still write */
    pf_u64 freeTotal;       /* the volume's free bytes */
    pf_u64 volumeBytes;     /* the volume's size as reported to this user */
    pf_u64 memBytes;        /* physical memory */
} PF_ENV;

/* One measured step of the plan. */
typedef struct pf_step {
    int kind;
    pf_u32 size;
    int depth;
    int threads;
    int buffer;             /* PF_BUF_* */
    int op;                 /* PF_OP_READ or PF_OP_WRITE */
    int sweep;              /* a sweep row */
    pf_u64 budget;          /* bytes per pass, 0: timed */
    int timeSec;
    int testIndex;          /* the fixed test, or the sweep size's index */
    char label[40];         /* "Seq 1M d8 t1" */
} PF_STEP;

typedef struct pf_plan {
    int nsteps;
    PF_STEP steps[PF_MAX_STEPS];
    pf_u64 regionBytes;     /* the tests' part of the file */
    pf_u64 fileBytes;       /* with the header */
    pf_u32 maxSize;         /* the largest request */
    pf_u32 minWrite;        /* the smallest write, the map's block */
    pf_u32 slots;           /* buffers: the most depth x threads */
    pf_u64 bufferBytes;     /* all buffers */
    pf_u64 mapBlocks;       /* verifiable runs */
    pf_u64 budgetWrites;    /* writes of the byte-budgeted steps, passes and warm-ups */
    pf_u32 estimateSec;
    int verify;             /* the run verifies */
    int warnSmallPart;      /* a part smaller than the depth */
    int warnCache;          /* cached reads of a file that fits in memory */
} PF_PLAN;

/* Presets (record 29 section 3.7) */
#define PF_PRESET_STANDARD  0
#define PF_PRESET_9X        1
#define PF_PRESET_STUDY     2
#define PF_PRESET_SWEEP     3
#define PF_PRESET_QUICK     4
#define PF_PRESETS          5
#define PF_PRESET_CUSTOM    (-1)

const char *pf_preset_name(int preset);
int pf_preset_by_name(const char *name);
void pf_defaults(PF_PARAMS *p, int os);
void pf_preset(PF_PARAMS *p, int preset);

/* The plan: 0 and *plan filled, or a refusal's code with its sentence in why. */
#define PF_OK               0
#define PF_REFUSE           1
int pf_plan(const PF_PARAMS *p, const PF_ENV *env, PF_PLAN *plan, char *why, size_t whyCap);

/* The free-space rule (section 4.3): 0, or 1 with the refusal in why. */
int pf_free_check(const PF_ENV *env, pf_u64 fileBytes, char *why, size_t whyCap);
pf_u64 pf_reserve(const PF_ENV *env);

/* Labels */
void pf_test_label(int kind, pf_u32 size, int depth, int threads, char *out, size_t cap);
void pf_test_long(int kind, pf_u32 size, int depth, int threads, char *out, size_t cap);
void pf_size_text(pf_u32 bytes, char *out, size_t cap);
const char *pf_buf_name(int buffer);

/* The generator (section 3.5): Marsaglia's 32-bit xorshift on a nonzero state. */
pf_u32 pf_xs_seed(pf_u32 seed);
pf_u32 pf_xs_mix(pf_u32 a, pf_u32 b, pf_u32 c);
pf_u32 pf_xs_next(pf_u32 *state);
pf_u32 pf_thread_seed(pf_u32 seed, int thread);

/* The verifiable pattern (section 3.3): Bytes (a multiple of PF_UNIT) at
 * unit Unit of the file. */
void pf_stamp(unsigned char *buf, pf_u32 bytes, pf_u32 runId, pf_u64 unit, pf_u32 gen);
void pf_fill_random(unsigned char *buf, pf_u32 bytes, pf_u32 seed);
/* Returns the number of bad units; *firstBad the first one's index in buf
 * and *foundGen the generation it carried (PF_GEN_UNKNOWN when its stamp is
 * not this run's or not this unit's). */
pf_u32 pf_check(const unsigned char *buf, pf_u32 bytes, pf_u32 runId, pf_u64 unit, pf_u32 gen,
                pf_u32 *firstBad, pf_u32 *foundGen);

/* The generation allocator (section 3.3): the caller holds the run's lock. */
typedef struct pf_gen {
    pf_u32 last;            /* the last value handed out; PF_GEN_LAST: saturated */
} PF_GEN;
void pf_gen_init(PF_GEN *g);
pf_u32 pf_gen_take(PF_GEN *g);  /* 0: none (saturated) */

/* Statistics (section 3.6) */
pf_u64 pf_ticks_us(pf_u64 ticks, pf_u64 freq);
pf_u64 pf_rate10(pf_u64 bytes, pf_u64 us, int mib);     /* tenths of MB/s or MiB/s; 0 for no time */
pf_u64 pf_iops(pf_u64 reqs, pf_u64 us);
void pf_fmt_tenths(pf_u64 v10, char *out, size_t cap);   /* "123.4" */
void pf_fmt_u64(pf_u64 v, char *out, size_t cap);
void pf_fmt_us(pf_u64 us, char *out, size_t cap);        /* "812 us", "3.45 ms", "1.20 s" */

typedef struct pf_lat {
    pf_u64 count;
    pf_u64 sumUs;
    pf_u64 minUs;
    pf_u64 maxUs;
    pf_u32 hist[PF_LAT_BUCKETS];
} PF_LAT;
void pf_lat_init(PF_LAT *l);
void pf_lat_add(PF_LAT *l, pf_u64 us);
void pf_lat_merge(PF_LAT *into, const PF_LAT *from);
pf_u64 pf_lat_edge(int bucket);                           /* a bucket's upper edge, us */
pf_u64 pf_lat_pct(const PF_LAT *l, int pct);              /* the bucket edge at pct percent */
pf_u64 pf_lat_mean(const PF_LAT *l);

/* The timer's plausibility (section 3.6): 1 when QPC agrees with the tick
 * count over an interval. */
int pf_timer_plausible(pf_u64 qpcUs, pf_u64 tickMs);

/* One pass of one step */
#define PF_ST_NONE          0
#define PF_ST_OK            1
#define PF_ST_STOPPED       2
#define PF_ST_FAILED        3
#define PF_ST_DATAERR       4
#define PF_ST_SKIPPED       5   /* the disk refused a misaligned buffer */
typedef struct pf_result {
    int status;
    pf_u64 bytes;
    pf_u64 reqs;
    pf_u64 us;
    pf_u64 stampUs;         /* processor time stamping buffers */
    int timerBad;
    int cpuLimited;
    pf_u32 error;           /* the system's error code of a failure */
    pf_u32 badUnits;
    PF_LAT lat;
} PF_RESULT;

typedef struct pf_shown {
    int status;
    pf_u64 rate10;
    pf_u64 iops;
    pf_u64 latMean, latP50, latP99, latMin, latMax;
    int haveLat;
    int timerBad;
    int cpuLimited;
} PF_SHOWN;

/* The value shown for a step over its passes (section 3.6). */
void pf_shown(const PF_RESULT *passes, int npasses, int show, int mib, int highRes, PF_SHOWN *out);

/* The processor share mark */
int pf_cpu_limited(pf_u64 stampUs, pf_u64 us);

/* Text */
typedef struct pf_text {
    char *buf;
    size_t len;
    size_t cap;
    int overflow;
} PF_TEXT;
void pf_tx_init(PF_TEXT *t, char *buf, size_t cap);
void pf_tx_s(PF_TEXT *t, const char *s);
void pf_tx_u(PF_TEXT *t, pf_u64 v);
void pf_tx_pad(PF_TEXT *t, const char *s, int width, int right);
void pf_tx_nl(PF_TEXT *t);

/* The file's ownership header (section 3.1): PF_UNIT bytes. */
typedef struct pf_header {
    pf_u32 version;
    pf_u32 runLo, runHi;    /* the run's 64-bit identity */
    pf_u64 plannedBytes;    /* the whole file */
    pf_u32 volSerial;
    pf_u32 createdLo, createdHi;   /* FILETIME */
} PF_HEADER;
void pf_header_build(const PF_HEADER *h, unsigned char *out);
int pf_header_parse(const unsigned char *in, pf_u32 len, PF_HEADER *h);   /* 1: the tool's own */

/* The leftover check (section 3.1) */
typedef struct pf_record {
    int present;
    pf_u32 runLo, runHi;
    pf_u64 plannedBytes;
    pf_u64 preparedBytes;
    pf_u32 volSerial;
    pf_u32 indexLo, indexHi;   /* NT file index, 0 0 not known */
} PF_RECORD;
#define PF_LEFT_FOREIGN     0   /* not the tool's: refuse, keep it */
#define PF_LEFT_OURS        1   /* the record matches: offer to delete */
#define PF_LEFT_UNRECORDED  2   /* the tool's header, no matching record: ask with the stronger prompt */
int pf_leftover(const PF_HEADER *h, int headerOk, const PF_RECORD *rec, pf_u64 size,
                pf_u32 indexLo, pf_u32 indexHi, int haveIndex);

/* Paths (section 4.1): 1 when Path is Dir or below it, compared without
 * case on canonical paths. */
int pf_path_under(const char *path, const char *dir);

/* The workload key (section 5.6) */
void pf_key(const PF_PARAMS *p, int highRes, char *out, size_t cap);
/* 1 when two keys are equal; else the differing field names in diff */
int pf_key_same(const char *a, const char *b, char *diff, size_t cap);

/* The report (section 5.6): the human text, then the machine lines. */
typedef struct pf_runinfo {
    const char *toolVersion;
    const char *date;
    const char *system;
    const char *drive;
    const char *fileSystem;
    const char *model;      /* may be "" */
    pf_u32 sector, physSector;
    pf_u64 timerFreq;       /* 0: milliseconds */
    pf_u32 attributes;
    const char *stor98;     /* the Stor98 page's text, or "" */
} PF_RUNINFO;
void pf_report(PF_TEXT *t, const PF_PARAMS *p, const PF_PLAN *plan, const PF_RESULT *res,
               int npasses, const PF_RUNINFO *ri);
void pf_summary_line(PF_TEXT *t, const PF_STEP *s, const PF_SHOWN *sh, int mib);

/* Saved results (section 5.6) */
#define PF_SAVED_MAX        PF_MAX_STEPS
typedef struct pf_saved_row {
    char label[40];
    int op;
    int buffer;
    pf_u64 rate10mb;        /* best pass, MB/s x10 */
    pf_u64 rate10mib;
    pf_u64 iops;
    pf_u64 latMeanUs;
} PF_SAVED_ROW;
typedef struct pf_saved {
    char key[512];
    int nrows;
    PF_SAVED_ROW rows[PF_SAVED_MAX];
} PF_SAVED;
/* 0 and *s filled, or the 1-based line number of the first bad line */
long pf_saved_parse(const char *text, size_t len, PF_SAVED *s);
/* change in percent tenths of cur against old; 0 when old is 0 */
long pf_change10(pf_u64 cur, pf_u64 old);

/* The command line (section 5.7): 0, or 1 with the refusal in why. */
typedef struct pf_cmdline {
    int run;
    int exitAtEnd;
    int selftest;
    char folder[260];
    char report[260];
} PF_CMDLINE;
int pf_cmdline(int argc, char **argv, PF_PARAMS *p, PF_CMDLINE *c, char *why, size_t whyCap);

#endif /* XHCIPERF_PF_CORE_H */
