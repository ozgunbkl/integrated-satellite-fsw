Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../../tools/test_layout.ps1')
. (Join-Path $PSScriptRoot '../../../tools/host_test_manifest.ps1')
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$main = Join-Path $projectRoot 'main/main.c'
$cases = @(
    @{ Name='normal'; File=$main; Command='gcc -DAPP=1 -Imain -c main/main.c'; Count=0 },
    @{ Name='relative'; File='main/main.c'; Command='gcc -c main/main.c'; Count=0 },
    @{ Name='undef_is_not_define'; File=$main; Command='gcc -UFSW_TEST_WDT_TASK_LOSS -c main/main.c'; Count=0 },
    @{ Name='zero_still_defined'; File=$main; Command='gcc -DFSW_TEST_WDT_TASK_LOSS=0'; Count=1 },
    @{ Name='spaced_define'; File=$main; Command='gcc -D FSW_TEST_NVS_INIT_FAILURE=1'; Count=1 },
    @{ Name='quoted_define'; File=$main; Command='gcc "-DFSW_TEST_WDT_TASK_LOSS=1"'; Count=1 },
    @{ Name='demo_define'; File=$main; Command='gcc -DSI_TEST_MODE=1'; Count=1 },
    @{ Name='host_include'; File=$main; Command='gcc -Itests/host/startup_core/include'; Count=1 },
    @{ Name='windows_include'; File=$main; Command='gcc -IC:\project\tests\host\mock'; Count=1 },
    @{ Name='generated_include'; File=$main; Command='gcc -I.pio/host-tests/include'; Count=1 },
    @{ Name='host_source'; File=(Join-Path $projectRoot 'tests/host/fixture.c'); Command='gcc -c fixture.c'; Count=1 },
    @{ Name='generated_source'; File=(Join-Path $projectRoot '.pio/host-tests/fixture.c'); Command='gcc -c fixture.c'; Count=1 }
)
foreach ($case in $cases) {
    $entry = [pscustomobject]@{file=$case.File; directory=$projectRoot; command=$case.Command}
    $errors = @(Get-ProductionTestViolations -ProjectRoot $projectRoot -Entries @($entry))
    if ($errors.Count -ne $case.Count) { throw "Unexpected guard result for $($case.Name): $($errors -join ', ')" }
}
$argumentsEntry = [pscustomobject]@{file=$main; directory=$projectRoot; arguments=@('gcc', '-D', 'FSW_TEST_NETIF_INIT_FAILURE=1')}
if (@(Get-ProductionTestViolations -ProjectRoot $projectRoot -Entries @($argumentsEntry)).Count -ne 1) {
    throw 'Compile arguments array was not checked.'
}
$plan = Get-HostTestPlan -ProjectRoot $projectRoot
if (@($plan.Tests.Name | Sort-Object -Unique).Count -ne $plan.Tests.Count) { throw 'Manifest names are not unique.' }
if (-not $plan.Headers.Count -or -not $plan.Tests.Count) { throw 'Manifest is empty.' }
Write-Output "PASS: $($cases.Count + 2) layout/manifest guard cases."
