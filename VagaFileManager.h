/*
 * VagaFileManager - two-pane file manager for AmigaOS 4.1
 * Copyright (C) 2026 vagappc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef VAGAFILEMANAGER_H
#define VAGAFILEMANAGER_H

#include "VagaFileManager_version.h"

/* Dichiarazioni condivise di VagaFileManager (globals e prototipi). */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/memory.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/datetime.h>
#include <dos/notify.h>
#include <dos/dostags.h>

#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/intuition.h>
#include <intuition/icclass.h>
#include <intuition/imageclass.h>
#include <intuition/cghooks.h>
#include <intuition/screens.h>
#include <intuition/pointerclass.h>
#include <intuition/menuclass.h>

#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <graphics/regions.h>

#include <utility/tagitem.h>

#include <workbench/icon.h>
#include <workbench/workbench.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <images/bitmap.h>

#include <workbench/startup.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/listbrowser.h>
#include <proto/bitmap.h>
#include <proto/wb.h>

#include <proto/locale.h>
#include <libraries/locale.h>

#define CATCOMP_NUMBERS
#include "VagaFileManager_strings.h"

#include "FileBrowser.h"

extern struct LocaleInfo gLoc;
#define LS(id) GetString(&gLoc, (id))

/* ---- messaggio drag & drop VFM -> VFM ---- */
struct VFMDropMessage
{
    struct Message vfm_Message;
    uint16         vfm_Type;
    LONG           vfm_NumPaths;
    WORD           vfm_MouseX;   /* screen position of the drop */
    WORD           vfm_MouseY;
    char           vfm_Paths[1];
};

/* ---- ID di finestre, oggetti, gadget e menu ---- */
enum { WID_MAIN = 0, WID_LAST };
enum { OID_MAIN = 0, OID_LAST };

enum
{
    GID_LIST = 1,
    GID_PATH,
    GID_BACK,
    GID_FORWARD,
    GID_OPEN,
    GID_REFRESH,
    GID_HOME,
    GID_TOGGLE_VIEW,
    GID_RENAME,
    GID_DELETE,
    GID_DUPLICATE,
    GID_COPY,
    GID_CUT,
    GID_PASTE,
    GID_INFO,
    GID_STATUS,
    GID_VIEW_FILTER,
    GID_SORT_MODE,
    GID_RENAME_STR,
    GID_RENAME_OK,
    GID_RENAME_CANCEL,
    GID_PROG_CANCEL,
    GID_PROG_PAUSE,
    GID_SPLIT,
    GID_CALCSIZE,
    GID_SHELL,
    GID_FILTER,
    GID_TOP_COPY,
    GID_TOP_MOVE,
    GID_TOP_DELETE,
    GID_CLEAR_FILTER       /* pulsante che svuota il filtro rapido */
};

enum
{
    MENU_NEWWINDOW = 100,
    MENU_INFO,
    MENU_QUIT,
    MENU_RENAME,
    MENU_DELETE,
    MENU_DUPLICATE,
    MENU_COPY,
    MENU_CUT,
    MENU_PASTE,
    MENU_FILEINFO,
    MENU_OPEN,
    MENU_REFRESH,
    MENU_TOGGLE_VIEW,
    MENU_ICON_SIZE_AUTO,
    MENU_ICON_SIZE_32,
    MENU_ICON_SIZE_48,
    MENU_ICON_SIZE_64,
    MENU_ICON_SIZE_96,
    MENU_VIEW_ICONS,
    MENU_VIEW_ALLFILES,
    MENU_VIEW_SHOWINFO,
    MENU_SORT_TYPE,
    MENU_SORT_NAME,
    MENU_SORT_DATE,
    MENU_SORT_SIZE,
    MENU_ADDBOOKMARK,
    MENU_MANAGE,
    MENU_CALCSIZE,
    MENU_SCRIPT_REFRESH,
    MENU_SCRIPT_OPENDIR,
    MENU_SHELL,
    MENU_NEW_DRAWER,
    MENU_NEW_TEXTFILE,
    MENU_SELECT_ALL,
    MENU_INVERT_SELECTION,
    MENU_DESELECT_ALL,
    MENU_CTX_OPEN,         /* Apri del menu contestuale: cassetti in nuove finestre */
    MENU_OPEN_WITH,        /* Apri con... (solo file) */
    MENU_BAR_PATH,         /* Vista > Vista barra: barra del percorso */
    MENU_BAR_TOOLS,        /* barra dei pulsanti */
    MENU_BAR_FILTER,       /* barra del filtro */
    MENU_BAR_STATUS,       /* barra di stato */
    MENU_SPLIT,            /* Vista > Dividi (anche senza barra pulsanti) */
    MENU_WB_EMULATION      /* Vista > Emulazione WB */
};
#define MENU_BOOKMARK_BASE 1000
#define MENU_SCRIPT_BASE   2000

enum { COL_ICON = 0, COL_NAME, COL_TYPE, COL_SIZE, COL_DATE, COL_COMMENT, NUM_COLS };

enum { VIEW_FILTER_ICONS_ONLY = 0, VIEW_FILTER_ALL_FILES = 1 };
enum { SORT_BY_TYPE = 0, SORT_BY_NAME, SORT_BY_DATE, SORT_BY_SIZE };
enum { CLIP_NONE = 0, CLIP_COPY, CLIP_CUT };

/* ---- limiti e costanti ---- */
#define PATH_MAX_LEN 1024
#define MAX_ENTRIES  1024
#define NAME_MAX_LEN 256
#define COMMENT_MAX_LEN 80
#define MAX_CLIPBOARD 256
#define PANE_ID_OFFSET 1000
#define MAX_PLUGINS 32
#define PLUGIN_DIR "PROGDIR:Plugins"
#define REXX_PORT_NAME "VAGAFILEMANAGER"

/* ---- voce di directory (copia in memoria per l'ordinamento) ---- */
struct DirEntryInfo
{
    char name[NAME_MAX_LEN];
    char typeStr[32];
    char sizeStr[32];
    char dateStr[LEN_DATSTRING];
    char comment[COMMENT_MAX_LEN];
    BOOL isDir;
    BOOL hasInfo;
    int64 fileSize;
    struct DateStamp dateStamp;
    uint32 typeRank;
};

#define DRAG_THRESHOLD 6
#define MAX_BOOKMARKS 64
#define BOOKMARK_FILE "ENVARC:VagaFileManager.bookmarks"
#define PREFS_FILE "ENVARC:VagaFileManager.prefs"
#define DEFAULT_WIN_WIDTH 820
#define DEFAULT_WIN_HEIGHT 620

struct Bookmark { char name[64]; char path[PATH_MAX_LEN]; ULONG viewMode, filter, sortMode; };

/* Impostazioni persistenti della finestra. Le impostazioni di vista
   (icone/lista, filtro, ordinamento, file info) sono memorizzate per
   percorso nel file delle preferenze. */
struct VFMPrefs
{
    LONG  width, height;
    ULONG split;
    ULONG hiddenBars;     /* BAR_* nascoste; 0 = tutte visibili (default) */
    ULONG wbEmulation;    /* Emulazione WB: niente barre, niente menu
                             contestuali, cassetti in nuove finestre */
};

/* Barre di ogni pannello che si possono nascondere (Vista > Vista barra).
   L'impostazione e' globale (entrambi i pannelli) e salvata nelle prefs. */
#define BAR_PATH    (1UL << 0)
#define BAR_TOOLS   (1UL << 1)
#define BAR_FILTER  (1UL << 2)
#define BAR_STATUS  (1UL << 3)
/* BarChecked: scelta dell'utente in Vista barra (spunte del menu).
   BarVisible: barra davvero mostrata; in Emulazione WB nessuna, ma la
   scelta resta salvata e torna disattivando l'emulazione. */
#define BarChecked(b) ((gPrefs.hiddenBars & (b)) == 0)
#define BarVisible(b) (!gPrefs.wbEmulation && BarChecked(b))

/* ---- stato globale ---- */
extern struct VFMPrefs gPrefs;
extern LONG gWinInnerW, gWinInnerH;

#define VFMDROP_TYPE 0x5646

/* ---- marcatore delle finestre VFM (per il drag & drop) ---- */
struct VFMWindowTag
{
    uint32 vft_Magic;
    char   vft_PortName[64];
};
#define VFM_WINDOW_MAGIC 0x56464D31UL  /* "VFM1" */

#define MAX_HISTORY 64
#define MAX_DELETE_DEPTH 256
/* maximum depth for copy/move and for the progress bar counts; beyond it
   the operation fails without touching the source */
#define MAX_COPY_DEPTH 256
#define SHARED_CLIP_PATH "T:FileList_Clipboard.txt"

enum
{
    IMG_UP = 0,
    IMG_OPEN,
    IMG_REFRESH,
    IMG_HOME,
    IMG_BACK,
    IMG_FORWARD,
    IMG_ICONS,
    IMG_DETAILS,
    IMG_RENAME,
    IMG_DELETE,
    IMG_DUPLICATE,
    IMG_COPY,
    IMG_CUT,
    IMG_PASTE,
    IMG_INFO,
    IMG_FILTER,
    IMG_SORT,
    IMG_SIZE,
    IMG_SHELL,
    IMG_SPLIT,
    IMG_CLEARFILTER,
    IMG_COUNT
};

/* Iconcine AISS per le voci di menu (20x20, in icons/m_*.png) */
enum
{
    MIMG_NEW = 0,
    MIMG_INFO,
    MIMG_QUIT,
    MIMG_RENAME,
    MIMG_DELETE,
    MIMG_DUPLICATE,
    MIMG_COPY,
    MIMG_CUT,
    MIMG_PASTE,
    MIMG_SIZE,
    MIMG_OPEN,
    MIMG_REFRESH,
    MIMG_ICONS,
    MIMG_SHOWINFO,
    MIMG_FILTER,
    MIMG_SORT,
    MIMG_BOOKMARK,
    MIMG_BOOKMARKADD,
    MIMG_SETTINGS,
    MIMG_SCRIPT,
    MIMG_FOLDER,
    MIMG_SHELL,
    MIMG_SELECTALL,
    MIMG_UNSELECTALL,
    MIMG_INVERT,
    MIMG_COUNT
};

/* ---- per-pane state ---- */
/* Ogni "pannello" (pane) ha il proprio stato e i propri gadget.  Le macro
   qui sotto mantengono compatibili i nomi storici: si riferiscono sempre al
   pannello attivo (gPanes[gActivePane]).  Prima di usare una funzione che
   lavora sul pannello, impostare gActivePane. */
struct Pane
{
    struct List fileList;
    Object     *listGad, *pathGad, *statusGad, *toggleGad;
    Object     *filterGad;
    Object     *sizeGad;
    Object     *btnRename, *btnDelete, *btnDuplicate, *btnCopy, *btnCut, *btnPaste, *btnInfo;
    Object     *paneLayout;
    struct ColumnInfo *colInfo;

    char        pathBuffer[PATH_MAX_LEN];
    char        filterBuffer[NAME_MAX_LEN];
    char        filterApplied[NAME_MAX_LEN];
    char        filterSeen[NAME_MAX_LEN];
    ULONG       filterChangeTicks;
    char        winTitle[PATH_MAX_LEN];
    LONG        entryCount;
    struct DiskObject *icons[MAX_ENTRIES];
    BOOL        isDir[MAX_ENTRIES];
    char        names[MAX_ENTRIES][NAME_MAX_LEN];
    char        comment[MAX_ENTRIES][COMMENT_MAX_LEN];
    int64       fileSize[MAX_ENTRIES];
    struct DateStamp dateStamp[MAX_ENTRIES];
    uint32      typeRank[MAX_ENTRIES];
    ULONG       viewFilter, sortMode, viewMode;
    ULONG       showInfoFiles;
    ULONG       iconSize;           /* 0 = automatic, else fixed image box */
    char        forwardStack[MAX_HISTORY][PATH_MAX_LEN];
    LONG        forwardCount;
    ULONG       lastRefreshTicks;

    struct NotifyRequest notify;
    BOOL        notifyActive;
    struct MsgPort *notifyPort;
    char        notifyPath[PATH_MAX_LEN];
};

extern struct Pane gPanes[2];
extern int gActivePane;

#define gIcons        (gPanes[gActivePane].icons)
#define gIsDir        (gPanes[gActivePane].isDir)
#define gNames        (gPanes[gActivePane].names)
#define gComment      (gPanes[gActivePane].comment)
#define gFileSize     (gPanes[gActivePane].fileSize)
#define gDateStamp    (gPanes[gActivePane].dateStamp)
#define gTypeRank     (gPanes[gActivePane].typeRank)
#define gEntryCount   (gPanes[gActivePane].entryCount)
#define gPathBuffer   (gPanes[gActivePane].pathBuffer)
#define gFilterText   (gPanes[gActivePane].filterBuffer)
#define gViewFilter   (gPanes[gActivePane].viewFilter)
#define gSortMode     (gPanes[gActivePane].sortMode)
#define gViewMode     (gPanes[gActivePane].viewMode)
#define gShowInfoFiles (gPanes[gActivePane].showInfoFiles)
#define gIconSize     (gPanes[gActivePane].iconSize)
#define gWinTitle     (gPanes[gActivePane].winTitle)
#define gForwardStack (gPanes[gActivePane].forwardStack)
#define gForwardCount (gPanes[gActivePane].forwardCount)
#define gNotify       (gPanes[gActivePane].notify)
#define gNotifyActive (gPanes[gActivePane].notifyActive)
#define gNotifyPort   (gPanes[gActivePane].notifyPort)
#define gNotifyPath   (gPanes[gActivePane].notifyPath)

extern struct Hook gSortHooks[2];
extern struct Hook gIconRenderHooks[2];

extern char gClipboard[MAX_CLIPBOARD][PATH_MAX_LEN];
extern LONG gClipboardCount;
extern ULONG gClipboardMode;

extern BOOL  gDragActive;
extern WORD  gDragStartX, gDragStartY;
extern BOOL  gDragButtonDown;

extern char gDropPortName[64];

extern struct Bookmark gBookmarks[MAX_BOOKMARKS];
extern LONG gBookmarkCount;
extern Object *gMenuRoot;
extern Object *gCtxMenuItem;   /* menu contestuale: cassetto */
extern Object *gCtxMenuFile;   /* menu contestuale: file (con Apri con...) */
extern Object *gCtxMenuEmpty;  /* menu contestuale: spazio vuoto */
extern Object *gMenuImg[MIMG_COUNT];
extern struct VFMWindowTag gWinTag;

extern char gPlugins[MAX_PLUGINS][PATH_MAX_LEN];
extern LONG gPluginCount;
extern char gRexxPortName[64];

extern volatile BOOL gProgActive;
extern volatile BOOL gProgCancel;
extern volatile BOOL gProgPause;
extern Object *gProgWinObj;
extern Object *gProgPauseBtn;
extern struct Window *gProgWin;
extern ULONG gProgDone;
extern uint64 gProgBytesDone;
extern uint64 gProgBytesTotal;

extern CONST_STRPTR gIconNames[IMG_COUNT];
extern Object *gBtnImg[IMG_COUNT];
extern Object *gBtnImgSel[IMG_COUNT];
extern Object *gMainWinObj;

/* ---- funzioni ---- */

/* ---- Log e utilita ---- */
void DBG(CONST_STRPTR fmt, ...);      /* solo con il ToolType DEBUG */
void LOGERR(CONST_STRPTR fmt, ...);   /* errori: scritti sempre */
extern BOOL gDebugLog;

/* ---- Directory, voci e percorsi ---- */
uint32 IconHookFunc(struct Hook *hook, APTR gadget, APTR node);
CONST_STRPTR TypeStringEx(struct ExamineData *ed);
uint32 TypeRankEx(struct ExamineData *ed);
void SizeStringEx(struct ExamineData *ed, STRPTR buf, ULONG bufSize);
void FormatHumanSize(uint64 n, STRPTR buf, ULONG bufSize);
extern char gSizeDecimalPoint;
void DateStringEx(struct ExamineData *ed, STRPTR buf, ULONG bufSize);
int CompareDateStamp(const struct DateStamp *a, const struct DateStamp *b);
int CompareEntries(const void *a, const void *b);
uint32 ListIconRenderHook(struct Hook *hook, struct Node *node, struct LBDrawMsg *msg);
uint32 SortCompareHook(struct Hook *hook, APTR obj, struct LBSortMsg *msg);
BOOL HasInfoFile(CONST_STRPTR fullPath);
BOOL DirHasInfoFiles(CONST_STRPTR drawer);
BOOL BuildChildPath(CONST_STRPTR currentPath, CONST_STRPTR name, STRPTR outPath, ULONG outSize);
BOOL GetParentPath(CONST_STRPTR currentPath, STRPTR outPath, ULONG outSize);
BOOL IsDirectoryPath(CONST_STRPTR path);
BOOL IsRootPath(CONST_STRPTR path);
void FreeEntries(struct List *list);
BOOL DosListNameToC(BSTR bname, STRPTR out, ULONG outSize);
BOOL RootNameExists(struct DirEntryInfo *entries, LONG count, CONST_STRPTR name);
LONG ReadDirectory(CONST_STRPTR drawer, struct List *list);
BOOL VFMParsePattern(CONST_STRPTR pattern, STRPTR parsed, LONG parsedSize);
BOOL VFMHasWildcard(CONST_STRPTR pattern);
void FreeDirectoryScratch(void);
void SetupNotify(CONST_STRPTR path);
void UpdateWindowTitle(struct Window *win, CONST_STRPTR path);
void SetPathGadget(Object *pathGad, struct Window *win, CONST_STRPTR path);
void UpdateStatus(Object *listGad, Object *statusGad, struct Window *win);
CONST_STRPTR FilePart(CONST_STRPTR path);

/* ---- Navigazione e selezione ---- */
void LoadPath(CONST_STRPTR path, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void NavigateTo(CONST_STRPTR path, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void GoUp(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void GoForward(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
BOOL TryOpenSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list, BOOL newInstance);
BOOL OpenSelectedEntries(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list, BOOL newWindows);
LONG CountSelectedEntries(struct List *list);
struct Node *GetFirstSelectedNode(struct List *list);
void SelectAllEntries(struct List *list);
void DeselectAllEntries(struct List *list);
void InvertSelectionEntries(struct List *list);

/* ---- Dialoghi e ricerca ---- */
void ShowMessage(CONST_STRPTR title, CONST_STRPTR body);
void InitEasyAtMouse(struct EasyStruct *es);
BOOL ConfirmRequest(CONST_STRPTR title, CONST_STRPTR body);
BOOL PromptRename(CONST_STRPTR oldName, STRPTR newNameBuf, ULONG bufSize);
BOOL PromptString(CONST_STRPTR title, STRPTR buf, ULONG bufSize);
LONG PromptDialog(CONST_STRPTR title, CONST_STRPTR prompt, STRPTR buf, ULONG bufSize, CONST_STRPTR buttons);
LONG PromptDialogCheck(CONST_STRPTR title, CONST_STRPTR prompt, STRPTR buf, ULONG bufSize,
                       CONST_STRPTR buttons, CONST_STRPTR checkLabel, ULONG *checked);
BOOL RevealPath(CONST_STRPTR path, struct Window *win);
void ShowAbout(struct Window *win);

/* ---- Vista ---- */
void DoToggleView(Object *listGad, Object *toggleGad, struct Window *win);
void SetViewMode(ULONG mode, Object *listGad, Object *toggleGad, struct Window *win);

/* ---- Percorsi e operazioni sui file ---- */
BOOL GetResolvedPath(CONST_STRPTR path, STRPTR out, ULONG outSize);
BOOL CreateDirUnlocked(CONST_STRPTR path);
BOOL IsPathSameOrInside(CONST_STRPTR path, CONST_STRPTR container);
BOOL IsDestInsideSrcForCopy(CONST_STRPTR src, CONST_STRPTR dst);
void CopyAttributes(CONST_STRPTR src, CONST_STRPTR dst);
BOOL CopySingleFile(CONST_STRPTR src, CONST_STRPTR dst);
void FreeCopyBuffer(void);
void CopyInfoFileIfPresent(CONST_STRPTR src, CONST_STRPTR dst);
BOOL CopyDirectoryRecursive(CONST_STRPTR src, CONST_STRPTR dst);
BOOL CopyEntryRecursive(CONST_STRPTR src, CONST_STRPTR dst);
BOOL MoveInfoFile(CONST_STRPTR src, CONST_STRPTR dst);
BOOL MoveEntry(CONST_STRPTR src, CONST_STRPTR dst);
BOOL DeleteEntryRecursive(CONST_STRPTR src, ULONG depth);
void DeleteSiblingInfo(CONST_STRPTR path);
BOOL MakeUniqueDestName(CONST_STRPTR dir, CONST_STRPTR baseName, STRPTR out, ULONG outSize);
BOOL CalculatePathSize(CONST_STRPTR path, int64 *totalBytes, int64 *fileCount, int64 *dirCount);
void DoCalculateSize(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);

/* ---- Conflitti di copia/spostamento ---- */
/* Cosa fare se la destinazione esiste gia'. */
enum {
    CONFLICT_ASK = 0,
    CONFLICT_OVERWRITE = 1,
    CONFLICT_OVERWRITE_ALL = 2,
    CONFLICT_UPDATE = 3,
    CONFLICT_UPDATE_ALL = 4,
    CONFLICT_RENAME = 5,
    CONFLICT_SKIP = 6
};
LONG AskConflict(CONST_STRPTR name);
void ResetCopyConflict(void);

/* ---- Conteggio e avanzamento ---- */
ULONG CountFilesRecursive(CONST_STRPTR src);
BOOL PathIsInfo(CONST_STRPTR path);
void CountTreeRecursive(CONST_STRPTR src, ULONG *files, uint64 *bytes);
ULONG CountDeleteObjects(CONST_STRPTR src);
void ProgressUpdate(void);
void ProgressSetBytes(uint64 total);
void ProgressSetCounts(ULONG done, ULONG total);
void ProgressPoll(void);
void ProgressAdvance(void);
void ProgressSetIndeterminate(BOOL on);
void ProgressSetIndeterminateTotal(ULONG total);
void ProgressSetCancelable(BOOL on);
void ProgressOpen(struct Window *win, ULONG total);
void ProgressClose(void);
void ProgressSetButtons(BOOL on);
void ProgressSetText(CONST_STRPTR text);
void FreeProgressBarClass(void);

/* ---- Azioni su selezione e appunti ---- */
void DoRenameSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void DoNewDrawer(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void DoNewTextFile(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void DoDeleteSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void DoDuplicateSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void DoCopySelected(struct List *list);
void DoCutSelected(struct List *list);
void DoPaste(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void DoTransferToPane(struct List *srcList, CONST_STRPTR srcDir, CONST_STRPTR dstDir, BOOL move, struct Window *win);
void SaveSharedClipboard(void);
BOOL LoadSharedClipboard(void);

/* ---- Plugin e ARexx ---- */
void ScanPlugins(void);
void RunPlugin(CONST_STRPTR path);

/* ---- Drag & drop ---- */
BOOL CollectSelectedFullPaths(struct List *list, char paths[][PATH_MAX_LEN], LONG maxPaths, LONG *count);
BOOL IsVFMWindow(struct Window *w, struct MsgPort **outPort);
void DoDragDropOut(struct List *list, struct Window *win, WORD screenX, WORD screenY);
void PerformDragDrop(struct List *list, struct Window *win);
void ShutdownDragDropInput(void);
void DoInfo(struct List *list, struct Window *win);
BOOL IsPointOverGadget(Object *gad, struct Window *win, WORD x, WORD y);
void HandleAppDrop(struct AppMessage *appmsg, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void StartDragVisual(struct List *list, struct Window *win);
void EndDragVisual(struct Window *win);
void SendVFMDrop(struct List *list, struct MsgPort *port, WORD screenX, WORD screenY);
void HandleVFMDrop(struct VFMDropMessage *drop, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list);
void DragTick(struct Window *win);

/* ---- Aspetto del drag (DragVisual.c) ---- */
BOOL  DragVisualBegin(struct List *list, struct Window *win);
void  DragVisualMove(WORD screenX, WORD screenY);
void  DragVisualSetTarget(struct Screen *scr, const struct IBox *box);
void  DragVisualEnd(void);
BOOL  IsDragOverlayWindow(struct Window *w);
ULONG DragTimerSignal(void);
ULONG AppDropReplySignal(void);
LONG  AskCopyError(CONST_STRPTR name, LONG err);
LONG  AskDeleteError(CONST_STRPTR name, LONG err);
void  AppRegistryEnsure(void);
void  AppRegistryRemove(void);
void  ProcessAppDropReplies(void);
BOOL  DragTimerFired(void);
void  DragTimerRearm(void);

/* ---- Immagini e pulsanti ---- */
Object *LoadIconImage(struct Screen *scr, CONST_STRPTR name);
void LoadMenuImages(struct Screen *scr);
void FreeMenuImages(void);
Object *MakeIconButton(ULONG id, Object *img, Object *imgSel, CONST_STRPTR hint);

/* ---- Menu, segnalibri e preferenze ---- */
void BuildMenu(void);
void UpdateMenu(void);
void BuildContextMenus(void);
void DoOpenWith(struct Window *win, struct List *list);   /* OpenWith.c */
void FreeContextMenus(void);
void SyncContextMenus(void);
void RefreshContextMenus(void);
void ManageBookmarks(Object *mainWinObj);
void AddBookmark(void);
void LoadBookmarks(void);
void SaveBookmarks(void);
BOOL PromptBookmark(STRPTR nameBuf, ULONG nameSize, STRPTR pathBuf, ULONG pathSize);
void LaunchInstance(CONST_STRPTR path, BOOL centerMouse);
void LaunchNewInstance(void);
void LoadPrefs(void);
void SavePrefs(void);
BOOL ApplyPathPrefs(int p, CONST_STRPTR path);
void RememberPathPrefs(CONST_STRPTR path, ULONG view, ULONG filter, ULONG sort,
                       ULONG showinfo, ULONG iconsize);

#endif /* VAGAFILEMANAGER_H */
