Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-CainePayloadPaths {
    return @('Bin/loader/CAINE.asi','Bin/loader/CAINE/CAINE.ini','Bin/loader/CAINE/background.png',
        'Bin/loader/CAINE/CrashReporter.exe','Bin/loader/CAINE/Updater.exe','Bin/loader/CAINE/ApplyUpdate.ps1',
        'Bin/loader/CAINE/Install-CAINE.ps1','Bin/loader/CAINE/Common.ps1')
}
function Assert-CainePathNotRedirected([string]$Path) {
    $current=[IO.Path]::GetFullPath($Path)
    while ($current) {
        if ((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing redirected path: $current" }
        $current=[IO.Path]::GetDirectoryName($current)
    }
}

function Get-CaineGameRoot([string]$GameRoot) {
    if (!$GameRoot) {
        $local = Join-Path (Split-Path $PSScriptRoot -Parent) 'local.json'
        if (Test-Path -LiteralPath $local) { $GameRoot = (Get-Content -Raw -LiteralPath $local | ConvertFrom-Json).gameRoot }
        else { $GameRoot = 'F:\Games\Vampire The Masquerade Bloodlines' }
    }
    return (Resolve-Path -LiteralPath $GameRoot).Path
}
function Assert-CaineGameStopped([string]$Root) {
    $running = @(Get-CimInstance Win32_Process -Filter "Name='Vampire.exe' OR Name='Loader.exe'")
    if (!$running.Count) { return }
    if (!$Root) { throw 'Close Bloodlines and its mod selection window before installing or uninstalling CAINE.' }
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    foreach ($process in $running) {
        if (!$process.ExecutablePath -or [IO.Path]::GetFullPath($process.ExecutablePath).StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Close this Bloodlines installation and its mod selection window before installing or uninstalling CAINE.' }
    }
}
function Get-CaineHash([string]$Path) {
    # .NET hashing works in both 32/64-bit Windows PowerShell without module auto-loading.
    $stream=[IO.File]::OpenRead($Path)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose();$stream.Dispose() }
}
function Assert-CaineX86([string]$Path) {
    $data = [IO.File]::ReadAllBytes($Path)
    if ($data.Length -lt 64 -or [BitConverter]::ToUInt16($data,0) -ne 0x5A4D) { throw "Not a PE image: $Path" }
    $offset = [BitConverter]::ToInt32($data,60)
    if ($offset -lt 0 -or $offset -gt $data.Length-26 -or [BitConverter]::ToUInt32($data,$offset) -ne 0x4550 -or [BitConverter]::ToUInt16($data,$offset+4) -ne 0x14c) { throw "Expected x86 PE image: $Path" }
}
function Assert-CaineInstallRoot([string]$Root) {
    Assert-CainePathNotRedirected $Root
    Assert-CaineX86 (Join-Path $Root 'Vampire.exe')
    Assert-CaineX86 (Join-Path $Root 'Loader.dll')
    if (!(Test-Path -LiteralPath (Join-Path $Root 'Bin\loader') -PathType Container)) { throw 'Native Bin\loader folder missing. Install a compatible Bloodlines Game Mod Loader first.' }
    # Never follow directory junctions when installing or removing owned files.
    foreach ($relative in @('Bin','Bin\loader','Bin\loader\CAINE','Bin\loader\KAIN')) {
        $p = Join-Path $Root $relative
        if ((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing redirected install directory: $p" }
    }
}
function Get-CainePackage {
    $root = Split-Path $PSScriptRoot -Parent
    if (Test-Path -LiteralPath (Join-Path $root 'package.json')) { return $root }
    $latest = Join-Path $root 'dist\latest-package.txt'
    if (!(Test-Path -LiteralPath $latest)) { throw 'Run Build PROJECT CAINE.cmd first.' }
    $package = (Get-Content -Raw -LiteralPath $latest).Trim()
    $prefix = [IO.Path]::GetFullPath((Join-Path $root 'dist')) + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($package)
    if (!$resolved.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Package must be inside this workspace dist folder.' }
    return $resolved
}
