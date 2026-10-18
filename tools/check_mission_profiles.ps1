[CmdletBinding()]
param([string]$Compiler = 'C:\cygwin64\bin\gcc.exe')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$otherFlags = @('FSW_TEST_STARTUP_FAILURE', 'FSW_TEST_WDT_TASK_LOSS', 'FSW_TEST_WDT_SUPERVISOR_STALL',
    'FSW_TEST_WDT_IDLE_STARVATION', 'FSW_TEST_WDT_INTERRUPT_MASK', 'FSW_TEST_WDT_GLOBAL_STARVATION',
    'FSW_TEST_WDT_TASK_RECOVERY', 'FSW_TEST_WDT_CORE', 'FSW_TEST_WIFI_TIMEOUT', 'FSW_TEST_WIFI_LIFECYCLE',
    'FSW_TEST_WIFI_INTERFACE_FAILURE', 'FSW_TEST_NVS_INIT_FAILURE', 'FSW_TEST_NETIF_INIT_FAILURE', 'FSW_TEST_EVENT_LOOP_FAILURE')
$cases = @(
    foreach ($value in @('-1', '0', '2')) {
        @{ Flags=@('-DFSW_TEST_MISSION_LIFECYCLE=' + $value); Message='Mission lifecycle selector must be 1' }
    }
    foreach ($flag in $otherFlags) {
        @{ Flags=@('-DFSW_TEST_MISSION_LIFECYCLE=1', ('-D' + $flag + '=1')); Message='Do not combine mission lifecycle and other fault profiles' }
    }
)
Push-Location $projectRoot
try {
    foreach ($case in $cases) {
        $savedPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $arguments = @('-std=c11', '-Werror', '-fsyntax-only', '-include', 'main/mission_lifecycle_test.h',
                           'tests/host/headers/translation_unit.c') + $case.Flags
            $output = (& $Compiler @arguments 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $savedPreference }
        if ($code -eq 0 -or -not $output.Contains($case.Message)) { throw 'Mission lifecycle guard did not reject invalid configuration.' }
    }
    Write-Output "PASS: $($cases.Count) invalid mission lifecycle configurations rejected; no target build/port/reset."
} finally { Pop-Location }
