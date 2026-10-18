[CmdletBinding()]
param(
    # Separate, explicit consent is required for this temporary existing-rule change.
    [switch]$SuspendPythonUdpBlock
)

# Run as Administrator. This test never requests an ESP32 reset or archive wipe.
# Our allow rule is removed in finally. With the opt-in switch, only the verified
# Python/Public UDP block is suspended and its original Enabled state is restored.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$pythonPath = 'C:\.platformio\penv\Scripts\python.exe'
$receiverPath = Join-Path $PSScriptRoot 'archive_downlink_check.py'
$logDirectory = Join-Path $projectRoot '.pio/host-tests'
$logPath = Join-Path $logDirectory 'archive_udp_firewall_check.log'
$ruleName = 'FSW-Archive-UDP-Test-' + [guid]::NewGuid().ToString('N')
$ruleCreated = $false
$udpBlockName = '{B67848EB-AE9E-40F6-8527-3F4D453CC2C8}'
$tcpBlockName = '{DEF70608-A2A4-4C90-BEAE-F8729D3C4ACF}'
$pythonBasePath = 'C:\.platformio\python3\python.exe'
$originalUdpEnabled = $null
$originalTcpEnabled = $null
$blockRestoreRequired = $false
$blockStopwatch = $null
$transcriptStarted = $false
$testExitCode = 1

try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator privileges are required; no firewall changes were made.'
    }
    if (-not (Test-Path -LiteralPath $pythonPath -PathType Leaf)) {
        throw 'PlatformIO Python was not found.'
    }
    $null = New-Item -ItemType Directory -Path $logDirectory -Force
    $null = Start-Transcript -Path $logPath -Force
    $transcriptStarted = $true
    if ($SuspendPythonUdpBlock) {
        # Resolve exact pre-existing targets before changing anything. Never select
        # all Python rules or modify the TCP rule to make a test pass.
        $udpBlock = Get-NetFirewallRule -Name $udpBlockName -PolicyStore PersistentStore
        $application = $udpBlock | Get-NetFirewallApplicationFilter
        $ports = $udpBlock | Get-NetFirewallPortFilter
        $addresses = $udpBlock | Get-NetFirewallAddressFilter
        if ($udpBlock.Direction -ne 'Inbound' -or $udpBlock.Action -ne 'Block' -or
            $udpBlock.Profile -ne 'Public' -or $application.Program -ne $pythonBasePath -or
            $ports.Protocol -ne 'UDP' -or ($ports.LocalPort -join ',') -ne 'Any' -or
            ($addresses.LocalAddress -join ',') -ne 'Any' -or
            ($addresses.RemoteAddress -join ',') -ne 'Any') {
            throw 'UDP block no longer matches the approved target; no rule was changed.'
        }
        $originalUdpEnabled = $udpBlock.Enabled.ToString()
        $tcpBlock = Get-NetFirewallRule -Name $tcpBlockName -PolicyStore PersistentStore
        $tcpApplication = $tcpBlock | Get-NetFirewallApplicationFilter
        $tcpPorts = $tcpBlock | Get-NetFirewallPortFilter
        if ($tcpBlock.Direction -ne 'Inbound' -or $tcpBlock.Action -ne 'Block' -or
            $tcpApplication.Program -ne $pythonBasePath -or $tcpPorts.Protocol -ne 'TCP') {
            throw 'TCP safety check failed; no rule was changed.'
        }
        $originalTcpEnabled = $tcpBlock.Enabled.ToString()
        Write-Host "ORIGINAL UDP BLOCK: $udpBlockName, enabled=$originalUdpEnabled."
        Write-Host "TCP BLOCK UNTOUCHED: $tcpBlockName, enabled=$originalTcpEnabled."
    }
    Write-Host "TEMP RULE: $ruleName"
    Write-Host 'ALLOW: ESP32 192.168.1.5 -> PC 192.168.1.3, UDP 10015, Wi-Fi/Public only.'
    $null = New-NetFirewallRule -Name $ruleName -DisplayName $ruleName `
        -Description 'Temporary ESP32 archive test; removed when this script finishes.' `
        -Direction Inbound -Action Allow -Enabled True -Protocol UDP -LocalPort 10015 `
        -LocalAddress '192.168.1.3' -RemoteAddress '192.168.1.5' `
        -InterfaceAlias 'Wi-Fi' -Profile Public -PolicyStore PersistentStore
    $ruleCreated = $true

    $activeRule = Get-NetFirewallRule -Name $ruleName -PolicyStore ActiveStore
    Write-Host ('ACTIVE RULE: enabled={0}, profile={1}, enforcement={2}' -f
        $activeRule.Enabled, $activeRule.Profile, $activeRule.EnforcementStatus)
    if ($activeRule.Enabled -ne 'True' -or $activeRule.EnforcementStatus -ne 'Enforced') {
        throw 'Temporary allow rule is not enforced; aborting before suspending any block.'
    }
    foreach ($blockRule in Get-NetFirewallRule -PolicyStore ActiveStore `
            -Enabled True -Direction Inbound -Action Block) {
        $application = $blockRule | Get-NetFirewallApplicationFilter
        if ($application.Program -match 'python|platformio' -or $application.Program -eq 'Any') {
            $ports = $blockRule | Get-NetFirewallPortFilter
            Write-Host ('EXISTING BLOCK: name={0}, program={1}, profile={2}, protocol={3}, port={4}' -f
                $blockRule.Name, $application.Program, $blockRule.Profile,
                $ports.Protocol, ($ports.LocalPort -join ','))
        }
    }

    if ($SuspendPythonUdpBlock -and $originalUdpEnabled -eq 'True') {
        # Set the restoration obligation first, including a partially failed change.
        $blockRestoreRequired = $true
        $blockStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        Set-NetFirewallRule -Name $udpBlockName -PolicyStore PersistentStore -Enabled False
        $suspendedBlock = Get-NetFirewallRule -Name $udpBlockName -PolicyStore ActiveStore
        if ($suspendedBlock.Enabled -ne 'False') {
            throw 'The approved UDP block was not suspended.'
        }
        Write-Host "UDP BLOCK TEMPORARILY SUSPENDED: $udpBlockName."
    }

    & $pythonPath -B $receiverPath --port COM7 --ip 192.168.1.5 --listen-ip 192.168.1.3 --index 0
    $testExitCode = $LASTEXITCODE
} catch {
    Write-Host ('FAIL: ' + $_.Exception.Message)
    $testExitCode = 1
} finally {
    # Restore the existing block before removing our allowance. Both cleanup steps
    # run independently so a failure in one never prevents attempting the other.
    if ($blockRestoreRequired) {
        try {
            Set-NetFirewallRule -Name $udpBlockName -PolicyStore PersistentStore `
                -Enabled $originalUdpEnabled
            $restoredBlock = Get-NetFirewallRule -Name $udpBlockName -PolicyStore ActiveStore
            if ($restoredBlock.Enabled.ToString() -ne $originalUdpEnabled) {
                throw "UDP block state was not restored: $udpBlockName"
            }
            $blockStopwatch.Stop()
            Write-Host ('RESTORE VERIFIED: UDP block enabled={0}; temporary interval={1:N1}s.' -f
                $originalUdpEnabled, $blockStopwatch.Elapsed.TotalSeconds)
        } catch {
            Write-Host ('RESTORE FAILED: ' + $_.Exception.Message)
            Write-Host "RESTORE THIS RULE: $udpBlockName to enabled=$originalUdpEnabled."
            $testExitCode = 2
        }
    }
    if ($ruleCreated) {
        try {
            Remove-NetFirewallRule -Name $ruleName -PolicyStore PersistentStore -ErrorAction Stop
            $remaining = @(Get-NetFirewallRule -PolicyStore PersistentStore |
                Where-Object { $_.Name -eq $ruleName })
            if ($remaining.Count -ne 0) {
                throw "Temporary rule still exists: $ruleName"
            }
            Write-Host "CLEANUP VERIFIED: removed temporary rule $ruleName."
        } catch {
            Write-Host ('CLEANUP FAILED: ' + $_.Exception.Message)
            Write-Host "REMOVE THIS RULE: $ruleName"
            $testExitCode = 2
        }
    }
    if ($null -ne $originalTcpEnabled) {
        try {
            $tcpAfter = Get-NetFirewallRule -Name $tcpBlockName -PolicyStore PersistentStore
            if ($tcpAfter.Enabled.ToString() -ne $originalTcpEnabled) {
                throw 'TCP rule state changed unexpectedly; it was not modified by this script.'
            }
            Write-Host "TCP STATE VERIFIED: unchanged, enabled=$originalTcpEnabled."
        } catch {
            Write-Host ('TCP VERIFY FAILED: ' + $_.Exception.Message)
            $testExitCode = 2
        }
    }
    if ($transcriptStarted) {
        $null = Stop-Transcript
    }
}

exit $testExitCode
