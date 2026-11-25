# Changelog

User-visible changes are recorded here for every build.  The version in
`VagaFileManager_version.h` is the authoritative build number.

## 1.1 build 90 - 2026-11-25

- LHA and GZIP plugins: the progress window no longer shows disabled
  buttons.  Its close button interrupts the compression after a
  confirmation; a message asks to wait while the files are removed and
  closes by itself.  A new archive is removed, an LHA archive that was
  being added to is put back as it was, and GZIP keeps the old archive.
- Find and Grep plugins: new "Include subfolders" checkbox (ticked by
  default); without it only the current drawer is searched.
- While a drawer keeps changing (for example during a long compression)
  VFM refreshes it at most every 2 seconds and stays responsive.

## 1.1 build 86 - 2026-11-25

- New View > Workbench emulation: hides all the bars, shows the icons,
  turns off the context menus (the right mouse button shows the menu
  bar) and opens drawers in new windows on double-click or Return, like
  the Workbench.  It is remembered; turning it off brings back the bars
  and the view you had before.
- When a file cannot be deleted (for example because it is open in an
  editor) a requester shows the reason given by the system, with Retry,
  Skip, Skip all and Cancel, instead of waiting.
- VFM, WB2VFM and the bundled plugins now write only errors to their log
  files in T:.  Set the DEBUG tooltype of the VagaFileManager (or WB2VFM)
  icon to TRUE to log everything; it ships as DEBUG=FALSE.

## 1.1 build 84 - 2026-11-25

- Version 1.1.  It collects all the changes listed below since the last
  release (1.0 build 62).  The build number keeps counting from 1.0.

## 1.0 build 83 - 2026-11-25

- New View > Bars submenu: show or hide the path bar, the button bar,
  the filter bar and the status bar.  Also in the context menu of empty
  space; the choice is remembered.  The View menu item for the icon
  size is now called "Size".
- New View > Split item (ticked in split view), so split view is
  available even with the button bar hidden.
- New button before the filter field: clears the filter and shows all
  files again.
- Greek translation.
- Program > Information now shows the VagaPPC logo; the mail address and
  the new "Donate" link to Ko-fi can be clicked.
- Keyboard shortcuts, shown next to the menu items: Amiga+C copy,
  Amiga+X cut, Amiga+V paste, Amiga+A select all, Amiga+D duplicate,
  Amiga+I file information, Ctrl+N new drawer, F2 rename, F5 refresh,
  Amiga+F filter field, Amiga+L path field.  Del, Esc and Shift+Return
  are now shown in the menus too.
- WB2VFM: a VFM window opened by double-clicking the desktop now finds
  the same Shell commands as the Workbench (for example make in a Shell
  opened from VFM), and it opens even if a RAM: drawer window is already
  open.

## 1.0 build 73 - 2026-11-25

- Sizes are now shown in a readable form, such as 512, 1,5K, 19M or
  190M (base 1024, decimal separator from your locale settings).  The
  list view, the progress window and Calculate size all use the same
  format; sorting by size still uses the exact byte count.
- WB2VagaFileManager is now called WB2VFM.  If you start it from
  WBStartup, replace the old program with the new one.
- WB2VFM: a double-click on the empty desktop background opens a VFM
  window on RAM:.  Double-clicking icons works as before.  Another
  drawer can be set with the DESKTOPPATH tooltype of the WB2VFM icon
  (for example DESKTOPPATH=Work:); if it is not available, RAM: opens.

## 1.0 build 70 - 2026-11-25

- The file lists now always show the final state of a drawer after a
  change made by another program (for example an archive just created):
  quick sequences of changes are no longer partly missed.
- Plugins always work on the pane they were started from, even if you
  click the other pane while they are running.  If that pane is closed
  meanwhile, the plugin is told so instead of acting on the wrong pane.
- Find (plugin) searches from the drawer open in the pane.  A single
  match is shown in VFM right away; with several matches, the one you
  pick is selected in VFM.  Searching for #?.info now finds icon files
  too, and VFM shows them even if .info files were hidden.
- The ARexx port now offers only what scripts cannot do by themselves:
  information about VFM and control of its window.  The commands RENAME,
  DELETE, DUPLICATE, COPY, CUT, PASTE, INFO, SIZE, FIND, GREP and
  SEARCHPATH have been removed; scripts handle files directly.  HELP
  lists the available commands.
- New ARexx commands: SELECT selects the files matching a name or
  pattern (for example #?.lha); SELECT ADD adds them to the current
  selection and SELECT NONE clears it.  STATUS shows a message in the
  status bar.
- SELECTED now returns a file listing the full path of every selected
  item, one per line: long selections are no longer cut and names with
  spaces are kept whole.
- New ARexx command PROTOCOL: returns the version of the command set
  (now 2), so scripts can check what this VFM supports.
- The ARexxDemo plugin has been removed.

## 1.0 build 61 - 2026-11-25

- All context menus now have "Show" (by icons / all files) and
  "Bookmarks", like the View and Bookmarks menus.
- Removed the small active-pane icon at the right of the path field; the
  active list is still outlined in split view.

## 1.0 build 60 - 2026-11-24

- New "Open with..." in the context menu of files: opens the selected
  files with a program of your choice.  The last five programs used are
  listed with their icons for a quick pick; "Choose..." picks a new one.

## 1.0 build 59 - 2026-11-24

- Calculate size now works on files too, and on a mix of files and
  drawers.  The Size button is active whenever something is selected.
- "Open" in the context menu opens drawers in a new window, like the
  Workbench; files are launched.
- The copy/move question after a drag & drop now shows what is being
  copied (up to two lines of names) and where to; long destinations are
  shortened at the start so the target drawer stays visible.

## 1.0 build 57 - 2026-11-24

### Changed
- The Edit menu's "Open" item is now called "Open in new window", since
  it opens the drawer in a new VFM instance; "Open" in the context menu
  behaves like a double click.  New catalog string
  `MSG_MENU_OPEN_NEWWIN` (run `make catalogs` on the Amiga).

## 1.0 build 56 - 2026-11-24

### Added
- Context menu on the file lists (right mouse button, both views, both
  panes), with the same icons as the Edit menu:
  - on a file or drawer: Open, Rename, Delete, Duplicate, Copy, Cut,
    Info, Calculate size;
  - on empty space: New > (Drawer, Text file), Paste, Shell, Refresh.
  Right-clicking an item that is not selected selects only that item; an
  item that is already selected keeps the current (multiple) selection.
  Empty space does not change the selection.  The pane clicked becomes
  the active one.  Elsewhere in the window the right button shows the
  normal menu bar.  "Open" behaves like a double click / Return.
  Uses Intuition's `WA_ContextMenuHook` (AmigaOS 4.1).

## 1.0 build 55 - 2026-11-24

### Added
- Keyboard shortcuts on the active list (ignored while the path or filter
  field is being edited, and on key repeat):
  - Return / keypad Enter opens the selection like a double click.  With
    several items selected it launches every selected file through the
    Workbench (drawers are skipped); above 10 files it asks for
    confirmation first.
  - Shift+Return opens the selected drawer in a new instance, like the
    Open button.
  - Backspace goes to the parent drawer.
  - Del deletes the selection, with the usual confirmation.
- New catalog string `MSG_OPEN_MANY_CONFIRM` (run `make catalogs` on the
  Amiga).

### Fixed
- German, French, Spanish and Polish catalogs: accented letters had been
  replaced by "?"-like garbage in build 52 (the `.ct` files were saved as
  UTF-8).  The original translations were restored from build 51, and the
  `.ct` files are again Latin-1 (Polish: Latin-2, codeset 5).
- Strings that had always been written without accents ("Groesse",
  "deja", "tamano", "Blad", "gia", ...) now use the proper letters in all
  catalogs, Italian included.

## 1.0 build 52 - 2026-11-23

### Changed
- In split view the active list is outlined with a frame in the fill
  color, the same color as the drag & drop target frame (list view).  The
  file browser class draws it itself (`FILEBROWSER_Highlight`), so it
  survives refreshes and window resizing.
- Every requester and dialog now opens centred under the mouse pointer
  (`REQPOS_CENTERMOUSE` for EasyRequests, `WPOS_CENTERMOUSE` for the
  ReAction windows).

### Fixed
- Copy errors are reported like the Workbench does: the name of the item
  and the system's own error text (for example "object is in use" when
  overwriting a busy file), with Retry, Skip and Cancel.  Skip keeps the
  source of a move; Cancel stops the operation without a further generic
  error message.  The same applies to a drawer that cannot be created.
- Cancelling a drag & drop copy no longer shows "Error while copying the
  dropped item".
- New catalog strings `MSG_COPY_ERROR_BODY` and `MSG_COPY_ERROR_BUTTONS`
  in all six languages.
- `WB2_BUILD` is now 52.

## 1.0 build 51 - 2026-11-23

### Fixed
- TAB in split view now switches between the two lists, and ESC clears
  the current selection.  The window asks for `IDCMP_VANILLAKEY`, so both
  keys arrive as characters (9 and 27), not as raw keys, and were ignored.
- `WB2_BUILD` is now 51.

## 1.0 build 50 - 2026-11-23

### Changed
- The list of AppWindows used for drops on other programs is now kept by
  VFM itself (`AppWinRegistry.c`), not by WB2VagaFileManager, which again
  only replaces Workbench drawer windows.  The first VFM started patches
  `AddAppWindow[A]`, `RemoveAppWindow`, `AddAppWindowDropZone[A]`,
  `RemoveAppWindowDropZone` and Intuition `CloseWindow`; other VFM
  windows use its list.  When that VFM quits it removes the patches, and
  another open VFM installs a new list at its next drag.
- Only AppWindows opened after VFM started are known: AmiDock, or an
  editor already open when VFM starts, does not receive drops.  On those
  windows the old behaviour stays (path typed into Shell windows and
  requesters, nothing on AmiDock or MultiEdit).
- `WB2_BUILD` is now 50 (no other change to WB2VagaFileManager).

## 1.0 build 49 - 2026-11-23

### Changed
- Dropping files on another program now works like the Workbench: VFM
  sends the window under the pointer the same AppMessage
  (`AMTYPE_APPWINDOW`, or `AMTYPE_APPWINDOWZONE` inside a drop zone) the
  Workbench would send, with the program's own ID and UserData.  Text
  editors such as MultiEdit open the file, AmiDock adds the object.  An
  AppWindow with drop zones only accepts drops inside a zone, and the zone
  is what gets outlined.  No ARexx command is involved.
- WB2VagaFileManager keeps a public list of AppWindows (semaphore
  `VagaFileManager.AppWindows`), because the Workbench does not tell other
  programs which port belongs to an AppWindow.  It patches
  `AddAppWindow[A]`, `RemoveAppWindow`, `AddAppWindowDropZone[A]`,
  `RemoveAppWindowDropZone` and Intuition `CloseWindow`.  AppWindows opened
  before WB2VagaFileManager started are not known: start it before AmiDock.
  Exchange "Disable" still turns off only the drawer replacement.
- Without WB2VagaFileManager, or on a window that is not a known
  AppWindow, the old behaviour stays: the path is typed into Shell windows
  and requesters, and dropping on AmiDock or MultiEdit does nothing.
- `WB2_BUILD` is now 49.

## 1.0 build 48 - 2026-11-23

### Fixed
- The system froze after dropping files on a text editor (MultiEdit) and
  then clicking anywhere: the mouse stopped moving.  A drop no longer
  sends any ARexx command to another program.

### Removed
- Dropping on MultiEdit (ARexx `ADDFILE`) and on AmiDock (ARexx
  `ADDOBJECT`): both windows are no longer drop targets and are not
  outlined while dragging.  Dropping on them does nothing.
- `WB2_BUILD` is now 48.

## 1.0 build 47 - 2026-11-23

### Changed
- Drag & drop rewritten to look and behave like the Workbench:
  - the icons of the dragged items (up to six, cascaded, with their
    names) follow the mouse pointer.  They are a shaped, borderless window
    that never takes input; with compositing it is semi-transparent.  If
    the icons cannot be shown, the standard drag & drop pointer is used.
  - the drop target under the pointer is outlined while dragging: the
    drawer icon or row in a VFM pane, the pane itself, another VFM window,
    a Workbench drawer window or drawer/disk icon, AmiDock, MultiEdit or a
    Shell window.
  - dropping where nothing can happen (the same drawer the items come
    from, a dragged drawer onto itself, the Workbench desktop, a window
    that accepts nothing) does nothing, without a message.
- The window under the pointer is found in the real front-to-back order of
  the screen's layers, so a window behind another one is no longer picked.
- Dropping on a drawer in a pane with an active filter now picks the
  drawer actually shown under the pointer.
- `WB2_BUILD` is now 47.

### Removed
- VFM no longer sends Workbench AppMessages to AmiDock's private, unnamed
  ports: a wrong port could receive a message it did not expect and hang
  the system.  AmiDock is reached only through its public ARexx
  `ADDOBJECT` command.

## 1.0 build 46 - 2026-11-23

### Fixed
- The system froze after dropping files on MultiEdit.  When an external
  ARexx host answers a command with an error, `rm_Result2` holds a
  numeric error code, not a result string; VFM read it as a string and
  freed it with `DeleteArgstring()`, corrupting memory.  The result
  string is now used and freed only when `rm_Result1` is 0.
- Sending an ARexx command to another program now finds the port and
  sends the message under `Forbid()`, so the port cannot disappear in
  between.
- `WB2_BUILD` is now 46.

## 1.0 build 45 - 2026-11-23

### Fixed
- Dropping files on a MultiEdit window now opens them in MultiEdit.
  VFM sends `ADDFILE` / `DISPLAYFILE` to MultiEdit's ARexx port
  (`MULTIEDIT.n`, or the custom `PORTNAME`) instead of typing the path
  as keystrokes, which MultiEdit ignored.
- `WB2_BUILD` is now 45.

## 1.0 build 44 - 2026-11-23

### Fixed
- Drag & drop between two VFM windows: the drop position was shifted by
  the window borders (about one title-bar height), so the item landed in
  the drawer one row above the pointer, or in the current path.  The
  screen-to-window conversion now subtracts only the window position,
  because gadget coordinates already include the borders.  The same fix
  applies to choosing the target pane in Split mode.
- Dropping from Workbench no longer blocks Workbench while the
  Copy / Move requester is open or during the transfer: the AppMessage is
  replied as soon as the dropped paths have been copied.
- `WB2_BUILD` is now 44.

## 1.0 build 43 - 2026-11-23

### Fixed
- Drag & drop onto a drawer under the mouse now works in **details/list
  view too**, not only in icon view.  The row is found from the
  listbrowser metrics (font height + 2, column-title bar + 2) and
  `LISTBROWSER_Top`.
- The details-view hit test ignores the column-title bar and the vertical
  scrollbar, so a drop on empty space falls back to the current path.
- `WB2_BUILD` is now 43.

## 1.0 build 42 - 2026-11-23

### Added
- Toolbar, menu and active-pane icons now try the AISS `tbimages:` assign
  first (24x24 PNGs).  If AISS or a specific icon is not available, VFM
  falls back to the local `PROGDIR:icons` copies.
- Menu icons are mapped to their AISS names (for example
  `file_selectall`, `file_unselectall`, `fx_invert`, `filesize`,
  `sortingandgrouping`, `window`, `filetext`).
- Special thanks: added **AlfredONE**.

### Fixed
- The `m_showinfo` menu icon was identical to the filter icon; it now uses
  the AISS information icon.  The `m_script` icon was identical to the
  shell icon; it now uses the AISS text-file icon.
- Avoid touching the `tbimages:` volume when AISS is not installed: the
  assign is checked through the DOS list first, so no "Please insert
  volume" requester appears.

### Notes
- The local `PROGDIR:icons` copies remain the fallback.
- Build 41 source changes (file drop into a drawer under the mouse, View >
  Size, Open moved to Edit) are included in this build.
- The built-in English special-thanks string is updated.  To update the
  localized `.catalog` files, run `make catalogs` on Amiga (CatComp is not
  available in the Linux cross-build environment).
- `WB2_BUILD` is now 42.

## 1.0 build 41 - 2026-11-23

### Added
- Drag & drop onto a drawer cell/icon inside a VFM pane now copies or
  moves the dropped items **into that drawer**, instead of always using
  the current pane path.  The destination drawer is found from the mouse
  position (FileBrowser `FILEBROWSER_GetIndexAt`).
- View menu: a **Size** submenu at the top with `Auto`, `32 x 32`,
  `48 x 48`, `64 x 64` and `96 x 96` fixed icon boxes.  The choice applies
  to the active pane and is saved per path in the preferences
  (`IconSize=`).

### Changed
- The **Open** item moved from the View menu to the Edit menu, immediately
  above **New**.
- `WB2_BUILD` is now 41.

## 1.0 build 40 - 2026-11-22

### Fixed
- `CompressLZX.py` failed even when LZX had created the archive.  The
  success check now accepts the normal "Operation successful" output and
  ignores harmless text such as "0 errors"; it only treats explicit
  failure messages as errors.
- The LZX command line is closer to the documented syntax:
  `LZX -m -X0 [-r] a archive files`; `-r` is used only when a drawer is
  selected, directory names get a trailing slash, and `-X0` prevents a
  second `.lzx` suffix.
- The source drawer is passed to `CD` without a trailing slash.
- LZX command lines are now written to `T:CompressLZX.log` for diagnosis.

### Changed
- The Copy / Move / Cancel requester is now a small ReAction window
  opened with `WPOS_CENTERMOUSE`, so it appears at the mouse pointer
  instead of at the centre of the screen.
- `WB2_BUILD` is now 40.

## 1.0 build 39 - 2026-11-22

### Added
- Drag & drop now asks **Copy / Move / Cancel** before transferring files.
  The question is shown when dropping on another VFM pane, another VFM
  window, or a Workbench drawer/disk icon.  Text drops on Shell/requester
  windows are unchanged.
- AISS menu icons:
  - Select all: `file_selectall`
  - Deselect all: `file_unselectall`
  - Invert selection: `fx_invert`
- `Plugins/CompressLZX.py`: creates an LZX archive with `C:LZX`, with the
  same save/destination/default-name/Add/Overwrite/Progress behaviour as
  `CompressLHA.py`.  Requires `C:LZX`.
- `Plugins/CompressGZIP.py`: packs the selection with `C:TAR`, compresses
  the TAR with `C:gzip` and stores it as `.tar.gz`.  Add mode decompresses
  the existing archive, appends the new entries and recompresses it.
  Requires `C:TAR` and `C:gzip`.
- `WB2_BUILD` is now 39.

### Changed
- The old drag rule "same device = move, other device = copy" is replaced
  by the explicit user choice.
- README documents the new menu icons and archive add-ons.

## 1.0 build 38 - 2026-11-22

### Fixed
- Cursor up/down in icon view now move by a whole row (using the number
  of columns reported by the FileBrowser), while left/right move by one
  entry.  In list view all cursor keys move by one entry.
- TAB now switches the active pane reliably in split view: a window IDCMP
  hook catches TAB even when window.class would consume it; the main loop
  performs the switch.
- `CompressLHA.py`: progress shows the total file count; default archive
  name from the first selected file/drawer; Add/Overwrite/Cancel prompt
  when the archive exists.

### Changed
- The active-pane marker is now the AISS `lightroom_front` icon at the
  right of the path field: lit for the active pane, dim
  (`lightroom_front_g`) for the other.
- `WB2_BUILD` is now 38.

## 1.0 build 37 - 2026-11-22

### Added
- Keyboard navigation in the panes:
  - cursor keys move the selection (if nothing is selected, the first
    entry is selected);
  - typing a letter selects the first entry starting with that letter;
  - ESC clears the selection;
  - TAB switches the active pane when the split view is open.
  Qualifier keys (Amiga/Alt) are ignored, so menu shortcuts still work.
- Active-pane marker: a bold `*` at the left of the path row of the pane
  that receives menus, buttons and add-on commands.  It is updated when
  clicking a pane, moving with TAB, opening/closing the split or dropping
  files.
- `CompressLHA.py`:
  - default archive name from the first selected file or drawer (for a
    file the extension is removed, then `.lha` is added);
  - if the archive already exists, asks **Add / Overwrite / Cancel**;
  - the progress window now shows the total file count while LHA works.
- `PROGRESS [NOCANCEL] [INDET] done total ...` adds an indeterminate
  progress mode with a total count for add-ons.
- `WB2_BUILD` is now 37.

## 1.0 build 36 - 2026-11-22

### Added
- New `PROGRESS` channel/ARexx command for add-ons:
  `PROGRESS [NOCANCEL] done total [bytesDone bytesTotal]` opens or updates
  the VFM progress window and returns `1` when Cancel was pressed;
  `PROGRESS CLOSE` closes it.  `NOCANCEL` opens a progress window without
  Cancel/Pause (used for external processes that cannot be interrupted).
- The native FIND and GREP commands now show the VFM progress window.
  FIND is indeterminate and can be cancelled; GREP updates per selected
  file and while scanning long lines.
- `Plugins/Grep.py` shows an indeterminate progress bar while scanning and
  stops when Cancel is pressed.
- `Plugins/CompressLHA.py` runs LHA asynchronously through a small script
  and animates a non-cancellable progress bar until LHA finishes.
- `WB2_BUILD` is now 36.

## 1.0 build 35 - 2026-11-22

### Added
- New `ADDONINFO` channel/ARexx command.  It returns the split state,
  active pane, both pane paths, the number of selected entries and the path
  of a temporary file containing the selected full paths (one per line).
  This lets add-ons handle file names with spaces and the split view
  reliably.
- `Plugins/CompressLHA.py`: creates an LHA archive from the selected
  files.  With no selection it asks to select files; in single-pane mode
  it uses an ASL save requester; in split view it asks only for the name
  and writes the archive into the other pane.  It uses the installed
  `C:LHA` executable and `LHA -m` (no overwrite queries).
- `WB2_BUILD` is now 35.

## 1.0 build 34 - 2026-11-22

### Fixed
- Find/Grep results window: a single click now only selects the row and
  keeps the window open.  Previously the window could close on the first
  release, before the selection was applied, so it revealed nothing.
  Use OK or a double-click to confirm the selected path.
- "Reveal" now highlights the found file reliably: the pane list is
  multi-select, where `LISTBROWSER_Selected` is not reliable.  The target
  node is selected directly, scrolled into view and refreshed.
- Reveal also clears an active quick filter (and, if needed, switches from
  "icons only" to all files) so the requested file is actually visible
  after the pane navigates to its drawer.
- `FIND`/`REVEAL` write the resolved parent, target name, active filter
  and result to `T:VagaFileManager.log` for diagnostics.
- `WB2_BUILD` is now 34.

## 1.0 build 33 - 2026-11-22

### Changed
- Code review and optimization pass.  No unused functions were found; dead
  code was removed instead (the quick-filter IDCMP hook and its
  `IDCMP_IDCMPUPDATE`/`gFilterPendingPane` plumbing, plus the write-only
  `gLastDragX`/`gLastDragY` globals).
- `ReadDirectory()` reuses a persistent scratch buffer instead of
  allocating and clearing about 500 KB on every directory read
  (navigation, refresh, quick filter).
- `CopySingleFile()` reuses a 64 KB copy buffer instead of allocating and
  freeing it for every file.
- The progress window redraws every eight items (and always on the last
  one) instead of once per file; Cancel/Pause polling is unchanged.
- `UpdateStatus()` no longer reads and validates the shared clipboard file
  on every status refresh.  The main loop still reloads it on window
  activation and about once per second, and Paste reloads it explicitly.
- The AmiDock port diagnostic logging copies port name/flags/signal while
  the Exec port list is still protected by `Forbid()`, avoiding a
  potential use-after-free after `Permit()`.
- `FreeDirectoryScratch()` and `FreeCopyBuffer()` release the two new
  buffers at shutdown.  `WB2_BUILD` is now 33.

## 1.0 build 32 - 2026-11-22

### Fixed
- AmiDock keeps its AppWindow port private: it is not in the public Exec
  port list.  The native AppMessage path is still used when the port is
  found in the window data or among the task's unnamed ports; otherwise
  VFM falls back to AmiDock's public `ADDOBJECT` ARexx command.
- The fallback is asynchronous: VFM does not wait for AmiDock's reply, so
  the drop cannot freeze the window.  The reply/result is written to
  `T:VagaFileManager.log`.

## 1.0 build 31 - 2026-11-22

### Fixed
- VFM-to-AmiDock drop now uses the native Workbench AppWindow protocol:
  VFM sends an `AMTYPE_APPWINDOW` `AppMessage` with `WBArg` locks/names
  to AmiDock's AppPort, exactly as Workbench does when an icon is dropped
  on the dock.  The ARexx `ADDOBJECT` path is no longer used.
- The AppPort is located among the private ports owned by the AmiDock
  task; the IDCMP port and the public `AMIDOCK` ARexx port are skipped.
- The AppMessage reply port is asynchronous, so VFM never waits inside
  the drop and cannot freeze if AmiDock does not answer.

## 1.0 build 30 - 2026-11-22

### Fixed
- Dropping an object on AmiDock no longer freezes VFM.  The `ADDOBJECT`
  command is now sent asynchronously: VFM does not wait for AmiDock's
  reply before returning to its event loop.  Replies are collected by the
  main loop and logged; if AmiDock does not answer, VFM remains usable.

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
