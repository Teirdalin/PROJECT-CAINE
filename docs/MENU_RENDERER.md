# Modern menus (CAINE 0.3.21)

CAINE draws inside Bloodlines with Dear ImGui 1.91.9b and the upstream DirectX 9
backend. Normal startup through the native loader is retained.

The main menu follows the revised placement: navigation in the left-middle and
the complete PROJECT CAINE artwork centered across the background.
The artwork's top and bottom are feathered into black with a smooth gradient;
the original PNG remains unchanged.
Native menu availability determines New Game, Reload, Save, Load, Main Menu and Quit;
CAINE adds Mods and Credits and presents Options as Settings. Escape resumes a
paused game or returns from a CAINE page. Stock transitions and confirmation
dialogs retain input whenever the native main menu reports itself busy.
Navigation starts at 14.5% of screen width and centers around 44% of screen
height, with vertical bounds for smaller resolutions and longer pause menus.
Continue is the first main-menu entry and loads the newest recognizable native
save in the active profile. It is disabled when none exists. The cached listing
refreshes once per second and is revalidated when clicked, checks the `JSAV`
header, rejects redirected/empty files and unsafe command tokens, and never
falls back to another profile's saves. Mod readiness gating also applies.
Quit from the main menu exits directly. Pause-menu quit confirmation is a
compact centered dialog with side-by-side Quit and Cancel buttons.

The modern settings pages cover Audio, Mouse, Keyboard, Gameplay, Display,
Graphics and Framework options. Ordinary settings are staged until Apply; Discard and leaving
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
`host_writeconfig` persists archived engine settings. CAINE remembers the added
non-archived `fps_max` and `mat_trilinear` preferences in a profile-specific INI
section, restoring only values explicitly selected in CAINE. No launch arguments
are rewritten. Framework preferences expose the existing runtime options with
restart requirements, plus live 75â€“150% interface scaling and verbosity.

Save/Load lists scan only the active `-game` folder. The browser does not rewrite
save files: confirmed actions dispatch native save/load commands so the engine
and Unscripted persistence hooks still own serialization. Existing save overwrites
require confirmation, including names differing only by case. Main-menu load
proceeds directly; in-game load confirms. Clicks re-enumerate saves and reject
missing, redirected or unrecognized load files. A file created after the list
was drawn still requires overwrite confirmation.
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
same-thread frame marker. During gameplay, an active mod-owned conversation may
render and capture input without native menu paint. Other gameplay/loading frames
do not draw or capture CAINE input. The passive intro skip prompt shares this same
presentation hook; see INTRO_SKIP.md. It owns no menu widgets or mouse cursor. GUI actions run on subsequent native menu paint,
not inside DirectX rendering. Network requests use the mod's asynchronous worker.

Conversation overlays use a wider bottom-anchored panel with a screen-edge
margin, a scrolling history/choice area and a pinned reply composer. Panel
height is capped at 38% of the screen at normal interface scale; enlarged text
can increase that limit just enough to keep the pinned controls usable. The
NPC and upper scene stay visible even with long responses or native choices.
Long responses remain scrollable and wrapped.
Reply input receives focus on opening or becoming available. A software cursor
is drawn only for gameplay overlays; main and pause menus keep native cursor
ownership. Window message capture is supplemented by six exact-profile native
CInput hooks for activation/deactivation, mouse polling, camera movement,
recentering and buttons. Native activation is suppressed while capture is held.
Closing, losing focus, native pause/deactivation, and an expired 500 ms frame
lease release ownership. The native activation routine restores the previous
mouse mode only when the game is foreground. The character sheet and other
specialized native panels retain their own input behavior.

The Dear ImGui backend restores render state. CAINE's small backend extension
retains a static managed-pool font atlas across frames and native device Reset,
avoiding a font upload on every visible frame. Devices that reject managed
textures retain the default-pool fallback. Default-pool geometry buffers and
fallback fonts are released after each visible frame so native device Reset
can continue without outstanding UI resources. Managed logo textures are bounded
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
engine-hash/byte-guarded hook at engine RVA 0xfb524 bypasses the exact four-call
Activision, White Wolf, NVIDIA and Troika startup block, landing at 0xfb55b after
its stack cleanup. The entire block and filenames are validated. The general
video playback entry is left untouched, allowing installed playback patches and
story videos to retain their behavior. No media files or launch arguments change.
Set SkipVideos=0 and restart to retain startup logos. Initialization attempts the
hook as soon as engine.dll is available on CAINE's worker; live startup timing
still needs acceptance with the player's launcher.

## Verification boundary

Native renderer tests cover PNG output, state restoration, device Reset and
rerender, main-menu action routing, Unicode editing and masked field submission.
The actual capture popup is exercised for opening-key isolation, repeat rejection,
fresh keys, Escape, focus loss and wheel input. A mapped installed engine test
executes the real startup block through the production detour repeatedly, with
an independently patched playback entry, to check coexistence and stack ABI.
The mapped installed client test executes production CInput hooks and original
activation/deactivation routines, verifying polling suppression, cleared deltas,
restoration, focus loss, native pause and lease expiry without a game world.
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

## Input and cursor ownership (0.3.16)

Bloodlines owns the cursor on native main/pause menus. A mod's gameplay overlay
draws one software cursor and suppresses the native cursor while it owns input.
The window is selected from the actual D3D9 swap chain, with the device's focus
window as fallback, rather than an arbitrary visible window in the process.

CAINE installs a game-thread `WH_GETMESSAGE` hook. Removed messages for that
window pass through the same modal policy as directly sent window messages.
Captured events reach the renderer before Bloodlines' internal queued input or
VGUI handling. Captured key-downs are translated once with Windows' current
keyboard layout; subsequent Unicode character messages enter the same queue.
Non-removing peeks, other windows, focus/lifecycle messages, non-modal gameplay
and system shortcuts retain native dispatch. A native WNDPROC replacement does
not remove the queue hook. No polling-to-text conversion or synthetic typing is
used in the game.

The existing foreground frame lease also blocks exact-profile engine cursor
warps at engine RVA `0x4bb50` and VGUI surface warps at `0x2b40`; the latter
blocks only the captured HWND. Outside the lease both delegate to their native
trampolines. Both module SHA-256 identities and complete entry instructions are
validated. Unsupported profiles retain their existing cursor behavior.

Tests exercise actual removed Windows messages on owned hidden windows after
a WNDPROC replacement, and actual D3D9 widgets for Unicode/Enter submission and
an End Conversation mouse click. The installed engine/client/surface binaries
are mapped locally to execute the production cursor detours, including native
passthrough. These checks do not establish live NPC, fullscreen or Alt-Tab
acceptance. Input diagnostics record cursor validity, foreground state, client
dimensions and event counts every five seconds, without keys or typed text.

## Conversation pointer (0.3.17)

The latest 0.3.16 gameplay log confirms a submitted player message and an AI
reply, while the player still reported an invisible cursor. Input delivery and
cursor presentation therefore need separate validation. Gameplay overlays now
draw a white arrow or text I-beam as final foreground geometry, using the same
solid-pixel path as the visible UI. They no longer depend on the font atlas's
cursor sprites or OS/VGUI cursor visibility. The native main/pause menu cursor
and cinematic overlay behavior remain unchanged. A fresh validated client-space
position keeps the pointer responsive during queued event bursts; otherwise the
processed UI position is used. Focus loss without mouse events hides the pointer.

Bounded `CAINE_OVERLAY_CURSOR` diagnostics add the drawn flag, pointer and processed
UI positions and shape. They contain no message text or credentials. A 1280x720
pixel-readback regression covers main menu to conversation, first-frame pointer,
device Reset and rerender. Live NPC cursor visibility still needs player acceptance.
