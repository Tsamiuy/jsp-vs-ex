# Types into the game window with PostMessage (no focus change, nothing reaches other windows).
# Usage: osk_type_post.ps1 [-Log <path to the game's stdout log>]  (default: stdout.txt in the current folder)
param([string]$Log = (Join-Path (Get-Location) "stdout.txt"))
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W { [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@
$log = $Log
for ($i = 0; $i -lt 900; $i++) { if ((Test-Path $log) -and (Select-String -Path $log -Pattern "LoadAsync" -Quiet)) { break }; Start-Sleep -Milliseconds 500 }
"loadasync seen at try $i"; Start-Sleep -Seconds 3
$h = (Get-Process JStarsRecomp | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle; "hwnd $h"
function key($vk) { [W]::PostMessageW($h, 0x100, [IntPtr]$vk, [IntPtr]1) | Out-Null; Start-Sleep -Milliseconds 60; [W]::PostMessageW($h, 0x101, [IntPtr]$vk, [IntPtr]0xC0000001) | Out-Null }
function ch($c) { [W]::PostMessageW($h, 0x102, [IntPtr][int][char]$c, [IntPtr]1) | Out-Null }
for ($k = 0; $k -lt 4; $k++) { key 0x08; Start-Sleep -Milliseconds 120 }
foreach ($c in "PC Port".ToCharArray()) { ch $c; Start-Sleep -Milliseconds 150 }
"typed at $(Get-Date -Format HH:mm:ss)"; Start-Sleep -Seconds 7
key 0x27; "right at $(Get-Date -Format HH:mm:ss)"; Start-Sleep -Seconds 7
key 0x0D; "enter (cancel) at $(Get-Date -Format HH:mm:ss)"
