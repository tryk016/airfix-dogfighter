$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$wrapper = Join-Path $repositoryRoot 'tools\Invoke-GhidraAnalysis.ps1'
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'airfix-ghidra-wrapper-' + [Guid]::NewGuid().ToString('N'))
$savedGhidraHome = $env:GHIDRA_HOME
$savedHelper = $env:AIRFIX_FAKE_HELPER
$savedArgumentLog = $env:AIRFIX_FAKE_ARGUMENT_LOG
$savedMode = $env:AIRFIX_FAKE_MODE
$savedPowerShell = $env:AIRFIX_FAKE_POWERSHELL

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw "Assertion failed: $Message"
    }
}

function Assert-Contains {
    param(
        [string[]]$Values,
        [string]$Expected,
        [string]$Message
    )

    Assert-True -Condition ($Values -contains $Expected) -Message $Message
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

try {
    $fakeHome = Join-Path $temporaryRoot 'ghidra'
    $fakeSupport = Join-Path $fakeHome 'support'
    $fakeHelper = Join-Path $temporaryRoot 'Fake-GhidraHeadless.ps1'
    $argumentLog = Join-Path $temporaryRoot 'arguments.txt'
    $projects = Join-Path $temporaryRoot 'projects'
    $reports = Join-Path $temporaryRoot 'reports'
    $inputFile = Join-Path $temporaryRoot 'Input.bin'

    New-Item -ItemType Directory -Force -Path $fakeSupport, $projects | Out-Null
    New-Item -ItemType File -Force -Path (
        Join-Path $projects 'MockProject.gpr'
    ), $inputFile | Out-Null

    @'
$ErrorActionPreference = 'Stop'
$forwarded = [string[]]$args
$forwarded | Set-Content -LiteralPath $env:AIRFIX_FAKE_ARGUMENT_LOG

if ($env:AIRFIX_FAKE_MODE -eq 'exit-failure') {
    exit 7
}

$postScriptIndex = [Array]::IndexOf($forwarded, '-postScript')
if ($postScriptIndex -lt 0 -or $postScriptIndex + 2 -ge $forwarded.Count) {
    throw 'fake launcher did not receive a post-script report path'
}
$reportPath = $forwarded[$postScriptIndex + 2]
$parent = Split-Path -Parent $reportPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null

if ($env:AIRFIX_FAKE_MODE -eq 'no-report') {
    exit 0
}
if ($env:AIRFIX_FAKE_MODE -eq 'empty-report') {
    New-Item -ItemType File -Force -Path $reportPath | Out-Null
    exit 0
}
if ($env:AIRFIX_FAKE_MODE -eq 'unresolved') {
    "program=fake`nunresolvedAddresses=1`n" |
        Set-Content -LiteralPath $reportPath -NoNewline
    exit 0
}
if ($env:AIRFIX_FAKE_MODE -eq 'address-reference-out-of-range') {
    @(
        'program=fake'
        'language=x86:LE:32:default'
        'selectedAddresses=2'
        'unresolvedAddresses=1'
        'unresolved=DEADBEEF'
    ) | Set-Content -LiteralPath $reportPath
    exit 0
}

"program=fake`nunresolvedAddresses=0`n" |
    Set-Content -LiteralPath $reportPath -NoNewline
'@ | Set-Content -LiteralPath $fakeHelper -NoNewline

    @'
@echo off
"%AIRFIX_FAKE_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%AIRFIX_FAKE_HELPER%" %*
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath (
        Join-Path $fakeSupport 'analyzeHeadless.bat'
    ) -NoNewline

    $env:GHIDRA_HOME = $fakeHome
    $env:AIRFIX_FAKE_HELPER = $fakeHelper
    $env:AIRFIX_FAKE_ARGUMENT_LOG = $argumentLog
    $env:AIRFIX_FAKE_MODE = 'success'
    $env:AIRFIX_FAKE_POWERSHELL = (Get-Process -Id $PID).Path

    $common = @{
        ProjectDirectory = $projects
        ProjectName = 'MockProject'
        ReportDirectory = $reports
        PostScript = 'ExportMemoryValues.java'
    }

    $programReport = & $wrapper @common `
        -ProgramName 'AirCraft.type' `
        -ReportSuffix 'program' `
        -PostScriptArguments @('10003F40')
    Assert-True -Condition (
        $programReport -eq (Join-Path $reports 'AirCraft.type.program.txt')
    ) -Message 'ProgramName mode returns its deterministic report path'
    $arguments = [string[]](Get-Content -LiteralPath $argumentLog)
    Assert-Contains $arguments '-process' 'ProgramName mode uses -process'
    Assert-Contains $arguments 'AirCraft.type' 'ProgramName reaches Ghidra'
    Assert-Contains $arguments '-noanalysis' 'reuse skips automatic analysis'
    Assert-True -Condition (
        $arguments -notcontains '-import'
    ) -Message 'ProgramName mode does not import a private input file'

    $legacyReport = & $wrapper @common `
        -InputFile $inputFile `
        -ReuseProject `
        -ReportSuffix 'legacy'
    Assert-True -Condition (
        $legacyReport -eq (Join-Path $reports 'Input.bin.legacy.txt')
    ) -Message 'legacy InputFile plus ReuseProject remains compatible'
    $arguments = [string[]](Get-Content -LiteralPath $argumentLog)
    Assert-Contains $arguments '-process' 'legacy reuse uses -process'
    Assert-Contains $arguments 'Input.bin' 'legacy reuse derives the program name'

    $importProjects = Join-Path $temporaryRoot 'import-projects'
    $importParameters = @{
        InputFile = $inputFile
        ProjectDirectory = $importProjects
        ProjectName = 'Imported'
        ReportDirectory = $reports
        PostScript = 'ExportMemoryValues.java'
        ReportSuffix = 'import'
    }
    & $wrapper @importParameters | Out-Null
    $arguments = [string[]](Get-Content -LiteralPath $argumentLog)
    Assert-Contains $arguments '-import' 'default InputFile mode imports'
    Assert-Contains $arguments (
        (Resolve-Path -LiteralPath $inputFile).Path
    ) 'import mode forwards the resolved input'
    Assert-Contains $arguments '-overwrite' 'import compatibility is preserved'

    foreach ($invalidName in @('..', '..\escape', '*.type', 'folder/type')) {
        Assert-Fails -ExpectedMessage 'single literal file name' -Action {
            & $wrapper @common -ProgramName $invalidName -ReportSuffix 'invalid'
        }
    }

    Assert-Fails -ExpectedMessage 'Existing Ghidra project was not found' -Action {
        & $wrapper `
            -ProgramName 'AirCraft.type' `
            -ProjectDirectory (Join-Path $temporaryRoot 'missing') `
            -ProjectName 'Missing' `
            -ReportDirectory $reports `
            -PostScript 'ExportMemoryValues.java' `
            -ReportSuffix 'missing'
    }

    Assert-Fails -ExpectedMessage 'Parameter set cannot be resolved' -Action {
        & $wrapper @common `
            -InputFile $inputFile `
            -ProgramName 'AirCraft.type' `
            -ReportSuffix 'ambiguous'
    }

    $env:AIRFIX_FAKE_MODE = 'unresolved'
    Assert-Fails -ExpectedMessage 'reported incomplete output' -Action {
        & $wrapper @common `
            -ProgramName 'AirCraft.type' `
            -ReportSuffix 'unresolved'
    }

    $env:AIRFIX_FAKE_MODE = 'address-reference-out-of-range'
    $addressReferenceParameters = $common.Clone()
    $addressReferenceParameters.PostScript =
        'ExportAddressReferences.java'
    Assert-Fails -ExpectedMessage 'reported incomplete output' -Action {
        & $wrapper @addressReferenceParameters `
            -ProgramName 'AirCraft.type' `
            -PostScriptArguments @('00401000', 'DEADBEEF') `
            -ReportSuffix 'address-reference-out-of-range'
    }
    $addressReferenceReport = Join-Path $reports (
        'AirCraft.type.address-reference-out-of-range.txt')
    $addressReferenceLines = [string[]](
        Get-Content -LiteralPath $addressReferenceReport)
    Assert-Contains $addressReferenceLines 'selectedAddresses=2' (
        'address-reference report preserves the requested-address count')
    Assert-Contains $addressReferenceLines 'unresolvedAddresses=1' (
        'address-reference report marks an out-of-range address unresolved')
    Assert-Contains $addressReferenceLines 'unresolved=DEADBEEF' (
        'address-reference report identifies the mistyped address')

    $env:AIRFIX_FAKE_MODE = 'no-report'
    Assert-Fails -ExpectedMessage 'did not create its report' -Action {
        & $wrapper @common `
            -ProgramName 'AirCraft.type' `
            -ReportSuffix 'absent'
    }

    $env:AIRFIX_FAKE_MODE = 'empty-report'
    Assert-Fails -ExpectedMessage 'did not refresh its report' -Action {
        & $wrapper @common `
            -ProgramName 'AirCraft.type' `
            -ReportSuffix 'empty'
    }

    $env:AIRFIX_FAKE_MODE = 'exit-failure'
    Assert-Fails -ExpectedMessage 'failed with exit code 7' -Action {
        & $wrapper @common `
            -ProgramName 'AirCraft.type' `
            -ReportSuffix 'failed'
    }

    Write-Output 'Invoke-GhidraAnalysis tests passed.'
}
finally {
    $env:GHIDRA_HOME = $savedGhidraHome
    $env:AIRFIX_FAKE_HELPER = $savedHelper
    $env:AIRFIX_FAKE_ARGUMENT_LOG = $savedArgumentLog
    $env:AIRFIX_FAKE_MODE = $savedMode
    $env:AIRFIX_FAKE_POWERSHELL = $savedPowerShell

    $resolvedTemporaryRoot = [System.IO.Path]::GetFullPath($temporaryRoot)
    $systemTemporaryRoot = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::GetTempPath())
    if ($resolvedTemporaryRoot.StartsWith(
            $systemTemporaryRoot,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTemporaryRoot)) {
        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
    }
}

# Expected native-process failures leave LASTEXITCODE nonzero even after their
# exceptions are asserted and handled. GitHub's pwsh wrapper propagates that
# stale value, so normalize it only after the complete test and cleanup pass.
$global:LASTEXITCODE = 0
