#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# g.sh <98|me> verb ...: t 'text' | k chord | tun up|down|eject|surprise | qom prop [value] | s name | quit | dos | off | restart
#   The tunnel bridge is tb0 (TB=tb1 for the second one). QMP 48800 (98 SE), 48810 (Me).
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
os="$1"; shift
case "$os" in 98|me) P=$(qmp_port "$os") ;; *) echo "usage: g.sh <98|me> verb ..."; exit 2 ;; esac
TBP=/machine/peripheral/${TB:-tb0}
case "$1" in
t) typek --port $P "$2" ;;
k) typek --port $P key "$2" ;;
tun) qmp --port $P cmd qom-set "{\"path\":\"$TBP\",\"property\":\"tunnel\",\"value\":\"$2\"}" ;;
qom) if [ -n "${3+x}" ]; then qmp --port $P cmd qom-set "{\"path\":\"$TBP\",\"property\":\"$2\",\"value\":$( [[ "$3" =~ ^-?[0-9]+$ ]] && echo "$3" || echo "\"$3\"" )}";
     else qmp --port $P cmd qom-get "{\"path\":\"$TBP\",\"property\":\"$2\"}"; fi ;;
s) qmp --port $P screendump "$B/$os/${2:-s}.png" >/dev/null && echo "shot $os/${2:-s}" ;;
quit) qmp --port $P cmd quit ;;
dos) typek --port $P key ctrl-esc; sleep 1; typek --port $P "r"; sleep 2; typek --port $P "command
"; sleep 4 ;;
off) typek --port $P key ctrl-esc; sleep 1; typek --port $P "u"; sleep 3; if [ $os = me ]; then typek --port $P key home; else typek --port $P key alt-s; fi; sleep 1; typek --port $P key ret ;;
restart) typek --port $P key ctrl-esc; sleep 1; typek --port $P "u"; sleep 3; if [ $os = me ]; then typek --port $P key home; typek --port $P key down; else typek --port $P key alt-r; fi; sleep 1; typek --port $P key ret ;;
*) echo "g.sh: unknown verb $1"; exit 2 ;;
esac
