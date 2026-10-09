# PROJECT CAINE

![PROJECT CAINE](assets/branding/PROJECT%20CAINE.png)

**A native modding framework for Vampire: The Masquerade — Bloodlines.**

[Download development releases](https://github.com/Teirdalin/PROJECT-CAINE/releases) · [Mod API](docs/MOD_API.md) · [Updates](docs/UPDATING.md) · [License](LICENSE)

CAINE loads with normal Windows x86 game startup through Bloodlines' existing
Game Mod Loader. It supplies a shared guarded hook manager, a versioned native
plugin API, modern menus, configuration, crash reports and serialization helpers.
CAINE has no AI service dependency. **Bloodlines: Unscripted** is a separate,
optional AI mod and is not distributed by this repository or framework updater.

Version **0.3.12-framework-dev** is a development prerelease. Native and renderer
regressions pass, but live gameplay acceptance remains pending. Character-creation
crash investigation and optional Unscripted conversation acceptance are not
claimed resolved by this release. No game binaries or unofficial SDK code are included.

## Install

1. Download and extract the **PROJECT-CAINE-0.3.12-framework-dev.zip** player asset
   from [Releases](https://github.com/Teirdalin/PROJECT-CAINE/releases).
2. Close Bloodlines and its mod selection window.
3. Double-click **Install PROJECT CAINE.cmd** and select your **Vampire.exe**.
   A compatible existing native loader and its Bin/loader directory are required.
4. Start Bloodlines with your usual launcher.

Installation verifies hashes, backs up owned previous files, and retains existing
configuration, saves and other mods. Modified or unowned files are preserved and
stop the installation. Command-line installation also supports an explicit path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Install-CAINE.ps1 -GameRoot "D:\Games\Bloodlines"
```

To uninstall, run **Uninstall PROJECT CAINE.cmd -GameRoot "D:\Games\Bloodlines"**.
Configuration, mods, logs, backups and saves are retained. The framework's own
files live under `Bin/loader/CAINE.asi` and `Bin/loader/CAINE/`.

## Menus and updates

Main-menu **Quit** exits directly. Pause-menu exits retain the unsaved-progress
confirmation.

CAINE supplies left-side main and pause navigation, its own background artwork,
modern settings, save/load and Mods pages. Resolution is a dropdown; keybinds
have primary/alternative boxes and a **Press the new key** popup. Specialized
panels such as the character sheet retain their original style. Startup logo
videos are skipped by default; hold Escape for 1.5 seconds during the opening
cinematic to use the supported native skip to Jack's tutorial.

Graphics offers a 60–135 degree field-of-view slider that applies immediately
through Bloodlines' `fov` command and remembers the preference across loads
and restarts. Other staged settings retain their Apply/Discard controls.

**Update Available** appears at the bottom of the main/pause navigation when a
newer release is ready. Clicking it downloads and verifies the update, opens
installation progress, then restarts Bloodlines with the original launch options.
Save first: unsaved gameplay is lost when the game closes. The updater retains
existing configuration and does not update Unscripted or other mods.
See [update behavior and configuration](docs/UPDATING.md).

Unsupported game fingerprints retain native behavior and log feature rejection.
Set `Menu/Modern=0` in `Bin/loader/CAINE/CAINE.ini` to use native menus, or
`Updates/Check=0` to disable update checks. Unknown patches are never overwritten.
[Renderer coverage](docs/MENU_RENDERER.md) describes compatibility and live-test limits.

## Plugins

Plugins use one folder each, for example:

```text
mods/Example/Example.dll
mods/Example/Example.xml
mods/Example/Example.cfg
mods/Example/logo.png
```

The XML identifies the library, configuration and artwork. CAINE's Mods page
shows metadata, load status, configuration and enable/disable controls. Native
plugin changes take effect after restarting. Plugins are trusted executable code.
See [the native C ABI](docs/MOD_API.md), [Hello CAINE](examples/hello-caine),
and [native interoperability](docs/NATIVE_INTEROP.md).

## Build

Use Windows, Visual Studio 2022 Desktop development with C++, and the Windows SDK.
Double-click **Build PROJECT CAINE.cmd** or run `scripts/Build.ps1`. Dependencies
are vendored. Native, renderer, deployment and updater tests run before packaging;
player and minimal update ZIPs are written to `dist/`. No installed game is
required for the framework tests. Optional exact native tests require a separately
provided game installation. Never copy game binaries into source control.

The public repository builds CAINE alone. The local `CAINE_BUILD_UNSCRIPTED`
option requires the separately maintained Unscripted source tree.
[Development guidance](docs/DEVELOPMENT.md) documents hook lifetime and verification.

## Diagnostics

Logs: `%LOCALAPPDATA%\PROJECT CAINE\Bloodlines\logs`. Local crash reports and
minidumps: `%LOCALAPPDATA%\PROJECT CAINE\Bloodlines\crashes`.
[Crash reporting](docs/CRASH_REPORTING.md) documents limits and configuration.
`CAINE_READY` indicates engine observation; individual feature readiness is logged
separately. Set `Runtime/Enabled=0` or `CAINE_DISABLED=1` for an emergency opt-out.

## License

Copyright (c) 2026 Teirdalin. PROJECT CAINE uses
[The Jelly Doughnut License (JDL-1)](LICENSE), identifier `LicenseRef-JDL-1`.
The public SDK and Hello CAINE example have the additional
[plugin-development permission](PLUGIN_API_PERMISSION.md). Independently authored
mods may use their authors' chosen licenses. Dependencies retain their own terms;
see [third-party notices](THIRD_PARTY_NOTICES.md).
