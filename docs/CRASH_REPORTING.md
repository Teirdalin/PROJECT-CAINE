# CAINE crash reporting (0.3.4)

CAINE starts a hidden x86 `CrashReporter.exe` alongside Bloodlines during normal
startup. The helper exits when that game process exits. No launcher, console,
administrator access, upload, or registry change is required.

Reports are written to `%LOCALAPPDATA%\PROJECT CAINE\Bloodlines\crashes`.
If `CAINE_LOG_DIR` is set, they are written to its `crashes` subdirectory instead.
Each report has a UTC timestamp, game PID and sequence number, with matching
`.txt` and `.dmp` files. Send both files and the matching `CAINE-<pid>.log` when
investigating a crash. Dumps contain process memory and may include private
data; they remain local until you choose to share them.

Since 0.3.13, `logs/CAINE.log` is a fresh current-run log on every normal startup.
The PID log is also truncated at startup, including when a PID is reused. Detailed
event traces are enabled by default; see [Debug logging](DEBUG_LOGGING.md). The
observer's exception path remains allocation-free and does not acquire the log lock.

The text report records the exception code, access kind and target, original x86
registers, faulting thread, module-relative fault address, best-effort stack
frames, actual loaded-module bases and paths, bounded raw stack bytes, and the
last 64 CAINE log events with thread IDs and uptime. The helper appends the
observed process exit code after shutdown. CAINE's logs include menu navigation
and native action entry/return without logging settings input values. Matching
framework debug symbols are provided in the release ZIP's `symbols` directory.

Bloodlines catches crashes itself. CAINE therefore observes severe **first-chance
exceptions** before native stack unwinding and always continues native exception
handling. A report is explicitly labeled `FIRST_CHANCE_CANDIDATE`: a game can
handle an exception and continue, so a report alone does not prove a fatal crash
or identify the component responsible. Ordinary C++ exceptions and debugger
notifications are ignored. No game exception filter is replaced.

The fault observer copies context into preallocated shared memory, signals the
external helper and waits at most eight seconds. It does not allocate, acquire
CAINE's log lock, walk stacks, or run DbgHelp in the game. The helper loads the
system DbgHelp using its absolute System32 path; Bloodlines' obsolete local
`dbghelp.dll` is left alone. Dump exceptions describe the faulting game thread,
not the helper's thread. Handle inheritance is restricted to the four diagnostic
IPC/process handles. No game command-line arguments or configuration contents
are passed to the helper.

Configuration in `Bin/loader/CAINE/CAINE.ini`:

```ini
[Crash]
Enabled=1
Dump=1
MaxReports=16
```

Missing settings use these defaults, preserving existing player configuration.
`Enabled=0` disables the observer and helper; `Dump=0` retains text reports only.
`MaxReports` is bounded to 1-64 severe exceptions per game process. Concurrent
exceptions while reporting are skipped. A timed-out reporter is not reused,
preventing a later exception from overwriting its context. Missing/failed helper
startup is logged, and the game continues with its native crash handling.

This is best-effort diagnosis. Forced termination, some Windows fail-fast paths,
an exhausted stack, or a crash before CAINE initializes may bypass the observer.
Existing game logs and Windows crash dumps remain valuable. A corrupt heap may
have been damaged earlier than the recorded failure location.

Native regression fixtures produce an actual fatal access violation and a
handled access violation, validate original exception context in real minidumps,
check preservation of the existing game-style exception filter, and verify that
C++ errors and a missing helper do not change application behavior. These are
isolated processes, not proof that character creation in Bloodlines is fixed.

Design references: Microsoft's [MiniDumpWriteDump guidance](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump),
[vectored exception handling](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling),
and [exception pointer address spaces](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ns-minidumpapiset-minidump_exception_information).
