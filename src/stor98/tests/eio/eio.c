/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026, Yeo Kheng Meng
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * eio - concurrent data-verify workload for the request engine (PLAN 0.2,
 * 0.3, 0.4; TESTING "Basic I/O", "DMA bounds").
 *
 *   eio <file> <sizeMB> [qd] [passes] [seed] [maxkb]
 *
 * Opens <file> unbuffered, overlapped and write-through (NT only), sizes it,
 * then per pass: writes the whole file in random-length chunks (1 sector up
 * to maxkb, default 192 KiB) with qd requests in flight, every sector
 * stamped with (pass, sector, seed); then reads it back in different random
 * chunks, again qd at a time, and checks every sector. Each request uses a
 * buffer at a different 512-byte offset inside its page, so PRP entry 1
 * starts mid-page and multi-page requests need PRP lists. A final pass
 * re-reads everything. Every request must complete exactly once with the
 * right byte count; any error, short transfer, mismatch or a request
 * outstanding for more than 120 s (timed from its own issue, so a request
 * stuck while others keep completing is still caught) is reported, and the
 * exit code is the
 * number of problems (0 = pass), capped at 250 so that it never wraps to 0;
 * 251-255 are setup failures. Offsets are 64-bit: sizeMB up to 2097151
 * (just under 2 TiB, 2^32 sectors) reaches every byte offset, beyond 2 GiB
 * and 4 GiB included (TESTING "Large disk"). Builds with MSVC 6.0:
 * cl /O2 eio.c
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXQD   32
#define SECT    512
#define MAGIC   0x4F49454EUL    /* "NEIO" */
#define REQ_LIMIT_MS 120000UL   /* per-request completion limit, from its issue */
#define MAX_SIZE_MB 0x1FFFFFUL  /* sectors still fit in 32 bits */
#define EXIT_CAP    250         /* problem count exit codes stop here (251-255: setup) */

static DWORD t0;
static unsigned long problems;
static unsigned long rng;

static unsigned long rnd(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 8) & 0xFFFFFF;
}

static void stamp(unsigned char *p, unsigned long pass, unsigned long sect, unsigned long seed)
{
    unsigned long *w = (unsigned long *)p;
    int i;

    w[0] = MAGIC;
    w[1] = pass;
    w[2] = sect;
    w[3] = seed;
    for (i = 4; i < SECT / 4; i++) {
        w[i] = (sect * 2654435761UL) ^ (pass << 24) ^ (unsigned long)i ^ seed;
    }
}

static int check(const unsigned char *p, unsigned long pass, unsigned long sect, unsigned long seed)
{
    unsigned char ref[SECT];

    stamp(ref, pass, sect, seed);
    return memcmp(ref, p, SECT) == 0;
}

typedef struct {
    OVERLAPPED ov;
    HANDLE ev;
    unsigned char *mem;     /* allocation */
    unsigned char *buf;     /* mem + page offset */
    unsigned long sect;     /* first sector */
    unsigned long count;    /* sectors */
    int busy;
    int write;
    DWORD issued;
} SLOT;

static SLOT slot[MAXQD];
static HANDLE h;
static int qd = 8;
static unsigned long maxSect = 384;
static unsigned long issuedCnt, doneCnt;

static void issue(int i, int write, unsigned long sect, unsigned long count, unsigned long pass, unsigned long seed)
{
    SLOT *s = &slot[i];
    unsigned long k;
    BOOL ok;
    DWORD n;
    ULONGLONG off = (ULONGLONG)sect * SECT;

    s->sect = sect;
    s->count = count;
    s->write = write;
    s->issued = GetTickCount();
    if (write) {
        for (k = 0; k < count; k++) {
            stamp(s->buf + k * SECT, pass, sect + k, seed);
        }
    } else {
        memset(s->buf, 0xA5, count * SECT);
    }
    ZeroMemory(&s->ov, sizeof(s->ov));
    s->ov.hEvent = s->ev;
    ResetEvent(s->ev);
    s->ov.Offset = (DWORD)off;
    s->ov.OffsetHigh = (DWORD)(off >> 32);
    if (write) {
        ok = WriteFile(h, s->buf, count * SECT, &n, &s->ov);
    } else {
        ok = ReadFile(h, s->buf, count * SECT, &n, &s->ov);
    }
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        printf("%7.3f issue %s sect %lu count %lu failed: %lu\n", (GetTickCount() - t0) / 1000.0,
               write ? "W" : "R", sect, count, GetLastError());
        problems++;
        return;
    }
    s->busy = 1;
    issuedCnt++;
}

static void finish(int i, unsigned long pass, unsigned long seed)
{
    SLOT *s = &slot[i];
    DWORD n = 0;
    unsigned long k;

    if (GetTickCount() - s->issued >= REQ_LIMIT_MS) {
        printf("%7.3f LATE: %s sect %lu count %lu took %lu ms\n", (GetTickCount() - t0) / 1000.0,
               s->write ? "W" : "R", s->sect, s->count, (unsigned long)(GetTickCount() - s->issued));
        problems++;
    }
    if (!GetOverlappedResult(h, &s->ov, &n, FALSE)) {
        printf("%7.3f %s sect %lu count %lu error %lu\n", (GetTickCount() - t0) / 1000.0,
               s->write ? "W" : "R", s->sect, s->count, GetLastError());
        problems++;
    } else if (n != s->count * SECT) {
        printf("%7.3f %s sect %lu count %lu short: %lu bytes\n", (GetTickCount() - t0) / 1000.0,
               s->write ? "W" : "R", s->sect, s->count, n);
        problems++;
    } else if (!s->write) {
        for (k = 0; k < s->count; k++) {
            if (!check(s->buf + k * SECT, pass, s->sect + k, seed)) {
                const unsigned long *w = (const unsigned long *)(s->buf + k * SECT);
                printf("%7.3f MISMATCH sect %lu (req %lu+%lu): %08lx %lu %lu %08lx\n",
                       (GetTickCount() - t0) / 1000.0, s->sect + k, s->sect, s->count,
                       w[0], w[1], w[2], w[3]);
                problems++;
                break;
            }
        }
    }
    s->busy = 0;
    doneCnt++;
}

/* Reports every outstanding request that has reached its own limit (timed
 * from its issue). Checked on every loop iteration, before more work is
 * issued and before another completion is processed, whatever the wait
 * returned: a hung request is caught at its bound even while other slots
 * keep completing (and being replenished) at once. */
static int overdue(void)
{
    DWORD now = GetTickCount();
    int i, late = 0;

    for (i = 0; i < qd; i++) {
        SLOT *s = &slot[i];

        if (s->busy && now - s->issued >= REQ_LIMIT_MS) {
            printf("%7.3f HANG: %s sect %lu count %lu outstanding for %lu ms\n",
                   (now - t0) / 1000.0, s->write ? "W" : "R", s->sect, s->count,
                   (unsigned long)(now - s->issued));
            problems++;
            late = 1;
        }
    }
    return late;
}

/* Runs one phase over [0, total) sectors with qd requests in flight. */
static void phase(int write, unsigned long total, unsigned long pass, unsigned long seed)
{
    unsigned long next = 0;
    HANDLE evs[MAXQD];
    int map[MAXQD];
    int i, n;
    DWORD r, wait, now;

    for (;;) {
        if (overdue()) {
            return;
        }
        for (i = 0; i < qd && next < total; i++) {
            if (!slot[i].busy) {
                unsigned long c = 1 + rnd() % maxSect;

                if (c > total - next) {
                    c = total - next;
                }
                issue(i, write, next, c, pass, seed);
                next += c;
            }
        }
        n = 0;
        for (i = 0; i < qd; i++) {
            if (slot[i].busy) {
                evs[n] = slot[i].ev;
                map[n] = i;
                n++;
            }
        }
        if (n == 0) {
            break;
        }
        /* Each request has its own 120 s limit, counted from its issue:
         * wait only until the oldest outstanding one reaches it. */
        wait = REQ_LIMIT_MS;
        now = GetTickCount();
        for (i = 0; i < n; i++) {
            DWORD age = now - slot[map[i]].issued;

            if (age >= REQ_LIMIT_MS) {
                wait = 0;
            } else if (REQ_LIMIT_MS - age < wait) {
                wait = REQ_LIMIT_MS - age;
            }
        }
        r = WaitForMultipleObjects(n, evs, FALSE, wait);
        if (r == WAIT_TIMEOUT) {
            continue;       /* the check at the top reports it */
        }
        if (overdue()) {
            return;
        }
        if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + (DWORD)n) {
            finish(map[r - WAIT_OBJECT_0], pass, seed);
        } else {
            printf("wait failed %lu\n", GetLastError());
            problems++;
            return;
        }
    }
}

int main(int argc, char **argv)
{
    unsigned long sizeMB, passes = 2, seed, total, p, bad;
    int i;
    ULONGLONG size;
    LONG sizeHigh;
    DWORD sizeLow;

    if (argc < 3) {
        printf("usage: eio <file> <sizeMB> [qd] [passes] [seed] [maxkb]\n");
        return 255;
    }
    sizeMB = strtoul(argv[2], NULL, 0);
    if (argc > 3) qd = atoi(argv[3]);
    if (argc > 4) passes = strtoul(argv[4], NULL, 0);
    seed = (argc > 5) ? strtoul(argv[5], NULL, 0) : GetTickCount();
    if (argc > 6) maxSect = strtoul(argv[6], NULL, 0) * 2;
    if (qd < 1 || qd > MAXQD || sizeMB < 1 || sizeMB > MAX_SIZE_MB || maxSect < 1) {
        printf("bad arguments\n");
        return 255;
    }
    rng = seed;
    total = sizeMB * 2048;
    t0 = GetTickCount();

    h = CreateFile(argv[1], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                   FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", argv[1], GetLastError());
        return 254;
    }
    /* 64-bit size: the high part through lpDistanceToMoveHigh, the result
     * checked (0xFFFFFFFF is a valid low part, so GetLastError decides) */
    size = (ULONGLONG)total * SECT;
    sizeHigh = (LONG)(size >> 32);
    SetLastError(NO_ERROR);
    sizeLow = SetFilePointer(h, (LONG)(DWORD)size, &sizeHigh, FILE_BEGIN);
    if (sizeLow == 0xFFFFFFFFUL && GetLastError() != NO_ERROR) {
        printf("seek to %lu MB failed: %lu\n", sizeMB, GetLastError());
        return 253;
    }
    if (!SetEndOfFile(h)) {
        printf("size failed: %lu\n", GetLastError());
        return 253;
    }
    for (i = 0; i < qd; i++) {
        slot[i].ev = CreateEvent(NULL, TRUE, FALSE, NULL);
        slot[i].mem = (unsigned char *)VirtualAlloc(NULL, maxSect * SECT + 8192, MEM_COMMIT, PAGE_READWRITE);
        if (slot[i].mem == NULL || slot[i].ev == NULL) {
            printf("alloc failed\n");
            return 252;
        }
        /* a different sector offset inside the page per slot */
        slot[i].buf = slot[i].mem + ((i * SECT) % 4096);
    }
    printf("eio %s %luMB qd %d passes %lu seed %lu maxkb %lu\n", argv[1], sizeMB, qd, passes, seed, maxSect / 2);
    for (p = 1; p <= passes && problems == 0; p++) {
        phase(1, total, p, seed);
        printf("%7.3f pass %lu written (%lu issued, %lu done, %lu problems)\n",
               (GetTickCount() - t0) / 1000.0, p, issuedCnt, doneCnt, problems);
        phase(0, total, p, seed);
        printf("%7.3f pass %lu verified (%lu issued, %lu done, %lu problems)\n",
               (GetTickCount() - t0) / 1000.0, p, issuedCnt, doneCnt, problems);
    }
    if (problems == 0) {
        phase(0, total, passes, seed);
    }
    printf("%7.3f %s: %lu requests issued, %lu completed, %lu problems\n", (GetTickCount() - t0) / 1000.0,
           (problems == 0 && issuedCnt == doneCnt) ? "PASS" : "FAIL", issuedCnt, doneCnt, problems);
    CloseHandle(h);
    bad = problems + (issuedCnt != doneCnt);
    if (bad < problems || bad > EXIT_CAP) {
        bad = EXIT_CAP;
    }
    return (int)bad;
}
