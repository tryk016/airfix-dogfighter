[CmdletBinding(DefaultParameterSetName = 'Import')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Import')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ReuseByInput')]
    [string]$InputFile,

    [Parameter(Mandatory = $true, ParameterSetName = 'ReuseByProgram')]
    [string]$ProgramName,

    [string]$ProjectDirectory = (Join-Path $PSScriptRoot '..\analysis\ghidra-projects'),

    [string]$ProjectName = 'AirfixDogfighter',

    [string]$ReportDirectory = (Join-Path $PSScriptRoot '..\artifacts\ghidra'),

    [string]$PostScript = 'ExportDecompilation.java',

    [string[]]$PostScriptArguments = @(),

    [string]$ReportSuffix = 'decomp',

    [Parameter(Mandatory = $true, ParameterSetName = 'ReuseByInput')]
    [Parameter(ParameterSetName = 'ReuseByProgram')]
    [switch]$ReuseProject
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptDirectory = Join-Path $PSScriptRoot 'ghidra'

function Assert-NonEmpty {
    param(
        [string]$Value,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "$Name must not be empty."
    }
}

function Assert-FileName {
    param(
        [string]$Value,
        [string]$Name
    )

    Assert-NonEmpty -Value $Value -Name $Name
    if ([System.IO.Path]::GetFileName($Value) -ne $Value -or
        $Value.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
        [System.Management.Automation.WildcardPattern]::ContainsWildcardCharacters($Value) -or
        $Value -eq '.' -or
        $Value -eq '..') {
        throw "$Name must be a single literal file name."
    }
}

function Find-GhidraHeadless {
    if ($env:GHIDRA_HOME) {
        $candidate = Join-Path $env:GHIDRA_HOME 'support\analyzeHeadless.bat'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    $command = Get-Command analyzeHeadless -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $candidate = Join-Path $env:USERPROFILE 'scoop\apps\ghidra\current\support\analyzeHeadless.bat'
    if (Test-Path -LiteralPath $candidate) { return $candidate }

    throw 'Ghidra analyzeHeadless was not found. See docs/toolchain/LOCK.md.'
}

function Set-JavaRuntime {
    if ($env:JAVA_HOME -and (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\java.exe'))) {
        return
    }

    $candidate = Join-Path $env:USERPROFILE 'scoop\apps\temurin21-jdk\current'
    if (Test-Path -LiteralPath (Join-Path $candidate 'bin\java.exe')) {
        $env:JAVA_HOME = $candidate
        return
    }

    if (-not (Get-Command java -ErrorAction SilentlyContinue)) {
        throw 'JDK 21 was not found. See docs/toolchain/LOCK.md.'
    }
}

$projectPath = [System.IO.Path]::GetFullPath($ProjectDirectory)
$reportPath = [System.IO.Path]::GetFullPath($ReportDirectory)
$reuseExisting = $PSBoundParameters.ContainsKey('ProgramName') -or [bool]$ReuseProject

Assert-FileName -Value $ProjectName -Name 'ProjectName'
Assert-FileName -Value $PostScript -Name 'PostScript'
Assert-FileName -Value $ReportSuffix -Name 'ReportSuffix'

$postScriptPath = Join-Path $scriptDirectory $PostScript
if (-not (Test-Path -LiteralPath $postScriptPath -PathType Leaf)) {
    throw "Ghidra post-script was not found in tools/ghidra: $PostScript"
}

if ($PSBoundParameters.ContainsKey('ProgramName')) {
    Assert-FileName -Value $ProgramName -Name 'ProgramName'
    $programToProcess = $ProgramName
    $resolvedInput = $null
}
else {
    if (-not (Test-Path -LiteralPath $InputFile -PathType Leaf)) {
        throw "InputFile must identify an existing file: $InputFile"
    }
    $resolvedInput = (Resolve-Path -LiteralPath $InputFile).Path
    $programToProcess = [System.IO.Path]::GetFileName($resolvedInput)
}

$reportName = $programToProcess + ".$ReportSuffix.txt"
$reportFile = Join-Path $reportPath $reportName

if ($reuseExisting) {
    $projectFile = Join-Path $projectPath "$ProjectName.gpr"
    if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
        throw "Existing Ghidra project was not found: $projectFile"
    }
}
else {
    New-Item -ItemType Directory -Force -Path $projectPath | Out-Null
}
New-Item -ItemType Directory -Force -Path $reportPath | Out-Null

Set-JavaRuntime
$headless = Find-GhidraHeadless

$arguments = @($projectPath, $ProjectName)
if ($reuseExisting) {
    $arguments += @('-process', $programToProcess, '-noanalysis')
}
else {
    $arguments += @(
        '-import', $resolvedInput,
        '-overwrite',
        '-analysisTimeoutPerFile', '600'
    )
}
$arguments += @(
    '-scriptPath', $scriptDirectory,
    '-postScript', $PostScript, $reportFile
)
$arguments += $PostScriptArguments

$startedAt = [DateTime]::UtcNow
& $headless @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Ghidra headless analysis failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $reportFile)) {
    throw "Ghidra post-script did not create its report: $reportFile"
}
$report = Get-Item -LiteralPath $reportFile
if ($report.Length -eq 0 -or $report.LastWriteTimeUtc -lt $startedAt) {
    throw "Ghidra post-script did not refresh its report: $reportFile"
}
$reportText = Get-Content -LiteralPath $reportFile -Raw
if ($reportText -match '(?m)^unresolvedAddresses=[1-9][0-9]*\r?$' -or
    $reportText -match '(?m)^DECOMPILATION_FAILED=') {
    throw "Ghidra post-script reported incomplete output: $reportFile"
}

Write-Output $reportFile
