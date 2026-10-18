[CmdletBinding()]
param([string]$Nm = 'C:\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32-elf-nm.exe',
      [ValidateSet('esp32dev_startup_hk_failure', 'esp32dev_recovery_boot_loop')][string]$TestProfile = 'esp32dev_startup_hk_failure')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$normal = Join-Path $projectRoot '.pio/build/esp32dev'
$panic = Join-Path $projectRoot ('.pio/build/' + $TestProfile)
$normalSymbols = & $Nm (Join-Path $normal 'firmware.elf')
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect normal recovery ELF.' }
$panicSymbols = & $Nm (Join-Path $panic 'firmware.elf')
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect panic recovery ELF.' }
$unwanted = 'WATCHDOG_TEST_|__wrap_xTaskCreatePinnedToCore|MISSION_LIFECYCLE_TEST_|__wrap_esp_|__wrap_nvs_'
if ($normalSymbols -match ('RECOVERY_LOOP_TEST_|__wrap_HK_Init|' + $unwanted) -or -not ($normalSymbols -match 'RECOVERY_Init')) {
    throw 'Normal image lacks recovery or contains a test hook.'
}
if (-not ($panicSymbols -match '__wrap_HK_Init') -or -not ($panicSymbols -match 'RECOVERY_Init') -or $panicSymbols -match $unwanted) {
    throw 'Selected panic image has missing/wrong test hooks or lacks recovery.'
}
if ([bool]($panicSymbols -match 'RECOVERY_LOOP_TEST_Armed') -ne ($TestProfile -eq 'esp32dev_recovery_boot_loop')) {
    throw 'Selected image has the wrong single-PANIC/three-PANIC injection hook.'
}
foreach ($name in @('partitions.bin', 'ota_data_initial.bin')) {
    if ((Get-FileHash -LiteralPath (Join-Path $normal $name) -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $panic $name) -Algorithm SHA256).Hash) { throw 'Normal/test partition or OTA selection differs.' }
}
Write-Output 'PASS: both recovery images, selected HK hook only, normal no injection, identical partition/OTA data; no upload/port action.'
# Bootloaders may contain different compile timestamps/validation digests; their
# full per-profile bytes are separately snapshotted/hash-bound by the runner.
