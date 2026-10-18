[CmdletBinding()]
param([string]$Compiler = 'gcc', [string]$Python = 'python', [switch]$BuildMetadata)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

# No flashing, serial, UDP commands, firewall changes or target-test runners here.
& (Join-Path $PSScriptRoot 'check_test_layout.ps1') -BuildMetadata:$BuildMetadata
& (Join-Path $PSScriptRoot 'check_component_dependencies.ps1') -BuildMetadata:$BuildMetadata
foreach ($test in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests/host') -Recurse -File -Filter 'check.ps1' | Sort-Object FullName) {
    & $test.FullName
}
& (Join-Path $PSScriptRoot 'run_host_tests.ps1') -Compiler $Compiler

$pythonFiles = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests/host') -Recurse -File -Filter 'test_*.py' | Sort-Object FullName)
if (-not $pythonFiles.Count) { throw 'No Python host fixtures discovered.' }
$pythonCount = 0
foreach ($test in $pythonFiles) {
    # Separate processes avoid collisions between identically named test_monitor modules.
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = (& $Python $test.FullName 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    $countMatch = [regex]::Match($output, '(?m)^Ran (\d+) tests? in ')
    if ($code -ne 0 -or -not $countMatch.Success -or [int]$countMatch.Groups[1].Value -eq 0) {
        Write-Output $output
        throw "Python host fixture failed or ran no tests: $($test.Name)"
    }
    $pythonCount += [int]$countMatch.Groups[1].Value
    $relative = $test.FullName.Substring($projectRoot.Length + 1)
    Write-Output "PASS: $relative ($($countMatch.Groups[1].Value) Python tests)."
}
if ($BuildMetadata) {
    & (Join-Path $PSScriptRoot 'check_watchdog_profiles.ps1') -Compiler $Compiler
    & (Join-Path $PSScriptRoot 'check_startup_profiles.ps1') -Compiler $Compiler
    & (Join-Path $PSScriptRoot 'check_mission_profiles.ps1') -Compiler $Compiler
}
else { Write-Output 'SKIP: target profile/normal ELF checks (use -BuildMetadata after a normal build).' }
Write-Output "All checks passed: C/header/PowerShell + $pythonCount Python tests in $($pythonFiles.Count) files. No target test was run."
