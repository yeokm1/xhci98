@echo off
rem XHEXIT.BAT step - the exit code the last program left, said on COM1 as
rem "XHLEG EXIT step code" (COMMAND.COM has no %ERRORLEVEL%).
if errorlevel 6 goto e6
if errorlevel 5 goto e5
if errorlevel 4 goto e4
if errorlevel 3 goto e3
if errorlevel 2 goto e2
if errorlevel 1 goto e1
echo XHLEG EXIT %1 0 >COM1
goto end
:e1
echo XHLEG EXIT %1 1 >COM1
goto end
:e2
echo XHLEG EXIT %1 2 >COM1
goto end
:e3
echo XHLEG EXIT %1 3 >COM1
goto end
:e4
echo XHLEG EXIT %1 4 >COM1
goto end
:e5
echo XHLEG EXIT %1 5 >COM1
goto end
:e6
echo XHLEG EXIT %1 6-or-more >COM1
:end
