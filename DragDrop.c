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
 * Drag & drop: raccolta dei percorsi selezionati, individuazione della
 * destinazione sotto il puntatore (ResolveDropTarget, usata sia per
 * evidenziare la destinazione durante il trascinamento sia per il drop),
 * trasferimento VFM->VFM e da/verso Workbench.
 * Prima di ogni trasferimento di file VFM chiede Copia / Sposta / Annulla.
 * Verso altri programmi fa come il Workbench: manda un AppMessage alla
 * AppWindow sotto il puntatore, trovata nell'elenco tenuto da VFM
 * (AppWinRegistry.c).  Se la finestra non e' nota,
 * a una Shell o a un requester il percorso viene digitato.  Nessun
 * comando ARexx durante il drop.
 * L'aspetto (icone che seguono il mouse, cornice) e' in DragVisual.c.
 */

#include "VagaFileManager.h"
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <libraries/keymap.h>
#include <interfaces/keymap.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <proto/layers.h>
#include <exec/semaphores.h>
#include "AppWindowRegistry.h"

/* Librerie e device per l'iniezione del testo: tenuti aperti per tutta la
   vita del processo, così gli eventi non vengono persi alla chiusura. */
static struct Library *gKeymapBase = NULL;
static struct KeymapIFace *gKeymap = NULL;
static struct MsgPort *gInputPort = NULL;
static struct IOStdReq gInputIO;
static BOOL gInputOpen = FALSE;

static void ShutdownAppDrop(void);
static void WindowScreenBox(struct Window *w, struct IBox *b);

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

    EndDragVisual(NULL);
    ShutdownAppDrop();
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

/* Chiede all'utente se copiare o spostare gli elementi trascinati.
   La finestra e' un requester ReAction aperto con WPOS_CENTERMOUSE, cosi'
   appare sotto il puntatore invece che al centro dello schermo.
   Ritorna 1 = copia, 2 = sposta, 0 = annulla. */
static LONG AskDragTransferMode(struct Screen *scr, CONST_STRPTR target)
{
    enum { DRAGREQ_COPY = 1, DRAGREQ_MOVE, DRAGREQ_CANCEL };
    Object *winObj = NULL;
    Object *copyBtn = NULL, *moveBtn = NULL, *cancelBtn = NULL;
    struct Window *rw = NULL;
    BOOL done = FALSE;
    LONG result = 0;
    ULONG sig = 0;
    char prompt[96];
    char pathText[80];

    snprintf(prompt, sizeof(prompt), "%s / %s",
        LS(MSG_COPY), LS(MSG_MOVE));

    if (!target) target = "";
    if (strlen(target) > 56)
        snprintf(pathText, sizeof(pathText), "%.53s...", target);
    else
        snprintf(pathText, sizeof(pathText), "%s", target);

    copyBtn = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, DRAGREQ_COPY, GA_RelVerify, TRUE,
        GA_Text, (ULONG)LS(MSG_COPY), GA_Underscore, 0, TAG_DONE);
    moveBtn = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, DRAGREQ_MOVE, GA_RelVerify, TRUE,
        GA_Text, (ULONG)LS(MSG_MOVE), GA_Underscore, 0, TAG_DONE);
    cancelBtn = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, DRAGREQ_CANCEL, GA_RelVerify, TRUE,
        GA_Text, (ULONG)LS(MSG_BTN_CANCEL), GA_Underscore, 0, TAG_DONE);

    winObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)LS(MSG_DRAGDROP_TITLE),
        WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, FALSE,
        WA_InnerWidth, 480, WA_InnerHeight, 100,
        scr ? WA_PubScreen : TAG_IGNORE, scr ? (ULONG)scr : 0,
        WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE, LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget",
                GA_ReadOnly, TRUE, GA_Text, (ULONG)prompt,
                GA_Underscore, 0, BUTTON_BevelStyle, BVS_NONE,
                BUTTON_Transparent, TRUE, TAG_DONE), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget",
                GA_ReadOnly, TRUE, GA_Text, (ULONG)pathText,
                GA_Underscore, 0, BUTTON_BevelStyle, BVS_NONE,
                BUTTON_Transparent, TRUE, TAG_DONE), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, (ULONG)copyBtn, CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)moveBtn, CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)cancelBtn, CHILD_WeightedWidth, 0,
                TAG_DONE), CHILD_WeightedHeight, 0,
            TAG_DONE), TAG_DONE);

    if (winObj)
    {
        rw = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
        if (rw)
        {
            IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
            while (!done)
            {
                ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
                if (wait & SIGBREAKF_CTRL_C)
                {
                    result = 0;
                    done = TRUE;
                }
                if (wait & sig)
                {
                    ULONG res;
                    WORD code = 0;

                    while ((res = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                    {
                        switch (res & WMHI_CLASSMASK)
                        {
                            case WMHI_CLOSEWINDOW:
                                result = 0;
                                done = TRUE;
                                break;
                            case WMHI_GADGETUP:
                                switch (res & WMHI_GADGETMASK)
                                {
                                    case DRAGREQ_COPY:
                                        result = 1;
                                        done = TRUE;
                                        break;
                                    case DRAGREQ_MOVE:
                                        result = 2;
                                        done = TRUE;
                                        break;
                                    case DRAGREQ_CANCEL:
                                    default:
                                        result = 0;
                                        done = TRUE;
                                        break;
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
        }
        IIntuition->DisposeObject(winObj);
    }

    return result;
}

/* Esegue il trasferimento con la modalita' scelta dall'utente. */
static BOOL DragTransfer(CONST_STRPTR src, CONST_STRPTR dst, BOOL move)
{
    if (IsDestInsideSrcForCopy(src, dst))
    {
        DBG("drop refused: destination inside source '%s'", src);
        return FALSE;
    }
    if (move)
        return MoveEntry(src, dst);
    return CopyEntryRecursive(src, dst);
}

/* Cassetto sotto il punto (coordinate della finestra) nel pannello p.
   Il FileBrowser restituisce la posizione visibile (riga o cella): il nodo
   corrispondente si cerca nella lista mostrata, e da LBNA_UserData si
   ricava l'indice dell'elemento (con il filtro attivo le due posizioni
   non coincidono).  Se cell non e' NULL riceve il rettangolo della riga o
   della cella, in coordinate della finestra. */
static BOOL PaneDrawerAt(int p, struct Window *win, WORD winX, WORD winY,
                         STRPTR out, ULONG outSize, struct IBox *cell)
{
    struct Pane *pn;
    struct FBGetIndexAt hi;
    struct Node *nd;
    LONG pos;
    ULONG index = MAX_ENTRIES;
    STRPTR name = NULL;

    if (p < 0 || p > 1 || !win || !out || outSize == 0) return FALSE;
    pn = &gPanes[p];
    if (!pn->listGad) return FALSE;
    if (((struct Gadget *)pn->listGad)->Width <= 0) return FALSE;

    memset(&hi, 0, sizeof(hi));
    hi.MethodID = FILEBROWSER_GetIndexAt;
    hi.fbgia_X = (int32)winX;   /* FileBrowser's IndexAt uses window coords */
    hi.fbgia_Y = (int32)winY;
    hi.fbgia_FontY = (win->RPort && win->RPort->Font)
        ? win->RPort->Font->tf_YSize : 0;
    hi.fbgia_Index = -1;
    IIntuition->IDoMethodA(pn->listGad, (Msg)&hi);
    if (hi.fbgia_Index < 0) return FALSE;

    for (nd = pn->fileList.lh_Head, pos = 0;
         nd->ln_Succ && pos < hi.fbgia_Index;
         nd = nd->ln_Succ, pos++)
        ;
    if (!nd->ln_Succ) return FALSE;

    IListBrowser->GetListBrowserNodeAttrs(nd, LBNA_UserData, &index,
        LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
    if (index >= (ULONG)pn->entryCount || !pn->isDir[index]) return FALSE;
    if (!name || !name[0]) name = pn->names[index];
    if (!name || !name[0]) return FALSE;

    if (!BuildChildPath(pn->pathBuffer, name, out, outSize))
        return FALSE;
    if (cell) *cell = hi.fbgia_Box;
    return TRUE;
}

static int PaneOfListGad(Object *listGad)
{
    int p;
    for (p = 0; p < 2; p++)
        if (gPanes[p].listGad && gPanes[p].listGad == listGad) return p;
    return gActivePane;
}

/* Se il punto di drop e' sopra un cassetto del pannello, restituisce il
   percorso di quel cassetto; altrimenti FALSE e il chiamante usa il
   percorso corrente del pannello.  Le coordinate sono relative alla
   finestra (non allo schermo). */
static BOOL DropDestinationAt(struct Window *win, Object *listGad,
                              WORD winX, WORD winY,
                              STRPTR out, ULONG outSize)
{
    return PaneDrawerAt(PaneOfListGad(listGad), win, winX, winY,
                        out, outSize, NULL);
}

/* Convert screen coordinates from a VFMDrop message to window coordinates.
   Gadget positions are relative to the window's outer top-left corner
   (borders included), so only LeftEdge/TopEdge are subtracted. */
static BOOL DropDestinationAtScreen(struct Window *win, Object *listGad,
                                    WORD screenX, WORD screenY,
                                    STRPTR out, ULONG outSize)
{
    if (!win) return FALSE;
    return DropDestinationAt(win, listGad,
        (WORD)(screenX - win->LeftEdge),
        (WORD)(screenY - win->TopEdge),
        out, outSize);
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

/* AmiDock e MultiEdit non ricevono drop da VFM: le loro AppPort sono
   private e la sola via pubblica sarebbe ARexx, che durante il drop
   bloccava il sistema (MultiEdit).  Il drop su queste finestre non fa
   nulla, come su una finestra che non accetta oggetti. */
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

static BOOL IsMultiEditWindow(struct Window *w)
{
    CONST_STRPTR task;

    if (!w) return FALSE;
    task = WindowTaskName(w);
    return (task && StrContainsNoCase(task, "multiedit")) ? TRUE : FALSE;
}

/* ===== AppWindow di altri programmi (come il Workbench) ===== */

/* Porta su cui tornano gli AppMessage spediti.  Il drop e' asincrono: VFM
   non aspetta la risposta. */
static struct MsgPort *gAppDropPort = NULL;
static LONG gAppDropPending = 0;

enum { AW_UNKNOWN = 0, AW_MISS, AW_HIT };

/* Cerca la finestra nell'elenco pubblico; va chiamata col semaforo
   ottenuto.  AW_MISS: AppWindow con drop zone, ma il punto e' fuori da
   tutte (il Workbench in quel caso non manda nulla). */
static LONG MatchAppWindow(struct VFMAppWinRegistry *reg, struct Window *w,
                           WORD sx, WORD sy, struct VFMAppWin **pe,
                           struct VFMAppZone **pz, struct IBox *box)
{
    struct MinNode *n;
    WORD mx = (WORD)(sx - w->LeftEdge), my = (WORD)(sy - w->TopEdge);

    for (n = reg->windows.mlh_Head; n->mln_Succ; n = n->mln_Succ)
    {
        struct VFMAppWin *e = (struct VFMAppWin *)n;
        struct MinNode *zn;

        if (e->win != w) continue;
        if (pe) *pe = e;
        if (pz) *pz = NULL;
        if (!e->zones.mlh_Head->mln_Succ)
        {
            if (box) WindowScreenBox(w, box);
            return AW_HIT;
        }
        for (zn = e->zones.mlh_Head; zn->mln_Succ; zn = zn->mln_Succ)
        {
            struct VFMAppZone *z = (struct VFMAppZone *)zn;
            WORD zl = z->relRight  ? (WORD)(w->Width + z->left)    : z->left;
            WORD zt = z->relBottom ? (WORD)(w->Height + z->top)    : z->top;
            WORD zw = z->relWidth  ? (WORD)(w->Width + z->width)   : z->width;
            WORD zh = z->relHeight ? (WORD)(w->Height + z->height) : z->height;
            if (mx >= zl && mx < zl + zw && my >= zt && my < zt + zh)
            {
                if (pz) *pz = z;
                if (box)
                {
                    box->Left = (WORD)(w->LeftEdge + zl);
                    box->Top = (WORD)(w->TopEdge + zt);
                    box->Width = zw;
                    box->Height = zh;
                }
                return AW_HIT;
            }
        }
        return AW_MISS;
    }
    return AW_UNKNOWN;
}

/* Apre l'elenco pubblico in lettura; NULL se nessun VFM lo ha
   installato.  Va chiuso con ReleaseAppRegistry(). */
static struct VFMAppWinRegistry *ObtainAppRegistry(void)
{
    struct VFMAppWinRegistry *reg;

    IExec->Forbid();
    reg = (struct VFMAppWinRegistry *)IExec->FindSemaphore(VFM_AWREG_NAME);
    if (reg) IExec->ObtainSemaphoreShared(&reg->sem);
    IExec->Permit();
    if (reg && reg->version != VFM_AWREG_VERSION)
    {
        IExec->ReleaseSemaphore(&reg->sem);
        reg = NULL;
    }
    return reg;
}

static void ReleaseAppRegistry(struct VFMAppWinRegistry *reg)
{
    if (reg) IExec->ReleaseSemaphore(&reg->sem);
}

static LONG LookupAppWindow(struct Window *w, WORD sx, WORD sy, struct IBox *box)
{
    struct VFMAppWinRegistry *reg = ObtainAppRegistry();
    LONG r;
    if (!reg) return AW_UNKNOWN;
    r = MatchAppWindow(reg, w, sx, sy, NULL, NULL, box);
    ReleaseAppRegistry(reg);
    return r;
}

static void FreeAppDropMessage(struct AppMessage *am)
{
    LONG i;
    if (!am) return;
    for (i = 0; i < am->am_NumArgs; i++)
        if (am->am_ArgList[i].wa_Lock) IDOS->UnLock(am->am_ArgList[i].wa_Lock);
    IExec->FreeVec(am);
}

ULONG AppDropReplySignal(void)
{
    return gAppDropPort ? (1UL << gAppDropPort->mp_SigBit) : 0;
}

void ProcessAppDropReplies(void)
{
    struct Message *m;
    if (!gAppDropPort) return;
    while ((m = IExec->GetMsg(gAppDropPort)) != NULL)
    {
        FreeAppDropMessage((struct AppMessage *)m);
        if (gAppDropPending > 0) gAppDropPending--;
    }
}

/* Alla chiusura: se un programma non ha ancora risposto, la porta resta
   allocata (liberarla farebbe rispondere a memoria non piu' nostra). */
static void ShutdownAppDrop(void)
{
    LONG i;
    if (!gAppDropPort) return;
    for (i = 0; i < 50 && gAppDropPending > 0; i++)
    {
        ProcessAppDropReplies();
        if (gAppDropPending > 0) IDOS->Delay(1);
    }
    ProcessAppDropReplies();
    if (gAppDropPending > 0)
    {
        DBG("AppWindow: %ld AppMessage senza risposta alla chiusura",
            (long)gAppDropPending);
        return;
    }
    IExec->FreeSysObject(ASOT_PORT, gAppDropPort);
    gAppDropPort = NULL;
}

/* Prepara l'argomento come il Workbench: file -> lock del cassetto che
   lo contiene + nome; cassetto o volume -> lock sull'oggetto, nome "". */
static BPTR LockWBArg(CONST_STRPTR path, CONST_STRPTR *name)
{
    BPTR lock, parent;
    struct ExamineData *ed;
    BOOL isDir = FALSE;

    *name = "";
    lock = IDOS->Lock(path, SHARED_LOCK);
    if (!lock) return 0;
    ed = IDOS->ExamineObjectTags(EX_LockInput, lock, TAG_END);
    if (ed)
    {
        isDir = EXD_IS_DIRECTORY(ed) ? TRUE : FALSE;
        IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
    }
    if (isDir) return lock;

    parent = IDOS->ParentDir(lock);
    if (!parent) return lock;
    IDOS->UnLock(lock);
    *name = IDOS->FilePart(path);
    return parent;
}

static struct AppMessage *BuildAppDropMessage(char (*paths)[PATH_MAX_LEN], LONG count)
{
    struct AppMessage *am;
    struct WBArg *args;
    char *names;
    ULONG size, namesLen = 0;
    LONG i, n = 0;

    for (i = 0; i < count; i++)
        namesLen += strlen(IDOS->FilePart(paths[i])) + 1;
    size = sizeof(struct AppMessage) + (ULONG)count * sizeof(struct WBArg) + namesLen;
    am = (struct AppMessage *)IExec->AllocVecTags(size,
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!am) return NULL;

    args = (struct WBArg *)(am + 1);
    names = (char *)(args + count);
    am->am_ArgList = args;
    for (i = 0; i < count; i++)
    {
        CONST_STRPTR nm;
        BPTR lock = LockWBArg(paths[i], &nm);
        if (!lock)
        {
            DBG("AppWindow: lock non riuscito per '%s'", paths[i]);
            continue;
        }
        strcpy(names, nm);
        args[n].wa_Lock = lock;
        args[n].wa_Name = (BYTE *)names;
        names += strlen(nm) + 1;
        n++;
        am->am_NumArgs = n;
    }
    if (n == 0)
    {
        IExec->FreeVec(am);
        return NULL;
    }

    am->am_Message.mn_Node.ln_Type = NT_MESSAGE;
    am->am_Message.mn_Length = (UWORD)(size > 0xFFFF ? 0xFFFF : size);
    am->am_Message.mn_ReplyPort = gAppDropPort;
    am->am_Version = AM_VERSION;
    am->am_Class = 0;
    return am;
}

/* Spedisce il drop alla AppWindow, come farebbe il Workbench. */
static BOOL SendAppWindowDrop(struct Window *w, WORD sx, WORD sy,
                              char (*paths)[PATH_MAX_LEN], LONG count)
{
    struct VFMAppWinRegistry *reg;
    struct VFMAppWin *e = NULL;
    struct VFMAppZone *z = NULL;
    struct AppMessage *am;
    ULONG secs = 0, micros = 0;
    BOOL sent = FALSE;

    if (!w || count <= 0) return FALSE;
    if (!gAppDropPort)
    {
        gAppDropPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
        if (!gAppDropPort) return FALSE;
    }
    /* lock e memoria si preparano prima di tenere il semaforo */
    am = BuildAppDropMessage(paths, count);
    if (!am) return FALSE;
    IIntuition->CurrentTime(&secs, &micros);

    reg = ObtainAppRegistry();
    if (reg && MatchAppWindow(reg, w, sx, sy, &e, &z, NULL) == AW_HIT && e)
    {
        am->am_Type = z ? AMTYPE_APPWINDOWZONE : AMTYPE_APPWINDOW;
        am->am_ID = z ? z->id : e->id;
        am->am_UserData = z ? z->userdata : e->userdata;
        am->am_MouseX = (WORD)(sx - w->LeftEdge);
        am->am_MouseY = (WORD)(sy - w->TopEdge);
        am->am_Seconds = secs;
        am->am_Micros = micros;
        /* spedito tenendo il semaforo: RemoveAppWindow attende, quindi
           la porta e' ancora valida */
        IExec->PutMsg(e->port, (struct Message *)am);
        gAppDropPending++;
        sent = TRUE;
    }
    ReleaseAppRegistry(reg);

    if (!sent)
    {
        FreeAppDropMessage(am);
        return FALSE;
    }
    DBG("AppWindow: AppMessage %s inviato a '%s' (%ld oggetti)",
        z ? "zona" : "finestra", w->Title ? (CONST_STRPTR)w->Title : "?",
        (long)am->am_NumArgs);
    return TRUE;
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

/* Finestra piu' in alto nel punto x,y, secondo l'ordine reale dei layer
   (dal davanti al fondo), come fa Intuition per i clic.  Le finestre del
   fantasma e della cornice non contano. */
static struct Window *WindowAtPointLayers(struct Screen *s, WORD x, WORD y)
{
    struct Layer *l;
    struct Window *found = NULL;

    if (!s) return NULL;
    ILayers->LockLayerInfo(&s->LayerInfo);
    for (l = s->LayerInfo.top_layer; l; l = l->back)
    {
        struct Window *w = (struct Window *)l->Window;
        if (!w) continue;
        if (l->Flags & LAYERHIDDEN) continue;
        if (IsDragOverlayWindow(w)) continue;
        if (x < l->bounds.MinX || x > l->bounds.MaxX ||
            y < l->bounds.MinY || y > l->bounds.MaxY) continue;
        found = w;
        break;
    }
    ILayers->UnlockLayerInfo(&s->LayerInfo);
    return found;
}

/* Versione semplice per gli screen diversi da quello di VFM: segue la
   lista delle finestre (usata solo per trovare una Shell). */
static struct Window *WindowAtPoint(struct Screen *s, WORD x, WORD y)
{
    struct Window *w;

    if (!s) return NULL;
    for (w = s->FirstWindow; w; w = w->NextWindow)
    {
        if (IsDragOverlayWindow(w)) continue;
        if (x >= w->LeftEdge && x < w->LeftEdge + w->Width &&
            y >= w->TopEdge  && y < w->TopEdge  + w->Height)
            return w;
    }
    return NULL;
}

/* Cerca la finestra sotto il puntatore.  Se lo screen davanti e' quello
   di VFM si usa l'ordine dei layer; altrimenti si cerca sullo screen
   davanti (e, solo per le Shell, sugli altri).  Le coordinate restituite
   sono quelle dello screen della finestra trovata. */
static struct Window *FindDropWindow(struct Window *ownWin, WORD *px, WORD *py)
{
    struct Screen *first, *s, *own = ownWin ? ownWin->WScreen : NULL;
    struct Window *w, *fallback = NULL;
    WORD fx = 0, fy = 0;

    first = IIntuition->LockScreenList();
    if (!first || first == own)
    {
        WORD x, y;
        if (first) IIntuition->UnlockScreenList();
        if (!own) return NULL;
        x = own->MouseX;
        y = own->MouseY;
        if (px) *px = x;
        if (py) *py = y;
        return WindowAtPointLayers(own, x, y);
    }

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
    }
    IIntuition->UnlockScreenList();

    if (!fallback && own)
    {
        fx = own->MouseX;
        fy = own->MouseY;
        fallback = WindowAtPointLayers(own, fx, fy);
    }
    if (px) *px = fx;
    if (py) *py = fy;
    return fallback;
}

/* ===== Destinazione del drop ===== */

enum
{
    DT_NONE = 0,     /* niente: il drop non fa nulla (come nel Workbench) */
    DT_VFM,          /* finestra VFM (questa o un'altra istanza) */
    DT_WORKBENCH,    /* cassetto o icona di cassetto del Workbench */
    DT_APPWIN,       /* AppWindow di un altro programma: AppMessage */
    DT_TEXT          /* Shell / requester: percorso digitato */
};

struct DropTarget
{
    LONG type;
    struct Window *win;
    WORD x, y;                    /* coordinate dello screen di win */
    struct MsgPort *vfmPort;
    struct IBox box;              /* da evidenziare, coordinate schermo */
    char drawer[PATH_MAX_LEN];    /* cassetto di destinazione */
};

/* Percorsi trascinati e cartella di origine, raccolti all'avvio del drag
   e usati per l'evidenziazione (non si evidenzia la cartella stessa). */
static char (*gDragPaths)[PATH_MAX_LEN] = NULL;
static LONG gDragCount = 0;
static char gDragSourceDir[PATH_MAX_LEN];
static struct DropTarget gTickTarget;

static void WindowScreenBox(struct Window *w, struct IBox *b)
{
    b->Left = w->LeftEdge;
    b->Top = w->TopEdge;
    b->Width = w->Width;
    b->Height = w->Height;
}

static void WindowInnerBox(struct Window *w, struct IBox *b)
{
    b->Left = w->LeftEdge + w->BorderLeft;
    b->Top = w->TopEdge + w->BorderTop;
    b->Width = w->Width - w->BorderLeft - w->BorderRight;
    b->Height = w->Height - w->BorderTop - w->BorderBottom;
    if (b->Width <= 0 || b->Height <= 0) WindowScreenBox(w, b);
}

static BOOL BoxContains(const struct IBox *b, WORD x, WORD y)
{
    return (x >= b->Left && x < b->Left + b->Width &&
            y >= b->Top && y < b->Top + b->Height) ? TRUE : FALSE;
}

/* Allarga il rettangolo perche' la cornice stia attorno all'oggetto. */
static void GrowBox(struct IBox *b, WORD d)
{
    b->Left -= d;
    b->Top -= d;
    b->Width += 2 * d;
    b->Height += 2 * d;
}

static BOOL SamePath(CONST_STRPTR a, CONST_STRPTR b)
{
    ULONG la, lb;
    if (!a || !b) return FALSE;
    la = strlen(a);
    lb = strlen(b);
    while (la > 0 && a[la - 1] == '/') la--;
    while (lb > 0 && b[lb - 1] == '/') lb--;
    if (la != lb) return FALSE;
    return (IUtility->Strnicmp(a, b, la) == 0) ? TRUE : FALSE;
}

static BOOL IsDraggedPath(CONST_STRPTR path, char (*paths)[PATH_MAX_LEN], LONG count)
{
    LONG i;
    if (!paths) return FALSE;
    for (i = 0; i < count; i++)
        if (SamePath(path, paths[i])) return TRUE;
    return FALSE;
}

/* Drop sulla finestra di questo VFM: cassetto sotto il puntatore, oppure
   la cartella del pannello.  Rilasciare nella cartella di origine non fa
   nulla, come trascinare un'icona nella sua stessa finestra. */
static void ResolveOwnWindow(struct Window *win, struct DropTarget *t,
                             char (*paths)[PATH_MAX_LEN], LONG count,
                             CONST_STRPTR srcDir)
{
    WORD wx = (WORD)(t->x - win->LeftEdge);
    WORD wy = (WORD)(t->y - win->TopEdge);
    int p;

    for (p = 0; p < 2; p++)
    {
        struct Gadget *g = (struct Gadget *)gPanes[p].listGad;
        struct IBox cell;

        if (!g) continue;
        if (wx < g->LeftEdge || wx >= g->LeftEdge + g->Width ||
            wy < g->TopEdge || wy >= g->TopEdge + g->Height) continue;
        if (IsRootPath(gPanes[p].pathBuffer)) return;

        if (PaneDrawerAt(p, win, wx, wy, t->drawer, sizeof(t->drawer), &cell))
        {
            if (IsDraggedPath(t->drawer, paths, count))
            {
                t->drawer[0] = '\0';
                return;
            }
            t->type = DT_VFM;
            if (cell.Width > 0 && cell.Height > 0)
            {
                t->box.Left = win->LeftEdge + cell.Left;
                t->box.Top = win->TopEdge + cell.Top;
                t->box.Width = cell.Width;
                t->box.Height = cell.Height;
            }
            return;
        }

        t->drawer[0] = '\0';
        if (srcDir && SamePath(gPanes[p].pathBuffer, srcDir)) return;
        t->type = DT_VFM;
        strncpy(t->drawer, gPanes[p].pathBuffer, sizeof(t->drawer) - 1);
        t->drawer[sizeof(t->drawer) - 1] = '\0';
        t->box.Left = win->LeftEdge + g->LeftEdge;
        t->box.Top = win->TopEdge + g->TopEdge;
        t->box.Width = g->Width;
        t->box.Height = g->Height;
        return;
    }
}

/* Cassetto o icona del Workbench sotto il puntatore. */
static BOOL ResolveWorkbench(struct DropTarget *t, char (*paths)[PATH_MAX_LEN],
                             LONG count, CONST_STRPTR srcDir)
{
    struct Window *w = t->win;
    char full[PATH_MAX_LEN];
    ULONG which, type = 0;
    LONG il = 0, it = 0, iw = 0, ih = 0;
    BOOL iconBox = FALSE;

    if (!WorkbenchBase || !IWorkbench || !w) return FALSE;
    t->drawer[0] = '\0';
    full[0] = '\0';
    {
        struct TagItem wbtags[] =
        {
            { WBOBJA_DrawerPath,     (ULONG)t->drawer },
            { WBOBJA_DrawerPathSize, (ULONG)sizeof(t->drawer) },
            { WBOBJA_FullPath,       (ULONG)full },
            { WBOBJA_FullPathSize,   (ULONG)sizeof(full) },
            { WBOBJA_Type,           (ULONG)&type },
            { WBOBJA_Left,           (ULONG)&il },
            { WBOBJA_Top,            (ULONG)&it },
            { WBOBJA_Width,          (ULONG)&iw },
            { WBOBJA_Height,         (ULONG)&ih },
            { TAG_DONE, 0 }
        };
        which = IWorkbench->WhichWorkbenchObjectA(w,
            t->x - w->LeftEdge, t->y - w->TopEdge, wbtags);
    }

    if (which == WBO_ICON)
    {
        /* icona di cassetto o disco: si copia DENTRO; icona di file: la
           destinazione e' il cassetto che la contiene */
        if (type == WBDRAWER || type == WBDISK)
        {
            strncpy(t->drawer, full, sizeof(t->drawer) - 1);
            t->drawer[sizeof(t->drawer) - 1] = '\0';
            iconBox = TRUE;
        }
    }
    else if (which != WBO_DRAWER)
    {
        t->drawer[0] = '\0';
    }
    if (!t->drawer[0]) return FALSE;
    if (IsDraggedPath(t->drawer, paths, count)) return FALSE;
    if (!iconBox && srcDir && SamePath(t->drawer, srcDir)) return FALSE;

    t->type = DT_WORKBENCH;
    WindowInnerBox(w, &t->box);
    if (iconBox && iw > 0 && ih > 0)
    {
        /* la posizione dell'icona e' relativa alla finestra: si prova
           prima rispetto al bordo esterno, poi all'area interna */
        struct IBox b;
        b.Left = w->LeftEdge + il;
        b.Top = w->TopEdge + it;
        b.Width = iw;
        b.Height = ih;
        if (!BoxContains(&b, t->x, t->y))
        {
            b.Left += w->BorderLeft;
            b.Top += w->BorderTop;
        }
        if (BoxContains(&b, t->x, t->y))
        {
            GrowBox(&b, 4);
            t->box = b;
        }
    }
    return TRUE;
}

/* Determina cosa succederebbe rilasciando ora: usata sia durante il
   trascinamento (evidenziazione) sia al rilascio. */
static void ResolveDropTarget(struct Window *ownWin, struct DropTarget *t,
                              char (*paths)[PATH_MAX_LEN], LONG count,
                              CONST_STRPTR srcDir)
{
    struct Window *w;
    struct MsgPort *vfmPort = NULL;
    BOOL isShell, ourTask, wbTask;

    t->type = DT_NONE;
    t->win = NULL;
    t->x = t->y = 0;
    t->vfmPort = NULL;
    memset(&t->box, 0, sizeof(t->box));
    t->drawer[0] = '\0';

    w = FindDropWindow(ownWin, &t->x, &t->y);
    if (!w) return;
    t->win = w;

    isShell = IsShellWindow(w);
    if (!isShell && IsVFMWindow(w, &vfmPort) && vfmPort)
    {
        t->vfmPort = vfmPort;
        if (w == ownWin)
            ResolveOwnWindow(w, t, paths, count, srcDir);
        else
        {
            t->type = DT_VFM;
            WindowInnerBox(w, &t->box);
        }
        return;
    }

    wbTask = StrContainsNoCase(WindowTaskName(w), "workbench");
    if (!isShell && ResolveWorkbench(t, paths, count, srcDir)) return;
    if (wbTask) return;

    /* AppWindow nota (anche AmiDock, MultiEdit, Shell...): come il
       Workbench, le si manda un AppMessage */
    switch (LookupAppWindow(w, t->x, t->y, &t->box))
    {
        case AW_HIT:  t->type = DT_APPWIN; return;
        case AW_MISS: memset(&t->box, 0, sizeof(t->box)); return;
        default: break;
    }

    /* AmiDock e MultiEdit non elencati: niente (mai ARexx nel drop) */
    if (IsAmiDockWindow(w) || IsMultiEditWindow(w)) return;

    ourTask = (w->UserPort &&
               w->UserPort->mp_SigTask == (void *)IExec->FindTask(NULL)) ? TRUE : FALSE;
    if (w != ownWin && !ourTask && !(w->Flags & WFLG_BACKDROP))
    {
        t->type = DT_TEXT;
        WindowScreenBox(w, &t->box);
    }
}

/* Copia/sposta i percorsi in un cassetto, chiedendo prima la modalita'. */
static void TransferToDrawer(struct Window *win, struct Screen *reqScreen,
                             CONST_STRPTR drawer, char (*paths)[PATH_MAX_LEN],
                             LONG count)
{
    LONG mode, i;
    BOOL anyErr = FALSE;

    mode = AskDragTransferMode(reqScreen ? reqScreen : (win ? win->WScreen : NULL),
                               drawer);
    if (mode == 0) return;

    {
        BOOL move = (mode == 2);
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
            if (!base || !base[0] || !BuildChildPath(drawer, base, dst, sizeof(dst)))
            { anyErr = TRUE; continue; }
            if (!DragTransfer(paths[i], dst, move)) anyErr = TRUE;
        }
        /* annullato: nessun messaggio (ProgressClose azzera gProgCancel) */
        if (gProgCancel) anyErr = FALSE;
        ProgressClose();
    }
    if (anyErr)
        ShowMessage(LS(MSG_DRAGDROP_TITLE), LS(MSG_DROP_COPY_FAILED));
}

/* ===== Avvio e gestione del drag ===== */

/* Gestisce il rilascio del drag sulla destinazione sotto il puntatore. */
void DoDragDropOut(struct List *list, struct Window *win, WORD screenX, WORD screenY)
{
    char (*paths)[PATH_MAX_LEN];
    struct DropTarget *t;
    LONG count = 0;

    if (IsRootPath(gPathBuffer)) return; /* cannot drag assigns out */
    DBG("DoDragDropOut: x=%d y=%d", (int)screenX, (int)screenY);

    paths = (char (*)[PATH_MAX_LEN])IExec->AllocVecTags(MAX_CLIPBOARD * PATH_MAX_LEN,
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    t = (struct DropTarget *)IExec->AllocVecTags(sizeof(struct DropTarget),
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (!paths || !t ||
        !CollectSelectedFullPaths(list, paths, MAX_CLIPBOARD, &count))
    {
        if (paths) IExec->FreeVec(paths);
        if (t) IExec->FreeVec(t);
        return;
    }

    ResolveDropTarget(win, t, paths, count, gPathBuffer);
    DBG("drop target: type=%ld window='%s' task='%s' drawer='%s'",
        (long)t->type,
        (t->win && t->win->Title) ? t->win->Title : "-",
        t->win ? WindowTaskName(t->win) : "-",
        t->drawer);

    switch (t->type)
    {
        case DT_WORKBENCH:
            TransferToDrawer(win, t->win ? t->win->WScreen : NULL,
                             t->drawer, paths, count);
            break;
        case DT_VFM:
            SendVFMDrop(list, t->vfmPort, t->x, t->y);
            break;
        case DT_APPWIN:
            if (!SendAppWindowDrop(t->win, t->x, t->y, paths, count))
                DBG("AppWindow: drop non inviato");
            break;
        case DT_TEXT:
            if (!DropTextToExternalWindow(t->win, paths, count))
                DBG("drop testuale non riuscito");
            break;
        default:
            /* come nel Workbench: rilasciare dove non si puo' non fa nulla */
            break;
    }

    IExec->FreeVec(t);
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
    /* chiude icone e cornice *prima* di requester e trasferimento */
    EndDragVisual(win);
    DoDragDropOut(list, win, screenX, screenY);
}

/* Chiamata dal loop principale quando scatta il timer del drag: muove le
   icone e, quando il puntatore si ferma o ogni pochi passi, aggiorna la
   destinazione evidenziata. */
void DragTick(struct Window *win)
{
    static WORD lastX = -32768, lastY = -32768;
    static ULONG tick = 0;
    static BOOL resolved = FALSE;
    struct Screen *scr;
    WORD x, y;
    BOOL moved;

    if (!DragTimerFired()) return;
    if (!gDragActive || !win || !(scr = win->WScreen)) return;

    x = scr->MouseX;
    y = scr->MouseY;
    DragVisualMove(x, y);

    moved = (x != lastX || y != lastY);
    if (moved)
    {
        lastX = x;
        lastY = y;
        resolved = FALSE;
    }
    tick++;

    /* fermo (primo tick senza movimento) oppure ogni 4 tick in movimento */
    if ((!moved && !resolved) || (moved && (tick & 3) == 0))
    {
        ResolveDropTarget(win, &gTickTarget, gDragPaths, gDragCount,
                          gDragSourceDir);
        if (gTickTarget.type != DT_NONE && gTickTarget.win &&
            gTickTarget.box.Width > 0)
            DragVisualSetTarget(gTickTarget.win->WScreen, &gTickTarget.box);
        else
            DragVisualSetTarget(NULL, NULL);
        if (!moved) resolved = TRUE;
    }

    DragTimerRearm();
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
/* Workbench waits for the AppMessage reply: the message is replied as soon
   as the dropped paths are copied, before the Copy/Move requester and the
   transfer, so Workbench is not blocked meanwhile.  *replied tells the
   caller whether the message has already been replied. */
static void HandleAppDropInner(struct AppMessage *appmsg, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list, BOOL *replied)
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

        char targetDir[PATH_MAX_LEN];
        LONG mode;
        BOOL move;

        if (!DropDestinationAt(win, listGad, appmsg->am_MouseX,
                               appmsg->am_MouseY, targetDir, sizeof(targetDir)))
        {
            strncpy(targetDir, gPathBuffer, sizeof(targetDir) - 1);
            targetDir[sizeof(targetDir) - 1] = '\0';
        }

        /* Everything needed from the message is copied: release Workbench. */
        IExec->ReplyMsg((struct Message *)appmsg);
        appmsg = NULL;
        *replied = TRUE;

        mode = AskDragTransferMode(win ? win->WScreen : NULL, targetDir);

        if (mode == 0)
        {
            IExec->FreeVec(srcs);
            return;
        }
        move = (mode == 2);

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
            if (!base || !base[0] || !BuildChildPath(targetDir, base, dst, sizeof(dst))) continue;
            if (!DragTransfer(srcs[i], dst, move) && !gProgCancel)
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

/* Handles an AppWindow drop and always replies the AppMessage. */
void HandleAppDrop(struct AppMessage *appmsg, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    BOOL replied = FALSE;
    if (!appmsg) return;
    HandleAppDropInner(appmsg, listGad, pathGad, statusGad, win, list, &replied);
    if (!replied) IExec->ReplyMsg((struct Message *)appmsg);
}

/* ===== Aspetto del drag e messaggi VFM->VFM ===== */

static void FreeDragPaths(void)
{
    if (gDragPaths)
    {
        IExec->FreeVec(gDragPaths);
        gDragPaths = NULL;
    }
    gDragCount = 0;
    gDragSourceDir[0] = '\0';
}

void StartDragVisual(struct List *list, struct Window *win)
{
    FreeDragPaths();
    gDragPaths = (char (*)[PATH_MAX_LEN])IExec->AllocVecTags(MAX_CLIPBOARD * PATH_MAX_LEN,
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (gDragPaths &&
        !CollectSelectedFullPaths(list, gDragPaths, MAX_CLIPBOARD, &gDragCount))
        gDragCount = 0;
    AppRegistryEnsure();   /* se il VFM che lo teneva e' stato chiuso */
    strncpy(gDragSourceDir, gPathBuffer, sizeof(gDragSourceDir) - 1);
    gDragSourceDir[sizeof(gDragSourceDir) - 1] = '\0';

    /* Come nel Workbench: le icone selezionate seguono il puntatore.  Se
       non si possono mostrare, resta il cursore standard di drag&drop. */
    if (!DragVisualBegin(list, win))
        IIntuition->SetWindowPointer(win, WA_PointerType, POINTERTYPE_DRAGANDDROP, TAG_DONE);
}

void EndDragVisual(struct Window *win)
{
    DragVisualEnd();
    FreeDragPaths();
    if (win)
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
        char targetDir[PATH_MAX_LEN];
        ULONG total = 0;
        LONG mode;
        BOOL move;

        if (!DropDestinationAtScreen(win, listGad, drop->vfm_MouseX,
                                     drop->vfm_MouseY,
                                     targetDir, sizeof(targetDir)))
        {
            strncpy(targetDir, gPathBuffer, sizeof(targetDir) - 1);
            targetDir[sizeof(targetDir) - 1] = '\0';
        }

        mode = AskDragTransferMode(win ? win->WScreen : NULL, targetDir);
        if (mode == 0)
        {
            IExec->FreeVec(drop);
            return;
        }
        move = (mode == 2);

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
            if (!base || !base[0] || !BuildChildPath(targetDir, base, dst, sizeof(dst)))
            { anyErr = TRUE; q += strlen(q) + 1; continue; }
            if (!DragTransfer(q, dst, move)) anyErr = TRUE;
            q += strlen(q) + 1;
        }
        if (gProgCancel) anyErr = FALSE;   /* annullato: nessun messaggio */
        ProgressClose();

        if (anyErr)
            ShowMessage(LS(MSG_DRAGDROP_TITLE), LS(MSG_DROP_COPY_FAILED));
    }
    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
    IExec->FreeVec(drop);
}
