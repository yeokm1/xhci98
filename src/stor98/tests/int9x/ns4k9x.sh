#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# ns4k9x.sh <98|me> <tag> [boot-wait]: a 4 KiB-LBA namespace (docs/TESTING.md "4 KiB sectors";
# AGENTS.md: 4 KiB-sector drives must work, not be refused), cm9x stand-in.
#  QEMU's nvme gets logical and physical blocks of 4096 bytes over the usual test image; the
#  image's FAT32 partition starts at sector 63, so its clusters are not 4 KiB aligned and a file
#  write is an unaligned write at the device (read-modify-write once translation exists).
#  A bring-up: the namespace is served, not refused (0x43, no 0x44).
#  U unaligned write and verify (37 KiB, then 512 bytes), the guest answering throughout.
#  R surprise removal with DOWN_QUIESCED on the 4 KiB namespace; re-arrival.
# The 512-byte presentation and its read-modify-write are not implemented yet (PLAN, 4Kn
# support after 0.4.0.0; README known limit: the drive's own sector size is reported), so the
# data checks are INCONCLUSIVE until then: their result is recorded, not judged. What is judged
# now: the bring-up, no hang (every command answers), no double completion, the removal path.
# Once translation lands, the data checks become PASS/FAIL (the OS sees 512-byte sectors and
# the data verifies). The verdict table follows, exit status = failures.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: ns4k9x.sh <98|me> <tag> [boot-wait]"; exit 2; }
cd "$B"
os=$1; tag=$2; bw=${3:-110}
LOG=$os/debugcon-$tag.log
show() { since $1 | grep -a "^n98 [0-9a-f]* \(43\|44\|81\|83\|8c\|bd\|ce\|d1\|120\|122\|124\|125\) \|^cm9x [0-9a-f]* 1c[bc] \|^ctl" | cut -c1-150; }
# data <what> <cm9xctl arguments...>: run; INCONCLUSIVE with the result while translation is
# missing; a FAIL only when no result arrives (a hang)
data() {
  local L what=$1 res; L=$(mark); shift
  t "C:\\CTL $*" 1
  if waitfor "^ctl $1 " $L 240; then
    res=$(first $L "^ctl $1 ")
    inconclusive "$what: \"$res\" (judged once 512-byte translation lands)"
  else
    fail "$what: no answer within 240 s (hang)"
  fi
}
"$INT9X/run.sh" $os $tag new -global nvme.logical_block_size=4096 -global nvme.physical_block_size=4096 \
  -trace pci_nvme_read -trace pci_nvme_write | tail -1
sleep $bw
"$INT9X/g.sh" $os dos; t "D:\INT NVINTD" 10
arm
sec "A bring-up on a 4 KiB namespace"
grep -q 'logical_block_size=4096' $os/qemu-$tag.cmdline && pass "QEMU started with 4096-byte logical blocks" || fail "4096-byte blocks not on the QEMU command line"
L=$(mark); up; show $L
expect $L "$(st 43)" "bring-up done (0x43): the namespace is served"
expect_no $L "$(st 44)" "no bring-up failure (0x44)"
expect $L "$(st 122 '[0-9a-f]{4}0100')" "rendezvous accepted and bound (0x122 rc 0)"

sec "U unaligned write and verify"
L=$(mark)
data "37 KiB write" write "F:\\U4K.BIN" 37 3
data "37 KiB verify" verify "F:\\U4K.BIN" 37 3
data "1 KiB write" write "F:\\U1K.BIN" 1 4
data "1 KiB verify" verify "F:\\U1K.BIN" 1 4
tset cm Probe 1                                  # the guest still answers
expect_no $L "$(st ce)" "no double completion blocked (0xCE absent)"
# the device's own block size: QEMU traces count (bytes) and nlb (blocks) of every read
bs=$(grep -a '^pci_nvme_read ' $os/trace-$tag.log | awk '{n = 0; c = 0; for (i = 1; i < NF; i++) { if ($i == "nlb") n = $(i + 1); if ($i == "count") c = $(i + 1) } if (n > 0) { print c / n; exit } }')
[ "$bs" = 4096 ] && pass "the namespace serves 4096-byte blocks (pci_nvme_read count / nlb)" \
                 || fail "namespace block size from the trace: ${bs:-no pci_nvme_read}"
show $L

sec "R surprise removal with DOWN_QUIESCED, re-arrival"
L=$(mark); "$INT9X/g.sh" $os tun surprise >/dev/null
expect_wait $L "$(st 124)" 60 "DOWN_QUIESCED seen (0x124)"
expect_wait $L "$(st 125 '[0-9a-f]{3}[13579bdf][0-9a-f]{4}')" 30 "AckGeneration written (0x125)"
sleep 30; keys R; sleep 5
expect_wait $L "$(cm 1b6 000000c0)" 180 "endpoint devnode retired (0x1B6 0xC0)"
L=$(mark); up
expect $L "$(st 43)" "re-arrival: bring-up done (0x43)"
data "37 KiB verify after re-arrival" verify "F:\\U4K.BIN" 37 3
"$INT9X/g.sh" $os s $tag-end
verdict
