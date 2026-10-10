. "$PSScriptRoot\..\scripts\Common.ps1"
function Check([bool]$Ok,[string]$Message) { if (!$Ok) { throw $Message } }
$lab=Join-Path ([IO.Path]::GetTempPath()) ('CAINE-launch-profile-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$lab\Vampire","$lab\CustomMod" | Out-Null
Check ((Get-CaineLaunchProfile $lab 'Auto') -ceq 'Vampire') 'Vanilla fallback'
New-Item -ItemType Directory -Path "$lab\Unofficial_Patch\cfg" | Out-Null
Check ((Get-CaineLaunchProfile $lab 'Auto') -ceq 'Vampire') 'Incomplete patch folder was selected'
New-Item -ItemType Directory -Path "$lab\Unofficial_Patch\maps" | Out-Null
Check ((Get-CaineLaunchProfile $lab 'Auto') -ceq 'Unofficial_Patch') 'Installed patch not selected'
Check ((Get-CaineLaunchProfile $lab 'Vampire') -ceq 'Vampire') 'Explicit vanilla choice overridden'
Check ((Get-CaineLaunchProfile $lab 'CustomMod') -ceq 'CustomMod') 'Third-party profile overridden'
Check ((& "$PSScriptRoot\..\scripts\Launch-CAINE.ps1" -GameRoot $lab -DetectOnly) -ceq 'CAINE_LAUNCH_PROFILE: Unofficial_Patch') 'Production launcher detection'
foreach ($bad in @('../Vampire','Missing','Vampire;quit')) {
    $rejected=$false;try { Get-CaineLaunchProfile $lab $bad | Out-Null } catch { $rejected=$true }
    Check $rejected "Unsafe or missing profile accepted: $bad"
}
Write-Output 'CAINE_LAUNCH_PROFILE_OK: patch detection, native profile selection, vanilla fallback, explicit overrides and invalid-path rejection; no game launched'
