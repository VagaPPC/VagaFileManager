/* ===== FileBrowser.h ===== */
/*
** File:        FileBrowser.h
** Description: FileBrowser gadget class: a listbrowser.gadget subclass that
**              adds an icon (cell) view mode. In the default details mode it
**              is the listbrowser, unchanged.
**
** OpenAmiga.org
** This file is part of the OpenAmiga "Listbrowser gadget with cell mode"
** project.
**
** For more information look at:
** http://openamiga.org/?function=viewproject&projectid=52
**
** Public domain: see LICENSE in the release archive.
**
** Change history:
**  2026-11-14 orgin, created
**  2026-11-14 orgin, FILEBROWSER_IconSize
**  2026-11-14 orgin, FILEBROWSER_IconSize sets a fixed image box
**  2026-11-15 orgin, 1.0: FILEBROWSER_VERSION/REVISION, public domain
**  2026-11-15 orgin, 1.1
**  2026-11-16 orgin, 1.2: scroller.gadget in icon mode, drawing clipped
**  2026-11-23 VFM: FILEBROWSER_GetIndexAt method
**  2026-11-23 VFM: FILEBROWSER_GetIndexAt returns the cell/row box
*/

#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include <intuition/classes.h>

/* The release this header belongs to (1.2) */
#define FILEBROWSER_VERSION  1
#define FILEBROWSER_REVISION 2

#define FILEBROWSER_Dummy        (TAG_USER + 0x46420000) /* 'FB' */

/* (uint32, ISG) FBVM_DETAILS (default, plain listbrowser) or FBVM_ICONS */
#define FILEBROWSER_ViewMode     (FILEBROWSER_Dummy + 1)

/* (uint32, ISG) Cell width in characters of the font in icon mode (default 14).
   Longer texts are cropped as "Long str..." */
#define FILEBROWSER_CellChars    (FILEBROWSER_Dummy + 2)

/* (uint32, ISG) Bit mask of list columns that are not shown in icon mode
   (bit n = column n). Default 0: every column is shown. */
#define FILEBROWSER_HiddenColumns (FILEBROWSER_Dummy + 3)

/* (int32, ISG) Pen used to fill the background in icon mode. Default -1: the
   background pen of the first node (custom pens) or BACKGROUNDPEN. */
#define FILEBROWSER_BackgroundPen (FILEBROWSER_Dummy + 4)

/* (struct Hook *, ISG) Icon mode: hook returning a full size icon for a node,
   drawn at its original size instead of the node's LBNCA_Image. Called as
   CallHookPkt(hook, gadget, node) and must return a struct DiskObject * that
   stays valid (owned by the application) or NULL to use the node's image.
   It may be called from input.device: no DOS I/O in the hook. */
#define FILEBROWSER_IconHook     (FILEBROWSER_Dummy + 5)

/* (uint32, ISG) Icon mode: size in pixels of the image box of every cell.
   Icons from FILEBROWSER_IconHook that are larger are drawn scaled down,
   keeping their aspect ratio; smaller ones are drawn at their original size,
   bottom aligned. The cells keep their size while icons load or change.
   Default 0: no fixed box, every icon at its original size and the box as
   large as the largest one shown. */
#define FILEBROWSER_IconSize     (FILEBROWSER_Dummy + 6)

/* (uint32, ISG) Icon mode: number of columns in the last layout (OM_GET).
   Used by VFM for keyboard navigation. */
#define FILEBROWSER_Columns     (FILEBROWSER_Dummy + 7)

/* (method) Icon mode: index of the cell under a window-relative point.
   The view must have been laid out (rendered) at least once.
   Returns -1 outside a cell or in details mode.  Used by VFM to find the
   drawer under the mouse during drag & drop, and by the context menu hook
   (fbgia_Node: the node shown at that position, NULL if none). */
#define FILEBROWSER_GetIndexAt  (FILEBROWSER_Dummy + 8)
struct FBGetIndexAt
{
 uint32 MethodID;
 int32  fbgia_X, fbgia_Y;
 int32  fbgia_FontY;   /* window font height, for details mode rows */
 int32  fbgia_Index;
 struct IBox fbgia_Box; /* out: cell (icons) or row (details) in window
                           coordinates, clipped to the visible area */
 struct Node *fbgia_Node; /* out: node at fbgia_Index, or NULL */
};

/* (BOOL, ISG) Details mode: draw a FILLPEN frame over the list border, to
   mark the active list (same color as the drag & drop target frame).
   Ignored in icon mode.  Default FALSE. */
#define FILEBROWSER_Highlight   (FILEBROWSER_Dummy + 9)

enum
{
 FBVM_DETAILS = 0,
 FBVM_ICONS   = 1
};

/*
** Create/free the class. It is private: create objects with
** NewObject(cl, NULL, ...) using the class pointer that
** MakeFileBrowserClass() returns. Every LISTBROWSER_* attribute works as
** with listbrowser.gadget.
*/
struct IClass *MakeFileBrowserClass(void);
void FreeFileBrowserClass(struct IClass *cl);

#endif /* FILEBROWSER_H */
