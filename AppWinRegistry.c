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
 * Modulo: AppWinRegistry.c
 * ------------------------
 * Elenco pubblico delle AppWindow (vedi AppWindowRegistry.h), tenuto da
 * VFM mentre e' in esecuzione.
 *
 * La struct AppWindow del Workbench e' privata: per sapere a quale porta
 * mandare l'AppMessage di un drop si intercettano AddAppWindow[A],
 * RemoveAppWindow, AddAppWindowDropZone[A], RemoveAppWindowDropZone e
 * Intuition CloseWindow.  Sono note solo le AppWindow aperte DOPO
 * l'installazione: quelle gia' aperte (spesso AmiDock) non si possono
 * scoprire.
 *
 * Il primo VFM avviato installa l'elenco; gli altri lo usano.  Chi lo ha
 * installato lo toglie alla chiusura; un VFM ancora aperto ne installa
 * uno nuovo al drag successivo.
 *
 * Le patch girano nel task di chi chiama (qualsiasi programma): niente
 * DOS, niente log, solo memoria condivisa e il semaforo dell'elenco.
 */

#include "VagaFileManager.h"
#include <stddef.h>
#include <exec/semaphores.h>
#include <workbench/workbench.h>
#include "AppWindowRegistry.h"

typedef struct AppWindow *(*AddAppWindowAFunc)(struct WorkbenchIFace *, ULONG, ULONG,
    struct Window *, struct MsgPort *, const struct TagItem *);
typedef BOOL (*RemoveAppWindowFunc)(struct WorkbenchIFace *, struct AppWindow *);
typedef struct AppWindowDropZone *(*AddAppWindowDropZoneAFunc)(struct WorkbenchIFace *,
    struct AppWindow *, ULONG, ULONG, const struct TagItem *);
typedef BOOL (*RemoveAppWindowDropZoneFunc)(struct WorkbenchIFace *, struct AppWindow *,
    struct AppWindowDropZone *);
typedef VOID (*CloseWindowFunc)(struct IntuitionIFace *, struct Window *);

static struct VFMAppWinRegistry *gReg = NULL;   /* solo se installato da noi */
static volatile LONG gPatchUsers = 0;           /* task dentro una patch */

static AddAppWindowAFunc gOrigAddAppWindowA = NULL;
static APTR gOrigAddAppWindow = NULL;           /* versione varargs */
static RemoveAppWindowFunc gOrigRemoveAppWindow = NULL;
static AddAppWindowDropZoneAFunc gOrigAddDropZoneA = NULL;
static APTR gOrigAddDropZone = NULL;            /* versione varargs */
static RemoveAppWindowDropZoneFunc gOrigRemoveDropZone = NULL;
static CloseWindowFunc gOrigCloseWindow = NULL;

static void EnterPatch(void) { IExec->Forbid(); gPatchUsers++; IExec->Permit(); }
static void LeavePatch(void) { IExec->Forbid(); gPatchUsers--; IExec->Permit(); }

static struct VFMAppWin *RegFindAW(APTR aw)
{
    struct MinNode *n;
    for (n = gReg->windows.mlh_Head; n->mln_Succ; n = n->mln_Succ)
        if (((struct VFMAppWin *)n)->aw == aw) return (struct VFMAppWin *)n;
    return NULL;
}

static void RegFreeEntry(struct VFMAppWin *e)
{
    struct MinNode *z;
    IExec->Remove((struct Node *)e);
    while ((z = (struct MinNode *)IExec->RemHead((struct List *)&e->zones)) != NULL)
        IExec->FreeVec(z);
    IExec->FreeVec(e);
}

static void RegAddWindow(struct AppWindow *aw, struct Window *win, struct MsgPort *port,
                         ULONG id, ULONG userdata)
{
    struct VFMAppWin *e;
    if (!gReg || !aw || !win || !port) return;
    e = (struct VFMAppWin *)IExec->AllocVecTags(sizeof(*e),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!e) return;
    e->aw = aw;
    e->win = win;
    e->port = port;
    e->id = id;
    e->userdata = userdata;
    IExec->NewMinList(&e->zones);
    IExec->ObtainSemaphore(&gReg->sem);
    IExec->AddTail((struct List *)&gReg->windows, (struct Node *)e);
    IExec->ReleaseSemaphore(&gReg->sem);
}

static void RegAddZone(struct AppWindow *aw, struct AppWindowDropZone *dz,
                       ULONG id, ULONG userdata, const struct TagItem *tags)
{
    struct VFMAppZone *z;
    struct VFMAppWin *e;
    struct TagItem *tstate = (struct TagItem *)tags, *ti;

    if (!gReg || !aw || !dz) return;
    z = (struct VFMAppZone *)IExec->AllocVecTags(sizeof(*z),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!z) return;
    z->dz = dz;
    z->id = id;
    z->userdata = userdata;
    while ((ti = IUtility->NextTagItem(&tstate)) != NULL)
    {
        switch (ti->ti_Tag)
        {
            case WBDZA_Left:      z->left = (WORD)ti->ti_Data; z->relRight = FALSE; break;
            case WBDZA_RelRight:  z->left = (WORD)ti->ti_Data; z->relRight = TRUE; break;
            case WBDZA_Top:       z->top = (WORD)ti->ti_Data; z->relBottom = FALSE; break;
            case WBDZA_RelBottom: z->top = (WORD)ti->ti_Data; z->relBottom = TRUE; break;
            case WBDZA_Width:     z->width = (WORD)ti->ti_Data; z->relWidth = FALSE; break;
            case WBDZA_RelWidth:  z->width = (WORD)ti->ti_Data; z->relWidth = TRUE; break;
            case WBDZA_Height:    z->height = (WORD)ti->ti_Data; z->relHeight = FALSE; break;
            case WBDZA_RelHeight: z->height = (WORD)ti->ti_Data; z->relHeight = TRUE; break;
            case WBDZA_Box:
                if (ti->ti_Data)
                {
                    const struct IBox *b = (const struct IBox *)ti->ti_Data;
                    z->left = b->Left;   z->top = b->Top;
                    z->width = b->Width; z->height = b->Height;
                    z->relRight = z->relBottom = z->relWidth = z->relHeight = FALSE;
                }
                break;
            default: break;
        }
    }
    IExec->ObtainSemaphore(&gReg->sem);
    e = RegFindAW(aw);
    if (e) IExec->AddTail((struct List *)&e->zones, (struct Node *)z);
    IExec->ReleaseSemaphore(&gReg->sem);
    if (!e) IExec->FreeVec(z);   /* AppWindow aperta prima di VFM: ignota */
}

static struct AppWindow *Patched_AddAppWindowA(struct WorkbenchIFace *Self, ULONG id,
    ULONG userdata, struct Window *window, struct MsgPort *port, const struct TagItem *tags)
{
    struct AppWindow *aw;
    EnterPatch();
    aw = gOrigAddAppWindowA(Self, id, userdata, window, port, tags);
    if (aw) RegAddWindow(aw, window, port, id, userdata);
    LeavePatch();
    return aw;
}

static struct AppWindow * VARARGS68K Patched_AddAppWindow(struct WorkbenchIFace *Self,
    ULONG id, ULONG userdata, struct Window *window, struct MsgPort *port, ...)
{
    struct AppWindow *aw;
    struct TagItem *tags;
    va_list ap;

    EnterPatch();
    va_startlinear(ap, port);
    tags = va_getlinearva(ap, struct TagItem *);
    aw = gOrigAddAppWindowA(Self, id, userdata, window, port, tags);
    va_end(ap);
    if (aw) RegAddWindow(aw, window, port, id, userdata);
    LeavePatch();
    return aw;
}

static BOOL Patched_RemoveAppWindow(struct WorkbenchIFace *Self, struct AppWindow *aw)
{
    BOOL r;
    EnterPatch();
    /* prima di rimuovere: chi sta spedendo un drop tiene il semaforo */
    if (gReg && aw)
    {
        struct VFMAppWin *e;
        IExec->ObtainSemaphore(&gReg->sem);
        if ((e = RegFindAW(aw)) != NULL) RegFreeEntry(e);
        IExec->ReleaseSemaphore(&gReg->sem);
    }
    r = gOrigRemoveAppWindow(Self, aw);
    LeavePatch();
    return r;
}

static struct AppWindowDropZone *Patched_AddDropZoneA(struct WorkbenchIFace *Self,
    struct AppWindow *aw, ULONG id, ULONG userdata, const struct TagItem *tags)
{
    struct AppWindowDropZone *dz;
    EnterPatch();
    dz = gOrigAddDropZoneA(Self, aw, id, userdata, tags);
    if (dz) RegAddZone(aw, dz, id, userdata, tags);
    LeavePatch();
    return dz;
}

static struct AppWindowDropZone * VARARGS68K Patched_AddDropZone(struct WorkbenchIFace *Self,
    struct AppWindow *aw, ULONG id, ULONG userdata, ...)
{
    struct AppWindowDropZone *dz;
    struct TagItem *tags;
    va_list ap;

    EnterPatch();
    va_startlinear(ap, userdata);
    tags = va_getlinearva(ap, struct TagItem *);
    dz = gOrigAddDropZoneA(Self, aw, id, userdata, tags);
    va_end(ap);
    if (dz) RegAddZone(aw, dz, id, userdata, tags);
    LeavePatch();
    return dz;
}

static BOOL Patched_RemoveDropZone(struct WorkbenchIFace *Self, struct AppWindow *aw,
                                   struct AppWindowDropZone *dz)
{
    BOOL r;
    EnterPatch();
    if (gReg && aw && dz)
    {
        struct VFMAppWin *e;
        IExec->ObtainSemaphore(&gReg->sem);
        if ((e = RegFindAW(aw)) != NULL)
        {
            struct MinNode *n;
            for (n = e->zones.mlh_Head; n->mln_Succ; n = n->mln_Succ)
            {
                if (((struct VFMAppZone *)n)->dz == dz)
                {
                    IExec->Remove((struct Node *)n);
                    IExec->FreeVec(n);
                    break;
                }
            }
        }
        IExec->ReleaseSemaphore(&gReg->sem);
    }
    r = gOrigRemoveDropZone(Self, aw, dz);
    LeavePatch();
    return r;
}

/* Un programma che chiude la finestra senza RemoveAppWindow lascerebbe
   una voce con porta non piu' valida: la si toglie qui. */
static VOID Patched_CloseWindow(struct IntuitionIFace *Self, struct Window *window)
{
    EnterPatch();
    if (gReg && window)
    {
        struct MinNode *n, *next;
        IExec->ObtainSemaphore(&gReg->sem);
        for (n = gReg->windows.mlh_Head; (next = n->mln_Succ) != NULL; n = next)
            if (((struct VFMAppWin *)n)->win == window)
                RegFreeEntry((struct VFMAppWin *)n);
        IExec->ReleaseSemaphore(&gReg->sem);
    }
    gOrigCloseWindow(Self, window);
    LeavePatch();
}

static APTR SetVec(struct Interface *iface, LONG offset, APTR func)
{
    return (APTR)IExec->SetMethod(iface, offset, (CONST_APTR)func);
}

/* Installa l'elenco se nessun VFM lo ha gia' fatto.  Si puo' chiamare
   piu' volte (all'avvio e a ogni drag). */
void AppRegistryEnsure(void)
{
    struct SignalSemaphore *old;

    if (gReg || !IWorkbench || !IIntuition) return;

    IExec->Forbid();
    old = IExec->FindSemaphore(VFM_AWREG_NAME);
    IExec->Permit();
    if (old) return;   /* lo tiene un altro VFM */

    gReg = (struct VFMAppWinRegistry *)IExec->AllocVecTags(sizeof(*gReg),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!gReg) return;
    gReg->version = VFM_AWREG_VERSION;
    IExec->NewMinList(&gReg->windows);
    gReg->sem.ss_Link.ln_Name = (STRPTR)VFM_AWREG_NAME;
    gReg->sem.ss_Link.ln_Pri = 0;

    gOrigAddAppWindowA = (AddAppWindowAFunc)SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindowA), (APTR)Patched_AddAppWindowA);
    gOrigAddAppWindow = SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindow), (APTR)Patched_AddAppWindow);
    gOrigRemoveAppWindow = (RemoveAppWindowFunc)SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, RemoveAppWindow), (APTR)Patched_RemoveAppWindow);
    gOrigAddDropZoneA = (AddAppWindowDropZoneAFunc)SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindowDropZoneA), (APTR)Patched_AddDropZoneA);
    gOrigAddDropZone = SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindowDropZone), (APTR)Patched_AddDropZone);
    gOrigRemoveDropZone = (RemoveAppWindowDropZoneFunc)SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, RemoveAppWindowDropZone), (APTR)Patched_RemoveDropZone);
    gOrigCloseWindow = (CloseWindowFunc)SetVec((struct Interface *)IIntuition,
        offsetof(struct IntuitionIFace, CloseWindow), (APTR)Patched_CloseWindow);

    /* pubblicato solo a patch installate */
    IExec->AddSemaphore(&gReg->sem);
    DBG("AppWindow: elenco installato (solo le AppWindow aperte da ora)");
}

/* Alla chiusura di VFM: toglie patch ed elenco, se sono nostri. */
void AppRegistryRemove(void)
{
    struct MinNode *n;
    APTR cur;

    if (!gReg) return;

    cur = SetVec((struct Interface *)IIntuition,
        offsetof(struct IntuitionIFace, CloseWindow), (APTR)gOrigCloseWindow);
    if (cur != (APTR)Patched_CloseWindow)
        DBG("AppWindow: CloseWindow ripatchata da un altro programma");
    SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, RemoveAppWindowDropZone), (APTR)gOrigRemoveDropZone);
    SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindowDropZone), gOrigAddDropZone);
    SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindowDropZoneA), (APTR)gOrigAddDropZoneA);
    SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, RemoveAppWindow), (APTR)gOrigRemoveAppWindow);
    SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindow), gOrigAddAppWindow);
    SetVec((struct Interface *)IWorkbench,
        offsetof(struct WorkbenchIFace, AddAppWindowA), (APTR)gOrigAddAppWindowA);

    /* il codice delle patch e' in VFM: non si esce finche' un altro
       programma ci sta dentro (per esempio un CloseWindow in corso) */
    while (gPatchUsers > 0) IDOS->Delay(5);
    IDOS->Delay(10);

    IExec->RemSemaphore(&gReg->sem);
    IExec->ObtainSemaphore(&gReg->sem);   /* attende i lettori (altri VFM) */
    while ((n = (struct MinNode *)gReg->windows.mlh_Head)->mln_Succ)
        RegFreeEntry((struct VFMAppWin *)n);
    IExec->ReleaseSemaphore(&gReg->sem);
    IExec->FreeVec(gReg);
    gReg = NULL;
    DBG("AppWindow: elenco rimosso");
}
