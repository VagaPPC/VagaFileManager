# VagaFileManager - AmigaOS 4.1 Reaction file manager
CC      = gcc
CATCOMP = CatComp
PROG    = VagaFileManager
WB2     = WB2VFM

# Cartella che raccoglie SOLO i file necessari all'esecuzione (distribuzione)
DIST    = VFM

# Ottimizzazione runtime:
#  -O3                  inline, loop/vector optimization, CSE ecc.
#  -fomit-frame-pointer libera un registro (backtrace piu' poveri)
#  -fno-strict-aliasing i cast BOOPSI/ReAction violano lo strict aliasing
CFLAGS  = -O3 -fomit-frame-pointer -fno-strict-aliasing

LANG = english italian french german polish spanish

OBJS = VagaFileManager.o DirList.o FileOps.o Dialogs.o DragDrop.o DragVisual.o FileBrowser.o AppWinRegistry.o OpenWith.o
WB2OBJS = WB2VFM.o

.PHONY: all catalogs dist clean

all: catalogs $(PROG) $(WB2) dist

$(PROG): $(OBJS)
	$(CC) -o $(PROG) $(OBJS) -lauto

$(WB2): $(WB2OBJS)
	$(CC) -o $(WB2) $(WB2OBJS) -lauto

VagaFileManager.o: VagaFileManager.c VagaFileManager.h FileBrowser.h VagaFileManager_strings.h
DirList.o: DirList.c VagaFileManager.h VagaFileManager_strings.h
FileOps.o: FileOps.c VagaFileManager.h VagaFileManager_strings.h
Dialogs.o: Dialogs.c VagaFileManager.h VagaFileManager_strings.h
DragDrop.o: DragDrop.c VagaFileManager.h FileBrowser.h VagaFileManager_strings.h AppWindowRegistry.h
DragVisual.o: DragVisual.c VagaFileManager.h
FileBrowser.o: FileBrowser.c FileBrowser.h VagaFileManager_strings.h
AppWinRegistry.o: AppWinRegistry.c VagaFileManager.h AppWindowRegistry.h
OpenWith.o: OpenWith.c VagaFileManager.h VagaFileManager_strings.h
WB2VFM.o: WB2VFM.c

catalogs:
	$(CATCOMP) VagaFileManager.cd CFILE VagaFileManager_strings.h
	$(CATCOMP) VagaFileManager.cd Catalogs/english/VagaFileManager.ct CATALOG Catalogs/english/VagaFileManager.catalog
	$(CATCOMP) VagaFileManager.cd Catalogs/italian/VagaFileManager.ct CATALOG Catalogs/italian/VagaFileManager.catalog
	$(CATCOMP) VagaFileManager.cd Catalogs/french/VagaFileManager.ct CATALOG Catalogs/french/VagaFileManager.catalog
	$(CATCOMP) VagaFileManager.cd Catalogs/german/VagaFileManager.ct CATALOG Catalogs/german/VagaFileManager.catalog
	$(CATCOMP) VagaFileManager.cd Catalogs/polish/VagaFileManager.ct CATALOG Catalogs/polish/VagaFileManager.catalog
	$(CATCOMP) VagaFileManager.cd Catalogs/spanish/VagaFileManager.ct CATALOG Catalogs/spanish/VagaFileManager.catalog

# Crea la cartella di distribuzione VFM con i soli file per l'esecuzione:
# eseguibili + icone, immagini della toolbar/menu, cataloghi, plugin.
# Le righe con "-" ignorano l'errore (es. cartella gia' esistente); prima
# la vecchia VFM viene cancellata per non lasciare file obsoleti.
dist: catalogs $(PROG) $(WB2)
	-Delete $(DIST) ALL quiet
	-MakeDir $(DIST) >NIL:
	-MakeDir $(DIST)/icons >NIL:
	-MakeDir $(DIST)/Catalogs >NIL:
	-MakeDir $(DIST)/Catalogs/english >NIL:
	-MakeDir $(DIST)/Catalogs/italian >NIL:
	-MakeDir $(DIST)/Catalogs/french >NIL:
	-MakeDir $(DIST)/Catalogs/german >NIL:
	-MakeDir $(DIST)/Catalogs/polish >NIL:
	-MakeDir $(DIST)/Catalogs/spanish >NIL:
	-MakeDir $(DIST)/Plugins >NIL:
	Copy $(PROG) $(DIST)
	Copy $(PROG).info $(DIST)
	Copy $(WB2) $(DIST)
	Copy $(WB2).info $(DIST)
	Copy icons/#? $(DIST)/icons
	Copy Catalogs/english/VagaFileManager.catalog $(DIST)/Catalogs/english
	Copy Catalogs/italian/VagaFileManager.catalog $(DIST)/Catalogs/italian
	Copy Catalogs/french/VagaFileManager.catalog $(DIST)/Catalogs/french
	Copy Catalogs/german/VagaFileManager.catalog $(DIST)/Catalogs/german
	Copy Catalogs/polish/VagaFileManager.catalog $(DIST)/Catalogs/polish
	Copy Catalogs/spanish/VagaFileManager.catalog $(DIST)/Catalogs/spanish
	Copy Plugins/#?.py $(DIST)/Plugins
	-Copy Plugins/#?.rexx $(DIST)/Plugins
	Copy LICENSE $(DIST)
	Copy README.txt $(DIST)
	-Copy README.Aminet $(DIST)
	Copy CHANGELOG.md $(DIST)

clean:
	Delete $(PROG) $(WB2) $(OBJS) $(WB2OBJS) Catalogs/english/VagaFileManager.catalog Catalogs/italian/VagaFileManager.catalog Catalogs/french/VagaFileManager.catalog Catalogs/german/VagaFileManager.catalog Catalogs/polish/VagaFileManager.catalog Catalogs/spanish/VagaFileManager.catalog quiet
	-Delete $(DIST) ALL quiet
