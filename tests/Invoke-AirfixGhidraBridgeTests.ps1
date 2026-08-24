$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$wrapper = Join-Path $repositoryRoot 'tools\Invoke-AirfixGhidraBridge.ps1'
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'airfix-ghidra-bridge-' + [Guid]::NewGuid().ToString('N'))

$savedGhidraHome = $env:GHIDRA_HOME
$savedJavaHome = $env:JAVA_HOME
$savedHelper = $env:AIRFIX_FAKE_HELPER
$savedArgumentLog = $env:AIRFIX_FAKE_ARGUMENT_LOG
$savedFunctionJson = $env:AIRFIX_FAKE_FUNCTION_JSON
$savedKnowledgeJson = $env:AIRFIX_FAKE_KNOWLEDGE_JSON
$savedKnowledgeCapture = $env:AIRFIX_FAKE_KNOWLEDGE_CAPTURE
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

function Write-Utf8Json {
    param(
        [string]$Path,
        [object]$Value
    )

    $json = $Value | ConvertTo-Json -Depth 32
    $encoding = New-Object -TypeName System.Text.UTF8Encoding -ArgumentList $false
    [System.IO.File]::WriteAllText($Path, $json, $encoding)
}

function Read-Json {
    param([string]$Path)

    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

function Get-PostScriptArguments {
    param([string]$ArgumentLog)

    $arguments = [string[]](Get-Content -LiteralPath $ArgumentLog)
    $postScriptIndex = [Array]::IndexOf($arguments, '-postScript')
    Assert-True -Condition (
        $postScriptIndex -ge 0 -and $postScriptIndex + 2 -lt $arguments.Count
    ) -Message 'fake analyzeHeadless receives -postScript and a report path'
    return @{
        All = $arguments
        Index = $postScriptIndex
    }
}

try {
    $fakeHome = Join-Path $temporaryRoot 'ghidra'
    $fakeSupport = Join-Path $fakeHome 'support'
    $fakeJavaHome = Join-Path $temporaryRoot 'java'
    $fakeHelper = Join-Path $temporaryRoot 'Fake-GhidraHeadless.ps1'
    $argumentLog = Join-Path $temporaryRoot 'arguments.txt'
    $knowledgeCapture = Join-Path $temporaryRoot 'applied-knowledge.json'
    $projects = Join-Path $temporaryRoot 'projects'
    $reports = Join-Path $temporaryRoot 'reports'
    $compareOutput = Join-Path $temporaryRoot 'compare.json'
    $functionFixture = Join-Path $temporaryRoot 'function.json'
    $knowledgeFixture = Join-Path $temporaryRoot 'knowledge.json'
    $applyKnowledgeInput = Join-Path $temporaryRoot 'knowledge-input.json'
    $ghidraCompareInput = Join-Path $temporaryRoot 'ghidra-compare.json'
    $rizinCompareInput = Join-Path $temporaryRoot 'rizin-compare.json'

    New-Item -ItemType Directory -Force -Path `
        $fakeSupport, `
        (Join-Path $fakeJavaHome 'bin'), `
        $projects, `
        $reports | Out-Null
    New-Item -ItemType File -Force -Path `
        (Join-Path $projects 'Synthetic.gpr'), `
        (Join-Path $fakeJavaHome 'bin\java.exe') | Out-Null

    $shaUpper = 'ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789'
    $shaLower = $shaUpper.ToLowerInvariant()

    $functionFixtureValue = [ordered]@{
        schema = 'airfix.re.ghidra-function.v1'
        source = [ordered]@{ sha256 = $shaUpper }
        functions = @(
            [ordered]@{
                id = 'FN-AIRCRAFT-00003F40'
                name = 'Synthetic::ProcessEvent'
                rva = '3f40'
                boundary = [ordered]@{
                    start_rva = '3f40'
                    end_rva_exclusive = '3fb8'
                }
                signature = [ordered]@{
                    calling_convention = '__thiscall'
                    return_type = 'void'
                    arguments = @()
                }
                cfg = [ordered]@{ block_count = 2 }
                instructions = @(
                    [ordered]@{
                        rva = '3f40'
                        bytes = '55'
                        mnemonic = 'PUSH'
                        text = 'PUSH EBP'
                    }
                )
                pseudocode = [ordered]@{ text = 'void ProcessEvent(void) {}' }
                callers = @()
                callees = @(
                    [ordered]@{ site_rva = '3f50'; target_rva = '6000' }
                )
                xrefs = @()
                globals = [ordered]@{
                    reads = @([ordered]@{ target_rva = 'd610' })
                    writes = @()
                }
                this_offsets = @(
                    [ordered]@{
                        offset = '448'
                        basis = 'direct-this-register'
                        sites = @('3f40')
                    }
                )
                constants = @()
                switch_tables = @()
                indirect_calls = @()
                pcode = @(
                    [ordered]@{
                        site_rva = '3f40'
                        ordinal = 0
                        opcode = 'COPY'
                        output = 'reg:eax:4'
                        inputs = @('unique:u0:4')
                    }
                )
                x87 = @()
            }
        )
    }
    Write-Utf8Json -Path $functionFixture -Value $functionFixtureValue

    $assessment = [ordered]@{
        status = 'confirmed'
        confidence = 3
        evidence = @(
            [ordered]@{ id = 'EV-20260824-001'; summary = 'Synthetic evidence' }
        )
        apply_eligible = $true
    }
    $notes = [ordered]@{
        comments = @('Synthetic comment B', 'Synthetic comment A', 'Synthetic comment A')
        hypotheses = @('Synthetic hypothesis')
    }
    $functionKnowledge = [ordered]@{
        rva = '3f40'
        name = 'Synthetic::ProcessEvent'
        signature = [ordered]@{
            calling_convention = '__thiscall'
            return_type = 'void'
            arguments = @(
                [ordered]@{ name = 'event'; type = 'SyntheticEvent *' }
            )
        }
        this_fields = @(
            [ordered]@{
                offset = '448'
                name = 'state'
                type = 'uint32_t'
                assessment = $assessment
                notes = $notes
            }
        )
        assessment = $assessment
        notes = $notes
    }
    $eventKnowledge = [ordered]@{
        id = '5f'
        name = 'SYNTHETIC_EVENT'
        assessment = $assessment
        notes = $notes
    }
    $knowledgeFixtureValue = [ordered]@{
        schema = 'airfix.re.ghidra-knowledge.v1'
        modules = @(
            [ordered]@{
                sha256 = $shaUpper
                functions = @($functionKnowledge)
                structures = @()
                vtables = @()
                event_ids = @($eventKnowledge)
            }
        )
    }
    Write-Utf8Json -Path $knowledgeFixture -Value $knowledgeFixtureValue

    $applyKnowledgeValue = [ordered]@{
        schema = 'airfix.re.ghidra-knowledge.v1'
        modules = @(
            [ordered]@{
                sha256 = $shaUpper
                functions = @(
                    [ordered]@{
                        rva = '0x3F40'
                        name = 'Synthetic::ProcessEvent'
                        signature = [ordered]@{
                            calling_convention = '__thiscall'
                            return_type = 'void'
                            arguments = @(
                                [ordered]@{ name = 'event'; type = 'SyntheticEvent *' }
                            )
                        }
                        this_fields = @(
                            [ordered]@{
                                offset = '0x448'
                                name = 'state'
                                type = 'uint32_t'
                                assessment = $assessment
                                notes = [ordered]@{
                                    comments = @('Z comment', 'A comment', 'A comment')
                                    hypotheses = @('Z hypothesis', 'A hypothesis')
                                }
                            }
                        )
                        assessment = $assessment
                        notes = [ordered]@{
                            comments = @('Z function comment', 'A function comment')
                            hypotheses = @('Z function hypothesis', 'A function hypothesis')
                        }
                    }
                )
                structures = @()
                vtables = @()
                event_ids = @(
                    [ordered]@{
                        id = '0x5F'
                        name = 'SYNTHETIC_EVENT'
                        assessment = $assessment
                        notes = $notes
                    }
                )
            }
        )
    }
    Write-Utf8Json -Path $applyKnowledgeInput -Value $applyKnowledgeValue

    $rizinCompareValue = [ordered]@{
        schema = 'airfix.re.rizin-function.v1'
        source = [ordered]@{ sha256 = $shaLower }
        binary = [ordered]@{ image_base = '10000000' }
        functions = @(
            [ordered]@{
                rva = '0x3F40'
                boundary = [ordered]@{
                    start = '0x10003F40'
                    end_exclusive = '0x10003FB8'
                }
                cfg = [ordered]@{ blocks = @([ordered]@{}) }
                calls = @(
                    [ordered]@{ site = '0x10003F50'; target = '0x10006000' }
                )
                data_refs = @(
                    [ordered]@{ target = '0x1000D610' }
                )
            }
        )
    }
    Write-Utf8Json -Path $ghidraCompareInput -Value $functionFixtureValue
    Write-Utf8Json -Path $rizinCompareInput -Value $rizinCompareValue

    @'
$ErrorActionPreference = 'Stop'
$forwarded = [string[]]$args
$encoding = New-Object -TypeName System.Text.UTF8Encoding -ArgumentList $false
$argumentText = [string]::Join([Environment]::NewLine, $forwarded)
[System.IO.File]::WriteAllText($env:AIRFIX_FAKE_ARGUMENT_LOG, $argumentText, $encoding)

$postScriptIndex = [Array]::IndexOf($forwarded, '-postScript')
if ($postScriptIndex -lt 0 -or $postScriptIndex + 2 -ge $forwarded.Count) {
    throw 'fake analyzeHeadless did not receive a post-script report path'
}
$postScript = $forwarded[$postScriptIndex + 1]
$reportPath = $forwarded[$postScriptIndex + 2]
$parent = Split-Path -Parent $reportPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null

if ($postScript -eq 'ApplyAirfixKnowledge.java') {
    if ($postScriptIndex + 3 -ge $forwarded.Count) {
        throw 'fake apply script did not receive a knowledge path'
    }
    $knowledgePath = $forwarded[$postScriptIndex + 3]
    if (-not (Test-Path -LiteralPath $knowledgePath -PathType Leaf)) {
        throw 'fake apply script received a missing knowledge path'
    }
    [System.IO.File]::WriteAllText(
        $env:AIRFIX_FAKE_KNOWLEDGE_CAPTURE,
        [System.IO.File]::ReadAllText($knowledgePath),
        $encoding)
    [System.IO.File]::WriteAllText($reportPath, "apply=ok`n", $encoding)
    exit 0
}

$fixturePath = $null
if ($postScript -eq 'ExportAirfixFunction.java') {
    $fixturePath = $env:AIRFIX_FAKE_FUNCTION_JSON
}
elseif ($postScript -eq 'ExportAirfixKnowledge.java') {
    $fixturePath = $env:AIRFIX_FAKE_KNOWLEDGE_JSON
}
else {
    throw "fake analyzeHeadless received unexpected post-script: $postScript"
}
if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
    throw 'fake analyzeHeadless fixture was not found'
}
[System.IO.File]::WriteAllText(
    $reportPath,
    [System.IO.File]::ReadAllText($fixturePath),
    $encoding)
'@ | Set-Content -LiteralPath $fakeHelper -NoNewline

    @'
@echo off
"%AIRFIX_FAKE_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%AIRFIX_FAKE_HELPER%" %*
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath (Join-Path $fakeSupport 'analyzeHeadless.bat') -NoNewline

    $pythonCommand = Get-Command python -CommandType Application -ErrorAction SilentlyContinue
    if (-not $pythonCommand) {
        $pythonCommand = Get-Command python3 -CommandType Application -ErrorAction SilentlyContinue
    }
    if (-not $pythonCommand) {
        throw 'A real Python CLI (python or python3) is required for this bridge test.'
    }
    $pythonPath = $pythonCommand.Source
    if ([string]::IsNullOrWhiteSpace($pythonPath)) {
        $pythonPath = $pythonCommand.Name
    }

    $env:GHIDRA_HOME = $fakeHome
    $env:JAVA_HOME = $fakeJavaHome
    $env:AIRFIX_FAKE_HELPER = $fakeHelper
    $env:AIRFIX_FAKE_ARGUMENT_LOG = $argumentLog
    $env:AIRFIX_FAKE_FUNCTION_JSON = $functionFixture
    $env:AIRFIX_FAKE_KNOWLEDGE_JSON = $knowledgeFixture
    $env:AIRFIX_FAKE_KNOWLEDGE_CAPTURE = $knowledgeCapture
    $env:AIRFIX_FAKE_POWERSHELL = (Get-Process -Id $PID).Path

    $common = @{
        ProgramName = 'Synthetic.type'
        ProjectDirectory = $projects
        ProjectName = 'Synthetic'
        ReportDirectory = $reports
        PythonPath = $pythonPath
    }

    $functionParameters = $common.Clone()
    $functionReport = [string](& $wrapper @functionParameters `
        -Mode ExportFunction `
        -FunctionId 'FN-AIRCRAFT-00003F40' `
        -Rva '0x3f40')
    $expectedFunctionReport = Join-Path $reports (
        'Synthetic.type.FN-AIRCRAFT-00003F40.ghidra.json')
    Assert-True -Condition (
        $functionReport -eq $expectedFunctionReport
    ) -Message 'ExportFunction returns its deterministic .json report path'
    $functionDocument = Read-Json -Path $functionReport
    Assert-True -Condition (
        $functionDocument.source.sha256 -ceq $shaLower
    ) -Message 'ExportFunction delegates SHA-256 normalization to the Python CLI'
    Assert-True -Condition (
        $functionDocument.functions[0].rva -ceq '0x00003F40'
    ) -Message 'ExportFunction delegates function RVA normalization to the Python CLI'
    $functionArgs = Get-PostScriptArguments -ArgumentLog $argumentLog
    $functionForwarded = [string[]]$functionArgs.All
    Assert-True -Condition (
        $functionForwarded[$functionArgs.Index + 1] -ceq 'ExportAirfixFunction.java'
    ) -Message 'ExportFunction routes to the function exporter post-script'
    Assert-True -Condition (
        $functionForwarded[$functionArgs.Index + 3] -ceq 'FN-AIRCRAFT-00003F40' -and
        $functionForwarded[$functionArgs.Index + 4] -ceq '0x00003F40'
    ) -Message 'ExportFunction forwards canonical FunctionId and RVA'

    Assert-Fails -ExpectedMessage 'FunctionId RVA suffix must match Rva' -Action {
        & $wrapper @functionParameters `
            -Mode ExportFunction `
            -FunctionId 'FN-AIRCRAFT-00003F40' `
            -Rva '0x3f41'
    }

    $knowledgeParameters = $common.Clone()
    $knowledgeReport = [string](& $wrapper @knowledgeParameters `
        -Mode ExportKnowledge `
        -Rvas @('0x6000', '3f40', '0x3F40'))
    $expectedKnowledgeReport = Join-Path $reports 'Synthetic.type.knowledge.json'
    Assert-True -Condition (
        $knowledgeReport -eq $expectedKnowledgeReport
    ) -Message 'ExportKnowledge returns its deterministic .json report path'
    $knowledgeDocument = Read-Json -Path $knowledgeReport
    Assert-True -Condition (
        $knowledgeDocument.modules[0].sha256 -ceq $shaLower -and
        $knowledgeDocument.modules[0].functions[0].rva -ceq '0x00003F40' -and
        $knowledgeDocument.modules[0].event_ids[0].id -ceq '0x0000005F'
    ) -Message 'ExportKnowledge normalizes SHA-256 and knowledge RVAs'
    $knowledgeArgs = Get-PostScriptArguments -ArgumentLog $argumentLog
    $knowledgeForwarded = [string[]]$knowledgeArgs.All
    Assert-True -Condition (
        $knowledgeForwarded[$knowledgeArgs.Index + 1] -ceq 'ExportAirfixKnowledge.java'
    ) -Message 'ExportKnowledge routes to the knowledge exporter post-script'
    Assert-True -Condition (
        $knowledgeForwarded[$knowledgeArgs.Index + 3] -ceq '0x00003F40' -and
        $knowledgeForwarded[$knowledgeArgs.Index + 4] -ceq '0x00006000' -and
        $knowledgeForwarded.Count -eq $knowledgeArgs.Index + 5
    ) -Message 'ExportKnowledge forwards sorted unique canonical RVAs'

    $applyParameters = $common.Clone()
    $applyReport = [string](& $wrapper @applyParameters `
        -Mode ApplyKnowledge `
        -KnowledgeFile $applyKnowledgeInput)
    $expectedApplyReport = Join-Path $reports 'Synthetic.type.knowledge-apply.json'
    Assert-True -Condition (
        $applyReport -eq $expectedApplyReport
    ) -Message 'ApplyKnowledge returns the routed post-script report path'
    Assert-True -Condition (
        (Get-Content -LiteralPath $applyReport -Raw).Trim() -ceq 'apply=ok'
    ) -Message 'ApplyKnowledge invokes the apply post-script'
    $appliedDocument = Read-Json -Path $knowledgeCapture
    Assert-True -Condition (
        $appliedDocument.modules[0].sha256 -ceq $shaLower -and
        $appliedDocument.modules[0].functions[0].rva -ceq '0x00003F40' -and
        $appliedDocument.modules[0].event_ids[0].id -ceq '0x0000005F'
    ) -Message 'ApplyKnowledge passes a normalized temporary knowledge document'
    $applyArgs = Get-PostScriptArguments -ArgumentLog $argumentLog
    $applyForwarded = [string[]]$applyArgs.All
    Assert-True -Condition (
        $applyForwarded[$applyArgs.Index + 1] -ceq 'ApplyAirfixKnowledge.java'
    ) -Message 'ApplyKnowledge routes to the apply post-script'
    $temporaryKnowledgePath = $applyForwarded[$applyArgs.Index + 3]
    Assert-True -Condition (
        $temporaryKnowledgePath -cne (Resolve-Path -LiteralPath $applyKnowledgeInput).Path -and
        -not (Test-Path -LiteralPath $temporaryKnowledgePath)
    ) -Message 'ApplyKnowledge uses and then removes its normalized temporary file'
    Assert-True -Condition (
        @($appliedDocument.modules[0].functions[0].notes.comments)[0] -ceq 'A function comment'
    ) -Message 'ApplyKnowledge normalization sorts duplicate free-form notes'

    $compareReport = [string](& $wrapper `
        -Mode Compare `
        -GhidraReport $ghidraCompareInput `
        -RizinReport $rizinCompareInput `
        -OutputFile $compareOutput `
        -PythonPath $pythonPath)
    Assert-True -Condition (
        $compareReport -eq [System.IO.Path]::GetFullPath($compareOutput)
    ) -Message 'Compare returns the requested output path'
    $compareDocument = Read-Json -Path $compareReport
    Assert-True -Condition (
        $compareDocument.schema -ceq 'airfix.re.ghidra-rizin-diff.v1' -and
        $compareDocument.source.sha256 -ceq $shaLower -and
        $compareDocument.function.rva -ceq '0x00003F40'
    ) -Message 'Compare accepts the synthetic Rizin report and normalizes its RVA'
    Assert-True -Condition (
        @($compareDocument.differences | ForEach-Object { $_.category }) -contains 'block_count'
    ) -Message 'Compare reports a cross-tool difference from the Rizin report'

    foreach ($publicReport in @($functionReport, $knowledgeReport, $compareReport)) {
        $reportText = Get-Content -LiteralPath $publicReport -Raw
        Assert-True -Condition (
            $reportText -notlike "*$repositoryRoot*" -and
            $reportText -notlike "*$temporaryRoot*"
        ) -Message 'synthetic public reports contain no private filesystem paths'
    }

    Write-Output 'Invoke-AirfixGhidraBridge tests passed.'
}
finally {
    $env:GHIDRA_HOME = $savedGhidraHome
    $env:JAVA_HOME = $savedJavaHome
    $env:AIRFIX_FAKE_HELPER = $savedHelper
    $env:AIRFIX_FAKE_ARGUMENT_LOG = $savedArgumentLog
    $env:AIRFIX_FAKE_FUNCTION_JSON = $savedFunctionJson
    $env:AIRFIX_FAKE_KNOWLEDGE_JSON = $savedKnowledgeJson
    $env:AIRFIX_FAKE_KNOWLEDGE_CAPTURE = $savedKnowledgeCapture
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

# Native-process failures can leave LASTEXITCODE nonzero after an asserted
# exception. Normalize it after the complete test and cleanup pass.
$global:LASTEXITCODE = 0
