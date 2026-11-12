# Take one screenshot of the running JStarsRecomp window right now (PrintWindow, works covered).
# usage: snap_now.ps1 -Out <png path>
param([string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W32n {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
}
"@
$p = Get-Process JStarsRecomp -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { "no window"; exit 1 }
$r = New-Object W32n+RECT
[void][W32n]::GetClientRect($p.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][W32n]::PrintWindow($p.MainWindowHandle, $dc, 3)
$g.ReleaseHdc($dc); $g.Dispose()
[void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Out))
$fmt = if ($Out -like "*.bmp") { [System.Drawing.Imaging.ImageFormat]::Bmp } else { [System.Drawing.Imaging.ImageFormat]::Png }
$bmp.Save($Out, $fmt); $bmp.Dispose()
"saved $Out"
