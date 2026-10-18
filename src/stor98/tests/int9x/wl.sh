#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# wl.sh <os> <tag> <regex> [timeout s] [from-line]: wait until debugcon-<tag>.log matches regex after from-line
#   (bounded by wall-clock time; exit 0 when matched, 1 on timeout)
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 3 ] || { echo "usage: wl.sh <os> <tag> <regex> [timeout s] [from-line]"; exit 2; }
f=$B/$1/debugcon-$2.log; re="$3"; to=${4:-60}; from=${5:-0}
end=$((SECONDS+to)); while [ $SECONDS -lt $end ]; do
  if tail -n +$((from+1)) "$f" 2>/dev/null | grep -aqE "$re"; then tail -n +$((from+1)) "$f" | grep -aE "$re" | tail -3; exit 0; fi
  sleep 2
done
echo "TIMEOUT waiting for $re"; exit 1
