# Vendored dependency

MinHook 1.3.4: https://github.com/TsudaKageyu/minhook/tree/v1.3.4

- Tag commit: `c3fcafdc10146beb5919319d0683e44e3c30d537`
- Source ZIP SHA-256: `172708123daa0c98d20d3a980b16a50be14af243dc95dee6f79c24193ad010e4`
- Source URL: https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4
- License: `minhook/LICENSE.txt` (BSD notices, including HDE).
- Kept unmodified. Upstream CMake still reports its internal version as 1.3.3; the pinned source tag is 1.3.4.
- Compiled statically into KAIN, using the x86 instruction decoder. No network fetch at build time.

The game, Unofficial Patch, loader, and RTX Remix binaries are not redistributed here.

## nlohmann/json 3.11.3

- Source: https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp
- Header SHA-256: 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
- License: json/LICENSE.MIT.
- Unmodified single header, used for bounded local transport and embedded save envelopes.
