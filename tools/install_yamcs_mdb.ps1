[CmdletBinding()]
param([Parameter(Mandatory)][string]$GroundProject, [string]$ExpectedCurrentHash, [switch]$Apply)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $projectRoot 'config/yamcs/xtce.xml'
$groundRoot = (Resolve-Path -LiteralPath $GroundProject).Path
if (-not (Test-Path -LiteralPath (Join-Path $groundRoot 'pom.xml'))) { throw 'Expected the named Yamcs Maven project.' }
$target = [IO.Path]::GetFullPath((Join-Path $groundRoot 'src/main/yamcs/mdb/xtce.xml'))
$prefix = $groundRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $target)) {
    throw 'Target is missing or outside the selected project.'
}
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$currentHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($sourceHash -eq $currentHash) { Write-Output 'NOOP: the source MDB already matches the maintained profile.'; return }
if (-not $Apply) {
    Write-Output "DRY RUN: would back up and install the maintained profile to $target"
    Write-Output "ExpectedCurrentHash=$currentHash"
    Write-Output 'No files changed. Explicit -Apply and the matching hash are required.'
    return
}
if (-not $ExpectedCurrentHash -or $currentHash -ne $ExpectedCurrentHash) { throw 'Ground MDB changed; inspect it and use the current hash. Nothing was overwritten.' }
& (Join-Path $PSScriptRoot 'check_yamcs_mdb.ps1') -GroundProject $groundRoot
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $ExpectedCurrentHash -or
    (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) {
    throw 'MDB changed during validation. Nothing was overwritten.'
}
$backup = $target + '.before-fsw-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff') + '.bak'
Copy-Item -LiteralPath $target -Destination $backup -ErrorAction Stop
try {
    # Deployment of the apply_patch-maintained artifact, not an in-place rewrite.
    Copy-Item -LiteralPath $source -Destination $target -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Installed MDB hash mismatch.' }
} catch {
    Copy-Item -LiteralPath $backup -Destination $target -Force -ErrorAction Stop
    throw
}
Write-Output "INSTALLED: $target"
Write-Output "RECOVERABLE BACKUP: $backup"
Write-Output 'Only the source MDB changed. Generated target/yamcs data, running server and board are untouched; rebuild/reload is a separate step.'
