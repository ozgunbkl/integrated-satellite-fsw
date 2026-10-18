# Early runner refusal checks only. Never invoke a real builder/uploader/port.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$runner = Join-Path $projectRoot 'tools/test_watchdog_system.ps1'
$parseTokens = $null
$parseErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile($runner, [ref]$parseTokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors -join "`n") }
$powershell = (Get-Process -Id $PID).Path
$missingTool = Join-Path $projectRoot '.pio/host-tests/watchdog_guard_nonexistent_tool.exe'
if (Test-Path -LiteralPath $missingTool) { throw 'Refusing unsafe runner guard test: sentinel tool exists.' }
$safeArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runner,
                   '-Port', '__HOST_NO_DEVICE__', '-PlatformIO', $missingTool,
                   '-Python', $missingTool, '-Esptool', $missingTool)
$cases = @(
    @{ Flags=@('-StarvationProfiles'); Message='require explicit -AllowRamReset' },
    @{ Flags=@('-StarvationProfiles', '-StarvationCore', '1'); Message='require explicit -AllowRamReset' },
    @{ Flags=@('-StarvationCore', '1', '-AllowRamReset'); Message='CPU selection requires -StarvationProfiles' },
    @{ Flags=@('-StarvationProfiles', '-StarvationCore', '1', '-AllowRamReset', '-ContinueAfterBaseline'); Message='require a fresh full baseline' },
    @{ Flags=@('-StarvationProfiles', '-StarvationCore', '2', '-AllowRamReset'); Message='ParameterArgumentValidationError'; Parameter='StarvationCore' },
    @{ Flags=@('-GlobalStarvation'); Message='requires explicit -AllowRamReset' },
    @{ Flags=@('-GlobalStarvation', '-StarvationProfiles', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-GlobalStarvation', '-ContinueAfterBaseline', '-AllowRamReset'); Message='requires a fresh full baseline' },
    @{ Flags=@('-GlobalStarvation', '-StarvationCore', '1', '-AllowRamReset'); Message='CPU selection requires -StarvationProfiles' },
    @{ Flags=@('-TaskRecovery'); Message='requires explicit -AllowRamReset' },
    @{ Flags=@('-TaskRecovery', '-ContinueAfterBaseline', '-AllowRamReset'); Message='requires a fresh full baseline' },
    @{ Flags=@('-TaskRecovery', '-GlobalStarvation', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-TaskRecovery', '-StarvationProfiles', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-TaskRecovery', '-StarvationCore', '1', '-AllowRamReset'); Message='CPU selection requires -StarvationProfiles' },
    @{ Flags=@('-TaskRecovery', '-AllowRamReset', '-UploadBaud', '460800'); Message='ParameterArgumentValidationError'; Parameter='UploadBaud' },
    @{ Flags=@('-StartupProfiles'); Message='require explicit -AllowRamReset' },
    @{ Flags=@('-StartupProfiles', '-ContinueAfterBaseline', '-AllowRamReset'); Message='require a fresh full baseline' },
    @{ Flags=@('-StartupProfiles', '-TaskRecovery', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-StartupProfiles', '-GlobalStarvation', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-StartupProfiles', '-StarvationProfiles', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-StartupProfiles', '-StarvationCore', '1', '-AllowRamReset'); Message='CPU selection requires -StarvationProfiles' },
    @{ Flags=@('-StartupProfiles', '-AllowRamReset', '-UploadBaud', '460800'); Message='ParameterArgumentValidationError'; Parameter='UploadBaud' },
    @{ Flags=@('-MissionLifecycle'); Message='requires explicit -AllowRamReset' },
    @{ Flags=@('-MissionLifecycle', '-ContinueAfterBaseline', '-AllowRamReset'); Message='requires a fresh full baseline' },
    @{ Flags=@('-MissionLifecycle', '-StartupProfiles', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-MissionLifecycle', '-TaskRecovery', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-MissionLifecycle', '-GlobalStarvation', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-MissionLifecycle', '-StarvationProfiles', '-AllowRamReset'); Message='Select one watchdog reset package' },
    @{ Flags=@('-MissionLifecycle', '-StarvationCore', '1', '-AllowRamReset'); Message='CPU selection requires -StarvationProfiles' },
    @{ Flags=@('-MissionLifecycle', '-AllowRamReset', '-UploadBaud', '460800'); Message='ParameterArgumentValidationError'; Parameter='UploadBaud' },
    @{ Flags=@('-MissionLifecycle', '-AllowRamReset'); Message='requires explicit -AllowModeCommands' },
    @{ Flags=@('-AllowModeCommands'); Message='Mode command approval requires -MissionLifecycle' },
    @{ Flags=@(); Message='Legacy reset package is incompatible with persistent recovery' },
    @{ Flags=@('-MissionLifecycle', '-AllowRamReset', '-AllowModeCommands'); Message='Legacy reset package is incompatible with persistent recovery' }
)
foreach ($case in $cases) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $arguments = $safeArguments + $case.Flags
        $output = (& $powershell @arguments 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    # Rendered ValidateSet prose wraps differently for a longer checkout path.
    # Assert the invariant validation error ID and parameter, not display width.
    $parameterMatches = -not $case.ContainsKey('Parameter') -or
        ($output -replace '\s+', '').Contains($case.Parameter)
    if ($exitCode -eq 0 -or -not $output.Contains($case.Message) -or -not $parameterMatches) {
        throw "Watchdog runner guard failed: $($case.Message)"
    }
}
Write-Output "PASS: watchdog runner parses and rejects $($cases.Count) unsafe configurations before build/flash; nonexistent tools/no hardware port."
