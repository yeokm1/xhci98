/*
 * test_uas.c - host vectors for the pure core of xhciuas.sys, the UAS class
 * driver (src\uas\uas_iu.c; roadmap-hcd.md task 31-A.2): the information
 * unit encoders and the parser, the SAM LUN field, the tag allocator, the
 * configuration walk for the UAS alternate setting and its Pipe Usage
 * descriptors, the REPORT LUNS parse, the status fold, the CDB direction
 * table and the storage id strings; and task 45.23's sense fold, vital
 * product data list, medium identity comparison and wake decision; and task
 * 45.52's pass-through header checks for the three caller layouts, the ATA
 * PASS-THROUGH (16) CDB, the drive's registers from sense, the outcome
 * written back, and the SMART requests disk.sys sends.
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
#include "../src/xhci98_xfer.h"
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

    /* Task 36.46: an endpoint past bNumEndpoints, with a companion and a
     * Pipe Usage after it, is no pipe of the setting (the bus opens four)
     * and lends it neither SuperSpeed nor a usage. */
    {
        static const UCHAR tail[] = {
            0x07, 0x05, 0x05, 0x02, 0x00, 0x02, 0x00,
            0x06, 0x30, 0x00, 0x00, 0x00, 0x00,
            0x04, 0x24, 0x03, 0x00 };
        UCHAR more[sizeof(hsCfg) + sizeof(tail)];

        memcpy(more, hsCfg, sizeof(hsCfg));
        memcpy(more + sizeof(hsCfg), tail, sizeof(tail));
        more[2] = (UCHAR)sizeof(more);
        CHECK_EQ(UasParseConfig(more, sizeof(more), &info), UAS_CFG_OK,
                 "36.46: a surplus endpoint after the four");
        CHECK(!info.SuperSpeed, "its companion is not the setting's");
        CHECK_EQ(info.EndpointAddress[UAS_PIPE_DATA_IN - 1], 0x83,
                 "its usage does not rename data-in");

        /* bNumEndpoints 3: data-out is surplus, so no pipe is named
         * data-out, as the bus would open only three. */
        memcpy(more, hsCfg, sizeof(hsCfg));
        more[32 + 4] = 3;
        CHECK_EQ(UasParseConfig(more, sizeof(hsCfg), &info),
                 UAS_CFG_BAD_PIPES, "36.46: data-out past bNumEndpoints 3");
    }
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
    CHECK_EQ(UasCdbDirection(0x4D), UAS_DIR_IN, "LOG SENSE");
    CHECK_EQ(UasCdbDirection(0xA2), UAS_DIR_IN, "SECURITY PROTOCOL IN");
    CHECK_EQ(UasCdbDirection(0xB5), UAS_DIR_OUT, "SECURITY PROTOCOL OUT");
    CHECK_EQ(UasCdbDirection(0x93), UAS_DIR_OUT, "WRITE SAME(16)");
    CHECK_EQ(UasCdbDirection(0x42), UAS_DIR_NONE,
             "0x42: UNMAP or READ SUB-CHANNEL, not told by the opcode");
    {
        UCHAR ata[16];
        ULONG k;

        for (k = 0; k < 16; k++) {
            ata[k] = 0;
        }
        ata[0] = 0x85;
        ata[2] = 0x0E;
        CHECK_EQ(UasCdbDataDirection(ata, 16), UAS_DIR_IN,
                 "ATA PASS-THROUGH(16), T_DIR 1");
        ata[2] = 0x06;
        CHECK_EQ(UasCdbDataDirection(ata, 16), UAS_DIR_OUT,
                 "ATA PASS-THROUGH(16), T_DIR 0");
        ata[0] = 0xA1;
        ata[2] = 0x0E;
        CHECK_EQ(UasCdbDataDirection(ata, 12), UAS_DIR_IN,
                 "ATA PASS-THROUGH(12), T_DIR 1");
        CHECK_EQ(UasCdbDataDirection(ata, 6), UasCdbDirection(0xA1),
                 "a short 0xA1 CDB is not ATA PASS-THROUGH");
        ata[0] = 0x28;
        CHECK_EQ(UasCdbDataDirection(ata, 10), UAS_DIR_IN,
                 "any other opcode is the table's");
    }
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

/* Task 36.21: the telemetry's buckets are the bus's (xhci_xstat.c,
 * test_snap), and its sums carry. */
static void test_tele(void)
{
    ULONG lo;
    ULONG hi;

    CHECK_EQ(UasTeleBucket(512), 0, "a sector in the first");
    CHECK_EQ(UasTeleBucket(65536), 2, "64 KiB in the third");
    CHECK_EQ(UasTeleBucket(65537), 3, "past it, 128 KiB's");
    CHECK_EQ(UasTeleBucket(1048576), 6, "1 MiB");
    CHECK_EQ(UasTeleBucket(2097152), 7, "2 MiB");
    CHECK_EQ(UasTeleBucket(2097153), 8, "above 2 MiB");
    CHECK_EQ(UasTeleDepthBucket(0), 0, "none reads as one");
    CHECK_EQ(UasTeleDepthBucket(3), 2, "three to four");
    CHECK_EQ(UasTeleDepthBucket(30), 5, "thirty: seventeen and more");
    lo = 0xFFFF0000UL;
    hi = 0;
    UasTeleAdd64(&lo, &hi, 0x20000UL);
    CHECK(lo == 0x10000UL && hi == 1, "a sum carries");
}

/* Task 36.22: the bus's answer to the transfer-policy question, as
 * uasAskPolicy (uas_fdo.c) hands it over. */
static void good_reply(XHCI98_TRANSFER_POLICY *r, ULONG bytes)
{
    memset(r, 0, sizeof(*r));
    r->Signature = XHCI98_POLICY_SIGNATURE;
    r->Version = XHCI98_POLICY_VERSION;
    r->Size = sizeof(*r);
    r->Status = XHCI98_POLICY_SUCCESS;
    r->CommandBytes = bytes;
    r->MinBytes = XHCI98_POLICY_MIN_BYTES;
    r->MaxBytes = XHCI98_POLICY_MAX_BYTES;
    r->StepBytes = XHCI98_POLICY_STEP_BYTES;
}

static void test_policy(void)
{
    XHCI98_TRANSFER_POLICY r;
    ULONG src;

    CHECK_EQ(XHCI98_POLICY_MIN_BYTES, 64UL * 1024, "64 KiB at least");
    CHECK_EQ(XHCI98_POLICY_MAX_BYTES, 2048UL * 1024, "2 MiB at most");
    CHECK_EQ(XHCI98_POLICY_DEFAULT_BYTES, 1024UL * 1024, "1 MiB default");
    CHECK_EQ(sizeof(XHCI98_TRANSFER_POLICY), 12 * 4,
             "a pointer-free request of twelve words");

    good_reply(&r, 0x100000UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x100000UL,
             "1 MiB taken");
    CHECK_EQ(src, XHCI98_TELE_SRC_BUS, "from the bus");
    good_reply(&r, 0x10000UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "64 KiB taken as the bus's own");
    CHECK_EQ(src, XHCI98_TELE_SRC_BUS, "still the bus's");
    good_reply(&r, 0x200000UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x200000UL,
             "2 MiB taken");
    good_reply(&r, 0x11000UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x11000UL,
             "68 KiB, a step, taken");

    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_UNSUPPORTED, NULL, &src),
             0x10000UL, "an older bus: 64 KiB");
    CHECK_EQ(src, XHCI98_TELE_SRC_UNSUPPORTED, "said as unsupported");
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_FAILED, NULL, &src), 0x10000UL,
             "a failed request: 64 KiB");
    CHECK_EQ(src, XHCI98_TELE_SRC_TRANSPORT, "said as transport");
    good_reply(&r, 0x100000UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_FAILED, &r, &src), 0x10000UL,
             "a failed request's buffer is not read");
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_REFUSED, &r, &src), 0x10000UL,
             "a refused header: 64 KiB");
    CHECK_EQ(src, XHCI98_TELE_SRC_MALFORMED, "said as out of contract");
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, NULL, &src), 0x10000UL,
             "success with no reply: 64 KiB");

    good_reply(&r, 0x100000UL);
    r.Signature = 0;
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "a wrong signature");
    CHECK_EQ(src, XHCI98_TELE_SRC_MALFORMED, "out of contract");
    good_reply(&r, 0x100000UL);
    r.Version = 2;
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "another version");
    good_reply(&r, 0x100000UL);
    r.Size = 8;
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "another size");
    good_reply(&r, 0x100000UL);
    r.Status = XHCI98_POLICY_INVALID;
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "a status that is not success");
    good_reply(&r, 0xF000UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "below the range");
    good_reply(&r, 0x201000UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "above it");
    good_reply(&r, 0x100200UL);
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "off a step");
    good_reply(&r, 0x100000UL);
    r.StepBytes = 512;
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "another step");
    good_reply(&r, 0x100000UL);
    r.MaxBytes = 0x400000UL;
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "another range");
    good_reply(&r, 0x100000UL);
    r.Reserved[3] = 1;
    CHECK_EQ(UasPolicyValue(UAS_POLICY_SENT_OK, &r, &src), 0x10000UL,
             "a reserved word set");
    CHECK_EQ(src, XHCI98_TELE_SRC_MALFORMED, "out of contract");
}

/* The device's own usbstor MaximumTransferLength (KB 2581464) over the
 * bus's answer: taken only valid and only with the bus's answer, rounded
 * to the nearest 4 KiB (uas_iu.h). */
static ULONG devlim(ULONG busBytes, ULONG busSource, ULONG found, ULONG type,
                    ULONG length, ULONG value, PULONG src, PULONG why)
{
    return UasDeviceLimit(busBytes, busSource, found, type, length, value,
                          src, why);
}

static void test_devlim(void)
{
    const ULONG D = UAS_DEVLIM_REG_DWORD;
    const ULONG BUS = XHCI98_TELE_SRC_BUS;
    ULONG src;
    ULONG why;

    CHECK_EQ(devlim(0x100000UL, BUS, 0, 0, 0, 0, &src, &why), 0x100000UL,
             "absent: the bus's 1 MiB");
    CHECK_EQ(why, UAS_DEVLIM_ABSENT, "said absent");
    CHECK_EQ(src, BUS, "the bus's still");

    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x10000UL, &src, &why),
             0x10000UL, "64 KiB for this device under a 1 MiB bus");
    CHECK_EQ(why, UAS_DEVLIM_USED, "said used");
    CHECK_EQ(src, XHCI98_TELE_SRC_DEVICE, "the device's");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x180000UL, &src, &why),
             0x180000UL, "1.5 MiB above the bus's 1 MiB: the device's wins");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x200000UL, &src, &why),
             0x200000UL, "2097152, the tools' 2 MiB, is 2 MiB (owner)");
    CHECK_EQ(why, UAS_DEVLIM_USED, "so in range");
    CHECK_EQ(src, XHCI98_TELE_SRC_DEVICE, "the device's");
    CHECK_EQ(devlim(0x10000UL, BUS, 1, D, 4, 0x100000UL, &src, &why),
             0x100000UL, "1 MiB above a 64 KiB bus value");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x1E000UL, &src, &why),
             0x1E000UL, "120 KiB (240 sectors) on the grid, unmoved");

    /* KB 2581464's ends: Microsoft's spellings of 64 KiB and 2 MiB. */
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 65535UL, &src, &why),
             0x10000UL, "65535 is 64 KiB");
    CHECK_EQ(why, UAS_DEVLIM_USED, "the range's low end is in it");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 2097120UL, &src, &why),
             0x200000UL, "2097120 is 2 MiB");
    CHECK_EQ(why, UAS_DEVLIM_USED, "the range's high end is in it");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 65534UL, &src, &why),
             0x100000UL, "65534 is below the range: the bus's");
    CHECK_EQ(why, UAS_DEVLIM_RANGE, "said out of range");
    CHECK_EQ(src, BUS, "the bus's source kept");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 2097121UL, &src, &why),
             0x200000UL, "2097121, between the two spellings, is 2 MiB");
    CHECK_EQ(why, UAS_DEVLIM_USED, "inside the widened range");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 2097153UL, &src, &why),
             0x100000UL, "2097153 is above it");
    CHECK_EQ(why, UAS_DEVLIM_RANGE, "out of range");
    CHECK_EQ(src, BUS, "the bus's source kept");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x400000UL, &src, &why),
             0x100000UL, "4 MiB is above it");
    CHECK_EQ(why, UAS_DEVLIM_RANGE, "out of range");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0UL, &src, &why), 0x100000UL,
             "0 is out of range, not off");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0xFFFFFFFFUL, &src, &why),
             0x100000UL, "all ones");

    /* Nearest 4 KiB, ties up; never more than 2 KiB moved. */
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x80000UL + 2047UL, &src,
                    &why), 0x80000UL, "2047 over 512 KiB rounds down");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x80000UL + 2048UL, &src,
                    &why), 0x81000UL, "a tie rounds up");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x80000UL - 2048UL, &src,
                    &why), 0x80000UL, "2048 under rounds up to it");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 0x80000UL - 2049UL, &src,
                    &why), 0x7F000UL, "2049 under rounds down");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 1000000UL, &src, &why),
             999424UL, "a decimal megabyte: 1000000 to 244 pages");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 66000UL, &src, &why),
             0x10000UL, "66000 rounds to 64 KiB");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 4, 2095000UL, &src, &why),
             0x1FF000UL, "2095000 rounds to 2 MiB less 4 KiB");

    /* Only a four-byte REG_DWORD. */
    CHECK_EQ(devlim(0x100000UL, BUS, 1, 1UL, 4, 0x10000UL, &src, &why),
             0x100000UL, "a REG_SZ is ignored");
    CHECK_EQ(why, UAS_DEVLIM_TYPE, "said its type");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, 3UL, 4, 0x10000UL, &src, &why),
             0x100000UL, "four bytes of REG_BINARY are not a DWORD");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, 11UL, 8, 0, &src, &why), 0x100000UL,
             "a REG_QWORD is ignored");
    CHECK_EQ(devlim(0x100000UL, BUS, 1, D, 2, 0, &src, &why), 0x100000UL,
             "a short DWORD is ignored");
    CHECK_EQ(why, UAS_DEVLIM_TYPE, "said its type");

    /* No bus answer: 64 KiB stands whatever the device's value says. */
    CHECK_EQ(devlim(0x10000UL, XHCI98_TELE_SRC_UNSUPPORTED, 1, D, 4,
                    2097120UL, &src, &why), 0x10000UL,
             "an older bus keeps 64 KiB");
    CHECK_EQ(why, UAS_DEVLIM_NO_BUS, "said not applied");
    CHECK_EQ(src, XHCI98_TELE_SRC_UNSUPPORTED, "the older bus's source");
    CHECK_EQ(devlim(0x10000UL, XHCI98_TELE_SRC_TRANSPORT, 1, D, 4,
                    0x10000UL, &src, &why), 0x10000UL,
             "a failed question keeps 64 KiB");
    CHECK_EQ(why, UAS_DEVLIM_NO_BUS, "not applied either");
    CHECK_EQ(devlim(0x10000UL, XHCI98_TELE_SRC_MALFORMED, 1, D, 4,
                    0x100000UL, &src, &why), 0x10000UL,
             "a reply out of contract keeps 64 KiB");
    CHECK_EQ(devlim(0x10000UL, XHCI98_TELE_SRC_UNSUPPORTED, 1, D, 4,
                    0x10UL, &src, &why), 0x10000UL,
             "an invalid value on an older bus is said invalid");
    CHECK_EQ(why, UAS_DEVLIM_RANGE, "range before the bus");
}

/* Task 45.23 (design record 20 section 4.3.3): the sense fold, from SPC's
 * fixed and descriptor formats worked by hand. */
static void test_sense_class(void)
{
    UCHAR s[18];

    memset(s, 0, sizeof(s));
    s[0] = 0x70; s[2] = 0x06; s[12] = 0x29;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_UA, "fixed: power on, reset");
    s[12] = 0x28;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_UA_MEDIUM,
             "fixed: not ready to ready, medium may have changed");
    s[2] = 0x02; s[12] = 0x3A;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_NO_MEDIUM, "fixed: no medium");
    s[12] = 0x04;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_BECOMING,
             "fixed: becoming ready or a start needed");
    s[12] = 0x05;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_OTHER,
             "fixed: NOT READY of another kind");
    s[2] = 0x05; s[12] = 0x20;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_ILLEGAL, "fixed: illegal");
    s[2] = 0x03;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_OTHER, "fixed: medium error");
    s[2] = 0x06; s[12] = 0x28;
    CHECK_EQ(UasSenseClass(s, 12), UAS_SENSE_OTHER,
             "fixed, too short for its ASC");
    s[0] = 0xF1;
    CHECK_EQ(UasSenseClass(s, 18), UAS_SENSE_UA_MEDIUM,
             "fixed deferred, valid bit set");
    memset(s, 0, sizeof(s));
    s[0] = 0x72; s[1] = 0x06; s[2] = 0x28;
    CHECK_EQ(UasSenseClass(s, 8), UAS_SENSE_UA_MEDIUM, "descriptor: 28h");
    s[1] = 0x02; s[2] = 0x3A;
    CHECK_EQ(UasSenseClass(s, 8), UAS_SENSE_NO_MEDIUM, "descriptor: 3Ah");
    s[0] = 0x00;
    CHECK_EQ(UasSenseClass(s, 8), UAS_SENSE_OTHER, "no response code");
    CHECK_EQ(UasSenseClass(NULL, 18), UAS_SENSE_OTHER, "no sense");
    CHECK_EQ(UasSenseClass(s, 2), UAS_SENSE_OTHER, "two bytes");
}

static void test_vpd_list(void)
{
    static const UCHAR p0[] = { 0x00, 0x00, 0x00, 0x03, 0x00, 0x80, 0x83 };
    static const UCHAR other[] = { 0x00, 0x80, 0x00, 0x01, 0x80 };
    static const UCHAR longer[] = { 0x00, 0x00, 0x00, 0x09, 0x00, 0x80 };

    CHECK_EQ(UasVpdListHas(p0, sizeof(p0), 0x80), 1, "page 0x80 listed");
    CHECK_EQ(UasVpdListHas(p0, sizeof(p0), 0x83), 1, "page 0x83 listed");
    CHECK_EQ(UasVpdListHas(p0, sizeof(p0), 0x89), 0, "page 0x89 not");
    CHECK_EQ(UasVpdListHas(p0, 5, 0x80), 0,
             "a list cut short names nothing, not even what came");
    CHECK_EQ(UasVpdListComplete(p0, 5), 0, "a list cut short is incomplete");
    CHECK_EQ(UasVpdListComplete(p0, sizeof(p0)), 1, "the whole list");
    CHECK_EQ(UasVpdListHas(other, sizeof(other), 0x80), 0,
             "not page 0x00");
    CHECK_EQ(UasVpdListHas(longer, sizeof(longer), 0x80), 0,
             "a list longer than what came: no page taken from it");
    CHECK_EQ(UasVpdListComplete(longer, sizeof(longer)), 0,
             "nor is it complete");
    CHECK_EQ(UasVpdListHas(p0, 3, 0x00), 0, "no header");
}

static void id_fixed(UAS_LUN_ID *id)
{
    memset(id, 0, sizeof(*id));
    id->Read = 1;
    id->SerialLength = 4;
    memcpy(id->Serial, "S123", 4);
    id->DevIdLength = 8;
    memcpy(id->DevId, "\x01\x03\x00\x04\x50\x01\x02\x03", 8);
    id->CapState = UAS_ID_CAP_VALID;
    id->Cap[7] = 0xFF;
    id->Cap[10] = 0x02;
}

static void test_id_match(void)
{
    UAS_LUN_ID was;
    UAS_LUN_ID now;

    id_fixed(&was);
    id_fixed(&now);
    CHECK_EQ(UasIdMatch(&was, &now), 1, "the same fixed disk");
    now.Serial[3] = '4';
    CHECK_EQ(UasIdMatch(&was, &now), 0, "another unit serial");
    id_fixed(&now);
    now.SerialLength = 3;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "a serial of another length");
    id_fixed(&now);
    now.DevId[7] = 0x04;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "another designator");
    id_fixed(&now);
    now.Cap[7] = 0xFE;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "another capacity");
    id_fixed(&now);
    now.Cap[2] = 0x01;
    CHECK_EQ(UasIdMatch(&was, &now), 0,
             "a capacity differing above READ CAPACITY (10)'s range");
    id_fixed(&now);
    now.Cap[10] = 0x10;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "another block length");
    id_fixed(&now);
    now.MediumChanged = 1;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "attention 28h fails the check");
    id_fixed(&now);
    now.Read = 0;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "a read that failed now");
    id_fixed(&now);
    was.Read = 0;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "a record never read");
    id_fixed(&was);
    was.SerialLength = 0;
    now.SerialLength = 0;
    CHECK_EQ(UasIdMatch(&was, &now), 1, "the designator page alone");
    was.DevIdLength = 0;
    now.DevIdLength = 0;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "neither page: fails safe");
    id_fixed(&was);
    id_fixed(&now);
    was.DevIdLength = 0;
    CHECK_EQ(UasIdMatch(&was, &now), 0,
             "a page that appeared since the record");
    id_fixed(&was);
    now.CapState = UAS_ID_CAP_NO_MEDIUM;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "a fixed disk with no medium now");
    id_fixed(&now);
    was.CapState = UAS_ID_CAP_FAILED;
    now.CapState = UAS_ID_CAP_FAILED;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "capacity unread both times");

    id_fixed(&was);
    id_fixed(&now);
    was.Removable = 1;
    now.Removable = 1;
    CHECK_EQ(UasIdMatch(&was, &now), 0,
             "removable with a medium: fails safe, however alike");
    was.CapState = UAS_ID_CAP_NO_MEDIUM;
    now.CapState = UAS_ID_CAP_NO_MEDIUM;
    CHECK_EQ(UasIdMatch(&was, &now), 1, "removable, empty both times");
    now.CapState = UAS_ID_CAP_VALID;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "removable, a medium inserted");
    now.Removable = 0;
    CHECK_EQ(UasIdMatch(&was, &now), 0, "the removable bit changed");
    CHECK_EQ(UasIdMatch(NULL, &now), 0, "no record");
}

static void test_wake(void)
{
    CHECK_EQ(UasWakeDecide(0, 0, 0, 0, 0, 0, 0, 0), UAS_WAKE_RELEASE,
             "an older bus: nothing kept, released");
    CHECK_EQ(UasWakeDecide(1, 1, 0, 0, 0, 0, 0, 0), UAS_WAKE_RELEASE,
             "withdrawn and not declared: released unasked");
    CHECK_EQ(UasWakeDecide(1, 0, 0, 1, XHCI98_RESUME_RUNNING, 3, 3, 0),
             UAS_WAKE_SAME,
             "a withdrawal that failed takes the query path");
    CHECK_EQ(UasWakeDecide(1, 1, 1, 1, XHCI98_RESUME_RUNNING, 3, 3, 0),
             UAS_WAKE_SAME, "declared, no re-initialization: released");
    CHECK_EQ(UasWakeDecide(1, 1, 1, 1, XHCI98_RESUME_RUNNING, 4, 3, 0),
             UAS_WAKE_CHECK, "declared and kept: the medium is checked");
    CHECK_EQ(UasWakeDecide(1, 1, 1, 1, XHCI98_RESUME_PENDING, 3, 3, 0),
             UAS_WAKE_WAIT, "the bus's pass still to end: wait");
    CHECK_EQ(UasWakeDecide(1, 1, 1, 1, XHCI98_RESUME_PENDING, 3, 3, 1),
             UAS_WAKE_FAIL, "still pending at the deadline: failed");
    CHECK_EQ(UasWakeDecide(1, 1, 1, 1, XHCI98_RESUME_FAILED, 3, 3, 0),
             UAS_WAKE_FAIL, "a failed resume: failed, never released");
    CHECK_EQ(UasWakeDecide(1, 1, 1, 0, XHCI98_RESUME_RUNNING, 3, 3, 0),
             UAS_WAKE_FAIL, "the query failed: failed");
    CHECK_EQ(UasWakeDecide(1, 1, 1, 1, 7, 3, 3, 0), UAS_WAKE_FAIL,
             "a state out of contract: failed");
    CHECK_EQ(UasWakeDecide(1, 0, 1, 1, XHCI98_RESUME_RUNNING, 0, 0xFFFFFFFFUL,
                           0),
             UAS_WAKE_CHECK, "an epoch that wrapped is still a change");
}

/* ------------------------------------------------------------------ */
/* Task 45.52: pass-through and SMART (design record 13 section 16).  */
/* The header offsets are the WDK 7.1 ntddscsi.h declarations', the   */
/* CDB and sense bytes SAT-3's and the SMART bytes ACS-3's and        */
/* ntdddisk.h's, each worked by hand.                                 */
/* ------------------------------------------------------------------ */

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

static ULONG get32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

/* SCSI_PASS_THROUGH: Length 0, ScsiStatus 2, CdbLength 6,
 * SenseInfoLength 7, DataIn 8, DataTransferLength 12, TimeOutValue 16,
 * DataBufferOffset 20 (32) or 24 (64, eight bytes), SenseInfoOffset 24 or
 * 32, Cdb 28 or 36. */
static void spt(UCHAR *b, ULONG layout, ULONG cdbLength, ULONG senseLength,
                ULONG dataIn, ULONG length, ULONG timeout, ULONG dataLo,
                ULONG dataHi, ULONG senseOffset, const UCHAR *cdb)
{
    ULONG wide;

    wide = layout == UAS_PT_LAYOUT_64;
    memset(b, 0xCC, 1024);
    b[0] = (UCHAR)(wide ? 56 : 44);
    b[1] = 0;
    b[2] = 0xEE;
    b[6] = (UCHAR)cdbLength;
    b[7] = (UCHAR)senseLength;
    b[8] = (UCHAR)dataIn;
    put32(b + 12, length);
    put32(b + 16, timeout);
    put32(b + (wide ? 24 : 20), dataLo);
    if (wide) {
        put32(b + 28, dataHi);
    }
    put32(b + (wide ? 32 : 24), senseOffset);
    memcpy(b + (wide ? 36 : 28), cdb, 16);
}

/* ATA_PASS_THROUGH_EX: Length 0, AtaFlags 2, DataTransferLength 8,
 * TimeOutValue 12, DataBufferOffset 20 (32) or 24 (64), PreviousTaskFile
 * 24 or 32, CurrentTaskFile 32 or 40. */
static void apt(UCHAR *b, ULONG layout, ULONG flags, ULONG length,
                ULONG timeout, ULONG dataLo, const UCHAR *prev,
                const UCHAR *cur)
{
    ULONG wide;

    wide = layout == UAS_PT_LAYOUT_64;
    memset(b, 0xCC, 1024);
    b[0] = (UCHAR)(wide ? 48 : 40);
    b[1] = 0;
    b[2] = (UCHAR)flags;
    b[3] = (UCHAR)(flags >> 8);
    put32(b + 8, length);
    put32(b + 12, timeout);
    put32(b + (wide ? 24 : 20), dataLo);
    if (wide) {
        put32(b + 28, 0);
    }
    memcpy(b + (wide ? 32 : 24), prev, 8);
    memcpy(b + (wide ? 40 : 32), cur, 8);
}

static UCHAR ptbuf[1024];

static void test_pt_basics(void)
{
    CHECK_EQ(UasPtHeaderSize(UAS_PT_SCSI, UAS_PT_LAYOUT_32), 44,
             "SCSI_PASS_THROUGH, and _32 on x64: 44 bytes");
    CHECK_EQ(UasPtHeaderSize(UAS_PT_SCSI_DIRECT, UAS_PT_LAYOUT_64), 56,
             "SCSI_PASS_THROUGH_DIRECT, 64-bit: 56 bytes");
    CHECK_EQ(UasPtHeaderSize(UAS_PT_ATA, UAS_PT_LAYOUT_32), 40,
             "ATA_PASS_THROUGH_EX, and _32: 40 bytes");
    CHECK_EQ(UasPtHeaderSize(UAS_PT_ATA_DIRECT, UAS_PT_LAYOUT_64), 48,
             "ATA_PASS_THROUGH_DIRECT, 64-bit: 48 bytes");
    CHECK_EQ(UasPtHeaderSize(4, UAS_PT_LAYOUT_32), 0, "an unknown kind");
    CHECK_EQ(UasPtHeaderSize(UAS_PT_SCSI, 2), 0, "an unknown layout");

    CHECK_EQ(UasPtTimeout(0), 30, "0: the class default, 30 s");
    CHECK_EQ(UasPtTimeout(1), 1, "1 s as asked");
    CHECK_EQ(UasPtTimeout(120), 120, "120 s as asked");
    CHECK_EQ(UasPtTimeout(121), 120, "above 120: clamped");
    CHECK_EQ(UasPtTimeout(0xFFFFFFFFUL), 120, "the largest: clamped");

    CHECK(UasPtMultiTarget(0x18), "COPY addresses other targets");
    CHECK(UasPtMultiTarget(0x39), "COMPARE");
    CHECK(UasPtMultiTarget(0x3A), "COPY AND VERIFY");
    CHECK(UasPtMultiTarget(0x83), "EXTENDED COPY and the token commands");
    CHECK(!UasPtMultiTarget(0x12), "INQUIRY is single-target");
    CHECK(!UasPtMultiTarget(0x85), "ATA PASS-THROUGH (16) is single-target");
}

static void test_pt_scsi_plan(void)
{
    static const UCHAR inquiry[16] = { 0x12, 0, 0, 0, 0x24, 0 };
    static const UCHAR none42[16] = { 0x42, 0, 0, 0, 0, 0, 0, 0, 8, 0 };
    static const UCHAR satIn[16] = { 0x85, 0x08, 0x0E, 0, 0, 0, 1, 0, 0,
                                     0, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR satOut[16] = { 0x85, 0x0A, 0x06, 0, 0, 0, 1, 0, 0,
                                      0, 0, 0, 0, 0xA0, 0x30, 0 };
    static const UCHAR satCkData[16] = { 0x85, 0x08, 0x2E, 0, 0, 0, 1, 0,
                                         0, 0, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR satCkNone[16] = { 0x85, 0x06, 0x20, 0, 0xDA, 0, 0,
                                         0, 0, 0, 0x4F, 0, 0xC2, 0xA0, 0xB0,
                                         0 };
    static const UCHAR xcopy[16] = { 0x83, 0x00 };
    static const UCHAR sat12Ck[16] = { 0xA1, 0x08, 0x2E, 0, 1, 0, 0, 0, 0xA0,
                                       0xEC, 0, 0 };
    UAS_PT_PLAN p;

    /* INQUIRY, 36 bytes in at 76, 32 bytes of sense at 44. */
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 36, 0, 76, 0, 44, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_OK, "INQUIRY, data in at 76");
    CHECK_EQ(p.HeaderSize, 44, "header 44");
    CHECK_EQ(p.Dir, UAS_DIR_IN, "data in");
    CHECK_EQ(p.DataOffset, 76, "data at 76");
    CHECK_EQ(p.DataLength, 36, "36 bytes");
    CHECK_EQ(p.SenseOffset, 44, "sense at 44");
    CHECK_EQ(p.SenseLength, 32, "32 sense bytes");
    CHECK_EQ(p.Timeout, 30, "timeout 0 asks for 30 s");
    CHECK_EQ(p.CdbLength, 6, "a 6-byte CDB");
    CHECK(bytes_eq(p.Cdb, inquiry, 6), "the CDB as sent");

    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 43, 112, 65536,
                       &p), UAS_PT_INVALID, "input shorter than the header");
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 43, 65536,
                       &p), UAS_PT_TOO_SMALL, "output shorter than the header");
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_64, ptbuf, 112, 112, 65536,
                       &p), UAS_PT_INVALID,
             "a 32-bit header read as the 64-bit layout: Length differs");
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 111, 65536,
                       &p), UAS_PT_INVALID, "data in runs past the output");
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 35,
                       &p), UAS_PT_INVALID, "longer than the LUN's maximum");

    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 0x20, 600, 0xFFFFFFF0UL, 0, 44,
        inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_INVALID,
             "offset FFFFFFF0h with 20h bytes: no wrap");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 36, 600, 40, 0, 76, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_INVALID, "data over the header");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 36, 600, 76, 0, 60, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_INVALID, "sense overlapping the data");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 36, 600, 76, 0, 40, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_INVALID, "sense over the header");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 36, 600, 44, 0, 80, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_OK, "sense after the data");
    CHECK_EQ(p.Timeout, 120, "600 s clamped to 120");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 36, 0, 44, 0, 81, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_INVALID, "sense past the output");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 253, 1, 0, 0, 0, 0, 44, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 400, 65536,
                       &p), UAS_PT_INVALID, "253 sense bytes: over the limit");
    spt(ptbuf, UAS_PT_LAYOUT_32, 0, 0, 1, 0, 0, 0, 0, 0, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 44, 65536,
                       &p), UAS_PT_INVALID, "a CDB of 0 bytes");
    spt(ptbuf, UAS_PT_LAYOUT_32, 17, 0, 1, 0, 0, 0, 0, 0, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 44, 65536,
                       &p), UAS_PT_INVALID, "a CDB of 17 bytes");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 0, 3, 0, 0, 0, 0, 0, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 44, 65536,
                       &p), UAS_PT_INVALID, "DataIn 3");
    spt(ptbuf, UAS_PT_LAYOUT_32, 16, 0, 1, 0, 0, 0, 0, 0, xcopy);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 44, 65536,
                       &p), UAS_PT_INVALID, "EXTENDED COPY refused");

    /* Direction unspecified. */
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 0, 2, 36, 0, 44, 0, 0, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 80, 65536,
                       &p), UAS_PT_OK, "INQUIRY, direction unspecified");
    CHECK_EQ(p.Dir, UAS_DIR_IN, "the opcode table says in");
    spt(ptbuf, UAS_PT_LAYOUT_32, 10, 0, 2, 8, 0, 44, 0, 0, none42);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 80, 65536,
                       &p), UAS_PT_INVALID,
             "an opcode with no direction, with data: refused");
    spt(ptbuf, UAS_PT_LAYOUT_32, 16, 0, 2, 512, 0, 44, 0, 0, satIn);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 556, 65536,
                       &p), UAS_PT_OK, "ATA PT (16), T_DIR in");
    CHECK_EQ(p.Dir, UAS_DIR_IN, "T_DIR 1: in");
    spt(ptbuf, UAS_PT_LAYOUT_32, 16, 0, 2, 512, 0, 44, 0, 0, satOut);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 44, 65536,
                       &p), UAS_PT_INVALID,
             "ATA PT (16), T_DIR out, data not in the input");
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 556, 44, 65536,
                       &p), UAS_PT_OK, "the same with the data in the input");
    CHECK_EQ(p.Dir, UAS_DIR_OUT, "T_DIR 0: out");
    spt(ptbuf, UAS_PT_LAYOUT_32, 16, 0, 1, 512, 0, 44, 0, 0, satCkData);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 556, 65536,
                       &p), UAS_PT_INVALID, "CK_COND with data: refused");
    spt(ptbuf, UAS_PT_LAYOUT_32, 15, 0, 1, 512, 0, 44, 0, 0, satCkData);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 556, 65536,
                       &p), UAS_PT_INVALID,
             "ATA PT (16) cut to 15 bytes: refused, never padded");
    spt(ptbuf, UAS_PT_LAYOUT_32, 15, 0, 1, 512, 0, 44, 0, 0, satIn);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 556, 65536,
                       &p), UAS_PT_INVALID,
             "ATA PT (16) of 15 bytes, no CK_COND: refused as well");
    spt(ptbuf, UAS_PT_LAYOUT_32, 11, 0, 1, 512, 0, 44, 0, 0, sat12Ck);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 556, 65536,
                       &p), UAS_PT_INVALID, "ATA PT (12) cut to 11 bytes");
    spt(ptbuf, UAS_PT_LAYOUT_32, 12, 0, 1, 512, 0, 44, 0, 0, sat12Ck);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 556, 65536,
                       &p), UAS_PT_INVALID, "ATA PT (12) CK_COND with data");
    spt(ptbuf, UAS_PT_LAYOUT_32, 16, 32, 2, 0, 0, 0, 0, 44, satCkNone);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 76, 65536,
                       &p), UAS_PT_OK, "CK_COND with no data: served");
    CHECK_EQ(p.Dir, UAS_DIR_NONE, "no data");

    /* The 64-bit layout. */
    spt(ptbuf, UAS_PT_LAYOUT_64, 6, 32, 1, 36, 5, 100, 0, 56, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_64, ptbuf, 56, 136, 65536,
                       &p), UAS_PT_OK, "64-bit: INQUIRY at 100");
    CHECK_EQ(p.HeaderSize, 56, "header 56");
    CHECK_EQ(p.DataOffset, 100, "the offset's low half");
    CHECK_EQ(p.SenseOffset, 56, "SenseInfoOffset at 32");
    CHECK_EQ(p.Timeout, 5, "5 s");
    CHECK(bytes_eq(p.Cdb, inquiry, 6), "the CDB at 36");
    spt(ptbuf, UAS_PT_LAYOUT_64, 6, 32, 1, 36, 5, 100, 1, 56, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_64, ptbuf, 56, 136, 65536,
                       &p), UAS_PT_INVALID, "an offset above 32 bits");
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 136, 136, 65536,
                       &p), UAS_PT_INVALID,
             "a 64-bit header read as the 32-bit layout");

    /* The direct form: a pointer, no region in the buffer. */
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 0, 1, 512, 0, 0x00401000UL, 0, 0,
        inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI_DIRECT, UAS_PT_LAYOUT_32, ptbuf, 44, 44,
                       65536, &p), UAS_PT_OK, "direct: a 32-bit pointer");
    CHECK_EQ(p.DataPtrLo, 0x00401000UL, "the pointer");
    CHECK_EQ(p.DataPtrHi, 0, "no high half");
    spt(ptbuf, UAS_PT_LAYOUT_64, 6, 0, 1, 512, 0, 0x00401000UL, 0x7FF,
        0, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI_DIRECT, UAS_PT_LAYOUT_64, ptbuf, 56, 56,
                       65536, &p), UAS_PT_OK, "direct: a 64-bit pointer");
    CHECK_EQ(p.DataPtrHi, 0x7FF, "its high half at 28");
    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 0, 1, 512, 0, 0, 0, 0, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI_DIRECT, UAS_PT_LAYOUT_32, ptbuf, 44, 44,
                       65536, &p), UAS_PT_INVALID, "direct: a null pointer");
}

static void test_ata_cdb(void)
{
    static const UCHAR zero[8] = { 0 };
    static const UCHAR ident[8] = { 0x00, 0x01, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR ident0[8] = { 0x00, 0x00, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR ident2[8] = { 0x00, 0x02, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR smartD1[8] = { 0xD1, 0x07, 1, 0x4F, 0xC2, 0xA0, 0xB0,
                                      0 };
    static const UCHAR pktId[8] = { 0x00, 0x00, 0, 0, 0, 0xA0, 0xA1, 0 };
    static const UCHAR identWant[16] = { 0x85, 0x08, 0x0E, 0, 0x00, 0, 0x01,
                                         0, 0, 0, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR rdPrev[8] = { 0, 0, 0x12, 0x34, 0x56, 0, 0, 0 };
    static const UCHAR rdCur[8] = { 0, 0x02, 0x78, 0x9A, 0xBC, 0x40, 0x25,
                                    0 };
    static const UCHAR rdWant[16] = { 0x85, 0x0D, 0x0E, 0, 0, 0, 0x02, 0x12,
                                      0x78, 0x34, 0x9A, 0x56, 0xBC, 0x40,
                                      0x25, 0 };
    static const UCHAR rs[8] = { 0xDA, 0, 0, 0x4F, 0xC2, 0xA0, 0xB0, 0 };
    static const UCHAR rsWant[16] = { 0x85, 0x06, 0x20, 0, 0xDA, 0, 0, 0, 0,
                                      0, 0x4F, 0, 0xC2, 0xA0, 0xB0, 0 };
    static const UCHAR save[8] = { 0xD2, 0xF1, 0, 0x4F, 0xC2, 0xA0, 0xB0, 0 };
    static const UCHAR wr[8] = { 0, 0x01, 0, 0, 0, 0xE0, 0x30, 0 };
    static const UCHAR rd2[8] = { 0, 0x02, 0, 0, 0, 0xE0, 0x20, 0 };
    static const UCHAR rd0[8] = { 0, 0x00, 0, 0, 0, 0xE0, 0xC8, 0 };
    UCHAR cdb[16];

    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, ident, 512, 0, cdb), UAS_PT_OK,
             "IDENTIFY DEVICE, PIO in");
    CHECK(bytes_eq(cdb, identWant, 16),
          "85h; PIO in (4); T_DIR, BYTE_BLOCK, T_LENGTH 2; count 1; ECh");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, ident0, 512, 0, cdb), UAS_PT_OK,
             "IDENTIFY with count 0: its count is not applicable");
    CHECK(bytes_eq(cdb, identWant, 16), "sent with count 1");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, ident2, 512, 0, cdb), UAS_PT_OK,
             "IDENTIFY with count 2 and one block");
    CHECK(bytes_eq(cdb, identWant, 16), "sent with count 1 too");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, ident2, 1024, 0, cdb),
             UAS_PT_INVALID, "IDENTIFY with two blocks: it returns one");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, smartD1, 512, 0, cdb),
             UAS_PT_OK, "READ THRESHOLDS with count 7");
    CHECK_EQ(cdb[6], 1, "count 1");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, pktId, 1024, 0, cdb),
             UAS_PT_INVALID, "IDENTIFY PACKET DEVICE with two blocks");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN | UAS_ATA_48BIT | UAS_ATA_USE_DMA,
                       rdPrev, rdCur, 1024, 0, cdb), UAS_PT_OK,
             "READ DMA EXT, two blocks");
    CHECK(bytes_eq(cdb, rdWant, 16),
          "DMA (6) and EXTEND; LBA bytes 31:24, 7:0 ... 47:40, 23:16");
    CHECK_EQ(UasAtaCdb(0, zero, rs, 0, 1, cdb), UAS_PT_OK,
             "SMART RETURN STATUS, non-data, CK_COND");
    CHECK(bytes_eq(cdb, rsWant, 16), "non-data (3), CK_COND, no length");
    CHECK_EQ(UasAtaCdb(0, zero, save, 0, 1, cdb), UAS_PT_OK,
             "AUTOSAVE: a non-data count register is a parameter");
    CHECK_EQ(cdb[6], 0xF1, "F1h sent unchanged");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_OUT, zero, wr, 512, 0, cdb), UAS_PT_OK,
             "WRITE SECTORS, PIO out");
    CHECK_EQ(cdb[1], 0x0A, "PIO out (5)");
    CHECK_EQ(cdb[2], 0x06, "no T_DIR; BYTE_BLOCK and T_LENGTH 2");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_OUT, zero, wr, 512, 1, cdb), UAS_PT_OK,
             "CK_COND with data is not set");
    CHECK_EQ(cdb[2], 0x06, "no CK_COND with data");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, rd2, 512, 0, cdb),
             UAS_PT_INVALID, "count 2 with 512 bytes");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, rd2, 1025, 0, cdb),
             UAS_PT_INVALID, "a length not a multiple of 512");
    CHECK_EQ(UasAtaCdb(UAS_ATA_DATA_IN, zero, rd0, 512, 0, cdb),
             UAS_PT_INVALID, "count 0 with data, not a count-N/A command");
}

static void test_ata_plan(void)
{
    static const UCHAR zero[8] = { 0 };
    static const UCHAR ident[8] = { 0x00, 0x01, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR power[8] = { 0, 0, 0, 0, 0, 0xA0, 0xE5, 0 };
    UAS_PT_PLAN p;

    apt(ptbuf, UAS_PT_LAYOUT_32, UAS_ATA_DATA_IN, 512, 0, 40, zero, ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 552, 65536,
                       &p), UAS_PT_OK, "IOCTL_ATA_PASS_THROUGH IDENTIFY");
    CHECK_EQ(p.HeaderSize, 40, "header 40");
    CHECK_EQ(p.Dir, UAS_DIR_IN, "in");
    CHECK_EQ(p.DataOffset, 40, "data at 40");
    CHECK_EQ(p.CdbLength, 16, "ATA PASS-THROUGH (16)");
    CHECK_EQ(p.Cdb[14], 0xEC, "the command");
    CHECK(bytes_eq(p.Cur, ident, 8), "the task file kept");
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 551, 65536,
                       &p), UAS_PT_INVALID, "data past the output");
    apt(ptbuf, UAS_PT_LAYOUT_32, 0x20 | UAS_ATA_DATA_IN, 512, 0, 40, zero,
        ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 552, 65536,
                       &p), UAS_PT_INVALID, "an unknown flag");
    apt(ptbuf, UAS_PT_LAYOUT_32, UAS_ATA_DATA_IN | UAS_ATA_DATA_OUT, 512, 0,
        40, zero, ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 552, 552, 65536,
                       &p), UAS_PT_INVALID, "both directions");
    apt(ptbuf, UAS_PT_LAYOUT_32, UAS_ATA_DATA_IN, 0, 0, 40, zero, ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 40, 65536,
                       &p), UAS_PT_INVALID, "a direction and no length");
    apt(ptbuf, UAS_PT_LAYOUT_32, 0, 512, 0, 40, zero, ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 552, 65536,
                       &p), UAS_PT_INVALID, "a length and no direction");

    apt(ptbuf, UAS_PT_LAYOUT_64, 0, 0, 7, 0, zero, power);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_64, ptbuf, 48, 48, 65536,
                       &p), UAS_PT_OK, "64-bit CHECK POWER MODE, no data");
    CHECK_EQ(p.HeaderSize, 48, "header 48");
    CHECK_EQ(p.Cdb[2], 0x20, "non-data: CK_COND for the registers");
    CHECK_EQ(p.Cdb[14], 0xE5, "CurrentTaskFile at 40");
    CHECK_EQ(p.Timeout, 7, "7 s");

    apt(ptbuf, UAS_PT_LAYOUT_32, UAS_ATA_DATA_IN, 512, 0, 0x00402000UL, zero,
        ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA_DIRECT, UAS_PT_LAYOUT_32, ptbuf, 40, 40,
                       65536, &p), UAS_PT_OK,
             "IOCTL_ATA_PASS_THROUGH_DIRECT: a pointer at 20");
    CHECK_EQ(p.DataPtrLo, 0x00402000UL, "the pointer");
}

static void test_ata_regs(void)
{
    static const UCHAR desc[22] = { 0x72, 0x01, 0x00, 0x1D, 0, 0, 0, 14,
                                    0x09, 0x0C, 0x01, 0x00, 0x00, 0x01,
                                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                    0x40, 0x50 };
    static const UCHAR descCur[8] = { 0x00, 0x01, 0x22, 0x44, 0x66, 0x40,
                                      0x50, 0 };
    static const UCHAR descPrev[8] = { 0, 0x00, 0x11, 0x33, 0x55, 0, 0, 0 };
    static const UCHAR fixed[18] = { 0x70, 0, 0x0B, 0x04, 0x51, 0xA0, 0x00,
                                     10, 0x00, 0x00, 0x4F, 0xC2, 0x00,
                                     0x1D, 0, 0, 0, 0 };
    static const UCHAR fixedCur[8] = { 0x04, 0x00, 0x00, 0x4F, 0xC2, 0xA0,
                                       0x51, 0 };
    static const UCHAR zero8[8] = { 0 };
    UCHAR s[22];
    UCHAR prev[8];
    UCHAR cur[8];

    CHECK_EQ(UasAtaRegsFromSense(desc, 22, prev, cur), UAS_ATAREG_OK,
             "descriptor format, ATA Status Return descriptor");
    CHECK(bytes_eq(cur, descCur, 8), "ERROR, COUNT, LBA low/mid/high, DEVICE,"
          " STATUS");
    CHECK(bytes_eq(prev, descPrev, 8), "EXTEND: the upper bytes");
    memcpy(s, desc, 22);
    s[10] = 0;
    CHECK_EQ(UasAtaRegsFromSense(s, 22, prev, cur), UAS_ATAREG_OK,
             "EXTEND clear");
    CHECK(bytes_eq(prev, zero8, 8), "no upper bytes taken");
    s[8] = 0x02;
    CHECK_EQ(UasAtaRegsFromSense(s, 22, prev, cur), UAS_ATAREG_NONE,
             "another descriptor only");
    CHECK_EQ(UasAtaRegsFromSense(desc, 21, prev, cur), UAS_ATAREG_NONE,
             "the descriptor cut short");

    CHECK_EQ(UasAtaRegsFromSense(fixed, 18, prev, cur), UAS_ATAREG_OK,
             "fixed format, 00h/1Dh");
    CHECK(bytes_eq(cur, fixedCur, 8), "INFORMATION and COMMAND-SPECIFIC");
    memcpy(s, fixed, 18);
    s[8] = 0x80 | 0x40;
    CHECK_EQ(UasAtaRegsFromSense(s, 18, prev, cur), UAS_ATAREG_PARTIAL,
             "fixed format, an upper count byte non-zero");
    s[8] = 0x80 | 0x20;
    CHECK_EQ(UasAtaRegsFromSense(s, 18, prev, cur), UAS_ATAREG_PARTIAL,
             "fixed format, an upper LBA byte non-zero");
    memcpy(s, fixed, 18);
    s[12] = 0x20;
    s[13] = 0x00;
    CHECK_EQ(UasAtaRegsFromSense(s, 18, prev, cur), UAS_ATAREG_NONE,
             "fixed format, INVALID COMMAND OPERATION CODE: no registers");
    CHECK_EQ(UasAtaRegsFromSense(fixed, 13, prev, cur), UAS_ATAREG_NONE,
             "fixed format too short for its ASC/ASCQ");
    CHECK_EQ(UasAtaRegsFromSense(NULL, 0, prev, cur), UAS_ATAREG_NONE,
             "no sense");
}

static void test_pt_result(void)
{
    static const UCHAR inquiry[16] = { 0x12, 0, 0, 0, 0x24, 0 };
    static const UCHAR zero[8] = { 0 };
    static const UCHAR ident[8] = { 0x00, 0x01, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR identBack[8] = { 0x00, 0x01, 0, 0, 0, 0xA0, 0x50, 0 };
    static const UCHAR identNoRegs[8] = { 0x04, 0x01, 0, 0, 0, 0xA0, 0x51,
                                          0 };
    static const UCHAR rs[8] = { 0xDA, 0, 0, 0x4F, 0xC2, 0xA0, 0xB0, 0 };
    static const UCHAR rsBack[8] = { 0x00, 0x00, 0x00, 0xF4, 0x2C, 0xA0, 0x50,
                                     0 };
    static const UCHAR noRegsBack[8] = { 0x04, 0, 0, 0x4F, 0xC2, 0xA0, 0x51,
                                         0 };
    static const UCHAR illegal[18] = { 0x70, 0, 0x05, 0, 0, 0, 0, 10, 0, 0,
                                       0, 0, 0x20, 0x00, 0, 0, 0, 0 };
    static const UCHAR rsSense[18] = { 0x70, 0, 0x01, 0x00, 0x50, 0xA0, 0x00,
                                       10, 0x00, 0x00, 0xF4, 0x2C, 0x00, 0x1D,
                                       0, 0, 0, 0 };
    UAS_PT_PLAN p;
    ULONG info;

    spt(ptbuf, UAS_PT_LAYOUT_32, 6, 32, 1, 36, 0, 76, 0, 44, inquiry);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 44, 112, 65536,
                       &p), UAS_PT_OK, "plan");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_SUCCESS, 0, 36, NULL, 0,
                         &info), UAS_PTR_SUCCESS, "GOOD");
    CHECK_EQ(ptbuf[2], 0, "ScsiStatus GOOD");
    CHECK_EQ(ptbuf[7], 0, "no sense");
    CHECK_EQ(get32(ptbuf + 12), 36, "36 bytes moved");
    CHECK_EQ(info, 112, "through the end of the data");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_DATA_OVERRUN, 0, 20, NULL, 0,
                         &info), UAS_PTR_SUCCESS, "a short transfer");
    CHECK_EQ(get32(ptbuf + 12), 20, "20 bytes moved");
    CHECK_EQ(info, 96, "through the bytes moved");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_ERROR, 2, 0, illegal, 18,
                         &info), UAS_PTR_SUCCESS,
             "CHECK CONDITION: the drive answered");
    CHECK_EQ(ptbuf[2], 2, "ScsiStatus CHECK CONDITION");
    CHECK_EQ(ptbuf[7], 18, "18 sense bytes");
    CHECK(bytes_eq(ptbuf + 44, illegal, 18), "the sense at its offset");
    CHECK_EQ(info, 62, "through the sense");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_BUSY, UAS_SCSI_BUSY, 0, NULL,
                         0, &info), UAS_PTR_SUCCESS, "BUSY from the drive");
    CHECK_EQ(ptbuf[2], 8, "ScsiStatus BUSY");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_BUSY,
                         UAS_SCSI_TASK_SET_FULL, 0, NULL, 0, &info),
             UAS_PTR_SUCCESS, "TASK SET FULL from the drive");
    CHECK_EQ(ptbuf[2], 0x28, "ScsiStatus TASK SET FULL");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_BUSY, 0, 0, NULL, 0, &info),
             UAS_PTR_BUSY, "this driver's own BUSY");
    CHECK_EQ(info, 0, "nothing returned");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_TIMEOUT, 0, 0, NULL, 0,
                         &info), UAS_PTR_TIMEOUT, "TIMEOUT");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_NO_DEVICE, 0, 0, NULL, 0,
                         &info), UAS_PTR_NO_DEVICE, "NO_DEVICE");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_INVALID_REQUEST, 0, 0, NULL,
                         0, &info), UAS_PTR_INVALID, "INVALID_REQUEST");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, UAS_SRB_ABORTED, 0, 0, NULL, 0,
                         &info), UAS_PTR_IO_ERROR, "ABORTED");
    CHECK_EQ(UasPtResult(&p, ptbuf, 112, 0x16, 0, 0, NULL, 0, &info),
             UAS_PTR_IO_ERROR, "REQUEST_FLUSHED");

    apt(ptbuf, UAS_PT_LAYOUT_32, UAS_ATA_DATA_IN, 512, 0, 40, zero, ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 552, 65536,
                       &p), UAS_PT_OK, "ATA plan");
    CHECK_EQ(UasPtResult(&p, ptbuf, 552, UAS_SRB_SUCCESS, 0, 512, NULL, 0,
                         &info), UAS_PTR_SUCCESS, "IDENTIFY GOOD");
    CHECK(bytes_eq(ptbuf + 32, identBack, 8),
          "Status 50h and Error 0 written by the driver");
    CHECK(bytes_eq(ptbuf + 24, zero, 8), "previous registers as sent");
    CHECK_EQ(get32(ptbuf + 8), 512, "512 bytes moved");
    CHECK_EQ(info, 552, "the header and the data");
    CHECK_EQ(UasPtResult(&p, ptbuf, 552, UAS_SRB_ERROR, 2, 0, illegal, 18,
                         &info), UAS_PTR_SUCCESS,
             "a bridge with no SAT: the drive answered");
    CHECK(bytes_eq(ptbuf + 32, identNoRegs, 8),
          "the registers as sent, Error 04h, Status 51h");
    CHECK_EQ(UasPtResult(&p, ptbuf, 552, UAS_SRB_BUSY, UAS_SCSI_BUSY, 0, NULL,
                         0, &info), UAS_PTR_BUSY,
             "BUSY has no ATA form: the IOCTL fails");

    apt(ptbuf, UAS_PT_LAYOUT_32, 0, 0, 0, 0, zero, rs);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 40, 65536,
                       &p), UAS_PT_OK, "RETURN STATUS plan");
    CHECK_EQ(UasPtResult(&p, ptbuf, 40, UAS_SRB_ERROR, 2, 0, rsSense, 18,
                         &info), UAS_PTR_SUCCESS, "CK_COND's CHECK CONDITION");
    CHECK(bytes_eq(ptbuf + 32, rsBack, 8),
          "the registers from fixed sense: F4h/2Ch");
    CHECK_EQ(info, 40, "the header alone");
    CHECK_EQ(UasPtResult(&p, ptbuf, 40, UAS_SRB_SUCCESS, 0, 0, NULL, 0,
                         &info), UAS_PTR_IO_ERROR,
             "non-data GOOD with no registers: no answer to report");
    CHECK_EQ(info, 0, "nothing returned");
    CHECK_EQ(UasPtResult(&p, ptbuf, 40, UAS_SRB_ERROR, 2, 0, illegal, 18,
                         &info), UAS_PTR_SUCCESS, "refused by the bridge");
    CHECK(bytes_eq(ptbuf + 32, noRegsBack, 8),
          "the registers as sent, Error 04h, Status 51h");
}

static void test_pt_partial48(void)
{
    static const UCHAR prev[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    static const UCHAR cur[8] = { 0, 0, 0, 0, 0, 0x40, 0x27, 0 };
    static const UCHAR partial[18] = { 0x70, 0, 0x01, 0x00, 0x50, 0x40, 0xFF,
                                       10, 0xA0, 0xFF, 0xFF, 0xFF, 0x00, 0x1D,
                                       0, 0, 0, 0 };
    UAS_PT_PLAN p;
    ULONG info;

    /* READ NATIVE MAX ADDRESS EXT: 48-bit, non-data. */
    apt(ptbuf, UAS_PT_LAYOUT_32, UAS_ATA_48BIT, 0, 0, 0, prev, cur);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 40, 65536,
                       &p), UAS_PT_OK, "48-bit plan");
    CHECK_EQ(p.Cdb[1], 0x07, "non-data (3) with EXTEND");
    CHECK_EQ(UasPtResult(&p, ptbuf, 40, UAS_SRB_ERROR, 2, 0, partial, 18,
                         &info), UAS_PTR_IO_ERROR,
             "fixed format cannot carry the upper LBA: failed, not wrong");
    CHECK_EQ(info, 0, "nothing returned");
}

static void test_sioc(void)
{
    UCHAR b[64];
    ULONG control;
    ULONG timeout;
    ULONG capacity;

    memset(b, 0, sizeof(b));
    put32(b, 28);
    memcpy(b + 4, "SCSIDISK", 8);
    put32(b + 12, 2);
    put32(b + 16, UAS_SIOC_IDENTIFY);
    put32(b + 24, 529);
    CHECK_EQ(UasSiocCheck(b, 60, 557, &control, &timeout, &capacity),
             UAS_PT_OK, "disk.sys's RCV shape: 60 in, 557 out, Length 529");
    CHECK_EQ(control, UAS_SIOC_IDENTIFY, "control code");
    CHECK_EQ(timeout, 2, "timeout");
    CHECK_EQ(capacity, 529, "capacity, more than the input carries");
    put32(b + 24, 530);
    CHECK_EQ(UasSiocCheck(b, 60, 557, &control, &timeout, &capacity),
             UAS_PT_INVALID, "Length past the buffer");
    put32(b + 24, 24);
    put32(b + 16, UAS_SIOC_SMART_VERSION);
    CHECK_EQ(UasSiocCheck(b, 52, 52, &control, &timeout, &capacity),
             UAS_PT_OK, "SMART_VERSION: 52 in and out, Length 24");
    put32(b, 32);
    CHECK_EQ(UasSiocCheck(b, 52, 52, &control, &timeout, &capacity),
             UAS_PT_INVALID, "HeaderLength not 28");
    put32(b, 28);
    CHECK_EQ(UasSiocCheck(b, 52, 27, &control, &timeout, &capacity),
             UAS_PT_TOO_SMALL, "output shorter than the envelope");
    CHECK_EQ(UasSiocCheck(b, 27, 52, &control, &timeout, &capacity),
             UAS_PT_UNKNOWN, "input shorter than the envelope");
    b[11] = 'X';
    CHECK_EQ(UasSiocCheck(b, 52, 52, &control, &timeout, &capacity),
             UAS_PT_UNKNOWN, "another signature: not this driver's");
    b[11] = 'K';
    put32(b + 16, 0x1B050DUL);
    CHECK_EQ(UasSiocCheck(b, 52, 52, &control, &timeout, &capacity),
             UAS_PT_UNKNOWN, "a control code past WRITE_SMART_LOG");
    put32(b + 16, 0x1B04FFUL);
    CHECK_EQ(UasSiocCheck(b, 52, 52, &control, &timeout, &capacity),
             UAS_PT_UNKNOWN, "a control code before SMART_VERSION");
}

/* SENDCMDINPARAMS: cBufferSize, IDEREGS (Features, SectorCount,
 * SectorNumber, CylLow, CylHigh, DriveHead, Command, Reserved), then
 * bDriveNumber, three reserved, four reserved ULONGs: 32 bytes. */
static void smartIn(UCHAR *b, UCHAR feature, UCHAR count, UCHAR number,
                    UCHAR cylLow, UCHAR cylHigh, UCHAR command)
{
    memset(b, 0, 32);
    put32(b, 512);
    b[4] = feature;
    b[5] = count;
    b[6] = number;
    b[7] = cylLow;
    b[8] = cylHigh;
    b[9] = 0xA0;
    b[10] = command;
}

static void test_smart_plan(void)
{
    static const UCHAR readWant[16] = { 0x85, 0x08, 0x0E, 0, 0xD0, 0, 0x01,
                                        0, 0x01, 0, 0x4F, 0, 0xC2, 0xA0,
                                        0xB0, 0 };
    static const UCHAR rsWant[16] = { 0x85, 0x06, 0x20, 0, 0xDA, 0, 0x00, 0,
                                      0x01, 0, 0x4F, 0, 0xC2, 0xA0, 0xB0, 0 };
    static const UCHAR identWant[16] = { 0x85, 0x08, 0x0E, 0, 0, 0, 0x01, 0,
                                         0, 0, 0, 0, 0, 0xA0, 0xEC, 0 };
    UCHAR in[32];
    UAS_SMART_PLAN p;

    smartIn(in, 0xD0, 1, 1, 0x00, 0x00, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_ATTRIBS, in, 32, 529, &p),
             UAS_SMART_NO_ERROR, "READ_SMART_ATTRIBS");
    CHECK_EQ(p.DataLength, 512, "512 bytes in");
    CHECK_EQ(p.OutLength, 528, "SENDCMDOUTPARAMS header and the data");
    CHECK(bytes_eq(p.Cdb, readWant, 16),
          "PIO in, count 1, LBA Mid/High forced to 4Fh/C2h");
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_ATTRIBS, in, 32, 527, &p),
             UAS_SMART_INVALID_BUFFER, "no room for the reply");
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_ATTRIBS, in, 31, 529, &p),
             UAS_SMART_INVALID_BUFFER, "a payload shorter than 32");
    smartIn(in, 0xD1, 1, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_ATTRIBS, in, 32, 529, &p),
             UAS_SMART_INVALID_COMMAND,
             "the registers disagree with the control code");
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_THRESHOLDS, in, 32, 529, &p),
             UAS_SMART_NO_ERROR, "READ_SMART_THRESHOLDS");
    CHECK_EQ(p.Cdb[4], 0xD1, "feature D1h");

    smartIn(in, 0xDA, 0, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_RETURN_STATUS, in, 32, 41, &p),
             UAS_SMART_NO_ERROR, "RETURN_STATUS in disk.sys's 41 bytes");
    CHECK(p.ReturnRegs, "IDEREGS back");
    CHECK_EQ(p.OutLength, 24, "16 and 8");
    CHECK(bytes_eq(p.Cdb, rsWant, 16), "non-data, CK_COND");
    CHECK_EQ(UasSmartPlan(UAS_SIOC_RETURN_STATUS, in, 32, 23, &p),
             UAS_SMART_INVALID_BUFFER, "no room for the registers");

    smartIn(in, 0xD8, 0, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_ENABLE_SMART, in, 32, 33, &p),
             UAS_SMART_NO_ERROR, "ENABLE_SMART in disk.sys's 33 bytes");
    CHECK_EQ(p.OutLength, 16, "the header alone");
    CHECK_EQ(UasSmartPlan(UAS_SIOC_ENABLE_SMART, in, 32, 16, &p),
             UAS_SMART_NO_ERROR, "16 bytes are enough");
    smartIn(in, 0xD2, 0xF1, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_AUTOSAVE, in, 32, 33, &p),
             UAS_SMART_NO_ERROR, "AUTOSAVE on");
    CHECK_EQ(p.Cdb[6], 0xF1, "its count F1h");
    smartIn(in, 0xD3, 0, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_SAVE_ATTRIBS, in, 32, 33, &p),
             UAS_SMART_NO_ERROR, "SAVE ATTRIBUTE VALUES");
    smartIn(in, 0xD4, 0, 0x01, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_OFFLINE_DIAGS, in, 32, 33, &p),
             UAS_SMART_NO_ERROR, "EXECUTE OFF-LINE IMMEDIATE");
    CHECK_EQ(p.Cdb[8], 0x01, "subcommand in LBA Low");
    smartIn(in, 0xDB, 0xF8, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_AUTO_OFFLINE, in, 32, 33, &p),
             UAS_SMART_NO_ERROR, "AUTOMATIC OFF-LINE");
    smartIn(in, 0xD9, 0, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_DISABLE_SMART, in, 32, 33, &p),
             UAS_SMART_NO_ERROR, "DISABLE_SMART");

    smartIn(in, 0x00, 0, 0, 0, 0, 0xEC);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_IDENTIFY, in, 32, 529, &p),
             UAS_SMART_NO_ERROR, "IDENTIFY");
    CHECK(bytes_eq(p.Cdb, identWant, 16),
          "IDENTIFY DEVICE, count 1, cylinders as sent");
    smartIn(in, 0x00, 0, 0, 0, 0, 0xA1);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_IDENTIFY, in, 32, 529, &p),
             UAS_SMART_INVALID_COMMAND, "ATAPI identify refused");

    smartIn(in, 0xD5, 2, 0xE0, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_LOG, in, 32, 1040, &p),
             UAS_SMART_NO_ERROR, "READ LOG, two blocks");
    CHECK_EQ(p.DataLength, 1024, "1024 bytes");
    CHECK_EQ(p.Cdb[6], 2, "count 2");
    CHECK_EQ(p.Cdb[8], 0xE0, "the log address in LBA Low");
    smartIn(in, 0xD5, 0, 0xE0, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_LOG, in, 32, 1040, &p),
             UAS_SMART_INVALID_COMMAND, "READ LOG of 0 blocks");
    smartIn(in, 0xD6, 1, 0xE0, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_WRITE_LOG, in, 32, 1040, &p),
             UAS_SMART_INVALID_COMMAND, "WRITE LOG refused");
    CHECK_EQ(UasSmartPlan(UAS_SIOC_SMART_VERSION, in, 32, 1040, &p),
             UAS_SMART_INVALID_COMMAND, "SMART_VERSION is not planned here");
}

static void test_smart_result(void)
{
    static const UCHAR rsOk[18] = { 0x70, 0, 0x01, 0x00, 0x50, 0xA0, 0x00,
                                    10, 0x00, 0x01, 0x4F, 0xC2, 0x00, 0x1D,
                                    0, 0, 0, 0 };
    static const UCHAR rsBad[18] = { 0x70, 0, 0x01, 0x00, 0x50, 0xA0, 0x00,
                                     10, 0x00, 0x01, 0xF4, 0x2C, 0x00, 0x1D,
                                     0, 0, 0, 0 };
    static const UCHAR abrt[18] = { 0x70, 0, 0x0B, 0x04, 0x51, 0xA0, 0x00,
                                    10, 0x00, 0x01, 0x4F, 0xC2, 0x00, 0x1D,
                                    0, 0, 0, 0 };
    static const UCHAR fault[18] = { 0x70, 0, 0x0B, 0x00, 0x60, 0xA0, 0x00,
                                     10, 0x00, 0x01, 0x4F, 0xC2, 0x00, 0x1D,
                                     0, 0, 0, 0 };
    static const UCHAR illegal[18] = { 0x70, 0, 0x05, 0, 0, 0, 0, 10, 0, 0,
                                       0, 0, 0x20, 0x00, 0, 0, 0, 0 };
    static const UCHAR okRegs[8] = { 0x00, 0x00, 0x01, 0x4F, 0xC2, 0xA0, 0x50,
                                     0 };
    UCHAR in[32];
    UCHAR data[512];
    UCHAR out[600];
    UAS_SMART_PLAN p;
    ULONG bytes;
    ULONG err;
    ULONG i;

    for (i = 0; i < 512; i++) {
        data[i] = (UCHAR)(i * 7);
    }
    smartIn(in, 0xD0, 1, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_ATTRIBS, in, 32, 529, &p),
             UAS_SMART_NO_ERROR, "plan");
    memset(out, 0xCC, sizeof(out));
    CHECK_EQ(UasSmartResult(&p, out, 529, UAS_SRB_SUCCESS, 0, 512, data, NULL,
                            0, &bytes, &err), UAS_PTR_SUCCESS,
             "READ DATA GOOD");
    CHECK_EQ(bytes, 528, "header and data");
    CHECK_EQ(get32(out), 512, "cBufferSize 512");
    CHECK_EQ(out[4], 0, "bDriverError 0");
    CHECK_EQ(out[5], 0, "bIDEError 0");
    CHECK_EQ(out[6] | out[7] | out[8] | out[15], 0, "reserved bytes 0");
    CHECK(bytes_eq(out + 16, data, 512), "the data");
    memset(out, 0xCC, sizeof(out));
    CHECK_EQ(UasSmartResult(&p, out, 527, UAS_SRB_SUCCESS, 0, 512, data, NULL,
                            0, &bytes, &err), UAS_PTR_INVALID,
             "an output one byte short of the data: refused");
    CHECK_EQ(err, UAS_SMART_INVALID_BUFFER, "SMART_INVALID_BUFFER");
    CHECK_EQ(bytes, 16, "the header alone");
    CHECK_EQ(out[16], 0xCC, "nothing written past the header");
    CHECK_EQ(UasSmartResult(&p, out, 15, UAS_SRB_SUCCESS, 0, 512, data, NULL,
                            0, &bytes, &err), UAS_PTR_INVALID,
             "an output shorter than the header");
    CHECK_EQ(bytes, 0, "nothing written");
    CHECK_EQ(UasSmartResult(&p, out, 529, UAS_SRB_DATA_OVERRUN, 0, 100, data,
                            NULL, 0, &bytes, &err), UAS_PTR_IO_ERROR,
             "a short READ DATA fails");
    CHECK_EQ(err, UAS_SMART_IDE_ERROR, "SMART_IDE_ERROR");
    CHECK_EQ(bytes, 16, "the header alone");
    CHECK_EQ(UasSmartResult(&p, out, 529, UAS_SRB_ERROR, 2, 0, data, illegal,
                            18, &bytes, &err), UAS_PTR_NOT_SUPPORTED,
             "a bridge with no SAT");
    CHECK_EQ(err, UAS_SMART_NOT_SUPPORTED, "SMART_NOT_SUPPORTED");
    CHECK_EQ(out[4], 9, "in bDriverError");
    CHECK_EQ(UasSmartResult(&p, out, 529, UAS_SRB_ERROR, 2, 0, data, abrt, 18,
                            &bytes, &err), UAS_PTR_IO_ERROR,
             "the drive aborted READ DATA");
    CHECK_EQ(out[5], 0x04, "bIDEError: ABRT");
    CHECK_EQ(UasSmartResult(&p, out, 529, UAS_SRB_TIMEOUT, 0, 0, data, NULL,
                            0, &bytes, &err), UAS_PTR_TIMEOUT, "TIMEOUT");
    CHECK_EQ(UasSmartResult(&p, out, 529, UAS_SRB_BUSY, UAS_SCSI_BUSY, 0,
                            data, NULL, 0, &bytes, &err), UAS_PTR_BUSY,
             "BUSY fails the request");

    smartIn(in, 0xDA, 0, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_RETURN_STATUS, in, 32, 41, &p),
             UAS_SMART_NO_ERROR, "plan");
    memset(out, 0xCC, sizeof(out));
    CHECK_EQ(UasSmartResult(&p, out, 41, UAS_SRB_ERROR, 2, 0, NULL, rsOk, 18,
                            &bytes, &err), UAS_PTR_SUCCESS,
             "RETURN STATUS: no threshold exceeded");
    CHECK_EQ(bytes, 24, "header and IDEREGS");
    CHECK_EQ(get32(out), 8, "cBufferSize 8");
    CHECK(bytes_eq(out + 16, okRegs, 8), "IDEREGS: 4Fh/C2h, Status 50h");
    CHECK_EQ(UasSmartResult(&p, out, 41, UAS_SRB_ERROR, 2, 0, NULL, rsBad, 18,
                            &bytes, &err), UAS_PTR_SUCCESS,
             "threshold exceeded is an answer, not an error");
    CHECK_EQ(out[16 + 3], 0xF4, "LBA Mid F4h");
    CHECK_EQ(out[16 + 4], 0x2C, "LBA High 2Ch");
    memset(out, 0xCC, sizeof(out));
    CHECK_EQ(UasSmartResult(&p, out, 23, UAS_SRB_ERROR, 2, 0, NULL, rsOk, 18,
                            &bytes, &err), UAS_PTR_INVALID,
             "RETURN STATUS with no room for its registers");
    CHECK_EQ(out[16], 0xCC, "nothing written past the header");
    CHECK_EQ(UasSmartResult(&p, out, 41, UAS_SRB_SUCCESS, 0, 0, NULL, NULL, 0,
                            &bytes, &err), UAS_PTR_IO_ERROR,
             "GOOD without registers: no status to report");
    CHECK_EQ(UasSmartResult(&p, out, 41, UAS_SRB_ERROR, 2, 0, NULL, fault, 18,
                            &bytes, &err), UAS_PTR_IO_ERROR,
             "DF with ERR clear is a failure");
    CHECK_EQ(err, UAS_SMART_IDE_ERROR, "SMART_IDE_ERROR");
    CHECK_EQ(out[5], 0x00, "bIDEError: the Error register, 0");

    smartIn(in, 0xD8, 0, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_ENABLE_SMART, in, 32, 33, &p),
             UAS_SMART_NO_ERROR, "plan");
    CHECK_EQ(UasSmartResult(&p, out, 33, UAS_SRB_ERROR, 2, 0, NULL, rsOk, 18,
                            &bytes, &err), UAS_PTR_SUCCESS, "ENABLE");
    CHECK_EQ(bytes, 16, "no register block");
    CHECK_EQ(get32(out), 0, "cBufferSize 0");

    UasSmartVersion(out);
    CHECK_EQ(out[0], 1, "bVersion 1");
    CHECK_EQ(out[1], 1, "bRevision 1");
    CHECK_EQ(out[2], 0, "bReserved");
    CHECK_EQ(out[3], 1, "bIDEDeviceMap 1");
    CHECK_EQ(get32(out + 4), 5, "CAP_ATA_ID_CMD | CAP_SMART_CMD");
    for (i = 8; i < 24; i++) {
        CHECK_EQ(out[i], 0, "dwReserved 0");
    }
}

/*
 * Issue 26: XhciUasNoAtaPassThrough's answers. The CDB test is the
 * operation code alone, as Linux's uas.c under US_FL_NO_ATA_1X; the sense
 * is usb_stor_sense_invalidCDB's 5/24/00; and through the writers an
 * answered request uses, the forms come back as a drive without SAT gives
 * them: SCSI CHECK CONDITION with the sense, ATA Error 04h and Status 51h
 * with nothing moved, SMART not supported.
 */
static void test_no_ata(void)
{
    static const UCHAR ata16[16] = { 0x85, 0x08, 0x0E, 0, 0, 0, 1, 0, 0, 0,
                                     0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR ata12[12] = { 0xA1, 0x08, 0x0E, 0, 1, 0, 0, 0, 0xEC,
                                     0, 0, 0 };
    static const UCHAR read10[10] = { 0x28, 0 };
    static const UCHAR inquiry[6] = { 0x12, 0, 0, 0, 0x24, 0 };
    static const UCHAR zero[8] = { 0 };
    static const UCHAR ident[8] = { 0x00, 0x01, 0, 0, 0, 0xA0, 0xEC, 0 };
    static const UCHAR identNoRegs[8] = { 0x04, 0x01, 0, 0, 0, 0xA0, 0x51,
                                          0 };
    static const UCHAR want[18] = { 0x70, 0, 0x05, 0, 0, 0, 0, 10, 0, 0,
                                    0, 0, 0x24, 0x00, 0, 0, 0, 0 };
    UCHAR sense[32];
    UCHAR in[32];
    UCHAR out[600];
    UAS_PT_PLAN p;
    UAS_SMART_PLAN sp;
    ULONG info;
    ULONG n;
    ULONG bytes;
    ULONG err;

    CHECK_EQ(UasNoAtaCdb(ata16, 16), 1, "85h answered");
    CHECK_EQ(UasNoAtaCdb(ata12, 12), 1, "A1h answered");
    CHECK_EQ(UasNoAtaCdb(read10, 10), 0, "READ (10) sent");
    CHECK_EQ(UasNoAtaCdb(inquiry, 6), 0, "INQUIRY sent");
    CHECK_EQ(UasNoAtaCdb(NULL, 16), 0, "no CDB");
    CHECK_EQ(UasNoAtaCdb(ata16, 0), 0, "a zero length");

    memset(sense, 0xCC, sizeof(sense));
    n = UasInvalidCdbSense(sense, sizeof(sense));
    CHECK_EQ(n, UAS_INVALID_CDB_SENSE, "18 bytes of sense");
    CHECK(bytes_eq(sense, want, 18), "fixed, ILLEGAL REQUEST, 24h/00h");
    CHECK_EQ(sense[18], 0xCC, "nothing past it");
    memset(sense, 0xCC, sizeof(sense));
    CHECK_EQ(UasInvalidCdbSense(sense, 8), 8, "a short buffer: its room");
    CHECK(bytes_eq(sense, want, 8), "the first 8 bytes");
    CHECK_EQ(sense[8], 0xCC, "nothing past the room");
    CHECK_EQ(UasInvalidCdbSense(sense, 0), 0, "no room");
    CHECK_EQ(UasInvalidCdbSense(NULL, 18), 0, "no buffer");

    /* SCSI pass-through with 85h, 512 bytes in, 32 bytes of sense. */
    n = UasInvalidCdbSense(sense, sizeof(sense));
    spt(ptbuf, UAS_PT_LAYOUT_32, 16, 32, 1, 512, 0, 80, 0, 44, ata16);
    CHECK_EQ(UasPtPlan(UAS_PT_SCSI, UAS_PT_LAYOUT_32, ptbuf, 80, 592, 65536,
                       &p), UAS_PT_OK, "85h plan");
    CHECK_EQ(UasNoAtaCdb(p.Cdb, p.CdbLength), 1, "the plan's CDB answered");
    CHECK_EQ(UasPtResult(&p, ptbuf, 592, UAS_SRB_ERROR,
                         UAS_SCSI_CHECK_CONDITION, 0, sense, n, &info),
             UAS_PTR_SUCCESS, "SCSI: the call succeeds");
    CHECK_EQ(ptbuf[2], UAS_SCSI_CHECK_CONDITION, "ScsiStatus CHECK CONDITION");
    CHECK_EQ(ptbuf[7], 18, "18 bytes of sense");
    CHECK(bytes_eq(ptbuf + 44, want, 18), "the sense in place");
    CHECK_EQ(get32(ptbuf + 12), 0, "nothing moved");

    /* ATA pass-through, IDENTIFY: the CDB this driver builds is 85h. */
    apt(ptbuf, UAS_PT_LAYOUT_32, UAS_ATA_DATA_IN, 512, 0, 40, zero, ident);
    CHECK_EQ(UasPtPlan(UAS_PT_ATA, UAS_PT_LAYOUT_32, ptbuf, 40, 552, 65536,
                       &p), UAS_PT_OK, "ATA plan");
    CHECK_EQ(UasNoAtaCdb(p.Cdb, p.CdbLength), 1, "ATA's CDB answered");
    CHECK_EQ(UasPtResult(&p, ptbuf, 552, UAS_SRB_ERROR,
                         UAS_SCSI_CHECK_CONDITION, 0, sense, n, &info),
             UAS_PTR_SUCCESS, "ATA: the call succeeds");
    CHECK(bytes_eq(ptbuf + 32, identNoRegs, 8), "Error 04h, Status 51h");
    CHECK_EQ(get32(ptbuf + 8), 0, "ATA: nothing moved");

    /* SMART READ DATA: not supported. */
    smartIn(in, 0xD0, 1, 1, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_READ_ATTRIBS, in, 32, 529, &sp),
             UAS_SMART_NO_ERROR, "SMART plan");
    CHECK_EQ(UasNoAtaCdb(sp.Cdb, 16), 1, "SMART's CDB answered");
    memset(out, 0xCC, sizeof(out));
    CHECK_EQ(UasSmartResult(&sp, out, 529, UAS_SRB_ERROR,
                            UAS_SCSI_CHECK_CONDITION, 0, NULL, sense, n,
                            &bytes, &err), UAS_PTR_NOT_SUPPORTED,
             "SMART: not supported");
    CHECK_EQ(err, UAS_SMART_NOT_SUPPORTED, "bDriverError SMART_NOT_SUPPORTED");
    CHECK_EQ(bytes, 16, "the header alone");

    /* SMART RETURN STATUS, no data: not supported either. */
    smartIn(in, 0xDA, 0, 0, 0x4F, 0xC2, 0xB0);
    CHECK_EQ(UasSmartPlan(UAS_SIOC_RETURN_STATUS, in, 32, 24, &sp),
             UAS_SMART_NO_ERROR, "RETURN STATUS plan");
    CHECK_EQ(UasSmartResult(&sp, out, 24, UAS_SRB_ERROR,
                            UAS_SCSI_CHECK_CONDITION, 0, NULL, sense, n,
                            &bytes, &err), UAS_PTR_NOT_SUPPORTED,
             "RETURN STATUS: not supported");
}

/* Issue 26: one failure's words for the bus's record. */
static void test_fail_pack(void)
{
    static const UCHAR fixed[18] = { 0x70, 0, 0x05, 0, 0, 0, 0, 10, 0, 0,
                                     0, 0, 0x24, 0x01, 0, 0, 0, 0 };
    static const UCHAR desc[8] = { 0x72, 0x06, 0x29, 0x00, 0, 0, 0, 0 };
    ULONG w[XHCI98_FAIL_WORDS];

    UasFailPack(w, XHCI98_FAIL_E_SENSE, XHCI98_FAIL_O_PROGRAM, 3, 0x85,
                0x04, 0x02, fixed, 18, 2, XHCI98_FAIL_X_FINAL, 30, 512);
    CHECK_EQ(w[0], 0x01010385UL, "event, origin, tag, opcode");
    CHECK_EQ(w[1], 0x04022401UL, "SRB status, SCSI status, ASC, ASCQ");
    CHECK_EQ(w[2], 0x0502101EUL, "key, slot state, transfers, timeout");
    CHECK_EQ(w[3], 512, "the data length");

    UasFailPack(w, XHCI98_FAIL_E_ENDED, XHCI98_FAIL_O_CLASS, 1, 0x28, 0x22,
                0, desc, 8, 0, 0, 10, 4096);
    CHECK_EQ(w[1] & 0xFFFFUL, 0x2900UL, "descriptor format: ASC 29h");
    CHECK_EQ(w[2] >> 24, 6, "descriptor format: UNIT ATTENTION");

    UasFailPack(w, XHCI98_FAIL_E_TIMEOUT, XHCI98_FAIL_O_CLASS, 2, 0x00, 0,
                0, NULL, 0, 1,
                XHCI98_FAIL_X_STATUS | XHCI98_FAIL_X_DATA_POSTED, 1000, 0);
    CHECK_EQ(w[2] >> 24, 0xFF, "no sense: key FFh");
    CHECK_EQ(w[1] & 0xFFFFUL, 0, "no sense: ASC and ASCQ 0");
    CHECK_EQ(w[2] & 0xFFUL, 255, "a timeout held at 255");
    CHECK_EQ((w[2] >> 8) & 0xFFUL, 0x21, "the status read outstanding");

    UasFailPack(w, XHCI98_FAIL_E_SENSE, 0, 0, 0, 0, 2, fixed, 13, 0, 0, 0,
                0);
    CHECK_EQ(w[2] >> 24, 0xFF, "fixed sense too short for its ASC: none");
    UasFailPack(w, 0x1FF, 0x1FF, 0x1FF, 0x1FF, 0x1FF, 0x1FF, NULL, 0, 0x1FF,
                0x1FF, 0, 0);
    CHECK_EQ(w[0], 0xFFFFFFFFUL, "each field cut to its width");
    CHECK_EQ(XHCI98_FAIL_TOTAL_WORDS, 36, "a header of four and eight of four");
    CHECK_EQ(sizeof(XHCI98_FAILURES), 144, "144 bytes on the wire");
}

/* Issue 26, XhciUasFastReset: when a timeout skips the ABORT TASK, and when
 * a final IU's data cancel asks for transport recovery. */
static void test_fast_reset(void)
{
    /* (fast, armed, progress, now, abortAt, startedAt) */
    CHECK_EQ(UasFastResetDue(0, 1, 0, 110, 100, 105), 0,
             "value 0: never, whatever the history");
    CHECK_EQ(UasFastResetDue(1, 0, 0, 110, 100, 105), 0,
             "no answered abort yet: the first timeout keeps ABORT TASK");
    CHECK_EQ(UasFastResetDue(1, 1, 0, 110, 100, 105), 1,
             "a second timeout after an answered abort, no progress: reset");
    CHECK_EQ(UasFastResetDue(1, 1, 1, 110, 100, 105), 0,
             "a command ended GOOD since: ABORT TASK again");
    CHECK_EQ(UasFastResetDue(1, 1, 0, 220, 100, 105), 1,
             "120 s after the abort: still inside the window");
    CHECK_EQ(UasFastResetDue(1, 1, 0, 221, 100, 105), 0,
             "121 s after: outside the window");
    CHECK_EQ(UasFastResetDue(1, 1, 0, 110, 100, 100), 1,
             "started in the abort's second: after it");
    CHECK_EQ(UasFastResetDue(1, 1, 0, 110, 100, 99), 0,
             "started before the abort: that abort's own aftermath");
    CHECK_EQ(UasFastResetDue(1, 1, 0, 5, 0xFFFFFFF0UL, 2), 1,
             "the seconds count wrapped: still 21 s after, started after");
    CHECK_EQ(UasFastResetDue(1, 1, 0, 5, 0xFFFFFFF0UL, 0xFFFFFFEFUL), 0,
             "wrapped, started a second before the abort");

    /* (fast, final, dataOutstanding, waited) */
    CHECK_EQ(UasFinalCancelDue(1, 1, 1, 5), 1, "5 s for the data cancel");
    CHECK_EQ(UasFinalCancelDue(1, 1, 1, 4), 0, "4 s: still waiting");
    CHECK_EQ(UasFinalCancelDue(0, 1, 1, 100), 0, "value 0: never");
    CHECK_EQ(UasFinalCancelDue(1, 0, 1, 100), 0, "no final IU: the timeout's");
    CHECK_EQ(UasFinalCancelDue(1, 1, 0, 100), 0, "no data outstanding");
    CHECK_EQ(XHCI98_FAIL_E_FAST_RESET, 7, "the ring's event 7");
}

/* Task 49.39 (design record 13 section 16.15): a timeout with the status
 * in. (final, dataOutstanding, finalAge) at the command's own deadline. */
static void test_final_timeout(void)
{
    CHECK_EQ(UasFinalTimeoutDue(1, 1, 30), UAS_FINAL_TIMEOUT_RESET,
             "final GOOD long in, data still busy: the device reset");
    CHECK_EQ(UasFinalTimeoutDue(1, 1, 2), UAS_FINAL_TIMEOUT_RESET,
             "final IU two ticks old, data outstanding: reset");
    CHECK_EQ(UasFinalTimeoutDue(1, 1, 1), UAS_FINAL_TIMEOUT_WAIT,
             "final IU one tick old: held, the data may follow");
    CHECK_EQ(UasFinalTimeoutDue(1, 1, 0), UAS_FINAL_TIMEOUT_WAIT,
             "final IU at the deadline: held");
    CHECK_EQ(UasFinalTimeoutDue(1, 0, 30), UAS_FINAL_TIMEOUT_NONE,
             "final IU in and the data back: not this rule");
    CHECK_EQ(UasFinalTimeoutDue(1, 0, 0), UAS_FINAL_TIMEOUT_NONE,
             "status then data inside the timeout: nothing outstanding");
    CHECK_EQ(UasFinalTimeoutDue(0, 1, 30), UAS_FINAL_TIMEOUT_NONE,
             "no final IU (a READ still streaming): ABORT TASK");
    CHECK_EQ(UasFinalTimeoutDue(0, 0, 0), UAS_FINAL_TIMEOUT_NONE,
             "neither: the ordinary timeout");
    CHECK_EQ(UasFinalTimeoutDue(1, 1, 0xFFFFFFFFUL),
             UAS_FINAL_TIMEOUT_RESET,
             "the age is a difference of seconds: any large one resets");
    CHECK_EQ(XHCI98_FAIL_E_FINAL_TIMEOUT, 8, "the ring's event 8");
}

/* Task 49.39 (design record 13 section 16.17): a drive a reset does not
 * bring back, given up. */
static void test_final_giveup(void)
{
    CHECK_EQ(UasFinalGiveUpDue(0), 0, "no reset yet: the first is taken");
    CHECK_EQ(UasFinalGiveUpDue(1), 0, "one reset: the second is taken");
    CHECK_EQ(UasFinalGiveUpDue(2), 0, "two resets: the third is taken");
    CHECK_EQ(UasFinalGiveUpDue(3), 1, "three: no fourth, given up");
    CHECK_EQ(UasFinalGiveUpDue(4), 1, "past the limit: given up");
    CHECK_EQ(UasFinalGiveUpDue(0xFFFFFFFFUL), 1, "any large count");
    CHECK_EQ(UAS_FINAL_RESET_LIMIT, 3, "the limit is three");
    CHECK_EQ(UasDataDelivered(1, 512), 1, "data delivered clears");
    CHECK_EQ(UasDataDelivered(1, 1), 1, "a short transfer with a byte");
    CHECK_EQ(UasDataDelivered(1, 0), 0,
             "no byte (a GOOD with no data phase): not cleared");
    CHECK_EQ(UasDataDelivered(0, 512), 0, "a failed transfer: not cleared");
    CHECK_EQ(XHCI98_FAIL_E_GIVEN_UP, 10, "the ring's event 10");
    {
        ULONG c[UAS_FINAL_DIRS];

        /* Two read resets, one write reset, a good write: the reads'
         * count survives, and the third read reset is the give-up. */
        c[0] = 0;
        c[1] = 0;
        UasFinalCount(c, UAS_FINAL_DIR_IN);
        UasFinalCount(c, UAS_FINAL_DIR_IN);
        UasFinalCount(c, UAS_FINAL_DIR_OUT);
        UasFinalClear(c, UAS_FINAL_DIR_OUT);
        CHECK_EQ(c[0], 2, "the write's data leaves the reads' count");
        CHECK_EQ(c[1], 0, "and clears the writes' own");
        CHECK_EQ(UasFinalDue(c, UAS_FINAL_DIR_IN), 0,
                 "a third read reset is still taken");
        UasFinalCount(c, UAS_FINAL_DIR_IN);
        CHECK_EQ(UasFinalDue(c, UAS_FINAL_DIR_IN), 1,
                 "three read resets: the next read's is the give-up");
        CHECK_EQ(UasFinalDue(c, UAS_FINAL_DIR_OUT), 0,
                 "a write's reset is counted by writes alone");
        UasFinalClear(c, UAS_FINAL_DIR_OUT);
        UasFinalCount(c, UAS_FINAL_DIR_OUT);
        UasFinalClear(c, UAS_FINAL_DIR_OUT);
        CHECK_EQ(UasFinalDue(c, UAS_FINAL_DIR_IN), 1,
                 "whatever the writes do, the reads stay given up");
        /* The mirror: write resets, reads delivered. */
        c[0] = 0;
        c[1] = 0;
        UasFinalCount(c, UAS_FINAL_DIR_OUT);
        UasFinalCount(c, UAS_FINAL_DIR_OUT);
        UasFinalCount(c, UAS_FINAL_DIR_IN);
        UasFinalClear(c, UAS_FINAL_DIR_IN);
        CHECK_EQ(c[1], 2, "a read's data leaves the writes' count");
        UasFinalCount(c, UAS_FINAL_DIR_OUT);
        CHECK_EQ(UasFinalDue(c, UAS_FINAL_DIR_OUT), 1,
                 "three write resets: the next write's is the give-up");
        CHECK_EQ(UasFinalDue(c, UAS_FINAL_DIR_IN), 0,
                 "and a read's reset is still taken");
        /* One reset for both directions at once counts each. */
        c[0] = 0;
        c[1] = 0;
        UasFinalCount(c, UAS_FINAL_DIR_IN | UAS_FINAL_DIR_OUT);
        CHECK_EQ(c[0] == 1 && c[1] == 1, 1, "a reset for both counts both");
        UasFinalClear(c, UAS_FINAL_DIR_IN);
        CHECK_EQ(c[0] == 0 && c[1] == 1, 1, "data in clears in alone");
        UasFinalClear(c, UAS_FINAL_DIR_IN | UAS_FINAL_DIR_OUT);
        CHECK_EQ(c[0] == 0 && c[1] == 0, 1, "a replug (start) clears both");
        c[0] = 2;
        c[1] = 3;
        CHECK_EQ(UasFinalDue(c, UAS_FINAL_DIR_IN | UAS_FINAL_DIR_OUT), 1,
                 "a reset for both: either direction at the limit");
        c[0] = 0xFFFFFFFFUL;
        UasFinalCount(c, UAS_FINAL_DIR_IN);
        CHECK_EQ(c[0], 0xFFFFFFFFUL, "the count saturates");
        CHECK_EQ(UasFinalDue(c, 0), 0, "no direction asking: no give-up");
    }
}

/* Task 49.39 (design record 13 section 16.16): the drive's own refusal of
 * ATA PASS-THROUGH, replayed per operation code. */
static void test_ata_replay(void)
{
    UCHAR a1[12];
    UCHAR c85[16];
    UCHAR rd10[10];
    UCHAR fixed[18];
    UCHAR desc[8];
    UCHAR out[18];
    ULONG n;

    memset(a1, 0, sizeof(a1));
    a1[0] = 0xA1;
    memset(c85, 0, sizeof(c85));
    c85[0] = 0x85;
    memset(rd10, 0, sizeof(rd10));
    rd10[0] = 0x28;
    memset(fixed, 0, sizeof(fixed));
    fixed[0] = 0x70;
    fixed[2] = 0x05;
    fixed[7] = 10;
    fixed[12] = 0x20;
    memset(desc, 0, sizeof(desc));
    desc[0] = 0x72;
    desc[1] = 0x05;
    desc[2] = 0x20;

    CHECK_EQ(UasAtaBit(a1, 12), UAS_ATA_REFUSED_12, "A1h is (12)");
    CHECK_EQ(UasAtaBit(c85, 16), UAS_ATA_REFUSED_16, "85h is (16)");
    CHECK_EQ(UasAtaBit(rd10, 10), 0, "READ (10) is neither");
    CHECK_EQ(UasAtaBit(NULL, 0), 0, "no CDB");

    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, fixed, 18),
             UAS_ATA_REFUSED_12, "(12) refused 5/20/00, fixed sense: latch");
    CHECK_EQ(UasAtaRefusal(c85, 16, UAS_SCSI_CHECK_CONDITION, desc, 8),
             UAS_ATA_REFUSED_16, "(16) refused, descriptor sense: latch");
    CHECK_EQ(UasAtaRefusal(a1, 12, 0, fixed, 18), 0,
             "GOOD never latches");
    CHECK_EQ(UasAtaRefusal(rd10, 10, UAS_SCSI_CHECK_CONDITION, fixed, 18),
             0, "another command's 5/20/00 is not this");
    fixed[12] = 0x24;
    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, fixed, 18), 0,
             "5/24/00 (a field) does not latch");
    fixed[12] = 0x20;
    fixed[13] = 0x01;
    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, fixed, 18), 0,
             "5/20/01 does not latch");
    fixed[13] = 0x00;
    fixed[2] = 0x06;
    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, fixed, 18), 0,
             "another sense key does not latch");
    fixed[2] = 0x05;
    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, fixed, 13), 0,
             "fixed sense too short to hold ASCQ");
    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, NULL, 0), 0,
             "no sense");
    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, desc, 3), 0,
             "descriptor sense too short");

    memset(out, 0xEE, sizeof(out));
    n = UasInvalidOpcodeSense(out, sizeof(out));
    CHECK_EQ(n, UAS_INVALID_CDB_SENSE, "the whole fixed sense");
    CHECK_EQ(UasAtaRefusal(a1, 12, UAS_SCSI_CHECK_CONDITION, out, n),
             UAS_ATA_REFUSED_12, "the replayed answer is the refusal");
    CHECK_EQ(out[7], UAS_INVALID_CDB_SENSE - 8, "its additional length");
    memset(out, 0xEE, sizeof(out));
    n = UasInvalidOpcodeSense(out, 5);
    CHECK_EQ(n, 5, "held to the room");
    CHECK_EQ(out[5], 0xEE, "nothing past the room");
    CHECK_EQ(UasInvalidOpcodeSense(NULL, 18), 0, "no buffer");
    CHECK_EQ(XHCI98_FAIL_E_REPLAYED, 9, "the ring's event 9");
}

int main(void)
{
    test_tags();
    test_tele();
    test_policy();
    test_devlim();
    test_command_iu();
    test_task_iu();
    test_parse();
    test_luns();
    test_config();
    test_report_luns();
    test_status();
    test_direction();
    test_ids();
    test_sense_class();
    test_vpd_list();
    test_id_match();
    test_wake();
    test_pt_basics();
    test_pt_scsi_plan();
    test_ata_cdb();
    test_ata_plan();
    test_ata_regs();
    test_pt_result();
    test_pt_partial48();
    test_sioc();
    test_smart_plan();
    test_smart_result();
    test_no_ata();
    test_fail_pack();
    test_fast_reset();
    test_final_timeout();
    test_final_giveup();
    test_ata_replay();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
