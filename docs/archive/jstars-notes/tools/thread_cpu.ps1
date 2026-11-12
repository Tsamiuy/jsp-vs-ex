# Per-THREAD CPU use of JStarsRecomp.exe: real CPU time (spinning counts, sleeping does not),
# as % of one core, over consecutive windows. Pair with PS3_SAMPLE=2 (same thread ids in the
# [samp] tid lines) and `-Commands threads` (guest call chain per host tid) to name each thread.
# usage: thread_cpu.ps1 [-Start 200] [-Window 5] [-Count 12] [-Top 20] [-Out thread_cpu.txt]
#   -Start  seconds after the process appears before the first window (e.g. once a fight runs)
param([int]$Start = 200, [int]$Window = 5, [int]$Count = 12, [int]$Top = 20,
      [string]$Out = "thread_cpu.txt")
$p = $null; $t0 = $null
for ($i = 0; $i -lt 1200 -and -not $p; $i++) {
  $p = Get-Process JStarsRecomp -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($p) { $t0 = Get-Date } else { Start-Sleep -Milliseconds 500 }
}
if (-not $p) { "no process" | Set-Content $Out; exit 1 }
$wait = $Start - ((Get-Date) - $t0).TotalSeconds
if ($wait -gt 0) { Start-Sleep -Seconds $wait }

function Snap($proc) {
  $h = @{}
  try { $proc.Refresh() } catch { return $null }
  foreach ($t in $proc.Threads) {
    try { $h[$t.Id] = @{ cpu = $t.TotalProcessorTime.TotalMilliseconds;
                         start = ('0x{0:X}' -f [int64]$t.StartAddress);
                         state = "$($t.ThreadState)/$($t.WaitReason)" } } catch { }
  }
  return $h
}

$lines = @("# thread_cpu: % of one core per thread, $Window s windows, top $Top (pid $($p.Id))")
for ($w = 0; $w -lt $Count; $w++) {
  $a = Snap $p; if (-not $a) { break }
  $wa = Get-Date
  Start-Sleep -Seconds $Window
  $b = Snap $p; if (-not $b) { break }
  $ms = ((Get-Date) - $wa).TotalMilliseconds
  $rows = foreach ($id in $b.Keys) {
    if ($a.ContainsKey($id)) {
      [pscustomobject]@{ Tid = $id; Pct = 100.0 * ($b[$id].cpu - $a[$id].cpu) / $ms;
                         Start = $b[$id].start; State = $b[$id].state }
    }
  }
  $sum = ($rows | Measure-Object -Property Pct -Sum).Sum
  $t = [int](((Get-Date) - $t0).TotalSeconds)
  $lines += ("--- t={0}s  process total {1:N0}% of one core ({2} threads) ---" -f $t, $sum, @($rows).Count)
  foreach ($r in ($rows | Sort-Object Pct -Descending | Select-Object -First $Top)) {
    $lines += ("  tid {0,6}  {1,5:N1}%  start {2}  {3}" -f $r.Tid, $r.Pct, $r.Start, $r.State)
  }
}
$lines | Set-Content $Out
