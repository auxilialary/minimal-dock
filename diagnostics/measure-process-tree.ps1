param(
    [Parameter(Mandatory)][int]$DockProcessId,
    [ValidateRange(1, 600)][int]$Seconds = 30,
    [string]$Scenario = 'idle',
    [string]$Output = (Join-Path $PSScriptRoot 'process-tree.csv')
)
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
# Attach only. This script never starts Dock, changes audio, or touches taskbars.
$root = Get-Process -Id $DockProcessId
if ($root.ProcessName -ne 'Dock') { throw 'The supplied PID must be Dock.exe.' }
$started = $root.StartTime.ToUniversalTime()
$identities = @{}
$previousCpu = @{}
$watch = [Diagnostics.Stopwatch]::StartNew()
$last = $watch.Elapsed.TotalSeconds
$rows = [Collections.Generic.List[object]]::new()
for ($sample = 0; $sample -le $Seconds; ++$sample) {
    $currentRoot = Get-Process -Id $DockProcessId -ErrorAction SilentlyContinue
    if (!$currentRoot -or $currentRoot.StartTime.ToUniversalTime() -ne $started) { break }
    $all = @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name, CreationDate)
    $selected = [Collections.Generic.HashSet[int]]::new()
    [void]$selected.Add($DockProcessId)
    do {
        $added = $false
        foreach ($p in $all) {
            if ($p.CreationDate.ToUniversalTime() -lt $started.AddMilliseconds(-1)) { continue }
            $key = "$($p.ProcessId):$($p.CreationDate.ToUniversalTime().Ticks)"
            if ($selected.Contains([int]$p.ParentProcessId) -or $identities.ContainsKey($key)) {
                if ($selected.Add([int]$p.ProcessId)) { $added = $true }
                $identities[$key] = $true
            }
        }
    } while ($added)
    $now = $watch.Elapsed.TotalSeconds
    $delta = $now - $last
    $private = 0L; $working = 0L; $cpuDelta = 0.0; $count = 0; $webviews = 0
    foreach ($p in $all | Where-Object { $selected.Contains([int]$_.ProcessId) }) {
        try {
            $live = Get-Process -Id $p.ProcessId -ErrorAction Stop
            if ([Math]::Abs(($live.StartTime.ToUniversalTime() - $p.CreationDate.ToUniversalTime()).TotalMilliseconds) -gt 1) { continue }
            $key = "$($p.ProcessId):$($p.CreationDate.ToUniversalTime().Ticks)"
            $cpu = $live.TotalProcessorTime.TotalSeconds
            if ($previousCpu.ContainsKey($key)) { $cpuDelta += [Math]::Max(0.0, $cpu - $previousCpu[$key]) }
            $previousCpu[$key] = $cpu
            $private += $live.PrivateMemorySize64; $working += $live.WorkingSet64; ++$count
            if ($live.ProcessName -eq 'msedgewebview2') { ++$webviews }
            $rows.Add([pscustomobject]@{scenario=$Scenario; sample=$sample; seconds=$now.ToString('R',$invariant); kind='process'; pid=$p.ProcessId;
                name=$p.Name; privateBytes=$live.PrivateMemorySize64; workingSet=$live.WorkingSet64; cpuSeconds=$cpu.ToString('R',$invariant); cpuOneCorePercent=''; processes=''; webviews=''})
        } catch { } # Processes can exit between the snapshot and the read.
    }
    $rows.Add([pscustomobject]@{scenario=$Scenario; sample=$sample; seconds=$now.ToString('R',$invariant); kind='total'; pid=$DockProcessId;
        name='tree'; privateBytes=$private; workingSet=$working; cpuSeconds='';
        cpuOneCorePercent=$(if ($sample -gt 0 -and $delta -gt 0) {(100 * $cpuDelta / $delta).ToString('R',$invariant)} else {''}); processes=$count; webviews=$webviews})
    $last = $now
    if ($sample -lt $Seconds) { Start-Sleep -Seconds 1 }
}
$rows | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding utf8
Write-Output "Saved $($rows.Count) rows to $Output"
