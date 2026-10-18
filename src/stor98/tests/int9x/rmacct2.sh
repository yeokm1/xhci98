#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# rmacct2.sh <98|me> <tag> "<modes>" [boot-wait]: pool accounting fail-closed (CorruptAcct), and (SKEW=1) BudgetSkewMs.
#  S  BudgetSkewMs 5600: the first check after the request is past 5500 ms: disable at once, outcome 2 with the budget bit.
#  Cn CorruptAcct n: this boot's accounting damaged at relinquish; the next load must fail closed (no disk,
#     stage 120 reason); a cold restart (shut down, quit, start; new CM proof) starts new accounting.
# 'back' (lib.sh) waits from the eject's own mark (a D2 re-plug already done counts), wl.sh is
# bounded by wall-clock time, and restarts are cold. The verdict table follows, exit status = failures.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: rmacct2.sh <98|me> <tag> \"<modes>\" [boot-wait]"; exit 2; }
cd "$B"
os=$1; tag=$2; modes=${3:-"3 4 1"}; bw=${4:-110}
LOG=$os/debugcon-$tag.log
show() { since $1 | grep -a "^n98 [0-9a-f]* 12[0c] \|^n98 [0-9a-f]* 131 \|^n98 [0-9a-f]* 43 \|^cm9x [0-9a-f]* 1ce 0[47]\|^ctl" | cut -c1-150; }
boot() { "$INT9X/g.sh" $os dos; t "D:\INT NVINTD" 10; }
FAILCLOSED="$(st 120 '0000..01')"
"$INT9X/run.sh" $os $tag new | tail -1; sleep $bw
boot
sec "0 bring-up, write"
L=$(mark); up
expect $L "$(st 43)" "bring-up done (0x43)"
write T1.BIN 1024 5; show $L

if [ "${SKEW:-0}" = 1 ]; then
sec "S BudgetSkewMs 5600"
tset q9 BudgetSkewMs 5600
L=$(mark); eject 10; back; show $L               # a new load that reads the option
L=$(mark); eject; show $L
expect $L "$(st 12c '[13579bdf][0-9a-f]{5}02')" "RemoveAck ISOLATED_NOT_CLEAN with the budget bit (0x12C detail 1x, outcome 2)"
expect_no $L "$(rmack 3)" "not FAILED"
tset q9 BudgetSkewMs 0
L=$(mark); back; verify T1.BIN 1024 5; show $L
fi

n=0
for m in $modes; do
  sec "C$m CorruptAcct $m: the next load fails closed; a cold restart recovers"
  tset q9 CorruptAcct $m
  L=$(mark); eject 10; back; show $L              # this load reads the option
  tset q9 CorruptAcct 0
  L=$(mark); eject; back; show $L                 # its relinquish damages the accounting; the next load must fail
  expect_count $L "$FAILCLOSED" 1 "loads that failed closed (0x120 rc 1)"
  expect_no $L "$(st 43)" "no bring-up on the damaged accounting"
  "$INT9X/g.sh" $os tun down >/dev/null; sleep 15; "$INT9X/g.sh" $os t "exit\n" >/dev/null; sleep 3; "$INT9X/g.sh" $os off
  for i in $(seq 1 60); do [ -f $os/qemu.pid ] || break; sleep 5; done
  [ -f $os/qemu.pid ] && { "$INT9X/g.sh" $os quit; sleep 5; rm -f $os/qemu.pid; }
  n=$((n+1)); export LOGTAG=$2-c$n; tag=$LOGTAG; LOG=$os/debugcon-$tag.log
  LOGTAG=$tag "$INT9X/run.sh" $os $2 keep | tail -1; sleep $bw; boot
  L=$(mark); up
  expect $L "$(st 43)" "after the cold restart: bring-up done (0x43), new accounting"
  verify T1.BIN 1024 5; show $L
done
"$INT9X/g.sh" $os s $tag-end
verdict
