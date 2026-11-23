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
 * AppWindowRegistry.h
 * -------------------
 * Elenco pubblico delle AppWindow del Workbench, tenuto dal primo
 * VagaFileManager avviato (AppWinRegistry.c) e letto da tutti.
 *
 * La struct AppWindow del Workbench e' privata: non esiste un'API per
 * sapere a quale porta il Workbench manda gli AppMessage di una finestra.
 * VFM intercetta AddAppWindow / RemoveAppWindow /
 * AddAppWindowDropZone / RemoveAppWindowDropZone e CloseWindow, e tiene
 * qui finestra, porta, ID, UserData e drop zone di ogni AppWindow.
 * Cosi' VFM puo' mandare alla finestra sotto il puntatore lo stesso
 * AppMessage che manderebbe il Workbench.
 *
 * Accesso: Forbid(); FindSemaphore(VFM_AWREG_NAME); ObtainSemaphoreShared();
 * Permit().  Il messaggio va spedito (PutMsg) TENENDO il semaforo: chi
 * chiama RemoveAppWindow attende il semaforo esclusivo, quindi la porta
 * non puo' sparire tra la ricerca e l'invio.
 */

#ifndef VFM_APPWINDOWREGISTRY_H
#define VFM_APPWINDOWREGISTRY_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <exec/ports.h>
#include <intuition/intuition.h>

#define VFM_AWREG_NAME     "VagaFileManager.AppWindows"
#define VFM_AWREG_VERSION  1

/* Drop zone di una AppWindow.  Le coordinate sono relative all'angolo
   in alto a sinistra della finestra (bordi compresi), come nei tag
   WBDZA_#?: i flag rel* dicono se il valore va sommato a larghezza o
   altezza della finestra. */
struct VFMAppZone
{
    struct MinNode node;
    APTR  dz;                 /* struct AppWindowDropZone * */
    ULONG id;
    ULONG userdata;
    WORD  left, top, width, height;
    UBYTE relRight, relBottom, relWidth, relHeight;
};

struct VFMAppWin
{
    struct MinNode node;
    APTR            aw;       /* struct AppWindow * */
    struct Window  *win;
    struct MsgPort *port;
    ULONG           id;
    ULONG           userdata;
    struct MinList  zones;    /* di struct VFMAppZone */
};

struct VFMAppWinRegistry
{
    struct SignalSemaphore sem;   /* sem.ss_Link.ln_Name = VFM_AWREG_NAME */
    ULONG                  version;
    struct MinList         windows;  /* di struct VFMAppWin */
};

#endif /* VFM_APPWINDOWREGISTRY_H */
