[CmdletBinding()]
param(
    [ValidateRange(30, 600)]
    [int]$WaitSeconds = 180
)

$ErrorActionPreference = 'Stop'

$workspace = Split-Path -Parent $PSScriptRoot
$stage = [IO.Path]::GetFullPath('C:\Users\Public\MTK_B8000H')
if ($stage -ne 'C:\Users\Public\MTK_B8000H') {
    throw "Unexpected staging path: $stage"
}

$spftSource = Get-ChildItem -LiteralPath (Join-Path $workspace 'artifacts\stock\extracted') `
    -Recurse -Directory -Filter 'SP_Flash_Tool_v3.1328.0.183' |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $spftSource) {
    throw 'SP Flash Tool package was not found in artifacts\stock\extracted.'
}

$spft = Join-Path $stage 'spft'
$configs = Join-Path $stage 'configs'
$backups = Join-Path $stage 'backups'
New-Item -ItemType Directory -Path $stage, $configs, $backups -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $spft 'Flash_tool.exe'))) {
    Copy-Item -LiteralPath $spftSource -Destination $spft -Recurse
}

$configNames = @('readback-probe-mbr-1.xml', 'readback-probe-mbr-2.xml')
foreach ($name in $configNames) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $configs $name) -Force
}

$outputs = @(
    (Join-Path $backups 'probe-mbr-1.bin'),
    (Join-Path $backups 'probe-mbr-2.bin')
)
foreach ($output in $outputs) {
    if (Test-Path -LiteralPath $output) {
        throw "Refusing to overwrite existing readback: $output"
    }
}

function Wait-MtkPreloader {
    param([int]$Timeout)

    $deadline = (Get-Date).AddSeconds($Timeout)
    while ((Get-Date) -lt $deadline) {
        $device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -like 'USB\VID_0E8D&PID_2000*' } |
            Select-Object -First 1
        if ($device) {
            Write-Host ("Detected {0} at {1}" -f $device.FriendlyName, (Get-Date -Format 'HH:mm:ss.fff'))
            return
        }
        Start-Sleep -Milliseconds 50
    }
    throw "MediaTek preloader did not appear within $Timeout seconds."
}

function Invoke-Readback {
    param(
        [string]$ConfigName,
        [string]$OutputPath,
        [int]$Number
    )

    Write-Host ""
    Write-Host "Pass ${Number}: disconnect the USB cable, power the tablet fully off, then reconnect it."
    Wait-MtkPreloader -Timeout $WaitSeconds

    $stdout = Join-Path $backups "spft-probe-$Number.stdout.txt"
    $stderr = Join-Path $backups "spft-probe-$Number.stderr.txt"
    $process = Start-Process -FilePath (Join-Path $spft 'Flash_tool.exe') `
        -ArgumentList @('-i', (Join-Path $configs $ConfigName), '-v') `
        -WorkingDirectory $spft `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -PassThru -Wait

    if (Test-Path -LiteralPath $stdout) {
        Get-Content -LiteralPath $stdout
    }
    if ((Test-Path -LiteralPath $stderr) -and (Get-Item -LiteralPath $stderr).Length) {
        Get-Content -LiteralPath $stderr
    }
    if ($process.ExitCode -ne 0) {
        throw "SP Flash Tool exited with code $($process.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $OutputPath)) {
        throw "SP Flash Tool did not create $OutputPath."
    }
    $length = (Get-Item -LiteralPath $OutputPath).Length
    if ($length -ne 0x80000) {
        throw "Unexpected readback size: $length bytes (expected 524288)."
    }
}

Write-Host 'READ-ONLY TEST: EMMC offset 0x00600000, length 0x00080000.'
Write-Host 'The XML contains no download, format, or erase operation.'
Invoke-Readback -ConfigName $configNames[0] -OutputPath $outputs[0] -Number 1
Invoke-Readback -ConfigName $configNames[1] -OutputPath $outputs[1] -Number 2

$hashes = $outputs | ForEach-Object { Get-FileHash -Algorithm SHA256 -LiteralPath $_ }
$hashes | Format-Table Path, Hash -AutoSize
if ($hashes[0].Hash -ne $hashes[1].Hash) {
    throw 'The two readbacks differ. Stop here; do not flash.'
}

Write-Host 'PASS: both 512 KiB readbacks are byte-identical.'
