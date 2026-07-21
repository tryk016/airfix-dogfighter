[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string] $SourceRoot = 'E:\roms\Airfix Dogfighter',

    [Parameter(Mandatory = $false)]
    [switch] $AsJson
)

$ErrorActionPreference = 'Stop'

function Get-PeMetadata {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    $result = [ordered]@{
        PeFormat    = $null
        Machine     = $null
        PeKind      = $null
        Sections    = $null
        PeTimestamp = $null
    }

    $stream = [System.IO.File]::OpenRead($Path)
    $reader = New-Object System.IO.BinaryReader($stream)

    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) {
            return [PSCustomObject] $result
        }

        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or ($peOffset + 26) -gt $stream.Length) {
            return [PSCustomObject] $result
        }

        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            return [PSCustomObject] $result
        }

        $machine = $reader.ReadUInt16()
        $sectionCount = $reader.ReadUInt16()
        $timestamp = $reader.ReadUInt32()
        $stream.Position += 8
        [void] $reader.ReadUInt16()
        $characteristics = $reader.ReadUInt16()
        $optionalMagic = $reader.ReadUInt16()

        $result.PeFormat = if ($optionalMagic -eq 0x20B) { 'PE32+' } else { 'PE32' }
        $result.Machine = switch ($machine) {
            0x014C { 'x86' }
            0x8664 { 'x64' }
            0xAA64 { 'ARM64' }
            default { '0x{0:X4}' -f $machine }
        }
        $result.PeKind = if (($characteristics -band 0x2000) -ne 0) { 'DLL' } else { 'EXE' }
        $result.Sections = $sectionCount
        $result.PeTimestamp = [DateTimeOffset]::FromUnixTimeSeconds($timestamp).UtcDateTime.ToString('o')
        return [PSCustomObject] $result
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Get-MagicBytes {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,

        [Parameter(Mandatory = $false)]
        [int] $Count = 8
    )

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $buffer = New-Object byte[] $Count
        $bytesRead = $stream.Read($buffer, 0, $buffer.Length)
        if ($bytesRead -eq 0) {
            return ''
        }

        return (($buffer[0..($bytesRead - 1)] | ForEach-Object { $_.ToString('X2') }) -join '')
    }
    finally {
        $stream.Dispose()
    }
}

$resolvedRoot = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
if (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
    throw "Source root is not a directory: $resolvedRoot"
}

$rows = foreach ($file in Get-ChildItem -LiteralPath $resolvedRoot -File -Recurse | Sort-Object FullName) {
    $pe = Get-PeMetadata -Path $file.FullName
    $relativePath = $file.FullName.Substring($resolvedRoot.Length).TrimStart('\')
    $magic = Get-MagicBytes -Path $file.FullName

    [PSCustomObject] [ordered]@{
        Path          = $relativePath
        Bytes         = $file.Length
        ModifiedUtc   = $file.LastWriteTimeUtc.ToString('o')
        Sha256        = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        Magic8        = $magic
        PeFormat      = $pe.PeFormat
        Machine       = $pe.Machine
        PeKind        = $pe.PeKind
        Sections      = $pe.Sections
        PeTimestamp   = $pe.PeTimestamp
    }
}

if ($AsJson) {
    $rows | ConvertTo-Json -Depth 3
}
else {
    $rows
}
