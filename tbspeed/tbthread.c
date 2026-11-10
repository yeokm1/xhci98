/*
 * tbthread.c - TBTHREAD.EXE, a developer diagnostic (never in a package):
 * which thread creations succeed on this computer, and why the others
 * fail. Written for a Windows 98 SE machine with much more memory than
 * 512 MB, where iperf's second thread cannot be created.
 *
 *   TBTHREAD            the report, on the console
 *   TBTHREAD > T.TXT    the report saved
 *
 * It prints the system's memory figures, then tries, in this order:
 * CreateThread with stack sizes from 0 (the image's default) to 64 MB and
 * the flags 0, CREATE_SUSPENDED and STACK_SIZE_PARAM_IS_A_RESERVATION (an
 * NT flag); _beginthreadex and _beginthread likewise; VirtualAlloc reserve
 * and commit of the same sizes; the largest block that can be reserved;
 * and how many 64 KB threads can exist at once. Each failure prints
 * GetLastError and the system's text for it. It changes nothing.
 */

#include <windows.h>
#include <process.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef STACK_SIZE_PARAM_IS_A_RESERVATION
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#endif

#define MAX_COUNT   512

static volatile LONG g_ran;
static volatile LONG g_quit;

static DWORD WINAPI th_api(LPVOID p)
{
    (void)p;
    InterlockedIncrement((LONG *)&g_ran);
    return 0;
}

static unsigned __stdcall th_crt(void *p)
{
    (void)p;
    InterlockedIncrement((LONG *)&g_ran);
    return 0;
}

static void __cdecl th_old(void *p)
{
    (void)p;
    InterlockedIncrement((LONG *)&g_ran);
}

static DWORD WINAPI th_park(LPVOID p)
{
    (void)p;
    while (!g_quit)
        Sleep(50);
    return 0;
}

static void errtext(DWORD e, char *buf, size_t len)
{
    DWORD n;
    buf[0] = '\0';
    n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, e, 0, buf, (DWORD)len, NULL);
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' '))
        buf[--n] = '\0';
}

static void fail_line(DWORD e)
{
    char t[160];
    errtext(e, t, sizeof(t));
    printf("FAILED, error %lu%s%s\n", (unsigned long)e, t[0] ? ": " : "", t);
}

static const char *kb(DWORD v, char *b)
{
    if (v == 0)
        strcpy(b, "0 (default)");
    else if (v >= 1048576 && v % 1048576 == 0)
        sprintf(b, "%lu MB", (unsigned long)(v / 1048576));
    else
        sprintf(b, "%lu KB", (unsigned long)(v / 1024));
    return b;
}

/* wait for the new thread to run and end; resume it first if suspended */
static void finish(HANDLE h, int suspended)
{
    DWORD w;
    LONG before = g_ran;
    if (suspended)
        ResumeThread(h);
    w = WaitForSingleObject(h, 10000);
    if (w == WAIT_OBJECT_0)
        printf("ok, ran%s\n", g_ran != before ? "" : " (no count?)");
    else
        printf("created, but did not end within 10 s\n");
    CloseHandle(h);
}

static const DWORD sizes[] = {
    0, 4096, 65536, 262144, 1048576, 2097152, 8388608, 67108864
};
#define NSIZES (sizeof(sizes) / sizeof(sizes[0]))

/* the stack every thread gets when its size argument is 0 */
static void image_stack(void)
{
    const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt;
    if (base == NULL || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    printf("This image's default stack: reserve %lu KB, commit %lu KB\n",
           (unsigned long)(nt->OptionalHeader.SizeOfStackReserve / 1024),
           (unsigned long)(nt->OptionalHeader.SizeOfStackCommit / 1024));
}

static void memory(void)
{
    MEMORYSTATUS m;
    SYSTEM_INFO si;
    OSVERSIONINFOA v;

    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    if (GetVersionExA(&v))
        printf("Windows %lu.%lu build %lu, platform %lu %s\n",
               (unsigned long)v.dwMajorVersion, (unsigned long)v.dwMinorVersion,
               (unsigned long)(v.dwBuildNumber & 0xFFFF),
               (unsigned long)v.dwPlatformId, v.szCSDVersion);
    GetSystemInfo(&si);
    printf("Page size %lu, allocation granularity %lu, processors %lu\n",
           (unsigned long)si.dwPageSize, (unsigned long)si.dwAllocationGranularity,
           (unsigned long)si.dwNumberOfProcessors);
    printf("Application addresses %08lX to %08lX\n",
           (unsigned long)si.lpMinimumApplicationAddress,
           (unsigned long)si.lpMaximumApplicationAddress);
    memset(&m, 0, sizeof(m));
    m.dwLength = sizeof(m);
    GlobalMemoryStatus(&m);
    printf("GlobalMemoryStatus (KB): load %lu%%\n", (unsigned long)m.dwMemoryLoad);
    printf("  physical  total %10lu  free %10lu\n",
           (unsigned long)(m.dwTotalPhys / 1024), (unsigned long)(m.dwAvailPhys / 1024));
    printf("  page file total %10lu  free %10lu\n",
           (unsigned long)(m.dwTotalPageFile / 1024), (unsigned long)(m.dwAvailPageFile / 1024));
    printf("  virtual   total %10lu  free %10lu\n",
           (unsigned long)(m.dwTotalVirtual / 1024), (unsigned long)(m.dwAvailVirtual / 1024));
}

static void api_threads(void)
{
    static const DWORD flags[] = { 0, CREATE_SUSPENDED, STACK_SIZE_PARAM_IS_A_RESERVATION };
    static const char *fname[] = { "0", "CREATE_SUSPENDED", "STACK_SIZE_PARAM_IS_A_RESERVATION" };
    unsigned f, i;
    char b[32];

    printf("\nCreateThread (dwStackSize, flags):\n");
    for (f = 0; f < 3; f++) {
        for (i = 0; i < NSIZES; i++) {
            DWORD id;
            HANDLE h;
            printf("  %-12s %-34s ", kb(sizes[i], b), fname[f]);
            SetLastError(0);
            h = CreateThread(NULL, sizes[i], th_api, NULL, flags[f], &id);
            if (h == NULL)
                fail_line(GetLastError());
            else
                finish(h, flags[f] == CREATE_SUSPENDED);
        }
    }
}

static void crt_threads(void)
{
    static const unsigned flags[] = { 0, CREATE_SUSPENDED };
    static const char *fname[] = { "0", "CREATE_SUSPENDED" };
    unsigned f, i;
    char b[32];

    printf("\n_beginthreadex (stack_size, initflag):\n");
    for (f = 0; f < 2; f++) {
        for (i = 0; i < NSIZES; i++) {
            unsigned id;
            HANDLE h;
            printf("  %-12s %-34s ", kb(sizes[i], b), fname[f]);
            SetLastError(0);
            errno = 0;
            h = (HANDLE)_beginthreadex(NULL, (unsigned)sizes[i], th_crt, NULL, flags[f], &id);
            if (h == NULL || h == INVALID_HANDLE_VALUE) {
                DWORD e = GetLastError();
                printf("errno %d, ", errno);
                fail_line(e);
            } else {
                finish(h, flags[f] == CREATE_SUSPENDED);
            }
        }
    }

    printf("\n_beginthread (stack_size):\n");
    for (i = 0; i < NSIZES; i++) {
        unsigned long r;
        LONG before = g_ran;
        int k;
        printf("  %-12s %-34s ", kb(sizes[i], b), "");
        SetLastError(0);
        errno = 0;
        r = _beginthread(th_old, (unsigned)sizes[i], NULL);
        if (r == (unsigned long)-1) {
            DWORD e = GetLastError();
            printf("errno %d, ", errno);
            fail_line(e);
            continue;
        }
        for (k = 0; k < 200 && g_ran == before; k++)
            Sleep(50);
        printf(g_ran != before ? "ok, ran\n" : "created, but did not run within 10 s\n");
    }
}

static void address_space(void)
{
    unsigned i;
    char b[32];
    DWORD lo, hi;

    printf("\nVirtualAlloc reserve, then commit, then both at once:\n");
    for (i = 1; i < NSIZES; i++) {
        void *p;
        printf("  %-12s reserve ", kb(sizes[i], b));
        p = VirtualAlloc(NULL, sizes[i], MEM_RESERVE, PAGE_READWRITE);
        if (p == NULL) {
            fail_line(GetLastError());
            continue;
        }
        printf("ok at %08lX, commit ", (unsigned long)p);
        if (VirtualAlloc(p, sizes[i], MEM_COMMIT, PAGE_READWRITE) == NULL)
            fail_line(GetLastError());
        else
            printf("ok\n");
        VirtualFree(p, 0, MEM_RELEASE);
        printf("  %-12s reserve+commit ", kb(sizes[i], b));
        p = VirtualAlloc(NULL, sizes[i], MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p == NULL) {
            fail_line(GetLastError());
            continue;
        }
        printf("ok\n");
        VirtualFree(p, 0, MEM_RELEASE);
    }

    lo = 0;
    hi = 0x7FFF0000u >> 16;
    while (lo < hi) {
        DWORD mid = (lo + hi + 1) / 2;
        void *p = VirtualAlloc(NULL, mid << 16, MEM_RESERVE, PAGE_NOACCESS);
        if (p != NULL) {
            VirtualFree(p, 0, MEM_RELEASE);
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    printf("  Largest block that can be reserved: %lu KB\n", (unsigned long)(lo << 6));
}

static void count_threads(void)
{
    static HANDLE h[MAX_COUNT];
    int n, i;
    DWORD e = 0, id;

    printf("\nThreads that can exist at once, CreateThread dwStackSize 64 KB\n"
           "(the reserve is the image's default above; up to %d): ", MAX_COUNT);
    g_quit = 0;
    for (n = 0; n < MAX_COUNT; n++) {
        h[n] = CreateThread(NULL, 65536, th_park, NULL, 0, &id);
        if (h[n] == NULL) {
            e = GetLastError();
            break;
        }
    }
    printf("%d\n", n);
    if (n < MAX_COUNT) {
        printf("  stopped at: ");
        fail_line(e);
    }
    g_quit = 1;
    for (i = 0; i < n; i++) {
        WaitForSingleObject(h[i], 10000);
        CloseHandle(h[i]);
    }
}

int main(void)
{
    printf("TBTHREAD 1.0 - thread creation on this computer\n\n");
    memory();
    image_stack();
    api_threads();
    crt_threads();
    address_space();
    count_threads();
    printf("\nMemory after the tests:\n");
    memory();
    printf("\nDone.\n");
    return 0;
}
