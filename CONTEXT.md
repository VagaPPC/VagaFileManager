# VagaFileManager — contesto

## Progetto
File manager AmigaOS 4.1 (Reaction/BOOPSI) a due pannelli.
Repo git, licenza GNU GPLv3 (`LICENSE`). Cartella `VagaFileManager/`.
Versione e build in `VagaFileManager_version.h` (`VFM_VERSION`, `VFM_BUILD`,
`VFM_VERSION_DATE`); usati da `$VER`, About e comando ARexx `VERSION`.

## Build
- Amiga: `make` → cataloghi (CatComp) + eseguibili + `VMF/` (solo runtime).
  `make dist` ricrea solo `VMF/`; `make clean` pulisce.
- CFLAGS `-O3 -fomit-frame-pointer -fno-strict-aliasing`; link `-lauto`.
- Eseguibili: `VagaFileManager`, `WB2VagaFileManager`.
- Cross-build Linux: `/home/vagappc/toolchains/build-vfm.sh`
  (ppc-amigaos-gcc 11.5.0 + header SDK nativo `/home/vagappc/src/SDK`),
  output in `VMF/`. I cataloghi richiedono CatComp su Amiga.
- `VMF/`: eseguibili + `.info`, `icons/`, `Catalogs/<lang>/`, `Plugins/`, `LICENSE`.
  L'icona del cassetto è `VMF.info`, da copiare accanto a `VMF/`.

## File
- `VagaFileManager.c`: main, loop eventi, pannelli, split, ARexx, plugin, preferenze.
- `VagaFileManager.h`: struct `Pane`, macro pannello attivo, enum GID/MENU/MIMG, prototipi.
- `Dialogs.c`: progress bar custom, dialoghi, About, segnalibri, menu, viste.
- `DirList.c`: lettura/filtro directory, hook, path, notify, navigazione, stato.
- `FileOps.c`: copia/move/delete, conflitti, appunti, rinomina, size, find/grep.
- `FileBrowser.c/.h`: listbrowser con vista icone (public domain OpenAmiga).
- `DragDrop.c`: drag&drop interno/esterno.
- `WB2VagaFileManager.c`: commodity che sostituisce le finestre Workbench.
- `Plugins/Find.py`, `Plugins/ARexxDemo.py`: esempi di addon.
- `VagaFileManager.cd` + `Catalogs/*/*.ct`: cataloghi.
  `VagaFileManager_strings.h` è generato da CatComp: rigenerare dopo modifiche al `.cd`.

## Architettura
- Stato per pannello `gPanes[2]` + `gActivePane`; macro `gPathBuffer`, `gIcons`, ...
- Split: `LM_ADDCHILD`/`LM_REMOVECHILD` + `WM_RETHINK`; ID pannello 2 + `PANE_ID_OFFSET` (1000).
- Menu `menuclass` (OS4.1 FE) con icone AISS 20×20 e `MA_Key` esplicite.
- Vista icone: `.info` nascosti; filtro "solo icone" per cartella (se esiste almeno un `.info`).
  Vista lista: mostra tutto; `.info` solo con "Mostra file info".
- Copia/move/rename: il `.info` segue sempre l'oggetto.
- Conflitti copia: Sovrascrivi / tutti, Aggiorna / tutti, Rinomina, Salta.
- Cancellazione DOpus-style: Delete diretto, poi `DeleteDirectoryContents` con
  `EX_DoCurrentDir`; azzera i bit di protezione, ritenta, sospende le notify.
- Drag&drop: stesso device → move, altri → copy; supporta più selezione.
- Apri (pulsante/menu) su cassetto/volume → nuova istanza VFM centrata sul mouse
  (+24,+24); doppio clic naviga. Anche "Nuova finestra".
- "Calcola dimensione": barra indeterminata (blocco scorrevole + contatori),
  un solo passaggio; Cancel/Pausa.
- Geometria finestra per volume (size + posizione): sezioni `[Volume]` nel prefs
  (`Name`, `Width`, `Height`, `Left`, `Top`, `HasPos`). Chiave = volume del path
  iniziale dell'istanza; navigare non cambia la geometria. Al salvataggio il file
  viene riletto e fuso, così più istanze non si sovrascrivono.
- Preferenze `ENVARC:VagaFileManager.prefs`: globali `Width/Height/Split`;
  `[Volume]`; `[Path]` con `Path/View/Filter/Sort/ShowInfo`.
  Il path del secondo pannello non è salvato (lo split riparte dal pannello 0).
- Bookmark `ENVARC:VagaFileManager.bookmarks`.
- Filtro rapido in fondo a ogni pannello: wildcard Amiga (`#?`, `?`, `[]`, `~`, `*`),
  ricerca "contiene" senza wildcard; non salvato.
- Shell: `C:NewShell` sul path attivo, finestra ~2/3×3/5 centrata.
- Log: `T:VagaFileManager.log`, `T:WB2VagaFileManager.log`.

## Plugin / ARexx
- Porta pubblica `VAGAFILEMANAGER`; con più istanze `VAGAFILEMANAGER.<task>`.
- I plugin stanno in `PROGDIR:Plugins`; menu Script → Refresh.
  `PROGDIR:` viene risolto in assoluto prima del lancio:
  - `.py` → `C:Python` (o `C:Python3`) `"<script>" "<porta>" "<path pannello>" "<eseguibile VFM>"`
  - `.rexx` → `C:RX "<script>"`
  - se Python non c'è, VFM mostra un messaggio (non resta muto).
- Il Python OS4 standard espone solo `amiga`, `arexx`, `asl`, `catalog`, `icon`,
  `installer`: **niente BOOPSI/ReAction diretto**.
- Per l'interfaccia grafica VFM espone due strade (entrambe `PromptDialog()`
  in `Dialogs.c`, label + string gadget + max 6 pulsanti):
  - ARexx: `DIALOG <title>|<prompt>|<default>|<buttons>` → `<index>|<text>`;
  - senza ARexx: `VagaFileManager --dialog "<result>|<title>|<prompt>|<value>|<buttons>"`
    oppure `VagaFileManager --dialog "@<spec-file>"` (5 righe, evita problemi
    di quoting).  Scrive `<index>|<testo>` nel file risultato; non apre la
    finestra principale né la porta ARexx.
- `ARexxDemo.py`: esempio puro Python con finestra Reaction via `--dialog`
  su file; invia i comandi con `C:RX` e uno script generato
  (`T:VFM_ARexxDemo.cmd` → `T:VFM_ARexxDemo.out`), non con `arexx.dorexx`
  (che su Python 2.5.6 restituisce risultato vuoto).  Se il risultato manca,
  mostra il path di lancio o `os.listdir()`.  Log diagnostico in
  `T:ARexxDemo.log`; se il dialogo fallisce mostra un messaggio ASL.
- Comandi: `HELP`, `VERSION`, `QUIT`, `NEW`, `SPLIT`, `CD`, `OPEN`, `UP`, `FORWARD`,
  `REFRESH`, `PATH`, `SEARCHPATH`, `ASK`, `DIALOG`, `REVEAL`, `ENTRIES`, `SELECTED`,
  `RENAME`, `DELETE`, `DUPLICATE`, `COPY`, `CUT`, `PASTE`, `INFO`, `FIND`, `GREP`,
  `SIZE`, `VIEW`, `SORT`, `MODE`, `SCRIPT`.

## API OS4
- `ObtainDirContextTags`/`ExamineDir` (non `Examine`/`ExNext`).
- Lock `SHARED_LOCK`/`EXCLUSIVE_LOCK` (non `ACCESS_READ`).
- `PromptString`/`PromptDialog` = `window.class` + gadget ReAction.
- `CreateDirUnlocked` per non tenere lock aperti (evita `ERROR_OBJECT_IN_USE`).
- Menu: `intuition/menuclass.h`, `MM_NEWMENU`, `MM_NEXTSELECT`, `WINDOW_MenuStrip`.

## Caveat / TODO
- `menuclass` richiede OS4.1 Final Edition (nessun fallback GadTools).
- `MAX_ENTRIES` 1024: directory più grandi troncate senza avviso.
- Il path del secondo pannello non è salvato.
- Drag verso Shell/requester = input sintetico (non drag Workbench nativo).
- `IsVFMWindow` usa nome task + magic in `UserData`.
- Preferenze: il merge è best-effort (due istanze che chiudono nello stesso
  istante possono ancora sovrascriversi).

## Test rapido
1. `make`; apri VFM su due volumi: geometrie separate.
2. Apri un cassetto col pulsante/menu Apri: nuova istanza centrata sul mouse.
3. Naviga: la finestra non cambia dimensione.
4. Calcola dimensione: barra animata, Cancel/Pausa.
5. Copia/sposta/duplica/cancella: progress, conflitti, `.info` segue.
6. Viste icone/lista, filtro rapido, `.info`.
7. Plugin → ARexxDemo: dialoghi Reaction su file; comandi ARexx (PATH/ENTRIES).
8. `Find.py`: cerca e rivela.
9. WB2: ToolType `DONOTWAIT`, Exchange mostra la versione.
