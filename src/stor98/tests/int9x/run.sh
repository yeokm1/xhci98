#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# int9x runs (local VM folder, lib.sh). Usage: run.sh <98|me> <tag> [new|keep|base] [extra QEMU args]
#   new : <os>/sys-<tag>.qcow2 = fresh overlay of <os>/base.qcow2 (a copy of the Q-9x prep overlay,
#         backed read-only by the VM folder's base\win98-prep or base\winme-prep)
#   NVMe: <os>/nvme-<tag>.qcow2 on the VM folder's base\nvme-g9-base.qcow2 (NVME_KEEP=<tag> reuses another run's)
# QMP 48800 (98) / 48810 (Me); VNC +1. QEMU from the VM folder's bin\.
# Logs: <os>/debugcon-<tag>.log, trace-<tag>.log (LOGTAG=<name> names them instead).
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 2 ] || { echo "usage: run.sh <98|me> <tag> [new|keep|base] [extra QEMU args]"; exit 2; }
os="$1"; tag="$2"; mode="${3:-new}"
export PATH="$BIN:$PATH"
case "$os" in
98) V=$B/98; QMP=48800; X=$B/xfer98.img ;;
me) V=$B/me; QMP=48810; X=$B/xferme.img ;;
*) echo "os must be 98 or me"; exit 2 ;;
esac
need "transfer image" "$X" "STOR98_VMS (build it with mkxfer.sh)"
need "NVMe base image" "$BASE/nvme-g9-base.qcow2" STOR98_VMS
VNC=$((QMP + 1))
cd "$V"
S=sys-$tag.qcow2
if [ "$mode" = new ]; then rm -f "$S"; "$BIN/qemu-img.exe" create -q -f qcow2 -F qcow2 -b base.qcow2 "$S"; fi
[ "$mode" = base ] && S=base.qcow2
NV="nvme-${NVME_KEEP:-$tag}.qcow2"
[ -f "$NV" ] || "$BIN/qemu-img.exe" create -q -f qcow2 -F qcow2 -b "$(w "$BASE/nvme-g9-base.qcow2")" "$NV"
LT=${LOGTAG:-$tag}
args=(
  -name "stor98-int-$os" -machine pc -accel tcg -cpu pentium3,-apic -smp 1 -m 512
  -L "$(w "$SHARE")"
  -display sdl -vga none -device cirrus-vga -vnc 127.0.0.1:$((VNC-5900))
  -qmp tcp:127.0.0.1:$QMP,server=on,wait=off -pidfile "$(w "$V/qemu.pid")"
  -rtc base=localtime -nic none
  -drive "if=ide,index=0,format=qcow2,file=$(w "$V/$S")"
  -drive "if=ide,index=1,format=raw,file=$(w "$X")"
  -drive if=ide,index=2,media=cdrom
  -global PIIX4_PM.acpi-pci-hotplug-with-bridge-support=off
  -drive "if=none,id=nvme0,format=qcow2,file=$(w "$V/$NV")"
  -device "usb4-tunnel-bridge,id=tb0,bus=pci.0,addr=0x5,chassis_nr=1,shpc-autoenable=on,msi=off"
  -device "usb4-host,id=u4,bus=pci.0,addr=0x4,tunnel-port=tb0,msi=off,msix=off"
  -device usb4-device,id=enc0,bus=u4.0,port=1,drive=nvme0,serial=USB4EMU01
  -debugcon "file:$(w "$V/debugcon-$LT.log")" -device usb4-debug-port
  -trace 'usb4_tp*' -trace 'usb4_dbg*' -trace 'usb4_h_*'
  -trace 'pci_nvme_create*' -trace 'pci_nvme_del*' -trace 'pci_nvme_err*' -trace 'pci_nvme_flush*'
  -trace 'qemu_system_*_request'
  -D "$(w "$V/trace-$LT.log")"
)
args+=("${@:4}")
echo "qemu-system-x86_64.exe ${args[*]}" > "qemu-$LT.cmdline"
nohup "$BIN/qemu-system-x86_64.exe" "${args[@]}" > "qemu-$LT.log" 2>&1 < /dev/null &
disown
sleep 3; cat "qemu-$LT.log"; echo "started $os $tag ($mode) QMP $QMP VNC $VNC pid $(cat qemu.pid 2>/dev/null)"
