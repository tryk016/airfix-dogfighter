$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$script = Join-Path $repositoryRoot 'tools\Prepare-ReWorkingCopy.ps1'
$testId = [Guid]::NewGuid().ToString('N')
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "airfix-re-working-copy-$testId")
$sourceRoot = Join-Path $temporaryRoot 'source'
$manifestPath = Join-Path $temporaryRoot "synthetic-$testId.sha256"
$workCopyParent = Join-Path (
    Join-Path $repositoryRoot 'analysis'
) 'work-copies'
$workSet = $null

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw "Assertion failed: $Message"
    }
}

function Assert-Fails {
    param(
        [scriptblock]$Action,
        [string]$ExpectedMessage
    )

    try {
        & $Action
    }
    catch {
        Assert-True -Condition (
            $_.Exception.Message -like "*$ExpectedMessage*"
        ) -Message (
            "expected failure containing '$ExpectedMessage', got " +
            "'$($_.Exception.Message)'"
        )
        return
    }
    throw "Expected failure containing '$ExpectedMessage'."
}

function Get-Sha256 {
    param([string]$Path)

    return (
        Get-FileHash -LiteralPath $Path -Algorithm SHA256
    ).Hash.ToUpperInvariant()
}

try {
    $nestedSource = Join-Path (Join-Path $sourceRoot 'Nested') 'Case.bin'
    $wrongSource = Join-Path $sourceRoot 'Wrong.bin'
    New-Item -ItemType Directory -Force -Path (
        Split-Path -Parent $nestedSource
    ) | Out-Null
    [System.IO.File]::WriteAllBytes(
        (Join-Path $sourceRoot 'Valid.bin'),
        [byte[]](0x41, 0x69, 0x72, 0x66, 0x69, 0x78))
    [System.IO.File]::WriteAllBytes(
        $nestedSource,
        [byte[]](0x00, 0x7f, 0x80, 0xff))
    [System.IO.File]::WriteAllBytes(
        $wrongSource,
        [byte[]](0x01, 0x02, 0x03))

    $validSource = Join-Path $sourceRoot 'Valid.bin'
    $validHash = Get-Sha256 -Path $validSource
    $nestedHash = Get-Sha256 -Path $nestedSource
    $missingHash = (
        [System.BitConverter]::ToString(
            [System.Security.Cryptography.SHA256]::Create().ComputeHash(
                [byte[]]@()))
    ).Replace('-', '')
    $wrongHash = ('0' * 64) -join ''
    $manifestLines = @(
        "$validHash  Valid.bin",
        "$nestedHash  Nested\Case.bin",
        "$missingHash  Missing.bin",
        "$wrongHash  Wrong.bin"
    )
    [System.IO.File]::WriteAllLines(
        $manifestPath,
        $manifestLines,
        [System.Text.UTF8Encoding]::new($false))

    $sourceHashBefore = Get-Sha256 -Path $validSource
    $sourceWriteTimeBefore = (
        Get-Item -LiteralPath $validSource
    ).LastWriteTimeUtc

    $copied = & $script `
        -SourceRoot $sourceRoot `
        -ManifestPath $manifestPath `
        -ManifestEntry 'Valid.bin'
    Assert-True ($copied.Status -eq 'Copied') `
        'the first preparation reports a copied file'
    Assert-True ($copied.Entry -eq 'Valid.bin') `
        'the canonical manifest path is returned'
    Assert-True ($copied.Sha256 -eq $validHash) `
        'the manifest hash is returned'
    Assert-True (Test-Path -LiteralPath $copied.CopyPath -PathType Leaf) `
        'the working copy was created'
    Assert-True ((Get-Sha256 -Path $copied.CopyPath) -eq $validHash) `
        'the working-copy bytes match the manifest'
    $workSet = Split-Path -Parent $copied.CopyPath

    Assert-True ((Get-Sha256 -Path $validSource) -eq $sourceHashBefore) `
        'the source bytes were not modified'
    Assert-True (
        (Get-Item -LiteralPath $validSource).LastWriteTimeUtc -eq
        $sourceWriteTimeBefore
    ) 'the source timestamp was not modified'

    $reused = & $script `
        -SourceRoot $sourceRoot `
        -ManifestPath $manifestPath `
        -ManifestEntry 'valid.bin'
    Assert-True ($reused.Status -eq 'Reused') `
        'a verified existing copy is reused case-insensitively'
    Assert-True ($reused.CopyPath -eq $copied.CopyPath) `
        'idempotent preparation returns the same path'

    $nested = & $script `
        -SourceRoot $sourceRoot `
        -ManifestPath $manifestPath `
        -ManifestEntry 'Nested/Case.bin'
    Assert-True ($nested.Status -eq 'Copied') `
        'manifest separators are portable'
    Assert-True ((Get-Sha256 -Path $nested.CopyPath) -eq $nestedHash) `
        'the nested copy matches its manifest hash'

    Assert-Fails -ExpectedMessage 'not present in the SHA-256 manifest' -Action {
        & $script `
            -SourceRoot $sourceRoot `
            -ManifestPath $manifestPath `
            -ManifestEntry 'Unlisted.bin'
    }
    Assert-Fails -ExpectedMessage 'Manifest source file was not found' -Action {
        & $script `
            -SourceRoot $sourceRoot `
            -ManifestPath $manifestPath `
            -ManifestEntry 'Missing.bin'
    }
    Assert-Fails -ExpectedMessage 'Source SHA-256 does not match' -Action {
        & $script `
            -SourceRoot $sourceRoot `
            -ManifestPath $manifestPath `
            -ManifestEntry 'Wrong.bin'
    }
    Assert-Fails -ExpectedMessage 'duplicate path' -Action {
        & $script `
            -SourceRoot $sourceRoot `
            -ManifestPath $manifestPath `
            -ManifestEntry @('Valid.bin', 'valid.bin')
    }

    $traversalManifest = Join-Path $temporaryRoot 'traversal.sha256'
    [System.IO.File]::WriteAllText(
        $traversalManifest,
        "$validHash  ..\escape.bin`n",
        [System.Text.UTF8Encoding]::new($false))
    Assert-Fails -ExpectedMessage 'unsafe path component' -Action {
        & $script `
            -SourceRoot $sourceRoot `
            -ManifestPath $traversalManifest `
            -ManifestEntry '..\escape.bin'
    }

    $duplicateManifest = Join-Path $temporaryRoot 'duplicate.sha256'
    [System.IO.File]::WriteAllLines(
        $duplicateManifest,
        @(
            "$validHash  Valid.bin",
            "$validHash  valid.bin"
        ),
        [System.Text.UTF8Encoding]::new($false))
    Assert-Fails -ExpectedMessage 'Duplicate manifest path' -Action {
        & $script `
            -SourceRoot $sourceRoot `
            -ManifestPath $duplicateManifest `
            -ManifestEntry 'Valid.bin'
    }

    $isolatedRepository = Join-Path $temporaryRoot 'isolated-repository'
    $isolatedTools = Join-Path $isolatedRepository 'tools'
    $isolatedAnalysis = Join-Path $isolatedRepository 'analysis'
    $junctionTarget = Join-Path $temporaryRoot 'junction-target'
    New-Item -ItemType Directory -Force -Path `
        $isolatedTools, `
        $isolatedAnalysis, `
        $junctionTarget | Out-Null
    $isolatedScript = Join-Path $isolatedTools 'Prepare-ReWorkingCopy.ps1'
    [System.IO.File]::Copy($script, $isolatedScript)
    $junctionPath = Join-Path $isolatedAnalysis 'work-copies'
    New-Item `
        -ItemType Junction `
        -Path $junctionPath `
        -Target $junctionTarget | Out-Null
    Assert-Fails `
        -ExpectedMessage 'must not be a symbolic link or reparse point' `
        -Action {
            & $isolatedScript `
                -SourceRoot $sourceRoot `
                -ManifestPath $manifestPath `
                -ManifestEntry 'Valid.bin'
        }
    Assert-True -Condition (
        @(Get-ChildItem -LiteralPath $junctionTarget -Force).Count -eq 0
    ) 'a work-copies junction is rejected before writing through it'
    [System.IO.Directory]::Delete($junctionPath)

    [System.IO.File]::WriteAllBytes(
        $copied.CopyPath,
        [byte[]](0xde, 0xad, 0xbe, 0xef))
    $conflictingHash = Get-Sha256 -Path $copied.CopyPath
    Assert-Fails -ExpectedMessage 'different content' -Action {
        & $script `
            -SourceRoot $sourceRoot `
            -ManifestPath $manifestPath `
            -ManifestEntry 'Valid.bin'
    }
    Assert-True (
        (Get-Sha256 -Path $copied.CopyPath) -eq $conflictingHash
    ) 'a conflicting existing copy is not overwritten'

    Write-Output 'Prepare-ReWorkingCopy tests passed.'
}
finally {
    if ($workSet) {
        $resolvedWorkCopyParent = [System.IO.Path]::GetFullPath($workCopyParent)
        $resolvedWorkSet = [System.IO.Path]::GetFullPath($workSet)
        $prefix = $resolvedWorkCopyParent.TrimEnd('\', '/') +
            [System.IO.Path]::DirectorySeparatorChar
        if ($resolvedWorkSet.StartsWith(
                $prefix,
                [System.StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolvedWorkSet) -match
                '^manifest-[0-9A-F]{64}$' -and
            (Test-Path -LiteralPath $resolvedWorkSet)) {
            Remove-Item -LiteralPath $resolvedWorkSet -Recurse -Force
        }
    }

    $resolvedTemporaryRoot = [System.IO.Path]::GetFullPath($temporaryRoot)
    $systemTemporaryRoot = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::GetTempPath())
    if ($resolvedTemporaryRoot.StartsWith(
            $systemTemporaryRoot,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemporaryRoot) -eq
            "airfix-re-working-copy-$testId" -and
        (Test-Path -LiteralPath $resolvedTemporaryRoot)) {
        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
    }
}

$global:LASTEXITCODE = 0
