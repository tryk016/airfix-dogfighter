[CmdletBinding()]
param(
    [string]$SourceRoot = 'E:\roms\Airfix Dogfighter',

    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\artifacts\pe'),

    [string]$LlvmReadObj
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $LlvmReadObj) {
    $command = Get-Command llvm-readobj -ErrorAction SilentlyContinue
    if ($command) {
        $LlvmReadObj = $command.Source
    }
    else {
        $candidate = Join-Path $env:USERPROFILE 'scoop\apps\llvm\current\bin\llvm-readobj.exe'
        if (Test-Path -LiteralPath $candidate) {
            $LlvmReadObj = $candidate
        }
    }
}
if (-not $LlvmReadObj -or -not (Test-Path -LiteralPath $LlvmReadObj)) {
    throw 'llvm-readobj was not found. See docs/toolchain/LOCK.md.'
}

$resolvedRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$extensions = '.exe', '.icd', '.dll', '.mode', '.type'
$modules = Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in $extensions } |
    Sort-Object FullName

function Get-PeSectionEntropy {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset + 6
        $sectionCount = $reader.ReadUInt16()
        $stream.Position = $peOffset + 20
        $optionalHeaderSize = $reader.ReadUInt16()
        $sectionTable = $peOffset + 24 + $optionalHeaderSize

        $result = for ($index = 0; $index -lt $sectionCount; $index++) {
            $stream.Position = $sectionTable + $index * 40
            $name = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(8)).TrimEnd([char]0)
            $stream.Position += 8
            $rawSize = $reader.ReadUInt32()
            $rawOffset = $reader.ReadUInt32()
            if ($rawSize -eq 0 -or $rawOffset + $rawSize -gt $stream.Length) {
                "$name=0.000"
                continue
            }

            $stream.Position = $rawOffset
            $data = $reader.ReadBytes([int]$rawSize)
            $counts = [int[]]::new(256)
            foreach ($value in $data) { $counts[$value]++ }
            $entropy = 0.0
            foreach ($count in $counts) {
                if ($count -eq 0) { continue }
                $probability = $count / [double]$data.Length
                $entropy -= $probability * ([Math]::Log($probability) / [Math]::Log(2.0))
            }
            '{0}={1:F3}' -f $name, $entropy
        }
        return $result -join ';'
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

$summary = foreach ($module in $modules) {
    $relativePath = $module.FullName.Substring($resolvedRoot.TrimEnd('\\').Length).TrimStart('\\')
    $reportName = ($relativePath -replace '[\\/:*?"<>|]', '_') + '.llvm.txt'
    $reportPath = Join-Path $outputPath $reportName
    $lines = @(& $LlvmReadObj --file-headers --sections --coff-imports --coff-exports $module.FullName)
    if ($LASTEXITCODE -ne 0) {
        throw "llvm-readobj failed for $relativePath with exit code $LASTEXITCODE"
    }
    [System.IO.File]::WriteAllLines($reportPath, $lines, [Text.UTF8Encoding]::new($false))

    $imports = [System.Collections.Generic.List[string]]::new()
    $sectionNames = [System.Collections.Generic.List[string]]::new()
    $insideImport = $false
    $importSymbols = 0
    $exports = 0
    $format = ''
    $architecture = ''
    $timestamp = ''

    foreach ($line in $lines) {
        if ($line -match '^Format:\s+(.+)$') { $format = $Matches[1] }
        elseif ($line -match '^Arch:\s+(.+)$') { $architecture = $Matches[1] }
        elseif ($line -match '^\s+TimeDateStamp:\s+(.+)$') { $timestamp = $Matches[1] }
        elseif ($line -eq 'Import {') { $insideImport = $true }
        elseif ($insideImport -and $line -match '^\s+Name:\s+(.+)$') {
            $imports.Add($Matches[1])
            $insideImport = $false
        }
        elseif ($line -match '^\s+Symbol:\s+') { $importSymbols++ }
        elseif ($line -eq 'Export {') { $exports++ }
        elseif ($line -match '^\s{4}Name:\s+(\.[^\s]+)\s') { $sectionNames.Add($Matches[1]) }
    }

    [pscustomobject]@{
        Path = $relativePath
        Bytes = $module.Length
        Sha256 = (Get-FileHash -LiteralPath $module.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        Format = $format
        Architecture = $architecture
        Timestamp = $timestamp
        Sections = ($sectionNames -join ';')
        SectionEntropy = Get-PeSectionEntropy -Path $module.FullName
        ImportLibraries = ($imports -join ';')
        ImportSymbols = $importSymbols
        Exports = $exports
        Report = $reportName
    }
}

$summary | Export-Csv -LiteralPath (Join-Path $outputPath 'summary.csv') -NoTypeInformation -Encoding utf8
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath 'summary.json') -Encoding utf8
$summary | Format-Table Path, Bytes, Architecture, ImportSymbols, Exports -AutoSize
Write-Output "Reports: $outputPath"
