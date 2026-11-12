# Draws a J-Stars Options menu label into a 512x64 PNG (straight alpha).
#   -Style dark : the highlighted look, black letters with a white rim and glow
#   -Style light: the normal look, white letters (grey bottom band) with a black rim
# exit_label.ps1 -Out exit_dark.png  -Style dark  -Y -5
# exit_label.ps1 -Out exit_light.png -Style light -Y -3
param([string]$Out, [string]$Text = "Exit Game", [string]$Style = "dark", [float]$Size = 30, [float]$X = 2, [float]$Y = -5, [string]$Font = "Arial Black")
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap 512, 64, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$rim = if ($Style -eq "light") { 0 } else { 255 }
$g.Clear([System.Drawing.Color]::FromArgb(0, $rim, $rim, $rim))
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$ff = New-Object System.Drawing.FontFamily $Font
$path.AddString($Text, $ff, [int][System.Drawing.FontStyle]::Regular, $Size, (New-Object System.Drawing.PointF($X, $Y)), [System.Drawing.StringFormat]::GenericTypographic)
# oblique like the game's labels
$m = New-Object System.Drawing.Drawing2D.Matrix(1, 0, -0.32, 1, 10, 0)
$path.Transform($m)
function New-RimPen($a, $c, $w) { $p = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb($a, $c, $c, $c)), $w; $p.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round; $p }
$b = $path.GetBounds()
if ($Style -eq "light") {
    $g.DrawPath((New-RimPen 110 0 9), $path)
    $g.DrawPath((New-RimPen 255 0 6), $path)
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 247, 247, 247))), $path)
    # the game's white labels darken over their bottom few rows
    $g.SetClip((New-Object System.Drawing.RectangleF(0, ($b.Bottom - 3), 512, 64)))
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 150, 150, 150))), $path)
    $g.ResetClip()
} else {
    $g.DrawPath((New-RimPen 60 255 9), $path)
    $g.DrawPath((New-RimPen 200 255 5), $path)
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 0, 0))), $path)
}
"bounds $($b.X) $($b.Y) $($b.Width) $($b.Height)"
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
