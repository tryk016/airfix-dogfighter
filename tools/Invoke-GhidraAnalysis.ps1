[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputFile,

    [string]$ProjectDirectory = (Join-Path $PSScriptRoot '..\analysis\ghidra-projects'),

    [string]$ProjectName = 'AirfixDogfighter',

    [string]$ReportDirectory = (Join-Path $PSScriptRoot '..\artifacts\ghidra')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedInput = (Resolve-Path -LiteralPath $InputFile).Path
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$scriptDirectory = Join-Path $PSScriptRoot 'ghidra'

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

Set-JavaRuntime
$headless = Find-GhidraHeadless
$projectPath = [System.IO.Path]::GetFullPath($ProjectDirectory)
$reportPath = [System.IO.Path]::GetFullPath($ReportDirectory)
$reportName = ([System.IO.Path]::GetFileName($resolvedInput)) + '.decomp.txt'
$reportFile = Join-Path $reportPath $reportName

New-Item -ItemType Directory -Force -Path $projectPath, $reportPath | Out-Null

$arguments = @(
    $projectPath,
    $ProjectName,
    '-import', $resolvedInput,
    '-overwrite',
    '-analysisTimeoutPerFile', '600',
    '-scriptPath', $scriptDirectory,
    '-postScript', 'ExportDecompilation.java', $reportFile
)

& $headless @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Ghidra headless analysis failed with exit code $LASTEXITCODE"
}

Write-Output $reportFile
