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
 * Modulo: DragDrop.c
 * ------------------
 * Drag & drop: raccolta dei percorsi selezionati, identificazione della
 * finestra di destinazione, trasferimento VFM->VFM (sposta sullo stesso
 * device, copia su device diversi), drop da/verso Workbench e altre app e
 * drop testuale su finestre esterne (Shell/requester ASL).
 */

#include "VagaFileManager.h"
#include <exec/tasks.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <libraries/keymap.h>
#include <interfaces/keymap.h>

/* Librerie e device per l'iniezione del testo: tenuti aperti per tutta la
   vita del processo, così gli eventi non vengono persi alla chiusura. */
static struct Library *gKeymapBase = NULL;
static struct KeymapIFace *gKeymap = NULL;
static struct MsgPort *gInputPort = NULL;
static struct IOStdReq gInputIO;
static BOOL gInputOpen = FALSE;

/* ===== Input device e drop testuale ===== */

static BOOL EnsureInputLibraries(void)
{
    if (!gKeymapBase)
        gKeymapBase = IExec->OpenLibrary("keymap.library", 50);
    if (gKeymapBase && !gKeymap)
        gKeymap = (struct KeymapIFace *)IExec->GetInterface(gKeymapBase, "main", 1, NULL);

    if (!gInputOpen)
    {
        if (!gInputPort)
            gInputPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
        if (gInputPort)
        {
            memset(&gInputIO, 0, sizeof(gInputIO));
            gInputIO.io_Message.mn_ReplyPort = gInputPort;
            gInputIO.io_Message.mn_Length = sizeof(gInputIO);
            if (IExec->OpenDevice("input.device", 0, (struct IORequest *)&gInputIO, 0) == 0)
                gInputOpen = TRUE;
        }
    }
    return (gKeymap && gInputOpen) ? TRUE : FALSE;
}

void ShutdownDragDropInput(void)
{
    if (gInputOpen)
    {
        IExec->CloseDevice((struct IORequest *)&gInputIO);
        gInputOpen = FALSE;
    }
    if (gInputPort)
    {
        IExec->FreeSysObject(ASOT_PORT, gInputPort);
        gInputPort = NULL;
    }
    if (gKeymap)
    {
        IExec->DropInterface((struct Interface *)gKeymap);
        gKeymap = NULL;
    }
    if (gKeymapBase)
    {
        IExec->CloseLibrary(gKeymapBase);
        gKeymapBase = NULL;
    }
}


/* ===== Percorsi selezionati e trasferimento ===== */

BOOL CollectSelectedFullPaths(struct List *list, char paths[][PATH_MAX_LEN], LONG maxPaths, LONG *count)
{
    struct Node *n;
    LONG c = 0;
    if (!list || !paths || !count) return FALSE;
    *count = 0;
    for (n = list->lh_Head; n->ln_Succ && c < maxPaths; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (IsRootPath(gPathBuffer))
        {
            strncpy(paths[c], name, PATH_MAX_LEN - 1);
        }
        else
        {
            if (!BuildChildPath(gPathBuffer, name, paths[c], PATH_MAX_LEN)) continue;
        }
        paths[c][PATH_MAX_LEN - 1] = '\0';
        c++;
    }
    *count = c;
    return (c > 0);
}

/* forward declarations (definite piu' sotto) */
static BOOL StrContainsNoCase(CONST_STRPTR hay, CONST_STRPTR needle);
static CONST_STRPTR WindowTaskName(struct Window *w);

BOOL IsVFMWindow(struct Window *w, struct MsgPort **outPort)
{
    struct VFMWindowTag *tag;
    CONST_STRPTR tname;
    struct Task *me;

    if (outPort) *outPort = NULL;
    if (!w || !w->UserData) return FALSE;

    /* Identifica le finestre VFM dal processo che le possiede e dal magic in
       UserData.  NON dal titolo della schermata, che ora lasciamo al
       Workbench (barra titolo del Workbench invariata). */
    tname = WindowTaskName(w);
    me = IExec->FindTask(NULL);
    if (!tname || !me || !me->tc_Node.ln_Name ||
        IUtility->Stricmp(tname, me->tc_Node.ln_Name) != 0)
        return FALSE;

    tag = (struct VFMWindowTag *)w->UserData;
    if (tag->vft_Magic != VFM_WINDOW_MAGIC) return FALSE;
    if (outPort)
        *outPort = IExec->FindPort(tag->vft_PortName);
    return TRUE;
}

/* Stesso device (stesso handler DOS)? */
static BOOL SameDevice(CONST_STRPTR a, CONST_STRPTR b)
{
    struct DevProc *da = IDOS->GetDeviceProc((STRPTR)a, NULL);
    struct DevProc *db = IDOS->GetDeviceProc((STRPTR)b, NULL);
    BOOL same = (da && db && da->dvp_Port && db->dvp_Port &&
                 da->dvp_Port == db->dvp_Port) ? TRUE : FALSE;
    if (da) IDOS->FreeDeviceProc(da);
    if (db) IDOS->FreeDeviceProc(db);
    return same;
}

/* Trascinamento: stesso device -> sposta; device diverso -> copia. */
static BOOL DragTransfer(CONST_STRPTR src, CONST_STRPTR dst)
{
    if (SameDevice(src, dst))
        return MoveEntry(src, dst);
    return CopyEntryRecursive(src, dst);
}

/* ===== Riconoscimento finestre Shell / requester =====
   Non esiste un'API pubblica per avviare un drag Workbench "nativo" da
   un'applicazione: Workbench gestisce internamente il trascinamento delle
   icone. Per le finestre che accettano testo (Shell, requester ASL) la via
   praticabile e' attivare la finestra di destinazione e iniettare il
   percorso come input da tastiera. */

static BOOL StrContainsNoCase(CONST_STRPTR hay, CONST_STRPTR needle)
{
    ULONG n = strlen(needle), i;
    if (!hay || !needle || n == 0) return FALSE;
    while (*hay)
    {
        for (i = 0; i < n; i++)
        {
            char a = hay[i], b = needle[i];
            if (a == '\0') break;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (i == n) return TRUE;
        hay++;
    }
    return FALSE;
}

static BOOL IsShellWindow(struct Window *w)
{
    struct Task *t;
    CONST_STRPTR name;

    if (!w) return FALSE;
    t = w->UserPort ? (struct Task *)w->UserPort->mp_SigTask : NULL;
    name = (t && t->tc_Node.ln_Name) ? t->tc_Node.ln_Name : NULL;
    if (name)
    {
        if (StrContainsNoCase(name, "shell")) return TRUE;
        if (StrContainsNoCase(name, "con-handler")) return TRUE;
        if (IUtility->Stricmp(name, "con") == 0) return TRUE;
        return FALSE;
    }
    /* fallback: senza nome del task, usa il titolo della finestra */
    if (w->Title && StrContainsNoCase(w->Title, "shell"))
        return TRUE;
    /* Le finestre console non hanno gadget e spesso non hanno Title. */
    if (!w->FirstGadget && !w->Title)
        return TRUE;
    return FALSE;
}

static CONST_STRPTR WindowTaskName(struct Window *w)
{
    struct Task *t;
    if (!w || !w->UserPort) return "?";
    t = (struct Task *)w->UserPort->mp_SigTask;
    return (t && t->tc_Node.ln_Name) ? t->tc_Node.ln_Name : "?";
}

static BOOL AppendText(char *buf, ULONG size, CONST_STRPTR text)
{
    ULONG l = (ULONG)strlen(buf);
    ULONG n = (ULONG)strlen(text);
    if (l + n >= size) return FALSE;
    memcpy(buf + l, text, n + 1);
    return TRUE;
}

/* TRUE if the text contains characters that are not plain text when typed
   into a window: C0 controls (Return, Ctrl-J, Esc...), DEL and C1 controls
   (0x80-0x9F, including CSI 0x9B). A file name containing them, dropped on
   a Shell, would run commands. */
static BOOL HasControlChars(CONST_STRPTR s)
{
    const UBYTE *p;
    for (p = (const UBYTE *)s; *p; p++)
    {
        if (*p < 0x20 || *p == 0x7F || (*p >= 0x80 && *p <= 0x9F))
            return TRUE;
    }
    return FALSE;
}

/* AmiDock accetta i file tramite il comando ARexx ADDOBJECT (porta AMIDOCK).
   Non usa lo stesso drag del Workbench, quindi senza questo caso un file
   trascinato da VFM verrebbe trattato come testo e rifiutato. */
static BOOL IsAmiDockWindow(struct Window *w)
{
    CONST_STRPTR task;
    CONST_STRPTR title;

    if (!w) return FALSE;
    task = WindowTaskName(w);
    title = w->Title;
    if (task && StrContainsNoCase(task, "amidock")) return TRUE;
    if (title && StrContainsNoCase(title, "amidock")) return TRUE;
    return FALSE;
}

static BOOL BuildAmiDockAddCommand(char *buf, ULONG size, CONST_STRPTR path)
{
    if (!buf || size == 0 || !path || !path[0]) return FALSE;
    /* ADDOBJECT FILE/A: il percorso va fra virgolette.  Non accettiamo
       virgolette o caratteri di controllo nel nome, per non rompere la
       sintassi del comando ARexx. */
    if (HasControlChars(path) || strchr(path, '"')) return FALSE;
    if (strlen(path) + 16 >= size) return FALSE;
    snprintf(buf, size, "ADDOBJECT \"%s\"", path);
    return TRUE;
}

static BOOL AddPathsToAmiDock(char (*paths)[PATH_MAX_LEN], LONG count)
{
    /* AmiDock usa AMIDOCK; le istanze multiple possono aggiungere .1, .2... */
    static const char *portNames[] = { "AMIDOCK", "AMIDOCK.1", "AMIDOCK.2", "AMIDOCK.3" };
    char command[PATH_MAX_LEN + 32];
    char result[256];
    LONG added = 0, failed = 0, i;
    ULONG p;

    for (i = 0; i < count; i++)
    {
        BOOL portFound = FALSE;
        LONG rc = -1;

        if (!BuildAmiDockAddCommand(command, sizeof(command), paths[i]))
        {
            DBG("AmiDock: percorso non gestibile '%s'", paths[i]);
            failed++;
            continue;
        }

        for (p = 0; p < sizeof(portNames) / sizeof(portNames[0]); p++)
        {
            result[0] = '\0';
            if (SendARexxCommandToPort(portNames[p], command, &rc, result, sizeof(result)))
            {
                portFound = TRUE;
                break;
            }
        }

        if (!portFound)
        {
            DBG("AmiDock: porta ARexx AMIDOCK non trovata");
            return FALSE;
        }

        if (rc == 0)
        {
            added++;
            DBG("AmiDock: aggiunto '%s'", paths[i]);
        }
        else
        {
            failed++;
            DBG("AmiDock: ADDOBJECT fallito per '%s' rc=%ld result='%s'",
                paths[i], (long)rc, result);
        }
    }

    DBG("AmiDock: %ld aggiunti, %ld falliti", (long)added, (long)failed);
    return (added > 0 && failed == 0);
}

static BOOL BuildDropText(char *buf, ULONG size, char (*paths)[PATH_MAX_LEN],
                          LONG count, BOOL forShell)
{
    LONG i;

    if (!buf || size == 0) return FALSE;
    buf[0] = '\0';

    for (i = 0; i < count; i++)
    {
        if (HasControlChars(paths[i]))
        {
            DBG("drop text: path with control characters rejected");
            return FALSE;
        }
    }

    for (i = 0; i < count; i++)
    {
        char item[PATH_MAX_LEN + 8];
        ULONG j = 0, len = (ULONG)strlen(paths[i]), k;

        if (forShell && j + 1 < sizeof(item)) item[j++] = '"';
        for (k = 0; k < len && j + 3 < sizeof(item); k++)
        {
            char c = paths[i][k];
            if (forShell && (c == '"' || c == '*')) item[j++] = '*';
            item[j++] = c;
        }
        if (forShell && j + 1 < sizeof(item)) item[j++] = '"';
        item[j] = '\0';

        if (!AppendText(buf, size, item)) break;
        if (forShell)
        {
            if (!AppendText(buf, size, " ")) break;
        }
        else
            break;  /* i requester accettano un solo percorso */
    }
    return (buf[0] != '\0');
}

static void AddRawKey(UWORD code, UWORD qual)
{
    struct InputEvent ie;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code;
    ie.ie_Qualifier = qual;
    ie.ie_NextEvent = NULL;

    gInputIO.io_Command = IND_WRITEEVENT;
    gInputIO.io_Flags = IOF_QUICK;
    gInputIO.io_Length = sizeof(struct InputEvent);
    gInputIO.io_Data = &ie;
    IExec->DoIO((struct IORequest *)&gInputIO);
}

static BOOL TypeTextIntoWindow(CONST_STRPTR text)
{
    UBYTE pairs[8192];
    LONG n, i;

    if (!text || !text[0]) return FALSE;
    /* last line of defence: never type a control character */
    if (HasControlChars(text)) return FALSE;
    if (!EnsureInputLibraries())
    {
        DBG("drop text: keymap/input.device non disponibili");
        return FALSE;
    }

    n = gKeymap->MapANSI(text, (int32)strlen(text), (STRPTR)pairs,
                         (int32)(sizeof(pairs) / 2), NULL);
    DBG("drop text: MapANSI ha prodotto %ld eventi", (long)n);
    if (n <= 0) return FALSE;

    /* Il primo evento dopo l'attivazione viene spesso consumato dalla
       finestra (focus/attivazione): manda prima due Shift "di cortesia". */
    IDOS->Delay(2);
    AddRawKey(0x60, 0);
    AddRawKey((UWORD)(0x60 | IECODE_UP_PREFIX), 0);
    AddRawKey(0x60, 0);
    AddRawKey((UWORD)(0x60 | IECODE_UP_PREFIX), 0);
    IDOS->Delay(1);

    for (i = 0; i < n; i++)
    {
        UWORD code = pairs[i * 2];
        UWORD qual = pairs[i * 2 + 1];

        AddRawKey(code, qual);
        AddRawKey((UWORD)(code | IECODE_UP_PREFIX), qual);
        IDOS->Delay(1);
    }

    IDOS->Delay(3);
    return TRUE;
}

static BOOL ActivateForDrop(struct Window *w)
{
    int attempt, i;

    if (!w) return FALSE;
    if (w->WScreen) IIntuition->ScreenToFront(w->WScreen);
    for (attempt = 0; attempt < 3; attempt++)
    {
        IIntuition->WindowToFront(w);
        IIntuition->ActivateWindow(w);
        for (i = 0; i < 20; i++)
        {
            if (w->Flags & WFLG_WINDOWACTIVE) return TRUE;
            IDOS->Delay(1);
        }
    }
    return (w->Flags & WFLG_WINDOWACTIVE) ? TRUE : FALSE;
}

static BOOL DropTextToExternalWindow(struct Window *w, char (*paths)[PATH_MAX_LEN], LONG count)
{
    char text[4096];
    BOOL shell;

    if (!w) return FALSE;
    shell = IsShellWindow(w);
    if (!BuildDropText(text, sizeof(text), paths, count, shell)) return FALSE;
    DBG("drop text: window='%s' task='%s' flags=%08lx shell=%ld text='%s'",
        w->Title ? w->Title : "?", WindowTaskName(w),
        (unsigned long)w->Flags, (long)shell, text);
    if (!ActivateForDrop(w))
    {
        DBG("drop: impossibile attivare la finestra di destinazione (flags=%08lx)",
            (unsigned long)w->Flags);
        return FALSE;
    }
    DBG("drop testuale su '%s' (%s)",
        w->Title ? w->Title : "?", shell ? "shell" : "requester");
    return TypeTextIntoWindow(text);
}

/* ===== Finestra sotto il puntatore ===== */

static struct Window *WindowAtPoint(struct Screen *s, WORD x, WORD y)
{
    struct Window *w;

    if (!s) return NULL;
    for (w = s->FirstWindow; w; w = w->NextWindow)
    {
        if (x >= w->LeftEdge && x < w->LeftEdge + w->Width &&
            y >= w->TopEdge  && y < w->TopEdge  + w->Height)
            return w;
    }
    return NULL;
}

/* Cerca la finestra sotto il puntatore su tutti gli screen. Il drop puo'
   avvenire anche su una Shell che non sta sullo screen del VFM: in quel
   caso WindowAtPoint sul solo screen del VFM troverebbe il backdrop del
   Workbench. Le coordinate vengono aggiornate a quelle dello screen scelto. */
static struct Window *FindDropWindow(struct Window *ownWin, WORD *px, WORD *py)
{
    struct Screen *first, *s, *own = ownWin ? ownWin->WScreen : NULL;
    struct Window *w, *fallback = NULL;
    WORD fx = 0, fy = 0;

    first = IIntuition->LockScreenList();
    if (!first)
    {
        if (own)
        {
            WORD x = own->MouseX, y = own->MouseY;
            w = WindowAtPoint(own, x, y);
            if (px) *px = x;
            if (py) *py = y;
            return w;
        }
        return NULL;
    }

    /* Lo screen in testa alla lista e' il piu' frontale: il puntatore e'
       su quello. Cerca li' la finestra sotto il puntatore. */
    {
        WORD x = first->MouseX, y = first->MouseY;
        w = WindowAtPoint(first, x, y);
        if (w && !(w->Flags & WFLG_BACKDROP))
        {
            if (px) *px = x;
            if (py) *py = y;
            IIntuition->UnlockScreenList();
            return w;
        }
        fallback = w;
        fx = x;
        fy = y;
    }

    /* Se sul primo screen c'e' solo il backdrop, la Shell puo' stare su
       un altro screen: cerca una finestra Shell. */
    for (s = first->NextScreen; s; s = s->NextScreen)
    {
        WORD x = s->MouseX, y = s->MouseY;
        w = WindowAtPoint(s, x, y);
        if (!w || (w->Flags & WFLG_BACKDROP)) continue;
        if (IsShellWindow(w))
        {
            if (px) *px = x;
            if (py) *py = y;
            IIntuition->UnlockScreenList();
            return w;
        }
        /* Non usare fallback su screen non frontali: le coordinate
           MouseX/MouseY potrebbero essere obsolete. */
    }

    if (!fallback && own)
    {
        WORD x = own->MouseX, y = own->MouseY;
        fallback = WindowAtPoint(own, x, y);
        fx = x;
        fy = y;
    }

    if (px) *px = fx;
    if (py) *py = fy;
    IIntuition->UnlockScreenList();
    return fallback;
}

/* ===== Avvio e gestione del drag ===== */

/* Gestisce il rilascio del drag: individua la destinazione (cassetto del
   Workbench, finestra VFM o finestra esterna) e trasferisce i percorsi. */
void DoDragDropOut(struct List *list, struct Window *win, WORD screenX, WORD screenY)
{

    char (*paths)[PATH_MAX_LEN];
    char targetDrawer[PATH_MAX_LEN];
    char targetFull[PATH_MAX_LEN];
    LONG count = 0, i;
    ULONG which = WBO_NONE, type = 0;
    BOOL anyErr = FALSE;
    struct Window *wbWin = NULL;

    if (IsRootPath(gPathBuffer)) return; /* cannot drag assigns out */
    DBG("DoDragDropOut: x=%d y=%d", (int)screenX, (int)screenY);

    paths = (char (*)[PATH_MAX_LEN])IExec->AllocVecTags(MAX_CLIPBOARD * PATH_MAX_LEN,
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (!paths) return;

    if (!CollectSelectedFullPaths(list, paths, MAX_CLIPBOARD, &count))
    {
        IExec->FreeVec(paths);
        return;
    }

    /* Cerca la finestra sotto il puntatore (anche su altri screen) */
    memset(targetDrawer, 0, sizeof(targetDrawer));
    memset(targetFull, 0, sizeof(targetFull));
    wbWin = FindDropWindow(win, &screenX, &screenY);
    if (WorkbenchBase && IWorkbench && wbWin)
    {
        struct TagItem wbtags[] =
        {
            { WBOBJA_DrawerPath,     (ULONG)targetDrawer },
            { WBOBJA_DrawerPathSize, (ULONG)sizeof(targetDrawer) },
            { WBOBJA_FullPath,       (ULONG)targetFull },
            { WBOBJA_FullPathSize,   (ULONG)sizeof(targetFull) },
            { WBOBJA_Type,           (ULONG)&type },
            { TAG_DONE, 0 }
        };
        which = IWorkbench->WhichWorkbenchObjectA(wbWin,
            screenX - wbWin->LeftEdge, screenY - wbWin->TopEdge, wbtags);
    }

    /* Determine the destination directory */
    if (which == WBO_ICON)
    {
        /* dropped on an icon: a drawer/disk icon means copy INTO it */
        if (type == WBDRAWER || type == WBDISK)
        {
            strncpy(targetDrawer, targetFull, sizeof(targetDrawer) - 1);
            targetDrawer[sizeof(targetDrawer) - 1] = '\0';
        }
        /* otherwise (file/tool/project icon): targetDrawer is its containing drawer */
    }
    else if (which != WBO_DRAWER)
    {
        targetDrawer[0] = '\0';
    }

    if (targetDrawer[0] != '\0')
    {
        ULONG total = 0;
        for (i = 0; i < count; i++) total += CountFilesRecursive(paths[i]);
        ProgressOpen(win, total);
        {
            uint64 b = 0;
            for (i = 0; i < count; i++) b += CountBytesRecursive(paths[i]);
            ProgressSetBytes(b);
        }
        ResetCopyConflict();
        for (i = 0; i < count && !gProgCancel; i++)
        {
            char dst[PATH_MAX_LEN];
            CONST_STRPTR base = FilePart(paths[i]);
            if (!base || !base[0] || !BuildChildPath(targetDrawer, base, dst, sizeof(dst)))
            { anyErr = TRUE; continue; }
            if (!DragTransfer(paths[i], dst)) anyErr = TRUE;
        }
        ProgressClose();
        if (anyErr)
            ShowMessage(LS(MSG_DRAGDROP_TITLE), LS(MSG_DROP_COPY_FAILED));
    }
    else
    {
        /* not a Workbench drawer: try a VFM window (drag to another instance) */
        {
            struct MsgPort *vfmPort = NULL;
            BOOL isShell = IsShellWindow(wbWin);
            BOOL ourTask = (wbWin && wbWin->UserPort &&
                            wbWin->UserPort->mp_SigTask == (void *)IExec->FindTask(NULL)) ? TRUE : FALSE;
            BOOL wbTask = (wbWin && StrContainsNoCase(WindowTaskName(wbWin), "workbench")) ? TRUE : FALSE;
            DBG("drop candidate: wbWin=%p title='%s' task='%s' flags=%08lx shell=%ld ours=%ld wb=%ld sameScreen=%ld",
                (void *)wbWin,
                (wbWin && wbWin->Title) ? wbWin->Title : "?",
                wbWin ? WindowTaskName(wbWin) : "-",
                wbWin ? (unsigned long)wbWin->Flags : 0UL,
                (long)isShell, (long)ourTask, (long)wbTask,
                (wbWin && win) ? (long)(wbWin->WScreen == win->WScreen) : -1L);
            if (!isShell && IsVFMWindow(wbWin, &vfmPort) && vfmPort)
            {
                DBG("drop on VFM window -> send VFM drop");
                SendVFMDrop(list, vfmPort, screenX, screenY);
            }
            else if (IsAmiDockWindow(wbWin))
            {
                /* AmiDock non è una finestra di testo: aggiunge l'oggetto
                   con il comando ARexx ADDOBJECT. */
                if (!AddPathsToAmiDock(paths, count))
                {
                    char dbg[512];
                    snprintf(dbg, sizeof(dbg),
                        "which=%lu type=%lu window=%s task=%s shell=%ld flags=%08lx x=%d y=%d drawer='%s'",
                        (unsigned long)which, (unsigned long)type,
                        wbWin->Title ? wbWin->Title : "?",
                        WindowTaskName(wbWin), (long)isShell,
                        (unsigned long)wbWin->Flags,
                        (int)screenX, (int)screenY, targetDrawer);
                    DBG("no drop target: %s", dbg);
                    ShowMessage(LS(MSG_DRAGDROP_TITLE), LS(MSG_DROP_COPY_FAILED));
                }
            }
            else if (wbWin && wbWin != win &&
                     !ourTask && !wbTask &&
                     !(wbWin->Flags & WFLG_BACKDROP))
            {
                /* Shell o file requester: inserisce il percorso come testo */
                if (!DropTextToExternalWindow(wbWin, paths, count))
                {
                    char dbg[512];
                    snprintf(dbg, sizeof(dbg),
                        "which=%lu type=%lu window=%s task=%s shell=%ld flags=%08lx x=%d y=%d drawer='%s'",
                        (unsigned long)which, (unsigned long)type,
                        wbWin->Title ? wbWin->Title : "?",
                        WindowTaskName(wbWin), (long)isShell,
                        (unsigned long)wbWin->Flags,
                        (int)screenX, (int)screenY, targetDrawer);
                    DBG("no drop target: %s", dbg);
                    ShowMessage(LS(MSG_DRAGDROP_TITLE), dbg);
                }
            }
            else
            {
                char dbg[512];
                snprintf(dbg, sizeof(dbg),
                    "which=%lu type=%lu window=%s task=%s shell=%ld flags=%08lx x=%d y=%d drawer='%s'",
                    (unsigned long)which, (unsigned long)type,
                    wbWin ? (wbWin->Title ? wbWin->Title : "?") : "none",
                    wbWin ? WindowTaskName(wbWin) : "-",
                    (long)isShell, wbWin ? (unsigned long)wbWin->Flags : 0UL,
                    (int)screenX, (int)screenY, targetDrawer);
                DBG("no drop target: %s", dbg);
                ShowMessage(LS(MSG_DRAGDROP_TITLE), dbg);
            }
        }
    }

    IExec->FreeVec(paths);
}

void PerformDragDrop(struct List *list, struct Window *win)
{
    WORD screenX, screenY;

    if (!gDragActive)
    {
        gDragButtonDown = FALSE;
        return;
    }
    /* capture the actual pointer position on screen at release */
    if (win->WScreen)
    {
        screenX = win->WScreen->MouseX;
        screenY = win->WScreen->MouseY;
    }
    else
    {
        screenX = (WORD)(win->LeftEdge + win->MouseX);
        screenY = (WORD)(win->TopEdge + win->MouseY);
    }
    gDragButtonDown = FALSE;
    gDragActive = FALSE;
    /* restore the normal pointer *before* any modal dialog/operation,
       otherwise the drag pointer stays on screen while copying/moving */
    EndDragVisual(win);
    DoDragDropOut(list, win, screenX, screenY);
}

void DoInfo(struct List *list, struct Window *win)
{
    struct Node *node; ULONG index = MAX_ENTRIES; STRPTR name = NULL;
    char full[PATH_MAX_LEN]; struct Screen *scr = NULL;
    LONG selCount = CountSelectedEntries(list);
    if (selCount != 1) { ShowMessage(LS(MSG_INFO_TITLE), LS(MSG_INFO_SELECT_ONE)); return; }
    node = GetFirstSelectedNode(list); if (!node) return;
    IListBrowser->GetListBrowserNodeAttrs(node, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
    if (!name || index >= (ULONG)gEntryCount) return;
    if (!BuildChildPath(gPathBuffer, name, full, sizeof(full))) return;
    if (win) scr = win->WScreen;

    /* WBInfo requires lock and name relative to lock; try lock on parent + name */
    {
        BPTR lock = IDOS->Lock(gPathBuffer, SHARED_LOCK);
        if (lock)
        {
            if (WorkbenchBase && IWorkbench)
            {
                IWorkbench->WBInfo(lock, name, scr);
            }
            IDOS->UnLock(lock);
        }
        else
        {
            /* fallback: try OpenWorkbenchObject on path (shows icon) */
            if (WorkbenchBase && IWorkbench)
            {
                IWorkbench->OpenWorkbenchObjectA(full, NULL);
            }
        }
    }
}

BOOL IsPointOverGadget(Object *gad, struct Window *win, WORD x, WORD y)
{
    struct Gadget *g = (struct Gadget *)gad;
    if (!g || !win) return FALSE;
    if (g->Width == 0 || g->Height == 0) return FALSE;
    return (x >= g->LeftEdge && x < g->LeftEdge + g->Width &&
            y >= g->TopEdge  && y < g->TopEdge  + g->Height);
}
void HandleAppDrop(struct AppMessage *appmsg, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    LONG i;
    if (!appmsg || appmsg->am_NumArgs < 1) return;

    /* If dropped on Path gadget: navigate (original behavior) */
    if (IsPointOverGadget(pathGad, win, appmsg->am_MouseX, appmsg->am_MouseY) ||
        (pathGad && ((struct Gadget *)pathGad)->Height > 0 &&
         appmsg->am_MouseY >= ((struct Gadget *)pathGad)->TopEdge - 2 &&
         appmsg->am_MouseY < ((struct Gadget *)pathGad)->TopEdge + ((struct Gadget *)pathGad)->Height + 2 &&
         appmsg->am_MouseX >= ((struct Gadget *)pathGad)->LeftEdge - 2))
    {
        struct WBArg *arg = &appmsg->am_ArgList[0];
        char path[PATH_MAX_LEN]; char dirPath[PATH_MAX_LEN]; path[0]='\0';
        if (arg->wa_Lock) IDOS->NameFromLock(arg->wa_Lock, path, sizeof(path));
        if (arg->wa_Name && arg->wa_Name[0] != '\0')
        {
            if (path[0] == '\0') { strncpy(path, arg->wa_Name, sizeof(path)-1); path[sizeof(path)-1]='\0'; }
            else IDOS->AddPart(path, arg->wa_Name, sizeof(path));
        }
        if (path[0] == '\0') return;
        if (IsDirectoryPath(path)) NavigateTo(path, listGad, pathGad, statusGad, win, list);
        else
        {
            BPTR lock = IDOS->Lock(path, SHARED_LOCK);
            if (lock)
            {
                BPTR parent = IDOS->ParentDir(lock);
                if (parent)
                {
                    if (IDOS->NameFromLock(parent, dirPath, sizeof(dirPath)))
                        NavigateTo(dirPath, listGad, pathGad, statusGad, win, list);
                    IDOS->UnLock(parent);
                }
                IDOS->UnLock(lock);
            }
        }
        return;
    }

    /* Dropped elsewhere in window: copy into current path */
    if (IsRootPath(gPathBuffer))
    {
        ShowMessage(LS(MSG_DRAGDROP_TITLE), LS(MSG_DROP_ROOT));
        return;
    }
    {
        LONG nArgs = appmsg->am_NumArgs;
        char (*srcs)[PATH_MAX_LEN];
        LONG nPaths = 0;
        ULONG total = 0;

        srcs = (char (*)[PATH_MAX_LEN])IExec->AllocVecTags((nArgs > 0 ? nArgs : 1) * PATH_MAX_LEN, AVT_Type, MEMF_PRIVATE, TAG_DONE);
        if (!srcs) return;

        for (i = 0; i < nArgs; i++)
        {
            struct WBArg *arg = &appmsg->am_ArgList[i];
            char src[PATH_MAX_LEN];
            src[0]='\0';
            if (arg->wa_Lock) IDOS->NameFromLock(arg->wa_Lock, src, sizeof(src));
            if (arg->wa_Name && arg->wa_Name[0] != '\0')
            {
                if (src[0] == '\0') { strncpy(src, arg->wa_Name, sizeof(src)-1); src[sizeof(src)-1]='\0'; }
                else IDOS->AddPart(src, arg->wa_Name, sizeof(src));
            }
            if (src[0] == '\0') continue;
            strncpy(srcs[nPaths], src, PATH_MAX_LEN - 1); srcs[nPaths][PATH_MAX_LEN - 1] = '\0';
            nPaths++;
        }
        if (nPaths == 0) { IExec->FreeVec(srcs); return; }

        for (i = 0; i < nPaths; i++) total += CountFilesRecursive(srcs[i]);

        ProgressOpen(win, total);
        {
            uint64 b = 0;
            for (i = 0; i < nPaths; i++) b += CountBytesRecursive(srcs[i]);
            ProgressSetBytes(b);
        }

        ResetCopyConflict();
        for (i = 0; i < nPaths && !gProgCancel; i++)
        {
            char dst[PATH_MAX_LEN]; CONST_STRPTR base = FilePart(srcs[i]);
            if (!base || !base[0] || !BuildChildPath(gPathBuffer, base, dst, sizeof(dst))) continue;
            if (!DragTransfer(srcs[i], dst))
            {
                char em[300]; snprintf(em, sizeof(em), LS(MSG_DROP_COPY_ERROR_NAME), base);
                ShowMessage(LS(MSG_DRAGDROP_TITLE), em);
            }
        }
        ProgressClose();
        IExec->FreeVec(srcs);
    }
    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
}

/* ===== Cursore di trascinamento e messaggi VFM->VFM ===== */

void StartDragVisual(struct List *list, struct Window *win)
{
    /* Use the standard, always-visible drag & drop cursor.
       The old code built a pointer from the icon bitmap; on many screen
       depths that pointer came out blank, so the mouse pointer vanished
       while dragging. */
    (void)list;
    IIntuition->SetWindowPointer(win, WA_PointerType, POINTERTYPE_DRAGANDDROP, TAG_DONE);
    DBG("drag pointer: standard drag&drop cursor");
}

void EndDragVisual(struct Window *win)
{
    IIntuition->SetWindowPointer(win, WA_PointerType, POINTERTYPE_NORMAL, TAG_DONE);
}

void SendVFMDrop(struct List *list, struct MsgPort *port, WORD screenX, WORD screenY)
{
    char (*paths)[PATH_MAX_LEN];
    LONG count = 0, i, totalLen = 0;
    struct VFMDropMessage *drop;

    if (!port) { DBG("VFM target port is NULL"); return; }

    paths = (char (*)[PATH_MAX_LEN])IExec->AllocVecTags(MAX_CLIPBOARD * PATH_MAX_LEN,
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (!paths) return;
    if (!CollectSelectedFullPaths(list, paths, MAX_CLIPBOARD, &count))
    {
        IExec->FreeVec(paths);
        return;
    }

    totalLen = 0;
    for (i = 0; i < count; i++) totalLen += strlen(paths[i]) + 1;

    /* Il messaggio passa a un'altra istanza VFM: deve essere in memoria
       condivisa, non MEMF_PRIVATE del processo sorgente. */
    drop = (struct VFMDropMessage *)IExec->AllocVecTags(sizeof(struct VFMDropMessage) + totalLen,
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!drop) { IExec->FreeVec(paths); return; }

    drop->vfm_Message.mn_Node.ln_Type = NT_MESSAGE;
    drop->vfm_Message.mn_Length = sizeof(struct VFMDropMessage) + totalLen;
    drop->vfm_Type = VFMDROP_TYPE;
    drop->vfm_NumPaths = count;
    drop->vfm_MouseX = screenX;
    drop->vfm_MouseY = screenY;
    {
        char *dst = drop->vfm_Paths;
        for (i = 0; i < count; i++)
        {
            strcpy(dst, paths[i]);
            dst += strlen(paths[i]) + 1;
        }
        *dst = '\0';
    }

    IExec->PutMsg(port, (struct Message *)drop);
    DBG("VFM drop sent (%ld paths)", (long)count);
    IExec->FreeVec(paths);
}

/* Esegue un drop VFM->VFM sul pannello di destinazione. */
void HandleVFMDrop(struct VFMDropMessage *drop, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    LONG i;
    BOOL anyErr = FALSE;

    DBG("VFM drop received: %ld path(s)", (long)drop->vfm_NumPaths);

    if (IsRootPath(gPathBuffer))
    {
        DBG("VFM drop in assigns view: ignored");
    }
    else
    {
        char *q = drop->vfm_Paths;
        ULONG total = 0;

        for (i = 0; i < drop->vfm_NumPaths; i++)
        {
            if (q[0] == '\0') break;
            total += CountFilesRecursive(q);
            q += strlen(q) + 1;
        }

        ProgressOpen(win, total);
        {
            uint64 b = 0;
            q = drop->vfm_Paths;
            for (i = 0; i < drop->vfm_NumPaths; i++)
            {
                if (q[0] == '\0') break;
                b += CountBytesRecursive(q);
                q += strlen(q) + 1;
            }
            ProgressSetBytes(b);
        }

        ResetCopyConflict();
        q = drop->vfm_Paths;
        for (i = 0; i < drop->vfm_NumPaths && !gProgCancel; i++)
        {
            char dst[PATH_MAX_LEN];
            CONST_STRPTR base;
            if (q[0] == '\0') break;
            base = FilePart(q);
            if (!base || !base[0] || !BuildChildPath(gPathBuffer, base, dst, sizeof(dst)))
            { anyErr = TRUE; q += strlen(q) + 1; continue; }
            if (!DragTransfer(q, dst)) anyErr = TRUE;
            q += strlen(q) + 1;
        }
        ProgressClose();

        if (anyErr)
            ShowMessage(LS(MSG_DRAGDROP_TITLE), LS(MSG_DROP_COPY_FAILED));
    }
    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
    IExec->FreeVec(drop);
}
