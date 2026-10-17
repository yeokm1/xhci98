' silent.vbs - show or set XhciSilentInstall, the switch of the xhci98
' silent install, on every xhci98 controller this machine has, and show the
' SilentClasses list it gates (roadmap-hcd task 38.2; design record 13
' section 6.11.8). Windows 98 SE and ME only: the driver never reads either
' on Windows 2000 and later, and this script changes nothing there.
'
'   Double-click it:                   it shows the value and the list, and
'                                      asks.
'   cscript //nologo silent.vbs show   shows them.
'   cscript //nologo silent.vbs 1      on: first checks that the list reads
'                                      back as SILENT.REG's six, and only
'                                      then sets the switch to 1.
'   cscript //nologo silent.vbs 0      off, the default: sets it to 0 and
'                                      leaves the list.
'
' The driver reads the switch and the list only when its controller starts,
' so a change takes effect at the next RESTART of Windows. Only a DWORD of
' exactly 1 is on; missing, 0, another number or another type is off.
'
' Exit codes: 0 done; 1 refused or nothing found (no controller, or NT);
' 2 usage; 3 partial (set on some controllers and not others); 4 the list
' did not verify (1 only: nothing was written); 5 not verified (a write
' that returned no error read back wrong: that controller's value may have
' changed).
'
' Windows Script Host and VBScript, because 98 SE and ME have them and no
' reg.exe. The controller's driver key is Class\USB\NNNN; Script Host cannot
' list subkeys or values itself, so regedit /e exports the class key (its
' section headers name the subkeys) and the SilentClasses key (its value
' lines are the list). If the class export answers nothing, every number
' from 0000 to 9999 is probed, which is slow. ASCII only, CRLF line endings.

Option Explicit

Const XHCI_DESC = "xHCI98 USB 3.x eXtensible Host Controller"
Const VALUE_NAME = "XhciSilentInstall"
Const CLASS_9X = "HKLM\System\CurrentControlSet\Services\Class\USB\"
Const LIST_KEY = "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB\SilentClasses"
Const TITLE = "xhci98 silent install"
Const PROBE_LAST = 9999
Const SILENT_MAX = 32

Dim sh, fso, isNT, base, keys, args, action, onConsole, report, i, answer
Dim actions, a, listed
Dim sixNames, sixBytes
Dim lsState, lsNames, lsTypes, lsData, lsCount

sixNames = Array("0", "1", "2", "3", "4", "5")
sixBytes = Array("80,09,00,00", "43,03,01,01", "43,03,01,02", _
                 "c3,08,06,50", "83,03,01,01", "83,03,01,02")

Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
onConsole = (LCase(Right(WScript.FullName, 11)) = "cscript.exe")
report = ""
base = CLASS_9X

isNT = (UCase(sh.Environment("PROCESS")("OS")) = "WINDOWS_NT")

Set args = WScript.Arguments
action = ""
actions = 0
For i = 0 To args.Count - 1
    a = LCase(args(i))
    If a = "/show" Or a = "-show" Then a = "show"
    actions = actions + 1
    action = a
    If a <> "show" And a <> "0" And a <> "1" Then Usage
Next
If actions > 1 Then
    action = "more than one of show, 1 and 0"
    Usage
End If

If isNT Then
    Say "The silent install is a Windows 98 SE and ME feature: the driver"
    Say "never reads " & VALUE_NAME & " or the SilentClasses list on Windows"
    Say "2000 and later. Nothing was read or changed."
    Finish 1
End If

keys = FindKeys()
If UBound(keys) < 0 Then
    Say "No xhci98 controller was found in the registry, so nothing was"
    Say "read or changed. Install the driver first."
    Finish 1
End If

If action = "" Then
    answer = InputBox(ShowText() & vbCrLf & _
        "Type 1 to turn the silent install on, or 0 to turn it off." & vbCrLf & _
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
    Case "1"
        CheckList
        SetAll 1
    Case "0"
        SetAll 0
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
' controller and whose NTMPDriver is xhci98.sys.
Function FindKeys()
    Dim names, name, desc, ok, found, drv
    found = ""
    listed = False
    names = SubKeysExport()
    If Not listed Then names = SubKeysProbe()
    For Each name In Split(Trim(names), " ")
        desc = ReadValue(base & name & "\DriverDesc", ok)
        If ok Then
            If VarType(desc) = vbString Then
                If LCase(desc) = LCase(XHCI_DESC) Then
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

' Where the exports go: %TEMP%, or the script's own directory when there is
' none.
Function TempFile()
    Dim t
    t = sh.ExpandEnvironmentStrings("%TEMP%")
    If t = "" Or t = "%TEMP%" Or Not fso.FolderExists(t) Then
        t = fso.GetParentFolderName(WScript.ScriptFullName)
    End If
    TempFile = fso.BuildPath(t, fso.GetTempName())
End Function

Function Export(key, tmp)
    Dim q
    q = Chr(34)
    On Error Resume Next
    Err.Clear
    sh.Run "regedit /e " & q & tmp & q & " " & q & key & q, 0, True
    Export = (Err.Number = 0)
    Err.Clear
End Function

' regedit exports the class key, and its section headers name the subkeys.
Function SubKeysExport()
    Dim tmp, f, line, prefix, rest, s
    s = ""
    tmp = TempFile()
    On Error Resume Next
    Err.Clear
    If Export("HKEY_LOCAL_MACHINE" & Mid(base, 5, Len(base) - 5), tmp) Then
        If fso.FileExists(tmp) Then
            prefix = "[HKEY_LOCAL_MACHINE" & UCase(Mid(base, 5))
            Set f = fso.OpenTextFile(tmp, 1, False, -2)
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
    End If
    Err.Clear
    SubKeysExport = s
End Function

' The last resort: every number from 0000 to 9999.
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

' --------------------------------------------------------------------------
' The list: a regedit /e export of SilentClasses, read for its value lines.
' lsState: "ok" (the key was read), or what went wrong. lsNames, lsTypes
' and lsData: each value's name, its type as the export writes it (hex,
' dword, string, hex(N), or what else) and its data, hex bytes lower case
' without spaces.

Sub ReadList()
    Dim tmp, f, line, inKey, header, pending, ok
    lsCount = 0
    ReDim lsNames(0)
    ReDim lsTypes(0)
    ReDim lsData(0)
    tmp = TempFile()
    If Not Export(LIST_KEY, tmp) Then
        lsState = "regedit /e of the SilentClasses key failed"
        Exit Sub
    End If
    If Not fso.FileExists(tmp) Then
        lsState = "regedit /e of the SilentClasses key left no file: the key is missing"
        Exit Sub
    End If
    On Error Resume Next
    Err.Clear
    Set f = fso.OpenTextFile(tmp, 1, False, -2)
    If Err.Number <> 0 Then
        Err.Clear
        lsState = "the export of the SilentClasses key could not be read"
        fso.DeleteFile tmp
        Exit Sub
    End If
    header = "[" & UCase(LIST_KEY) & "]"
    inKey = False
    ok = False
    pending = ""
    Do While Not f.AtEndOfStream
        line = Trim(f.ReadLine())
        If Err.Number <> 0 Then Exit Do
        If pending <> "" Then
            line = pending & line
            pending = ""
        End If
        If Right(line, 1) = "\" And Left(line, 1) <> "[" Then
            pending = Left(line, Len(line) - 1)
        ElseIf Left(line, 1) = "[" Then
            inKey = (UCase(line) = header)
            If inKey Then ok = True
        ElseIf inKey And line <> "" Then
            AddLine line
        End If
    Loop
    If Err.Number <> 0 Then
        Err.Clear
        f.Close
        fso.DeleteFile tmp
        Err.Clear
        lsState = "the export of the SilentClasses key could not be read"
        Exit Sub
    End If
    If pending <> "" And inKey Then AddLine pending
    f.Close
    fso.DeleteFile tmp
    Err.Clear
    On Error GoTo 0
    If ok Then
        lsState = "ok"
    Else
        lsState = "the SilentClasses key is missing"
    End If
End Sub

' One value line: "name"=data, or @=data for the default value.
Sub AddLine(line)
    Dim name, rest, j, c, eq, data, t
    If Left(line, 1) = "@" Then
        name = "(default)"
        rest = Mid(line, 2)
    ElseIf Left(line, 1) = Chr(34) Then
        name = ""
        j = 2
        Do While j <= Len(line)
            c = Mid(line, j, 1)
            If c = "\" And j < Len(line) Then
                name = name & Mid(line, j + 1, 1)
                j = j + 2
            ElseIf c = Chr(34) Then
                Exit Do
            Else
                name = name & c
                j = j + 1
            End If
        Loop
        rest = Mid(line, j + 1)
    Else
        Exit Sub
    End If
    eq = InStr(rest, "=")
    If eq = 0 Then Exit Sub
    data = Trim(Mid(rest, eq + 1))
    If LCase(Left(data, 4)) = "hex:" Then
        t = "hex"
        data = LCase(Replace(Mid(data, 5), " ", ""))
    ElseIf LCase(Left(data, 6)) = "dword:" Then
        t = "dword"
        data = LCase(Mid(data, 7))
    ElseIf LCase(Left(data, 4)) = "hex(" Then
        eq = InStr(data, ":")
        If eq = 0 Then eq = Len(data)
        t = LCase(Left(data, eq - 1))
        data = LCase(Replace(Mid(data, eq + 1), " ", ""))
    ElseIf Left(data, 1) = Chr(34) Then
        t = "string"
    Else
        t = "unknown"
    End If
    ReDim Preserve lsNames(lsCount)
    ReDim Preserve lsTypes(lsCount)
    ReDim Preserve lsData(lsCount)
    lsNames(lsCount) = name
    lsTypes(lsCount) = t
    lsData(lsCount) = data
    lsCount = lsCount + 1
End Sub

' The index of a value by name, or -1.
Function FindValue(name)
    Dim j
    FindValue = -1
    For j = 0 To lsCount - 1
        If lsNames(j) = name Then
            FindValue = j
            Exit Function
        End If
    Next
End Function

Function IsSix(name)
    Dim j
    IsSix = False
    For j = 0 To 5
        If sixNames(j) = name Then IsSix = True
    Next
End Function

' What the driver's admission makes of a value (design record 13 section
' 6.11.1): a 4-byte binary whose control byte has 80 or 40 is kept.
Function DriverKeeps(j)
    Dim b
    DriverKeeps = False
    If lsTypes(j) <> "hex" And lsTypes(j) <> "hex(3)" Then Exit Function
    b = Split(lsData(j), ",")
    If UBound(b) <> 3 Then Exit Function
    If Len(b(0)) < 1 Or Len(b(0)) > 2 Then Exit Function
    On Error Resume Next
    Err.Clear
    DriverKeeps = ((CLng("&H" & b(0)) And &HC0) <> 0)
    If Err.Number <> 0 Then DriverKeeps = False
    Err.Clear
End Function

Function ValueText(j)
    ValueText = Chr(34) & lsNames(j) & Chr(34) & " " & lsTypes(j) & ":" & lsData(j)
End Function

' Whether the six of SILENT.REG are there with their type and bytes; ""
' when they all are, else what was found.
Function SixProblems()
    Dim j, k, s
    s = ""
    For k = 0 To 5
        j = FindValue(sixNames(k))
        If j < 0 Then
            s = s & "  value " & Chr(34) & sixNames(k) & Chr(34) & " is missing" & vbCrLf
        ElseIf lsTypes(j) <> "hex" Then
            s = s & "  value " & ValueText(j) & " is not a binary (hex:) value" & vbCrLf
        ElseIf lsData(j) <> sixBytes(k) Then
            s = s & "  value " & ValueText(j) & " should be hex:" & sixBytes(k) & vbCrLf
        End If
    Next
    SixProblems = s
End Function

' Every value beyond the six, with what the driver makes of it, and the
' count it would keep toward its cap.
Function OthersText()
    Dim j, s, kept, others
    s = ""
    kept = 0
    others = 0
    For j = 0 To lsCount - 1
        If DriverKeeps(j) Then kept = kept + 1
        If Not IsSix(lsNames(j)) Then
            others = others + 1
            If DriverKeeps(j) Then
                s = s & "  " & ValueText(j) & " - the driver keeps it" & vbCrLf
            Else
                s = s & "  " & ValueText(j) & " - the driver ignores it" & vbCrLf
            End If
        End If
    Next
    If others > 0 Then
        s = "Other values in the list (the driver reads these too; a kept one" & _
            vbCrLf & "silences its class as well):" & vbCrLf & s
    End If
    s = s & "The driver would keep " & CStr(kept) & " of its cap of " & _
        CStr(SILENT_MAX) & " entries."
    If kept > SILENT_MAX Then
        s = s & " The ones past " & CStr(SILENT_MAX) & " are dropped, in an" & _
            " order Windows chooses."
    End If
    OthersText = s & vbCrLf
End Function

Function ListText()
    Dim s, p
    ReadList
    s = "The list, " & LIST_KEY & ":" & vbCrLf
    If lsState <> "ok" Then
        ListText = s & "  " & lsState & " - nothing is silent." & vbCrLf
        Exit Function
    End If
    p = SixProblems()
    If p = "" Then
        s = s & "  the six values of SILENT.REG are there, as SILENT.REG has them." & vbCrLf
    Else
        s = s & "  not as SILENT.REG has it:" & vbCrLf & p
    End If
    ListText = s & OthersText()
End Function

' --------------------------------------------------------------------------

' What the driver makes of the switch: only a DWORD of exactly 1 is on.
Function Describe(name)
    Dim v, ok, t
    v = ReadValue(base & name & "\" & VALUE_NAME, ok)
    If Not ok Then
        Describe = "not set - off"
        Exit Function
    End If
    t = VarType(v)
    If t <> vbInteger And t <> vbLong Then
        Describe = "not a DWORD - off (the driver reads only a DWORD of 1)"
    ElseIf v = 1 Then
        Describe = "1 - on"
    ElseIf v = 0 Then
        Describe = "0 - off"
    Else
        Describe = CStr(v) & " - off (only 1 turns it on)"
    End If
End Function

Function ShowText()
    Dim s, k
    s = VALUE_NAME & " on each xhci98 controller:" & vbCrLf
    For Each k In keys
        s = s & "  " & base & k & vbCrLf & "      " & Describe(k) & vbCrLf
    Next
    ShowText = s & ListText()
End Function

' The check before any gate is written: the list as it is now, after the
' merge, must hold the six of SILENT.REG. Values beyond them are reported,
' not refused.
Sub CheckList()
    Dim p
    ReadList
    If lsState <> "ok" Then
        p = "  " & lsState & vbCrLf
    Else
        p = SixProblems()
    End If
    If p <> "" Then
        Say "The list did not read back as SILENT.REG's six values:"
        Say p
        Say "The switch was not set on any controller."
        Say ""
        Say ShowText()
        Finish 4
    End If
    Say "The list reads back as SILENT.REG's six values."
    Say OthersText()
End Sub

Function WriteValue(name, v)
    On Error Resume Next
    Err.Clear
    sh.RegWrite base & name & "\" & VALUE_NAME, v, "REG_DWORD"
    WriteValue = Err.Number
    Err.Clear
End Function

' Each controller ends set (written and read back), not set (the write
' failed: the value is as it was) or unverified (written without error and
' then read back wrong or not at all: it may or may not have changed).
Sub SetAll(v)
    Dim k, rc, back, ok, nSet, nNot, nUnv, total, t
    nSet = 0
    nNot = 0
    nUnv = 0
    total = 0
    For Each k In keys
        total = total + 1
        rc = WriteValue(k, v)
        If rc <> 0 Then
            nNot = nNot + 1
            Say "not set:    " & base & k & " (error " & CStr(rc) & ")"
        Else
            back = ReadValue(base & k & "\" & VALUE_NAME, ok)
            If ok Then
                t = VarType(back)
                If (t = vbInteger Or t = vbLong) Then
                    ok = (back = v)
                Else
                    ok = False
                End If
            End If
            If ok Then
                nSet = nSet + 1
                Say "set:        " & base & k
            Else
                nUnv = nUnv + 1
                Say "unverified: " & base & k & " (written, then read back wrong or not at all)"
            End If
        End If
    Next
    Say ""
    If nUnv > 0 Then
        Say "UNVERIFIED: the switch may have changed on " & CStr(nUnv) & " of " & _
            CStr(total) & " controllers."
        Say ""
        Say ShowText()
        Say "Run silent-off.bat to set every controller back to 0, or check"
        Say VALUE_NAME & " under each controller's key in regedit."
        Finish 5
    End If
    If nSet = total Then
        Say ShowText()
        Say VALUE_NAME & " is now " & CStr(v) & " on every xhci98 controller."
        Say ""
        Say "RESTART WINDOWS for this to take effect: the driver reads the"
        Say "switch and the list only when its controller starts."
        Finish 0
    End If
    If nSet = 0 Then
        Say "The switch was not changed."
        Say ""
        Say ShowText()
        Finish 1
    End If
    Say "PARTIAL: the switch was set on " & CStr(nSet) & " of " & CStr(total) & _
        " controllers."
    Say ""
    Say ShowText()
    Say "Run silent-off.bat to set every controller back to 0, or correct the"
    Say "failure and run the script again."
    Finish 3
End Sub

Sub Usage()
    Say "Unknown choice: " & action
    Say ""
    Say "Use show, 1 (on) or 0 (off), for example:"
    Say "  cscript //nologo silent.vbs show"
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
