# wmake makefile for XHCIQUAL.EXE (Open Watcom, 32-bit flat, DOS/32A).
# Build with build.cmd, or run wmake yourself with the Watcom env set.
#
# "system dos32a" embeds the full DOS/32A extender as the EXE stub
# (op stub=dos32a.exe in wlink.lnk), so XHCIQUAL.EXE is one standalone
# file - no DOS4GW.EXE to carry. DOS/32A is DOS/4GW-compatible: same
# zero-based flat model and DPMI services the code relies on.
#
# -bt=dos  DOS target        -mf  flat model      -s  no stack checks
# (the hardware ISR is an explicit assembly entry in irqasm.asm)

CC = wcc386
ASM = wasm
CFLAGS = -bt=dos -mf -q -s -wx -ox -zq
AFLAGS = -zq

OBJS = main.obj pci.obj quirks.obj xhcicap.obj bringup.obj devid.obj &
       legacy.obj irq.obj irqasm.obj report.obj mmiodiag.obj route.obj &
       xhci_pirq.obj verdict.obj

# option map -> XHCIQUAL.MAP: keep it paired with each field-test binary so a
# reported DOS/32A fault EIP can be resolved to a symbol later (docs/contributing/lessons.md).
xhciqual.exe : $(OBJS)
	wlink system dos32a option quiet option map=xhciqual.map name $^@ file { $(OBJS) }

.c.obj :
	$(CC) $(CFLAGS) $[@

.asm.obj :
	$(ASM) $(AFLAGS) $[@

main.obj : main.c qual.h ../src/xhci_version.h
pci.obj : pci.c qual.h ../src/xhci_version.h
quirks.obj : quirks.c qual.h ../src/xhci_version.h
xhcicap.obj : xhcicap.c qual.h ../src/xhci_version.h
bringup.obj : bringup.c qual.h ../src/xhci_version.h
devid.obj : devid.c qual.h ../src/xhci_version.h
legacy.obj : legacy.c qual.h ../src/xhci_version.h
irq.obj : irq.c qual.h ../src/xhci_version.h
irqasm.obj : irqasm.asm
report.obj : report.c qual.h ../src/xhci_version.h
mmiodiag.obj : mmiodiag.c qual.h ../src/xhci_version.h
verdict.obj : verdict.c qual.h ../src/xhci_version.h
# Task 36.11: the routing report shares the driver's reviewed-pair table.
# Both are built as the host suites build the pure core, with no DDK;
# -za because the DOS target makes `interrupt` a keyword, which the core
# uses as a name.
route.obj : route.c qual.h ../src/xhci_version.h ../src/xhci_pirq.h ../src/xhci_compat.h
	$(CC) $(CFLAGS) -DXHCI_HOST_TEST route.c
xhci_pirq.obj : ../src/xhci_pirq.c ../src/xhci_pirq.h ../src/xhci_compat.h
	$(CC) $(CFLAGS) -za -DXHCI_HOST_TEST -fo=xhci_pirq.obj ../src/xhci_pirq.c

clean : .SYMBOLIC
	-rm -f *.obj *.err xhciqual.exe xhciqual.map
