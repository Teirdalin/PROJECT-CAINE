# First Person Melee

CAINE supplies **Settings > Gameplay > First Person Melee**, disabled by default.
Changes apply on the native camera thread and persist in the existing CAINE.ini:
`[Gameplay] FirstPersonMelee=0|1`, `MeleeBodyCamera=0|1` (default 1).
This is a framework feature; Bloodlines: Unscripted is not required.

When enabled, ordinary melee uses a first-person camera if the player's
camera preference is first person. The normal camera toggle can still choose
third person. The feature supports two presentations:

- **Body camera (experimental):** retain the native third-person character and
  melee animations. Refresh the native animation cache and attach the camera
  origin to `Bip01 Head`, using the model's bone-local `eyes` attachment offset.
  Both translations and rotations of the animated head move the camera position;
  mouse aim stays unchanged so animation does not steer the player's aim.
  Armor models without a named eyes attachment use the stock head-local eye
  offset (4.5, 2.2, 0). The head name is resolved from the current model every
  callback, so model changes and loads cannot retain another model's bone ID.
  Keep the character's camera-distance opacity at 1 only after a valid head view.
  Reduce that view's world near clipping plane from the stock 8 units to 1,
  matching the native viewmodel distance. An existing tighter plane is retained.
  The native builder restores its defaults each frame; far distance, other
  cameras and viewmodel projection are unchanged. This prevents nearby body
  geometry from being cut off by the wider stock near plane; it does not hide
  head/hair geometry intersecting the eye position.
  This offers visible native
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
The view builder at 0x191710 owns CViewSetup at +0x10. Its exact near-plane
initialization bytes at 0x19179f and view-base bytes at 0x1917e1 are verified
before installing camera hooks. World near/far are view +0x5c/+0x60; viewmodel
near/far are +0x64/+0x68. Only finite, positive, ordered world planes are changed.
The head camera independently verifies GetModelPtr (0x8f900), SetupBones
(0x919c0), and the installed player renderable's SetupBones entry. The installed
studio header version is 0x9e3, with length at 0x8c, 160-byte bones and 60-byte
attachments. Their tables, names, offsets, native matrices and final positions
are bounded and validated before changing the view. Missing or invalid data
returns to the native chase camera and opacity. A reentrancy guard prevents a
bone callback recursively rebuilding the camera.
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
hook conflict. Head-camera tests additionally cover animated translation and
rotation, model replacement, missing attachments, malformed headers, invalid
matrices and native bone-setup failure. They execute the installed GetModelPtr
helper, with a controlled SetupBones continuation that verifies its calling
convention; they do not execute the full live animation renderer. A read-only
live check confirmed the current player's version, head index and SetupBones
entry against that contract. All 59 installed player models have Bip01 Head;
some armor/beast models omit named eyes attachments.
Clipping checks execute the installed builder's near-plane writes with a
controlled continuation, then exercise the production camera detour. They
cover the 8-to-1 reduction, preserving tighter planes, invalid/unordered values,
native camera defaults, failed head setup, and a modified clipping contract
rejecting the entire melee hook batch before any camera code is changed.
The real Gameplay menu persistence and a D3D9 render preview
also have regression checks.

These checks do **not** establish live combat rendering or animation quality.
Test fists and several weapons, each clan/gender model, standing/crouching,
looking down, close walls, attack/block/combos, gun switching, feeding, stealth
kills, ladders, disciplines, death, dialogue, save/load and cinematic return.
The head camera still needs acceptance for facial/body clipping and animation
alignment on every model; per-view head hiding or separate arms assets remain
future development. It stays
opt-in pending that visual acceptance. The native first-person mode may retain
the game's short camera-distance transition; no global camera fade is patched.
