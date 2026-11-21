# Draws a J-Stars main menu row label into a 300x43 PNG (straight alpha), in the look of the game's own row labels
# (drawn here from a Windows font; no game picture is used or shipped).
#   -Style light : the normal look, white letters shading to grey with a black rim and a soft dark edge
#   -Style dark  : the highlighted look, black letters with a soft white glow
# mm_label.ps1 -Out battle_light.png -Text "Battle Modes" -Style light
# Made for the Battle Modes page (2026-11-19).
param([string]$Out, [string]$Text = "Battle Modes", [string]$Style = "light", [float]$Size = 28, [float]$X = 10, [float]$Y = 7,
      [float]$Track = 2.6, [float]$Slant = -0.34, [float]$WidthScale = 1.25, [string]$Font = "Impact")   # defaults matched to the game labels 2026-11-19
Add-Type -AssemblyName System.Drawing
$W = 300; $H = 43
$bmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$edge = if ($Style -eq "light") { 0 } else { 255 }
$g.Clear([System.Drawing.Color]::FromArgb(0, $edge, $edge, $edge))
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$ff = New-Object System.Drawing.FontFamily $Font
$sf = [System.Drawing.StringFormat]::GenericTypographic
# one character at a time, so the letters can be spaced like the game's
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$cx = 0.0
foreach ($ch in $Text.ToCharArray()) {
    if ($ch -eq ' ') { $cx += $Size * 0.28 + $Track; continue }
    $p1 = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p1.AddString([string]$ch, $ff, [int][System.Drawing.FontStyle]::Regular, $Size, (New-Object System.Drawing.PointF(0, 0)), $sf)
    $b1 = $p1.GetBounds()
    $m1 = New-Object System.Drawing.Drawing2D.Matrix -ArgumentList ([float]1), ([float]0), ([float]0), ([float]1), ([float]$cx), ([float]0); $p1.Transform($m1)
    $path.AddPath($p1, $false)
    $cx += $b1.Right + $Track
}
$b = $path.GetBounds()
# widen, then place: left edge at X, top at Y
$m = New-Object System.Drawing.Drawing2D.Matrix -ArgumentList ([float]$WidthScale), ([float]0), ([float]0), ([float]1), ([float]($X - $b.Left * $WidthScale)), ([float]($Y - $b.Top))
$path.Transform($m)
$b = $path.GetBounds()
# slant about the bottom line (the game's labels lean right)
$sk = New-Object System.Drawing.Drawing2D.Matrix -ArgumentList ([float]1), ([float]0), ([float]$Slant), ([float]1), ([float](-$Slant * $b.Bottom)), ([float]0)
$path.Transform($sk)
$b = $path.GetBounds()
function New-RimPen($a, $c, $w) { $p = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb($a, $c, $c, $c)), $w; $p.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round; $p }
if ($Style -eq "light") {
    $g.DrawPath((New-RimPen 70 0 9), $path)
    $g.DrawPath((New-RimPen 150 0 7), $path)
    $g.DrawPath((New-RimPen 255 0 3), $path)
    $r = New-Object System.Drawing.RectangleF($b.Left, $b.Top, $b.Width, $b.Height)
    $gb = New-Object System.Drawing.Drawing2D.LinearGradientBrush($r, [System.Drawing.Color]::White, [System.Drawing.Color]::Gray, [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
    $cb = New-Object System.Drawing.Drawing2D.ColorBlend 4
    $cb.Colors = @([System.Drawing.Color]::FromArgb(255, 255, 255, 255), [System.Drawing.Color]::FromArgb(255, 255, 255, 255), [System.Drawing.Color]::FromArgb(255, 226, 226, 226), [System.Drawing.Color]::FromArgb(255, 246, 246, 246))   # whiter since 2026-11-19 (the maintainer: the new labels were greyer than the game's; before: 250 / 150 / 205)
    $cb.Positions = @([float]0, [float]0.42, [float]0.75, [float]1)
    $gb.InterpolationColors = $cb
    $g.FillPath($gb, $path)
} else {
    $g.DrawPath((New-RimPen 40 255 10), $path)
    $g.DrawPath((New-RimPen 90 255 7), $path)
    $g.DrawPath((New-RimPen 170 235 4), $path)
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 0, 0))), $path)
}
"bounds $([int]$b.X) $([int]$b.Y) $([int]$b.Width) $([int]$b.Height)"
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
