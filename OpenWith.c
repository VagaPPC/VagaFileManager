/*
 * VagaFileManager - two-pane file manager for AmigaOS 4.1
 * Copyright (C) 2026 vagappc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/* ===== Apri con... =====
   Apre i file selezionati con un programma scelto dall'utente.  Una
   finestra mostra gli ultimi programmi usati (al massimo OPENWITH_MAX,
   con la loro icona): un click lancia subito.  [Scegli...] apre il file
   requester; se la lista e' vuota si apre direttamente quello.

   Il programma parte come dal Workbench, una sola volta, con tutti i
   file come argomenti (WBArg), quindi rispetta tooltype e stack.

   La lista dei recenti sta in un file a parte, riletto a ogni apertura e
   riscritto subito dopo una scelta: piu' istanze di VFM vedono sempre la
   stessa lista, senza passare dalla fusione di SavePrefs(). */

#include "VagaFileManager.h"
#include <proto/asl.h>
#include <libraries/asl.h>
#include <images/label.h>
#include <libraries/keymap.h>

#define OPENWITH_FILE   "ENVARC:VagaFileManager.openwith"
#define OPENWITH_MAX    5
#define OW_ICON_BOX     32      /* riquadro delle icone nella finestra */

#define OWREQ_CHOOSE    100
#define OWREQ_CANCEL    101

/* ---- immagine che disegna una DiskObject ----
   Classe privata (sottoclasse di imageclass) usata come immagine dei
   pulsanti: disegna l'icona vera (PNG/GlowIcon) con DrawIconState,
   ridotta nel riquadro OW_ICON_BOX mantenendo le proporzioni. */

#define ICONIMG_DiskObject (TAG_USER + 0x4F570001)  /* 'OW' */

struct IconImgData
{
    struct DiskObject *dobj;
};

static uint32 IconImgDispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID)
    {
        case OM_NEW:
        {
            Object *obj = (Object *)IIntuition->IDoSuperMethodA(cl, o, msg);
            if (obj)
            {
                struct IconImgData *d = (struct IconImgData *)INST_DATA(cl, obj);
                d->dobj = (struct DiskObject *)IUtility->GetTagData(ICONIMG_DiskObject, 0,
                              ((struct opSet *)msg)->ops_AttrList);
                ((struct Image *)obj)->Width  = OW_ICON_BOX;
                ((struct Image *)obj)->Height = OW_ICON_BOX;
            }
            return (uint32)obj;
        }

        case IM_DRAW:
        case IM_DRAWFRAME:
        {
            struct impDraw *imp = (struct impDraw *)msg;
            struct Image *im = (struct Image *)o;
            struct IconImgData *d = (struct IconImgData *)INST_DATA(cl, o);
            struct Rectangle r;
            int32 w = OW_ICON_BOX, h = OW_ICON_BOX;
            BOOL scale = FALSE;

            if (!d->dobj || !imp->imp_RPort) return 0;
            if (IIcon->GetIconRectangle(imp->imp_RPort, d->dobj, NULL, &r,
                                        ICONDRAWA_Borderless, TRUE, TAG_DONE))
            {
                w = r.MaxX - r.MinX + 1;
                h = r.MaxY - r.MinY + 1;
                if (w > OW_ICON_BOX || h > OW_ICON_BOX)
                {
                    if (w >= h) { h = h * OW_ICON_BOX / w; w = OW_ICON_BOX; }
                    else        { w = w * OW_ICON_BOX / h; h = OW_ICON_BOX; }
                    if (w < 1) w = 1;
                    if (h < 1) h = 1;
                    scale = TRUE;
                }
            }
            IIcon->DrawIconState(imp->imp_RPort, d->dobj, NULL,
                imp->imp_Offset.X + im->LeftEdge + (OW_ICON_BOX - w) / 2,
                imp->imp_Offset.Y + im->TopEdge  + (OW_ICON_BOX - h) / 2,
                (imp->imp_State == IDS_SELECTED) ? IDS_SELECTED : IDS_NORMAL,
                ICONDRAWA_Borderless, TRUE,
                ICONDRAWA_EraseBackground, FALSE,
                scale ? ICONDRAWA_Width  : TAG_IGNORE, w,
                scale ? ICONDRAWA_Height : TAG_IGNORE, h,
                TAG_DONE);
            return 0;
        }

        default:
            break;
    }
    return IIntuition->IDoSuperMethodA(cl, o, msg);
}

/* ---- lista dei programmi recenti ---- */

static int LoadRecent(char rec[][PATH_MAX_LEN])
{
    BPTR fh;
    char line[PATH_MAX_LEN];
    int n = 0;

    fh = IDOS->Open(OPENWITH_FILE, MODE_OLDFILE);
    if (!fh) return 0;
    while (n < OPENWITH_MAX && IDOS->FGets(fh, line, sizeof(line)))
    {
        LONG len = (LONG)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;
        strncpy(rec[n], line, PATH_MAX_LEN - 1);
        rec[n][PATH_MAX_LEN - 1] = '\0';
        n++;
    }
    IDOS->Close(fh);
    return n;
}

static void SaveRecent(char rec[][PATH_MAX_LEN], int n)
{
    BPTR fh = IDOS->Open(OPENWITH_FILE, MODE_NEWFILE);
    int i;

    if (!fh) return;
    for (i = 0; i < n; i++)
        IDOS->FPrintf(fh, "%s\n", rec[i]);
    IDOS->Close(fh);
}

static BOOL PathExists(CONST_STRPTR path)
{
    BPTR l = IDOS->Lock(path, SHARED_LOCK);
    if (!l) return FALSE;
    IDOS->UnLock(l);
    return TRUE;
}

/* Toglie i programmi che non esistono piu'.  TRUE se la lista e' cambiata. */
static BOOL PruneRecent(char rec[][PATH_MAX_LEN], int *n)
{
    int i, out = 0;
    BOOL changed = FALSE;

    for (i = 0; i < *n; i++)
    {
        if (!PathExists(rec[i])) { changed = TRUE; continue; }
        if (out != i) memcpy(rec[out], rec[i], PATH_MAX_LEN);
        out++;
    }
    *n = out;
    return changed;
}

/* Porta 'prog' in cima alla lista (aggiungendolo se manca). */
static void PushRecent(char rec[][PATH_MAX_LEN], int *n, CONST_STRPTR prog)
{
    char tmp[PATH_MAX_LEN];
    int i, pos = -1;

    strncpy(tmp, prog, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (i = 0; i < *n; i++)
        if (!IUtility->Stricmp(rec[i], tmp)) { pos = i; break; }
    if (pos < 0)
        pos = (*n < OPENWITH_MAX) ? (*n)++ : OPENWITH_MAX - 1;
    for (i = pos; i > 0; i--)
        memcpy(rec[i], rec[i - 1], PATH_MAX_LEN);
    memcpy(rec[0], tmp, PATH_MAX_LEN);
}

/* ---- scelta del programma con il file requester ---- */

static BOOL ChooseProgram(struct Window *win, CONST_STRPTR initial, STRPTR out, ULONG outSize)
{
    struct FileRequester *fr;
    char drawer[PATH_MAX_LEN];
    BOOL ok = FALSE;

    strncpy(drawer, "SYS:Utilities", sizeof(drawer) - 1);
    drawer[sizeof(drawer) - 1] = '\0';
    if (initial && initial[0])
    {
        STRPTR part;
        strncpy(drawer, initial, sizeof(drawer) - 1);
        drawer[sizeof(drawer) - 1] = '\0';
        part = IDOS->PathPart(drawer);
        if (part) *part = '\0';
    }

    fr = (struct FileRequester *)IAsl->AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)LS(MSG_OPENWITH_ASL_TITLE),
        win ? ASLFR_Window : TAG_IGNORE, (ULONG)win,
        win ? ASLFR_SleepWindow : TAG_IGNORE, TRUE,
        ASLFR_InitialDrawer, (ULONG)drawer,
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);
    if (!fr) return FALSE;

    if (IAsl->AslRequestTags(fr, TAG_DONE) && fr->fr_File && fr->fr_File[0])
    {
        strncpy(out, fr->fr_Drawer ? fr->fr_Drawer : "", outSize - 1);
        out[outSize - 1] = '\0';
        ok = IDOS->AddPart(out, fr->fr_File, outSize) ? TRUE : FALSE;
    }
    IAsl->FreeAslRequest(fr);
    return ok;
}

/* ---- finestra "Aprire con:" ----
   Ritorna l'indice del programma scelto (0..n-1), n per [Scegli...],
   -1 per annulla. */

static Object *OwText(CONST_STRPTR text)
{
    return IIntuition->NewObject(NULL, "button.gadget",
        GA_ReadOnly, TRUE, GA_Text, (ULONG)text,
        GA_Underscore, 0, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE, BUTTON_Justification, BCJ_LEFT,
        TAG_DONE);
}

static LONG AskOpenWith(struct Screen *scr, CONST_STRPTR title,
                        char rec[][PATH_MAX_LEN], int n)
{
    Class *imgClass;
    struct DiskObject *dobj[OPENWITH_MAX];
    Object *img[OPENWITH_MAX];
    Object *list, *winObj;
    struct Window *rw;
    ULONG sig = 0;
    LONG result = -1;
    BOOL done = FALSE;
    int i;

    memset(dobj, 0, sizeof(dobj));
    memset(img, 0, sizeof(img));

    imgClass = IIntuition->MakeClass(NULL, "imageclass", NULL, sizeof(struct IconImgData), 0);
    if (imgClass) imgClass->cl_Dispatcher.h_Entry = (HOOKFUNC)IconImgDispatch;

    list = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceInner, TRUE,
        TAG_DONE);

    for (i = 0; i < n && list; i++)
    {
        Object *row, *iconBtn = NULL, *nameBtn;
        CONST_STRPTR name = IDOS->FilePart(rec[i]);

        if (!name || !name[0]) name = rec[i];

        /* senza .info: l'icona predefinita per il tipo (programma) */
        dobj[i] = IIcon->GetIconTags(rec[i], ICONGETA_FailIfUnavailable, FALSE, TAG_DONE);
        if (imgClass && dobj[i])
            img[i] = IIntuition->NewObject(imgClass, NULL, ICONIMG_DiskObject, (ULONG)dobj[i], TAG_DONE);
        if (img[i])
            iconBtn = IIntuition->NewObject(NULL, "button.gadget",
                GA_ID, i + 1, GA_RelVerify, TRUE,
                BUTTON_RenderImage, (ULONG)img[i],
                BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
                TAG_DONE);
        nameBtn = IIntuition->NewObject(NULL, "button.gadget",
            GA_ID, i + 1, GA_RelVerify, TRUE,
            GA_Text, (ULONG)name, GA_Underscore, 0,
            BUTTON_Justification, BCJ_LEFT,
            TAG_DONE);

        row = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_VertAlignment, LALIGN_CENTER,
            iconBtn ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)iconBtn,
            iconBtn ? CHILD_WeightedWidth : TAG_IGNORE, 0,
            LAYOUT_AddChild, (ULONG)nameBtn,
            CHILD_WeightedHeight, 0,
            TAG_DONE);
        if (row)
            IIntuition->SetAttrs(list, LAYOUT_AddChild, (ULONG)row, CHILD_WeightedHeight, 0, TAG_DONE);
    }

    winObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)title,
        WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, FALSE,
        WA_InnerWidth, 320,
        WA_IDCMP, IDCMP_RAWKEY,
        scr ? WA_PubScreen : TAG_IGNORE, (ULONG)scr,
        WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, (ULONG)OwText(LS(MSG_OPENWITH_PROMPT)), CHILD_WeightedHeight, 0,
            list ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)list,
            list ? CHILD_WeightedHeight : TAG_IGNORE, 0,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, OWREQ_CHOOSE, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)LS(MSG_OPENWITH_CHOOSE), GA_Underscore, 0, TAG_DONE),
                    CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, OWREQ_CANCEL, GA_RelVerify, TRUE,
                    GA_Text, (ULONG)LS(MSG_BTN_CANCEL), GA_Underscore, 0, TAG_DONE),
                    CHILD_WeightedWidth, 0,
                TAG_DONE), CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);

    if (winObj && (rw = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN)) != NULL)
    {
        IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
        while (!done)
        {
            ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
            if (wait & SIGBREAKF_CTRL_C) { result = -1; done = TRUE; }
            if (wait & sig)
            {
                ULONG res;
                WORD code = 0;
                while ((res = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                {
                    switch (res & WMHI_CLASSMASK)
                    {
                        case WMHI_CLOSEWINDOW:
                            result = -1; done = TRUE;
                            break;
                        case WMHI_RAWKEY:
                            if ((res & WMHI_KEYMASK) == RAWKEY_ESC) { result = -1; done = TRUE; }
                            break;
                        case WMHI_GADGETUP:
                        {
                            ULONG id = res & WMHI_GADGETMASK;
                            if (id >= 1 && id <= (ULONG)n) result = (LONG)id - 1;
                            else if (id == OWREQ_CHOOSE) result = n;
                            else result = -1;
                            done = TRUE;
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }

    /* i pulsanti non liberano le immagini: prima la finestra, poi il resto */
    if (winObj) IIntuition->DisposeObject(winObj);
    else if (list) IIntuition->DisposeObject(list);
    for (i = 0; i < OPENWITH_MAX; i++)
    {
        if (img[i]) IIntuition->DisposeObject(img[i]);
        if (dobj[i]) IIcon->FreeDiskObject(dobj[i]);
    }
    if (imgClass) IIntuition->FreeClass(imgClass);
    return result;
}

/* ---- lancio ---- */

/* Lancia 'prog' come dal Workbench, con i file 'names' (nel cassetto
   'dir') come argomenti. */
static BOOL LaunchWith(CONST_STRPTR prog, CONST_STRPTR dir, char (*names)[NAME_MAX_LEN], LONG n)
{
    struct TagItem *tags;
    BPTR lock;
    BOOL ok = FALSE;
    LONG i;

    if (!WorkbenchBase || !IWorkbench) return FALSE;
    lock = IDOS->Lock(dir, SHARED_LOCK);
    if (!lock) return FALSE;

    tags = (struct TagItem *)IExec->AllocVecTags((2 * n + 1) * sizeof(struct TagItem),
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (tags)
    {
        for (i = 0; i < n; i++)
        {
            tags[2 * i].ti_Tag      = WBOPENA_ArgLock;
            tags[2 * i].ti_Data     = (ULONG)lock;
            tags[2 * i + 1].ti_Tag  = WBOPENA_ArgName;
            tags[2 * i + 1].ti_Data = (ULONG)names[i];
        }
        tags[2 * n].ti_Tag = TAG_DONE;
        DBG("open with '%s': %ld file(s) in '%s'", prog, (long)n, dir);
        ok = IWorkbench->OpenWorkbenchObjectA((STRPTR)prog, tags) ? TRUE : FALSE;
        IExec->FreeVec(tags);
    }
    IDOS->UnLock(lock);
    return ok;
}

/* Voce "Apri con..." (menu Modifica e menu contestuale): agisce sui soli
   file selezionati, i cassetti vengono ignorati. */
void DoOpenWith(struct Window *win, struct List *list)
{
    char (*names)[NAME_MAX_LEN] = NULL;
    char (*rec)[PATH_MAX_LEN] = NULL;
    char prog[PATH_MAX_LEN];
    char title[NAME_MAX_LEN + 8];
    struct Node *nd;
    LONG count = 0, k = 0;
    int nrec = 0;
    BOOL choose = TRUE, ok;

    if (!list || gPathBuffer[0] == '\0') return;

    for (nd = list->lh_Head; nd->ln_Succ; nd = nd->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        IListBrowser->GetListBrowserNodeAttrs(nd, LBNA_Selected, &sel, LBNA_UserData, &index, TAG_DONE);
        if (sel && index < (ULONG)gEntryCount && !gIsDir[index]) count++;
    }
    if (count == 0) return;

    names = (char (*)[NAME_MAX_LEN])IExec->AllocVecTags(count * NAME_MAX_LEN,
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    rec = (char (*)[PATH_MAX_LEN])IExec->AllocVecTags(OPENWITH_MAX * PATH_MAX_LEN,
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (!names || !rec) goto out;

    for (nd = list->lh_Head; nd->ln_Succ && k < count; nd = nd->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;
        IListBrowser->GetListBrowserNodeAttrs(nd, LBNA_Selected, &sel, LBNA_UserData, &index,
                                              LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!sel || !name || index >= (ULONG)gEntryCount || gIsDir[index]) continue;
        strncpy(names[k], name, NAME_MAX_LEN - 1);
        k++;
    }
    count = k;
    if (count == 0) goto out;

    /* titolo: il file (o il primo, seguito da ", ...") */
    snprintf(title, sizeof(title), "%s%s", names[0], count > 1 ? ", ..." : "");

    nrec = LoadRecent(rec);
    if (PruneRecent(rec, &nrec)) SaveRecent(rec, nrec);

    if (nrec > 0)
    {
        LONG r = AskOpenWith(win ? win->WScreen : NULL, title, rec, nrec);
        if (r < 0) goto out;
        if (r < nrec)
        {
            strncpy(prog, rec[r], sizeof(prog) - 1);
            prog[sizeof(prog) - 1] = '\0';
            choose = FALSE;
        }
    }
    if (choose && !ChooseProgram(win, nrec > 0 ? rec[0] : NULL, prog, sizeof(prog)))
        goto out;

    ok = LaunchWith(prog, gPathBuffer, names, count);
    if (ok)
    {
        /* rilegge: un'altra istanza puo' averla cambiata nel frattempo */
        nrec = LoadRecent(rec);
        PushRecent(rec, &nrec, prog);
        SaveRecent(rec, nrec);
    }
    else
    {
        char msg[PATH_MAX_LEN + 64];
        snprintf(msg, sizeof(msg), LS(MSG_OPENWITH_FAILED), IDOS->FilePart(prog));
        ShowMessage(LS(MSG_ERROR_TITLE), msg);
    }

out:
    if (names) IExec->FreeVec(names);
    if (rec) IExec->FreeVec(rec);
}
