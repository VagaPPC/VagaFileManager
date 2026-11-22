# Changelog

User-visible changes are recorded here for every build.  The version in
`VagaFileManager_version.h` is the authoritative build number.

## 1.0 build 17 - 2026-11-22

### Changed
- Copy/move: added DOS error diagnostics to `T:VagaFileManager.log`.
- Directory copy accepts `IoErr` 0 after a successful directory scan in
  addition to `ERROR_NO_MORE_ENTRIES` (false-failure avoidance on some
  filesystems).

## 1.0 build 16 - 2026-11-22

### Changed
- Quick filter: 300 ms debounce.  The directory is no longer re-read on
  every keystroke; the list updates as soon as typing pauses.
- Find: patterns without wildcards use the fast substring search again;
  `dos.library` pattern matching is used only when wildcards are present.

## 1.0 build 15 - 2026-11-22

### Added
- Find accepts AmigaDOS wildcards (`#?`, `?`, `[]`, `~`) and `*`.
  Patterns without wildcards remain substring searches.
- The wildcard translation is shared with the quick filter
  (`VFMParsePattern`).

## 1.0 build 14 - 2026-11-22

### Added
- `Plugins/Find.py` rewritten from scratch for the file-based plugin
  channel: it asks VFM for the search pattern, searches recursively from
  the active pane path (`PATH`), shows the results in a Reaction
  listbrowser and reveals the selected file.
- `CHANGELOG.md`.

### Changed
- The Find/Grep results window now returns the selected row.  Find
  navigates the active pane to the result's drawer and selects the file
  (double-click or select + OK).

## 1.0 build 13 - 2026-11-22

### Added
- Quick filter is applied in real time while typing; Enter is no longer
  required.  The string gadget uses its work buffer and the filter is
  checked on every Intuition tick.

## 1.0 build 12 - 2026-11-22

### Fixed
- Plugin file channel: the synthetic `RexxMsg` now sets `RXFF_RESULT`, so
  `VERSION`, `PATH`, `ENTRIES`, `SELECTED` and other query commands return
  their text instead of an empty string.

## 1.0 build 11 - 2026-11-22

### Added
- File-based command channel for Python plugins: `req_`/`rep_` files plus
  `VagaFileManager --notify <drop-port>`.  It reuses the ARexx command
  dispatch through a synthetic `RexxMsg`, so no ARexx host lookup or
  `C:RX` is needed.
- Python plugins receive the drop port and channel directory as arguments.
- `ARexxDemo.py` uses the channel for commands and keeps `C:RX` only as a
  fallback for older VFM builds.

## 1.0 build 10 - 2026-11-22

### Fixed
- Root view: DELETE and Copy/Move no longer act on a whole volume or
  assign; the path gadget has its own buffer and `LoadPath` works on a
  copy.
- Cancelling a move no longer deletes the source; partial copies and read
  errors are reported as failures.
- Drag&drop rejects file names containing C0/C1 control characters, so a
  name containing Return cannot run commands in a Shell.
- Recursive copy/delete/count use heap buffers and a 256-level limit;
  copying a deep drawer tree no longer overflows the stack.  A failed copy
  does not delete the source during a move.

## 1.0 build 9 - 2026-11-22

### Changed
- Distribution drawer renamed from `VMF` to `VFM`; drawer icon is
  `VFM.info`.
- Added `README.Aminet` for Aminet and OS4Depot.
- WB2 version string uses the shared version date.

### Fixed
- The old `VMF`/`VFM` mixed naming in build and documentation.

## 1.0 build 8 - 2026-11-22

### Fixed
- Reverted the explicit `AddPort()` change that could freeze the system:
  `ASOPORT_Public` already adds a named port.  The ARexx port uses the
  original single creation path.
- Added `VagaFileManager --findport <name>` for diagnostics.

## 1.0 build 7 - 2026-11-21 (withdrawn)

- Experimental explicit `AddPort()` for the ARexx port.  It double-added
  the port and could freeze AmigaOS.  Use build 8 or newer.

## 1.0 build 6 - 2026-11-21

### Fixed
- `PromptDialog` keeps the caller's default text instead of clearing it
  (the `PATH` default now appears in the ARexx demo dialog).

## 1.0 build 5 - 2026-11-21

### Added
- `VagaFileManager --dialog "@spec-file"`: a standalone Reaction dialog
  host for addons, independent of ARexx.
- `RunPlugin` passes the VFM executable to Python plugins.

## 1.0 build 4 - 2026-11-21

### Added
- Diagnostic logging of the ARexx port `FindPort()` result.

## 1.0 build 3 - 2026-11-21

### Added
- The About window shows the live ARexx port name.

## 1.0 build 2 - 2026-11-21

### Added
- 1 MB `$STACK` cookie for deep directory operations.
- ARexx result diagnostics in `T:VagaFileManager.log`.

## 1.0 build 1 - 2026-11-20

### Added
- Build number in version strings, About, WB2 and the ARexx `VERSION`
  reply.

## Earlier development

- Initial two-pane Reaction file manager, icon/list views, split mode,
  drag&drop, copy/move/delete, progress, size calculation, bookmarks,
  quick filter, per-volume geometry, new-instance open, WB2 patch, ARexx
  port, Python/ARexx plugin example and GPLv3 licensing.

When adding a user-visible change, add it under the next build number
before updating `VFM_BUILD`.
