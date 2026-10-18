# wmake makefile for XHCIDI.EXE, the Windows 98 SE and ME device install
# helper (design record 27 section 3.2). Run build.cmd, which stages the
# DDK's headers and import library into obj\ and sets INCLUDE to obj\inc
# and Open Watcom's h, in that order.
#
# -bt=windows the Windows 3.x target   -ml  the large model: setupx.h's
# -zW Windows prologue and epilogue         structures hold far pointers
# -zu SS is not assumed to be DGROUP   -s   no stack checks
# -wx every warning, -we each an error -oas size, relaxed aliasing
#
# di_core.c is the pure part the host suite test\test_di.c also builds.

CC = wcc
CFLAGS = -bt=windows -ml -zW -zu -s -wx -we -zq -oas

OBJS = xhcidi.obj di_sys.obj di_core.obj

# option map: the linker's copyright lines, which NOTICE.TXT carries, and
# the runtime modules it bound (record 27 section 3.4).
xhcidi.exe : $(OBJS) obj\setupx.lib
	wlink system windows option quiet option map=xhcidi.map option stack=16k name $^@ file { $(OBJS) } library obj\setupx.lib

.c.obj :
	$(CC) $(CFLAGS) $[@

xhcidi.obj : xhcidi.c di_sys.h di_core.h ../../src/xhci_version.h obj\inc\setupx.h obj\inc\windows.h
di_sys.obj : di_sys.c di_sys.h di_core.h obj\inc\setupx.h obj\inc\windows.h
di_core.obj : di_core.c di_core.h

clean : .SYMBOLIC
	-rm -f *.obj *.err xhcidi.exe xhcidi.map
