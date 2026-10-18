#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# smoke9x.sh <98|me> <tag> [boot-wait]: smoke on 9x with the cm9x stand-in and the debug-port image.
#  A bring-up + write/verify; B surprise removal with cm9x link-state (DOWN_QUIESCED);
#  C re-arrival (reuse of the RELEASED block) + verify; D surprise with NoFlag (no evidence:
#  MD-1, QUARANTINED) then re-arrival (E1/E2 -> RELEASED); E clean eject via cm9x (CTL eject:
#  REMOVE_PENDING, the MD-5 removal transaction).
# Every section ends with PASS/FAIL checks; the verdict table follows, exit status = failures.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: smoke9x.sh <98|me> <tag> [boot-wait]"; exit 2; }
cd "$B"
os=$1; tag=$2; bw=${3:-110}
LOG=$os/debugcon-$tag.log
pool() { since $1 | grep -a "^n98 [0-9a-f]* 12[0-9] \|^n98 [0-9a-f]* 43 \|^n98 [0-9a-f]* 44 \|^n98 [0-9a-f]* 83 \|^n98 [0-9a-f]* d1 \|^n98 [0-9a-f]* cf \|^n98 [0-9a-f]* 67 \|^n98 [0-9a-f]* 81 \|^cm9x\|^ctl" | grep -av "cm9x [0-9a-f]* 1c9 \|heartbeat" | cut -c1-150; }
"$INT9X/run.sh" $os $tag new | tail -1; sleep $bw
"$INT9X/g.sh" $os dos; t "D:\INT NVINTD" 10
grep -a "cm9x DriverEntry\|ports active\|bridge devnode;" $LOG | cut -c1-120 | head -4
arm

sec "A bring-up, write/verify"
L=$(mark); up
expect $L "$(st 43)" "bring-up done (0x43)"
expect $L "$(st 120 '[0-9a-f]{6}00')" "pool block found (0x120 rc 0)"
expect $L "$(st 122 '[0-9a-f]{4}0100')" "rendezvous accepted, entry bound at the find (0x122 rc 0): cm9x publishes UP before it re-enumerates"
write T1.BIN 2048 5; verify T1.BIN 2048 5
pool $L

sec "B surprise removal, CM link-state (DOWN_QUIESCED)"
L=$(mark); "$INT9X/g.sh" $os tun surprise >/dev/null
expect_wait $L "$(st 124)" 60 "DOWN_QUIESCED seen (0x124)"
expect_wait $L "$(st 125 '[0-9a-f]{3}[13579bdf][0-9a-f]{4}')" 30 "AckGeneration written (0x125, written bit)"
expect_wait $L "$(cm 1cc '[0-9a-f]{6}01')" 30 "the CM saw the acknowledgement (0x1CC acked), not its timeout"
sleep 30; keys B; sleep 5
expect_wait $L "$(cm 1b6 000000c0)" 180 "endpoint devnode retired (0x1B6 0xC0)"
expect $L "$(st d1 '[0-9a-f]{6}02')" "isolation evidence 2, link down (0xD1)"
sleep 20
expect_no $L "$(st 83)" "no MD-1 deadline after the evidence (0x83 absent)"
pool $L

sec "C re-arrival: reuse of the RELEASED block, verify"
L=$(mark); up
expect $L "$(st 43)" "bring-up done (0x43)"
expect $L "$(st 120 '[0-9a-f]{5}200')" "the RELEASED block reused (0x120 rc 0, how 2)"
verify T1.BIN 2048 5
pool $L

sec "D surprise without QUIESCED (NoFlag): MD-1, QUARANTINED; re-arrival releases it"
tset cm NoFlag 1
L=$(mark); "$INT9X/g.sh" $os tun surprise >/dev/null
expect_wait $L "$(st 81)" 60 "gone entered (0x81)"
expect_wait $L "$(st 83)" 60 "MD-1 gone deadline taken (0x83)"
expect_wait $L "$(cm 1cc '[0-9a-f]{6}00')" 60 "the CM's link-down wait timed out (0x1CC not acked)"
waitfor "$(cm 1b6 000000c0)" $L 180; sleep 20
expect_no $L "$(st 125 '[0-9a-f]{3}[13579bdf][0-9a-f]{4}')" "no AckGeneration for a statement without QUIESCED"
pool $L
tset cm NoFlag 0
L=$(mark); up
expect $L "$(st 43)" "bring-up done (0x43)"
expect $L "$(st 121 '[0-9a-f]{2}0[45][0-9a-f]{4}')" "the quarantined record released by E1 or E2 (0x121 cause 4 or 5)"
verify T1.BIN 2048 5
pool $L

sec "E clean eject (CTL eject: REMOVE_PENDING, MD-5 removal transaction)"
arm
L=$(mark); t "start /m C:\\CTL eject - 0" 2; waitfor "$EJECT_RESULT" $L 240; sleep 30
expect $L "$(st 12a)" "request seen by stor98 (0x12A)"
expect $L "$(rmack 1)" "RemoveAck CLEAN published (0x12C outcome 1)"
expect $L "$(ejected C1EA)" "eject result C1EA (clean)"
pool $L
"$INT9X/g.sh" $os s $tag-end
decoder $os/trace-$tag.log --ejects
verdict
