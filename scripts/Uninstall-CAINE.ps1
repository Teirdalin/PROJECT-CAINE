param([string]$GameRoot)
. "$PSScriptRoot\Common.ps1"
$GameRoot = Get-CaineGameRoot $GameRoot
Assert-CaineGameStopped $GameRoot
Assert-CaineInstallRoot $GameRoot
$plugin = Join-Path $GameRoot 'Bin\loader\CAINE.asi'
$receipt = Join-Path $GameRoot 'Bin\loader\CAINE\install.json'
$background = Join-Path $GameRoot 'Bin\loader\CAINE\background.png'
$crashReporter = Join-Path $GameRoot 'Bin\loader\CAINE\CrashReporter.exe'
if (!(Test-Path -LiteralPath $plugin)) { Write-Output 'CAINE.asi is already absent.'; return }
foreach ($file in @($plugin,$receipt,$background,$crashReporter)) {
    if ((Test-Path -LiteralPath $file) -and ((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing redirected file: $file" }
}
if (!(Test-Path -LiteralPath $receipt)) { throw 'No CAINE ownership receipt. Nothing removed.' }
$installed = Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
if ($installed.product -ne 'CAINE' -or (Get-CaineHash $plugin) -ne $installed.pluginSha256) { throw 'CAINE.asi has changed since installation. Nothing removed.' }
$hasBackground = Test-Path -LiteralPath $background
if ($hasBackground -and (!($installed.PSObject.Properties.Name -contains 'backgroundSha256') -or (Get-CaineHash $background) -ne $installed.backgroundSha256)) { throw 'CAINE background has changed or is unowned. Nothing removed.' }
$hasCrashReporter = Test-Path -LiteralPath $crashReporter
if ($hasCrashReporter -and (!($installed.PSObject.Properties.Name -contains 'crashReporterSha256') -or (Get-CaineHash $crashReporter) -ne $installed.crashReporterSha256)) { throw 'CAINE crash reporter has changed or is unowned. Nothing removed.' }
$updaterPaths=@('Bin/loader/CAINE/Updater.exe','Bin/loader/CAINE/ApplyUpdate.ps1','Bin/loader/CAINE/Install-CAINE.ps1','Bin/loader/CAINE/Common.ps1')
foreach ($rel in $updaterPaths) {
    $path=Join-Path $GameRoot $rel
    Assert-CainePathNotRedirected $path
    if ((Test-Path -LiteralPath $path) -and (!($installed.PSObject.Properties.Name -contains 'updaterFiles') -or
        !($installed.updaterFiles.PSObject.Properties.Name -ccontains $rel) -or (Get-CaineHash $path) -cne $installed.updaterFiles.$rel)) { throw "Updater file is changed or unowned: $rel. Nothing removed." }
}
$backupRoot = Join-Path $GameRoot 'CAINE_Backups'
if ((Test-Path -LiteralPath $backupRoot) -and ((Get-Item -LiteralPath $backupRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Refusing redirected backup directory.' }
$backup = Join-Path $backupRoot ('uninstall-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $plugin -Destination "$backup\CAINE.asi"
Copy-Item -LiteralPath $receipt -Destination "$backup\install.json"
foreach ($rel in $updaterPaths) {
    $path=Join-Path $GameRoot $rel
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $backup (Split-Path $rel -Leaf));Remove-Item -LiteralPath $path }
}
if ($hasBackground) { Copy-Item -LiteralPath $background -Destination "$backup\background.png"; Remove-Item -LiteralPath $background }
if ($hasCrashReporter) { Copy-Item -LiteralPath $crashReporter -Destination "$backup\CrashReporter.exe"; Remove-Item -LiteralPath $crashReporter }
Remove-Item -LiteralPath $plugin
Remove-Item -LiteralPath $receipt
Write-Output 'CAINE_UNINSTALL_OK: plugin removed. Configuration, logs, backups, other mods, and saves retained.'
