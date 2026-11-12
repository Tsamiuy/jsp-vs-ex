# Side-by-side screenshots with a large label printed on each tile (top-left, on a dark box).
# usage: labeled.ps1 -Out <png> -Cols 2 -Items "LABEL1=path1","LABEL2=path2",...
param([string]$Out, [int]$Cols = 2, [string[]]$Items)
Add-Type -AssemblyName System.Drawing
$Items = @($Items | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$w = 800; $h = 450; $gap = 6
$rows = [math]::Ceiling($Items.Count / $Cols)
$bmp = New-Object System.Drawing.Bitmap ($w * $Cols + $gap * ($Cols - 1)), ($h * $rows + $gap * ($rows - 1))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::White)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$font = New-Object System.Drawing.Font "Segoe UI", 34, ([System.Drawing.FontStyle]::Bold)
$box = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(210, 0, 0, 0))
$fg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 230, 0))
for ($i = 0; $i -lt $Items.Count; $i++) {
  $label, $path = $Items[$i] -split '=', 2
  $x = ($i % $Cols) * ($w + $gap); $y = [math]::Floor($i / $Cols) * ($h + $gap)
  $img = [System.Drawing.Image]::FromFile($path)
  $g.DrawImage($img, $x, $y, $w, $h); $img.Dispose()
  $sz = $g.MeasureString($label, $font)
  $g.FillRectangle($box, $x + 8, $y + 8, $sz.Width + 16, $sz.Height + 4)
  $g.DrawString($label, $font, $fg, $x + 16, $y + 10)
}
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
"saved $Out"
