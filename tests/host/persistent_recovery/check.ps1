# Refusal guards only: nonexistent executables and no valid serial device.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$runner = Join-Path $projectRoot 'tools/test_persistent_recovery.ps1'
$tokens = $null
$parseErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors -join "`n") }
$powershell = (Get-Process -Id $PID).Path
$sentinel = Join-Path $projectRoot '.pio/host-tests/persistent_guard_nonexistent.exe'
if (Test-Path -LiteralPath $sentinel) { throw 'Unsafe refusal fixture: sentinel tool exists.' }
$base = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runner,
          '-Python', $sentinel, '-PlatformIO', $sentinel, '-Esptool', $sentinel)
$all = @('-AllowRamReset', '-AllowPersistentWrites', '-AllowRecoveryCommands', '-AllowTestRamLoss')
$cases = @(
    @{Flags=@(); Message='Requires explicit -AllowRamReset'},
    @{Flags=@('-AllowRamReset'); Message='Requires explicit -AllowPersistentWrites'},
    @{Flags=@('-AllowRamReset', '-AllowPersistentWrites'); Message='Requires explicit -AllowRecoveryCommands'},
    @{Flags=@('-AllowRamReset', '-AllowPersistentWrites', '-AllowRecoveryCommands'); Message='Requires explicit -AllowTestRamLoss'},
    @{Flags=($all + @('-BootLoopTest')); Message='BOOT_LOOP selection requires separate'},
    @{Flags=($all + @('-AllowBootLoopHistory')); Message='BOOT_LOOP selection requires separate'},
    @{Flags=($all + @('-UseRomLoader')); Message='ROM loader is disabled in this preservation-bound workflow'},
    @{Flags=($all + @('-BoardIp', 'not-an-ip')); Message='BoardIp must be an IPv4 address'},
    @{Flags=($all + @('-BoardIp', '::1')); Message='BoardIp must be an IPv4 address'},
    @{Flags=($all + @('-Port', '__HOST_NO_DEVICE__')); Message='An explicit Windows COM port is required'},
    @{Flags=($all + @('-BoardMac', 'not-a-mac')); Message='An explicit expected board MAC is required'},
    @{Flags=($all + @('-Url', 'http://localhost:8090/path')); Message='Url must be an HTTP(S) server origin'},
    @{Flags=($all + @('-Url', 'http://user:password@localhost:8090')); Message='Url must be an HTTP(S) server origin'},
    @{Flags=($all + @('-UploadBaud', '9600')); Message='ValidateSet'},
    @{Flags=$all; Message='A required local tool does not exist'}
)
foreach ($case in $cases) {
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $arguments = $base + $case.Flags
        $diagnostic = (& $powershell @arguments 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    if ($code -eq 0 -or -not $diagnostic.Contains($case.Message)) { throw "Persistent recovery runner refusal failed: $($case.Message)" }
}
Write-Output "PASS: persistent recovery runner parses; $($cases.Count) early refusals with nonexistent tools and no device access."
& (Join-Path $PSScriptRoot 'runner_mock.ps1')
