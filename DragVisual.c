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
 * Modulo: DragVisual.c
 * --------------------
 * Aspetto del drag & drop, come nel Workbench:
 *  - le icone degli oggetti trascinati seguono il puntatore ("fantasma"):
 *    una finestra senza bordi, sagomata sulle icone (WA_ShapeRegion),
 *    sempre davanti, non attivabile e trasparente ai clic;
 *  - la destinazione sotto il puntatore viene evidenziata con una cornice
 *    (un'altra finestra sagomata ad anello);
 *  - un timer (timer.device, 50 Hz) muove il fantasma mentre il gadget
 *    lista tiene l'input, perche' in quel momento la finestra non riceve
 *    IDCMP_MOUSEMOVE.
 * Nessuna di queste finestre riceve input: Intuition continua a mandare
 * il rilascio del tasto alla finestra VFM attiva.
 */

#include "VagaFileManager.h"
#include <devices/timer.h>
#include <graphics/regions.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>

#define GHOST_MAX_ICONS   6       /* icone mostrate al massimo */
#define GHOST_STEP_X      12      /* scostamento tra icone a cascata */
#define GHOST_STEP_Y      8
#define GHOST_MAX_W       640
#define GHOST_MAX_H       480
#define GHOST_OPAQUENESS  200     /* 0..255, con compositing attivo */
#define FRAME_THICK       3       /* spessore della cornice */
#define DRAG_TICK_MICROS  20000   /* 50 aggiornamenti al secondo */

static struct Window *gGhostWin = NULL;
static struct Region *gGhostRegion = NULL;
static struct BitMap *gGhostBM = NULL;
static WORD gGhostW = 0, gGhostH = 0;
static WORD gGhostHotX = 0, gGhostHotY = 0;
static WORD gGhostLastX = -32768, gGhostLastY = -32768;

static struct Window *gFrameWin = NULL;
static struct Region *gFrameRegion = NULL;
static struct Screen *gFrameScreen = NULL;
static struct IBox gFrameBox;

static struct MsgPort *gTimerPort = NULL;
static struct TimeRequest *gTimerReq = NULL;
static BOOL gTimerOpen = FALSE;
static BOOL gTimerPending = FALSE;

/* ===== Timer ===== */

static void TimerSend(void)
{
    if (!gTimerOpen || gTimerPending) return;
    gTimerReq->Request.io_Command = TR_ADDREQUEST;
    gTimerReq->Time.Seconds = 0;
    gTimerReq->Time.Microseconds = DRAG_TICK_MICROS;
    IExec->SendIO((struct IORequest *)gTimerReq);
    gTimerPending = TRUE;
}

static BOOL TimerOpen(void)
{
    if (gTimerOpen) return TRUE;
    gTimerPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!gTimerPort) return FALSE;
    gTimerReq = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
        ASOIOR_Size, sizeof(struct TimeRequest),
        ASOIOR_ReplyPort, gTimerPort,
        TAG_DONE);
    if (gTimerReq &&
        IExec->OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)gTimerReq, 0) == 0)
    {
        gTimerOpen = TRUE;
        return TRUE;
    }
    if (gTimerReq) IExec->FreeSysObject(ASOT_IOREQUEST, gTimerReq);
    gTimerReq = NULL;
    IExec->FreeSysObject(ASOT_PORT, gTimerPort);
    gTimerPort = NULL;
    return FALSE;
}

static void TimerClose(void)
{
    if (gTimerOpen)
    {
        if (gTimerPending)
        {
            if (!IExec->CheckIO((struct IORequest *)gTimerReq))
                IExec->AbortIO((struct IORequest *)gTimerReq);
            IExec->WaitIO((struct IORequest *)gTimerReq);
            gTimerPending = FALSE;
        }
        IExec->CloseDevice((struct IORequest *)gTimerReq);
        gTimerOpen = FALSE;
    }
    if (gTimerReq)
    {
        IExec->FreeSysObject(ASOT_IOREQUEST, gTimerReq);
        gTimerReq = NULL;
    }
    if (gTimerPort)
    {
        IExec->FreeSysObject(ASOT_PORT, gTimerPort);
        gTimerPort = NULL;
    }
}

ULONG DragTimerSignal(void)
{
    return (gTimerOpen && gTimerPort) ? (1UL << gTimerPort->mp_SigBit) : 0;
}

/* TRUE se il timer e' scaduto; la richiesta viene raccolta. */
BOOL DragTimerFired(void)
{
    if (!gTimerOpen || !gTimerPending) return FALSE;
    if (!IExec->CheckIO((struct IORequest *)gTimerReq)) return FALSE;
    IExec->WaitIO((struct IORequest *)gTimerReq);
    gTimerPending = FALSE;
    return TRUE;
}

void DragTimerRearm(void)
{
    TimerSend();
}

/* ===== Fantasma delle icone ===== */

static void FreeGhost(void)
{
    if (gGhostWin)
    {
        IIntuition->CloseWindow(gGhostWin);
        gGhostWin = NULL;
    }
    if (gGhostRegion)
    {
        IGraphics->DisposeRegion(gGhostRegion);
        gGhostRegion = NULL;
    }
    if (gGhostBM)
    {
        IGraphics->FreeBitMap(gGhostBM);
        gGhostBM = NULL;
    }
    gGhostW = gGhostH = 0;
    gGhostLastX = gGhostLastY = -32768;
}

static void DrawGhostIcons(struct RastPort *rp, struct DiskObject **icons,
                           CONST_STRPTR *labels, const struct Rectangle *rects,
                           LONG n, ULONG bgColor)
{
    LONG i;

    IGraphics->RectFillColor(rp, 0, 0, gGhostW - 1, gGhostH - 1, bgColor);
    /* dall'ultima alla prima: la prima icona resta davanti */
    for (i = n - 1; i >= 0; i--)
    {
        IIcon->DrawIconState(rp, icons[i], labels[i],
            i * GHOST_STEP_X - rects[i].MinX,
            i * GHOST_STEP_Y - rects[i].MinY,
            IDS_NORMAL,
            ICONDRAWA_Borderless, TRUE,
            ICONDRAWA_Frameless, TRUE,
            ICONDRAWA_EraseBackground, FALSE,
            ICONDRAWA_EraseLabelBackground, FALSE,
            TAG_DONE);
    }
}

/* Costruisce l'immagine e la sagoma del fantasma.  Le icone vengono
   disegnate due volte, su fondo nero e su fondo bianco: i pixel uguali
   nei due casi appartengono alle icone, quelli diversi sono il fondo.
   Cosi' la sagoma funziona con icone a colori, PNG e su schermi a
   palette, senza leggere le maschere interne delle icone. */
static BOOL BuildGhost(struct Window *win, struct DiskObject **icons,
                       CONST_STRPTR *labels, LONG n)
{
    struct Screen *scr = win->WScreen;
    struct Rectangle rects[GHOST_MAX_ICONS];
    struct RastPort rp;
    ULONG *pix1 = NULL, *pix2 = NULL;
    LONG i, w = 0, h = 0, x, y;
    ULONG depth;
    BOOL ok = FALSE;

    if (!scr || n <= 0) return FALSE;

    IGraphics->InitRastPort(&rp);
    if (win->RPort && win->RPort->Font)
        IGraphics->SetFont(&rp, win->RPort->Font);

    for (i = 0; i < n; i++)
    {
        LONG iw, ih;
        if (!IIcon->GetIconRectangle(&rp, icons[i], labels[i], &rects[i],
                ICONDRAWA_Borderless, TRUE,
                ICONDRAWA_Frameless, TRUE,
                TAG_DONE))
            return FALSE;
        iw = rects[i].MaxX - rects[i].MinX + 1;
        ih = rects[i].MaxY - rects[i].MinY + 1;
        if (i * GHOST_STEP_X + iw > w) w = i * GHOST_STEP_X + iw;
        if (i * GHOST_STEP_Y + ih > h) h = i * GHOST_STEP_Y + ih;
    }
    if (w <= 0 || h <= 0) return FALSE;
    if (w > GHOST_MAX_W) w = GHOST_MAX_W;
    if (h > GHOST_MAX_H) h = GHOST_MAX_H;
    gGhostW = (WORD)w;
    gGhostH = (WORD)h;

    /* punto di presa: centro della prima icona, come nel Workbench */
    gGhostHotX = (WORD)((rects[0].MaxX - rects[0].MinX + 1) / 2);
    gGhostHotY = (WORD)((rects[0].MaxY - rects[0].MinY + 1) / 2);

    depth = IGraphics->GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    gGhostBM = IGraphics->AllocBitMap(w, h, depth, BMF_CLEAR, scr->RastPort.BitMap);
    pix1 = (ULONG *)IExec->AllocVecTags(w * h * 4, AVT_Type, MEMF_PRIVATE, TAG_DONE);
    pix2 = (ULONG *)IExec->AllocVecTags(w * h * 4, AVT_Type, MEMF_PRIVATE, TAG_DONE);
    gGhostRegion = IGraphics->NewRegion();
    if (!gGhostBM || !pix1 || !pix2 || !gGhostRegion) goto out;

    rp.BitMap = gGhostBM;
    DrawGhostIcons(&rp, icons, labels, rects, n, 0xFF000000);
    IGraphics->ReadPixelArray(&rp, 0, 0, (uint8 *)pix1, 0, 0, w * 4,
                              PIXF_A8R8G8B8, w, h);
    DrawGhostIcons(&rp, icons, labels, rects, n, 0xFFFFFFFF);
    IGraphics->ReadPixelArray(&rp, 0, 0, (uint8 *)pix2, 0, 0, w * 4,
                              PIXF_A8R8G8B8, w, h);

    /* sagoma: per ogni riga, unisce le sequenze di pixel opachi */
    for (y = 0; y < h; y++)
    {
        const ULONG *r1 = pix1 + y * w;
        const ULONG *r2 = pix2 + y * w;
        x = 0;
        while (x < w)
        {
            LONG start;
            while (x < w && ((r1[x] ^ r2[x]) & 0x00FFFFFF)) x++;
            if (x >= w) break;
            start = x;
            while (x < w && !((r1[x] ^ r2[x]) & 0x00FFFFFF)) x++;
            {
                struct Rectangle rr;
                rr.MinX = (WORD)start;
                rr.MaxX = (WORD)(x - 1);
                rr.MinY = (WORD)y;
                rr.MaxY = (WORD)y;
                IGraphics->OrRectRegion(gGhostRegion, &rr);
            }
        }
    }
    ok = (gGhostRegion->RegionRectangle != NULL);

out:
    if (pix1) IExec->FreeVec(pix1);
    if (pix2) IExec->FreeVec(pix2);
    if (!ok) FreeGhost();
    return ok;
}

static BOOL OpenGhostWindow(struct Window *win, WORD sx, WORD sy)
{
    gGhostWin = IIntuition->OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)win->WScreen,
        WA_Left, (LONG)(sx - gGhostHotX),
        WA_Top, (LONG)(sy - gGhostHotY),
        WA_Width, (ULONG)gGhostW,
        WA_Height, (ULONG)gGhostH,
        WA_Borderless, TRUE,
        WA_Activate, FALSE,
        WA_ToolBox, TRUE,
        WA_StayTop, TRUE,
        WA_RMBTrap, TRUE,
        WA_SmartRefresh, TRUE,
        WA_NoCareRefresh, TRUE,
        WA_ShapeRegion, (ULONG)gGhostRegion,
        WA_Opaqueness, GHOST_OPAQUENESS,
        WA_OverrideOpaqueness, TRUE,
        WA_NoHitThreshold, 255,
        WA_DropShadows, FALSE,
        WA_IDCMP, 0,
        TAG_DONE);
    if (!gGhostWin) return FALSE;

    IGraphics->BltBitMapRastPort(gGhostBM, 0, 0, gGhostWin->RPort, 0, 0,
                                 gGhostW, gGhostH, 0xC0);
    gGhostLastX = sx;
    gGhostLastY = sy;
    return TRUE;
}

/* Raccoglie le icone degli elementi selezionati del pannello attivo. */
static LONG CollectGhostIcons(struct List *list, struct DiskObject **icons,
                              CONST_STRPTR *labels, BOOL *ownIcon, LONG max)
{
    struct Node *nd;
    LONG n = 0;

    for (nd = list->lh_Head; nd->ln_Succ && n < max; nd = nd->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;
        struct DiskObject *dobj;

        IListBrowser->GetListBrowserNodeAttrs(nd, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(nd, LBNA_UserData, &index,
            LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (index >= (ULONG)gEntryCount) continue;

        dobj = gIcons[index];
        ownIcon[n] = FALSE;
        if (!dobj)
        {
            dobj = IIcon->GetDefDiskObject(gIsDir[index] ? WBDRAWER : WBPROJECT);
            if (!dobj) continue;
            ownIcon[n] = TRUE;
        }
        icons[n] = dobj;
        labels[n] = name;
        n++;
    }
    return n;
}

/* ===== Cornice sulla destinazione ===== */

static void CloseFrame(void)
{
    if (gFrameWin)
    {
        IIntuition->CloseWindow(gFrameWin);
        gFrameWin = NULL;
    }
    if (gFrameRegion)
    {
        IGraphics->DisposeRegion(gFrameRegion);
        gFrameRegion = NULL;
    }
    gFrameScreen = NULL;
    memset(&gFrameBox, 0, sizeof(gFrameBox));
}

void DragVisualSetTarget(struct Screen *scr, const struct IBox *box)
{
    struct Rectangle r;
    struct DrawInfo *dri;
    WORD w, h;

    if (!scr || !box || box->Width < 2 * FRAME_THICK + 2 ||
        box->Height < 2 * FRAME_THICK + 2)
    {
        CloseFrame();
        return;
    }
    if (gFrameWin && scr == gFrameScreen &&
        box->Left == gFrameBox.Left && box->Top == gFrameBox.Top &&
        box->Width == gFrameBox.Width && box->Height == gFrameBox.Height)
        return;

    CloseFrame();
    w = box->Width;
    h = box->Height;

    gFrameRegion = IGraphics->NewRegion();
    if (!gFrameRegion) return;
    r.MinX = 0; r.MinY = 0; r.MaxX = w - 1; r.MaxY = h - 1;
    IGraphics->OrRectRegion(gFrameRegion, &r);
    r.MinX = FRAME_THICK; r.MinY = FRAME_THICK;
    r.MaxX = w - 1 - FRAME_THICK; r.MaxY = h - 1 - FRAME_THICK;
    IGraphics->ClearRectRegion(gFrameRegion, &r);

    gFrameWin = IIntuition->OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)scr,
        WA_Left, (LONG)box->Left,
        WA_Top, (LONG)box->Top,
        WA_Width, (ULONG)w,
        WA_Height, (ULONG)h,
        WA_Borderless, TRUE,
        WA_Activate, FALSE,
        WA_ToolBox, TRUE,
        WA_StayTop, TRUE,
        WA_RMBTrap, TRUE,
        WA_SmartRefresh, TRUE,
        WA_NoCareRefresh, TRUE,
        WA_ShapeRegion, (ULONG)gFrameRegion,
        WA_Opaqueness, 255,
        WA_OverrideOpaqueness, TRUE,
        WA_NoHitThreshold, 255,
        WA_DropShadows, FALSE,
        WA_IDCMP, 0,
        TAG_DONE);
    if (!gFrameWin)
    {
        IGraphics->DisposeRegion(gFrameRegion);
        gFrameRegion = NULL;
        return;
    }

    dri = IIntuition->GetScreenDrawInfo(scr);
    IGraphics->SetAPen(gFrameWin->RPort,
        dri ? (UBYTE)dri->dri_Pens[FILLPEN] : 3);
    IGraphics->RectFill(gFrameWin->RPort, 0, 0, w - 1, h - 1);
    if (dri) IIntuition->FreeScreenDrawInfo(scr, dri);

    gFrameScreen = scr;
    gFrameBox = *box;

    /* il fantasma resta davanti alla cornice */
    if (gGhostWin) IIntuition->WindowToFront(gGhostWin);
}

/* ===== API ===== */

BOOL IsDragOverlayWindow(struct Window *w)
{
    return (w && (w == gGhostWin || w == gFrameWin)) ? TRUE : FALSE;
}

BOOL DragVisualBegin(struct List *list, struct Window *win)
{
    struct DiskObject *icons[GHOST_MAX_ICONS];
    CONST_STRPTR labels[GHOST_MAX_ICONS];
    BOOL ownIcon[GHOST_MAX_ICONS];
    LONG n, i;
    BOOL ok = FALSE;

    DragVisualEnd();
    if (!list || !win || !win->WScreen) return FALSE;

    TimerOpen();

    n = CollectGhostIcons(list, icons, labels, ownIcon, GHOST_MAX_ICONS);
    if (n > 0 && BuildGhost(win, icons, labels, n))
        ok = OpenGhostWindow(win, win->WScreen->MouseX, win->WScreen->MouseY);
    if (!ok) FreeGhost();

    for (i = 0; i < n; i++)
        if (ownIcon[i]) IIcon->FreeDiskObject(icons[i]);

    TimerSend();
    DBG("drag visual: %ld icone, fantasma %s", (long)n, ok ? "aperto" : "non disponibile");
    return ok;
}

void DragVisualMove(WORD sx, WORD sy)
{
    if (!gGhostWin) return;
    if (sx == gGhostLastX && sy == gGhostLastY) return;
    gGhostLastX = sx;
    gGhostLastY = sy;
    IIntuition->ChangeWindowBox(gGhostWin,
        (WORD)(sx - gGhostHotX), (WORD)(sy - gGhostHotY), gGhostW, gGhostH);
}

void DragVisualEnd(void)
{
    CloseFrame();
    FreeGhost();
    TimerClose();
}
