param([string]$GameRoot,[ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Mod = 'Unofficial_Patch')
. "$PSScriptRoot\Common.ps1"
$GameRoot = Get-CaineGameRoot $GameRoot
if (!(Test-Path -LiteralPath (Join-Path $GameRoot $Mod) -PathType Container)) { throw "Mod directory missing: $Mod" }
if (!(Test-Path -LiteralPath (Join-Path $GameRoot 'Bin\loader\CAINE.asi'))) { throw 'Install CAINE first.' }
Assert-CaineGameStopped
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = Join-Path $GameRoot 'Vampire.exe'
$start.WorkingDirectory = $GameRoot
$start.Arguments = "-game $Mod"
$start.UseShellExecute = $false
[Diagnostics.Process]::Start($start) | Out-Null
Write-Output 'Bloodlines launched. CAINE also works with your usual launcher.'
