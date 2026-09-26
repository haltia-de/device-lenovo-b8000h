[CmdletBinding()]
param(
    [int]$Minutes = 10,
    [string]$Config = 'C:\Users\Public\MTK_B8000H\configs\download-userdata-raw-idx20.xml',
    [string]$Image = 'C:\Users\Public\MTK_B8000H\flash-userdata\b8000h-phosh-emmc-root.ext4',
    [string]$ExpectedHash = '7E1CEB0D95F25ADFB653B4AEA18C0BFB5273FD602355392421D8EA3191D5E54E'
)
# Writes only ROM index 20 (USRDATA) as a normal raw image. It never formats
# eMMC and does not enable BOOT, RECOVERY, NVRAM or the partition table.
$ErrorActionPreference = 'Stop'
$spft = 'C:\Users\Public\MTK_B8000H\spft-v5'
$expectedBytes = 1073741824L
$item = Get-Item -LiteralPath $Image
if ($item.Length -ne $expectedBytes) { throw "Unexpected raw image size: $($item.Length)" }
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $Image).Hash -ne $ExpectedHash) {
    throw 'Raw image hash mismatch'
}
$xml = Get-Content -LiteralPath $Config -Raw
$enabled = [regex]::Matches($xml, '<rom index="(\d+)" enable="true">')
if ($enabled.Count -ne 1 -or $enabled[0].Groups[1].Value -ne '20') {
    throw 'Config must enable exactly ROM index 20 (USRDATA)'
}
if ($xml -match '<format|<erase') { throw 'Format/erase command is forbidden' }

$logDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\linux-port\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stdout = Join-Path $logDir 'dl-userdata-raw.stdout.txt'
$stderr = "$stdout.err"
$deadline = (Get-Date).AddMinutes($Minutes)
$attempt = 0
while ((Get-Date) -lt $deadline) {
    $attempt++
    Write-Output "ATTEMPT $attempt $(Get-Date -Format HH:mm:ss)"
    $p = Start-Process "$spft\flash_tool.exe" -ArgumentList '-i', $Config `
        -WorkingDirectory $spft -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr -PassThru -Wait
    $text = Get-Content -LiteralPath $stdout -Raw
    if ($text -match 'Scaning USB port failed') { continue }

    $qt = Get-ChildItem 'C:\ProgramData\SP_FT_Logs' -Directory |
        Sort-Object LastWriteTime | Select-Object -Last 1
    $qtLog = Join-Path $qt.FullName 'QT_FLASH_TOOL.log'
    $daConnected = (Test-Path $qtLog -PathType Leaf) -and
        (Select-String -LiteralPath $qtLog -SimpleMatch 'DA Connected' -Quiet)
    if ($p.ExitCode -ne 0 -and -not $daConnected) {
        Write-Output "retry: failed before DA connect ($($qt.Name))"
        continue
    }
    Write-Output $text
    Write-Output "SPFT_DONE exit=$($p.ExitCode) log=$qtLog"
    exit $p.ExitCode
}
Write-Output 'SPFT_TIMEOUT: tablet was not detected'
exit 3
