[CmdletBinding()]
param(
    [string]$Port = 'COM7',
    [string]$PlatformIO = 'C:\.platformio\penv\Scripts\platformio.exe',
    [string]$Python = 'C:\.platformio\penv\Scripts\python.exe',
    [string]$Esptool = 'C:\.platformio\packages\tool-esptoolpy\esptool.py',
    [string]$Url = 'http://localhost:8090',
    [string]$BoardIp = '192.168.1.5',
    [string]$BoardMac = '34:5f:45:aa:e5:94',
    # Match the normal PlatformIO upload path that completed on this board.
    # This is a tested configuration choice, NOT a serial root-cause fix.
    [ValidateSet(57600, 115200, 460800)][int]$UploadBaud = 460800,
    [switch]$UseRomLoader,
    [switch]$AllowRamReset,
    [switch]$AllowPersistentWrites,
    [switch]$AllowRecoveryCommands,
    [switch]$AllowTestRamLoss,
    [switch]$AllowHistoricalFault,
    [switch]$BootLoopTest,
    [switch]$AllowBootLoopHistory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Refuse BEFORE tool invocation, directory creation, port access or network I/O.
if (-not $AllowRamReset) { throw 'Requires explicit -AllowRamReset after complete archive preservation approval.' }
if (-not $AllowPersistentWrites) { throw 'Requires explicit -AllowPersistentWrites: durable recovery history remains after restore.' }
if (-not $AllowRecoveryCommands) { throw 'Requires explicit -AllowRecoveryCommands for bounded rearm/stale-token/NOMINAL requests.' }
if (-not $AllowTestRamLoss) { throw 'Requires explicit -AllowTestRamLoss: only original pre-flash RAM is preserved; temporary test RAM is lost at restore.' }
if ($UseRomLoader) { throw 'ROM loader is disabled in this preservation-bound workflow; use the verified stub path. No build/reset/erase was attempted.' }
if ([bool]$BootLoopTest -ne [bool]$AllowBootLoopHistory) { throw 'BOOT_LOOP selection requires separate -BootLoopTest and -AllowBootLoopHistory approval together.' }
$boardAddress = $null
if (-not [Net.IPAddress]::TryParse($BoardIp, [ref]$boardAddress) -or $boardAddress.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'BoardIp must be an IPv4 address.'
}
if ($Port -notmatch '^COM[1-9][0-9]*$') { throw 'An explicit Windows COM port is required.' }
if ($BoardMac -notmatch '^(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$') { throw 'An explicit expected board MAC is required.' }
$origin = $null
if (-not [Uri]::TryCreate($Url, [UriKind]::Absolute, [ref]$origin) -or $origin.Scheme -notin @('http', 'https') -or
    $origin.UserInfo -or $origin.AbsolutePath -ne '/' -or $origin.Query -or $origin.Fragment) { throw 'Url must be an HTTP(S) server origin without credentials/path/query.' }
foreach ($tool in @($PlatformIO, $Python, $Esptool)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw 'A required local tool does not exist; no download/port access.' }
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logDirectory = Join-Path $projectRoot ('.pio/host-tests/persistent-recovery-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
$null = New-Item -ItemType Directory -Path $logDirectory
$restoreRequired = $false
$selectedPassed = $false
$panicReaderStarted = $false
$baselineBoot = $baselineHistory = 0
$normalImages = $panicImages = $null
$testProfile = if ($BootLoopTest) { 'esp32dev_recovery_boot_loop' } else { 'esp32dev_startup_hk_failure' }
$selectedPhase = if ($BootLoopTest) { 'boot-loop' } else { 'panic' }
$expectedTestBoots = if ($BootLoopTest) { 4 } else { 2 }
$sdkHash = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'sdkconfig.esp32dev') -Algorithm SHA256).Hash

function Invoke-Checked {
    param([string]$Program, [string[]]$ProgramArguments, [string]$LogName)
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @ProgramArguments 2>&1 | ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath (Join-Path $logDirectory $LogName) -ErrorAction Stop
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($exitCode -ne 0) { throw 'Persistent recovery package step failed; inspect its bounded proof/log.' }
}

function Save-Images {
    param([ValidateSet('esp32dev', 'esp32dev_startup_hk_failure', 'esp32dev_recovery_boot_loop')][string]$Profile)
    $buildRoot = Join-Path $projectRoot ('.pio/build/' + $Profile)
    $layout = Get-Content -Encoding UTF8 -Raw -LiteralPath (Join-Path $buildRoot 'flasher_args.json') | ConvertFrom-Json
    if ($layout.app.offset -ne '0x20000' -or $layout.bootloader.offset -ne '0x1000' -or
        $layout.'partition-table'.offset -ne '0x8000' -or $layout.otadata.offset -ne '0xf000' -or
        $layout.flash_settings.flash_mode -ne 'dio' -or $layout.flash_settings.flash_freq -ne '40m' -or
        $layout.flash_settings.flash_size -ne '4MB' -or $layout.app.encrypted -ne 'false' -or
        $layout.bootloader.encrypted -ne 'false') { throw 'Unexpected flash layout; refusing upload.' }
    $regions = @(@{Offset='0x1000'; File='bootloader.bin'; Limit=28672},
                 @{Offset='0x8000'; File='partitions.bin'; Limit=4096},
                 @{Offset='0xf000'; File='ota_data_initial.bin'; Limit=8192},
                 @{Offset='0x20000'; File='firmware.bin'; Limit=1740800})
    $imageDirectory = Join-Path $logDirectory $Profile
    $null = New-Item -ItemType Directory -Path $imageDirectory
    foreach ($region in $regions) {
        $source = Join-Path $buildRoot $region.File
        $file = Get-Item -LiteralPath $source
        if ($file.Length -le 0 -or $file.Length -gt $region.Limit) { throw 'Invalid flash image size.' }
        $destination = Join-Path $imageDirectory $region.File
        Copy-Item -LiteralPath $source -Destination $destination
        $region.Path = $destination
        $region.Hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($region.Hash -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) { throw 'Image snapshot verification failed.' }
    }
    # Never consult mutable build output after taking these verified snapshots.
    return ,$regions
}

function Invoke-Upload {
    param([object[]]$Images, [string]$LogName)
    if ($UseRomLoader) { throw 'ROM loader is disabled in this preservation-bound workflow; no uploader invoked.' }
    if ($sdkHash -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot 'sdkconfig.esp32dev') -Algorithm SHA256).Hash) {
        throw 'SDK configuration changed during the package; refusing upload.'
    }
    $arguments = @($Esptool, '--chip', 'esp32', '--port', $Port, '--baud', [string]$UploadBaud,
                   '--before', 'default_reset', '--after', 'no_reset')
    $arguments += @('write_flash', '-z',
                   '--flash_mode', 'dio', '--flash_freq', '40m', '--flash_size', '4MB')
    foreach ($image in $Images) {
        if ($image.Hash -ne (Get-FileHash -LiteralPath $image.Path -Algorithm SHA256).Hash) { throw 'A snapshotted image changed; refusing upload.' }
        $arguments += @($image.Offset, $image.Path)
    }
    # no_reset leaves the chip in the loader; the reader triggers ONE observed
    # boot. Do not add PIO/hard-reset fallback that would create unobserved boots.
    Invoke-Checked $Python $arguments $LogName
}

function Read-Recovery {
    param([string]$Phase, [string]$ProofName, [Nullable[uint32]]$PreviousBoot)
    $phaseHistory = if ($BootLoopTest -and $Phase -in @('restore','fallback')) { $baselineHistory -bor 64 } else { $baselineHistory }
    $arguments = @('-B', 'tools/persistent_recovery_check.py', '--port', $Port, '--url', $Url,
                   '--board-ip', $BoardIp, '--phase', $Phase, '--reset', '--allow-persistent-writes',
                   '--send-recovery-commands', '--expected-history', [string]$phaseHistory,
                   '--output', (Join-Path $logDirectory $ProofName))
    if ($null -ne $PreviousBoot) { $arguments += @('--previous-boot', [string]$PreviousBoot) }
    if ($Phase -ne 'restore') { $arguments += @('--initial-boot', [string]$baselineBoot) }
    if ($Phase -eq 'boot-loop') { $arguments += '--allow-boot-loop-test' }
    if ($phaseHistory -band 64) {
        if (-not $AllowBootLoopHistory) { throw 'BOOT_LOOP history approval required before actual recovery commands.' }
        $arguments += '--allow-boot-loop-history'
    }
    if ($baselineHistory -band 2) {
        if (-not $AllowHistoricalFault) { throw 'Historical FAULT approval is required before actual recovery commands.' }
        $arguments += '--allow-fault-history'
    }
    Invoke-Checked $Python $arguments ($Phase + '_reader.log')
}

function Read-Baseline {
    param([string]$ProofName)
    Invoke-Checked $Python @('-B', 'tools/persistent_recovery_check.py', '--inspect-current', '--port', $Port,
                             '--output', (Join-Path $logDirectory $ProofName)) ($ProofName + '.log') | Out-Host
    $bound = Get-Content -Encoding UTF8 -Raw -LiteralPath (Join-Path $logDirectory $ProofName) | ConvertFrom-Json
    if ($bound.result -ne 'PASS' -or $bound.status.history_flags -notin @(0, 2, 32, 34) -or
        $bound.status.boot -lt 1 -or $bound.status.boot -gt 4294967292 -or
        $bound.status.storage_ok -ne 1 -or $bound.status.stable -ne 1) {
        throw 'Completed passive baseline counter/history is unavailable; no reset authorized.'
    }
    return $bound.status
}

function Read-NvsProbe {
    param([string]$ProofName, [string]$CompareName)
    $table = $normalImages[1]
    if ($table.Hash -ne (Get-FileHash -LiteralPath $table.Path -Algorithm SHA256).Hash) { throw 'Snapshotted partition table changed before probe.' }
    $arguments = @('-B', 'tools/flash_nvs_probe.py', '--port', $Port, '--expected-mac', $BoardMac,
                   '--partition-table', $table.Path, '--esptool-dir', (Split-Path -Parent $Esptool),
                   '--allow-ram-reset', '--output', (Join-Path $logDirectory $ProofName))
    if ($CompareName) { $arguments += @('--compare', (Join-Path $logDirectory $CompareName)) }
    Invoke-Checked $Python $arguments ($ProofName + '.log')
}

Push-Location $projectRoot
try {
    # No MDB install/reload/server mutation here. Missing new runtime command
    # fails BEFORE any board command/upload, even if repo MDB passed offline.
    Invoke-Checked $Python @('-B', 'tools/persistent_recovery_check.py', '--preflight', '--url', $Url,
                             '--board-ip', $BoardIp) 'preflight.log'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev') 'normal_build.log'
    Invoke-Checked $PlatformIO @('run', '-e', $testProfile) 'panic_build.log'
    $powershell = (Get-Process -Id $PID).Path
    Invoke-Checked $powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/check_test_layout.ps1', '-BuildMetadata') 'normal_metadata.log'
    Invoke-Checked $powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/check_persistent_recovery_images.ps1', '-TestProfile', $testProfile) 'image_isolation.log'
    $normalImages = Save-Images 'esp32dev'
    $panicImages = Save-Images $testProfile
    foreach ($index in @(1, 2)) {
        if ($normalImages[$index].Hash -ne $panicImages[$index].Hash) { throw 'Snapshotted partition/OTA data differs; refusing upload.' }
    }
    $baseline = Read-Baseline 'baseline.json'
    $baselineBoot = [uint32]$baseline.boot
    $baselineHistory = [uint32]$baseline.history_flags
    if ($BootLoopTest -and $baselineBoot -gt 4294967290) { throw 'Baseline counter lacks room for four test boots and normal restore; no reset.' }
    if (($baselineHistory -band 2) -and -not $AllowHistoricalFault) {
        throw 'Existing nonfatal FAULT history requires separate -AllowHistoricalFault; no reset or history clear.'
    }
    Invoke-Checked $Python @('-B', 'tools/recovery_archive_guard.py', '--url', $Url, '--board-ip', $BoardIp,
                             '--send-retained-reads', '--first', (Join-Path $logDirectory 'original.json'),
                             '--second', (Join-Path $logDirectory 'original-retained.json'),
                             '--receipt', (Join-Path $logDirectory 'preservation.json')) 'archive_preservation.log'
    # The guard just performed two full live reads; historical exports or a
    # successful BEGIN-only log do not authorize this transition.
    $receipt = Get-Content -Encoding UTF8 -Raw -LiteralPath (Join-Path $logDirectory 'preservation.json') | ConvertFrom-Json
    if ($receipt.result -ne 'PASS' -or $receipt.whole_capacity_verified -ne $true -or $receipt.manifest.record_count -ne 30 -or
        $receipt.sha256 -ne (Get-FileHash -LiteralPath (Join-Path $logDirectory 'original.json') -Algorithm SHA256).Hash -or
        $receipt.sha256 -ne (Get-FileHash -LiteralPath (Join-Path $logDirectory 'original-retained.json') -Algorithm SHA256).Hash) {
        throw 'Fresh complete preservation receipt mismatch; refusing upload.'
    }
    $confirmed = Read-Baseline 'baseline_confirmed.json'
    if ($confirmed.boot -ne $baselineBoot -or $confirmed.history_flags -ne $baselineHistory -or $confirmed.held -ne $baseline.held) {
        throw 'Baseline boot/history/gate changed during preservation; stop before resetting.'
    }
    $restoreRequired = $true # The first loader probe ALSO resets current RAM.
    Read-NvsProbe 'before_panic.json' $null
    Invoke-Upload $panicImages 'panic_upload.log'
    Read-NvsProbe 'after_panic.json' 'before_panic.json'
    $panicReaderStarted = $true
    Read-Recovery $selectedPhase 'panic_proof.json' $null
    $proof = Get-Content -Encoding UTF8 -Raw -LiteralPath (Join-Path $logDirectory 'panic_proof.json') | ConvertFrom-Json
    if ($proof.result -ne 'PASS' -or $proof.boots -ne $expectedTestBoots -or $proof.journals.Count -ne $expectedTestBoots -or
        $proof.initial_boot -ne $baselineBoot -or $proof.expected_baseline_history -ne $baselineHistory -or
        $proof.journals[0].boot -ne $baselineBoot + 1 -or $proof.journals[-1].boot -ne $baselineBoot + $expectedTestBoots) { throw 'Selected panic proof incomplete or detached from baseline.' }
    if ($BootLoopTest -and ($proof.selected_abort_count -ne 3 -or $proof.journals[-1].failed_boots -ne 3 -or
        $proof.journals[-1].flags -ne ($baselineHistory -bor 97))) { throw 'Three PANIC/BOOT_LOOP evidence missing; selection is not PASS.' }
    $selectedPassed = $true
} finally {
    try {
        if ($restoreRequired) {
            Write-Host 'RESTORE: normal snapshotted image; NVS history MUST be verified after upload; temporary test RAM will be lost.'
            $restoreProbeReady = $false
            try {
                # Before the selected reader starts, the app must not have run;
                # a failed/partial upload is NOT permission to discard NVS changes.
                $compareBeforeRestore = if (-not $panicReaderStarted) { 'before_panic.json' } else { $null }
                Read-NvsProbe 'before_restore.json' $compareBeforeRestore
                $restoreProbeReady = $true
            } catch {
                Write-Warning 'Pre-restore NVS proof unavailable/changed. Normal flash will still be attempted, but no unproven app boot/ground approval.'
            }
            Invoke-Upload $normalImages 'normal_restore_upload.log'
            if (-not $restoreProbeReady) {
                Read-NvsProbe 'after_restore.json' $null
                throw 'Normal image written; pre-restore NVS retention is unproven. App LEFT IN LOADER; operator review required.'
            }
            Read-NvsProbe 'after_restore.json' 'before_restore.json'
            if ($selectedPassed) {
                Read-Recovery 'restore' 'restore_proof.json' ([uint32]$proof.journals[-1].boot)
            } else {
                # Failed selected test remains FAIL. This proves only safe normal
                # restore, not the selected panic/persistence scenario.
                Read-Recovery 'fallback' 'fallback_restore_proof.json' $null
            }
            if ($selectedPassed) {
                Write-Host 'RESTORE PASS: normal image; retained history, required ground gate and mission progress checked.'
            } else {
                Write-Host 'FALLBACK PASS: normal boot/health/mission progress only; selected package remains FAIL; no cross-upload retention proof.'
            }
        }
    } finally {
        Write-Host "EVIDENCE DIRECTORY: $logDirectory"
        Pop-Location
    }
}
if (-not $selectedPassed) { throw 'Selected persistent-recovery test did not pass.' }
Write-Host "PASS: selected $selectedPhase/SAFE/approval and normal-reset persistent gate; not the whole recovery matrix."
