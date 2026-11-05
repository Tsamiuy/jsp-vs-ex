# Capture the JStarsRecomp window to PNGs at given seconds after it appears
# (PrintWindow with PW_RENDERFULLCONTENT, so it works when the window is covered).
# usage: snap_window.ps1 -At 60,80,100 [-Out run\shots]
param([string]$At = "60,90", [string]$Out = "$PSScriptRoot\run\shots")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
}
"@
New-Item -ItemType Directory -Force $Out | Out-Null
$t0 = $null
for ($i = 0; $i -lt 600 -and -not $t0; $i++) {
  $p = Get-Process JStarsRecomp -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($p) { $t0 = Get-Date } else { Start-Sleep -Milliseconds 500 }
}
if (-not $t0) { "no window"; exit 1 }
foreach ($s in ($At -split "[, ]+" | Where-Object { $_ } | ForEach-Object { [int]$_ })) {
  $wait = $s - ((Get-Date) - $t0).TotalSeconds
  if ($wait -gt 0) { Start-Sleep -Seconds $wait }
  $p = Get-Process JStarsRecomp -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { "process gone at $s s"; break }
  $h = $p.MainWindowHandle; $r = New-Object W32+RECT; [W32]::GetClientRect($h, [ref]$r) | Out-Null
  $w = [Math]::Max(1, $r.R - $r.L); $ht = [Math]::Max(1, $r.B - $r.T)
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [W32]::PrintWindow($h, $dc, 3) | Out-Null   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
  $g.ReleaseHdc($dc); $g.Dispose()
  $f = Join-Path $Out ("shot_{0:D3}s.png" -f $s); $bmp.Save($f); $bmp.Dispose(); "saved $f"
}
