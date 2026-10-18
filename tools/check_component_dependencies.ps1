[CmdletBinding()]
param([switch]$BuildMetadata)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'component_graph.ps1')
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$iniLines = @(Get-Content -LiteralPath (Join-Path $projectRoot 'platformio.ini') |
    Where-Object { $_ -notmatch '^\s*[;#]' })
if ($iniLines -match '^\s*lib_extra_dirs\s*=') {
    throw 'Project components must be built by ESP-IDF, not a second LDF path.'
}
if ($iniLines -match '(?:^|\s)-I\s*["'']?(?:main|components)(?:[/\\\s"'']|$)') {
    throw 'Global project include flags hide missing CMake component dependencies.'
}
# Read-only audit of direct PROJECT header dependencies, not SDK/link coverage.
# Current registrations use literal SRCS and INCLUDE_DIRS "."; fail on missing sources.
$components = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'components') -Directory) +
              @(Get-Item -LiteralPath (Join-Path $projectRoot 'main'))
$headerOwners = @{}
foreach ($component in $components) {
    foreach ($header in Get-ChildItem -LiteralPath $component.FullName -Filter '*.h' -File) {
        $headerOwners[$header.Name] = @($headerOwners[$header.Name]) + $component.Name
    }
}
function Read-Section {
    param([string]$Text, [string]$Section)
    $pattern = '(?ms)^\s*' + $Section + '\s+(.+?)(?=^\s*(?:SRCS|INCLUDE_DIRS|PRIV_INCLUDE_DIRS|REQUIRES|PRIV_REQUIRES)\b|\))'
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) { return @() }
    return @([regex]::Matches($match.Groups[1].Value, '"([^"\r\n]+)"|([A-Za-z_][A-Za-z_0-9]*)') |
        ForEach-Object { if ($_.Groups[1].Success) { $_.Groups[1].Value } else { $_.Groups[2].Value } })
}
$missing = @()
$edgeCount = 0
$registeredSources = @()
$projectGraph = @{}
foreach ($component in $components) {
    $buildText = Get-Content -LiteralPath (Join-Path $component.FullName 'CMakeLists.txt') -Raw
    $public = @(Read-Section $buildText 'REQUIRES')
    $private = @(Read-Section $buildText 'PRIV_REQUIRES')
    $projectGraph[$component.Name] = @(@($public) + @($private) |
        Where-Object { $_ -in $components.Name } | Sort-Object -Unique)
    $files = @(Get-ChildItem -LiteralPath $component.FullName -Filter '*.h' -File)
    foreach ($source in @(Read-Section $buildText 'SRCS')) {
        $sourceFile = Get-Item -LiteralPath (Join-Path $component.FullName $source)
        $files += $sourceFile
        $registeredSources += $sourceFile.FullName
    }
    $edges = @{}
    foreach ($file in $files) {
        foreach ($include in Select-String -LiteralPath $file.FullName -Pattern '^\s*#\s*include\s*["<]([^">]+)[">]') {
            $name = $include.Matches[0].Groups[1].Value
            # Quoted same-directory headers win, including the two utils.h owners.
            if (Test-Path -LiteralPath (Join-Path $component.FullName $name)) { continue }
            if (-not $headerOwners.ContainsKey($name)) { continue } # SDK/standard/optional local config.
            $owners = @($headerOwners[$name] | Where-Object { $_ })
            if ($owners.Count -ne 1) { throw "Ambiguous project header: $name" }
            $owner = $owners[0]
            $isPublic = $file.Extension -eq '.h'
            $edges[$owner] = $isPublic -or ($edges.ContainsKey($owner) -and $edges[$owner])
        }
    }
    $edgeCount += $edges.Count
    foreach ($owner in $public | Where-Object { $_ -in $components.Name }) {
        if (-not $edges.ContainsKey($owner) -or -not $edges[$owner]) {
            throw "Project dependency needlessly public: $($component.Name) -> $owner"
        }
    }
    foreach ($owner in $edges.Keys) {
        if ($owner -notin $public -and ($edges[$owner] -or $owner -notin $private)) {
            $scope = if ($edges[$owner]) { 'REQUIRES' } else { 'REQUIRES or PRIV_REQUIRES' }
            $missing += "$($component.Name) -> $owner ($scope)"
        }
    }
}
if ($missing.Count) {
    $missing | Sort-Object | ForEach-Object { Write-Output "MISSING: $_" }
    throw "$($missing.Count) direct project component dependencies are undeclared."
}
Write-Output "PASS: $($components.Count) components, $edgeCount direct project header dependency edges declared."
Write-Output 'PASS: no global project -I flags or second lib_extra_dirs build path.'
$dependencyOrder = @(Get-ComponentDependencyOrder -Graph $projectGraph)
Write-Output ('PASS: declared project graph is acyclic; dependency-first order: ' + ($dependencyOrder -join ', '))
if ($BuildMetadata) {
    $buildRoot = Join-Path $projectRoot '.pio/build/esp32dev'
    $databaseFile = Get-Item -LiteralPath (Join-Path $buildRoot 'compile_commands.json')
    $inputs = @(Get-Item -LiteralPath (Join-Path $projectRoot 'platformio.ini')) +
        @(Get-Item -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt')) +
        @($components | ForEach-Object { Get-Item -LiteralPath (Join-Path $_.FullName 'CMakeLists.txt') })
    if (@($inputs | Where-Object { $_.LastWriteTimeUtc -gt $databaseFile.LastWriteTimeUtc }).Count) {
        throw 'Build metadata predates configuration. Reconfigure ESP-IDF and build esp32dev; PlatformIO may cache nested component CMakeLists.txt changes (see docs/component_dependencies.md).'
    }
    $counts = @{}
    # Windows PowerShell 5.1 returns a JSON array as one pipeline item. Assign
    # first, then enumerate; @(ConvertFrom-Json) would wrap a nested array.
    $entries = Get-Content -LiteralPath $databaseFile.FullName -Raw | ConvertFrom-Json
    foreach ($entry in $entries) {
        $sourcePath = if ([IO.Path]::IsPathRooted($entry.file)) { $entry.file } else { Join-Path $entry.directory $entry.file }
        $sourcePath = $sourcePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
        if (-not ($sourcePath.StartsWith((Join-Path $projectRoot 'components') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
                  $sourcePath.StartsWith((Join-Path $projectRoot 'main') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) { continue }
        $sourcePath = [IO.Path]::GetFullPath($sourcePath)
        $counts[$sourcePath] = 1 + [int]$counts[$sourcePath]
    }
    foreach ($sourcePath in $registeredSources) {
        if ($counts[$sourcePath] -ne 1) { throw "Source not compiled exactly once in IDF metadata: $sourcePath" }
    }
    $description = Get-Content -LiteralPath (Join-Path $buildRoot 'project_description.json') -Raw | ConvertFrom-Json
    $mapFile = Get-Item -LiteralPath (Join-Path $buildRoot ($description.project_name + '.map'))
    if ($mapFile.LastWriteTimeUtc -lt $databaseFile.LastWriteTimeUtc) { throw 'Link map predates build metadata.' }
    if (Select-String -LiteralPath $mapFile.FullName -Pattern '[/\\]lib[0-9a-f]{3,}[/\\]') {
        throw 'PlatformIO LDF object/archive still appears in firmware link map.'
    }
    Write-Output "PASS: $($registeredSources.Count) project source files occur once in IDF compile metadata; no LDF archive in link map."
}
Write-Output 'LIMIT: implicit SDK/link-only edges and runtime call/lock cycles are not checked; metadata is not a hardware test.'
