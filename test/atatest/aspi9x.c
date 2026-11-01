/*
 * aspi9x.c - ASPI9X.EXE, the Windows 98 SE and ME pass-through probe
 * (roadmap-usb4.md task 45.52, reading R3; design record 13 section 16.10).
 * A Win32 console program, built with MSVC 6.0 (build.cmd), for 98 SE and
 * ME. In no package.
 *
 * On 98 SE and ME a program's route to a disk's commands is ASPI
 * (WNASPI32.DLL, loaded at run time; the layouts are the Windows 98 DDK's
 * block\wnaspi32 WNASPI32.H, as XHCIPERF's). It prints what ASPI shows
 * and answers, so a leg can read whether a disk behind this project's UAS
 * driver is reachable that way:
 *
 *   ASPI9X
 *
 * For each host adapter ASPI lists: its manager and identifier strings;
 * for each target 0 to 7 (LUN 0) that answers SC_GET_DEV_TYPE: the device
 * type, then, through SC_EXEC_SCSI_CMD,
 *   I  INQUIRY, 36 bytes in (vendor and product shown);
 * and for a direct-access disk (type 0) only:
 *   A1 ATA PASS-THROUGH (16) IDENTIFY DEVICE, 512 bytes in (model shown);
 *   A3 ATA PASS-THROUGH (12) SMART READ DATA, 512 bytes in (the checksum);
 *   A4 ATA PASS-THROUGH (16) SMART RETURN STATUS, no data, CK_COND.
 * Each line gives the ASPI status, the host adapter and target status, and
 * the sense key, ASC and ASCQ. Nothing is written to any disk.
 * Exit 0 when the program ran, 2 when ASPI is not there.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define SC_HA_INQUIRY       0x00
#define SC_GET_DEV_TYPE     0x01
#define SC_EXEC_SCSI_CMD    0x02
#define SC_ABORT_SRB        0x03
#define SS_PENDING          0x00
#define SS_COMP             0x01
#define SRB_DIR_IN          0x08
#define SRB_DIR_OUT         0x10
#define SRB_EVENT_NOTIFY    0x40

#pragma pack(push, 1)
typedef struct {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    BYTE HaCount, HaScsiId;
    BYTE ManagerId[16], Identifier[16], Unique[16];
    WORD Rsvd1;
} SRB_HAINQ;

typedef struct {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    BYTE Target, Lun, DeviceType, Rsvd1;
} SRB_GDEV;

typedef struct {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    BYTE Target, Lun;
    WORD Rsvd1;
    DWORD BufLen;
    BYTE *BufPointer;
    BYTE SenseLen, CdbLen, HaStat, TargStat;
    void *PostProc;
    void *Rsvd2;
    BYTE Rsvd3[16];
    BYTE Cdb[16];
    BYTE Sense[16];             /* SENSE_LEN 14, plus 2 */
} SRB_EXEC;

typedef struct {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    void *ToAbort;
} SRB_ABORT;
#pragma pack(pop)

typedef DWORD (*SENDASPI)(void *);
typedef DWORD (*GETASPIINFO)(void);

static SENDASPI g_send;
static int g_hung;              /* a request needed an abort: stop */

static void put_text(const BYTE *p, int n)
{
    int i;

    printf(" \"");
    for (i = 0; i < n; i++) {
        putchar((p[i] >= 0x20 && p[i] < 0x7F) ? p[i] : '.');
    }
    printf("\"");
}

/* IDENTIFY's model: words 27 to 46, each word's first character high. */
static void put_model(const BYTE *id)
{
    BYTE m[40];
    int i;

    for (i = 0; i < 40; i += 2) {
        m[i] = id[54 + i + 1];
        m[i + 1] = id[54 + i];
    }
    printf(" model");
    put_text(m, 40);
}

static void exec(BYTE ha, BYTE t, const char *tag, const BYTE *cdb, int cdbLen,
                 BYTE flags, DWORD len, int decode)
{
    static BYTE buf[512];
    SRB_EXEC s;
    HANDLE ev;
    DWORD rc;
    BYTE sum;
    DWORD i;

    if (g_hung) {
        return;
    }
    ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    memset(&s, 0, sizeof(s));
    memset(buf, 0, sizeof(buf));
    s.Cmd = SC_EXEC_SCSI_CMD;
    s.HaId = ha;
    s.Target = t;
    s.Lun = 0;
    s.Flags = (BYTE)(flags | SRB_EVENT_NOTIFY);
    s.BufLen = len;
    s.BufPointer = buf;
    s.SenseLen = 14;
    s.CdbLen = (BYTE)cdbLen;
    s.PostProc = (void *)ev;
    memcpy(s.Cdb, cdb, cdbLen);
    rc = g_send(&s);
    if (rc == SS_PENDING && WaitForSingleObject(ev, 10000) != WAIT_OBJECT_0) {
        SRB_ABORT ab;

        memset(&ab, 0, sizeof(ab));
        ab.Cmd = SC_ABORT_SRB;
        ab.HaId = ha;
        ab.ToAbort = &s;
        g_send(&ab);
        printf("ASPI9X %u:%u %s: no answer in 10 s, abort sent; waiting\n",
               ha, t, tag);
        /* ASPI owns s and buf until it says the request is done. */
        WaitForSingleObject(ev, INFINITE);
        CloseHandle(ev);
        printf("ASPI9X %u:%u %s: ended status=%02X\n", ha, t, tag, s.Status);
        g_hung = 1;
        return;
    }
    CloseHandle(ev);
    printf("ASPI9X %u:%u %s: status=%02X ha=%02X target=%02X", ha, t, tag,
           s.Status, s.HaStat, s.TargStat);
    if (s.TargStat == 2 && (s.Sense[0] & 0x7E) == 0x72) {
        printf(" sense=descriptor key=%X asc=%02X ascq=%02X",
               s.Sense[1] & 0x0F, s.Sense[2], s.Sense[3]);
    } else if (s.TargStat == 2) {
        printf(" sense=fixed key=%X asc=%02X ascq=%02X", s.Sense[2] & 0x0F,
               s.Sense[12], s.Sense[13]);
        if (s.Sense[12] == 0x00 && s.Sense[13] == 0x1D) {
            /* SAT's ATA registers: ERROR, STATUS, DEVICE, COUNT; LBA. */
            printf(" ata-err=%02X ata-status=%02X lba-mid=%02X lba-high=%02X",
                   s.Sense[3], s.Sense[4], s.Sense[10], s.Sense[11]);
        }
    }
    if (s.Status == SS_COMP && decode == 1) {
        put_text(buf + 8, 8);
        put_text(buf + 16, 16);
    }
    if (s.Status == SS_COMP && decode == 2) {
        put_model(buf);
    }
    if (s.Status == SS_COMP && decode == 3) {
        sum = 0;
        for (i = 0; i < 512; i++) {
            sum = (BYTE)(sum + buf[i]);
        }
        printf(" smart-rev=%u checksum=%s", buf[0] | (buf[1] << 8),
               sum == 0 ? "ok" : "BAD");
    }
    printf("\n");
}

int main(void)
{
    HMODULE dll;
    GETASPIINFO info;
    DWORD si;
    BYTE nha;
    BYTE ha;
    BYTE t;
    BYTE cdb[16];

    dll = LoadLibraryA("WNASPI32.DLL");
    if (dll == NULL) {
        printf("ASPI9X WNASPI32.DLL not loaded: error %lu\n",
               (unsigned long)GetLastError());
        return 2;
    }
    info = (GETASPIINFO)GetProcAddress(dll, "GetASPI32SupportInfo");
    g_send = (SENDASPI)GetProcAddress(dll, "SendASPI32Command");
    if (info == NULL || g_send == NULL) {
        printf("ASPI9X WNASPI32.DLL has no ASPI entry points\n");
        return 2;
    }
    si = info();
    printf("ASPI9X support info %08lX: status %02lX, %lu host adapter(s)\n",
           (unsigned long)si, (unsigned long)((si >> 8) & 0xFF),
           (unsigned long)(si & 0xFF));
    if (((si >> 8) & 0xFF) != SS_COMP) {
        return 2;
    }
    nha = (BYTE)(si & 0xFF);
    for (ha = 0; ha < nha && !g_hung; ha++) {
        SRB_HAINQ h;

        memset(&h, 0, sizeof(h));
        h.Cmd = SC_HA_INQUIRY;
        h.HaId = ha;
        g_send(&h);
        printf("ASPI9X adapter %u: status=%02X", ha, h.Status);
        put_text(h.ManagerId, 16);
        put_text(h.Identifier, 16);
        printf("\n");
        for (t = 0; t < 8; t++) {
            SRB_GDEV g;

            memset(&g, 0, sizeof(g));
            g.Cmd = SC_GET_DEV_TYPE;
            g.HaId = ha;
            g.Target = t;
            if (g_hung) {
                break;
            }
            if (g_send(&g) != SS_COMP || g.Status != SS_COMP) {
                continue;
            }
            printf("ASPI9X %u:%u device type %02X\n", ha, t, g.DeviceType);

            memset(cdb, 0, sizeof(cdb));
            cdb[0] = 0x12;
            cdb[4] = 36;
            exec(ha, t, "I INQUIRY", cdb, 6, SRB_DIR_IN, 36, 1);
            if (g.DeviceType != 0) {
                /* Only a direct-access disk gets the ATA probes: A1h is
                 * BLANK to an MMC drive. */
                continue;
            }

            memset(cdb, 0, sizeof(cdb));
            cdb[0] = 0x85;
            cdb[1] = 4 << 1;
            cdb[2] = 0x0E;
            cdb[6] = 1;
            cdb[13] = 0xA0;
            cdb[14] = 0xEC;
            exec(ha, t, "A1 ATA16 IDENTIFY", cdb, 16, SRB_DIR_IN, 512, 2);

            memset(cdb, 0, sizeof(cdb));
            cdb[0] = 0xA1;
            cdb[1] = 4 << 1;
            cdb[2] = 0x0E;
            cdb[3] = 0xD0;
            cdb[4] = 1;
            cdb[6] = 0x4F;
            cdb[7] = 0xC2;
            cdb[8] = 0xA0;
            cdb[9] = 0xB0;
            exec(ha, t, "A3 ATA12 SMART READ DATA", cdb, 12, SRB_DIR_IN,
                 512, 3);

            memset(cdb, 0, sizeof(cdb));
            cdb[0] = 0x85;
            cdb[1] = 3 << 1;
            cdb[2] = 0x20;
            cdb[4] = 0xDA;
            cdb[10] = 0x4F;
            cdb[12] = 0xC2;
            cdb[13] = 0xA0;
            cdb[14] = 0xB0;
            exec(ha, t, "A4 ATA16 SMART RETURN STATUS", cdb, 16, 0, 0, 0);
        }
    }
    printf("ASPI9X done\n");
    return 0;
}
