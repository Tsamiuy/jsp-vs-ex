# Put screenshots side by side (scaled to half size) with a label under each.
# usage: montage.ps1 -Out <png> -Cols 2 -Items "label1=path1","label2=path2",...
param([string]$Out, [int]$Cols = 2, [string[]]$Items)
Add-Type -AssemblyName System.Drawing
$Items = @($Items | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$w = 640; $h = 360; $lab = 28
$rows = [math]::Ceiling($Items.Count / $Cols)
$bmp = New-Object System.Drawing.Bitmap ($w * $Cols), (($h + $lab) * $rows)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Black)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$font = New-Object System.Drawing.Font "Segoe UI", 13
for ($i = 0; $i -lt $Items.Count; $i++) {
  $label, $path = $Items[$i] -split '=', 2
  $x = ($i % $Cols) * $w; $y = [math]::Floor($i / $Cols) * ($h + $lab)
  $img = [System.Drawing.Image]::FromFile($path)
  $g.DrawImage($img, $x, $y, $w, $h); $img.Dispose()
  $g.DrawString($label, $font, [System.Drawing.Brushes]::White, $x + 6, $y + $h + 3)
}
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
"saved $Out"
