/*
 * test_uas.c - host vectors for the pure core of xhciuas.sys, the UAS class
 * driver (src\uas\uas_iu.c; roadmap-hcd.md task 31-A.2): the information
 * unit encoders and the parser, the SAM LUN field, the tag allocator, the
 * configuration walk for the UAS alternate setting and its Pipe Usage
 * descriptors, the REPORT LUNS parse, the status fold, the CDB direction
 * table and the storage id strings.
 *
 * Every expected byte was worked by hand from the UAS 1.0 specification's
 * IU layouts (COMMAND IU section 6.2.1, SENSE IU 6.2.2, RESPONSE IU 6.2.3,
 * TASK MANAGEMENT IU 6.2.4, READ READY and WRITE READY 6.2.5 and 6.2.6),
 * SAM's single-level LUN formats and the USB 3.0 chapter 9 descriptor
 * layouts, not from the code under test. The two configuration vectors are
 * CONSTRUCTED in the shape UAS devices report (a Bulk-Only setting 0 and a
 * UAS setting 1), not read off a device. The id forms are this driver's own
 * policy (uas_pdo.c) and test that policy.
 */

#include <stdio.h>
#include <string.h>
#include "../src/uas/uas_iu.h"
#include "test_harness.h"

static int bytes_eq(const UCHAR *a, const UCHAR *b, ULONG n)
{
    return memcmp(a, b, n) == 0;
}

/* ------------------------------------------------------------------ */

static void test_tags(void)
{
    UAS_TAGS t;

    UasTagInit(&t, 4);
    CHECK_EQ(UasTagAlloc(&t), 1, "first tag is 1, never 0");
    CHECK_EQ(UasTagAlloc(&t), 2, "second tag");
    CHECK_EQ(UasTagAlloc(&t), 3, "third tag");
    CHECK_EQ(UasTagAlloc(&t), 4, "fourth tag");
    CHECK_EQ(UasTagAlloc(&t), 0, "a full set hands out nothing");
    CHECK_EQ(UasTagsInUse(&t), 4, "four in use");
    CHECK(UasTagFree(&t, 2), "free 2");
    CHECK(!UasTagFree(&t, 2), "a double free is refused");
    CHECK(!UasTagFree(&t, 0), "tag 0 is never valid");
    CHECK(!UasTagFree(&t, 5), "a tag past the count is refused");
    CHECK(!UasTagBusy(&t, 2), "2 is free");
    CHECK(UasTagBusy(&t, 3), "3 is busy");
    CHECK_EQ(UasTagAlloc(&t), 2, "the only free tag");
    CHECK(UasTagFree(&t, 1), "free 1");
    CHECK(UasTagFree(&t, 3), "free 3");
    /* The last handed out was 2, so the search starts at 3: 3 is reused
     * before 1, which was freed first but sits behind the cursor. */
    CHECK_EQ(UasTagAlloc(&t), 3, "the search resumes after the last tag");
    CHECK_EQ(UasTagAlloc(&t), 1, "then wraps");
    CHECK_EQ(UasTagsInUse(&t), 4, "four in use again");

    UasTagInit(&t, 40);
    CHECK_EQ(t.Count, UAS_MAX_TAGS, "the count is capped at UAS_MAX_TAGS");
    UasTagInit(&t, 0);
    CHECK_EQ(UasTagAlloc(&t), 0, "an empty set hands out nothing");

    /* Streamless: one command tag and one for task management. */
    UasTagInit(&t, 2);
    CHECK_EQ(UasTagAlloc(&t), 1, "command tag");
    CHECK_EQ(UasTagAlloc(&t), 2, "task management tag");
    CHECK_EQ(UasTagAlloc(&t), 0, "nothing more");
    CHECK(UasTagFree(&t, 1), "the command ends");
    CHECK_EQ(UasTagAlloc(&t), 1, "the next command reuses the only tag");
}

/* ------------------------------------------------------------------ */

static void test_command_iu(void)
{
    static const UCHAR read10[10] = {
        0x28, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08, 0x00 };
    static const UCHAR want[32] = {
        0x01, 0x00, 0x12, 0x34, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const UCHAR cdb16[16] = {
        0x88, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    UCHAR iu[40];

    memset(iu, 0xEE, sizeof(iu));
    CHECK_EQ(UasIuBuildCommand(iu, sizeof(iu), 0x1234, 3, UAS_TASK_ORDERED,
                               read10, 10), 32, "a COMMAND IU is 32 bytes");
    CHECK(bytes_eq(iu, want, 32), "READ(10), tag 0x1234, LUN 3, ORDERED");
    CHECK_EQ(iu[32], 0xEE, "nothing written past the IU");

    CHECK_EQ(UasIuBuildCommand(iu, sizeof(iu), 1, 0, UAS_TASK_SIMPLE, cdb16,
                               16), 32, "a 16-byte CDB fits the fixed field");
    CHECK_EQ(iu[6], 0, "no additional CDB length");
    CHECK(bytes_eq(iu + 16, cdb16, 16), "READ(16) in bytes 16..31");

    CHECK_EQ(UasIuBuildCommand(iu, sizeof(iu), 1, 0, 0, cdb16, 17), 0,
             "a 17-byte CDB is refused");
    CHECK_EQ(UasIuBuildCommand(iu, sizeof(iu), 0, 0, 0, read10, 10), 0,
             "tag 0 is refused");
    CHECK_EQ(UasIuBuildCommand(iu, sizeof(iu), 0x10000, 0, 0, read10, 10),
             0, "a tag wider than 16 bits is refused");
    CHECK_EQ(UasIuBuildCommand(iu, 31, 1, 0, 0, read10, 10), 0,
             "a buffer shorter than the IU is refused");
    CHECK_EQ(UasIuBuildCommand(iu, sizeof(iu), 1, 0, 0, read10, 0), 0,
             "an empty CDB is refused");

    CHECK_EQ(UasIuBuildCommand(iu, sizeof(iu), 2, 300, 0, read10, 10), 32,
             "LUN 300 encodes");
    CHECK_EQ(iu[8], 0x41, "flat addressing: 01b and bits 13:8");
    CHECK_EQ(iu[9], 0x2C, "bits 7:0");
}

static void test_task_iu(void)
{
    static const UCHAR want[16] = {
        0x05, 0x00, 0x00, 0x07, 0x01, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const UCHAR wantReset[16] = {
        0x05, 0x00, 0x00, 0x20, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    UCHAR iu[16];

    CHECK_EQ(UasIuBuildTaskMgmt(iu, sizeof(iu), 7, UAS_TMF_ABORT_TASK, 5, 0),
             16, "a TASK MANAGEMENT IU is 16 bytes");
    CHECK(bytes_eq(iu, want, 16), "ABORT TASK of tag 5 under tag 7");
    CHECK_EQ(UasIuBuildTaskMgmt(iu, sizeof(iu), 0x20,
                                UAS_TMF_LOGICAL_UNIT_RESET, 0, 2),
             16, "LOGICAL UNIT RESET");
    CHECK(bytes_eq(iu, wantReset, 16), "LOGICAL UNIT RESET of LUN 2");
    CHECK_EQ(UasIuBuildTaskMgmt(iu, 15, 1, 1, 1, 0), 0,
             "a short buffer is refused");
    CHECK_EQ(UasIuBuildTaskMgmt(iu, sizeof(iu), 0, 1, 1, 0), 0,
             "tag 0 is refused");
}

static void test_parse(void)
{
    UCHAR buf[300];
    UAS_IU_INFO info;
    ULONG i;

    /* SENSE IU: tag 9, qualifier 0x0102, CHECK CONDITION, 18 bytes of
     * fixed-format sense (key 6, UNIT ATTENTION; ASC 0x29). */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x03;
    buf[3] = 0x09;
    buf[4] = 0x01;
    buf[5] = 0x02;
    buf[6] = 0x02;
    buf[15] = 18;
    buf[16] = 0x70;
    buf[18] = 0x06;
    buf[28] = 0x29;
    CHECK_EQ(UasIuParse(buf, 34, &info), UAS_PARSE_OK, "a SENSE IU parses");
    CHECK_EQ(info.Id, UAS_IU_SENSE, "its id");
    CHECK_EQ(info.Tag, 9, "its tag");
    CHECK_EQ(info.Qualifier, 0x0102, "its status qualifier");
    CHECK_EQ(info.Status, UAS_SCSI_CHECK_CONDITION, "its status");
    CHECK_EQ(info.SenseLength, 18, "its sense length");

    CHECK_EQ(UasIuParse(buf, 20, &info), UAS_PARSE_OK,
             "a SENSE IU cut short of its sense still parses");
    CHECK_EQ(info.SenseLength, 4, "only the sense bytes that arrived");

    buf[14] = 0x01;
    buf[15] = 0x00;
    for (i = 16; i < sizeof(buf); i++) {
        buf[i] = (UCHAR)i;
    }
    CHECK_EQ(UasIuParse(buf, sizeof(buf), &info), UAS_PARSE_OK,
             "a SENSE IU claiming 256 bytes");
    CHECK_EQ(info.SenseLength, UAS_SENSE_MAX, "capped at 252");

    CHECK_EQ(UasIuParse(buf, 15, &info), UAS_PARSE_SHORT,
             "a SENSE IU shorter than its header");
    CHECK_EQ(UasIuParse(buf, 3, &info), UAS_PARSE_SHORT,
             "shorter than any IU");

    /* RESPONSE IU: tag 0x0102, additional info 0xABCDEF, OVERLAPPED TAG. */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x04;
    buf[2] = 0x01;
    buf[3] = 0x02;
    buf[4] = 0xAB;
    buf[5] = 0xCD;
    buf[6] = 0xEF;
    buf[7] = UAS_RC_OVERLAPPED_TAG;
    CHECK_EQ(UasIuParse(buf, 8, &info), UAS_PARSE_OK, "a RESPONSE IU");
    CHECK_EQ(info.Id, UAS_IU_RESPONSE, "its id");
    CHECK_EQ(info.Tag, 0x0102, "its tag");
    CHECK_EQ(info.ResponseInfo, 0xABCDEF, "its additional information");
    CHECK_EQ(info.ResponseCode, UAS_RC_OVERLAPPED_TAG, "its code");
    CHECK_EQ(UasIuParse(buf, 7, &info), UAS_PARSE_SHORT,
             "a RESPONSE IU shorter than 8");

    buf[0] = 0x06;
    buf[2] = 0x00;
    buf[3] = 0x11;
    CHECK_EQ(UasIuParse(buf, 4, &info), UAS_PARSE_OK, "READ READY");
    CHECK_EQ(info.Id, UAS_IU_READ_READY, "its id");
    CHECK_EQ(info.Tag, 0x11, "its tag");
    buf[0] = 0x07;
    CHECK_EQ(UasIuParse(buf, 4, &info), UAS_PARSE_OK, "WRITE READY");
    CHECK_EQ(info.Id, UAS_IU_WRITE_READY, "its id");

    buf[0] = 0x01;
    CHECK_EQ(UasIuParse(buf, 32, &info), UAS_PARSE_UNKNOWN,
             "a COMMAND IU never arrives on the status pipe");
    buf[0] = 0x05;
    CHECK_EQ(UasIuParse(buf, 16, &info), UAS_PARSE_UNKNOWN,
             "nor a TASK MANAGEMENT IU");
}

static void test_luns(void)
{
    static const UCHAR peripheral[8] = { 0x00, 0x05, 0, 0, 0, 0, 0, 0 };
    static const UCHAR flat[8] = { 0x40, 0x05, 0, 0, 0, 0, 0, 0 };
    static const UCHAR bus[8] = { 0x01, 0x05, 0, 0, 0, 0, 0, 0 };
    static const UCHAR deep[8] = { 0x00, 0x05, 0x00, 0x01, 0, 0, 0, 0 };
    static const UCHAR logical[8] = { 0x80, 0x05, 0, 0, 0, 0, 0, 0 };
    UCHAR out[8];
    ULONG lun;

    CHECK(UasLunDecode(peripheral, &lun) && lun == 5, "peripheral LUN 5");
    CHECK(UasLunDecode(flat, &lun) && lun == 5, "flat LUN 5");
    CHECK(!UasLunDecode(bus, &lun), "a nonzero bus id is refused");
    CHECK(!UasLunDecode(deep, &lun), "a second level is refused");
    CHECK(!UasLunDecode(logical, &lun),
          "logical unit addressing is refused");
    CHECK(UasLunEncode(16383, out) && out[0] == 0x7F && out[1] == 0xFF,
          "the largest flat LUN");
    CHECK(!UasLunEncode(16384, out), "past flat addressing");
}

/* ------------------------------------------------------------------ */

/*
 * A High Speed UAS device: interface 0 setting 0 Bulk-Only (08/06/50, two
 * bulk endpoints), setting 1 UAS (08/06/62, four bulk endpoints of 512
 * bytes, each followed by its Pipe Usage descriptor). Endpoints: 0x01
 * command OUT, 0x82 status IN, 0x83 data-in, 0x04 data-out.
 */
static const UCHAR hsCfg[] = {
    0x09, 0x02, 0x55, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    /* 9: setting 0, Bulk-Only */
    0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x00, 0x02, 0x00,
    /* 32: setting 1, UAS */
    0x09, 0x04, 0x00, 0x01, 0x04, 0x08, 0x06, 0x62, 0x00,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00,
    0x04, 0x24, 0x01, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00,
    0x04, 0x24, 0x02, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x00, 0x02, 0x00,
    0x04, 0x24, 0x03, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x00, 0x02, 0x00,
    0x04, 0x24, 0x04, 0x00 };

/*
 * The same device at SuperSpeed: 1024-byte endpoints, each followed by a
 * SuperSpeed Endpoint Companion (bMaxBurst 15; bmAttributes MaxStreams 5 on
 * the status and data endpoints, i.e. 32 streams, 0 on the command
 * endpoint) and then its Pipe Usage descriptor. Setting 0 is omitted.
 */
static const UCHAR ssCfg[] = {
    0x09, 0x02, 0x56, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x01, 0x04, 0x08, 0x06, 0x62, 0x00,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x04, 0x00,
    0x06, 0x30, 0x0F, 0x00, 0x00, 0x00,
    0x04, 0x24, 0x01, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x04, 0x00,
    0x06, 0x30, 0x0F, 0x05, 0x00, 0x00,
    0x04, 0x24, 0x02, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x00, 0x04, 0x00,
    0x06, 0x30, 0x0F, 0x05, 0x00, 0x00,
    0x04, 0x24, 0x03, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x00, 0x04, 0x00,
    0x06, 0x30, 0x0F, 0x05, 0x00, 0x00,
    0x04, 0x24, 0x04, 0x00 };

static void test_config(void)
{
    UAS_CONFIG_INFO info;
    UCHAR bad[sizeof(hsCfg)];

    CHECK_EQ(sizeof(hsCfg), 0x55, "the HS vector's wTotalLength");
    CHECK_EQ(sizeof(ssCfg), 0x56, "the SS vector's wTotalLength");

    CHECK_EQ(UasParseConfig(hsCfg, sizeof(hsCfg), &info), UAS_CFG_OK,
             "the High Speed UAS setting is found");
    CHECK_EQ(info.InterfaceNumber, 0, "interface 0");
    CHECK_EQ(info.AlternateSetting, 1, "setting 1, not the Bulk-Only 0");
    CHECK_EQ(info.EndpointCount, 4, "four endpoints");
    CHECK_EQ(info.EndpointAddress[UAS_PIPE_COMMAND - 1], 0x01, "command");
    CHECK_EQ(info.EndpointAddress[UAS_PIPE_STATUS - 1], 0x82, "status");
    CHECK_EQ(info.EndpointAddress[UAS_PIPE_DATA_IN - 1], 0x83, "data-in");
    CHECK_EQ(info.EndpointAddress[UAS_PIPE_DATA_OUT - 1], 0x04, "data-out");
    CHECK_EQ(info.MaxPacket[UAS_PIPE_STATUS - 1], 512, "512-byte packets");
    CHECK(!info.SuperSpeed, "no companions: not SuperSpeed");
    CHECK_EQ(info.MaxStreams[UAS_PIPE_DATA_IN - 1], 0, "no streams");

    CHECK_EQ(UasParseConfig(ssCfg, sizeof(ssCfg), &info), UAS_CFG_OK,
             "the SuperSpeed UAS setting is found");
    CHECK(info.SuperSpeed, "companions: SuperSpeed");
    CHECK_EQ(info.MaxPacket[UAS_PIPE_DATA_OUT - 1], 1024, "1024-byte packets");
    CHECK_EQ(info.MaxStreams[UAS_PIPE_COMMAND - 1], 0,
             "the command pipe has no streams");
    CHECK_EQ(info.MaxStreams[UAS_PIPE_STATUS - 1], 32, "status: 2^5 streams");
    CHECK_EQ(info.MaxStreams[UAS_PIPE_DATA_IN - 1], 32, "data-in: 32");
    CHECK_EQ(info.MaxStreams[UAS_PIPE_DATA_OUT - 1], 32, "data-out: 32");

    memcpy(bad, hsCfg, sizeof(hsCfg));
    bad[sizeof(hsCfg) - 2] = 0x03;  /* data-out's usage claims data-in */
    CHECK_EQ(UasParseConfig(bad, sizeof(bad), &info), UAS_CFG_BAD_PIPES,
             "a duplicated pipe usage leaves data-out unnamed");

    memcpy(bad, hsCfg, sizeof(hsCfg));
    bad[32 + 7] = 0x50;             /* setting 1 is Bulk-Only too */
    CHECK_EQ(UasParseConfig(bad, sizeof(bad), &info), UAS_CFG_NO_INTERFACE,
             "no 08/06/62 setting");

    memcpy(bad, hsCfg, sizeof(hsCfg));
    bad[41 + 2] = 0x81;             /* the command endpoint made IN */
    CHECK_EQ(UasParseConfig(bad, sizeof(bad), &info), UAS_CFG_BAD_PIPES,
             "a command pipe that is IN is refused");

    memcpy(bad, hsCfg, sizeof(hsCfg));
    bad[sizeof(hsCfg) - 4] = 0x40;  /* the last descriptor runs past the end */
    CHECK_EQ(UasParseConfig(bad, sizeof(bad), &info), UAS_CFG_MALFORMED,
             "a descriptor overrunning the buffer");

    CHECK_EQ(UasParseConfig(hsCfg, 8, &info), UAS_CFG_MALFORMED,
             "shorter than a configuration descriptor");
}

/* ------------------------------------------------------------------ */

static void test_report_luns(void)
{
    UCHAR buf[8 + 5 * 8];
    UCHAR luns[8];

    memset(buf, 0, sizeof(buf));
    buf[3] = 5 * 8;
    buf[8 + 1] = 0;                 /* LUN 0 */
    buf[16 + 1] = 2;                /* LUN 2 */
    buf[24 + 1] = 2;                /* LUN 2 again */
    buf[32 + 0] = 0x41;             /* flat 300: past 255, skipped */
    buf[32 + 1] = 0x2C;
    buf[40 + 0] = 0xC0;             /* extended addressing: skipped */
    CHECK_EQ(UasParseReportLuns(buf, sizeof(buf), luns, 8), 2,
             "two usable LUNs");
    CHECK_EQ(luns[0], 0, "LUN 0");
    CHECK_EQ(luns[1], 2, "LUN 2, once");
    CHECK_EQ(UasParseReportLuns(buf, sizeof(buf), luns, 1), 1,
             "the caller's limit holds");

    buf[3] = 0xFF;                  /* a list longer than what arrived */
    CHECK_EQ(UasParseReportLuns(buf, 16, luns, 8), 1,
             "only entries that arrived are read");
    CHECK_EQ(UasParseReportLuns(buf, 7, luns, 8), 0, "no header");
}

static void test_status(void)
{
    CHECK_EQ(UasSrbStatus(UAS_SCSI_GOOD, 512, 512), UAS_SRB_SUCCESS,
             "GOOD, every byte");
    CHECK_EQ(UasSrbStatus(UAS_SCSI_GOOD, 0, 0), UAS_SRB_SUCCESS,
             "GOOD, no data");
    CHECK_EQ(UasSrbStatus(UAS_SCSI_GOOD, 512, 36), UAS_SRB_DATA_OVERRUN,
             "GOOD, short: the underrun convention");
    CHECK_EQ(UasSrbStatus(UAS_SCSI_CHECK_CONDITION, 512, 0), UAS_SRB_ERROR,
             "CHECK CONDITION");
    CHECK_EQ(UasSrbStatus(UAS_SCSI_BUSY, 512, 0), UAS_SRB_BUSY, "BUSY");
    CHECK_EQ(UasSrbStatus(UAS_SCSI_TASK_SET_FULL, 512, 0), UAS_SRB_BUSY,
             "TASK SET FULL is retried as BUSY");
    CHECK_EQ(UasSrbStatus(0x18, 0, 0), UAS_SRB_ERROR,
             "RESERVATION CONFLICT");
}

static void test_direction(void)
{
    CHECK_EQ(UasCdbDirection(0x28), UAS_DIR_IN, "READ(10)");
    CHECK_EQ(UasCdbDirection(0x88), UAS_DIR_IN, "READ(16)");
    CHECK_EQ(UasCdbDirection(0x12), UAS_DIR_IN, "INQUIRY");
    CHECK_EQ(UasCdbDirection(0x25), UAS_DIR_IN, "READ CAPACITY(10)");
    CHECK_EQ(UasCdbDirection(0x9E), UAS_DIR_IN, "READ CAPACITY(16)");
    CHECK_EQ(UasCdbDirection(0x1A), UAS_DIR_IN, "MODE SENSE(6)");
    CHECK_EQ(UasCdbDirection(0x5A), UAS_DIR_IN, "MODE SENSE(10)");
    CHECK_EQ(UasCdbDirection(0x03), UAS_DIR_IN, "REQUEST SENSE");
    CHECK_EQ(UasCdbDirection(0x2A), UAS_DIR_OUT, "WRITE(10)");
    CHECK_EQ(UasCdbDirection(0x8A), UAS_DIR_OUT, "WRITE(16)");
    CHECK_EQ(UasCdbDirection(0x00), UAS_DIR_NONE, "TEST UNIT READY");
    CHECK_EQ(UasCdbDirection(0x35), UAS_DIR_NONE, "SYNCHRONIZE CACHE(10)");
    CHECK_EQ(UasCdbDirection(0x1B), UAS_DIR_NONE, "START STOP UNIT");
}

/* ------------------------------------------------------------------ */

static const UCHAR inqDisk[36] = {
    0x00, 0x00, 0x06, 0x02, 0x1F, 0x00, 0x00, 0x00,
    'S', 'a', 'n', 'D', 'i', 's', 'k', ' ',
    'E', 'x', 't', 'r', 'e', 'm', 'e', ' ',
    'P', 'r', 'o', ' ', ' ', ' ', ' ', ' ',
    '1', '.', '0', '0' };

static const char *next_sz(const char *p)
{
    return p + strlen(p) + 1;
}

static void test_ids(void)
{
    char out[512];
    const char *p;
    UCHAR inq[36];
    ULONG n;

    n = UasBuildId(UAS_ID_DEVICE, inqDisk, 36, "ABC", 0, out, sizeof(out));
    CHECK(n != 0, "the device id builds");
    CHECK(strcmp(out, "XHCIUAS\\Disk&Ven_SanDisk&Prod_Extreme_Pro&Rev_1.00")
              == 0,
          "device id: trailing spaces trimmed, inner ones underscored");
    CHECK_EQ(n, strlen(out) + 1, "its length includes the NUL");

    n = UasBuildId(UAS_ID_HARDWARE, inqDisk, 36, "ABC", 0, out, sizeof(out));
    CHECK(n != 0, "the hardware ids build");
    p = out;
    CHECK(strcmp(p, "XHCIUAS\\DiskSanDisk_Extreme_Pro_____1.00") == 0,
          "most specific: the padded fields at their width");
    p = next_sz(p);
    CHECK(strcmp(p, "XHCIUAS\\DiskSanDisk_Extreme_Pro_____") == 0,
          "without the revision");
    p = next_sz(p);
    CHECK(strcmp(p, "XHCIUAS\\DiskSanDisk_") == 0, "the vendor alone");
    p = next_sz(p);
    CHECK(strcmp(p, "USBSTOR\\GenDisk") == 0,
          "what NUSB's usbntmap.inf binds on Windows 98 SE");
    p = next_sz(p);
    CHECK(strcmp(p, "GenDisk") == 0, "what disk.inf binds on NT");
    p = next_sz(p);
    CHECK(*p == 0, "the MULTI_SZ ends");
    CHECK_EQ(n, (ULONG)(p - out) + 1, "its length covers the final NUL");

    n = UasBuildId(UAS_ID_COMPATIBLE, inqDisk, 36, NULL, 0, out,
                   sizeof(out));
    CHECK(n != 0 && strcmp(out, "XHCIUAS\\Disk") == 0 &&
              out[strlen(out) + 1] == 0,
          "compatible: the type alone");

    n = UasBuildId(UAS_ID_INSTANCE, inqDisk, 36, "AB C,1\\", 1, out,
                   sizeof(out));
    CHECK(n != 0 && strcmp(out, "AB_C_1_&1") == 0,
          "instance: serial sanitised, then the LUN");
    n = UasBuildId(UAS_ID_INSTANCE, inqDisk, 36, "", 12, out, sizeof(out));
    CHECK(n != 0 && strcmp(out, "12") == 0, "instance with no serial");
    n = UasBuildId(UAS_ID_INSTANCE, inqDisk, 36, NULL, 0, out, sizeof(out));
    CHECK(n != 0 && strcmp(out, "0") == 0, "instance, NULL serial, LUN 0");

    n = UasBuildId(UAS_ID_TEXT, inqDisk, 36, NULL, 0, out, sizeof(out));
    CHECK(n != 0 && strcmp(out, "SanDisk Extreme Pro UAS Device") == 0,
          "the description keeps its spaces");

    memcpy(inq, inqDisk, 36);
    inq[0] = 0x05;
    n = UasBuildId(UAS_ID_HARDWARE, inq, 36, NULL, 0, out, sizeof(out));
    p = next_sz(next_sz(next_sz(out)));
    CHECK(n != 0 && strncmp(out, "XHCIUAS\\CdRom", 13) == 0 &&
              strcmp(p, "USBSTOR\\GenCdRom") == 0 &&
              strcmp(next_sz(p), "GenCdRom") == 0,
          "a CD-ROM unit gets the CD-ROM generics");

    inq[0] = 0x0E;
    n = UasBuildId(UAS_ID_COMPATIBLE, inq, 36, NULL, 0, out, sizeof(out));
    CHECK(n != 0 && strcmp(out, "XHCIUAS\\Disk") == 0,
          "the reduced block command set is a disk");

    inq[0] = 0x0D;
    n = UasBuildId(UAS_ID_HARDWARE, inq, 36, NULL, 0, out, sizeof(out));
    p = next_sz(next_sz(next_sz(out)));
    CHECK(n != 0 && strncmp(out, "XHCIUAS\\Other", 13) == 0 && *p == 0,
          "an enclosure unit gets no generic id");

    memcpy(inq, inqDisk, 36);
    inq[8] = 0x01;
    inq[9] = 0xC3;
    n = UasBuildId(UAS_ID_DEVICE, inq, 36, NULL, 0, out, sizeof(out));
    CHECK(n != 0 && strncmp(out, "XHCIUAS\\Disk&Ven___nDisk", 24) == 0,
          "control and high bytes become underscores");

    CHECK_EQ(UasBuildId(UAS_ID_HARDWARE, inqDisk, 36, NULL, 0, out, 40), 0,
             "a buffer too small is an error, not a truncation");
    CHECK_EQ(UasBuildId(UAS_ID_DEVICE, inqDisk, 35, NULL, 0, out,
                        sizeof(out)), 0,
             "short INQUIRY data is refused");
    CHECK_EQ(UasBuildId(99, inqDisk, 36, NULL, 0, out, sizeof(out)), 0,
             "an unknown kind is refused");
}

int main(void)
{
    test_tags();
    test_command_iu();
    test_task_iu();
    test_parse();
    test_luns();
    test_config();
    test_report_luns();
    test_status();
    test_direction();
    test_ids();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
