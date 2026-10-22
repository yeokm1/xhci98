@echo off
rem XHSEND.BAT name file - a file on COM1 between XHLEG LOG lines.
if not exist %2 goto none
echo XHLEG LOG-BEGIN %1>COM1
type %2>COM1
echo.>COM1
echo XHLEG LOG-END %1>COM1
goto end
:none
echo XHLEG LOG-MISSING %1 %2>COM1
:end
