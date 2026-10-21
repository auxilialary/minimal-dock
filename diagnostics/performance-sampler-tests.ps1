$ErrorActionPreference = 'Stop'
$fixture = @{Sample=0; Started=[datetime]::UtcNow.AddMinutes(-1)}
function Get-Process {
    [CmdletBinding()]param([int]$Id)
    if ($Id -notin @(900,901,902)) { throw 'Unknown fixture process' }
    [pscustomobject]@{ProcessName=$(if($Id -eq 900){'Dock'}else{'msedgewebview2'});
        StartTime=$fixture.Started; PrivateMemorySize64=([long]($Id-899)*100);
        WorkingSet64=([long]($Id-899)*200);
        TotalProcessorTime=[timespan]::FromSeconds($fixture.Sample * ($Id-899) / 10.0)}
}
function Get-CimInstance {
    param([string]$ClassName)
    ++$fixture.Sample
    foreach($id in @(900,901,902)) {
        [pscustomobject]@{ProcessId=$id; ParentProcessId=($id-1); Name=$(if($id -eq 900){'Dock.exe'}else{'msedgewebview2.exe'}); CreationDate=$fixture.Started}
    }
}
$output = Join-Path $PSScriptRoot 'performance-sampler-fixture.csv'
& (Join-Path $PSScriptRoot 'measure-process-tree.ps1') -DockProcessId 900 -Seconds 1 -Scenario 'synthetic-fixture' -Output $output
$rows = @(Import-Csv -LiteralPath $output)
$totals = @($rows | Where-Object kind -EQ 'total')
if ($rows.Count -ne 8 -or $totals.Count -ne 2) { throw 'Wrong sample/process count' }
foreach($total in $totals) {
    if ($total.privateBytes -ne 600 -or $total.workingSet -ne 1200 -or $total.processes -ne 3 -or $total.webviews -ne 2) { throw 'Wrong process-tree aggregation' }
}
if ([double]$totals[1].cpuOneCorePercent -le 0) { throw 'CPU deltas were not measured' }
'PASS: synthetic process-tree discovery, private/working-set sums, WebView counts, CPU deltas, CSV output. No real processes sampled.'
