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

/* WB2VFM - sostituisce le finestre dei cassetti del Workbench
 * (fino alla build 70 si chiamava WB2VagaFileManager)
 *
 * Utility Commodities (gestibile da Exchange) che:
 *  - patcha Intuition->OpenWindowTagList via SetMethod(): quando il
 *    Workbench apre un cassetto o un volume, lancia VagaFileManager su
 *    quel percorso e impedisce al Workbench di aprire la propria finestra;
 *  - con un doppio click sullo sfondo del desktop (finestra radice del
 *    Workbench, non su un'icona) apre un'istanza di VagaFileManager su
 *    RAM:, o sul percorso del ToolType DESKTOPPATH dell'icona di WB2VFM
 *    (es. DESKTOPPATH=Work:).
 *
 * Uso:
 *   WB2VFM [percorso/VagaFileManager]
 *
 * Senza argomento usa l'eseguibile VagaFileManager nella stessa
 * directory di WB2VFM (percorso assoluto).
 *
 * Appare in Exchange come "WB2VFM"; da li' si puo' abilitare/disabilitare
 * (entrambe le funzioni) o terminare (Kill).
 */

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <devices/inputevent.h>
#include <intuition/intuitionbase.h>
#include <graphics/layers.h>
#include <libraries/commodities.h>
#include <interfaces/commodities.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/layers.h>
#include <proto/icon.h>

#include "VagaFileManager_version.h"

#define PATH_MAX_LEN 1024
/* Stessa versione di VFM. */
#define WB2_VERSION VFM_VERSION
/* La commodity ha lo stesso numero di build di VFM. */
#define WB2_BUILD   VFM_BUILD

static const char wb2_version_tag[] __attribute__((used)) =
    "\0$VER: WB2VFM " WB2_VERSION " build " VFM_STR(WB2_BUILD) " (" VFM_VERSION_DATE ")";

static BOOL gActive = FALSE;
static char gExePath[PATH_MAX_LEN];

static struct Library *gCxBase = NULL;
static struct CommoditiesIFace *gICx = NULL;
static struct MsgPort *gCxPort = NULL;
static struct MsgPort *gClosePort = NULL;
static CxObj *gBroker = NULL;

/* Doppio click sullo sfondo del desktop -> VFM su questo percorso
   (ToolType DESKTOPPATH, default RAM:). */
#define DESKTOP_PATH     "RAM:"
static char gDesktopPath[PATH_MAX_LEN] = DESKTOP_PATH;
#define CLICK_SENDER_ID  1
#define CLICK_MAX_MOVE   4      /* pixel di tolleranza fra i due click */

/* ultimo click sinistro (per riconoscere il doppio click) */
static BOOL  gLastClickValid = FALSE;
static ULONG gLastClickSecs = 0, gLastClickMicros = 0;
static WORD  gLastClickX = 0, gLastClickY = 0;

struct CloseMsg
{
    struct Message msg;
    char path[PATH_MAX_LEN];
};

typedef struct Window *(*OpenWindowTagListFunc)(struct IntuitionIFace *,
                                                const struct NewWindow *,
                                                const struct TagItem *);
static OpenWindowTagListFunc gOrigOpenWindowTagList = NULL;

/* ===== Log ===== */

/* T:WB2VFM.log riceve sempre gli errori (LogErr); il resto (Log) solo
   con il ToolType DEBUG nell'icona di WB2VFM (DEBUG=FALSE lo spegne). */
static BOOL gDebug = FALSE;

static void LogWrite(CONST_STRPTR prefix, CONST_STRPTR fmt, va_list ap)
{
    BPTR fh;
    char buf[1024];

    vsnprintf(buf, sizeof(buf), fmt, ap);

    fh = IDOS->Open("T:WB2VFM.log", MODE_READWRITE);
    if (!fh)
        fh = IDOS->Open("T:WB2VFM.log", MODE_NEWFILE);
    if (fh)
    {
        IDOS->ChangeFilePosition(fh, 0, OFFSET_END);
        IDOS->FPrintf(fh, "%s%s\n", prefix, buf);
        IDOS->Close(fh);
    }
}

static void Log(CONST_STRPTR fmt, ...)
{
    va_list ap;
    if (!gDebug) return;
    va_start(ap, fmt);
    LogWrite("", fmt, ap);
    va_end(ap);
}

static void LogErr(CONST_STRPTR fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogWrite("ERROR: ", fmt, ap);
    va_end(ap);
}

/* ===== Avvio di VFM ===== */

static void LaunchVFM(CONST_STRPTR path)
{
    char cmd[PATH_MAX_LEN * 2 + 32];
    LONG err;
    if (!gExePath[0] || !path || !path[0]) return;
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", gExePath, path);
    err = IDOS->SystemTags(cmd, SYS_Asynch, TRUE, SYS_Input, 0, SYS_Output, 0,
                           SYS_StackSize, 262144, TAG_DONE);
    if (err == -1)
        LogErr("cannot launch '%s'", cmd);
    else
        Log("launch cmd='%s' rc=%ld", cmd, (long)err);
}

/* Richiesta pendente del doppio click sul desktop (vedi HandleLeftClick):
   la finestra che il Workbench apre per quel percorso viene sostituita da
   VFM come quella di un'icona, cosi' VFM parte dal processo Workbench e
   ne eredita il path dei comandi (la commodity, avviata da WBStartup, non
   ne ha uno: make & co. non si troverebbero nella Shell aperta da VFM). */
static volatile BOOL  gDesktopPending = FALSE;
static volatile ULONG gDesktopPendingTicks = 0;
static char gDesktopLaunch[PATH_MAX_LEN];

/* Tick (1/50 s) dall'inizio del giorno. */
static ULONG NowTicks(void)
{
    struct DateStamp ds;
    IDOS->DateStamp(&ds);
    return (ULONG)ds.ds_Minute * 60 * 50 + (ULONG)ds.ds_Tick;
}

/* Sostituisce la finestra che il Workbench sta aprendo per 'full' con
   VFM.  Gira nel processo Workbench (dentro OpenWindowTagList). */
static struct Window *InterceptDrawerWindow(struct IntuitionIFace *Self,
                                            const struct NewWindow *newWindow,
                                            const struct TagItem *tagList,
                                            CONST_STRPTR full)
{
    struct Window *hidden = NULL;

    /* Non restituire NULL: Workbench mostrerebbe
       "Questo cassetto non può essere aperto".
       Apriamo una finestra valida ma invisibile/1x1 fuori
       schermo, poi lanciamo VFM. */
    if (newWindow)
    {
        struct NewWindow nw = *newWindow;
        nw.LeftEdge = -32000;
        nw.TopEdge  = -32000;
        nw.Width    = 1;
        nw.Height   = 1;
        nw.Flags   |= WFLG_BORDERLESS;
        hidden = gOrigOpenWindowTagList ?
            gOrigOpenWindowTagList(Self, &nw, tagList) : NULL;
    }
    Log("hidden window %p", (void *)hidden);

    LaunchVFM(full);

    /* Chiudi l'oggetto Workbench (la finestra nascosta) dal
       lato Workbench: CloseWindow cross-task blocca il
       sistema.  Un secondo dopo il setup e' completato. */
    if (gClosePort)
    {
        struct CloseMsg *cm = (struct CloseMsg *)
            IExec->AllocVecTags(sizeof(struct CloseMsg),
                AVT_Type, MEMF_SHARED,
                AVT_ClearWithValue, 0, TAG_DONE);
        if (cm)
        {
            cm->msg.mn_Length = sizeof(struct CloseMsg);
            strncpy(cm->path, full, sizeof(cm->path) - 1);
            cm->path[sizeof(cm->path) - 1] = '\0';
            IExec->PutMsg(gClosePort, (struct Message *)cm);
        }
    }
    return hidden;
}

static struct Window *Patched_OpenWindowTagList(struct IntuitionIFace *Self,
                                                const struct NewWindow *newWindow,
                                                const struct TagItem *tagList)
{
    struct Task *me = IExec->FindTask(NULL);
    CONST_STRPTR tname = (me && me->tc_Node.ln_Name) ? me->tc_Node.ln_Name : NULL;

    if (gActive && tname &&
        (IUtility->Stricmp(tname, "Workbench") == 0 ||
         IUtility->Stricmp(tname, "wb") == 0))
    {
        struct Screen *scr = IIntuition->LockPubScreen("Workbench");
        if (scr)
        {
            WORD mx = scr->MouseX, my = scr->MouseY;
            struct Window *w = scr->FirstWindow;
            struct Window *hit = NULL;

            while (w)
            {
                if (mx >= w->LeftEdge && mx < w->LeftEdge + w->Width &&
                    my >= w->TopEdge  && my < w->TopEdge  + w->Height)
                { hit = w; break; }
                w = w->NextWindow;
            }

            if (hit && IWorkbench)
            {
                char full[PATH_MAX_LEN];
                ULONG type = 0;
                struct TagItem tags[4];
                ULONG which;

                full[0] = '\0';
                tags[0].ti_Tag  = WBOBJA_FullPath;
                tags[0].ti_Data = (ULONG)full;
                tags[1].ti_Tag  = WBOBJA_FullPathSize;
                tags[1].ti_Data = sizeof(full);
                tags[2].ti_Tag  = WBOBJA_Type;
                tags[2].ti_Data = (ULONG)&type;
                tags[3].ti_Tag  = TAG_DONE;
                tags[3].ti_Data = 0;

                which = IWorkbench->WhichWorkbenchObjectA(hit,
                            mx - hit->LeftEdge, my - hit->TopEdge, tags);

                if (which == WBO_ICON && full[0] &&
                    (type == WBDRAWER || type == WBDISK))
                {
                    IIntuition->UnlockPubScreen(NULL, scr);
                    Log("intercept '%s' (type=%lu)", full, (unsigned long)type);
                    return InterceptDrawerWindow(Self, newWindow, tagList, full);
                }
            }
            IIntuition->UnlockPubScreen(NULL, scr);
        }

        /* Richiesta del doppio click sul desktop: questa e' la finestra
           che il Workbench apre per il percorso chiesto.  Le icone hanno
           la precedenza (controllate sopra).  La commodity annulla la
           richiesta dopo mezzo secondo; qui si scarta comunque una
           richiesta vecchia di piu' di 1 secondo.  Lettura e azzeramento
           del flag sono atomici: la commodity puo' annullarlo insieme. */
        {
            BOOL took;
            IExec->Forbid();
            took = gDesktopPending;
            gDesktopPending = FALSE;
            IExec->Permit();
            if (took)
            {
                ULONG now = NowTicks();
                if (now >= gDesktopPendingTicks && now - gDesktopPendingTicks < 50)
                {
                    Log("intercept desktop request '%s'", gDesktopLaunch);
                    return InterceptDrawerWindow(Self, newWindow, tagList, gDesktopLaunch);
                }
                Log("desktop request expired");
            }
        }
    }

    return gOrigOpenWindowTagList ?
        gOrigOpenWindowTagList(Self, newWindow, tagList) : NULL;
}

/* ===== Doppio click sullo sfondo del desktop ===== */

/* TRUE se il punto (mx,my) dello schermo Workbench cade sulla finestra
   radice del Workbench (backdrop, del task Workbench) e non su un'icona,
   e lo schermo Workbench e' quello in primo piano. */
static BOOL IsDesktopBackground(struct Screen *scr, WORD mx, WORD my)
{
    struct Layer *layer;
    struct Window *win = NULL;
    BOOL ok = FALSE;
    ULONG ilock;

    if (mx < 0 || my < 0 || mx >= scr->Width || my >= scr->Height) return FALSE;

    /* un altro schermo davanti al Workbench: il click non e' sul desktop */
    ilock = IIntuition->LockIBase(0);
    ok = (((struct IntuitionBase *)IntuitionBase)->FirstScreen == scr) ? TRUE : FALSE;
    IIntuition->UnlockIBase(ilock);
    if (!ok) return FALSE;
    ok = FALSE;

    /* finestra in primo piano sotto il puntatore (ordine dei layer) */
    ILayers->LockLayerInfo(&scr->LayerInfo);
    layer = ILayers->WhichLayer(&scr->LayerInfo, mx, my);
    if (layer) win = layer->Window;
    if (win && (win->Flags & WFLG_BACKDROP) && win->UserPort &&
        win->UserPort->mp_SigTask)
    {
        CONST_STRPTR tname = ((struct Task *)win->UserPort->mp_SigTask)->tc_Node.ln_Name;
        if (tname && (IUtility->Stricmp(tname, "Workbench") == 0 ||
                      IUtility->Stricmp(tname, "wb") == 0))
            ok = TRUE;
    }
    ILayers->UnlockLayerInfo(&scr->LayerInfo);

    /* sopra un'icona il doppio click la apre: non e' lo sfondo */
    if (ok && IWorkbench &&
        IWorkbench->WhichWorkbenchObjectA(win, mx - win->LeftEdge,
                                          my - win->TopEdge, NULL) == WBO_ICON)
        ok = FALSE;

    return ok;
}

/* Un click sinistro (copia dell'evento inviata dal filtro commodity). */
static void HandleLeftClick(const struct InputEvent *ie)
{
    struct Screen *scr;
    WORD mx, my;
    ULONG secs, micros;
    BOOL dbl = FALSE;

    if (!ie) return;
    secs = ie->ie_TimeStamp.Seconds;
    micros = ie->ie_TimeStamp.Microseconds;

    scr = IIntuition->LockPubScreen("Workbench");
    if (!scr) return;
    mx = scr->MouseX;
    my = scr->MouseY;

    if (gLastClickValid &&
        IIntuition->DoubleClick(gLastClickSecs, gLastClickMicros, secs, micros) &&
        mx - gLastClickX <= CLICK_MAX_MOVE && gLastClickX - mx <= CLICK_MAX_MOVE &&
        my - gLastClickY <= CLICK_MAX_MOVE && gLastClickY - my <= CLICK_MAX_MOVE)
        dbl = TRUE;

    if (dbl)
    {
        /* un terzo click non deve contare come un altro doppio click */
        gLastClickValid = FALSE;
        if (IsDesktopBackground(scr, mx, my))
        {
            IIntuition->UnlockPubScreen(NULL, scr);
            /* percorso non raggiungibile (disco tolto, assign
               mancante): si apre RAM: invece di una finestra vuota */
            CONST_STRPTR path = gDesktopPath;
            BPTR l = IDOS->Lock(path, SHARED_LOCK);
            if (l) IDOS->UnLock(l);
            else
            {
                LogErr("DESKTOPPATH '%s' not found, using %s", path, DESKTOP_PATH);
                path = DESKTOP_PATH;
            }
            Log("desktop double-click at %ld,%ld -> %s",
                (long)mx, (long)my, path);

            /* Si chiede al Workbench di aprire il percorso: la patch
               intercetta la sua finestra e lancia VFM dal processo
               Workbench (path dei comandi corretto).  Se il Workbench
               rifiuta, si lancia VFM direttamente come prima. */
            strncpy(gDesktopLaunch, path, sizeof(gDesktopLaunch) - 1);
            gDesktopLaunch[sizeof(gDesktopLaunch) - 1] = '\0';
            gDesktopPendingTicks = NowTicks();
            gDesktopPending = TRUE;
            if (!IWorkbench || !IWorkbench->OpenWorkbenchObjectA(gDesktopLaunch, NULL))
            {
                gDesktopPending = FALSE;
                LogErr("OpenWorkbenchObject failed, launching directly");
                LaunchVFM(path);
            }
            else
            {
                /* Il Workbench apre una finestra solo se quella del
                   percorso non e' gia' aperta (anche la finestra nascosta
                   di un intercetto precedente): se entro mezzo secondo
                   la patch non ha preso la richiesta, la si annulla e
                   VFM parte da qui, cosi' non resta in sospeso. */
                BOOL still = TRUE;
                int t;
                for (t = 0; t < 25 && still; t++)
                {
                    IDOS->Delay(2);
                    still = gDesktopPending;
                }
                IExec->Forbid();
                still = gDesktopPending;
                gDesktopPending = FALSE;
                IExec->Permit();
                if (still)
                {
                    Log("no Workbench window for '%s', launching directly", path);
                    LaunchVFM(path);
                }
            }
            return;
        }
    }
    else
    {
        gLastClickValid = TRUE;
        gLastClickSecs = secs;
        gLastClickMicros = micros;
        gLastClickX = mx;
        gLastClickY = my;
    }
    IIntuition->UnlockPubScreen(NULL, scr);
}

/* Filtro commodity: ogni pressione del tasto sinistro (non il rilascio,
   qualunque qualificatore) arriva come copia sulla porta del broker.
   L'evento prosegue normalmente verso il Workbench. */
static BOOL InstallClickFilter(void)
{
    CxObj *filter, *sender;
    IX ix;

    filter = gICx->CreateCxObj(CX_FILTER, 0, 0);
    if (!filter) return FALSE;

    memset(&ix, 0, sizeof(ix));
    ix.ix_Version  = IX_VERSION;
    ix.ix_Class    = IECLASS_RAWMOUSE;
    ix.ix_Code     = IECODE_LBUTTON;
    ix.ix_CodeMask = 0xFFFF;          /* esattamente LBUTTON (giu') */
    ix.ix_Qualifier = 0;
    ix.ix_QualMask = 0;               /* qualificatori indifferenti */
    gICx->SetFilterIX(filter, &ix);

    sender = gICx->CreateCxObj(CX_SEND, (int32)gCxPort, CLICK_SENDER_ID);
    if (!sender)
    {
        gICx->DeleteCxObj(filter);
        return FALSE;
    }
    gICx->AttachCxObj(filter, sender);
    gICx->AttachCxObj(gBroker, filter);

    if (gICx->CxObjError(filter))
    {
        LogErr("click filter error %ld", (long)gICx->CxObjError(filter));
        return FALSE;
    }
    return TRUE;
}

/* ===== Patch di OpenWindowTagList ===== */

static BOOL InstallPatch(void)
{
    if (gActive || !IIntuition) return FALSE;
    gOrigOpenWindowTagList = (OpenWindowTagListFunc)IExec->SetMethod(
        (struct Interface *)IIntuition,
        (LONG)offsetof(struct IntuitionIFace, OpenWindowTagList),
        (CONST_APTR)Patched_OpenWindowTagList);
    gActive = (gOrigOpenWindowTagList != NULL);
    Log("InstallPatch active=%ld exe='%s'", (long)gActive, gExePath);
    return gActive;
}

static void RemovePatch(void)
{
    if (!gActive) return;
    IExec->SetMethod((struct Interface *)IIntuition,
        (LONG)offsetof(struct IntuitionIFace, OpenWindowTagList),
        (CONST_APTR)gOrigOpenWindowTagList);
    gActive = FALSE;
    gOrigOpenWindowTagList = NULL;
    Log("RemovePatch");
}

static void Shutdown(void)
{
    RemovePatch();
    if (gICx && gBroker)
    {
        gICx->DeleteCxObjAll(gBroker);
        gBroker = NULL;
    }
    if (gCxPort)
    {
        struct Message *m;
        while ((m = IExec->GetMsg(gCxPort)) != NULL)
            IExec->ReplyMsg(m);
        IExec->FreeSysObject(ASOT_PORT, gCxPort);
        gCxPort = NULL;
    }
    if (gClosePort)
    {
        struct Message *m;
        while ((m = IExec->GetMsg(gClosePort)) != NULL)
            IExec->ReplyMsg(m);
        IExec->FreeSysObject(ASOT_PORT, gClosePort);
        gClosePort = NULL;
    }
    if (gICx) { IExec->DropInterface((struct Interface *)gICx); gICx = NULL; }
    if (gCxBase) { IExec->CloseLibrary(gCxBase); gCxBase = NULL; }
}

/* ===== ToolType ===== */

/* Legge i ToolType dall'icona del programma, sia da Workbench (argc == 0,
   argv e' il WBStartup) sia da Shell (icona accanto all'eseguibile). */
static void ReadToolTypes(int argc, char **argv)
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
        STRPTR v = IIcon->FindToolType((STRPTR *)dob->do_ToolTypes, "DESKTOPPATH");
        if (v && v[0])
        {
            strncpy(gDesktopPath, v, sizeof(gDesktopPath) - 1);
            gDesktopPath[sizeof(gDesktopPath) - 1] = '\0';
        }
        v = IIcon->FindToolType((STRPTR *)dob->do_ToolTypes, "DEBUG");
        if (v)
            gDebug = (IIcon->MatchToolValue(v, "FALSE") || IIcon->MatchToolValue(v, "NO") ||
                      IIcon->MatchToolValue(v, "OFF") || IIcon->MatchToolValue(v, "0"))
                     ? FALSE : TRUE;
    }
    IIcon->FreeDiskObject(dob);
}

/* ===== Punto di ingresso ===== */

int main(int argc, char **argv)
{
    struct NewBroker nb;
    LONG cxerr = 0;
    BOOL done = FALSE;

    if (argc > 1 && argv[1] && argv[1][0])
    {
        BPTR l = IDOS->Lock(argv[1], SHARED_LOCK);
        gExePath[0] = '\0';
        if (l)
        {
            IDOS->NameFromLock(l, gExePath, sizeof(gExePath));
            IDOS->UnLock(l);
        }
        if (!gExePath[0])
        {
            strncpy(gExePath, argv[1], sizeof(gExePath) - 1);
            gExePath[sizeof(gExePath) - 1] = '\0';
        }
    }
    else
    {
        BPTR dir = IDOS->GetProgramDir();
        gExePath[0] = '\0';
        if (dir && IDOS->NameFromLock(dir, gExePath, sizeof(gExePath)))
        {
            if (!IDOS->AddPart(gExePath, "VagaFileManager", sizeof(gExePath)))
                gExePath[0] = '\0';
        }
        if (!gExePath[0])
        {
            strncpy(gExePath, "PROGDIR:VagaFileManager", sizeof(gExePath) - 1);
            gExePath[sizeof(gExePath) - 1] = '\0';
        }
    }

    {
        BPTR l = IDOS->Lock(gExePath, SHARED_LOCK);
        if (!l)
        {
            IDOS->Printf("WB2VFM: eseguibile non trovato: '%s'\n", gExePath);
            return RETURN_FAIL;
        }
        IDOS->UnLock(l);
    }

    ReadToolTypes(argc, argv);

    gCxBase = IExec->OpenLibrary("commodities.library", 0);
    if (gCxBase)
        gICx = (struct CommoditiesIFace *)IExec->GetInterface(gCxBase, "main", 1, NULL);
    if (!gICx)
    {
        IDOS->Printf("WB2VFM: commodities.library non disponibile.\n");
        Shutdown();
        return RETURN_FAIL;
    }

    gCxPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!gCxPort)
    {
        IDOS->Printf("WB2VFM: impossibile creare la porta.\n");
        Shutdown();
        return RETURN_FAIL;
    }
    gClosePort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!gClosePort)
    {
        IDOS->Printf("WB2VFM: impossibile creare la porta di chiusura.\n");
        Shutdown();
        return RETURN_FAIL;
    }

    memset(&nb, 0, sizeof(nb));
    nb.nb_Version  = NB_VERSION;
    nb.nb_Name     = "WB2VFM";
    Log("WB2VFM build %s", VFM_STR(WB2_BUILD));
    nb.nb_Title    = "WB2VFM " WB2_VERSION " build " VFM_STR(WB2_BUILD);
    nb.nb_Descr    = "VFM drawer windows, desktop double-click opens RAM:";
    nb.nb_Unique   = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags    = COF_ENABLE;
    nb.nb_Pri      = 0;
    nb.nb_Port     = gCxPort;
    nb.nb_ReservedChannel = 0;

    gBroker = gICx->CxBroker(&nb, &cxerr);
    if (!gBroker)
    {
        IDOS->Printf("WB2VFM: CxBroker fallita (errore %ld).\n", (long)cxerr);
        Shutdown();
        return RETURN_FAIL;
    }
    /* senza filtro la commodity funziona comunque (solo i cassetti) */
    if (!InstallClickFilter())
        LogErr("desktop double-click not available");
    gICx->ActivateCxObj(gBroker, TRUE);

    if (!InstallPatch())
    {
        IDOS->Printf("WB2VFM: impossibile installare la patch (exe '%s').\n", gExePath);
        Shutdown();
        return RETURN_FAIL;
    }

    Log("commodity avviata, broker='%s' exe='%s' desktop='%s'",
        "WB2VFM", gExePath, gDesktopPath);

    while (!done)
    {
        ULONG sig = IExec->Wait((1UL << gCxPort->mp_SigBit) |
                                (1UL << gClosePort->mp_SigBit) |
                                SIGBREAKF_CTRL_C);
        CxMsg *cxm;

        if (sig & SIGBREAKF_CTRL_C) break;

        if (sig & (1UL << gClosePort->mp_SigBit))
        {
            struct CloseMsg *cm;
            while ((cm = (struct CloseMsg *)IExec->GetMsg(gClosePort)) != NULL)
            {
                /* il Workbench ha finito il setup della sua finestra */
                IDOS->Delay(50);
                {
                    BOOL closed = FALSE;
                    if (IWorkbench && cm->path[0])
                        closed = IWorkbench->CloseWorkbenchObjectA(cm->path, NULL);
                    Log("closing Workbench object '%s' -> %ld",
                        cm->path, (long)closed);
                }
                IExec->FreeVec(cm);
            }
        }

        while ((cxm = (CxMsg *)IExec->GetMsg(gCxPort)) != NULL)
        {
            /* click sinistro: ignorato se la commodity e' disabilitata
               da Exchange (DISABLE toglie la patch e azzera gActive) */
            if (gICx->CxMsgType(cxm) == CXM_IEVENT &&
                gICx->CxMsgID(cxm) == CLICK_SENDER_ID)
            {
                if (gActive)
                    HandleLeftClick((const struct InputEvent *)gICx->CxMsgData(cxm));
            }
            else if (gICx->CxMsgType(cxm) == CXM_COMMAND)
            {
                LONG id = gICx->CxMsgID(cxm);
                switch (id)
                {
                    case CXCMD_DISABLE:
                        Log("Exchange: DISABLE");
                        RemovePatch();
                        break;
                    case CXCMD_ENABLE:
                        Log("Exchange: ENABLE");
                        if (!gActive) InstallPatch();
                        break;
                    case CXCMD_UNIQUE:
                        Log("Exchange: UNIQUE (duplicate launch)");
                        if (!gActive) InstallPatch();
                        break;
                    case CXCMD_KILL:
                        Log("Exchange: KILL");
                        done = TRUE;
                        break;
                    default:
                        break;
                }
            }
            IExec->ReplyMsg((struct Message *)cxm);
        }
    }

    Shutdown();
    return RETURN_OK;
}
