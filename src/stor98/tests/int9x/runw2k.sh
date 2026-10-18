#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# runw2k.sh <tag>: Windows 2000 on an overlay of w2k/base.qcow2 (copy of the engine's w2k image) and
# w2k/nvme-<tag>.qcow2 on w2k/nvme-base.qcow2. QMP 48820, VNC 48821. xfer-x86 as E: (vvfat, snapshot;
# mkxfer.sh fills it).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
[ $# -ge 1 ] || { echo "usage: runw2k.sh <tag>"; exit 2; }
V=$B/w2k
export PATH="$BIN:$PATH"
tag=$1
need "Windows 2000 base overlay" "$V/base.qcow2" STOR98_VMS
need "Windows 2000 NVMe base overlay" "$V/nvme-base.qcow2" STOR98_VMS
need "transfer folder" "$B/xfer-x86" "STOR98_VMS (build it with mkxfer.sh)"
cd "$V"
rm -f "sys-$tag.qcow2"; "$BIN/qemu-img.exe" create -q -f qcow2 -F qcow2 -b base.qcow2 "sys-$tag.qcow2"
[ -f "nvme-$tag.qcow2" ] || "$BIN/qemu-img.exe" create -q -f qcow2 -F qcow2 -b nvme-base.qcow2 "nvme-$tag.qcow2"
nohup "$BIN/qemu-system-x86_64.exe" -L "$(w "$SHARE")" -pidfile "$(w "$V/qemu.pid")" \
  -name stor98-int-w2k -machine pc,acpi=off -accel tcg -cpu pentium3,-apic -m 256 \
  -display sdl -vga cirrus -vnc 127.0.0.1:$((48821-5900)) -qmp tcp:127.0.0.1:48820,server=on,wait=off \
  -rtc base=localtime -net none \
  -drive "if=ide,index=0,format=qcow2,file=$(w "$V/sys-$tag.qcow2")" \
  -drive "if=ide,index=1,format=raw,snapshot=on,file=fat:16:$(w "$B/xfer-x86")" \
  -global PIIX4_PM.acpi-pci-hotplug-with-bridge-support=off \
  -drive "if=none,id=nvme0,format=qcow2,file=$(w "$V/nvme-$tag.qcow2")" \
  -device "usb4-tunnel-bridge,id=tb0,bus=pci.0,addr=0x5,chassis_nr=1,shpc-autoenable=on,msi=off" \
  -device "usb4-host,id=u4,bus=pci.0,addr=0x4,tunnel-port=tb0,msi=off,msix=off" \
  -device usb4-device,id=enc0,bus=u4.0,port=1,drive=nvme0,serial=USB4EMU01 \
  -device usb4-debug-port -chardev "file,id=dcon,path=$(w "$V/dcon-$tag.log")" -device isa-debugcon,iobase=0xe9,chardev=dcon \
  -trace 'usb4_dbg_*' -trace 'usb4_tp_*' -trace 'pci_nvme_err*' -D "$(w "$V/trace-$tag.log")" \
  > "$V/qemu-$tag.out" 2>&1 < /dev/null &
disown; sleep 3; cat "$V/qemu-$tag.out"; echo "started w2k $tag pid $(cat "$V/qemu.pid")"
