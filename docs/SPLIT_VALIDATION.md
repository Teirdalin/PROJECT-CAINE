# PROJECT CAINE / Bloodlines: Unscripted split validation

October 8, 2026. Windows x86 native framework 0.3.0; optional Unscripted 0.2.0-dev.

- Framework-only CMake build succeeds with CAINE_BUILD_UNSCRIPTED=OFF.
- 12 native tests pass: four original startup/hook tests and eight mod API cases.
  The API tests load an independent DLL, install a real x86 detour through CAINE,
  call its configuration callback, persist restart-only toggles without unloading,
  and reject invalid XML, DTDs, traversal, future versions and incompatible ABIs.
- 61 Python tests pass after moving AI code into its own package. Three new tests
  cover legacy WAL/settings migration, destination preservation, test isolation,
  old save-sidecar names and corruption precedence.
- A packaged service cold-starts against a separate test data directory, extracts
  installed content read-only, and completes native snapshot transport. No shared
  credentials are loaded for that test.
- Framework install/upgrade/uninstall tests preserve configuration, unrelated
  plugins and saves; changed or unowned ASIs are rejected.
- Separate deployment tests preserve legacy KAIN configuration while retiring its
  owned ASI, install/upgrade Unscripted, retain CFG and user files, and reject a
  changed installed mod DLL.
- Both original logo files were copied unchanged into their respective source assets.
- Installed game module hashes, hook prologues and relocation profiles still match
  the recorded build. This is read-only verification, not gameplay acceptance.

CAINE owns the four existing native menu hooks. Unscripted registers configuration
pages via the public API, and owns four persistence/world hooks through CAINE's
guarded hook manager. The source has separate build targets and distribution payloads.
The versioned ABI is a development contract; general inter-mod dependencies and a
shared save-state/event bus are future work.

**Remaining acceptance:** native Mods menu layout/input, actual logos rendered in
game (renderer not implemented yet), enabled Unscripted gameplay, conversation UI,
live NPC speech, and real save/load flows. No claim of a finished AI overhaul.
The user's normal game installation and saves were not modified.

Historical validation of the earlier combined build remains in the previous docs
and archived outputs. Its menu/audio observations do not establish visual or
gameplay acceptance of this split.
