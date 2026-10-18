function Get-ComponentDependencyOrder {
    param([Parameter(Mandatory)][hashtable]$Graph)

    foreach ($name in $Graph.Keys) {
        foreach ($dependency in $Graph[$name]) {
            if (-not $Graph.ContainsKey($dependency)) {
                throw "Unknown project component dependency: $name -> $dependency"
            }
        }
    }
    $remaining = @($Graph.Keys | Sort-Object)
    $ordered = @()
    while ($remaining.Count -gt 0) {
        $ready = @($remaining | Where-Object {
            $name = $_
            @($Graph[$name] | Where-Object { $_ -notin $ordered }).Count -eq 0
        })
        if ($ready.Count -eq 0) {
            throw ('Project dependency cycle blocks ordering: ' + ($remaining -join ', '))
        }
        $ordered += $ready
        $remaining = @($remaining | Where-Object { $_ -notin $ready })
    }
    return $ordered
}
