[CmdletBinding()]
param([string]$Port = 'COM7', [int]$Duration = 600,
      [string]$PlatformIO = 'C:\.platformio\penv\Scripts\platformio.exe',
      [string]$Python = 'C:\.platformio\penv\Scripts\python.exe')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logDirectory = Join-Path $projectRoot '.pio/host-tests'
$restoreRequired = $false
function Invoke-Checked {
    param([string]$Program, [string[]]$ProgramArguments, [string]$LogName)
    $nativePreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @ProgramArguments 2>&1 | ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath (Join-Path $logDirectory $LogName) -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $nativePreference }
    if ($nativeExit -ne 0) { throw "$Program failed with exit code $nativeExit" }
}
if ($Duration -lt 300) { throw 'Use at least a five-minute bounded soak.' }
$null = New-Item -ItemType Directory -Path $logDirectory -Force
Push-Location $projectRoot
try {
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev') 'wifi_system_normal_build.log'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_wifi_interface_failure') 'wifi_system_interface_build.log'
    $restoreRequired = $true
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_wifi_interface_failure', '-t', 'upload', '--upload-port', $Port) 'wifi_system_interface_upload.log'
    Invoke-Checked $Python @('-B', 'tools/wifi_system_check.py', '--port', $Port, '--mode', 'interface', '--reset') 'wifi_system_interface_check.log'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_wifi_lifecycle') 'wifi_system_lifecycle_build.log'
    Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev_wifi_lifecycle', '-t', 'upload', '--upload-port', $Port) 'wifi_system_lifecycle_upload.log'
    Invoke-Checked $Python @('-B', 'tools/wifi_system_check.py', '--port', $Port, '--mode', 'lifecycle', '--duration', "$Duration", '--reset') 'wifi_system_lifecycle_check.log'
    Invoke-Checked $Python @('-B', 'tools/yamcs_archive_command_check.py', '--port', $Port, '--send') 'wifi_system_reconnect_yamcs_check.log'
} finally {
    try {
        if ($restoreRequired) {
            Write-Host 'RESTORE: normal firmware; RAM archive from tests is lost.'
            Invoke-Checked $PlatformIO @('run', '-e', 'esp32dev', '-t', 'upload', '--upload-port', $Port) 'wifi_system_normal_restore.log'
            Invoke-Checked $Python @('-B', 'tools/archive_startup_check.py', '--port', $Port, '--reset', '--event-loop') 'wifi_system_normal_check.log'
            Write-Host 'RESTORE PASS: normal firmware, IP and core startup verified.'
        }
    } finally { Pop-Location }
}
