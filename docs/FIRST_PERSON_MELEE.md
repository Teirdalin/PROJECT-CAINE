# First Person Melee

CAINE supplies **Settings > Gameplay > First Person Melee**, disabled by default.
Changes apply on the native camera thread and persist in the existing CAINE.ini:
`[Gameplay] FirstPersonMelee=0|1`, `MeleeBodyCamera=0|1` (default 1).
This is a framework feature; Bloodlines: Unscripted is not required.

When enabled, ordinary melee keeps the native eye position if the player's
camera preference is first person. The normal camera toggle can still choose
third person. The feature supports two presentations:

- **Body camera (experimental):** retain the native third-person character and
  melee animations, but omit the chase-camera position and angle adjustment.
  Keep the character's camera-distance opacity at 1. This offers visible native
  animations without manufacturing a set of first-person animation assets.
- **Native first person (custom viewmodels):** suppress the ordinary melee
  forced-third-person flag. The stock installed melee definitions use
  `models/weapons/w_null.mdl`; their commented first-person model paths have no
  corresponding models in the inspected installation. Stock attacks therefore
  have no visible hands in this mode. Custom viewmodels are required for an
  arms presentation; this release does not add or redistribute such models.

## Independent native reconstruction

The installed client SHA-256 is
`9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01`.
Five independently inspected boundaries are hooked as one guarded transaction:
weapon camera selection (RVA 0x9c250), CInput camera thinking (0xff130), camera
view adjustment (0xffb00), camera-distance model opacity (0xffaf0), and the
camera-toggle holster tail (0xff87f). The last boundary preserves the native
toggle's selected preference and avoids its subsequent inven_holster command
only for eligible opt-in melee. Its registers, flags and native epilogue remain
intact; disabling the feature restores that original branch.
The native CInput object/vtable and active-weapon/data helpers are also verified.
An unknown client or altered target disables this feature without replacing
another mod's hook or disabling CAINE's other systems.

Native weapon camera parsing maps both `melee` and `force_3rd` to class 16.
The feature additionally requires the bounded `weapon_melee` item_type token;
forced-third-person firearms and other non-melee items are excluded. A custom
melee weapon that explicitly declares force_3rd is included when the user opts
in. Weapon definitions, camera preference cvars, combat logic and save records
are not modified. No per-frame console command or persistent entity pointer is
used. Re-equip, load and teardown resolve the current weapon through the native
handle-aware getter on each eligible callback.

The verified local player life state, forced-camera type, cinematic camera
override index, native dialogue index, special character-camera flags, ladder
movement query, and special camera blends gate the feature. Configuration
changes during these states are deferred until ordinary play resumes. Native
camera callbacks remain authoritative outside eligible melee. Disabling the
feature re-evaluates the current weapon's original camera route on that thread.
The body mode only activates while the native forced-third-person flag is set.

Community camera mods demonstrate weapon-data camera selection, and also
document viewmodel and ladder problems when weapon files are changed wholesale:
[Always 3rd person view author notes](https://www.moddb.com/games/vampire-the-masquerade-bloodlines/downloads/always-3rd-person-view).
These notes informed the investigation; no mod or unofficial SDK code was used.

## Validation and remaining acceptance

Mapped installed-client tests execute the actual camera hook entries, native
weapon-data lookup, weapon camera selection and forced flag writer. Isolated
view/opacity/think continuations allow their state and calling convention to be
checked without launching gameplay. Tests cover off/on, both modes, restoring
the original route, death/dialogue/ladder/cinematic guards, explicit third person, ranged
weapons, missing entities, configuration reload, thread exclusion and a camera
hook conflict. The real Gameplay menu persistence and a D3D9 render preview
also have regression checks.

These checks do **not** establish live combat rendering or animation quality.
Test fists and several weapons, each clan/gender model, standing/crouching,
looking down, close walls, attack/block/combos, gun switching, feeding, stealth
kills, ladders, disciplines, death, dialogue, save/load and cinematic return.
The body camera may show head/body clipping and animation alignment problems;
head-bone clipping or separate arms assets remain future development. It stays
opt-in pending that visual acceptance. The native first-person mode may retain
the game's short camera-distance transition; no global camera fade is patched.
