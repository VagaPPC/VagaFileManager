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
#define VFM_VERSION       "1.0"
#define VFM_BUILD         3
#define VFM_VERSION_DATE  "21.11.2026"

/* Helper per trasformare VFM_BUILD in stringa. */
#define VFM_STR_(x) #x
#define VFM_STR(x)  VFM_STR_(x)

/* "1.0 build 1" e "1.0 build 1 (21.11.2026)" */
#define VFM_VERSION_FULL  VFM_VERSION " build " VFM_STR(VFM_BUILD)
#define VFM_VERSION_LONG  VFM_VERSION_FULL " (" VFM_VERSION_DATE ")"

#endif /* VAGAFILEMANAGER_VERSION_H */
