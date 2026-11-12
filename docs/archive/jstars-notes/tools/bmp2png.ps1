# Convert the BMPs of an RSX_CAP capture (or one frame folder) to PNG for viewing.
#   powershell -ExecutionPolicy Bypass -File bmp2png.ps1 <capture or frame dir> [-Scale 0.5] [-Only rt_0ED10000]
# Writes <frame>\png\<name>.png next to the BMPs; the BMPs are left untouched.
# -Scale shrinks the images (handy for sending), -Only keeps names containing that text.
param([Parameter(Mandatory)][string]$Path, [double]$Scale = 1.0, [string]$Only = "")
Add-Type -AssemblyName System.Drawing

$frames = if (Test-Path (Join-Path $Path "manifest.txt")) { @(Get-Item $Path) }
          else { Get-ChildItem $Path -Directory -Filter "frame_*" | Sort-Object Name }
foreach ($f in $frames) {
    $out = Join-Path $f.FullName "png"
    New-Item -ItemType Directory -Force $out | Out-Null
    foreach ($b in Get-ChildItem $f.FullName -Filter "*.bmp" | Where-Object { $_.Name -like "*$Only*" }) {
        $img = [System.Drawing.Image]::FromFile($b.FullName)
        try {
            $w = [Math]::Max(1, [int]($img.Width * $Scale)); $h = [Math]::Max(1, [int]($img.Height * $Scale))
            $bmp = New-Object System.Drawing.Bitmap $w, $h
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
            $g.DrawImage($img, 0, 0, $w, $h); $g.Dispose()
            $dst = Join-Path $out ($b.BaseName + ".png")
            $bmp.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
            "saved $dst"
        } finally { $img.Dispose() }
    }
}
