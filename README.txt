VagaFileManager (AmigaOS 4.1, Reaction)

Build (makefile included):
  make            ; builds the program, all catalogs and the VMF/ folder
  make dist       ; only (re)build the VMF/ distribution folder
  make catalogs   ; only (re)generate the .catalog files (requires CatComp)
  make clean      ; remove built files (and VMF/)
  or manually: make catalogs ; gcc -o VagaFileManager VagaFileManager.c DirList.c FileOps.c Dialogs.c DragDrop.c FileBrowser.c -lauto

Distribution folder (VMF/):
  After a successful build, make recreates VMF/ containing ONLY the files
  needed to run the program:
    VagaFileManager, VagaFileManager.info
    WB2VagaFileManager, WB2VagaFileManager.info
    icons/            (toolbar and menu PNGs)
    Catalogs/<lang>/VagaFileManager.catalog
    Plugins/Find.py   (example ARexx/Python plugin)
  Note: VMF/ is deleted and regenerated on every build.

Installation:
- Run VagaFileManager from the VMF/ folder (keep icons/ and Catalogs/ next
  to the executable; PROGDIR:icons and PROGDIR:Catalogs are used).
- Put the "icons" folder next to the executable (PROGDIR:icons).
  The toolbar buttons load their images from PROGDIR:icons/*.png
  (icons come from AISS, 24x24 PNGs).

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

Views:
- Icon view (Workbench-like) and details/list view.
- In list view the leftmost column shows each entry's icon, scaled to
  the text height.
- Clicking a column title (Name/Type/Size/Date) sorts by that column
  (click again to reverse).

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
