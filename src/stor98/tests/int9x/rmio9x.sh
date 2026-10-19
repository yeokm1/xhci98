#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# rmio9x.sh <98|me> <tag> [boot-wait]: surprise removal during I/O, with interrupts and in the
# polling mode (docs/TESTING.md "Sustained absence during I/O", "Polling mode"), cm9x stand-in.
#  F a 64 MiB write running, then tunnel surprise: gone with commands issued; cm9x's
#    DOWN_QUIESCED is evidence 2 and the held requests complete NO_DEVICE then (not at the MD-1
#    deadline); the writer returns; re-arrival verifies the data written before.
#  P StorPollMode 1 (D:\POLL1.REG, read by the next load): polling forced (0x152 why 1),
#    HwInterrupt never called (0x156 absent), write/verify, then F's removal during I/O again.
#  Z StorPollMode 0 again: interrupt mode (0x152 why 0), verify.
# Needs the transfer image of mkxfer.sh (POLL1.REG, POLL0.REG). The verdict table follows,
# exit status = failures.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: rmio9x.sh <98|me> <tag> [boot-wait]"; exit 2; }
cd "$B"
os=$1; tag=$2; bw=${3:-110}; SECTIONS="${SECTIONS:-F P Z}"
LOG=$os/debugcon-$tag.log
show() { since $1 | grep -a "^n98 [0-9a-f]* 12[0245] \|^n98 [0-9a-f]* \(43\|81\|83\|84\|d1\|152\|156\) \|^cm9x [0-9a-f]* 1c[bc] \|^ctl" | cut -c1-150; }
# reload: surprise removal of an idle device, the CM's removal, re-arrival: a new load
reload() {
  local L; L=$(mark); "$INT9X/g.sh" $os tun surprise >/dev/null
  waitfor "$(cm 1b6 000000c0)" $L 180; sleep 5; keys "$1"; sleep 5
  up
}
# rmio <name> <section>: removal while a write of <name> runs, and its checks
rmio() {
  local L n
  L=$(mark); t "start /m C:\\CTL write F:\\$1 65536 4" 10
  "$INT9X/g.sh" $os tun surprise >/dev/null
  expect_wait $L "$(st 81)" 30 "gone entered (0x81)"
  n=$(since $L | grep -aE "$(st 81)" | head -1 | awk '{print substr($4, 5, 4)}')
  if [ -n "$n" ] && [ $(( 0x$n )) -gt 0 ]; then pass "commands issued at T1: $(( 0x$n ))"
  else inconclusive "no command issued when the device went (the write had not reached the driver): the held-request checks below do not apply"; fi
  expect_wait $L "$(st 124)" 60 "DOWN_QUIESCED seen (0x124)"
  expect_wait $L "$(st d1 '[0-9a-f]{6}02')" 30 "isolation evidence 2 (0xD1)"
  expect_wait $L "$(st 125 '[0-9a-f]{3}[13579bdf][0-9a-f]{4}')" 30 "AckGeneration written (0x125)"
  expect_wait $L "$(cm 1cc '[0-9a-f]{6}01')" 30 "the CM saw the acknowledgement (0x1CC acked)"
  sleep 30; keys "$2"; sleep 5; keys "$2b"
  if waitfor "^ctl write $(re_file "$1")" $L 240; then pass "the writer returned: $(first $L "^ctl write $(re_file "$1")")"
  else fail "the writer did not return within 240 s of the removal"; fi
  expect_wait $L "$(cm 1b6 000000c0)" 180 "endpoint devnode retired (0x1B6 0xC0)"
  expect_no $L "$(st 83)" "no MD-1 deadline: the evidence came first (0x83 absent)"
  expect_no $L "$(st 84)" "no held request completed by the deadline path (0x84 absent)"
}
"$INT9X/run.sh" $os $tag new | tail -1; sleep $bw
"$INT9X/g.sh" $os dos; t "D:\INT NVINTD" 10
arm
sec "0 bring-up, write/verify"
L=$(mark); up
expect $L "$(st 43)" "bring-up done (0x43)"
expect $L "$(st 152 '[0-9a-f]{2}00[0-9a-f]{4}')" "interrupt mode (0x152 why 0)"
write T1.BIN 2048 5; verify T1.BIN 2048 5

if want F; then sec "F surprise removal during I/O (interrupts)"
rmio SR.BIN F
L=$(mark); up; verify T1.BIN 2048 5; show $L
fi
if want P; then sec "P polling mode forced (StorPollMode 1)"
t "regedit /s D:\POLL1.REG" 6
L=$(mark); reload P0; show $L
expect $L "$(st 152 '[0-9a-f]{2}01[0-9a-f]{4}')" "polling forced by StorPollMode (0x152 why 1)"
expect $L "$(st 43)" "bring-up done in the polling mode (0x43)"
LP=$L
write P1.BIN 4096 6; verify P1.BIN 4096 6; verify T1.BIN 2048 5
rmio SP.BIN P
expect_no $LP "$(st 156)" "HwInterrupt not called while polling (0x156 absent)"
L=$(mark); up
expect $L "$(st 152 '[0-9a-f]{2}01[0-9a-f]{4}')" "re-arrival still polling (0x152 why 1)"
verify P1.BIN 4096 6; show $L
fi
if want Z; then sec "Z interrupt mode again (StorPollMode 0)"
t "regedit /s D:\POLL0.REG" 6
L=$(mark); reload Z0; show $L
expect $L "$(st 152 '[0-9a-f]{2}00[0-9a-f]{4}')" "interrupt mode (0x152 why 0)"
verify T1.BIN 2048 5
fi
"$INT9X/g.sh" $os s $tag-end
decoder $os/trace-$tag.log
verdict
