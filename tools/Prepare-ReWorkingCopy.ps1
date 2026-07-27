[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    [Parameter(Mandatory = $true)]
    [string[]]$ManifestEntry,

    [string]$ManifestPath = (
        Join-Path (
            Join-Path (
                Join-Path (
                    Join-Path $PSScriptRoot '..'
                ) 'docs'
            ) 'evidence'
        ) 'source-manifest.sha256'
    )
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Comparison {
    if ($PSVersionTable.PSEdition -eq 'Desktop' -or
        $env:OS -eq 'Windows_NT') {
        return [System.StringComparison]::OrdinalIgnoreCase
    }
    return [System.StringComparison]::Ordinal
}

function Get-NormalizedRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $separators = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    return $fullPath.TrimEnd($separators)
}

function Test-IsWithinRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Candidate,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $normalizedCandidate = [System.IO.Path]::GetFullPath($Candidate)
    $normalizedRoot = Get-NormalizedRoot -Path $Root
    if ($normalizedCandidate.Equals($normalizedRoot, (Get-Comparison))) {
        return $true
    }
    $prefix = $normalizedRoot + [System.IO.Path]::DirectorySeparatorChar
    return $normalizedCandidate.StartsWith($prefix, (Get-Comparison))
}

function Assert-NotReparsePoint {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band
            [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Name must not be a symbolic link or reparse point: $Path"
    }
}

function ConvertTo-ManifestPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "$Name must not be empty."
    }

    $normalized = $Value.Replace('\', '/')
    if ($normalized.StartsWith('/') -or
        $normalized -match '^[A-Za-z]:' -or
        $normalized.IndexOf([char]0) -ge 0) {
        throw "$Name must be a relative manifest path: $Value"
    }

    $parts = [string[]]$normalized.Split('/')
    if ($parts.Count -eq 0) {
        throw "$Name must be a relative manifest path: $Value"
    }

    $reservedName = '^(?i:CON|PRN|AUX|NUL|CLOCK\$|COM[1-9]|LPT[1-9])(?:\.|$)'
    foreach ($part in $parts) {
        if ([string]::IsNullOrEmpty($part) -or
            $part -eq '.' -or
            $part -eq '..' -or
            $part -match '[<>:"|?*\x00-\x1f]' -or
            $part.EndsWith('.') -or
            $part.EndsWith(' ') -or
            $part -match $reservedName) {
            throw "$Name contains an unsafe path component: $Value"
        }
    }

    return $parts -join '/'
}

function Join-ManifestPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $result = $Root
    foreach ($part in $RelativePath.Split('/')) {
        $result = Join-Path $result $part
    }
    return $result
}

if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
    throw "SourceRoot must identify an existing directory: $SourceRoot"
}
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "ManifestPath must identify an existing file: $ManifestPath"
}
if ($ManifestEntry.Count -eq 0) {
    throw 'ManifestEntry must contain at least one path.'
}

$resolvedRepositoryRoot = (
    Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
).Path
$resolvedSourceRoot = (
    Resolve-Path -LiteralPath $SourceRoot
).Path
$resolvedManifestPath = (
    Resolve-Path -LiteralPath $ManifestPath
).Path
Assert-NotReparsePoint `
    -Path $resolvedRepositoryRoot `
    -Name 'Repository root'
Assert-NotReparsePoint `
    -Path $resolvedSourceRoot `
    -Name 'Source root'
Assert-NotReparsePoint `
    -Path $resolvedManifestPath `
    -Name 'Manifest file'

$manifestRecords = [System.Collections.Generic.Dictionary[string, object]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
$lineNumber = 0
foreach ($line in [System.IO.File]::ReadAllLines($resolvedManifestPath)) {
    $lineNumber++
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }
    if ($line -notmatch '^(?<hash>[0-9A-Fa-f]{64})  (?<path>.+)$') {
        throw "Invalid SHA-256 manifest line $lineNumber."
    }

    $normalizedPath = ConvertTo-ManifestPath `
        -Value $Matches.path `
        -Name "Manifest path on line $lineNumber"
    if ($manifestRecords.ContainsKey($normalizedPath)) {
        throw "Duplicate manifest path on line ${lineNumber}: $normalizedPath"
    }
    $manifestRecords.Add(
        $normalizedPath,
        [pscustomobject]@{
            Path = $normalizedPath
            Sha256 = $Matches.hash.ToUpperInvariant()
        })
}
if ($manifestRecords.Count -eq 0) {
    throw 'The SHA-256 manifest contains no entries.'
}

$requestedPaths = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
$selectedRecords = [System.Collections.Generic.List[object]]::new()
foreach ($requestedEntry in $ManifestEntry) {
    $normalizedRequest = ConvertTo-ManifestPath `
        -Value $requestedEntry `
        -Name 'ManifestEntry'
    if (-not $requestedPaths.Add($normalizedRequest)) {
        throw "ManifestEntry contains a duplicate path: $normalizedRequest"
    }
    if (-not $manifestRecords.ContainsKey($normalizedRequest)) {
        throw "ManifestEntry is not present in the SHA-256 manifest: $normalizedRequest"
    }
    $selectedRecords.Add($manifestRecords[$normalizedRequest])
}

$analysisDirectory = Join-Path $resolvedRepositoryRoot 'analysis'
if (Test-Path -LiteralPath $analysisDirectory) {
    if (-not (Test-Path -LiteralPath $analysisDirectory -PathType Container)) {
        throw 'The analysis path exists but is not a directory.'
    }
    Assert-NotReparsePoint `
        -Path $analysisDirectory `
        -Name 'Analysis directory'
}
else {
    New-Item -ItemType Directory -Path $analysisDirectory | Out-Null
}
Assert-NotReparsePoint `
    -Path $analysisDirectory `
    -Name 'Analysis directory'

$workCopyParent = Join-Path $analysisDirectory 'work-copies'
if (Test-Path -LiteralPath $workCopyParent) {
    if (-not (Test-Path -LiteralPath $workCopyParent -PathType Container)) {
        throw 'The analysis/work-copies path exists but is not a directory.'
    }
    Assert-NotReparsePoint `
        -Path $workCopyParent `
        -Name 'Working-copy directory'
}
else {
    New-Item -ItemType Directory -Path $workCopyParent | Out-Null
}
Assert-NotReparsePoint `
    -Path $workCopyParent `
    -Name 'Working-copy directory'
$resolvedWorkCopyParent = (
    Resolve-Path -LiteralPath $workCopyParent
).Path
if (-not (Test-IsWithinRoot `
        -Candidate $resolvedWorkCopyParent `
        -Root $resolvedRepositoryRoot)) {
    throw 'The analysis/work-copies directory resolves outside the repository.'
}

$manifestHash = (
    Get-FileHash -LiteralPath $resolvedManifestPath -Algorithm SHA256
).Hash.ToUpperInvariant()
$workSet = Join-Path $resolvedWorkCopyParent "manifest-$manifestHash"
if (Test-Path -LiteralPath $workSet) {
    if (-not (Test-Path -LiteralPath $workSet -PathType Container)) {
        throw 'The manifest work-copy path exists but is not a directory.'
    }
    Assert-NotReparsePoint `
        -Path $workSet `
        -Name 'Manifest work-copy directory'
}
else {
    New-Item -ItemType Directory -Path $workSet | Out-Null
}
Assert-NotReparsePoint `
    -Path $workSet `
    -Name 'Manifest work-copy directory'
$resolvedWorkSet = (Resolve-Path -LiteralPath $workSet).Path
if (-not (Test-IsWithinRoot `
        -Candidate $resolvedWorkSet `
        -Root $resolvedWorkCopyParent)) {
    throw 'The manifest work-copy directory resolves outside analysis/work-copies.'
}

foreach ($record in $selectedRecords) {
    $sourceCandidate = Join-ManifestPath `
        -Root $resolvedSourceRoot `
        -RelativePath $record.Path
    if (-not (Test-Path -LiteralPath $sourceCandidate -PathType Leaf)) {
        throw "Manifest source file was not found: $($record.Path)"
    }
    $sourceComponent = $resolvedSourceRoot
    foreach ($part in $record.Path.Split('/')) {
        $sourceComponent = Join-Path $sourceComponent $part
        Assert-NotReparsePoint `
            -Path $sourceComponent `
            -Name 'Manifest source path'
    }
    $resolvedSource = (Resolve-Path -LiteralPath $sourceCandidate).Path
    if (-not (Test-IsWithinRoot `
            -Candidate $resolvedSource `
            -Root $resolvedSourceRoot)) {
        throw "Manifest source file resolves outside SourceRoot: $($record.Path)"
    }

    $sourceHashBefore = (
        Get-FileHash -LiteralPath $resolvedSource -Algorithm SHA256
    ).Hash.ToUpperInvariant()
    if ($sourceHashBefore -ne $record.Sha256) {
        throw "Source SHA-256 does not match the manifest: $($record.Path)"
    }

    $destination = Join-ManifestPath `
        -Root $resolvedWorkSet `
        -RelativePath $record.Path
    $destinationComponent = $resolvedWorkSet
    $destinationParts = [string[]]$record.Path.Split('/')
    for ($index = 0; $index -lt $destinationParts.Count - 1; $index++) {
        $nextDestinationComponent = Join-Path `
            $destinationComponent `
            $destinationParts[$index]
        if (Test-Path -LiteralPath $nextDestinationComponent) {
            if (-not (Test-Path `
                    -LiteralPath $nextDestinationComponent `
                    -PathType Container)) {
                throw (
                    'Copy destination component is not a directory: ' +
                    $record.Path
                )
            }
            Assert-NotReparsePoint `
                -Path $nextDestinationComponent `
                -Name 'Copy destination path'
        }
        else {
            New-Item `
                -ItemType Directory `
                -Path $nextDestinationComponent | Out-Null
        }
        $destinationComponent = $nextDestinationComponent
        Assert-NotReparsePoint `
            -Path $destinationComponent `
            -Name 'Copy destination path'
        if (-not (Test-IsWithinRoot `
                -Candidate $destinationComponent `
                -Root $resolvedWorkSet)) {
            throw (
                'Copy destination resolves outside the manifest work set: ' +
                $record.Path
            )
        }
    }
    $destinationParent = $destinationComponent
    $resolvedDestinationParent = (
        Resolve-Path -LiteralPath $destinationParent
    ).Path
    if (-not (Test-IsWithinRoot `
            -Candidate $resolvedDestinationParent `
            -Root $resolvedWorkSet)) {
        throw "Copy destination resolves outside the manifest work set: $($record.Path)"
    }

    if (Test-Path -LiteralPath $destination) {
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            throw "Copy destination is not a regular file: $($record.Path)"
        }
        Assert-NotReparsePoint `
            -Path $destination `
            -Name 'Copy destination'
        $resolvedDestination = (Resolve-Path -LiteralPath $destination).Path
        if (-not (Test-IsWithinRoot `
                -Candidate $resolvedDestination `
                -Root $resolvedWorkSet)) {
            throw "Copy destination resolves outside the manifest work set: $($record.Path)"
        }
        $destinationHash = (
            Get-FileHash -LiteralPath $resolvedDestination -Algorithm SHA256
        ).Hash.ToUpperInvariant()
        if ($destinationHash -ne $record.Sha256) {
            throw "Existing working copy has different content: $($record.Path)"
        }

        [pscustomobject]@{
            Entry = $record.Path
            Sha256 = $record.Sha256
            CopyPath = $resolvedDestination
            Status = 'Reused'
        }
        continue
    }

    $createdDestination = $false
    try {
        $sourceStream = [System.IO.File]::Open(
            $resolvedSource,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::Read)
        try {
            $destinationStream = [System.IO.File]::Open(
                $destination,
                [System.IO.FileMode]::CreateNew,
                [System.IO.FileAccess]::Write,
                [System.IO.FileShare]::None)
            $createdDestination = $true
            try {
                $sourceStream.CopyTo($destinationStream)
                $destinationStream.Flush($true)
            }
            finally {
                $destinationStream.Dispose()
            }
        }
        finally {
            $sourceStream.Dispose()
        }

        $destinationHash = (
            Get-FileHash -LiteralPath $destination -Algorithm SHA256
        ).Hash.ToUpperInvariant()
        $sourceHashAfter = (
            Get-FileHash -LiteralPath $resolvedSource -Algorithm SHA256
        ).Hash.ToUpperInvariant()
        if ($destinationHash -ne $record.Sha256 -or
            $sourceHashAfter -ne $record.Sha256) {
            throw "Working-copy verification failed: $($record.Path)"
        }
    }
    catch {
        if ($createdDestination -and
            (Test-Path -LiteralPath $destination -PathType Leaf) -and
            (Test-IsWithinRoot `
                -Candidate $destination `
                -Root $resolvedWorkSet)) {
            Remove-Item -LiteralPath $destination -Force
        }
        throw
    }

    [pscustomobject]@{
        Entry = $record.Path
        Sha256 = $record.Sha256
        CopyPath = [System.IO.Path]::GetFullPath($destination)
        Status = 'Copied'
    }
}
