/*
 * pf_s98.c - Stor98's counter answer as XHCIPERF reads it (pf_s98.h;
 * design record 29 sections 5.5 and 6).
 */

#include <string.h>
#include "pf_s98.h"

static pf_u32 s98_get32(const unsigned char *p)
{
    return (pf_u32)p[0] | ((pf_u32)p[1] << 8) | ((pf_u32)p[2] << 16) | ((pf_u32)p[3] << 24);
}

int s98_parse(const unsigned char *b, pf_u32 len, S98_ANSWER *a)
{
    int i;

    memset(a, 0, sizeof(*a));
    if (len < S98_ANSWER_BYTES || s98_get32(b + S98_OFS_SIGNATURE) != S98_SIGNATURE ||
        s98_get32(b + S98_OFS_VERSION) != S98_VERSION) {
        return 0;
    }
    a->version = s98_get32(b + S98_OFS_VERSION);
    a->length = s98_get32(b + S98_OFS_LENGTH);
    if (a->length < S98_ANSWER_BYTES) {
        return 0;
    }
    a->epoch = s98_get32(b + S98_OFS_EPOCH);
    if (a->epoch == 0) {
        return 0;
    }
    a->build = s98_get32(b + S98_OFS_BUILD);
    a->pci = s98_get32(b + S98_OFS_PCI);
    a->vendorDevice = s98_get32(b + S98_OFS_VENDORDEVICE);
    a->state = s98_get32(b + S98_OFS_STATE);
    for (i = 0; i < 16; i++) {
        unsigned char c = b[S98_OFS_DRIVERVERSION + i];

        a->driverVersion[i] = (c >= 0x20 && c < 0x7F) ? (char)c : 0;
        if (c == 0) {
            break;
        }
    }
    a->driverVersion[16] = 0;
    a->blockSize = s98_get32(b + S98_OFS_BLOCKSIZE);
    a->blocks = (pf_u64)s98_get32(b + S98_OFS_BLOCKSLO) | ((pf_u64)s98_get32(b + S98_OFS_BLOCKSHI) << 32);
    a->maxTransfer = s98_get32(b + S98_OFS_MAXTRANSFER);
    a->breaks = s98_get32(b + S98_OFS_BREAKS);
    a->mdts = s98_get32(b + S98_OFS_MDTS);
    a->maxChild = s98_get32(b + S98_OFS_MAXCHILD);
    a->childLimit = s98_get32(b + S98_OFS_CHILDLIMIT);
    a->childrenIo = s98_get32(b + S98_OFS_CHILDRENIO);
    a->writeThrough = s98_get32(b + S98_OFS_WT);
    a->vwc = s98_get32(b + S98_OFS_VWC);
    a->polling = s98_get32(b + S98_OFS_POLLING);
    a->dcbSeen = s98_get32(b + S98_OFS_DCBSEEN);
    a->dcbMaxXfer = s98_get32(b + S98_OFS_DCBMAXXFER);
    a->dcbMaxSg = s98_get32(b + S98_OFS_DCBMAXSG);
    a->dcbDmd = s98_get32(b + S98_OFS_DCBDMD);
    for (i = 0; i < S98_EVENTS; i++) {
        a->events[i] = s98_get32(b + S98_OFS_EVENTS + 4 * i);
    }
    for (i = 0; i < S98_TOTALS; i++) {
        const unsigned char *p = b + S98_OFS_TOTALS + 8 * i;

        a->tot[i] = (pf_u64)s98_get32(p) | ((pf_u64)s98_get32(p + 4) << 32);
    }
    return 1;
}

int s98_identity(const unsigned char *page, pf_u32 len, pf_u32 *pci)
{
    *pci = 0;
    if (len < S98_VPD_LEN || page[1] != S98_VPD_PAGE || page[3] < S98_VPD_LEN - 4 ||
        s98_get32(page + 4) != S98_SIGNATURE || s98_get32(page + 8) != S98_VERSION) {
        return 0;
    }
    *pci = s98_get32(page + 12);
    return 1;
}

int s98_lists_identity(const unsigned char *page, pf_u32 len)
{
    pf_u32 n, i;

    if (len < 4 || page[1] != 0x00) {
        return 0;
    }
    n = page[3];
    for (i = 0; i < n && 4 + i < len; i++) {
        if (page[4 + i] == S98_VPD_PAGE) {
            return 1;
        }
    }
    return 0;
}

int s98_diff(const S98_ANSWER *b, const S98_ANSWER *a, S98_DELTA *d)
{
    int i;

    memset(d, 0, sizeof(*d));
    if (b->epoch != a->epoch) {
        return 0;
    }
    for (i = 0; i < S98_TOTALS; i++) {
        if (a->tot[i] < b->tot[i]) {
            return 0;
        }
        d->tot[i] = a->tot[i] - b->tot[i];
    }
    for (i = 0; i < S98_EVENTS; i++) {
        d->events[i] = (a->events[i] - b->events[i]) & 0xFFFFFFFFu;
    }
    return 1;
}

int s98_size_bucket(pf_u32 bytes)
{
    if (bytes < 4096u) {
        return 0;
    }
    if (bytes == 4096u) {
        return 1;
    }
    if (bytes <= 8192u) {
        return 2;
    }
    if (bytes <= 16384u) {
        return 3;
    }
    if (bytes <= 32768u) {
        return 4;
    }
    if (bytes < 64512u) {
        return 5;
    }
    if (bytes == 64512u) {
        return 6;
    }
    if (bytes <= 65536u) {
        return 7;
    }
    if (bytes <= 131072u) {
        return 8;
    }
    if (bytes <= 262144u) {
        return 9;
    }
    if (bytes <= 524288u) {
        return 10;
    }
    if (bytes <= 1048576u) {
        return 11;
    }
    return 12;
}

static const char *const s98_size_names[S98_SIZES] = {
    "below 4 KiB", "4 KiB", "to 8 KiB", "to 16 KiB", "to 32 KiB", "below 63 KiB", "63 KiB",
    "to 64 KiB", "to 128 KiB", "to 256 KiB", "to 512 KiB", "to 1 MiB", "above 1 MiB"
};

const char *s98_size_name(int bucket)
{
    if (bucket < 0 || bucket >= S98_SIZES) {
        return "?";
    }
    return s98_size_names[bucket];
}

/* The fewest runs of each run bucket (stor98pf.h) */
static const pf_u32 s98_run_min[S98_RUN_NOT_SEEN] = { 1, 2, 3, 5, 9, 17, 18, 33, 65 };

/* Scattered: in the test's size class, most requests arrived whole with
 * more than one physical run per 64 KiB of the request (record 29 section
 * 3.2). Double-buffering before Stor98 hides the caller's layout, whatever
 * the runs say. */
int s98_scattered(const S98_DELTA *d, pf_u32 size, int write)
{
    int b = s98_size_bucket(size);
    int cls = b - S98_RUN_CLASS_FIRST;
    int dir = write ? 1 : 0;
    pf_u64 few = 0, more = 0, unseen, sum, pieces, cutReqs;
    pf_u32 per64 = (size + 65535u) / 65536u;
    int i;

    if (cls < 0 || cls >= S98_RUN_CLASSES) {
        return S98_SCAT_UNKNOWN;
    }
    if (d->tot[S98_T_DOUBLEBUF] != 0) {
        return S98_SCAT_HIDDEN;
    }
    for (i = 0; i < S98_RUN_NOT_SEEN; i++) {
        pf_u64 v = d->tot[S98_T_RUNS + cls * S98_RUN_BUCKETS + i];

        if (s98_run_min[i] > per64) {
            more += v;
        } else {
            few += v;
        }
    }
    unseen = d->tot[S98_T_RUNS + cls * S98_RUN_BUCKETS + S98_RUN_NOT_SEEN];
    pieces = d->tot[S98_T_SRBSIZE + dir * S98_SIZES + 6];
    sum = few + more;
    if (sum == 0) {
        if (pieces != 0 || unseen != 0) {
            return S98_SCAT_HIDDEN;
        }
        return S98_SCAT_UNKNOWN;
    }
    /* the test's requests that arrived cut or with a layout not seen:
     * unless the whole, seen ones are most of them, the layout is hidden.
     * The pieces counted are the full 63 KiB ones (a cut request's last,
     * smaller piece falls in another class), so a request gives
     * size / 63 KiB of them; a part group counts as a request. */
    {
        pf_u64 full = (pf_u64)size / 64512u;

        if (full == 0) {
            full = 1;
        }
        cutReqs = (pieces + full - 1u) / full;
    }
    if (sum <= cutReqs + unseen) {
        return S98_SCAT_HIDDEN;
    }
    return more * 2 > sum ? S98_SCAT_YES : S98_SCAT_NO;
}

const char *s98_scattered_text(int c)
{
    switch (c) {
    case S98_SCAT_YES:
        return "scattered, as seen by Stor98";
    case S98_SCAT_NO:
        return "not scattered, as seen by Stor98";
    case S98_SCAT_HIDDEN:
        return "not seen: requests were cut or double-buffered before Stor98";
    default:
        return "not known";
    }
}

static void s98_hist(PF_TEXT *t, const char *title, const pf_u64 *h)
{
    int i;
    int any = 0;

    pf_tx_s(t, title);
    for (i = 0; i < S98_SIZES; i++) {
        if (h[i] != 0) {
            pf_tx_s(t, any ? ", " : " ");
            pf_tx_s(t, s98_size_names[i]);
            pf_tx_s(t, ": ");
            pf_tx_u(t, h[i]);
            any = 1;
        }
    }
    if (!any) {
        pf_tx_s(t, " none");
    }
    pf_tx_nl(t);
}

void s98_facts(PF_TEXT *t, const S98_ANSWER *a, int is9x)
{
    pf_tx_s(t, "Stor98 ");
    pf_tx_s(t, a->driverVersion);
    pf_tx_s(t, (a->build & 1) ? " (Windows 98 and ME build)" : ((a->build & 2) ? " (x64 build)" : " (x86 build)"));
    pf_tx_s(t, ", PCI bus ");
    pf_tx_u(t, a->pci >> 16);
    pf_tx_s(t, " device ");
    pf_tx_u(t, (a->pci >> 3) & 0x1F);
    pf_tx_s(t, " function ");
    pf_tx_u(t, a->pci & 7);
    pf_tx_nl(t);
    pf_tx_s(t, "Largest request granted: ");
    pf_tx_u(t, a->maxTransfer / 1024u);
    pf_tx_s(t, " KiB, physical breaks ");
    pf_tx_u(t, a->breaks);
    pf_tx_s(t, "; largest command: ");
    pf_tx_u(t, a->maxChild / 1024u);
    pf_tx_s(t, " KiB");
    if (a->mdts != 0) {
        pf_tx_s(t, " (drive limit ");
        pf_tx_u(t, a->mdts / 1024u);
        pf_tx_s(t, " KiB)");
    }
    pf_tx_nl(t);
    pf_tx_s(t, "Commands at once: ");
    pf_tx_u(t, a->childLimit);
    pf_tx_s(t, "; block size ");
    pf_tx_u(t, a->blockSize);
    pf_tx_s(t, "; ");
    pf_tx_s(t, (a->polling & 1) ? "polling" : ((a->polling & 2) ? "polling after an interrupt storm" : "interrupts"));
    pf_tx_s(t, "; write cache ");
    pf_tx_s(t, a->vwc ? "present" : "none");
    pf_tx_s(t, a->writeThrough == 2 ? ", writes with FUA" : (a->writeThrough == 1 ? ", cache off" : ""));
    pf_tx_nl(t);
    if (is9x) {
        if (a->dcbSeen & 1) {
            pf_tx_s(t, "IOS: largest transfer ");
            pf_tx_u(t, a->dcbMaxXfer / 1024u);
            pf_tx_s(t, " KiB, ");
            pf_tx_u(t, a->dcbMaxSg);
            pf_tx_s(t, " scatter/gather elements; the system demands physical scatter/gather: ");
            pf_tx_s(t, (a->dcbDmd & S98_DCB_PHYS_SGD) ? "yes" : "no (an IOS layer above SCSIPORT removed the demand)");
            pf_tx_nl(t);
            if (a->dcbDmd & S98_DCB_PHYS_SGD) {
                /* 98 SE's page allocator decides it, not the buffer's mode */
                pf_tx_s(t, "So a request reaches Stor98 whole only when its buffer lies in ");
                pf_tx_u(t, a->dcbMaxSg);
                pf_tx_s(t, " physical pieces or fewer, which even an aligned buffer is not sure to: it depends on "
                           "how ordered Windows' free memory is (usually after a fresh start, often not after "
                           "other programs have run). Each test's buffer-layout line says what reached Stor98.");
                pf_tx_nl(t);
            }
        } else {
            pf_tx_s(t, "IOS: not seen yet (no request since Stor98 started)");
            pf_tx_nl(t);
        }
    }
}

void s98_page(PF_TEXT *t, const S98_ANSWER *a, const S98_DELTA *d, int is9x, pf_u64 testReqs,
              pf_u32 testSize, int write)
{
    int dir = write ? 1 : 0;
    int b = s98_size_bucket(testSize);
    char sz[24];
    int i;
    pf_u64 depthMax = 0;

    pf_tx_s(t, "Everything Stor98's adapter did during this test, from any program:");
    pf_tx_nl(t);
    pf_size_text(testSize, sz, sizeof(sz));
    pf_tx_s(t, "The test issued ");
    pf_tx_u(t, testReqs);
    pf_tx_s(t, write ? " writes of " : " reads of ");
    pf_tx_s(t, sz);
    pf_tx_s(t, ". Stor98 received ");
    pf_tx_u(t, d->tot[S98_T_SRBSIZE + dir * S98_SIZES + b]);
    pf_tx_s(t, " requests in that size class");
    if (b != 6) {
        pf_tx_s(t, " and ");
        pf_tx_u(t, d->tot[S98_T_SRBSIZE + dir * S98_SIZES + 6]);
        pf_tx_s(t, " of 63 KiB");
    }
    pf_tx_s(t, ", and sent ");
    pf_tx_u(t, d->tot[S98_T_CMDSIZE + dir * S98_SIZES + b]);
    pf_tx_s(t, " commands in it.");
    pf_tx_nl(t);
    s98_hist(t, write ? "Writes received:" : "Reads received:", &d->tot[S98_T_SRBSIZE + dir * S98_SIZES]);
    s98_hist(t, write ? "Write commands sent:" : "Read commands sent:", &d->tot[S98_T_CMDSIZE + dir * S98_SIZES]);
    pf_tx_s(t, "Split by Stor98: ");
    pf_tx_u(t, d->tot[S98_T_SPLITS]);
    pf_tx_s(t, "; bounced: ");
    pf_tx_u(t, d->tot[S98_T_BOUNCED]);
    pf_tx_s(t, "; flushes: ");
    pf_tx_u(t, d->tot[S98_T_FLUSHES]);
    pf_tx_s(t, "; failed: ");
    pf_tx_u(t, d->tot[S98_T_SRBERRORS]);
    pf_tx_nl(t);
    {
        static const char *const dn[S98_DEPTHS] = { "1", "2", "3-4", "5-8", "9-16", "17-32", "above 32" };
        int seen = 0;

        for (i = 0; i < S98_DEPTHS; i++) {
            if (d->tot[S98_T_DEPTH + i] != 0) {
                depthMax = (pf_u64)i;
                seen = 1;
            }
        }
        pf_tx_s(t, "Most commands at the drive at once: ");
        pf_tx_s(t, seen ? dn[depthMax] : "none (no data command reached the drive)");
        pf_tx_nl(t);
    }
    pf_tx_s(t, "Buffer layout: ");
    pf_tx_s(t, s98_scattered_text(s98_scattered(d, testSize, write)));
    pf_tx_s(t, " (if no other program used this disk during the test)");
    pf_tx_nl(t);
    if (is9x) {
        pf_tx_s(t, "IOS handed over: ");
        pf_tx_u(t, d->tot[S98_T_SG9X + 0]);
        pf_tx_s(t, " linear, ");
        pf_tx_u(t, d->tot[S98_T_SG9X + 1]);
        pf_tx_s(t, " scatter/gather lists, ");
        pf_tx_u(t, d->tot[S98_T_SG9X + 2]);
        pf_tx_s(t, " not recognised; double-buffered ");
        pf_tx_u(t, d->tot[S98_T_DOUBLEBUF]);
        pf_tx_nl(t);
    }
    if (d->events[0] || d->events[1] || d->events[2] || d->events[3]) {
        pf_tx_s(t, "Errors: fast fails ");
        pf_tx_u(t, d->events[0]);
        pf_tx_s(t, ", busy ");
        pf_tx_u(t, d->events[1]);
        pf_tx_s(t, ", resets ");
        pf_tx_u(t, d->events[2]);
        pf_tx_s(t, ", deadlines ");
        pf_tx_u(t, d->events[3]);
        pf_tx_nl(t);
    }
    s98_facts(t, a, is9x);
}

void s98_why_set(S98_WHY *w, int why, char letter, int n, const char *id)
{
    size_t i;

    w->why = why;
    w->letter = letter;
    w->n = n;
    w->models = 0;
    w->cmAnswered = 0;
    for (i = 0; id != NULL && id[i] != 0 && i < 60; i++) {
        w->id[i] = (id[i] >= 0x20 && id[i] < 0x7F) ? id[i] : '?';
    }
    w->id[i] = 0;
}

static void s98_why_sentence(PF_TEXT *t, const S98_WHY *w)
{
    char l[3];

    l[0] = w->letter;
    l[1] = ':';
    l[2] = 0;
    switch (w->why) {
    case S98_WHY_NOLETTER:
        if (w->cmAnswered) {
            pf_tx_s(t, " No present disk under Enum\\SCSI holds ");
            pf_tx_s(t, l);
            pf_tx_s(t, ".");
        } else {
            pf_tx_s(t, " No disk under Enum\\SCSI holds ");
            pf_tx_s(t, l);
            pf_tx_s(t, " (the configuration manager found none of its disks present, so the registry's claims were taken).");
        }
        break;
    case S98_WHY_NOTARGET:
        pf_tx_s(t, " The disk holding ");
        pf_tx_s(t, l);
        pf_tx_s(t, " (");
        pf_tx_s(t, w->id);
        pf_tx_s(t, ") is on no adapter that answers as Stor98 (ASPI adapters: ");
        pf_tx_u(t, (pf_u64)(w->n < 0 ? 0 : w->n));
        pf_tx_s(t, ").");
        break;
    case S98_WHY_NOMATCH:
        pf_tx_s(t, " Stor98 answers on ");
        pf_tx_u(t, (pf_u64)(w->n < 0 ? 0 : w->n));
        if (w->models < w->n) {
            /* a target whose model was not read may be this disk */
            pf_tx_s(t, " targets, but the model of ");
            pf_tx_u(t, (pf_u64)(w->n - (w->models < 0 ? 0 : w->models)));
            pf_tx_s(t, " of them could not be read, so whether one is the disk holding ");
            pf_tx_s(t, l);
            pf_tx_s(t, " (");
            pf_tx_s(t, w->id);
            pf_tx_s(t, ") is not known.");
            break;
        }
        pf_tx_s(t, " targets (models read: ");
        pf_tx_u(t, (pf_u64)(w->models < 0 ? 0 : w->models));
        pf_tx_s(t, "), but none is the disk holding ");
        pf_tx_s(t, l);
        pf_tx_s(t, " (");
        pf_tx_s(t, w->id);
        pf_tx_s(t, ").");
        break;
    default:
        break;
    }
}

void s98_state_sentence(PF_TEXT *t, int state, int matches, int ids, const S98_WHY *w)
{
    switch (state) {
    case S98_ST_OK:
        break;
    case S98_ST_ADMIN:
        pf_tx_s(t, "This disk is driven by Stor98. Reading its counters needs administrator rights.");
        break;
    case S98_ST_NOASPI:
        pf_tx_s(t, "Windows' ASPI layer (WNASPI32.DLL) is not here, so Stor98's counters cannot be read.");
        break;
    case S98_ST_AMBIG:
        pf_tx_s(t, "This disk's Stor98 adapter could not be told apart from another one (Stor98 targets of its model: ");
        pf_tx_u(t, (pf_u64)(matches < 0 ? 0 : matches));
        pf_tx_s(t, "; present disks of its id: ");
        pf_tx_u(t, (pf_u64)(ids < 0 ? 0 : ids));
        pf_tx_s(t, ").");
        break;
    case S98_ST_DEAD:
        pf_tx_s(t, "Stor98 did not answer in time; its counters are not read for the rest of this run.");
        break;
    case S98_ST_LETTER:
        pf_tx_s(t, "Windows' registry gives this drive letter to ");
        pf_tx_u(t, (pf_u64)(ids < 0 ? 0 : ids));
        pf_tx_s(t, " disks and does not say which is present, so Stor98's counters cannot be matched to it.");
        break;
    default:
        pf_tx_s(t, "Stor98's counters are not reported by this disk's driver: it is not Stor98, "
                   "or a Stor98 without the counter answer.");
        if (w != NULL) {
            s98_why_sentence(t, w);
        }
        break;
    }
}

void s98_report_line(PF_TEXT *t, int state, int matches, int ids, const S98_WHY *w)
{
    pf_tx_s(t, "Stor98: ");
    if (state == S98_ST_OK) {
        pf_tx_s(t, "Stor98 drives this disk, but no test's counters were taken.");
    } else {
        s98_state_sentence(t, state, matches, ids, w);
    }
    pf_tx_nl(t);
}
