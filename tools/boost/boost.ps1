# CPU boost switch for the active power plan: "Maximum processor state" 100 % = boost, 99 % = no boost.
#   boost.ps1 toggle | on | off | status
# Changes AC and battery (DC) values of the CURRENT plan only. Does not need admin.
param([ValidateSet('toggle','on','off','status')][string]$Mode = 'toggle')

function Get-Max {
    # returns @{ac=<int>; dc=<int>} of the current plan's max processor state
    $q = powercfg /query SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX | Out-String
    $ac = [regex]::Match($q, 'Current AC Power Setting Index:\s*0x([0-9a-fA-F]+)')
    $dc = [regex]::Match($q, 'Current DC Power Setting Index:\s*0x([0-9a-fA-F]+)')
    if (-not $ac.Success -or -not $dc.Success) { return $null }
    @{ ac = [Convert]::ToInt32($ac.Groups[1].Value, 16); dc = [Convert]::ToInt32($dc.Groups[1].Value, 16) }
}

function Show-Toast([string]$text) {
    try {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $n = New-Object System.Windows.Forms.NotifyIcon
        $n.Icon = [System.Drawing.SystemIcons]::Information
        $n.BalloonTipTitle = 'CPU boost'
        $n.BalloonTipText = $text
        $n.Visible = $true
        $n.ShowBalloonTip(3000)
        Start-Sleep -Milliseconds 3200
        $n.Dispose()
    } catch { Write-Host $text }
}

$cur = Get-Max
if ($null -eq $cur) { Show-Toast 'Could not read the power plan (PROCTHROTTLEMAX not available).'; exit 1 }

if ($Mode -eq 'status') {
    $s = if ($cur.ac -ge 100) { 'ON (100 %)' } else { "OFF ($($cur.ac) %)" }
    Show-Toast "Boost is $s on AC, battery max $($cur.dc) %"; exit 0
}

$boostOn = switch ($Mode) { 'on' { $true } 'off' { $false } default { $cur.ac -lt 100 } }   # toggle: judged by AC value
$v = if ($boostOn) { 100 } else { 99 }
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX $v
powercfg /setdcvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX $v
powercfg /setactive SCHEME_CURRENT
$new = Get-Max
if ($new -and $new.ac -eq $v) {
    Show-Toast ("Boost " + $(if ($boostOn) { 'ON  (max 100 %)' } else { 'OFF (max 99 %)' }))
} else {
    Show-Toast 'The change did not apply. Is the plan locked by your PC maker?'
    exit 1
}
