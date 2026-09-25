[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Command,
    [string]$Port = 'COM11',
    [int]$WaitMs = 4000
)
# Runs shell commands on the B8000-H Linux over the USB ACM console
# (non-interactive `cat /dev/ttyGS0 | sh` on the tablet) and prints the output.
$ErrorActionPreference = 'Stop'
$marker = "__END_$(Get-Random)__"
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.WriteTimeout = 3000
$sp.Open()
try {
    $sp.DtrEnable = $true
    Start-Sleep -Milliseconds 300
    [void]$sp.ReadExisting()
    $sp.Write("$Command 2>&1; echo $marker`n")
    $buf = ''
    $deadline = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $deadline -and -not $buf.Contains($marker)) {
        Start-Sleep -Milliseconds 100
        $buf += $sp.ReadExisting()
    }
    $buf.Replace($marker, '').TrimEnd()
}
finally {
    $sp.Close()
}
