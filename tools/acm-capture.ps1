[CmdletBinding()]
param(
    [int]$Minutes = 15,
    [string]$Port = 'COM11',
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\linux-port\logs')
)
# Read-only capture of the B8000H-LINUX ACM console. Never writes to the port:
# host->tablet traffic currently wedges the musb controller.
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$deadline = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $deadline) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) {
        Start-Sleep -Milliseconds 500
        continue
    }
    $file = Join-Path $OutDir ("acm-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Write-Output "CAPTURE $file"
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200
    try {
        $sp.Open()
        $sp.DtrEnable = $true
        while ((Get-Date) -lt $deadline -and $sp.IsOpen) {
            $chunk = $sp.ReadExisting()
            if ($chunk) {
                [IO.File]::AppendAllText($file, $chunk)
                if ($chunk -match '=== END REPORT (\d+) ===') { Write-Output "REPORT $($Matches[1]) captured" }
            }
            Start-Sleep -Milliseconds 200
        }
    }
    catch {
        Write-Output "port closed: $($_.Exception.Message)"
    }
    finally {
        if ($sp.IsOpen) { $sp.Close() }
    }
}
