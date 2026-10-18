[CmdletBinding()]
param(
    [string]$Port = 'COM7',
    [string]$PlatformIO = 'C:\.platformio\penv\Scripts\platformio.exe',
    [string]$Python = 'C:\.platformio\penv\Scripts\python.exe',
    [string]$Esptool = 'C:\.platformio\packages\tool-esptoolpy\esptool.py',
    [ValidateSet(57600, 115200)][int]$UploadBaud = 115200,
    [switch]$ContinueAfterBaseline,
    [switch]$StarvationProfiles,
    [switch]$GlobalStarvation,
    [switch]$TaskRecovery,
    [switch]$StartupProfiles,
    [switch]$MissionLifecycle,
    [switch]$AllowModeCommands,
    [ValidateSet(0, 1)][int]$StarvationCore = 0,
    [switch]$AllowRamReset
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logDirectory = Join-Path $projectRoot '.pio/host-tests'
if (([int]$GlobalStarvation.IsPresent + [int]$StarvationProfiles.IsPresent + [int]$TaskRecovery.IsPresent + [int]$StartupProfiles.IsPresent + [int]$MissionLifecycle.IsPresent) -gt 1) { throw 'Select one watchdog reset package.' }
if ($StarvationCore -ne 0 -and -not $StarvationProfiles) { throw 'CPU selection requires -StarvationProfiles.' }
if ($AllowModeCommands -and -not $MissionLifecycle) { throw 'Mode command approval requires -MissionLifecycle.' }
$profileSuffix = if ($StarvationCore -eq 1) { '_cpu1' } else { '' }
if ($MissionLifecycle) {
    if (-not $AllowRamReset) { throw 'Mission lifecycle requires explicit -AllowRamReset after archive preservation and three NOMINAL ground command approval.' }
    if ($ContinueAfterBaseline) { throw 'Mission lifecycle requires a fresh full baseline.' }
    if (-not $AllowModeCommands) { throw 'Mission lifecycle requires explicit -AllowModeCommands for three NOMINAL requests.' }
    $logDirectory = Join-Path $logDirectory ('mission-lifecycle-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
}
if ($StartupProfiles) {
    if (-not $AllowRamReset) { throw 'Startup profiles require explicit -AllowRamReset after archive preservation.' }
    if ($ContinueAfterBaseline) { throw 'Startup profiles require a fresh full baseline.' }
    $logDirectory = Join-Path $logDirectory ('startup-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
}
if ($TaskRecovery) {
    if (-not $AllowRamReset) { throw 'Task recovery requires explicit -AllowRamReset after archive preservation.' }
    if ($ContinueAfterBaseline) { throw 'Task recovery requires a fresh full baseline.' }
    $logDirectory = Join-Path $logDirectory ('wdt-recovery-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
}
if ($GlobalStarvation) {
    if (-not $AllowRamReset) { throw 'Global starvation requires explicit -AllowRamReset after archive preservation.' }
    if ($ContinueAfterBaseline) { throw 'Global starvation requires a fresh full baseline.' }
    $logDirectory = Join-Path $logDirectory ('wdt-global-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
}
if ($StarvationProfiles) {
    if (-not $AllowRamReset) { throw 'Starvation profiles require explicit -AllowRamReset after archive preservation.' }
    if ($ContinueAfterBaseline) { throw 'Starvation profiles require a fresh full baseline; continuation is not supported.' }
    $logPrefix = if ($StarvationCore -eq 1) { 'wdt-starvation-cpu1-' } else { 'wdt-starvation-' }
    $logDirectory = Join-Path $logDirectory ($logPrefix + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
}
# These legacy packages assume every recovered boot can become NOMINAL without
# a durable per-boot ground approval. Never silently bypass the new policy to
# make their old oracle pass, or flash/leave an inhibited image unexpectedly.
if (Test-Path -LiteralPath (Join-Path $projectRoot 'components/recovery/recovery.h')) {
    throw 'Legacy reset package is incompatible with persistent recovery; use a separately prepared persistent-state-aware runner.'
}
$restoreRequired = $false
function Invoke-Checked {
    param([string]$Program, [string[]]$ProgramArguments, [string]$LogName)
    $nativePreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @ProgramArguments 2>&1 | ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath (Join-Path $logDirectory $LogName) -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $nativePreference }
    if ($nativeExit -ne 0) { throw "$Program failed with exit code $nativeExit" }
}
function Invoke-FirmwareUpload {
    param([string]$Profile, [string]$LogName)
    # Same verified regions as PIO, but a conservative fixed USB baud without
    # another platformio.ini change/rebuild. Never erase/write the NVS region.
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot ('.pio/build/' + $Profile)))
    $layout = Get-Content -LiteralPath (Join-Path $buildRoot 'flasher_args.json') -Raw | ConvertFrom-Json
    if ($layout.app.offset -ne '0x20000' -or $layout.bootloader.offset -ne '0x1000' -or
        $layout.'partition-table'.offset -ne '0x8000' -or $layout.otadata.offset -ne '0xf000' -or
        $layout.flash_settings.flash_mode -ne 'dio' -or $layout.flash_settings.flash_freq -ne '40m' -or
        $layout.flash_settings.flash_size -ne '4MB' -or $layout.app.encrypted -ne 'false' -or
        $layout.bootloader.encrypted -ne 'false') { throw 'Unexpected flash layout; refusing upload.' }
    $regions = @(
        @{Offset='0x1000'; File='bootloader.bin'; Limit=28672},
        @{Offset='0x8000'; File='partitions.bin'; Limit=4096},
        @{Offset='0xf000'; File='ota_data_initial.bin'; Limit=8192},
        @{Offset='0x20000'; File='firmware.bin'; Limit=1740800}
    )
    $uploadArguments = @($Esptool, '--chip', 'esp32', '--port', $Port, '--baud', [string]$UploadBaud,
                         '--before', 'default_reset', '--after', 'hard_reset', 'write_flash', '-z',
                         '--flash_mode', 'dio', '--flash_freq', '40m', '--flash_size', '4MB')
    foreach ($region in $regions) {
        $path = [IO.Path]::GetFullPath((Join-Path $buildRoot $region.File))
        if (-not $path.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Flash file outside expected build directory.'
        }
        $file = Get-Item -LiteralPath $path
        if ($file.Length -le 0 -or $file.Length -gt $region.Limit) { throw 'Invalid flash image size.' }
        $uploadArguments += @($region.Offset, $path)
    }
    Invoke-Checked $Python $uploadArguments $LogName
}
$null = New-Item -ItemType Directory -Path $logDirectory -Force
Push-Location $projectRoot
try {
    # Build ALL images before any flash. Do not touch NVS, sdkconfig or ground.
    $profiles = if ($MissionLifecycle) { @('esp32dev', 'esp32dev_mission_lifecycle') }
                elseif ($StartupProfiles) { @('esp32dev', 'esp32dev_startup_hk_failure', 'esp32dev_startup_task_early', 'esp32dev_startup_task_late') }
                elseif ($TaskRecovery) { @('esp32dev', 'esp32dev_wdt_task_recovery') }
                elseif ($GlobalStarvation) { @('esp32dev', 'esp32dev_wdt_global_starvation') }
                elseif ($StarvationProfiles) { @('esp32dev', ('esp32dev_wdt_idle_starvation' + $profileSuffix), ('esp32dev_wdt_interrupt_mask' + $profileSuffix)) }
                else { @('esp32dev', 'esp32dev_wdt_task_loss', 'esp32dev_wdt_supervisor_stall') }
    foreach ($profile in $profiles) {
        Invoke-Checked $PlatformIO @('run', '-e', $profile) ('wdt_' + $profile + '_build.log')
    }
    if ($ContinueAfterBaseline) {
        # Resume only after the unchanged normal image already passed this
        # package's baseline. A rebuild/new image requires a fresh full run.
        $baseline = Get-Item -LiteralPath (Join-Path $logDirectory 'wdt_normal_check.log')
        $normal = Get-Item -LiteralPath (Join-Path $projectRoot '.pio/build/esp32dev/firmware.bin')
        if ($normal.LastWriteTimeUtc -gt $baseline.LastWriteTimeUtc -or
            -not (Select-String -LiteralPath $baseline.FullName -Pattern '^PASS: shared SDK timeout.*normal mission cycles')) {
            throw 'No successful baseline for the unchanged normal image; run full package.'
        }
    }
    $restoreRequired = $true # Also restore after a failed/partial first upload.
    if (-not $ContinueAfterBaseline) {
        Invoke-FirmwareUpload 'esp32dev' 'wdt_normal_upload.log'
        Invoke-Checked $Python @('-B', 'tools/watchdog_system_check.py', '--port', $Port,
                                '--mode', 'normal', '--duration', '90', '--reset') 'wdt_normal_check.log'
    }
    if ($MissionLifecycle) {
        Invoke-FirmwareUpload 'esp32dev_mission_lifecycle' 'mission_lifecycle_upload.log'
        $proofPath = Join-Path $logDirectory 'mission_lifecycle_proof.json'
        Invoke-Checked $Python @('-B', 'tools/mission_lifecycle_check.py', '--port', $Port,
                                '--reset', '--send-mode-commands', '--output', $proofPath) 'mission_lifecycle_check.log'
    } elseif ($StartupProfiles) {
        foreach ($startup in @(@{Env='esp32dev_startup_hk_failure'; Kind='hk_init'},
                               @{Env='esp32dev_startup_task_early'; Kind='task_early'},
                               @{Env='esp32dev_startup_task_late'; Kind='task_late'})) {
            Invoke-FirmwareUpload $startup.Env ('startup_' + $startup.Kind + '_upload.log')
            $proofPath = Join-Path $logDirectory ('startup_' + $startup.Kind + '_proof.json')
            Invoke-Checked $Python @('-B', 'tools/startup_required_check.py', '--port', $Port,
                                    '--profile', $startup.Kind, '--duration', '45', '--reset',
                                    '--output', $proofPath) ('startup_' + $startup.Kind + '_check.log')
        }
    } elseif ($TaskRecovery) {
        Invoke-FirmwareUpload 'esp32dev_wdt_task_recovery' 'wdt_recovery_upload.log'
        Invoke-Checked $Python @('-B', 'tools/watchdog_recovery_check.py', '--port', $Port,
                                '--reset', '--send-mode-commands') 'wdt_recovery_check.log'
    } elseif ($GlobalStarvation) {
        Invoke-FirmwareUpload 'esp32dev_wdt_global_starvation' 'wdt_global_upload.log'
        Invoke-Checked $Python @('-B', 'tools/watchdog_system_check.py', '--port', $Port,
                                '--mode', 'global_starvation', '--duration', '45', '--reset') 'wdt_global_check.log'
    } elseif ($StarvationProfiles) {
        foreach ($mode in @('idle_starvation', 'interrupt_mask')) {
            Invoke-FirmwareUpload ('esp32dev_wdt_' + $mode + $profileSuffix) ('wdt_' + $mode + '_upload.log')
            Invoke-Checked $Python @('-B', 'tools/watchdog_system_check.py', '--port', $Port,
                                    '--mode', $mode, '--fault-core', [string]$StarvationCore, '--duration', '45', '--reset') ('wdt_' + $mode + '_check.log')
        }
    } else {
        Invoke-FirmwareUpload 'esp32dev_wdt_task_loss' 'wdt_task_loss_upload.log'
        Invoke-Checked $Python @('-B', 'tools/watchdog_system_check.py', '--port', $Port,
                                '--mode', 'task_loss', '--duration', '60', '--reset') 'wdt_task_loss_check.log'
        Invoke-FirmwareUpload 'esp32dev_wdt_supervisor_stall' 'wdt_supervisor_upload.log'
        Invoke-Checked $Python @('-B', 'tools/watchdog_system_check.py', '--port', $Port,
                                '--mode', 'supervisor_stall', '--duration', '45', '--reset') 'wdt_supervisor_check.log'
    }
} finally {
    try {
        if ($restoreRequired) {
            Write-Host 'RESTORE: normal firmware; all prior RAM archive/fault state is lost.'
            try {
                Invoke-FirmwareUpload 'esp32dev' 'wdt_restore_upload.log'
            } catch {
                Write-Warning 'Conservative-baud restore failed; retry normal image with the standard PIO uploader.'
                Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev', '-t', 'upload', '--upload-port', $Port) 'wdt_restore_fallback_upload.log'
            }
            Invoke-Checked $Python @('-B', 'tools/watchdog_system_check.py', '--port', $Port,
                                    '--mode', 'normal', '--duration', '90', '--reset') 'wdt_restore_check.log'
            Write-Host 'RESTORE PASS: normal firmware and SDK-protected mission progress verified.'
            Write-Host "EVIDENCE DIRECTORY: $logDirectory"
        }
    } finally { Pop-Location }
}
