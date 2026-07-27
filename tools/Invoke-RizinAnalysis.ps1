[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputFile,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedSha256,

    [Parameter(Mandatory = $true)]
    [string]$FunctionId,

    [Parameter(Mandatory = $true)]
    [string]$Rva,

    [string]$RizinHome,

    [string]$PythonPath,

    [string]$SleighHome,

    [switch]$CreateMissingFunction
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..'))
$analysisRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'analysis'))
$workCopiesRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $analysisRoot 'work-copies'))
$artifactsRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'artifacts'))
$reportRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $artifactsRoot 'rizin'))
$exporterPath = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot 'rizin\export_function_report.py'))
$pathComparison = if (
    [System.Environment]::OSVersion.Platform -eq
        [System.PlatformID]::Win32NT
) {
    [System.StringComparison]::OrdinalIgnoreCase
}
else {
    [System.StringComparison]::Ordinal
}

function Assert-NonEmpty {
    param(
        [string]$Value,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "$Name must not be empty."
    }
}

function Resolve-ExistingFile {
    param(
        [string]$Path,
        [string]$Name
    )

    Assert-NonEmpty -Value $Path -Name $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name must identify an existing file."
    }
    return [System.IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath $Path).Path)
}

function Resolve-ExistingDirectory {
    param(
        [string]$Path,
        [string]$Name
    )

    Assert-NonEmpty -Value $Path -Name $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Name must identify an existing directory."
    }
    return [System.IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath $Path).Path)
}

function Assert-NotReparsePoint {
    param(
        [string]$Path,
        [string]$Name
    )

    $attributes = [System.IO.File]::GetAttributes($Path)
    if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Name must not be a symbolic link or junction."
    }
}

function Ensure-SafeDirectory {
    param(
        [string]$Path,
        [string]$Parent,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        throw "$Name parent directory does not exist."
    }
    Assert-NotReparsePoint -Path $Parent -Name "$Name parent"

    if (Test-Path -LiteralPath $Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
            throw "$Name exists but is not a directory."
        }
        Assert-NotReparsePoint -Path $Path -Name $Name
        return
    }

    New-Item -ItemType Directory -Path $Path | Out-Null
    Assert-NotReparsePoint -Path $Path -Name $Name
}

function Test-IsWithinDirectory {
    param(
        [string]$Path,
        [string]$Directory
    )

    $directoryWithSeparator = $Directory.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    return $Path.StartsWith($directoryWithSeparator, $pathComparison)
}

function Assert-NoReparsePointBelowRoot {
    param(
        [string]$Path,
        [string]$Root
    )

    $rootWithSeparator = $Root.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    $relative = $Path.Substring($rootWithSeparator.Length)
    $segments = $relative.Split(
        [char[]]@(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar
        ),
        [System.StringSplitOptions]::RemoveEmptyEntries)
    $current = $Root
    foreach ($segment in $segments) {
        $current = Join-Path $current $segment
        $attributes = [System.IO.File]::GetAttributes($current)
        if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'InputFile must not traverse a symbolic link or junction.'
        }
    }
}

function Find-RizinHome {
    param([string]$RequestedHome)

    $candidateHome = $RequestedHome
    if ([string]::IsNullOrWhiteSpace($candidateHome)) {
        $candidateHome = $env:RIZIN_HOME
    }

    if (-not [string]::IsNullOrWhiteSpace($candidateHome)) {
        $resolvedHome = Resolve-ExistingDirectory `
            -Path $candidateHome `
            -Name 'RizinHome'
    }
    else {
        $command = Get-Command rizin -CommandType Application `
            -ErrorAction SilentlyContinue
        if (-not $command) {
            throw (
                'Rizin was not found. Pass -RizinHome, set RIZIN_HOME, ' +
                'or put rizin on PATH.'
            )
        }
        $resolvedHome = [System.IO.Path]::GetFullPath(
            (Split-Path -Parent $command.Source))
    }

    $names = if (
        [System.Environment]::OSVersion.Platform -eq
            [System.PlatformID]::Win32NT
    ) {
        @('rizin.exe')
    }
    else {
        @('rizin', 'rizin.exe')
    }
    foreach ($name in $names) {
        $candidate = Join-Path $resolvedHome $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $resolvedHome
        }
    }
    throw 'RizinHome must contain the rizin executable.'
}

function Find-Python {
    param([string]$RequestedPath)

    $candidatePath = $RequestedPath
    if ([string]::IsNullOrWhiteSpace($candidatePath)) {
        $candidatePath = $env:AIRFIX_RIZIN_PYTHON
    }
    if (-not [string]::IsNullOrWhiteSpace($candidatePath)) {
        return Resolve-ExistingFile -Path $candidatePath -Name 'PythonPath'
    }

    foreach ($name in @('python', 'python3')) {
        $command = Get-Command $name -CommandType Application `
            -ErrorAction SilentlyContinue
        if ($command) {
            return [System.IO.Path]::GetFullPath($command.Source)
        }
    }
    throw (
        'Python was not found. Pass -PythonPath, set AIRFIX_RIZIN_PYTHON, ' +
        'or put python on PATH.'
    )
}

function ConvertTo-CanonicalRva {
    param([string]$Value)

    Assert-NonEmpty -Value $Value -Name 'Rva'
    $hex = $Value
    if ($hex.StartsWith('0x', [System.StringComparison]::OrdinalIgnoreCase)) {
        $hex = $hex.Substring(2)
    }
    if ($hex -notmatch '^[0-9A-Fa-f]{1,8}$') {
        throw 'Rva must be a 32-bit hexadecimal value.'
    }
    $numeric = [System.Convert]::ToUInt32($hex, 16)
    return ('0x{0:X8}' -f $numeric)
}

function Assert-NoLocalPaths {
    param(
        [object]$Value,
        [string[]]$KnownPaths
    )

    if ($null -eq $Value) {
        return
    }
    if ($Value -is [string]) {
        foreach ($knownPath in $KnownPaths) {
            if (-not [string]::IsNullOrWhiteSpace($knownPath) -and
                $Value.IndexOf($knownPath, $pathComparison) -ge 0) {
                throw 'Rizin report contains a local filesystem path.'
            }
        }
        if ($Value -match '^[A-Za-z]:[\\/]' -or
            $Value -match '^\\\\' -or
            $Value -match '^/(?:home|Users|mnt|private|tmp|var|opt)/' -or
            $Value -match '^~[\\/]') {
            throw 'Rizin report contains an absolute filesystem path.'
        }
        return
    }
    if ($Value -is [System.Collections.IDictionary]) {
        foreach ($entry in $Value.GetEnumerator()) {
            Assert-NoLocalPaths `
                -Value ([string]$entry.Key) `
                -KnownPaths $KnownPaths
            Assert-NoLocalPaths -Value $entry.Value -KnownPaths $KnownPaths
        }
        return
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        foreach ($item in $Value) {
            Assert-NoLocalPaths -Value $item -KnownPaths $KnownPaths
        }
        return
    }
    foreach ($property in $Value.PSObject.Properties) {
        Assert-NoLocalPaths `
            -Value ([string]$property.Name) `
            -KnownPaths $KnownPaths
        Assert-NoLocalPaths -Value $property.Value -KnownPaths $KnownPaths
    }
}

if (-not (Test-Path -LiteralPath $workCopiesRoot -PathType Container)) {
    throw 'The analysis/work-copies directory does not exist.'
}
if (-not (Test-Path -LiteralPath $exporterPath -PathType Leaf)) {
    throw 'The committed Rizin report exporter was not found.'
}
Assert-NotReparsePoint -Path $repositoryRoot -Name 'Repository root'
Assert-NotReparsePoint -Path $analysisRoot -Name 'Analysis directory'
Assert-NotReparsePoint `
    -Path $workCopiesRoot `
    -Name 'analysis/work-copies directory'
Assert-NotReparsePoint -Path $exporterPath -Name 'Rizin exporter'

$resolvedInput = Resolve-ExistingFile -Path $InputFile -Name 'InputFile'
$resolvedWorkCopiesRoot = Resolve-ExistingDirectory `
    -Path $workCopiesRoot `
    -Name 'analysis/work-copies'
if (-not (Test-IsWithinDirectory `
        -Path $resolvedInput `
        -Directory $resolvedWorkCopiesRoot)) {
    throw 'InputFile must be below analysis/work-copies.'
}
Assert-NoReparsePointBelowRoot `
    -Path $resolvedInput `
    -Root $resolvedWorkCopiesRoot

Assert-NonEmpty -Value $ExpectedSha256 -Name 'ExpectedSha256'
if ($ExpectedSha256 -notmatch '^[0-9A-Fa-f]{64}$') {
    throw 'ExpectedSha256 must contain exactly 64 hexadecimal characters.'
}
$expectedHash = $ExpectedSha256.ToLowerInvariant()
$actualHash = (
    Get-FileHash -LiteralPath $resolvedInput -Algorithm SHA256
).Hash.ToLowerInvariant()
if ($actualHash -cne $expectedHash) {
    throw 'InputFile SHA-256 does not match ExpectedSha256.'
}

Assert-NonEmpty -Value $FunctionId -Name 'FunctionId'
if ($FunctionId -cnotmatch '^FN-[A-Z0-9_]+-[0-9A-F]{8}$') {
    throw 'FunctionId must use canonical FN-MODULE-RVA form.'
}
$canonicalRva = ConvertTo-CanonicalRva -Value $Rva
$functionRvaSuffix = $FunctionId.Substring(
    $FunctionId.Length - 8)
if ($functionRvaSuffix -cne $canonicalRva.Substring(2)) {
    throw 'FunctionId RVA suffix must match Rva.'
}

$resolvedRizinHome = Find-RizinHome -RequestedHome $RizinHome
$resolvedPython = Find-Python -RequestedPath $PythonPath
$resolvedSleighHome = $null
if (-not [string]::IsNullOrWhiteSpace($SleighHome)) {
    $resolvedSleighHome = Resolve-ExistingDirectory `
        -Path $SleighHome `
        -Name 'SleighHome'
}

Ensure-SafeDirectory `
    -Path $artifactsRoot `
    -Parent $repositoryRoot `
    -Name 'Artifacts directory'
Ensure-SafeDirectory `
    -Path $reportRoot `
    -Parent $artifactsRoot `
    -Name 'Rizin report directory'
$reportFile = Join-Path $reportRoot "$FunctionId.rizin.json"
$temporaryReport = Join-Path $reportRoot (
    ".$FunctionId.partial-" + [Guid]::NewGuid().ToString('N') + '.json')
$arguments = @(
    $exporterPath,
    '--input', $resolvedInput,
    '--output', $temporaryReport,
    '--rizin-home', $resolvedRizinHome,
    '--function-id', $FunctionId,
    '--rva', $canonicalRva,
    '--expected-sha256', $expectedHash
)
if ($resolvedSleighHome) {
    $arguments += @('--sleigh-home', $resolvedSleighHome)
}
if ($CreateMissingFunction) {
    $arguments += '--create-missing-function'
}

$startedAt = [DateTime]::UtcNow
try {
    $global:LASTEXITCODE = 0
    & $resolvedPython @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Rizin report export failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $temporaryReport -PathType Leaf)) {
        throw 'Rizin exporter did not create its report.'
    }
    $reportInfo = Get-Item -LiteralPath $temporaryReport
    if ($reportInfo.Length -eq 0 -or
        $reportInfo.LastWriteTimeUtc -lt $startedAt.AddSeconds(-2)) {
        throw 'Rizin exporter did not create a fresh non-empty report.'
    }

    try {
        $report = Get-Content -LiteralPath $temporaryReport -Raw |
            ConvertFrom-Json
    }
    catch {
        throw 'Rizin exporter created invalid JSON.'
    }
    if (-not $report -or
        -not ($report.PSObject.Properties.Name -contains 'schema') -or
        $report.schema -cne 'airfix.re.rizin-function.v1') {
        throw 'Rizin report schema is invalid.'
    }
    if (-not ($report.PSObject.Properties.Name -contains 'source') -or
        -not $report.source -or
        -not ($report.source.PSObject.Properties.Name -contains 'sha256') -or
        [string]$report.source.sha256 -cne $expectedHash) {
        throw 'Rizin report source hash is invalid.'
    }
    if (-not ($report.PSObject.Properties.Name -contains 'functions')) {
        throw 'Rizin report functions array is missing.'
    }
    $functions = @($report.functions)
    if ($functions.Count -ne 1 -or -not $functions[0]) {
        throw 'Rizin report must contain exactly one function.'
    }
    $function = $functions[0]
    if (-not ($function.PSObject.Properties.Name -contains 'id') -or
        [string]$function.id -cne $FunctionId) {
        throw 'Rizin report function ID is invalid.'
    }
    if (-not ($function.PSObject.Properties.Name -contains 'rva') -or
        [string]$function.rva -cne $canonicalRva) {
        throw 'Rizin report function RVA is invalid.'
    }

    Assert-NoLocalPaths -Value $report -KnownPaths @(
        $repositoryRoot,
        $resolvedWorkCopiesRoot,
        $resolvedInput,
        $reportRoot,
        $temporaryReport,
        $resolvedRizinHome,
        $resolvedPython,
        $resolvedSleighHome
    )

    Move-Item -LiteralPath $temporaryReport -Destination $reportFile -Force
}
finally {
    if (Test-Path -LiteralPath $temporaryReport) {
        Remove-Item -LiteralPath $temporaryReport -Force
    }
}

Write-Output $reportFile
