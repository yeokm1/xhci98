#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# prep.sh <98|me> [mpd] [boot-wait]: boot <os>/base.qcow2 itself, install D:\INT <mpd>, shut down.
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 1 ] || { echo "usage: prep.sh <98|me> [mpd] [boot-wait]"; exit 2; }
cd "$B"
os=$1; mpd=${2:-NVINTD}; bw=${3:-100}
"$INT9X/run.sh" $os prep-$mpd base | tail -1; sleep $bw
"$INT9X/g.sh" $os s prep-a
"$INT9X/g.sh" $os dos; "$INT9X/g.sh" $os t "D:\INT $mpd\n"; sleep 10
"$INT9X/g.sh" $os s prep-b
"$INT9X/g.sh" $os t "exit\n"; sleep 3
"$INT9X/g.sh" $os off; for i in $(seq 1 36); do [ -f $os/qemu.pid ] || break; sleep 5; done
[ -f $os/qemu.pid ] && { echo "$os: shutdown hung, quitting"; "$INT9X/g.sh" $os quit; sleep 3; rm -f $os/qemu.pid; } || echo "$os: clean shutdown"
