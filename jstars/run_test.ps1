# Boot-test JStarsRecomp.exe for a limited time and keep its logs.
#   powershell -ExecutionPolicy Bypass -File run_test.ps1 [seconds] [-Commands stat,threads]
# Mounts the ISO read-only as the disc, maps /dev_hdd0 to .\hdd0, and writes
# run\stdout.txt and run\stderr.txt. Unmounts the ISO afterwards.
#
# -Commands sends debug-console commands (see port\jstars_debug.cpp) near the
# end of the run, five seconds apart; their answers land in run\dbg.txt.out.
#
# Verbose runtime logging is OFF by default (PS3_VERBOSE=0): with it on, the
# match logs ~4,900 lines/s and runs at less than half speed (docs/notes
# FINDINGS section 30). -VerboseLog turns it back on; a PS3_VERBOSE already set
# in the environment is kept as is.
param([int]$Seconds = 90, [string[]]$Commands = @(), [switch]$VerboseLog)

$here = $PSScriptRoot
$root = Split-Path $here
$iso  = "$root\RPCS3\games\J-Stars Victory Vs+.iso"

New-Item -ItemType Directory -Force "$here\hdd0", "$here\run", "$here\run\spu_miss" | Out-Null
$img = Get-DiskImage -ImagePath $iso
$mountedHere = -not $img.Attached
if ($mountedHere) { $img = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru }
$drive = ($img | Get-Volume).DriveLetter

$env:PS3_VFS_ROOT  = "${drive}:/"
$env:PS3_HDD0_ROOT = "$here\hdd0".Replace('\', '/')
$env:PS3_DEV_FLASH = "$root\RPCS3\dev_flash".Replace('\', '/')
$env:PS3_TITLE     = "J-Stars Victory VS+ (recomp test)"
if ($VerboseLog) { $env:PS3_VERBOSE = "1" }
elseif (-not $env:PS3_VERBOSE) { $env:PS3_VERBOSE = "0" }
# Unlifted SPURS job binaries are written here (one file per fingerprint) so
# they can be lifted and registered.
$env:SPU_DUMP_MISS = "$here\run\spu_miss".Replace('\', '/')

# "powershell -File" hands a comma list over as one string; split it here.
$Commands = @($Commands | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$dbg = "$here\run\dbg.txt"
if ($Commands.Count) {
    Set-Content -Path $dbg -Value $null
    Set-Content -Path "$dbg.out" -Value $null
    $env:PS3_DEBUG = $dbg.Replace('\', '/')
}

# The maintainer keeps CPU boost off when idle: turn it on for the run and off again afterwards (also when
# the run fails or is stopped). JSTARS_NO_BOOST=1 skips both; JSTARS_BOOST_STAY=1 leaves it on afterwards.
$boost = Join-Path $root "tools\boost"
if (-not $env:JSTARS_NO_BOOST -and (Test-Path "$boost\boost_on.bat")) { & cmd /c "`"$boost\boost_on.bat`"" | Out-Null }
try {
    # JSTARS_BUILD=build-pdb starts the debug-info build instead of build\ (CPU profiling).
    $bdir = if ($env:JSTARS_BUILD) { $env:JSTARS_BUILD } else { "build" }
    $p = Start-Process -FilePath "$here\port\$bdir\JStarsRecomp.exe" `
        -ArgumentList "`"$here\EBOOT.elf`"" -WorkingDirectory "$here\run" `
        -RedirectStandardOutput "$here\run\stdout.txt" `
        -RedirectStandardError "$here\run\stderr.txt" -PassThru -NoNewWindow
    $queryAt = [Math]::Max(5, $Seconds - 5 * $Commands.Count - 5)
    $exited = $p.WaitForExit($queryAt * 1000)
    if (-not $exited) {
        foreach ($c in $Commands) { Set-Content -Path $dbg -Value $c; Start-Sleep -Seconds 5 }
        $exited = $p.WaitForExit([Math]::Max(1, $Seconds - $queryAt - 5 * $Commands.Count) * 1000)
    }
    if ($exited) {
        "exited by itself, code $($p.ExitCode)"
    } else {
        # ask the game to close first, force it only if it does not
        $null = $p.CloseMainWindow()
        if (-not $p.WaitForExit(5000)) { Stop-Process -Id $p.Id -Force }
        "still running after $Seconds s - stopped it"
    }
} finally {
    if ($mountedHere) { Dismount-DiskImage -ImagePath $iso | Out-Null }
    if (-not $env:JSTARS_NO_BOOST -and -not $env:JSTARS_BOOST_STAY -and (Test-Path "$boost\boost_off.bat")) { & cmd /c "`"$boost\boost_off.bat`"" | Out-Null }
}
"logs: $here\run\stdout.txt, $here\run\stderr.txt"
if ($Commands.Count) { "console answers: $dbg.out" }
