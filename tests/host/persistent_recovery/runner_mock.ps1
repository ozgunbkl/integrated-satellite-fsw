# Real runner workflow/functions; all builders, uploaders, readers and HTTP are
# replaced before invocation. Only generated fixture data under .pio is written.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$source = Get-Content -Encoding UTF8 -Raw -LiteralPath (Join-Path $workspace 'tools/test_persistent_recovery.ps1')
$tokens = $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Runner source does not parse.' }
$offset = $source.LastIndexOf('Push-Location $projectRoot', [StringComparison]::Ordinal)
if ($offset -lt 0) { throw 'Runner workflow entry was not found.' }
$workflow = [scriptblock]::Create($source.Substring($offset))
$fixture = Join-Path $workspace ('.pio/host-tests/persistent-runner-mock-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixture
$script:trace = [Collections.Generic.List[string]]::new()
$Port = '__NO_DEVICE__'
$Url = 'http://unused.invalid'
$BoardIp = '192.0.2.1'
$BoardMac = '34:5f:45:aa:e5:94'
$AllowHistoricalFault = $true
$BootLoopTest = $AllowBootLoopHistory = $false
$testProfile = 'esp32dev_startup_hk_failure'
$selectedPhase = 'panic'
$expectedTestBoots = 2
$Python = $PlatformIO = $Esptool = '__NO_EXECUTABLE__'

function Write-FixtureJson($Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}
function Invoke-Checked {
    param([string]$Program, [string[]]$ProgramArguments, [string]$LogName)
    $script:trace.Add('checked:' + $LogName)
    if ($script:scenario -eq $LogName) { throw 'Selected mock step failed.' }
    if ($LogName -eq 'archive_preservation.log') {
        foreach ($name in @('original.json', 'original-retained.json')) {
            [IO.File]::WriteAllBytes((Join-Path $logDirectory $name), [byte[]](1, 2, 3))
        }
        $digest = (Get-FileHash -LiteralPath (Join-Path $logDirectory 'original.json') -Algorithm SHA256).Hash
        if ($script:scenario -eq 'bad_receipt') { $digest = 'BAD_HASH' }
        Write-FixtureJson (Join-Path $logDirectory 'preservation.json') @{
            result='PASS'; whole_capacity_verified=$true; manifest=@{record_count=30}; sha256=$digest
        }
    }
}
function Save-Images {
    param([string]$Profile)
    $script:trace.Add('snapshot:' + $Profile)
    if ($script:scenario -eq 'snapshot') { throw 'Mock image snapshot failed.' }
    $partitionHash = if ($script:scenario -eq 'partition_changed' -and $Profile -eq 'esp32dev_startup_hk_failure') { 'DIFFERENT' } else { 'PARTITION' }
    return ,@(@{Hash='BOOT'; Path=$Profile}, @{Hash=$partitionHash}, @{Hash='OTA'}, @{Hash='APP'})
}
function Read-Baseline {
    param([string]$ProofName)
    $script:trace.Add('baseline:' + $ProofName)
    if ($script:scenario -eq 'baseline') { throw 'Mock passive baseline failure.' }
    $boot = if ($script:scenario -eq 'baseline_changed' -and $ProofName -eq 'baseline_confirmed.json') { 11 } else { 10 }
    return @{boot=$boot; history_flags=2; held=0; storage_ok=1; stable=1}
}
function Read-NvsProbe {
    param([string]$ProofName, [string]$CompareName)
    $script:trace.Add('probe:' + $ProofName)
    if ($script:scenario -eq $ProofName -or
        ($script:scenario -eq 'nvs_changed' -and $ProofName -in @('after_panic.json', 'before_restore.json'))) {
        throw 'Mock missing/changed NVS invariant.'
    }
    if ($CompareName -and -not (Test-Path -LiteralPath (Join-Path $logDirectory $CompareName))) {
        throw 'Mock reference probe missing.'
    }
    Write-FixtureJson (Join-Path $logDirectory $ProofName) @{result='PASS'}
}
function Invoke-Upload {
    param([object[]]$Images, [string]$LogName)
    $script:trace.Add('upload:' + $LogName)
    if ($script:scenario -eq $LogName) { throw 'Selected mock upload failed.' }
}
function Read-Recovery {
    param([string]$Phase, [string]$ProofName, [Nullable[uint32]]$PreviousBoot)
    $script:trace.Add('reader:' + $Phase)
    if ($script:scenario -eq $Phase) { throw 'Selected mock reader failed.' }
    if ($Phase -eq 'restore' -and $PreviousBoot -ne (10 + $expectedTestBoots)) { throw 'Restore did not bind previous persistent boot.' }
    $result = if ($script:scenario -eq 'bad_panic_proof') { 'FAIL' } else { 'PASS' }
    $journals = @(for ($i=1; $i -le $expectedTestBoots; ++$i) { @{boot=10+$i; failed_boots=$i-1; flags=if ($i -eq 4) {99} else {35}} })
    Write-FixtureJson (Join-Path $logDirectory $ProofName) @{
        result=$result; boots=$expectedTestBoots; journals=$journals; initial_boot=10; expected_baseline_history=2
        selected_abort_count=if ($script:scenario -eq 'loop_bad_count') {2} else {3}
    }
    if ($script:scenario -eq 'loop_missing_flag') { $journals[-1].flags=35; Write-FixtureJson (Join-Path $logDirectory $ProofName) @{
        result='PASS'; boots=4; journals=$journals; initial_boot=10; expected_baseline_history=2; selected_abort_count=3
    } }
}

$caseCount = 0
foreach ($scenario in @('success', 'preflight.log', 'normal_build.log', 'panic_build.log', 'normal_metadata.log',
                         'image_isolation.log', 'snapshot', 'partition_changed', 'baseline', 'historical_unapproved',
                         'archive_preservation.log', 'bad_receipt', 'baseline_changed', 'before_panic.json', 'after_panic.json',
                         'nvs_changed', 'panic_upload.log', 'panic', 'bad_panic_proof', 'before_restore.json',
                         'normal_restore_upload.log', 'after_restore.json', 'restore', 'loop_success', 'loop_bad_count', 'loop_missing_flag')) {
    $script:scenario = $scenario
    $script:trace.Clear()
    $projectRoot = $fixture
    $logDirectory = Join-Path $fixture $scenario.Replace('.', '_')
    $null = New-Item -ItemType Directory -Path $logDirectory
    $restoreRequired = $selectedPassed = $false
    $panicReaderStarted = $false
    $baselineBoot = $baselineHistory = 0
    $AllowHistoricalFault = $scenario -ne 'historical_unapproved'
    $BootLoopTest = $AllowBootLoopHistory = $scenario.StartsWith('loop_')
    $selectedPhase = if ($BootLoopTest) {'boot-loop'} else {'panic'}
    $testProfile = if ($BootLoopTest) {'esp32dev_recovery_boot_loop'} else {'esp32dev_startup_hk_failure'}
    $expectedTestBoots = if ($BootLoopTest) {4} else {2}
    $normalImages = $panicImages = $null
    $failed = $false
    try { . $workflow *> $null } catch { $failed = $true }
    if ($failed -eq ($scenario -in @('success','loop_success'))) { throw "Workflow failure propagation incorrect: $scenario" }
    $uploads = @($script:trace | Where-Object { $_.StartsWith('upload:') })
    if ($scenario -in @('preflight.log', 'normal_build.log', 'panic_build.log', 'normal_metadata.log', 'image_isolation.log',
                        'snapshot', 'partition_changed', 'baseline', 'historical_unapproved',
                        'archive_preservation.log', 'bad_receipt', 'baseline_changed')) {
        if ($uploads.Count) { throw "Unsafe upload before complete preflight/preservation: $scenario" }
        if (@($script:trace | Where-Object { $_.StartsWith('probe:') }).Count) { throw 'Loader reset before preservation and baseline confirmation.' }
    } else {
        $expectedUploads = if ($scenario -eq 'before_panic.json') { 'upload:normal_restore_upload.log' } else { 'upload:panic_upload.log,upload:normal_restore_upload.log' }
        if (($uploads -join ',') -ne $expectedUploads) {
            throw "Missing/duplicate normal restore on error path: $scenario"
        }
        if ($scenario -in @('panic_upload.log', 'panic', 'bad_panic_proof', 'after_panic.json', 'loop_bad_count', 'loop_missing_flag')) {
            if (-not $script:trace.Contains('reader:fallback') -or $selectedPassed) { throw 'Failed selection became PASS or skipped fallback validation.' }
        } elseif ($scenario -in @('normal_restore_upload.log', 'before_panic.json', 'nvs_changed', 'before_restore.json', 'after_restore.json')) {
            if ($script:trace.Contains('reader:restore') -or $script:trace.Contains('reader:fallback')) { throw 'App boot/approval attempted without a successful NVS invariant.' }
        } elseif (-not $script:trace.Contains('reader:restore')) { throw 'Successful selection did not validate persistent restore.' }
        if ($scenario -in @('success','loop_success')) {
            $expectedTrace = @('baseline:baseline.json', 'checked:archive_preservation.log', 'baseline:baseline_confirmed.json',
                              'probe:before_panic.json', 'upload:panic_upload.log', 'probe:after_panic.json', ('reader:' + $selectedPhase),
                              'probe:before_restore.json', 'upload:normal_restore_upload.log', 'probe:after_restore.json', 'reader:restore')
            $lastIndex = -1
            foreach ($entry in $expectedTrace) {
                $currentIndex = $script:trace.IndexOf($entry)
                if ($currentIndex -le $lastIndex) { throw 'Baseline/preservation/NVS/write/boot order changed.' }
                $lastIndex = $currentIndex
            }
        }
    }
    ++$caseCount
}

# Exercise the real artifact/flash/read functions with fake binary/layout data.
foreach ($name in @('Save-Images', 'Invoke-Upload', 'Read-Recovery', 'Read-Baseline', 'Read-NvsProbe')) {
    $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
    if ($null -eq $definition) { throw 'Required runner function was not found.' }
    . ([scriptblock]::Create($definition.Extent.Text))
}
$projectRoot = Join-Path $fixture 'artifacts'
$logDirectory = Join-Path $projectRoot 'snapshots'
$buildRoot = Join-Path $projectRoot '.pio/build/esp32dev'
$null = New-Item -ItemType Directory -Path $logDirectory, $buildRoot
[IO.File]::WriteAllBytes((Join-Path $projectRoot 'sdkconfig.esp32dev'), [byte[]](1, 2, 3))
$sdkHash = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'sdkconfig.esp32dev') -Algorithm SHA256).Hash
$layout = @{
    app=@{offset='0x20000'; encrypted='false'}; bootloader=@{offset='0x1000'; encrypted='false'}
    'partition-table'=@{offset='0x8000'}; otadata=@{offset='0xf000'}
    flash_settings=@{flash_mode='dio'; flash_freq='40m'; flash_size='4MB'}
}
Write-FixtureJson (Join-Path $buildRoot 'flasher_args.json') $layout
foreach ($name in @('bootloader.bin', 'partitions.bin', 'ota_data_initial.bin', 'firmware.bin')) {
    [IO.File]::WriteAllBytes((Join-Path $buildRoot $name), [byte[]](1, 2, 3))
}
$images = Save-Images 'esp32dev'
$normalImages = $images
if ($images.Count -ne 4 -or @($images | Where-Object { -not $_.Hash -or -not (Test-Path -LiteralPath $_.Path) }).Count) {
    throw 'Image snapshots incomplete or not hash-bound.'
}
$script:lastArguments = @()
function Invoke-Checked {
    param([string]$Program, [string[]]$ProgramArguments, [string]$LogName)
    $script:lastArguments = $ProgramArguments
}
$UploadBaud = 460800
$UseRomLoader = $false
$BootLoopTest = $AllowBootLoopHistory = $false
Invoke-Upload $images 'mock'
if ($script:lastArguments -notcontains 'no_reset' -or $script:lastArguments -contains 'hard_reset' -or
    $script:lastArguments -contains 'erase_flash' -or $script:lastArguments -contains '0x9000' -or
    $script:lastArguments -notcontains '0x20000' -or $script:lastArguments -notcontains '460800') { throw 'Unsafe flash regions, baud or an unobserved post-upload boot.' }
++ $caseCount
$BootLoopTest = $AllowBootLoopHistory = $true
Read-Recovery 'boot-loop' 'mock_loop.json' $null
if ($script:lastArguments -notcontains '--allow-boot-loop-test' -or $script:lastArguments -contains '--allow-boot-loop-history') { throw 'Injection lacks its distinct approval or accepts prior loop history.' }
++ $caseCount
Read-Recovery 'restore' 'mock_loop_restore.json' ([uint32]14)
$historyPosition = [Array]::IndexOf($script:lastArguments, '--expected-history')
if ($script:lastArguments -notcontains '--allow-boot-loop-history' -or $script:lastArguments[$historyPosition + 1] -ne '66') { throw 'Restore dropped sticky BOOT_LOOP history/approval.' }
++ $caseCount
$AllowBootLoopHistory = $false
$script:lastArguments = @()
$rejected = $false
try { Read-Recovery 'restore' 'unused_loop.json' ([uint32]14) } catch { $rejected=$true }
if (-not $rejected -or $script:lastArguments.Count) { throw 'Loop history not approved but actual reader invoked.' }
++ $caseCount
$BootLoopTest = $AllowBootLoopHistory = $false
$UseRomLoader = $true
$UploadBaud = 115200
$script:lastArguments = @()
$rejected = $false
try { Invoke-Upload $images 'mock' } catch { $rejected=$true }
if (-not $rejected -or $script:lastArguments.Count) { throw 'Unsupported ROM path reached the uploader.' }
$UseRomLoader = $false
$UploadBaud = 460800
++ $caseCount
Read-Recovery 'restore' 'mock_proof.json' ([uint32]12)
if ($script:lastArguments -notcontains '12' -or $script:lastArguments -notcontains '--allow-persistent-writes' -or
    $script:lastArguments -notcontains '--send-recovery-commands' -or
    $script:lastArguments -notcontains '--expected-history' -or $script:lastArguments -notcontains '--allow-fault-history') { throw 'Reader lost token/history or explicit action flags.' }
++ $caseCount
Read-Recovery 'panic' 'mock_panic.json' $null
if ($script:lastArguments -notcontains '--initial-boot' -or $script:lastArguments -notcontains '10') { throw 'Selected first boot is not bound to baseline.' }
++ $caseCount
$AllowHistoricalFault = $false
$script:lastArguments = @()
$rejected = $false
try { Read-Recovery 'panic' 'unused.json' $null } catch { $rejected = $true }
if (-not $rejected -or $script:lastArguments.Count) { throw 'Historical approval missing but reader invoked.' }
$AllowHistoricalFault = $true
++ $caseCount
Write-FixtureJson (Join-Path $logDirectory 'baseline_real.json') @{result='PASS'; status=@{boot=10; history_flags=2; storage_ok=1; stable=1}}
$baselineResult = Read-Baseline 'baseline_real.json'
if ($baselineResult.boot -ne 10 -or $script:lastArguments -notcontains '--inspect-current' -or $script:lastArguments -contains '--reset') { throw 'Baseline reset the app or lost counter.' }
++ $caseCount
Write-FixtureJson (Join-Path $logDirectory 'baseline_bad.json') @{result='PASS'; status=@{boot=10; history_flags=6; storage_ok=1; stable=1}}
$rejected = $false
try { $null = Read-Baseline 'baseline_bad.json' } catch { $rejected = $true }
if (-not $rejected) { throw 'Unsafe baseline history accepted.' }
++ $caseCount
$Esptool = 'C:/__NO_EXECUTABLE__/esptool.py'
Read-NvsProbe 'probe_mock.json' 'prior_probe.json'
if ($script:lastArguments -notcontains '--compare' -or $script:lastArguments -notcontains '--expected-mac' -or
    $script:lastArguments -notcontains '--allow-ram-reset' -or $script:lastArguments -contains 'write_flash') { throw 'NVS probe lost invariants or attempted to flash.' }
++ $caseCount
[IO.File]::WriteAllBytes($images[3].Path, [byte[]](9))
$script:lastArguments = @()
$rejected = $false
try { Invoke-Upload $images 'mock' } catch { $rejected = $true }
if (-not $rejected -or $script:lastArguments.Count) { throw 'Changed image reached uploader.' }
++ $caseCount
[IO.File]::WriteAllBytes((Join-Path $projectRoot 'sdkconfig.esp32dev'), [byte[]](9))
$rejected = $false
try { Invoke-Upload $images 'mock' } catch { $rejected = $true }
if (-not $rejected -or $script:lastArguments.Count) { throw 'Changed SDK configuration reached uploader.' }
++ $caseCount
Write-Output "PASS: $caseCount persistent runner workflow/artifact cases; mock tools/HTTP/serial only, generated fixtures retained."
