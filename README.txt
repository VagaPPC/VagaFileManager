VagaFileManager (AmigaOS 4.1, Reaction)

Version 1.0 build 44 (23.11.2026)

A modernization project for AmigaOS 4.1.

About the Project
The old OS3 file management system was functional back in the floppy disk era, and despite the improvements introduced over time, as the number of files grew it became increasingly difficult to launch applications and manage files efficiently.
It is no coincidence that many file managers (and even full Workbench replacements and upgrades like DOpus Magellan and Scalos) were created on Amiga to allow dynamic filesystem navigation alongside icon views.
VFM is my personal attempt to fill this gap and put an end to having dozens of open windows where you constantly have to arrange icons, snapshot positions, resize windows, and so on. MorphOS found an excellent solution with Ambient, whereas OS4 (suffering from a state of near-abandonment by its parent company) has been left behind.
To keep the core program lightweight and fast, I am introducing a plugin system to add external features without bloating the main application. Plugins can be written in ARexx and Python.
Note: I am not a hardcore developer, and I made use of LLMs during the development of this project.

Features
- Directory Navigation: Fast and dynamic browsing across your filesystem.
- Context-Sensitive Buttons: Dynamic file management buttons tailored to your current selection.
- "Split" Mode: Dual-pane workflow inspired by DOpus.
- Update Copy Mode: Copy only newer or modified files (currently under testing).
- Workbench Replacement: Commodity patch to seamlessly intercept and replace standard Workbench windows.
- Scripting & Extensibility:
- Built-in ARexx port.
- Python plugin support (Work In Progress).
- Drag & Drop, Workbench style: the icons of the dragged items follow the
  mouse pointer and the drop target under the pointer (drawer icon or row,
  pane, Workbench drawer or icon, another program's window) is outlined.
  Dropping files on another VFM pane, another VFM window or a Workbench
  drawer asks Copy / Move / Cancel at the mouse pointer.  Dropping where
  nothing can happen (for example in the same drawer) does nothing.
  Dropping on another program works like the Workbench: the window under
  the pointer (text editor, AmiDock, Shell...) receives the same AppMessage
  the Workbench would send.  VFM can only do this for windows opened
  after VFM started (the Workbench does not tell other programs where an
  AppWindow's messages go), so AmiDock, usually started earlier, is not
  reached.  On other windows the path is typed into Shell windows and file
  requesters.
- Automatic Window Updates: Real-time refreshes when directory contents change.
- Smart List View: Automatically displays files even if they do not have an associated .info icon file.
- Customizable Views: Tailor your layout with automatic memory for window sizes and view preferences.
- Per-Volume Window Geometry: window size and position are remembered separately for each volume a window is opened on; navigating does not change them.
- Open in a New Window: the Open button/menu opens a selected drawer (or volume) in a new VFM instance, centered on the mouse with a small offset; double-click navigates in the current window.
- Size Calculation: recursive drawer/volume size with an animated activity bar (single pass, cancel and pause supported).
- Favorite Paths: Bookmark and quickly jump to your most-used directories.
- Queue Control: Cancel or pause ongoing file operations and resume them later.
- Quick Filter: real-time filtering while typing (300 ms debounce), with
  wildcard and regex support.
- Find: recursive file search with a results window; selecting a result
  reveals the file in the active pane.  Supports AmigaDOS wildcards
  (#?, ?, [], ~) and *; without wildcards the name is a substring match.
- Performance: Faster and more responsive than the standard Workbench.
- Actively Maintained: Supported directly by the author.

Roadmap
Remote & Archive Support: I would like VFM to eventually handle protocols like FTP/SFTP/SSH as well as archives such as LHA, LZX, ZIP, etc.
Architectural Considerations: Beyond the implementation complexity, building all of this directly into the executable would make the program heavier and somewhat redundant, since AmigaOS can already handle these formats through dedicated filesystem handlers. I am currently evaluating the cleanest way to integrate this functionality.
Current Focus: For now, development priority is focused on finalizing and expanding the add-on/plugin system.
Feedback & Contributions
Any suggestions, bug reports, ideas, and constructive criticism are very welcome! Feel free to open an Issue or start a Discussion in this repository.

Build (makefile included):
  make            ; builds the program, all catalogs and the VFM/ folder
  make dist       ; only (re)build the VFM/ distribution folder
  make catalogs   ; only (re)generate the .catalog files (requires CatComp)
  make clean      ; remove built files (and VFM/)
  or manually: make catalogs ; gcc -o VagaFileManager VagaFileManager.c DirList.c FileOps.c Dialogs.c DragDrop.c DragVisual.c FileBrowser.c -lauto

Distribution folder (VFM/):
  After a successful build, make recreates VFM/ containing ONLY the files
  needed to run the program:
    VagaFileManager, VagaFileManager.info
    WB2VagaFileManager, WB2VagaFileManager.info
    icons/            (toolbar and menu PNGs)
    Catalogs/<lang>/VagaFileManager.catalog
    Plugins/          (example ARexx/Python plugins)
    README.txt        (general documentation)
    README.Aminet     (readme for Aminet/OS4Depot uploads)
    CHANGELOG.md      (build history)
    LICENSE
  Note: VFM/ is deleted and regenerated on every build.
  The drawer icon for VFM/ is VFM.info, stored next to the folder:
  copy both VFM/ and VFM.info to the target directory.

Installation:
- Run VagaFileManager from the VFM/ folder (keep icons/ and Catalogs/ next
  to the executable; PROGDIR:icons and PROGDIR:Catalogs are used).
- Put the "icons" folder next to the executable (PROGDIR:icons).
  If AISS is installed and the `tbimages:` assign exists, toolbar, menu and
  active-pane icons are loaded from `tbimages:` (24x24 PNGs); otherwise VFM
  falls back to the local `PROGDIR:icons` copies.

Toolbar / chooser icons (AISS names):
  Up        arrowup            View (label)   filter
  Open      open               List (label)   sortingandgrouping
  Refresh   refresh
  Home      home
  View mode drawers (icons) / listview (details)
  Rename    rename
  Delete    delete
  Duplicate duplicate
  Copy      copy
  Paste     paste
  Info      info
  Each icon also has a "_s" (selected/pressed) variant.

Menu icons (AISS names):
  Select all       file_selectall
  Deselect all     file_unselectall
  Invert selection fx_invert

Views:
- Icon view (Workbench-like) and details/list view.
- In list view the leftmost column shows each entry's icon, scaled to
  the text height.
- Clicking a column title (Name/Type/Size/Date) sorts by that column
  (click again to reverse).
- The active pane is marked with an AISS `lightroom_front` icon at the
  right of its path row (lit for the active pane, dim for the other).
- View > Size sets a fixed icon box (Auto, 32, 48, 64 or 96 pixels).  The
  choice is remembered per path.

Keyboard:
- Cursor keys move the selection; in icon view up/down move by a row,
  left/right by one entry.  If nothing is selected, the first entry is
  selected.
- Typing a letter selects the first entry starting with that letter.
- ESC clears the selection.
- In split view, TAB switches to the other pane.
- In split view the active list is outlined in the fill color.
- Requesters and dialogs open under the mouse pointer.
- Copy errors show the system error text (for example "object is in use")
  with Retry, Skip and Cancel, like the Workbench.
- Amiga/Alt shortcuts are left to menus.

Python / ARexx addons:
- Copy a .py or .rexx file to PROGDIR:Plugins, then use "Scripts -> Refresh".
- VFM passes six arguments to Python addons:
    C:Python "<script>" "<arexx-port>" "<path>" "<vfm-executable>" \
             "<drop-port>" "<plugin-channel-dir>"
  The drop port and the channel directory are used by the file-based
  command channel.
- VFM looks for C:Python, then C:Python3.
- Plugin command channel (no ARexx needed):
    - write a one-line request to "<plugin-channel-dir>/req_<id>.txt";
    - run:  VagaFileManager --notify "<drop-port>"
    - read "<plugin-channel-dir>/rep_<id>.txt", format "<rc>|<result>".
  The commands are the same as the ARexx API (VERSION, CD, PATH, ENTRIES,
  SELECTED, DIALOG, REVEAL, ...).  This works even when the system ARexx
  host lookup/RX is not working.
- The stock AmigaOS Python does not expose BOOPSI/ReAction classes.  For a
  Reaction interface an addon can either use the channel command
    DIALOG <title>|<prompt>|<default>|<buttons>  ->  <index>|<text>
  or, without ARexx, launch VFM in dialog mode:
    VagaFileManager --dialog "<result>|<title>|<prompt>|<value>|<buttons>"
    VagaFileManager --dialog "@<spec-file>"
  The spec-file form has five lines (result, title, prompt, value,
  buttons); it avoids quoting problems when the text contains '|' or '"'.
  VFM writes "<index>|<typed-text>" to the result file; 0 = cancelled.
  <buttons> accepts up to six labels separated by '|'.
  See Plugins/ARexxDemo.py for a complete, commented example: the
  Reaction window uses --dialog and commands use the file channel; it
  also keeps a C:RX fallback for older VFM builds.  Diagnostics are in
  T:ARexxDemo.log.
- Python plugins use the file channel and do not need ARexx.  `.rexx`
  plugins are ARexx scripts and still use C:RX.  The VFM ARexx port remains
  available to external clients.
- `Plugins/Find.py` is a complete example: it asks VFM for the pattern,
  runs the native recursive FIND, shows the results and reveals the
  selected file.
- `Plugins/Grep.py` recursively searches text files from the active pane
  path.  It uses ProAction (if running) for the results list, with an ASL
  fallback, and reveals the selected file in VFM.
- `Plugins/CompressLHA.py` creates an LHA archive from the selected files.
  Single-pane mode uses an ASL save requester; split view asks only for the
  archive name and stores it in the other pane.  It uses C:LHA.
- `Plugins/CompressLZX.py` works the same way with C:LZX and creates a
  `.lzx` archive.  If the archive exists it offers Add / Overwrite /
  Cancel.
- `Plugins/CompressGZIP.py` packs the selection with C:TAR and compresses
  the result with C:gzip, producing a `.tar.gz` archive.  It also offers
  Add / Overwrite / Cancel; Add decompresses the existing archive, appends
  the new entries and compresses it again.
- All three archive add-ons use the VFM progress window with the total
  file count and do not need a VFM rebuild to be updated.
- Add-ons can drive the VFM progress window with the channel command
  `PROGRESS [NOCANCEL] [INDET] done total [bytesDone bytesTotal]`;
  `PROGRESS CLOSE` closes it.  FIND/GREP and the bundled add-ons use it.
- If no window appears, run it once from a Shell to see Python errors:
    C:Python PROGDIR:Plugins/ARexxDemo.py VAGAFILEMANAGER PROGDIR: PROGDIR:VagaFileManager
  and check T:ARexxDemo.log.

ARexx diagnostics:
- With VFM running, from the project directory:
    C:Python Tools/ARexxProbe.py VAGAFILEMANAGER VFM/VagaFileManager
  The probe uses VFM --findport for VAGAFILEMANAGER and WORKBENCH, then
  runs two clean RX scripts (LF only) against WORKBENCH and VFM.
  Results go to T:ARexxProbe.log and to the Shell.

GitHub mirror:
- MIRROR.md describes the Gitea push mirror (recommended), the manual push
  and the optional GitHub Actions pull mirror.
- Tools/mirror-github.sh pushes this checkout to
  https://github.com/VagaPPC/VagaFileManager.git after the SSH key (or a PAT)
  has been authorized on GitHub.
- .github/workflows/mirror.yml is the optional scheduled pull mirror.

Localization:
- Built-in language: english (fallback)
- Uses the system language by default via locale.library catalogs
- Catalog name: VagaFileManager.catalog
- Languages provided (.ct): english, italian, french, german, polish, spanish
- OS4 uses ENGLISH language names for catalog directories:
    Catalogs/english, Catalogs/italian, Catalogs/french,
    Catalogs/german, Catalogs/polish, Catalogs/spanish
- Install the generated .catalog files as:
    PROGDIR:Catalogs/<language>/VagaFileManager.catalog
    or
    LOCALE:Catalogs/<language>/VagaFileManager.catalog

License
-------
VagaFileManager is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program.  If not, see <https://www.gnu.org/licenses/>.

The full license text is in the LICENSE file, which is also copied into
the VFM/ distribution folder.

Note: FileBrowser.c and FileBrowser.h are a separate work by OpenAmiga.org
("Listbrowser gadget with cell mode") released into the public domain.
They are used with one small VFM addition: the read-only
`FILEBROWSER_Columns` get attribute, used for keyboard navigation in icon
view.  The rest is unchanged.
