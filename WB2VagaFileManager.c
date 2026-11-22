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

/* WB2VagaFileManager - sostituisce le finestre dei cassetti del Workbench
 *
 * Utility Commodities (gestibile da Exchange) che patcha
 * Intuition->OpenWindowTagList via SetMethod(): quando il Workbench apre
 * un cassetto o un volume, lancia VagaFileManager su quel percorso e
 * impedisce al Workbench di aprire la propria finestra.
 *
 * Uso:
 *   WB2VagaFileManager [percorso/VagaFileManager]
 *
 * Senza argomento usa l'eseguibile VagaFileManager nella stessa
 * directory di WB2VagaFileManager (percorso assoluto).
 *
 * Appare in Exchange come "WB2VagaFileManager"; da li' si puo'
 * abilitare/disabilitare o terminare (Kill).
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
#include <libraries/commodities.h>
#include <interfaces/commodities.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/wb.h>

#include "VagaFileManager_version.h"

#define PATH_MAX_LEN 1024
#define WB2_VERSION "1.0"
/* Build proprio della commodity; tenerlo allineato alle release VFM. */
#define WB2_BUILD   26

static const char wb2_version_tag[] __attribute__((used)) =
    "\0$VER: WB2VagaFileManager " WB2_VERSION " build " VFM_STR(WB2_BUILD) " (" VFM_VERSION_DATE ")";

static BOOL gActive = FALSE;
static char gExePath[PATH_MAX_LEN];

static struct Library *gCxBase = NULL;
static struct CommoditiesIFace *gICx = NULL;
static struct MsgPort *gCxPort = NULL;
static CxObj *gBroker = NULL;

typedef struct Window *(*OpenWindowTagListFunc)(struct IntuitionIFace *,
                                                const struct NewWindow *,
                                                const struct TagItem *);
static OpenWindowTagListFunc gOrigOpenWindowTagList = NULL;

/* ===== Log ===== */

static void Log(CONST_STRPTR fmt, ...)
{
    BPTR fh;
    va_list ap;
    char buf[1024];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fh = IDOS->Open("T:WB2VagaFileManager.log", MODE_READWRITE);
    if (!fh)
        fh = IDOS->Open("T:WB2VagaFileManager.log", MODE_NEWFILE);
    if (fh)
    {
        IDOS->ChangeFilePosition(fh, 0, OFFSET_END);
        IDOS->FPrintf(fh, "%s\n", buf);
        IDOS->Close(fh);
    }
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
    Log("launch cmd='%s' rc=%ld", cmd, (long)err);
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
                    (void)newWindow;
                    (void)tagList;
                    IIntuition->UnlockPubScreen(NULL, scr);
                    Log("intercept '%s' (type=%lu)", full, (unsigned long)type);
                    LaunchVFM(full);

                    /* Comportamento stabile: nessuna finestra nascosta da
                       chiudere.  Il Workbench puo' mostrare per un attimo
                       "cassetto non apribile", ma non si blocca e il device
                       puo' essere riaperto. */
                    return NULL;
                }
            }
            IIntuition->UnlockPubScreen(NULL, scr);
        }
    }

    return gOrigOpenWindowTagList ?
        gOrigOpenWindowTagList(Self, newWindow, tagList) : NULL;
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
    if (gICx) { IExec->DropInterface((struct Interface *)gICx); gICx = NULL; }
    if (gCxBase) { IExec->CloseLibrary(gCxBase); gCxBase = NULL; }
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
            IDOS->Printf("WB2VagaFileManager: eseguibile non trovato: '%s'\n", gExePath);
            return RETURN_FAIL;
        }
        IDOS->UnLock(l);
    }

    gCxBase = IExec->OpenLibrary("commodities.library", 0);
    if (gCxBase)
        gICx = (struct CommoditiesIFace *)IExec->GetInterface(gCxBase, "main", 1, NULL);
    if (!gICx)
    {
        IDOS->Printf("WB2VagaFileManager: commodities.library non disponibile.\n");
        Shutdown();
        return RETURN_FAIL;
    }

    gCxPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!gCxPort)
    {
        IDOS->Printf("WB2VagaFileManager: impossibile creare la porta.\n");
        Shutdown();
        return RETURN_FAIL;
    }

    memset(&nb, 0, sizeof(nb));
    nb.nb_Version  = NB_VERSION;
    nb.nb_Name     = "WB2VagaFileManager";
    Log("WB2 build %s", VFM_STR(WB2_BUILD));
    nb.nb_Title    = "WB2 VagaFileManager " WB2_VERSION " build " VFM_STR(WB2_BUILD);
    nb.nb_Descr    = "WB2 VagaFileManager drawer patch " WB2_VERSION " build " VFM_STR(WB2_BUILD);
    nb.nb_Unique   = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags    = COF_ENABLE;
    nb.nb_Pri      = 0;
    nb.nb_Port     = gCxPort;
    nb.nb_ReservedChannel = 0;

    gBroker = gICx->CxBroker(&nb, &cxerr);
    if (!gBroker)
    {
        IDOS->Printf("WB2VagaFileManager: CxBroker fallita (errore %ld).\n", (long)cxerr);
        Shutdown();
        return RETURN_FAIL;
    }
    gICx->ActivateCxObj(gBroker, TRUE);

    if (!InstallPatch())
    {
        IDOS->Printf("WB2VagaFileManager: impossibile installare la patch (exe '%s').\n", gExePath);
        Shutdown();
        return RETURN_FAIL;
    }

    Log("commodity avviata, broker='%s' exe='%s'", "WB2VagaFileManager", gExePath);

    while (!done)
    {
        ULONG sig = IExec->Wait((1UL << gCxPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        CxMsg *cxm;

        if (sig & SIGBREAKF_CTRL_C) break;

        while ((cxm = (CxMsg *)IExec->GetMsg(gCxPort)) != NULL)
        {
            if (gICx->CxMsgType(cxm) == CXM_COMMAND)
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
