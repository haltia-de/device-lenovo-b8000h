[CmdletBinding()]
param(
    [int]$Bytes = 262144,
    [int]$Block = 2048,
    [int]$SleepMs = 5,
    [string]$Port = 'COM11'
)
# Measures whether paced bulk OUT over the B8000-H ACM console survives:
# sends $Bytes random bytes to "timeout 40 head -c N | md5sum" in $Block
# writes with $SleepMs pauses and compares the md5.
$ErrorActionPreference = 'Stop'
$data = New-Object byte[] $Bytes
(New-Object Random 42).NextBytes($data)
$want = ([BitConverter]::ToString([Security.Cryptography.MD5]::Create().ComputeHash($data)) -replace '-', '').ToLower()
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.WriteTimeout = 10000
$sp.Open(); $sp.DtrEnable = $true
try {
    Start-Sleep -Milliseconds 300; $null = $sp.ReadExisting()
    $sp.Write("timeout 40 head -c $Bytes | md5sum; echo PT_END`n")
    Start-Sleep -Milliseconds 400
    $t0 = Get-Date; $sent = 0
    try {
        for ($o = 0; $o -lt $Bytes; $o += $Block) {
            $sp.Write($data, $o, [math]::Min($Block, $Bytes - $o)); $sent = $o + $Block
            if ($SleepMs -gt 0) { Start-Sleep -Milliseconds $SleepMs }
        }
    } catch { "write stalled after $sent bytes: $($_.Exception.Message)" }
    $dt = ((Get-Date) - $t0).TotalSeconds
    $buf = ''; $deadline = (Get-Date).AddSeconds(50); $next = (Get-Date).AddSeconds(1)
    while ((Get-Date) -lt $deadline -and $buf -notmatch 'PT_END') {
        Start-Sleep -Milliseconds 100; $buf += $sp.ReadExisting()
        if ((Get-Date) -gt $next -and $sent -ge $Bytes) { $sp.Write("`n"); $next = (Get-Date).AddSeconds(1) }
    }
    $got = if ($buf -match '([0-9a-f]{32})') { $Matches[1] } else { 'none' }
    '{0} block={1} sleep={2}ms sent={3} in {4:N1}s ({5:N0} KiB/s) md5 {6}' -f ($(if ($got -eq $want) {'OK'} else {'FAIL'})), $Block, $SleepMs, $sent, $dt, ($sent / 1KB / [math]::Max($dt, 0.01)), $got
}
finally { $sp.Close() }
