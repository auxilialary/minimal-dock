param()
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$suites = 'native','media','lyrics','popup-audio','performance','autostart','visibility'
foreach ($suite in $suites) {
    & cmd /d /c "diagnostics\run-$suite-tests.cmd"
    if ($LASTEXITCODE) { throw "$suite regression suite failed ($LASTEXITCODE)" }
}
& (Join-Path $PSScriptRoot 'performance-sampler-tests.ps1')
