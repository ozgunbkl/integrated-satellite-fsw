Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../../tools/component_graph.ps1')

foreach ($case in @(
    @{ Name='empty'; Graph=@{}; Order='' },
    @{ Name='leaf'; Graph=@{ common=@() }; Order='common' },
    @{ Name='chain'; Graph=@{ main=@('fdir'); fdir=@('time'); time=@() }; Order='time,fdir,main' },
    @{ Name='diamond'; Graph=@{ app=@('a','b'); a=@('base'); b=@('base'); base=@() }; Order='base,a,b,app' }
)) {
    $actual = @(Get-ComponentDependencyOrder -Graph $case.Graph) -join ','
    if ($actual -ne $case.Order) { throw "Unexpected dependency order for $($case.Name): $actual" }
    Write-Output "PASS: dependency graph $($case.Name)"
}
foreach ($case in @(
    @{ Name='self_cycle'; Graph=@{ a=@('a') }; Error='cycle blocks ordering' },
    @{ Name='private_cycle'; Graph=@{ a=@('b'); b=@('a') }; Error='cycle blocks ordering' },
    @{ Name='cycle_with_consumer'; Graph=@{ app=@('a'); a=@('b'); b=@('a'); leaf=@() }; Error='cycle blocks ordering' },
    @{ Name='unknown_dependency'; Graph=@{ a=@('missing') }; Error='Unknown project component dependency' }
)) {
    $rejected = $false
    try { $null = Get-ComponentDependencyOrder -Graph $case.Graph }
    catch {
        if (-not $_.Exception.Message.Contains($case.Error)) { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Invalid graph accepted: $($case.Name)" }
    Write-Output "PASS: dependency graph rejects $($case.Name)"
}
