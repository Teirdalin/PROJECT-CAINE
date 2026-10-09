# Long strings and native saves

October 8, 2026. PROJECT CAINE 0.3.2 and Bloodlines: Unscripted 0.2.1-dev.

CAINE replaces the supported Bloodlines build's faulty Python save-line reader
with a dynamically growing reader. It protects all saved Python strings, including
older Unscripted snapshots, independently of which plugins are enabled. The
original game and Python DLLs remain unchanged on disk.

## Fault and crash evidence

The stock function at vampire.dll RVA `0x19bc80` initially allocates 128 bytes.
Its growth branch repeatedly allocates only 256 bytes and copies only the first
128 bytes. The write index keeps increasing. Long strings lose data and then
overwrite the heap.

The October 8 11:39:42 crash ran CAINE 0.3.0 and Unscripted 0.2.0-dev. Its raw stack
contains vampire.dll RVA `0x19bcdc`, the allocation in this broken growth branch,
and the native restore path. The preceding temporary map save contains a
422-byte snapshot line. This is concrete evidence of a save-reader defect that
fits the reported character-creation crash. It does not establish that every
possible character-creation crash has been eliminated.

The newer 0.3.1 modern menu renderer was installed after that crash.

## Behavior and limits

- The replacement returns length-aware Python strings, with no silent truncation.
  It preserves the game's first CR/LF terminator and subsequent newline skipping,
  respects the native block end, and seeks back to the next token after block reads.
- Reads use 4096-byte blocks. The serialized line limit is 256 MiB plus 1024 bytes,
  sufficient for worst-case escaped legacy strings under the existing 64 MiB
  snapshot limit. Invalid cursors, truncated blocks and oversized lines raise a
  Python I/O error; allocation failures retain Python's allocation error.
- New Unscripted snapshots use a Python list containing the marker
  `Unscripted.hex.v1` followed by lowercase hex strings. Each chunk represents
  48 original bytes, keeping protocol-0 lines below the faulty reader's 128-byte
  threshold. The JSON envelope, checksum, schema and save-specific state are
  unchanged. This is lossless for UTF-8, quotes, backslashes and control bytes.
- Unscripted still reads its old single-string format and the KAIN/CAINE legacy
  keys. Chunk decoding rejects unknown formats, wrong types, odd/invalid hex,
  invalid chunk sizes and snapshots above 64 MiB.
- An older Unscripted binary cannot understand the new list representation.
  Keep Unscripted 0.2.1-dev or later when loading its new snapshots. Old long-string
  saves require the CAINE fix to avoid the original reader defect.
- `CaineLongStringsReady` reports successful installation. Unscripted requires
  CAINE 0.3.2 and this readiness before enabling its persistence hooks. Unknown
  DLL hashes or conflicting hooks are refused; no offset guessing is used.

The exact game/Python hashes, guarded bytes, wrapper layout and reader vtable
are recorded in `config/native-save-reader-profile.json`.

## Verification

`tests/snapshot_native_tests.cpp` loads the installed Python 2.1 interpreter and
maps the actual game DLL in a separate process without invoking its DllMain or
starting the engine. Only the interpreter's engine-console redirection is
disabled in this test host, and its installed library path is supplied explicitly.
These test-only changes are never installed in CAINE or Bloodlines.

The test uses guarded allocator replacements in the isolated image to reproduce
the exact stock reader's data loss and overwrite without damaging the test heap.
It then installs the production CAINE reader hook and verifies:

- Boundary lengths around 128/256 bytes, 422 bytes, 4096 bytes, 1 MiB and 64 MiB.
- CR/LF, native cursor position, EOF, truncated blocks and Python error reporting.
- Actual installed cPickle restoration through the game's native file wrapper,
  for old single strings and new chunks, preserving an unrelated story variable.
- New chunks through the unpatched reader, with no allocation guard overwrite.
- Unicode, quotes, backslashes, newline/tab/NUL bytes and chunk boundaries.
- Malformed chunks and snapshots above the supported size are rejected.

Run `scripts/Build.ps1 -WithUnscripted -TestGameRoot <game directory>` to register
and run this regression alongside the framework tests. The existing protected
`build/game-acceptance` copy is selected automatically when present.

Live character creation, gameplay and save/load acceptance remain pending.
The native regression does not launch the game, load a player's save or confirm
the modern menu's visual/input behavior.
