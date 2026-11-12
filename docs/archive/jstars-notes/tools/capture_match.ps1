# One command for the standard demo-match frame capture (the STATUS.md recipe).
#   powershell -ExecutionPolicy Bypass -File docs\archive\jstars-notes\tools\capture_match.ps1 -Name rt49 `
#       [-Cap "5650:2:40"] [-Seconds 275] [-Shots "200,230,250"] [-Env @{STRIP_NATIVE='1'}] [-Commands stat]
#
# What it does:
#   1. sets PAD_SCRIPT=31:0x4000 (one CROSS, no menu input: title ~140 s, idle demo match ~190 s),
#      PERF=1, RSX_FRAMELOG=60, RSX_CAP=<Cap>, RSX_CAP_DIR=jstars\run\rsxcap_<Name>, plus -Env;
#   2. starts snap_window.ps1 (screenshots to jstars\run\shots_<Name>) and run_test.ps1 together;
#   3. restores the environment, then prints capstat.py for the new capture.
# It only launches the existing build (jstars\port\build\JStarsRecomp.exe); it never builds.
# Per project rules only the AI assistant runs the game.
param(
    [Parameter(Mandatory)][string]$Name,
    [string]$Cap = "5650:2:40",
    [int]$Seconds = 275,
    [string]$Shots = "200,230,250",
    [hashtable]$Env = @{},
    [string[]]$Commands = @("stat")
)

$tools  = $PSScriptRoot
$root   = Split-Path (Split-Path $tools)
$jstars = Join-Path $root "jstars"
$capDir = Join-Path $jstars "run\rsxcap_$Name"
$shotDir = Join-Path $jstars "run\shots_$Name"
if (Test-Path $capDir) { "capture dir $capDir already exists - pick another -Name"; exit 1 }

$vars = @{
    PAD_SCRIPT   = "31:0x4000"
    PERF         = "1"
    RSX_FRAMELOG = "60"
    RSX_CAP      = $Cap
    RSX_CAP_DIR  = $capDir.Replace('\', '/')
}
foreach ($k in $Env.Keys) { $vars[$k] = [string]$Env[$k] }

$saved = @{}
foreach ($k in $vars.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); Set-Item "env:$k" $vars[$k] }
"env: " + (($vars.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join " ")

$snap = $null
try {
    if ($Shots) {
        $snap = Start-Process powershell -PassThru -NoNewWindow -ArgumentList @(
            "-ExecutionPolicy", "Bypass", "-File", "`"$jstars\snap_window.ps1`"", "-At", "`"$Shots`"", "-Out", "`"$shotDir`"")
    }
    & (Join-Path $jstars "run_test.ps1") $Seconds -Commands $Commands
} finally {
    foreach ($k in $saved.Keys) {
        if ($null -eq $saved[$k]) { Remove-Item "env:$k" -ErrorAction SilentlyContinue } else { Set-Item "env:$k" $saved[$k] }
    }
    if ($snap -and -not $snap.HasExited) { $snap.WaitForExit(15000) | Out-Null }
}

# Keep a copy of the run's settings and logs' tails next to the capture.
if (Test-Path $capDir) {
    $info = @("date: $(Get-Date -Format s)", "seconds: $Seconds", "shots: $Shots") +
            ($vars.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" })
    Set-Content -Path (Join-Path $capDir "run_info.txt") -Value $info
    Select-String -Path (Join-Path $jstars "run\stderr.txt") -Pattern "\[PERF\]" |
        Select-Object -Last 20 | ForEach-Object { $_.Line } | Set-Content (Join-Path $capDir "perf_tail.txt")
    python3 (Join-Path $tools "capstat.py") $capDir
} else {
    "no capture written to $capDir (did the run reach frame $($Cap.Split(':')[0])?)"
}
