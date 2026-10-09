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
' systems; NNNN differs between machines, so every NNNN is probed and the
' keys whose DriverDesc is this driver's controller are the ones used.
' ASCII only, CRLF line endings.

Option Explicit

Const XHCI_DESC = "xHCI98 USB 3.x eXtensible Host Controller"
Const VALUE_NAME = "XhciIntelPortSwitch"
Const CLASS_9X = "HKLM\System\CurrentControlSet\Services\Class\USB\"
Const CLASS_NT = "HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\"
Const TITLE = "xhci98 port switchover"
' Every number up to PROBE_ALL is probed; past it, probing stops after
' PROBE_GAP numbers in a row with no key at all.
Const PROBE_ALL = 999
Const PROBE_GAP = 200

Dim sh, isNT, base, keys, args, action, elevated, onConsole, report, i, answer

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
elevated = False
For i = 0 To args.Count - 1
    If LCase(args(i)) = "-elevated" Then
        elevated = True
    Else
        action = LCase(args(i))
    End If
Next
If action = "/show" Or action = "-show" Then action = "show"
If action <> "" And action <> "show" And action <> "0" And action <> "1" Then
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
    Dim n, misses, name, desc, ok, found, drv
    found = ""
    misses = 0
    n = 0
    Do While n <= 9999 And (n <= PROBE_ALL Or misses < PROBE_GAP)
        name = Right("000" & CStr(n), 4)
        desc = ReadValue(base & name & "\DriverDesc", ok)
        If ok Then
            misses = 0
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
        Else
            misses = misses + 1
        End If
        n = n + 1
    Loop
    FindKeys = Split(Trim(found), " ")
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
