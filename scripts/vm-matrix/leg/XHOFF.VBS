' XHOFF.VBS - shuts Windows 2000 down, forced, powered off where the
' machine can (Win32Shutdown 12): 2000 has no shutdown.exe. The release
' matrix's leg scripts and make-start.ps1 run it where shutdown.exe is
' missing (build-and-test.md, "Faster emulator legs").
Dim os
For Each os In GetObject("winmgmts:{impersonationLevel=impersonate,(Shutdown)}!\\.\root\cimv2").ExecQuery("select * from Win32_OperatingSystem where Primary=true")
    os.Win32Shutdown 12
Next
