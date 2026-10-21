param([Parameter(Mandatory)][string]$Trace)
$ErrorActionPreference = 'Stop'
$lines = Get-Content -LiteralPath $Trace
$lines | Where-Object { $_.StartsWith('#') }
$events = @($lines | Where-Object { !$_.StartsWith('#') } | ConvertFrom-Csv -Delimiter "`t")
$milestones = @('process.entry', 'native.initialized', 'native.theme.loaded', 'windows.composition.created',
    'environment.request', 'environment.completed', 'system:ui:boot', 'system:ui:painted',
    'island.system.usable', 'island.apps.visible', 'media.initial.snapshot', 'media.unavailable',
    'watchdog.ready', 'application.ready')
foreach ($name in $milestones) {
    $found = $events | Where-Object event -EQ $name | Select-Object -First 1
    [pscustomobject]@{milestone=$name; milliseconds=$(if ($found) {$found.elapsed_ms} else {'not recorded / not applicable'})}
}
$events | Where-Object { $_.event -like '*perf:longtask:*' } | Format-Table
