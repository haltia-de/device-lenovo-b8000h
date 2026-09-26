[CmdletBinding()]
param(
    [string]$Port = 'COM11',
    [string]$Image = 'C:\Users\Public\MTK_B8000H\flash-userdata\b8000h-phosh-emmc-root.ext4.gz',
    [string]$ExpectedImageHash = '34E98964BCD8C4CBD8163688F6BDB3B045F859E80363205B4234E14E3A34B56F',
    [string]$ExpectedRawHash = '7E1CEB0D95F25ADFB653B4AEA18C0BFB5273FD602355392421D8EA3191D5E54E'
)
# Streams a gzip-compressed 1 GiB ext4 image through the proven ACM PIO
# channel. The tablet decompresses directly into only the live-verified
# USRDATA block device, then hashes the written 1 GiB back from eMMC.
$ErrorActionPreference = 'Stop'
$expectedBytes = 288431371L
$expectedSectors = '27589888'
$item = Get-Item -LiteralPath $Image
if ($item.Length -ne $expectedBytes) { throw "Unexpected image size: $($item.Length)" }
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $Image).Hash -ne $ExpectedImageHash) {
    throw 'Image hash mismatch'
}

$sp = [IO.Ports.SerialPort]::new($Port, 115200)
$sp.ReadTimeout = 1000
$sp.WriteTimeout = 30000
$sp.Open()
$sp.DtrEnable = $true
try {
    Start-Sleep -Milliseconds 300
    [void]$sp.ReadExisting()
    $command = ('BB=/bin/busybox; T=/dev/mmcblk1p20; S=$($BB cat /sys/class/block/mmcblk1p20/size 2>/dev/null); ' +
        '[ "$S" = {0} ] || {{ echo __BAD_TARGET__:$S; exit; }}; ' +
        '[ -x "$BB" ] || {{ echo __NO_BUSYBOX__; exit; }}; ' +
        'echo __WRITE_READY__; $BB head -c {1} | $BB gzip -dc | ' +
        '$BB dd of=$T bs=65536 count=16384 iflag=fullblock conv=fsync; ' +
        'echo __WRITE_DONE__; $BB dd if=$T bs=1048576 count=1024 iflag=fullblock 2>/dev/null | ' +
        '$BB sha256sum; ' +
        'echo __VERIFY_DONE__' -f $expectedSectors, $expectedBytes) + "`n"
    $sp.Write($command)

    $reply = ''
    $readyDeadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $readyDeadline -and $reply -notmatch '__WRITE_READY__') {
        Start-Sleep -Milliseconds 100
        $reply += $sp.ReadExisting()
        if ($reply -match '__BAD_TARGET__') { throw "Tablet rejected target: $reply" }
        if ($reply -match '__NO_BUSYBOX__') { throw "Tablet initramfs lacks BusyBox: $reply" }
    }
    if ($reply -notmatch '__WRITE_READY__') { throw "Tablet did not become ready: $reply" }

    $src = [IO.File]::OpenRead($item.FullName)
    try {
        $buffer = [byte[]]::new(65536)
        $done = 0L
        $lastPercent = -1
        while (($count = $src.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $sp.BaseStream.Write($buffer, 0, $count)
            $done += $count
            $percent = [int](100 * $done / $item.Length)
            if ($percent -ge $lastPercent + 5) {
                Write-Output "WRITE $percent% ($done/$($item.Length))"
                $lastPercent = $percent
            }
        }
        $sp.BaseStream.Flush()
    }
    finally {
        $src.Dispose()
    }

    $verifyDeadline = (Get-Date).AddMinutes(20)
    while ((Get-Date) -lt $verifyDeadline -and $reply -notmatch '__VERIFY_DONE__') {
        Start-Sleep -Milliseconds 200
        $reply += $sp.ReadExisting()
    }
    Write-Output $reply
    if ($reply -notmatch '__WRITE_DONE__') { throw 'Tablet did not finish dd' }
    if ($reply -notmatch [regex]::Escape($ExpectedRawHash.ToLowerInvariant())) {
        throw 'Tablet read-back SHA256 did not match'
    }
    if ($reply -notmatch '__VERIFY_DONE__') { throw 'Tablet verification timed out' }
    Write-Output 'PASS: eMMC USRDATA contains the verified Phosh ext4 image'
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}
