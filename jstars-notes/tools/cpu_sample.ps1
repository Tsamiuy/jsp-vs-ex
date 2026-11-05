# Sample JStarsRecomp.exe's CPU use (percent of ONE core) at given seconds after it starts.
# usage: cpu_sample.ps1 -At "60,200,240" -Out <file>
# 60 s is the title screen (idle), 200+ s the demo cards.
param([string]$At = "60,200,240", [string]$Out = "cpu.txt")
$t0 = $null
for ($i = 0; $i -lt 600 -and -not $t0; $i++) {
  $p = Get-Process JStarsRecomp -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($p) { $t0 = Get-Date } else { Start-Sleep -Milliseconds 500 }
}
if (-not $t0) { "no process" | Set-Content $Out; exit 1 }
$lines = @()
foreach ($s in ($At -split ',' | ForEach-Object { [int]$_ })) {
  $wait = $s - ((Get-Date) - $t0).TotalSeconds
  if ($wait -gt 0) { Start-Sleep -Seconds $wait }
  $p = Get-Process JStarsRecomp -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { break }
  $c0 = $p.TotalProcessorTime.TotalMilliseconds; $w0 = Get-Date
  Start-Sleep -Seconds 5
  $p.Refresh(); $c1 = $p.TotalProcessorTime.TotalMilliseconds; $w1 = Get-Date
  $pct = 100.0 * ($c1 - $c0) / (($w1 - $w0).TotalMilliseconds)
  $lines += ("t={0}s cpu={1:N0}% of one core" -f $s, $pct)
}
$lines | Set-Content $Out
