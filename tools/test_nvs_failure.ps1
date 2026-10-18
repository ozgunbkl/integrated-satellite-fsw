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
        # Native stderr warnings do not imply a failed compiler in Windows PS5.
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
    Write-Host 'BUILD: normal firmware before the explicit NVS fault profile.'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev') (Join-Path $logDirectory 'nvs_normal_build.log')
    Write-Host 'BUILD: NVS init error injection; real NVS init/erase are not called.'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_nvs_failure') (Join-Path $logDirectory 'nvs_failure_build.log')
    $restoreRequired = $true # Also restore after a partially failed test upload.
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_nvs_failure', '-t', 'upload', '--upload-port', $Port) (Join-Path $logDirectory 'nvs_failure_upload.log')
    Invoke-Checked $Python @('-B', 'tools/nvs_failure_check.py', '--port', $Port, '--reset') (Join-Path $logDirectory 'nvs_failure_check.log')
} finally {
    try {
        if ($restoreRequired) {
            Write-Host 'RESTORE: normal esp32dev firmware; existing RAM records are cleared.'
            Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev', '-t', 'upload', '--upload-port', $Port) (Join-Path $logDirectory 'nvs_normal_restore.log')
            Invoke-Checked $Python @('-B', 'tools/archive_startup_check.py', '--port', $Port, '--reset', '--network') (Join-Path $logDirectory 'nvs_normal_boot_check.log')
            Write-Host 'RESTORE PASS: normal firmware boots with an IP, ready stack and core tasks.'
        }
    } finally {
        Pop-Location
    }
}
