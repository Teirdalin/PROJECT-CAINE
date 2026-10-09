param([string]$GameRoot,[string]$PackagePath,[string]$ProgressFile,[switch]$ChooseGame)
. "$PSScriptRoot\Common.ps1"
if ($ChooseGame -and !$GameRoot) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog=New-Object Windows.Forms.OpenFileDialog
    $dialog.Title='Select Vampire.exe in your Bloodlines installation'
    $dialog.Filter='Bloodlines executable (Vampire.exe)|Vampire.exe'
    $dialog.CheckFileExists=$true
    try {
        if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { Write-Output 'Installation cancelled. Nothing changed.';return }
        $GameRoot=Split-Path $dialog.FileName -Parent
    } finally { $dialog.Dispose() }
}
$GameRoot = Get-CaineGameRoot $GameRoot
Assert-CaineGameStopped $GameRoot
Assert-CaineInstallRoot $GameRoot
$package = if ($PackagePath) { (Resolve-Path -LiteralPath $PackagePath).Path } else { Get-CainePackage }
Assert-CainePathNotRedirected $package
function Install-Progress([int]$Percent,[string]$Text) {
    if ($ProgressFile) {
        @{progress=$Percent;message=$Text} | ConvertTo-Json | Set-Content -LiteralPath "$ProgressFile.tmp" -Encoding UTF8
        Move-Item -LiteralPath "$ProgressFile.tmp" -Destination $ProgressFile -Force
    }
}
$manifest = Get-Content -Raw -LiteralPath "$package\package.json" | ConvertFrom-Json
$allowed = @(Get-CainePayloadPaths)
if ($manifest.architecture -ne 'x86' -or @($manifest.files).Count -ne $allowed.Count) { throw 'Unexpected package manifest.' }
foreach ($rel in $allowed) {
    Assert-CainePathNotRedirected (Join-Path "$package\payload" $rel)
    $entries = @($manifest.files | Where-Object { $_.path -ceq $rel })
    if ($entries.Count -ne 1 -or (Get-CaineHash (Join-Path "$package\payload" $rel)) -ne $entries[0].sha256) { throw "Package integrity failed: $rel" }
}
Assert-CaineX86 "$package\payload\Bin\loader\CAINE.asi"
Assert-CaineX86 "$package\payload\Bin\loader\CAINE\CrashReporter.exe"
Assert-CaineX86 "$package\payload\Bin\loader\CAINE\Updater.exe"
Install-Progress 55 'Package hashes verified. Checking installed ownership...'
$updaterPaths=@('Bin/loader/CAINE/Updater.exe','Bin/loader/CAINE/ApplyUpdate.ps1','Bin/loader/CAINE/Install-CAINE.ps1','Bin/loader/CAINE/Common.ps1')
$plugin = Join-Path $GameRoot 'Bin\loader\CAINE.asi'
$config = Join-Path $GameRoot 'Bin\loader\CAINE\CAINE.ini'
$receipt = Join-Path $GameRoot 'Bin\loader\CAINE\install.json'
$background = Join-Path $GameRoot 'Bin\loader\CAINE\background.png'
$crashReporter = Join-Path $GameRoot 'Bin\loader\CAINE\CrashReporter.exe'
foreach ($file in @($plugin,$config,$receipt,$background,$crashReporter)) {
    if ((Test-Path -LiteralPath $file) -and ((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing redirected file: $file" }
}
$previous = $null
if (Test-Path -LiteralPath $plugin) {
    if (!(Test-Path -LiteralPath $receipt)) { throw 'Existing CAINE.asi has no ownership receipt. Preserve it and resolve manually.' }
    $previous = Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
    if ($previous.product -ne 'CAINE' -or (Get-CaineHash $plugin) -ne $previous.pluginSha256) { throw 'Installed CAINE.asi differs from its ownership receipt. No overwrite performed.' }
} elseif (Test-Path -LiteralPath $receipt) { throw 'CAINE receipt exists but plugin is missing. Resolve the partial installation first.' }
if (Test-Path -LiteralPath $background) {
    if (!$previous -or !($previous.PSObject.Properties.Name -contains 'backgroundSha256') -or (Get-CaineHash $background) -ne $previous.backgroundSha256) { throw 'Existing CAINE background has no matching ownership receipt. Nothing changed.' }
}
if (Test-Path -LiteralPath $crashReporter) {
    if (!$previous -or !($previous.PSObject.Properties.Name -contains 'crashReporterSha256') -or (Get-CaineHash $crashReporter) -ne $previous.crashReporterSha256) { throw 'Existing CAINE crash reporter has no matching ownership receipt. Nothing changed.' }
}
$oldUpdater=@{}
foreach ($rel in $updaterPaths) {
    $path=Join-Path $GameRoot $rel
    Assert-CainePathNotRedirected $path
    $oldUpdater[$rel]=Test-Path -LiteralPath $path
    if ($oldUpdater[$rel] -and (!$previous -or !($previous.PSObject.Properties.Name -contains 'updaterFiles') -or
        !($previous.updaterFiles.PSObject.Properties.Name -ccontains $rel) -or (Get-CaineHash $path) -cne $previous.updaterFiles.$rel)) { throw "Installed updater file is modified or unowned: $rel. Nothing changed." }
}
$legacyPlugin = Join-Path $GameRoot 'Bin\loader\KAIN.asi'
$legacyConfig = Join-Path $GameRoot 'Bin\loader\KAIN\KAIN.ini'
$legacyReceipt = Join-Path $GameRoot 'Bin\loader\KAIN\install.json'
$hasLegacy = Test-Path -LiteralPath $legacyPlugin
foreach ($file in @($legacyPlugin,$legacyConfig,$legacyReceipt)) {
    if ((Test-Path -LiteralPath $file) -and ((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing redirected legacy file: $file" }
}
if ($hasLegacy) {
    if (!(Test-Path -LiteralPath $legacyReceipt)) { throw 'Legacy KAIN.asi has no ownership receipt. Nothing changed.' }
    $legacy = Get-Content -Raw -LiteralPath $legacyReceipt | ConvertFrom-Json
    if ($legacy.product -ne 'KAIN' -or (Get-CaineHash $legacyPlugin) -ne $legacy.pluginSha256) { throw 'Legacy KAIN.asi differs from its receipt. Nothing changed.' }
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$backup = Join-Path $GameRoot "CAINE_Backups\$stamp"
if ((Test-Path -LiteralPath (Join-Path $GameRoot 'CAINE_Backups')) -and ((Get-Item -LiteralPath (Join-Path $GameRoot 'CAINE_Backups')).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Refusing redirected backup directory.' }
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Install-Progress 65 'Backing up the previous CAINE installation...'
$oldPlugin = Test-Path -LiteralPath $plugin
$oldConfig = Test-Path -LiteralPath $config
$oldReceipt = Test-Path -LiteralPath $receipt
$oldBackground = Test-Path -LiteralPath $background
$oldCrashReporter = Test-Path -LiteralPath $crashReporter
foreach ($file in @($plugin,$config,$receipt,$background,$crashReporter)) {
    if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination (Join-Path $backup (Split-Path $file -Leaf)) }
}
foreach ($rel in $updaterPaths) {
    if ($oldUpdater[$rel]) { Copy-Item -LiteralPath (Join-Path $GameRoot $rel) -Destination (Join-Path $backup (Split-Path $rel -Leaf)) }
}
if ($hasLegacy) {
    New-Item -ItemType Directory -Path "$backup\legacy-KAIN" | Out-Null
    foreach ($file in @($legacyPlugin,$legacyConfig,$legacyReceipt)) {
        if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination "$backup\legacy-KAIN" }
    }
}
try {
    Install-Progress 75 'Installing CAINE. Existing configuration is retained...'
    New-Item -ItemType Directory -Path (Split-Path $config -Parent) -Force | Out-Null
    if (!$oldConfig) {
        $configSource = "$package\payload\Bin\loader\CAINE\CAINE.ini"
        if ($hasLegacy -and (Test-Path -LiteralPath $legacyConfig)) { $configSource = $legacyConfig }
        Copy-Item -LiteralPath $configSource -Destination $config
    }
    Copy-Item -LiteralPath "$package\payload\Bin\loader\CAINE.asi" -Destination $plugin -Force
    Copy-Item -LiteralPath "$package\payload\Bin\loader\CAINE\background.png" -Destination $background -Force
    Copy-Item -LiteralPath "$package\payload\Bin\loader\CAINE\CrashReporter.exe" -Destination $crashReporter -Force
    $crashReporterHash = Get-CaineHash $crashReporter
    if ($crashReporterHash -ne (Get-CaineHash "$package\payload\Bin\loader\CAINE\CrashReporter.exe")) { throw 'Installed crash reporter verification failed.' }
    $backgroundHash = Get-CaineHash $background
    if ($backgroundHash -ne (Get-CaineHash "$package\payload\Bin\loader\CAINE\background.png")) { throw 'Installed background verification failed.' }
    $installedHash = Get-CaineHash $plugin
    if ($installedHash -ne (Get-CaineHash "$package\payload\Bin\loader\CAINE.asi")) { throw 'Installed hash verification failed.' }
    $updaterHashes=@{}
    foreach ($rel in $updaterPaths) {
        $source=Join-Path "$package\payload" $rel
        $target=Join-Path $GameRoot $rel
        Copy-Item -LiteralPath $source -Destination $target -Force
        $updaterHashes[$rel]=Get-CaineHash $target
        if ($updaterHashes[$rel] -cne (Get-CaineHash $source)) { throw "Installed updater verification failed: $rel" }
    }
    Install-Progress 90 'Verifying installation and recording ownership...'
    @{product='CAINE';version=$manifest.version;pluginSha256=$installedHash;backgroundSha256=$backgroundHash;crashReporterSha256=$crashReporterHash;updaterFiles=$updaterHashes;installedUtc=[DateTime]::UtcNow.ToString('o');backup=$backup} |
        ConvertTo-Json | Set-Content -LiteralPath $receipt -Encoding UTF8
    if ($hasLegacy) { Remove-Item -LiteralPath $legacyPlugin }
} catch {
    if ($hasLegacy -and !(Test-Path -LiteralPath $legacyPlugin)) {
        Copy-Item -LiteralPath "$backup\legacy-KAIN\KAIN.asi" -Destination $legacyPlugin
    }
    foreach ($entry in @(@($plugin,$oldPlugin),@($config,$oldConfig),@($receipt,$oldReceipt),@($background,$oldBackground),@($crashReporter,$oldCrashReporter))) {
        if ($entry[1]) { Copy-Item -LiteralPath (Join-Path $backup (Split-Path $entry[0] -Leaf)) -Destination $entry[0] -Force }
        elseif (Test-Path -LiteralPath $entry[0]) { Remove-Item -LiteralPath $entry[0] -Force }
    }
    foreach ($rel in $updaterPaths) {
        $target=Join-Path $GameRoot $rel
        if ($oldUpdater[$rel]) { Copy-Item -LiteralPath (Join-Path $backup (Split-Path $rel -Leaf)) -Destination $target -Force }
        elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
    }
    throw
}
Write-Output "CAINE_INSTALL_OK: $plugin"
Write-Output 'CAINE now loads with normal Bloodlines startup through the installed native loader.'
Write-Output "Backups: $backup"
