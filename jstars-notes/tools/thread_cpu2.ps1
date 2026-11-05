# Per-THREAD CPU of JStarsRecomp.exe with what tells polling from work: user vs kernel time
# (yield/Sleep(0)/short-sleep loops burn KERNEL time), context switches per second (each wake
# is one), and the thread's entry point (module+rva; exe rvas resolve with tools\symdumps.py's
# llvm-symbolizer line: llvm-symbolizer --obj=JStarsRecomp.exe 0x140000000+rva).
# No admin needed. Read-only: it only queries the process.
# usage: thread_cpu2.ps1 [-Start 200] [-Window 5] [-Count 12] [-Top 30] [-Out thread_cpu2.txt]
#        [-Name JStarsRecomp]
param([int]$Start = 200, [int]$Window = 5, [int]$Count = 12, [int]$Top = 30,
      [string]$Out = "thread_cpu2.txt", [string]$Name = "JStarsRecomp")

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class TC2 {
  [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int cls, IntPtr buf, int len, out int ret);
  [DllImport("ntdll.dll")] static extern int NtQueryInformationThread(IntPtr h, int cls, out IntPtr info, int len, IntPtr ret);
  [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int access, bool inherit, int tid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  public class T { public int Tid; public long Kernel, User; public uint Cs; public int State, Wait; public long Start; }
  // SYSTEM_PROCESS_INFORMATION / SYSTEM_THREAD_INFORMATION (x64 layout)
  public static List<T> Snap(int pid) {
    int len = 1 << 22; IntPtr buf = IntPtr.Zero; int ret, st;
    for (;;) {
      buf = Marshal.AllocHGlobal(len);
      st = NtQuerySystemInformation(5, buf, len, out ret);
      if (st == unchecked((int)0xC0000004)) { Marshal.FreeHGlobal(buf); len = Math.Max(len * 2, ret + 65536); continue; }
      break;
    }
    var res = new List<T>();
    if (st != 0) { Marshal.FreeHGlobal(buf); return res; }
    long off = 0;
    for (;;) {
      IntPtr p = new IntPtr(buf.ToInt64() + off);
      int next = Marshal.ReadInt32(p, 0);
      int nthreads = Marshal.ReadInt32(p, 4);
      long upid = Marshal.ReadInt64(p, 0x50);
      if (upid == pid) {
        for (int i = 0; i < nthreads; i++) {
          IntPtr t = new IntPtr(p.ToInt64() + 0x100 + i * 0x50);
          var x = new T();
          x.Kernel = Marshal.ReadInt64(t, 0x00);
          x.User = Marshal.ReadInt64(t, 0x08);
          x.Tid = (int)Marshal.ReadInt64(t, 0x30);   // ClientId.UniqueThread
          x.Cs = (uint)Marshal.ReadInt32(t, 0x40);   // ContextSwitches
          x.State = Marshal.ReadInt32(t, 0x44);
          x.Wait = Marshal.ReadInt32(t, 0x48);
          IntPtr h = OpenThread(0x0040 /*QUERY_INFORMATION*/, false, x.Tid);
          if (h != IntPtr.Zero) { IntPtr sa; if (NtQueryInformationThread(h, 9, out sa, IntPtr.Size, IntPtr.Zero) == 0) x.Start = sa.ToInt64(); CloseHandle(h); }
          res.Add(x);
        }
        break;
      }
      if (next == 0) break;
      off += next;
    }
    Marshal.FreeHGlobal(buf);
    return res;
  }
}
"@

$p = $null; $t0 = $null
for ($i = 0; $i -lt 1200 -and -not $p; $i++) {
  $p = Get-Process $Name -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($p) { $t0 = Get-Date } else { Start-Sleep -Milliseconds 500 }
}
if (-not $p) { "no process" | Set-Content $Out; exit 1 }
$wait = $Start - ((Get-Date) - $t0).TotalSeconds
if ($wait -gt 0) { Start-Sleep -Seconds $wait }

# module ranges, to name entry points
$mods = @()
try { foreach ($m in $p.Modules) { $mods += [pscustomobject]@{ B = [int64]$m.BaseAddress; E = [int64]$m.BaseAddress + $m.ModuleMemorySize; N = $m.ModuleName } } } catch { }
function Where-Addr([int64]$a) {
  if ($a -eq 0) { return "?" }
  foreach ($m in $mods) { if ($a -ge $m.B -and $a -lt $m.E) { return ('{0}+0x{1:X}' -f $m.N, ($a - $m.B)) } }
  return ('0x{0:X}' -f $a)
}
$waitNames = @{0='Executive';1='FreePage';2='PageIn';4='Suspended';5='Suspended';6='UserRequest';7='Executive';
  13='UserRequest';14='EventPair';15='Queue';18='LpcReply';19='VirtualMemory';20='PageOut';
  21='Rendezvous';22='Keyed';23='Keyed';24='Keyed';25='Keyed';26='Executive';27='Executive';
  30='WrDelayExecution';31='Suspended';32='WrAlertByThreadId';33='Resource';34='PushLock';35='Mutex';37='Preempted'}

$lines = @("# thread_cpu2: % of one core (user+kernel), kernel %, wakes/s, entry, state; $Window s windows (pid $($p.Id))")
for ($w = 0; $w -lt $Count; $w++) {
  $a = [TC2]::Snap($p.Id); $wa = Get-Date
  if ($a.Count -eq 0) { break }
  Start-Sleep -Seconds $Window
  $b = [TC2]::Snap($p.Id); if ($b.Count -eq 0) { break }
  $ms = ((Get-Date) - $wa).TotalMilliseconds
  $ah = @{}; foreach ($x in $a) { $ah[$x.Tid] = $x }
  $rows = foreach ($x in $b) {
    if ($ah.ContainsKey($x.Tid)) {
      $o = $ah[$x.Tid]
      $k = ($x.Kernel - $o.Kernel) / 10000.0; $u = ($x.User - $o.User) / 10000.0   # 100 ns -> ms
      [pscustomobject]@{ Tid = $x.Tid; Pct = 100.0 * ($k + $u) / $ms; Kp = 100.0 * $k / $ms;
                         Cs = ($x.Cs - $o.Cs) * 1000.0 / $ms; Start = (Where-Addr $x.Start);
                         State = $(if ($x.State -eq 5) { "Wait/" + $waitNames[$x.Wait] } else { "State" + $x.State }) }
    }
  }
  $sum = ($rows | Measure-Object -Property Pct -Sum).Sum
  $ksum = ($rows | Measure-Object -Property Kp -Sum).Sum
  $cssum = ($rows | Measure-Object -Property Cs -Sum).Sum
  $t = [int](((Get-Date) - $t0).TotalSeconds)
  $lines += ("--- t={0}s  total {1:N0}% of one core (kernel {2:N0}%), {3:N0} wakes/s, {4} threads ---" -f $t, $sum, $ksum, $cssum, @($rows).Count)
  foreach ($r in ($rows | Sort-Object Pct -Descending | Select-Object -First $Top)) {
    $lines += ("  tid {0,6}  {1,5:N1}%  kern {2,5:N1}%  {3,7:N0} wakes/s  {4,-40} {5}" -f $r.Tid, $r.Pct, $r.Kp, $r.Cs, $r.Start, $r.State)
  }
}
# Name the exe entry points (thread procs) when llvm-symbolizer and the exe's PDB are at hand.
$sym = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe"
$exe = $null; try { $exe = $p.MainModule.FileName } catch { }
$rvas = @($lines | Select-String -Pattern "$Name\.exe\+0x([0-9A-F]+)" -AllMatches | ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
if ($exe -and (Test-Path $sym) -and $rvas.Count -gt 0) {
  $inp = ($rvas | ForEach-Object { '0x{0:X}' -f (0x140000000 + [Convert]::ToInt64($_, 16)) }) -join "`n"
  $res = $inp | & $sym "--obj=$exe" --functions=linkage --no-inlines 2>$null
  $names = @(); $blk = @()
  foreach ($l in $res) { if ($l -eq "") { if ($blk.Count) { $names += $blk[0] }; $blk = @() } else { $blk += $l } }
  if ($blk.Count) { $names += $blk[0] }
  $lines += "# entry points:"
  for ($i = 0; $i -lt $rvas.Count -and $i -lt $names.Count; $i++) { $lines += ("#   {0}.exe+0x{1} = {2}" -f $Name, $rvas[$i], $names[$i]) }
}
$lines | Set-Content $Out
