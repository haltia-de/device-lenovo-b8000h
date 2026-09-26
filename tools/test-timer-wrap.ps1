[CmdletBinding()]
param(
    [string]$Port = 'COM11',
    [int]$TargetSeconds = 720,
    [int]$PollSeconds = 30
)
# Read-only endurance test for the MT6589 32-bit architected-counter fix.
# Success requires one uninterrupted boot to survive at least two 330 s wraps.
$ErrorActionPreference = 'Stop'
$shell = Join-Path $PSScriptRoot 'acm-shell.ps1'
$bootId = $null
$highest = 0.0
$deadline = (Get-Date).AddSeconds($TargetSeconds + 180)

while ((Get-Date) -lt $deadline) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) {
        Write-Output "WAIT $(Get-Date -Format HH:mm:ss) $Port absent"
        Start-Sleep -Seconds 2
        continue
    }
    try {
        $reply = & $shell -Port $Port -WaitMs 5000 -Command `
            "echo __TIMER_SAMPLE__; cat /proc/sys/kernel/random/boot_id; cut -d' ' -f1 /proc/uptime; dmesg | grep -m1 'clocksource: arch_sys_counter'"
        $lines = @($reply -split "`r?`n" | Where-Object { $_.Trim() })
        $sample = [Array]::IndexOf($lines, '__TIMER_SAMPLE__')
        if ($sample -lt 0 -or $lines.Count -lt ($sample + 3)) { throw "short ACM reply: $reply" }
        $seenBootId = $lines[$sample + 1].Trim()
        $uptime = [double]::Parse($lines[$sample + 2].Trim(), [Globalization.CultureInfo]::InvariantCulture)
        if ($null -eq $bootId) { $bootId = $seenBootId }
        if ($seenBootId -ne $bootId) { throw "tablet rebooted (boot id changed)" }
        if ($uptime -lt $highest) { throw "tablet uptime moved backwards" }
        $highest = $uptime
        Write-Output ("UPTIME {0:N1}s {1}" -f $uptime, ($lines | Select-Object -Skip ($sample + 3) -First 1))
        if ($uptime -ge $TargetSeconds) {
            Write-Output "PASS: uninterrupted uptime reached $uptime seconds"
            exit 0
        }
    }
    catch {
        Write-Output "RETRY $(Get-Date -Format HH:mm:ss): $($_.Exception.Message)"
    }
    Start-Sleep -Seconds $PollSeconds
}
throw "Timer-wrap test timed out; highest observed uptime was $highest seconds"
