# Native development contract

## Loading and lifetime

The installed Game Mod Loader documents scanning `Bin/loader` for `.asi`/`.vtm`
libraries. CAINE uses this route and does not supply a proxy renderer DLL or an
external injector. `DllMain` only captures its handle, checks the emergency opt-out,
and starts a worker without waiting. Initialization happens after loader-lock release.
The worker pins CAINE for process lifetime. Do not hot-unload it, invoke cleanup from
`DllMain`, or replace its binary while Bloodlines is running. Restart to update.

Status export `LONG WINAPI CaineGetStatus(void)`: 0 dormant/disabled, 1 starting,
2 observing the engine, 3 disabled by configuration, -1 failed.
`DWORD WINAPI CaineVersion(void)` returns 0x000312 (0.3.18).
The framework installs its guarded Mods menu on the supported client profile.
Feature initialization can fail after creating detours; those trampolines are retained
until process exit. Log writes are synchronized across control and callback threads.
Public plugins use [mod API v1](MOD_API.md), not the core C++ implementation ABI.

The module observer runs on a worker thread. **It is not a game-thread dispatcher.**
Do not call engine entities, UI, audio, the embedded Python interpreter, or simulation
code from it. The first feature requiring game state must establish and verify an
appropriate game-thread entry point and register lifecycle callbacks there.

## Hooking

Use one `caine::Hooks` owner, on a serialized control path. A reviewed `HookSpec`
requires an ID, module SHA-256, RVA, and at least eight exact expected entry bytes.
The module must be loaded as an x86 PE image. The target must be within committed,
readable executable memory. Unknown hashes, altered prologues, and duplicate targets
or IDs are rejected. A fresh disk hash is checked immediately before installation.
The original trampoline is published before enabling the hook.

`Module::ScanExecutable(ParsePattern("55 8B EC ?? ??"))` returns *all* matches in
readable executable sections. A feature must require exactly one match and verify
its surrounding code and function ABI before converting it into an approved RVA.
Never take the first match, treat a missing match as zero, or approve the current
hash/bytes dynamically in production. The test fixture does that only to test the API.

Document the original x86 convention (`cdecl`, `stdcall`, `thiscall`, etc.), argument
layout, object lifetime, and supported module hashes beside each hook. A generic
Source SDK header is not proof of Bloodlines' ABI. Reject unknown third-party patches
instead of overwriting them. MinHook handles instruction relocation and thread
suspension; its errors are surfaced, not ignored.

Hook removal requires all detours to be quiescent, including threads in trampolines.
`RemoveAll` is provided for controlled teardown/tests, not concurrent hot reload.
Do not destroy a hooks owner while detours can still execute. A batch validates every target before enabling any hook.
Failures during enabling retain allocated trampolines; never roll back live pointers
by destroying their owner. The runtime requires restart for every native mod change.

## Content and scripting

For example, a launch configuration can use `-game Unofficial_Patch`. Its loose-file
trees include `python`, `dlg`, `vdata`, `scripts`, `cfg`, `maps`, `materials`, `models`,
`sound`, `resource`, and `particles`. Stage original CAINE content under `content/`
using the same relative paths when a feature needs it. The native installer currently
deploys its eight owned payload files and ownership receipt; content deployment needs a separate,
manifested overlay with backup/restore before adding game-file overrides.

Bloodlines includes `vampire_python21.dll` and Python 2.1-era game scripts. Do not
replace that interpreter or assume Python 3 syntax/APIs. Avoid overriding large
Unofficial Patch scripts without merging the installed version. Save schema,
migration, and world lifecycle designs are feature work, not established SDK support.

## Checks before shipping a feature

1. Re-run the read-only inventory and compare module hashes with the reviewed profile.
2. Add the smallest feature with explicit availability/error states and a config switch.
3. Run appropriate native tests and package/install checks.
4. Verify startup through an ordinary launcher, check the CAINE log, and test the actual
   feature in-game using an appropriate test save or new game.
5. Check transitions/save-load/shutdown relevant to that feature and record what was
   actually tested. Maintain compatibility with existing loader fixes and RTX Remix.

Reference: [Microsoft DLL initialization guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)
and [MinHook source](https://github.com/TsudaKageyu/minhook/tree/v1.3.4).
