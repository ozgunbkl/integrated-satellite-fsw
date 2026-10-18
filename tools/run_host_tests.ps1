[CmdletBinding()]
param(
    [string]$Compiler = 'gcc',
    [string[]]$TestName = @(),
    [switch]$SkipHeaders,
    [switch]$List
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputDirectory = Join-Path $projectRoot '.pio/host-tests'
$binarySuffix = if ($env:OS -eq 'Windows_NT') { '.exe' } else { '' }

. (Join-Path $PSScriptRoot 'host_test_manifest.ps1')
$plan = Get-HostTestPlan -ProjectRoot $projectRoot
$hostSuiteTests = $plan.Tests
$headerChecks = $plan.Headers

if ($List) { $hostSuiteTests.Name; return }
foreach ($name in $TestName) {
    if ($name -notin $hostSuiteTests.Name) { throw "Unknown host test: $name. Use -List for valid names." }
}
if ($TestName.Count) { $hostSuiteTests = @($hostSuiteTests | Where-Object { $_.Name -in $TestName }) }
if ($SkipHeaders) { $headerChecks = @() }
$compilerPath = (Get-Command $Compiler -CommandType Application -ErrorAction Stop |
    Select-Object -First 1).Source

function Invoke-CheckedProgram {
    param([string]$ProgramPath, [string[]]$ProgramArguments = @())
    & $ProgramPath @ProgramArguments
    if ($LASTEXITCODE -ne 0) {
        throw "$ProgramPath failed with exit code $LASTEXITCODE"
    }
}

$null = New-Item -ItemType Directory -Path $outputDirectory -Force
Push-Location $projectRoot
try {
    foreach ($headerCheck in $headerChecks) {
        $headerArguments = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic-errors', '-fsyntax-only')
        foreach ($includeDirectory in $headerCheck.Includes) {
            $headerArguments += @('-I', $includeDirectory)
        }
        $headerArguments += @('-include', $headerCheck.Name, '-include', $headerCheck.Name,
                              'tests/host/headers/translation_unit.c')
        Write-Host "Checking standalone header $($headerCheck.Name)..."
        Invoke-CheckedProgram -ProgramPath $compilerPath -ProgramArguments $headerArguments
    }
    Write-Host "PASS: $($headerChecks.Count) standalone headers."

    foreach ($hostTest in $hostSuiteTests) {
        $relativeBinary = '.pio/host-tests/' + $hostTest.Name + $binarySuffix
        $compileArguments = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter')
        foreach ($includeDirectory in $hostTest.Includes) {
            $compileArguments += @('-I', $includeDirectory)
        }
        $compileArguments += $hostTest.Sources
        $compileArguments += $hostTest.LinkFlags
        $compileArguments += @('-o', $relativeBinary)

        Write-Host "Building $($hostTest.Name)..."
        Invoke-CheckedProgram -ProgramPath $compilerPath -ProgramArguments $compileArguments
        Write-Host "Running $($hostTest.Name)..."
        if ($hostTest.ContainsKey('Cases')) {
            foreach ($case in $hostTest.Cases) {
                Invoke-CheckedProgram -ProgramPath (Join-Path $projectRoot $relativeBinary) -ProgramArguments @($case)
            }
        } else {
            Invoke-CheckedProgram -ProgramPath (Join-Path $projectRoot $relativeBinary)
        }
    }
    if ($SkipHeaders) { Write-Host 'SKIP: standalone headers (explicit -SkipHeaders).' }
    if ($TestName.Count -or $SkipHeaders) {
        Write-Host "Selected host checks passed: $($hostSuiteTests.Count) C group(s); not a full regression."
    } else {
        Write-Host "All host checks passed: $($hostSuiteTests.Count) C groups; $($headerChecks.Count) headers."
    }
} finally {
    Pop-Location
}
