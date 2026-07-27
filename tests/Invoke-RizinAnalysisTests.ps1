$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$wrapper = Join-Path $repositoryRoot 'tools\Invoke-RizinAnalysis.ps1'
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'airfix-rizin-wrapper-' + [Guid]::NewGuid().ToString('N'))
$testToken = [Guid]::NewGuid().ToString('N').Substring(0, 8).ToUpperInvariant()
$functionId = "FN-TEST$testToken-00001234"
$expectedRva = '0x00001234'
$workCopiesRoot = Join-Path $repositoryRoot 'analysis\work-copies'
$workCopyDirectory = Join-Path $workCopiesRoot ("synthetic-$testToken")
$inputFile = Join-Path $workCopyDirectory 'Synthetic Module.bin'
$reportFile = Join-Path (
    (Join-Path $repositoryRoot 'artifacts\rizin')
) "$functionId.rizin.json"
$savedHelper = $env:AIRFIX_FAKE_RIZIN_HELPER
$savedArgumentLog = $env:AIRFIX_FAKE_RIZIN_ARGUMENT_LOG
$savedMode = $env:AIRFIX_FAKE_RIZIN_MODE
$savedPowerShell = $env:AIRFIX_FAKE_RIZIN_POWERSHELL

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
    $fakeRizinHome = Join-Path $temporaryRoot 'fake rizin'
    $fakePythonDirectory = Join-Path $temporaryRoot 'fake python'
    $fakePython = Join-Path $fakePythonDirectory 'python.cmd'
    $fakeHelper = Join-Path $temporaryRoot 'Fake-RizinExporter.ps1'
    $argumentLog = Join-Path $temporaryRoot 'arguments.txt'
    $sleighHome = Join-Path $temporaryRoot 'fake sleigh'
    $outsideInput = Join-Path $temporaryRoot 'Outside.bin'

    New-Item -ItemType Directory -Force -Path `
        $fakeRizinHome, `
        $fakePythonDirectory, `
        $sleighHome, `
        $workCopyDirectory | Out-Null
    New-Item -ItemType File -Force -Path (
        Join-Path $fakeRizinHome 'rizin.exe'
    ) | Out-Null
    [System.IO.File]::WriteAllBytes(
        $inputFile,
        [byte[]](0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00))
    [System.IO.File]::WriteAllBytes(
        $outsideInput,
        [byte[]](0x4D, 0x5A, 0x01, 0x02))
    $expectedHash = (
        Get-FileHash -LiteralPath $inputFile -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    $outsideHash = (
        Get-FileHash -LiteralPath $outsideInput -Algorithm SHA256
    ).Hash.ToLowerInvariant()

    @'
$ErrorActionPreference = 'Stop'
$forwarded = [string[]]$args
$forwarded | Set-Content -LiteralPath $env:AIRFIX_FAKE_RIZIN_ARGUMENT_LOG

if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'exit-failure') {
    exit 9
}

function Get-ArgumentValue {
    param([string]$Name)
    $index = [Array]::IndexOf($forwarded, $Name)
    if ($index -lt 0 -or $index + 1 -ge $forwarded.Count) {
        throw "fake exporter did not receive $Name"
    }
    return $forwarded[$index + 1]
}

$inputPath = Get-ArgumentValue '--input'
$outputPath = Get-ArgumentValue '--output'
$expectedHash = Get-ArgumentValue '--expected-sha256'
$functionId = Get-ArgumentValue '--function-id'
$rva = Get-ArgumentValue '--rva'

if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'no-report') {
    exit 0
}
$parent = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'invalid-json') {
    '{not-json' | Set-Content -LiteralPath $outputPath -NoNewline
    exit 0
}

$schema = 'airfix.re.rizin-function.v1'
$sourceHash = $expectedHash
$reportedId = $functionId
$reportedRva = $rva
$extra = [ordered]@{}
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'wrong-schema') {
    $schema = 'airfix.re.rizin-function.v0'
}
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'wrong-hash') {
    $sourceHash = '0' * 64
}
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'wrong-id') {
    $reportedId = 'FN-WRONG-00001234'
}
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'wrong-rva') {
    $reportedRva = '0x00005678'
}
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'local-path') {
    $extra.local_path = $inputPath
}

$function = [ordered]@{
    id = $reportedId
    rva = $reportedRva
    va = '0x10001234'
    boundary = [ordered]@{
        start = '0x10001234'
        end = '0x1000123F'
    }
    signature = 'int32_t synthetic(void)'
    calls = @()
    data_refs = @()
    xrefs = @()
    instructions = @()
    cfg = [ordered]@{}
}
foreach ($entry in $extra.GetEnumerator()) {
    $function[$entry.Key] = $entry.Value
}
$functions = @($function)
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'multiple-functions') {
    $functions += $function
}
$report = [ordered]@{
    schema = $schema
    source = [ordered]@{ sha256 = $sourceHash }
    functions = $functions
}
$report | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $outputPath -NoNewline
if ($env:AIRFIX_FAKE_RIZIN_MODE -eq 'stale-report') {
    (Get-Item -LiteralPath $outputPath).LastWriteTimeUtc =
        [DateTime]::UtcNow.AddMinutes(-10)
}
'@ | Set-Content -LiteralPath $fakeHelper -NoNewline

    @'
@echo off
"%AIRFIX_FAKE_RIZIN_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%AIRFIX_FAKE_RIZIN_HELPER%" %*
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath $fakePython -NoNewline

    $env:AIRFIX_FAKE_RIZIN_HELPER = $fakeHelper
    $env:AIRFIX_FAKE_RIZIN_ARGUMENT_LOG = $argumentLog
    $env:AIRFIX_FAKE_RIZIN_MODE = 'success'
    $env:AIRFIX_FAKE_RIZIN_POWERSHELL = (Get-Process -Id $PID).Path

    $common = @{
        InputFile = $inputFile
        ExpectedSha256 = $expectedHash
        FunctionId = $functionId
        Rva = '1234'
        RizinHome = $fakeRizinHome
        PythonPath = $fakePython
    }

    $isolatedRepository = Join-Path $temporaryRoot 'isolated-repository'
    $isolatedTools = Join-Path $isolatedRepository 'tools'
    $isolatedRizinTools = Join-Path $isolatedTools 'rizin'
    $isolatedWorkCopy = Join-Path (
        Join-Path $isolatedRepository 'analysis\work-copies'
    ) 'synthetic'
    $isolatedArtifacts = Join-Path $isolatedRepository 'artifacts'
    $junctionTarget = Join-Path $temporaryRoot 'report-junction-target'
    New-Item -ItemType Directory -Force -Path `
        $isolatedRizinTools, `
        $isolatedWorkCopy, `
        $isolatedArtifacts, `
        $junctionTarget | Out-Null
    $isolatedWrapper = Join-Path $isolatedTools 'Invoke-RizinAnalysis.ps1'
    [System.IO.File]::Copy($wrapper, $isolatedWrapper)
    [System.IO.File]::Copy(
        (Join-Path $repositoryRoot 'tools\rizin\export_function_report.py'),
        (Join-Path $isolatedRizinTools 'export_function_report.py'))
    $isolatedInput = Join-Path $isolatedWorkCopy 'Synthetic Module.bin'
    [System.IO.File]::Copy($inputFile, $isolatedInput)
    $reportJunction = Join-Path $isolatedArtifacts 'rizin'
    New-Item `
        -ItemType Junction `
        -Path $reportJunction `
        -Target $junctionTarget | Out-Null
    $isolatedParameters = $common.Clone()
    $isolatedParameters.InputFile = $isolatedInput
    Assert-Fails `
        -ExpectedMessage 'must not be a symbolic link or junction' `
        -Action {
            & $isolatedWrapper @isolatedParameters
        }
    Assert-True -Condition (
        @(Get-ChildItem -LiteralPath $junctionTarget -Force).Count -eq 0
    ) 'a report junction is rejected before writing through it'
    [System.IO.Directory]::Delete($reportJunction)

    $inputJunctionRepository = Join-Path `
        $temporaryRoot `
        'input-junction-repository'
    $inputJunctionTools = Join-Path $inputJunctionRepository 'tools'
    $inputJunctionRizinTools = Join-Path $inputJunctionTools 'rizin'
    $inputJunctionAnalysis = Join-Path $inputJunctionRepository 'analysis'
    $inputJunctionTarget = Join-Path $temporaryRoot 'input-junction-target'
    $inputJunctionDirectory = Join-Path $inputJunctionTarget 'synthetic'
    New-Item -ItemType Directory -Force -Path `
        $inputJunctionRizinTools, `
        $inputJunctionAnalysis, `
        $inputJunctionDirectory | Out-Null
    $inputJunctionWrapper = Join-Path `
        $inputJunctionTools `
        'Invoke-RizinAnalysis.ps1'
    [System.IO.File]::Copy($wrapper, $inputJunctionWrapper)
    [System.IO.File]::Copy(
        (Join-Path $repositoryRoot 'tools\rizin\export_function_report.py'),
        (Join-Path $inputJunctionRizinTools 'export_function_report.py'))
    $inputBehindJunction = Join-Path `
        $inputJunctionDirectory `
        'Synthetic Module.bin'
    [System.IO.File]::Copy($inputFile, $inputBehindJunction)
    $workCopiesJunction = Join-Path $inputJunctionAnalysis 'work-copies'
    New-Item `
        -ItemType Junction `
        -Path $workCopiesJunction `
        -Target $inputJunctionTarget | Out-Null
    $inputJunctionParameters = $common.Clone()
    $inputJunctionParameters.InputFile = Join-Path `
        $workCopiesJunction `
        'synthetic\Synthetic Module.bin'
    Assert-Fails `
        -ExpectedMessage 'must not be a symbolic link or junction' `
        -Action {
            & $inputJunctionWrapper @inputJunctionParameters
        }
    [System.IO.Directory]::Delete($workCopiesJunction)

    $result = & $wrapper @common -SleighHome $sleighHome
    Assert-True -Condition (
        $result -eq $reportFile
    ) -Message 'wrapper returns the deterministic artifacts/rizin report path'
    Assert-True -Condition (
        Test-Path -LiteralPath $reportFile -PathType Leaf
    ) -Message 'wrapper publishes the validated report'
    $reportText = Get-Content -LiteralPath $reportFile -Raw
    Assert-True -Condition (
        $reportText -notlike "*$repositoryRoot*"
    ) -Message 'published report does not contain the repository path'
    Assert-True -Condition (
        $reportText -notlike "*$inputFile*"
    ) -Message 'published report does not contain the input path'

    $arguments = [string[]](Get-Content -LiteralPath $argumentLog)
    Assert-True -Condition (
        $arguments[0] -eq (
            Join-Path $repositoryRoot 'tools\rizin\export_function_report.py'
        )
    ) -Message 'wrapper calls the committed exporter'
    Assert-Contains $arguments '--input' 'input flag is an individual argument'
    Assert-Contains $arguments (
        (Resolve-Path -LiteralPath $inputFile).Path
    ) 'canonical work-copy path reaches the exporter'
    Assert-Contains $arguments '--rizin-home' 'Rizin home flag is forwarded'
    Assert-Contains $arguments (
        (Resolve-Path -LiteralPath $fakeRizinHome).Path
    ) 'Rizin home directory reaches the exporter'
    Assert-Contains $arguments '--function-id' 'function ID flag is forwarded'
    Assert-Contains $arguments $functionId 'function ID reaches the exporter'
    Assert-Contains $arguments '--rva' 'RVA flag is forwarded'
    Assert-Contains $arguments $expectedRva 'RVA is canonicalized'
    Assert-Contains $arguments '--sleigh-home' 'optional Sleigh home is forwarded'
    Assert-Contains $arguments (
        (Resolve-Path -LiteralPath $sleighHome).Path
    ) 'Sleigh home reaches the exporter'
    Assert-True -Condition (
        $arguments -notcontains '--create-missing-function'
    ) -Message 'missing-function creation remains disabled by default'

    $result = & $wrapper @common -CreateMissingFunction
    Assert-True -Condition (
        $result -eq $reportFile
    ) -Message 'wrapper accepts explicit missing-function creation opt-in'
    $arguments = [string[]](Get-Content -LiteralPath $argumentLog)
    Assert-Contains $arguments '--create-missing-function' (
        'explicit missing-function creation opt-in is forwarded')

    $outsideParameters = $common.Clone()
    $outsideParameters.InputFile = $outsideInput
    $outsideParameters.ExpectedSha256 = $outsideHash
    Assert-Fails -ExpectedMessage 'below analysis/work-copies' -Action {
        & $wrapper @outsideParameters
    }
    $wrongHashParameters = $common.Clone()
    $wrongHashParameters.ExpectedSha256 = '0' * 64
    Assert-Fails -ExpectedMessage 'does not match ExpectedSha256' -Action {
        & $wrapper @wrongHashParameters
    }
    $invalidHashParameters = $common.Clone()
    $invalidHashParameters.ExpectedSha256 = 'not-a-hash'
    Assert-Fails -ExpectedMessage 'exactly 64 hexadecimal' -Action {
        & $wrapper @invalidHashParameters
    }
    $invalidIdParameters = $common.Clone()
    $invalidIdParameters.FunctionId = 'fn-test-00001234'
    Assert-Fails -ExpectedMessage 'canonical FN-MODULE-RVA' -Action {
        & $wrapper @invalidIdParameters
    }
    $invalidRvaParameters = $common.Clone()
    $invalidRvaParameters.Rva = 'not-hex'
    Assert-Fails -ExpectedMessage '32-bit hexadecimal' -Action {
        & $wrapper @invalidRvaParameters
    }
    $mismatchedRvaParameters = $common.Clone()
    $mismatchedRvaParameters.Rva = '5678'
    Assert-Fails -ExpectedMessage 'suffix must match' -Action {
        & $wrapper @mismatchedRvaParameters
    }
    $invalidRizinParameters = $common.Clone()
    $invalidRizinParameters.RizinHome = $sleighHome
    Assert-Fails -ExpectedMessage 'RizinHome must contain' -Action {
        & $wrapper @invalidRizinParameters
    }
    $invalidPythonParameters = $common.Clone()
    $invalidPythonParameters.PythonPath = (
        Join-Path $temporaryRoot 'missing-python.exe')
    Assert-Fails -ExpectedMessage 'PythonPath must identify an existing file' `
        -Action {
            & $wrapper @invalidPythonParameters
        }

    foreach ($case in @(
        @{
            Mode = 'exit-failure'
            Message = 'failed with exit code 9'
        },
        @{
            Mode = 'no-report'
            Message = 'did not create its report'
        },
        @{
            Mode = 'invalid-json'
            Message = 'created invalid JSON'
        },
        @{
            Mode = 'stale-report'
            Message = 'fresh non-empty report'
        },
        @{
            Mode = 'wrong-schema'
            Message = 'schema is invalid'
        },
        @{
            Mode = 'wrong-hash'
            Message = 'source hash is invalid'
        },
        @{
            Mode = 'wrong-id'
            Message = 'function ID is invalid'
        },
        @{
            Mode = 'wrong-rva'
            Message = 'function RVA is invalid'
        },
        @{
            Mode = 'multiple-functions'
            Message = 'exactly one function'
        },
        @{
            Mode = 'local-path'
            Message = 'local filesystem path'
        }
    )) {
        $env:AIRFIX_FAKE_RIZIN_MODE = $case.Mode
        Assert-Fails -ExpectedMessage $case.Message -Action {
            & $wrapper @common
        }
    }

    $publishedAfterFailures = Get-Content -LiteralPath $reportFile -Raw
    Assert-True -Condition (
        $publishedAfterFailures -ceq $reportText
    ) -Message 'failed exports do not replace the last validated report'

    Write-Output 'Invoke-RizinAnalysis tests passed.'
}
finally {
    $env:AIRFIX_FAKE_RIZIN_HELPER = $savedHelper
    $env:AIRFIX_FAKE_RIZIN_ARGUMENT_LOG = $savedArgumentLog
    $env:AIRFIX_FAKE_RIZIN_MODE = $savedMode
    $env:AIRFIX_FAKE_RIZIN_POWERSHELL = $savedPowerShell

    if (Test-Path -LiteralPath $reportFile) {
        Remove-Item -LiteralPath $reportFile -Force
    }

    $resolvedWorkCopyDirectory = [System.IO.Path]::GetFullPath(
        $workCopyDirectory)
    $resolvedWorkCopiesRoot = [System.IO.Path]::GetFullPath($workCopiesRoot)
    if ($resolvedWorkCopyDirectory.StartsWith(
            $resolvedWorkCopiesRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedWorkCopyDirectory)) {
        Remove-Item -LiteralPath $resolvedWorkCopyDirectory -Recurse -Force
    }

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

# Expected fake-process failures leave LASTEXITCODE nonzero after the exception
# is asserted. Normalize it only after all tests and cleanup have passed.
$global:LASTEXITCODE = 0
