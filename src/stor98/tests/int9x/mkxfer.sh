#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# mkxfer.sh: xfer98.img / xferme.img (FAT16, D: in the guest) and xfer-x86\ (E: on 2000, runw2k.sh)
# from the build outputs of this checkout (STOR98_REPO for another). Run only while no VM uses them.
#   D:\NVINTD.MPD debug-port 9x image, D:\NVINT.MPD release 9x image, D:\CM9X.SYS, D:\CTL.EXE (tests\cm9x),
#   D:\INT.BAT    installs them (INT <name>), D:\POLL1.REG / D:\POLL0.REG set StorPollMode to 1 / 0
#   xfer-x86\     stor98d.sys (debug-port), stor98.sys, stor98.inf
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
S=${STOR98_REPO:-$REPO}
W=$S/out
for f in free-dbgport/win9x/stor98.mpd free/win9x/stor98.mpd cm9x/cm9x.sys cm9x/cm9xctl.exe \
         free-dbgport/x86/stor98.sys free/x86/stor98.sys; do
  need "build output" "$W/$f" "STOR98_REPO (scripts\\build.cmd, STOR98_DBGPORT=1 scripts\\build.cmd, tests\\cm9x\\build.cmd)"
done
X=$B/xfer; mkdir -p "$X"
cp "$W/free-dbgport/win9x/stor98.mpd" "$X/NVINTD.MPD"
cp "$W/free/win9x/stor98.mpd" "$X/NVINT.MPD"
cp "$W/cm9x/cm9x.sys" "$X/CM9X.SYS"
cp "$W/cm9x/cm9xctl.exe" "$X/CTL.EXE"
crlf() { sed 's/$/\r/'; }
crlf > "$X/INT.BAT" <<'EOB'
@echo off
rem INT NAME: install D:\NAME.MPD as the stor98 port driver image, and the int-0.2 cm9x stand-in
rem Guest images prepared before the rename load the port driver as NVME2K.MPD or
rem NVME98.MPD (their PortDriver value); the image is copied over those names too.
if "%1"=="" goto usage
copy D:\%1.MPD C:\WINDOWS\SYSTEM\IOSUBSYS\STOR98.MPD
if exist C:\WINDOWS\SYSTEM\IOSUBSYS\NVME2K.MPD copy D:\%1.MPD C:\WINDOWS\SYSTEM\IOSUBSYS\NVME2K.MPD
if exist C:\WINDOWS\SYSTEM\IOSUBSYS\NVME98.MPD copy D:\%1.MPD C:\WINDOWS\SYSTEM\IOSUBSYS\NVME98.MPD
copy D:\CM9X.SYS C:\WINDOWS\SYSTEM32\DRIVERS\CM9X.SYS
copy D:\CTL.EXE C:\CTL.EXE
echo int installed %1
goto end
:usage
echo usage: INT name
:end
EOB
# The polling mode switch (REG_SZ, read by every load): regedit /s D:\POLL1.REG, D:\POLL0.REG
for m in 1 0; do
  crlf > "$X/POLL$m.REG" <<EOR
REGEDIT4

[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\stor98]
"StorPollMode"="$m"

EOR
done
for img in xfer98.img xferme.img; do
  rm -f "$B/$img"
  "$BIN/qemu-img.exe" convert -f vvfat -O raw "fat:16:$(w "$X")" "$B/$img"
done
mkdir -p "$B/xfer-x86"
cp "$W/free-dbgport/x86/stor98.sys" "$B/xfer-x86/stor98d.sys"
cp "$W/free/x86/stor98.sys" "$B/xfer-x86/stor98.sys"
cp "$S/inf/stor98-x86.inf" "$B/xfer-x86/stor98.inf"
ls -la "$X" "$B"/*.img "$B/xfer-x86"
