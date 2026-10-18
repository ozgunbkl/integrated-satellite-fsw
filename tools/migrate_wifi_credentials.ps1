[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourcePath = Join-Path $projectRoot 'main/main.c'
$privatePath = Join-Path $projectRoot 'local/wifi_credentials.h'
$sourceText = [IO.File]::ReadAllText($sourcePath)
$ssidMatch = [regex]::Match($sourceText, '(?m)^#define\s+WIFI_SSID\s+(.+)$')
$passwordMatch = [regex]::Match($sourceText, '(?m)^#define\s+WIFI_PASSWORD\s+(.+)$')
if (-not $ssidMatch.Success -or -not $passwordMatch.Success) {
    if (Test-Path -LiteralPath $privatePath) { Write-Host 'Credentials already migrated; no values printed.'; return }
    throw 'Expected two credential definitions; no files changed.'
}
if (Test-Path -LiteralPath $privatePath) { throw 'Refusing to overwrite existing private credentials.' }
$null = New-Item -ItemType Directory -Path (Join-Path $projectRoot 'local') -Force
$utf8 = New-Object Text.UTF8Encoding($false)
# Mechanical relocation only. Preserve the original C literals without logging them.
$privateText = "#ifndef WIFI_CREDENTIALS_H`n#define WIFI_CREDENTIALS_H`n#define FSW_WIFI_SSID " + $ssidMatch.Groups[1].Value.Trim() + "`n#define FSW_WIFI_PASSWORD " + $passwordMatch.Groups[1].Value.Trim() + "`n#endif`n"
[IO.File]::WriteAllText($privatePath, $privateText, $utf8)
$updatedText = [regex]::Replace($sourceText, '(?m)^#define\s+WIFI_(SSID|PASSWORD)\s+.+\r?\n', '')
[IO.File]::WriteAllText($sourcePath, $updatedText, $utf8)
Write-Host 'Credentials relocated into ignored local configuration; values were not printed.'
