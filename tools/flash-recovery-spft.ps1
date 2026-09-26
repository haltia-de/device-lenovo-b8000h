[CmdletBinding()]
param(
    [int]$Minutes = 10,
    [string]$Config = 'C:\Users\Public\MTK_B8000H\configs\download-recovery-idx11.xml',
    [string]$Image = 'C:\Users\Public\MTK_B8000H\flash-recovery\recovery.img',
    [string]$ExpectedHash = 'F4EF22FFA2A19828E57EBB54C0FDF01CA2158EEFD1C9F288675668071B028D92'
)
# Writes ONLY the RECOVERY partition (SPFT rom index 11, verified by a dry run:
# index 11 = RECOVERY @ 0x2D80000; index 5 would be __NODL_NVRAM).
# Restarts SP Flash Tool while it times out scanning for the BROM/preloader port.

$ErrorActionPreference = 'Stop'
$spft = 'C:\Users\Public\MTK_B8000H\spft-v5'
$logDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\linux-port\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'dl-recovery.stdout.txt'

if ((Get-FileHash -Algorithm SHA256 -LiteralPath $Image).Hash -ne $ExpectedHash) {
    throw 'Image hash mismatch'
}
if (-not (Select-String -LiteralPath $Config -SimpleMatch '<rom index="11" enable="true">' -Quiet)) {
    throw 'Config does not target rom index 11 (RECOVERY)'
}

$deadline = (Get-Date).AddMinutes($Minutes)
$attempt = 0
while ((Get-Date) -lt $deadline) {
    $attempt++
    Write-Output "ATTEMPT $attempt $(Get-Date -Format HH:mm:ss)"
    $p = Start-Process "$spft\flash_tool.exe" -ArgumentList '-i', $Config -WorkingDirectory $spft `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -Wait
    $text = Get-Content -LiteralPath $log -Raw
    if ($text -match 'Scaning USB port failed') { continue }
    # Failures before the DA is connected cannot have written anything (e.g. SPFT
    # grabbed the short BROM port instead of the preloader) - just retry.
    $qt = Get-ChildItem C:\ProgramData\SP_FT_Logs -Directory | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($p.ExitCode -ne 0 -and -not (Select-String -Path "$($qt.FullName)\QT_FLASH_TOOL.log" -SimpleMatch 'DA Connected' -Quiet)) {
        Write-Output "retry: failed before DA connect ($($qt.Name))"
        continue
    }
    Write-Output $text
    Write-Output "SPFT_DONE exit=$($p.ExitCode)"
    exit $p.ExitCode
}
Write-Output 'SPFT_TIMEOUT: tablet was not detected'
exit 3
