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
 * Modulo: Dialogs.c
 * -----------------
 * Interfaccia utente: gadget della progress bar, finestra di avanzamento,
 * dialoghi (messaggi, conferme, rinomina, conflitti), About, segnalibri,
 * menu, cambio vista e helper per immagini/pulsanti.
 */

#include "VagaFileManager.h"

static BOOL IconFileExists(CONST_STRPTR path);
static BOOL TbimagesAvailable(void);

/* ===== Gadget progress bar personalizzato ===== */

#define PB_Dummy        (TAG_USER + 0x50420000)
#define PB_Level        (PB_Dummy + 1)
#define PB_Max          (PB_Dummy + 2)
#define PB_Indeterminate (PB_Dummy + 3)

typedef struct { ULONG level, max; BOOL indeterminate; } PBData;

static Class *gProgressBarClass = NULL;

static void PB_Render(PBData *d, Object *o, struct GadgetInfo *gi, struct RastPort *rp)
{
    struct Gadget *g = (struct Gadget *)o;
    struct DrawInfo *dri = gi ? gi->gi_DrInfo : NULL;
    int32 x0, y0, x1, y1, fill;

    if (!dri || !g || g->Width < 4 || g->Height < 4) return;

    x0 = g->LeftEdge; y0 = g->TopEdge;
    x1 = g->LeftEdge + g->Width - 1; y1 = g->TopEdge + g->Height - 1;

    /* cornice */
    IGraphics->SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
    IGraphics->RectFill(rp, x0, y0, x1, y1);
    IGraphics->SetAPen(rp, dri->dri_Pens[SHINEPEN]);
    IGraphics->RectFill(rp, x0 + 1, y0 + 1, x1 - 1, y1 - 1);
    IGraphics->SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
    IGraphics->RectFill(rp, x0 + 2, y0 + 2, x1 - 2, y1 - 2);

    /* riempimento / animazione */
    if (d->indeterminate)
    {
        int32 span = x1 - x0 - 3;
        int32 block = span / 4;
        int32 pos, sx, ex;

        if (block < 8) block = 8;
        pos = (int32)(d->level % (ULONG)(span + block));
        sx = x0 + 2 + pos - block;
        ex = sx + block - 1;
        if (sx < x0 + 2) sx = x0 + 2;
        if (ex > x1 - 2) ex = x1 - 2;
        if (ex >= sx)
        {
            IGraphics->SetAPen(rp, dri->dri_Pens[FILLPEN]);
            IGraphics->RectFill(rp, sx, y0 + 2, ex, y1 - 2);
        }
    }
    else
    {
        fill = (d->max > 0) ? (int32)((int64)d->level * (x1 - x0 - 3) / d->max) : 0;
        if (fill > 0)
        {
            IGraphics->SetAPen(rp, dri->dri_Pens[FILLPEN]);
            IGraphics->RectFill(rp, x0 + 2, y0 + 2, x0 + 2 + fill - 1, y1 - 2);
        }
    }
}

static uint32 PB_Dispatch(Class *cl, Object *o, Msg msg)
{
    PBData *d;
    uint32 result;

    switch (msg->MethodID)
    {
    case OM_NEW:
    {
        struct opSet *ops = (struct opSet *)msg;
        struct TagItem *tl = ops->ops_AttrList, *tag;

        result = IIntuition->IDoSuperMethodA(cl, o, msg);
        if (result)
        {
            d = (PBData *)INST_DATA(cl, (Object *)result);
            IUtility->ClearMem(d, sizeof(*d));
            d->max = 100;
            while ((tag = IUtility->NextTagItem(&tl)))
            {
                if (tag->ti_Tag == PB_Level) d->level = (ULONG)tag->ti_Data;
                else if (tag->ti_Tag == PB_Max) d->max = (ULONG)tag->ti_Data;
                else if (tag->ti_Tag == PB_Indeterminate) d->indeterminate = tag->ti_Data ? TRUE : FALSE;
            }
        }
        return result;
    }

    case OM_SET:
    case OM_UPDATE:
    {
        struct opSet *ops = (struct opSet *)msg;
        struct TagItem *tl = ops->ops_AttrList, *tag;

        result = IIntuition->IDoSuperMethodA(cl, o, msg);
        d = (PBData *)INST_DATA(cl, o);
        while ((tag = IUtility->NextTagItem(&tl)))
        {
            if (tag->ti_Tag == PB_Level) d->level = (ULONG)tag->ti_Data;
            else if (tag->ti_Tag == PB_Max) d->max = (ULONG)tag->ti_Data;
            else if (tag->ti_Tag == PB_Indeterminate) d->indeterminate = tag->ti_Data ? TRUE : FALSE;
        }
        if (ops->ops_GInfo)
        {
            struct RastPort *rp;
            if ((rp = IIntuition->ObtainGIRPort(ops->ops_GInfo)))
            {
                PB_Render(d, o, ops->ops_GInfo, rp);
                IIntuition->ReleaseGIRPort(rp);
            }
        }
        return result;
    }

    case GM_RENDER:
    {
        struct gpRender *gpr = (struct gpRender *)msg;
        d = (PBData *)INST_DATA(cl, o);
        PB_Render(d, o, gpr->gpr_GInfo, gpr->gpr_RPort);
        return TRUE;
    }

    case GM_HITTEST:
        return GMR_GADGETHIT;

    default:
        break;
    }
    return IIntuition->IDoSuperMethodA(cl, o, msg);
}

static Class *MakeProgressBarClass(void)
{
    if (!gProgressBarClass)
    {
        gProgressBarClass = IIntuition->MakeClass(NULL, "gadgetclass", NULL,
                                                  sizeof(PBData), 0);
        if (gProgressBarClass)
            gProgressBarClass->cl_Dispatcher.h_Entry = (HOOKFUNC)PB_Dispatch;
    }
    return gProgressBarClass;
}

void FreeProgressBarClass(void)
{
    if (gProgressBarClass)
    {
        IIntuition->FreeClass(gProgressBarClass);
        gProgressBarClass = NULL;
    }
}

/* ===== Dialoghi semplici ===== */

/* Tutti i requester si aprono centrati sotto il puntatore del mouse. */
static struct TagItem gReqAtMouse[] =
{
    { ESA_Position, REQPOS_CENTERMOUSE },
    { TAG_DONE, 0 }
};

void InitEasyAtMouse(struct EasyStruct *es)
{
    memset(es, 0, sizeof(*es));
    es->es_StructSize = sizeof(*es);
    es->es_Flags = ESF_TAGGED;
    es->es_TagList = gReqAtMouse;
}

void ShowMessage(CONST_STRPTR title, CONST_STRPTR body)
{
    struct EasyStruct es;
    APTR args[1];

    if (!body) body = "";
    if (!title) title = LS(MSG_APP_TITLE);
    InitEasyAtMouse(&es);
    es.es_Title = (STRPTR)title;
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = (STRPTR)LS(MSG_BTN_OK);
    args[0] = (APTR)body;
    IIntuition->EasyRequestArgs(NULL, &es, NULL, args);
}

BOOL ConfirmRequest(CONST_STRPTR title, CONST_STRPTR body)
{
    struct EasyStruct es;
    APTR args[1];

    if (!body) body = "";
    if (!title) title = LS(MSG_APP_TITLE);
    InitEasyAtMouse(&es);
    es.es_Title = (STRPTR)title;
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = (STRPTR)LS(MSG_BTN_OK_CANCEL);
    args[0] = (APTR)body;
    return (IIntuition->EasyRequestArgs(NULL, &es, NULL, args) == 1);
}

/* Chiede cosa fare quando la destinazione di una copia/spostamento esiste.
   Ritorna 1=Sovrascrivi, 2=Sovrascrivi tutti, 3=Aggiorna, 4=Aggiorna tutti,
   5=Rinomina, 6=Salta; 0 se la finestra viene chiusa (annulla). */
LONG AskConflict(CONST_STRPTR name)
{
    struct EasyStruct es;
    APTR args[1];
    char body[400];
    char buttons[256];

    snprintf(body, sizeof(body), LS(MSG_CONFLICT_BODY), name ? name : "");
    snprintf(buttons, sizeof(buttons), "%s|%s|%s|%s|%s|%s",
        LS(MSG_CONFLICT_OVERWRITE), LS(MSG_CONFLICT_OVERWRITE_ALL),
        LS(MSG_CONFLICT_UPDATE), LS(MSG_CONFLICT_UPDATE_ALL),
        LS(MSG_CONFLICT_RENAME), LS(MSG_CONFLICT_SKIP));
    InitEasyAtMouse(&es);
    es.es_Title = (STRPTR)LS(MSG_CONFLICT_TITLE);
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = buttons;
    args[0] = (APTR)body;
    return IIntuition->EasyRequestArgs(NULL, &es, NULL, args);
}

/* Errore durante una copia, come il Workbench: nome dell'oggetto e testo
   dell'errore DOS (Fault(), nella lingua del sistema, per esempio
   "oggetto in uso").  Ritorna 1=Riprova, 2=Salta, 0=Annulla. */
LONG AskCopyError(CONST_STRPTR name, LONG err)
{
    struct EasyStruct es;
    APTR args[1];
    char fault[160];
    char body[PATH_MAX_LEN + 200];

    fault[0] = '\0';
    IDOS->Fault(err, NULL, fault, sizeof(fault));
    snprintf(body, sizeof(body), LS(MSG_COPY_ERROR_BODY),
             (name && name[0]) ? name : "", fault);
    InitEasyAtMouse(&es);
    es.es_Title = (STRPTR)LS(MSG_ERROR_TITLE);
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = (STRPTR)LS(MSG_COPY_ERROR_BUTTONS);
    args[0] = (APTR)body;
    return IIntuition->EasyRequestArgs(NULL, &es, NULL, args);
}

/* Richiesta di una riga di testo: 'buf' contiene il testo iniziale e, se
   l'utente conferma, il testo inserito.  'width' e' la larghezza interna. */
static BOOL PromptStringWidth(CONST_STRPTR title, STRPTR buf, ULONG bufSize, ULONG width)
{
    Object *winObj = NULL, *strGad = NULL;
    struct Window *win = NULL;
    BOOL ok = FALSE, done = FALSE;
    ULONG sig = 0;

    strGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_RENAME_STR, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, bufSize - 1, STRINGA_TextVal, (ULONG)buf, STRINGA_Buffer, (ULONG)buf, TAG_DONE);

    winObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)title, WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, FALSE, WA_InnerWidth, width, WA_InnerHeight, 60, WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, (ULONG)strGad, CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_RENAME_OK, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_BTN_OK), TAG_DONE),
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_RENAME_CANCEL, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_BTN_CANCEL), TAG_DONE),
            TAG_DONE), CHILD_WeightedHeight, 0,
        TAG_DONE), TAG_DONE);

    if (winObj)
    {
        win = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
        if (win)
        {
            IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
            while (!done)
            {
                ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
                if (wait & SIGBREAKF_CTRL_C) { done = TRUE; ok = FALSE; }
                if (wait & sig)
                {
                    ULONG res; WORD code = 0;
                    while ((res = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                    {
                        switch (res & WMHI_CLASSMASK)
                        {
                            case WMHI_CLOSEWINDOW: done = TRUE; ok = FALSE; win = NULL; break;
                            case WMHI_GADGETUP:
                                switch (res & WMHI_GADGETMASK)
                                {
                                    case GID_RENAME_OK: case GID_RENAME_STR:
                                    { STRPTR t = NULL; IIntuition->GetAttr(STRINGA_TextVal, strGad, (ULONG *)&t);
                                      if (t) { strncpy(buf, t, bufSize - 1); buf[bufSize - 1] = '\0'; }
                                      ok = TRUE; done = TRUE; break; }
                                    case GID_RENAME_CANCEL: ok = FALSE; done = TRUE; break;
                                    default: break;
                                }
                                break;
                            default: break;
                        }
                    }
                }
            }
        }
        IIntuition->DisposeObject(winObj);
    }
    return ok;
}

BOOL PromptRename(CONST_STRPTR oldName, STRPTR newNameBuf, ULONG bufSize)
{
    char title[300];

    if (!oldName || !newNameBuf || bufSize == 0) return FALSE;
    snprintf(title, sizeof(title), LS(MSG_RENAME_TITLE), oldName);
    strncpy(newNameBuf, oldName, bufSize - 1); newNameBuf[bufSize - 1] = '\0';
    return PromptStringWidth(title, newNameBuf, bufSize, 360);
}

BOOL PromptString(CONST_STRPTR title, STRPTR buf, ULONG bufSize)
{
    if (!buf || bufSize == 0) return FALSE;
    buf[0] = '\0';
    return PromptStringWidth(title, buf, bufSize, 420);
}

/* Dialogo Reaction generico per gli addon (comando ARexx DIALOG).
   Campi: titolo, prompt, testo iniziale, pulsanti separati da '|'
   (massimo 6).  Ritorna l'indice del pulsante (1..n) e lascia in buf
   il testo inserito; 0 se la finestra viene chiusa. */
#define GID_DLG_STR 2000
#define GID_DLG_BTN 2001

LONG PromptDialog(CONST_STRPTR title, CONST_STRPTR prompt,
                  STRPTR buf, ULONG bufSize, CONST_STRPTR buttons)
{
    Object *winObj = NULL, *strGad = NULL, *labelGad = NULL, *btnRow = NULL;
    Object *btn[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
    struct TagItem rowTags[32];
    struct Window *win = NULL;
    BOOL done = FALSE;
    LONG result = 0, nButtons = 0, i;
    ULONG sig = 0;
    char labels[6][64];
    CONST_STRPTR p = buttons;

    if (!buf || bufSize == 0) return 0;
    /* Keep the caller's default text: the string gadget uses buf directly.
       If the window is closed, the caller still gets the current text. */
    buf[bufSize - 1] = '\0';

    /* Dividi i pulsanti; i campi vuoti valgono "OK". */
    while (p && *p && nButtons < 6)
    {
        CONST_STRPTR sep = strchr(p, '|');
        ULONG n = sep ? (ULONG)(sep - p) : (ULONG)strlen(p);
        if (n >= sizeof(labels[0])) n = sizeof(labels[0]) - 1;
        memcpy(labels[nButtons], p, n);
        labels[nButtons][n] = '\0';
        if (!labels[nButtons][0])
            strncpy(labels[nButtons], "OK", sizeof(labels[0]) - 1);
        labels[nButtons][sizeof(labels[0]) - 1] = '\0';
        nButtons++;
        p = sep ? sep + 1 : NULL;
    }
    if (nButtons == 0)
    {
        strncpy(labels[0], "OK", sizeof(labels[0]) - 1);
        labels[0][sizeof(labels[0]) - 1] = '\0';
        nButtons = 1;
    }

    strGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_DLG_STR, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, bufSize - 1,
        STRINGA_TextVal, (ULONG)buf, STRINGA_Buffer, (ULONG)buf, TAG_DONE);

    labelGad = IIntuition->NewObject(NULL, "button.gadget",
        GA_ReadOnly, TRUE,
        GA_Text, (ULONG)((prompt && prompt[0]) ? prompt : ""),
        BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
        TAG_DONE);

    for (i = 0; i < nButtons; i++)
    {
        btn[i] = IIntuition->NewObject(NULL, "button.gadget",
            GA_ID, (ULONG)(GID_DLG_BTN + i), GA_RelVerify, TRUE,
            GA_Text, (ULONG)labels[i], TAG_DONE);
    }

    /* Riga dei pulsanti costruita dinamicamente: solo quelli presenti. */
    {
        int n = 0;
        rowTags[n].ti_Tag = LAYOUT_Orientation; rowTags[n++].ti_Data = LAYOUT_ORIENT_HORIZ;
        rowTags[n].ti_Tag = LAYOUT_EvenSize;    rowTags[n++].ti_Data = TRUE;
        for (i = 0; i < nButtons; i++)
        {
            rowTags[n].ti_Tag = LAYOUT_AddChild;     rowTags[n++].ti_Data = (ULONG)btn[i];
            rowTags[n].ti_Tag = CHILD_WeightedWidth; rowTags[n++].ti_Data = 0;
        }
        rowTags[n].ti_Tag = TAG_DONE; rowTags[n++].ti_Data = 0;
        btnRow = IIntuition->NewObjectA(NULL, "layout.gadget", rowTags);
    }

    winObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)((title && title[0]) ? title : LS(MSG_APP_TITLE)),
        WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, FALSE,
        WA_InnerWidth, 560, WA_InnerHeight, 140,
        WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, (ULONG)labelGad, CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)strGad, CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)btnRow, CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);

    DBG("PromptDialog: winObj=%p win=%p buttons=%ld",
        (void *)winObj, (void *)win, (long)nButtons);
    if (winObj)
    {
        win = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
        DBG("PromptDialog: opened win=%p", (void *)win);
        if (win)
        {
            IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
            while (!done)
            {
                ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
                if (wait & SIGBREAKF_CTRL_C) { done = TRUE; result = 0; }
                if (wait & sig)
                {
                    ULONG res; WORD code = 0;
                    while ((res = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                    {
                        switch (res & WMHI_CLASSMASK)
                        {
                            case WMHI_CLOSEWINDOW:
                                done = TRUE; result = 0; win = NULL; break;
                            case WMHI_GADGETUP:
                            {
                                ULONG id = res & WMHI_GADGETMASK;
                                STRPTR t = NULL;
                                IIntuition->GetAttr(STRINGA_TextVal, strGad, (ULONG *)&t);
                                if (t)
                                {
                                    strncpy(buf, t, bufSize - 1);
                                    buf[bufSize - 1] = '\0';
                                }
                                if (id == GID_DLG_STR)
                                    result = 1;  /* Invio = primo pulsante */
                                else if (id >= GID_DLG_BTN && id < GID_DLG_BTN + (ULONG)nButtons)
                                    result = (LONG)(id - GID_DLG_BTN) + 1;
                                else
                                    result = 0;
                                done = TRUE;
                                break;
                            }
                            default: break;
                        }
                    }
                }
            }
        }
        IIntuition->DisposeObject(winObj);
    }
    return result;
}

static Object *gProgTextGad = NULL;
static Object *gProgBar = NULL;
static Object *gProgInfoGad = NULL;
static ULONG gProgTotal = 0;
static BOOL gProgIndeterminate = FALSE;
static ULONG gProgPhase = 0;
static ULONG gProgAdvanceCount = 0;
static BOOL gProgCancelable = TRUE;
static ULONG gProgIndetTotal = 0;

/* Abilita/disabilita Cancel e Pause.  Usato dai comandi PROGRESS degli
   addon: per operazioni esterne (LHA) i pulsanti non devono fermare il
   processo figlio. */
void ProgressSetCancelable(BOOL on)
{
    gProgCancelable = on;
}

/* ===== Finestra di avanzamento ===== */

static void FormatBytes(uint64 n, STRPTR buf, ULONG size)
{
    if (n >= (uint64)1024 * 1024 * 1024)
        snprintf(buf, size, "%lu GB", (unsigned long)(n / ((uint64)1024 * 1024 * 1024)));
    else if (n >= (uint64)1024 * 1024)
        snprintf(buf, size, "%lu MB", (unsigned long)(n / ((uint64)1024 * 1024)));
    else if (n >= 1024)
        snprintf(buf, size, "%lu KB", (unsigned long)(n / 1024));
    else
        snprintf(buf, size, "%lu B", (unsigned long)n);
}

/* Compone la riga informativa mostrata sotto la progress bar. */
static void BuildProgInfo(STRPTR buf, ULONG size)
{
    ULONG pct;

    if (gProgIndeterminate)
    {
        static const char *dots[4] = { "", ".", "..", "..." };
        char a[32];
        FormatBytes(gProgBytesDone, a, sizeof(a));
        if (gProgIndetTotal > 0)
            snprintf(buf, size, "%lu file   %s %s",
                     (unsigned long)gProgIndetTotal,
                     a, dots[gProgPhase & 3]);
        else
            snprintf(buf, size, "%lu file   %s %s",
                     (unsigned long)gProgDone, a, dots[gProgPhase & 3]);
        return;
    }

    pct = gProgTotal ? (ULONG)(((uint64)gProgDone * 100) / gProgTotal) : 0;

    if (gProgBytesTotal)
    {
        char a[32], b[32];
        FormatBytes(gProgBytesDone, a, sizeof(a));
        FormatBytes(gProgBytesTotal, b, sizeof(b));
        snprintf(buf, size, "%lu / %lu file   %s / %s   %lu%%",
                 (unsigned long)gProgDone, (unsigned long)gProgTotal,
                 a, b, (unsigned long)pct);
    }
    else
    {
        snprintf(buf, size, "%lu / %lu file   %lu%%",
                 (unsigned long)gProgDone, (unsigned long)gProgTotal,
                 (unsigned long)pct);
    }
}

void ProgressSetBytes(uint64 total)
{
    gProgBytesTotal = total;
    gProgBytesDone = 0;
    ProgressUpdate();
}

/* Imposta il conteggio mostrato dalla progress bar. */
void ProgressSetCounts(ULONG done, ULONG total)
{
    gProgDone  = done;
    gProgTotal = (total > 0) ? total : 1;
}

/* Ridisegna la progress bar e la riga informativa (percentuale, byte,
   elementi). */
void ProgressUpdate(void)
{
    static char infoText[160];

    if (!gProgWin) return;
    if (gProgBar)
    {
        ULONG level = gProgIndeterminate ? gProgPhase : gProgDone;
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)gProgBar, gProgWin, NULL,
            PB_Level, level, TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)gProgBar, gProgWin, NULL, 1);
    }
    BuildProgInfo(infoText, sizeof(infoText));
    if (gProgInfoGad)
    {
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)gProgInfoGad, gProgWin, NULL,
            GA_Text, (ULONG)infoText, TAG_DONE);
    }
    else if (gProgTextGad)
    {
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)gProgTextGad, gProgWin, NULL,
            GA_Text, (ULONG)infoText, TAG_DONE);
    }
    if (gProgIndeterminate) gProgPhase++;
}

/* Gestisce gli eventi della finestra di avanzamento: chiusura/Annulla
   interrompono, Pausa/Continua commuta gProgPause. */
static void HandleProgressInput(void)
{
    ULONG res; WORD code = 0;
    while ((res = IIntuition->IDoMethod(gProgWinObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
    {
        switch (res & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                gProgCancel = TRUE;
                break;
            case WMHI_GADGETUP:
            {
                ULONG id = res & WMHI_GADGETMASK;
                if (id == GID_PROG_CANCEL) gProgCancel = TRUE;
                else if (id == GID_PROG_PAUSE)
                {
                    gProgPause = !gProgPause;
                    if (gProgPauseBtn)
                        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)gProgPauseBtn, gProgWin, NULL,
                            GA_Text, (ULONG)LS(gProgPause ? MSG_PROG_CONTINUE : MSG_PROG_PAUSE), TAG_DONE);
                }
                break;
            }
            default: break;
        }
    }
}

void ProgressPoll(void)
{
    ULONG sig = 0;
    if (!gProgWinObj || !gProgWin) return;
    IIntuition->GetAttr(WINDOW_SigMask, gProgWinObj, &sig);
    {
        ULONG pending = IExec->SetSignal(0, 0);
        if (pending & ~sig)
            IExec->SetSignal(pending & ~sig, ~0UL); /* non perdere gli altri segnali */
        if (pending & sig)
            HandleProgressInput();
    }
    while (gProgPause && !gProgCancel)
    {
        ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
        if (wait & SIGBREAKF_CTRL_C) { gProgCancel = TRUE; break; }
        if (wait & sig)
            HandleProgressInput();
    }
}

void ProgressAdvance(void)
{
    if (!gProgActive) return;
    if (gProgDone < gProgTotal) gProgDone++;
    gProgAdvanceCount++;

    /* Ridisegna ogni 8 elementi (e sempre sull'ultimo): copiando migliaia
       di file piccoli un refresh completo per file dominava il tempo.
       ProgressPoll resta a ogni chiamata, per Cancel/Pausa reattivi. */
    if ((gProgAdvanceCount & 7) == 0 || gProgDone >= gProgTotal)
        ProgressUpdate();
    ProgressPoll();
}

/* Attiva/disattiva la modalita' indeterminata: la barra mostra un blocco
   che scorre, senza percentuale.  Usata dal calcolo dimensioni, dove il
   totale non e' noto in anticipo. */
void ProgressSetIndeterminate(BOOL on)
{
    gProgIndeterminate = on;
    gProgPhase = 0;
    gProgIndetTotal = 0;
    if (gProgBar)
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)gProgBar, gProgWin, NULL,
            PB_Indeterminate, on, PB_Max, on ? 1 : gProgTotal, PB_Level, 0, TAG_DONE);
    ProgressUpdate();
}

/* Totale mostrato nella riga informativa della barra indeterminata. */
void ProgressSetIndeterminateTotal(ULONG total)
{
    gProgIndetTotal = total;
}

/* Apre la finestra di avanzamento con il numero totale di elementi. */
void ProgressOpen(struct Window *win, ULONG total)
{
    Object *progGad = NULL;

    (void)win;
    gProgBar = NULL;
    gProgTextGad = NULL;
    gProgInfoGad = NULL;
    gProgActive = TRUE; gProgCancel = FALSE; gProgPause = FALSE;
    gProgDone = 0;
    gProgTotal = (total > 0 ? total : 1);
    gProgBytesDone = 0;
    gProgBytesTotal = 0;
    gProgIndeterminate = FALSE;
    gProgPhase = 0;
    gProgAdvanceCount = 0;

    /* Barra di progresso vera (gadget custom); fallback di solo testo. */
    gProgBar = IIntuition->NewObject(MakeProgressBarClass(), NULL,
        PB_Max, gProgTotal, PB_Level, 0,
        TAG_DONE);
    if (!gProgBar)
    {
        gProgTextGad = IIntuition->NewObject(NULL, "button.gadget",
            GA_ReadOnly, TRUE, GA_Text, (ULONG)"0%",
            BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE);
    }
    progGad = gProgBar ? gProgBar : gProgTextGad;

    /* Riga di testo con numero file, dimensione e percentuale. */
    gProgInfoGad = IIntuition->NewObject(NULL, "button.gadget",
        GA_ReadOnly, TRUE, GA_Text, (ULONG)"",
        BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE);

    DBG("progress: open total=%lu bar=%p text=%p",
        (unsigned long)gProgTotal, (void *)gProgBar, (void *)gProgTextGad);

    gProgWinObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)LS(MSG_PROG_TITLE), WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, gProgCancelable ? TRUE : FALSE,
        WA_SizeGadget, FALSE, WA_Activate, TRUE,
        WA_InnerWidth, 460, WA_InnerHeight, 112,
        WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, (ULONG)progGad, CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,
                CHILD_MinHeight, 24, CHILD_MaxHeight, 24,
            LAYOUT_AddChild, (ULONG)gProgInfoGad, CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,
                CHILD_MinHeight, 18, CHILD_MaxHeight, 18,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_PROG_CANCEL, GA_RelVerify, TRUE, GA_Disabled, gProgCancelable ? FALSE : TRUE, GA_Text, (ULONG)LS(MSG_BTN_CANCEL), TAG_DONE),
                LAYOUT_AddChild, (ULONG)(gProgPauseBtn = IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_PROG_PAUSE, GA_RelVerify, TRUE, GA_Disabled, gProgCancelable ? FALSE : TRUE, GA_Text, (ULONG)LS(MSG_PROG_PAUSE), TAG_DONE)),
            TAG_DONE), CHILD_WeightedHeight, 0,
        TAG_DONE), TAG_DONE);

    if (gProgWinObj)
        gProgWin = (struct Window *)IIntuition->IDoMethod(gProgWinObj, WM_OPEN);

    DBG("progress: obj=%p win=%p", (void *)gProgWinObj, (void *)gProgWin);
    if (gProgWin)
    {
        IIntuition->WindowToFront(gProgWin);
        ProgressUpdate();
    }
    else
    {
        /* niente finestra: evita di restare in stato "progress" */
        gProgActive = FALSE;
        gProgCancelable = TRUE;
    }
}

void ProgressClose(void)
{
    if (gProgWinObj) { IIntuition->DisposeObject(gProgWinObj); gProgWinObj = NULL; }
    gProgWin = NULL; gProgPauseBtn = NULL;
    gProgBar = NULL; gProgTextGad = NULL; gProgInfoGad = NULL; gProgTotal = 0;
    gProgBytesDone = 0; gProgBytesTotal = 0;
    gProgIndeterminate = FALSE; gProgPhase = 0;
    gProgActive = FALSE; gProgCancel = FALSE; gProgPause = FALSE;
    gProgCancelable = TRUE;
    gProgIndetTotal = 0;
}

/* ===== Cambio vista ===== */

void DoToggleView(Object *listGad, Object *toggleGad, struct Window *win)
{
    gViewMode = (gViewMode == FBVM_DETAILS) ? FBVM_ICONS : FBVM_DETAILS;
    /* La vista lista mostra tutti i file.  Quella a icone mostra solo i file
       con icona, ma se nella cartella non c'e' nessun .info mostra tutti. */
    if (gViewMode == FBVM_ICONS)
        gViewFilter = DirHasInfoFiles(gPathBuffer) ? VIEW_FILTER_ICONS_ONLY
                                                   : VIEW_FILTER_ALL_FILES;
    else
        gViewFilter = VIEW_FILTER_ALL_FILES;
    IIntuition->SetGadgetAttrs((struct Gadget*)listGad, win, NULL, FILEBROWSER_ViewMode, gViewMode, TAG_DONE);
    if (gViewMode == FBVM_ICONS)
        IIntuition->SetGadgetAttrs((struct Gadget*)toggleGad, win, NULL,
            BUTTON_RenderImage, (ULONG)gBtnImg[IMG_DETAILS],
            BUTTON_SelectImage, (ULONG)gBtnImgSel[IMG_DETAILS],
            GA_HintInfo, (ULONG)LS(MSG_BTN_DETAILS), TAG_DONE);
    else
        IIntuition->SetGadgetAttrs((struct Gadget*)toggleGad, win, NULL,
            BUTTON_RenderImage, (ULONG)gBtnImg[IMG_ICONS],
            BUTTON_SelectImage, (ULONG)gBtnImgSel[IMG_ICONS],
            GA_HintInfo, (ULONG)LS(MSG_BTN_ICONS), TAG_DONE);
}

/* ===== Finestra About =====
   Etichetta di sola lettura: un button trasparente senza bordo (come la
   barra di stato).  Evita label.image, che su alcuni sistemi resta vuota,
   e permette il grassetto tramite BUTTON_SoftStyle. */

static Object *MakeAboutLabel(CONST_STRPTR text, ULONG style)
{
    return IIntuition->NewObject(NULL, "button.gadget",
        GA_ReadOnly, TRUE,
        GA_Underscore, 0,
        GA_Text, (ULONG)text,
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,
        BUTTON_Justification, BCJ_CENTER,
        BUTTON_SoftStyle, style,
        TAG_DONE);
}

void ShowAbout(struct Window *win)
{
    Object *winObj = NULL;
    struct Window *aboutWin = NULL;
    ULONG sig = 0;
    BOOL done = FALSE;
    struct Screen *scr = win ? win->WScreen : NULL;
    char byText[128];
    char suffixText[128];
    char verText[128];
    char rexxText[128];
    char thanksText[256];
    CONST_STRPTR thanksPtr;

    snprintf(byText, sizeof(byText), "%s ", LS(MSG_ABOUT_BY));
    snprintf(suffixText, sizeof(suffixText), " %s", LS(MSG_ABOUT_AUTHOR_SUFFIX));
    snprintf(verText, sizeof(verText), LS(MSG_ABOUT_VERSION), VFM_VERSION_LONG);
    snprintf(rexxText, sizeof(rexxText), "ARexx: %s", gRexxPortName);

    /* I cataloghi .catalog gia' installati possono essere precedenti
       all'aggiunta di AlfredONE: appendilo alla stringa localizzata. */
    thanksPtr = LS(MSG_ABOUT_THANKS);
    if (!strstr(thanksPtr, "AlfredONE"))
    {
        snprintf(thanksText, sizeof(thanksText), "%s, AlfredONE", thanksPtr);
        thanksPtr = thanksText;
    }

    winObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)LS(MSG_APP_TITLE), WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, FALSE, WA_InnerWidth, 380, WA_InnerHeight, 210, WA_PubScreen, (ULONG)scr, WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,

            LAYOUT_AddChild, (ULONG)MakeAboutLabel(LS(MSG_APP_TITLE), FSF_BOLD),
                CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, (ULONG)MakeAboutLabel(verText, 0),
                CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, (ULONG)MakeAboutLabel(rexxText, 0),
                CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,

            /* by VagaPPC (Benedetto Lorello) -- "VagaPPC" in grassetto */
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE), CHILD_WeightedWidth, 100,
                LAYOUT_AddChild, (ULONG)MakeAboutLabel(byText, 0), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)MakeAboutLabel(LS(MSG_ABOUT_AUTHOR_NAME), FSF_BOLD), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)MakeAboutLabel(suffixText, 0), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE), CHILD_WeightedWidth, 100,
            TAG_DONE), CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, (ULONG)MakeAboutLabel(LS(MSG_ABOUT_MAIL), 0),
                CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, (ULONG)MakeAboutLabel(thanksPtr, 0),
                CHILD_WeightedWidth, 100, CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE), CHILD_WeightedWidth, 100,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_RENAME_OK, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_BTN_OK), TAG_DONE), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE), CHILD_WeightedWidth, 100,
            TAG_DONE), CHILD_WeightedHeight, 0,
        TAG_DONE), TAG_DONE);

    if (winObj)
    {
        aboutWin = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
        if (aboutWin)
        {
            IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
            while (!done)
            {
                ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
                if (wait & SIGBREAKF_CTRL_C) done = TRUE;
                if (wait & sig)
                {
                    ULONG res; WORD code = 0;
                    while ((res = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                    {
                        switch (res & WMHI_CLASSMASK)
                        {
                            case WMHI_CLOSEWINDOW: done = TRUE; break;
                            case WMHI_GADGETUP: done = TRUE; break;
                            default: break;
                        }
                    }
                }
            }
        }
        IIntuition->DisposeObject(winObj);
    }
}

/* ===== Segnalibri ===== */

void AddBookmark(void)
{
    char name[64], path[PATH_MAX_LEN];
    strncpy(name, gWinTitle, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    strncpy(path, gPathBuffer, PATH_MAX_LEN - 1);
    path[PATH_MAX_LEN - 1] = '\0';
    if (!PromptBookmark(name, sizeof(name), path, sizeof(path))) return;
    if (name[0] == '\0' || path[0] == '\0') return;
    if (gBookmarkCount < MAX_BOOKMARKS)
    {
        strncpy(gBookmarks[gBookmarkCount].name, name, sizeof(gBookmarks[0].name) - 1);
        gBookmarks[gBookmarkCount].name[sizeof(gBookmarks[0].name) - 1] = '\0';
        strncpy(gBookmarks[gBookmarkCount].path, path, PATH_MAX_LEN - 1);
        gBookmarks[gBookmarkCount].path[PATH_MAX_LEN - 1] = '\0';
        gBookmarks[gBookmarkCount].viewMode = gViewMode;
        gBookmarks[gBookmarkCount].filter = gViewFilter;
        gBookmarks[gBookmarkCount].sortMode = gSortMode;
        gBookmarkCount++;
        SaveBookmarks();
    }
}

void LoadBookmarks(void)
{
    BPTR fh = IDOS->Open(BOOKMARK_FILE, MODE_OLDFILE);
    char line[PATH_MAX_LEN + 128];
    gBookmarkCount = 0;
    if (!fh) return;
    while (gBookmarkCount < MAX_BOOKMARKS && IDOS->FGets(fh, line, sizeof(line)))
    {
        char *p = line, *name, *path, *vm, *fl, *so;
        ULONG len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) { line[--len] = '\0'; }
        name = p; p = strchr(p, '|'); if (!p) continue; *p++ = '\0';
        path = p; p = strchr(p, '|'); if (!p) continue; *p++ = '\0';
        vm = p; p = strchr(p, '|'); if (!p) continue; *p++ = '\0';
        fl = p; p = strchr(p, '|'); if (!p) continue; *p++ = '\0';
        so = p;
        strncpy(gBookmarks[gBookmarkCount].name, name, sizeof(gBookmarks[0].name) - 1);
        gBookmarks[gBookmarkCount].name[sizeof(gBookmarks[0].name) - 1] = '\0';
        strncpy(gBookmarks[gBookmarkCount].path, path, PATH_MAX_LEN - 1);
        gBookmarks[gBookmarkCount].path[PATH_MAX_LEN - 1] = '\0';
        gBookmarks[gBookmarkCount].viewMode = strtoul(vm, NULL, 10);
        gBookmarks[gBookmarkCount].filter = strtoul(fl, NULL, 10);
        gBookmarks[gBookmarkCount].sortMode = strtoul(so, NULL, 10);
        gBookmarkCount++;
    }
    IDOS->Close(fh);
}

void SaveBookmarks(void)
{
    BPTR fh = IDOS->Open(BOOKMARK_FILE, MODE_NEWFILE);
    LONG i;
    if (!fh) return;
    for (i = 0; i < gBookmarkCount; i++)
    {
        IDOS->FPrintf(fh, "%s|%s|%lu|%lu|%lu\n",
            gBookmarks[i].name, gBookmarks[i].path,
            (unsigned long)gBookmarks[i].viewMode, (unsigned long)gBookmarks[i].filter, (unsigned long)gBookmarks[i].sortMode);
    }
    IDOS->Close(fh);
}

BOOL PromptBookmark(STRPTR nameBuf, ULONG nameSize, STRPTR pathBuf, ULONG pathSize)
{
    Object *winObj = NULL, *nameGad = NULL, *pathGad = NULL;
    struct Window *win = NULL;
    BOOL ok = FALSE, done = FALSE;
    ULONG sig = 0;

    nameGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_RENAME_STR, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, nameSize - 1, STRINGA_TextVal, (ULONG)nameBuf, STRINGA_Buffer, (ULONG)nameBuf, TAG_DONE);
    pathGad = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID, GID_SORT_MODE, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, pathSize - 1, STRINGA_TextVal, (ULONG)pathBuf, STRINGA_Buffer, (ULONG)pathBuf, TAG_DONE);

    winObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)LS(MSG_MENU_ADDBOOKMARK), WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, FALSE, WA_InnerWidth, 420, WA_InnerHeight, 120, WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ReadOnly, TRUE, GA_Text, (ULONG)LS(MSG_COL_NAME), GA_Underscore, 0, BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)nameGad, CHILD_WeightedWidth, 100,
            TAG_DONE), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ReadOnly, TRUE, GA_Text, (ULONG)LS(MSG_LABEL_PATH), GA_Underscore, 0, BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, TAG_DONE), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)pathGad, CHILD_WeightedWidth, 100,
            TAG_DONE), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_RENAME_OK, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_BTN_OK), TAG_DONE),
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_RENAME_CANCEL, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_BTN_CANCEL), TAG_DONE),
            TAG_DONE), CHILD_WeightedHeight, 0,
        TAG_DONE), TAG_DONE);

    if (winObj)
    {
        win = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
        if (win)
        {
            IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
            while (!done)
            {
                ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
                if (wait & SIGBREAKF_CTRL_C) { done = TRUE; ok = FALSE; }
                if (wait & sig)
                {
                    ULONG res; WORD code = 0;
                    while ((res = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                    {
                        switch (res & WMHI_CLASSMASK)
                        {
                            case WMHI_CLOSEWINDOW: done = TRUE; ok = FALSE; break;
                            case WMHI_GADGETUP:
                                switch (res & WMHI_GADGETMASK)
                                {
                                    case GID_RENAME_OK:
                                    { STRPTR t = NULL; IIntuition->GetAttr(STRINGA_TextVal, nameGad, (ULONG *)&t); if (t) { strncpy(nameBuf, t, nameSize - 1); nameBuf[nameSize - 1] = '\0'; } t = NULL; IIntuition->GetAttr(STRINGA_TextVal, pathGad, (ULONG *)&t); if (t) { strncpy(pathBuf, t, pathSize - 1); pathBuf[pathSize - 1] = '\0'; } ok = TRUE; done = TRUE; break; }
                                    case GID_RENAME_CANCEL: ok = FALSE; done = TRUE; break;
                                    default: break;
                                }
                                break;
                            default: break;
                        }
                    }
                }
            }
        }
        IIntuition->DisposeObject(winObj);
    }
    return ok;
}

Object *gMenuRoot = NULL;
Object *gMenuImg[MIMG_COUNT];

static CONST_STRPTR gMenuImgFiles[MIMG_COUNT] =
{
    "m_new", "m_info", "m_quit", "m_rename", "m_delete", "m_duplicate",
    "m_copy", "m_cut", "m_paste", "m_size", "m_open", "m_refresh",
    "m_icons", "m_showinfo", "m_filter", "m_sort", "m_bookmark",
    "m_bookmarkadd", "m_settings", "m_script", "m_folder", "m_shell",
    "m_selectall", "m_unselectall", "m_invert"
};

/* Corrispondenti nomi AISS in tbimages: per i menu si usano le icone
   24x24 installate da AISS; se mancano si torna alle copie locali 20x20. */
static CONST_STRPTR gMenuImgAiss[MIMG_COUNT] =
{
    "new", "info", "quit", "rename", "delete", "duplicate",
    "copy", "cut", "paste", "filesize", "open", "refresh",
    "window", "info", "filter", "sortingandgrouping", "bookmark",
    "bookmarkadd", "settings", "filetext", "folder", "shell",
    "file_selectall", "file_unselectall", "fx_invert"
};

/* ===== Menu ===== */

void LoadMenuImages(struct Screen *scr)
{
    int i;
    for (i = 0; i < MIMG_COUNT; i++)
    {
        char path[300];
        if (gMenuImg[i]) continue;
        path[0] = '\0';
        if (TbimagesAvailable())
        {
            snprintf(path, sizeof(path), "tbimages:%s", gMenuImgAiss[i]);
            if (!IconFileExists(path))
                path[0] = '\0';
        }
        if (!path[0])
            snprintf(path, sizeof(path), "PROGDIR:icons/%s.png", gMenuImgFiles[i]);
        gMenuImg[i] = IIntuition->NewObject(NULL, "bitmap.image",
            BITMAP_SourceFile, (ULONG)path,
            BITMAP_Screen, (ULONG)scr,
            BITMAP_Masking, TRUE,
            TAG_DONE);
    }
}

void FreeMenuImages(void)
{
    int i;
    for (i = 0; i < MIMG_COUNT; i++)
    {
        if (gMenuImg[i]) { IIntuition->DisposeObject(gMenuImg[i]); gMenuImg[i] = NULL; }
    }
}

/* ---- costruzione del menu con menuclass (icone AISS) ---- */

static void AddMenuTag(struct TagItem *tags, int *n, Tag tag, ULONG data)
{
    tags[*n].ti_Tag = tag;
    tags[*n].ti_Data = data;
    (*n)++;
}

static void AddMenuImage(struct TagItem *tags, int *n, int img)
{
    if (img >= 0 && img < MIMG_COUNT && gMenuImg[img])
    {
        AddMenuTag(tags, n, MA_Image, (ULONG)gMenuImg[img]);
        AddMenuTag(tags, n, MA_FreeImage, FALSE);
    }
}

static void AddMenuItem(struct TagItem *tags, int *n, CONST_STRPTR label,
                        ULONG id, CONST_STRPTR key, int img,
                        BOOL toggle, BOOL selected, BOOL disabled)
{
    AddMenuTag(tags, n, NM_Item, (ULONG)label);
    /* Le etichette di un solo carattere (es. un segnalibro "S") non devono
       essere interpretate come tasto di scelta rapida. */
    AddMenuTag(tags, n, MA_EmbeddedKey, FALSE);
    if (id) AddMenuTag(tags, n, MA_ID, id);
    if (key && key[0]) AddMenuTag(tags, n, MA_Key, (ULONG)key);
    AddMenuImage(tags, n, img);
    if (toggle)
    {
        AddMenuTag(tags, n, MA_Toggle, TRUE);
        AddMenuTag(tags, n, MA_Selected, selected ? TRUE : FALSE);
    }
    if (disabled) AddMenuTag(tags, n, MA_Disabled, TRUE);
}

static void AddMenuSeparator(struct TagItem *tags, int *n)
{
    AddMenuTag(tags, n, NM_Item, (ULONG)ML_SEPARATOR);
}

/* Crea un albero menuclass (T_ROOT) dalla lista di tag mpNewMenu.
   NULL se la costruzione fallisce. */
static Object *NewMenuTree(struct TagItem *tags)
{
    struct mpNewMenu nm;
    Object *root = IIntuition->NewObject(NULL, "menuclass", MA_Type, (ULONG)T_ROOT, TAG_DONE);

    if (!root) return NULL;
    nm.MethodID = MM_NEWMENU;
    nm.mpnm_Reserved = 0;
    nm.mpnm_AttrList = tags;
    if (!IIntuition->IDoMethodA(root, (Msg)&nm))
    {
        ULONG err = 0;
        IIntuition->GetAttr(MA_ErrorCode, root, &err);
        DBG("menuclass MM_NEWMENU failed: %lu", (unsigned long)err);
        IIntuition->DisposeObject(root);
        return NULL;
    }
    return root;
}

void BuildMenu(void)
{
    struct TagItem *tags;
    int n = 0, j;

    tags = (struct TagItem *)IExec->AllocVecTags(4096 * sizeof(struct TagItem),
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (!tags) return;

    /* Program */
    AddMenuTag(tags, &n, NM_Menu, (ULONG)LS(MSG_MENU_PROGRAM));
    AddMenuItem(tags, &n, LS(MSG_MENU_NEWWINDOW), MENU_NEWWINDOW, "N", MIMG_NEW, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_INFO), MENU_INFO, "I", MIMG_INFO, FALSE, FALSE, FALSE);
    AddMenuSeparator(tags, &n);
    AddMenuItem(tags, &n, LS(MSG_MENU_QUIT), MENU_QUIT, "E", MIMG_QUIT, FALSE, FALSE, FALSE);

    /* Edit */
    AddMenuTag(tags, &n, NM_Menu, (ULONG)LS(MSG_MENU_EDIT));
    AddMenuItem(tags, &n, LS(MSG_MENU_OPEN_NEWWIN), MENU_OPEN, NULL, MIMG_OPEN, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_OPEN_WITH), MENU_OPEN_WITH, NULL, MIMG_OPEN, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_NEW), 0, NULL, MIMG_NEW, FALSE, FALSE, FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_BEGIN);
    AddMenuItem(tags, &n, LS(MSG_MENU_NEW_DRAWER), MENU_NEW_DRAWER, NULL, MIMG_FOLDER, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_NEW_TEXTFILE), MENU_NEW_TEXTFILE, NULL, MIMG_SCRIPT, FALSE, FALSE, FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_END);
    AddMenuSeparator(tags, &n);
    AddMenuItem(tags, &n, LS(MSG_MENU_SELECT_ALL), MENU_SELECT_ALL, "A", MIMG_SELECTALL, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_INVERT_SELECTION), MENU_INVERT_SELECTION, NULL, MIMG_INVERT, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_DESELECT_ALL), MENU_DESELECT_ALL, NULL, MIMG_UNSELECTALL, FALSE, FALSE, FALSE);
    AddMenuSeparator(tags, &n);
    AddMenuItem(tags, &n, LS(MSG_RENAME), MENU_RENAME, NULL, MIMG_RENAME, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_DELETE), MENU_DELETE, NULL, MIMG_DELETE, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_DUPLICATE), MENU_DUPLICATE, NULL, MIMG_DUPLICATE, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_COPY), MENU_COPY, NULL, MIMG_COPY, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_CUT), MENU_CUT, NULL, MIMG_CUT, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_PASTE), MENU_PASTE, NULL, MIMG_PASTE, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_INFO), MENU_FILEINFO, NULL, MIMG_INFO, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_CALC_SIZE), MENU_CALCSIZE, NULL, MIMG_SIZE, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_SHELL), MENU_SHELL, NULL, MIMG_SHELL, FALSE, FALSE, FALSE);

    /* View */
    AddMenuTag(tags, &n, NM_Menu, (ULONG)LS(MSG_MENU_VIEW));
    AddMenuItem(tags, &n, LS(MSG_COL_SIZE), 0, NULL, MIMG_ICONS, FALSE, FALSE, FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_BEGIN);
    AddMenuItem(tags, &n, "Auto", MENU_ICON_SIZE_AUTO, NULL, -1,
                TRUE, (gIconSize == 0), FALSE);
    AddMenuItem(tags, &n, "32 x 32", MENU_ICON_SIZE_32, NULL, -1,
                TRUE, (gIconSize == 32), FALSE);
    AddMenuItem(tags, &n, "48 x 48", MENU_ICON_SIZE_48, NULL, -1,
                TRUE, (gIconSize == 48), FALSE);
    AddMenuItem(tags, &n, "64 x 64", MENU_ICON_SIZE_64, NULL, -1,
                TRUE, (gIconSize == 64), FALSE);
    AddMenuItem(tags, &n, "96 x 96", MENU_ICON_SIZE_96, NULL, -1,
                TRUE, (gIconSize == 96), FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_END);
    AddMenuSeparator(tags, &n);
    AddMenuItem(tags, &n, LS(MSG_BTN_REFRESH), MENU_REFRESH, NULL, MIMG_REFRESH, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_BTN_ICONS), MENU_TOGGLE_VIEW, NULL, MIMG_ICONS, FALSE, FALSE, FALSE);
    AddMenuSeparator(tags, &n);
    AddMenuItem(tags, &n, LS(MSG_MENU_SHOWINFO), MENU_VIEW_SHOWINFO, NULL, MIMG_SHOWINFO,
                TRUE, gShowInfoFiles ? TRUE : FALSE, (gViewMode == FBVM_ICONS));
    AddMenuItem(tags, &n, LS(MSG_MENU_SHOW), 0, NULL, MIMG_FILTER, FALSE, FALSE, FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_BEGIN);
    AddMenuItem(tags, &n, LS(MSG_VIEW_ICONS_ONLY), MENU_VIEW_ICONS, NULL, MIMG_ICONS,
                TRUE, (gViewFilter == VIEW_FILTER_ICONS_ONLY), FALSE);
    AddMenuItem(tags, &n, LS(MSG_VIEW_ALL_FILES), MENU_VIEW_ALLFILES, NULL, MIMG_FILTER,
                TRUE, (gViewFilter == VIEW_FILTER_ALL_FILES), FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_END);
    AddMenuItem(tags, &n, LS(MSG_SORT), 0, NULL, MIMG_SORT, FALSE, FALSE, FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_BEGIN);
    AddMenuItem(tags, &n, LS(MSG_SORT_BY_TYPE), MENU_SORT_TYPE, NULL, MIMG_SORT,
                TRUE, (gSortMode == SORT_BY_TYPE), FALSE);
    AddMenuItem(tags, &n, LS(MSG_SORT_BY_NAME), MENU_SORT_NAME, NULL, MIMG_SORT,
                TRUE, (gSortMode == SORT_BY_NAME), FALSE);
    AddMenuItem(tags, &n, LS(MSG_SORT_BY_DATE), MENU_SORT_DATE, NULL, MIMG_SORT,
                TRUE, (gSortMode == SORT_BY_DATE), FALSE);
    AddMenuItem(tags, &n, LS(MSG_SORT_BY_SIZE), MENU_SORT_SIZE, NULL, MIMG_SORT,
                TRUE, (gSortMode == SORT_BY_SIZE), FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_END);

    /* Bookmarks */
    AddMenuTag(tags, &n, NM_Menu, (ULONG)LS(MSG_MENU_BOOKMARKS));
    AddMenuItem(tags, &n, LS(MSG_MENU_ADDBOOKMARK), MENU_ADDBOOKMARK, NULL, MIMG_BOOKMARKADD, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_MANAGE), MENU_MANAGE, NULL, MIMG_SETTINGS, FALSE, FALSE, FALSE);
    if (gBookmarkCount > 0)
    {
        AddMenuSeparator(tags, &n);
        for (j = 0; j < gBookmarkCount; j++)
            AddMenuItem(tags, &n, gBookmarks[j].name, MENU_BOOKMARK_BASE + j, NULL, MIMG_BOOKMARK, FALSE, FALSE, FALSE);
    }

    /* Script */
    AddMenuTag(tags, &n, NM_Menu, (ULONG)LS(MSG_MENU_SCRIPT));
    for (j = 0; j < gPluginCount; j++)
        AddMenuItem(tags, &n, FilePart(gPlugins[j]), MENU_SCRIPT_BASE + j, NULL, MIMG_SCRIPT, FALSE, FALSE, FALSE);
    if (gPluginCount > 0) AddMenuSeparator(tags, &n);
    AddMenuItem(tags, &n, LS(MSG_SCRIPT_REFRESH), MENU_SCRIPT_REFRESH, NULL, MIMG_REFRESH, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_SCRIPT_OPENDIR), MENU_SCRIPT_OPENDIR, NULL, MIMG_FOLDER, FALSE, FALSE, FALSE);

    AddMenuTag(tags, &n, TAG_DONE, 0);

    gMenuRoot = NewMenuTree(tags);
    IExec->FreeVec(tags);
}

/* ---- menu contestuali (tasto destro sulla lista) ----
   Due menu fissi, costruiti una volta all'avvio: uno per un file o un
   cassetto sotto il mouse, uno per lo spazio vuoto.  Usano gli stessi
   MENU_* ID (e le stesse icone) del menu Modifica, quindi le azioni
   passano dallo stesso switch del loop principale.  Apri usa
   MENU_CTX_OPEN: lancia i file e apre ogni cassetto selezionato in una
   nuova finestra (come il Workbench). */

Object *gCtxMenuItem = NULL;
Object *gCtxMenuFile = NULL;
Object *gCtxMenuEmpty = NULL;

/* Separatore + sottomenu "Mostra" (per icone / tutti file) e
   "Preferiti", comuni a tutti i menu contestuali.  La spunta di "Mostra"
   viene riallineata da SyncContextMenus() prima di ogni apertura; i
   preferiti cambiano raramente e i menu vengono ricostruiti da
   RefreshContextMenus() quando la lista cambia. */
static void AddCtxViewEntries(struct TagItem *tags, int *n)
{
    AddMenuSeparator(tags, n);
    AddMenuItem(tags, n, LS(MSG_MENU_SHOW), 0, NULL, MIMG_FILTER, FALSE, FALSE, FALSE);
    AddMenuTag(tags, n, NM_SubItems, SI_BEGIN);
    AddMenuItem(tags, n, LS(MSG_VIEW_ICONS_ONLY), MENU_VIEW_ICONS, NULL, MIMG_ICONS,
                TRUE, (gViewFilter == VIEW_FILTER_ICONS_ONLY), FALSE);
    AddMenuItem(tags, n, LS(MSG_VIEW_ALL_FILES), MENU_VIEW_ALLFILES, NULL, MIMG_FILTER,
                TRUE, (gViewFilter == VIEW_FILTER_ALL_FILES), FALSE);
    AddMenuTag(tags, n, NM_SubItems, SI_END);

    AddMenuItem(tags, n, LS(MSG_MENU_BOOKMARKS), 0, NULL, MIMG_BOOKMARK, FALSE, FALSE, FALSE);
    AddMenuTag(tags, n, NM_SubItems, SI_BEGIN);
    AddMenuItem(tags, n, LS(MSG_MENU_ADDBOOKMARK), MENU_ADDBOOKMARK, NULL, MIMG_BOOKMARKADD, FALSE, FALSE, FALSE);
    AddMenuItem(tags, n, LS(MSG_MENU_MANAGE), MENU_MANAGE, NULL, MIMG_SETTINGS, FALSE, FALSE, FALSE);
    if (gBookmarkCount > 0)
    {
        int j;
        AddMenuSeparator(tags, n);
        for (j = 0; j < gBookmarkCount; j++)
            AddMenuItem(tags, n, gBookmarks[j].name, MENU_BOOKMARK_BASE + j, NULL, MIMG_BOOKMARK, FALSE, FALSE, FALSE);
    }
    AddMenuTag(tags, n, NM_SubItems, SI_END);
}

/* Voci per un file o un cassetto; withOpenWith aggiunge "Apri con..."
   (solo per i file). */
static void AddCtxItemEntries(struct TagItem *tags, int *n, BOOL withOpenWith)
{
    AddMenuTag(tags, n, NM_Menu, (ULONG)LS(MSG_MENU_EDIT));
    AddMenuItem(tags, n, LS(MSG_BTN_OPEN), MENU_CTX_OPEN, NULL, MIMG_OPEN, FALSE, FALSE, FALSE);
    if (withOpenWith)
        AddMenuItem(tags, n, LS(MSG_MENU_OPEN_WITH), MENU_OPEN_WITH, NULL, MIMG_OPEN, FALSE, FALSE, FALSE);
    AddMenuSeparator(tags, n);
    AddMenuItem(tags, n, LS(MSG_RENAME), MENU_RENAME, NULL, MIMG_RENAME, FALSE, FALSE, FALSE);
    AddMenuItem(tags, n, LS(MSG_DELETE), MENU_DELETE, NULL, MIMG_DELETE, FALSE, FALSE, FALSE);
    AddMenuItem(tags, n, LS(MSG_DUPLICATE), MENU_DUPLICATE, NULL, MIMG_DUPLICATE, FALSE, FALSE, FALSE);
    AddMenuSeparator(tags, n);
    AddMenuItem(tags, n, LS(MSG_COPY), MENU_COPY, NULL, MIMG_COPY, FALSE, FALSE, FALSE);
    AddMenuItem(tags, n, LS(MSG_CUT), MENU_CUT, NULL, MIMG_CUT, FALSE, FALSE, FALSE);
    AddMenuSeparator(tags, n);
    AddMenuItem(tags, n, LS(MSG_INFO), MENU_FILEINFO, NULL, MIMG_INFO, FALSE, FALSE, FALSE);
    AddMenuItem(tags, n, LS(MSG_CALC_SIZE), MENU_CALCSIZE, NULL, MIMG_SIZE, FALSE, FALSE, FALSE);
    AddCtxViewEntries(tags, n);
    AddMenuTag(tags, n, TAG_DONE, 0);
}

#define CTX_TAGS 1024     /* 64 preferiti x ~6 tag + le voci fisse */

/* Firma della lista preferiti con cui sono stati costruiti i menu
   contestuali: se cambia (aggiunta, rimozione, rinomina, riordino) i
   menu vanno ricostruiti. */
static ULONG gCtxBookmarkSig = 0;

static ULONG BookmarkSignature(void)
{
    ULONG h = 5381 + (ULONG)gBookmarkCount;
    int j;
    const char *c;

    for (j = 0; j < gBookmarkCount; j++)
    {
        for (c = gBookmarks[j].name; *c; c++) h = h * 33 + (UBYTE)*c;
        h = h * 33 + 1;
    }
    return h;
}

void BuildContextMenus(void)
{
    struct TagItem *tags;
    Object *mItem, *mFile, *mEmpty, *oItem, *oFile, *oEmpty;
    int n;

    tags = (struct TagItem *)IExec->AllocVecTags(CTX_TAGS * sizeof(struct TagItem),
        AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (!tags) return;

    /* cassetto */
    n = 0;
    AddCtxItemEntries(tags, &n, FALSE);
    mItem = NewMenuTree(tags);

    /* file */
    memset(tags, 0, CTX_TAGS * sizeof(struct TagItem));
    n = 0;
    AddCtxItemEntries(tags, &n, TRUE);
    mFile = NewMenuTree(tags);

    /* spazio vuoto */
    memset(tags, 0, CTX_TAGS * sizeof(struct TagItem));
    n = 0;
    AddMenuTag(tags, &n, NM_Menu, (ULONG)LS(MSG_MENU_EDIT));
    AddMenuItem(tags, &n, LS(MSG_MENU_NEW), 0, NULL, MIMG_NEW, FALSE, FALSE, FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_BEGIN);
    AddMenuItem(tags, &n, LS(MSG_MENU_NEW_DRAWER), MENU_NEW_DRAWER, NULL, MIMG_FOLDER, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_MENU_NEW_TEXTFILE), MENU_NEW_TEXTFILE, NULL, MIMG_SCRIPT, FALSE, FALSE, FALSE);
    AddMenuTag(tags, &n, NM_SubItems, SI_END);
    AddMenuItem(tags, &n, LS(MSG_PASTE), MENU_PASTE, NULL, MIMG_PASTE, FALSE, FALSE, FALSE);
    AddMenuSeparator(tags, &n);
    AddMenuItem(tags, &n, LS(MSG_MENU_SHELL), MENU_SHELL, NULL, MIMG_SHELL, FALSE, FALSE, FALSE);
    AddMenuItem(tags, &n, LS(MSG_BTN_REFRESH), MENU_REFRESH, NULL, MIMG_REFRESH, FALSE, FALSE, FALSE);
    AddCtxViewEntries(tags, &n);
    AddMenuTag(tags, &n, TAG_DONE, 0);
    mEmpty = NewMenuTree(tags);

    IExec->FreeVec(tags);

    /* Sostituisce i puntatori in un colpo solo (l'hook del menu
       contestuale li legge dal task input.device), poi libera i vecchi. */
    IExec->Forbid();
    oItem = gCtxMenuItem;   gCtxMenuItem = mItem;
    oFile = gCtxMenuFile;   gCtxMenuFile = mFile;
    oEmpty = gCtxMenuEmpty; gCtxMenuEmpty = mEmpty;
    IExec->Permit();
    if (oItem)  IIntuition->DisposeObject(oItem);
    if (oFile)  IIntuition->DisposeObject(oFile);
    if (oEmpty) IIntuition->DisposeObject(oEmpty);

    gCtxBookmarkSig = BookmarkSignature();
}

/* Ricostruisce i menu contestuali se i preferiti sono cambiati. */
void RefreshContextMenus(void)
{
    if (gCtxBookmarkSig != BookmarkSignature())
        BuildContextMenus();
}

/* I menu contestuali sono costruiti una volta sola: prima di mostrarne
   uno si riallinea la spunta di "Mostra" al filtro
   corrente (puo' essere cambiato dal menu Vista o da un click precedente). */
static void SetCtxChecked(Object *root, ULONG id, BOOL on)
{
    struct mpSetState ss;
    ss.MethodID = MM_SETSTATE;
    ss.mpss_Reserved = 0;
    ss.mpss_ID = id;
    ss.mpss_ApplyMask = MS_CHECKED;
    ss.mpss_StateMask = on ? MS_CHECKED : 0;
    IIntuition->IDoMethodA(root, (Msg)&ss);
}

void SyncContextMenus(void)
{
    Object *menus[3];
    int i;

    menus[0] = gCtxMenuItem;
    menus[1] = gCtxMenuFile;
    menus[2] = gCtxMenuEmpty;
    for (i = 0; i < 3; i++)
    {
        if (!menus[i]) continue;
        SetCtxChecked(menus[i], MENU_VIEW_ICONS, gViewFilter == VIEW_FILTER_ICONS_ONLY);
        SetCtxChecked(menus[i], MENU_VIEW_ALLFILES, gViewFilter == VIEW_FILTER_ALL_FILES);
    }
}

void FreeContextMenus(void)
{
    if (gCtxMenuItem)  { IIntuition->DisposeObject(gCtxMenuItem);  gCtxMenuItem = NULL; }
    if (gCtxMenuFile)  { IIntuition->DisposeObject(gCtxMenuFile);  gCtxMenuFile = NULL; }
    if (gCtxMenuEmpty) { IIntuition->DisposeObject(gCtxMenuEmpty); gCtxMenuEmpty = NULL; }
}

/* ===== Gestione segnalibri (dialogo) ===== */

void ManageBookmarks(Object *mainWinObj)
{
    struct List bmlist;
    (void)mainWinObj;
    struct ColumnInfo *bci = NULL;
    Object *winObj = NULL, *bmGad = NULL;
    struct Window *win = NULL;
    ULONG sig = 0;
    BOOL done = FALSE;
    LONG j;

    IExec->NewList(&bmlist);
    for (j = 0; j < gBookmarkCount; j++)
    {
        struct Node *n = IListBrowser->AllocListBrowserNode(2,
            LBNA_UserData, j,
            LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)gBookmarks[j].name,
            LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)gBookmarks[j].path,
            TAG_DONE);
        if (n) IExec->AddTail(&bmlist, n);
    }

    bci = IListBrowser->AllocLBColumnInfo(2,
        LBCIA_Column, 0, LBCIA_Title, (ULONG)LS(MSG_COL_NAME), LBCIA_Weight, 40,
        LBCIA_Column, 1, LBCIA_Title, (ULONG)LS(MSG_LABEL_PATH), LBCIA_Weight, 60,
        TAG_DONE);

    bmGad = IIntuition->NewObject(NULL, "listbrowser.gadget",
        GA_ID, GID_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&bmlist, LISTBROWSER_ColumnInfo, (ULONG)bci,
        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_MultiSelect, FALSE,
        TAG_DONE);

    winObj = IIntuition->NewObject(NULL, "window.class",
        WA_Title, (ULONG)LS(MSG_MENU_MANAGE), WA_Activate, TRUE, WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_InnerWidth, 500, WA_InnerHeight, 300, WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_Layout, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, (ULONG)bmGad, CHILD_WeightedHeight, 100,
            LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_RENAME, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_MENU_EDIT), TAG_DONE), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_DELETE, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_DELETE), TAG_DONE), CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "layout.gadget", TAG_DONE), CHILD_WeightedWidth, 100,
                LAYOUT_AddChild, (ULONG)IIntuition->NewObject(NULL, "button.gadget", GA_ID, GID_RENAME_CANCEL, GA_RelVerify, TRUE, GA_Text, (ULONG)LS(MSG_BTN_OK), TAG_DONE), CHILD_WeightedWidth, 0,
            TAG_DONE), CHILD_WeightedHeight, 0,
        TAG_DONE), TAG_DONE);

    if (winObj)
    {
        win = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
        if (win)
        {
            IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
            while (!done)
            {
                ULONG wait = IExec->Wait(sig | SIGBREAKF_CTRL_C);
                if (wait & SIGBREAKF_CTRL_C) done = TRUE;
                if (wait & sig)
                {
                    ULONG res; WORD code = 0;
                    while ((res = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
                    {
                        switch (res & WMHI_CLASSMASK)
                        {
                            case WMHI_CLOSEWINDOW: done = TRUE; break;
                            case WMHI_GADGETUP:
                                switch (res & WMHI_GADGETMASK)
                                {
                                    case GID_RENAME_CANCEL: done = TRUE; break;
                                    case GID_RENAME:
                                    case GID_DELETE:
                                    {
                                        struct Node *n = NULL;
                                        IIntuition->GetAttrs(bmGad, LISTBROWSER_SelectedNode, &n, TAG_DONE);
                                        if (n)
                                        {
                                            ULONG idx = MAX_BOOKMARKS;
                                            IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &idx, TAG_DONE);
                                            if (idx < (ULONG)gBookmarkCount)
                                            {
                                                if ((res & WMHI_GADGETMASK) == GID_DELETE)
                                                {
                                                    for (j = (LONG)idx; j < gBookmarkCount - 1; j++) gBookmarks[j] = gBookmarks[j + 1];
                                                    gBookmarkCount--;
                                                }
                                                else
                                                {
                                                    char nm[64], pt[PATH_MAX_LEN];
                                                    strncpy(nm, gBookmarks[idx].name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = '\0';
                                                    strncpy(pt, gBookmarks[idx].path, PATH_MAX_LEN - 1); pt[PATH_MAX_LEN - 1] = '\0';
                                                    if (PromptBookmark(nm, sizeof(nm), pt, sizeof(pt)))
                                                    {
                                                        strncpy(gBookmarks[idx].name, nm, sizeof(gBookmarks[idx].name) - 1);
                                                        gBookmarks[idx].name[sizeof(gBookmarks[idx].name) - 1] = '\0';
                                                        strncpy(gBookmarks[idx].path, pt, PATH_MAX_LEN - 1);
                                                        gBookmarks[idx].path[PATH_MAX_LEN - 1] = '\0';
                                                    }
                                                }
                                                SaveBookmarks();
                                                UpdateMenu();
                                                IIntuition->SetWindowTitles(win, (STRPTR)LS(MSG_MENU_MANAGE), (STRPTR)~0);
                                                /* refresh the list */
                                                IListBrowser->FreeListBrowserList(&bmlist);
                                                for (j = 0; j < gBookmarkCount; j++)
                                                {
                                                    struct Node *n3 = IListBrowser->AllocListBrowserNode(2,
                                                        LBNA_UserData, j,
                                                        LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)gBookmarks[j].name,
                                                        LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)gBookmarks[j].path,
                                                        TAG_DONE);
                                                    if (n3) IExec->AddTail(&bmlist, n3);
                                                }
                                                IIntuition->RefreshSetGadgetAttrs((struct Gadget *)bmGad, win, NULL, LISTBROWSER_Labels, (ULONG)&bmlist, TAG_DONE);
                                            }
                                        }
                                        break;
                                    }
                                    default: break;
                                }
                                break;
                            default: break;
                        }
                    }
                }
            }
        }
        IIntuition->DisposeObject(winObj);
    }
    IListBrowser->FreeListBrowserList(&bmlist);
    if (bci) IListBrowser->FreeLBColumnInfo(bci);
}

/* ===== Vista, menu e nuove istanze ===== */

void SetViewMode(ULONG mode, Object *listGad, Object *toggleGad, struct Window *win)
{
    gViewMode = mode;

    if (listGad)
    {
        if (win)
            IIntuition->SetGadgetAttrs((struct Gadget *)listGad, win, NULL,
                FILEBROWSER_ViewMode, gViewMode,
                FILEBROWSER_IconSize, gIconSize, TAG_DONE);
        else
            IIntuition->SetAttrs(listGad,
                FILEBROWSER_ViewMode, gViewMode,
                FILEBROWSER_IconSize, gIconSize, TAG_DONE);
    }

    if (toggleGad)
    {
        ULONG img = (ULONG)((gViewMode == FBVM_ICONS) ? gBtnImg[IMG_DETAILS] : gBtnImg[IMG_ICONS]);
        ULONG sel = (ULONG)((gViewMode == FBVM_ICONS) ? gBtnImgSel[IMG_DETAILS] : gBtnImgSel[IMG_ICONS]);
        CONST_STRPTR hint = (gViewMode == FBVM_ICONS) ? LS(MSG_BTN_DETAILS) : LS(MSG_BTN_ICONS);

        if (win)
            IIntuition->SetGadgetAttrs((struct Gadget *)toggleGad, win, NULL,
                BUTTON_RenderImage, img, BUTTON_SelectImage, sel, GA_HintInfo, (ULONG)hint, TAG_DONE);
        else
            IIntuition->SetAttrs(toggleGad,
                BUTTON_RenderImage, img, BUTTON_SelectImage, sel, GA_HintInfo, (ULONG)hint, TAG_DONE);
    }
}

void UpdateMenu(void)
{
    Object *old = gMenuRoot;

    BuildMenu();
    if (!gMenuRoot)
    {
        gMenuRoot = old;          /* costruzione fallita: mantieni il menu precedente */
        return;
    }
    if (gMainWinObj)
        IIntuition->SetAttrs(gMainWinObj, WINDOW_MenuStrip, (ULONG)gMenuRoot, TAG_DONE);
    if (old && old != gMenuRoot)
        IIntuition->DisposeObject(old);
    RefreshContextMenus();
}

/* Lancia una nuova istanza di VFM sul percorso indicato ("-" = vista
   radice).  Salva prima le preferenze, cosi' la nuova istanza trova la
   geometria aggiornata del proprio volume.  Con centerMouse TRUE la
   nuova finestra viene centrata sul puntatore (con un piccolo offset),
   invece di riusare la posizione memorizzata del volume. */
void LaunchInstance(CONST_STRPTR path, BOOL centerMouse)
{
    char exe[PATH_MAX_LEN];
    char cmd[PATH_MAX_LEN * 2 + 64];
    BPTR dir = IDOS->GetProgramDir();

    if (!dir) return;
    if (!IDOS->NameFromLock(dir, exe, sizeof(exe))) return;
    if (!IDOS->AddPart(exe, "VagaFileManager", sizeof(exe))) return;

    /* La nuova istanza deve partire con le impostazioni correnti. */
    SavePrefs();

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"%s", exe, (path && path[0]) ? path : "-",
             centerMouse ? " -m" : "");
    IDOS->SystemTags(cmd, SYS_Asynch, TRUE, SYS_Input, 0, SYS_Output, 0,
                     SYS_StackSize, 262144, TAG_DONE);
}

void LaunchNewInstance(void)
{
    LaunchInstance(gPathBuffer, TRUE);
}

/* ===== Immagini e pulsanti ===== */

static BOOL IconFileExists(CONST_STRPTR path)
{
    BPTR lock;
    if (!path || !path[0]) return FALSE;
    lock = IDOS->Lock((STRPTR)path, SHARED_LOCK);
    if (!lock) return FALSE;
    IDOS->UnLock(lock);
    return TRUE;
}

/* Controlla il DosList invece di Lock("tbimages:..."): su un sistema senza
   AISS un Lock su un volume inesistente potrebbe aprire un requester
   "Please insert volume". */
static BOOL TbimagesAvailable(void)
{
    struct DosList *dl;
    BOOL found = FALSE;

    dl = IDOS->LockDosList(LDF_ASSIGNS | LDF_READ);
    if (dl)
    {
        if (IDOS->FindDosEntry(dl, "tbimages", LDF_ASSIGNS))
            found = TRUE;
        IDOS->UnLockDosList(LDF_ASSIGNS | LDF_READ);
    }
    return found;
}

Object *LoadIconImage(struct Screen *scr, CONST_STRPTR name)
{
    char path[300];
    if (!name) return NULL;

    /* AISS installs its 24x24 toolbar PNGs in tbimages: (no extension). */
    path[0] = '\0';
    if (TbimagesAvailable())
    {
        snprintf(path, sizeof(path), "tbimages:%s", name);
        if (!IconFileExists(path))
            path[0] = '\0';
    }
    if (!path[0])
        snprintf(path, sizeof(path), "PROGDIR:icons/%s.png", name);

    return IIntuition->NewObject(NULL, "bitmap.image",
        BITMAP_SourceFile, (ULONG)path,
        BITMAP_Screen, (ULONG)scr,
        BITMAP_Masking, TRUE,
        TAG_DONE);
}

Object *MakeIconButton(ULONG id, Object *img, Object *imgSel, CONST_STRPTR hint)
{
    return IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, id, GA_RelVerify, TRUE,
        GA_HintInfo, (ULONG)hint,
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,
        BUTTON_RenderImage, (ULONG)img,
        BUTTON_SelectImage, (ULONG)imgSel,
        TAG_DONE);
}
