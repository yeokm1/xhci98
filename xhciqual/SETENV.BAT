@echo off
rem SETENV.BAT - Open Watcom environment for the DOS qualification tool.
rem Run once per shell:  xhciqual\SETENV.BAT   (install dir: C:\WATCOM)
rem Values per the Open Watcom installer's requested environment changes.

set WATCOM=C:\WATCOM
set PATH=%WATCOM%\BINNT64;%WATCOM%\BINNT;%PATH%
set INCLUDE=%WATCOM%\H;%WATCOM%\H\NT;%WATCOM%\H\NT\DIRECTX;%WATCOM%\H\NT\DDK;%INCLUDE%
set EDPATH=%WATCOM%\EDDAT
set WHTMLHELP=%WATCOM%\BINNT\HELP
set WIPFC=%WATCOM%\WIPFC

echo xhci98: Watcom environment set (WATCOM=%WATCOM%).
