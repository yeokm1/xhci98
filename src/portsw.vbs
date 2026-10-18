' portsw.vbs - show or set XhciIntelPortSwitch, the switch of the Intel
' 7-, 8- and 9-series port switchover, on every xhci98 controller this
' machine has (roadmap-hcd task 36.42; design record 16 section 5).
'
'   Double-click it:                   it shows the value and asks.
'   cscript //nologo portsw.vbs show   shows the value.
'   cscript //nologo portsw.vbs 1      on: the driver moves the switchable
'                                      connectors of a listed Intel chipset
'                                      from the USB 2.0 controller to itself.
'   cscript //nologo portsw.vbs 0      off, the default: they stay where
'                                      the firmware put them.
'
' From 3.1.0.0 the value is set machine-wide, in
' HKLM\System\CurrentControlSet\Services\USB, which every xhci98 controller
' reads first: a value there wins over the controller's own, which the
' install writes and this script leaves as it is (roadmap-usb4 task
' 42-A.17). The script shows both and says which one is in force. To give
' one controller a different value, delete the machine-wide value in
' regedit and set that controller's own.
'
' The driver reads the value only when its controller starts, so a change
' takes effect at the next RESTART of Windows. Setting 1 or 0 needs an
' administrator; on Vista and 7 it asks Windows for that itself.
'
' Value 2 (the switchover on any Intel controller, at your own risk) is
' shown but never set here: it is set by hand, after reading the release
' notes' warning.
'
' Windows Script Host and VBScript, because every target has them with
' nothing to install: Windows 98 SE and ME (which have no reg.exe), 2000
' (none by default), XP, XP x64, Vista and 7. The controller's driver key is
' Class\USB\NNNN on 98 and ME and Class\{36FC9E60-...}\NNNN on the NT
' systems; NNNN differs between machines, so every NNNN subkey is read and
' the keys whose DriverDesc is this driver's controller are the ones used.
' Script Host cannot list subkeys itself: WMI lists them on the NT systems,
' and on 98 and ME (where WMI is not part of a stock install) regedit /e
' exports the class key and its section headers are read. If neither
' answers, every number from 0000 to 9999 is probed, which is slow.
' ASCII only, CRLF line endings.

Option Explicit

Const XHCI_DESC = "xHCI98 USB 3.x eXtensible Host Controller"
Const VALUE_NAME = "XhciIntelPortSwitch"
Const CLASS_9X = "HKLM\System\CurrentControlSet\Services\Class\USB\"
Const CLASS_NT = "HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\"
Const MACHINE_KEY = "HKLM\System\CurrentControlSet\Services\USB\"
Const TITLE = "xhci98 port switchover"
Const PROBE_LAST = 9999
Const HKEY_LOCAL_MACHINE = &H80000002

Dim sh, isNT, base, keys, args, action, elevated, onConsole, report, i, answer
Dim actions, a, listed

Set sh = CreateObject("WScript.Shell")
onConsole = (LCase(Right(WScript.FullName, 11)) = "cscript.exe")
report = ""

isNT = (UCase(sh.Environment("PROCESS")("OS")) = "WINDOWS_NT")
If isNT Then
    base = CLASS_NT
Else
    base = CLASS_9X
End If

Set args = WScript.Arguments
action = ""
actions = 0
elevated = False
For i = 0 To args.Count - 1
    a = LCase(args(i))
    If a = "-elevated" Then
        elevated = True
    Else
        If a = "/show" Or a = "-show" Then a = "show"
        actions = actions + 1
        action = a
        If a <> "show" And a <> "0" And a <> "1" Then Usage
    End If
Next
If actions > 1 Then
    action = "more than one of show, 1 and 0"
    Usage
End If

keys = FindKeys()
If UBound(keys) < 0 Then
    Say "No xhci98 controller was found in the registry, so nothing was"
    Say "read or changed. Install the driver first."
    Finish 1
End If

If action = "" Then
    answer = InputBox(ShowText() & vbCrLf & _
        "Type 1 to turn the switchover on, or 0 to turn it off." & vbCrLf & _
        "Leave it empty, or Cancel, to change nothing.", TITLE)
    answer = Trim(answer)
    If answer = "" Then
        Say ShowText()
        Say "Nothing was changed."
        Finish 0
    End If
    action = answer
End If

Select Case action
    Case "show"
        Say ShowText()
        Finish 0
    Case "0", "1"
        SetAll CLng(action)
    Case Else
        Usage
End Select

' --------------------------------------------------------------------------

Function ReadValue(path, ok)
    On Error Resume Next
    Err.Clear
    ReadValue = sh.RegRead(path)
    ok = (Err.Number = 0)
    Err.Clear
End Function

' Every NNNN under the USB class key whose DriverDesc is this driver's
' controller (the root hub and hubs have their own descriptions); on 98 and
' ME its NTMPDriver must be xhci98.sys too.
Function FindKeys()
    Dim names, name, desc, ok, found, drv
    found = ""
    listed = False
    If isNT Then
        names = SubKeysWmi()
    Else
        names = SubKeysExport()
    End If
    If Not listed Then names = SubKeysProbe()
    For Each name In Split(Trim(names), " ")
        desc = ReadValue(base & name & "\DriverDesc", ok)
        If ok Then
            If VarType(desc) = vbString Then
                If LCase(desc) = LCase(XHCI_DESC) Then
                    If isNT Then
                        found = found & name & " "
                    Else
                        drv = ReadValue(base & name & "\NTMPDriver", ok)
                        If ok Then
                            If VarType(drv) = vbString Then
                                If LCase(drv) = "xhci98.sys" Then
                                    found = found & name & " "
                                End If
                            End If
                        End If
                    End If
                End If
            End If
        End If
    Next
    FindKeys = Split(Trim(found), " ")
End Function

' A class instance key's name: four decimal digits.
Function IsInstance(k)
    Dim j
    IsInstance = False
    If Len(k) <> 4 Then Exit Function
    For j = 1 To 4
        If InStr("0123456789", Mid(k, j, 1)) = 0 Then Exit Function
    Next
    IsInstance = True
End Function

' The NT systems: WMI's registry provider lists the subkeys.
Function SubKeysWmi()
    Dim reg, subs, rc, k, s
    s = ""
    On Error Resume Next
    Err.Clear
    Set reg = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\default:StdRegProv")
    If Err.Number = 0 Then
        rc = reg.EnumKey(HKEY_LOCAL_MACHINE, Mid(base, 6, Len(base) - 6), subs)
        If Err.Number = 0 And rc = 0 Then
            listed = True
            If IsArray(subs) Then
                For Each k In subs
                    If IsInstance(k) Then s = s & k & " "
                Next
            End If
        End If
    End If
    Err.Clear
    SubKeysWmi = s
End Function

' 98 and ME: regedit exports the class key to a temporary file, and its
' section headers name the subkeys.
Function SubKeysExport()
    Dim fso, tmp, f, line, prefix, rest, s, q
    s = ""
    q = Chr(34)
    On Error Resume Next
    Err.Clear
    Set fso = CreateObject("Scripting.FileSystemObject")
    tmp = fso.BuildPath(fso.GetSpecialFolder(2), fso.GetTempName())
    sh.Run "regedit /e " & q & tmp & q & " " & q & "HKEY_LOCAL_MACHINE" & _
        Mid(base, 5, Len(base) - 5) & q, 0, True
    If Err.Number = 0 And fso.FileExists(tmp) Then
        prefix = "[HKEY_LOCAL_MACHINE" & UCase(Mid(base, 5))
        Set f = fso.OpenTextFile(tmp, 1)
        If Err.Number = 0 Then
            listed = True
            Do While Not f.AtEndOfStream
                line = UCase(Trim(f.ReadLine()))
                If Left(line, Len(prefix)) = prefix And Right(line, 1) = "]" Then
                    rest = Mid(line, Len(prefix) + 1, Len(line) - Len(prefix) - 1)
                    If IsInstance(rest) Then s = s & rest & " "
                End If
            Loop
            f.Close
        End If
        fso.DeleteFile tmp
    End If
    Err.Clear
    SubKeysExport = s
End Function

' The last resort: every number from 0000 to 9999, whether or not the
' numbers before it exist.
Function SubKeysProbe()
    Dim n, name, ok, s, v
    s = ""
    For n = 0 To PROBE_LAST
        name = Right("000" & CStr(n), 4)
        v = ReadValue(base & name & "\DriverDesc", ok)
        If ok Then s = s & name & " "
    Next
    SubKeysProbe = s
End Function

' 0 when the value is not there (RegRead's "file not found" or "path not
' found"), 1 when it was read into v, 2 when it is there in a type
' RegRead cannot read (a REG_QWORD, for one): the driver then reads
' "another type" and its default stands.
Function ReadState(path, v)
    Dim n
    On Error Resume Next
    Err.Clear
    v = sh.RegRead(path)
    n = Err.Number
    Err.Clear
    If n = 0 Then
        ReadState = 1
    ElseIf n = &H80070002 Or n = &H80070003 Then
        ReadState = 0
    Else
        ReadState = 2
    End If
End Function

' Whether the machine-wide value is there, in any type: then it is the one
' every controller uses.
Function MachineSet()
    Dim v
    MachineSet = (ReadState(MACHINE_KEY & VALUE_NAME, v) <> 0)
End Function

' What the driver makes of one value: only a DWORD is read, and since
' 2.3.0.0 a missing one is off.
Function DescribeAt(path)
    Dim v, st, t
    st = ReadState(path, v)
    If st = 0 Then
        DescribeAt = "not set - off (a missing value is read as 0)"
        Exit Function
    End If
    If st = 2 Then
        DescribeAt = "set, in a type this script cannot read - off (the driver reads only a DWORD)"
        Exit Function
    End If
    t = VarType(v)
    If t <> vbInteger And t <> vbLong And t <> vbDouble Then
        DescribeAt = "not a DWORD - off (the driver reads only a DWORD)"
    ElseIf v = 0 Then
        DescribeAt = "0 - off"
    ElseIf v = 2 Then
        DescribeAt = "2 - on, on any Intel controller, at your own risk"
    Else
        DescribeAt = CStr(v) & " - on, on the listed Intel chipsets"
    End If
End Function

Function Describe(name)
    Dim v
    If MachineSet() Then
        If ReadState(base & name & "\" & VALUE_NAME, v) <> 0 Then
            Describe = "its own value: " & DescribeAt(base & name & "\" & VALUE_NAME) & _
                vbCrLf & "      NOT IN FORCE: the machine-wide value is used"
        Else
            Describe = "its own value: not set; the machine-wide value is used"
        End If
    Else
        Describe = DescribeAt(base & name & "\" & VALUE_NAME) & _
            vbCrLf & "      in force: no machine-wide value is set"
    End If
End Function

Function ShowText()
    Dim s, k
    s = VALUE_NAME & " machine-wide, which every xhci98 controller reads first:" & vbCrLf
    s = s & "  " & MACHINE_KEY & vbCrLf & "      "
    If MachineSet() Then
        s = s & DescribeAt(MACHINE_KEY & VALUE_NAME) & " - IN FORCE on every controller" & vbCrLf
    Else
        s = s & "not set - each controller's own value below is used" & vbCrLf
    End If
    s = s & VALUE_NAME & " on each xhci98 controller:" & vbCrLf
    For Each k In keys
        s = s & "  " & base & k & vbCrLf & "      " & Describe(k) & vbCrLf
    Next
    ShowText = s
End Function

' The machine-wide value; RegWrite creates the USB key where it is absent.
Function WriteMachine(v)
    On Error Resume Next
    Err.Clear
    sh.RegWrite MACHINE_KEY & VALUE_NAME, v, "REG_DWORD"
    WriteMachine = Err.Number
    Err.Clear
End Function

' NT 6 (Vista and 7) under User Account Control: a write from a process
' without the administrator token is refused, so the script asks for it
' and runs again, once.
Function IsNT6()
    Dim ver, ok
    IsNT6 = False
    If Not isNT Then Exit Function
    ver = ReadValue("HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\CurrentVersion", ok)
    If ok Then
        If VarType(ver) = vbString Then
            If InStr(ver, ".") > 1 Then
                IsNT6 = (CInt(Left(ver, InStr(ver, ".") - 1)) >= 6)
            End If
        End If
    End If
End Function

' Writes the machine-wide value only, and reads it back; the controllers'
' own values are left as they are. Exit code 3: the elevated copy was
' started, and this one does not know its result.
Sub SetAll(v)
    Dim rc, back, ok, t
    rc = WriteMachine(v)
    If rc <> 0 Then
        If IsNT6() And Not elevated Then
            CreateObject("Shell.Application").ShellExecute "wscript.exe", _
                """" & WScript.ScriptFullName & """ " & CStr(v) & " -elevated", _
                "", "runas", 1
            Say "Windows is asking for administrator rights. Nothing was"
            Say "changed by this window; the result is shown in the window"
            Say "that opens next."
            Finish 3
        End If
        Say "Could not write " & VALUE_NAME & " to"
        Say "  " & MACHINE_KEY
        Say "(error " & CStr(rc) & "). Run this as an administrator."
        Say "Nothing was changed."
        Say ""
        Say ShowText()
        Finish 1
    End If
    back = ReadValue(MACHINE_KEY & VALUE_NAME, ok)
    If ok Then
        t = VarType(back)
        If t = vbInteger Or t = vbLong Then
            ok = (back = v)
        Else
            ok = False
        End If
    End If
    If Not ok Then
        Say VALUE_NAME & " was written to"
        Say "  " & MACHINE_KEY
        Say "and did not read back as " & CStr(v) & "."
        Say ""
        Say ShowText()
        Finish 1
    End If
    Say ShowText()
    Say VALUE_NAME & " is now " & CStr(v) & " machine-wide, for every xhci98"
    Say "controller; their own values were left as they are."
    Say ""
    Say "RESTART WINDOWS for this to take effect: the driver reads the"
    Say "value only when its controller starts."
    Finish 0
End Sub

Sub Usage()
    Say "Unknown choice: " & action
    Say ""
    Say "Use show, 1 (on) or 0 (off), for example:"
    Say "  cscript //nologo portsw.vbs show"
    Finish 2
End Sub

Sub Say(s)
    report = report & s & vbCrLf
End Sub

Sub Finish(code)
    If onConsole Then
        WScript.Echo report
    Else
        MsgBox report, vbInformation, TITLE
    End If
    WScript.Quit code
End Sub
