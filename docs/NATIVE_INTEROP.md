# Native interoperability and owned serialization — CAINE 0.3.12

CAINE's bindings were independently authored from the installed Bloodlines binaries,
their exported interfaces and installed scripts. No code, headers, reconstructed
implementations or tools from the unofficial Bloodlines SDK were copied into CAINE.
The SDK was inspected only as guidance for content formats and dialogue events.
Research downloads and extracted references stay in ignored `build/sdk-research`.
They are not runtime dependencies or release contents.

## Stable mod API

Use `CaineHostV1` in `include/caine/mod_api.h`. Check its byte size before using an
appended member. CAINE accepts the earlier mod descriptor size; mods requiring the
new API declare minimum framework 0.3.8. No C++ objects or allocation ownership cross
this Windows x86 C ABI. Optional `gameUI` callbacks use the existing renderer and
typed controls; keep presentation callbacks nonblocking.

`readDialogue` copies the currently visible native opening, source filename, original
row, NPC/player handles and up to four **already filtered** native responses. Game
CP1252 text becomes UTF-8 without truncation. `claimDialogue` grants one mod ownership
of that packet's presentation. `queueDialoguePick` queues a current response index;
the native client executes the original action on its own thread. Index -2 uses the
original end-conversation route. Generated text never becomes Python/script code.

A token belongs to one live packet. Every packet, closure or release invalidates old
tokens; serial-validated entity handles, server row/count, client active state and
response text are rechecked at execution. A recycled entity or wrong thread fails
closed. Unclaimed dialogue stays native. Native handle values are **not persistent
identities**. No native object pointer is returned to a mod by the dialogue API.

### Accepted NPC interaction (0.3.11)

The independently inspected `PlayerUse` boundary at game RVA `0x167aa6` runs after
native NPC selection and its use gate succeed. EDI identifies the base NPC entity;
ESI identifies the player component. CAINE observes only the pressed-use bit at
player component `+0x208c`, validates both entity serials, and gives native dialogue
750 milliseconds to take priority. An auto-ending ambient float does not consume
this pending interaction. Scripted uses and held-key repeats are excluded.

If no visible native dialogue takes ownership, `readDialogue` returns an ambient
context with `responseCount=0`, `line=-1`, a source filename, and an empty opening.
The ABI structure size is unchanged. Mods consuming this mode require 0.3.11 and
must not treat the source as permission to execute any dialogue row or quest action.
Only close (`-2`) is accepted, and it releases the overlay without calling the
native HUD handler. Ambient contexts expire if unclaimed; ownership, serial reuse,
player replacement and explicit load boundaries revoke them. NPCs with no verified
dialogue source currently retain their native interaction.

In 0.3.12, gameplay player lookup uses the serial-validated entity registry at
game RVA `0x566458` and the exact-profile player component vtable at `0x4a271c`.
The former helper at `0x1193b0` reads the transient console-command client index
at `0x70b25c`. Read-only inspection of the playing game found that index is -1
outside a command callback, so the helper returned no player and blocked ambient
conversation capture. The replacement caches a validated handle, rejects ambiguous
players and rescans after replacement. Diagnostic logs record accepted-use button
states, capture, rejection and ambient handoff without copying private dialogue.

### Player field of view (0.3.12)

Graphics includes a live 60–135 degree FOV slider. Each changed value saves the
preference in `Bin/loader/CAINE/graphics.ini` and submits `fov <integer>` through
the guarded `VEngineClient006` command interface on the game window's thread.
The installed command is not a ConVar: its callback at game RVA `0xd2d80` writes
an integer at player component `+0x1e78`. CAINE reads that independently verified
field to check application; it does not write the field or cinematic camera state.
After loads and restarts, a saved preference is resubmitted at most every 250 ms
until it matches the validated player. Discard affects staged settings only;
live FOV changes are already saved. An absent preference leaves the game's FOV
untouched. The updater preserves this user-created configuration.

Native tests execute the guarded installed interaction detour with its register
contract and exercise ambient closure, native dialogue priority, recycled handles,
load cancellation and the FOV command/configuration round trip. The player fixture
reproduces the actual playing entity layout with command-client index -1 and uses
the production registry resolver, without a stubbed player helper. These tests do
not establish full live gameplay or visual acceptance.

`readScriptScalar` is a read-only CPython 2.1 adapter on the game window's thread.
It preserves the caller's Python exception and correctly releases newly owned
references. Supported reads:

- `CAINE_SCRIPT_GLOBAL`: an exact global key, through the verified native dictionary.
- `CAINE_SCRIPT_QUEST`: `FindPlayer().GetQuestState(name)`, verified in installed
  scripts and the game's method table. `GetQuest` is not the installed binding.
- `CAINE_SCRIPT_HAS_ITEM`: `FindPlayer().HasItem(name)`.
- `CAINE_SCRIPT_PLAYER_PRESENT`: whether the native player exists.
- `CAINE_SCRIPT_PLAYER_PROPERTY`: `clan`, `humanity`, `health`, `base_bloodpool`.
- `CAINE_SCRIPT_PLAYER_INFO`: `name`, `money`, `masquerade`, `male`, using the verified
  `GetName`, `CurrentMoney`, `GetMasqueradeLevel`, `IsMale` methods.

The scalar carries null, int32, finite float64 or length-delimited UTF-8; `textBytes`
preserves embedded NULs. Return 1 means success, 0 means absent/unready/wrong thread,
-1 means capacity exceeded, and -2 means unsupported type/name. Oversize UTF-8 is
rejected, never clipped. PyLong, Unicode, arbitrary object graphs and arbitrary
expressions/methods are not implicitly converted or executed.

## Registry and serialization

Enumerate `typeInfo` from index zero until it returns zero. Flags distinguish
serializable owned types from live native views and partial layouts. Registry schema
version 1 includes `caine.scalar`, `caine.value`, `caine.entity`,
`caine.dialogue-context`, plus the live Python object/save-file, entity handle and
partial server/client dialogue views.

`serializeOwned` validates and canonicalizes a UTF-8 JSON envelope:

```json
{"type":"caine.scalar","version":1,"value":{"kind":"int32","value":7}}
```

Supported scalar kinds are null, int32, float64, utf8 and enum with explicit domain
and integer value. `caine.value` owns nested arrays/objects. `caine.entity` requires
a map and stable caller-supplied identity; it rejects pointer/address/handle/token
members. `caine.dialogue-context` stores source and row, never a live token or entity
handle. These records do not resolve a native object automatically after loading.

Unknown fields are retained within known version-1 records. Unknown types/versions,
duplicate members, invalid UTF-8, incompatible scalar values, non-finite numbers,
excessive nesting and records over 64 MiB are rejected. Ask for required capacity
with null output, then provide that many bytes; the count includes NUL. An undersized
buffer is untouched. This is a CAINE-owned format, **not** an engine memory dump or
a complete replacement for Bloodlines' native save serializer.

The shared `caine_values` implementation also owns the Python blob codec. Its
48-byte hex chunks are safe for the game's historical pickle line reader. Unscripted
retains its `Unscripted.hex.v1` marker and accepts legacy string snapshots, preserving
old saves. CAINE's reader fix, this codec and the mod's existing save/restore hooks
remain separate responsibilities. No raw native pointers are saved.

## Recovered native views and compatibility

The definitions in `native_types.hpp` intentionally contain only confirmed fields.
PythonObject is 8 bytes; PythonSaveFile is 16 bytes with reader at 8 and block end at
12. CPython tp_dealloc is at type offset 0x18. An entity handle is four bytes, with
13 index bits and a serial; the verified native table has 12-byte entries.

The server dialogue view has NPC handle at 0, player at 4, data at 8, current row at
0x2830 and visible count at 0x2834. The packet has 2048-byte opening/response slots,
responses beginning at 0x804. The client HUD uses active at 0x1b4, waiting at 0x1b3,
opening at 0xa28, count at 0x1228, responses at 0x122c. These are partial views, not
claims about complete class sizes or inheritance.

The profile in `docs/INSTALLED_BUILD.md` is the supported installed 1.2/UP build.
Game/client/Python hashes and relocated instruction guards are checked before
binding. Other vanilla/official/UP binaries require separate verification; a matching
version label alone is insufficient. Unknown profiles retain native functionality.
Documented engine interfaces are preferred where available; hardcoded layouts are
limited to exact profiles. Runtime patch conflicts are rejected, not overwritten.

## Extending recovery

### Dialogueless pedestrians (0.3.17)

An independently inspected direct-hit boundary at game RVA `0x167674` supplies
EDI as the trace entity and ESI as the local player's component, immediately
before the original NPC CanUse predicate. The native short-range forward trace
and excluded-entity check have already run. CAINE observes only a fresh use press,
the exact `npc_VPedestrian` class, a nonempty target name, living state/positive
health, no dialogue source, a present NPC adapter and serial-validated handles.
It preserves registers/flags and continues through the original predicate.
It never forces a native Use, feeding action, scripted dialogue or quest action.
The exact game hash and complete overwritten instruction bytes are mandatory.
The trace is optional: a conflicting live patch disables pedestrian observation
while the existing scripted/ambient dialogue bridge continues working.

Confirmed entity fields are the NPC adapter at `0x98`, classname at `0x11c`,
dialogue source at `0x128`, life state at `0x200`, health at `0x210`, and target
name at `0x26c`. The string/health/life offsets were checked against the installed
module's own save-field metadata and read functions. These are partial, profile-
specific views, not reconstructed full class headers.

The ambient packet uses `entity://npc_vpedestrian/<hex UTF-8 targetname>` instead
of fabricating a dialogue filename. Response count remains zero and line -1;
native dialogue retains its 750 ms priority window. Death, identity change,
serial reuse and save/load invalidate this context. A consumer must match the
target to exactly one authored entity or single-live-child spawner on the current
installed map before constructing a persistent personality. Unnamed, ambiguous,
multi-child and unmatched pedestrians are unsupported. Stable map/spawner identity
does not yet distinguish successive replacements from an infinitely respawning
maker; general spawn-instance persistence remains future work.

Tests execute the production detour in the mapped installed game binary, covering
the living pedestrian, dead/combatant/scripted rejection, fresh press, serial reuse
and closure. These checks do not establish live trace selection or NPC acceptance.

Start with `tools/inspect_game.py` and `tools/recover_native_metadata.py` against the
actual installation. The latter records PE/CLR status, observed interface names and
candidate Python method records. A string or candidate record does not prove a
callable signature. Verify registration, argument parsing, caller cleanup, ownership,
object lifetime, relocation and build hash before adding an API/profile. Add native
detour/layout and boundary tests, then document the difference between fixture/native
evidence and live gameplay. Never generate release bindings from speculative offsets.

Full entity datamaps/network property types, native character/inventory containers,
generic world events, full BSP/save object graphs, scene/VCD/LIP writers and complete
engine class hierarchies remain unverified. Existing local VPK, DLG, vdata and BSP
entity extraction is preserved; no unnecessary replacement format runtime was added.

References: [SDK author listing](https://www.moddb.com/mods/vtmb-unofficial-patch/downloads/bloodlines-sdk),
[CPython 2.1 object header](https://github.com/python/cpython/blob/v2.1.3/Include/object.h),
[CPython 2.1 method metadata](https://github.com/python/cpython/blob/v2.1.3/Include/methodobject.h).
