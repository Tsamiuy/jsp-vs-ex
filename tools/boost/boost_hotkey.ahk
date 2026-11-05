; Optional. Needs AutoHotkey v2 (https://www.autohotkey.com). Double-click to run; Ctrl+Alt+B toggles boost.
; Fn cannot be used: it is handled by the laptop firmware and Windows never sees it.
#Requires AutoHotkey v2.0
^!b:: Run('powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' A_ScriptDir '\boost.ps1" toggle', , 'Hide')
