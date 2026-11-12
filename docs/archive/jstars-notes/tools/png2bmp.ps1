param([string]$In, [string]$Out)
Add-Type -AssemblyName System.Drawing
$i = [System.Drawing.Image]::FromFile($In); $b = New-Object System.Drawing.Bitmap $i
$b.Save($Out, [System.Drawing.Imaging.ImageFormat]::Bmp); $b.Dispose(); $i.Dispose()
