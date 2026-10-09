# Hold Escape to skip the opening cinematic (CAINE 0.3.5)

The Embrace/courtroom opening on `sp_theatre` displays a small **Hold Escape to
Skip** prompt at the lower right. A crimson progress bar fills during a continuous
1.5-second hold. Releasing Escape cancels it. On completion, CAINE queues the
game's native skip and transitions to `sp_tutorial_1` with landmark `tutorial`,
where the tutorial with Jack begins. This does not skip the tutorial or publisher
startup videos.

The native `vskip_intro` command sets a flag; it does not itself interrupt a scene
that has already started. CAINE queues these commands, in order, via the installed
`CEngineClient::ClientCmd` command buffer:

```
vskip_intro
ent_fire embrace_o_matic Start
```

The Start input's native skip branch consumes and clears the flag, sets
`G.Story_State=-3`, and invokes the game's change-level/landmark routine. Re-entering
Start takes this branch instead of restarting the animation. `ent_fire` is
registered without cheat flags in the supported native build; its callback queues
the entity input for the server. CAINE does not enable cheats, replace saves,
perform a raw `map` jump, run Python or call server entities from rendering.
An exact-byte hook at the native `vskip_intro` callback (game RVA 0xdb3f0)
guards CAINE's queued request again on the command thread. If the opening ended
before it executes, the flag is not set. A flag owned by CAINE that has not been
consumed is cleared on leaving the opening; other native skip requests remain
unchanged. This prevents a late hold from leaving a skip flag for a later scene.

## Guards and input

The active client's current level, running state, pause state and console visibility
are queried only on the game's window/presentation thread. Character creation,
native main/pause menus, loading, other maps and an unfocused game do not show the
prompt. Scene entry requires a released key before the next hold. Focus loss,
pause, console use, a clock reversal or a rendering gap longer than 250 ms cancels
the hold and requires a fresh press. A completed request cannot fire twice in the
same opening. The tail of its Escape press is swallowed until release so it does
not immediately open the tutorial's pause menu. Other keys and system shortcuts
remain native; Escape's saved binding is never rewritten.

Engine SHA-256:
`9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5`

Client SHA-256:
`9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01`

Game SHA-256:
`996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8`

Verified `VEngineClient006` slots: 6 console visibility (RVA 0x1a1b0),
28 queued ClientCmd (0x1a570), 57 active client/server (0x1a9e0),
59 pause (0x1aa20), 106 GetLevelName (0x1b800). Runtime vtable addresses are
checked in addition to the file hashes. Slot 79 is the game directory getter,
and is not used for level detection. The effective loose `sp_theatre.bsp` must
contain `embrace_o_matic` as a choreographed scene and the expected tutorial
change-level entity. Unsupported modules or map contracts leave native behavior.

The prompt reuses CAINE's existing shader presentation hook. It draws only its
small panel and bar over the cinematic; no full-screen background or mouse cursor
is added. Direct3D state restoration and per-frame resource release are shared
with the existing renderer. No additional server frame hook is installed, so
Unscripted retains ownership of its existing frame callback.

## Configuration and logs

Existing INI files are preserved; absent values default to:

```ini
[Intro]
Enabled=1
HoldMilliseconds=1500
```

The supported duration is 500-5000 ms. Restart to apply changes. This feature
works independently of `[Menu] Modern`. Set `[Intro] Enabled=0` to restore native
Escape handling for the opening cinematic.

CAINE records `CAINE_INTRO_SKIP_PROFILE`, `CAINE_INTRO_SKIP_READY`,
`CAINE_INTRO_ENTERED`, `CAINE_INTRO_PROMPT_SHOWN`, `CAINE_INTRO_SKIP_REQUESTED`
and `CAINE_INTRO_LEFT` as applicable, or an unavailability reason. These records
also enter crash-report breadcrumbs.
`CAINE_INTRO_SKIP_COMMAND_ACCEPTED`, `CAINE_INTRO_SKIP_CANCELLED` and
`CAINE_INTRO_SKIP_FLAG_CLEARED` describe the command guard when applicable.

## Validation boundary

Native tests exercise hold timing, entry/release safeguards, cancellation,
single-request behavior, level-name restrictions, duration bounds, malformed BSP
lumps and the installed base/Unofficial Patch entity contracts. Renderer tests
verify an unchanged cinematic background, state restoration, device Reset and
rerender, and no emitted GUI action. Binary inspection verifies the native skip
flag, Start input branch, tutorial arguments, command registration and actual
engine interface slots.
An isolated native test maps the installed game DLL without initializing it,
executes its relocated skip callback through an exact-byte detour/trampoline,
and runs the actual Start skip branch. The Python and level-transition helper
functions are intercepted to verify the story assignment and level/landmark
arguments without creating a game world. It also checks rejected flag requests,
flag consumption and hook restoration.

These checks do not execute a real cinematic or map transition. Live acceptance
must verify a fresh character's opening, short presses, mid-scene holding,
Alt-Tab/console cancellation, arrival with Jack and normal Escape behavior in
gameplay. This feature does not establish a fix for the separate reported
character-creation crash.
