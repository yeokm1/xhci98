/*
 * ptcancel.c - PTCANCEL.EXE, the pass-through cancel and removal client
 * (roadmap-usb4.md task 45.52; design record 13 section 16.5, the queued
 * request's cancel routine). A Win32 console program, built with MSVC 6.0
 * (build.cmd), for Windows 2000, XP, Vista and 7. In no package.
 *
 * It opens a disk for overlapped I/O and sends it many
 * IOCTL_SCSI_PASS_THROUGH READ(10)s at once, each of 64 KiB from LBA 0,
 * so that on a drive that runs one command at a time most of them wait in
 * the UAS driver's queue. Then, by mode:
 *
 *   PTCANCEL disk count cancel   CancelIo at once, then wait for each
 *   PTCANCEL disk count exit     end the process with them outstanding
 *   PTCANCEL disk count wait     wait up to 120 s for each (unplug the
 *                                drive meanwhile for the removal leg)
 *
 *   disk   \\.\PhysicalDriveN; count 1 to 64. Administrator rights.
 *
 * Before cancelling or exiting it prints how many of the requests are
 * still pending (HasOverlappedIoCompleted), so a leg reads whether they
 * were queued at all: a stack that serves pass-through synchronously
 * finishes each inside DeviceIoControl. For cancel and wait it prints each
 * request's outcome (the call's error, 995 for cancelled, and the SCSI
 * status) and the totals; nothing is written to the disk. Exit 0 when it
 * ran, 2 on a usage, setup or open error. The mode and count are checked,
 * and every buffer and event made, before the disk is opened; a buffer is
 * released only once its request is known to have finished.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PT_IOCTL_SCSI_PASS_THROUGH 0x0004D004UL
#define PT_MAX                     64
#define PT_LEN                     65536

#pragma pack(push, 1)
typedef struct {
    USHORT Length;
    UCHAR  ScsiStatus;
    UCHAR  PathId;
    UCHAR  TargetId;
    UCHAR  Lun;
    UCHAR  CdbLength;
    UCHAR  SenseInfoLength;
    UCHAR  DataIn;
    UCHAR  Pad[3];
    ULONG  DataTransferLength;
    ULONG  TimeOutValue;
    ULONG  DataBufferOffset;     /* ULONG_PTR: a 32-bit program */
    ULONG  SenseInfoOffset;
    UCHAR  Cdb[16];
} PT_SPT;
#pragma pack(pop)

typedef struct {
    PT_SPT spt;
    UCHAR  sense[32];
    UCHAR  data[PT_LEN];
} PT_BUF;

static PT_BUF *g_buf[PT_MAX];
static OVERLAPPED g_ov[PT_MAX];
static DWORD g_sent[PT_MAX];    /* 0: pending or done; else the error */
static int g_done[PT_MAX];      /* 1: finished, its buffer may be freed */

static void ptFree(int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (g_ov[i].hEvent != NULL && g_done[i]) {
            CloseHandle(g_ov[i].hEvent);
            g_ov[i].hEvent = NULL;
        }
        if (g_buf[i] != NULL && g_done[i]) {
            VirtualFree(g_buf[i], 0, MEM_RELEASE);
            g_buf[i] = NULL;
        }
    }
}

static void usage(void)
{
    printf("usage: PTCANCEL \\\\.\\PhysicalDriveN count cancel|exit|wait\n");
}

int main(int argc, char **argv)
{
    HANDLE h;
    int count;
    int i;
    int ok;
    int cancelled;
    int other;
    DWORD got;
    DWORD err;
    DWORD w;
    const char *mode;
    char *end;
    long n;

    if (argc != 4) {
        usage();
        return 2;
    }
    n = strtol(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || n < 1 || n > PT_MAX) {
        usage();
        return 2;
    }
    count = (int)n;
    mode = argv[3];
    if (strcmp(mode, "cancel") != 0 && strcmp(mode, "exit") != 0 &&
        strcmp(mode, "wait") != 0) {
        printf("PTCANCEL unknown mode '%s'\n", mode);
        usage();
        return 2;
    }
    for (i = 0; i < count; i++) {
        g_done[i] = 1;      /* nothing submitted yet */
        memset(&g_ov[i], 0, sizeof(OVERLAPPED));
        g_buf[i] = (PT_BUF *)VirtualAlloc(NULL, sizeof(PT_BUF), MEM_COMMIT,
                                          PAGE_READWRITE);
        if (g_buf[i] == NULL) {
            printf("PTCANCEL cannot allocate buffer %d: error %lu\n", i,
                   (unsigned long)GetLastError());
            ptFree(i);
            return 2;
        }
        g_ov[i].hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (g_ov[i].hEvent == NULL) {
            printf("PTCANCEL cannot create event %d: error %lu\n", i,
                   (unsigned long)GetLastError());
            ptFree(i + 1);
            return 2;
        }
    }
    h = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("PTCANCEL cannot open %s: error %lu\n", argv[1],
               (unsigned long)GetLastError());
        ptFree(count);
        return 2;
    }
    printf("PTCANCEL %s %d %s\n", argv[1], count, mode);
    for (i = 0; i < count; i++) {
        PT_BUF *b;

        b = g_buf[i];
        b->spt.Length = (USHORT)sizeof(PT_SPT);
        b->spt.CdbLength = 10;
        b->spt.SenseInfoLength = sizeof(b->sense);
        b->spt.DataIn = 1;
        b->spt.DataTransferLength = PT_LEN;
        b->spt.TimeOutValue = 30;
        b->spt.DataBufferOffset = (ULONG)((PUCHAR)b->data - (PUCHAR)b);
        b->spt.SenseInfoOffset = (ULONG)((PUCHAR)b->sense - (PUCHAR)b);
        b->spt.Cdb[0] = 0x28;                       /* READ(10), LBA 0 */
        b->spt.Cdb[7] = (UCHAR)((PT_LEN / 512) >> 8);
        b->spt.Cdb[8] = (UCHAR)((PT_LEN / 512) & 0xFF);
        g_sent[i] = 0;
        g_done[i] = 0;
        if (!DeviceIoControl(h, PT_IOCTL_SCSI_PASS_THROUGH, b, sizeof(PT_BUF),
                             b, sizeof(PT_BUF), &got, &g_ov[i])) {
            err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                /* refused at once: no completion will signal it */
                g_sent[i] = err;
                g_done[i] = 1;
                printf("PTCANCEL %d: sent error %lu\n", i, (unsigned long)err);
            }
        }
    }
    ok = 0;
    for (i = 0; i < count; i++) {
        if (g_sent[i] == 0 && !HasOverlappedIoCompleted(&g_ov[i])) {
            ok++;
        }
    }
    printf("PTCANCEL pending now: %d of %d\n", ok, count);
    if (strcmp(mode, "exit") == 0) {
        printf("PTCANCEL exit\n");
        ExitProcess(0);
    }
    if (strcmp(mode, "cancel") == 0) {
        printf("PTCANCEL CancelIo: %d\n", CancelIo(h) ? 1 : 0);
    }
    ok = 0;
    cancelled = 0;
    other = 0;
    for (i = 0; i < count; i++) {
        if (g_sent[i] != 0) {
            printf("PTCANCEL %d: refused at once, error %lu\n", i,
                   (unsigned long)g_sent[i]);
            other++;
            continue;
        }
        w = WaitForSingleObject(g_ov[i].hEvent, 120000);
        if (w != WAIT_OBJECT_0) {
            /* still outstanding: its buffer and event stay until exit */
            printf("PTCANCEL %d: not done after 120 s\n", i);
            other++;
            continue;
        }
        g_done[i] = 1;
        if (GetOverlappedResult(h, &g_ov[i], &got, FALSE)) {
            printf("PTCANCEL %d: done scsi=%02X len=%lu\n", i,
                   g_buf[i]->spt.ScsiStatus,
                   (unsigned long)g_buf[i]->spt.DataTransferLength);
            ok++;
        } else {
            err = GetLastError();
            printf("PTCANCEL %d: error %lu\n", i, (unsigned long)err);
            if (err == ERROR_OPERATION_ABORTED) {
                cancelled++;
            } else {
                other++;
            }
        }
    }
    printf("PTCANCEL totals: done %d cancelled %d other %d\n", ok, cancelled,
           other);
    ptFree(count);
    CloseHandle(h);
    return 0;
}
