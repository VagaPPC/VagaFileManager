# Changelog

User-visible changes are recorded here for every build.  The version in
`VagaFileManager_version.h` is the authoritative build number.

## 1.0 build 29 - 2026-11-22

### Added
- Drag & drop from VFM to AmiDock: when the drop target is AmiDock, VFM
  sends its `ADDOBJECT` ARexx command directly to the `AMIDOCK` port
  instead of trying to type the path into the dock window.  Dragging a
  program, drawer or file from VFM now adds it to AmiDock like a
  Workbench icon drop.
- The direct ARexx message does not use RX or an ARexx host, so it also
  works on systems where the external `RX` command cannot resolve hosts.

## 1.0 build 28 - 2026-11-22

### Fixed
- WB2 commodity: the hidden Workbench drawer window is closed with
  `CloseWorkbenchObjectA(path)` after about one second.  This removes the
  Workbench error without a cross-task `CloseWindow` (which froze the
  system) and lets the same device be reopened after VFM is closed.
  `WB2_BUILD` 28.

## 1.0 build 27 - 2026-11-22

### Changed
- WB2 commodity: the helper window is a 1x1 off-screen backdrop
  (`WFLG_BACKDROP|WFLG_BORDERLESS`) and is never closed from the commodity
  task.  It should suppress the Workbench error without the freeze; the
  reopen behaviour is to be verified.  `WB2_BUILD` 27.

## 1.0 build 26 - 2026-11-22

### Changed
- WB2 commodity: removed the hidden-window workaround entirely.  The
  stable NULL return is back: VFM launches, the device can be reopened,
  and Workbench no longer freezes.  A transient "drawer cannot be opened"
  message may still appear.  `WB2_BUILD` 26 is logged at startup.

## 1.0 build 25 - 2026-11-22

### Fixed
- WB2 commodity: reverted the cross-task `CloseWindow` of the hidden
  window; it froze the Workbench.  The hidden window stays open, so the
  "This drawer cannot be opened" message is still suppressed, but the
  same device cannot yet be reopened after closing VFM.  `WB2_BUILD` 25.

## 1.0 build 24 - 2026-11-22

### Fixed
- WB2 commodity: the hidden Workbench window is closed about one second
  after the intercept.  Previously it stayed open, so Workbench thought
  the drawer was already open and a second click did not launch VFM.
- Commodity build number is now `WB2_BUILD` 24.

## 1.0 build 23 - 2026-11-22

### Fixed
- WB2 commodity: intercepting a drawer/device returned NULL to Workbench,
  which briefly displayed "This drawer cannot be opened".  It now returns
  a valid hidden 1x1 off-screen window and opens VFM normally.

### Added
- `WB2_BUILD` for the commodity, shown in `$VER` and in the Commodity
  Exchange title/description (currently build 23).

## 1.0 build 22 - 2026-11-22

### Added
- Clicking the empty window background clears the selection in both
  panes (same as `MENU_DESELECT_ALL` / `DeselectAllEntries`).

## 1.0 build 21 - 2026-11-22

### Added
- List view: clicking on empty space clears the selection
  (`LISTBROWSER_AllowNoSelection, TRUE`).  Icon view already cleared on an
  empty click.

## 1.0 build 20 - 2026-11-22

### Fixed
- Paste button: starts disabled, is visibly refreshed with
  `RefreshSetGadgetAttrs`, and is enabled only when the shared clipboard
  has at least one existing path.  The clipboard is revalidated once per
  second and `DoPaste` reloads it before use.

## 1.0 build 19 - 2026-11-22

### Fixed
- VFM-to-VFM drag&drop: `VFMPLUGIN_TYPE` was 0x5646, the same as
  `VFMDROP_TYPE`; every drop was handled as a plugin notification and
  never copied/moved.  The plugin type is now 0x5647.
- Paste button state follows the shared clipboard; entries whose paths no
  longer exist are discarded, so Paste is disabled after a move/delete
  that invalidated the clipboard.

## 1.0 build 18 - 2026-11-22

### Fixed
- Drag&drop between VFM instances: the `VFMDropMessage` is allocated in
  shared memory, so the receiving task can read it (it was
  `MEMF_PRIVATE`).
- Activating a VFM window reloads the shared clipboard and refreshes the
  toolbar, enabling Paste after a copy in another instance.

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
