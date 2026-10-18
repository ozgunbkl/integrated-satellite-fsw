function Get-ProductionTestViolations {
    param([Parameter(Mandatory)][string]$ProjectRoot, [AllowEmptyCollection()][object[]]$Entries)
    $root = [IO.Path]::GetFullPath($ProjectRoot).Replace('\', '/').TrimEnd('/') + '/'
    foreach ($entry in $Entries) {
        $source = if ([IO.Path]::IsPathRooted($entry.file)) { $entry.file } else { Join-Path $entry.directory $entry.file }
        $source = [IO.Path]::GetFullPath($source).Replace('\', '/')
        if (-not $source.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { continue }
        $relative = $source.Substring($root.Length)
        if ($relative -match '^(?:tests/|\.pio/host-tests/)') {
            "Test source in production metadata: $relative"
        }
        $command = if ($entry.PSObject.Properties['command']) { [string]$entry.command }
                   elseif ($entry.PSObject.Properties['arguments']) { $entry.arguments -join ' ' }
                   else { throw "Compile entry has no command/arguments: $relative" }
        $command = $command.Replace('\', '/').Replace('"', '').Replace("'", '')
        # C macro names are case-sensitive; avoid Turkish IgnoreCase range folding.
        if ($command -cmatch '(?:^|\s)-D\s*(?:FSW_TEST_[A-Za-z0-9_]+|SI_TEST_MODE)\b') {
            "Test define in production metadata: $relative"
        }
        if ($command -match '(?:tests/host|\.pio/host-tests)(?:/|\s|$)') {
            "Host fixture path in production metadata: $relative"
        }
    }
}
