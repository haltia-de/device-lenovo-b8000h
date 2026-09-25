[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Command,
    [string]$Port = 'COM11',
    [int]$WaitMs = 3000
)
# Runs one shell command over the B8000H-LINUX USB ACM console and prints the reply.
$ErrorActionPreference = 'Stop'
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.ReadTimeout = 500
$sp.Open()
try {
    Start-Sleep -Milliseconds 300
    [void]$sp.ReadExisting()
    $sp.Write("$Command; echo __END__`n")
    $buf = ''
    $deadline = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $deadline -and $buf -notmatch '(?m)^__END__') {
        Start-Sleep -Milliseconds 100
        $buf += $sp.ReadExisting()
    }
    $buf
}
finally {
    $sp.Close()
}
