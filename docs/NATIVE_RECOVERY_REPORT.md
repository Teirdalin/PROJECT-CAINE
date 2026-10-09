# Native recovery integration report — October 8, 2026

CAINE 0.3.8 and Bloodlines: Unscripted 0.2.4 are development builds. This work adds
independently authored bindings and serializers; it does not claim recovery of the
entire proprietary game SDK or completion of live gameplay acceptance.

## Inspected sources and provenance

The actual protected game copy was inspected: Vampire.exe, Loader.dll, engine.dll,
vampire.dll, client.dll and vampire_python21.dll. Hashes match the installation
profile in `INSTALLED_BUILD.md`. Their PE headers contain no managed CLR directory;
native disassembly and metadata inspection were used rather than ILSpy. Existing
installed Python scripts, dialogue extraction, VPK precedence, vdata resources, BSP
entities, menu/configuration interfaces, save hooks and Python conversions were
audited. The local extraction contains 147 dialogue files and 19,483 dialogue rows.

The SDK author's v2.20 archive was downloaded and checked against its published MD5
`b5335959630ab5f817d7ab6d6540c76b`. Its dialogue-tool documentation/entity definitions
were used only for orientation. It is a content-authoring toolkit, not the missing
original gameplay C++ source headers. **No unofficial SDK code was copied into any
framework or mod source, package or generated binding.** The reference archive is
isolated in ignored development output. CPython's public 2.1 headers were consulted
for Python object and method metadata ABI facts.

`tools/recover_native_metadata.py` independently scans the installed PE files. It
found 56 candidate game PyMethodDef records and 469 in the Python DLL; these are
research candidates, not automatic bindings. It also records observed interface
names and CLR absence. `reports/recovered-native-metadata.json` stays local.

## Recovered and integrated interfaces

- Exact, relocated server dialogue capture/release and client HUD paint/activation/
  choice hooks. Verified x86 thiscall/fastcall adapters preserve stack cleanup.
- Copied opening/source/row/available responses with bounded CP1252-to-UTF-8 conversion.
  The native game remains responsible for filtering choices, scripts, costs, quest
  changes and dialogue transitions.
- Entity table index/serial validation, live packet capabilities, exclusive ownership,
  deferred choice execution and invalidation on closure/release/new packets.
- Shared read-only Python globals, player existence, quest and inventory adapters.
  The installed method is **GetQuestState**, not the previously used GetQuest.
- Selected player properties and identity/money/masquerade/sex readers through
  verified scripting names; no guessed native player struct or stat offsets.
- Central PythonObject/PythonSaveFile/entity-handle/partial dialogue definitions,
  compile-time x86 size/offset checks and shared Python reference release.
- Existing engine settings, keybindings, video modes, intro skip, crash reporting and
  guarded save-reader APIs remain intact.

Native datamaps, networking field descriptions, complete character/NPC inheritance,
inventory/equipment containers and generic native event dispatch were not sufficiently
verified to expose them. The game's native serialized save graph has not been replaced.

## Serialization and consumers

CAINE now enumerates owned and live types through `typeInfo` and validates/canonicalizes
versioned owned JSON through `serializeOwned`. Supported records cover typed scalars,
enums, arrays/objects, stable caller-supplied entity identities and dialogue source/row
contexts. Unknown fields survive within supported versions; duplicate keys, incompatible
types/versions, ephemeral handles/tokens, invalid UTF-8, overflow and excessive size/depth
are rejected. Native memory images/padding/pointers are never the save representation.

The Python blob codec moved from Unscripted into `caine_values`. Unscripted keeps its
old chunk marker and legacy snapshot decoding. Its embedded native save key and known
restore calling convention are unchanged. Long strings retain the existing limits:
64 MiB owned snapshots and 256 MiB bounded native pickle lines.

Unscripted uses the shared dialogue and scalar APIs. Its free-text UI sends asynchronous
service requests and preserves original responses as native choices. Provider proposals
are one-use capabilities; the presentation thread revalidates the current packet before
execution. Late replies from a changed save/conversation are discarded. The native NPC
speech/lipsync route remains incomplete; generated replies currently use the new text UI.

CAINE's pause menu includes Quit to Desktop and owns its exit confirmation. Pending
disconnect/quit/resume actions are no longer mistaken for native child dialogs; verified
alpha fields are completed immediately instead of yielding during the old fade.
Long control-button labels wrap in the shared renderer.

## Verification and remaining acceptance

**Final results: 33/33 native CTest cases and 67/67 Python service tests passed.**
The rebuilt packaged service passed isolated startup/DPAPI/HTTP snapshot/restore
checks. Framework and split-mod install/upgrade regressions passed. The protected
copy now has CAINE 0.3.8-framework-dev and Unscripted 0.2.4-dev; ownership receipts
and installed hashes match the build. Twenty-eight protected files (current saves,
settings, native game binaries/media and unowned files) remain byte-identical.

The verification logs under `reports` record the exact final counts. Native checks cover
owned-value round trips, type registry/C ABI capacity behavior, old descriptor compatibility,
installed-DLL dialogue detours and calling conventions, handle reuse, current choices,
closure/release, installed CPython scalars, exception/refcount/thread handling, native restore
ABI, large pickle strings, menu transitions, renderer device reset and existing regressions.
Service tests use the actual loopback HTTP routes with a deterministic provider to exercise
native-open/send/job/claim, unknown actions, single-use capabilities, save reload, map change
and replies arriving after a load. These checks do not make external Gemini calls.

An isolated rendered dialogue preview exercises wrapping, free-text submission and D3D
device reset. Package/upgrade checks verify owned-file transactions and preserved settings.
The protected test installation is updated only while its game is stopped, with backups
and before/after hashes. The ordinary F:\Games installation is outside this deployment.

Real character creation, an NPC conversation through Gemini, pause/resume art transitions,
native quest progression after an AI proposal, full player save/load and map travel still
need live acceptance. The most recent earlier runtime log reported a startup-video hook
byte conflict despite a matching disk hash; unknown runtime patches are not overwritten.
The existing character-creation crash cannot be declared resolved by these fixtures.

## Next recovery priorities

Verify live dialogue capture and thread/window identity against a real NPC first. Then
recover stable map/entity resolution and native events needed for witnessed knowledge,
followed by fuller character/inventory readers. Verify datamap/save-field metadata before
adding generic native serialization. Recover the actual NPC voice/scene/LIP path before
claiming voiced AI conversations. Add separate exact profiles for other game/patch builds.
See `NATIVE_INTEROP.md` for the extension procedure and ownership rules.
