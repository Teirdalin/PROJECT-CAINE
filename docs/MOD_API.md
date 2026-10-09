# PROJECT CAINE mod API v1

Build Windows x86 with MSVC and -A Win32. Public interfaces are
include/caine/mod_api.h and the optional C++17 wrapper include/caine/mod.hpp.
The core implementation header is not a plugin ABI.

## SDK license

CAINE uses JDL-1 (`LicenseRef-JDL-1`). Its public headers, Hello CAINE example,
and API documentation code snippets have the additional permission in
[PLUGIN_API_PERMISSION.md](../PLUGIN_API_PERMISSION.md). You may incorporate
those portions into your plugins and development kits, subject to that notice.
Independently authored plugins may use your chosen license; retain the supplied
copyright, JDL-1 and permission notices for the SDK portions you incorporate.

Export undecorated CaineMod_Query(uint32_t) with C linkage. Its CaineModV1 descriptor
and strings remain valid until process exit. CAINE checks structure size, ABI,
minimum framework version, stable ID and version against XML before calling start.
Return null for unsupported ABI versions. DLL initialization happens before querying;
metadata is not a sandbox.

## Package layout

Create VTMB/mods/Example/Example.xml, Example.dll, Example.cfg, and optional logo.png.
The manifest filename matches its folder:

    <mod schemaVersion="1" id="example" name="Example" version="1.0.0"
         author="Your name" dll="Example.dll" config="Example.cfg"
         logo="logo.png" minimumFramework="0.3.0">
      <description>What this mod does.</description>
    </mod>

Only plain filenames inside the folder are accepted. Traversal, redirected files,
malformed XML, DTD declarations, unsupported schema versions, duplicate IDs and
future framework requirements are rejected. XML uses Windows XmlLite with DTDs
prohibited ([Microsoft reference](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms752908(v=vs.85))).
Logo is an optional PNG, rendered by the modern Mods browser. General mod-to-mod dependency resolution is not
implemented yet.

Mod/Enabled=0 in the CFG skips DLL loading while keeping metadata visible.
A missing Enabled key defaults to enabled. The menu changes only this field.
Pending changes require restart; loaded and next-launch state are shown separately.
The host provides absolute game, mod and configuration paths.

## Threads and lifetime

Start, tick, module inspection and hook installation run on the serialized CAINE
control worker, not the game thread. Tick must be bounded. Use asynchronous workers
for network/service work and verified game-thread detours for game APIs.
Logging is thread-safe.

Configure, canEnterGame and menuFrame run on the main-menu thread. Configure handles
OPEN, BUILD, ACTION, CHAR and WANTS_TEXT events. BUILD supplies rows via
addRow (128 in the modern browser, eight in the native fallback); UINT32_MAX is a noninteractive label. Returning zero closes the page
(WANTS_TEXT instead reports whether text capture is needed).
ACTION is deferred to paint so a clicked widget cannot delete itself.
No network requests or hook installation in menu callbacks.

canEnterGame optionally redirects stock New/Load/Reload selections to configuration.
Console loads and other entry routes still need mod-specific handling.

Module inspection pins and fingerprints each target. Hook batches require exact
SHA-256, executable RVA and at least eight reviewed bytes. The host owns trampolines
and rejects conflicting hooks. Failed mods remain loaded because detours/workers may
already exist. There is no hot unload or automatic gameplay rollback.

CAINE does not yet supply a generic save bus or universal entity/dialogue API.
Unscripted owns its save adapter. Extract shared lifecycle contracts as additional
consumers establish what is reusable.

## Verification boundary

Native tests exercise x86 loading, real hooks through the public ABI, restart toggle
semantics, XML rejection and configuration callbacks. These do not prove rendered
game integration or save/load behavior. A separate DirectX 9 renderer test verifies
PNG output, state restoration, reset/re-render and keyboard action dispatch.
See MENU_RENDERER.md for the live acceptance boundary.


## Long-string save compatibility (0.3.2)

CAINE installs the supported game’s guarded Python save-line reader independently
of enabled plugins. The optional undecorated `CaineLongStringsReady` export has
signature `BOOL WINAPI(void)` and reports whether this fix is installed. A mod
that depends on large serialized values should require framework 0.3.2 and check
this readiness before installing its persistence hooks. The mod ABI remains v1.
See [LONG_STRINGS.md](LONG_STRINGS.md) for limits and profile details.


## Rich settings controls (0.3.3)

ABI v1 is retained. `CaineMenuV1` appends optional `addControl` and `value`
fields. Check `menu->size >= sizeof(CaineMenuV1)` and `menu->addControl` before
using them. A mod requiring these controls should specify minimumFramework
0.3.3 and minimum framework version `0x000303u` in its DLL descriptor.
Older mods can keep using addRow unchanged. A mod offering native fallback can
continue its existing rows/CHAR editor when addControl is null.

BUILD may submit up to 4096 `CaineControlV1` objects. CAINE copies all strings
before the callback returns. Labels and hints wrap; input fields scroll horizontally.
Kinds are TEXT, HEADING, BUTTON, TOGGLE, SLIDER, INPUT, CHOICE and TAB.
Flags are SELECTED, DISABLED, SECRET, LIVE and INTEGER. IDs must remain stable
for each page; do not reuse one ID for a different control while it is active.

Input capacity is a UTF-8 byte limit, excluding NUL, up to 65536; zero selects
4096. Unicode entry and Windows clipboard paste are supported. The installed
font determines which glyphs can be displayed. The framework rejects oversized
initial values instead of clipping them. A server accepting 2048 Unicode
characters should allow sufficient UTF-8 bytes and validate character count
when applying the value. Masked fields clear after submission; credentials must
never be supplied as labels, hints, logging or status messages.

VALUE events carry the control ID in the callback's `value` argument.
`menu->value` is a transient `CaineValueV1`: text for inputs, number for toggles
and sliders. Copy what your asynchronous worker needs during the callback.
Buttons, choices and tabs also emit VALUE with their ID. Input commits on
Enter/Apply, or each edit when LIVE is set. Slider changes commit on release,
or on each changed value when LIVE is set (supported since 0.3.12).
The settings provider validates and persists changes; rendering does not do it.

## Shared native API (0.3.8)

See [Native interoperability](NATIVE_INTEROP.md) for copied dialogue packets,
read-only Python/player adapters, game UI callbacks, type enumeration and owned
serialization. Earlier descriptor sizes remain supported; new members require 0.3.8.

Since 0.3.11, the shared dialogue API also exposes accepted ambient NPC interactions
as zero responses and line -1. An opening may be empty. Such contexts allow free-text
presentation and closure only; source identity never grants native quest actions.
Consumers supporting this mode declare minimumFramework 0.3.11. See the native
interoperability document for ownership and lifetime details.
