# CAINE updates

CAINE checks [GitHub releases](https://github.com/Teirdalin/PROJECT-CAINE/releases)
in a background worker at startup and every six hours. Its main and pause menus
show **Update Available** at the bottom of the navigation list when a newer
compatible release has a complete update asset with a GitHub SHA-256 digest.

Clicking it opens a confirmation; **Later** closes it without installation.
**Install and Restart** starts download progress. Finish saving first:
Bloodlines closes through its native quit action after the download is verified.
A separate **PROJECT CAINE — Update** window displays extraction, backup,
installation, verification and restart progress. It waits for the exact original
game process to exit; it never kills the game or hot-unloads plugins. Bloodlines
then restarts with its original command line and working directory, including
the selected game directory and launcher's options.

The updater changes only CAINE's eight declared payload files and ownership
receipt. It retains the existing CAINE.ini. Saves, video/keybind settings,
Unscripted, other mods and game binaries are outside the transaction. Previous
owned files are backed up under the installation's CAINE_Backups directory.
Modified or unowned files stop installation; copy failures roll back. A failure
after quitting leaves the game closed and displays its reason. Restart through
your usual launcher when ready. No user data is sent to GitHub.

In `Bin/loader/CAINE/CAINE.ini`:

```ini
[Updates]
Check=1
Preview=1
IntervalHours=6
```

Set Check=0 to disable checking, or Preview=0 for stable releases only. Missing
settings retain these defaults so an older configuration does not need replacing.
Network failures do not block startup. Failed downloads can be retried from the
progress dialog. Downloads stay under LOCALAPPDATA/PROJECT CAINE/Bloodlines/updates
for diagnosis. Unsupported native menu profiles retain the stock UI; use the
release installer for those builds.

HTTPS certificate validation stays enabled. Only this repository's release URLs
and GitHub's binary asset redirect hosts are allowed. Download size and SHA-256
must match GitHub metadata. The ZIP has an exact file allowlist and extraction
limits; traversal, duplicate entries, extra files, links and mismatched package
versions are rejected. Payload hashes and the existing ownership receipt are
checked again. The helper executes its installed, receipt-verified installer,
never downloaded scripts. GitHub account/release authority is the update trust
boundary; SHA-256 detects corruption, it is not an independent signing key.

## Releases

Build with `scripts/Build.ps1`. It runs native, renderer, deployment and updater
regressions, then produces the player ZIP and `PROJECT-CAINE-update.zip`. Publish
the latter with that exact asset name under `v<package version>`. Development
versions use `-framework-dev` and must be marked prerelease. GitHub supplies the
asset digest. Stable versions are plain major.minor.patch. The checker selects
the greatest newer compatible version among the latest twenty published releases.
Unscripted is released independently.

## Acceptance boundary

Protocol tests cover version ordering, release channels, URL/digest validation
and Windows restart argument quoting. Updater transaction tests use disposable
native-loader installations and a separate fixture game process, including
actual helper/PowerShell handoff and restart. Renderer tests exercise the progress
modal and device reset. These do not establish a completed in-game update or
live gameplay acceptance for every supported launcher.
# Progress file reliability (0.3.16)

The native helper reads metadata with Windows read/write/delete sharing. This
allows the installer to replace its complete progress JSON atomically while the
helper polls it; the previous CRT stream could deny replacement and abort an
otherwise valid update. Metadata remains bounded to 2 MiB and incomplete reads
are rejected. Progress polling tolerates a missing/invalid intermediate sample.
