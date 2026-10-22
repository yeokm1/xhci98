/*
 * test_pf.c - host vectors for XHCIPERF's pure half (xhciperf\pf_core.c;
 * design record 29 section 9; roadmap-usb4.md task 43.50): the presets and
 * the plan's refusals per system family, the free-space rule, the
 * generator, the verifiable pattern and its check, the generation
 * allocator at its limit, the statistics at their bounds, the latency
 * histogram, the header and the leftover rule, paths, the workload key,
 * the saved file read back, and the command line.
 */

#include <stdio.h>
#include <string.h>
#include "../xhciperf/pf_core.h"
#include "test_harness.h"

static void env_nt(PF_ENV *e)
{
    memset(e, 0, sizeof(*e));
    e->os = PF_OS_NT;
    e->sector = 512;
    e->freeUser = (pf_u64)100000 * 1048576u;
    e->freeTotal = e->freeUser;
    e->volumeBytes = (pf_u64)200000 * 1048576u;
    e->memBytes = (pf_u64)4096 * 1048576u;
}

static void test_presets(void)
{
    PF_PARAMS p;
    PF_ENV e;
    PF_PLAN pl;
    char why[200];

    env_nt(&e);
    pf_defaults(&p, PF_OS_NT);
    CHECK_EQ(p.preset, PF_PRESET_STANDARD, "NT default: Standard");
    CHECK_EQ(p.fileMiB, 1024, "Standard: 1 GiB");
    CHECK_EQ(p.ntests, 4, "Standard: four tests");
    CHECK_EQ(p.tests[0].depth, 8, "Standard: Seq 1M d8");
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "Standard plans on NT");
    CHECK_EQ(pl.nsteps, 8, "Standard: write and read of four tests");
    CHECK(strcmp(pl.steps[0].label, "Seq 1M d8 t1") == 0, "the first label");
    CHECK_EQ(pl.steps[0].op, PF_OP_WRITE, "write first");
    CHECK_EQ(pl.steps[1].op, PF_OP_READ, "then read");
    CHECK_EQ(pl.slots, 32, "the most slots: Rnd 4K d32");
    CHECK_EQ(pl.fileBytes, (pf_u64)1024 * 1048576u + PF_HEADER_BYTES, "file = region + header");

    e.os = PF_OS_9X;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "Standard refused on 9x (depth 8)");
    CHECK(strstr(why, "Queue depth 1") != NULL, "the 9x refusal names queue depth 1");

    pf_defaults(&p, PF_OS_9X);
    CHECK_EQ(p.preset, PF_PRESET_9X, "9x default: the 9x standard");
    CHECK_EQ(p.fileMiB, 256, "9x standard: 256 MiB");
    CHECK(p.writeThrough && p.verify, "9x standard: write-through and verified");
    CHECK_EQ(p.tests[0].size, 512u * 1024u, "9x standard: 512 KiB first");
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "the 9x standard plans on 9x");
    CHECK(pl.mapBlocks != 0, "a verified run has a map");

    pf_preset(&p, PF_PRESET_STUDY);
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "the study plans on 9x");
    CHECK_EQ(pl.nsteps, 3 * 5 * 2, "study: 3 buffers x 5 sizes x write and read");
    CHECK_EQ(pl.steps[0].size, 64u * 1024u, "study starts at 64 KiB");
    CHECK_EQ(pl.steps[29].buffer, PF_BUF_SCATTER, "study ends with the scattered buffer");
    CHECK_EQ(pl.steps[0].budget, (pf_u64)256 * 1048576u, "study: total per size the region");

    CHECK_EQ(pf_preset_by_name("Quick"), PF_PRESET_QUICK, "preset by name, any case");
    CHECK_EQ(pf_preset_by_name("nope"), PF_PRESET_CUSTOM, "unknown preset");
}

static void test_refusals(void)
{
    PF_PARAMS p;
    PF_ENV e;
    PF_PLAN pl;
    char why[200];

    env_nt(&e);
    pf_defaults(&p, PF_OS_NT);
    e.sector = 4096;
    p.tests[0].size = 512;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "512 bytes unbuffered on a 4096-byte sector");
    p.access = PF_ACC_CACHED;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "cached: any multiple of 512");
    CHECK(pl.warnCache, "a cached file that fits in memory warns");

    pf_defaults(&p, PF_OS_NT);
    env_nt(&e);
    e.fat = 1;
    p.fileMiB = 2048;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "FAT: at most 1 GiB of region");
    p.fileMiB = 1024;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "FAT: 1 GiB");

    pf_defaults(&p, PF_OS_NT);
    env_nt(&e);
    p.fileMiB = 16;
    p.tests[0].threads = 8;
    p.tests[0].depth = 32;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "region smaller than size x depth x threads");

    pf_defaults(&p, PF_OS_NT);
    p.pattern = PF_PAT_RANDOM;
    p.verify = 1;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "verify needs the verifiable pattern");

    pf_defaults(&p, PF_OS_NT);
    p.pattern = PF_PAT_VERIFY;
    p.verify = 1;
    p.fileMiB = 16384;
    p.ntests = 1;
    p.tests[0].size = 512;
    p.tests[0].depth = 1;
    p.tests[0].kind = PF_RND;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "a map over 16 MiB refused");
    p.tests[0].size = 4096;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "16 GiB of 4 KiB blocks: a 16 MiB map");

    pf_defaults(&p, PF_OS_NT);
    p.passes = 9;
    p.timeSec = 60;
    p.ntests = 16;
    {
        int i;

        for (i = 0; i < 16; i++) {
            p.tests[i] = p.tests[i % 4];
        }
    }
    p.bufMask = 7;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "a run over the step or time limit refused");

    pf_defaults(&p, PF_OS_NT);
    e.alignMask = 0xFFF;
    p.bufMask = 1 << PF_BUF_OFFSET;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "an offset against the adapter's alignment");
}

static void test_sweep_budget(void)
{
    PF_PARAMS p;
    PF_ENV e;
    PF_PLAN pl;
    char why[200];
    int i;

    env_nt(&e);
    pf_defaults(&p, PF_OS_NT);
    pf_preset(&p, PF_PRESET_SWEEP);
    p.sweepDepth = 4;
    p.sweepTotalMiB = 100;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "a sweep plans");
    for (i = 0; i < pl.nsteps; i++) {
        CHECK_EQ(pl.steps[i].budget, (pf_u64)96 * 1048576u, "one budget, a multiple of 8 MiB x 4");
    }
    p.sweepTotalMiB = 16;
    p.sweepMax = 8u * 1048576u;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "a total below largest x depth refused");
    p.sweepTotalMiB = 0;
    p.sweepMin = 1000;
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_REFUSE, "a sweep size not a power of two");
}

static void test_free(void)
{
    PF_ENV e;
    char why[200];

    env_nt(&e);
    e.freeUser = e.freeTotal = (pf_u64)1100 * 1048576u;
    CHECK_EQ(pf_reserve(&e), (pf_u64)64 * 1048576u, "a data volume keeps 64 MiB");
    CHECK_EQ(pf_free_check(&e, (pf_u64)1024 * 1048576u, why, sizeof(why)), 0, "1 GiB fits with 64 MiB left");
    e.systemVolume = 1;
    CHECK_EQ(pf_reserve(&e), e.volumeBytes / 10, "the Windows volume keeps 10%");
    CHECK_EQ(pf_free_check(&e, (pf_u64)1024 * 1048576u, why, sizeof(why)), 1, "refused on the Windows volume");
    CHECK(strstr(why, "must leave") != NULL, "the refusal names the reserve");
    e.volumeBytes = (pf_u64)1000 * 1048576u;
    CHECK_EQ(pf_reserve(&e), (pf_u64)512 * 1048576u, "never below 512 MiB");
    e.systemVolume = 0;
    e.freeUser = (pf_u64)500 * 1048576u;
    CHECK_EQ(pf_free_check(&e, (pf_u64)1024 * 1048576u, why, sizeof(why)), 1, "a quota smaller than the file");
}

static void test_generator(void)
{
    pf_u32 s = 0;
    pf_u32 a, b;

    a = pf_xs_next(&s);
    CHECK(s != 0 && a != 0, "a zero state is remapped");
    CHECK_EQ(pf_xs_seed(0), 0x9E3779B9u, "seed 0 -> the golden ratio");
    s = 1;
    CHECK_EQ(pf_xs_next(&s), 270369u, "xorshift32(1), shifts 13 17 5");
    CHECK_EQ(pf_xs_next(&s), 67634689u, "the second value");
    a = pf_thread_seed(0x1A2B3C4Du, 0);
    b = pf_thread_seed(0x1A2B3C4Du, 1);
    CHECK(a != b && a != 0 && b != 0, "threads get different nonzero seeds");
    CHECK(pf_thread_seed(0x9E3779B9u ^ 0x9E3779B9u, 0) != 0, "a derived zero is remapped");
    CHECK(pf_xs_mix(0, 0, 0xFFFFFFFFu) != 0, "a mixed zero is remapped");
}

static void test_pattern(void)
{
    static unsigned char buf[4 * PF_UNIT];
    pf_u32 first, found;

    pf_stamp(buf, sizeof(buf), 0xAABBCCDDu, 100, 7);
    CHECK(buf[0] == 'X' && buf[1] == 'P' && buf[2] == 'R' && buf[3] == 'F', "the stamp's signature");
    CHECK_EQ(buf[8] | (buf[9] << 8), 100, "the unit index");
    CHECK_EQ(pf_check(buf, sizeof(buf), 0xAABBCCDDu, 100, 7, &first, &found), 0, "a stamped buffer checks clean");
    CHECK_EQ(pf_check(buf, sizeof(buf), 0xAABBCCDDu, 100, 8, &first, &found), 4, "an older generation: a dropped write");
    CHECK_EQ(found, 7, "the dropped write's found generation");
    CHECK_EQ(pf_check(buf, sizeof(buf), 0xAABBCCDDu, 101, 7, &first, &found), 4, "another offset: a misplaced write");
    CHECK_EQ(found, PF_GEN_UNKNOWN, "a misplaced unit's generation is unknown");
    CHECK_EQ(pf_check(buf, sizeof(buf), 0x11111111u, 100, 7, &first, &found), 4, "another run's units");
    buf[2 * PF_UNIT + 300] ^= 1;
    CHECK_EQ(pf_check(buf, sizeof(buf), 0xAABBCCDDu, 100, 7, &first, &found), 1, "a changed body byte");
    CHECK_EQ(first, 2, "the first bad unit");
    CHECK_EQ(found, 7, "its stamp is still right");
    pf_stamp(buf, PF_UNIT, 0xAABBCCDDu, 100, 7);
    pf_stamp(buf, PF_UNIT, 0xAABBCCDDu, 100, 9);
    CHECK_EQ(pf_check(buf, PF_UNIT, 0xAABBCCDDu, 100, 9, &first, &found), 0, "a block written twice: the second");
    CHECK_EQ(pf_check(buf, PF_UNIT, 0xAABBCCDDu, 100, 7, &first, &found), 1, "a block written twice: not the first");
}

static void test_gen(void)
{
    PF_GEN g;
    pf_u32 a, b;

    pf_gen_init(&g);
    CHECK_EQ(pf_gen_take(&g), 1, "the first generation is 1");
    g.last = 0xFFFFFFF0u;
    a = pf_gen_take(&g);
    CHECK_EQ(a, 0xFFFFFFF1u, "near the limit");
    g.last = 0xFFFFFFFDu;
    a = pf_gen_take(&g);
    CHECK_EQ(a, 0xFFFFFFFEu, "the last value");
    /* two workers interleaved at exhaustion, one already past its stop
     * check: both get none */
    a = pf_gen_take(&g);
    b = pf_gen_take(&g);
    CHECK_EQ(a, 0, "saturated: none");
    CHECK_EQ(b, 0, "saturated: none again");
    CHECK_EQ(g.last, PF_GEN_LAST, "it stays saturated, never wraps");
    g.last = 0xFFFFFFFFu;
    CHECK_EQ(pf_gen_take(&g), 0, "past the limit: none");
}

static void test_stats(void)
{
    PF_LAT l;
    char s[40];

    CHECK_EQ(pf_ticks_us(1193182u, 1193182u), 1000000u, "one second at the 8254's rate");
    CHECK_EQ(pf_ticks_us((pf_u64)3579545u * 7200u, 3579545u), (pf_u64)7200000000u, "two hours at the ACPI timer");
    CHECK_EQ(pf_ticks_us((pf_u64)0xFFFFFFFEu * 7200u, 0xFFFFFFFEu), (pf_u64)7200000000u, "two hours near 2^32 Hz");
    CHECK_EQ(pf_ticks_us(5, 0), 0, "no frequency");
    CHECK_EQ(pf_rate10(100000000u, 1000000u, 0), 1000u, "100 MB in 1 s = 100.0 MB/s");
    CHECK_EQ(pf_rate10(104857600u, 1000000u, 1), 1000u, "100 MiB in 1 s = 100.0 MiB/s");
    CHECK_EQ(pf_rate10(0, 0, 0), 0, "a zero interval");
    CHECK_EQ(pf_rate10((pf_u64)8000000000u * 7200u, (pf_u64)7200000000u, 0), 80000u, "8 GB/s for two hours, MB");
    CHECK_EQ(pf_rate10((pf_u64)8000000000u * 7200u, (pf_u64)7200000000u, 1), 76294u, "8 GB/s for two hours, MiB");
    CHECK_EQ(pf_rate10(105u, 100u, 0), 11u, "1.05 rounds to 1.1 (half up)");
    CHECK_EQ(pf_rate10(104u, 100u, 0), 10u, "1.04 rounds to 1.0");
    CHECK_EQ(pf_iops(1000, 500000), 2000, "1000 requests in 0.5 s");
    pf_fmt_tenths(1234, s, sizeof(s));
    CHECK(strcmp(s, "123.4") == 0, "tenths");
    pf_fmt_tenths(5, s, sizeof(s));
    CHECK(strcmp(s, "0.5") == 0, "a half");
    pf_fmt_us(812, s, sizeof(s));
    CHECK(strcmp(s, "812 us") == 0, "microseconds");
    pf_fmt_us(3456, s, sizeof(s));
    CHECK(strcmp(s, "3.46 ms") == 0, "milliseconds");
    pf_fmt_us(1200000, s, sizeof(s));
    CHECK(strcmp(s, "1.20 s") == 0, "seconds");

    pf_lat_init(&l);
    CHECK_EQ(pf_lat_pct(&l, 50), 0, "an empty histogram");
    {
        int i;

        for (i = 0; i < 99; i++) {
            pf_lat_add(&l, 100);
        }
        pf_lat_add(&l, 50000);
    }
    CHECK_EQ(l.count, 100, "100 requests");
    CHECK_EQ(l.minUs, 100, "min");
    CHECK_EQ(l.maxUs, 50000, "max");
    CHECK_EQ(pf_lat_mean(&l), (99u * 100u + 50000u + 50u) / 100u, "mean");
    CHECK(pf_lat_pct(&l, 50) >= 100 && pf_lat_pct(&l, 50) < 140, "the median's bucket edge");
    CHECK(pf_lat_pct(&l, 99) >= 100 && pf_lat_pct(&l, 99) < 140, "99% are in the first bucket");
    CHECK_EQ(pf_lat_pct(&l, 100), 50000, "the 100th percentile is the max");
    CHECK_EQ(pf_lat_edge(0), 1, "the first edge is 1 us");
    CHECK(pf_lat_edge(63) > 50000000u, "the last edge is past 50 s");
    CHECK(pf_timer_plausible(1000000u, 1000u), "QPC and the tick count agree");
    CHECK(!pf_timer_plausible(3000000u, 1000u), "QPC far ahead");
    CHECK(!pf_timer_plausible(100u, 1000u), "QPC far behind");
    CHECK(pf_cpu_limited(11, 100) && !pf_cpu_limited(10, 100), "the processor share mark at 10%");
}

static void test_shown(void)
{
    PF_RESULT r[3];
    PF_SHOWN sh;
    int i;

    memset(r, 0, sizeof(r));
    for (i = 0; i < 3; i++) {
        r[i].status = PF_ST_OK;
        r[i].us = 1000000u;
        r[i].reqs = 100;
        pf_lat_init(&r[i].lat);
        pf_lat_add(&r[i].lat, 1000u * (pf_u64)(i + 1));
    }
    r[0].bytes = 100000000u;
    r[1].bytes = 300000000u;
    r[2].bytes = 200000000u;
    pf_shown(r, 3, PF_SHOW_BEST, 0, 1, &sh);
    CHECK_EQ(sh.rate10, 3000, "best: 300.0");
    CHECK_EQ(sh.latMean, 2000, "best: that pass's latency");
    pf_shown(r, 3, PF_SHOW_MEAN, 0, 1, &sh);
    CHECK_EQ(sh.rate10, 2000, "mean: 200.0");
    CHECK_EQ(sh.latMean, 2000, "mean: the passes merged");
    pf_shown(r, 3, PF_SHOW_MEDIAN, 0, 1, &sh);
    CHECK_EQ(sh.rate10, 2000, "median: 200.0");
    pf_shown(r, 2, PF_SHOW_MEDIAN, 0, 1, &sh);
    CHECK_EQ(sh.rate10, 2000, "median of two: their mean");
    pf_shown(r, 3, PF_SHOW_BEST, 0, 0, &sh);
    CHECK(!sh.haveLat, "no latency with the millisecond timer");
    r[1].status = PF_ST_DATAERR;
    pf_shown(r, 3, PF_SHOW_BEST, 0, 1, &sh);
    CHECK_EQ(sh.status, PF_ST_DATAERR, "a data error shows");
    CHECK_EQ(sh.rate10, 2000, "the best clean pass");
    r[0].status = r[2].status = PF_ST_FAILED;
    pf_shown(r, 3, PF_SHOW_BEST, 0, 1, &sh);
    CHECK(sh.status == PF_ST_FAILED || sh.status == PF_ST_DATAERR, "no clean pass");
    CHECK_EQ(sh.rate10, 0, "no value");
}

static void test_header(void)
{
    PF_HEADER h, g;
    PF_RECORD rec;
    unsigned char b[PF_UNIT];

    memset(&h, 0, sizeof(h));
    h.runLo = 0x12345678u;
    h.runHi = 0x9ABCDEF0u;
    h.plannedBytes = (pf_u64)257 * 1048576u;
    h.volSerial = 0xCAFEBABEu;
    pf_header_build(&h, b);
    CHECK_EQ(pf_header_parse(b, PF_UNIT, &g), 1, "the header reads back");
    CHECK(g.runLo == h.runLo && g.runHi == h.runHi && g.plannedBytes == h.plannedBytes, "its fields");
    b[100] = 1;
    CHECK_EQ(pf_header_parse(b, PF_UNIT, &g), 0, "a header with stray bytes is not ours");
    b[100] = 0;
    b[0] = 'Y';
    CHECK_EQ(pf_header_parse(b, PF_UNIT, &g), 0, "another signature");
    b[0] = 'X';
    pf_header_parse(b, PF_UNIT, &g);

    memset(&rec, 0, sizeof(rec));
    rec.present = 1;
    rec.runLo = h.runLo;
    rec.runHi = h.runHi;
    rec.plannedBytes = h.plannedBytes;
    rec.volSerial = h.volSerial;
    rec.indexLo = 5;
    CHECK_EQ(pf_leftover(&g, 1, &rec, h.plannedBytes, 5, 0, 1), PF_LEFT_OURS, "a recorded leftover");
    CHECK_EQ(pf_leftover(&g, 1, &rec, (pf_u64)64 * 1048576u, 5, 0, 1), PF_LEFT_OURS, "a half-prepared one");
    CHECK_EQ(pf_leftover(&g, 1, &rec, h.plannedBytes + 1, 5, 0, 1), PF_LEFT_FOREIGN, "longer than planned");
    CHECK_EQ(pf_leftover(&g, 1, &rec, 1000, 5, 0, 1), PF_LEFT_FOREIGN, "shorter than the header");
    CHECK_EQ(pf_leftover(&g, 1, &rec, h.plannedBytes, 6, 0, 1), PF_LEFT_UNRECORDED, "another file index");
    CHECK_EQ(pf_leftover(&g, 0, &rec, h.plannedBytes, 5, 0, 1), PF_LEFT_FOREIGN, "no header: foreign");
    rec.runLo++;
    CHECK_EQ(pf_leftover(&g, 1, &rec, h.plannedBytes, 5, 0, 1), PF_LEFT_UNRECORDED, "another run's record");
    CHECK_EQ(pf_leftover(&g, 1, NULL, h.plannedBytes, 5, 0, 1), PF_LEFT_UNRECORDED, "no record");
}

static void test_paths(void)
{
    CHECK(pf_path_under("C:\\WINDOWS\\SYSTEM\\X", "c:\\windows"), "below, any case");
    CHECK(pf_path_under("C:\\WINDOWS", "C:\\WINDOWS\\"), "the folder itself, a trailing backslash");
    CHECK(!pf_path_under("C:\\WINDOWSX\\A", "C:\\WINDOWS"), "a longer name is not below");
    CHECK(!pf_path_under("D:\\", "C:\\WINDOWS"), "another drive");
    CHECK(pf_path_under("C:/Windows/Temp", "C:\\WINDOWS"), "forward slashes");
}

static void test_key_saved(void)
{
    PF_PARAMS p;
    PF_ENV e;
    PF_PLAN pl;
    PF_RESULT res[PF_MAX_STEPS * PF_MAX_PASSES];
    PF_RUNINFO ri;
    PF_TEXT t;
    PF_SAVED sv;
    static char buf[65536];
    char k1[512], k2[512], diff[200];
    char why[200];
    int i;

    env_nt(&e);
    pf_defaults(&p, PF_OS_NT);
    pf_key(&p, 1, k1, sizeof(k1));
    p.writeThrough = 1;
    pf_key(&p, 1, k2, sizeof(k2));
    CHECK(!pf_key_same(k1, k2, diff, sizeof(diff)), "write-through changes the key");
    CHECK(strcmp(diff, "wt") == 0, "and is named");
    p.writeThrough = 0;
    p.seed = 5;
    pf_key(&p, 0, k2, sizeof(k2));
    CHECK(!pf_key_same(k1, k2, diff, sizeof(diff)), "seed and timer");
    CHECK(strcmp(diff, "seed, timer") == 0, "both named");
    CHECK(pf_key_same(k1, k1, diff, sizeof(diff)), "a key equals itself");

    pf_defaults(&p, PF_OS_NT);
    CHECK_EQ(pf_plan(&p, &e, &pl, why, sizeof(why)), PF_OK, "plan for the report");
    memset(res, 0, sizeof(res));
    for (i = 0; i < pl.nsteps; i++) {
        res[i * PF_MAX_PASSES].status = PF_ST_OK;
        res[i * PF_MAX_PASSES].bytes = (pf_u64)(i + 1) * 100000000u;
        res[i * PF_MAX_PASSES].us = 1000000u;
        res[i * PF_MAX_PASSES].reqs = 100;
        pf_lat_init(&res[i * PF_MAX_PASSES].lat);
        pf_lat_add(&res[i * PF_MAX_PASSES].lat, 250);
    }
    memset(&ri, 0, sizeof(ri));
    ri.toolVersion = "3.2.0.0";
    ri.date = "2026-10-22 13:00";
    ri.system = "Windows 7 6.1.7601 x64";
    ri.drive = "F:\\";
    ri.fileSystem = "NTFS";
    ri.model = "QEMU NVMe Ctrl";
    ri.sector = 512;
    ri.timerFreq = 3579545u;
    ri.stor98 = "";
    pf_tx_init(&t, buf, sizeof(buf));
    pf_report(&t, &p, &pl, res, 1, &ri);
    CHECK(!t.overflow, "the report fits");
    CHECK(strstr(buf, "Seq 1M d8 t1   write aligned        100.0 MB/s") != NULL, "a summary line");
    CHECK(strstr(buf, "XPRF-RUN 1 file=1024") != NULL, "the machine key line");
    {
        /* every human line within 79 columns */
        const char *s = buf;
        int ok = 1;

        while (*s) {
            const char *e2 = strstr(s, "\r\n");
            size_t n = e2 ? (size_t)(e2 - s) : strlen(s);

            if (n > 79 && strncmp(s, "XPRF-", 5) != 0) {
                ok = 0;
            }
            if (!e2) {
                break;
            }
            s = e2 + 2;
        }
        CHECK(ok, "the report's lines fit 79 columns");
    }
    CHECK_EQ(pf_saved_parse(buf, t.len, &sv), 0, "the saved file reads back");
    CHECK_EQ(sv.nrows, 8, "eight results");
    CHECK(strcmp(sv.rows[0].label, "Seq 1M d8 t1") == 0 && sv.rows[0].op == PF_OP_WRITE, "the first row");
    CHECK_EQ(sv.rows[1].rate10mb, 2000, "the second row's rate");
    CHECK(pf_key_same(sv.key, k1, diff, sizeof(diff)), "its key is the run's");
    {
        static char bad[] = "XPRF-RUN 1 file=1\r\nXPRF-RESULT 1 Seq 1M d1 t1|read|aligned|1|1|x|1|1|1\r\n";

        CHECK_EQ(pf_saved_parse(bad, sizeof(bad) - 1, &sv), 2, "a bad line is named by number");
    }
    {
        static char none[] = "hello\r\n";

        CHECK(pf_saved_parse(none, sizeof(none) - 1, &sv) != 0, "a file with no run line is refused");
    }
    CHECK_EQ(pf_change10(110, 100), 100, "+10.0%");
    CHECK_EQ(pf_change10(90, 100), -100, "-10.0%");
    CHECK_EQ(pf_change10(5, 0), 0, "no old value");
}

static void test_cmdline(void)
{
    PF_PARAMS p;
    PF_CMDLINE c;
    char why[200];
    char *a1[] = { "x", "/run", "/preset", "quick", "/folder", "F:\\T", "/depth", "4", "/report", "r.txt", "/exit" };
    char *a2[] = { "x", "/frobnicate" };
    char *a3[] = { "x", "/size", "8" };
    char *a4[] = { "x", "/buffer", "scattered", "/pattern", "zeros" };

    pf_defaults(&p, PF_OS_NT);
    CHECK_EQ(pf_cmdline(11, a1, &p, &c, why, sizeof(why)), 0, "a full command line");
    CHECK(c.run && c.exitAtEnd, "run and exit");
    CHECK_EQ(p.preset, PF_PRESET_CUSTOM, "the depth makes it custom");
    CHECK_EQ(p.tests[0].depth, 4, "depth set");
    CHECK(strcmp(c.folder, "F:\\T") == 0 && strcmp(c.report, "r.txt") == 0, "folder and report");
    CHECK_EQ(pf_cmdline(2, a2, &p, &c, why, sizeof(why)), 1, "an unknown switch");
    CHECK_EQ(pf_cmdline(3, a3, &p, &c, why, sizeof(why)), 1, "/size below 16");
    pf_defaults(&p, PF_OS_NT);
    CHECK_EQ(pf_cmdline(5, a4, &p, &c, why, sizeof(why)), 0, "buffer and pattern");
    CHECK_EQ(p.bufMask, 1 << PF_BUF_SCATTER, "scattered");
    CHECK_EQ(p.pattern, PF_PAT_ZEROS, "zeros");
    CHECK(!p.verify, "zeros cannot be verified");
}

static void test_labels(void)
{
    char s[64];

    pf_test_label(PF_SEQ, 1048576u, 8, 1, s, sizeof(s));
    CHECK(strcmp(s, "Seq 1M d8 t1") == 0, "Seq 1M d8 t1");
    pf_test_label(PF_RND, 4096u, 32, 16, s, sizeof(s));
    CHECK(strcmp(s, "Rnd 4K d32 t16") == 0, "Rnd 4K d32 t16");
    pf_test_long(PF_SEQ, 1048576u, 8, 1, s, sizeof(s));
    CHECK(strcmp(s, "Sequential 1 MiB, depth 8, 1 thread") == 0, "the long label");
    pf_test_long(PF_RND, 4096u, 1, 4, s, sizeof(s));
    CHECK(strcmp(s, "Random 4 KiB, depth 1, 4 threads") == 0, "threads in the long label");
    pf_size_text(512, s, sizeof(s));
    CHECK(strcmp(s, "512") == 0, "512 bytes");
}

int main(void)
{
    test_presets();
    test_refusals();
    test_sweep_budget();
    test_free();
    test_generator();
    test_pattern();
    test_gen();
    test_stats();
    test_shown();
    test_header();
    test_paths();
    test_key_saved();
    test_cmdline();
    test_labels();
    printf("test_pf: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
