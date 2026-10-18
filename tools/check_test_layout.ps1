[CmdletBinding()]
param([switch]$BuildMetadata)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_manifest.ps1')
. (Join-Path $PSScriptRoot 'test_layout.ps1')
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$plan = Get-HostTestPlan -ProjectRoot $projectRoot
$names = @($plan.Tests.Name)
if (@($names | Sort-Object -Unique).Count -ne $names.Count) { throw 'Duplicate host test names.' }
$registered = @('tests/host/headers/translation_unit.c')
foreach ($test in $plan.Tests) {
    if (-not @($test.Sources | Where-Object { $_ -like 'tests/host/*.c' }).Count) {
        throw "No maintained host fixture for $($test.Name)."
    }
    if ($test.ContainsKey('Cases')) {
        if (-not $test.Cases.Count -or @($test.Cases | Sort-Object -Unique).Count -ne $test.Cases.Count) {
            throw "Empty or duplicate process cases: $($test.Name)."
        }
    }
    foreach ($path in @($test.Includes) + @($test.Sources)) {
        if ($path -match '(?:^|[/\\])\.pio[/\\]' -or
            -not (Test-Path -LiteralPath (Join-Path $projectRoot $path))) {
            throw "Generated or missing host input: $path"
        }
    }
    $registered += $test.Sources
}
$fixtures = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests/host') -Recurse -File -Filter '*.c')
foreach ($fixture in $fixtures) {
    $relative = $fixture.FullName.Substring($projectRoot.Length + 1).Replace('\', '/')
    if ($relative -notin $registered) { throw "Unregistered host fixture: $relative" }
}
Write-Output "PASS: $($plan.Tests.Count) C groups, $($fixtures.Count) maintained C fixtures; no missing/orphan/generated host inputs."
if ($BuildMetadata) {
    $buildRoot = Join-Path $projectRoot '.pio/build/esp32dev'
    $entries = Get-Content -LiteralPath (Join-Path $buildRoot 'compile_commands.json') -Raw | ConvertFrom-Json
    $violations = @(Get-ProductionTestViolations -ProjectRoot $projectRoot -Entries $entries)
    if ($violations.Count) { throw ($violations -join "`n") }
    $elf = Get-Item -LiteralPath (Join-Path $buildRoot 'firmware.elf')
    $productionInputs = @(Get-ChildItem -Path (Join-Path $projectRoot 'main'), (Join-Path $projectRoot 'components') -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.h' }) +
        @(Get-Item -LiteralPath (Join-Path $projectRoot 'platformio.ini'), (Join-Path $projectRoot 'sdkconfig.esp32dev'))
    if (@($productionInputs | Where-Object { $_.LastWriteTimeUtc -gt $elf.LastWriteTimeUtc }).Count) {
        throw 'Normal ELF predates production sources/config; build esp32dev before checking isolation.'
    }
    $nm = 'C:\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32-elf-nm.exe'
    $symbols = & $nm $elf.FullName
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect normal firmware ELF.' }
    if ($symbols -cmatch '(?:Archive_CorruptMemoryForTest|RECOVERY_LOOP_TEST_|WATCHDOG_TEST_|WIFI_TEST_|__wrap_(?:vSolarHarvest_Task|vCommandInjectionTask|FDIR_ProcessCommand|HK_Init|xTaskCreatePinnedToCore|nvs_flash_(?:init|erase)|esp_[A-Za-z0-9_]+|YAMCS_MarkNetworkStackReady))') {
        throw 'Target fault-injection hooks found in normal firmware ELF.'
    }
    Write-Output 'PASS: fresh normal metadata/ELF: no host source/include paths, test defines or known injection hooks.'
} else { Write-Output 'SKIP: normal build metadata/ELF (use -BuildMetadata).' }
Write-Output 'LIMIT: registered host inputs and known production test leaks only; not target execution or universal preprocessing/coverage proof.'
