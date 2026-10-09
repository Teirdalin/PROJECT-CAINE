param([ValidateSet('Release','Debug')][string]$Configuration = 'Release', [switch]$WithUnscripted, [string]$TestGameRoot)
. "$PSScriptRoot\Common.ps1"
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ build tools are required.' }
$vs = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install the Visual Studio 2022 Desktop development with C++ workload.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (!(Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
$build = Join-Path $root 'build\x86'
if (!$TestGameRoot -and (Test-Path -LiteralPath "$root\build\game-acceptance\Vampire\dlls\vampire.dll")) { $TestGameRoot = "$root\build\game-acceptance" }
& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A Win32 "-DCMAKE_GENERATOR_INSTANCE=$vs" -DBUILD_TESTING=ON "-DCAINE_TEST_GAME_ROOT=$TestGameRoot" "-DCAINE_BUILD_UNSCRIPTED=$($WithUnscripted.IsPresent.ToString().ToUpperInvariant())"
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE) { throw 'C++ build failed.' }
& $ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Native tests failed. No package created.' }
if ($Configuration -ne 'Release') { Write-Output 'Debug build and tests passed. Use Release for packaging.'; return }
$package = Join-Path $root ('dist\PROJECT-CAINE-0.3.12-framework-dev-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload = Join-Path $package 'payload\Bin\loader'
New-Item -ItemType Directory -Force -Path "$payload\CAINE", "$package\scripts", "$package\licenses", "$package\docs" | Out-Null
Copy-Item -LiteralPath "$root\docs\NATIVE_INTEROP.md","$root\docs\NATIVE_RECOVERY_REPORT.md","$root\docs\MOD_API.md","$root\docs\MENU_RENDERER.md","$root\docs\LONG_STRINGS.md" -Destination "$package\docs"
Copy-Item -LiteralPath "$root\docs\CRASH_REPORTING.md","$root\docs\INTRO_SKIP.md" -Destination "$package\docs"
Copy-Item -LiteralPath "$root\docs\SPLIT_VALIDATION.md" -Destination "$package\docs\SPLIT_VALIDATION.md"
Copy-Item -LiteralPath "$root\assets" -Destination "$package\assets" -Recurse
New-Item -ItemType Directory -Path "$package\include\caine" -Force | Out-Null
Copy-Item -LiteralPath "$root\include\caine\mod_api.h","$root\include\caine\mod.hpp" -Destination "$package\include\caine"
Copy-Item -LiteralPath "$root\examples" -Destination "$package\examples" -Recurse
Copy-Item -LiteralPath "$build\examples\hello-caine\Release\mod.dll" -Destination "$package\examples\hello-caine\mod.dll"
Copy-Item -LiteralPath "$build\Release\CAINE.asi" -Destination "$payload\CAINE.asi"
Copy-Item -LiteralPath "$build\Release\CAINE\CrashReporter.exe" -Destination "$payload\CAINE\CrashReporter.exe"
Copy-Item -LiteralPath "$build\Release\CAINE\Updater.exe" -Destination "$payload\CAINE\Updater.exe"
foreach ($name in @('ApplyUpdate.ps1','Install-CAINE.ps1','Common.ps1')) {
    Copy-Item -LiteralPath "$PSScriptRoot\$name" -Destination "$payload\CAINE\$name"
}
Copy-Item -LiteralPath "$root\docs\UPDATING.md" -Destination "$package\docs"
New-Item -ItemType Directory -Path "$package\symbols" -Force | Out-Null
Copy-Item -LiteralPath "$build\Release\CAINE.pdb" -Destination "$package\symbols\CAINE.pdb"
Copy-Item -LiteralPath "$root\config\CAINE.ini" -Destination "$payload\CAINE\CAINE.ini"
Copy-Item -LiteralPath "$root\assets\branding\PROJECT CAINE.png" -Destination "$payload\CAINE\background.png"
foreach ($name in @('Common.ps1','Install-CAINE.ps1','Uninstall-CAINE.ps1','Launch-CAINE.ps1')) {
    Copy-Item -LiteralPath "$PSScriptRoot\$name" -Destination "$package\scripts\$name"
}
foreach ($name in @('Install PROJECT CAINE.cmd','Uninstall PROJECT CAINE.cmd','Launch PROJECT CAINE.cmd','README.md','LICENSE','PLUGIN_API_PERMISSION.md','THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath "$root\$name" -Destination "$package\$name"
}
Copy-Item -LiteralPath "$root\licenses\JDL-1.txt" -Destination "$package\licenses\JDL-1.txt"
Copy-Item -LiteralPath "$root\third_party\minhook\LICENSE.txt" -Destination "$package\licenses\MinHook.txt"
Copy-Item -LiteralPath "$root\third_party\json\LICENSE.MIT" -Destination "$package\licenses\nlohmann-json.txt"
Copy-Item -LiteralPath "$root\third_party\imgui\LICENSE.txt" -Destination "$package\licenses\DearImGui.txt"
$files = @(Get-ChildItem -LiteralPath "$package\payload" -Recurse -File | ForEach-Object {
    @{ path=$_.FullName.Substring((Join-Path $package 'payload').Length+1).Replace('\','/'); sha256=(Get-CaineHash $_.FullName) }
})
@{version='0.3.12-framework-dev';architecture='x86';license='LicenseRef-JDL-1';files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$package\package.json" -Encoding UTF8
& "$root\tests\deployment_tests.ps1" -Package $package -FixtureExe "$build\test-host\Release\Vampire.exe" -FixtureDll "$build\fixtures\Release\engine.dll"
$updateZip=Join-Path $package 'PROJECT-CAINE-update.zip'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::Open($updateZip,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in @('package.json') + @($files | ForEach-Object { 'payload/'+$_.path })) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(Join-Path $package $name),$name,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
& "$root\tests\update_deployment_tests.ps1" -Package $package -FixtureExe "$build\test-host\Release\Vampire.exe" -FixtureDll "$build\fixtures\Release\engine.dll" -UpdateHost "$build\update-host\Release\Vampire.exe"
Compress-Archive -Path "$package\*" -DestinationPath "$package.zip"
$package | Set-Content -LiteralPath "$root\dist\latest-package.txt" -Encoding UTF8
Write-Output "CAINE_BUILD_OK: $package"
Write-Output "ZIP: $package.zip"
