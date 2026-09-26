[CmdletBinding()]
param(
    [string]$Image = (Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\linux-port\b8000h-sd.img')
)
# Writes the B8000-H microSD image to a removable card in a USB card reader.
# Run from an elevated PowerShell (Run as administrator). Refuses anything that
# is not a removable/USB/SD disk between 4 and 64 GB, and asks for confirmation.

$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell window (Run as administrator).'
}
$imageItem = Get-Item -LiteralPath $Image
Write-Host "Image: $($imageItem.FullName) ($([math]::Round($imageItem.Length / 1MB)) MB)"

$candidates = Get-Disk | Where-Object {
    $_.BusType -in 'USB', 'SD', 'MMC' -and
    $_.Size -ge 4GB -and $_.Size -le 64GB -and
    -not $_.IsBoot -and -not $_.IsSystem
}
if (-not $candidates) {
    Get-Disk | Format-Table Number, FriendlyName, BusType, @{N='GB';E={[math]::Round($_.Size/1GB,1)}}, IsBoot, IsSystem
    throw 'No removable 4-64 GB card found. Insert the microSD into the card reader and retry.'
}
if (@($candidates).Count -gt 1) {
    $candidates | Format-Table Number, FriendlyName, BusType, @{N='GB';E={[math]::Round($_.Size/1GB,1)}}
    throw 'More than one candidate disk. Leave only the microSD card reader connected.'
}
$disk = $candidates
if ($imageItem.Length -gt $disk.Size) { throw 'Image is larger than the card.' }

Write-Host ''
Write-Host "TARGET: disk $($disk.Number) '$($disk.FriendlyName)' bus=$($disk.BusType) size=$([math]::Round($disk.Size/1GB,1)) GB" -ForegroundColor Yellow
Write-Host 'EVERYTHING ON THIS CARD WILL BE ERASED.' -ForegroundColor Yellow
$answer = Read-Host 'Type YES to write the image'
if ($answer -ne 'YES') { throw 'Cancelled.' }

# Take the card's volumes offline so Windows does not hold them open.
Set-Disk -Number $disk.Number -IsReadOnly $false
Clear-Disk -Number $disk.Number -RemoveData -RemoveOEM -Confirm:$false -ErrorAction SilentlyContinue
Set-Disk -Number $disk.Number -IsOffline $true

$src = [IO.File]::OpenRead($imageItem.FullName)
$dst = [IO.FileStream]::new("\\.\PhysicalDrive$($disk.Number)", [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::ReadWrite, 4MB, [IO.FileOptions]::WriteThrough)
try {
    $buf = [byte[]]::new(4MB)
    $done = 0L
    while (($n = $src.Read($buf, 0, $buf.Length)) -gt 0) {
        if ($n % 512) { [Array]::Clear($buf, $n, 512 - ($n % 512)); $n += 512 - ($n % 512) }
        $dst.Write($buf, 0, $n)
        $done += $n
        Write-Progress -Activity 'Writing microSD' -PercentComplete ([int](100 * $done / $imageItem.Length))
    }
    $dst.Flush()
}
finally {
    $dst.Dispose(); $src.Dispose()
}
Set-Disk -Number $disk.Number -IsOffline $false
Write-Host 'Done. Remove the card and insert it into the tablet.' -ForegroundColor Green
