' XHNORST.VBS [seconds] - answers Windows 2000's "System Settings Change"
' question (restart now?) with No, waiting up to the given seconds (default
' 60) for it. The release matrix's playback check runs it once the audio
' device is in (LEGNT.CMD, :norst): while the question is open Windows'
' install is still pending and XHCIINST's uninstall stops with exit 4.
' A key is sent only right after AppActivate brought the box forward, and
' the answer counts only once the box is gone; until then it tries again.
' Exit 0 when it answered, 1 when the question did not come, 2 when it
' stayed open.
Dim sh, i, t, seen
Set sh = CreateObject("WScript.Shell")
t = 60
If WScript.Arguments.Count > 0 Then t = CInt(WScript.Arguments(0))
seen = False
For i = 1 To t
    If sh.AppActivate("System Settings Change") Then
        seen = True
        WScript.Sleep 300
        If sh.AppActivate("System Settings Change") Then sh.SendKeys "n"
        WScript.Sleep 2000
        If Not sh.AppActivate("System Settings Change") Then
            WScript.Echo "XHNORST answered No after " & i & " s"
            WScript.Quit 0
        End If
        WScript.Echo "XHNORST the question is still open after a No at " & i & " s"
    End If
    WScript.Sleep 1000
Next
If seen Then
    WScript.Echo "XHNORST the question stayed open for " & t & " s"
    WScript.Quit 2
End If
WScript.Echo "XHNORST no restart question in " & t & " s"
WScript.Quit 1
