/*
 * atatest.c - ATATEST.EXE, the pass-through test client (roadmap-usb4.md
 * task 45.27). A Win32 console program, built with MSVC 6.0 (build.cmd),
 * for Windows 2000, XP, Vista and 7 (the 64-bit ones under WOW64). In no
 * package.
 *
 * It sends a disk the requests a SMART or ATA tool sends, through each
 * route Windows gives a program, and prints what came back, so a leg can
 * read which of them reach the disk's driver and how it answers:
 *
 *   ATATEST disk
 *
 *   disk   \\.\PhysicalDriveN or a drive letter such as E: (the volume
 *          \\.\E: is opened). Administrator rights are needed.
 *
 * Nothing is written to the disk: every ATA command sent is a read
 * (IDENTIFY DEVICE, SMART READ DATA) or carries no data (SMART RETURN
 * STATUS, CHECK POWER MODE), and every SCSI command is INQUIRY.
 *
 * The requests, one line each:
 *   P  IOCTL_STORAGE_QUERY_PROPERTY, the adapter and device descriptors
 *      (bus type, vendor and product).
 *   S1 IOCTL_SCSI_PASS_THROUGH, INQUIRY, data in.
 *   S2 The same with the direction unspecified.
 *   A1 IOCTL_SCSI_PASS_THROUGH, ATA PASS-THROUGH (16) IDENTIFY DEVICE,
 *      data in.
 *   A2 The same with the direction unspecified (the CDB's T_DIR says in).
 *   A3 ATA PASS-THROUGH (12) SMART READ DATA, direction unspecified.
 *   A4 ATA PASS-THROUGH (16) SMART RETURN STATUS, no data, CK_COND set.
 *   A5 IOCTL_SCSI_PASS_THROUGH_DIRECT, ATA PASS-THROUGH (16) IDENTIFY
 *      DEVICE, data in.
 *   M1 SMART_GET_VERSION.
 *   M2 SMART_RCV_DRIVE_DATA, SMART READ DATA (disk.sys's own route).
 *   T1 IOCTL_ATA_PASS_THROUGH, IDENTIFY DEVICE (Vista and later define
 *      it; earlier systems answer it as unknown).
 *   T2 IOCTL_ATA_PASS_THROUGH, CHECK POWER MODE, no data.
 *
 * Added for task 45.52 (design record 13 section 16.11):
 *   A6 IOCTL_ATA_PASS_THROUGH_DIRECT, IDENTIFY DEVICE, from a page.
 *   T3 IOCTL_ATA_PASS_THROUGH, SMART READ DATA.
 *   M3 SMART_RCV_DRIVE_DATA, SMART READ ATTRIBUTE THRESHOLDS.
 *   M4 SMART_SEND_DRIVE_COMMAND, SMART RETURN STATUS (the drive's health:
 *      LBA Mid/High 4Fh/C2h, or F4h/2Ch when a threshold is exceeded).
 * and for SMART data (A3, M2, T3) a decode: the revision, the checksum
 * (the 512 bytes sum to 0), and each attribute's id, current value and raw
 * low word; for thresholds each id and its threshold.
 *
 * Output, ASCII: per request whether the call succeeded and its error,
 * the SCSI status, the length returned, the sense key, ASC and ASCQ, and
 * for data a short decode (INQUIRY's vendor and product, IDENTIFY's model).
 * Exit 0 when the program ran, 2 on a usage error or an unopenable disk.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define AT_IOCTL_SCSI_PASS_THROUGH          0x0004D004UL
#define AT_IOCTL_SCSI_PASS_THROUGH_DIRECT   0x0004D014UL
#define AT_IOCTL_ATA_PASS_THROUGH           0x0004D02CUL
#define AT_IOCTL_STORAGE_QUERY_PROPERTY     0x002D1400UL
#define AT_SMART_GET_VERSION                0x00074080UL
#define AT_SMART_RCV_DRIVE_DATA             0x0007C088UL
#define AT_IOCTL_ATA_PASS_THROUGH_DIRECT    0x0004D030UL
#define AT_SMART_SEND_DRIVE_COMMAND         0x0007C084UL

#define AT_DATA_OUT         0
#define AT_DATA_IN          1
#define AT_DATA_UNSPECIFIED 2

#define AT_SENSE_LEN        32
#define AT_DATA_LEN         512

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
} AT_SPT;

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
    PVOID  DataBuffer;
    ULONG  SenseInfoOffset;
    UCHAR  Cdb[16];
} AT_SPTD;

typedef struct {
    USHORT Length;
    USHORT AtaFlags;
    UCHAR  PathId;
    UCHAR  TargetId;
    UCHAR  Lun;
    UCHAR  ReservedAsUchar;
    ULONG  DataTransferLength;
    ULONG  TimeOutValue;
    ULONG  ReservedAsUlong;
    ULONG  DataBufferOffset;     /* ULONG_PTR: a 32-bit program */
    UCHAR  PreviousTaskFile[8];
    UCHAR  CurrentTaskFile[8];
} AT_APT;

typedef struct {
    USHORT Length;
    USHORT AtaFlags;
    UCHAR  PathId;
    UCHAR  TargetId;
    UCHAR  Lun;
    UCHAR  ReservedAsUchar;
    ULONG  DataTransferLength;
    ULONG  TimeOutValue;
    ULONG  ReservedAsUlong;
    PVOID  DataBuffer;           /* a 32-bit program */
    UCHAR  PreviousTaskFile[8];
    UCHAR  CurrentTaskFile[8];
} AT_APTD;

typedef struct {
    UCHAR bFeaturesReg;
    UCHAR bSectorCountReg;
    UCHAR bSectorNumberReg;
    UCHAR bCylLowReg;
    UCHAR bCylHighReg;
    UCHAR bDriveHeadReg;
    UCHAR bCommandReg;
    UCHAR bReserved;
} AT_IDEREGS;

typedef struct {
    ULONG      cBufferSize;
    AT_IDEREGS irDriveRegs;
    UCHAR      bDriveNumber;
    UCHAR      bReserved[3];
    ULONG      dwReserved[4];
    UCHAR      bBuffer[1];
} AT_SENDCMDIN;

typedef struct {
    UCHAR bDriverError;
    UCHAR bIDEError;
    UCHAR bReserved[2];
    ULONG dwReserved[2];
} AT_DRIVERSTATUS;

typedef struct {
    ULONG           cBufferSize;
    AT_DRIVERSTATUS DriverStatus;
    UCHAR           bBuffer[AT_DATA_LEN];
} AT_SENDCMDOUT;

typedef struct {
    UCHAR bVersion;
    UCHAR bRevision;
    UCHAR bReserved;
    UCHAR bIDEDeviceMap;
    ULONG fCapabilities;
    ULONG dwReserved[4];
} AT_GETVERSIONINPARAMS;
#pragma pack(pop)

typedef struct {
    AT_SPT spt;
    UCHAR  sense[AT_SENSE_LEN];
    UCHAR  pad[4];
    UCHAR  data[AT_DATA_LEN];
} AT_SPT_BUF;

typedef struct {
    AT_SPTD sptd;
    UCHAR   sense[AT_SENSE_LEN];
} AT_SPTD_BUF;

typedef struct {
    AT_APT apt;
    UCHAR  data[AT_DATA_LEN];
} AT_APT_BUF;

static void usage(void)
{
    printf("usage: ATATEST disk\n"
           "  disk: \\\\.\\PhysicalDriveN or a drive letter such as E:\n");
}

/* Printable ASCII of n bytes, spaces trimmed at the end. */
static void put_text(const UCHAR *p, int n)
{
    char s[64];
    int i;

    if (n > 63) {
        n = 63;
    }
    for (i = 0; i < n; i++) {
        s[i] = (char)((p[i] >= 0x20 && p[i] < 0x7F) ? p[i] : '.');
    }
    while (n > 0 && s[n - 1] == ' ') {
        n--;
    }
    s[n] = 0;
    printf(" \"%s\"", s);
}

/* IDENTIFY's model, words 27 to 46, each word's bytes swapped. */
static void put_model(const UCHAR *id)
{
    UCHAR m[40];
    int i;

    for (i = 0; i < 40; i += 2) {
        m[i] = id[54 + i + 1];
        m[i + 1] = id[54 + i];
    }
    printf(" model");
    put_text(m, 40);
}

static void put_sense(const UCHAR *s, ULONG n)
{
    if (n == 0) {
        printf(" sense=-");
        return;
    }
    if ((s[0] & 0x7F) == 0x72 || (s[0] & 0x7F) == 0x73) {
        printf(" sense=desc key=%X asc=%02X ascq=%02X", s[1] & 0x0F, s[2],
               s[3]);
        /* ATA Status Return descriptor (09h): error and status. */
        if (n >= 22 && s[8] == 0x09) {
            printf(" ata-err=%02X ata-status=%02X", s[11], s[21]);
        }
        return;
    }
    if (n >= 14) {
        printf(" sense=fixed key=%X asc=%02X ascq=%02X", s[2] & 0x0F, s[12],
               s[13]);
        return;
    }
    printf(" sense=short(%lu)", n);
}

/* SMART READ DATA or READ ATTRIBUTE THRESHOLDS (ACS-3): revision, checksum
 * over all 512 bytes, then 30 entries of 12 bytes from offset 2. */
static void put_smart(const UCHAR *d, int thresholds)
{
    UCHAR sum;
    int i;
    int shown;
    const UCHAR *e;

    sum = 0;
    for (i = 0; i < 512; i++) {
        sum = (UCHAR)(sum + d[i]);
    }
    printf(" smart-rev=%u checksum=%s", d[0] | (d[1] << 8),
           sum == 0 ? "ok" : "BAD");
    shown = 0;
    for (i = 0; i < 30 && shown < 8; i++) {
        e = d + 2 + i * 12;
        if (e[0] == 0) {
            continue;
        }
        if (thresholds) {
            printf(" %02X:thr=%u", e[0], e[1]);
        } else {
            printf(" %02X:val=%u,raw=%u", e[0], e[3], e[5] | (e[6] << 8));
        }
        shown++;
    }
}

static void spt_run(HANDLE h, const char *tag, const char *what,
                    const UCHAR *cdb, UCHAR cdbLen, UCHAR dataIn,
                    ULONG length, int decode)
{
    AT_SPT_BUF b;
    DWORD got;
    BOOL ok;

    memset(&b, 0, sizeof(b));
    b.spt.Length = (USHORT)sizeof(AT_SPT);
    b.spt.CdbLength = cdbLen;
    b.spt.SenseInfoLength = AT_SENSE_LEN;
    b.spt.DataIn = dataIn;
    b.spt.DataTransferLength = length;
    b.spt.TimeOutValue = 10;
    b.spt.DataBufferOffset = (ULONG)((PUCHAR)b.data - (PUCHAR)&b);
    b.spt.SenseInfoOffset = (ULONG)((PUCHAR)b.sense - (PUCHAR)&b);
    memcpy(b.spt.Cdb, cdb, cdbLen);
    got = 0;
    ok = DeviceIoControl(h, AT_IOCTL_SCSI_PASS_THROUGH, &b, sizeof(b), &b,
                         sizeof(b), &got, NULL);
    printf("ATATEST %s %s: ioctl=%d err=%lu", tag, what, ok ? 1 : 0,
           ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" scsi=%02X len=%lu", b.spt.ScsiStatus,
               b.spt.DataTransferLength);
        put_sense(b.sense, b.spt.SenseInfoLength);
        if (b.spt.ScsiStatus == 0 && b.spt.DataTransferLength >= 36 &&
            decode == 1) {
            put_text(b.data + 8, 8);
            put_text(b.data + 16, 16);
        }
        if (b.spt.ScsiStatus == 0 && b.spt.DataTransferLength >= 94 &&
            decode == 2) {
            put_model(b.data);
        }
        if (b.spt.ScsiStatus == 0 &&
            b.spt.DataTransferLength == AT_DATA_LEN && decode == 3) {
            put_smart(b.data, 0);
        }
    }
    printf("\n");
}

static void sptd_run(HANDLE h, const char *tag, const char *what,
                     const UCHAR *cdb, UCHAR cdbLen, UCHAR dataIn,
                     PUCHAR data, ULONG length)
{
    AT_SPTD_BUF b;
    DWORD got;
    BOOL ok;

    memset(&b, 0, sizeof(b));
    b.sptd.Length = (USHORT)sizeof(AT_SPTD);
    b.sptd.CdbLength = cdbLen;
    b.sptd.SenseInfoLength = AT_SENSE_LEN;
    b.sptd.DataIn = dataIn;
    b.sptd.DataTransferLength = length;
    b.sptd.TimeOutValue = 10;
    b.sptd.DataBuffer = data;
    b.sptd.SenseInfoOffset = (ULONG)((PUCHAR)b.sense - (PUCHAR)&b);
    memcpy(b.sptd.Cdb, cdb, cdbLen);
    got = 0;
    ok = DeviceIoControl(h, AT_IOCTL_SCSI_PASS_THROUGH_DIRECT, &b, sizeof(b),
                         &b, sizeof(b), &got, NULL);
    printf("ATATEST %s %s: ioctl=%d err=%lu", tag, what, ok ? 1 : 0,
           ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" scsi=%02X len=%lu", b.sptd.ScsiStatus,
               b.sptd.DataTransferLength);
        put_sense(b.sense, b.sptd.SenseInfoLength);
        if (b.sptd.ScsiStatus == 0 && b.sptd.DataTransferLength >= 94) {
            put_model(data);
        }
    }
    printf("\n");
}

static void apt_run(HANDLE h, const char *tag, const char *what,
                    UCHAR command, USHORT flags, ULONG length)
{
    AT_APT_BUF b;
    DWORD got;
    BOOL ok;

    memset(&b, 0, sizeof(b));
    b.apt.Length = (USHORT)sizeof(AT_APT);
    b.apt.AtaFlags = flags;
    b.apt.DataTransferLength = length;
    b.apt.TimeOutValue = 10;
    b.apt.DataBufferOffset = (ULONG)((PUCHAR)b.data - (PUCHAR)&b);
    b.apt.CurrentTaskFile[1] = (UCHAR)(length != 0 ? 1 : 0);
    b.apt.CurrentTaskFile[5] = 0xA0;
    b.apt.CurrentTaskFile[6] = command;
    got = 0;
    ok = DeviceIoControl(h, AT_IOCTL_ATA_PASS_THROUGH, &b, sizeof(b), &b,
                         sizeof(b), &got, NULL);
    printf("ATATEST %s %s: ioctl=%d err=%lu", tag, what, ok ? 1 : 0,
           ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" len=%lu ata-err=%02X ata-status=%02X count=%02X",
               b.apt.DataTransferLength, b.apt.CurrentTaskFile[0],
               b.apt.CurrentTaskFile[6], b.apt.CurrentTaskFile[1]);
        if (length != 0 && b.apt.DataTransferLength >= 94) {
            put_model(b.data);
        }
    }
    printf("\n");
}

/* IOCTL_ATA_PASS_THROUGH SMART READ DATA (B0h, D0h, LBA Mid/High 4Fh/C2h). */
static void apt_smart_run(HANDLE h)
{
    AT_APT_BUF b;
    DWORD got;
    BOOL ok;

    memset(&b, 0, sizeof(b));
    b.apt.Length = (USHORT)sizeof(AT_APT);
    b.apt.AtaFlags = 1 | 2;
    b.apt.DataTransferLength = AT_DATA_LEN;
    b.apt.TimeOutValue = 10;
    b.apt.DataBufferOffset = (ULONG)((PUCHAR)b.data - (PUCHAR)&b);
    b.apt.CurrentTaskFile[0] = 0xD0;
    b.apt.CurrentTaskFile[1] = 1;
    b.apt.CurrentTaskFile[3] = 0x4F;
    b.apt.CurrentTaskFile[4] = 0xC2;
    b.apt.CurrentTaskFile[5] = 0xA0;
    b.apt.CurrentTaskFile[6] = 0xB0;
    got = 0;
    ok = DeviceIoControl(h, AT_IOCTL_ATA_PASS_THROUGH, &b, sizeof(b), &b,
                         sizeof(b), &got, NULL);
    printf("ATATEST T3 ATA SMART READ DATA: ioctl=%d err=%lu", ok ? 1 : 0,
           ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" len=%lu ata-err=%02X ata-status=%02X",
               b.apt.DataTransferLength, b.apt.CurrentTaskFile[0],
               b.apt.CurrentTaskFile[6]);
        if (b.apt.DataTransferLength == AT_DATA_LEN) {
            put_smart(b.data, 0);
        }
    }
    printf("\n");
}

/* IOCTL_ATA_PASS_THROUGH_DIRECT IDENTIFY DEVICE into the caller's page. */
static void aptd_run(HANDLE h, PUCHAR data)
{
    AT_APTD b;
    DWORD got;
    BOOL ok;

    memset(&b, 0, sizeof(b));
    memset(data, 0, AT_DATA_LEN);
    b.Length = (USHORT)sizeof(AT_APTD);
    b.AtaFlags = 1 | 2;
    b.DataTransferLength = AT_DATA_LEN;
    b.TimeOutValue = 10;
    b.DataBuffer = data;
    b.CurrentTaskFile[1] = 1;
    b.CurrentTaskFile[5] = 0xA0;
    b.CurrentTaskFile[6] = 0xEC;
    got = 0;
    ok = DeviceIoControl(h, AT_IOCTL_ATA_PASS_THROUGH_DIRECT, &b, sizeof(b),
                         &b, sizeof(b), &got, NULL);
    printf("ATATEST A6 ATA_PASS_THROUGH_DIRECT IDENTIFY: ioctl=%d err=%lu",
           ok ? 1 : 0, ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" len=%lu ata-err=%02X ata-status=%02X", b.DataTransferLength,
               b.CurrentTaskFile[0], b.CurrentTaskFile[6]);
        if (b.DataTransferLength >= 94) {
            put_model(data);
        }
    }
    printf("\n");
}

/* A descriptor's NUL-terminated string at off, read within the got bytes
 * returned; one that runs to the end unterminated is shown as such. */
static void put_string(const UCHAR *out, ULONG got, ULONG off)
{
    ULONG n;

    if (off == 0 || off >= got) {
        return;
    }
    for (n = 0; off + n < got && out[off + n] != 0; n++) {
    }
    put_text(out + off, (int)n);
    if (off + n == got) {
        printf("(unterminated)");
    }
}

static void property_run(HANDLE h, ULONG id, const char *tag)
{
    ULONG query[3];
    UCHAR out[1024];
    DWORD got;
    BOOL ok;

    query[0] = id;   /* StorageAdapterProperty 1, StorageDeviceProperty 0 */
    query[1] = 0;    /* PropertyStandardQuery */
    query[2] = 0;
    memset(out, 0, sizeof(out));
    got = 0;
    ok = DeviceIoControl(h, AT_IOCTL_STORAGE_QUERY_PROPERTY, query,
                         sizeof(query), out, sizeof(out), &got, NULL);
    printf("ATATEST %s %s: ioctl=%d err=%lu", tag,
           id == 1 ? "adapter descriptor" : "device descriptor", ok ? 1 : 0,
           ok ? 0UL : (unsigned long)GetLastError());
    if (got > sizeof(out)) {
        got = sizeof(out);
    }
    if (ok && id == 1 && got >= 25) {
        /* STORAGE_ADAPTER_DESCRIPTOR: MaximumTransferLength at 8,
         * BusType at 24. */
        printf(" maxxfer=%lu bustype=%u", *(ULONG *)(out + 8), out[24]);
    }
    if (ok && id == 0 && got >= 36) {
        /* STORAGE_DEVICE_DESCRIPTOR: VendorIdOffset at 12,
         * ProductIdOffset at 16, BusType at 28. */
        printf(" bustype=%u", *(ULONG *)(out + 28));
        put_string(out, got, *(ULONG *)(out + 12));
        put_string(out, got, *(ULONG *)(out + 16));
    }
    printf("\n");
}

static void smart_run(HANDLE h)
{
    AT_GETVERSIONINPARAMS v;
    UCHAR in[sizeof(AT_SENDCMDIN) + 16];
    AT_SENDCMDOUT out;
    AT_SENDCMDIN *c;
    DWORD got;
    BOOL ok;

    memset(&v, 0, sizeof(v));
    got = 0;
    ok = DeviceIoControl(h, AT_SMART_GET_VERSION, NULL, 0, &v, sizeof(v),
                         &got, NULL);
    printf("ATATEST M1 SMART_GET_VERSION: ioctl=%d err=%lu", ok ? 1 : 0,
           ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" version=%u.%u map=%02X caps=%08lX", v.bVersion, v.bRevision,
               v.bIDEDeviceMap, v.fCapabilities);
    }
    printf("\n");

    memset(in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    c = (AT_SENDCMDIN *)in;
    c->cBufferSize = AT_DATA_LEN;
    c->irDriveRegs.bFeaturesReg = 0xD0;
    c->irDriveRegs.bSectorCountReg = 1;
    c->irDriveRegs.bSectorNumberReg = 1;
    c->irDriveRegs.bCylLowReg = 0x4F;
    c->irDriveRegs.bCylHighReg = 0xC2;
    c->irDriveRegs.bDriveHeadReg = 0xA0;
    c->irDriveRegs.bCommandReg = 0xB0;
    got = 0;
    ok = DeviceIoControl(h, AT_SMART_RCV_DRIVE_DATA, in,
                         sizeof(AT_SENDCMDIN) - 1, &out, sizeof(out), &got,
                         NULL);
    printf("ATATEST M2 SMART_RCV_DRIVE_DATA READ DATA: ioctl=%d err=%lu",
           ok ? 1 : 0, ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" got=%lu size=%lu driver-err=%02X ide-err=%02X", got,
               out.cBufferSize, out.DriverStatus.bDriverError,
               out.DriverStatus.bIDEError);
        if (out.cBufferSize == AT_DATA_LEN) {
            put_smart(out.bBuffer, 0);
        }
    }
    printf("\n");

    c->irDriveRegs.bFeaturesReg = 0xD1;
    memset(&out, 0, sizeof(out));
    got = 0;
    ok = DeviceIoControl(h, AT_SMART_RCV_DRIVE_DATA, in,
                         sizeof(AT_SENDCMDIN) - 1, &out, sizeof(out), &got,
                         NULL);
    printf("ATATEST M3 SMART_RCV_DRIVE_DATA READ THRESHOLDS: ioctl=%d err=%lu",
           ok ? 1 : 0, ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" got=%lu size=%lu driver-err=%02X", got, out.cBufferSize,
               out.DriverStatus.bDriverError);
        if (out.cBufferSize == AT_DATA_LEN) {
            put_smart(out.bBuffer, 1);
        }
    }
    printf("\n");

    /* RETURN STATUS: SENDCMDOUTPARAMS with an IDEREGS (8 bytes) after it. */
    c->cBufferSize = 0;
    c->irDriveRegs.bFeaturesReg = 0xDA;
    c->irDriveRegs.bSectorCountReg = 0;
    memset(&out, 0, sizeof(out));
    got = 0;
    ok = DeviceIoControl(h, AT_SMART_SEND_DRIVE_COMMAND, in,
                         sizeof(AT_SENDCMDIN) - 1, &out,
                         sizeof(ULONG) + sizeof(AT_DRIVERSTATUS) +
                             sizeof(AT_IDEREGS),
                         &got, NULL);
    printf("ATATEST M4 SMART_SEND_DRIVE_COMMAND RETURN STATUS: ioctl=%d "
           "err=%lu", ok ? 1 : 0, ok ? 0UL : (unsigned long)GetLastError());
    if (ok) {
        printf(" got=%lu size=%lu driver-err=%02X", got, out.cBufferSize,
               out.DriverStatus.bDriverError);
        if (out.cBufferSize >= 8) {
            printf(" lba-mid=%02X lba-high=%02X status=%02X %s",
                   out.bBuffer[3], out.bBuffer[4], out.bBuffer[6],
                   (out.bBuffer[3] == 0x4F && out.bBuffer[4] == 0xC2)
                       ? "healthy"
                       : (out.bBuffer[3] == 0xF4 && out.bBuffer[4] == 0x2C)
                             ? "THRESHOLD EXCEEDED" : "unknown");
        }
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    char path[MAX_PATH];
    HANDLE h;
    UCHAR cdb[16];
    PUCHAR direct;
    PUCHAR directRaw;

    if (argc != 2 || strlen(argv[1]) > MAX_PATH - 8) {
        usage();
        return 2;
    }
    if (strlen(argv[1]) == 2 && argv[1][1] == ':') {
        sprintf(path, "\\\\.\\%c:", argv[1][0]);
    } else {
        strcpy(path, argv[1]);
    }
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ATATEST cannot open %s: error %lu\n", path,
               (unsigned long)GetLastError());
        return 2;
    }
    printf("ATATEST %s\n", path);

    property_run(h, 1, "P");
    property_run(h, 0, "P");

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x12;
    cdb[4] = 36;
    spt_run(h, "S1", "INQUIRY in", cdb, 6, AT_DATA_IN, 36, 1);
    spt_run(h, "S2", "INQUIRY unspecified", cdb, 6, AT_DATA_UNSPECIFIED, 36,
            1);

    /* ATA PASS-THROUGH (16): PIO data-in (protocol 4), T_DIR in,
     * BYT_BLOK, T_LENGTH in the count; IDENTIFY DEVICE. */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x85;
    cdb[1] = 4 << 1;
    cdb[2] = 0x0E;
    cdb[6] = 1;
    cdb[13] = 0xA0;
    cdb[14] = 0xEC;
    spt_run(h, "A1", "ATA16 IDENTIFY in", cdb, 16, AT_DATA_IN, AT_DATA_LEN, 2);
    spt_run(h, "A2", "ATA16 IDENTIFY unspecified", cdb, 16,
            AT_DATA_UNSPECIFIED, AT_DATA_LEN, 2);

    /* ATA PASS-THROUGH (12): SMART READ DATA (B0h, feature D0h). */
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
    spt_run(h, "A3", "ATA12 SMART READ DATA unspecified", cdb, 12,
            AT_DATA_UNSPECIFIED, AT_DATA_LEN, 3);

    /* ATA PASS-THROUGH (16): SMART RETURN STATUS (B0h, feature DAh),
     * non-data (protocol 3), CK_COND so the registers come back. */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x85;
    cdb[1] = 3 << 1;
    cdb[2] = 0x20;
    cdb[4] = 0xDA;
    cdb[10] = 0x4F;
    cdb[12] = 0xC2;
    cdb[13] = 0xA0;
    cdb[14] = 0xB0;
    spt_run(h, "A4", "ATA16 SMART RETURN STATUS none", cdb, 16,
            AT_DATA_UNSPECIFIED, 0, 0);

    /* The same IDENTIFY as A1 through the direct route, from a buffer
     * aligned to a page (any adapter alignment is met). */
    directRaw = (PUCHAR)VirtualAlloc(NULL, 8192, MEM_COMMIT, PAGE_READWRITE);
    if (directRaw != NULL) {
        direct = directRaw;
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = 0x85;
        cdb[1] = 4 << 1;
        cdb[2] = 0x0E;
        cdb[6] = 1;
        cdb[13] = 0xA0;
        cdb[14] = 0xEC;
        sptd_run(h, "A5", "ATA16 IDENTIFY direct in", cdb, 16, AT_DATA_IN,
                 direct, AT_DATA_LEN);
        aptd_run(h, direct);
        VirtualFree(directRaw, 0, MEM_RELEASE);
    }

    smart_run(h);

    /* ATA_FLAGS_DRDY_REQUIRED 1, ATA_FLAGS_DATA_IN 2. */
    apt_run(h, "T1", "ATA IDENTIFY", 0xEC, 1 | 2, AT_DATA_LEN);
    apt_run(h, "T2", "ATA CHECK POWER MODE", 0xE5, 1, 0);
    apt_smart_run(h);

    CloseHandle(h);
    printf("ATATEST done\n");
    return 0;
}
