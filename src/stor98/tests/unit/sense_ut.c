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
 * sense_ut - host unit test of src/sense.c (PLAN "Sense rules", R3-9;
 * TESTING "Sense construction", unit-harness route; review round 2 P2-7:
 * sense ownership). Built against the NT and the 9x SRB.H by
 * tests\unit\build.cmd; the exit code is the number of failed checks.
 *
 * The SRBs are built by hand with any flags and buffers, which the
 * pass-through route cannot do: in particular two requests failing in one
 * completion batch, one without autosense and one with it, the case no
 * guest tool can produce (pass-through requests are untagged and run one
 * at a time).
 */

#include <stdio.h>
#include <string.h>
#include "nvme2k.h"

static int failures;
static int completions;

/* The two completion helpers sense.c uses (nvme2k_scsi.c in the driver). */
BOOLEAN ScsiSuccess(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    (void)DevExt;
    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    completions++;
    return TRUE;
}

BOOLEAN ScsiError(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR SrbStatus)
{
    (void)DevExt;
    if (SrbStatus == SRB_STATUS_ERROR && Srb->ScsiStatus == SCSISTAT_CHECK_CONDITION &&
        (Srb->SrbStatus & SRB_STATUS_AUTOSENSE_VALID)) {
        SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    }
    Srb->SrbStatus = SrbStatus;
    completions++;
    return TRUE;
}

#include "sense.c"

static HW_DEVICE_EXTENSION dev;

static void check(int cond, const char *what)
{
    printf("  %s: %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) {
        failures++;
    }
}

/* A failed request: sense buffer of len bytes (guard 0xCC), flags. */
static void failreq(SCSI_REQUEST_BLOCK *srb, UCHAR *buf, int len, ULONG flags, UCHAR key, UCHAR asc)
{
    memset(srb, 0, sizeof(*srb));
    memset(buf, 0xCC, 32);
    srb->Function = SRB_FUNCTION_EXECUTE_SCSI;
    srb->Cdb[0] = 0x28;
    srb->SrbFlags = flags;
    srb->SenseInfoBuffer = len ? buf : NULL;
    srb->SenseInfoBufferLength = (UCHAR)len;
    srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    ScsiSetSense(&dev, srb, key, asc, 0);
}

static int untouched(UCHAR *buf)
{
    int i;

    for (i = 0; i < 32; i++) {
        if (buf[i] != 0xCC) {
            return 0;
        }
    }
    return 1;
}

/* REQUEST SENSE with allocation length alloc; returns the key, sets *asc. */
static int reqsense(int alloc, UCHAR *asc, ULONG *len, UCHAR desc)
{
    SCSI_REQUEST_BLOCK srb;
    UCHAR data[64];

    memset(&srb, 0, sizeof(srb));
    memset(data, 0xCC, sizeof(data));
    srb.Function = SRB_FUNCTION_EXECUTE_SCSI;
    srb.Cdb[0] = SCSIOP_REQUEST_SENSE;
    srb.Cdb[1] = desc;
    srb.Cdb[4] = (UCHAR)alloc;
    srb.DataBuffer = data;
    srb.DataTransferLength = sizeof(data);
    ScsiSenseNewCommand(&dev, &srb);
    ScsiHandleRequestSense(&dev, &srb);
    *len = srb.DataTransferLength;
    *asc = data[12];
    if (srb.ScsiStatus != SCSISTAT_GOOD) {
        return -1;
    }
    return data[2] & 0x0F;
}

static void newcmd(UCHAR op)
{
    SCSI_REQUEST_BLOCK srb;

    memset(&srb, 0, sizeof(srb));
    srb.Function = SRB_FUNCTION_EXECUTE_SCSI;
    srb.Cdb[0] = op;
    ScsiSenseNewCommand(&dev, &srb);
}

int main(void)
{
    SCSI_REQUEST_BLOCK a, b, c;
    UCHAR ba[32], bb[32], bc[32];
    UCHAR asc;
    ULONG len;
    int key, i;

    memset(&dev, 0, sizeof(dev));

    printf("1. A fails without autosense, then B with autosense in the same batch (review 2, P2-7)\n");
    failreq(&a, ba, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);
    failreq(&b, bb, 18, 0, SCSI_SENSE_MEDIUM_ERROR, 0x11);
    check(!(a.SrbStatus & SRB_STATUS_AUTOSENSE_VALID), "A: no AUTOSENSE_VALID");
    check((b.SrbStatus & SRB_STATUS_AUTOSENSE_VALID) && bb[0] == 0x70 && bb[2] == 3 && bb[12] == 0x11 &&
          bb[18] == 0xCC, "B: exactly 18 bytes of autosense, 03/11");
    key = reqsense(18, &asc, &len, 0);
    check(key == 5 && asc == 0x21 && len == 18, "REQUEST SENSE returns A's 05/21 (B's autosense did not erase it)");
    key = reqsense(18, &asc, &len, 0);
    check(key == 0, "then NO SENSE");

    printf("2. A and B both without autosense: one entry each, oldest first\n");
    failreq(&a, ba, 14, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);
    failreq(&b, bb, 18, SRB_FLAGS_DISABLE_AUTOSENSE, SCSI_SENSE_MEDIUM_ERROR, 0x11);
    check(untouched(ba), "A (14-byte buffer): no byte written");
    check(untouched(bb) && !(b.SrbStatus & SRB_STATUS_AUTOSENSE_VALID), "B (DISABLE_AUTOSENSE): no byte written");
    key = reqsense(18, &asc, &len, 0);
    check(key == 5 && asc == 0x21, "first REQUEST SENSE: A's 05/21");
    key = reqsense(18, &asc, &len, 0);
    check(key == 3 && asc == 0x11, "second REQUEST SENSE: B's 03/11");
    key = reqsense(18, &asc, &len, 0);
    check(key == 0, "third: NO SENSE");

    printf("3. A new command ends kept sense; INQUIRY and REPORT LUNS do not\n");
    failreq(&a, ba, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);
    newcmd(SCSIOP_INQUIRY);
    newcmd(0xA0);
    check(dev.SenseCount == 1, "kept across INQUIRY and REPORT LUNS");
    newcmd(0x28);
    key = reqsense(18, &asc, &len, 0);
    check(key == 0, "READ(10) started: REQUEST SENSE gives NO SENSE");

    printf("4. Reset (new start, resume, acknowledged reset) ends kept sense\n");
    failreq(&a, ba, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);
    ScsiSenseReset(&dev);
    key = reqsense(18, &asc, &len, 0);
    check(key == 0, "after ScsiSenseReset: NO SENSE");

    printf("5. Bounded REQUEST SENSE\n");
    failreq(&a, ba, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);
    key = reqsense(8, &asc, &len, 0);
    check(key == 5 && len == 8, "allocation length 8: 8 bytes");
    key = reqsense(18, &asc, &len, 1);
    check(key == -1, "DESC=1: CHECK CONDITION");
    key = reqsense(18, &asc, &len, 0);
    check(key == 5 && asc == 0x24, "the refusal's own sense (05/24) is kept for the next REQUEST SENSE");

    printf("6. More failures than entries: the oldest keep their place\n");
    ScsiSenseReset(&dev);
    for (i = 0; i < ENG_SENSE_KEEP + 1; i++) {
        failreq(&c, bc, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, (UCHAR)(0x30 + i));
    }
    check(dev.SenseCount == ENG_SENSE_KEEP && dev.SenseDropped == 1, "entries capped, one dropped");
    key = reqsense(18, &asc, &len, 0);
    check(key == 5 && asc == 0x30, "oldest returned first");
    ScsiSenseReset(&dev);

    printf("7. Overflow, then replenishment: a dropped request never gets a later one's sense (review 3, N4)\n");
    ScsiSenseReset(&dev);
    for (i = 0; i < ENG_SENSE_KEEP + 1; i++) {
        /* A-D kept (0x40..0x43), E (0x44) overflows */
        failreq(&c, bc, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, (UCHAR)(0x40 + i));
    }
    check(dev.SenseCount == ENG_SENSE_KEEP && dev.SenseLost == 1, "A-D kept, E queued as a placeholder");
    key = reqsense(18, &asc, &len, 0);
    check(key == 5 && asc == 0x40, "REQUEST SENSE: A");
    failreq(&c, bc, 0, 0, SCSI_SENSE_MEDIUM_ERROR, 0x11);  /* F fails after a slot freed */
    check(dev.SenseCount == ENG_SENSE_KEEP - 1 && dev.SenseLost == 2,
          "F queued behind E's placeholder, not in the freed slot");
    for (i = 1; i < ENG_SENSE_KEEP; i++) {
        key = reqsense(18, &asc, &len, 0);
        check(key == 5 && asc == (UCHAR)(0x40 + i), "REQUEST SENSE: B, C, D in order");
    }
    key = reqsense(18, &asc, &len, 0);
    check(key == 0 && asc == 0 && len == 18, "E (lost): NO SENSE, not F's 03/11");
    key = reqsense(18, &asc, &len, 0);
    check(key == 0 && asc == 0, "F (lost behind E): NO SENSE");
    check(dev.SenseCount == 0 && dev.SenseLost == 0, "queue empty");
    failreq(&c, bc, 0, 0, SCSI_SENSE_MEDIUM_ERROR, 0x11);  /* G: stored again */
    key = reqsense(18, &asc, &len, 0);
    check(key == 3 && asc == 0x11, "after the placeholders drain, G's 03/11 is kept and returned");
    for (i = 0; i < ENG_SENSE_KEEP + 2; i++) {
        failreq(&c, bc, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x22);
    }
    newcmd(0x28);
    check(dev.SenseCount == 0 && dev.SenseLost == 0, "a new command ends kept entries and placeholders");
    for (i = 0; i < ENG_SENSE_KEEP + 2; i++) {
        failreq(&c, bc, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x22);
    }
    ScsiSenseReset(&dev);
    check(dev.SenseCount == 0 && dev.SenseLost == 0, "ScsiSenseReset ends placeholders");

    printf("8. Placeholder saturation is sticky (review 4): 260 failures, 259 reads, then a refused DESC=1\n");
    ScsiSenseReset(&dev);
    for (i = 0; i < 260; i++) {
        failreq(&c, bc, 0, 0, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);   /* INQUIRYs failing, no autosense */
        newcmd(SCSIOP_INQUIRY);
    }
    check(dev.SenseCount == ENG_SENSE_KEEP && dev.SenseLost == 0xFF, "4 kept, placeholders saturated");
    for (i = 0; i < 259; i++) {
        key = reqsense(18, &asc, &len, 0);
    }
    check(key == 0 && dev.SenseLost == 0xFF, "after 259 reads: still sticky, NO SENSE");
    key = reqsense(18, &asc, &len, 1);      /* refused; its own 05/24 is not stored while sticky */
    check(key == -1, "DESC=1 refused");
    key = reqsense(18, &asc, &len, 0);
    check(key == 0 && asc == 0, "260th failure: NO SENSE, never the refusal's 05/24");
    for (i = 0; i < 300 && key == 0; i++) {
        key = reqsense(18, &asc, &len, 0);
    }
    check(key == 0 && dev.SenseCount == 0, "sticky: NO SENSE however many reads follow");
    ScsiSenseReset(&dev);
    failreq(&c, bc, 0, 0, SCSI_SENSE_MEDIUM_ERROR, 0x11);
    key = reqsense(18, &asc, &len, 0);
    check(key == 3 && asc == 0x11, "after ScsiSenseReset: storage resumes");
    ScsiSenseReset(&dev);

    printf("9. Another LU (DevExt NULL): nothing kept\n");
    memset(&c, 0, sizeof(c));
    c.SenseInfoBuffer = NULL;
    ScsiSetSense(NULL, &c, SCSI_SENSE_ILLEGAL_REQUEST, 0x25, 0);
    check(dev.SenseCount == 0, "LU 0 unaffected");

    printf("%s: %d failed check(s), %d completions\n", failures ? "FAIL" : "PASS", failures, completions);
    return failures > 250 ? 250 : failures;     /* a count of 256 would exit 0 */
}
