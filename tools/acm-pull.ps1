[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Remote,
    [Parameter(Mandatory)][string]$Local,
    [string]$Port = 'COM11',
    [int]$Chunk = 8192
)
# Copies a small file from the B8000-H Linux to the PC over the USB ACM
# console: base64 per chunk with an md5 per chunk, retried until it matches.
# For screenshots and logs (tens of KiB to a few MiB) when Wi-Fi is down.
$ErrorActionPreference = 'Stop'
$shell = Join-Path $PSScriptRoot 'acm-shell.ps1'
$size = [int64]((& $shell -Port $Port -WaitMs 8000 -Command "stat -c %s '$Remote'").Trim())
$n = [math]::Ceiling($size / $Chunk)
$data = New-Object System.IO.MemoryStream
for ($i = 0; $i -lt $n; $i++) {
    for ($try = 1; ; $try++) {
        $cmd = "dd if='$Remote' bs=$Chunk skip=$i count=1 2>/dev/null | base64 -w0; echo; " +
               "dd if='$Remote' bs=$Chunk skip=$i count=1 2>/dev/null | md5sum"
        $out = & $shell -Port $Port -WaitMs 20000 -Command $cmd | Out-String
        $lines = $out -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ }
        $md5 = ($lines | Where-Object { $_ -match '^[0-9a-f]{32}\s+-$' } | Select-Object -First 1) -replace '\s.*$', ''
        $b64 = ($lines | Where-Object { $_ -match '^[A-Za-z0-9+/]+=*$' -and $_ -notmatch '^[0-9a-f]{32}$' }) -join ''
        try { $bytes = [Convert]::FromBase64String($b64) } catch { $bytes = @() }
        $got = ([BitConverter]::ToString(
            [Security.Cryptography.MD5]::Create().ComputeHash([byte[]]$bytes)) -replace '-', '').ToLower()
        if ($md5 -and $got -eq $md5) { $data.Write($bytes, 0, $bytes.Length); break }
        if ($try -ge 5) { throw "chunk $i failed after $try tries" }
    }
}
[IO.File]::WriteAllBytes($Local, $data.ToArray())
"$Local : $($data.Length) of $size bytes"
