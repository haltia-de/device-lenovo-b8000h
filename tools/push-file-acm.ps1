[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Path,   # local (Windows) file
    [Parameter(Mandatory)] [string]$Dest,   # destination path on the tablet
    [string]$Port = 'COM10',
    [int]$ChunkKiB = 256
)
# Push a file of any size to the running tablet over the USB ACM shell
# (/usr/local/bin/acm-shell: raw tty, "cat ttyGS0 | sh"). Per chunk: gzip on
# the host, "head -c N | gunzip >> DEST" on the tablet, binary payload sent
# after the command line has been read. md5 of the whole file is compared.
$ErrorActionPreference = 'Stop'
$data = [IO.File]::ReadAllBytes((Resolve-Path $Path))
$md5 = [Security.Cryptography.MD5]::Create()
$want = ([BitConverter]::ToString($md5.ComputeHash($data)) -replace '-', '').ToLower()

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.WriteTimeout = 20000
$sp.ReadTimeout = 500
$sp.Encoding = [Text.Encoding]::ASCII
$sp.Open()
$sp.DtrEnable = $true
$buf = New-Object Text.StringBuilder

function Read-Until([string]$marker, [int]$ms) {
    $deadline = (Get-Date).AddMilliseconds($ms)
    $next = (Get-Date).AddMilliseconds(1500)
    while ((Get-Date) -lt $deadline) {
        # musb PIO may hold IN data until the next OUT packet: nudge.
        if ((Get-Date) -gt $next) { $sp.Write("`n"); $next = (Get-Date).AddMilliseconds(1500) }
        $null = $buf.Append($sp.ReadExisting())
        $s = $buf.ToString()
        $i = $s.IndexOf($marker)
        if ($i -ge 0) {
            $out = $s.Substring(0, $i)
            $null = $buf.Remove(0, $i + $marker.Length)
            return $out
        }
        Start-Sleep -Milliseconds 20
    }
    throw "Timeout waiting for $marker. Got: $($buf.ToString())"
}

try {
    Start-Sleep -Milliseconds 300
    $null = $sp.ReadExisting()
    $tmp = "$Dest.part"
    $sp.Write("rm -f '$tmp'; echo R_EADY`n")
    $null = Read-Until 'R_EADY' 15000
    $chunk = $ChunkKiB * 1KB
    $t0 = Get-Date
    for ($o = 0; $o -lt $data.Length; $o += $chunk) {
        $len = [math]::Min($chunk, $data.Length - $o)
        $ms = New-Object IO.MemoryStream
        $gz = New-Object IO.Compression.GZipStream($ms, [IO.Compression.CompressionLevel]::Optimal, $true)
        $gz.Write($data, $o, $len); $gz.Dispose()
        $gzb = $ms.ToArray()
        $tag = "C$o"
        # RDP COM redirection may merge the command line with the payload:
        # only send the payload after the tablet says head is about to read.
        $sp.Write("echo G$tag`_; timeout 60 head -c $($gzb.Length) | gunzip >> '$tmp'; wc -c < '$tmp'; echo $tag`_`n")
        $null = Read-Until "G$tag`_" 15000
        Start-Sleep -Milliseconds 150
        for ($p = 0; $p -lt $gzb.Length; $p += 4096) {
            $sp.Write($gzb, $p, [math]::Min(4096, $gzb.Length - $p))
        }
        $got = (Read-Until "$tag`_" 120000).Trim()
        if ($got -notmatch "(^|\s)$($o + $len)(\s|$)") { throw "chunk at $o failed: '$got'" }
    }
    $sp.Write("mv '$tmp' '$Dest' && md5sum '$Dest'; echo M_D5`n")
    $got = Read-Until 'M_D5' 30000
    if ($got -notmatch $want) { throw "md5 mismatch: $got (want $want)" }
    Write-Output ("OK {0} {1} bytes md5 {2} in {3:N0}s" -f $Dest, $data.Length, $want, ((Get-Date) - $t0).TotalSeconds)
}
finally {
    $sp.Close()
}
