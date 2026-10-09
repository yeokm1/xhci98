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

' What the driver makes of the value: only a DWORD is read, and since
' 2.3.0.0 a missing one is off.
Function Describe(name)
    Dim v, ok, t
    v = ReadValue(base & name & "\" & VALUE_NAME, ok)
    If Not ok Then
        Describe = "not set - off (a missing value is read as 0)"
        Exit Function
    End If
    t = VarType(v)
    If t <> vbInteger And t <> vbLong And t <> vbDouble Then
        Describe = "not a DWORD - off (the driver reads only a DWORD)"
    ElseIf v = 0 Then
        Describe = "0 - off"
    ElseIf v = 2 Then
        Describe = "2 - on, on any Intel controller, at your own risk"
    Else
        Describe = CStr(v) & " - on, on the listed Intel chipsets"
    End If
End Function

Function ShowText()
    Dim s, k
    s = VALUE_NAME & " on each xhci98 controller:" & vbCrLf
    For Each k In keys
        s = s & "  " & base & k & vbCrLf & "      " & Describe(k) & vbCrLf
    Next
    ShowText = s
End Function

Function WriteValue(name, v)
    On Error Resume Next
    Err.Clear
    sh.RegWrite base & name & "\" & VALUE_NAME, v, "REG_DWORD"
    WriteValue = Err.Number
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

' Exit code 3: the elevated copy was started, and this one does not know
' its result.
Sub SetAll(v)
    Dim k, rc, written
    written = 0
    For Each k In keys
        rc = WriteValue(k, v)
        If rc <> 0 Then
            If written = 0 And IsNT6() And Not elevated Then
                CreateObject("Shell.Application").ShellExecute "wscript.exe", _
                    """" & WScript.ScriptFullName & """ " & CStr(v) & " -elevated", _
                    "", "runas", 1
                Say "Windows is asking for administrator rights. Nothing was"
                Say "changed by this window; the result is shown in the window"
                Say "that opens next."
                Finish 3
            End If
            Say "Could not write " & VALUE_NAME & " to"
            Say "  " & base & k
            Say "(error " & CStr(rc) & "). Run this as an administrator."
            If written > 0 Then
                Say "The " & CStr(written) & " key(s) listed before it were set."
            End If
            Say ""
            Say ShowText()
            Finish 1
        End If
        written = written + 1
    Next
    Say ShowText()
    Say VALUE_NAME & " is now " & CStr(v) & " on every xhci98 controller."
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
