# Flag screenshots where the top-right-quarter triangle glitch shows: a large flat colour over
# x in [W/2, W], y in [0, (H/2)*(W-x)/(W/2)] (the maintainer 2026-10-24). usage: tri_scan.ps1 <dir>
param([string]$Dir)
Add-Type -AssemblyName System.Drawing
foreach ($f in Get-ChildItem "$Dir\*.png" | Sort-Object Name) {
  $b = [System.Drawing.Bitmap]::FromFile($f.FullName); $W = $b.Width; $H = $b.Height
  $pts = @(@(0.52,0.05),@(0.52,0.40),@(0.60,0.20),@(0.70,0.10),@(0.62,0.30),@(0.80,0.05),@(0.56,0.35))
  $c = foreach ($p in $pts) { $b.GetPixel([int]($p[0]*$W), [int]($p[1]*$H)) }
  $o = @(@(0.25,0.75),@(0.10,0.90),@(0.40,0.60)) | ForEach-Object { $b.GetPixel([int]($_[0]*$W), [int]($_[1]*$H)) }
  $b.Dispose()
  $r = ($c | Measure-Object R -Maximum -Minimum); $g = ($c | Measure-Object G -Maximum -Minimum); $bl = ($c | Measure-Object B -Maximum -Minimum)
  $spread = [math]::Max([math]::Max($r.Maximum-$r.Minimum, $g.Maximum-$g.Minimum), $bl.Maximum-$bl.Minimum)
  $diff = ($o | ForEach-Object { [math]::Abs($_.R-$c[0].R)+[math]::Abs($_.G-$c[0].G)+[math]::Abs($_.B-$c[0].B) } | Measure-Object -Maximum).Maximum
  if ($spread -le 6 -and $diff -gt 40) { "FLAT $($f.Name) spread=$spread rgb=$($c[0].R),$($c[0].G),$($c[0].B)" }
}
