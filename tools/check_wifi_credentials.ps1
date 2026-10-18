[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $projectRoot
try {
    & git check-ignore --quiet -- local/wifi_credentials.h
    if ($LASTEXITCODE -ne 0) { throw 'Private credentials are not excluded by Git.' }
    $privatePath = Join-Path $projectRoot 'local/wifi_credentials.h'
    if (-not (Test-Path -LiteralPath $privatePath)) { throw 'Private configuration missing.' }
    $privateText = [IO.File]::ReadAllText($privatePath)
    $privateValues = @()
    foreach ($key in @('SSID', 'PASSWORD')) {
        $definition = [regex]::Match($privateText, '(?m)^#define\s+FSW_WIFI_' + $key + '\s+(".*")\s*$')
        if (-not $definition.Success) { throw 'Invalid private configuration shape; values withheld.' }
        $privateValues += $definition.Groups[1].Value
    }
    $files = @(& git ls-files --cached --others --exclude-standard)
    foreach ($relativePath in $files) {
        if (-not (Test-Path -LiteralPath $relativePath -PathType Leaf)) { continue }
        if ([IO.Path]::GetExtension($relativePath) -notin @('.c', '.h', '.md', '.ini', '.py', '.ps1', '.txt', '.json', '.yml', '.yaml')) { continue }
        $contents = [IO.File]::ReadAllText((Join-Path $projectRoot $relativePath))
        foreach ($privateValue in $privateValues) {
            if ($contents.Contains($privateValue)) { throw "Private value found in non-ignored file: $relativePath (value withheld)." }
        }
    }
    $previousMain = (& git show HEAD:main/main.c 2>$null) -join "`n"
    $historicalExposure = $false
    foreach ($privateValue in $privateValues) { if ($previousMain.Contains($privateValue)) { $historicalExposure = $true } }
    Write-Host 'PASS: current non-ignored sources contain no matching private credential literals; local configuration is ignored.'
    Write-Host "Historical HEAD exposure: $historicalExposure. History was not rewritten; compiled firmware may still contain credentials."
} finally { Pop-Location }
