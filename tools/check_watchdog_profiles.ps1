[CmdletBinding()]
param([string]$Compiler = 'C:\cygwin64\bin\gcc.exe', [switch]$SkipNormalElf)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputDirectory = Join-Path $projectRoot '.pio/host-tests'
$null = New-Item -ItemType Directory -Path $outputDirectory -Force
$watchdogFlags = @('FSW_TEST_WDT_TASK_LOSS', 'FSW_TEST_WDT_SUPERVISOR_STALL',
                   'FSW_TEST_WDT_IDLE_STARVATION', 'FSW_TEST_WDT_INTERRUPT_MASK', 'FSW_TEST_WDT_GLOBAL_STARVATION',
                   'FSW_TEST_WDT_TASK_RECOVERY')
$otherFlags = @('FSW_TEST_WIFI_TIMEOUT', 'FSW_TEST_WIFI_LIFECYCLE', 'FSW_TEST_WIFI_INTERFACE_FAILURE',
               'FSW_TEST_NVS_INIT_FAILURE', 'FSW_TEST_NETIF_INIT_FAILURE', 'FSW_TEST_EVENT_LOOP_FAILURE')
$cases = @(
    for ($i = 0; $i -lt $watchdogFlags.Count; ++$i) {
        for ($j = $i + 1; $j -lt $watchdogFlags.Count; ++$j) {
            @{ Name="wdt_pair_$i`_$j"; Flags=@(('-D' + $watchdogFlags[$i] + '=1'), ('-D' + $watchdogFlags[$j] + '=1')); Message='Select one watchdog test profile' }
        }
        for ($j = 0; $j -lt $otherFlags.Count; ++$j) {
            @{ Name="wdt_other_$i`_$j"; Flags=@(('-D' + $watchdogFlags[$i] + '=1'), ('-D' + $otherFlags[$j] + '=1')); Message='Do not combine watchdog and network fault profiles' }
        }
    }
)
$cases += @(
    @{ Name='core_without_profile'; Flags=@('-DFSW_TEST_WDT_CORE=1'); Message='Core selector requires a starvation test profile' },
    @{ Name='core_with_task_loss'; Flags=@('-DFSW_TEST_WDT_CORE=1', '-DFSW_TEST_WDT_TASK_LOSS=1'); Message='Core selector requires a starvation test profile' },
    @{ Name='core_with_supervisor'; Flags=@('-DFSW_TEST_WDT_CORE=1', '-DFSW_TEST_WDT_SUPERVISOR_STALL=1'); Message='Core selector requires a starvation test profile' },
    @{ Name='core_with_global'; Flags=@('-DFSW_TEST_WDT_CORE=1', '-DFSW_TEST_WDT_GLOBAL_STARVATION=1'); Message='Core selector requires a starvation test profile' },
    @{ Name='core_with_recovery'; Flags=@('-DFSW_TEST_WDT_CORE=1', '-DFSW_TEST_WDT_TASK_RECOVERY=1'); Message='Core selector requires a starvation test profile' },
    @{ Name='idle_invalid_core'; Flags=@('-DFSW_TEST_WDT_CORE=2', '-DFSW_TEST_WDT_IDLE_STARVATION=1'); Message='Watchdog test core must be 0 or 1' },
    @{ Name='interrupt_negative_core'; Flags=@('-DFSW_TEST_WDT_CORE=-1', '-DFSW_TEST_WDT_INTERRUPT_MASK=1'); Message='Watchdog test core must be 0 or 1' },
    @{ Name='cpu1_iwdt_disabled'; Flags=@('-DFSW_TEST_WDT_CORE=1', '-DFSW_TEST_WDT_INTERRUPT_MASK=1', '-DCONFIG_ESP_INT_WDT_CHECK_CPU1=0'); Message='CPU1 interrupt test requires SDK CPU1 interrupt watchdog checks' }
)
Push-Location $projectRoot
try {
    foreach ($case in $cases) {
        $arguments = @('-std=c11', '-Itests/host/watchdog_injection/include', '-Itests/host/watchdog/include', '-Itests/host/watchdog_port/include',
                       '-Icomponents/watchdog', '-Icomponents/fsw_common', '-Icomponents/system_mode', '-Icomponents/fdir') + $case.Flags +
                     @('-c','components/watchdog/watchdog_test_esp32.c','-o',('.pio/host-tests/invalid_' + $case.Name + '.o'))
        $nativePreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $diagnostic = (& $Compiler @arguments 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $nativePreference }
        if ($code -eq 0 -or -not $diagnostic.Contains($case.Message)) { throw "Profile guard did not reject $($case.Name) as expected." }
        Write-Host "PASS: incompatible profile rejected: $($case.Name)"
    }
    if ($SkipNormalElf) {
        Write-Host 'SKIP: normal firmware ELF; compile guards only, no target build/execution.'
        return
    }
    $nm = 'C:\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32-elf-nm.exe'
    $symbols = & $nm '.pio/build/esp32dev/firmware.elf'
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect normal firmware ELF.' }
    if ($symbols -match 'WATCHDOG_TEST_|starvation_worker|global_worker') { throw 'Fault injection symbols found in normal firmware.' }
    Write-Host 'PASS: normal ESP32 ELF contains no WATCHDOG_TEST_ fault-injection hooks.'
} finally { Pop-Location }
