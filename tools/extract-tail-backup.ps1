[CmdletBinding()]
param(
    [string]$Source = 'C:\Users\Public\MTK_B8000H\backups\tail-recovery-through-expdb.bin',
    [string]$Destination = 'C:\Users\Public\MTK_B8000H\backups\partitions'
)

$ErrorActionPreference = 'Stop'
$regions = @(
    @{ Name = 'boot.img';     Offset = 0x0000000L; Length = 0x0600000L },
    @{ Name = 'recovery.img'; Offset = 0x0600000L; Length = 0x0600000L },
    @{ Name = 'sec_ro.img';   Offset = 0x0C00000L; Length = 0x0600000L },
    @{ Name = 'misc.bin';     Offset = 0x1200000L; Length = 0x0080000L },
    @{ Name = 'logo.bin';     Offset = 0x1280000L; Length = 0x0300000L },
    @{ Name = 'ebr2.bin';     Offset = 0x1580000L; Length = 0x0080000L }
)

$sourceItem = Get-Item -LiteralPath $Source
$required = ($regions | ForEach-Object { $_.Offset + $_.Length } | Measure-Object -Maximum).Maximum
if ($sourceItem.Length -lt $required) {
    throw "Source is too short: $($sourceItem.Length) bytes; need at least $required."
}

New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$input = [IO.File]::OpenRead($sourceItem.FullName)
try {
    foreach ($region in $regions) {
        $outputPath = Join-Path $Destination $region.Name
        if (Test-Path -LiteralPath $outputPath) {
            throw "Refusing to overwrite $outputPath"
        }
        [void]$input.Seek($region.Offset, [IO.SeekOrigin]::Begin)
        $output = [IO.File]::Create($outputPath)
        try {
            $remaining = $region.Length
            $buffer = [byte[]]::new(1MB)
            while ($remaining -gt 0) {
                $count = [Math]::Min($buffer.Length, $remaining)
                $read = $input.Read($buffer, 0, $count)
                if ($read -le 0) { throw "Unexpected EOF while extracting $($region.Name)" }
                $output.Write($buffer, 0, $read)
                $remaining -= $read
            }
        }
        finally {
            $output.Dispose()
        }
    }
}
finally {
    $input.Dispose()
}

Get-FileHash -Algorithm SHA256 -LiteralPath ($regions | ForEach-Object { Join-Path $Destination $_.Name })
