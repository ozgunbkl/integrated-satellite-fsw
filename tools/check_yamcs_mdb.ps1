[CmdletBinding()]
param([Parameter(Mandatory)][string]$GroundProject, [string]$MdbFile)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $MdbFile) { $MdbFile = Join-Path $projectRoot 'config/yamcs/xtce.xml' }
$mdbPath = (Resolve-Path -LiteralPath $MdbFile).Path
$groundRoot = (Resolve-Path -LiteralPath $GroundProject).Path
$jars = Join-Path $groundRoot 'target/bundle-tmp/lib'
if (-not (Test-Path -LiteralPath (Join-Path $jars 'yamcs-core-5.12.0.jar'))) {
    throw 'Expected the existing pinned Yamcs 5.12.0 packaged libraries; no dependencies will be downloaded.'
}
$outputDirectory = Join-Path $projectRoot '.pio/yamcs-mdb-check'
$null = New-Item -ItemType Directory -Path $outputDirectory -Force
$classpath = (Join-Path $groundRoot 'target/classes') + [IO.Path]::PathSeparator + (Join-Path $jars '*')
$postSource = Get-Item -LiteralPath (Join-Path $groundRoot 'src/main/java/com/example/myproject/MyCommandPostprocessor.java')
$postClass = Get-Item -LiteralPath (Join-Path $groundRoot 'target/classes/com/example/myproject/MyCommandPostprocessor.class')
if ($postSource.LastWriteTimeUtc -gt $postClass.LastWriteTimeUtc) {
    throw 'Ground command postprocessor source is newer than its class; rebuild that project before native verification.'
}
$preSource = Get-Item -LiteralPath (Join-Path $groundRoot 'src/main/java/com/example/myproject/MyPacketPreprocessor.java')
$preClass = Get-Item -LiteralPath (Join-Path $groundRoot 'target/classes/com/example/myproject/MyPacketPreprocessor.class')
if ($preSource.LastWriteTimeUtc -gt $preClass.LastWriteTimeUtc) {
    throw 'Ground packet preprocessor source is newer than its class; rebuild that project before native verification.'
}
function Invoke-JavaTool {
    param([string]$Program, [string[]]$ToolArguments)
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = (& $Program @ToolArguments 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    if ($code -ne 0) { Write-Output $output; throw "$Program failed ($code)." }
    $output -split "`n" | Where-Object { $_ -and $_ -notmatch '^Picked up JAVA_TOOL_OPTIONS:' } | Write-Output
}
Push-Location $outputDirectory
try {
    Invoke-JavaTool -Program 'javac' -ToolArguments @('-encoding','UTF-8','--release','17','-cp',$classpath,'-d',$outputDirectory,
        (Join-Path $projectRoot 'tests/host/yamcs_mdb/YamcsMdbCheck.java'))
    # Isolate Yamcs default logging/cache home inside generated test output.
    # This changes only this Java process, never the user's HOME/environment.
    Invoke-JavaTool -Program 'java' -ToolArguments @(('-Duser.home=' + $outputDirectory),'-ea','-cp',
        ($outputDirectory + [IO.Path]::PathSeparator + $classpath), 'YamcsMdbCheck', $mdbPath)
    Write-Output 'PASS: offline native MDB/command check only; no server, link, network or board action.'
} finally { Pop-Location }
