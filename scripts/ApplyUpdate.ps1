param([Parameter(Mandatory=$true)][string]$Request)
. "$PSScriptRoot\Common.ps1"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$statusPath=Join-Path $PSScriptRoot 'progress.json'
function Update-Progress([int]$Percent,[string]$Text) {
    @{progress=$Percent;message=$Text} | ConvertTo-Json | Set-Content -LiteralPath "$statusPath.tmp" -Encoding UTF8
    Move-Item -LiteralPath "$statusPath.tmp" -Destination $statusPath -Force
}
try {
    $requestData=Get-Content -LiteralPath $Request -Raw | ConvertFrom-Json
    if ($requestData.schema -ne 1) { throw 'Unsupported updater request.' }
    $zip=[IO.Path]::GetFullPath($requestData.zip)
    if ([IO.Path]::GetDirectoryName($zip) -ne $PSScriptRoot) { throw 'Package is outside the private staging directory.' }
    Assert-CainePathNotRedirected $zip
    $game=Get-CaineGameRoot $requestData.gameRoot
    Assert-CainePathNotRedirected $game
    Assert-CaineGameStopped $game
    if ((Get-CaineHash $zip) -cne $requestData.sha256) { throw 'Staged update SHA-256 verification failed. Nothing installed.' }
    Update-Progress 12 'Inspecting the verified update archive...'
    $destination=Join-Path $PSScriptRoot 'package'
    if (Test-Path -LiteralPath $destination) { throw 'Extraction directory already exists. Use a fresh update attempt.' }
    $allowed=@('package.json') + @(Get-CainePayloadPaths | ForEach-Object { 'payload/'+$_ })
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        $bytes=0L
        foreach ($entry in $archive.Entries) {
            $name=$entry.FullName
            # Update assets contain only files: exact allowlist, no directories, links, traversal or extras.
            if ($allowed -cnotcontains $name -or !$seen.Add($name) -or $entry.Length -gt 67108864 -or
                ($entry.ExternalAttributes -band 0x400) -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw "Unexpected ZIP entry: $name" }
            $bytes+=$entry.Length
            if ($bytes -gt 268435456) { throw 'Update archive exceeds the extraction limit.' }
        }
        if ($seen.Count -ne $allowed.Count) { throw 'Update archive is missing required files.' }
        New-Item -ItemType Directory -Path $destination | Out-Null
        $index=0
        foreach ($entry in $archive.Entries) {
            $target=Join-Path $destination $entry.FullName
            New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
            $index++;Update-Progress (15+[int](25*$index/$allowed.Count)) 'Extracting CAINE files...'
        }
    } finally { $archive.Dispose() }
    $manifest=Get-Content -LiteralPath "$destination\package.json" -Raw | ConvertFrom-Json
    if ($manifest.version -cne $requestData.version) { throw 'Release tag and package version disagree. Nothing installed.' }
    Update-Progress 45 'Verifying CAINE payload and installed ownership...'
    # Execute this installed, receipt-verified installer; never execute scripts from the downloaded archive.
    & "$PSScriptRoot\Install-CAINE.ps1" -GameRoot $game -PackagePath $destination -ProgressFile $statusPath
    Update-Progress 96 'Installation complete. Preparing Bloodlines restart...'
    exit 0
} catch {
    Update-Progress 0 ('Update stopped: '+$_.Exception.Message+' Existing saves, settings and other mods were retained. See CAINE_Backups for any installation backup.')
    exit 1
}
