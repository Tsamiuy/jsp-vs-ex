# Draws a J-Stars screen title (512x64, yellow-to-orange letters with a brown rim) or the faint grey watermark of the
# help bar (1024x128, right-aligned) as a straight-alpha PNG, in the look of the game's "Main Menu" pictures (drawn
# here from Windows fonts; no game picture is used or shipped).
#   mm_title.ps1 -Out battle_title.png -Text "Battle Modes" -Kind title
#   mm_title.ps1 -Out battle_mark.png  -Text "Battle Modes" -Kind watermark
# Made for the Battle Modes page (2026-11-19).
param([string]$Out, [string]$Text = "Battle Modes", [string]$Kind = "title", [float]$Size = 0, [float]$Slant = 0, [float]$WidthScale = 0, [float]$Track = 0, [string]$Font = "")
Add-Type -AssemblyName System.Drawing
$title = $Kind -eq "title"
if ($title) { $W = 512; $H = 64 } else { $W = 1024; $H = 128 }
if (-not $Font) { $Font = if ($title) { "Arial" } else { "Impact" } }
if ($Size -eq 0) { $Size = if ($title) { 43 } else { 66 } }
if ($Slant -eq 0) { $Slant = if ($title) { -0.60 } else { -0.34 } }
if ($WidthScale -eq 0) { $WidthScale = if ($title) { 1.40 } else { 1.18 } }
if ($Track -eq 0) { $Track = if ($title) { 2.0 } else { 0.5 } }
$style = if ($title) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
$bmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
if ($title) { $g.Clear([System.Drawing.Color]::FromArgb(0, 98, 40, 16)) } else { $g.Clear([System.Drawing.Color]::FromArgb(0, 153, 153, 153)) }
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$ff = New-Object System.Drawing.FontFamily $Font
$sf = [System.Drawing.StringFormat]::GenericTypographic
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$cx = 0.0
foreach ($ch in $Text.ToCharArray()) {
    if ($ch -eq ' ') { $cx += $Size * $(if ($title) { 0.42 } else { 0.22 }) + $Track; continue }
    $p1 = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p1.AddString([string]$ch, $ff, [int]$style, $Size, (New-Object System.Drawing.PointF(0, 0)), $sf)
    $b1 = $p1.GetBounds()
    $m1 = New-Object System.Drawing.Drawing2D.Matrix -ArgumentList ([float]1), ([float]0), ([float]0), ([float]1), ([float]$cx), ([float]0); $p1.Transform($m1)
    $path.AddPath($p1, $false)
    $cx += $b1.Right + $Track
}
$b = $path.GetBounds()
# title: top at y 14, left at x 6; watermark: top at y 33, right edge at x 1004 (measured on the game's pictures)
$top = if ($title) { 14 } else { 33 }
$m = New-Object System.Drawing.Drawing2D.Matrix -ArgumentList ([float]$WidthScale), ([float]0), ([float]0), ([float]1), ([float](-$b.Left * $WidthScale)), ([float]($top - $b.Top))
$path.Transform($m)
$b = $path.GetBounds()
$sk = New-Object System.Drawing.Drawing2D.Matrix -ArgumentList ([float]1), ([float]0), ([float]$Slant), ([float]1), ([float](-$Slant * $b.Bottom)), ([float]0)
$path.Transform($sk)
$b = $path.GetBounds()
$dx = if ($title) { 4 - $b.Left } else { 1004 - $b.Right }
$mv = New-Object System.Drawing.Drawing2D.Matrix -ArgumentList ([float]1), ([float]0), ([float]0), ([float]1), ([float]$dx), ([float]0)
$path.Transform($mv)
$b = $path.GetBounds()
if ($title) {
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 98, 40, 16)), 4.5; $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawPath($pen, $path)
    $r = New-Object System.Drawing.RectangleF($b.Left, $b.Top, $b.Width, $b.Height)
    $gb = New-Object System.Drawing.Drawing2D.LinearGradientBrush($r, [System.Drawing.Color]::Yellow, [System.Drawing.Color]::Orange, [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
    $cb = New-Object System.Drawing.Drawing2D.ColorBlend 3
    $cb.Colors = @([System.Drawing.Color]::FromArgb(255, 255, 255, 32), [System.Drawing.Color]::FromArgb(255, 255, 206, 0), [System.Drawing.Color]::FromArgb(255, 255, 130, 0))
    $cb.Positions = @([float]0, [float]0.5, [float]1)
    $gb.InterpolationColors = $cb
    $g.FillPath($gb, $path)
} else {
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(26, 153, 153, 153))), $path)
}
"bounds $([int]$b.X) $([int]$b.Y) $([int]$b.Width) $([int]$b.Height)"
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
