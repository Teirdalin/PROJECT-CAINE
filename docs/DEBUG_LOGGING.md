# CAINE debugging log (0.3.13)

Normal startup creates a fresh `%LOCALAPPDATA%\PROJECT CAINE\Bloodlines\logs\CAINE.log`.
Its previous contents are cleared before any runtime features or mods start. The
matching `CAINE-<pid>.log` is also created fresh, including when Windows reuses a PID.
Older PID logs and crash reports remain available for earlier crash investigations.
`CAINE_LOG_DIR` overrides this directory for isolated tests or custom diagnostics.

Each event has a UTC timestamp with milliseconds, increasing sequence number,
uptime, process ID, thread ID and EVENT/TRACE label. Writes are serialized across
threads and sent directly to Windows, without a user-space buffering delay.
Embedded line breaks are escaped so one event stays on one line. Third-party
messages are bounded to 64 KiB without splitting a UTF-8 character.

Verbose tracing is enabled by default, including for existing configurations.
Coverage includes:

- Module identities, hook validation, installation, removal and rejection reasons.
- Mod discovery, library loading, callback startup, enable changes and failures.
- Runtime/render heartbeats every five seconds, with poll/frame counts and state.
- Window attachment, focus and size changes; menu navigation and control events.
- Gameplay player presence, handle replacement, input-button state and FOV changes.
- Accepted NPC use, rejected use, ambient handoff, native dialogue packets,
  presentation ownership, original response selection, closure and invalidation.
- Native engine command submission, FOV persistence/reapplication, observed
  level/pause/console transitions through the existing guarded intro observer.
- Native Python save-reader activity and failures, mod-provided save/load events,
  update progress and existing crash breadcrumbs/reports.

This records CAINE's observed events. It cannot expose every operation inside the
closed-source game or a third-party DLL; unsupported profiles reject their hooks.
Frame polling and unchanged state are summarized rather than repeated each frame.
Map/pause observations require the existing intro observer to be enabled and ready.

Input text, credentials, configuration contents and conversation text are not
included in framework event metadata. Input events contain control IDs and byte
counts; commands contain their verb and argument length. Individual mods control
their own supplied log messages and must avoid logging private data.

Use `CAINE.log` for the latest run. For a crash, include the matching PID log and
the `.txt`/`.dmp` files described in [Crash reporting](CRASH_REPORTING.md). Logging
failure never throws into a game hook; Windows debugger output still reports it.
If another running instance or an editor holds exclusive ownership of `CAINE.log`,
CAINE retains the fresh PID log and records that fallback instead of truncating
the other instance's active file. No log files are written under DLL loader lock.

To reduce diagnostic detail while keeping significant events, set:

```ini
[Logging]
Verbose=0
```

Restart after changing this setting. Missing settings use `Verbose=1`.

Regression checks cover startup clearing in the actual ASI bootstrap, fresh logs
with a reused PID, concurrent writers, UTF-8 boundaries, sequence/context fields,
crash breadcrumbs, verbosity, locked current files and an unwritable log directory.
These checks do not establish full live gameplay acceptance.
