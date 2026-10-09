param([Parameter(Mandatory=$true)][string]$Package,[Parameter(Mandatory=$true)][string]$FixtureExe,[Parameter(Mandatory=$true)][string]$FixtureDll)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Check([bool]$Ok,[string]$Message) { if (!$Ok) { throw $Message } }
$lab = Join-Path (Split-Path $PSScriptRoot -Parent) ('build\install-lab-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$lab\Bin\loader", "$lab\Unofficial_Patch\save" -Force | Out-Null
Copy-Item -LiteralPath $FixtureExe -Destination "$lab\Vampire.exe"
Copy-Item -LiteralPath $FixtureDll -Destination "$lab\Loader.dll"
'unrelated plugin' | Set-Content -LiteralPath "$lab\Bin\loader\other.asi"
'save sentinel' | Set-Content -LiteralPath "$lab\Unofficial_Patch\save\untouched.sav"
$saveHash = (Get-FileHash -LiteralPath "$lab\Unofficial_Patch\save\untouched.sav").Hash
$otherHash = (Get-FileHash -LiteralPath "$lab\Bin\loader\other.asi").Hash
$install = Join-Path $Package 'scripts\Install-CAINE.ps1'
$uninstall = Join-Path $Package 'scripts\Uninstall-CAINE.ps1'
& $install -GameRoot $lab
Check (Test-Path -LiteralPath "$lab\Bin\loader\CAINE.asi") 'Install must create ASI'
Check (Test-Path -LiteralPath "$lab\Bin\loader\CAINE\background.png") 'Install must create the menu artwork'
Check (Test-Path -LiteralPath "$lab\Bin\loader\CAINE\CrashReporter.exe") 'Install must create the crash reporter'
$reporterHash=(Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE\CrashReporter.exe").Hash
$backgroundHash = (Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE\background.png").Hash
$pluginHash = (Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE.asi").Hash
'[Runtime]', 'Enabled=0' | Set-Content -LiteralPath "$lab\Bin\loader\CAINE\CAINE.ini"
$configHash = (Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE\CAINE.ini").Hash
# Upgrade an ownership receipt from before the reporter existed.
Remove-Item -LiteralPath "$lab\Bin\loader\CAINE\CrashReporter.exe"
$oldReceipt=Get-Content -LiteralPath "$lab\Bin\loader\CAINE\install.json" -Raw | ConvertFrom-Json
$oldReceipt.PSObject.Properties.Remove('crashReporterSha256')
$oldReceipt.version='0.3.3-framework-dev'
$oldReceipt | ConvertTo-Json | Set-Content -LiteralPath "$lab\Bin\loader\CAINE\install.json" -Encoding UTF8
& $install -GameRoot $lab
Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE\CAINE.ini").Hash -eq $configHash) 'Upgrade must preserve configuration'
Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE\CrashReporter.exe").Hash -eq $reporterHash) 'Upgrade must preserve reporter identity'
# Tampered payload fails closed without touching installed files.
$sourceAsi = Join-Path $Package 'payload\Bin\loader\CAINE.asi'
$sourceBytes = [IO.File]::ReadAllBytes($sourceAsi)
try {
    [IO.File]::WriteAllBytes($sourceAsi, [byte[]]($sourceBytes + 0))
    $rejected = $false
    try { & $install -GameRoot $lab } catch { $rejected = $true }
    Check $rejected 'Tampered package must be rejected'
    Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE.asi").Hash -eq $pluginHash) 'Tampered package changed installed ASI'
} finally { [IO.File]::WriteAllBytes($sourceAsi,$sourceBytes) }
# Modified artwork must be preserved, and reject the operation before the ASI changes.
$backgroundBytes=[IO.File]::ReadAllBytes("$lab\Bin\loader\CAINE\background.png")
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE\background.png",[byte[]]($backgroundBytes+0))
$rejected=$false
try { & $install -GameRoot $lab } catch { $rejected=$true }
Check $rejected 'Changed background must not be overwritten'
Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE.asi").Hash -eq $pluginHash) 'Background rejection changed the ASI'
$rejected=$false
try { & $uninstall -GameRoot $lab } catch { $rejected=$true }
Check $rejected 'Changed background must not be removed'
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE\background.png",$backgroundBytes)
# Reporter ownership and payload integrity have the same guards as the ASI.
$reporterSource=Join-Path $Package 'payload\Bin\loader\CAINE\CrashReporter.exe'
$reporterBytes=[IO.File]::ReadAllBytes($reporterSource)
try {
    [IO.File]::WriteAllBytes($reporterSource,[byte[]]($reporterBytes+0))
    $rejected=$false;try { & $install -GameRoot $lab } catch { $rejected=$true }
    Check $rejected 'Tampered reporter payload must be rejected'
    Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE\CrashReporter.exe").Hash -eq $reporterHash) 'Reporter payload rejection changed installed reporter'
} finally { [IO.File]::WriteAllBytes($reporterSource,$reporterBytes) }
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE\CrashReporter.exe",[byte[]]($reporterBytes+0))
$rejected=$false;try { & $install -GameRoot $lab } catch { $rejected=$true }
Check $rejected 'Changed installed reporter must not be overwritten'
$rejected=$false;try { & $uninstall -GameRoot $lab } catch { $rejected=$true }
Check $rejected 'Changed installed reporter must not be removed'
Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE.asi").Hash -eq $pluginHash) 'Reporter ownership rejection changed ASI'
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE\CrashReporter.exe",$reporterBytes)
# Changed installed files are not removed or overwritten.
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE.asi", [byte[]]($sourceBytes + 0))
$rejected = $false
try { & $uninstall -GameRoot $lab } catch { $rejected = $true }
Check $rejected 'Changed installed ASI must be preserved'
$rejected = $false
try { & $install -GameRoot $lab } catch { $rejected = $true }
Check $rejected 'Changed installed ASI must not be overwritten'
[IO.File]::WriteAllBytes("$lab\Bin\loader\CAINE.asi",$sourceBytes)
& $uninstall -GameRoot $lab
Check (!(Test-Path -LiteralPath "$lab\Bin\loader\CAINE.asi")) 'Uninstall must remove owned ASI'
Check (!(Test-Path -LiteralPath "$lab\Bin\loader\CAINE\background.png")) 'Uninstall must remove owned menu artwork'
Check (!(Test-Path -LiteralPath "$lab\Bin\loader\CAINE\CrashReporter.exe")) 'Uninstall must remove owned crash reporter'
Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\CAINE\CAINE.ini").Hash -eq $configHash) 'Uninstall must retain configuration'
Check ((Get-FileHash -LiteralPath "$lab\Unofficial_Patch\save\untouched.sav").Hash -eq $saveHash) 'Save was changed'
Check ((Get-FileHash -LiteralPath "$lab\Bin\loader\other.asi").Hash -eq $otherHash) 'Other plugin was changed'
# Unowned ASI cannot be claimed by installer.
Copy-Item -LiteralPath $reporterSource -Destination "$lab\Bin\loader\CAINE\CrashReporter.exe"
$rejected=$false;try { & $install -GameRoot $lab } catch { $rejected=$true }
Check $rejected 'Unowned reporter must be preserved'
Check (!(Test-Path -LiteralPath "$lab\Bin\loader\CAINE.asi")) 'Unowned reporter rejection must precede ASI installation'
Remove-Item -LiteralPath "$lab\Bin\loader\CAINE\CrashReporter.exe"
Copy-Item -LiteralPath $sourceAsi -Destination "$lab\Bin\loader\CAINE.asi"
$rejected = $false
try { & $install -GameRoot $lab } catch { $rejected = $true }
Check $rejected 'Unowned ASI must be preserved'
Write-Output 'CAINE_DEPLOYMENT_OK: install, upgrade, config preservation, tamper/ownership guards, uninstall, save/other-plugin preservation'
Write-Output "Disposable lab: $lab"
