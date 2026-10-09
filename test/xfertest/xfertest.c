/*
 * xfertest.c - XFERTEST.EXE, the transfer-size test client (roadmap-hcd.md
 * tasks 36.21, 36.22 and 36.26). A Win32 console program, built with
 * MSVC 6.0 (build.cmd), that runs unchanged on Windows 98 SE, ME, 2000, XP,
 * Vista and 7, the 64-bit ones under WOW64.
 *
 * It moves data in requests of a size it is told, sequentially, from or to
 * a file or a raw disk, times them, and prints the rate:
 *
 *   XFERTEST READ   path blockKB totalMB [options]
 *   XFERTEST WRITE  path blockKB totalMB [options]   writes the pattern
 *   XFERTEST VERIFY path blockKB totalMB [options]   reads and checks it
 *
 *   path      a file, or on Windows 2000 and later a raw disk such as
 *             \\.\PhysicalDrive1. A raw disk is read and written without
 *             the file system, so each request reaches the disk class driver
 *             as one transfer, which it sends as SRBs of at most the
 *             adapter's MaximumTransferLength: this is how 36.22's sized
 *             SRBs are sent. WRITE to a raw disk destroys what is on it.
 *   blockKB   the request size, 1 to 8192 KiB.
 *   totalMB   how much to move, 1 to 4095 MiB.
 *   -skew N   place the buffer N bytes past a page boundary (0 to 3584, a
 *             multiple of 512 for a raw disk): an unaligned buffer.
 *   -at N     start N MiB into the file or disk.
 *   -nobuf    open a file with FILE_FLAG_NO_BUFFERING (NT only; a raw disk
 *             is always unbuffered).
 *   -seed N   the pattern's seed (WRITE and VERIFY), default 1.
 *   -reps N   do it N times (1 to 100) and print each and the mean.
 *
 * The pattern: every 512-byte sector holds its sector number from the start
 * of the run and the seed in its first two words, and a byte sequence
 * derived from both after them, so a sector in the wrong place, a stale
 * sector and a torn one all fail VERIFY.
 *
 * Output, one line per repetition and a summary, ASCII, so a log is
 * readable on every target:
 *   XFERTEST READ rep 1: 104857600 bytes in 1234 ms, 81.0 MB/s
 *   XFERTEST READ mean: 81.0 MB/s over 3 reps (block 1024 KiB, skew 0)
 * MB is 10^6 bytes, as the storage benchmarks report it. Exit 0 on success,
 * 1 on an I/O or verify failure, 2 on a usage error.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_READ     0
#define OP_WRITE    1
#define OP_VERIFY   2

static void usage(void)
{
    printf("usage: XFERTEST READ|WRITE|VERIFY path blockKB totalMB "
           "[-skew N] [-at MB] [-nobuf] [-seed N] [-reps N]\n"
           "  path: a file, or \\\\.\\PhysicalDriveN on Windows 2000 and "
           "later (WRITE destroys it)\n");
}

static int parse_ul(const char *s, unsigned long *out)
{
    char *end;
    unsigned long v;

    if (s == NULL || *s == 0) {
        return 0;
    }
    v = strtoul(s, &end, 0);
    if (*end != 0) {
        return 0;
    }
    *out = v;
    return 1;
}

static unsigned long pattern_word(unsigned long sector, unsigned long seed,
                                  unsigned long i)
{
    unsigned long x;

    x = sector * 2654435761UL + seed * 40503UL + i * 2246822519UL;
    x ^= x >> 15;
    x *= 2654435761UL;
    x ^= x >> 13;
    return x;
}

static void fill_sector(unsigned long *w, unsigned long sector,
                        unsigned long seed)
{
    unsigned long i;

    w[0] = sector;
    w[1] = seed;
    for (i = 2; i < 128; i++) {
        w[i] = pattern_word(sector, seed, i);
    }
}

/* 0 when the sector holds its pattern; else the first word that differs,
 * plus 1. */
static unsigned long check_sector(const unsigned long *w,
                                  unsigned long sector, unsigned long seed)
{
    unsigned long i;

    if (w[0] != sector) {
        return 1;
    }
    if (w[1] != seed) {
        return 2;
    }
    for (i = 2; i < 128; i++) {
        if (w[i] != pattern_word(sector, seed, i)) {
            return i + 1;
        }
    }
    return 0;
}

static int is_nt(void)
{
    OSVERSIONINFOA v;

    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    if (!GetVersionExA(&v)) {
        return 0;
    }
    return v.dwPlatformId == VER_PLATFORM_WIN32_NT;
}

/* Seek to `mb` MiB with the 64-bit SetFilePointer form 98 also has. */
static int seek_mb(HANDLE h, unsigned long mb)
{
    LONG lo;
    LONG hi;
    DWORD r;

    lo = (LONG)((mb & 0xFFFUL) << 20);
    hi = (LONG)(mb >> 12);
    r = SetFilePointer(h, lo, &hi, FILE_BEGIN);
    if (r == 0xFFFFFFFFUL && GetLastError() != NO_ERROR) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    unsigned long op;
    const char *path;
    unsigned long blockKB;
    unsigned long totalMB;
    unsigned long skew;
    unsigned long at;
    unsigned long seed;
    unsigned long reps;
    unsigned long rep;
    unsigned long block;
    unsigned long blocks;
    unsigned long b;
    unsigned long s;
    unsigned long sectors;
    unsigned long bad;
    unsigned long sectorBase;
    int nobuf;
    int i;
    int raw;
    unsigned char *mem;
    unsigned char *buf;
    HANDLE h;
    DWORD flags;
    DWORD access;
    DWORD done;
    LARGE_INTEGER freq;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    double ms;
    double rate;
    double sum;
    const char *name;
    int failed;

    if (argc < 5) {
        usage();
        return 2;
    }
    if (_stricmp(argv[1], "READ") == 0) {
        op = OP_READ;
        name = "READ";
    } else if (_stricmp(argv[1], "WRITE") == 0) {
        op = OP_WRITE;
        name = "WRITE";
    } else if (_stricmp(argv[1], "VERIFY") == 0) {
        op = OP_VERIFY;
        name = "VERIFY";
    } else {
        usage();
        return 2;
    }
    path = argv[2];
    if (!parse_ul(argv[3], &blockKB) || blockKB < 1 || blockKB > 8192 ||
        !parse_ul(argv[4], &totalMB) || totalMB < 1 || totalMB > 4095) {
        usage();
        return 2;
    }
    skew = 0;
    at = 0;
    seed = 1;
    reps = 1;
    nobuf = 0;
    for (i = 5; i < argc; i++) {
        if (strcmp(argv[i], "-skew") == 0 && i + 1 < argc) {
            if (!parse_ul(argv[++i], &skew) || skew > 3584) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "-at") == 0 && i + 1 < argc) {
            if (!parse_ul(argv[++i], &at) || at > 1048575UL) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "-seed") == 0 && i + 1 < argc) {
            if (!parse_ul(argv[++i], &seed)) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "-reps") == 0 && i + 1 < argc) {
            if (!parse_ul(argv[++i], &reps) || reps < 1 || reps > 100) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "-nobuf") == 0) {
            nobuf = 1;
        } else {
            usage();
            return 2;
        }
    }
    raw = strncmp(path, "\\\\.\\", 4) == 0;
    if ((raw || nobuf) && (skew % 512) != 0) {
        printf("XFERTEST: -skew must be a multiple of 512 for unbuffered "
               "I/O\n");
        return 2;
    }
    if (nobuf && !is_nt()) {
        printf("XFERTEST: -nobuf is for Windows 2000 and later\n");
        return 2;
    }
    block = blockKB * 1024UL;
    blocks = (totalMB * 1024UL) / blockKB;
    if (blocks == 0) {
        blocks = 1;
    }
    sectors = block / 512UL;

    mem = (unsigned char *)VirtualAlloc(NULL, block + 8192UL,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE);
    if (mem == NULL) {
        printf("XFERTEST: no memory for a %lu-byte buffer\n", block);
        return 1;
    }
    buf = mem + skew;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        printf("XFERTEST: no performance counter\n");
        return 1;
    }

    access = (op == OP_WRITE) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    flags = FILE_ATTRIBUTE_NORMAL;
    if (nobuf) {
        flags |= FILE_FLAG_NO_BUFFERING;
    }
    if (op == OP_WRITE) {
        flags |= FILE_FLAG_WRITE_THROUGH;
    }
    h = CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    (op == OP_WRITE && !raw) ? OPEN_ALWAYS : OPEN_EXISTING,
                    flags, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("XFERTEST: cannot open %s (error %lu)\n", path,
               (unsigned long)GetLastError());
        return 1;
    }

    failed = 0;
    sum = 0.0;
    for (rep = 1; rep <= reps && !failed; rep++) {
        if (!seek_mb(h, at)) {
            printf("XFERTEST: cannot seek to %lu MiB (error %lu)\n", at,
                   (unsigned long)GetLastError());
            failed = 1;
            break;
        }
        bad = 0;
        QueryPerformanceCounter(&t0);
        for (b = 0; b < blocks; b++) {
            sectorBase = b * sectors;
            if (op == OP_WRITE) {
                for (s = 0; s < sectors; s++) {
                    fill_sector((unsigned long *)(buf + s * 512UL),
                                sectorBase + s, seed);
                }
                if (!WriteFile(h, buf, block, &done, NULL) || done != block) {
                    printf("XFERTEST: write of block %lu failed (error "
                           "%lu, %lu bytes)\n", b,
                           (unsigned long)GetLastError(),
                           (unsigned long)done);
                    failed = 1;
                    break;
                }
            } else {
                if (!ReadFile(h, buf, block, &done, NULL) || done != block) {
                    printf("XFERTEST: read of block %lu failed (error %lu, "
                           "%lu bytes)\n", b, (unsigned long)GetLastError(),
                           (unsigned long)done);
                    failed = 1;
                    break;
                }
                if (op == OP_VERIFY) {
                    for (s = 0; s < sectors; s++) {
                        unsigned long w;

                        w = check_sector((unsigned long *)(buf + s * 512UL),
                                         sectorBase + s, seed);
                        if (w != 0) {
                            if (bad < 8) {
                                printf("XFERTEST: sector %lu of the run "
                                       "differs at word %lu (holds sector "
                                       "%lu, seed %lu)\n",
                                       sectorBase + s, w - 1,
                                       ((unsigned long *)(buf +
                                                          s * 512UL))[0],
                                       ((unsigned long *)(buf +
                                                          s * 512UL))[1]);
                            }
                            bad++;
                        }
                    }
                }
            }
        }
        if (op == OP_WRITE && !failed && !FlushFileBuffers(h) && !raw) {
            printf("XFERTEST: flush failed (error %lu)\n",
                   (unsigned long)GetLastError());
            failed = 1;
        }
        QueryPerformanceCounter(&t1);
        if (failed) {
            break;
        }
        ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 /
             (double)freq.QuadPart;
        rate = (ms > 0.0) ? ((double)blocks * (double)block / 1000.0) / ms
                          : 0.0;
        sum += rate;
        printf("XFERTEST %s rep %lu: %.0f bytes in %.0f ms, %.1f MB/s",
               name, rep, (double)blocks * (double)block, ms, rate);
        if (op == OP_VERIFY) {
            printf(", %lu sectors bad", bad);
        }
        printf("\n");
        if (bad != 0) {
            failed = 1;
        }
    }
    CloseHandle(h);
    VirtualFree(mem, 0, MEM_RELEASE);
    if (failed) {
        printf("XFERTEST %s FAILED\n", name);
        return 1;
    }
    printf("XFERTEST %s mean: %.1f MB/s over %lu reps (block %lu KiB, skew "
           "%lu, at %lu MiB%s)\n", name, sum / (double)reps, reps, blockKB,
           skew, at, raw ? ", raw" : (nobuf ? ", unbuffered" : ""));
    return 0;
}
