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

static const char version_tag[] __attribute__((used)) = "\0$VER: VagaFileManager 1.0 (19.11.2026)";

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

char gDropPortName[64];

struct Bookmark gBookmarks[MAX_BOOKMARKS];
LONG gBookmarkCount = 0;
struct VFMWindowTag gWinTag;

char gPlugins[MAX_PLUGINS][PATH_MAX_LEN];
LONG gPluginCount = 0;
WORD gLastDragX = 0, gLastDragY = 0;

static struct Hook gFilterIdcmpHook;
static int gFilterPendingPane = -1;

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
    "filter", "sortingandgrouping", "filesize", "shell", "split"
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

/* ===== Log di debug ===== */

void DBG(CONST_STRPTR fmt, ...)
{
    BPTR fh;
    va_list args;
    char buf[1024];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    fh = IDOS->Open("T:VagaFileManager.log", MODE_READWRITE);
    if (!fh)
        fh = IDOS->Open("T:VagaFileManager.log", MODE_NEWFILE);
    if (fh)
    {
        IDOS->ChangeFilePosition(fh, 0, OFFSET_END);
        IDOS->FPrintf(fh, "%s\n", buf);
        IDOS->Close(fh);
    }
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
            return TRUE;
        }
    }
    return FALSE;
}

void RememberPathPrefs(CONST_STRPTR path, ULONG view, ULONG filter, ULONG sort, ULONG showinfo)
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
                      gPanes[0].sortMode, gPanes[0].showInfoFiles);
    if (gPanes[1].paneLayout)
        RememberPathPrefs(gPanes[1].pathBuffer, gPanes[1].viewMode, gPanes[1].viewFilter,
                          gPanes[1].sortMode, gPanes[1].showInfoFiles);

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
    Object *pathRow, *toolRow, *filterRow;

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

    pn->pathGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_PATH + off, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, PATH_MAX_LEN - 1, STRINGA_TextVal, (ULONG)pn->pathBuffer,
        STRINGA_Buffer, (ULONG)pn->pathBuffer, TAG_DONE);

    pn->statusGad = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_STATUS + off, GA_ReadOnly, TRUE, GA_Text, (ULONG)LS(MSG_STATUS_EMPTY),
        GA_Underscore, 0, BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE);

    pn->filterGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_FILTER + off, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, NAME_MAX_LEN - 1,
        STRINGA_TextVal, (ULONG)pn->filterBuffer,
        STRINGA_Buffer, (ULONG)pn->filterBuffer,
        ICA_TARGET, ICTARGET_IDCMP,
        TAG_DONE);

    pn->listGad = IIntuition->NewObject(gFbClass, NULL,
        GA_ID, GID_LIST + off, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&pn->fileList, LISTBROWSER_ColumnInfo, (ULONG)pn->colInfo,
        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_TitleClickable, TRUE,
        LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_MultiSelect, TRUE,
        FILEBROWSER_ViewMode, pn->viewMode, FILEBROWSER_IconHook, (ULONG)&gIconHooks[p],
        FILEBROWSER_HiddenColumns, (1UL << COL_TYPE) | (1UL << COL_SIZE) | (1UL << COL_DATE) | (1UL << COL_COMMENT),
        TAG_DONE);

    pn->toggleGad = MakeIconButton(GID_TOGGLE_VIEW + off,
        (pn->viewMode == FBVM_ICONS) ? gBtnImg[IMG_DETAILS] : gBtnImg[IMG_ICONS],
        (pn->viewMode == FBVM_ICONS) ? gBtnImgSel[IMG_DETAILS] : gBtnImgSel[IMG_ICONS],
        (pn->viewMode == FBVM_ICONS) ? LS(MSG_BTN_DETAILS) : LS(MSG_BTN_ICONS));

    /* barra del percorso */
    pathRow = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_HOME + off, gBtnImg[IMG_HOME], gBtnImgSel[IMG_HOME], LS(MSG_BTN_HOME)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)pn->toggleGad, CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_BACK + off, gBtnImg[IMG_BACK], gBtnImgSel[IMG_BACK], LS(MSG_BTN_BACK)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_FORWARD + off, gBtnImg[IMG_FORWARD], gBtnImgSel[IMG_FORWARD], LS(MSG_BTN_FORWARD)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)MakeIconButton(GID_REFRESH + off, gBtnImg[IMG_REFRESH], gBtnImgSel[IMG_REFRESH], LS(MSG_BTN_REFRESH)), CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)pn->pathGad, CHILD_WeightedWidth, 100,
        TAG_DONE);

    /* barra operazioni; "Calcola dimensione" e "Dividi" */
    {
        Object *btnRename = MakeIconButton(GID_RENAME + off, gBtnImg[IMG_RENAME], gBtnImgSel[IMG_RENAME], LS(MSG_RENAME));
        Object *btnDelete = MakeIconButton(GID_DELETE + off, gBtnImg[IMG_DELETE], gBtnImgSel[IMG_DELETE], LS(MSG_DELETE));
        Object *btnDuplicate = MakeIconButton(GID_DUPLICATE + off, gBtnImg[IMG_DUPLICATE], gBtnImgSel[IMG_DUPLICATE], LS(MSG_DUPLICATE));
        Object *btnCopy = MakeIconButton(GID_COPY + off, gBtnImg[IMG_COPY], gBtnImgSel[IMG_COPY], LS(MSG_COPY));
        Object *btnCut = MakeIconButton(GID_CUT + off, gBtnImg[IMG_CUT], gBtnImgSel[IMG_CUT], LS(MSG_CUT));
        Object *btnPaste = MakeIconButton(GID_PASTE + off, gBtnImg[IMG_PASTE], gBtnImgSel[IMG_PASTE], LS(MSG_PASTE));
        Object *btnInfo = MakeIconButton(GID_INFO + off, gBtnImg[IMG_INFO], gBtnImgSel[IMG_INFO], LS(MSG_INFO));
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

    {
        Object *filterLabel = IIntuition->NewObject(NULL, "button.gadget",
            GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (ULONG)LS(MSG_FILTER),
            BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE);

        filterRow = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)filterLabel, CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)pn->filterGad, CHILD_WeightedWidth, 100,
            TAG_DONE);
    }

    pn->paneLayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)pathRow, CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)toolRow, CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)pn->listGad, CHILD_WeightedHeight, 100,
        LAYOUT_AddChild, (ULONG)filterRow, CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)pn->statusGad, CHILD_WeightedHeight, 0,
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

/* Attiva/disattiva la modalita' split: mostra o nasconde il secondo
   pannello e la barra comandi condivisa. */
static void ToggleSplit(struct Window *win, Object *winObj)
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
        PaneInit(1, start);
        /* eredita le impostazioni del pannello 0, poi applica quelle del path */
        pn->viewMode      = gPanes[0].viewMode;
        pn->viewFilter    = gPanes[0].viewFilter;
        pn->sortMode      = gPanes[0].sortMode;
        pn->showInfoFiles = gPanes[0].showInfoFiles;
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

static uint32 FilterIDCMPHook(struct Hook *hook, Object *obj, struct IntuiMessage *msg)
{
    (void)hook;
    (void)obj;

    if (msg->Class == IDCMP_IDCMPUPDATE && msg->IAddress)
    {
        ULONG gid = IUtility->GetTagData(GA_ID, 0, (struct TagItem *)msg->IAddress);
        int p = (gid >= PANE_ID_OFFSET) ? 1 : 0;
        ULONG bid = gid - (ULONG)p * PANE_ID_OFFSET;

        if (bid == GID_FILTER && gPanes[p].filterGad)
        {
            STRPTR txt = NULL;
            IIntuition->GetAttr(STRINGA_TextVal, gPanes[p].filterGad, (ULONG *)&txt);
            if (txt && txt != gPanes[p].filterBuffer)
            {
                strncpy(gPanes[p].filterBuffer, txt, sizeof(gPanes[p].filterBuffer) - 1);
                gPanes[p].filterBuffer[sizeof(gPanes[p].filterBuffer) - 1] = '\0';
            }
            gFilterPendingPane = p;
        }
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

    gActivePane = p;
    IIntuition->SetGadgetAttrs((struct Gadget *)pn->listGad, win, NULL,
        LISTBROWSER_Labels, ~0, TAG_DONE);
    ReadDirectory(pn->pathBuffer, &pn->fileList);
    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)pn->listGad, win, NULL,
        LISTBROWSER_Labels, (ULONG)&pn->fileList, TAG_DONE);
    UpdateStatus(pn->listGad, pn->statusGad, win);
    gActivePane = save;
}

/* ---- plugin (script ARexx) ---- */

void RunPlugin(CONST_STRPTR path)
{
    char absPath[PATH_MAX_LEN];
    char cmd[PATH_MAX_LEN * 2 + 128];
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
            DBG("plugin run: Python not found (C:Python / C:Python3)");
            ShowMessage(LS(MSG_APP_TITLE),
                "Python not found: install C:Python or C:Python3.");
            return;
        }

        /* I plugin Python ricevono anche il path del pannello attivo:
           e' il path della finestra al momento del lancio. */
        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\" \"%s\"",
                 py, absPath, gRexxPortName, gPathBuffer);
        stack = 1024 * 1024;
    }
    else
        snprintf(cmd, sizeof(cmd), "C:RX \"%s\"", absPath);
    {
        LONG rc = IDOS->SystemTags(cmd, SYS_Asynch, TRUE, SYS_Input, 0, SYS_Output, 0,
                                   SYS_StackSize, stack, TAG_DONE);
        DBG("plugin run: rc=%ld cmd=%s", (long)rc, cmd);
        if (rc == -1)
        {
            DBG("plugin run: failed to start");
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

static void RexxSelectedNames(int gp, STRPTR out, ULONG outSize)
{
    struct Node *n;
    out[0] = '\0';
    for (n = gPanes[gp].fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE;
        STRPTR name = NULL;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel,
            LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!sel || !name) continue;
        if (out[0]) strncat(out, " ", outSize - strlen(out) - 1);
        strncat(out, name, outSize - strlen(out) - 1);
    }
}

static BOOL SelectEntryByName(Object *listGad, struct List *list, CONST_STRPTR name)
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
            IIntuition->SetAttrs(listGad,
                LISTBROWSER_Selected, idx,
                LISTBROWSER_MakeVisible, idx,
                TAG_DONE);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL GetSearchRoot(int gp, STRPTR out, ULONG outSize)
{
    struct Pane *pn = &gPanes[gp];
    struct Node *node;
    ULONG selCount;

    if (!out || outSize == 0) return FALSE;
    out[0] = '\0';

    /* Se e' selezionato un solo cassetto/volume, cerca dentro quello;
       altrimenti usa la directory corrente. */
    selCount = CountSelectedEntries(&pn->fileList);
    if (selCount == 1)
    {
        ULONG index = MAX_ENTRIES;
        STRPTR name = NULL;
        node = GetFirstSelectedNode(&pn->fileList);
        if (node)
        {
            IListBrowser->GetListBrowserNodeAttrs(node, LBNA_UserData, &index,
                LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
            if (name && index < (ULONG)pn->entryCount && pn->isDir[index])
            {
                if (pn->pathBuffer[0] == '\0')
                {
                    strncpy(out, name, outSize - 1);
                    out[outSize - 1] = '\0';
                }
                else if (!BuildChildPath(pn->pathBuffer, name, out, outSize))
                    out[0] = '\0';

                if (out[0]) return TRUE;
            }
        }
    }

    strncpy(out, pn->pathBuffer, outSize - 1);
    out[outSize - 1] = '\0';
    return out[0] != '\0';
}

static BOOL RexxRenameSelected(int gp, CONST_STRPTR newName)
{
    struct Node *node = GetFirstSelectedNode(&gPanes[gp].fileList);
    ULONG index = MAX_ENTRIES;
    STRPTR name = NULL;
    char oldPath[PATH_MAX_LEN], newPath[PATH_MAX_LEN];

    if (!node || !newName || !newName[0]) return FALSE;
    if (IsRootPath(gPathBuffer)) return FALSE;
    IListBrowser->GetListBrowserNodeAttrs(node, LBNA_UserData, &index,
        LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
    if (!name || index >= (ULONG)gEntryCount) return FALSE;
    if (!BuildChildPath(gPathBuffer, name, oldPath, sizeof(oldPath))) return FALSE;
    if (!BuildChildPath(gPathBuffer, newName, newPath, sizeof(newPath))) return FALSE;
    if (!IDOS->Rename(oldPath, newPath)) return FALSE;
    MoveInfoFile(oldPath, newPath);
    return TRUE;
}

static LONG RexxDeleteSelected(int gp, struct Window *win)
{
    struct Node *n;
    LONG count = 0;
    ULONG total = 0;
    BOOL useProgress = FALSE;

    if (IsRootPath(gPanes[gp].pathBuffer)) return 0;

    /* conta gli oggetti (file + cassetti) per la barra di avanzamento */
    for (n = gPanes[gp].fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;
        char full[PATH_MAX_LEN];

        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index,
            LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (!BuildChildPath(gPathBuffer, name, full, sizeof(full))) continue;
        total += CountDeleteObjects(full);
    }
    if (total > 0)
    {
        ProgressOpen(win, total);
        useProgress = TRUE;
    }

    SetDeleteProgress(TRUE);
    for (n = gPanes[gp].fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;
        char full[PATH_MAX_LEN];

        if (useProgress && gProgCancel) break;

        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index,
            LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;

        if (IsRootPath(gPathBuffer))
        {
            strncpy(full, name, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
        }
        else if (!BuildChildPath(gPathBuffer, name, full, sizeof(full)))
            continue;

        if (DeleteEntryRecursive(full, 0)) { DeleteSiblingInfo(full); count++; }
    }
    SetDeleteProgress(FALSE);
    if (useProgress) ProgressClose();
    return count;
}

static int64 RexxSelectedSize(int gp)
{
    struct Node *n;
    int64 total = 0;
    BOOL any = FALSE;

    for (n = gPanes[gp].fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;
        char full[PATH_MAX_LEN];

        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index,
            LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount || !gIsDir[index]) continue;

        if (IsRootPath(gPathBuffer))
        {
            strncpy(full, name, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
        }
        else if (!BuildChildPath(gPathBuffer, name, full, sizeof(full)))
            continue;

        {
            int64 b = 0;
            if (CalculatePathSize(full, &b, NULL, NULL)) { total += b; any = TRUE; }
        }
    }
    return any ? total : -1;
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
        RexxSetResult(msg,
            "CD <path> | OPEN [name] | UP | FORWARD | REFRESH | PATH | ENTRIES | SELECTED | "
            "RENAME <name> | DELETE | DUPLICATE | COPY | CUT | PASTE | INFO | SIZE | "
            "VIEW ICONS|ALLFILES | SORT TYPE|NAME|DATE|SIZE | MODE ICONS|DETAILS | "
            "SPLIT | NEW | SCRIPT <file> | ASK <prompt> | "
            "DIALOG <title>|<prompt>|<text>|<buttons> | "
            "REVEAL <path> | SEARCHPATH | FIND | GREP | QUIT | VERSION");
    }
    else if (!IUtility->Stricmp(cmd, "VERSION"))
        RexxSetResult(msg, "VagaFileManager 1.0");
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
    else if (!IUtility->Stricmp(cmd, "SEARCHPATH"))
    {
        char root[PATH_MAX_LEN];
        if (GetSearchRoot(gp, root, sizeof(root)))
            RexxSetResult(msg, root);
        else
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "");
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
        char *fields[4];
        ULONG sizes[4];
        LONG f;
        char *s = args;

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

        {
            LONG idx = PromptDialog(title, prompt, text, sizeof(text), buttons);
            if (idx > 0)
            {
                char result[512];
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
        char parent[PATH_MAX_LEN];
        if (!args[0])
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "Missing path");
        }
        else if (GetParentPath(args, parent, sizeof(parent)))
        {
            CONST_STRPTR targetName = FilePart(args);
            BOOL selected;

            NavigateTo(parent, listGad, pathGad, statusGad, win, fl);
            selected = SelectEntryByName(listGad, fl, targetName);
            if (!selected && gViewFilter == VIEW_FILTER_ICONS_ONLY)
            {
                /* il file potrebbe essere nascosto dal filtro "solo icone" */
                gViewFilter = VIEW_FILTER_ALL_FILES;
                LoadPath(parent, listGad, pathGad, statusGad, win, fl);
                selected = SelectEntryByName(listGad, fl, targetName);
                UpdateMenu();
            }
            if (!selected)
            {
                msg->rm_Result1 = RC_ERROR;
                RexxSetResult(msg, "File not found");
            }
            else
            {
                if (win) IIntuition->ActivateWindow(win);
                RexxSetResult(msg, "OK");
            }
        }
        else
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "Cannot reveal path");
        }
    }
    else if (!IUtility->Stricmp(cmd, "ENTRIES"))
    {
        char b[32];
        snprintf(b, sizeof(b), "%ld", (long)gEntryCount);
        RexxSetResult(msg, b);
    }
    else if (!IUtility->Stricmp(cmd, "SELECTED"))
    {
        char b[1024];
        RexxSelectedNames(gp, b, sizeof(b));
        RexxSetResult(msg, b);
    }
    else if (!IUtility->Stricmp(cmd, "RENAME"))
    {
        if (RexxRenameSelected(gp, args))
            LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, fl);
        else
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "Rename failed");
        }
    }
    else if (!IUtility->Stricmp(cmd, "DELETE"))
    {
        if (RexxDeleteSelected(gp, win) > 0)
            LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, fl);
        else
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "Nothing deleted");
        }
    }
    else if (!IUtility->Stricmp(cmd, "DUPLICATE"))
        DoDuplicateSelected(listGad, pathGad, statusGad, win, fl);
    else if (!IUtility->Stricmp(cmd, "COPY"))
        DoCopySelected(fl);
    else if (!IUtility->Stricmp(cmd, "CUT"))
        DoCutSelected(fl);
    else if (!IUtility->Stricmp(cmd, "PASTE"))
        DoPaste(listGad, pathGad, statusGad, win, fl);
    else if (!IUtility->Stricmp(cmd, "INFO"))
        DoInfo(fl, win);
    else if (!IUtility->Stricmp(cmd, "FIND"))
        DoFindFile(win);
    else if (!IUtility->Stricmp(cmd, "GREP"))
        DoGrep(win);
    else if (!IUtility->Stricmp(cmd, "SIZE"))
    {
        int64 sz = RexxSelectedSize(gp);
        if (sz < 0)
        {
            msg->rm_Result1 = RC_ERROR;
            RexxSetResult(msg, "No directory selected");
        }
        else
        {
            char b[32];
            snprintf(b, sizeof(b), "%lld", (long long)sz);
            RexxSetResult(msg, b);
        }
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
    else if (!IUtility->Stricmp(cmd, "SCRIPT"))
        RunPlugin(args);
    else
    {
        msg->rm_Result1 = RC_ERROR;
        RexxSetResult(msg, "Unknown command");
    }

    {
        int cp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
        if (gPanes[cp].listGad)
            UpdateStatus(gPanes[cp].listGad, gPanes[cp].statusGad, win);
    }
}

/* ===== Stato dei pannelli ===== */

static void RefreshAfterSelection(Object *listGad, struct Window *win)
{
    if (listGad)
        IIntuition->RefreshGList((struct Gadget *)listGad, win, NULL, 1);
    if (gMainWinObj)
        IIntuition->IDoMethod(gMainWinObj, WM_RETHINK);
}

/* Aggiorna le barre di stato (e lo stato "ghost" dei pulsanti) di ENTRAMBI i
   pannelli. Serve per esempio dopo Copia/Taglia, cosi' il pulsante Incolla
   si abilita subito anche nell'altro pannello. */
static void UpdateAllStatuses(struct Window *win)
{
    int q, save = gActivePane;
    for (q = 0; q < 2; q++)
    {
        if (!gPanes[q].listGad || !gPanes[q].statusGad) continue;
        gActivePane = q;
        UpdateStatus(gPanes[q].listGad, gPanes[q].statusGad, win);
    }
    gActivePane = save;
}

/* Cambia il pannello attivo e riallinea gli indicatori: "* " nella barra di
   stato segna il pannello su cui agiscono menu e pulsanti, e il titolo della
   finestra segue il percorso del pannello attivo. */
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
        DBG("ARexx port '%s' %s", gRexxPortName, gRexxPort ? "opened" : "FAILED");
    }

    /* cartella plugin / script ARexx */
    CreateDirUnlocked(PLUGIN_DIR);
    ScanPlugins();


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
        memset(&gFilterIdcmpHook, 0, sizeof(gFilterIdcmpHook));
        gFilterIdcmpHook.h_Entry = (HOOKFUNC)FilterIDCMPHook;

        CreatePaneUI(0);
        LoadBookmarks();
        BuildMenu();

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

        objects[OID_MAIN] = IIntuition->NewObject(NULL, "window.class",
            WA_Title, (ULONG)LS(MSG_APP_TITLE),
            WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
            WA_InnerWidth, (ULONG)gWinInnerW, WA_InnerHeight, (ULONG)gWinInnerH,
            gWinPosValid ? WA_Left : TAG_IGNORE, gWinPosValid ? (ULONG)gWinLeft : 0,
            gWinPosValid ? WA_Top  : TAG_IGNORE, gWinPosValid ? (ULONG)gWinTop  : 0,
            WINDOW_IconifyGadget, TRUE, WINDOW_AppPort, (ULONG)appPort, WINDOW_AppWindow, TRUE,
            WINDOW_MenuStrip, (ULONG)gMenuRoot,
            WA_IDCMP, IDCMP_INTUITICKS | IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE | IDCMP_IDCMPUPDATE,
            WINDOW_IDCMPHook, (ULONG)&gFilterIdcmpHook,
            WINDOW_IDCMPHookBits, IDCMP_IDCMPUPDATE,
            WINDOW_GadgetHelp, TRUE,
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

                while (!done)
                {
                    ULONG notifyBits = 0;
                    ULONG rexxSig = gRexxPort ? (1UL << gRexxPort->mp_SigBit) : 0;
                    ULONG wait;

                    for (p = 0; p < 2; p++)
                        if (gPanes[p].notifyPort)
                            notifyBits |= 1UL << gPanes[p].notifyPort->mp_SigBit;

                    wait = IExec->Wait(winSig | appSig | notifyBits | rexxSig | SIGBREAKF_CTRL_C);
                    if (wait & SIGBREAKF_CTRL_C) done = TRUE;

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
                            if (gPanes[p].lastRefreshTicks == 0 || ticks - gPanes[p].lastRefreshTicks >= 25)
                            {
                                int save = gActivePane;
                                gPanes[p].lastRefreshTicks = (ULONG)ds.ds_Minute * 50 + ds.ds_Tick;
                                DBG("directory changed, refreshing pane %d", p);
                                if (windows[WID_MAIN] && gPanes[p].listGad)
                                {
                                    gActivePane = p;
                                    LoadPath(gPanes[p].pathBuffer, gPanes[p].listGad, gPanes[p].pathGad,
                                             gPanes[p].statusGad, windows[WID_MAIN], &gPanes[p].fileList);
                                }
                                gActivePane = save;
                            }
                        }
                    }

                    /* ---- drag & drop da altri programmi / altri pannelli ---- */
                    if (wait & appSig)
                    {
                        struct AppMessage *appmsg;
                        while ((appmsg = (struct AppMessage *)IExec->GetMsg(appPort)) != NULL)
                        {
                            if (appmsg->am_Type == VFMDROP_TYPE)
                            {
                                struct VFMDropMessage *drop = (struct VFMDropMessage *)appmsg;
                                int tp = -1;
                                if (windows[WID_MAIN] && gPanes[1].listGad)
                                {
                                    WORD wx = (WORD)(drop->vfm_MouseX - windows[WID_MAIN]->LeftEdge - windows[WID_MAIN]->BorderLeft);
                                    WORD wy = (WORD)(drop->vfm_MouseY - windows[WID_MAIN]->TopEdge - windows[WID_MAIN]->BorderTop);
                                    if (PointInPane(1, wx, wy)) tp = 1;
                                    else if (PointInPane(0, wx, wy)) tp = 0;
                                }
                                if (tp < 0) tp = 0;
                                gActivePane = tp;
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
                                gActivePane = tp;
                                if (gPanes[tp].listGad)
                                    HandleAppDrop(appmsg, gPanes[tp].listGad, gPanes[tp].pathGad,
                                                  gPanes[tp].statusGad, windows[WID_MAIN], &gPanes[tp].fileList);
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
                                                if (relEvent==LBRE_DOUBLECLICK) TryOpenSelected(listGad,pathGad,statusGad,windows[WID_MAIN],fl,FALSE);
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
                                    int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                                    Object *listGad   = gPanes[gp].listGad;
                                    Object *pathGad   = gPanes[gp].pathGad;
                                    Object *statusGad = gPanes[gp].statusGad;
                                    Object *toggleGad = gPanes[gp].toggleGad;
                                    struct List *fl   = &gPanes[gp].fileList;

                                    gActivePane = gp;
                                    /* menuclass: raccoglie tutti gli ID selezionati
                                       prima di eseguire i comandi (UpdateMenu puo'
                                       ricostruire l'albero) */
                                    if (gMenuRoot)
                                    {
                                        ns.MethodID = MM_NEXTSELECT;
                                        ns.mpns_Reserved = 0;
                                        ns.mpns_CurrentID = NO_MENU_ID;
                                        while (idCount < 16)
                                        {
                                            ULONG id = IIntuition->IDoMethodA(gMenuRoot, (Msg)&ns);
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
                                                case MENU_REFRESH: LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); break;
                                                case MENU_TOGGLE_VIEW: DoToggleView(listGad, toggleGad, windows[WID_MAIN]); LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); needStatus=TRUE; break;
                                                case MENU_VIEW_ICONS: gViewFilter = VIEW_FILTER_ICONS_ONLY; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_VIEW_ALLFILES: gViewFilter = VIEW_FILTER_ALL_FILES; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); break;
                                                case MENU_VIEW_SHOWINFO: gShowInfoFiles = !gShowInfoFiles; LoadPath(gPathBuffer,listGad,pathGad,statusGad,windows[WID_MAIN],fl); UpdateMenu(); needStatus=TRUE; break;
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
                                    if (code == SELECTUP && gDragButtonDown)
                                    {
                                        int gp = (gActivePane == 1 && gPanes[1].listGad) ? 1 : 0;
                                        gActivePane = gp;
                                        PerformDragDrop(&gPanes[gp].fileList, windows[WID_MAIN]);
                                    }
                                    break;
                                }
                                case WMHI_INTUITICK:
                                {
                                    if (windows[WID_MAIN])
                                    {
                                        int gp = -1;
                                        for (p = 0; p < 2; p++)
                                        {
                                            if (!gPanes[p].listGad) continue;
                                            if (((struct Gadget *)gPanes[p].listGad)->Flags & GFLG_SELECTED) { gp = p; break; }
                                        }
                                        if (gp >= 0)
                                        {
                                            gActivePane = gp;
                                            /* memorizza l'ultima posizione del puntatore durante il drag,
                                               così il menu di drop usa la posizione di rilascio */
                                            gLastDragX = windows[WID_MAIN]->MouseX;
                                            gLastDragY = windows[WID_MAIN]->MouseY;
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
                                default: break;
                            }
                        }
                        if (gFilterPendingPane >= 0 && windows[WID_MAIN])
                        {
                            int fp = gFilterPendingPane;
                            gFilterPendingPane = -1;
                            if (fp == 0 || gPanes[1].listGad)
                                RefreshPaneAfterFilter(fp, windows[WID_MAIN]);
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
    FreeMenuImages();
    for (p = 0; p < 2; p++)
    {
        if (gPanes[p].colInfo) { IListBrowser->FreeLBColumnInfo(gPanes[p].colInfo); gPanes[p].colInfo = NULL; }
    }
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
    FreeProgressBarClass();
    return RETURN_OK;
}
