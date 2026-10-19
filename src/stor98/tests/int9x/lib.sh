#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# lib.sh: sourced by every int9x script. One VM root, the tools taken from it, the guest
# helpers and the verdict bookkeeping. Nothing here reads the usb4-emu checkout or its VM
# images (docs/TESTING.md, "Local test VMs").
#
#   STOR98_VMS       the VM folder (default: the repository's vms\ junction)
#   STOR98_QMP_PY    qmp.py         (default: <VM folder>/tools/qmp.py)
#   STOR98_TYPEKEYS  typekeys.py    (default: <VM folder>/tools/reenum-test/typekeys.py)
#   STOR98_QEMU_BIN  QEMU and DLLs  (default: <VM folder>/bin)
#   STOR98_PYTHON    a Python 3 interpreter (default: python3, else py -3)
#   STOR98_REPO      the checkout whose out\ the transfer images take (default: this one)
#
# Folders under the VM root: int\ (this harness: <os>\ overlays, logs and screenshots,
# the transfer images), bin\ (QEMU), share\ (firmware), base\ (the base images).

INT9X=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$INT9X/../.." && pwd)
VMS=${STOR98_VMS:-$REPO/vms}
B=$VMS/int
BIN=${STOR98_QEMU_BIN:-$VMS/bin}
SHARE=$VMS/share
BASE=$VMS/base
QMP_PY=${STOR98_QMP_PY:-$VMS/tools/qmp.py}
TYPEKEYS=${STOR98_TYPEKEYS:-$VMS/tools/reenum-test/typekeys.py}
export PYTHONDONTWRITEBYTECODE=1        # never write __pycache__ into the VM folder's tools

die() { echo "int9x: $*" >&2; exit 2; }
# need <what> <path> <variable that overrides it>
need() { [ -e "$2" ] || die "$1 not found at $2 (set $3, or see docs/TESTING.md \"Local test VMs\")"; }
need "VM folder" "$VMS" STOR98_VMS
need "harness folder" "$B" STOR98_VMS
need "qmp.py" "$QMP_PY" STOR98_QMP_PY
need "typekeys.py" "$TYPEKEYS" STOR98_TYPEKEYS
need "QEMU" "$BIN/qemu-system-x86_64.exe" STOR98_QEMU_BIN

if [ -n "${STOR98_PYTHON:-}" ]; then
  read -ra PY <<< "$STOR98_PYTHON"
elif python3 -c 'import sys; sys.exit(sys.version_info[0] < 3)' 2>/dev/null; then
  PY=(python3)
elif py -3 -c 'import sys' 2>/dev/null; then
  PY=(py -3)
else
  die "no Python 3 interpreter (python3 or py -3); set STOR98_PYTHON"
fi
"${PY[@]}" -c 'import sys; sys.exit(sys.version_info[0] < 3)' 2>/dev/null || die "${PY[*]} is not Python 3"

w() { cygpath -w "$1"; }
qmp() { "${PY[@]}" "$QMP_PY" "$@"; }
# typekeys.py imports qmp: it is found next to qmp.py
typek() { PYTHONPATH="$(w "$(dirname "$QMP_PY")")" "${PY[@]}" "$TYPEKEYS" "$@"; }
qmp_port() { case "$1" in 98) echo 48800 ;; me) echo 48810 ;; w2k) echo 48820 ;; *) return 1 ;; esac; }

# ---- guest helpers (the caller sets os, tag and LOG and runs in $B) ------------------
t() { "$INT9X/g.sh" "$os" t "$1\n" >/dev/null; sleep "${2:-4}"; }
mark() { wc -l < "$LOG"; }
since() { tail -n +$(( $1 + 1 )) "$LOG"; }
# waitfor <ERE> [from-line] [timeout s]: 0 when a line after from-line matches
waitfor() { "$INT9X/wl.sh" "$os" "$tag" "$1" "${3:-150}" "${2:-0}" >/dev/null || { echo "   (timeout waiting for: $1)"; return 1; }; }
want() { case " $SECTIONS " in *" $1 "*) return 0 ;; esac; return 1; }
arm() { "$INT9X/g.sh" "$os" qom x-faults "${1:-at=stage code=0x1cf act=surprise}" >/dev/null; }
qom() { "$INT9X/g.sh" "$os" qom "$1" "$2" >/dev/null; }
keys() { "$INT9X/g.sh" "$os" s "$tag-$1" >/dev/null; "$INT9X/g.sh" "$os" k ret >/dev/null; sleep 4; "$INT9X/g.sh" "$os" k ret >/dev/null; }
# A bring-up after the mark: done (0x43), failed (0x44), or a pool find that failed (0x120 rc 1-7)
BRINGUP='^n98 [0-9a-f]+ 43 |^n98 [0-9a-f]+ 44 |^n98 [0-9a-f]+ 120 0000..0[1-7]'
up() { local L; L=$(mark); arm; "$INT9X/g.sh" "$os" tun up >/dev/null; waitfor "$BRINGUP" "$L" 240; sleep 8; }
EJ=0
eject() { EJ=$(mark); t "start /m C:\\CTL eject - 0" 2; waitfor "^ctl eject result" "$EJ" 300; sleep "${1:-25}"; }
# back: the device again after the last eject (EJ, its mark). The tunnel is raised only after
# an eject that tore it down (C1EA, E3xx); an E2xx's D2 recovery re-enumerates by itself, and
# its bring-up may already be in the log, so the wait starts at the eject's mark.
back() {
  if since "$EJ" | grep -aqE "^ctl eject result (C1EA|E3)"; then arm; "$INT9X/g.sh" "$os" tun up >/dev/null; fi
  waitfor "$BRINGUP" "$EJ" 300; sleep 8
}

# ---- stage-line patterns ------------------------------------------------------------
# st <code> [arg ERE]: a stor98 debug-port line, "n98 <TSC> <code> <arg>" (codes above 0xFF
# have three digits; the argument is 8 hex digits)
st() { echo "^n98 [0-9a-f]+ $1 ${2:-[0-9a-f]{8\}}\\b"; }
# cm <code> [arg ERE]: a cm9x line
cm() { echo "^cm9x [0-9a-f]+ $1 ${2:-[0-9a-f]{8\}}\\b"; }
# RemoveAck (0x12C): detail(2) sequence(4) outcome(2); outcome 0x80 = decided, not published
rmack() { st 12c "[0-9a-f]{6}0$1"; }
EJECT_RESULT='^ctl eject result '
ejected() { echo "^ctl eject result $1 "; }
re_file() { local f=${1//./\\.}; echo "F:.$f"; }

# ---- verdicts -----------------------------------------------------------------------
FAILS=0; INCONCL=0; RESULTS=(); SEC=-; SECS=()
sec() { SEC=${1%% *}; SECS+=("$SEC"); echo; echo "=== $1"; }
record() { RESULTS+=("$1|$SEC|$2"); case $1 in FAIL) FAILS=$((FAILS + 1)) ;; INCONCLUSIVE) INCONCL=$((INCONCL + 1)) ;; esac; echo "   $1: $2"; }
pass() { record PASS "$1"; }
fail() { record FAIL "$1"; }
inconclusive() { record INCONCLUSIVE "$1"; }
first() { since "$1" | grep -aE "$2" | head -1 | cut -c1-110; }
# expect <from-line> <ERE> <what>: a debugcon line after the mark matches
expect() { if since "$1" | grep -aqE "$2"; then pass "$3"; else fail "$3 (no line /$2/)"; fi; }
# expect_no <from-line> <ERE> <what>: no line after the mark matches
expect_no() { if since "$1" | grep -aqE "$2"; then fail "$3 (seen: $(first "$1" "$2"))"; else pass "$3"; fi; }
# expect_wait <from-line> <ERE> <timeout s> <what>
expect_wait() { if waitfor "$2" "$1" "$3"; then pass "$4"; else fail "$4 (not within $3 s)"; fi; }
# expect_count <from-line> <ERE> <min> <what>: at least min matching lines
expect_count() { local n; n=$(since "$1" | grep -acE "$2"); if [ "$n" -ge "$3" ]; then pass "$4 ($n)"; else fail "$4 ($n, want >= $3)"; fi; }
# tset <q9|cm> <name> <value>: a cm9xctl registry switch, confirmed by its echo (a FAIL if not)
tset() { local L; L=$(mark); t "C:\\CTL set $1 $2 $3" 3; since "$L" | grep -aq "^ctl set $1 $2 = $3" || fail "set $1 $2 $3 not confirmed"; }
# ctlrun <timeout s> <result ERE> <ok ERE> <what> <cm9xctl arguments...>: run, wait for the
# result line, PASS when it matches the ok pattern
ctlrun() {
  local L to=$1 res=$2 ok=$3 what=$4; L=$(mark); shift 4
  t "C:\\CTL $*" 1
  if ! waitfor "$res" "$L" "$to"; then fail "$what (no result within $to s)"; return; fi
  if since "$L" | grep -aE "$res" | grep -aqE "$ok"; then pass "$what"; else fail "$what ($(first "$L" "$res"))"; fi
}
# write <file> <KiB> <seed> / verify <file> <KiB> <seed>: on F: in the guest
write() { ctlrun "${4:-240}" "^ctl write $(re_file "$1")" "written and committed" "write $1 ($2 KiB)" write "F:\\$1" "$2" "$3"; }
verify() { ctlrun "${4:-240}" "^ctl verify $(re_file "$1")" ": $2 KiB seed $3 OK" "verify $1" verify "F:\\$1" "$2" "$3"; }
# decoder <trace> [--ejects]: q9log.py's statement (and eject) verdicts; a FAIL when it
# reports FAIL or INCOMPLETE (with --ejects, also when no eject was found)
decoder() {
  local out rc
  echo; echo "=== decoder"
  SEC=decoder; SECS+=(decoder)
  out=$("${PY[@]}" "$REPO/tests/cm9x/q9log.py" "$@"); rc=$?
  echo "$out" | grep "^statement\|^summary\|^eject\|^ejects" | cut -c1-220
  case $rc in
    0) pass "q9log.py: no FAIL or INCOMPLETE statement or eject" ;;
    1) fail "q9log.py reports FAIL or INCOMPLETE${2:+ (or no eject found)}" ;;
    *) fail "q9log.py could not read $1" ;;
  esac
}
verdict() {
  local s v
  echo; echo "=== verdict: $(basename "$0") $os $tag"
  for s in "${SECS[@]}"; do
    v=PASS
    printf '%s\n' "${RESULTS[@]}" | grep -q "^INCONCLUSIVE|$s|" && v=INCONCLUSIVE
    printf '%s\n' "${RESULTS[@]}" | grep -q "^FAIL|$s|" && v=FAIL
    printf '%s\n' "${RESULTS[@]}" | grep -q "|$s|" || v="(no checks)"
    printf '  %-8s %s\n' "$s" "$v"
  done
  echo "  checks: $(printf '%s\n' "${RESULTS[@]}" | grep -c '^PASS|') passed, $FAILS failed, $INCONCL inconclusive"
  printf '%s\n' "${RESULTS[@]}" | grep '^FAIL|\|^INCONCLUSIVE|' | awk -F'|' '{printf "  %-12s %-8s %s\n", $1, $2, $3}'
  [ "$FAILS" -gt 125 ] && FAILS=125
  exit "$FAILS"
}
