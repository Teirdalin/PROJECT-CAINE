# Modern menus (CAINE 0.3.8)

CAINE draws inside Bloodlines with Dear ImGui 1.91.9b and the upstream DirectX 9
backend. Normal startup through the native loader is retained.

The main menu follows the revised placement: navigation in the left-middle and
the complete PROJECT CAINE artwork centered across the background. Native menu
availability determines New Game, Continue, Reload, Save, Load, Main Menu and Quit;
CAINE adds Mods and Credits and presents Options as Settings. Escape resumes a
paused game or returns from a CAINE page. Stock transitions and confirmation
dialogs retain input whenever the native main menu reports itself busy.
Navigation starts at 14.5% of screen width and centers around 44% of screen
height, with vertical bounds for smaller resolutions and longer pause menus.

The modern settings pages cover Audio, Mouse, Keyboard, Gameplay, Video and
Visual options. Ordinary settings are staged until Apply; Discard and leaving
the screen abandon pending changes. Resolution is a dropdown of deduplicated
native display modes; it shows the current or pending selection. Keyboard actions
have Primary and Alternative boxes. Clicking either opens "Press the new key";
keyboard keys, mouse buttons and the wheel are captured. Escape, Cancel and
focus loss dismiss capture without changing bindings. Clear binding removes only
that slot. Rebinding uses existing engine bind/unbind commands and persists
immediately, preserving the other slot and any additional existing bindings.
Names are checked against the game's native key table before command submission.
Video choices are read from the engine's real mode list
and use the installed `_setvideomode` command. Video changes may require restart.
`host_writeconfig` persists engine settings. No launch arguments are rewritten.

Save/Load lists scan only the active `-game` folder. The browser does not rewrite
save files: confirmed actions dispatch native save/load commands so the engine
and Unscripted persistence hooks still own serialization. Existing save overwrites
require confirmation, including names differing only by case; load also confirms.
New names allow ASCII letters, numbers, underscores and hyphens, up to 128 bytes.
This build lists filenames rather than character portraits and saved map metadata.

Credits read the installed game's effective loose/VPK credits resource. Original
game credits are not redistributed in the framework package. Mod authors and
framework dependencies are listed separately.

The Mods browser keeps a scrolling catalog, artwork/details and restart enable
checkbox. Unscripted uses tabs, searchable NPC/voice lists, masked key input,
Unicode text fields, checkboxes and sliders. The old eight-row menu adapter is
retained as fallback. Rich controls are a size-gated extension of mod ABI v1.

The character sheet, inventory, quest log, dialogue and HUD retain their original
visual identity and behavior in this build. Character creation remains native.
The new renderer does not yet adapt all of those specialized gameplay panels.

## Profiles and render ownership

The supported client hash and original four exact native menu hook byte ranges
remain guarded. Native Busy at client RVA 0x65c90 is used only under that profile.
Engine SHA-256 `9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5`
and exact runtime method addresses guard VEngineCvar001, VEngineClient006 and
VENGINE_GAMEUIFUNCS_VERSION001. No current Source SDK vtable layout is assumed.
An unsupported engine retains its native options and save/load dialogs.

The supported shader hook remains at RVA 0x1b2fc, immediately before EndScene
in SwapBuffers. Rendering requires the native menu's visibility check and a
same-thread frame marker. Outside the opening cinematic, gameplay/loading frames with no menu paint do
not draw or capture CAINE input. The passive intro skip prompt shares this same
presentation hook; see INTRO_SKIP.md. It owns no menu widgets or mouse cursor. GUI actions run on subsequent native menu paint,
not inside DirectX rendering. Network requests use the mod's asynchronous worker.

The upstream backend restores render state. Default-pool font and geometry
buffers are released after each visible frame so native device Reset paths can
continue without outstanding UI resources. Managed logo textures are bounded
to 16 MiB/4096x4096 and cached, with a limit of 64 entries. Device replacement
clears their cache. Segoe UI is read from Windows; no external font is packaged.
System keys and Alt-F4 stay native. Input capture expires if no modern frame
is rendered for 500 ms.

`Bin/loader/CAINE/background.png` is an owned, hash-checked asset; upgrade and
uninstall back it up and reject user-modified or unowned artwork. Configuration,
saves and unrelated plugins remain preserved. Set [Menu] Modern=0 and restart
to use native menus. The intro prompt remains independently configurable with
[Intro] Enabled. Existing INI files are preserved; absent Modern defaults on.

CAINE page changes are immediate. Native button reconstruction happens only when
switching between native and modern presentation, instead of on every page.
Under the exact client profile, visual alpha fields at +0x2d8 and +0x2e0 are
snapped to their +0x2dc target (constructor at 0x6580b, Paint at 0x65af0).
Original Paint retains native lifecycle/audio and dialog handling. The CAINE
background is opaque so the stock pause artwork cannot bleed through it.

Startup skips default on independently with [Startup] SkipVideos=1. An
engine-hash/byte-guarded cdecl playback hook at engine RVA 0xfbd10 bypasses only
the four hardcoded Activision, White Wolf, NVIDIA and Troika logo paths. Other
videos still call the original helper. No media files or launch arguments change.
Set SkipVideos=0 and restart to retain startup logos. Initialization attempts the
hook as soon as engine.dll is available on CAINE's worker; live startup timing
still needs acceptance with the player's launcher.

## Verification boundary

Native renderer tests cover PNG output, state restoration, device Reset and
rerender, main-menu action routing, Unicode editing and masked field submission.
The actual capture popup is exercised for opening-key isolation, repeat rejection,
fresh keys, Escape, focus loss and wheel input. A mapped installed engine test
executes its playback entry through the production detour for all four logo paths.
Game-menu tests use a synthetic engine adapter and test staging, numeric bounds,
inversion, video dispatch, bindings, active save-folder selection, confirmations,
command validation and effective installed-resource reading. These tests do not
execute those commands in a real game. The real installed Python/save-reader
regression remains separate and verifies long-string compatibility.

PNG previews are generated by the actual renderer with fixture data at 800x600,
1280x720 and 1920x1080. Live menu visuals, mouse capture, save/load, character
creation, fullscreen Alt-Tab and resolution changes still need acceptance in the
protected game copy. A passing offline suite does not establish that acceptance.

## Upstream

Dear ImGui: https://github.com/ocornut/imgui/releases/tag/v1.91.9b (MIT).
Source provenance: third_party/imgui/SOURCE.json.
Backend: https://github.com/ocornut/imgui/blob/v1.91.9b/backends/imgui_impl_dx9.cpp.

## Cursor ownership (0.3.8)

Bloodlines owns the menu cursor. CAINE no longer draws an ImGui cursor or suppresses native WM_SETCURSOR handling. Its mouse position tracking and menu input capture remain active. Native renderer/action checks pass; single-cursor appearance requires live acceptance.
