# Play J-Stars Victory VS+ (the recompiled port) with no time limit.
#   Start it with "Play J-Stars.bat" in the PS3Recomp folder.
# Mounts the ISO read-only as the disc, turns CPU boost on, starts the game,
# waits until you close the game window, then turns boost off and unmounts.
# Logs go to run\play_stdout.txt and run\play_stderr.txt (overwritten each time).

$here = $PSScriptRoot
$root = Split-Path $here
$iso  = if ($env:JSTARS_ISO) { $env:JSTARS_ISO } else { "$root\RPCS3\games\J-Stars Victory Vs+.iso" }   # set JSTARS_ISO to use another path
$exe  = "$here\port\build\JStarsRecomp.exe"

if (-not (Test-Path $exe)) { Write-Host "Game not built yet: $exe"; Read-Host "Press Enter to close"; exit 1 }
if (Get-Process JStarsRecomp -ErrorAction SilentlyContinue) {
    Write-Host "The game is already running."; Read-Host "Press Enter to close"; exit 1 }

New-Item -ItemType Directory -Force "$here\hdd0", "$here\run" | Out-Null
$img = Get-DiskImage -ImagePath $iso
$mountedHere = -not $img.Attached
if ($mountedHere) { $img = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru }
$drive = ($img | Get-Volume).DriveLetter

$env:PS3_VFS_ROOT  = "${drive}:/"
$env:PS3_HDD0_ROOT = "$here\hdd0".Replace('\', '/')
$env:PS3_DEV_FLASH = "$root\RPCS3\dev_flash".Replace('\', '/')
$env:PS3_TITLE     = "J-Stars Victory VS+"
$env:PS3_VERBOSE   = "0"

# Player settings from "J-Stars Settings.bat" (play_settings.txt: KEY=VALUE lines,
# e.g. RES_SCALE=3 for 4K, FULLSCREEN=1). Missing file = 720p in a window.
$settings = "$here\play_settings.txt"
if (Test-Path $settings) {
    foreach ($line in Get-Content $settings) {
        if ($line -match '^\s*([A-Z0-9_]+)\s*=\s*(.*?)\s*$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

$boost = Join-Path $root "tools\boost"
if (Test-Path "$boost\boost_on.bat") { & cmd /c "`"$boost\boost_on.bat`"" | Out-Null }
try {
    Write-Host "Starting J-Stars Victory VS+ ... (close the game window to quit)"
    $p = Start-Process -FilePath $exe -ArgumentList "`"$here\EBOOT.elf`"" -WorkingDirectory "$here\run" `
        -RedirectStandardOutput "$here\run\play_stdout.txt" -RedirectStandardError "$here\run\play_stderr.txt" `
        -PassThru -NoNewWindow
    $p.WaitForExit()
} finally {
    # Test Fight.bat: keep this session's log, named after the fighter and the time.
    if ($env:AUTO_FIGHT -and (Test-Path "$here\run\balance_log.txt")) {
        $kept = "$here\run\test_fights"
        New-Item -ItemType Directory -Force $kept | Out-Null
        $fighter = ($env:AUTO_FIGHT -split ' ')[0]
        Copy-Item "$here\run\balance_log.txt" "$kept\fighter_${fighter}_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').txt"
    }
    if (Test-Path "$boost\boost_off.bat") { & cmd /c "`"$boost\boost_off.bat`"" | Out-Null }
    if ($mountedHere) { Dismount-DiskImage -ImagePath $iso | Out-Null }
}
