[CmdletBinding()]
param(
    [int]$Seconds = 30
)

$ErrorActionPreference = 'Stop'
$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
$last = ''
Write-Host "Watching MediaTek USB states for $Seconds seconds..."

while ([DateTime]::UtcNow -lt $deadline) {
    $devices = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -match 'VID_0E8D' -or
            $_.FriendlyName -match 'MediaTek|MT65|PreLoader|VCOM'
        } |
        Select-Object Status, Class, FriendlyName, InstanceId
    $snapshot = ($devices | ConvertTo-Json -Compress)
    if ($snapshot -ne $last) {
        Write-Host "[$(Get-Date -Format 'HH:mm:ss.fff')]"
        if ($devices) {
            $devices | Format-Table -AutoSize
        } else {
            Write-Host 'No MediaTek USB device present.'
        }
        $last = $snapshot
    }
    Start-Sleep -Milliseconds 150
}
