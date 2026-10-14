#!/bin/sh
# run-crosscheck.sh - build xhci98's USB4 software connection manager core
# strictly as C89 and run it against usb4-emu's fabric model, as
# usb4-emu's tools/usb4cm/host/run-host-test.sh does for usb4cm
# (roadmap-usb4.md task 40-A.5; design record 22 section 14.1).
#
# usb4-emu is read in place and never modified; everything built goes
# under xhci98's out/u4-crosscheck/. Run from Git Bash through
# usb4-emu's MSYS2 UCRT64 shell:
#   <usb4-emu>/scripts/msys2-shell.sh "sh <xhci98>/scripts/usb4-crosscheck/run-crosscheck.sh"
# USB4EMU names the usb4-emu checkout (default ../usb4-emu beside the
# repository, or the main checkout's sibling for a worktree).
#
# Adapted from usb4-emu's run-host-test.sh (Copyright (c) 2026 usb4-emu
# contributors, SPDX-License-Identifier: GPL-2.0-only); the fabric file
# list follows src/qemu/tests/usb4/run-fabric-test.sh. Changed for xhci98
# by Yeo Kheng Meng and Claude, 2026-10-15. A reading, not a gate.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
emu="${USB4EMU:-$repo/../usb4-emu}"
[ -d "$emu/src/qemu/hw/usb4" ] || emu="$repo/../../usb4-emu"
qemu="$emu/src/qemu"
out="${OUT:-$repo/out/u4-crosscheck}"
mkdir -p "$out/trace"
[ -d "$qemu/hw/usb4" ] || { echo "usb4-emu not found at $emu" >&2; exit 2; }

echo "usb4-emu: $emu ($(git -C "$emu" rev-parse --short HEAD 2>/dev/null)," \
     "QEMU fork $(git -C "$qemu" rev-parse --short HEAD 2>/dev/null))"
echo "xhci98:   $repo ($(git -C "$repo" rev-parse --short HEAD 2>/dev/null))"

# The core must stay plain C89 (MSVC 6.0 and the DDK compile it too).
# MinGW-w64 is LLP64 like the amd64 DDK: unsigned long stays 32 bits;
# xhci_compat.h's _WIN64 branch names the pointer-sized type __int64.
c89="-std=c89 -pedantic -Wall -Wextra -Werror -Wdeclaration-after-statement \
     -Wno-unused-parameter -Wno-long-long -D__int64=__INT64_TYPE__ -DXHCI_HOST_TEST -O1"
for f in tbt_ring tbt_hw tbt_icm tbt_u4cm; do
    gcc $c89 -c "$repo/src/cm/$f.c" -o "$out/$f.o"
done
gcc $c89 -c "$here/u4cm-glue.c" -o "$out/u4cm-glue.o"

python3 "$qemu/tests/usb4/gen-trace-stub.py" "$qemu/hw/usb4/trace-events" \
    "$out/trace/trace-hw_usb4.h"
# Newer fabric models split the router's assembly helpers into their own
# file; linked when the checkout has it.
asm=""
if [ -f "$qemu/hw/usb4/usb4-router-asm.c" ]; then
    asm="$qemu/hw/usb4/usb4-router-asm.c"
fi
gcc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -Wno-sign-compare -Wno-missing-field-initializers \
    -I"$here" \
    -I"$qemu/tests/usb4/stub" -I"$out" -I"$qemu/include" -I"$qemu" \
    -I"$qemu/hw/usb4" -I"$repo/src" \
    $(pkg-config --cflags glib-2.0) \
    "$here/u4cm-crosscheck.c" \
    "$out/u4cm-glue.o" \
    "$out/tbt_ring.o" "$out/tbt_hw.o" "$out/tbt_icm.o" "$out/tbt_u4cm.o" \
    "$qemu/hw/usb4/usb4-ctl.c" "$qemu/hw/usb4/usb4-fabric.c" \
    "$qemu/hw/usb4/usb4-router.c" "$qemu/hw/usb4/usb4-router-tb3.c" \
    "$qemu/hw/usb4/usb4-router-adl.c" "$qemu/hw/usb4/usb4-preos.c" \
    "$qemu/hw/usb4/usb4-drom.c" \
    "$qemu/hw/usb4/usb4-tunnel.c" "$qemu/hw/usb4/usb4-fabric-usb3.c" \
    $asm \
    $(pkg-config --libs glib-2.0) -o "$out/u4cm-crosscheck"
"$out/u4cm-crosscheck"
