/*
 * VagaFileManager - two-pane file manager for AmigaOS 4.1
 * Copyright (C) 2026 vagappc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef VAGAFILEMANAGER_VERSION_H
#define VAGAFILEMANAGER_VERSION_H

/* Versione applicazione e numero di build.
   Incrementare VFM_BUILD ad ogni build/rilascio. */
#define VFM_VERSION       "1.1"
#define VFM_BUILD         97
#define VFM_VERSION_DATE  "26.11.2026"

/* Versione del protocollo dei comandi (porta ARexx e canale plugin),
   restituita dal comando PROTOCOL.  Si incrementa solo quando un comando
   cambia o viene rimosso, o quando cambia il formato di una risposta;
   un comando nuovo non la cambia.  Le build che non conoscono PROTOCOL
   rispondono "Unknown command": per i plugin e' il protocollo 1.
     1  build fino alla 69 (senza PROTOCOL; le 63-69 erano di prova)
     2  build 70: niente operazioni sui file, SELECTED su file, sessioni */
#define VFM_PROTOCOL      2

/* Helper per trasformare VFM_BUILD in stringa. */
#define VFM_STR_(x) #x
#define VFM_STR(x)  VFM_STR_(x)

/* "1.1 build 84" e "1.1 build 84 (25.11.2026)".  Il numero di build
   non riparte con una nuova versione: e' un contatore unico, sempre
   crescente, a cui fanno riferimento CHANGELOG, commit e segnalazioni. */
#define VFM_VERSION_FULL  VFM_VERSION " build " VFM_STR(VFM_BUILD)
#define VFM_VERSION_LONG  VFM_VERSION_FULL " (" VFM_VERSION_DATE ")"

#endif /* VAGAFILEMANAGER_VERSION_H */
