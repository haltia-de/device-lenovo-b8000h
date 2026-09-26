[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$WslPath,   # file inside WSL B8000Build
    [Parameter(Mandatory)] [string]$Dest,      # destination path on the tablet
    [string]$Port = 'COM11',
    [int]$Chunk = 600
)
# Push a small file (up to ~100 KiB) to the tablet over the ACM shell as
# gzip+base64 in short lines; large host->device writes hang musb PIO.
$ErrorActionPreference = 'Stop'
$shell = Join-Path $PSScriptRoot 'acm-shell.ps1'
$b64 = (wsl.exe -d B8000Build -- sh -c "gzip -9c '$WslPath' | base64 -w0").Trim()
$md5 = ((wsl.exe -d B8000Build -- md5sum $WslPath) -split ' ')[0]
& $shell -Port $Port -WaitMs 2000 -Command "rm -f /tmp/push.b64" | Out-Null
for ($i = 0; $i -lt $b64.Length; $i += $Chunk) {
    $c = $b64.Substring($i, [Math]::Min($Chunk, $b64.Length - $i))
    & $shell -Port $Port -WaitMs 1200 -Command "echo -n '$c' >> /tmp/push.b64" | Out-Null
}
$reply = & $shell -Port $Port -WaitMs 8000 -Command "base64 -d /tmp/push.b64 | gunzip > '$Dest' && chmod +x '$Dest'; md5sum '$Dest'"
if ($reply -notmatch $md5) { throw "md5 mismatch for ${Dest}: $reply" }
Write-Output "OK $Dest $md5"
