#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# rm9x2.sh <98|me> <tag> [boot-wait]: removal-transaction tests for the int-0.2 review fixes.
#  F shutdown episode then remove (test option ShnPrime), each option set before the load that
#    reads it: F2 SHST complete (outcome 1), then F1 SHST timed out (outcome 2, no SHST credit).
#  G shared-line ISR during and after the drain (IsrProbe), a read of BIG.BIN held at fetch, 4 KiB children.
#  H budget-forced disable (read held, Flush held, SHST 15 s, RDY clears 1.5 s after CC.EN=0): outcome 2, not FAILED.
#  I identity loss mid-transaction (cm9x Overlap 3, SHST 1.5 s): no RemoveAck; the CM isolates first.
#  J BUSY without limit (cm9x SkipQuery, transaction held ~6 s, writes issued meanwhile).
#  K overlap during an open transaction (cm9x Overlap 1, SHST 1.5 s): first request acknowledged, overlap logged.
# Expected values: docs/INTEGRATION-0.2.md, "Removal transaction" and "Requalification". SECTIONS="F G ..."
# selects sections (rm9x4.sh: F G H, rm9x5.sh: F); the verdict table follows, exit status = failures.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: $(basename "$0") <98|me> <tag> [boot-wait]"; exit 2; }
cd "$B"
os=$1; tag=$2; bw=${3:-110}; SECTIONS="${SECTIONS:-F G H I J K}"
LOG=$os/debugcon-$tag.log
show() { since $1 | grep -a "^n98 [0-9a-f]* 12[0-9a-f] \|^n98 [0-9a-f]* 13[01] \|^n98 [0-9a-f]* \(43\|81\|83\|d1\|cf\|67\|60\|61\|a3\|a5\|a6\|c8\) \|^cm9x [0-9a-f]* 1c[bcde] \|^cm9x [0-9a-f]* 1b6 000000b[0-9a-c] \|^ctl" | grep -av "^n98 [0-9a-f]* 12[2378] " | cut -c1-150; }
count() { since $1 | grep -acE "$2"; }
# the transaction steps: P9R_* in the low byte of 0x12B (2 drain, 3 Flush, 4 SHN, 5 disable)
step() { st 12b "[0-9a-f]{6}0$1"; }
NOSHST='[0-9a-f][0-389ab][0-9a-f]{4}'      # RemoveAck detail without the SHST bit (0x04)
"$INT9X/run.sh" $os $tag new | tail -1; sleep $bw
"$INT9X/g.sh" $os dos; t "D:\INT NVINTD" 10
grep -a "cm9x DriverEntry\|ports active" $LOG | cut -c1-120 | head -3
sec "0 bring-up, write/verify, BIG.BIN for the held reads of G and H"
L=$(mark); up; show $L
expect $L "$(st 43)" "bring-up done (0x43)"
write T1.BIN 2048 5; verify T1.BIN 2048 5
write BIG.BIN 16384 1 600

if want F; then
sec "F2 shutdown episode (SHST complete) then remove: outcome 1 (ShnPrime 2)"
tset q9 ShnPrime 2
L=$(mark); eject 10; back; show $L                # a new load that reads ShnPrime 2
tset q9 ShnPrime 1                                # read by the load F1 brings back
L=$(mark); write T4.BIN 256 4; eject 40; keys F2; show $L
expect $L "$(st 131 '000102[0-9a-f]{2}')" "shutdown episode primed, SHST complete (0x131 ShnPrime 2)"
expect $L "$(rmack 1)" "RemoveAck CLEAN (0x12C outcome 1)"
expect $L "$(ejected C1EA)" "eject result C1EA"
sec "F1 shutdown episode (SHST timed out) then remove: outcome 2 (ShnPrime 1)"
L=$(mark); back; show $L
L=$(mark); verify T4.BIN 256 4; write T3.BIN 256 3; eject 40; keys F1; show $L
expect $L "$(st 131 '000101[0-9a-f]{2}')" "shutdown episode primed, SHST timed out (0x131 ShnPrime 1)"
expect $L "$(st 12c "${NOSHST}02")" "RemoveAck ISOLATED_NOT_CLEAN without SHST credit (0x12C outcome 2)"
expect $L "$(ejected E202)" "eject result E202 (D2 recovery)"
tset q9 ShnPrime 0
L=$(mark); back; verify T1.BIN 2048 5; verify T3.BIN 256 3; show $L
fi
if want G; then sec "G shared-line ISR (IsrProbe) during and after the drain; a read held at fetch; 4 KiB children"
tset q9 IsrProbe 1; tset q9 Frag 4
L=$(mark); eject 10; back; show $L                # a new load with the options
expect $L "$(st 131 '0000040[23]')" "test options read: IsrProbe, Frag 4 KiB (0x131)"
arm "at=stage code=0x1cf act=surprise;at=fetch op=read nth=6 act=hold"
tset cm SkipQuery 1
L=$(mark); EJ=$L; t "start /m command /c copy /b F:\\BIG.BIN NUL" 8; t "start /m C:\\CTL eject - 0" 2
sleep 30; keys G; waitfor "$EJECT_RESULT" $L 300; sleep 10; keys G2; show $L
# awk has no \b: a stage argument ends at a non-hex character or the end of the line.
n=$(since $L | awk '/^n98 [0-9a-f]+ 12b [0-9a-f]+0[345]([^0-9a-f]|$)/{f=1; seen=1} /^n98 [0-9a-f]+ 12c /{f=0} f && /^n98 [0-9a-f]+ b6 /{c++} END {print (seen ? c + 0 : -1)}')
if [ "$n" -lt 0 ]; then
    fail "drain end (0x12B step 3-5) never logged: child submits after it not judged"
elif [ "$n" -le 1 ]; then
    pass "child submits after the drain step up to the RemoveAck: $n (the Flush at most)"
else
    fail "child submits after the drain step up to the RemoveAck: $n (the Flush at most)"
fi
echo "   child submits during the drain: $(since $L | awk '/^n98 [0-9a-f]+ 12b [0-9a-f]+02([^0-9a-f]|$)/{f=1} /^n98 [0-9a-f]+ 12b [0-9a-f]+0[345]([^0-9a-f]|$)/{f=0} f && /^n98 [0-9a-f]+ b6 /{c++} END {print c + 0}')"
echo "   probe calls (step:result): $(since $L | grep -a '^n98 [0-9a-f]* 12f ' | awk '{print substr($4,5,2)":"substr($4,8,1)}' | sort | uniq -c | tr '\n' ' ')"
n=$(since $L | grep -aE "$(st 12f)" | awk '{s = substr($4, 5, 2); r = substr($4, 7, 2); if (s >= "04" && r != "00") n++} END {print n + 0}')
[ "$n" -eq 0 ] && pass "the probe ISR claimed nothing from CC.SHN on (0x12F step >= 4)" \
               || fail "the probe ISR claimed $n interrupts from CC.SHN on (0x12F step >= 4)"
expect $L "$(st 12f)" "the probe ISR ran during the transaction (0x12F)"
expect $L "$(rmack 2)" "RemoveAck ISOLATED_NOT_CLEAN (0x12C outcome 2: the drain timed out on the held read)"
tset cm SkipQuery 0; tset q9 IsrProbe 0; tset q9 Frag 0
L=$(mark); back; verify T1.BIN 2048 5; show $L
fi
if want H; then sec "H budget-forced disable: outcome 2 with the budget bit, not FAILED"
tset cm SkipQuery 1
arm "at=stage code=0x1cf act=surprise;at=fetch op=read nth=6 act=hold;at=fetch op=flush act=hold"
qom x-shst-delay-ms 15000; qom x-rdy-clear-delay-ms 1500
L=$(mark); EJ=$L; t "start /m command /c copy /b F:\\BIG.BIN NUL" 8; t "start /m C:\\CTL eject - 0" 2
sleep 30; keys H; waitfor "$EJECT_RESULT" $L 300; sleep 10; keys H2; show $L
expect $L "$(rmack 2)" "RemoveAck ISOLATED_NOT_CLEAN (0x12C outcome 2), not FAILED"
expect_no $L "$(rmack 3)" "no FAILED RemoveAck"
if since $L | grep -aqE "$(st 12c '[13579bdf][0-9a-f]{5}02')"; then pass "the budget bit (0x10) in the RemoveAck detail"
else inconclusive "no budget bit: the drain timed out on the held read first (as in the 2026-10-16 runs), so the budget path was not reached; rmacct2.sh S covers it"; fi
qom x-shst-delay-ms 0; qom x-rdy-clear-delay-ms 0; tset cm SkipQuery 0
L=$(mark); back; verify T1.BIN 2048 5; show $L
fi
if want I; then sec "I identity loss mid-transaction (cm9x Overlap 3): no RemoveAck, the CM isolates first"
tset cm Overlap 3; qom x-shst-delay-ms 1500
L=$(mark); eject 40; keys I; show $L
expect $L "$(st 126 00000020)" "identity lost: the entry's slot changed (0x126 reason 0x20)"
expect $L "$(st 12c '[0-9a-f]{6}[89a-f][0-9a-f]')" "the outcome decided but not published (0x12C bit 7)"
expect $L "$(ejected E3FF)" "eject result E3FF (no acknowledgement: the CM isolates first)"
qom x-shst-delay-ms 0; tset cm Overlap 0
L=$(mark); back; verify T1.BIN 2048 5; show $L
fi
if want J; then sec "J BUSY without limit: writes during a ~6 s transaction (SkipQuery)"
tset cm SkipQuery 1
arm "at=stage code=0x1cf act=surprise;at=fetch op=flush act=hold"
qom x-shst-delay-ms 1900; qom x-rdy-clear-delay-ms 1500
L=$(mark); EJ=$L; t "start /m C:\\CTL eject - 0" 1; t "start /m C:\\CTL write F:\\T9.BIN 64 9" 1; t "start /m C:\\CTL write F:\\T8.BIN 64 8" 1
waitfor "$EJECT_RESULT" $L 300; sleep 25; keys J; show $L | grep -av " 130 "
echo "   BUSY per SRB: $(since $L | grep -a '^n98 [0-9a-f]* 130 ' | awk '{print substr($4,1,4)}' | sort | uniq -c | sort -rn | head -3 | tr '\n' ' ')"
expect_no $L "$(st c8)" "no BUSY-limit error (0xC8): the transaction answers BUSY without a limit"
n=$(count $L "$(st 130)")
[ "$n" -gt 0 ] && pass "removal BUSY completions: $n" \
               || inconclusive "no request reached the driver during the transaction (0 BUSY, as in the final 2026-10-16 round)"
qom x-shst-delay-ms 0; qom x-rdy-clear-delay-ms 0; tset cm SkipQuery 0
L=$(mark); back; verify T1.BIN 2048 5; show $L
fi
if want K; then sec "K overlap during an open transaction (Overlap 1, SHST 1.5 s)"
tset cm Overlap 1; qom x-shst-delay-ms 1500
L=$(mark); eject 30; keys K; show $L
expect $L "$(st 12e)" "overlapping request logged and ignored (0x12E)"
expect $L "$(rmack 1)" "the first request acknowledged CLEAN (0x12C outcome 1)"
expect $L "$(ejected C1EA)" "eject result C1EA"
qom x-shst-delay-ms 0; tset cm Overlap 0
L=$(mark); back; verify T1.BIN 2048 5; show $L
fi
"$INT9X/g.sh" $os s $tag-end
decoder $os/trace-$tag.log --ejects
verdict
