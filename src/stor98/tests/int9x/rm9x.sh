#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# rm9x.sh <98|me> <tag> [boot-wait]: removal-transaction tests with cm9x (interface 3 R9-R11).
#  A clean eject after a write: outcome 1 (C1EA), blocks RELEASED; re-arrival verifies the data.
#  B veto (file held open): E117, no request; the disk keeps working.
#  C retry (cm9x Overlap 2: the same request re-published after the ack) and overlap (Overlap 1).
#  D FAILED (RDY never clears): outcome 3, the CM isolates first; re-arrival verifies.
#  E CM timeout (AckTimeoutMs 300, SHST delayed): the CM isolates first; re-arrival verifies.
# Expected values: docs/INTEGRATION-0.2.md, "Removal tests" and "Requalification". SECTIONS="A B ..."
# selects sections; the verdict table follows, exit status = failures.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: rm9x.sh <98|me> <tag> [boot-wait]"; exit 2; }
cd "$B"
os=$1; tag=$2; bw=${3:-110}; SECTIONS="${SECTIONS:-A B C D E}"
LOG=$os/debugcon-$tag.log
show() { since $1 | grep -a "^n98 [0-9a-f]* 12[0-9a-e] \|^n98 [0-9a-f]* \(43\|81\|83\|d1\|cf\|67\) \|^cm9x [0-9a-f]* 1c[bcde] \|^cm9x [0-9a-f]* 1b6 000000b[0-9] \|^ctl" | grep -av "^n98 [0-9a-f]* 12[2378] " | cut -c1-150; }
# the a and b fields of the first cm9x line with this code and argument after the mark
cmab() { since $1 | grep -aE "$(cm $2 $3)" | head -1 | awk '{print $(NF-1), $NF}'; }
ACKED_LINK="$(st 125 '[0-9a-f]{3}[13579bdf][0-9a-f]{4}')"
"$INT9X/run.sh" $os $tag new | tail -1; sleep $bw
"$INT9X/g.sh" $os dos; t "D:\INT NVINTD" 10
grep -a "cm9x DriverEntry\|ports active" $LOG | cut -c1-120 | head -3
sec "0 bring-up, write/verify"
L=$(mark); up; show $L
expect $L "$(st 43)" "bring-up done (0x43)"
write T1.BIN 2048 5; verify T1.BIN 2048 5

if want A; then sec "A clean eject after a write"
write T2.BIN 1024 7
L=$(mark); eject; keys A; show $L
expect $L "$(st 12a)" "request seen (0x12A)"
expect $L "$(st 12c '0f[0-9a-f]{4}01')" "RemoveAck CLEAN with drain, Flush, SHST and RDY=0 (0x12C 0F..01)"
expect $L "$(ejected C1EA)" "eject result C1EA"
L=$(mark); up; verify T1.BIN 2048 5; verify T2.BIN 1024 7; show $L
fi
if want B; then sec "B veto: a file held open"
L=$(mark); t "start /m C:\\CTL hold F:\\HOLD.TXT 25" 5; eject 5; sleep 30
expect $L "$(ejected E117)" "eject result E117 (Query_Remove_SubTree vetoed)"
expect_no $L "$(st 12a)" "no request published, none seen (0x12A absent)"
verify T1.BIN 2048 5; show $L
fi
if want C; then sec "C1 retry: the same request re-published after the acknowledgement (Overlap 2)"
tset cm Overlap 2
L=$(mark); eject; keys C1; show $L
expect $L "$(ejected C1EA)" "eject result C1EA"
n=$(since $L | grep -acE "$(st 12c)"); [ "$n" -eq 1 ] && pass "RemoveAck written once (R9c)" || fail "RemoveAck written $n times"
ab=$(cmab $L 1b6 000000b3)
if [ -z "$ab" ]; then fail "cm9x retry check (0x1B6 0xB3) missing"
elif [ "${ab% *}" = "${ab#* }" ]; then pass "RemoveAck unchanged by the retry ($ab)"
else fail "RemoveAck changed by the retry ($ab)"; fi
tset cm Overlap 0
L=$(mark); up; verify T1.BIN 2048 5; show $L
sec "C2 overlap: a second request 300 ms after the first (Overlap 1)"
tset cm Overlap 1
L=$(mark); eject; keys C2; show $L
expect $L "$(ejected C1EA)" "eject result C1EA"
first_seq=$(cmab $L 1b6 000000b1 | awk '{print $1}')
ab=$(cmab $L 1b6 000000b2)
if [ -z "$ab" ] || [ -z "$first_seq" ]; then fail "cm9x overlap check (0x1B6 0xB1, 0xB2) missing"
elif [ $(( 0x${ab% *} >> 8 )) -eq $(( 0x$first_seq )) ]; then pass "RemoveAck names the first request ($ab, first $first_seq)"
else fail "RemoveAck does not name the first request ($ab, first $first_seq)"; fi
tset cm Overlap 0
L=$(mark); up; verify T1.BIN 2048 5; show $L
fi
if want D; then sec "D FAILED: RDY never clears after CC.EN=0: outcome 3, the CM isolates first"
qom x-rdy-clear-delay-ms -1
L=$(mark); eject 40; keys D; show $L
expect $L "$(st 12c '07[0-9a-f]{4}03')" "RemoveAck FAILED at the disable cap (0x12C 07..03)"
expect $L "$(ejected E303)" "eject result E303 (the CM isolates first)"
expect_wait $L "$ACKED_LINK" 60 "DOWN_QUIESCED applied and acknowledged (0x125 written)"
qom x-rdy-clear-delay-ms 0
L=$(mark); up; verify T1.BIN 2048 5; show $L
fi
if want E; then sec "E CM timeout (AckTimeoutMs 300, SHST delayed 1.5 s): the CM isolates first"
tset cm AckTimeoutMs 300
qom x-shst-delay-ms 1500
L=$(mark); eject 40; keys E; show $L
expect $L "$(ejected E3FF)" "eject result E3FF (no acknowledgement within 300 ms)"
expect_wait $L "$ACKED_LINK" 60 "DOWN_QUIESCED applied and acknowledged (0x125 written)"
qom x-shst-delay-ms 0
tset cm AckTimeoutMs 0
L=$(mark); up; verify T1.BIN 2048 5; show $L
fi
"$INT9X/g.sh" $os s $tag-end
decoder $os/trace-$tag.log --ejects
verdict
