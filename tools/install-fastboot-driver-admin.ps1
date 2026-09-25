[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent()
)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell window (Run as administrator).'
}

$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -eq 'USB\VID_0BB4&PID_0C01\MT8389_TABLET_S3' } |
    Select-Object -First 1
if (-not $device) {
    throw 'Lenovo MT8389 fastboot device (USB\VID_0BB4&PID_0C01) is not connected.'
}

$wantedTitle = 'TP-LINK - Other hardware - Android Bootloader Interface'
$session = New-Object -ComObject Microsoft.Update.Session
$searcher = $session.CreateUpdateSearcher()
$result = $searcher.Search("IsInstalled=0 and Type='Driver'")
$update = $null
for ($i = 0; $i -lt $result.Updates.Count; $i++) {
    $candidate = $result.Updates.Item($i)
    if ($candidate.Title -eq $wantedTitle -and
        $candidate.DriverModel -eq 'Android Bootloader Interface') {
        $update = $candidate
        break
    }
}
if (-not $update) {
    throw "Applicable Windows Update driver was not found: $wantedTitle"
}

Write-Host "Found: $($update.Title)"
Write-Host "Driver date: $($update.DriverVerDate)"
if (-not $update.EulaAccepted) {
    $update.AcceptEula()
}

$selection = New-Object -ComObject Microsoft.Update.UpdateColl
[void]$selection.Add($update)
if (-not $update.IsDownloaded) {
    $downloader = $session.CreateUpdateDownloader()
    $downloader.Updates = $selection
    $downloadResult = $downloader.Download()
    if ($downloadResult.ResultCode -notin 2, 3) {
        throw "Driver download failed with result code $($downloadResult.ResultCode)."
    }
}

$installer = $session.CreateUpdateInstaller()
$installer.Updates = $selection
$installResult = $installer.Install()
Write-Host "Install result code: $($installResult.ResultCode)"
Write-Host "Reboot required: $($installResult.RebootRequired)"
if ($installResult.ResultCode -notin 2, 3) {
    throw "Driver installation failed with result code $($installResult.ResultCode)."
}

Start-Sleep -Seconds 2
$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -eq 'USB\VID_0BB4&PID_0C01\MT8389_TABLET_S3' } |
    Select-Object -First 1
$device | Select-Object Status, Class, FriendlyName, InstanceId, Problem | Format-List
if ($device.Status -ne 'OK') {
    throw 'The package installed, but the fastboot device is not in OK state. Reconnect it once.'
}

Write-Host 'Fastboot driver installed. Return to the verification step.'
