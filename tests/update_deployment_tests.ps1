param([Parameter(Mandatory=$true)][string]$Package,[Parameter(Mandatory=$true)][string]$FixtureExe,
    [Parameter(Mandatory=$true)][string]$FixtureDll,[Parameter(Mandatory=$true)][string]$UpdateHost)
. "$PSScriptRoot\..\scripts\Common.ps1"
function Check([bool]$Ok,[string]$Message) { if (!$Ok) { throw $Message } }
$lab=Join-Path (Split-Path $PSScriptRoot -Parent) ('build\update-lab-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$lab\Bin\loader","$lab\Unofficial_Patch\save","$lab\mods\Unscripted" -Force | Out-Null
Copy-Item -LiteralPath $UpdateHost -Destination "$lab\Vampire.exe"
Copy-Item -LiteralPath $FixtureDll -Destination "$lab\Loader.dll"
'save sentinel' | Set-Content -LiteralPath "$lab\Unofficial_Patch\save\untouched.sav"
'independent mod' | Set-Content -LiteralPath "$lab\mods\Unscripted\Unscripted.dll"
$saveHash=Get-CaineHash "$lab\Unofficial_Patch\save\untouched.sav"
$modHash=Get-CaineHash "$lab\mods\Unscripted\Unscripted.dll"
$installer=Join-Path $Package 'scripts\Install-CAINE.ps1'
& $installer -GameRoot $lab
'[Runtime]','Enabled=0','[Menu]','Modern=0' | Set-Content -LiteralPath "$lab\Bin\loader\CAINE\CAINE.ini"
$configHash=Get-CaineHash "$lab\Bin\loader\CAINE\CAINE.ini"
$pluginHash=Get-CaineHash "$lab\Bin\loader\CAINE.asi"
$updaterHash=Get-CaineHash "$lab\Bin\loader\CAINE\Updater.exe"
# Receipt-verified upgrade from pre-updater CAINE adds owned files without replacing INI.
$receipt=Get-Content -LiteralPath "$lab\Bin\loader\CAINE\install.json" -Raw | ConvertFrom-Json
foreach($name in @('Updater.exe','ApplyUpdate.ps1','Install-CAINE.ps1','Common.ps1')) { Remove-Item -LiteralPath "$lab\Bin\loader\CAINE\$name" }
$receipt.PSObject.Properties.Remove('updaterFiles');$receipt.version='0.3.8-framework-dev'
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$lab\Bin\loader\CAINE\install.json" -Encoding UTF8
& $installer -GameRoot $lab
Check ((Get-CaineHash "$lab\Bin\loader\CAINE\CAINE.ini") -ceq $configHash) 'Pre-updater upgrade replaced INI'
Check ((Get-CaineHash "$lab\Bin\loader\CAINE\Updater.exe") -ceq $updaterHash) 'Pre-updater upgrade did not add helper'
# Failure after earlier payload copies must restore all owned files and receipt.
$beforeReceipt=Get-CaineHash "$lab\Bin\loader\CAINE\install.json"
$script:injectFailure=$true
function Copy-Item {
    [CmdletBinding()]param([string[]]$LiteralPath,[string]$Destination,[switch]$Force)
    if($script:injectFailure -and $Destination -eq "$lab\Bin\loader\CAINE\Updater.exe" -and $LiteralPath[0].Contains('payload')) {
        $script:injectFailure=$false;throw 'Injected updater copy failure'
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force
}
$rejected=$false
try { & $installer -GameRoot $lab } catch { $rejected=$true }
Remove-Item Function:\Copy-Item
Check $rejected 'Copy-failure injection did not reject installation'
Check ((Get-CaineHash "$lab\Bin\loader\CAINE.asi") -ceq $pluginHash) 'Rollback changed ASI'
Check ((Get-CaineHash "$lab\Bin\loader\CAINE\Updater.exe") -ceq $updaterHash) 'Rollback changed helper'
Check ((Get-CaineHash "$lab\Bin\loader\CAINE\install.json") -ceq $beforeReceipt) 'Rollback changed receipt'
# Reject modified installed update machinery before changing anything.
$helperBytes=[IO.File]::ReadAllBytes("$lab\Bin\loader\CAINE\Updater.exe")
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE\Updater.exe",[byte[]]($helperBytes+0))
$rejected=$false;try { & $installer -GameRoot $lab } catch { $rejected=$true }
Check $rejected 'Modified installed updater was overwritten'
Check ((Get-CaineHash "$lab\Bin\loader\CAINE\install.json") -ceq $beforeReceipt) 'Ownership rejection changed receipt'
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE\Updater.exe",$helperBytes)
# Exact ZIP allowlist rejects traversal even when the outer SHA matches.
$badStage=Join-Path $lab 'bad-archive'
New-Item -ItemType Directory -Path $badStage | Out-Null
foreach($name in @('ApplyUpdate.ps1','Install-CAINE.ps1','Common.ps1')) { Copy-Item -LiteralPath "$lab\Bin\loader\CAINE\$name" -Destination $badStage }
$badZip=Join-Path $badStage 'PROJECT-CAINE-update.zip'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::Open($badZip,[IO.Compression.ZipArchiveMode]::Create)
try { $entry=$archive.CreateEntry('../escape.txt');$writer=New-Object IO.StreamWriter($entry.Open());$writer.Write('bad');$writer.Dispose() } finally { $archive.Dispose() }
@{schema=1;gameRoot=$lab;zip=$badZip;sha256=(Get-CaineHash $badZip);version='0.3.9-framework-dev'} | ConvertTo-Json | Set-Content -LiteralPath "$badStage\request.json" -Encoding UTF8
& powershell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$badStage\ApplyUpdate.ps1" -Request "$badStage\request.json" *> "$badStage\apply.log"
Check ($LASTEXITCODE -ne 0) 'Traversal ZIP was accepted'
Check (!(Test-Path -LiteralPath "$lab\escape.txt")) 'ZIP escaped the staging directory'
Check ((Get-CaineHash "$lab\Bin\loader\CAINE\install.json") -ceq $beforeReceipt) 'Bad ZIP touched installed files'
# Actual native helper: handshake, exact-process wait, hidden PowerShell install, original-argument restart.
$stage=Join-Path $lab 'helper-stage';New-Item -ItemType Directory -Path $stage | Out-Null
foreach($name in @('Updater.exe','ApplyUpdate.ps1','Install-CAINE.ps1','Common.ps1')) { Copy-Item -LiteralPath "$lab\Bin\loader\CAINE\$name" -Destination $stage }
Copy-Item -LiteralPath "$Package\PROJECT-CAINE-update.zip" -Destination "$stage\PROJECT-CAINE-update.zip"
$marker=Join-Path $lab 'exit marker'
$arguments='"'+$marker+'" -game "Unofficial Patch" -w 1280 -h 720'
$game=Start-Process -FilePath "$lab\Vampire.exe" -ArgumentList $arguments -WorkingDirectory $lab -WindowStyle Hidden -PassThru
$eventName='Local\CAINE.Update.'+[Guid]::NewGuid().ToString()
$ready=New-Object Threading.EventWaitHandle($false,[Threading.EventResetMode]::ManualReset,$eventName)
$command='"'+$lab+'\Vampire.exe" '+$arguments
$manifest=Get-Content -LiteralPath "$Package\package.json" -Raw | ConvertFrom-Json
@{schema=1;gameRoot=$lab;zip="$stage\PROJECT-CAINE-update.zip";sha256=(Get-CaineHash "$stage\PROJECT-CAINE-update.zip");
    version=$manifest.version;gamePid=$game.Id;creationTime=$game.StartTime.ToFileTimeUtc();commandLine=$command;
    workingDirectory=$lab;readyEvent=$eventName} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$stage\request.json" -Encoding UTF8
$helper=Start-Process -FilePath "$stage\Updater.exe" -ArgumentList ('--request "'+$stage+'\request.json"') -WorkingDirectory $stage -WindowStyle Hidden -PassThru
try {
    Check ($ready.WaitOne(15000)) 'Native updater did not handshake with fixture game'
    Check (!$game.HasExited) 'Fixture exited before helper waiting check'
    Check ((Get-CaineHash "$lab\Bin\loader\CAINE\install.json") -ceq $beforeReceipt) 'Helper installed before game exited'
    'exit' | Set-Content -LiteralPath $marker
    Check ($helper.WaitForExit(45000)) 'Native updater did not complete installation/restart'
    Check ($helper.ExitCode -eq 0) 'Native updater failed (see helper-stage/progress.json)'
    $record=Get-Content -LiteralPath "$marker.restarted"
    Check ($record[0] -ceq $command) 'Restart changed original launch arguments'
    Check ($record[1] -ceq $lab) 'Restart changed original working directory'
} finally { $ready.Dispose() }
Check ((Get-CaineHash "$lab\Bin\loader\CAINE\CAINE.ini") -ceq $configHash) 'Updater changed INI'
Check ((Get-CaineHash "$lab\Unofficial_Patch\save\untouched.sav") -ceq $saveHash) 'Updater changed save'
Check ((Get-CaineHash "$lab\mods\Unscripted\Unscripted.dll") -ceq $modHash) 'Updater changed independent mod'
Write-Output 'CAINE_UPDATE_DEPLOYMENT_OK: legacy upgrade, rollback, ownership, ZIP traversal, exact process wait, native helper transaction, restart arguments/cwd, save/config/mod preservation'
Write-Output "Disposable lab: $lab"
# The intentionally rejected child archive test must not leak its exit code to Build.ps1.
$global:LASTEXITCODE=0
