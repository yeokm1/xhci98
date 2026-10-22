/*
 * pf_core.c - XHCIPERF's pure half (pf_core.h; design record 29).
 */

#include <string.h>
#include "pf_core.h"

/* ------------------------------------------------------------------------ */
/* Bounded strings                                                           */
/* ------------------------------------------------------------------------ */

static void pf_cpy(char *out, size_t cap, const char *s)
{
    size_t n;

    if (cap == 0) {
        return;
    }
    n = strlen(s);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, s, n);
    out[n] = 0;
}

static void pf_cat(char *out, size_t cap, const char *s)
{
    size_t have = strlen(out);

    if (have + 1 >= cap) {
        return;
    }
    pf_cpy(out + have, cap - have, s);
}

static char pf_up(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    if (c == '/') {
        return '\\';
    }
    return c;
}

void pf_fmt_u64(pf_u64 v, char *out, size_t cap)
{
    char tmp[24];
    int i = 0;
    int j;

    do {
        tmp[i++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v != 0 && i < (int)sizeof(tmp));
    if (cap == 0) {
        return;
    }
    for (j = 0; j < i && (size_t)j + 1 < cap; j++) {
        out[j] = tmp[i - 1 - j];
    }
    out[j] = 0;
}

void pf_fmt_tenths(pf_u64 v10, char *out, size_t cap)
{
    char whole[24];
    char frac[2];

    pf_fmt_u64(v10 / 10, whole, sizeof(whole));
    frac[0] = (char)('0' + (int)(v10 % 10));
    frac[1] = 0;
    pf_cpy(out, cap, whole);
    pf_cat(out, cap, ".");
    pf_cat(out, cap, frac);
}

/* v in hundredths -> "a.bc" */
static void pf_fmt_hundredths(pf_u64 v100, char *out, size_t cap)
{
    char whole[24];
    char frac[3];

    pf_fmt_u64(v100 / 100, whole, sizeof(whole));
    frac[0] = (char)('0' + (int)((v100 / 10) % 10));
    frac[1] = (char)('0' + (int)(v100 % 10));
    frac[2] = 0;
    pf_cpy(out, cap, whole);
    pf_cat(out, cap, ".");
    pf_cat(out, cap, frac);
}

void pf_fmt_us(pf_u64 us, char *out, size_t cap)
{
    char n[32];

    if (us < 1000) {
        pf_fmt_u64(us, n, sizeof(n));
        pf_cpy(out, cap, n);
        pf_cat(out, cap, " us");
    } else if (us < 1000000) {
        pf_fmt_hundredths((us + 5) / 10, n, sizeof(n));
        pf_cpy(out, cap, n);
        pf_cat(out, cap, " ms");
    } else {
        pf_fmt_hundredths((us + 5000) / 10000, n, sizeof(n));
        pf_cpy(out, cap, n);
        pf_cat(out, cap, " s");
    }
}

/* ------------------------------------------------------------------------ */
/* Text                                                                      */
/* ------------------------------------------------------------------------ */

void pf_tx_init(PF_TEXT *t, char *buf, size_t cap)
{
    t->buf = buf;
    t->len = 0;
    t->cap = cap;
    t->overflow = 0;
    if (cap != 0) {
        buf[0] = 0;
    }
}

void pf_tx_s(PF_TEXT *t, const char *s)
{
    size_t n = strlen(s);

    if (t->cap == 0) {
        t->overflow = 1;
        return;
    }
    if (t->len + n + 1 > t->cap) {
        n = t->cap - 1 - t->len;
        t->overflow = 1;
    }
    memcpy(t->buf + t->len, s, n);
    t->len += n;
    t->buf[t->len] = 0;
}

void pf_tx_u(PF_TEXT *t, pf_u64 v)
{
    char n[24];

    pf_fmt_u64(v, n, sizeof(n));
    pf_tx_s(t, n);
}

void pf_tx_pad(PF_TEXT *t, const char *s, int width, int right)
{
    int n = (int)strlen(s);
    int i;

    if (right) {
        for (i = n; i < width; i++) {
            pf_tx_s(t, " ");
        }
    }
    pf_tx_s(t, s);
    if (!right) {
        for (i = n; i < width; i++) {
            pf_tx_s(t, " ");
        }
    }
}

void pf_tx_nl(PF_TEXT *t)
{
    pf_tx_s(t, "\r\n");
}

/* ------------------------------------------------------------------------ */
/* Labels                                                                    */
/* ------------------------------------------------------------------------ */

void pf_size_text(pf_u32 bytes, char *out, size_t cap)
{
    char n[24];

    if (bytes >= 1048576u && bytes % 1048576u == 0) {
        pf_fmt_u64(bytes / 1048576u, n, sizeof(n));
        pf_cpy(out, cap, n);
        pf_cat(out, cap, "M");
    } else if (bytes >= 1024u && bytes % 1024u == 0) {
        pf_fmt_u64(bytes / 1024u, n, sizeof(n));
        pf_cpy(out, cap, n);
        pf_cat(out, cap, "K");
    } else {
        pf_fmt_u64(bytes, n, sizeof(n));
        pf_cpy(out, cap, n);
    }
}

void pf_test_label(int kind, pf_u32 size, int depth, int threads, char *out, size_t cap)
{
    char s[24];
    char n[24];

    pf_cpy(out, cap, kind == PF_RND ? "Rnd " : "Seq ");
    pf_size_text(size, s, sizeof(s));
    pf_cat(out, cap, s);
    pf_cat(out, cap, " d");
    pf_fmt_u64((pf_u64)depth, n, sizeof(n));
    pf_cat(out, cap, n);
    pf_cat(out, cap, " t");
    pf_fmt_u64((pf_u64)threads, n, sizeof(n));
    pf_cat(out, cap, n);
}

void pf_test_long(int kind, pf_u32 size, int depth, int threads, char *out, size_t cap)
{
    char s[24];
    char n[24];

    pf_cpy(out, cap, kind == PF_RND ? "Random " : "Sequential ");
    if (size >= 1048576u && size % 1048576u == 0) {
        pf_fmt_u64(size / 1048576u, n, sizeof(n));
        pf_cat(out, cap, n);
        pf_cat(out, cap, " MiB");
    } else if (size >= 1024u && size % 1024u == 0) {
        pf_fmt_u64(size / 1024u, n, sizeof(n));
        pf_cat(out, cap, n);
        pf_cat(out, cap, " KiB");
    } else {
        pf_fmt_u64(size, s, sizeof(s));
        pf_cat(out, cap, s);
        pf_cat(out, cap, " bytes");
    }
    pf_cat(out, cap, ", depth ");
    pf_fmt_u64((pf_u64)depth, n, sizeof(n));
    pf_cat(out, cap, n);
    pf_cat(out, cap, threads == 1 ? ", 1 thread" : ", ");
    if (threads != 1) {
        pf_fmt_u64((pf_u64)threads, n, sizeof(n));
        pf_cat(out, cap, n);
        pf_cat(out, cap, " threads");
    }
}

const char *pf_buf_name(int buffer)
{
    switch (buffer) {
    case PF_BUF_OFFSET:
        return "offset";
    case PF_BUF_SCATTER:
        return "scattered";
    default:
        return "aligned";
    }
}

/* ------------------------------------------------------------------------ */
/* Presets (record 29 section 3.7)                                           */
/* ------------------------------------------------------------------------ */

static const char *const pf_preset_names[PF_PRESETS] = {
    "Standard",
    "Standard for Windows 98 and ME",
    "Request-size study for Windows 98 and ME",
    "Sweep",
    "Quick"
};

static const char *const pf_preset_keys[PF_PRESETS] = {
    "standard", "standard9x", "study9x", "sweep", "quick"
};

const char *pf_preset_name(int preset)
{
    if (preset < 0 || preset >= PF_PRESETS) {
        return "Custom";
    }
    return pf_preset_names[preset];
}

static int pf_ieq(const char *a, const char *b)
{
    while (*a != 0 && *b != 0) {
        if (pf_up(*a) != pf_up(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int pf_preset_by_name(const char *name)
{
    int i;

    for (i = 0; i < PF_PRESETS; i++) {
        if (pf_ieq(name, pf_preset_keys[i])) {
            return i;
        }
    }
    return PF_PRESET_CUSTOM;
}

static void pf_set_test(PF_TEST *t, int kind, pf_u32 size, int depth, int threads)
{
    t->kind = kind;
    t->size = size;
    t->depth = depth;
    t->threads = threads;
}

void pf_preset(PF_PARAMS *p, int preset)
{
    int os = p->os;
    pf_u32 seed = p->seed;
    int show = p->show;
    int mib = p->unitMiB;

    memset(p, 0, sizeof(*p));
    p->os = os;
    p->seed = seed;
    p->show = show;
    p->unitMiB = mib;
    p->preset = preset;
    p->ops = PF_OP_BOTH;
    p->access = PF_ACC_UNBUF;
    p->flushInWrite = 1;
    p->bufMask = 1 << PF_BUF_ALIGNED;
    p->stopOnError = 1;
    p->sweepMin = 512;
    p->sweepMax = 8u * 1048576u;
    p->sweepDepth = 1;
    switch (preset) {
    case PF_PRESET_9X:
        p->fileMiB = 256;
        p->passes = 3;
        p->timeSec = 5;
        p->writeThrough = 1;
        p->pattern = PF_PAT_VERIFY;
        p->verify = 1;
        p->ntests = 4;
        pf_set_test(&p->tests[0], PF_SEQ, 512u * 1024u, 1, 1);
        pf_set_test(&p->tests[1], PF_SEQ, 1048576u, 1, 1);
        pf_set_test(&p->tests[2], PF_SEQ, 64u * 1024u, 1, 1);
        pf_set_test(&p->tests[3], PF_RND, 4096u, 1, 1);
        break;
    case PF_PRESET_STUDY:
        p->fileMiB = 256;
        p->passes = 1;
        p->writeThrough = 1;
        p->pattern = PF_PAT_VERIFY;
        p->verify = 1;
        p->bufMask = (1 << PF_BUF_ALIGNED) | (1 << PF_BUF_OFFSET) | (1 << PF_BUF_SCATTER);
        p->sweep = 1;
        p->sweepMin = 64u * 1024u;
        p->sweepMax = 1048576u;
        p->sweepTotalMiB = 0;
        break;
    case PF_PRESET_SWEEP:
        p->fileMiB = (os == PF_OS_9X) ? 256 : 1024;
        p->passes = 1;
        p->pattern = PF_PAT_RANDOM;
        p->sweep = 1;
        break;
    case PF_PRESET_QUICK:
        p->fileMiB = 64;
        p->passes = 1;
        p->timeSec = 2;
        p->pattern = PF_PAT_RANDOM;
        p->ntests = 2;
        pf_set_test(&p->tests[0], PF_SEQ, 1048576u, 1, 1);
        pf_set_test(&p->tests[1], PF_RND, 4096u, 1, 1);
        break;
    default:
        p->preset = PF_PRESET_STANDARD;
        p->fileMiB = 1024;
        p->passes = 3;
        p->timeSec = 5;
        p->pattern = PF_PAT_RANDOM;
        p->ntests = 4;
        pf_set_test(&p->tests[0], PF_SEQ, 1048576u, 8, 1);
        pf_set_test(&p->tests[1], PF_SEQ, 1048576u, 1, 1);
        pf_set_test(&p->tests[2], PF_RND, 4096u, 32, 1);
        pf_set_test(&p->tests[3], PF_RND, 4096u, 1, 1);
        break;
    }
}

void pf_defaults(PF_PARAMS *p, int os)
{
    memset(p, 0, sizeof(*p));
    p->os = os;
    p->seed = 0x1A2B3C4Du;
    p->show = PF_SHOW_BEST;
    pf_preset(p, os == PF_OS_9X ? PF_PRESET_9X : PF_PRESET_STANDARD);
}

/* ------------------------------------------------------------------------ */
/* Free space (section 4.3)                                                  */
/* ------------------------------------------------------------------------ */

pf_u64 pf_reserve(const PF_ENV *env)
{
    pf_u64 r;

    if (!env->systemVolume) {
        return (pf_u64)64 * 1048576u;
    }
    r = env->volumeBytes / 10;
    if (r < (pf_u64)512 * 1048576u) {
        r = (pf_u64)512 * 1048576u;
    }
    return r;
}

static void pf_mb_text(pf_u64 bytes, char *out, size_t cap)
{
    char n[24];

    pf_fmt_u64(bytes / 1000000u, n, sizeof(n));
    pf_cpy(out, cap, n);
    pf_cat(out, cap, " MB");
}

int pf_free_check(const PF_ENV *env, pf_u64 fileBytes, char *why, size_t whyCap)
{
    pf_u64 reserve = pf_reserve(env);
    char a[32];
    char b[32];
    char c[32];

    if (fileBytes <= env->freeUser && fileBytes + reserve <= env->freeTotal) {
        return 0;
    }
    pf_mb_text(env->freeTotal < env->freeUser ? env->freeTotal : env->freeUser, a, sizeof(a));
    pf_mb_text(fileBytes, b, sizeof(b));
    pf_mb_text(reserve, c, sizeof(c));
    pf_cpy(why, whyCap, "The drive has ");
    pf_cat(why, whyCap, a);
    pf_cat(why, whyCap, " free; this run needs ");
    pf_cat(why, whyCap, b);
    pf_cat(why, whyCap, " and must leave ");
    pf_cat(why, whyCap, c);
    pf_cat(why, whyCap, ". Lower the file size or choose another drive.");
    return 1;
}

/* ------------------------------------------------------------------------ */
/* The plan (section 3.8)                                                    */
/* ------------------------------------------------------------------------ */

static int pf_pow2(pf_u32 v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static int pf_refuse(char *why, size_t cap, const char *s)
{
    pf_cpy(why, cap, s);
    return PF_REFUSE;
}

static void pf_add_step(PF_PLAN *pl, int kind, pf_u32 size, int depth, int threads, int buffer, int op,
                        int sweep, pf_u64 budget, int timeSec, int testIndex)
{
    PF_STEP *s;

    if (pl->nsteps >= PF_MAX_STEPS) {
        return;
    }
    s = &pl->steps[pl->nsteps++];
    s->kind = kind;
    s->size = size;
    s->depth = depth;
    s->threads = threads;
    s->buffer = buffer;
    s->op = op;
    s->sweep = sweep;
    s->budget = budget;
    s->timeSec = timeSec;
    s->testIndex = testIndex;
    pf_test_label(kind, size, depth, threads, s->label, sizeof(s->label));
}

int pf_plan(const PF_PARAMS *p, const PF_ENV *env, PF_PLAN *pl, char *why, size_t whyCap)
{
    int b, i, op, k;
    int ops[2];
    int nops = 0;
    pf_u64 region;
    pf_u64 budget = 0;
    pf_u64 sec = 0;
    pf_u64 bytesPerPass = 0;
    pf_u32 sector = env->sector != 0 ? env->sector : 512;
    pf_u32 unitAlign;
    int writes = 0;

    memset(pl, 0, sizeof(*pl));
    if (why != NULL && whyCap != 0) {
        why[0] = 0;
    }
    if (p->fileMiB < 16) {
        return pf_refuse(why, whyCap, "The file must be at least 16 MB.");
    }
    if (p->passes < 1 || p->passes > PF_MAX_PASSES) {
        return pf_refuse(why, whyCap, "Choose 1 to 9 passes.");
    }
    if ((p->ops & PF_OP_BOTH) == 0) {
        return pf_refuse(why, whyCap, "Choose read, write or both.");
    }
    if ((p->bufMask & 7) == 0) {
        return pf_refuse(why, whyCap, "Choose a buffer layout.");
    }
    if (env->fat && p->fileMiB > 1024) {
        return pf_refuse(why, whyCap, "On a FAT drive the file is at most 1 GB; lower the file size.");
    }
    if (p->pattern != PF_PAT_VERIFY && p->verify) {
        return pf_refuse(why, whyCap, "Only the verifiable pattern can be verified; choose it or clear Verify.");
    }
    region = (pf_u64)p->fileMiB * 1048576u;
    pl->regionBytes = region;
    pl->fileBytes = region + PF_HEADER_BYTES;
    pl->verify = p->verify;
    if (p->ops & PF_OP_WRITE) {
        ops[nops++] = PF_OP_WRITE;
    }
    if (p->ops & PF_OP_READ) {
        ops[nops++] = PF_OP_READ;
    }
    unitAlign = (p->access == PF_ACC_UNBUF) ? sector : 1;

    if (p->sweep) {
        pf_u32 s;
        pf_u64 unit;
        int depth = p->sweepDepth < 1 ? 1 : p->sweepDepth;

        if (!pf_pow2(p->sweepMin) || !pf_pow2(p->sweepMax) || p->sweepMin < 512 ||
            p->sweepMax > 8u * 1048576u || p->sweepMin > p->sweepMax) {
            return pf_refuse(why, whyCap, "The sweep's sizes are powers of two from 512 bytes to 8 MB, smallest first.");
        }
        if (env->os == PF_OS_9X && depth > 1) {
            return pf_refuse(why, whyCap, "Queue depth 1: Windows 98 and ME file I/O waits for each request.");
        }
        if (depth > 4) {
            return pf_refuse(why, whyCap, "The sweep's depth is 1 to 4.");
        }
        if (p->sweepMin < unitAlign) {
            return pf_refuse(why, whyCap, "Unbuffered requests cannot be smaller than the drive's sector; raise the smallest size.");
        }
        if (!p->sweepTimed) {
            budget = (pf_u64)(p->sweepTotalMiB != 0 ? p->sweepTotalMiB : p->fileMiB) * 1048576u;
            unit = (pf_u64)p->sweepMax * (pf_u64)depth;
            budget -= budget % unit;
            if (budget == 0) {
                return pf_refuse(why, whyCap, "Raise the total per size to at least the largest size times the depth.");
            }
        }
        for (b = 0; b < PF_BUFS; b++) {
            if ((p->bufMask & (1 << b)) == 0) {
                continue;
            }
            for (s = p->sweepMin, k = 0; s <= p->sweepMax && s != 0; s <<= 1, k++) {
                for (i = 0; i < nops; i++) {
                    op = ops[i];
                    pf_add_step(pl, PF_SEQ, s, depth, 1, b, op, 1, budget, p->sweepTimed ? (p->timeSec > 0 ? p->timeSec : 5) : 0, k);
                }
            }
        }
    } else {
        if (p->ntests < 1) {
            return pf_refuse(why, whyCap, "Choose at least one test.");
        }
        if (p->timeSec < 1 || p->timeSec > 60) {
            return pf_refuse(why, whyCap, "Choose a measuring time of 1 to 60 seconds.");
        }
        for (i = 0; i < p->ntests; i++) {
            const PF_TEST *t = &p->tests[i];

            if (t->size < 512 || t->size > 8u * 1048576u || t->size % 512 != 0) {
                return pf_refuse(why, whyCap, "A request size is 512 bytes to 8 MB, a multiple of 512.");
            }
            if (t->size % unitAlign != 0) {
                return pf_refuse(why, whyCap, "Unbuffered requests must be a multiple of the drive's sector; change the size.");
            }
            if (t->depth < 1 || t->depth > PF_MAX_DEPTH || t->threads < 1 || t->threads > PF_MAX_THREADS) {
                return pf_refuse(why, whyCap, "Depth is 1 to 32 and threads 1 to 8.");
            }
            if (env->os == PF_OS_9X && t->depth > 1) {
                return pf_refuse(why, whyCap, "Queue depth 1: Windows 98 and ME file I/O waits for each request.");
            }
        }
        for (b = 0; b < PF_BUFS; b++) {
            if ((p->bufMask & (1 << b)) == 0) {
                continue;
            }
            for (k = 0; k < p->ntests; k++) {
                const PF_TEST *t = &p->tests[k];

                for (i = 0; i < nops; i++) {
                    pf_add_step(pl, t->kind, t->size, t->depth, t->threads, b, ops[i], 0, 0, p->timeSec, k);
                }
            }
        }
    }
    if (pl->nsteps >= PF_MAX_STEPS) {
        return pf_refuse(why, whyCap, "Too many tests in one run; choose fewer sizes or buffers.");
    }

    /* Sizes, buffers, the map */
    pl->minWrite = 0xFFFFFFFFu;
    for (i = 0; i < pl->nsteps; i++) {
        const PF_STEP *s = &pl->steps[i];
        pf_u32 slots = (pf_u32)(s->depth * s->threads);
        pf_u64 part;

        if (s->size > pl->maxSize) {
            pl->maxSize = s->size;
        }
        if (slots > pl->slots) {
            pl->slots = slots;
        }
        if (s->op == PF_OP_WRITE) {
            writes = 1;
            if (s->size < pl->minWrite) {
                pl->minWrite = s->size;
            }
        }
        part = region / (pf_u64)s->threads;
        part -= part % s->size;
        if (part < s->size) {
            return pf_refuse(why, whyCap, "The file is too small for this test: raise the file size or lower the threads.");
        }
        if (part / s->size < (pf_u64)s->depth) {
            pl->warnSmallPart = 1;
        }
        if ((pf_u64)s->size * (pf_u64)slots > region) {
            return pf_refuse(why, whyCap, "The file is smaller than the largest size times the depth and threads; raise the file size.");
        }
        if (s->buffer == PF_BUF_OFFSET && env->alignMask != 0 && (sector & env->alignMask) != 0) {
            return pf_refuse(why, whyCap, "This disk's adapter does not accept the offset buffer; choose another layout.");
        }
    }
    if (!writes) {
        pl->minWrite = 0;
    }
    {
        /* As the system half allocates them: one bank of slots + 1 buffers,
         * each the largest size rounded to a page plus a page; the scattered
         * layout's bank and the temporary bank it is made with; and the
         * verification buffer (at least the 1 MiB preparation chunk) */
        pf_u64 page = 4096u;
        pf_u64 stride = ((pf_u64)pl->maxSize + page - 1u) / page * page + page;
        pf_u64 bank = stride * ((pf_u64)pl->slots + 1u);
        pf_u64 vb = ((pl->maxSize > 1048576u) ? (pf_u64)pl->maxSize : (pf_u64)1048576u) + page;

        pl->bufferBytes = bank * (((p->bufMask & (1 << PF_BUF_SCATTER)) != 0) ? 3u : 1u) + vb;
    }
    if (pl->bufferBytes > (pf_u64)256 * 1048576u ||
        (env->memBytes != 0 && pl->bufferBytes > env->memBytes / 4)) {
        return pf_refuse(why, whyCap, "The buffers would be too large: lower the queue depth, the threads or the largest size.");
    }
    if (p->verify) {
        pf_u32 smallest = 0xFFFFFFFFu;

        for (i = 0; i < pl->nsteps; i++) {
            if (pl->steps[i].size < smallest) {
                smallest = pl->steps[i].size;
            }
        }
        pl->mapBlock = writes ? pl->minWrite : smallest;
        pl->mapBlocks = region / pl->mapBlock;
        if (pl->mapBlocks * 4 > PF_MAP_MAX_BYTES) {
            return pf_refuse(why, whyCap, "To verify, lower the file size or raise the smallest write size.");
        }
    }

    /* Time and write budget */
    for (i = 0; i < pl->nsteps; i++) {
        const PF_STEP *s = &pl->steps[i];
        pf_u64 times = (pf_u64)p->passes + (p->warmup ? 1 : 0);

        if (s->budget != 0) {
            bytesPerPass += s->budget * times;
            if (s->op == PF_OP_WRITE) {
                pl->budgetWrites += (s->budget / s->size) * times;
            }
            sec += (s->budget * times) / (20u * 1048576u) + 1;
        } else {
            sec += (pf_u64)s->timeSec * times;
        }
        /* the pause comes before every pass, a warm-up's included */
        sec += (pf_u64)p->pauseSec * times;
    }
    if (pl->budgetWrites > PF_GEN_LAST) {
        return pf_refuse(why, whyCap, "This run would write too many requests to verify; lower the total or the passes.");
    }
    sec += pl->fileBytes / (20u * 1048576u);
    if (p->verify) {
        sec += bytesPerPass / (40u * 1048576u);
    }
    if (sec > PF_MAX_RUN_SEC) {
        return pf_refuse(why, whyCap, "This run would take more than 2 hours; lower the passes, the time or the file size.");
    }
    pl->estimateSec = (pf_u32)sec;
    if (p->access == PF_ACC_CACHED && env->memBytes != 0 && region <= env->memBytes) {
        pl->warnCache = 1;
    }
    return pf_free_check(env, pl->fileBytes, why, whyCap) ? PF_REFUSE : PF_OK;
}

/* ------------------------------------------------------------------------ */
/* The generator and the pattern                                             */
/* ------------------------------------------------------------------------ */

#define PF_GOLDEN   0x9E3779B9u

pf_u32 pf_xs_seed(pf_u32 seed)
{
    return seed != 0 ? seed : PF_GOLDEN;
}

pf_u32 pf_xs_next(pf_u32 *state)
{
    pf_u32 x = *state;

    if (x == 0) {
        x = PF_GOLDEN;
    }
    x ^= (x << 13) & 0xFFFFFFFFu;
    x ^= x >> 17;
    x ^= (x << 5) & 0xFFFFFFFFu;
    x &= 0xFFFFFFFFu;
    *state = x;
    return x;
}

pf_u32 pf_xs_mix(pf_u32 a, pf_u32 b, pf_u32 c)
{
    pf_u32 s = a ^ ((b * PF_GOLDEN) & 0xFFFFFFFFu) ^ (((c + 1u) * 0x85EBCA6Bu) & 0xFFFFFFFFu);

    return pf_xs_seed(s & 0xFFFFFFFFu);
}

pf_u32 pf_thread_seed(pf_u32 seed, int thread)
{
    pf_u32 s = pf_xs_seed(seed) ^ ((((pf_u32)thread + 1u) * PF_GOLDEN) & 0xFFFFFFFFu);

    return pf_xs_seed(s & 0xFFFFFFFFu);
}

static void pf_put32(unsigned char *p, pf_u32 v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static pf_u32 pf_get32(const unsigned char *p)
{
    return (pf_u32)p[0] | ((pf_u32)p[1] << 8) | ((pf_u32)p[2] << 16) | ((pf_u32)p[3] << 24);
}

static void pf_unit_body(unsigned char *u, pf_u32 runId, pf_u32 unitLo, pf_u32 gen)
{
    pf_u32 st = pf_xs_mix(runId, unitLo, gen);
    int i;

    for (i = 16; i < (int)PF_UNIT; i += 4) {
        pf_put32(u + i, pf_xs_next(&st));
    }
}

void pf_stamp(unsigned char *buf, pf_u32 bytes, pf_u32 runId, pf_u64 unit, pf_u32 gen)
{
    pf_u32 i;

    for (i = 0; i + PF_UNIT <= bytes; i += PF_UNIT) {
        unsigned char *u = buf + i;
        pf_u32 idx = (pf_u32)((unit + i / PF_UNIT) & 0xFFFFFFFFu);

        u[0] = 'X';
        u[1] = 'P';
        u[2] = 'R';
        u[3] = 'F';
        pf_put32(u + 4, runId);
        pf_put32(u + 8, idx);
        pf_put32(u + 12, gen);
        pf_unit_body(u, runId, idx, gen);
    }
}

void pf_fill_random(unsigned char *buf, pf_u32 bytes, pf_u32 seed)
{
    pf_u32 st = pf_xs_seed(seed);
    pf_u32 i;

    for (i = 0; i + 4 <= bytes; i += 4) {
        pf_put32(buf + i, pf_xs_next(&st));
    }
}

pf_u32 pf_check(const unsigned char *buf, pf_u32 bytes, pf_u32 runId, pf_u64 unit, pf_u32 gen,
                pf_u32 *firstBad, pf_u32 *foundGen)
{
    unsigned char want[PF_UNIT];
    pf_u32 i;
    pf_u32 bad = 0;

    *firstBad = 0;
    *foundGen = 0;
    for (i = 0; i + PF_UNIT <= bytes; i += PF_UNIT) {
        const unsigned char *u = buf + i;
        pf_u32 idx = (pf_u32)((unit + i / PF_UNIT) & 0xFFFFFFFFu);

        pf_stamp(want, PF_UNIT, runId, (pf_u64)idx, gen);
        if (memcmp(u, want, PF_UNIT) != 0) {
            if (bad == 0) {
                *firstBad = i / PF_UNIT;
                if (u[0] == 'X' && u[1] == 'P' && u[2] == 'R' && u[3] == 'F' && pf_get32(u + 4) == runId &&
                    pf_get32(u + 8) == idx) {
                    *foundGen = pf_get32(u + 12);
                } else {
                    *foundGen = PF_GEN_UNKNOWN;
                }
            }
            bad++;
        }
    }
    return bad;
}

void pf_gen_init(PF_GEN *g)
{
    g->last = 0;
}

pf_u32 pf_gen_take(PF_GEN *g)
{
    if (g->last >= PF_GEN_LAST) {
        g->last = PF_GEN_LAST;
        return 0;
    }
    g->last++;
    return g->last;
}

/* ------------------------------------------------------------------------ */
/* Statistics                                                                */
/* ------------------------------------------------------------------------ */

pf_u64 pf_ticks_us(pf_u64 ticks, pf_u64 freq)
{
    if (freq == 0) {
        return 0;
    }
    return (ticks / freq) * 1000000u + ((ticks % freq) * 1000000u) / freq;
}

pf_u64 pf_rate10(pf_u64 bytes, pf_u64 us, int mib)
{
    pf_u64 whole, rem, v;

    if (us == 0) {
        return 0;
    }
    if (!mib) {
        /* bytes per us = MB/s; tenths, rounded to the nearest */
        return (bytes * 10u + us / 2) / us;
    }
    /* MiB/s x 10 = bytes x 10 / us x 15625 / 16384, by parts so no product
     * overflows: whole bytes per us, then the remainder */
    whole = bytes / us;
    rem = bytes % us;
    v = whole * 156250u + (rem * 156250u) / us;
    return (v + 8192u) / 16384u;
}

pf_u64 pf_iops(pf_u64 reqs, pf_u64 us)
{
    if (us == 0) {
        return 0;
    }
    return (reqs * 1000000u + us / 2) / us;
}

void pf_lat_init(PF_LAT *l)
{
    memset(l, 0, sizeof(*l));
}

static pf_u64 pf_edges[PF_LAT_BUCKETS];
static int pf_edges_ready;

static void pf_edges_init(void)
{
    int i;
    pf_u64 e = 1;

    for (i = 0; i < PF_LAT_BUCKETS; i++) {
        pf_edges[i] = e;
        e = (e * 4u + 2u) / 3u;
        if (e <= pf_edges[i]) {
            e = pf_edges[i] + 1;
        }
    }
    pf_edges_ready = 1;
}

pf_u64 pf_lat_edge(int bucket)
{
    if (!pf_edges_ready) {
        pf_edges_init();
    }
    if (bucket < 0) {
        bucket = 0;
    }
    if (bucket >= PF_LAT_BUCKETS) {
        bucket = PF_LAT_BUCKETS - 1;
    }
    return pf_edges[bucket];
}

void pf_lat_add(PF_LAT *l, pf_u64 us)
{
    int i;

    if (!pf_edges_ready) {
        pf_edges_init();
    }
    if (l->count == 0 || us < l->minUs) {
        l->minUs = us;
    }
    if (us > l->maxUs) {
        l->maxUs = us;
    }
    l->count++;
    l->sumUs += us;
    for (i = 0; i < PF_LAT_BUCKETS - 1; i++) {
        if (us <= pf_edges[i]) {
            break;
        }
    }
    l->hist[i]++;
}

void pf_lat_merge(PF_LAT *into, const PF_LAT *from)
{
    int i;

    if (from->count == 0) {
        return;
    }
    if (into->count == 0 || from->minUs < into->minUs) {
        into->minUs = from->minUs;
    }
    if (from->maxUs > into->maxUs) {
        into->maxUs = from->maxUs;
    }
    into->count += from->count;
    into->sumUs += from->sumUs;
    for (i = 0; i < PF_LAT_BUCKETS; i++) {
        into->hist[i] += from->hist[i];
    }
}

pf_u64 pf_lat_pct(const PF_LAT *l, int pct)
{
    pf_u64 need, acc = 0;
    int i;

    if (l->count == 0) {
        return 0;
    }
    need = (l->count * (pf_u64)pct + 99u) / 100u;
    if (need == 0) {
        need = 1;
    }
    for (i = 0; i < PF_LAT_BUCKETS; i++) {
        acc += l->hist[i];
        if (acc >= need) {
            pf_u64 e = pf_lat_edge(i);

            return e > l->maxUs ? l->maxUs : e;
        }
    }
    return l->maxUs;
}

pf_u64 pf_lat_mean(const PF_LAT *l)
{
    if (l->count == 0) {
        return 0;
    }
    return (l->sumUs + l->count / 2) / l->count;
}

int pf_timer_plausible(pf_u64 qpcUs, pf_u64 tickMs)
{
    pf_u64 tUs = tickMs * 1000u;
    pf_u64 hi = tUs + tUs / 2 + 100000u;
    pf_u64 lo = (tUs > 100000u) ? (tUs - 100000u) * 2 / 3 : 0;

    return qpcUs <= hi && qpcUs >= lo;
}

int pf_cpu_limited(pf_u64 stampUs, pf_u64 us)
{
    return us != 0 && stampUs * 10u > us;
}

void pf_shown(const PF_RESULT *pa, int n, int show, int mib, int highRes, PF_SHOWN *out)
{
    pf_u64 rates[PF_MAX_PASSES];
    int okIdx[PF_MAX_PASSES];
    int nok = 0;
    int i, j, best = -1;
    PF_LAT merged;

    memset(out, 0, sizeof(*out));
    out->status = PF_ST_NONE;
    for (i = 0; i < n && i < PF_MAX_PASSES; i++) {
        if (pa[i].status == PF_ST_OK) {
            rates[nok] = pf_rate10(pa[i].bytes, pa[i].us, mib);
            okIdx[nok] = i;
            nok++;
        } else if (pa[i].status == PF_ST_FAILED || pa[i].status == PF_ST_DATAERR) {
            out->status = pa[i].status;
        } else if (pa[i].status != PF_ST_NONE && out->status == PF_ST_NONE) {
            out->status = pa[i].status;
        }
    }
    if (nok == 0) {
        return;
    }
    /* a pass that failed, found a data error, was stopped or was not
     * accepted stays the test's status beside the other passes' rate */
    if (out->status == PF_ST_NONE) {
        out->status = PF_ST_OK;
    }
    for (i = 0; i < nok; i++) {
        if (best < 0 || rates[i] > rates[best]) {
            best = i;
        }
        if (pa[okIdx[i]].timerBad) {
            out->timerBad = 1;
        }
        if (pa[okIdx[i]].cpuLimited) {
            out->cpuLimited = 1;
        }
    }
    pf_lat_init(&merged);
    if (show == PF_SHOW_BEST) {
        const PF_RESULT *r = &pa[okIdx[best]];

        out->rate10 = rates[best];
        out->iops = pf_iops(r->reqs, r->us);
        merged = r->lat;
    } else {
        pf_u64 sum = 0;
        pf_u64 io = 0;

        for (i = 0; i < nok; i++) {
            sum += rates[i];
            io += pf_iops(pa[okIdx[i]].reqs, pa[okIdx[i]].us);
            pf_lat_merge(&merged, &pa[okIdx[i]].lat);
        }
        if (show == PF_SHOW_MEAN) {
            out->rate10 = (sum + (pf_u64)nok / 2) / (pf_u64)nok;
            out->iops = (io + (pf_u64)nok / 2) / (pf_u64)nok;
        } else {
            pf_u64 s[PF_MAX_PASSES];
            pf_u64 t2;

            for (i = 0; i < nok; i++) {
                s[i] = rates[i];
            }
            for (i = 1; i < nok; i++) {
                for (j = i; j > 0 && s[j - 1] > s[j]; j--) {
                    t2 = s[j];
                    s[j] = s[j - 1];
                    s[j - 1] = t2;
                }
            }
            out->rate10 = (nok % 2) ? s[nok / 2] : (s[nok / 2 - 1] + s[nok / 2] + 1) / 2;
            for (i = 0; i < nok; i++) {
                s[i] = pf_iops(pa[okIdx[i]].reqs, pa[okIdx[i]].us);
            }
            for (i = 1; i < nok; i++) {
                for (j = i; j > 0 && s[j - 1] > s[j]; j--) {
                    t2 = s[j];
                    s[j] = s[j - 1];
                    s[j - 1] = t2;
                }
            }
            out->iops = (nok % 2) ? s[nok / 2] : (s[nok / 2 - 1] + s[nok / 2] + 1) / 2;
            (void)io;
        }
    }
    if (highRes && merged.count != 0) {
        out->haveLat = 1;
        out->latMean = pf_lat_mean(&merged);
        out->latP50 = pf_lat_pct(&merged, 50);
        out->latP99 = pf_lat_pct(&merged, 99);
        out->latMin = merged.minUs;
        out->latMax = merged.maxUs;
    }
}

/* ------------------------------------------------------------------------ */
/* The header and leftovers                                                  */
/* ------------------------------------------------------------------------ */

static const char pf_sig[] = "XHCIPERF-TESTFILE";

static void pf_put64(unsigned char *p, pf_u64 v)
{
    pf_put32(p, (pf_u32)(v & 0xFFFFFFFFu));
    pf_put32(p + 4, (pf_u32)(v >> 32));
}

static pf_u64 pf_get64(const unsigned char *p)
{
    return (pf_u64)pf_get32(p) | ((pf_u64)pf_get32(p + 4) << 32);
}

void pf_header_build(const PF_HEADER *h, unsigned char *out)
{
    memset(out, 0, PF_UNIT);
    memcpy(out, pf_sig, sizeof(pf_sig) - 1);
    pf_put32(out + 20, 1);
    pf_put32(out + 24, h->runLo);
    pf_put32(out + 28, h->runHi);
    pf_put64(out + 32, h->plannedBytes);
    pf_put32(out + 40, h->volSerial);
    pf_put32(out + 44, h->createdLo);
    pf_put32(out + 48, h->createdHi);
}

int pf_header_parse(const unsigned char *in, pf_u32 len, PF_HEADER *h)
{
    int i;

    memset(h, 0, sizeof(*h));
    if (len < PF_UNIT || memcmp(in, pf_sig, sizeof(pf_sig) - 1) != 0) {
        return 0;
    }
    h->version = pf_get32(in + 20);
    if (h->version != 1) {
        return 0;
    }
    for (i = 52; i < (int)PF_UNIT; i++) {
        if (in[i] != 0) {
            return 0;
        }
    }
    h->runLo = pf_get32(in + 24);
    h->runHi = pf_get32(in + 28);
    h->plannedBytes = pf_get64(in + 32);
    h->volSerial = pf_get32(in + 40);
    h->createdLo = pf_get32(in + 44);
    h->createdHi = pf_get32(in + 48);
    return 1;
}

int pf_leftover(const PF_HEADER *h, int headerOk, const PF_RECORD *rec, pf_u64 size,
                pf_u32 indexLo, pf_u32 indexHi, int haveIndex)
{
    if (!headerOk) {
        return PF_LEFT_FOREIGN;
    }
    if (size < PF_HEADER_BYTES || size > h->plannedBytes) {
        return PF_LEFT_FOREIGN;
    }
    if (rec == NULL || !rec->present || rec->runLo != h->runLo || rec->runHi != h->runHi ||
        rec->plannedBytes != h->plannedBytes || rec->volSerial != h->volSerial) {
        return PF_LEFT_UNRECORDED;
    }
    if (haveIndex && (rec->indexLo != 0 || rec->indexHi != 0) &&
        (rec->indexLo != indexLo || rec->indexHi != indexHi)) {
        return PF_LEFT_UNRECORDED;
    }
    return PF_LEFT_OURS;
}

int pf_path_under(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    size_t i;

    while (n > 0 && (dir[n - 1] == '\\' || dir[n - 1] == '/')) {
        n--;
    }
    if (n == 0 || strlen(path) < n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (pf_up(path[i]) != pf_up(dir[i])) {
            return 0;
        }
    }
    return path[n] == 0 || path[n] == '\\' || path[n] == '/';
}

/* ------------------------------------------------------------------------ */
/* The workload key                                                          */
/* ------------------------------------------------------------------------ */

static void pf_key_put(char *out, size_t cap, const char *k, pf_u64 v)
{
    char n[24];

    if (out[0] != 0) {
        pf_cat(out, cap, ";");
    }
    pf_cat(out, cap, k);
    pf_cat(out, cap, "=");
    pf_fmt_u64(v, n, sizeof(n));
    pf_cat(out, cap, n);
}

/* A byte-budgeted sweep's bytes per step as pf_plan makes them: the total
 * rounded down to the largest size times the depth; 0 when timed */
static pf_u64 pf_sweep_budget(const PF_PARAMS *p)
{
    pf_u64 budget, unit;
    int depth = p->sweepDepth < 1 ? 1 : p->sweepDepth;

    if (!p->sweep || p->sweepTimed || p->sweepMax == 0) {
        return 0;
    }
    budget = (pf_u64)(p->sweepTotalMiB != 0 ? p->sweepTotalMiB : p->fileMiB) * 1048576u;
    unit = (pf_u64)p->sweepMax * (pf_u64)depth;
    return budget - budget % unit;
}

void pf_key(const PF_PARAMS *p, int highRes, char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = 0;
    pf_key_put(out, cap, "file", p->fileMiB);
    pf_key_put(out, cap, "access", (pf_u64)p->access);
    pf_key_put(out, cap, "wt", (pf_u64)p->writeThrough);
    pf_key_put(out, cap, "flush", (pf_u64)p->flushInWrite);
    pf_key_put(out, cap, "pattern", (pf_u64)p->pattern);
    pf_key_put(out, cap, "verify", (pf_u64)(p->verify ? (p->verifyDuring ? 2 : 1) : 0));
    pf_key_put(out, cap, "time", (pf_u64)(p->sweep ? (p->sweepTimed ? p->timeSec : 0) : p->timeSec));
    pf_key_put(out, cap, "total", (pf_u64)(p->sweep && !p->sweepTimed ? (p->sweepTotalMiB ? p->sweepTotalMiB : p->fileMiB) : 0));
    pf_key_put(out, cap, "budget", pf_sweep_budget(p));
    pf_key_put(out, cap, "passes", (pf_u64)p->passes);
    pf_key_put(out, cap, "warmup", (pf_u64)p->warmup);
    pf_key_put(out, cap, "pause", (pf_u64)p->pauseSec);
    pf_key_put(out, cap, "seed", p->seed);
    pf_key_put(out, cap, "show", (pf_u64)p->show);
    pf_key_put(out, cap, "timer", (pf_u64)(highRes ? 1 : 0));
}

static const char *pf_key_field(const char *key, const char *name, size_t *vlen)
{
    size_t n = strlen(name);
    const char *s = key;

    while (*s != 0) {
        const char *e = strchr(s, ';');
        size_t flen = e ? (size_t)(e - s) : strlen(s);

        if (flen > n && memcmp(s, name, n) == 0 && s[n] == '=') {
            *vlen = flen - n - 1;
            return s + n + 1;
        }
        if (!e) {
            break;
        }
        s = e + 1;
    }
    *vlen = 0;
    return NULL;
}

int pf_key_same(const char *a, const char *b, char *diff, size_t cap)
{
    static const char *const names[] = {
        "file", "access", "wt", "flush", "pattern", "verify", "time", "total", "budget", "passes",
        "warmup", "pause", "seed", "show", "timer"
    };
    int i, same = 1;

    if (cap != 0) {
        diff[0] = 0;
    }
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        size_t la, lb;
        const char *va = pf_key_field(a, names[i], &la);
        const char *vb = pf_key_field(b, names[i], &lb);

        if (va == NULL || vb == NULL || la != lb || memcmp(va, vb, la) != 0) {
            same = 0;
            if (cap != 0) {
                if (diff[0] != 0) {
                    pf_cat(diff, cap, ", ");
                }
                pf_cat(diff, cap, names[i]);
            }
        }
    }
    return same;
}

const PF_SAVED_ROW *pf_saved_find(const PF_SAVED *sv, const PF_PLAN *plan, int step)
{
    const PF_STEP *s = &plan->steps[step];
    int i, occ = 0;

    for (i = 0; i < step; i++) {
        const PF_STEP *o = &plan->steps[i];

        if (strcmp(o->label, s->label) == 0 && o->op == s->op && o->buffer == s->buffer) {
            occ++;
        }
    }
    for (i = 0; i < sv->nrows; i++) {
        const PF_SAVED_ROW *r = &sv->rows[i];

        if (r->occ == occ && strcmp(r->label, s->label) == 0 && r->op == s->op && r->buffer == s->buffer) {
            return r->npass != 0 ? r : NULL;
        }
    }
    return NULL;
}

pf_u64 pf_saved_value(const PF_SAVED_ROW *row, int show, int mib)
{
    PF_RESULT r[PF_MAX_PASSES];
    PF_SHOWN sh;
    int i;

    memset(r, 0, sizeof(r));
    for (i = 0; i < row->npass && i < PF_MAX_PASSES; i++) {
        r[i].status = PF_ST_OK;
        r[i].bytes = row->bytes[i];
        r[i].us = row->us[i];
        r[i].reqs = row->reqs[i];
    }
    pf_shown(r, row->npass, show, mib, 0, &sh);
    return sh.rate10;
}

long pf_change10(pf_u64 cur, pf_u64 old)
{
    pf_u64 d;

    if (old == 0) {
        return 0;
    }
    if (cur >= old) {
        d = ((cur - old) * 1000u + old / 2) / old;
        return d > 2000000000u ? 2000000000L : (long)d;
    }
    d = ((old - cur) * 1000u + old / 2) / old;
    return -(long)d;
}

/* ------------------------------------------------------------------------ */
/* The report                                                                */
/* ------------------------------------------------------------------------ */

static const char *pf_op_name(int op)
{
    return op == PF_OP_WRITE ? "write" : "read";
}

static const char *pf_status_text(int st)
{
    switch (st) {
    case PF_ST_STOPPED:
        return "stopped";
    case PF_ST_FAILED:
        return "failed";
    case PF_ST_DATAERR:
        return "data error";
    case PF_ST_SKIPPED:
        return "not accepted";
    default:
        return "-";
    }
}

void pf_summary_line(PF_TEXT *t, const PF_STEP *s, const PF_SHOWN *sh, int mib)
{
    char a[40];
    char b[24];

    pf_tx_pad(t, s->label, 15, 0);
    pf_tx_pad(t, pf_op_name(s->op), 6, 0);
    pf_tx_pad(t, pf_buf_name(s->buffer), 10, 0);
    if (sh->status == PF_ST_OK || (sh->rate10 != 0)) {
        pf_fmt_tenths(sh->rate10, a, sizeof(a));
        pf_cat(a, sizeof(a), mib ? " MiB/s" : " MB/s");
        pf_tx_pad(t, a, 15, 1);
        pf_fmt_u64(sh->iops, b, sizeof(b));
        pf_cat(b, sizeof(b), " IOPS");
        pf_tx_pad(t, b, 13, 1);
        if (sh->haveLat) {
            pf_fmt_us(sh->latMean, a, sizeof(a));
            pf_tx_pad(t, a, 11, 1);
        } else {
            pf_tx_pad(t, "-", 11, 1);
        }
        if (sh->status != PF_ST_OK) {
            pf_tx_s(t, " ");
            pf_tx_s(t, pf_status_text(sh->status));
        }
        if (sh->cpuLimited) {
            pf_tx_s(t, " cpu");
        }
        if (sh->timerBad) {
            pf_tx_s(t, " timer");
        }
    } else {
        pf_tx_pad(t, pf_status_text(sh->status), 15, 1);
    }
    pf_tx_nl(t);
}

static const char *pf_access_text(const PF_PARAMS *p)
{
    if (p->access == PF_ACC_CACHED) {
        return p->writeThrough ? "cached, write-through" : "cached";
    }
    return p->writeThrough ? "unbuffered, write-through" : "unbuffered";
}

static const char *pf_pattern_text(int pat)
{
    switch (pat) {
    case PF_PAT_VERIFY:
        return "verifiable";
    case PF_PAT_ZEROS:
        return "zeros";
    default:
        return "random, fixed";
    }
}

static const char *pf_show_text(int show)
{
    switch (show) {
    case PF_SHOW_MEAN:
        return "mean";
    case PF_SHOW_MEDIAN:
        return "median";
    default:
        return "best";
    }
}

void pf_report(PF_TEXT *t, const PF_PARAMS *p, const PF_PLAN *plan, const PF_RESULT *res,
               int npasses, const PF_RUNINFO *ri)
{
    int i, k;
    char key[512];
    PF_SHOWN sh;
    int highRes = ri->timerFreq != 0;

    pf_tx_s(t, "xHCI98 Disk Benchmark ");
    pf_tx_s(t, ri->toolVersion);
    pf_tx_nl(t);
    pf_tx_s(t, "Date:        ");
    pf_tx_s(t, ri->date);
    pf_tx_nl(t);
    pf_tx_s(t, "System:      ");
    pf_tx_s(t, ri->system);
    pf_tx_nl(t);
    pf_tx_s(t, "Drive:       ");
    pf_tx_s(t, ri->drive);
    pf_tx_s(t, " (");
    pf_tx_s(t, ri->fileSystem);
    pf_tx_s(t, ", sector ");
    pf_tx_u(t, ri->sector);
    if (ri->physSector != 0) {
        pf_tx_s(t, ", physical sector ");
        pf_tx_u(t, ri->physSector);
    }
    pf_tx_s(t, ")");
    pf_tx_nl(t);
    if (ri->model[0] != 0) {
        pf_tx_s(t, "Disk:        ");
        pf_tx_s(t, ri->model);
        pf_tx_nl(t);
    }
    pf_tx_s(t, "Preset:      ");
    pf_tx_s(t, pf_preset_name(p->preset));
    pf_tx_nl(t);
    pf_tx_s(t, "File:        ");
    pf_tx_u(t, p->fileMiB);
    pf_tx_s(t, " MiB, ");
    pf_tx_s(t, pf_access_text(p));
    pf_tx_s(t, ", pattern ");
    pf_tx_s(t, pf_pattern_text(p->pattern));
    if (p->verify) {
        pf_tx_s(t, p->verifyDuring ? ", checked during the read" : ", verified");
    }
    pf_tx_nl(t);
    if (ri->endVerify != PF_EV_NONE) {
        static const char *const ev[] = {
            "", "clean", "DATA ERRORS FOUND", "the file could not be read", "stopped before its end: not known to be clean"
        };

        pf_tx_s(t, "Whole file:  ");
        pf_tx_s(t, (ri->endVerify > 0 && ri->endVerify <= PF_EV_STOPPED) ? ev[ri->endVerify] : "?");
        pf_tx_nl(t);
    }
    pf_tx_s(t, "Passes:      ");
    pf_tx_u(t, (pf_u64)p->passes);
    if (!p->sweep || p->sweepTimed) {
        pf_tx_s(t, " of ");
        pf_tx_u(t, (pf_u64)p->timeSec);
        pf_tx_s(t, " s");
    }
    pf_tx_s(t, ", shown: ");
    pf_tx_s(t, pf_show_text(p->show));
    pf_tx_s(t, ", seed ");
    pf_tx_u(t, p->seed);
    pf_tx_nl(t);
    pf_tx_s(t, "Timer:       ");
    if (highRes) {
        pf_tx_s(t, "performance counter, ");
        pf_tx_u(t, ri->timerFreq);
        pf_tx_s(t, " Hz");
    } else {
        pf_tx_s(t, "milliseconds");
    }
    pf_tx_nl(t);
    pf_tx_s(t, "Units:       ");
    pf_tx_s(t, p->unitMiB ? "MiB/s (1,048,576 bytes)" : "MB/s (1,000,000 bytes)");
    pf_tx_s(t, "; latency is the mean");
    pf_tx_nl(t);
    pf_tx_nl(t);
    for (i = 0; i < plan->nsteps; i++) {
        pf_shown(&res[i * PF_MAX_PASSES], npasses, p->show, p->unitMiB, highRes, &sh);
        pf_summary_line(t, &plan->steps[i], &sh, p->unitMiB);
    }
    if (ri->stor98[0] != 0) {
        pf_tx_nl(t);
        pf_tx_s(t, ri->stor98);
        pf_tx_nl(t);
    }
    pf_tx_nl(t);
    pf_tx_s(t, "Every pass:");
    pf_tx_nl(t);
    for (i = 0; i < plan->nsteps; i++) {
        for (k = 0; k < npasses; k++) {
            const PF_RESULT *r = &res[i * PF_MAX_PASSES + k];
            char a[32];

            if (r->status == PF_ST_NONE) {
                continue;
            }
            pf_tx_s(t, "  ");
            pf_tx_pad(t, plan->steps[i].label, 15, 0);
            pf_tx_pad(t, pf_op_name(plan->steps[i].op), 6, 0);
            pf_tx_pad(t, pf_buf_name(plan->steps[i].buffer), 10, 0);
            pf_tx_s(t, "pass ");
            pf_tx_u(t, (pf_u64)(k + 1));
            pf_tx_s(t, ": ");
            pf_fmt_tenths(pf_rate10(r->bytes, r->us, p->unitMiB), a, sizeof(a));
            pf_tx_s(t, a);
            pf_tx_s(t, r->status == PF_ST_OK ? "" : " ");
            pf_tx_s(t, r->status == PF_ST_OK ? "" : pf_status_text(r->status));
            pf_tx_nl(t);
        }
    }

    /* The machine lines (section 5.6), canonical units */
    pf_key(p, highRes, key, sizeof(key));
    pf_tx_nl(t);
    pf_tx_s(t, "XPRF-RUN 1 ");
    pf_tx_s(t, key);
    pf_tx_nl(t);
    if (ri->endVerify != PF_EV_NONE) {
        pf_tx_s(t, "XPRF-VERIFY 1 ");
        pf_tx_u(t, (pf_u64)ri->endVerify);
        pf_tx_nl(t);
    }
    for (i = 0; i < plan->nsteps; i++) {
        for (k = 0; k < npasses; k++) {
            const PF_RESULT *r = &res[i * PF_MAX_PASSES + k];

            if (r->status == PF_ST_NONE) {
                continue;
            }
            pf_tx_s(t, "XPRF-RESULT 1 ");
            pf_tx_s(t, plan->steps[i].label);
            pf_tx_s(t, "|");
            pf_tx_s(t, pf_op_name(plan->steps[i].op));
            pf_tx_s(t, "|");
            pf_tx_s(t, pf_buf_name(plan->steps[i].buffer));
            pf_tx_s(t, "|");
            pf_tx_u(t, (pf_u64)(k + 1));
            pf_tx_s(t, "|");
            pf_tx_u(t, (pf_u64)r->status);
            pf_tx_s(t, "|");
            pf_tx_u(t, r->bytes);
            pf_tx_s(t, "|");
            pf_tx_u(t, r->reqs);
            pf_tx_s(t, "|");
            pf_tx_u(t, r->us * 1000u);
            pf_tx_s(t, "|");
            pf_tx_u(t, pf_lat_mean(&r->lat) * 1000u);
            pf_tx_nl(t);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Saved results                                                             */
/* ------------------------------------------------------------------------ */

static int pf_parse_u64(const char *s, size_t n, pf_u64 *v)
{
    size_t i;
    pf_u64 x = 0;

    if (n == 0 || n > 19) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        x = x * 10u + (pf_u64)(s[i] - '0');
    }
    *v = x;
    return 1;
}

static int pf_buf_by_name(const char *s, size_t n)
{
    int b;

    for (b = 0; b < PF_BUFS; b++) {
        const char *nm = pf_buf_name(b);

        if (strlen(nm) == n && memcmp(nm, s, n) == 0) {
            return b;
        }
    }
    return -1;
}

long pf_saved_parse(const char *text, size_t len, PF_SAVED *sv)
{
    size_t pos = 0;
    long line = 0;
    int haveRun = 0;

    memset(sv, 0, sizeof(*sv));
    while (pos < len) {
        size_t e = pos;
        size_t n;
        const char *s;

        while (e < len && text[e] != '\n') {
            e++;
        }
        line++;
        s = text + pos;
        n = e - pos;
        if (n > 0 && s[n - 1] == '\r') {
            n--;
        }
        if (n >= 11 && memcmp(s, "XPRF-RUN 1 ", 11) == 0) {
            if (n - 11 >= sizeof(sv->key)) {
                return line;
            }
            memcpy(sv->key, s + 11, n - 11);
            sv->key[n - 11] = 0;
            haveRun = 1;
        } else if (n >= 14 && memcmp(s, "XPRF-RESULT 1 ", 14) == 0) {
            const char *f[10];
            size_t fl[10];
            int nf = 0;
            size_t i = 14;
            size_t st = 14;
            pf_u64 pass, status, bytes, reqs, ns, lat;
            PF_SAVED_ROW *row = NULL;
            int r;

            for (; i <= n && nf < 10; i++) {
                if (i == n || s[i] == '|') {
                    f[nf] = s + st;
                    fl[nf] = i - st;
                    nf++;
                    st = i + 1;
                }
            }
            if (nf != 9 || fl[0] == 0 || fl[0] >= sizeof(sv->rows[0].label) ||
                !pf_parse_u64(f[3], fl[3], &pass) || !pf_parse_u64(f[4], fl[4], &status) ||
                !pf_parse_u64(f[5], fl[5], &bytes) || !pf_parse_u64(f[6], fl[6], &reqs) ||
                !pf_parse_u64(f[7], fl[7], &ns) || !pf_parse_u64(f[8], fl[8], &lat) ||
                pf_buf_by_name(f[2], fl[2]) < 0 ||
                !((fl[1] == 4 && memcmp(f[1], "read", 4) == 0) || (fl[1] == 5 && memcmp(f[1], "write", 5) == 0))) {
                return line;
            }
            /* The latest row of this label, operation and buffer; a pass
             * number that does not rise begins the next test of them (the
             * lines are written test by test, passes rising) */
            {
                int occ = 0;

                for (r = 0; r < sv->nrows; r++) {
                    PF_SAVED_ROW *x = &sv->rows[r];

                    if (strlen(x->label) == fl[0] && memcmp(x->label, f[0], fl[0]) == 0 &&
                        x->op == (fl[1] == 5 ? PF_OP_WRITE : PF_OP_READ) && x->buffer == pf_buf_by_name(f[2], fl[2])) {
                        row = x;
                        occ = x->occ + 1;
                    }
                }
                if (row != NULL && pass <= row->lastPass) {
                    row = NULL;
                }
                if (row == NULL) {
                    if (sv->nrows >= PF_SAVED_MAX) {
                        return line;
                    }
                    row = &sv->rows[sv->nrows++];
                    memcpy(row->label, f[0], fl[0]);
                    row->label[fl[0]] = 0;
                    row->op = (fl[1] == 5) ? PF_OP_WRITE : PF_OP_READ;
                    row->buffer = pf_buf_by_name(f[2], fl[2]);
                    row->occ = occ;
                }
                row->lastPass = pass;
            }
            if (status != PF_ST_OK || ns == 0) {
                pos = e + 1;
                continue;
            }
            if (row->npass < PF_MAX_PASSES) {
                row->bytes[row->npass] = bytes;
                row->us[row->npass] = ns / 1000u;
                row->reqs[row->npass] = reqs;
                row->npass++;
            }
            (void)lat;
        } else if (n >= 14 && memcmp(s, "XPRF-VERIFY 1 ", 14) == 0) {
            pf_u64 v;

            if (!pf_parse_u64(s + 14, n - 14, &v) || v > PF_EV_STOPPED) {
                return line;
            }
            sv->endVerify = (int)v;
        }
        pos = e + 1;
    }
    if (!haveRun) {
        return line == 0 ? 1 : line;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* The command line                                                          */
/* ------------------------------------------------------------------------ */

static int pf_num(const char *s, pf_u32 lo, pf_u32 hi, pf_u32 *v)
{
    pf_u64 x;

    if (!pf_parse_u64(s, strlen(s), &x) || x < lo || x > hi) {
        return 0;
    }
    *v = (pf_u32)x;
    return 1;
}

int pf_cmdline(int argc, char **argv, PF_PARAMS *p, PF_CMDLINE *c, char *why, size_t whyCap)
{
    int i;
    pf_u32 v;

    memset(c, 0, sizeof(*c));
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *nx = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (a[0] != '/' && a[0] != '-') {
            pf_cpy(why, whyCap, "Unknown argument: ");
            pf_cat(why, whyCap, a);
            return 1;
        }
        a++;
        if (pf_ieq(a, "run")) {
            c->run = 1;
        } else if (pf_ieq(a, "exit")) {
            c->exitAtEnd = 1;
        } else if (pf_ieq(a, "selftest")) {
            c->selftest = 1;
        } else if (pf_ieq(a, "cached")) {
            p->access = PF_ACC_CACHED;
        } else if (pf_ieq(a, "writethrough")) {
            p->writeThrough = 1;
        } else if (pf_ieq(a, "verify")) {
            p->verify = 1;
            p->pattern = PF_PAT_VERIFY;
        } else if (nx == NULL) {
            pf_cpy(why, whyCap, "Unknown switch, or one without its value: /");
            pf_cat(why, whyCap, a);
            return 1;
        } else if (pf_ieq(a, "preset")) {
            int pr = pf_preset_by_name(nx);

            if (pr == PF_PRESET_CUSTOM) {
                pf_cpy(why, whyCap, "Unknown preset: ");
                pf_cat(why, whyCap, nx);
                return 1;
            }
            pf_preset(p, pr);
            i++;
        } else if (pf_ieq(a, "folder")) {
            pf_cpy(c->folder, sizeof(c->folder), nx);
            i++;
        } else if (pf_ieq(a, "report")) {
            pf_cpy(c->report, sizeof(c->report), nx);
            i++;
        } else if (pf_ieq(a, "size")) {
            if (!pf_num(nx, 16, 16384, &v)) {
                pf_cpy(why, whyCap, "/size takes 16 to 16384 (MiB).");
                return 1;
            }
            p->fileMiB = v;
            p->preset = PF_PRESET_CUSTOM;
            i++;
        } else if (pf_ieq(a, "min") || pf_ieq(a, "max")) {
            /* the sweep's smallest or largest size, in KiB */
            if (!pf_num(nx, 1, 8192, &v) || !pf_pow2(v)) {
                pf_cpy(why, whyCap, "/min and /max take a power of two from 1 to 8192 (KiB).");
                return 1;
            }
            if (pf_ieq(a, "min")) {
                p->sweepMin = v * 1024u;
            } else {
                p->sweepMax = v * 1024u;
            }
            p->sweep = 1;
            p->preset = PF_PRESET_CUSTOM;
            i++;
        } else if (pf_ieq(a, "passes")) {
            if (!pf_num(nx, 1, PF_MAX_PASSES, &v)) {
                pf_cpy(why, whyCap, "/passes takes 1 to 9.");
                return 1;
            }
            p->passes = (int)v;
            p->preset = PF_PRESET_CUSTOM;
            i++;
        } else if (pf_ieq(a, "time")) {
            if (!pf_num(nx, 1, 60, &v)) {
                pf_cpy(why, whyCap, "/time takes 1 to 60 (seconds).");
                return 1;
            }
            p->timeSec = (int)v;
            p->preset = PF_PRESET_CUSTOM;
            i++;
        } else if (pf_ieq(a, "depth") || pf_ieq(a, "threads")) {
            int t;
            int isDepth = pf_ieq(a, "depth");

            if (!pf_num(nx, 1, isDepth ? PF_MAX_DEPTH : PF_MAX_THREADS, &v)) {
                pf_cpy(why, whyCap, isDepth ? "/depth takes 1 to 32." : "/threads takes 1 to 8.");
                return 1;
            }
            for (t = 0; t < p->ntests; t++) {
                if (isDepth) {
                    p->tests[t].depth = (int)v;
                } else {
                    p->tests[t].threads = (int)v;
                }
            }
            if (isDepth) {
                p->sweepDepth = (int)v;
            }
            p->preset = PF_PRESET_CUSTOM;
            i++;
        } else if (pf_ieq(a, "buffer")) {
            if (pf_ieq(nx, "aligned")) {
                p->bufMask = 1 << PF_BUF_ALIGNED;
            } else if (pf_ieq(nx, "offset")) {
                p->bufMask = 1 << PF_BUF_OFFSET;
            } else if (pf_ieq(nx, "scattered")) {
                p->bufMask = 1 << PF_BUF_SCATTER;
            } else {
                pf_cpy(why, whyCap, "/buffer takes aligned, offset or scattered.");
                return 1;
            }
            p->preset = PF_PRESET_CUSTOM;
            i++;
        } else if (pf_ieq(a, "pattern")) {
            if (pf_ieq(nx, "verifiable")) {
                p->pattern = PF_PAT_VERIFY;
            } else if (pf_ieq(nx, "random")) {
                p->pattern = PF_PAT_RANDOM;
                p->verify = 0;
            } else if (pf_ieq(nx, "zeros")) {
                p->pattern = PF_PAT_ZEROS;
                p->verify = 0;
            } else {
                pf_cpy(why, whyCap, "/pattern takes verifiable, random or zeros.");
                return 1;
            }
            p->preset = PF_PRESET_CUSTOM;
            i++;
        } else {
            pf_cpy(why, whyCap, "Unknown switch: /");
            pf_cat(why, whyCap, a);
            return 1;
        }
    }
    return 0;
}
