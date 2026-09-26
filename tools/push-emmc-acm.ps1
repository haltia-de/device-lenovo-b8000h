[CmdletBinding()]
param(
    [string]$Image = 'C:\Users\Public\MTK_B8000H\flash-userdata\b8000h-phosh-emmc-root.ext4',
    [string]$ExpectedHash = 'EFC184D030EC245232D7576989270D25A810B0163411F65AF07CD9D2C3ADAD6E',
    [string]$Target = '/dev/mmcblk0p20',
    [long]$TargetBytes = 14126022656,
    [int]$ChunkMiB = 4,
    [int]$StartChunk = 0,
    [int]$MaxChunks = 0,
    [string]$Port = 'COM11'
)
# Writes a raw ext4 image into the eMMC USRDATA partition from the running
# B8000-H Linux over the USB ACM console: per chunk gzip -> head -c -> gunzip
# -> dd seek, then read back and compare md5. The next chunk is only sent after
# the tablet printed its "ready" marker, so no data reaches the shell itself.

$ErrorActionPreference = 'Stop'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $Image).Hash -ne $ExpectedHash) { throw 'Image hash mismatch' }
$size = (Get-Item -LiteralPath $Image).Length
$chunk = $ChunkMiB * 1MB
$count = [int][math]::Ceiling($size / $chunk)
$bs = 65536
$md5 = [Security.Cryptography.MD5]::Create()

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.WriteTimeout = 20000
$sp.ReadTimeout = 500
$sp.Encoding = [Text.Encoding]::ASCII
$sp.Open()
$sp.DtrEnable = $true
$buf = New-Object Text.StringBuilder

function Read-Until([string]$marker, [int]$ms, [switch]$Nudge) {
    # The musb PIO gadget sometimes holds IN data until the next OUT packet;
    # an empty line to the shell releases it. Only nudge when no binary
    # payload can still be in flight.
    $deadline = (Get-Date).AddMilliseconds($ms)
    $next = (Get-Date).AddMilliseconds(1000)
    while ((Get-Date) -lt $deadline) {
        if ($Nudge -and (Get-Date) -gt $next) { $sp.Write("`n"); $next = (Get-Date).AddMilliseconds(1000) }
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
function Send-Line([string]$line) { $sp.Write("$line`n") }

try {
    Start-Sleep -Milliseconds 300
    $null = $sp.ReadExisting()
    # The initramfs console is "cat ttyGS0 | tee /dev/console | sh"; tee pushes
    # every payload byte through fbcon and stalls USB. Replace it by a plain
    # "cat | sh" (kill the respawn loop first, then the old pipeline).
    Send-Line 'if pidof tee >/dev/null; then C=$(pidof cat | tr " " "\n" | head -n1); L=$(cut -d" " -f4 /proc/$C/stat); setsid sh -c "sleep 2; exec sh -c \"cat /dev/ttyGS0 | sh >/dev/ttyGS0 2>&1\"" </dev/null >/dev/null 2>&1 & kill $L; kill $(pidof tee) $C; fi'
    Start-Sleep -Seconds 4
    $null = $sp.ReadExisting()
    # Sync and verify the target from the tablet side.
    Send-Line "echo SYNC_; cat /sys/class/block/mmcblk0p20/size; cat /sys/class/block/mmcblk0p20/ro; grep -c USRDATA /sys/class/block/mmcblk0p20/uevent; echo _SYNC"
    $null = Read-Until 'SYNC_' 15000 -Nudge
    $info = (Read-Until '_SYNC' 15000 -Nudge).Trim() -split '\s+'
    if ([long]$info[0] * 512 -ne $TargetBytes -or $info[1] -ne '0' -or $info[2] -ne '1') {
        throw "Target check failed: $($info -join ' ')"
    }
    Write-Output "target ok: $Target $TargetBytes bytes rw USRDATA; chunks=$count x $ChunkMiB MiB"

    $src = [IO.File]::OpenRead($Image)
    $last = if ($MaxChunks -gt 0) { [math]::Min($count, $StartChunk + $MaxChunks) } else { $count }
    $t0 = Get-Date
    for ($i = $StartChunk; $i -lt $last; $i++) {
        $src.Position = [long]$i * $chunk
        $data = New-Object byte[] ([math]::Min($chunk, $size - $src.Position))
        $n = 0; while ($n -lt $data.Length) { $n += $src.Read($data, $n, $data.Length - $n) }
        $want = ([BitConverter]::ToString($md5.ComputeHash($data)) -replace '-', '').ToLower()
        $ms = New-Object IO.MemoryStream
        $gz = New-Object IO.Compression.GZipStream($ms, [IO.Compression.CompressionLevel]::Optimal, $true)
        $gz.Write($data, 0, $data.Length); $gz.Dispose()
        $gzb = $ms.ToArray()
        $seek = [long]$i * $chunk / $bs
        $blocks = [int][math]::Ceiling($data.Length / $bs)
        for ($try = 1; $try -le 3; $try++) {
            Send-Line "timeout 60 head -c $($gzb.Length) | gunzip | dd of=$Target bs=$bs seek=$seek conv=notrunc,fsync 2>/dev/null; dd if=$Target bs=$bs skip=$seek count=$blocks 2>/dev/null | head -c $($data.Length) | md5sum; echo E$i`_"
            # Let the shell read the line and start head before any payload.
            Start-Sleep -Milliseconds 400
            for ($o = 0; $o -lt $gzb.Length; $o += 4096) {
                $sp.Write($gzb, $o, [math]::Min(4096, $gzb.Length - $o))
            }
            $got = (Read-Until "E$i`_" 120000 -Nudge).Trim()
            if ($got -match '([0-9a-f]{32})' -and $Matches[1] -eq $want) { break }
            Write-Output "chunk $i try $try md5 mismatch: '$got'"
            if ($try -eq 3) { throw "chunk $i failed" }
        }
        $el = ((Get-Date) - $t0).TotalSeconds
        $done = $i - $StartChunk + 1
        Write-Output ("chunk {0}/{1} ok gz={2}KiB  {3:N0}s elapsed, eta {4:N0}s" -f ($i + 1), $count, [int]($gzb.Length / 1KB), $el, ($el / $done * ($last - $i - 1)))
    }
    $src.Dispose()
    Write-Output 'ALL_DONE'
}
finally {
    $sp.Close()
}
