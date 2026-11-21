# VagaFileManager — contesto compatto

## Progetto
File manager AmigaOS 4.1 (Reaction/BOOPSI) a due pannelli verticali.
Cartella: `VagaFileManager/`. Repository git; licenza GNU GPLv3 (vedi `LICENSE`).

## Build
```sh
make            # compila, genera cataloghi e ricrea la cartella VMF/
make dist       # solo la cartella di distribuzione VMF/
```
- `gcc` + `-lauto`; cataloghi con `CatComp`.
- `CFLAGS = -O3 -fomit-frame-pointer -fno-strict-aliasing` (ottimizzazione runtime).
- Eseguibili: `VagaFileManager`, `WB2VagaFileManager`.
- Dopo la compilazione `make` ricrea `VMF/` con SOLO i file runtime:
  - `VagaFileManager`, `VagaFileManager.info`
  - `WB2VagaFileManager`, `WB2VagaFileManager.info`
  - `icons/` (toolbar + `m_*.png` per il menu)
  - `Catalogs/<lang>/VagaFileManager.catalog`
  - `Plugins/Find.py`
  - La cartella `VMF/` viene cancellata e rigenerata a ogni build.

## File principali
- `VagaFileManager.c`: main, loop eventi, due pannelli, ARexx, plugin, preferenze.
- `VagaFileManager.h`: struct `Pane`, macro pannello attivo, enum GID/MENU/MIMG, prototipi.
- `Dialogs.c`: finestre, About, menu `menuclass`, `UpdateMenu`, `SetViewMode`, `LaunchNewInstance`.
- `DirList.c`: `ReadDirectory`, `LoadPath`, hook, `UpdateStatus`, `.info` in lista.
- `FileOps.c`: copia/move/delete ricorsivi, `.info`, rinomina, find/grep C.
- `FileBrowser.c`: classe listbrowser con vista icone, lazo, multiselezione.
- `DragDrop.c`: drag&drop interno/esterno.
- `WB2VagaFileManager.c`: utility Commodities che sostituisce le finestre Workbench;
  l'icona `.info` ha il ToolType `DONOTWAIT`; la commodity espone `$VER` (1.0).
- `Plugins/Find.py`: find ricorsivo Python + ARexx.
- `VagaFileManager.cd` + `Catalogs/*/VagaFileManager.ct`: cataloghi (ID 0–128; non rinumerare).
- `VagaFileManager_strings.h`: generato da CatComp, ma hand-edited in passato.

## Scelte/architettura
- Stato per pannello in `gPanes[2]` + `gActivePane`; macro (`gPathBuffer`, `gIcons`, …) puntano al pannello attivo.
- Split: `LM_ADDCHILD`/`LM_REMOVECHILD` + `WM_RETHINK`; gadget pannello 2 con `+PANE_ID_OFFSET` (1000).
- Menu: `menuclass` BOOPSI con `MA_Image` (AISS 20×20) e `MA_EmbeddedKey=FALSE` (etichette di 1 carattere, es. segnalibro “S”, non devono diventare shortcut).
- Scorciatoie menu esplicite con `MA_Key` (`N`, `I`, `E`, `A`).
- Menu Edit: `Nuovo` (submenu `Cassetto` / `File testo`), `Seleziona tutto`,
  `Inverti selezione`, `Annulla selezione`.
- Le voci “Info” (toolbar e menu Edit) sono tradotte in italiano come “Informazioni”.
- Dopo i comandi di selezione da menu si chiama `RefreshGList` (le modifiche a
  `LBNA_Selected` non ridisegnano da sole la classe FileBrowser).
- ARexx host `VAGAFILEMANAGER`; con più istanze la seconda usa un nome univoco (`VAGAFILEMANAGER.<task>`) e `Find.py` riceve il nome porta come argomento.
- `PROGDIR:` viene risolto in assoluto prima di lanciare plugin (Python/RX).
- Vista icone: drag dall’immagine = drag&drop file; drag da label/sfondo/lazo = selezione multipla. Shift/Ctrl+click = toggle.
- `.info`: in vista lista compaiono solo con “Mostra file info”; in vista icone sempre nascosti.
- Copia/move/rename: il `<file>.info` segue sempre l’oggetto (file o cassetto).
- Cancellazione: `DeleteEntryRecursive` gestisce esplicitamente anche i cassetti vuoti
  (delete diretto al primo livello, senza tenere aperto il contesto di directory).
  Prima di scendere in un cassetto e prima del `Delete` finale vengono azzerati i bit
  di protezione di tutti i cassetti antenati (dall'alto verso il basso) e del cassetto
  stesso (le directory estratte da archivi sono spesso read-only: senza write bit sul
  parent le voci non si possono rimuovere).
  Il `Delete` viene ritentato piu' volte, con un secondo passaggio sui figli rimasti;
  se l'oggetto non esiste piu' il risultato e' considerato riuscito. Prima della
  cancellazione i `NotifyRequest` dei due pannelli vengono sospesi e riattivati dopo.
- Operazioni massive (cancella, copia, sposta, duplica): barra di progressione
  con Pausa/Continua; il pulsante Cancel o la chiusura della finestra interrompono
  l'operazione (`gProgCancel`). Usa `slider.gadget` come barra principale,
  `fuelgauge.gadget` come alternativa e, se mancano entrambi, un gadget di testo
  con percentuale; la finestra si apre sullo screen pubblico predefinito e viene
  portata in primo piano.
- Preferenze: `ENVARC:VagaFileManager.prefs`
  - globali: `Width`, `Height`, `Split`
  - per path: sezioni `[Path]` con `Path`, `View`, `Filter`, `Sort`, `ShowInfo`
  - caricamento all’avvio; salvataggio all’uscita e prima di “Nuova”
  - impostazioni di vista per-path; dimensione/split globali (finestra)
- “Nuova” replica path del pannello attivo (e impostazioni correnti, salvate prima del lancio); `-` = vista radice.
- Bookmark: `ENVARC:VagaFileManager.bookmarks`; al ripristino le impostazioni del bookmark hanno precedenza sul path.
- Shell: pulsante AISS `shell` nella toolbar (prima di Dividi) e voce menu Edit → Shell.
  Apre `C:NewShell WINDOW "CON:x/y/w/h/title/CLOSE"` con `SYS_CurrentDir` sul path del pannello attivo;
  finestra circa 2/3 x 3/5 dello schermo, centrata (min 640x400).
- Locale: `locale.library` viene aperta esplicitamente (`gLocale`), senza dipendere dal global `ILocale`.
- Filtro rapido in fondo a ogni pannello: label `MSG_FILTER` + string.gadget,
  sotto la lista e sopra la barra di stato Selezione/Elementi.
  Filtra in tempo reale (IDCMP_IDCMPUPDATE) e accetta wildcard Amiga (`#?`, `?`, `[]`, `~`)
  oltre a `*` e `?`; `*` viene tradotto in `#?` prima di `ParsePatternNoCase`.
  Senza wildcard è una ricerca "contiene". Il testo del filtro non è salvato nelle preferenze.
- Python plugin: il path del pannello attivo viene passato come `argv[2]` al lancio;
  `Find.py` usa quello e, come fallback, `PATH` via ARexx.
  `SEARCHPATH` resta disponibile per altri script (cassetto/volume selezionato o path corrente).

## Comandi ARexx utili
`PATH`, `SEARCHPATH`, `ASK <prompt>`, `REVEAL <path>`, `CD`, `OPEN`, `UP`, `FORWARD`, `REFRESH`,
`ENTRIES`, `SELECTED`, `RENAME`, `DELETE`, `DUPLICATE`, `COPY`, `CUT`, `PASTE`, `INFO`, `SIZE`,
`VIEW ICONS|ALLFILES`, `SORT TYPE|NAME|DATE|SIZE`, `MODE ICONS|DETAILS`, `SPLIT`, `NEW`,
`SCRIPT <file>`, `FIND`, `GREP`, `QUIT`, `VERSION`.

## API/note OS4
- `ObtainDirContextTags`/`ExamineDir` (non usare Examine/ExNext obsoleti).
- Lock: `SHARED_LOCK`/`EXCLUSIVE_LOCK` (non `ACCESS_READ`).
- Menu: `intuition/menuclass.h`, `MM_NEWMENU`, `MM_NEXTSELECT`, `WINDOW_MenuStrip`.
- Requester stringa: `PromptString()` interna (window.class + string.gadget).
- Python OS4: moduli `arexx`, `asl`, `amiga`; eseguibile `C:Python`.

## Stato attuale (ultime modifiche)
- Menu con icone AISS; segnalibri a 1 carattere corretti.
- `Find.py` in inglese, riceve il path della finestra come argomento (fallback `PATH`), porta ARexx per istanza.
- Preferenze per-path con sezioni `[Path]`; window size/split globali.
- “Nuova” passa il path e salva prima le impostazioni.
- Vista icone/lista aggiornata anche nel gadget durante `LoadPath`.
- Menu Edit: Nuovo (Cassetto/File testo) e comandi di selezione.
- Drop verso Shell/file requester tramite input sintetico (attivazione finestra + `IND_WRITEEVENT`).
- Commodity WB2 con `$VER` 1.0 e versione nella descrizione.

## Caveat / possibili TODO
- `menuclass` richiede OS4.1 Final Edition; se manca, il menu può non apparire (manca fallback GadTools).
- Preferenze con più istanze: l’ultima chiusura sovrascrive il file (last-close-wins).
- Il path del secondo pannello non viene salvato; al ripristino split usa il path del pannello 0.
- Se serve dimensione/split per-path, va esteso il formato `[Path]`.
- Il drag&drop in uscita verso Workbench/VFM e' gestito direttamente (copia/move).
  Verso Shell e file requester usa l'attivazione della finestra di destinazione + input sintetico
  (`keymap.library/MapANSI` + `input.device/IND_WRITEEVENT`, con due Shift
  iniziali “di cortesia” per non perdere il primo carattere). Le finestre Shell
  vengono riconosciute dal task (`Shell Process`, `con-handler`, `CON`) o
  dall'assenza di gadget/title; le finestre Workbench (task `Workbench`) sono escluse.
  Non e' un drag
  Workbench nativo (non esiste API pubblica): inserisce il percorso come testo
  nel gadget attivo (per la Shell tutti i percorsi, quotati; per i requester il primo).
- In ingresso VagaFileManager ha `WINDOW_AppWindow, TRUE`: riceve già drop
  `AMTYPE_APPWINDOW` da Workbench/FileRequest/altre app tramite `HandleAppDrop`.
- Vecchie chiavi `Pane0*`/`Pane1*` nel prefs vengono ignorate (tranne Width/Height/Split).
- `VagaFileManager_strings.h` contiene `CatCompBlock` con tutti gli ID: aggiungendo stringhe al `.cd`
  va rigenerato (ora `make` esegue `catalogs` prima della compilazione). Un ID mancante nel block
  fa crashare `GetString` (successo con Shell).
- Log: `T:VagaFileManager.log`, `T:WB2VagaFileManager.log`.

## Test rapido
1. `make`.
2. Menu: icone presenti, check/ghost, segnalibro “S” corretto.
3. Vista lista + “Mostra file info”: compaiono `.info`; vista icone: nascosti.
4. Sposta/rinomina file con `.info`.
5. Multiselezione: lazo da sfondo/label, Shift/Ctrl+click, drag da icona selezionata.
6. `Find.py`: entra in un cassetto (senza selezionare nulla); requester nome; reveal.
7. Shell: icona prima di Dividi e menu Edit → Shell; deve aprirsi sul path del pannello,
   in una finestra rettangolare centrata (circa 2/3 x 3/5 dello schermo) con gadget di chiusura.
8. Filtro in basso (sopra la barra Selezione/Elementi): scrivere `*.info`, `#?.jpg`, `?a*`;
   la lista deve aggiornarsi mentre si scrive.
9. Cancellazione: selezionare un cassetto vuoto e usare Delete; deve sparire senza errori.
   Con un cassetto pieno: niente errore "potrebbe non essere vuoto"; barra di progressione.
10. Barra di progressione su copia/sposta/duplica/cancella massivi: chiudendola
   l'operazione si interrompe.
11. Drop da FileRequest/Shell/altra app sopra la finestra: deve comparire la scelta Copia/Sposta.
12. Naviga A/B con viste diverse; torna indietro; chiudi/riapri; “Nuova”.
13. `WB2VagaFileManager.info`: Info mostra il ToolType `DONOTWAIT`.
14. Edit → Nuovo → Cassetto/File testo: crea l'elemento e aggiorna la lista.
15. Edit → Seleziona tutto / Inverti selezione / Annulla selezione.
16. Drag su una Shell: inserisce i percorsi quotati; su un file requester: il primo percorso.
17. Commodity WB2: Exchange/Exchanger mostra il nome con versione `$VER`.
