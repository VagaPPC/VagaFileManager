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

/*
 * Modulo: VagaFileManager.c
 * -------------------------
 * Nucleo dell'applicazione: variabili globali, preferenze, costruzione dei
 * due pannelli, modalita' split, plugin, porta ARexx, attivazione del
 * pannello e ciclo principale degli eventi.
 */

#define CATCOMP_NUMBERS
#define CATCOMP_BLOCK
#define CATCOMP_CODE
#include "VagaFileManager_strings.h"

#include "VagaFileManager.h"

#include <rexx/storage.h>
#include <rexx/errors.h>
#include <interfaces/rexxsyslib.h>

#include <ctype.h>
#include <libraries/keymap.h>
#include <devices/inputevent.h>
#include <devices/timer.h>

/* Tipo dei messaggi "notify" usati dal canale comandi dei plugin.
   Deve essere diverso da VFMDROP_TYPE (0x5646) e dai valori AMTYPE_*:
   con lo stesso valore i drop VFM venivano scambiati per notifiche. */
#define VFMPLUGIN_TYPE 0x5647

static const char version_tag[] __attribute__((used)) = "\0$VER: VagaFileManager " VFM_VERSION_LONG;

/* Cookie di stack: garantisce almeno 1 MB di stack anche quando VFM viene
   avviato da Shell (dove il default e' molto piu' piccolo dei 256 KB del
   .info).  Evita il crash aprendo cartelle molto profonde. */
static const char stack_cookie[] __attribute__((used)) = "$STACK:1048576";

struct LocaleInfo gLoc;
static struct Library *gLocaleBase = NULL;
static struct LocaleIFace *gLocale = NULL;

/* ---- stato per pannello ---- */
struct Pane gPanes[2];
int gActivePane = 0;

/* hook per pannello (h_Data = indice pannello) */
struct Hook gSortHooks[2];
struct Hook gIconRenderHooks[2];
static struct Hook gIconHooks[2];

/* ---- stato condiviso ---- */
char gClipboard[MAX_CLIPBOARD][PATH_MAX_LEN];
LONG gClipboardCount = 0;
ULONG gClipboardMode = CLIP_NONE;

BOOL  gDragActive = FALSE;
WORD  gDragStartX = 0, gDragStartY = 0;
BOOL  gDragButtonDown = FALSE;

static volatile BOOL gTabSwitchRequest = FALSE;

/* Menu contestuale: l'hook (contesto input.device) annota qui il pannello
   e la posizione visibile sotto il mouse; il loop principale li legge
   quando arriva la scelta.  gCtxMenuBlocked disattiva l'hook mentre i
   gadget delle liste vengono creati o distrutti (ToggleSplit). */
struct CtxMenuInfo
{
    int     pane;
    LONG    pos;      /* posizione visibile nella lista, -1 = spazio vuoto */
    Object *menu;     /* menu restituito a Intuition */
};
static struct CtxMenuInfo gCtxInfo;
static struct Hook gCtxMenuHook;
static volatile BOOL gCtxMenuBlocked = FALSE;
static struct Hook gKeyIDCMPHook;

char gDropPortName[64];
char gPluginDir[PATH_MAX_LEN];

/* Sessioni dei plugin: ogni lancio di un plugin Python riceve una
   sottocartella propria del canale (T:VFM_<task>/s<n>) legata al
   pannello da cui e' partito.  I comandi che arrivano da quella cartella
   agiscono sempre su quel pannello, anche se nel frattempo l'utente ha
   attivato l'altro.  Alla fine del processo DOS ci risponde con un
   DeathMessage e la cartella viene cancellata. */
#define MAX_PLUGIN_SESSIONS 16
struct PluginSession
{
    BOOL   used;
    int    pane;
    ULONG  paneGen;              /* gPane1Gen al lancio (solo pannello 1) */
    char   dir[PATH_MAX_LEN];
    struct DeathMessage *dm;     /* MEMF_SHARED, riusato fra i lanci */
};
static struct PluginSession gSessions[MAX_PLUGIN_SESSIONS];
static struct MsgPort *gSessionPort = NULL;
static ULONG gSessionSeq = 0;
/* Incrementato a ogni apertura del pannello 1: se il pannello viene
   chiuso e riaperto, le sessioni legate a quello vecchio non valgono piu'. */
static ULONG gPane1Gen = 0;
/* Emulazione WB: vista del pannello prima di attivarla (-1 = nessuna),
   ripristinata quando la si disattiva. */
static LONG gPreEmuView = -1;

/* Cartella della sessione che sta eseguendo un comando (o NULL). */
static CONST_STRPTR gCurSessionDir = NULL;

struct Bookmark gBookmarks[MAX_BOOKMARKS];
LONG gBookmarkCount = 0;
struct VFMWindowTag gWinTag;

char gPlugins[MAX_PLUGINS][PATH_MAX_LEN];
LONG gPluginCount = 0;

static struct MsgPort *gRexxPort = NULL;
char gRexxPortName[64] = REXX_PORT_NAME;
static struct Library *gRexxBase = NULL;
static struct RexxSysIFace *gIRexx = NULL;
static volatile BOOL gRexxQuit = FALSE;

volatile BOOL gProgActive = FALSE;
volatile BOOL gProgCancel = FALSE;
volatile BOOL gProgPause  = FALSE;
Object *gProgWinObj = NULL;
Object *gProgPauseBtn = NULL;
struct Window *gProgWin = NULL;
ULONG gProgDone  = 0;
uint64 gProgBytesDone  = 0;
uint64 gProgBytesTotal = 0;

CONST_STRPTR gIconNames[IMG_COUNT] =
{
    "arrowup", "open", "refresh", "home", "arrowleft", "arrowright",
    "drawers", "listview",
    "rename", "delete", "duplicate", "copy", "cut", "paste", "info",
    "filter", "sortingandgrouping", "filesize", "shell", "split",
    "clearlister"
};

Object *gBtnImg[IMG_COUNT];
Object *gBtnImgSel[IMG_COUNT];

Object *gMainWinObj = NULL;

/* ---- impostazioni persistenti ---- */
struct VFMPrefs gPrefs;
LONG gWinInnerW = DEFAULT_WIN_WIDTH;
LONG gWinInnerH = DEFAULT_WIN_HEIGHT;

/* contesto UI usato dalla divisione in due pannelli */
static Class *gFbClass = NULL;
static Object *gRootLayout = NULL;
static Object *gPanesRow = NULL;
static Object *gTopBar = NULL;
static Object *gTopCopyBtn = NULL, *gTopMoveBtn = NULL, *gTopDeleteBtn = NULL;
static struct Screen *gPubScreen = NULL;

/* ===== Log =====
   T:VagaFileManager.log riceve sempre gli errori (LOGERR).  I messaggi di
   diagnostica (DBG) vengono scritti solo con il ToolType DEBUG nell'icona
   di VagaFileManager (DEBUG o DEBUG=TRUE; DEBUG=FALSE li disattiva). */

BOOL gDebugLog = FALSE;

static void LogWrite(CONST_STRPTR prefix, CONST_STRPTR fmt, va_list args)
{
    BPTR fh;
    char buf[1024];

    vsnprintf(buf, sizeof(buf), fmt, args);

    fh = IDOS->Open("T:VagaFileManager.log", MODE_READWRITE);
    if (!fh)
        fh = IDOS->Open("T:VagaFileManager.log", MODE_NEWFILE);
    if (fh)
    {
        IDOS->ChangeFilePosition(fh, 0, OFFSET_END);
        IDOS->FPrintf(fh, "%s%s\n", prefix, buf);
        IDOS->Close(fh);
    }
}

void DBG(CONST_STRPTR fmt, ...)
{
    va_list args;
    if (!gDebugLog) return;
    va_start(args, fmt);
    LogWrite("", fmt, args);
    va_end(args);
}

void LOGERR(CONST_STRPTR fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogWrite("ERROR: ", fmt, args);
    va_end(args);
}

/* Legge il ToolType DEBUG dall'icona del programma (avvio da Workbench:
   argv e' il WBStartup; da Shell: icona accanto all'eseguibile). */
static void ReadDebugToolType(int argc, char **argv)
{
    struct DiskObject *dob = NULL;
    BPTR old;

    if (!IIcon) return;
    if (argc == 0 && argv)
    {
        struct WBStartup *wbs = (struct WBStartup *)argv;
        if (wbs->sm_NumArgs > 0 && wbs->sm_ArgList)
        {
            old = IDOS->SetCurrentDir(wbs->sm_ArgList[0].wa_Lock);
            dob = IIcon->GetDiskObject(wbs->sm_ArgList[0].wa_Name);
            IDOS->SetCurrentDir(old);
        }
    }
    else
    {
        char name[256];
        if (IDOS->GetCliProgramName(name, sizeof(name)) && IDOS->GetProgramDir())
        {
            old = IDOS->SetCurrentDir(IDOS->GetProgramDir());
            dob = IIcon->GetDiskObject(IDOS->FilePart(name));
            IDOS->SetCurrentDir(old);
        }
    }
    if (!dob) return;
    {
        STRPTR v = IIcon->FindToolType((STRPTR *)dob->do_ToolTypes, "DEBUG");
        if (v)
            gDebugLog = (IIcon->MatchToolValue(v, "FALSE") || IIcon->MatchToolValue(v, "NO") ||
                         IIcon->MatchToolValue(v, "OFF") || IIcon->MatchToolValue(v, "0"))
                        ? FALSE : TRUE;
    }
    IIcon->FreeDiskObject(dob);
}

/* ===== Inizializzazione e preferenze ===== */

static void PaneInit(int p, CONST_STRPTR path)
{
    struct Pane *pn = &gPanes[p];

    IExec->NewList(&pn->fileList);
    pn->listGad = pn->pathGad = pn->statusGad = pn->toggleGad = NULL;
    pn->filterGad = NULL;
    pn->sizeGad = NULL;
    pn->btnRename = pn->btnDelete = pn->btnDuplicate = pn->btnCopy = NULL;
    pn->btnCut = pn->btnPaste = pn->btnInfo = NULL;
    pn->paneLayout = NULL;
    pn->colInfo = NULL;
    pn->pathBuffer[0] = '\0';
    pn->filterBuffer[0] = '\0';
    pn->filterApplied[0] = '\0';
    pn->filterSeen[0] = '\0';
    pn->filterChangeTicks = 0;
    if (path)
    {
        strncpy(pn->pathBuffer, path, PATH_MAX_LEN - 1);
        pn->pathBuffer[PATH_MAX_LEN - 1] = '\0';
    }
    pn->winTitle[0] = '\0';
    pn->entryCount = 0;
    memset(pn->icons, 0, sizeof(pn->icons));
    memset(pn->isDir, 0, sizeof(pn->isDir));
    pn->forwardCount = 0;
    pn->viewFilter = VIEW_FILTER_ICONS_ONLY;
    pn->sortMode = SORT_BY_TYPE;
    pn->viewMode = FBVM_ICONS;
    pn->showInfoFiles = 0;
    pn->iconSize = 0;
    pn->lastRefreshTicks = 0;
    memset(&pn->notify, 0, sizeof(pn->notify));
    pn->notifyActive = FALSE;
    pn->notifyPath[0] = '\0';
}

#define MAX_PATH_PREFS 256
#define MAX_VOLUME_GEOM 64
#define VOLUME_KEY_LEN 64

/* Scostamento usato quando una nuova istanza viene centrata sul mouse,
   per non sovrapporla esattamente alla finestra da cui e' partita. */
#define MOUSE_OFFSET_X 24
#define MOUSE_OFFSET_Y 24

struct PathPrefs
{
    char  path[PATH_MAX_LEN];
    ULONG viewMode, viewFilter, sortMode, showInfoFiles;
    ULONG iconSize;
};

/* Geometria della finestra memorizzata per volume.  Ogni istanza usa il
   volume su cui e' stata aperta: navigare non cambia la geometria. */
struct VolumeGeom
{
    char  volume[VOLUME_KEY_LEN];
    LONG  width, height;
    LONG  left, top;
    BOOL  hasPos;
};

static struct PathPrefs gPathPrefs[MAX_PATH_PREFS];
static LONG gPathPrefsCount = 0;
static struct VolumeGeom gVolumeGeom[MAX_VOLUME_GEOM];
static LONG gVolumeGeomCount = 0;

static char gInstanceVolume[VOLUME_KEY_LEN] = "";
static LONG gWinLeft = 0, gWinTop = 0;
static BOOL gWinPosValid = FALSE;
static BOOL gWinGeomCaptured = FALSE;

/* Estrae la chiave del volume da un percorso: "Work:Projects" -> "Work".
   La vista radice (percorso vuoto) usa "-".  Gli assign sono trattati
   come volumi separati (la chiave e' il loro nome). */
static void GetVolumeKey(CONST_STRPTR path, STRPTR out, ULONG outSize)
{
    CONST_STRPTR colon;
    ULONG n;

    if (!out || outSize == 0) return;
    if (!path || !path[0])
    {
        strncpy(out, "-", outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }
    colon = strchr(path, ':');
    if (colon)
    {
        n = (ULONG)(colon - path);
        if (n >= outSize) n = outSize - 1;
        memcpy(out, path, n);
        out[n] = '\0';
    }
    else
    {
        strncpy(out, path, outSize - 1);
        out[outSize - 1] = '\0';
    }
}

static void ClampWindowSize(LONG *w, LONG *h)
{
    if (*w < 400)  *w = 400;
    if (*w > 4096) *w = 4096;
    if (*h < 300)  *h = 300;
    if (*h > 4096) *h = 4096;
}

static struct VolumeGeom *FindVolumeGeom(struct VolumeGeom *vols, LONG count, CONST_STRPTR key)
{
    LONG i;
    for (i = 0; i < count; i++)
        if (!IUtility->Stricmp(vols[i].volume, key))
            return &vols[i];
    return NULL;
}

static void StoreVolumeGeom(struct VolumeGeom *vols, LONG *count, CONST_STRPTR key,
                            LONG w, LONG h, LONG left, LONG top, BOOL hasPos)
{
    struct VolumeGeom *vg = FindVolumeGeom(vols, *count, key);

    if (!vg)
    {
        if (*count >= MAX_VOLUME_GEOM) return;
        vg = &vols[(*count)++];
        memset(vg, 0, sizeof(*vg));
        strncpy(vg->volume, key, VOLUME_KEY_LEN - 1);
        vg->volume[VOLUME_KEY_LEN - 1] = '\0';
    }
    vg->width  = w;
    vg->height = h;
    vg->left   = left;
    vg->top    = top;
    vg->hasPos = hasPos;
}

static void ApplyVolumeGeom(CONST_STRPTR key)
{
    struct VolumeGeom *vg = FindVolumeGeom(gVolumeGeom, gVolumeGeomCount, key);

    if (!vg) return;
    gWinInnerW = vg->width;
    gWinInnerH = vg->height;
    if (vg->hasPos)
    {
        gWinLeft = vg->left;
        gWinTop  = vg->top;
        gWinPosValid = TRUE;
    }
}

static void PrefsDefaults(void)
{
    memset(&gPrefs, 0, sizeof(gPrefs));
    gPrefs.width  = DEFAULT_WIN_WIDTH;
    gPrefs.height = DEFAULT_WIN_HEIGHT;
    gPathPrefsCount = 0;
    gVolumeGeomCount = 0;
    gInstanceVolume[0] = '\0';
    gWinLeft = gWinTop = 0;
    gWinPosValid = FALSE;
    gWinGeomCaptured = FALSE;
}

static void CaptureWindowSize(struct Window *win)
{
    if (!win) return;
    gWinInnerW = win->Width  - win->BorderLeft - win->BorderRight;
    gWinInnerH = win->Height - win->BorderTop  - win->BorderBottom;
    gWinLeft = win->LeftEdge;
    gWinTop  = win->TopEdge;
    gWinPosValid = TRUE;
    gWinGeomCaptured = TRUE;
}

/* Limita la geometria alla schermata pubblica corrente. */
static void ClampWindowGeom(void)
{
    LONG sw, sh;

    if (!gPubScreen) return;
    sw = gPubScreen->Width;
    sh = gPubScreen->Height;

    if (gWinInnerW > sw - 40) gWinInnerW = sw - 40;
    if (gWinInnerH > sh - 40) gWinInnerH = sh - 40;
    ClampWindowSize(&gWinInnerW, &gWinInnerH);

    if (gWinPosValid)
    {
        if (gWinLeft < 0) gWinLeft = 0;
        if (gWinTop < 0)  gWinTop = 0;
        if (gWinLeft + gWinInnerW > sw) gWinLeft = sw - gWinInnerW;
        if (gWinTop + gWinInnerH > sh)  gWinTop = sh - gWinInnerH;
        if (gWinLeft < 0) gWinLeft = 0;
        if (gWinTop < 0)  gWinTop = 0;
    }
}

/* Legge il file delle preferenze in strutture fornite dal chiamante.
   Usata sia all'avvio sia prima del salvataggio (per fondere i dati di
   altre istanze in esecuzione). */
static void ParsePrefs(CONST_STRPTR file,
                       struct VFMPrefs *prefs,
                       struct PathPrefs *paths, LONG *pathCount,
                       struct VolumeGeom *vols, LONG *volCount)
{
    BPTR fh;
    char line[PATH_MAX_LEN + 64];
    struct PathPrefs *cur = NULL;
    struct VolumeGeom *cvol = NULL;
    BOOL inPath = FALSE, inVolume = FALSE;
    LONG i, out;

    *pathCount = 0;
    *volCount = 0;

    fh = IDOS->Open((STRPTR)file, MODE_OLDFILE);
    if (fh)
    {
        while (IDOS->FGets(fh, line, sizeof(line)))
        {
            LONG len = (LONG)strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';

            if (!strncmp(line, "[Path]", 6))
            {
                inPath = TRUE; inVolume = FALSE;
                cur = NULL; cvol = NULL;
                if (*pathCount < MAX_PATH_PREFS)
                {
                    cur = &paths[(*pathCount)++];
                    memset(cur, 0, sizeof(*cur));
                    cur->viewMode      = FBVM_ICONS;
                    cur->viewFilter    = VIEW_FILTER_ICONS_ONLY;
                    cur->sortMode      = SORT_BY_TYPE;
                    cur->showInfoFiles = 0;
                    cur->iconSize      = 0;
                }
            }
            else if (!strncmp(line, "[Volume]", 8))
            {
                inPath = FALSE; inVolume = TRUE;
                cur = NULL; cvol = NULL;
                if (*volCount < MAX_VOLUME_GEOM)
                {
                    cvol = &vols[(*volCount)++];
                    memset(cvol, 0, sizeof(*cvol));
                }
            }
            else if (inPath && cur && !strncmp(line, "Path=", 5))
            {
                strncpy(cur->path, line + 5, PATH_MAX_LEN - 1);
                cur->path[PATH_MAX_LEN - 1] = '\0';
            }
            else if (inPath && cur && !strncmp(line, "View=", 5))
            {
                cur->viewMode = (atoi(line + 5) == FBVM_ICONS) ? FBVM_ICONS : FBVM_DETAILS;
            }
            else if (inPath && cur && !strncmp(line, "Filter=", 7))
            {
                cur->viewFilter = (atoi(line + 7) == VIEW_FILTER_ALL_FILES) ? VIEW_FILTER_ALL_FILES : VIEW_FILTER_ICONS_ONLY;
            }
            else if (inPath && cur && !strncmp(line, "Sort=", 5))
            {
                LONG v = atoi(line + 5);
                cur->sortMode = (v >= SORT_BY_TYPE && v <= SORT_BY_SIZE) ? (ULONG)v : SORT_BY_TYPE;
            }
            else if (inPath && cur && !strncmp(line, "ShowInfo=", 9))
            {
                cur->showInfoFiles = atoi(line + 9) ? 1 : 0;
            }
            else if (inPath && cur && !strncmp(line, "IconSize=", 9))
            {
                LONG v = atoi(line + 9);
                cur->iconSize = (v > 0 && v <= 256) ? (ULONG)v : 0;
            }
            else if (inVolume && cvol && !strncmp(line, "Name=", 5))
            {
                strncpy(cvol->volume, line + 5, VOLUME_KEY_LEN - 1);
                cvol->volume[VOLUME_KEY_LEN - 1] = '\0';
            }
            else if (inVolume && cvol && !strncmp(line, "Width=", 6))
                cvol->width = atoi(line + 6);
            else if (inVolume && cvol && !strncmp(line, "Height=", 7))
                cvol->height = atoi(line + 7);
            else if (inVolume && cvol && !strncmp(line, "Left=", 5))
                cvol->left = atoi(line + 5);
            else if (inVolume && cvol && !strncmp(line, "Top=", 4))
                cvol->top = atoi(line + 4);
            else if (inVolume && cvol && !strncmp(line, "HasPos=", 7))
                cvol->hasPos = atoi(line + 7) ? TRUE : FALSE;
            else if (!inPath && !inVolume && !strncmp(line, "Width=", 6))
                prefs->width = atoi(line + 6);
            else if (!inPath && !inVolume && !strncmp(line, "Height=", 7))
                prefs->height = atoi(line + 7);
            else if (!inPath && !inVolume && !strncmp(line, "Split=", 6))
                prefs->split = atoi(line + 6) ? 1 : 0;
            else if (!inPath && !inVolume && !strncmp(line, "WBEmulation=", 12))
                prefs->wbEmulation = atoi(line + 12) ? 1 : 0;
            else if (!inPath && !inVolume && !strncmp(line, "HiddenBars=", 11))
                prefs->hiddenBars = (ULONG)atoi(line + 11) &
                                    (BAR_PATH | BAR_TOOLS | BAR_FILTER | BAR_STATUS);
        }
        IDOS->Close(fh);
    }

    /* scarta le sezioni senza percorso */
    out = 0;
    for (i = 0; i < *pathCount; i++)
    {
        if (paths[i].path[0])
        {
            if (out != i) paths[out] = paths[i];
            out++;
        }
    }
    *pathCount = out;

    /* scarta le sezioni senza nome e limita le dimensioni */
    out = 0;
    for (i = 0; i < *volCount; i++)
    {
        if (!vols[i].volume[0]) continue;
        ClampWindowSize(&vols[i].width, &vols[i].height);
        if (out != i) vols[out] = vols[i];
        out++;
    }
    *volCount = out;

    ClampWindowSize(&prefs->width, &prefs->height);
}

void LoadPrefs(void)
{
    PrefsDefaults();

    ParsePrefs(PREFS_FILE, &gPrefs, gPathPrefs, &gPathPrefsCount,
               gVolumeGeom, &gVolumeGeomCount);

    gWinInnerW = gPrefs.width;
    gWinInnerH = gPrefs.height;
}

BOOL ApplyPathPrefs(int p, CONST_STRPTR path)
{
    LONG i;

    if (p < 0 || p > 1 || !path || !path[0]) return FALSE;

    for (i = 0; i < gPathPrefsCount; i++)
    {
        if (!IUtility->Stricmp(gPathPrefs[i].path, path))
        {
            gPanes[p].viewMode      = gPathPrefs[i].viewMode;
            gPanes[p].viewFilter    = gPathPrefs[i].viewFilter;
            gPanes[p].sortMode      = gPathPrefs[i].sortMode;
            gPanes[p].showInfoFiles = gPathPrefs[i].showInfoFiles;
            gPanes[p].iconSize      = gPathPrefs[i].iconSize;
            return TRUE;
        }
    }
    return FALSE;
}

void RememberPathPrefs(CONST_STRPTR path, ULONG view, ULONG filter, ULONG sort,
                       ULONG showinfo, ULONG iconsize)
{
    struct PathPrefs *pp = NULL;
    LONG i;

    if (!path || !path[0]) return;

    for (i = 0; i < gPathPrefsCount; i++)
    {
        if (!IUtility->Stricmp(gPathPrefs[i].path, path))
        {
            pp = &gPathPrefs[i];
            break;
        }
    }

    /* Emulazione WB: la vista (sempre icone) e' forzata, non scelta: non
       deve sostituire quella ricordata per il cassetto, ne' creare voci
       nuove che dopo l'emulazione aprirebbero tutto a icone. */
    if (gPrefs.wbEmulation)
    {
        if (!pp) return;
        view   = pp->viewMode;
        filter = pp->viewFilter;
    }

    if (!pp)
    {
        if (gPathPrefsCount >= MAX_PATH_PREFS) return;
        pp = &gPathPrefs[gPathPrefsCount++];
        memset(pp, 0, sizeof(*pp));
        strncpy(pp->path, path, PATH_MAX_LEN - 1);
        pp->path[PATH_MAX_LEN - 1] = '\0';
    }

    pp->viewMode      = view;
    pp->viewFilter    = filter;
    pp->sortMode      = sort;
    pp->showInfoFiles = showinfo;
    pp->iconSize      = iconsize;
}

void SavePrefs(void)
{
    BPTR fh;
    struct Window *win = NULL;
    struct VFMPrefs diskPrefs;
    struct PathPrefs *diskPaths = NULL;
    struct VolumeGeom *diskVols = NULL;
    LONG diskPathCount = 0, diskVolCount = 0;
    BOOL merged = FALSE;
    LONG i, j;

    if (gMainWinObj)
        IIntuition->GetAttr(WINDOW_Window, gMainWinObj, (ULONG *)&win);
    if (win) CaptureWindowSize(win);

    ClampWindowSize(&gWinInnerW, &gWinInnerH);

    gPrefs.width  = gWinInnerW;
    gPrefs.height = gWinInnerH;
    gPrefs.split  = gPanes[1].paneLayout ? 1 : 0;

    RememberPathPrefs(gPanes[0].pathBuffer, gPanes[0].viewMode, gPanes[0].viewFilter,
                      gPanes[0].sortMode, gPanes[0].showInfoFiles, gPanes[0].iconSize);
    if (gPanes[1].paneLayout)
        RememberPathPrefs(gPanes[1].pathBuffer, gPanes[1].viewMode, gPanes[1].viewFilter,
                          gPanes[1].sortMode, gPanes[1].showInfoFiles, gPanes[1].iconSize);

    /* Rilegge il file per fondere i dati di altre istanze in esecuzione. */
    diskPaths = (struct PathPrefs *)IExec->AllocVecTags(
        MAX_PATH_PREFS * sizeof(struct PathPrefs),
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    diskVols = (struct VolumeGeom *)IExec->AllocVecTags(
        MAX_VOLUME_GEOM * sizeof(struct VolumeGeom),
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);

    if (diskPaths && diskVols)
    {
        memset(&diskPrefs, 0, sizeof(diskPrefs));
        ParsePrefs(PREFS_FILE, &diskPrefs, diskPaths, &diskPathCount,
                   diskVols, &diskVolCount);

        /* I valori globali di questa istanza vincono. */
        diskPrefs.width  = gPrefs.width;
        diskPrefs.height = gPrefs.height;
        diskPrefs.split  = gPrefs.split;
        diskPrefs.hiddenBars = gPrefs.hiddenBars;
        diskPrefs.wbEmulation = gPrefs.wbEmulation;

        /* La geometria vale per il volume su cui l'istanza e' stata
           aperta, non per l'ultima cartella visitata. */
        if (gWinGeomCaptured && gInstanceVolume[0])
            StoreVolumeGeom(diskVols, &diskVolCount, gInstanceVolume,
                            gWinInnerW, gWinInnerH, gWinLeft, gWinTop, gWinPosValid);

        /* Unisce le preferenze di vista per percorso di questa istanza. */
        for (i = 0; i < gPathPrefsCount; i++)
        {
            if (!gPathPrefs[i].path[0]) continue;
            for (j = 0; j < diskPathCount; j++)
                if (!IUtility->Stricmp(diskPaths[j].path, gPathPrefs[i].path))
                    break;
            if (j < diskPathCount)
                diskPaths[j] = gPathPrefs[i];
            else if (diskPathCount < MAX_PATH_PREFS)
                diskPaths[diskPathCount++] = gPathPrefs[i];
        }

        merged = TRUE;
    }

    fh = IDOS->Open(PREFS_FILE, MODE_NEWFILE);
    if (fh)
    {
        const struct VFMPrefs *wp = merged ? &diskPrefs : &gPrefs;
        const struct PathPrefs *pa = merged ? diskPaths : gPathPrefs;
        const struct VolumeGeom *va = merged ? diskVols : gVolumeGeom;
        LONG paCount = merged ? diskPathCount : gPathPrefsCount;
        LONG vaCount = merged ? diskVolCount : gVolumeGeomCount;

        IDOS->FPrintf(fh, "Width=%ld\n",  (long)wp->width);
        IDOS->FPrintf(fh, "Height=%ld\n", (long)wp->height);
        IDOS->FPrintf(fh, "Split=%ld\n",  (long)wp->split);
        IDOS->FPrintf(fh, "HiddenBars=%ld\n", (long)wp->hiddenBars);
        IDOS->FPrintf(fh, "WBEmulation=%ld\n", (long)wp->wbEmulation);

        for (i = 0; i < vaCount; i++)
        {
            if (!va[i].volume[0]) continue;
            IDOS->FPrintf(fh, "[Volume]\n");
            IDOS->FPrintf(fh, "Name=%s\n",     va[i].volume);
            IDOS->FPrintf(fh, "Width=%ld\n",   (long)va[i].width);
            IDOS->FPrintf(fh, "Height=%ld\n",  (long)va[i].height);
            IDOS->FPrintf(fh, "Left=%ld\n",    (long)va[i].left);
            IDOS->FPrintf(fh, "Top=%ld\n",     (long)va[i].top);
            IDOS->FPrintf(fh, "HasPos=%ld\n",  (long)(va[i].hasPos ? 1 : 0));
        }

        for (i = 0; i < paCount; i++)
        {
            if (!pa[i].path[0]) continue;
            IDOS->FPrintf(fh, "[Path]\n");
            IDOS->FPrintf(fh, "Path=%s\n",      pa[i].path);
            IDOS->FPrintf(fh, "View=%ld\n",     (long)pa[i].viewMode);
            IDOS->FPrintf(fh, "Filter=%ld\n",   (long)pa[i].viewFilter);
            IDOS->FPrintf(fh, "Sort=%ld\n",     (long)pa[i].sortMode);
            IDOS->FPrintf(fh, "ShowInfo=%ld\n", (long)pa[i].showInfoFiles);
            IDOS->FPrintf(fh, "IconSize=%ld\n", (long)pa[i].iconSize);
        }

        IDOS->Close(fh);
    }

    if (diskPaths) IExec->FreeVec(diskPaths);
    if (diskVols) IExec->FreeVec(diskVols);
}

/* ===== Costruzione dell'interfaccia dei pannelli =====
   Costruisce i gadget e il layout di un pannello: lista, percorso,
   pulsanti, filtro e barra di stato. */
static Object *CreatePaneUI(int p)
{
    struct Pane *pn = &gPanes[p];
    ULONG off = (ULONG)p * PANE_ID_OFFSET;
    Object *pathRow = NULL, *toolRow = NULL, *filterRow = NULL;

    pn->colInfo = IListBrowser->AllocLBColumnInfo(NUM_COLS,
        LBCIA_Column, COL_ICON, LBCIA_Title, (ULONG)"", LBCIA_Width, 20,
        LBCIA_Column, COL_NAME, LBCIA_Title, (ULONG)LS(MSG_COL_NAME), LBCIA_Weight, 45,
            LBCIA_Sortable, TRUE, LBCIA_AutoSort, TRUE, LBCIA_SortArrow, TRUE, LBCIA_CompareHook, (ULONG)&gSortHooks[p],
        LBCIA_Column, COL_TYPE, LBCIA_Title, (ULONG)LS(MSG_COL_TYPE), LBCIA_Weight, 15,
            LBCIA_Sortable, TRUE, LBCIA_AutoSort, TRUE, LBCIA_SortArrow, TRUE, LBCIA_CompareHook, (ULONG)&gSortHooks[p],
        LBCIA_Column, COL_SIZE, LBCIA_Title, (ULONG)LS(MSG_COL_SIZE), LBCIA_Weight, 15,
            LBCIA_Sortable, TRUE, LBCIA_AutoSort, TRUE, LBCIA_SortArrow, TRUE, LBCIA_CompareHook, (ULONG)&gSortHooks[p],
        LBCIA_Column, COL_DATE, LBCIA_Title, (ULONG)LS(MSG_COL_DATE), LBCIA_Weight, 25,
            LBCIA_Sortable, TRUE, LBCIA_AutoSort, TRUE, LBCIA_SortArrow, TRUE, LBCIA_CompareHook, (ULONG)&gSortHooks[p],
        LBCIA_Column, COL_COMMENT, LBCIA_Title, (ULONG)LS(MSG_COL_COMMENT), LBCIA_Weight, 20,
            LBCIA_Sortable, TRUE, LBCIA_AutoSort, TRUE, LBCIA_SortArrow, TRUE, LBCIA_CompareHook, (ULONG)&gSortHooks[p],
        TAG_DONE);

    /* Le barre nascoste (Vista > Vista barra) non vengono create: i loro
       gadget restano NULL e il resto del codice li tratta come assenti.
       Nessun gadget esiste fuori dal layout, quindi nessun aggiornamento
       puo' disegnarlo in una posizione vecchia. */
    pn->pathGad = pn->statusGad = pn->filterGad = pn->toggleGad = NULL;
    pn->btnRename = pn->btnDelete = pn->btnDuplicate = pn->btnCopy = NULL;
    pn->btnCut = pn->btnPaste = pn->btnInfo = NULL;
    pn->sizeGad = NULL;

    if (BarVisible(BAR_PATH))
    pn->pathGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_PATH + off, GA_RelVerify, TRUE, GA_TabCycle, FALSE,
        STRINGA_MaxChars, PATH_MAX_LEN - 1, STRINGA_TextVal, (ULONG)pn->pathBuffer,
        TAG_DONE);
    /* No STRINGA_Buffer = pn->pathBuffer: the gadget needs a buffer of its
       own. Otherwise the root view label, or a path typed but not yet
       confirmed with Return, would end up in gPathBuffer, and IsRootPath()
       and every file operation would act on a different path from the one
       shown in the list. */

    if (BarVisible(BAR_STATUS))
    pn->statusGad = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_STATUS + off, GA_ReadOnly, TRUE, GA_Text, (ULONG)LS(MSG_STATUS_EMPTY),
        GA_Underscore, 0, BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE);

    if (BarVisible(BAR_FILTER))
    pn->filterGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_FILTER + off, GA_RelVerify, TRUE, GA_TabCycle, FALSE,
        STRINGA_MaxChars, NAME_MAX_LEN - 1,
        STRINGA_TextVal, (ULONG)pn->filterBuffer,
        STRINGA_Buffer, (ULONG)pn->filterBuffer,
        /* Il work buffer e' il testo mentre si digita: cosi' il filtro
           puo' essere applicato a ogni Intuition tick, senza Invio. */
        STRINGA_WorkBuffer, (ULONG)pn->filterBuffer,
        ICA_TARGET, ICTARGET_IDCMP,
        TAG_DONE);

    pn->listGad = IIntuition->NewObject(gFbClass, NULL,
        GA_ID, GID_LIST + off, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&pn->fileList, LISTBROWSER_ColumnInfo, (ULONG)pn->colInfo,
        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_TitleClickable, TRUE,
        LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_MultiSelect, TRUE,
        LISTBROWSER_AllowNoSelection, TRUE,
        FILEBROWSER_ViewMode, pn->viewMode, FILEBROWSER_IconSize, pn->iconSize,
        FILEBROWSER_IconHook, (ULONG)&gIconHooks[p],
        FILEBROWSER_HiddenColumns, (1UL << COL_TYPE) | (1UL << COL_SIZE) | (1UL << COL_DATE) | (1UL << COL_COMMENT),
        TAG_DONE);

    /* barra del percorso */
    if (BarVisible(BAR_PATH))
    {
    pn->toggleGad = MakeIconButton(GID_TOGGLE_VIEW + off,
        (pn->viewMode == FBVM_ICONS) ? gBtnImg[IMG_DETAILS] : gBtnImg[IMG_ICONS],
        (pn->viewMode == FBVM_ICONS) ? gBtnImgSel[IMG_DETAILS] : gBtnImgSel[IMG_ICONS],
        (pn->viewMode == FBVM_ICONS) ? LS(MSG_BTN_DETAILS) : LS(MSG_BTN_ICONS));

    pathRow = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_HOME + off, gBtnImg[IMG_HOME], gBtnImgSel[IMG_HOME], LS(MSG_BTN_HOME)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)pn->toggleGad, CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_BACK + off, gBtnImg[IMG_BACK], gBtnImgSel[IMG_BACK], LS(MSG_BTN_BACK)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_FORWARD + off, gBtnImg[IMG_FORWARD], gBtnImgSel[IMG_FORWARD], LS(MSG_BTN_FORWARD)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_REFRESH + off, gBtnImg[IMG_REFRESH], gBtnImgSel[IMG_REFRESH], LS(MSG_BTN_REFRESH)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)pn->pathGad, CHILD_WeightedWidth, 100,
        TAG_DONE);
    }

    /* barra operazioni; "Calcola dimensione" e "Dividi" */
    if (BarVisible(BAR_TOOLS))
    {
        Object *btnRename = MakeIconButton(GID_RENAME + off, gBtnImg[IMG_RENAME], gBtnImgSel[IMG_RENAME], LS(MSG_RENAME));
        Object *btnDelete = MakeIconButton(GID_DELETE + off, gBtnImg[IMG_DELETE], gBtnImgSel[IMG_DELETE], LS(MSG_DELETE));
        Object *btnDuplicate = MakeIconButton(GID_DUPLICATE + off, gBtnImg[IMG_DUPLICATE], gBtnImgSel[IMG_DUPLICATE], LS(MSG_DUPLICATE));
        Object *btnCopy = MakeIconButton(GID_COPY + off, gBtnImg[IMG_COPY], gBtnImgSel[IMG_COPY], LS(MSG_COPY));
        Object *btnCut = MakeIconButton(GID_CUT + off, gBtnImg[IMG_CUT], gBtnImgSel[IMG_CUT], LS(MSG_CUT));
        Object *btnPaste = MakeIconButton(GID_PASTE + off, gBtnImg[IMG_PASTE], gBtnImgSel[IMG_PASTE], LS(MSG_PASTE));
        Object *btnInfo = MakeIconButton(GID_INFO + off, gBtnImg[IMG_INFO], gBtnImgSel[IMG_INFO], LS(MSG_INFO));

        /* Incolla nasce ghostato: UpdateStatus lo abilita solo se il
           clipboard condiviso contiene almeno un percorso valido. */
        IIntuition->SetAttrs(btnPaste, GA_Disabled, TRUE, TAG_DONE);
        Object *sizeBtn = MakeIconButton(GID_CALCSIZE + off, gBtnImg[IMG_SIZE], gBtnImgSel[IMG_SIZE],
                                         LS(MSG_CALC_SIZE));
        Object *shellBtn = MakeIconButton(GID_SHELL + off, gBtnImg[IMG_SHELL], gBtnImgSel[IMG_SHELL],
                                          LS(MSG_BTN_SHELL_HINT));
        Object *splitBtn = (p == 0)
            ? MakeIconButton(GID_SPLIT, gBtnImg[IMG_SPLIT], gBtnImgSel[IMG_SPLIT],
                             LS(MSG_BTN_SPLIT_HINT))
            : IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE);

        pn->btnRename = btnRename;
        pn->btnDelete = btnDelete;
        pn->btnDuplicate = btnDuplicate;
        pn->btnCopy = btnCopy;
        pn->btnCut = btnCut;
        pn->btnPaste = btnPaste;
        pn->btnInfo = btnInfo;
        pn->sizeGad = sizeBtn;

        toolRow = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)btnRename, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)btnDelete, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)btnDuplicate, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)btnCopy, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)btnCut, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)btnPaste, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)btnInfo, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)sizeBtn, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)shellBtn, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)splitBtn, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE), CHILD_WeightedWidth, 100,
            TAG_DONE);
    }

    if (BarVisible(BAR_FILTER))
    {
        Object *filterLabel = IIntuition->NewObject(NULL, "button.gadget",
            GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (ULONG)LS(MSG_FILTER),
            BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE);

        /* Filtro: [svuota] [testo] */
        filterRow = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)filterLabel, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)MakeIconButton(GID_CLEAR_FILTER + off,
                gBtnImg[IMG_CLEARFILTER], gBtnImgSel[IMG_CLEARFILTER],
                LS(MSG_BTN_CLEAR_FILTER)), CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)pn->filterGad, CHILD_WeightedWidth, 100,
            TAG_DONE);
    }

    /* Le barre assenti vengono saltate: sia LAYOUT_AddChild sia il suo
       CHILD_WeightedHeight diventano TAG_IGNORE. */
    pn->paneLayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
        pathRow ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)pathRow,
        pathRow ? CHILD_WeightedHeight : TAG_IGNORE, 0,
        toolRow ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)toolRow,
        toolRow ? CHILD_WeightedHeight : TAG_IGNORE, 0,
        LAYOUT_AddChild, (ULONG)pn->listGad, CHILD_WeightedHeight, 100,
        filterRow ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)filterRow,
        filterRow ? CHILD_WeightedHeight : TAG_IGNORE, 0,
        pn->statusGad ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)pn->statusGad,
        pn->statusGad ? CHILD_WeightedHeight : TAG_IGNORE, 0,
        TAG_DONE);

    return pn->paneLayout;
}

static BOOL PointInPane(int p, WORD x, WORD y)
{
    /* usa l'intero pannello (percorso + toolbar + lista + filtro + stato),
       non solo la lista: un drop sul campo percorso o sulla barra di stato
       deve comunque agire su quel pannello. */
    struct Gadget *g = (struct Gadget *)gPanes[p].paneLayout;
    if (!g) g = (struct Gadget *)gPanes[p].listGad;
    if (!g || g->Width == 0 || g->Height == 0) return FALSE;
    return (x >= g->LeftEdge && x < g->LeftEdge + g->Width &&
            y >= g->TopEdge  && y < g->TopEdge  + g->Height);
}

/* Dimensione fissa delle icone: 0 = automatica, altrimenti box immagine
   in pixel.  Vale per il pannello attivo e viene ricordata per percorso. */
static void ApplyPaneIconSize(ULONG size, Object *listGad, struct Window *win)
{
    gIconSize = size;
    if (listGad)
    {
        if (win)
            IIntuition->SetGadgetAttrs((struct Gadget *)listGad, win, NULL,
                FILEBROWSER_IconSize, size, TAG_DONE);
        else
            IIntuition->SetAttrs(listGad, FILEBROWSER_IconSize, size, TAG_DONE);
    }
    RememberPathPrefs(gPathBuffer, gViewMode, gViewFilter, gSortMode,
                      gShowInfoFiles, gIconSize);
}

/* ===== Barra condivisa e modalita' split =====
   Crea e aggiunge la barra dei comandi condivisa (solo in vista divisa).
   Viene aggiunta in coda al layout verticale, cioe' in basso. */

static void ShowTopBar(struct Window *win)
{
    struct lmAddChild add;
    struct TagItem attrs[2];

    if (!gRootLayout || gTopBar) return;

    gTopCopyBtn = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_TOP_COPY, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_COPY), TAG_DONE);
    gTopMoveBtn = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_TOP_MOVE, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_MOVE), TAG_DONE);
    gTopDeleteBtn = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_TOP_DELETE, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_DELETE), TAG_DONE);

    gTopBar = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, (ULONG)gTopCopyBtn,   CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)gTopMoveBtn,   CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)gTopDeleteBtn, CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE), CHILD_WeightedWidth, 100,
        TAG_DONE);
    if (!gTopBar) return;

    attrs[0].ti_Tag  = CHILD_WeightedHeight;
    attrs[0].ti_Data = 0;
    attrs[1].ti_Tag  = TAG_DONE;
    attrs[1].ti_Data = 0;

    memset(&add, 0, sizeof(add));
    add.MethodID       = LM_ADDCHILD;
    add.lm_Window      = win;
    add.lm_Object      = gTopBar;
    add.lm_ObjectAttrs = attrs;
    IIntuition->IDoMethodA(gRootLayout, (Msg)&add);
}

/* Rimuove e distrugge la barra condivisa: sparisce completamente. */
static void HideTopBar(struct Window *win)
{
    struct lmRemoveChild rem;

    if (!gRootLayout || !gTopBar) return;

    memset(&rem, 0, sizeof(rem));
    rem.MethodID  = LM_REMOVECHILD;
    rem.lm_Window = win;
    rem.lm_Object = gTopBar;
    IIntuition->IDoMethodA(gRootLayout, (Msg)&rem);

    gTopBar = NULL;
    gTopCopyBtn = gTopMoveBtn = gTopDeleteBtn = NULL;
}

/* attiva (o chiude) il secondo pannello */
static void UpdateTopBar(struct Window *win);
static void UpdatePaneMarkers(struct Window *win);

/* Vista > Vista barra: ricostruisce i pannelli aperti con le sole barre
   visibili (layout.gadget non sa nascondere un figlio).  Il nuovo layout
   prende il posto del vecchio con CHILD_ReplaceObject, che distrugge il
   vecchio; la lista dei nodi appartiene al pannello e viene riletta. */
static void RebuildPaneBars(struct Window *win)
{
    int p, save = gActivePane;

    if (!win || !gPanesRow || !gMainWinObj) return;

    gCtxMenuBlocked = TRUE;
    for (p = 0; p < 2; p++)
    {
        struct Pane *pn = &gPanes[p];
        Object *oldLayout = pn->paneLayout;
        struct ColumnInfo *oldCol = pn->colInfo;
        struct lmModifyChild mod;
        struct TagItem attrs[2];
        Object *newLayout;

        if (!oldLayout) continue;

        /* una barra del filtro nascosta non deve lasciare la lista filtrata
           senza che si veda perche' */
        if (!BarVisible(BAR_FILTER))
        {
            pn->filterBuffer[0] = '\0';
            pn->filterApplied[0] = '\0';
            pn->filterSeen[0] = '\0';
        }

        /* CreatePaneUI scrive i gadget nel pannello: si salvano quelli
           vecchi per tornare indietro se la costruzione fallisce (poca
           memoria), invece di lasciare il pannello a meta'. */
        struct
        {
            Object *paneLayout, *listGad, *pathGad, *statusGad, *filterGad, *toggleGad;
            Object *btnRename, *btnDelete, *btnDuplicate, *btnCopy, *btnCut, *btnPaste, *btnInfo, *sizeGad;
            struct ColumnInfo *colInfo;
        } old;
        old.paneLayout = pn->paneLayout; old.listGad = pn->listGad;
        old.pathGad = pn->pathGad; old.statusGad = pn->statusGad;
        old.filterGad = pn->filterGad; old.toggleGad = pn->toggleGad;
        old.btnRename = pn->btnRename; old.btnDelete = pn->btnDelete;
        old.btnDuplicate = pn->btnDuplicate; old.btnCopy = pn->btnCopy;
        old.btnCut = pn->btnCut; old.btnPaste = pn->btnPaste;
        old.btnInfo = pn->btnInfo; old.sizeGad = pn->sizeGad;
        old.colInfo = pn->colInfo;

        /* i nodi restano al pannello: staccarli dal vecchio gadget */
        if (pn->listGad)
            IIntuition->SetAttrs(pn->listGad, LISTBROWSER_Labels, ~0, TAG_DONE);

        newLayout = CreatePaneUI(p);
        if (!newLayout)
        {
            LOGERR("bars: cannot rebuild pane %d", p);
            if (pn->colInfo && pn->colInfo != oldCol)
                IListBrowser->FreeLBColumnInfo(pn->colInfo);
            pn->paneLayout = old.paneLayout;
            pn->colInfo    = old.colInfo;
            pn->listGad    = old.listGad;
            pn->pathGad    = old.pathGad;
            pn->statusGad  = old.statusGad;
            pn->filterGad  = old.filterGad;
            pn->toggleGad  = old.toggleGad;
            pn->btnRename  = old.btnRename;
            pn->btnDelete  = old.btnDelete;
            pn->btnDuplicate = old.btnDuplicate;
            pn->btnCopy    = old.btnCopy;
            pn->btnCut     = old.btnCut;
            pn->btnPaste   = old.btnPaste;
            pn->btnInfo    = old.btnInfo;
            pn->sizeGad    = old.sizeGad;
            if (pn->listGad)
                IIntuition->SetGadgetAttrs((struct Gadget *)pn->listGad, win, NULL,
                    LISTBROWSER_Labels, (ULONG)&pn->fileList, TAG_DONE);
            continue;
        }

        attrs[0].ti_Tag  = CHILD_ReplaceObject;
        attrs[0].ti_Data = (ULONG)newLayout;
        attrs[1].ti_Tag  = TAG_DONE;
        attrs[1].ti_Data = 0;
        memset(&mod, 0, sizeof(mod));
        mod.MethodID       = LM_MODIFYCHILD;
        mod.lm_Window      = win;
        mod.lm_Object      = oldLayout;
        mod.lm_ObjectAttrs = attrs;
        IIntuition->IDoMethodA(gPanesRow, (Msg)&mod);

        if (oldCol) IListBrowser->FreeLBColumnInfo(oldCol);
    }

    IIntuition->IDoMethod(gMainWinObj, WM_RETHINK);

    for (p = 0; p < 2; p++)
    {
        struct Pane *pn = &gPanes[p];
        if (!pn->listGad) continue;
        gActivePane = p;
        LoadPath(pn->pathBuffer, pn->listGad, pn->pathGad, pn->statusGad, win, &pn->fileList);
        UpdateStatus(pn->listGad, pn->statusGad, win);
    }
    gActivePane = save;
    /* LoadPath ha messo nel titolo il percorso dell'ultimo pannello letto */
    if (gPanes[save].listGad)
        UpdateWindowTitle(win, gPanes[save].pathBuffer);
    UpdatePaneMarkers(win);
    gCtxMenuBlocked = FALSE;
    DBG("bars: hidden mask %lu", (unsigned long)gPrefs.hiddenBars);
}
static void RefreshAfterSelection(Object *listGad, struct Window *win);
static void ClearOtherPaneSelection(int gp, struct Window *win);

/* Attiva/disattiva la modalita' split: mostra o nasconde il secondo
   pannello e la barra comandi condivisa. */
static void ToggleSplitPanes(struct Window *win, Object *winObj);

static void ToggleSplit(struct Window *win, Object *winObj)
{
    gCtxMenuBlocked = TRUE;
    ToggleSplitPanes(win, winObj);
    gCtxMenuBlocked = FALSE;
    /* spunta di Vista > Dividi */
    UpdateMenu();
}

static void ToggleSplitPanes(struct Window *win, Object *winObj)
{
    struct Pane *pn = &gPanes[1];

    if (!win || !gRootLayout || !gPanesRow) return;

    if (pn->paneLayout)
    {
        struct lmRemoveChild rem;

        gActivePane = 1;
        if (pn->listGad) IIntuition->SetAttrs(pn->listGad, LISTBROWSER_Labels, ~0, TAG_DONE);
        FreeEntries(&pn->fileList);
        if (pn->notifyActive) { IDOS->EndNotify(&pn->notify); pn->notifyActive = FALSE; }

        memset(&rem, 0, sizeof(rem));
        rem.MethodID  = LM_REMOVECHILD;
        rem.lm_Window = win;
        rem.lm_Object = pn->paneLayout;
        IIntuition->IDoMethodA(gPanesRow, (Msg)&rem);

        pn->paneLayout = NULL;
        pn->listGad = pn->pathGad = pn->statusGad = pn->toggleGad = NULL;
        pn->filterGad = NULL;
        pn->sizeGad = NULL;
        pn->btnRename = pn->btnDelete = pn->btnDuplicate = pn->btnCopy = NULL;
        pn->btnCut = pn->btnPaste = pn->btnInfo = NULL;
        if (pn->colInfo) { IListBrowser->FreeLBColumnInfo(pn->colInfo); pn->colInfo = NULL; }
        gActivePane = 0;
        HideTopBar(win);
        IIntuition->IDoMethod(winObj, WM_RETHINK);
        UpdateTopBar(win);
        UpdatePaneMarkers(win);
        DBG("split: second pane closed");
    }
    else
    {
        int save = gActivePane;
        char start[PATH_MAX_LEN];
        struct lmAddChild add;
        struct TagItem attrs[3];

        strncpy(start, gPanes[0].pathBuffer, sizeof(start) - 1);
        start[sizeof(start) - 1] = '\0';

        gActivePane = 1;
        gPane1Gen++;
        PaneInit(1, start);
        /* eredita le impostazioni del pannello 0, poi applica quelle del path */
        pn->viewMode      = gPanes[0].viewMode;
        pn->viewFilter    = gPanes[0].viewFilter;
        pn->sortMode      = gPanes[0].sortMode;
        pn->showInfoFiles = gPanes[0].showInfoFiles;
        pn->iconSize      = gPanes[0].iconSize;
        ApplyPathPrefs(1, pn->pathBuffer);
        ReadDirectory(pn->pathBuffer, &pn->fileList);
        if (pn->viewFilter == VIEW_FILTER_ICONS_ONLY && pn->entryCount == 0 && !IsRootPath(pn->pathBuffer))
        {
            pn->viewFilter = VIEW_FILTER_ALL_FILES;
            ReadDirectory(pn->pathBuffer, &pn->fileList);
        }
        CreatePaneUI(1);

        attrs[0].ti_Tag  = CHILD_WeightedWidth;
        attrs[0].ti_Data = 100;
        attrs[1].ti_Tag  = CHILD_WeightedHeight;
        attrs[1].ti_Data = 100;
        attrs[2].ti_Tag  = TAG_DONE;
        attrs[2].ti_Data = 0;
        memset(&add, 0, sizeof(add));
        add.MethodID      = LM_ADDCHILD;
        add.lm_Window     = win;
        add.lm_Object     = pn->paneLayout;
        add.lm_ObjectAttrs = attrs;
        IIntuition->IDoMethodA(gPanesRow, (Msg)&add);

        ShowTopBar(win);
        IIntuition->IDoMethod(winObj, WM_RETHINK);
        UpdateTopBar(win);
        SetupNotify(pn->pathBuffer);
        UpdateStatus(pn->listGad, pn->statusGad, win);
        UpdateWindowTitle(win, pn->pathBuffer);
        gActivePane = save;
        UpdatePaneMarkers(win);
        DBG("split: second pane opened");
    }
}

/* ===== Comandi esterni (shell) e plugin ===== */

static void OpenShellHere(void)
{
    BPTR lock;
    LONG rc;
    LONG sw, sh, ww, wh, wx, wy;
    char cmd[256];
    CONST_STRPTR title = LS(MSG_MENU_SHELL);

    if (!gPathBuffer[0]) return;

    lock = IDOS->Lock(gPathBuffer, SHARED_LOCK);
    if (!lock) return;

    /* Finestra rettangolare, circa 2/3 x 3/5 dello schermo, centrata. */
    sw = gPubScreen ? gPubScreen->Width  : 1024;
    sh = gPubScreen ? gPubScreen->Height : 768;
    ww = (sw * 2) / 3;
    wh = (sh * 3) / 5;
    if (ww < 640) ww = 640;
    if (wh < 400) wh = 400;
    if (ww > sw - 40) ww = sw - 40;
    if (wh > sh - 60) wh = sh - 60;
    if (ww < 320) ww = 320;
    if (wh < 200) wh = 200;
    wx = (sw - ww) / 2;
    wy = (sh - wh) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;

    /* /CLOSE: gadget standard, incluso quello di chiusura. */
    snprintf(cmd, sizeof(cmd), "C:NewShell WINDOW \"CON:%ld/%ld/%ld/%ld/%s/CLOSE\"",
             (long)wx, (long)wy, (long)ww, (long)wh,
             (title && title[0]) ? title : "Shell");

    /* SYS_CurrentDir passa il lock della directory al processo figlio,
       che lo rilascia alla chiusura. */
    rc = IDOS->SystemTags(cmd,
        SYS_CurrentDir, lock,
        SYS_Asynch, TRUE,
        SYS_Input, 0,
        SYS_Output, 0,
        SYS_StackSize, 262144,
        TAG_DONE);

    if (rc < 0)
        IDOS->UnLock(lock);
}

/* ---- filtro rapido ---- */

/* TAB e' gestito da un hook IDCMP: window.class puo' consumare il tasto
   prima che arrivi al loop principale.  Il hook si limita a segnalare la
   richiesta; il cambio pannello avviene nel loop, fuori dall'input.
   Con IDCMP_VANILLAKEY attivo TAB arriva come carattere 9 (VANILLAKEY),
   non come RAWKEY: si controllano entrambe le classi. */
static uint32 KeyIDCMPHook(struct Hook *hook, APTR window, struct IntuiMessage *msg)
{
    (void)hook;
    (void)window;

    if (msg && msg->Class == IDCMP_RAWKEY)
    {
        ULONG code = msg->Code & 0xff;

        if (!(code & IECODE_UP_PREFIX) &&
            !(msg->Qualifier & (IEQUALIFIER_LCOMMAND |
                                IEQUALIFIER_RCOMMAND |
                                IEQUALIFIER_LALT |
                                IEQUALIFIER_RALT |
                                IEQUALIFIER_REPEAT)) &&
            (code & ~IECODE_UP_PREFIX) == RAWKEY_TAB)
            gTabSwitchRequest = TRUE;
    }
    /* Ctrl+I produce anch'esso il carattere 9: non e' TAB */
    else if (msg && msg->Class == IDCMP_VANILLAKEY && msg->Code == 9 &&
             !(msg->Qualifier & (IEQUALIFIER_LCOMMAND |
                                 IEQUALIFIER_RCOMMAND |
                                 IEQUALIFIER_LALT |
                                 IEQUALIFIER_RALT |
                                 IEQUALIFIER_CONTROL |
                                 IEQUALIFIER_REPEAT)))
    {
        gTabSwitchRequest = TRUE;
    }
    return 0;
}

static void RefreshPaneAfterFilter(int p, struct Window *win)
{
    struct Pane *pn;
    int save = gActivePane;

    if (p < 0 || p > 1 || !win) return;
    pn = &gPanes[p];
    if (!pn->listGad) return;

    strncpy(pn->filterApplied, pn->filterBuffer, sizeof(pn->filterApplied) - 1);
    pn->filterApplied[sizeof(pn->filterApplied) - 1] = '\0';

    gActivePane = p;
    IIntuition->SetGadgetAttrs((struct Gadget *)pn->listGad, win, NULL,
        LISTBROWSER_Labels, ~0, TAG_DONE);
    ReadDirectory(pn->pathBuffer, &pn->fileList);
    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)pn->listGad, win, NULL,
        LISTBROWSER_Labels, (ULONG)&pn->fileList, TAG_DONE);
    UpdateStatus(pn->listGad, pn->statusGad, win);
    gActivePane = save;
}

/* ---- rilettura differita dopo le notifiche DOS ----
   Le notifiche che arrivano entro REFRESH_MIN_TICKS dall'ultima rilettura
   non si possono scartare: un programma che crea un file lo scrive in piu'
   passi (crea, scrive, chiude) e l'ultima notifica e' quella che conta.
   Il pannello viene segnato e riletto allo scadere di questo timer.  Con
   modifiche continue (un archivio in scrittura, un programma che legge
   una cartella grande) la cartella si rilegge al massimo ogni 2 secondi:
   rileggerla ogni mezzo secondo rendeva VFM lentissimo. */
#define REFRESH_DELAY_SECS 2
#define REFRESH_MIN_TICKS  (REFRESH_DELAY_SECS * 50)

static struct MsgPort *gRefreshTimerPort = NULL;
static struct TimeRequest *gRefreshTimerReq = NULL;
static BOOL gRefreshTimerOpen = FALSE;
static BOOL gRefreshTimerPending = FALSE;
static BOOL gRefreshPending[2] = { FALSE, FALSE };

static void RefreshTimerOpen(void)
{
    gRefreshTimerPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!gRefreshTimerPort) return;
    gRefreshTimerReq = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
        ASOIOR_Size, sizeof(struct TimeRequest),
        ASOIOR_ReplyPort, gRefreshTimerPort,
        TAG_DONE);
    if (gRefreshTimerReq &&
        IExec->OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)gRefreshTimerReq, 0) == 0)
    {
        gRefreshTimerOpen = TRUE;
        return;
    }
    if (gRefreshTimerReq) IExec->FreeSysObject(ASOT_IOREQUEST, gRefreshTimerReq);
    gRefreshTimerReq = NULL;
    IExec->FreeSysObject(ASOT_PORT, gRefreshTimerPort);
    gRefreshTimerPort = NULL;
}

static void RefreshTimerClose(void)
{
    if (gRefreshTimerOpen)
    {
        if (gRefreshTimerPending)
        {
            if (!IExec->CheckIO((struct IORequest *)gRefreshTimerReq))
                IExec->AbortIO((struct IORequest *)gRefreshTimerReq);
            IExec->WaitIO((struct IORequest *)gRefreshTimerReq);
            gRefreshTimerPending = FALSE;
        }
        IExec->CloseDevice((struct IORequest *)gRefreshTimerReq);
        gRefreshTimerOpen = FALSE;
    }
    if (gRefreshTimerReq) IExec->FreeSysObject(ASOT_IOREQUEST, gRefreshTimerReq);
    gRefreshTimerReq = NULL;
    if (gRefreshTimerPort) IExec->FreeSysObject(ASOT_PORT, gRefreshTimerPort);
    gRefreshTimerPort = NULL;
}

static ULONG RefreshTimerSignal(void)
{
    return gRefreshTimerOpen ? (1UL << gRefreshTimerPort->mp_SigBit) : 0;
}

/* Arma il timer, se non e' gia' in corso.  FALSE se non e' disponibile. */
static BOOL RefreshTimerArm(void)
{
    if (!gRefreshTimerOpen) return FALSE;
    if (gRefreshTimerPending) return TRUE;
    gRefreshTimerReq->Request.io_Command = TR_ADDREQUEST;
    gRefreshTimerReq->Time.Seconds = REFRESH_DELAY_SECS;
    gRefreshTimerReq->Time.Microseconds = 0;
    IExec->SendIO((struct IORequest *)gRefreshTimerReq);
    gRefreshTimerPending = TRUE;
    return TRUE;
}

/* Rilegge la cartella del pannello p dopo una notifica. */
static void RefreshPaneFromNotify(int p, struct Window *win)
{
    struct DateStamp ds;
    int save = gActivePane;

    IDOS->DateStamp(&ds);
    gPanes[p].lastRefreshTicks = (ULONG)ds.ds_Minute * 50 + ds.ds_Tick;
    gRefreshPending[p] = FALSE;
    if (!win || !gPanes[p].listGad) return;

    DBG("directory changed, refreshing pane %d", p);
    gActivePane = p;
    LoadPath(gPanes[p].pathBuffer, gPanes[p].listGad, gPanes[p].pathGad,
             gPanes[p].statusGad, win, &gPanes[p].fileList);
    gActivePane = save;
}

/* ---- sessioni dei plugin ---- */

/* Cancella i file di una cartella del canale (e, se richiesto, le
   sottocartelle di primo livello con il loro contenuto), poi la cartella. */
static void DeleteChannelDir(CONST_STRPTR dir, BOOL subdirs)
{
    APTR ctx;
    struct ExamineData *ed;
    char path[PATH_MAX_LEN];

    if (!dir || !dir[0]) return;

    ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)dir,
            EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
    if (ctx)
    {
        while ((ed = IDOS->ExamineDir(ctx)) != NULL)
        {
            snprintf(path, sizeof(path), "%s/%s", dir, ed->Name);
            if (EXD_IS_DIRECTORY(ed))
            {
                if (subdirs) DeleteChannelDir(path, FALSE);
            }
            else
                IDOS->Delete(path);
        }
        IDOS->ReleaseDirContext(ctx);
    }
    IDOS->Delete(dir);
}

static struct PluginSession *SessionOpen(int pane)
{
    struct PluginSession *s = NULL;
    LONG i;
    BPTR l;

    if (!gPluginDir[0] || !gSessionPort) return NULL;

    for (i = 0; i < MAX_PLUGIN_SESSIONS; i++)
        if (!gSessions[i].used) { s = &gSessions[i]; break; }
    if (!s)
    {
        LOGERR("plugin session: table full");
        return NULL;
    }

    if (!s->dm)
    {
        s->dm = (struct DeathMessage *)IExec->AllocVecTags(sizeof(struct DeathMessage),
                    AVT_Type, MEMF_SHARED, TAG_DONE);
        if (!s->dm) return NULL;
    }
    memset(s->dm, 0, sizeof(*s->dm));
    s->dm->dm_Msg.mn_ReplyPort = gSessionPort;
    s->dm->dm_Msg.mn_Length = sizeof(*s->dm);

    snprintf(s->dir, sizeof(s->dir), "%s/s%lu", gPluginDir,
             (unsigned long)++gSessionSeq);
    l = IDOS->CreateDir(s->dir);
    if (!l)
    {
        LOGERR("plugin session: cannot create '%s'", s->dir);
        s->dir[0] = '\0';
        return NULL;
    }
    IDOS->UnLock(l);

    s->pane = pane;
    s->paneGen = gPane1Gen;
    s->used = TRUE;
    DBG("plugin session: open '%s' pane=%d", s->dir, pane);
    return s;
}

static void SessionClose(struct PluginSession *s)
{
    DBG("plugin session: close '%s'", s->dir);
    DeleteChannelDir(s->dir, FALSE);
    s->dir[0] = '\0';
    s->used = FALSE;
}

/* Il pannello della sessione esiste ancora (e non e' stato riaperto)? */
static BOOL SessionPaneAlive(const struct PluginSession *s)
{
    if (s->pane == 0) return TRUE;
    return (gPanes[1].listGad && s->paneGen == gPane1Gen) ? TRUE : FALSE;
}

/* DeathMessage restituiti da DOS: il processo del plugin e' terminato. */
static void ProcessSessionDeaths(void)
{
    struct Message *m;
    LONG i;

    if (!gSessionPort) return;
    while ((m = IExec->GetMsg(gSessionPort)) != NULL)
    {
        for (i = 0; i < MAX_PLUGIN_SESSIONS; i++)
        {
            if (gSessions[i].used && (struct Message *)gSessions[i].dm == m)
            {
                SessionClose(&gSessions[i]);
                break;
            }
        }
    }
}

/* All'uscita: se un plugin e' ancora in esecuzione il suo DeathMessage
   arrivera' piu' tardi.  In quel caso porta e messaggi non vengono
   liberati e la porta passa a PA_IGNORE, cosi' DOS non scrive in memoria
   liberata e non segnala un task che non esiste piu'. */
static void SessionsShutdown(void)
{
    BOOL alive = FALSE;
    LONG i;

    ProcessSessionDeaths();
    for (i = 0; i < MAX_PLUGIN_SESSIONS; i++)
    {
        if (gSessions[i].used)
        {
            alive = TRUE;
            DeleteChannelDir(gSessions[i].dir, FALSE);
            gSessions[i].used = FALSE;
        }
        else if (gSessions[i].dm)
        {
            IExec->FreeVec(gSessions[i].dm);
            gSessions[i].dm = NULL;
        }
    }
    if (gSessionPort)
    {
        if (alive)
            gSessionPort->mp_Flags = PA_IGNORE;
        else
            IExec->FreeSysObject(ASOT_PORT, gSessionPort);
        gSessionPort = NULL;
    }
}

/* ---- plugin (script ARexx) ---- */

void RunPlugin(CONST_STRPTR path)
{
    struct PluginSession *session = NULL;
    CONST_STRPTR channel = gPluginDir;
    char absPath[PATH_MAX_LEN];
    char exePath[PATH_MAX_LEN];
    char cmd[PATH_MAX_LEN * 6 + 256];
    ULONG len, stack = 524288;
    BPTR dir;

    if (!path || !path[0]) return;

    /* Il figlio (Python/RX) non eredita PROGDIR: dell'applicazione:
       risolviamo il percorso in assoluto prima di lanciarlo. */
    absPath[0] = '\0';
    if (strncmp(path, "PROGDIR:", 8) == 0)
    {
        dir = IDOS->GetProgramDir();
        if (dir && IDOS->NameFromLock(dir, absPath, sizeof(absPath)) &&
            IDOS->AddPart(absPath, path + 8, sizeof(absPath)))
        {
            /* ok */
        }
        else
        {
            strncpy(absPath, path, sizeof(absPath) - 1);
            absPath[sizeof(absPath) - 1] = '\0';
        }
    }
    else
    {
        strncpy(absPath, path, sizeof(absPath) - 1);
        absPath[sizeof(absPath) - 1] = '\0';
    }

    len = strlen(absPath);
    if (len > 3 && IUtility->Stricmp(absPath + len - 3, ".py") == 0)
    {
        CONST_STRPTR py = NULL;
        BPTR lock;

        /* Python puo' essere installato come C:Python (2.x) o C:Python3. */
        lock = IDOS->Lock("C:Python", SHARED_LOCK);
        if (lock) { IDOS->UnLock(lock); py = "C:Python"; }
        else
        {
            lock = IDOS->Lock("C:Python3", SHARED_LOCK);
            if (lock) { IDOS->UnLock(lock); py = "C:Python3"; }
        }
        if (!py)
        {
            LOGERR("plugin run: Python not found (C:Python / C:Python3)");
            ShowMessage(LS(MSG_APP_TITLE),
                "Python not found: install C:Python or C:Python3.");
            return;
        }

        /* Terzo argomento: l'eseguibile VFM, cosi' l'addon puo' aprire
           un dialogo Reaction con "--dialog" senza passare da ARexx. */
        exePath[0] = '\0';
        dir = IDOS->GetProgramDir();
        if (!dir || !IDOS->NameFromLock(dir, exePath, sizeof(exePath)) ||
            !IDOS->AddPart(exePath, "VagaFileManager", sizeof(exePath)))
            strncpy(exePath, "VagaFileManager", sizeof(exePath) - 1);
        exePath[sizeof(exePath) - 1] = '\0';

        /* Sessione propria, legata al pannello attivo al lancio.  Se non
           si riesce a crearla si usa la cartella comune (pannello attivo
           al momento di ogni comando, come nelle build precedenti). */
        session = SessionOpen((gActivePane == 1 && gPanes[1].listGad) ? 1 : 0);
        if (session) channel = session->dir;

        /* Argomenti Python:
             1 porta ARexx, 2 path pannello, 3 eseguibile VFM,
             4 porta del drawer per --notify, 5 directory del canale
             (la cartella della sessione), 6 DEBUG/NODEBUG (ToolType
             DEBUG di VFM: il plugin scrive nel suo log ogni passo o solo
             gli errori).
           I plugin possono quindi usare il canale su file senza ARexx. */
        snprintf(cmd, sizeof(cmd),
                 "\"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" %s",
                 py, absPath, gRexxPortName, gPathBuffer, exePath,
                 gDropPortName, channel, gDebugLog ? "DEBUG" : "NODEBUG");
        stack = 1024 * 1024;
    }
    else
        snprintf(cmd, sizeof(cmd), "C:RX \"%s\"", absPath);
    {
        LONG rc = IDOS->SystemTags(cmd, SYS_Asynch, TRUE, SYS_Input, 0, SYS_Output, 0,
                                   SYS_StackSize, stack,
                                   session ? NP_NotifyOnDeathMessage : TAG_IGNORE,
                                   (ULONG)(session ? session->dm : NULL),
                                   TAG_DONE);
        DBG("plugin run: rc=%ld cmd=%s", (long)rc, cmd);
        if (rc == -1)
        {
            LOGERR("plugin run: failed to start");
            if (session) SessionClose(session);
            ShowMessage(LS(MSG_APP_TITLE),
                "Cannot start the plugin. Check C:Python / C:Python3 and the script path.");
        }
    }
}

void ScanPlugins(void)
{
    APTR ctx;
    struct ExamineData *ed;

    gPluginCount = 0;
    ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)PLUGIN_DIR,
                                     EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
    if (!ctx) return;

    while (gPluginCount < MAX_PLUGINS && (ed = IDOS->ExamineDir(ctx)) != NULL)
    {
        ULONG len;
        BOOL isRexx, isPy;
        if (EXD_IS_DIRECTORY(ed)) continue;
        len = strlen(ed->Name);
        isRexx = (len > 5 && IUtility->Stricmp(ed->Name + len - 5, ".rexx") == 0) ? TRUE : FALSE;
        isPy   = (len > 3 && IUtility->Stricmp(ed->Name + len - 3, ".py") == 0) ? TRUE : FALSE;
        if (!isRexx && !isPy) continue;
        strncpy(gPlugins[gPluginCount], PLUGIN_DIR, PATH_MAX_LEN - 1);
        gPlugins[gPluginCount][PATH_MAX_LEN - 1] = '\0';
        if (!IDOS->AddPart(gPlugins[gPluginCount], ed->Name, PATH_MAX_LEN)) continue;
        gPluginCount++;
    }
    IDOS->ReleaseDirContext(ctx);

    /* Ordine alfabetico (senza distinzione maiuscole/minuscole) sul nome
       del file, cosi' il menu Script e' stabile e facile da consultare. */
    {
        static char tmp[PATH_MAX_LEN];
        LONG i, j;
        for (i = 1; i < gPluginCount; i++)
        {
            strcpy(tmp, gPlugins[i]);
            for (j = i - 1; j >= 0 &&
                 IUtility->Stricmp(FilePart(gPlugins[j]), FilePart(tmp)) > 0; j--)
                strcpy(gPlugins[j + 1], gPlugins[j]);
            strcpy(gPlugins[j + 1], tmp);
        }
    }
    DBG("plugins found: %ld", (long)gPluginCount);
}

/* ===== Porta ARexx ===== */

static void RexxSetResult(struct RexxMsg *msg, CONST_STRPTR text)
{
    if (!gIRexx) return;
    if (msg->rm_Result2) { gIRexx->DeleteArgstring((STRPTR)msg->rm_Result2); msg->rm_Result2 = 0; }
    if (text && (msg->rm_Action & RXFF_RESULT))
        msg->rm_Result2 = (LONG)gIRexx->CreateArgstring(text, strlen(text));
}

/* Scrive i percorsi completi delle voci selezionate (uno per riga) in un
   file dentro la directory del canale plugin e ne restituisce il percorso.
   Usato da SELECTED e ADDONINFO: nessun limite di lunghezza e nessun
   problema con i nomi che contengono spazi. */
static ULONG gSelectedListSeq = 0;

static LONG WriteSelectedFullPaths(int gp, STRPTR outPath, ULONG outSize)
{
    struct Pane *pn = &gPanes[gp];
    struct Node *n;
    char path[PATH_MAX_LEN];
    char full[PATH_MAX_LEN];
    BPTR fh;
    LONG count = 0;

    if (outPath && outSize) outPath[0] = '\0';

    /* Nella cartella della sessione, se il comando arriva da un plugin:
       il file sparisce insieme alla sessione. */
    if (gCurSessionDir)
        snprintf(path, sizeof(path), "%s/selected_%lu.txt", gCurSessionDir,
                 (unsigned long)++gSelectedListSeq);
    else if (gPluginDir[0])
        snprintf(path, sizeof(path), "%s/selected_%lu.txt", gPluginDir,
                 (unsigned long)++gSelectedListSeq);
    else
        snprintf(path, sizeof(path), "T:VFM_selected_%08lx_%lu.txt",
                 (unsigned long)(ULONG)IExec->FindTask(NULL),
                 (unsigned long)++gSelectedListSeq);

    fh = IDOS->Open(path, MODE_NEWFILE);
    if (!fh) return -1;

    for (n = pn->fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE;
        ULONG index = MAX_ENTRIES;
        STRPTR name = NULL;

        IListBrowser->GetListBrowserNodeAttrs(n,
            LBNA_Selected, &sel,
            LBNA_UserData, &index,
            LBNA_Column, COL_NAME, LBNCA_Text, &name,
            TAG_DONE);

        if (!sel || !name || index >= (ULONG)pn->entryCount) continue;

        if (IsRootPath(pn->pathBuffer))
        {
            strncpy(full, name, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
        }
        else if (!BuildChildPath(pn->pathBuffer, name, full, sizeof(full)))
            continue;

        IDOS->FPrintf(fh, "%s\n", full);
        count++;
    }

    IDOS->Close(fh);

    if (outPath && outSize)
    {
        strncpy(outPath, path, outSize - 1);
        outPath[outSize - 1] = '\0';
    }
    return count;
}

static BOOL SelectEntryByName(Object *listGad, struct Window *win,
                              struct List *list, CONST_STRPTR name)
{
    struct Node *n;
    if (!list || !name || !name[0]) return FALSE;

    /* seleziona solo il file trovato */
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
        IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, FALSE, TAG_DONE);

    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        STRPTR text = NULL;
        ULONG idx = MAX_ENTRIES;
        IListBrowser->GetListBrowserNodeAttrs(n,
            LBNA_UserData, &idx,
            LBNA_Column, COL_NAME, LBNCA_Text, &text,
            TAG_DONE);
        if (text && IUtility->Stricmp(text, name) == 0 && idx < (ULONG)gEntryCount)
        {
            /* La lista dei pannelli e' MultiSelect: LISTBROWSER_Selected non
               e' affidabile in quel caso.  Selezioniamo il nodo direttamente
               e poi lo rendiamo visibile. */
            IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, TRUE, TAG_DONE);
            if (listGad)
            {
                if (win)
                    IIntuition->SetGadgetAttrs((struct Gadget *)listGad, win, NULL,
                        LISTBROWSER_MakeVisible, idx, TAG_DONE);
                else
                    IIntuition->SetAttrs(listGad, LISTBROWSER_MakeVisible, idx, TAG_DONE);
                if (win)
                    IIntuition->RefreshGList((struct Gadget *)listGad, win, NULL, 1);
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* Comando SELECT: seleziona le voci del pannello gp il cui nome
   corrisponde al pattern AmigaDOS (senza distinzione maiuscole/minuscole;
   un nome senza caratteri jolly e' un confronto esatto).  add == FALSE
   sostituisce la selezione, TRUE la amplia.  Restituisce il numero di
   voci che corrispondono, -1 se il pattern non e' valido. */
static LONG SelectEntriesByPattern(int gp, CONST_STRPTR pattern, BOOL add,
                                   struct Window *win)
{
    struct Pane *pn = &gPanes[gp];
    struct Node *n;
    STRPTR parsed;
    ULONG size;
    LONG count = 0;
    ULONG firstIdx = MAX_ENTRIES;

    if (!pattern || !pattern[0]) return -1;

    size = strlen(pattern) * 2 + 2;
    parsed = (STRPTR)IExec->AllocVecTags(size, AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!parsed) return -1;
    if (IDOS->ParsePatternNoCase(pattern, parsed, size) < 0)
    {
        IExec->FreeVec(parsed);
        return -1;
    }

    for (n = pn->fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        STRPTR name = NULL;
        ULONG idx = MAX_ENTRIES;
        BOOL match;

        IListBrowser->GetListBrowserNodeAttrs(n,
            LBNA_UserData, &idx,
            LBNA_Column, COL_NAME, LBNCA_Text, &name,
            TAG_DONE);
        match = (name && idx < (ULONG)pn->entryCount &&
                 IDOS->MatchPatternNoCase(parsed, name)) ? TRUE : FALSE;

        if (match)
        {
            IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, TRUE, TAG_DONE);
            if (count == 0) firstIdx = idx;
            count++;
        }
        else if (!add)
            IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, FALSE, TAG_DONE);
    }
    IExec->FreeVec(parsed);

    if (pn->listGad && firstIdx < MAX_ENTRIES)
    {
        if (win)
            IIntuition->SetGadgetAttrs((struct Gadget *)pn->listGad, win, NULL,
                LISTBROWSER_MakeVisible, firstIdx, TAG_DONE);
        else
            IIntuition->SetAttrs(pn->listGad, LISTBROWSER_MakeVisible, firstIdx, TAG_DONE);
    }
    RefreshAfterSelection(pn->listGad, win);
    /* Come con il mouse: la selezione vive in un solo pannello. */
    if (count > 0) ClearOtherPaneSelection(gp, win);
    return count;
}

/* Svuota il filtro rapido di un pannello.  Serve a REVEAL/Find: il file
   cercato puo' essere stato nascosto dal filtro attivo. */
static void ClearPaneFilter(int gp, struct Window *win)
{
    struct Pane *pn = &gPanes[gp];

    if (!pn->filterBuffer[0] && !pn->filterApplied[0]) return;
    pn->filterBuffer[0] = '\0';
    pn->filterApplied[0] = '\0';
    pn->filterSeen[0] = '\0';
    if (pn->filterGad)
    {
        if (win)
            IIntuition->SetGadgetAttrs((struct Gadget *)pn->filterGad, win, NULL,
                STRINGA_TextVal, (ULONG)"", TAG_DONE);
        else
            IIntuition->SetAttrs(pn->filterGad, STRINGA_TextVal, (ULONG)"", TAG_DONE);
    }
}

/* Porta il pannello attivo sulla cartella del path e seleziona il file.
   Usata dal comando REVEAL e dalla finestra risultati di Find/Grep. */
BOOL RevealPath(CONST_STRPTR path, struct Window *win)
{
    char parent[PATH_MAX_LEN];
    CONST_STRPTR targetName;
    BOOL selected;
    int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
    struct Pane *pn = &gPanes[gp];

    if (!path || !path[0] || !pn->listGad) return FALSE;
    if (!GetParentPath(path, parent, sizeof(parent))) return FALSE;

    DBG("reveal: path='%s' pane=%d parent='%s'", path, gp, parent);

    targetName = FilePart(path);
    NavigateTo(parent, pn->listGad, pn->pathGad, pn->statusGad, win, &pn->fileList);
    selected = SelectEntryByName(pn->listGad, win, &pn->fileList, targetName);

    /* Il file puo' essere nascosto dal filtro rapido, dal filtro "solo
       icone" o, se e' un .info, dall'opzione "mostra .info" (salvata per
       cartella) e dalla vista icone, che non mostra mai i .info.  Per
       rivelarlo bisogna mostrarlo: un solo reload se serve. */
    {
        ULONG tl = targetName ? strlen(targetName) : 0;
        BOOL isInfo = (tl > 5 && IUtility->Stricmp(targetName + tl - 5, ".info") == 0) ? TRUE : FALSE;
        BOOL infoHidden = (isInfo && (!gShowInfoFiles || gViewMode == FBVM_ICONS)) ? TRUE : FALSE;

    if (!selected && (pn->filterBuffer[0] || gViewFilter == VIEW_FILTER_ICONS_ONLY || infoHidden))
    {
        if (pn->filterBuffer[0])
            ClearPaneFilter(gp, win);
        if (gViewFilter == VIEW_FILTER_ICONS_ONLY)
            gViewFilter = VIEW_FILTER_ALL_FILES;
        if (infoHidden)
        {
            gShowInfoFiles = TRUE;
            /* LoadPath applica gViewMode al gadget */
            if (gViewMode == FBVM_ICONS) gViewMode = FBVM_DETAILS;
        }
        LoadPath(parent, pn->listGad, pn->pathGad, pn->statusGad, win, &pn->fileList);
        selected = SelectEntryByName(pn->listGad, win, &pn->fileList, targetName);
        UpdateMenu();
    }
    }

    DBG("reveal: selected=%ld name='%s' filter='%s' viewfilter=%lu",
        (long)selected, targetName ? targetName : "",
        pn->filterBuffer, (unsigned long)gViewFilter);
    if (selected)
    {
        UpdateStatus(pn->listGad, pn->statusGad, win);
        if (win) IIntuition->ActivateWindow(win);
    }
    return selected;
}

/* Esegue un comando ricevuto sulla porta ARexx.  Il comando e' la prima
   parola dell'argomento, il resto sono i parametri. */
static void ExecuteRexxCommand(struct RexxMsg *msg, int gp, struct Window *win)
{
    char line[1024], cmd[32];
    char *args, *p;
    STRPTR a0 = (STRPTR)ARG0(msg);
    Object *listGad = gPanes[gp].listGad;
    Object *pathGad = gPanes[gp].pathGad;
    Object *statusGad = gPanes[gp].statusGad;
    Object *toggleGad = gPanes[gp].toggleGad;
    struct List *fl = &gPanes[gp].fileList;
    BOOL keepStatus = FALSE;   /* STATUS: non sovrascrivere il messaggio */

    DBG("rexx: '%s'", a0 ? a0 : "(null)");
    msg->rm_Result1 = RC_OK;
    gActivePane = gp;

    if (!a0)
    {
        msg->rm_Result1 = RC_ERROR;
        RexxSetResult(msg, "No command");
        return;
    }

    strncpy(line, a0, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    p = line;
    while (*p == ' ' || *p == '\t') p++;
    args = p;
    while (*args && *args != ' ' && *args != '\t') args++;
    {
        ULONG n = (ULONG)(args - p);
        if (n >= sizeof(cmd)) n = sizeof(cmd) - 1;
        memcpy(cmd, p, n);
        cmd[n] = '\0';
    }
    while (*args == ' ' || *args == '\t') args++;
    {
        ULONG l = strlen(args);
        while (l > 0 && (args[l-1] == ' ' || args[l-1] == '\t' || args[l-1] == '\n' || args[l-1] == '\r'))
            args[--l] = '\0';
    }

    if (!cmd[0])
    {
        msg->rm_Result1 = RC_ERROR;
        RexxSetResult(msg, "No command");
        return;
    }

    if (!IUtility->Stricmp(cmd, "HELP"))
    {
        /* La porta espone solo stato e interfaccia di VFM: le operazioni
           sui file (rinomina, copia, cancella...) le fanno i plugin. */
        RexxSetResult(msg,
            "PATH | ENTRIES | SELECTED | ADDONINFO | VERSION | PROTOCOL | "
            "CD <path> | OPEN [name] | UP | FORWARD | REFRESH | REVEAL <path> | "
            "SELECT [ADD] <pattern> | SELECT NONE | "
            "VIEW ICONS|ALLFILES | SORT TYPE|NAME|DATE|SIZE | MODE ICONS|DETAILS | SPLIT | "
            "DIALOG [CHECK=0|1:<label>|]<title>|<prompt>|<text>|<buttons> | ASK <prompt> | STATUS [text] | "
            "PROGRESS [NOCANCEL] [NOBUTTONS] [INDET] done total [bytesDone bytesTotal] | PROGRESS TEXT [message] | PROGRESS CLOSE | "
            "NEW | SCRIPT <file> | QUIT");
    }
    else if (!IUtility->Stricmp(cmd, "VERSION"))
        RexxSetResult(msg, "VagaFileManager " VFM_VERSION_FULL);
    else if (!IUtility->Stricmp(cmd, "PROTOCOL"))
        RexxSetResult(msg, VFM_STR(VFM_PROTOCOL));
    else if (!IUtility->Stricmp(cmd, "QUIT"))
        gRexxQuit = TRUE;
    else if (!IUtility->Stricmp(cmd, "NEW"))
        LaunchNewInstance();
    else if (!IUtility->Stricmp(cmd, "SPLIT"))
    {
        if (win) ToggleSplit(win, gMainWinObj);
    }
    else if (!IUtility->Stricmp(cmd, "CD"))
        NavigateTo(args[0] ? args : "", listGad, pathGad, statusGad, win, fl);
    else if (!IUtility->Stricmp(cmd, "OPEN"))
    {
        if (!args[0])
            TryOpenSelected(listGad, pathGad, statusGad, win, fl, FALSE);
        else
        {
            char full[PATH_MAX_LEN];
            BOOL handled = FALSE;
            if (IsDirectoryPath(args))
            {
                NavigateTo(args, listGad, pathGad, statusGad, win, fl);
                handled = TRUE;
            }
            else if (IsRootPath(gPathBuffer))
            {
                strncpy(full, args, sizeof(full) - 1);
                full[sizeof(full) - 1] = '\0';
            }
            else if (!BuildChildPath(gPathBuffer, args, full, sizeof(full)))
                handled = TRUE;

            if (!handled)
            {
                if (IsDirectoryPath(full))
                    NavigateTo(full, listGad, pathGad, statusGad, win, fl);
                else if (IWorkbench)
                    IWorkbench->OpenWorkbenchObjectA(full, NULL);
            }
        }
    }
    else if (!IUtility->Stricmp(cmd, "REFRESH"))
        LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, fl);
    else if (!IUtility->Stricmp(cmd, "UP"))
        GoUp(listGad, pathGad, statusGad, win, fl);
    else if (!IUtility->Stricmp(cmd, "FORWARD"))
        GoForward(listGad, pathGad, statusGad, win, fl);
    else if (!IUtility->Stricmp(cmd, "PATH"))
        RexxSetResult(msg, gPathBuffer);
    else if (!IUtility->Stricmp(cmd, "PROGRESS"))
    {
        char *a = args;

        if (!IUtility->Stricmp(a, "CLOSE"))
        {
            ProgressClose();
            RexxSetResult(msg, "OK");
        }
        else if (!IUtility->Strnicmp(a, "TEXT", 4) && (a[4] == ' ' || a[4] == '\t' || a[4] == '\0'))
        {
            /* PROGRESS TEXT <messaggio>: testo al posto del conteggio nella
               finestra aperta; senza messaggio torna il conteggio */
            a += 4;
            while (*a == ' ' || *a == '\t') a++;
            ProgressSetText(a);
            ProgressPoll();
            RexxSetResult(msg, gProgCancel ? "1" : "0");
        }
        else
        {
            BOOL nocancel = FALSE;
            BOOL nobuttons = FALSE;
            BOOL indet = FALSE;
            LONG done = 0, total = 0;
            unsigned long long bytesDone = 0, bytesTotal = 0;
            int n;

            /* parole chiave in qualunque ordine prima dei numeri:
               NOCANCEL, NOBUTTONS (niente Annulla/Pausa: solo il pulsante
               di chiusura, che interrompe), INDET */
            for (;;)
            {
                if (!IUtility->Strnicmp(a, "NOCANCEL", 8)) { nocancel = TRUE; a += 8; }
                else if (!IUtility->Strnicmp(a, "NOBUTTONS", 9)) { nobuttons = TRUE; a += 9; }
                else if (!IUtility->Strnicmp(a, "INDET", 5)) { indet = TRUE; a += 5; }
                else break;
                while (*a == ' ' || *a == '\t') a++;
            }

            n = sscanf(a, "%ld %ld %llu %llu",
                       &done, &total, &bytesDone, &bytesTotal);
            if (n < 2)
            {
                msg->rm_Result1 = RC_ERROR;
                RexxSetResult(msg,
                    "PROGRESS [NOCANCEL] [NOBUTTONS] [INDET] done total [bytesDone bytesTotal] | PROGRESS TEXT [message] | PROGRESS CLOSE");
            }
            else
            {
                if (!gProgActive || !gProgWin)
                {
                    if (gProgWinObj) ProgressClose();
                    ProgressSetCancelable(nocancel ? FALSE : TRUE);
                    ProgressSetButtons(nobuttons ? FALSE : TRUE);
                    ProgressOpen(win, total > 0 ? (ULONG)total : 1);
                    if (indet || total <= 0) ProgressSetIndeterminate(TRUE);
                }

                ProgressSetCounts((done > 0) ? (ULONG)done : 0,
                                  (total > 0) ? (ULONG)total : 1);
                ProgressSetIndeterminateTotal(
                    (indet && total > 0) ? (ULONG)total : 0);
                if (n >= 4)
                {
                    gProgBytesDone  = (uint64)bytesDone;
                    gProgBytesTotal = (uint64)bytesTotal;
                }
                else
                {
                    gProgBytesDone  = 0;
                    gProgBytesTotal = 0;
                }

                ProgressUpdate();
                ProgressPoll();
                RexxSetResult(msg, gProgCancel ? "1" : "0");
            }
        }
    }
    else if (!IUtility->Stricmp(cmd, "ADDONINFO"))
    {
        char info[PATH_MAX_LEN * 3 + 64];
        char listPath[PATH_MAX_LEN];
        LONG n = WriteSelectedFullPaths(gp, listPath, sizeof(listPath));

        if (n < 0)
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "");
        }
        else
        {
            snprintf(info, sizeof(info), "%ld\n%ld\n%s\n%s\n%ld\n%s",
                     (long)(gPanes[1].paneLayout ? 1 : 0),
                     (long)gp,
                     gPanes[0].pathBuffer,
                     gPanes[1].paneLayout ? gPanes[1].pathBuffer : "",
                     (long)n,
                     listPath);
            RexxSetResult(msg, info);
        }
    }
    else if (!IUtility->Stricmp(cmd, "ASK"))
    {
        char answer[256];
        CONST_STRPTR prompt = args[0] ? args : (CONST_STRPTR)LS(MSG_FIND_TITLE);
        memset(answer, 0, sizeof(answer));
        if (PromptString(prompt, answer, sizeof(answer)))
            RexxSetResult(msg, answer);
        else
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "");
        }
    }
    else if (!IUtility->Stricmp(cmd, "DIALOG"))
    {
        char title[128] = "VFM";
        char prompt[256] = "";
        char text[256] = "";
        char buttons[128] = "OK|Cancel";
        char checkLabel[128] = "";
        ULONG checked = 0;
        BOOL hasCheck = FALSE;
        char *fields[4];
        ULONG sizes[4];
        LONG f;
        char *s = args;

        /* Prefisso facoltativo: CHECK=<0|1>:<etichetta>| aggiunge una
           casella di spunta sotto il campo di testo; la risposta diventa
           <indice>|<0|1>|<testo>.  Senza prefisso nulla cambia. */
        if (!IUtility->Strnicmp(s, "CHECK=", 6))
        {
            char *sep;
            hasCheck = TRUE;
            s += 6;
            checked = (*s == '1') ? 1 : 0;
            if (*s) s++;
            if (*s == ':') s++;
            sep = strchr(s, '|');
            {
                ULONG n = sep ? (ULONG)(sep - s) : (ULONG)strlen(s);
                if (n >= sizeof(checkLabel)) n = sizeof(checkLabel) - 1;
                memcpy(checkLabel, s, n);
                checkLabel[n] = '\0';
            }
            s = sep ? sep + 1 : NULL;
        }

        fields[0] = title;   sizes[0] = sizeof(title);
        fields[1] = prompt;  sizes[1] = sizeof(prompt);
        fields[2] = text;    sizes[2] = sizeof(text);
        fields[3] = buttons; sizes[3] = sizeof(buttons);

        /* Formato: titolo|prompt|testo|pulsanti (i campi mancanti
           mantengono i valori di default). */
        for (f = 0; f < 4 && s; f++)
        {
            /* Gli ultimi tre campi sono separati da '|'; il quarto
               (pulsanti) contiene a sua volta '|' e prende tutto il resto. */
            char *sep = (f < 3) ? strchr(s, '|') : NULL;
            ULONG n = sep ? (ULONG)(sep - s) : (ULONG)strlen(s);
            if (n >= sizes[f]) n = sizes[f] - 1;
            memcpy(fields[f], s, n);
            fields[f][n] = '\0';
            s = sep ? sep + 1 : NULL;
        }

        DBG("DIALOG: title='%s' prompt='%s' text='%s' buttons='%s'",
            title, prompt, text, buttons);
        {
            LONG idx = PromptDialogCheck(title, prompt, text, sizeof(text), buttons,
                                         hasCheck ? checkLabel : NULL, &checked);
            DBG("DIALOG: idx=%ld text='%s' check=%lu", (long)idx, text, (unsigned long)checked);
            if (idx > 0)
            {
                char result[512];
                if (hasCheck)
                    snprintf(result, sizeof(result), "%ld|%lu|%s", (long)idx,
                             (unsigned long)checked, text);
                else
                    snprintf(result, sizeof(result), "%ld|%s", (long)idx, text);
                RexxSetResult(msg, result);
            }
            else
            {
                msg->rm_Result1 = RC_ERROR;
                RexxSetResult(msg, "");
            }
        }
    }
    else if (!IUtility->Stricmp(cmd, "REVEAL"))
    {
        if (!args[0])
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "Missing path");
        }
        else if (!RevealPath(args, win))
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "File not found");
        }
        else
            RexxSetResult(msg, "OK");
    }
    else if (!IUtility->Stricmp(cmd, "ENTRIES"))
    {
        char b[32];
        snprintf(b, sizeof(b), "%ld", (long)gEntryCount);
        RexxSetResult(msg, b);
    }
    else if (!IUtility->Stricmp(cmd, "SELECTED"))
    {
        /* Risultato: il percorso del file con la lista (anche vuota). */
        char listPath[PATH_MAX_LEN];
        if (WriteSelectedFullPaths(gp, listPath, sizeof(listPath)) < 0)
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "");
        }
        else
            RexxSetResult(msg, listPath);
    }
    else if (!IUtility->Stricmp(cmd, "VIEW"))
    {
        if (!IUtility->Stricmp(args, "ICONS")) gViewFilter = VIEW_FILTER_ICONS_ONLY;
        else if (!IUtility->Stricmp(args, "ALLFILES")) gViewFilter = VIEW_FILTER_ALL_FILES;
        else { msg->rm_Result1 = RC_ERROR; RexxSetResult(msg, "VIEW ICONS|ALLFILES"); return; }
        LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, fl);
        UpdateMenu();
    }
    else if (!IUtility->Stricmp(cmd, "SORT"))
    {
        if (!IUtility->Stricmp(args, "TYPE")) gSortMode = SORT_BY_TYPE;
        else if (!IUtility->Stricmp(args, "NAME")) gSortMode = SORT_BY_NAME;
        else if (!IUtility->Stricmp(args, "DATE")) gSortMode = SORT_BY_DATE;
        else if (!IUtility->Stricmp(args, "SIZE")) gSortMode = SORT_BY_SIZE;
        else { msg->rm_Result1 = RC_ERROR; RexxSetResult(msg, "SORT TYPE|NAME|DATE|SIZE"); return; }
        LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, fl);
        UpdateMenu();
    }
    else if (!IUtility->Stricmp(cmd, "MODE"))
    {
        if (!IUtility->Stricmp(args, "ICONS")) SetViewMode(FBVM_ICONS, listGad, toggleGad, win);
        else if (!IUtility->Stricmp(args, "DETAILS")) SetViewMode(FBVM_DETAILS, listGad, toggleGad, win);
        else { msg->rm_Result1 = RC_ERROR; RexxSetResult(msg, "MODE ICONS|DETAILS"); return; }
        LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, fl);
        UpdateMenu();
    }
    else if (!IUtility->Stricmp(cmd, "SELECT"))
    {
        char pat[512];
        char *a = args;
        BOOL add = FALSE;
        ULONG l;

        if (!IUtility->Strnicmp(a, "ADD", 3) && (a[3] == ' ' || a[3] == '\t'))
        {
            add = TRUE;
            a += 3;
            while (*a == ' ' || *a == '\t') a++;
        }
        /* Il pattern puo' essere tra virgolette (nomi con spazi). */
        strncpy(pat, a, sizeof(pat) - 1);
        pat[sizeof(pat) - 1] = '\0';
        l = strlen(pat);
        if (l >= 2 && pat[0] == '"' && pat[l - 1] == '"')
        {
            memmove(pat, pat + 1, l - 2);
            pat[l - 2] = '\0';
        }

        if (!add && !IUtility->Stricmp(pat, "NONE"))
        {
            DeselectAllEntries(fl);
            RefreshAfterSelection(listGad, win);
            RexxSetResult(msg, "0");
        }
        else
        {
            LONG n = pat[0] ? SelectEntriesByPattern(gp, pat, add, win) : -1;
            if (n < 0)
            {
                msg->rm_Result1 = RC_ERROR;
                RexxSetResult(msg, "SELECT [ADD] <pattern> | SELECT NONE");
            }
            else
            {
                char b[32];
                snprintf(b, sizeof(b), "%ld", (long)n);
                RexxSetResult(msg, b);
            }
        }
    }
    else if (!IUtility->Stricmp(cmd, "STATUS"))
    {
        /* Messaggio nella barra di stato del pannello: resta finche' VFM
           non aggiorna la barra (selezione, navigazione...).  STATUS senza
           testo ripristina subito la barra normale. */
        static char statusMsg[2][256];
        if (args[0] && statusGad)
        {
            snprintf(statusMsg[gp], sizeof(statusMsg[gp]), "%s", args);
            if (win)
                IIntuition->RefreshSetGadgetAttrs((struct Gadget *)statusGad, win, NULL,
                    GA_Text, (ULONG)statusMsg[gp], TAG_DONE);
            else
                IIntuition->SetAttrs(statusGad, GA_Text, (ULONG)statusMsg[gp], TAG_DONE);
            keepStatus = TRUE;
        }
    }
    else if (!IUtility->Stricmp(cmd, "SCRIPT"))
        RunPlugin(args);
    else
    {
        msg->rm_Result1 = RC_ERROR;
        RexxSetResult(msg, "Unknown command");
    }

    if (!keepStatus)
    {
        int cp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
        if (gPanes[cp].listGad)
            UpdateStatus(gPanes[cp].listGad, gPanes[cp].statusGad, win);
    }
}

/* ---- Canale comandi dei plugin (senza ARexx) ----
   Il plugin scrive req_<id>.txt nella propria directory T:VFM_<task>,
   manda un messaggio VFMPLUGIN_TYPE alla porta del drawer e aspetta
   rep_<id>.txt.  Eseguiamo il comando con la stessa logica ARexx, ma su
   un RexxMsg sintetico: nessun host ARexx e nessun problema con RX. */
static void ExecutePluginCommand(CONST_STRPTR cmd, int gp, struct Window *win,
                                 LONG *rcOut, char *result, ULONG resultSize)
{
    struct MsgPort *reply;
    struct RexxMsg *msg;
    struct Message *m;

    if (rcOut) *rcOut = RC_ERROR;
    if (result && resultSize) result[0] = '\0';
    if (!gIRexx || !cmd || !cmd[0]) return;

    reply = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    if (!reply) return;

    msg = gIRexx->CreateRexxMsg(reply, NULL, NULL);
    if (!msg)
    {
        IExec->FreeSysObject(ASOT_PORT, reply);
        return;
    }

    /* RXFF_RESULT e' indispensabile: RexxSetResult() scrive rm_Result2
       solo se il flag e' presente.  Senza, VERSION/PATH/ENTRIES/SELECTED
       restituiscono una stringa vuota. */
    msg->rm_Action = RXCOMM | RXFF_RESULT;
    msg->rm_Args[0] = gIRexx->CreateArgstring(cmd, strlen(cmd));
    if (!msg->rm_Args[0])
    {
        gIRexx->DeleteRexxMsg(msg);
        IExec->FreeSysObject(ASOT_PORT, reply);
        return;
    }

    ExecuteRexxCommand(msg, gp, win);
    IExec->ReplyMsg((struct Message *)msg);
    IExec->Wait(1UL << reply->mp_SigBit);
    while ((m = IExec->GetMsg(reply)) != NULL)
    {
        /* il RexxMsg sintetico e' tornato: lo liberiamo sotto */
    }

    if (rcOut) *rcOut = msg->rm_Result1;
    /* rm_Result2 e' un argstring solo se rm_Result1 == 0. */
    if (msg->rm_Result1 == 0 && msg->rm_Result2 && result && resultSize)
    {
        strncpy(result, (STRPTR)msg->rm_Result2, resultSize - 1);
        result[resultSize - 1] = '\0';
    }

    gIRexx->DeleteArgstring(msg->rm_Args[0]);
    if (msg->rm_Result1 == 0 && msg->rm_Result2)
        gIRexx->DeleteArgstring((STRPTR)msg->rm_Result2);
    gIRexx->DeleteRexxMsg(msg);
    IExec->FreeSysObject(ASOT_PORT, reply);
}

/* Comandi che non dipendono da un pannello: restano disponibili anche
   quando il pannello della sessione e' stato chiuso. */
static BOOL PluginCommandNeedsPane(CONST_STRPTR cmd)
{
    static const char *const neutral[] = {
        "HELP", "VERSION", "PROTOCOL", "QUIT", "NEW", "SPLIT", "ASK", "DIALOG",
        "PROGRESS", "SCRIPT", NULL
    };
    char word[32];
    ULONG n = 0;
    LONG i;

    while (*cmd == ' ' || *cmd == '\t') cmd++;
    while (cmd[n] && cmd[n] != ' ' && cmd[n] != '\t' && n < sizeof(word) - 1)
    {
        word[n] = cmd[n];
        n++;
    }
    word[n] = '\0';

    for (i = 0; neutral[i]; i++)
        if (!IUtility->Stricmp(word, neutral[i])) return FALSE;
    return TRUE;
}

/* Legge ed esegue una richiesta req_* della cartella 'dir'.
   session == NULL: cartella comune, pannello attivo al momento.
   Restituisce TRUE se ha trovato una richiesta. */
static BOOL ProcessOneChannelRequest(CONST_STRPTR dir, struct PluginSession *session,
                                     struct Window *win)
{
    APTR ctx;
    struct ExamineData *ed;
    char name[256];
    char full[PATH_MAX_LEN];
    char replyPath[PATH_MAX_LEN];
    char cmd[2048];
    char result[4096];
    BPTR fh;
    LONG rc = RC_ERROR;
    int gp;
    int save = gActivePane;
    BOOL found = FALSE;
    BOOL run = TRUE;
    ULONG l;

    name[0] = '\0';
    result[0] = '\0';
    ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)dir,
            EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
    if (!ctx) return FALSE;

    while ((ed = IDOS->ExamineDir(ctx)) != NULL)
    {
        ULONG n = strlen(ed->Name);
        if (!EXD_IS_DIRECTORY(ed) && n > 4 && strncmp(ed->Name, "req_", 4) == 0)
        {
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, ed->Name, n);
            name[n] = '\0';
            found = TRUE;
            break;
        }
    }
    IDOS->ReleaseDirContext(ctx);
    if (!found) return FALSE;

    snprintf(full, sizeof(full), "%s/%s", dir, name);
    cmd[0] = '\0';
    fh = IDOS->Open(full, MODE_OLDFILE);
    if (fh)
    {
        if (!IDOS->FGets(fh, cmd, sizeof(cmd))) cmd[0] = '\0';
        IDOS->Close(fh);
    }
    IDOS->Delete(full);

    l = strlen(cmd);
    while (l > 0 && (cmd[l - 1] == '\n' || cmd[l - 1] == '\r'))
        cmd[--l] = '\0';

    snprintf(replyPath, sizeof(replyPath), "%s/rep_%s", dir, name + 4);

    if (!session)
        gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
    else if (SessionPaneAlive(session))
        gp = session->pane;
    else if (PluginCommandNeedsPane(cmd))
    {
        /* Il pannello del plugin non c'e' piu': niente ripiego silenzioso
           sull'altro pannello. */
        DBG("plugin session '%s': pane closed, '%s' refused", dir, cmd);
        run = FALSE;
        strcpy(result, "Pane closed");
        gp = 0;
    }
    else
        gp = 0;

    if (run)
    {
        gCurSessionDir = session ? session->dir : NULL;
        ExecutePluginCommand(cmd, gp, win, &rc, result, sizeof(result));
        gCurSessionDir = NULL;

        /* ExecuteRexxCommand rende attivo il pannello del comando: per una
           sessione legata all'altro pannello ripristiniamo quello scelto
           dall'utente (se esiste ancora). */
        if (session && gActivePane != save &&
            (save == 0 || gPanes[1].listGad))
            gActivePane = save;
    }

    fh = IDOS->Open(replyPath, MODE_NEWFILE);
    if (fh)
    {
        IDOS->FPrintf(fh, "%ld|%s", (long)rc, result);
        IDOS->Close(fh);
    }
    return TRUE;
}

/* Esegue tutte le richieste in attesa: cartella comune e sessioni. */
static void ProcessPluginRequests(struct Window *win)
{
    BOOL any;
    LONG i;

    if (!gPluginDir[0]) return;

    do
    {
        any = FALSE;
        if (ProcessOneChannelRequest(gPluginDir, NULL, win)) any = TRUE;
        for (i = 0; i < MAX_PLUGIN_SESSIONS; i++)
        {
            if (gSessions[i].used &&
                ProcessOneChannelRequest(gSessions[i].dir, &gSessions[i], win))
                any = TRUE;
        }
    } while (any && !gRexxQuit);
}

static void CleanupPluginDir(void)
{
    if (!gPluginDir[0]) return;
    DeleteChannelDir(gPluginDir, TRUE);
}

/* Manda il messaggio di notifica alla porta pubblica del drawer. */
static int NotifyVFM(CONST_STRPTR portName)
{
    struct MsgPort *target, *reply;
    struct AppMessage *msg;
    struct Message *m;

    if (!portName || !portName[0]) return 1;
    target = IExec->FindPort(portName);
    if (!target) return 2;

    reply = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    if (!reply) return 3;

    msg = (struct AppMessage *)IExec->AllocVecTags(sizeof(struct AppMessage),
              AVT_Type, MEMF_SHARED, TAG_DONE);
    if (!msg)
    {
        IExec->FreeSysObject(ASOT_PORT, reply);
        return 4;
    }

    memset(msg, 0, sizeof(*msg));
    msg->am_Message.mn_ReplyPort = reply;
    msg->am_Message.mn_Length = sizeof(*msg);
    msg->am_Type = VFMPLUGIN_TYPE;

    IExec->PutMsg(target, (struct Message *)msg);
    IExec->Wait(1UL << reply->mp_SigBit);
    while ((m = IExec->GetMsg(reply)) != NULL)
    {
        /* notifica consumata */
    }

    IExec->FreeVec(msg);
    IExec->FreeSysObject(ASOT_PORT, reply);
    return 0;
}

/* ===== Stato dei pannelli ===== */

static void RefreshAfterSelection(Object *listGad, struct Window *win)
{
    if (listGad)
        IIntuition->RefreshGList((struct Gadget *)listGad, win, NULL, 1);
    if (gMainWinObj)
        IIntuition->IDoMethod(gMainWinObj, WM_RETHINK);
}

/* Evidenzia la lista del pannello attivo. */
static void UpdatePaneMarkers(struct Window *win)
{
    int p;
    BOOL split = (gPanes[1].listGad != NULL);

    /* vista doppia: cornice blu attorno alla lista attiva (vista lista) */
    for (p = 0; p < 2; p++)
    {
        Object *lg = gPanes[p].listGad;
        ULONG on = (split && p == gActivePane) ? TRUE : FALSE;

        if (!lg) continue;
        if (win)
            IIntuition->SetGadgetAttrs((struct Gadget *)lg, win, NULL,
                FILEBROWSER_Highlight, on, TAG_DONE);
        else
            IIntuition->SetAttrs(lg, FILEBROWSER_Highlight, on, TAG_DONE);
    }
}

/* Aggiorna le barre di stato (e lo stato "ghost" dei pulsanti) di ENTRAMBI i
   pannelli. Serve per esempio dopo Copia/Taglia, cosi' il pulsante Incolla
   si abilita subito anche nell'altro pannello. */
static void UpdateAllStatuses(struct Window *win)
{
    int q, save = gActivePane;
    for (q = 0; q < 2; q++)
    {
        if (!gPanes[q].listGad) continue;
        gActivePane = q;
        UpdateStatus(gPanes[q].listGad, gPanes[q].statusGad, win);
    }
    gActivePane = save;
    UpdatePaneMarkers(win);
}

/* Cambia il pannello attivo e riallinea gli indicatori: il marcatore "*"
   nella riga del percorso segna il pannello su cui agiscono menu, pulsanti
   e comandi dei plugin; il titolo della finestra segue il percorso del
   pannello attivo. */
static void SetActivePane(int p, struct Window *win)
{
    if (p < 0 || p > 1) p = 0;
    if (p == 1 && !gPanes[1].listGad) p = 0;
    if (p == gActivePane) return;

    gActivePane = p;

    /* riallinea le barre di stato di entrambi i pannelli */
    UpdateAllStatuses(win);

    /* il titolo della finestra segue il pannello attivo */
    if (win)
        UpdateWindowTitle(win, gPanes[p].pathBuffer);
}

/* Una sola lista per volta puo' avere elementi selezionati: appena si
   seleziona qualcosa in un pannello, deseleziona l'altro.  Non tocca la
   selezione quando si cambia pannello senza selezionare (navigazione). */
static void ClearOtherPaneSelection(int gp, struct Window *win)
{
    int other = gp ^ 1;
    ULONG nsel = 0;

    if (!gPanes[gp].listGad || !gPanes[other].listGad) return;

    IIntuition->GetAttr(LISTBROWSER_NumSelected, gPanes[gp].listGad, &nsel);
    if (nsel == 0) return;                 /* nessuna selezione qui */
    IIntuition->GetAttr(LISTBROWSER_NumSelected, gPanes[other].listGad, &nsel);
    if (nsel == 0) return;                 /* l'altro e' gia' vuoto */

    DeselectAllEntries(&gPanes[other].fileList);
    RefreshAfterSelection(gPanes[other].listGad, win);
    UpdateStatus(gPanes[other].listGad, gPanes[other].statusGad, win);
}

/* ===== Navigazione da tastiera ===== */

static ULONG KeyQualifiers(void)
{
    ULONG qual = 0;

    if (gMainWinObj)
        IIntuition->GetAttr(WINDOW_Qualifier, gMainWinObj, &qual);
    return qual;
}

static LONG PaneSelectedIndex(struct List *list)
{
    struct Node *n;

    if (!list) return -1;
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE;
        ULONG index = MAX_ENTRIES;

        IListBrowser->GetListBrowserNodeAttrs(n,
            LBNA_Selected, &sel,
            LBNA_UserData, &index,
            TAG_DONE);
        if (sel && index < (ULONG)gEntryCount)
            return (LONG)index;
    }
    return -1;
}

static void SelectPaneIndex(int gp, LONG index, struct Window *win)
{
    struct Pane *pn = &gPanes[gp];
    struct Node *n;

    if (!pn->listGad || !win || pn->entryCount <= 0) return;

    if (index < 0) index = 0;
    if (index >= pn->entryCount) index = pn->entryCount - 1;

    for (n = pn->fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
        IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, FALSE, TAG_DONE);

    for (n = pn->fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG idx = MAX_ENTRIES;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &idx, TAG_DONE);
        if ((LONG)idx == index)
        {
            IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, TRUE, TAG_DONE);
            break;
        }
    }

    IIntuition->SetGadgetAttrs((struct Gadget *)pn->listGad, win, NULL,
        LISTBROWSER_MakeVisible, index, TAG_DONE);
    IIntuition->RefreshGList((struct Gadget *)pn->listGad, win, NULL, 1);
    UpdateStatus(pn->listGad, pn->statusGad, win);
    ClearOtherPaneSelection(gp, win);
}

static void MovePaneSelection(LONG delta, BOOL vertical, struct Window *win)
{
    struct Pane *pn = &gPanes[gActivePane];
    LONG index;
    LONG step = 1;

    if (!pn->listGad || pn->entryCount <= 0) return;

    if (vertical && pn->viewMode == FBVM_ICONS)
    {
        ULONG cols = 0;
        IIntuition->GetAttr(FILEBROWSER_Columns, pn->listGad, &cols);
        if (cols > 0) step = (LONG)cols;
    }

    index = PaneSelectedIndex(&pn->fileList);
    if (index < 0)
        index = 0;                       /* nessuna selezione: parti dal primo */
    else
        index += delta * step;

    SelectPaneIndex(gActivePane, index, win);
}

static void SelectByLetter(unsigned char ch, struct Window *win)
{
    struct Pane *pn = &gPanes[gActivePane];
    LONG i;
    int upper = toupper((int)ch);

    if (!pn->listGad || pn->entryCount <= 0) return;

    for (i = 0; i < pn->entryCount; i++)
    {
        if (pn->names[i][0] &&
            toupper((int)(unsigned char)pn->names[i][0]) == upper)
        {
            SelectPaneIndex(gActivePane, i, win);
            return;
        }
    }
}

static void ClearPaneSelection(struct Window *win)
{
    struct Pane *pn = &gPanes[gActivePane];

    if (!pn->listGad) return;
    DeselectAllEntries(&pn->fileList);
    RefreshAfterSelection(pn->listGad, win);
    UpdateStatus(pn->listGad, pn->statusGad, win);
}

/* ===== Menu contestuale ===== */

/* Hook WA_ContextMenuHook: Intuition lo chiama (dal task input.device)
   quando l'utente preme il tasto destro nella finestra.  Restituisce il
   menu per un file/cassetto o quello per lo spazio vuoto se il mouse e'
   sopra una lista, altrimenti NULL (menu normale della finestra).
   Qui non si tocca la selezione ne' si ridisegna nulla: lo fa il loop
   principale quando arriva la scelta (PrepareMenuPick). */
static uint32 ContextMenuHookFunc(struct Hook *hook, struct Window *win, struct ContextMenuMsg *msg)
{
    int p;

    (void)hook;
    if (!msg || msg->State != CM_QUERY) return 0;
    msg->Menu = NULL;
    msg->Context = NULL;
    if (!win || gCtxMenuBlocked || gDragActive || gDragButtonDown) return 0;
    /* Emulazione WB: nessun menu contestuale, il tasto destro mostra la
       barra dei menu come nel Workbench */
    if (gPrefs.wbEmulation) return 0;

    for (p = 0; p < 2; p++)
    {
        struct Gadget *g = (struct Gadget *)gPanes[p].listGad;
        struct FBGetIndexAt hi;
        Object *menu;

        if (!g || g->Width <= 0 || g->Height <= 0) continue;
        if (win->MouseX < g->LeftEdge || win->MouseX >= g->LeftEdge + g->Width ||
            win->MouseY < g->TopEdge  || win->MouseY >= g->TopEdge + g->Height)
            continue;

        memset(&hi, 0, sizeof(hi));
        hi.MethodID = FILEBROWSER_GetIndexAt;
        hi.fbgia_X = win->MouseX;
        hi.fbgia_Y = win->MouseY;
        hi.fbgia_FontY = (win->RPort && win->RPort->Font) ? win->RPort->Font->tf_YSize : 0;
        hi.fbgia_Index = -1;
        IIntuition->IDoMethodA((Object *)g, (Msg)&hi);

        SyncContextMenus();
        if (hi.fbgia_Index < 0)
            menu = gCtxMenuEmpty;
        else
        {
            /* file o cassetto?  Il nodo e' quello mostrato dal gadget in
               questo momento; l'indice in isDir[] viene da LBNA_UserData. */
            ULONG index = MAX_ENTRIES;
            BOOL isDir = TRUE;
            if (hi.fbgia_Node)
            {
                IListBrowser->GetListBrowserNodeAttrs(hi.fbgia_Node, LBNA_UserData, &index, TAG_DONE);
                if (index < MAX_ENTRIES) isDir = gPanes[p].isDir[index];
            }
            menu = (!isDir && gCtxMenuFile) ? gCtxMenuFile : gCtxMenuItem;
        }
        if (!menu) return 0;
        gCtxInfo.pane = p;
        gCtxInfo.pos  = hi.fbgia_Index;
        gCtxInfo.menu = menu;
        msg->Menu = menu;
        msg->Context = &gCtxInfo;
        return 0;
    }
    return 0;
}

/* Chiamata all'arrivo di WMHI_MENUPICK.  Per una scelta dal menu
   contestuale: rende attivo il pannello cliccato e, se la voce sotto il
   mouse non era selezionata, seleziona solo quella (se lo era, la
   selezione multipla resta).  Restituisce il pannello su cui eseguire il
   comando e in *root l'albero menu da cui leggere le voci scelte. */
static int PrepareMenuPick(struct Window *win, Object **root)
{
    ULONG type = IMT_DEFAULT;
    APTR ctx = NULL;
    int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;

    *root = gMenuRoot;
    if (!gMainWinObj) return gp;
    IIntuition->GetAttr(WINDOW_MenuType, gMainWinObj, &type);
    if (type != IMT_CONTEXT_WINDOW) return gp;
    IIntuition->GetAttr(WINDOW_MenuContext, gMainWinObj, (ULONG *)&ctx);
    if (ctx != &gCtxInfo || !gCtxInfo.menu) return gp;

    *root = gCtxInfo.menu;
    if (gCtxInfo.pane < 0 || gCtxInfo.pane > 1 || !gPanes[gCtxInfo.pane].listGad)
        return gp;
    gp = gCtxInfo.pane;

    if (gCtxInfo.pos >= 0 && win)
    {
        struct Node *n;
        LONG pos = 0;

        for (n = gPanes[gp].fileList.lh_Head; n->ln_Succ && pos < gCtxInfo.pos; n = n->ln_Succ)
            pos++;
        if (n->ln_Succ)
        {
            ULONG sel = FALSE, index = MAX_ENTRIES;
            IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, LBNA_UserData, &index, TAG_DONE);
            if (!sel && index < (ULONG)gPanes[gp].entryCount)
                SelectPaneIndex(gp, (LONG)index, win);
        }
    }
    return gp;
}

/* Enter, Shift+Enter, Backspace e Del sulla lista attiva.  Ignorano
   l'autorepeat: un tasto tenuto premuto non deve lanciare lo stesso
   programma piu' volte o riaprire la richiesta di eliminazione.
   Quando il campo percorso o il filtro sono attivi, Intuition passa i
   tasti al gadget stringa e qui non arriva nulla.
   Ritorna TRUE se il tasto e' stato gestito. */
#define PANEKEY_ENTER     1
#define PANEKEY_BACKSPACE 2
#define PANEKEY_DEL       3

static BOOL HandlePaneKey(int which, ULONG qual, struct Window *win)
{
    struct Pane *pn = &gPanes[gActivePane];

    if (!pn->listGad || !win) return FALSE;
    if (qual & IEQUALIFIER_REPEAT) return TRUE;

    switch (which)
    {
        case PANEKEY_ENTER:
            if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                /* come il pulsante Apri: cassetto in una nuova istanza */
                TryOpenSelected(pn->listGad, pn->pathGad, pn->statusGad, win, &pn->fileList, TRUE);
            else
                /* Emulazione WB: come il doppio click, i cassetti si
                   aprono in nuove finestre */
                OpenSelectedEntries(pn->listGad, pn->pathGad, pn->statusGad, win, &pn->fileList,
                                    gPrefs.wbEmulation ? TRUE : FALSE);
            break;

        case PANEKEY_BACKSPACE:
            GoUp(pn->listGad, pn->pathGad, pn->statusGad, win, &pn->fileList);
            break;

        case PANEKEY_DEL:
            if (CountSelectedEntries(&pn->fileList) > 0)
                DoDeleteSelected(pn->listGad, pn->pathGad, pn->statusGad, win, &pn->fileList);
            break;

        default:
            return FALSE;
    }
    UpdateAllStatuses(win);
    return TRUE;
}

/* Scorciatoie da tastiera che corrispondono a voci di menu (la
   combinazione e' mostrata accanto alla voce): stessa azione del menu,
   sul pannello attivo. */
static void DoKeyCommand(ULONG id, struct Window *win)
{
    int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
    struct Pane *pn = &gPanes[gp];
    struct List *fl = &pn->fileList;

    if (!win || !pn->listGad) return;

    switch (id)
    {
        case MENU_COPY:      DoCopySelected(fl); UpdateAllStatuses(win); break;
        case MENU_CUT:       DoCutSelected(fl); UpdateAllStatuses(win); break;
        case MENU_PASTE:     DoPaste(pn->listGad, pn->pathGad, pn->statusGad, win, fl);
                             UpdateAllStatuses(win); break;
        case MENU_RENAME:    DoRenameSelected(pn->listGad, pn->pathGad, pn->statusGad, win, fl); break;
        case MENU_DUPLICATE: DoDuplicateSelected(pn->listGad, pn->pathGad, pn->statusGad, win, fl); break;
        case MENU_FILEINFO:  DoInfo(fl, win); break;
        case MENU_NEW_DRAWER: DoNewDrawer(pn->listGad, pn->pathGad, pn->statusGad, win, fl); break;
        case MENU_SELECT_ALL:
            SelectAllEntries(fl);
            RefreshAfterSelection(pn->listGad, win);
            UpdateStatus(pn->listGad, pn->statusGad, win);
            break;
        case MENU_REFRESH:
            LoadPath(pn->pathBuffer, pn->listGad, pn->pathGad, pn->statusGad, win, fl);
            break;
        default: break;
    }
}

/* Ctrl+F / Ctrl+L: attiva il campo filtro o percorso del pannello attivo
   (niente se la barra e' nascosta). */
static void ActivatePaneField(BOOL filter)
{
    int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
    Object *gad = filter ? gPanes[gp].filterGad : gPanes[gp].pathGad;
    struct wmActivateGadget m;

    if (!gad || !gMainWinObj) return;
    m.MethodID = WM_ACTIVATEGADGET;
    m.wma_Object = (struct Gadget *)gad;
    IIntuition->IDoMethodA(gMainWinObj, (Msg)&m);
}

static void HandleVanillaKey(ULONG key, struct Window *win)
{
    ULONG qual = KeyQualifiers();

    if (qual & IEQUALIFIER_REPEAT)
    {
        /* tenendo premuto un tasto si continua solo a scorrere/selezionare */
        if (qual & (IEQUALIFIER_CONTROL | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
            return;
    }

    /* Amiga+tasto.  C/V/X/A/D/I sono scorciatoie di menu (Intuition le
       trasforma in una scelta di menu e disegna il simbolo Amiga accanto
       alla voce); qui arrivano solo se il menu non le ha prese, per
       esempio con Amiga sinistro.  F e L non hanno una voce di menu. */
    if ((qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) &&
        !(qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)))
    {
        switch (tolower((int)key))
        {
            case 'c': DoKeyCommand(MENU_COPY, win); break;
            case 'v': DoKeyCommand(MENU_PASTE, win); break;
            case 'x': DoKeyCommand(MENU_CUT, win); break;
            case 'a': DoKeyCommand(MENU_SELECT_ALL, win); break;
            case 'd': DoKeyCommand(MENU_DUPLICATE, win); break;
            case 'i': DoKeyCommand(MENU_FILEINFO, win); break;
            case 'f': ActivatePaneField(TRUE); break;
            case 'l': ActivatePaneField(FALSE); break;
            default: break;
        }
        return;
    }

    if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND |
                IEQUALIFIER_LALT | IEQUALIFIER_RALT))
        return;

    /* Ctrl+lettera: con VANILLAKEY arriva il carattere di controllo
       (Ctrl+A = 1 ... Ctrl+Z = 26), quindi Ctrl+I non va confuso con TAB */
    if (qual & IEQUALIFIER_CONTROL)
    {
        int letter = 0;
        BOOL handled = TRUE;

        /* Backspace (8) e Return (13) arrivano come Ctrl+H e Ctrl+M: non
           sono lettere.  Ctrl+I e Ctrl+TAB danno entrambi 9 e non si
           distinguono: vale come Ctrl+I.  I tasti non gestiti qui (Return,
           Backspace, Del, Esc) proseguono come senza Ctrl. */
        if (key >= 1 && key <= 26 && key != 8 && key != 13)
            letter = 'a' + (int)key - 1;
        else if (isalpha((int)key)) letter = tolower((int)key);
        switch (letter)
        {
            case 'c': DoKeyCommand(MENU_COPY, win); break;
            case 'v': DoKeyCommand(MENU_PASTE, win); break;
            case 'x': DoKeyCommand(MENU_CUT, win); break;
            case 'a': DoKeyCommand(MENU_SELECT_ALL, win); break;
            case 'd': DoKeyCommand(MENU_DUPLICATE, win); break;
            case 'i': DoKeyCommand(MENU_FILEINFO, win); break;
            case 'n': DoKeyCommand(MENU_NEW_DRAWER, win); break;
            case 'f': ActivatePaneField(TRUE); break;
            case 'l': ActivatePaneField(FALSE); break;
            default: handled = FALSE; break;
        }
        if (handled) return;
    }

    /* con IDCMP_VANILLAKEY Return/Enter, Backspace e Del arrivano qui */
    if (key == 13) { HandlePaneKey(PANEKEY_ENTER, qual, win); return; }
    if (key == 8)  { HandlePaneKey(PANEKEY_BACKSPACE, qual, win); return; }
    if (key == 127) { HandlePaneKey(PANEKEY_DEL, qual, win); return; }

    /* con IDCMP_VANILLAKEY, TAB ed ESC arrivano qui come caratteri */
    if (key == 9)
    {
        /* vista doppia: passa all'altra lista */
        if (!(qual & IEQUALIFIER_REPEAT)) gTabSwitchRequest = TRUE;
        return;
    }
    if (key == 27)
    {
        /* annulla la selezione corrente */
        ClearPaneSelection(win);
        return;
    }

    if (key >= 32 && key < 127 && isalpha((int)key))
        SelectByLetter((unsigned char)key, win);
}

static void HandleRawKey(ULONG key, struct Window *win)
{
    ULONG qual = KeyQualifiers();

    if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND |
                IEQUALIFIER_LALT | IEQUALIFIER_RALT))
        return;

    if (key & IECODE_UP_PREFIX) return;   /* solo pressione, non rilascio */

    /* di solito arrivano come VANILLAKEY; qui per sicurezza */
    switch (key & ~IECODE_UP_PREFIX)
    {
        case RAWKEY_RETURN:
        case RAWKEY_ENTER:     HandlePaneKey(PANEKEY_ENTER, qual, win); return;
        case RAWKEY_BACKSPACE: HandlePaneKey(PANEKEY_BACKSPACE, qual, win); return;
        case RAWKEY_DEL:       HandlePaneKey(PANEKEY_DEL, qual, win); return;
        default: break;
    }
    key &= ~IECODE_UP_PREFIX;

    switch (key)
    {
        case RAWKEY_CRSRUP:
            MovePaneSelection(-1, TRUE, win);
            break;

        case RAWKEY_CRSRDOWN:
            MovePaneSelection(1, TRUE, win);
            break;

        case RAWKEY_CRSRLEFT:
            MovePaneSelection(-1, FALSE, win);
            break;

        case RAWKEY_CRSRRIGHT:
            MovePaneSelection(1, FALSE, win);
            break;

        case RAWKEY_ESC:
            ClearPaneSelection(win);
            break;

        case RAWKEY_TAB:
            if (!(qual & (IEQUALIFIER_REPEAT | IEQUALIFIER_CONTROL)))
                gTabSwitchRequest = TRUE;
            break;

        case RAWKEY_F2:
            if (!(qual & IEQUALIFIER_REPEAT)) DoKeyCommand(MENU_RENAME, win);
            break;

        case RAWKEY_F5:
            if (!(qual & IEQUALIFIER_REPEAT)) DoKeyCommand(MENU_REFRESH, win);
            break;

        default:
            break;
    }
}

/* Ricarica la vista di un singolo pannello (imposta temporaneamente
   gActivePane, perche' LoadPath usa le macro del pannello attivo). */
static void RefreshPane(int p, struct Window *win)
{
    int save = gActivePane;
    if (p >= 0 && p <= 1 && gPanes[p].listGad && win)
    {
        gActivePane = p;
        LoadPath(gPanes[p].pathBuffer, gPanes[p].listGad, gPanes[p].pathGad,
                 gPanes[p].statusGad, win, &gPanes[p].fileList);
    }
    gActivePane = save;
}

/* Abilita i pulsanti della barra superiore: copia/sposta hanno senso solo
   con la vista divisa (serve il secondo pannello). */
static void UpdateTopBar(struct Window *win)
{
    BOOL split = (gPanes[1].listGad != NULL);

    if (gTopCopyBtn)
        IIntuition->SetGadgetAttrs((struct Gadget *)gTopCopyBtn, win, NULL,
            GA_Disabled, split ? FALSE : TRUE, TAG_DONE);
    if (gTopMoveBtn)
        IIntuition->SetGadgetAttrs((struct Gadget *)gTopMoveBtn, win, NULL,
            GA_Disabled, split ? FALSE : TRUE, TAG_DONE);
}

/* ---- Modalita' dialogo standalone ----
   Un addon puo' chiedere una finestra Reaction senza usare ARexx:
       VagaFileManager --dialog "<risultato>|<titolo>|<testo>|<valore>|<pulsanti>"
   oppure, per testi che contengono '|' o '"':
       VagaFileManager --dialog "@<file-specifica>"
   La specifica su file ha cinque righe: risultato, titolo, testo, valore,
   pulsanti.  Alla chiusura scrive "<indice>|<testo>" nel file risultato.
   Indice 0 = annullato, 1 = Invio/primo pulsante. */
static BOOL ReadDialogSpec(CONST_STRPTR spec, char *resultFile, ULONG resultSize,
                           char *title, ULONG titleSize,
                           char *prompt, ULONG promptSize,
                           char *value, ULONG valueSize,
                           char *buttons, ULONG buttonsSize)
{
    char *fields[5];
    ULONG sizes[5];
    int i;

    fields[0] = resultFile; sizes[0] = resultSize;
    fields[1] = title;      sizes[1] = titleSize;
    fields[2] = prompt;     sizes[2] = promptSize;
    fields[3] = value;      sizes[3] = valueSize;
    fields[4] = buttons;    sizes[4] = buttonsSize;

    for (i = 0; i < 5; i++) fields[i][0] = '\0';

    if (spec && spec[0] == '@')
    {
        BPTR fh = IDOS->Open(spec + 1, MODE_OLDFILE);
        if (!fh) return FALSE;
        for (i = 0; i < 5; i++)
        {
            ULONG n;
            if (!IDOS->FGets(fh, fields[i], sizes[i]))
            {
                fields[i][0] = '\0';
                continue;
            }
            n = strlen(fields[i]);
            while (n && (fields[i][n - 1] == '\n' || fields[i][n - 1] == '\r'))
                fields[i][--n] = '\0';
        }
        IDOS->Close(fh);
        return TRUE;
    }

    for (i = 0; i < 5 && spec; i++)
    {
        CONST_STRPTR sep = (i < 4) ? strchr(spec, '|') : NULL;
        ULONG n = sep ? (ULONG)(sep - spec) : (ULONG)strlen(spec);
        if (n >= sizes[i]) n = sizes[i] - 1;
        memcpy(fields[i], spec, n);
        fields[i][n] = '\0';
        spec = sep ? sep + 1 : NULL;
    }
    return TRUE;
}

/* ===== Punto di ingresso =====
   Apre le librerie, crea finestra e pannelli, poi gestisce il ciclo degli
   eventi fino alla chiusura. */
int main(int argc, char **argv)
{
    struct Window *windows[WID_LAST];
    Object *objects[OID_LAST];
    struct MsgPort *appPort = NULL;
    LONG ii;
    int p;
    CONST_STRPTR homePath = ""; /* home: assigns only */
    BOOL done = FALSE;
    BOOL centerMouse = FALSE;
    char startPath[PATH_MAX_LEN];

    /* Notifica per il canale comandi dei plugin: manda un messaggio alla
       porta del drawer e aspetta la risposta.  Non apre finestre. */
    if (argc > 2 && argv[1] && !strcmp(argv[1], "--notify"))
        return NotifyVFM(argv[2]);

    /* Diagnostica: verifica la visibilita' di una porta pubblica da un
       processo esterno.  Uso: VagaFileManager --findport VAGAFILEMANAGER */
    if (argc > 2 && argv[1] && !strcmp(argv[1], "--findport"))
    {
        struct MsgPort *port = IExec->FindPort(argv[2]);
        printf("FindPort(%s)=%p\n", argv[2], (void *)port);
        return port ? 0 : 1;
    }

    /* Dialogo standalone per gli addon: nessuna finestra principale,
       nessuna porta ARexx, solo window.class e il file risultato. */
    if (argc > 2 && argv[1] && !strcmp(argv[1], "--dialog"))
    {
        char resultFile[PATH_MAX_LEN] = "";
        char title[128] = "VFM";
        char prompt[256] = "";
        char value[512] = "";
        char buttons[128] = "OK|Cancel";
        LONG idx;
        BPTR out;

        if (!ReadDialogSpec(argv[2], resultFile, sizeof(resultFile),
                            title, sizeof(title), prompt, sizeof(prompt),
                            value, sizeof(value), buttons, sizeof(buttons)) ||
            !resultFile[0])
            return 20;

        idx = PromptDialog(title, prompt, value, sizeof(value), buttons);
        out = IDOS->Open(resultFile, MODE_NEWFILE);
        if (out)
        {
            IDOS->FPrintf(out, "%ld|%s", (long)idx, value);
            IDOS->Close(out);
        }
        return 0;
    }

    windows[WID_MAIN] = NULL; objects[OID_MAIN] = NULL;
    gActivePane = 0;
    memset(gPanes, 0, sizeof(gPanes));
    IExec->NewList(&gPanes[0].fileList);
    IExec->NewList(&gPanes[1].fileList);
    memset(gIcons, 0, sizeof(gIcons));

    for (p = 0; p < 2; p++)
    {
        memset(&gSortHooks[p], 0, sizeof(gSortHooks[p]));
        gSortHooks[p].h_Entry = (HOOKFUNC)SortCompareHook;
        gSortHooks[p].h_Data  = (APTR)(ULONG)p;
        memset(&gIconRenderHooks[p], 0, sizeof(gIconRenderHooks[p]));
        gIconRenderHooks[p].h_Entry = (HOOKFUNC)ListIconRenderHook;
        gIconRenderHooks[p].h_Data  = (APTR)(ULONG)p;
        memset(&gIconHooks[p], 0, sizeof(gIconHooks[p]));
        gIconHooks[p].h_Entry = (HOOKFUNC)IconHookFunc;
        gIconHooks[p].h_Data  = (APTR)(ULONG)p;
    }

    gClipboardCount = 0; gClipboardMode = CLIP_NONE;
    LoadSharedClipboard();   /* per lo stato iniziale del pulsante Incolla */
    memset(&gLoc, 0, sizeof(gLoc));

    /* Locale/catalog: default to system language; fall back to built-in strings */
    gLocaleBase = IExec->OpenLibrary("locale.library", 0);
    if (gLocaleBase)
        gLocale = (struct LocaleIFace *)IExec->GetInterface(gLocaleBase, "main", 1, NULL);

    if (gLocale)
    {
        gLoc.li_ILocale = gLocale;
        gLoc.li_Catalog = gLocale->OpenCatalog(NULL, "VagaFileManager.catalog",
                                              OC_BuiltInLanguage, "english",
                                              TAG_DONE);

        /* separatore decimale delle dimensioni (1.9M / 1,9M) */
        {
            struct Locale *loc = gLocale->OpenLocale(NULL);
            if (loc)
            {
                if (loc->loc_DecimalPoint && loc->loc_DecimalPoint[0])
                    gSizeDecimalPoint = (char)loc->loc_DecimalPoint[0];
                gLocale->CloseLocale(loc);
            }
        }
    }

    startPath[0] = '\0';
    if (argc > 1 && argv[1] && argv[1][0])
    {
        /* "-" significa "vista radice" (elenco volumi/assign). */
        if (strcmp(argv[1], "-") != 0)
        {
            strncpy(startPath, argv[1], sizeof(startPath) - 1);
            startPath[sizeof(startPath) - 1] = '\0';
        }
    }
    else if (!IDOS->GetCliCurrentDirName(startPath, sizeof(startPath)))
    {
        BPTR cur = IDOS->GetCurrentDir();
        if (cur)
            IDOS->NameFromLock(cur, startPath, sizeof(startPath));
        else
            startPath[0] = '\0';
    }
    if (startPath[0] == '\0')
        strncpy(startPath, homePath, sizeof(startPath)-1);
    startPath[sizeof(startPath)-1] = '\0';

    /* "-m" (secondo argomento) = nuova istanza centrata sul mouse. */
    if (argc > 2 && argv[2] && !strcmp(argv[2], "-m"))
        centerMouse = TRUE;

    ReadDebugToolType(argc, argv);
    LoadPrefs();

    /* Geometria della finestra: la chiave e' il volume del percorso
       iniziale.  Le nuove istanze sullo stesso volume la riusano; la
       navigazione non la cambia. */
    GetVolumeKey(startPath, gInstanceVolume, sizeof(gInstanceVolume));
    ApplyVolumeGeom(gInstanceVolume);
    /* Le istanze aperte con "-m" usano la dimensione del volume ma non
       la posizione memorizzata: vengono centrate sul mouse. */
    if (centerMouse) gWinPosValid = FALSE;

    gFbClass = MakeFileBrowserClass();
    if (!gFbClass)
    {
        if (gLoc.li_ILocale && gLoc.li_Catalog) gLoc.li_ILocale->CloseCatalog(gLoc.li_Catalog);
        if (gLocale) { IExec->DropInterface((struct Interface *)gLocale); gLocale = NULL; }
        if (gLocaleBase) { IExec->CloseLibrary(gLocaleBase); gLocaleBase = NULL; }
        return RETURN_FAIL;
    }
    snprintf(gDropPortName, sizeof(gDropPortName), "VFM_Drop_%08lx", (unsigned long)(ULONG)IExec->FindTask(NULL));
    gWinTag.vft_Magic = VFM_WINDOW_MAGIC;
    snprintf(gWinTag.vft_PortName, sizeof(gWinTag.vft_PortName), "%s", gDropPortName);
    /* elenco delle AppWindow per il drop verso altri programmi */
    AppRegistryEnsure();
    appPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT,
        ASOPORT_Name, (ULONG)gDropPortName,
        ASOPORT_Public, TRUE,
        TAG_DONE);
    DBG("VagaFileManager started, drop port '%s'", gDropPortName);

    gPanes[0].notifyPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    gPanes[1].notifyPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);

    /* ARexx host: porta pubblica con il nome del programma */
    gRexxBase = IExec->OpenLibrary("rexxsyslib.library", 0);
    if (gRexxBase)
        gIRexx = (struct RexxSysIFace *)IExec->GetInterface(gRexxBase, "main", 1, NULL);
    if (gIRexx)
    {
        if (IExec->FindPort((CONST_STRPTR)REXX_PORT_NAME))
            snprintf(gRexxPortName, sizeof(gRexxPortName), "%s.%08lx",
                     REXX_PORT_NAME, (unsigned long)(ULONG)IExec->FindTask(NULL));
        else
            snprintf(gRexxPortName, sizeof(gRexxPortName), "%s", REXX_PORT_NAME);

        gRexxPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT,
            ASOPORT_Name, (ULONG)gRexxPortName,
            ASOPORT_Public, TRUE,
            TAG_DONE);

        if (!gRexxPort && !strcmp(gRexxPortName, REXX_PORT_NAME))
        {
            /* fallback: nome univoco, la prima istanza conserva il nome base */
            snprintf(gRexxPortName, sizeof(gRexxPortName), "%s.%08lx",
                     REXX_PORT_NAME, (unsigned long)(ULONG)IExec->FindTask(NULL));
            gRexxPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT,
                ASOPORT_Name, (ULONG)gRexxPortName,
                ASOPORT_Public, TRUE,
                TAG_DONE);
        }
        DBG("ARexx port '%s' %s (FindPort=%p)",
            gRexxPortName, gRexxPort ? "opened" : "FAILED",
            gRexxPort ? (void *)IExec->FindPort(gRexxPortName) : NULL);
    }

    /* cartella plugin / script ARexx */
    CreateDirUnlocked(PLUGIN_DIR);
    ScanPlugins();

    /* Canale comandi dei plugin: directory privata dell'istanza. */
    snprintf(gPluginDir, sizeof(gPluginDir), "T:VFM_%08lx",
             (unsigned long)(ULONG)IExec->FindTask(NULL));
    CleanupPluginDir();
    {
        BPTR l = IDOS->CreateDir(gPluginDir);
        if (l) IDOS->UnLock(l);
    }
    /* Porta privata che riceve i DeathMessage dei plugin terminati. */
    gSessionPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    RefreshTimerOpen();
    DBG("plugin channel '%s' (session port %p)", gPluginDir, (void *)gSessionPort);

    /* lock the default public screen (Workbench) for the toolbar icons */
    gPubScreen = IIntuition->LockPubScreen(NULL);
    for (ii = 0; ii < IMG_COUNT; ii++)
    {
        char sel[64];
        gBtnImg[ii] = LoadIconImage(gPubScreen, gIconNames[ii]);
        snprintf(sel, sizeof(sel), "%s_s", gIconNames[ii]);
        gBtnImgSel[ii] = LoadIconImage(gPubScreen, sel);
    }
    LoadMenuImages(gPubScreen);
    ClampWindowGeom();

    if (centerMouse && gPubScreen)
    {
        gWinLeft = gPubScreen->MouseX - gWinInnerW / 2 + MOUSE_OFFSET_X;
        gWinTop  = gPubScreen->MouseY - gWinInnerH / 2 + MOUSE_OFFSET_Y;
        gWinPosValid = TRUE;
        ClampWindowGeom();
    }

    /* primo pannello */
    gActivePane = 0;
    PaneInit(0, startPath);
    ApplyPathPrefs(0, startPath);
    ReadDirectory(gPanes[0].pathBuffer, &gPanes[0].fileList);

    if (appPort)
    {
        CreatePaneUI(0);
        LoadBookmarks();
        BuildMenu();
        BuildContextMenus();

        /* riga dei due pannelli */
        gPanesRow = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_SpaceOuter, TRUE, LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)gPanes[0].paneLayout, CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 100,
            TAG_DONE);

        /* La barra dei comandi condivisa non esiste all'avvio: viene creata
           e aggiunta in basso solo quando si attiva la vista divisa. */
        gRootLayout = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE, LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)gPanesRow, CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 100,
            TAG_DONE);

        memset(&gKeyIDCMPHook, 0, sizeof(gKeyIDCMPHook));
        gKeyIDCMPHook.h_Entry = (HOOKFUNC)KeyIDCMPHook;
        memset(&gCtxMenuHook, 0, sizeof(gCtxMenuHook));
        gCtxMenuHook.h_Entry = (HOOKFUNC)ContextMenuHookFunc;

        objects[OID_MAIN] = IIntuition->NewObject(NULL, "window.class",
            WA_Title, (ULONG)LS(MSG_APP_TITLE),
            WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
            WA_InnerWidth, (ULONG)gWinInnerW, WA_InnerHeight, (ULONG)gWinInnerH,
            gWinPosValid ? WA_Left : TAG_IGNORE, gWinPosValid ? (ULONG)gWinLeft : 0,
            gWinPosValid ? WA_Top  : TAG_IGNORE, gWinPosValid ? (ULONG)gWinTop  : 0,
            WINDOW_IconifyGadget, TRUE, WINDOW_AppPort, (ULONG)appPort, WINDOW_AppWindow, TRUE,
            WINDOW_MenuStrip, (ULONG)gMenuRoot,
            WA_IDCMP, IDCMP_INTUITICKS | IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE |
                     IDCMP_VANILLAKEY | IDCMP_RAWKEY,
            WINDOW_GadgetHelp, TRUE,
            WINDOW_IDCMPHook, (ULONG)&gKeyIDCMPHook,
            WA_ContextMenuHook, (ULONG)&gCtxMenuHook,
            WINDOW_IDCMPHookBits, IDCMP_RAWKEY | IDCMP_VANILLAKEY,
            gWinPosValid ? TAG_IGNORE : WINDOW_Position,
            gWinPosValid ? (ULONG)0 : (ULONG)WPOS_CENTERMOUSE,
            WINDOW_Layout, (ULONG)gRootLayout,
            TAG_DONE);

        gMainWinObj = objects[OID_MAIN];

        if (objects[OID_MAIN])
        {
            windows[WID_MAIN] = (struct Window *)IIntuition->IDoMethod(objects[OID_MAIN], WM_OPEN);
            if (windows[WID_MAIN])
            {
                windows[WID_MAIN]->UserData = (BYTE *)&gWinTag;
                ULONG winSig = 0, appSig = 1UL << appPort->mp_SigBit;
                IIntuition->GetAttr(WINDOW_SigMask, objects[OID_MAIN], &winSig);
                gActivePane = 0;
                /* primo refresh all'apertura della finestra */
                LoadPath(gPanes[0].pathBuffer, gPanes[0].listGad, gPanes[0].pathGad,
                         gPanes[0].statusGad, windows[WID_MAIN], &gPanes[0].fileList);

                CaptureWindowSize(windows[WID_MAIN]);

                /* Default: vista singola. La divisione si apre solo su richiesta
                   (pulsante/voce "Dividi"). */
                gActivePane = 0;
                UpdateTopBar(windows[WID_MAIN]);
                UpdatePaneMarkers(windows[WID_MAIN]);

                while (!done)
                {
                    ULONG notifyBits = 0;
                    ULONG rexxSig = gRexxPort ? (1UL << gRexxPort->mp_SigBit) : 0;
                    ULONG dragSig = DragTimerSignal();
                    ULONG appDropSig = AppDropReplySignal();
                    ULONG sessionSig = gSessionPort ? (1UL << gSessionPort->mp_SigBit) : 0;
                    ULONG refreshSig = RefreshTimerSignal();
                    ULONG wait;

                    for (p = 0; p < 2; p++)
                        if (gPanes[p].notifyPort)
                            notifyBits |= 1UL << gPanes[p].notifyPort->mp_SigBit;

                    wait = IExec->Wait(winSig | appSig | notifyBits | rexxSig |
                                       dragSig | appDropSig | sessionSig | refreshSig |
                                       SIGBREAKF_CTRL_C);
                    if (wait & SIGBREAKF_CTRL_C) done = TRUE;

                    /* plugin terminati: via le loro cartelle di sessione */
                    if (sessionSig && (wait & sessionSig))
                        ProcessSessionDeaths();

                    /* risposte agli AppMessage dei drop verso altre app */
                    if (appDropSig && (wait & appDropSig))
                        ProcessAppDropReplies();
                    /* drag in corso: muove le icone ed evidenzia la destinazione */
                    if (dragSig && (wait & dragSig))
                        DragTick(windows[WID_MAIN]);

                    /* ---- comandi ARexx ---- */
                    if (rexxSig && (wait & rexxSig))
                    {
                        struct RexxMsg *rxmsg;
                        int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                        while ((rxmsg = (struct RexxMsg *)IExec->GetMsg(gRexxPort)) != NULL)
                        {
                            if (gIRexx && gIRexx->IsRexxMsg(rxmsg))
                            {
                                ExecuteRexxCommand(rxmsg, gp, windows[WID_MAIN]);
                                DBG("rexx: result1=%ld result2=%p",
                                    (long)rxmsg->rm_Result1,
                                    (void *)(ULONG)rxmsg->rm_Result2);
                                gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                            }
                            IExec->ReplyMsg((struct Message *)rxmsg);
                        }
                        if (gRexxQuit) done = TRUE;
                    }

                    /* ---- directory changes ---- */
                    for (p = 0; p < 2; p++)
                    {
                        struct MsgPort *np = gPanes[p].notifyPort;
                        if (!np) continue;
                        if (wait & (1UL << np->mp_SigBit))
                        {
                            struct Message *nm;
                            struct DateStamp ds;
                            ULONG ticks;
                            while ((nm = IExec->GetMsg(np)) != NULL)
                                IExec->ReplyMsg(nm);
                            /* lascia completare eventuali riscritture di .info
                               (es. tooltype appena modificati) prima di
                               rileggere la cartella */
                            IDOS->Delay(5);
                            IDOS->DateStamp(&ds);
                            ticks = (ULONG)ds.ds_Minute * 50 + ds.ds_Tick;
                            if (ticks < gPanes[p].lastRefreshTicks) ticks += 1440 * 50;  /* midnight rollover */
                            if (gPanes[p].lastRefreshTicks == 0 || ticks - gPanes[p].lastRefreshTicks >= REFRESH_MIN_TICKS)
                                RefreshPaneFromNotify(p, windows[WID_MAIN]);
                            else
                            {
                                /* troppo presto: rilettura differita, cosi'
                                   lo stato finale della cartella si vede
                                   sempre.  Senza timer si rilegge subito. */
                                gRefreshPending[p] = TRUE;
                                if (!RefreshTimerArm())
                                    RefreshPaneFromNotify(p, windows[WID_MAIN]);
                            }
                        }
                    }

                    /* ---- rilettura differita ---- */
                    if (refreshSig && (wait & refreshSig) && gRefreshTimerPending &&
                        IExec->CheckIO((struct IORequest *)gRefreshTimerReq))
                    {
                        IExec->WaitIO((struct IORequest *)gRefreshTimerReq);
                        gRefreshTimerPending = FALSE;
                        for (p = 0; p < 2; p++)
                            if (gRefreshPending[p])
                                RefreshPaneFromNotify(p, windows[WID_MAIN]);
                    }

                    /* ---- drag & drop da altri programmi / altri pannelli ---- */
                    if (wait & appSig)
                    {
                        struct AppMessage *appmsg;
                        while ((appmsg = (struct AppMessage *)IExec->GetMsg(appPort)) != NULL)
                        {
                            if (appmsg->am_Type == VFMPLUGIN_TYPE)
                            {
                                ProcessPluginRequests(windows[WID_MAIN]);
                                IExec->ReplyMsg((struct Message *)appmsg);
                                if (gRexxQuit) done = TRUE;
                            }
                            else if (appmsg->am_Type == VFMDROP_TYPE)
                            {
                                struct VFMDropMessage *drop = (struct VFMDropMessage *)appmsg;
                                int tp = -1;
                                if (windows[WID_MAIN] && gPanes[1].listGad)
                                {
                                    /* gadget coordinates include the window borders */
                                    WORD wx = (WORD)(drop->vfm_MouseX - windows[WID_MAIN]->LeftEdge);
                                    WORD wy = (WORD)(drop->vfm_MouseY - windows[WID_MAIN]->TopEdge);
                                    if (PointInPane(1, wx, wy)) tp = 1;
                                    else if (PointInPane(0, wx, wy)) tp = 0;
                                }
                                if (tp < 0) tp = 0;
                                SetActivePane(tp, windows[WID_MAIN]);
                                if (gPanes[tp].listGad)
                                    HandleVFMDrop(drop, gPanes[tp].listGad, gPanes[tp].pathGad,
                                                  gPanes[tp].statusGad, windows[WID_MAIN], &gPanes[tp].fileList);
                            }
                            else if (appmsg->am_Type == AMTYPE_APPWINDOW)
                            {
                                int tp = 0;
                                DBG("AppWindow drop: %ld args at %d,%d",
                                    (long)appmsg->am_NumArgs,
                                    (int)appmsg->am_MouseX, (int)appmsg->am_MouseY);
                                if (gPanes[1].listGad && PointInPane(1, appmsg->am_MouseX, appmsg->am_MouseY)) tp = 1;
                                SetActivePane(tp, windows[WID_MAIN]);
                                /* HandleAppDrop replies the message itself, before
                                   the Copy/Move requester */
                                if (gPanes[tp].listGad)
                                    HandleAppDrop(appmsg, gPanes[tp].listGad, gPanes[tp].pathGad,
                                                  gPanes[tp].statusGad, windows[WID_MAIN], &gPanes[tp].fileList);
                                else
                                    IExec->ReplyMsg((struct Message *)appmsg);
                            }
                            else if (appmsg->am_Type == AMTYPE_APPICON && appmsg->am_NumArgs == 0)
                            {
                                windows[WID_MAIN] = (struct Window *)IIntuition->IDoMethod(objects[OID_MAIN], WM_OPEN);
                                if (windows[WID_MAIN])
                                {
                                    windows[WID_MAIN]->UserData = (BYTE *)&gWinTag;
                                    IIntuition->GetAttr(WINDOW_SigMask, objects[OID_MAIN], &winSig);
                                    UpdateStatus(gPanes[gActivePane].listGad, gPanes[gActivePane].statusGad, windows[WID_MAIN]);
                                }
                                else done = TRUE;
                                IExec->ReplyMsg((struct Message *)appmsg);
                            }
                            else
                            {
                                IExec->ReplyMsg((struct Message *)appmsg);
                            }
                        }
                    }

                    /* ---- eventi finestra ---- */
                    if (wait & winSig)
                    {
                        ULONG result; WORD code = 0; BOOL needStatus = FALSE;
                        while ((result = IIntuition->IDoMethod(objects[OID_MAIN], WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                        {
                            switch (result & WMHI_CLASSMASK)
                            {
                                case WMHI_NEWSIZE:
                                    if (windows[WID_MAIN])
                                    {
                                        CaptureWindowSize(windows[WID_MAIN]);
                                    }
                                    break;
                                case WMHI_ACTIVE:
                                    /* un'altra istanza puo' aver aggiornato il
                                       clipboard condiviso: rileggilo e abilita
                                       il pulsante Incolla */
                                    LoadSharedClipboard();
                                    UpdateAllStatuses(windows[WID_MAIN]);
                                    break;
                                case WMHI_CLOSEWINDOW:
                                    CaptureWindowSize(windows[WID_MAIN]);
                                    windows[WID_MAIN]=NULL; done=TRUE; break;
                                case WMHI_ICONIFY:
                                    CaptureWindowSize(windows[WID_MAIN]);
                                    IIntuition->IDoMethod(objects[OID_MAIN], WM_ICONIFY);
                                    windows[WID_MAIN]=NULL; break;
                                case WMHI_UNICONIFY:
                                    windows[WID_MAIN] = (struct Window *)IIntuition->IDoMethod(objects[OID_MAIN], WM_OPEN);
                                    if (windows[WID_MAIN])
                                    {
                                        CaptureWindowSize(windows[WID_MAIN]);
                                        windows[WID_MAIN]->UserData = (BYTE *)&gWinTag;
                                        IIntuition->GetAttr(WINDOW_SigMask, objects[OID_MAIN], &winSig);
                                        UpdateStatus(gPanes[gActivePane].listGad, gPanes[gActivePane].statusGad, windows[WID_MAIN]);
                                    }
                                    break;
                                case WMHI_GADGETUP:
                                {
                                    ULONG gid = result & WMHI_GADGETMASK;

                                    /* barra superiore condivisa: agisce sul pannello
                                       attivo e trasferisce verso l'altro pannello */
                                    if (gid == GID_TOP_COPY || gid == GID_TOP_MOVE ||
                                        gid == GID_TOP_DELETE)
                                    {
                                        int src = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                                        int dst = src ^ 1;

                                        if (gid == GID_TOP_DELETE)
                                        {
                                            DoDeleteSelected(gPanes[src].listGad, gPanes[src].pathGad,
                                                             gPanes[src].statusGad, windows[WID_MAIN],
                                                             &gPanes[src].fileList);
                                        }
                                        else if (gPanes[1].listGad)
                                        {
                                            DoTransferToPane(&gPanes[src].fileList,
                                                             gPanes[src].pathBuffer,
                                                             gPanes[dst].pathBuffer,
                                                             (gid == GID_TOP_MOVE) ? TRUE : FALSE,
                                                             windows[WID_MAIN]);
                                            RefreshPane(dst, windows[WID_MAIN]);
                                            if (gid == GID_TOP_MOVE)
                                                RefreshPane(src, windows[WID_MAIN]);
                                        }
                                        UpdateAllStatuses(windows[WID_MAIN]);
                                        break;
                                    }

                                    {
                                    int gp = (gid >= PANE_ID_OFFSET) ? 1 : 0;
                                    ULONG bid = gid - (ULONG)gp * PANE_ID_OFFSET;

                                    if (gp == 0 || gPanes[1].listGad)
                                    {
                                        Object *listGad   = gPanes[gp].listGad;
                                        Object *pathGad   = gPanes[gp].pathGad;
                                        Object *statusGad = gPanes[gp].statusGad;
                                        Object *toggleGad = gPanes[gp].toggleGad;
                                        struct List *fl   = &gPanes[gp].fileList;
                                        BOOL paneChanged = (gActivePane != gp);

                                        SetActivePane(gp, windows[WID_MAIN]);
                                        switch (bid)
                                        {
                                            case GID_PATH:
                                            { STRPTR txt=NULL; IIntuition->GetAttr(STRINGA_TextVal, pathGad, (ULONG*)&txt); if (txt) NavigateTo(txt, listGad, pathGad, statusGad, windows[WID_MAIN], fl); break; }
                                            case GID_FILTER:
                                            {
                                                STRPTR txt=NULL;
                                                IIntuition->GetAttr(STRINGA_TextVal, gPanes[gp].filterGad, (ULONG*)&txt);
                                                if (txt && txt != gPanes[gp].filterBuffer)
                                                {
                                                    strncpy(gPanes[gp].filterBuffer, txt, sizeof(gPanes[gp].filterBuffer) - 1);
                                                    gPanes[gp].filterBuffer[sizeof(gPanes[gp].filterBuffer) - 1] = '\0';
                                                }
                                                RefreshPaneAfterFilter(gp, windows[WID_MAIN]);
                                                break;
                                            }
                                            case GID_CLEAR_FILTER:
                                                /* svuota il campo e mostra di nuovo tutto */
                                                ClearPaneFilter(gp, windows[WID_MAIN]);
                                                RefreshPaneAfterFilter(gp, windows[WID_MAIN]);
                                                break;
                                            case GID_BACK: GoUp(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_FORWARD: GoForward(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_OPEN: TryOpenSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl,TRUE); break;
                                            case GID_REFRESH: LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_HOME: NavigateTo(homePath,listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_RENAME: DoRenameSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_DELETE: DoDeleteSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_DUPLICATE: DoDuplicateSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_COPY: DoCopySelected(fl); UpdateAllStatuses(windows[WID_MAIN]); break;
                                            case GID_CUT: DoCutSelected(fl); UpdateAllStatuses(windows[WID_MAIN]); break;
                                            case GID_PASTE: DoPaste(listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateAllStatuses(windows[WID_MAIN]); break;
                                            case GID_INFO: DoInfo(fl, windows[WID_MAIN]); break;
                                            case GID_CALCSIZE: DoCalculateSize(listGad, pathGad, statusGad, windows[WID_MAIN], fl); break;
                                            case GID_SHELL: OpenShellHere(); break;
                                            case GID_SPLIT: if (windows[WID_MAIN]) ToggleSplit(windows[WID_MAIN], objects[OID_MAIN]); break;
                                            case GID_TOGGLE_VIEW:
                                                DoToggleView(listGad, toggleGad, windows[WID_MAIN]);
                                                LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl);
                                                UpdateMenu();
                                                needStatus=TRUE; break;
                                            case GID_VIEW_FILTER: gViewFilter = code; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_SORT_MODE: gSortMode = code; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                            case GID_LIST:
                                            {
                                                uint32 relEvent=0; IIntuition->GetAttrs(listGad, LISTBROWSER_RelEvent, &relEvent, TAG_DONE);
                                                if (relEvent==LBRE_DOUBLECLICK) TryOpenSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl,gPrefs.wbEmulation ? TRUE : FALSE);   /* Emulazione WB: cassetto in una nuova finestra */
                                                /* selezionando qui si svuota la selezione dell'altro pannello */
                                                ClearOtherPaneSelection(gp, windows[WID_MAIN]);
                                                /* release of a potential drag started on the list/icon gadget */
                                                if (gDragButtonDown)
                                                    PerformDragDrop(fl, windows[WID_MAIN]);
                                                needStatus=TRUE; break;
                                            }
                                            default: break;
                                        }
                                        if (paneChanged) UpdateMenu();
                                    }
                                    }
                                    break;
                                }
                                case WMHI_MENUPICK:
                                {
                                    ULONG ids[16];
                                    int idCount = 0, miIndex;
                                    struct mpNextSelect ns;
                                    Object *pickRoot = NULL;
                                    int gp = PrepareMenuPick(windows[WID_MAIN], &pickRoot);
                                    Object *listGad   = gPanes[gp].listGad;
                                    Object *pathGad   = gPanes[gp].pathGad;
                                    Object *statusGad = gPanes[gp].statusGad;
                                    Object *toggleGad = gPanes[gp].toggleGad;
                                    struct List *fl   = &gPanes[gp].fileList;

                                    SetActivePane(gp, windows[WID_MAIN]);

                                    /* menuclass: raccoglie tutti gli ID selezionati
                                       prima di eseguire i comandi (UpdateMenu puo'
                                       ricostruire l'albero) */
                                    if (pickRoot)
                                    {
                                        ns.MethodID = MM_NEXTSELECT;
                                        ns.mpns_Reserved = 0;
                                        ns.mpns_CurrentID = NO_MENU_ID;
                                        while (idCount < 16)
                                        {
                                            ULONG id = IIntuition->IDoMethodA(pickRoot, (Msg)&ns);
                                            if (id == NO_MENU_ID) break;
                                            ids[idCount++] = id;
                                            ns.mpns_CurrentID = id;
                                        }
                                    }
                                    for (miIndex = 0; miIndex < idCount; miIndex++)
                                    {
                                        ULONG menuID = ids[miIndex];
                                        if (menuID >= MENU_BOOKMARK_BASE && menuID < MENU_BOOKMARK_BASE + MAX_BOOKMARKS)
                                        {
                                            LONG bidx = (LONG)(menuID - MENU_BOOKMARK_BASE);
                                            if (bidx < gBookmarkCount)
                                            {
                                                NavigateTo(gBookmarks[bidx].path, listGad, pathGad, statusGad, windows[WID_MAIN], fl);
                                                /* il segnalibro conserva le sue impostazioni:
                                                   applicate dopo la navigazione, hanno la precedenza
                                                   su quelle memorizzate per il percorso */
                                                gViewFilter = gBookmarks[bidx].filter;
                                                gSortMode = gBookmarks[bidx].sortMode;
                                                SetViewMode(gBookmarks[bidx].viewMode, listGad, toggleGad, windows[WID_MAIN]);
                                                LoadPath(gPathBuffer, listGad, pathGad, statusGad, windows[WID_MAIN], fl);
                                                UpdateMenu();
                                            }
                                        }
                                        else if (menuID >= MENU_SCRIPT_BASE && menuID < MENU_SCRIPT_BASE + MAX_PLUGINS)
                                        {
                                            LONG sidx = (LONG)(menuID - MENU_SCRIPT_BASE);
                                            if (sidx < gPluginCount)
                                                RunPlugin(gPlugins[sidx]);
                                        }
                                        else
                                        {
                                            switch (menuID)
                                            {
                                                case MENU_NEWWINDOW: LaunchNewInstance(); break;
                                                case MENU_SHELL: OpenShellHere(); break;
                                                case MENU_NEW_DRAWER: DoNewDrawer(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_NEW_TEXTFILE: DoNewTextFile(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_SELECT_ALL: SelectAllEntries(fl); RefreshAfterSelection(listGad, windows[WID_MAIN]); needStatus=TRUE; break;
                                                case MENU_INVERT_SELECTION: InvertSelectionEntries(fl); RefreshAfterSelection(listGad, windows[WID_MAIN]); needStatus=TRUE; break;
                                                case MENU_DESELECT_ALL: DeselectAllEntries(fl); RefreshAfterSelection(listGad, windows[WID_MAIN]); needStatus=TRUE; break;
                                                case MENU_INFO: ShowAbout(windows[WID_MAIN]); break;
                                                case MENU_QUIT: done = TRUE; break;
                                                case MENU_RENAME: DoRenameSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_DELETE: DoDeleteSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_DUPLICATE: DoDuplicateSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_COPY: DoCopySelected(fl); UpdateAllStatuses(windows[WID_MAIN]); break;
                                                case MENU_CUT: DoCutSelected(fl); UpdateAllStatuses(windows[WID_MAIN]); break;
                                                case MENU_PASTE: DoPaste(listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateAllStatuses(windows[WID_MAIN]); break;
                                                case MENU_FILEINFO: DoInfo(fl, windows[WID_MAIN]); break;
                                                case MENU_CALCSIZE: DoCalculateSize(listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_SCRIPT_REFRESH: ScanPlugins(); UpdateMenu(); break;
                                                case MENU_SCRIPT_OPENDIR: CreateDirUnlocked(PLUGIN_DIR); NavigateTo((CONST_STRPTR)PLUGIN_DIR,listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_OPEN: TryOpenSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl,TRUE); break;
                                                case MENU_OPEN_WITH: DoOpenWith(windows[WID_MAIN], fl); break;
                                                case MENU_CTX_OPEN: OpenSelectedEntries(listGad,pathGad,statusGad,windows[WID_MAIN],fl,TRUE); needStatus=TRUE; break;
                                                case MENU_REFRESH: LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_TOGGLE_VIEW: DoToggleView(listGad, toggleGad, windows[WID_MAIN]); LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); needStatus=TRUE; break;
                                                case MENU_ICON_SIZE_AUTO: ApplyPaneIconSize(0, listGad, windows[WID_MAIN]); UpdateMenu(); break;
                                                case MENU_ICON_SIZE_32: ApplyPaneIconSize(32, listGad, windows[WID_MAIN]); UpdateMenu(); break;
                                                case MENU_ICON_SIZE_48: ApplyPaneIconSize(48, listGad, windows[WID_MAIN]); UpdateMenu(); break;
                                                case MENU_ICON_SIZE_64: ApplyPaneIconSize(64, listGad, windows[WID_MAIN]); UpdateMenu(); break;
                                                case MENU_ICON_SIZE_96: ApplyPaneIconSize(96, listGad, windows[WID_MAIN]); UpdateMenu(); break;
                                                case MENU_VIEW_ICONS: gViewFilter = VIEW_FILTER_ICONS_ONLY; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_VIEW_ALLFILES: gViewFilter = VIEW_FILTER_ALL_FILES; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_VIEW_SHOWINFO: gShowInfoFiles = !gShowInfoFiles; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); needStatus=TRUE; break;
                                                case MENU_SPLIT:
                                                    ToggleSplit(windows[WID_MAIN], gMainWinObj);
                                                    /* il pannello 1 puo' essere sparito */
                                                    gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                                                    listGad   = gPanes[gp].listGad;
                                                    pathGad   = gPanes[gp].pathGad;
                                                    statusGad = gPanes[gp].statusGad;
                                                    toggleGad = gPanes[gp].toggleGad;
                                                    fl        = &gPanes[gp].fileList;
                                                    needStatus = TRUE;
                                                    break;
                                                case MENU_WB_EMULATION:
                                                    /* Il menu principale non cambia: la
                                                       voce resta sempre raggiungibile per
                                                       tornare indietro. */
                                                    gPrefs.wbEmulation = gPrefs.wbEmulation ? 0 : 1;
                                                    /* il Workbench ha una finestra per
                                                       cassetto: si chiude la vista doppia */
                                                    if (gPrefs.wbEmulation && gPanes[1].listGad)
                                                        ToggleSplit(windows[WID_MAIN], gMainWinObj);
                                                    if (gPrefs.wbEmulation)
                                                    {
                                                        /* LoadPath passera' a icone: si
                                                           ricorda la vista per il ritorno */
                                                        gPreEmuView = (LONG)gViewMode;
                                                    }
                                                    else if (gPreEmuView >= 0)
                                                    {
                                                        /* torna la vista di prima */
                                                        gViewMode = (ULONG)gPreEmuView;
                                                        if (gViewMode != FBVM_ICONS)
                                                            gViewFilter = VIEW_FILTER_ALL_FILES;
                                                        gPreEmuView = -1;
                                                    }
                                                    RebuildPaneBars(windows[WID_MAIN]);
                                                    gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                                                    listGad   = gPanes[gp].listGad;
                                                    pathGad   = gPanes[gp].pathGad;
                                                    statusGad = gPanes[gp].statusGad;
                                                    toggleGad = gPanes[gp].toggleGad;
                                                    fl        = &gPanes[gp].fileList;
                                                    UpdateMenu();
                                                    SavePrefs();
                                                    needStatus = TRUE;
                                                    break;
                                                case MENU_BAR_PATH:
                                                case MENU_BAR_TOOLS:
                                                case MENU_BAR_FILTER:
                                                case MENU_BAR_STATUS:
                                                {
                                                    ULONG bit = (menuID == MENU_BAR_PATH)   ? BAR_PATH :
                                                                (menuID == MENU_BAR_TOOLS)  ? BAR_TOOLS :
                                                                (menuID == MENU_BAR_FILTER) ? BAR_FILTER : BAR_STATUS;
                                                    gPrefs.hiddenBars ^= bit;
                                                    RebuildPaneBars(windows[WID_MAIN]);
                                                    /* i gadget sono nuovi: i puntatori presi
                                                       all'inizio del menu non valgono piu' */
                                                    listGad   = gPanes[gp].listGad;
                                                    pathGad   = gPanes[gp].pathGad;
                                                    statusGad = gPanes[gp].statusGad;
                                                    toggleGad = gPanes[gp].toggleGad;
                                                    UpdateMenu();
                                                    SavePrefs();
                                                    break;
                                                }
                                                case MENU_SORT_TYPE: gSortMode = SORT_BY_TYPE; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_SORT_NAME: gSortMode = SORT_BY_NAME; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_SORT_DATE: gSortMode = SORT_BY_DATE; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_SORT_SIZE: gSortMode = SORT_BY_SIZE; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_ADDBOOKMARK: AddBookmark(); UpdateMenu(); break;
                                                case MENU_MANAGE: ManageBookmarks(objects[OID_MAIN]); break;
                                                default: break;
                                            }
                                        }
                                    }
                                    break;
                                }
                                case WMHI_MOUSEBUTTONS:
                                {
                                    /* SELECTUP is always delivered: an extra release signal for drags */
                                    if (code == SELECTUP)
                                    {
                                        if (gDragButtonDown)
                                        {
                                            int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                                            SetActivePane(gp, windows[WID_MAIN]);
                                            PerformDragDrop(&gPanes[gp].fileList, windows[WID_MAIN]);
                                        }
                                        else
                                        {
                                            /* click sul fondo della finestra:
                                               deseleziona, come "Deselect all" */
                                            BOOL changed = FALSE;
                                            for (p = 0; p < 2; p++)
                                            {
                                                ULONG nsel = 0;
                                                if (!gPanes[p].listGad) continue;
                                                IIntuition->GetAttr(LISTBROWSER_NumSelected,
                                                    gPanes[p].listGad, &nsel);
                                                if (nsel)
                                                {
                                                    DeselectAllEntries(&gPanes[p].fileList);
                                                    RefreshAfterSelection(gPanes[p].listGad, windows[WID_MAIN]);
                                                    changed = TRUE;
                                                }
                                            }
                                            if (changed)
                                                UpdateAllStatuses(windows[WID_MAIN]);
                                        }
                                    }
                                    break;
                                }
                                case WMHI_INTUITICK:
                                {
                                    if (windows[WID_MAIN])
                                    {
                                        int gp = -1;
                                        int fp;
                                        struct DateStamp ds;
                                        ULONG nowTicks;

                                        /* Filtro live con debounce: si aggiorna
                                           dopo 15 tick (0,3 s) senza nuove
                                           digitazioni, non a ogni carattere. */
                                        IDOS->DateStamp(&ds);
                                        nowTicks = (ULONG)ds.ds_Minute * 3000UL + ds.ds_Tick;
                                        for (fp = 0; fp < 2; fp++)
                                        {
                                            if (!gPanes[fp].listGad) continue;
                                            if (strcmp(gPanes[fp].filterBuffer,
                                                       gPanes[fp].filterSeen) != 0)
                                            {
                                                strncpy(gPanes[fp].filterSeen,
                                                        gPanes[fp].filterBuffer,
                                                        sizeof(gPanes[fp].filterSeen) - 1);
                                                gPanes[fp].filterSeen[sizeof(gPanes[fp].filterSeen) - 1] = '\0';
                                                gPanes[fp].filterChangeTicks = nowTicks;
                                            }
                                            if (strcmp(gPanes[fp].filterBuffer,
                                                       gPanes[fp].filterApplied) != 0 &&
                                                (nowTicks - gPanes[fp].filterChangeTicks) >= 15)
                                                RefreshPaneAfterFilter(fp, windows[WID_MAIN]);
                                        }
                                        {
                                            static ULONG lastClipCheck = 0;
                                            if (nowTicks - lastClipCheck >= 50)
                                            {
                                                lastClipCheck = nowTicks;
                                                LoadSharedClipboard();
                                                UpdateAllStatuses(windows[WID_MAIN]);
                                            }
                                        }
                                        for (p = 0; p < 2; p++)
                                        {
                                            if (!gPanes[p].listGad) continue;
                                            if (((struct Gadget *)gPanes[p].listGad)->Flags & GFLG_SELECTED) { gp = p; break; }
                                        }
                                        if (gp >= 0)
                                        {
                                            SetActivePane(gp, windows[WID_MAIN]);
                                            if (!gDragButtonDown)
                                            {
                                                /* press started on the list/icon gadget */
                                                gDragButtonDown = TRUE;
                                                gDragActive = FALSE;
                                                gDragStartX = windows[WID_MAIN]->MouseX;
                                                gDragStartY = windows[WID_MAIN]->MouseY;
                                            }
                                            else if (!gDragActive)
                                            {
                                                WORD dx = windows[WID_MAIN]->MouseX - gDragStartX;
                                                WORD dy = windows[WID_MAIN]->MouseY - gDragStartY;
                                                BOOL start = FALSE;

                                                if (dx < 0) dx = (WORD)-dx;
                                                if (dy < 0) dy = (WORD)-dy;
                                                if (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD)
                                                    start = TRUE;

                                                /* In vista LISTA il trascinamento dentro la
                                                   lista serve alla selezione multipla
                                                   (rubber band del listbrowser): avvia il
                                                   drag&drop dei file solo quando il
                                                   puntatore esce dalla lista. */
                                                if (start && gPanes[gp].viewMode != FBVM_ICONS)
                                                {
                                                    struct Gadget *lg = (struct Gadget *)gPanes[gp].listGad;
                                                    WORD mx = windows[WID_MAIN]->MouseX;
                                                    WORD my = windows[WID_MAIN]->MouseY;
                                                    if (lg &&
                                                        mx >= lg->LeftEdge && mx < lg->LeftEdge + lg->Width &&
                                                        my >= lg->TopEdge  && my < lg->TopEdge + lg->Height)
                                                        start = FALSE;
                                                }

                                                if (start)
                                                {
                                                    gDragActive = TRUE;
                                                    /* tell the list/icon gadget a drag started so it keeps
                                                       the current (possibly multi) selection on release */
                                                    ((struct Gadget *)gPanes[gp].listGad)->Flags &= ~GFLG_SELECTED;
                                                    /* visual feedback: visible drag&drop cursor */
                                                    StartDragVisual(&gPanes[gp].fileList, windows[WID_MAIN]);
                                                }
                                            }
                                        }
                                    }
                                    break;
                                }
                                case WMHI_VANILLAKEY:
                                {
                                    if (windows[WID_MAIN] && gPanes[gActivePane].listGad)
                                        HandleVanillaKey((ULONG)(result & WMHI_KEYMASK),
                                                         windows[WID_MAIN]);
                                    break;
                                }
                                case WMHI_RAWKEY:
                                {
                                    if (windows[WID_MAIN] && gPanes[gActivePane].listGad)
                                        HandleRawKey((ULONG)(result & WMHI_KEYMASK),
                                                     windows[WID_MAIN]);
                                    break;
                                }
                                default: break;
                            }
                        }
                        if (gTabSwitchRequest)
                        {
                            gTabSwitchRequest = FALSE;
                            if (windows[WID_MAIN] && gPanes[1].listGad)
                                SetActivePane(gActivePane ? 0 : 1, windows[WID_MAIN]);
                        }
                        if (needStatus && windows[WID_MAIN])
                        {
                            int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                            UpdateStatus(gPanes[gp].listGad, gPanes[gp].statusGad, windows[WID_MAIN]);
                        }
                    }
                }
            }
        }
    }

    /* ---- chiusura: salva le impostazioni e libera i pannelli ---- */
    SavePrefs();
    for (p = 0; p < 2; p++)
    {
        gActivePane = p;
        if (gPanes[p].listGad)
            IIntuition->SetAttrs(gPanes[p].listGad, LISTBROWSER_Labels, ~0, TAG_DONE);
        FreeEntries(&gPanes[p].fileList);
        if (gPanes[p].notifyActive) { IDOS->EndNotify(&gPanes[p].notify); gPanes[p].notifyActive = FALSE; }
    }
    gActivePane = 0;

    if (objects[OID_MAIN]) { IIntuition->DisposeObject(objects[OID_MAIN]); windows[WID_MAIN]=NULL; }
    if (gMenuRoot) { IIntuition->DisposeObject(gMenuRoot); gMenuRoot = NULL; }
    FreeContextMenus();
    FreeMenuImages();
    for (p = 0; p < 2; p++)
    {
        if (gPanes[p].colInfo) { IListBrowser->FreeLBColumnInfo(gPanes[p].colInfo); gPanes[p].colInfo = NULL; }
    }
    SessionsShutdown();
    RefreshTimerClose();
    CleanupPluginDir();
    if (appPort) IExec->FreeSysObject(ASOT_PORT, appPort);

    if (gRexxPort)
    {
        struct Message *rm;
        while ((rm = IExec->GetMsg(gRexxPort)) != NULL)
            IExec->ReplyMsg(rm);
        IExec->FreeSysObject(ASOT_PORT, gRexxPort);
        gRexxPort = NULL;
    }
    if (gIRexx) { IExec->DropInterface((struct Interface *)gIRexx); gIRexx = NULL; }
    if (gRexxBase) { IExec->CloseLibrary(gRexxBase); gRexxBase = NULL; }
    if (gFbClass) FreeFileBrowserClass(gFbClass);
    for (p = 0; p < 2; p++)
    {
        if (gPanes[p].notifyPort)
        {
            IExec->FreeSysObject(ASOT_PORT, gPanes[p].notifyPort);
            gPanes[p].notifyPort = NULL;
        }
    }
    if (gLoc.li_ILocale && gLoc.li_Catalog)
        gLoc.li_ILocale->CloseCatalog(gLoc.li_Catalog);
    if (gLocale) { IExec->DropInterface((struct Interface *)gLocale); gLocale = NULL; }
    if (gLocaleBase) { IExec->CloseLibrary(gLocaleBase); gLocaleBase = NULL; }
    for (ii = 0; ii < IMG_COUNT; ii++)
    {
        if (gBtnImg[ii])    IIntuition->DisposeObject(gBtnImg[ii]);
        if (gBtnImgSel[ii]) IIntuition->DisposeObject(gBtnImgSel[ii]);
    }
    if (gPubScreen) IIntuition->UnlockPubScreen(NULL, gPubScreen);
    ShutdownDragDropInput();
    AppRegistryRemove();
    FreeProgressBarClass();
    FreeDirectoryScratch();
    FreeCopyBuffer();
    return RETURN_OK;
}
