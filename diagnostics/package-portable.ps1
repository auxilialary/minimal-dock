param([string]$BuildDirectory = 'x64/Release', [string]$Commit = '',
      [string]$Destination = 'artifacts')
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
if (!$Commit) { $Commit = (git rev-parse HEAD).Trim() }
if ($Commit -notmatch '^[0-9a-f]{40}$') { throw 'A full commit SHA is required' }
$build = (Resolve-Path $BuildDirectory).Path
$destinationPath = [IO.Path]::GetFullPath((Join-Path $PWD $Destination))
New-Item -ItemType Directory -Force $destinationPath | Out-Null
# Use a fresh staging folder so previous builds cannot leak into the ZIP.
$stage = Join-Path $destinationPath ([guid]::NewGuid().ToString())
$root = Join-Path $stage 'GlassSplit'
New-Item -ItemType Directory -Force $root | Out-Null
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -or !$vs) { throw 'Visual Studio 2026 not found' }
$version = (Get-Content "$vs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt").Trim()
$dumpbin = "$vs/VC/Tools/MSVC/$version/bin/Hostx64/x64/dumpbin.exe"
$crt = Get-ChildItem "$vs/VC/Redist/MSVC/*/x64/Microsoft.VC145.CRT" -Directory |
    Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw 'Redistributable v145 x64 CRT not found' }
$expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in @('Dock.exe', 'WebView2Loader.dll')) {
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination $root
    [void]$expected.Add($name)
}
# Inspect the actual binary imports and include their app-local CRT dependency closure.
$queue = [Collections.Generic.Queue[string]]::new()
$queue.Enqueue('Dock.exe'); $queue.Enqueue('WebView2Loader.dll')
while ($queue.Count) {
    $name = $queue.Dequeue()
    $imports = & $dumpbin /nologo /dependents (Join-Path $root $name)
    if ($LASTEXITCODE) { throw "Cannot inspect $name" }
    foreach ($line in $imports) {
        $dll = $line.Trim()
        if ($dll -match '^(msvcp|vcruntime|concrt)\w*\.dll$' -and $expected.Add($dll)) {
            Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination $root
            $queue.Enqueue($dll)
        }
    }
}
$assets = @(git ls-files -- ui)
if ($LASTEXITCODE -or !$assets -or 'ui/index.html' -notin $assets) { throw 'UI inventory missing' }
foreach ($asset in $assets) {
    $source = Join-Path $build $asset
    if ((Get-FileHash $source).Hash -ne (Get-FileHash $asset).Hash) { throw "Stale UI asset: $asset" }
    $target = Join-Path $root $asset
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
    [void]$expected.Add($asset)
}
@{commit=$Commit; architecture='x64'; configuration='Release'} |
    ConvertTo-Json | Set-Content (Join-Path $root 'version.json') -Encoding utf8
[void]$expected.Add('version.json')
$zip = Join-Path $destinationPath 'GlassSplit-Windows-x64.zip'
Compress-Archive -Path $root -DestinationPath $zip -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
    $files = @($archive.Entries | Where-Object { $_.Name })
    if ($files.Count -ne $expected.Count) { throw 'Archive entry count mismatch' }
    foreach ($entry in $files) {
        $name = $entry.FullName.Replace('\','/')
        if (!$name.StartsWith('GlassSplit/') -or !$expected.Remove($name.Substring(11))) {
            throw "Unexpected archive entry: $name"
        }
        $stream = $entry.Open()
        try {
            $hash = [Security.Cryptography.SHA256]::Create()
            try { $digest = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','') }
            finally { $hash.Dispose() }
        } finally { $stream.Dispose() }
        if ($digest -ne (Get-FileHash (Join-Path $stage $name)).Hash) { throw "Archive corruption: $name" }
    }
} finally { $archive.Dispose() }
# Only this invocation's generated staging directory is removed.
if ((Split-Path $stage) -ne $destinationPath) { throw 'Invalid staging path' }
Remove-Item -LiteralPath $stage -Recurse -Force
Write-Host "Verified $($files.Count) files in $zip (commit $Commit)"
