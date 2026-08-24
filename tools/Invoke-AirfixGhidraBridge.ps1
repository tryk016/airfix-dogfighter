[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'ExportFunction',
        'ExportKnowledge',
        'ApplyKnowledge',
        'Compare')]
    [string]$Mode,

    [string]$ProgramName,

    [string]$InputFile,

    [string]$FunctionId,

    [string]$Rva,

    [string[]]$Rvas = @(),

    [string]$KnowledgeFile,

    [string]$GhidraReport,

    [string]$RizinReport,

    [string]$OutputFile,

    [string]$ProjectDirectory,

    [string]$ProjectName = 'AirfixDogfighter',

    [string]$ReportDirectory,

    [string]$PythonPath = 'python'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ghidraWrapper = Join-Path $PSScriptRoot 'Invoke-GhidraAnalysis.ps1'
$bridgeTool = Join-Path $PSScriptRoot 're\airfix_ghidra_bridge.py'
if ([string]::IsNullOrWhiteSpace($ProjectDirectory)) {
    $ProjectDirectory = Join-Path $PSScriptRoot '..\analysis\ghidra-projects'
}
if ([string]::IsNullOrWhiteSpace($ReportDirectory)) {
    $ReportDirectory = Join-Path $PSScriptRoot '..\artifacts\ghidra'
}

function Assert-NonEmpty {
    param([string]$Value, [string]$Name)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "$Name must not be empty."
    }
}

function Resolve-ExistingFile {
    param([string]$Path, [string]$Name)
    Assert-NonEmpty -Value $Path -Name $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name must identify an existing file."
    }
    $resolved = [System.IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath $Path).Path)
    $attributes = [System.IO.File]::GetAttributes($resolved)
    if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Name must not be a symbolic link or junction."
    }
    $parent = Split-Path -Parent $resolved
    $parentAttributes = [System.IO.File]::GetAttributes($parent)
    if (($parentAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Name parent must not be a symbolic link or junction."
    }
    return $resolved
}

function ConvertTo-CanonicalRva {
    param([string]$Value, [string]$Name)
    Assert-NonEmpty -Value $Value -Name $Name
    $text = $Value.Trim()
    if ($text.StartsWith('0x', [System.StringComparison]::OrdinalIgnoreCase)) {
        $text = $text.Substring(2)
    }
    if ($text -cnotmatch '^[0-9A-Fa-f]{1,8}$') {
        throw "$Name must contain one to eight hexadecimal digits."
    }
    $number = [Convert]::ToUInt32($text, 16)
    return ('0x{0:X8}' -f $number)
}

function Invoke-BridgePython {
    param([string[]]$Arguments)
    & $PythonPath $bridgeTool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Airfix Ghidra bridge validation failed with exit code $LASTEXITCODE."
    }
}

function Invoke-GhidraMode {
    param(
        [string]$PostScript,
        [string[]]$PostScriptArguments,
        [string]$ReportSuffix
    )

    $hasProgram = -not [string]::IsNullOrWhiteSpace($ProgramName)
    $hasInput = -not [string]::IsNullOrWhiteSpace($InputFile)
    if ($hasProgram -eq $hasInput) {
        throw 'Exactly one of ProgramName or InputFile is required.'
    }

    $parameters = @{
        ProjectDirectory = $ProjectDirectory
        ProjectName = $ProjectName
        ReportDirectory = $ReportDirectory
        PostScript = $PostScript
        PostScriptArguments = $PostScriptArguments
        ReportSuffix = $ReportSuffix
        ReportExtension = 'json'
    }
    if ($hasProgram) {
        $parameters.ProgramName = $ProgramName
        $parameters.ReuseProject = $true
    }
    else {
        $parameters.InputFile = $InputFile
    }
    $wrapperOutput = @(& $ghidraWrapper @parameters)
    if ($wrapperOutput.Count -eq 0) {
        throw 'Ghidra wrapper did not return a report path.'
    }
    return [string]$wrapperOutput[$wrapperOutput.Count - 1]
}

if (-not (Test-Path -LiteralPath $ghidraWrapper -PathType Leaf) -or
    -not (Test-Path -LiteralPath $bridgeTool -PathType Leaf)) {
    throw 'Committed Airfix Ghidra bridge tools are incomplete.'
}

switch ($Mode) {
    'ExportFunction' {
        Assert-NonEmpty -Value $FunctionId -Name 'FunctionId'
        if ($FunctionId -cnotmatch '^FN-[A-Z0-9][A-Z0-9_-]*-[0-9A-F]{8}$') {
            throw 'FunctionId must use canonical FN-MODULE-RVA form.'
        }
        $canonicalRva = ConvertTo-CanonicalRva -Value $Rva -Name 'Rva'
        if ($FunctionId.Substring($FunctionId.Length - 8) -cne
            $canonicalRva.Substring(2)) {
            throw 'FunctionId RVA suffix must match Rva.'
        }
        $report = Invoke-GhidraMode `
            -PostScript 'ExportAirfixFunction.java' `
            -PostScriptArguments @($FunctionId, $canonicalRva) `
            -ReportSuffix "$FunctionId.ghidra"
        Invoke-BridgePython -Arguments @(
            'normalize-function', '--input', $report, '--output', $report)
        Write-Output $report
        break
    }
    'ExportKnowledge' {
        if ($Rvas.Count -eq 0) {
            throw 'Rvas must contain at least one function RVA.'
        }
        $canonicalRvas = @(
            $Rvas | ForEach-Object {
                ConvertTo-CanonicalRva -Value $_ -Name 'Rvas'
            } | Sort-Object -Unique)
        $report = Invoke-GhidraMode `
            -PostScript 'ExportAirfixKnowledge.java' `
            -PostScriptArguments $canonicalRvas `
            -ReportSuffix 'knowledge'
        Invoke-BridgePython -Arguments @(
            'normalize-knowledge', '--input', $report, '--output', $report)
        Write-Output $report
        break
    }
    'ApplyKnowledge' {
        $knowledge = Resolve-ExistingFile `
            -Path $KnowledgeFile -Name 'KnowledgeFile'
        $temporary = Join-Path ([System.IO.Path]::GetTempPath()) (
            'airfix-ghidra-knowledge-' +
            [Guid]::NewGuid().ToString('N') + '.json')
        try {
            Invoke-BridgePython -Arguments @(
                'normalize-knowledge', '--input', $knowledge,
                '--output', $temporary)
            $report = Invoke-GhidraMode `
                -PostScript 'ApplyAirfixKnowledge.java' `
                -PostScriptArguments @($temporary) `
                -ReportSuffix 'knowledge-apply'
            Write-Output $report
        }
        finally {
            if (Test-Path -LiteralPath $temporary) {
                Remove-Item -LiteralPath $temporary -Force
            }
        }
        break
    }
    'Compare' {
        if (-not [string]::IsNullOrWhiteSpace($ProgramName) -or
            -not [string]::IsNullOrWhiteSpace($InputFile)) {
            throw 'Compare does not accept ProgramName or InputFile.'
        }
        $ghidra = Resolve-ExistingFile `
            -Path $GhidraReport -Name 'GhidraReport'
        $rizin = Resolve-ExistingFile `
            -Path $RizinReport -Name 'RizinReport'
        Assert-NonEmpty -Value $OutputFile -Name 'OutputFile'
        $output = [System.IO.Path]::GetFullPath($OutputFile)
        Invoke-BridgePython -Arguments @(
            'compare', '--ghidra', $ghidra, '--rizin', $rizin,
            '--output', $output)
        Write-Output $output
        break
    }
}
