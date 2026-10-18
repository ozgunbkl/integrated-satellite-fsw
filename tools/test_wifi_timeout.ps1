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
        # Windows PowerShell wraps native stderr as NativeCommandError, even for
        # successful compiler warnings. Keep those visible and check the exit code.
        $ErrorActionPreference = 'Continue'
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
    # Keep a built normal image available before placing test firmware on the board.
    Write-Host 'BUILD: normal esp32dev image first.'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev') (Join-Path $logDirectory 'wifi_normal_build.log')
    Write-Host 'BUILD: explicit timeout test profile (shared sdkconfig; temporary Wi-Fi settings in RAM).'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_wifi_timeout') (Join-Path $logDirectory 'wifi_timeout_build.log')
    $restoreRequired = $true # Even a partially failed test upload requires restoration.
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_wifi_timeout', '-t', 'upload', '--upload-port', $Port) (Join-Path $logDirectory 'wifi_timeout_upload.log')
    Invoke-Checked $Python @('-B', 'tools/wifi_timeout_check.py', '--port', $Port, '--reset') (Join-Path $logDirectory 'wifi_timeout_check.log')
} finally {
    try {
        if ($restoreRequired) {
            Write-Host 'RESTORE: uploading normal esp32dev firmware; RAM archive will be cleared.'
            Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev', '-t', 'upload', '--upload-port', $Port) (Join-Path $logDirectory 'wifi_normal_restore.log')
            Invoke-Checked $Python @('-B', 'tools/archive_startup_check.py', '--port', $Port, '--reset', '--network') (Join-Path $logDirectory 'wifi_normal_boot_check.log')
            Write-Host 'RESTORE PASS: normal firmware boots with an IP and core readiness.'
        }
    } finally {
        Pop-Location
    }
}
