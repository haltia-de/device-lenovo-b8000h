[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent()
)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell window (Run as administrator).'
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$driverRoot = Join-Path $projectRoot 'artifacts\drivers\mediatek-preloader-vcom-win10'
$cab = Join-Path $projectRoot 'artifacts\drivers\mediatek-preloader-vcom-win10.cab'
$inf = Join-Path $driverRoot 'cdc-acm.inf'
$catalog = Join-Path $driverRoot 'cdc-acm.cat'
$expectedHash = 'CE72E97C07582AD41DEBFAEEE6F6284ECD28285DF5F9CAA05193653FB236561F'

if ((Get-FileHash -Algorithm SHA256 -LiteralPath $cab).Hash -ne $expectedHash) {
    throw 'The Microsoft Update Catalog CAB hash does not match the pinned value.'
}
if ((Get-AuthenticodeSignature -LiteralPath $catalog).Status -ne 'Valid') {
    throw 'The extracted driver catalog signature is not valid.'
}
if (-not (Select-String -LiteralPath $inf -SimpleMatch 'USB\Vid_0e8d&Pid_2000' -Quiet)) {
    throw 'The driver INF does not match the tablet preloader hardware ID.'
}

& pnputil.exe /add-driver $inf /install
if ($LASTEXITCODE -ne 0) {
    throw "pnputil failed with exit code $LASTEXITCODE"
}

Write-Host 'MediaTek preloader driver added. Reconnect the powered-off tablet and run watch-mtk.ps1.'

