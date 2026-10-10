param([string]$GameRoot,[ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Mod = 'Auto',[switch]$DetectOnly,[switch]$ChooseGame)
. "$PSScriptRoot\Common.ps1"
if ($ChooseGame -and !$GameRoot -and !$DetectOnly) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog=New-Object Windows.Forms.OpenFileDialog
    $dialog.Title='Select Vampire.exe in your Bloodlines installation'
    $dialog.Filter='Bloodlines executable (Vampire.exe)|Vampire.exe'
    $dialog.CheckFileExists=$true
    try {
        if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { Write-Output 'Launch cancelled.';return }
        $GameRoot=Split-Path $dialog.FileName -Parent
    } finally { $dialog.Dispose() }
}
$GameRoot = Get-CaineGameRoot $GameRoot
$Mod = Get-CaineLaunchProfile $GameRoot $Mod
if ($DetectOnly) { Write-Output "CAINE_LAUNCH_PROFILE: $Mod"; return }
if (!(Test-Path -LiteralPath (Join-Path $GameRoot 'Bin\loader\CAINE.asi'))) { throw 'Install CAINE first.' }
Assert-CaineGameStopped $GameRoot
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = Join-Path $GameRoot 'Vampire.exe'
$start.WorkingDirectory = $GameRoot
$start.Arguments = "-game $Mod"
$start.UseShellExecute = $false
[Diagnostics.Process]::Start($start) | Out-Null
Write-Output "Bloodlines launched with native profile: $Mod. CAINE also works with your usual launcher."
