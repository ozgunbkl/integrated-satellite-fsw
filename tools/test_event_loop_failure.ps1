[CmdletBinding()]
param(
    [string]$Port = 'COM7',
    [string]$PlatformIO = 'C:\.platformio\penv\Scripts\platformio.exe',
    [string]$Python = 'C:\.platformio\penv\Scripts\python.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logDirectory = Join-Path $projectRoot '.pio/host-tests'
$restoreRequired = $false

function Invoke-Checked {
    param([string]$Program, [string[]]$ProgramArguments, [string]$LogFile)
    $nativeCommandPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue' # Native stderr can contain successful warnings.
        & $Program @ProgramArguments 2>&1 |
            ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath $LogFile -ErrorAction Stop
        $nativeExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $nativeCommandPreference
    }
    if ($nativeExitCode -ne 0) {
        throw "$Program failed with exit code $nativeExitCode"
    }
}

$null = New-Item -ItemType Directory -Path $logDirectory -Force
Push-Location $projectRoot
try {
    Write-Host 'BUILD: normal firmware before the explicit event-loop fault profile.'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev') (Join-Path $logDirectory 'event_normal_build.log')
    Write-Host 'BUILD: event-loop error injection; real TCP/IP initialization remains enabled.'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_event_loop_failure') (Join-Path $logDirectory 'event_failure_build.log')
    $restoreRequired = $true # A partial test upload also requires restoration.
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_event_loop_failure', '-t', 'upload', '--upload-port', $Port) (Join-Path $logDirectory 'event_failure_upload.log')
    Invoke-Checked $Python @('-B', 'tools/event_loop_failure_check.py', '--port', $Port, '--reset') (Join-Path $logDirectory 'event_failure_check.log')
} finally {
    try {
        if ($restoreRequired) {
            Write-Host 'RESTORE: normal esp32dev firmware; existing RAM records are cleared.'
            Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev', '-t', 'upload', '--upload-port', $Port) (Join-Path $logDirectory 'event_normal_restore.log')
            Invoke-Checked $Python @('-B', 'tools/archive_startup_check.py', '--port', $Port, '--reset', '--event-loop') (Join-Path $logDirectory 'event_normal_boot_check.log')
            Write-Host 'RESTORE PASS: normal firmware boots with an IP, stack/event loop and core readiness.'
        }
    } finally {
        Pop-Location
    }
}
