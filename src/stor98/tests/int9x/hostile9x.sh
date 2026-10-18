#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# hostile9x.sh <98|me> <tag> [boot-wait]: the interface 3 rendezvous refusals (R5a-R5d, R8a)
# against cm9x's Hostile modes 1-12 (tests/cm9x/cm9x.c, ApplyHostile). cm9x applies a mode at
# its next UP, before it re-enumerates the bridge, so each mode is one surprise removal and
# one re-arrival; stor98's find then logs 0x122 with the refusal code (src/pool9x.c,
# P9Rendezvous). Expected codes, in P9Rendezvous's order of checks:
#   4, 5   locator too short, of the wrong type          rc 2 (R5a)
#   6, 7   the proof's cookie and the locator's differ   rc 11 (R5b)
#   1, 2, 3, 11, 12  any table address other than the proof's TableLinear (unaligned, not
#          present, private arena, provider or locator moved): rc 14, the proof being the
#          address authority (R3a), checked before range, alignment and presence (R5c)
#   8      table header Count wrong                      rc 6 (R5d)
#   9      a second entry for the same function          rc 9 (R8a, no channel)
#   10     accepted at the find (rc 0); 15 s after UP cm9x writes UP and DOWN_QUIESCED
#          together: the instance stops acting on the entry (0x126 reason 3, R5e)
# Every refused load still starts, without the link-state channel. The verdict table follows,
# exit status = failures.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: hostile9x.sh <98|me> <tag> [boot-wait]"; exit 2; }
cd "$B"
os=$1; tag=$2; bw=${3:-110}; MODES="${MODES:-1 2 3 4 5 6 7 8 9 10 11 12}"
LOG=$os/debugcon-$tag.log
rc_of() { case $1 in 1|2|3|11|12) echo 14 ;; 4|5) echo 2 ;; 6|7) echo 11 ;; 8) echo 6 ;; 9) echo 9 ;; 10) echo 0 ;; esac; }
show() { since $1 | grep -a "^n98 [0-9a-f]* 12[0-6] \|^n98 [0-9a-f]* 43 \|^cm9x [0-9a-f]* 1b6 0000a\|^cm9x [0-9a-f]* 1c[abc] \|^ctl" | cut -c1-150; }
# cycle: surprise removal, the CM's removal of the devnode, then re-arrival (a new load)
cycle() {
  local L; L=$(mark); "$INT9X/g.sh" $os tun surprise >/dev/null
  waitfor "$(cm 1b6 000000c0)" $L 180; sleep 5; keys "$1"; sleep 5
  up
}
"$INT9X/run.sh" $os $tag new | tail -1; sleep $bw
"$INT9X/g.sh" $os dos; t "D:\INT NVINTD" 10
arm
sec "0 bring-up without a hostile mode"
L=$(mark); up
expect $L "$(st 43)" "bring-up done (0x43)"
expect $L "$(st 122 '[0-9a-f]{4}0100')" "rendezvous accepted and bound (0x122 rc 0)"
write T1.BIN 1024 5
for m in $MODES; do
  rc=$(rc_of $m)
  sec "H$m cm9x Hostile $m: expected 0x122 rc $rc"
  tset cm Hostile $m
  L=$(mark); cycle H$m; show $L
  expect $L "$(cm 1b6 "0000a0$(printf %02x $m)")" "cm9x applied hostile mode $m (0x1B6 0xA0$(printf %02X $m))"
  if [ "$m" = 10 ]; then
    expect $L "$(st 122 '[0-9a-f]{4}0100')" "accepted and bound at the find (0x122 rc 0)"
    expect_wait $L "$(st 126 00000003)" 60 "illegal flags (UP with DOWN_QUIESCED): identity lost, reason 3 (0x126)"
  else
    expect $L "$(st 122 "[0-9a-f]{6}$(printf %02x $rc)")" "rendezvous refused with rc $rc (0x122)"
    expect_no $L "$(st 122 '[0-9a-f]{4}0100')" "no entry bound"
  fi
  expect $L "$(st 43)" "the load starts without the channel (0x43)"
done
sec "Z hostile mode off: accepted again"
tset cm Hostile 0
L=$(mark); cycle Z; show $L
expect $L "$(cm 1b6 000000a0)" "cm9x restored the rendezvous (0x1B6 0xA0)"
expect $L "$(st 122 '[0-9a-f]{4}0100')" "rendezvous accepted and bound (0x122 rc 0)"
verify T1.BIN 1024 5
"$INT9X/g.sh" $os s $tag-end
verdict
