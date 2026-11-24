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
 * Modulo: DirList.c
 * -----------------
 * Modello delle directory: lettura e filtro delle voci, hook di disegno e
 * ordinamento, helper sui percorsi, notifiche del filesystem, navigazione,
 * titolo/barra di stato e selezione.
 */

#include "VagaFileManager.h"

/* ===== Hook di disegno e ordinamento =====
   Gli hook sono per-pannello: h_Data contiene l'indice del pannello.  Ogni
   volta che un hook viene invocato rendiamo attivo il pannello corretto,
   cosi' le macro gNames/gIcons/... puntano ai dati giusti anche con due
   pannelli aperti. */

static void ActivatePaneFromHook(struct Hook *hook)
{
    ULONG p = (ULONG)hook->h_Data;
    if (p < 2) gActivePane = (int)p;
}

uint32 IconHookFunc(struct Hook *hook, APTR gadget, APTR node)
{
    uint32 index = MAX_ENTRIES;
    (void)gadget;
    uint32 result = 0;
    int save = gActivePane;

    ActivatePaneFromHook(hook);
    IListBrowser->GetListBrowserNodeAttrs((struct Node *)node,
                                          LBNA_UserData, &index,
                                          TAG_DONE);
    if (index < MAX_ENTRIES)
        result = (uint32)gIcons[index];
    gActivePane = save;
    return result;
}

CONST_STRPTR TypeStringEx(struct ExamineData *ed)
{
    if (EXD_IS_LINK(ed))
    {
        if (EXD_IS_DIRECTORY(ed)) return (CONST_STRPTR)LS(MSG_TYPE_DIR_LINK);
        return (CONST_STRPTR)LS(MSG_TYPE_LINK);
    }
    if (EXD_IS_DIRECTORY(ed)) return (CONST_STRPTR)LS(MSG_TYPE_DRAWER);
    if (EXD_IS_FILE(ed)) return (CONST_STRPTR)LS(MSG_TYPE_FILE);
    if (EXD_IS_SOFTLINK(ed)) return (CONST_STRPTR)LS(MSG_TYPE_SOFTLINK);
    return (CONST_STRPTR)LS(MSG_TYPE_OTHER);
}

uint32 TypeRankEx(struct ExamineData *ed)
{
    if (EXD_IS_DIRECTORY(ed)) return 0;
    if (EXD_IS_FILE(ed)) return 1;
    if (EXD_IS_LINK(ed)) return 2;
    return 3;
}

void SizeStringEx(struct ExamineData *ed, STRPTR buf, ULONG bufSize)
{
    if (EXD_IS_FILE(ed) && !EXD_IS_DIRECTORY(ed))
        sprintf(buf, "%lld", (long long)ed->FileSize);
    else
    {
        strncpy(buf, "-", bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
}

void DateStringEx(struct ExamineData *ed, STRPTR buf, ULONG bufSize)
{
    struct DateTime dt;
    buf[0] = '\0';
    memset(&dt, 0, sizeof(dt));
    dt.dat_Stamp = ed->Date;
    dt.dat_Format = FORMAT_DOS;
    dt.dat_StrDate = buf;
    if (!IDOS->DateToStr(&dt))
    {
        strncpy(buf, "-", bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
}

int CompareDateStamp(const struct DateStamp *a, const struct DateStamp *b)
{
    if (a->ds_Days != b->ds_Days) return (a->ds_Days < b->ds_Days) ? -1 : 1;
    if (a->ds_Minute != b->ds_Minute) return (a->ds_Minute < b->ds_Minute) ? -1 : 1;
    if (a->ds_Tick != b->ds_Tick) return (a->ds_Tick < b->ds_Tick) ? -1 : 1;
    return 0;
}

int CompareEntries(const void *a, const void *b)
{
    const struct DirEntryInfo *ea = (const struct DirEntryInfo *)a;
    const struct DirEntryInfo *eb = (const struct DirEntryInfo *)b;
    int c;

    switch (gSortMode)
    {
        case SORT_BY_TYPE:
            if (ea->typeRank != eb->typeRank) return (ea->typeRank < eb->typeRank) ? -1 : 1;
            return IUtility->Stricmp(ea->name, eb->name);
        case SORT_BY_NAME:
            return IUtility->Stricmp(ea->name, eb->name);
        case SORT_BY_DATE:
            /* dal file più aggiornato (discendente) */
            c = CompareDateStamp(&eb->dateStamp, &ea->dateStamp);
            if (c != 0) return c;
            return IUtility->Stricmp(ea->name, eb->name);
        case SORT_BY_SIZE:
            /* dal file più grande (discendente) */
            if (ea->fileSize != eb->fileSize) return (ea->fileSize > eb->fileSize) ? -1 : 1;
            return IUtility->Stricmp(ea->name, eb->name);
        default:
            break;
    }
    return IUtility->Stricmp(ea->name, eb->name);
}

uint32 ListIconRenderHook(struct Hook *hook, struct Node *node, struct LBDrawMsg *msg)
{
    ULONG index = MAX_ENTRIES;
    struct DiskObject *dobj;
    struct Rectangle *b;
    int32 size, cy;
    int save;

    if (msg->lbdm_MethodID != LB_DRAW) return LBCB_UNKNOWN;

    save = gActivePane;
    ActivatePaneFromHook(hook);
    IListBrowser->GetListBrowserNodeAttrs(node, LBNA_UserData, &index, TAG_DONE);
    if (index >= (ULONG)gEntryCount) { gActivePane = save; return LBCB_OK; }
    dobj = gIcons[index];
    if (!dobj) { gActivePane = save; return LBCB_OK; }

    b = &msg->lbdm_Bounds;
    size = b->MaxY - b->MinY + 1;
    if (size < 8) size = 8;
    if (size > 24) size = 24;
    cy = b->MinY + ((b->MaxY - b->MinY + 1) - size) / 2;

    IIcon->DrawIconState(msg->lbdm_RastPort, dobj, NULL,
        b->MinX + 1, cy,
        msg->lbdm_State == LBR_SELECTED ? IDS_SELECTED : IDS_NORMAL,
        ICONDRAWA_Borderless, TRUE,
        ICONDRAWA_EraseBackground, FALSE,
        ICONDRAWA_Width, size,
        ICONDRAWA_Height, size,
        TAG_DONE);
    gActivePane = save;
    return LBCB_OK;
}

uint32 SortCompareHook(struct Hook *hook, APTR obj, struct LBSortMsg *msg)
{
    ULONG a = (ULONG)msg->lbsm_UserDataA;
    (void)obj;
    ULONG b = (ULONG)msg->lbsm_UserDataB;
    int32 c = 0;
    int save = gActivePane;

    ActivatePaneFromHook(hook);
    if (a >= (ULONG)gEntryCount || b >= (ULONG)gEntryCount) { gActivePane = save; return 0; }

    switch (msg->lbsm_Column)
    {
        case COL_NAME:
            c = IUtility->Stricmp(gNames[a], gNames[b]);
            break;
        case COL_TYPE:
            if (gTypeRank[a] != gTypeRank[b])
            {
                uint32 r = (uint32)((gTypeRank[a] < gTypeRank[b]) ? -1 : 1);
                gActivePane = save;
                return r;
            }
            c = IUtility->Stricmp(gNames[a], gNames[b]);
            break;
        case COL_SIZE:
            if (gFileSize[a] != gFileSize[b])
            {
                uint32 r = (uint32)((gFileSize[a] > gFileSize[b]) ? -1 : 1);
                gActivePane = save;
                return r;
            }
            c = IUtility->Stricmp(gNames[a], gNames[b]);
            break;
        case COL_DATE:
            c = CompareDateStamp(&gDateStamp[b], &gDateStamp[a]);
            if (c != 0) { gActivePane = save; return (uint32)c; }
            c = IUtility->Stricmp(gNames[a], gNames[b]);
            break;
        case COL_COMMENT:
            c = IUtility->Stricmp(gComment[a], gComment[b]);
            break;
        default:
            c = IUtility->Stricmp(gNames[a], gNames[b]);
            break;
    }
    gActivePane = save;
    if (c < 0) return (uint32)-1;
    if (c > 0) return 1;
    return 0;
}

/* ===== Percorsi e file .info ===== */

BOOL HasInfoFile(CONST_STRPTR fullPath)
{
    char infoPath[PATH_MAX_LEN + 8];
    BPTR lock;
    if (!fullPath) return FALSE;
    if (strlen(fullPath) + 5 >= sizeof(infoPath)) return FALSE;
    strcpy(infoPath, fullPath);
    strcat(infoPath, ".info");
    lock = IDOS->Lock(infoPath, SHARED_LOCK);
    if (lock) { IDOS->UnLock(lock); return TRUE; }
    return FALSE;
}

/* TRUE se nella cartella esiste almeno un file <qualcosa>.info: serve a
   decidere se, nella vista a icone, filtrare solo i file con icona. */
BOOL DirHasInfoFiles(CONST_STRPTR drawer)
{
    APTR ctx;
    struct ExamineData *ed;
    BOOL found = FALSE;

    if (!drawer || !drawer[0]) return FALSE;
    ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)drawer,
                                     EX_DataFields, EXF_NAME, TAG_END);
    if (!ctx) return FALSE;
    while ((ed = IDOS->ExamineDir(ctx)) != NULL)
    {
        if (PathIsInfo(ed->Name))
        {
            found = TRUE;
            break;
        }
    }
    IDOS->ReleaseDirContext(ctx);
    return found;
}

BOOL BuildChildPath(CONST_STRPTR currentPath, CONST_STRPTR name, STRPTR outPath, ULONG outSize)
{
    if (!currentPath || !name || !outPath || outSize == 0) return FALSE;
    strncpy(outPath, currentPath, outSize - 1);
    outPath[outSize - 1] = '\0';
    return IDOS->AddPart(outPath, name, outSize) ? TRUE : FALSE;
}

BOOL GetParentPath(CONST_STRPTR currentPath, STRPTR outPath, ULONG outSize)
{
    BPTR lock, parent;
    BOOL ok = FALSE;
    if (!currentPath || !outPath || outSize == 0) return FALSE;
    outPath[0] = '\0';

    /* Already at root */
    if (currentPath[0] == '\0') return FALSE;

    lock = IDOS->Lock(currentPath, SHARED_LOCK);
    if (lock)
    {
        parent = IDOS->ParentDir(lock);
        if (parent)
        {
            if (IDOS->NameFromLock(parent, outPath, outSize)) ok = TRUE;
            IDOS->UnLock(parent);
        }
        else
        {
            /* No parent (e.g. volume/assign root) -> go to devices/assigns list */
            outPath[0] = '\0';
            ok = TRUE;
        }
        IDOS->UnLock(lock);
    }
    return ok;
}

BOOL IsDirectoryPath(CONST_STRPTR path)
{
    BPTR lock;
    BOOL isDir = FALSE;
    if (!path) return FALSE;
    lock = IDOS->Lock(path, SHARED_LOCK);
    if (lock)
    {
        struct ExamineData *ed = IDOS->ExamineObjectTags(EX_FileLockInput, lock,
            EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
        if (ed)
        {
            isDir = EXD_IS_DIRECTORY(ed) ? TRUE : FALSE;
            IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
        }
        IDOS->UnLock(lock);
    }
    return isDir;
}

BOOL IsRootPath(CONST_STRPTR path)
{
    /* defence in depth: the label shown in the path gadget also means
       the root view */
    return (path == NULL || path[0] == '\0' ||
            strcmp(path, LS(MSG_ROOT_ASSIGNS)) == 0);
}

void FreeEntries(struct List *list)
{
    LONG i;
    if (list)
    {
        IListBrowser->FreeListBrowserList(list);
        IExec->NewList(list);
    }
    for (i = 0; i < gEntryCount; i++)
    {
        if (gIcons[i]) { IIcon->FreeDiskObject(gIcons[i]); gIcons[i] = NULL; }
        gIsDir[i] = FALSE;
        gNames[i][0] = '\0';
    }
    gEntryCount = 0;
}

BOOL DosListNameToC(BSTR bname, STRPTR out, ULONG outSize)
{
    UBYTE *b;
    ULONG len;
    if (!bname || !out || outSize < 2) return FALSE;
    b = (UBYTE *)BADDR(bname);
    if (!b) return FALSE;
    len = b[0];
    if (len + 2 > outSize) len = outSize - 2;
    IExec->CopyMem(b + 1, out, len);
    out[len] = ':';
    out[len + 1] = '\0';
    return TRUE;
}

BOOL RootNameExists(struct DirEntryInfo *entries, LONG count, CONST_STRPTR name)
{
    LONG i;
    for (i = 0; i < count; i++)
    {
        if (IUtility->Stricmp(entries[i].name, name) == 0) return TRUE;
    }
    return FALSE;
}

/* Filtro rapido: accetta le wildcard AmigaDOS (#?, ?, [], ~, ...) e
   anche le piu' comuni * e ?.  Gli asterischi vengono tradotti in #?
   prima di passare il pattern a dos.library.  Il pattern tokenizzato
   viene riusato per tutte le voci della stessa directory. */
static char gLastFilter[NAME_MAX_LEN];
static char gLastParsed[NAME_MAX_LEN * 4 + 8];
static BOOL gLastFilterValid = FALSE;

/* ===== Lettura e filtro delle directory ===== */

/* Buffer di lavoro riusato da ReadDirectory.  La directory viene riletta
   molto spesso (navigazione, refresh, filtro rapido); riallocare e
   azzerare ~500 KB a ogni lettura era sprecato.  Il buffer non viene
   azzerato: ReadDirectory scrive tutti i campi delle voci che usa. */
static struct DirEntryInfo *gDirScratch = NULL;

static struct DirEntryInfo *GetDirScratch(void)
{
    if (!gDirScratch)
        gDirScratch = (struct DirEntryInfo *)IExec->AllocVecTags(
            MAX_ENTRIES * sizeof(struct DirEntryInfo),
            AVT_Type, MEMF_PRIVATE, TAG_DONE);
    return gDirScratch;
}

void FreeDirectoryScratch(void)
{
    if (gDirScratch) { IExec->FreeVec(gDirScratch); gDirScratch = NULL; }
}

/* TRUE se il pattern contiene una wildcard (Amiga o *). */
BOOL VFMHasWildcard(CONST_STRPTR pattern)
{
    if (!pattern) return FALSE;
    for (; *pattern; pattern++)
        if (strchr("*?#[~'", *pattern)) return TRUE;
    return FALSE;
}

/* Traduce un pattern utente in un pattern dos.library e lo tokenizza.
   Accetta le wildcard AmigaDOS (#?, ?, [], ~, ...) e anche * (convertito
   in #?).  Senza wildcard il pattern diventa "contiene": #?testo#?.
   Usata dalla quick filter e da Find. */
BOOL VFMParsePattern(CONST_STRPTR pattern, STRPTR parsed, LONG parsedSize)
{
    char translated[NAME_MAX_LEN * 2 + 2];
    ULONG i, o = 0;
    BOOL inClass = FALSE, quote = FALSE, hasWildcard;

    if (!pattern || !parsed || parsedSize <= 0) return FALSE;

    hasWildcard = VFMHasWildcard(pattern);
    if (!hasWildcard)
    {
        translated[o++] = '#';
        translated[o++] = '?';
    }

    for (i = 0; pattern[i] && o + 3 < sizeof(translated); i++)
    {
        char c = pattern[i];

        if (quote)
        {
            translated[o++] = c;
            quote = FALSE;
            continue;
        }
        if (c == '\'')
        {
            translated[o++] = c;
            quote = TRUE;
            continue;
        }
        if (c == '[')
        {
            inClass = TRUE;
            translated[o++] = c;
            continue;
        }
        if (c == ']')
        {
            inClass = FALSE;
            translated[o++] = c;
            continue;
        }
        if (c == '*' && !inClass)
        {
            translated[o++] = '#';
            translated[o++] = '?';
        }
        else
        {
            translated[o++] = c;
        }
    }
    if (!hasWildcard) { translated[o++] = '#'; translated[o++] = '?'; }
    translated[o] = '\0';

    return (IDOS->ParsePatternNoCase(translated, parsed, parsedSize) >= 0) ? TRUE : FALSE;
}

static BOOL EntryMatchesFilter(CONST_STRPTR name)
{
    if (!gFilterText[0]) return TRUE;
    if (!name) return FALSE;

    if (IUtility->Stricmp(gLastFilter, gFilterText) != 0)
    {
        strncpy(gLastFilter, gFilterText, sizeof(gLastFilter) - 1);
        gLastFilter[sizeof(gLastFilter) - 1] = '\0';
        gLastFilterValid = VFMParsePattern(gFilterText, gLastParsed,
                                           (LONG)sizeof(gLastParsed));
    }

    if (!gLastFilterValid) return TRUE;
    return IDOS->MatchPatternNoCase(gLastParsed, name) ? TRUE : FALSE;
}

/* Legge il contenuto di una directory, applica il filtro della vista e
   riempie i nodi del ListBrowser.  Ritorna il numero di voci lette. */
LONG ReadDirectory(CONST_STRPTR drawer, struct List *list)
{
    APTR context;
    struct ExamineData *ed;
    struct DirEntryInfo *entries;
    LONG count = 0, i;

    FreeEntries(list);

    entries = GetDirScratch();
    if (!entries) return 0;

    if (IsRootPath(drawer))
    {
        /* Home view: mounted volumes + assigns (skips unmounted devices like CD0:) */
        ULONG flags = LDF_VOLUMES | LDF_ASSIGNS | LDF_READ;
        struct DosList *dl = IDOS->LockDosList(flags);
        while (count < MAX_ENTRIES && (dl = IDOS->NextDosEntry(dl, flags)) != NULL)
        {
            char nameBuf[NAME_MAX_LEN];
            CONST_STRPTR tstr = LS(MSG_TYPE_ASSIGN);
            if (!DosListNameToC(dl->dol_Name, nameBuf, sizeof(nameBuf))) continue;
            if (RootNameExists(entries, count, nameBuf)) continue;
            if (!EntryMatchesFilter(nameBuf)) continue;

            switch (dl->dol_Type)
            {
                case DLT_DEVICE:     tstr = LS(MSG_TYPE_DEVICE); break;
                case DLT_VOLUME:     tstr = LS(MSG_TYPE_VOLUME); break;
                case DLT_LOCK:       tstr = LS(MSG_TYPE_ASSIGN); break;
                case DLT_LATE:       tstr = LS(MSG_TYPE_LATE_ASSIGN); break;
                case DLT_NONBINDING: tstr = LS(MSG_TYPE_NONBINDING); break;
                default:             tstr = LS(MSG_TYPE_ASSIGN); break;
            }

            strncpy(entries[count].name, nameBuf, NAME_MAX_LEN - 1);
            entries[count].name[NAME_MAX_LEN - 1] = '\0';
            strncpy(entries[count].typeStr, tstr, sizeof(entries[count].typeStr) - 1);
            entries[count].typeStr[sizeof(entries[count].typeStr) - 1] = '\0';
            strncpy(entries[count].sizeStr, "-", sizeof(entries[count].sizeStr) - 1);
            entries[count].sizeStr[sizeof(entries[count].sizeStr) - 1] = '\0';
            strncpy(entries[count].dateStr, "-", sizeof(entries[count].dateStr) - 1);
            entries[count].dateStr[sizeof(entries[count].dateStr) - 1] = '\0';
            entries[count].isDir = TRUE;
            entries[count].hasInfo = FALSE;
            entries[count].fileSize = 0;
            entries[count].comment[0] = '\0';
            memset(&entries[count].dateStamp, 0, sizeof(entries[count].dateStamp));
            entries[count].typeRank = (dl->dol_Type == DLT_VOLUME) ? 0 : (dl->dol_Type == DLT_DEVICE ? 1 : 2);
            count++;
        }
        IDOS->UnLockDosList(flags);
    }
    else
    {
        context = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)drawer,
                                             EX_DataFields, EXF_NAME | EXF_SIZE | EXF_DATE | EXF_TYPE | EXF_COMMENT,
                                             TAG_END);
        if (context != NULL)
        {
            while (count < MAX_ENTRIES && (ed = IDOS->ExamineDir(context)) != NULL)
            {
                char fullPath[PATH_MAX_LEN];
                BOOL hasInfo, isInfo;

                isInfo = PathIsInfo(ed->Name);
                /* i .info si mostrano solo nella vista a lista, e solo se
                   l'utente ha attivato "Mostra file info" */
                if (isInfo && (!gShowInfoFiles || gViewMode == FBVM_ICONS))
                    continue;

                if (!EntryMatchesFilter(ed->Name)) continue;

                strncpy(fullPath, drawer, sizeof(fullPath) - 1);
                fullPath[sizeof(fullPath) - 1] = '\0';
                if (!IDOS->AddPart(fullPath, ed->Name, sizeof(fullPath))) continue;

                hasInfo = HasInfoFile(fullPath);
                /* filtro "per icone": mostra solo le voci (file O cassetti)
                   che hanno un'icona associata (<nome>.info).  Il caso
                   "nessuna icona nella cartella" e' gestito a monte. */
                if (gViewFilter == VIEW_FILTER_ICONS_ONLY && !hasInfo && !isInfo)
                    continue;

                strncpy(entries[count].name, ed->Name, NAME_MAX_LEN - 1);
                entries[count].name[NAME_MAX_LEN - 1] = '\0';
                strncpy(entries[count].typeStr, TypeStringEx(ed), sizeof(entries[count].typeStr) - 1);
                entries[count].typeStr[sizeof(entries[count].typeStr) - 1] = '\0';
                SizeStringEx(ed, entries[count].sizeStr, sizeof(entries[count].sizeStr));
                DateStringEx(ed, entries[count].dateStr, sizeof(entries[count].dateStr));
                if (ed->Comment && ed->Comment[0])
                {
                    strncpy(entries[count].comment, ed->Comment, COMMENT_MAX_LEN - 1);
                    entries[count].comment[COMMENT_MAX_LEN - 1] = '\0';
                }
                else
                    entries[count].comment[0] = '\0';
                entries[count].isDir = EXD_IS_DIRECTORY(ed) ? TRUE : FALSE;
                entries[count].hasInfo = hasInfo;
                entries[count].fileSize = ed->FileSize;
                entries[count].dateStamp = ed->Date;
                entries[count].typeRank = TypeRankEx(ed);
                count++;
            }
            IDOS->ReleaseDirContext(context);
        }
    }

    if (count > 1) qsort(entries, count, sizeof(struct DirEntryInfo), CompareEntries);

    for (i = 0; i < count; i++)
    {
        char itemPath[PATH_MAX_LEN];
        struct Node *node;

        if (IsRootPath(drawer))
        {
            strncpy(itemPath, entries[i].name, sizeof(itemPath)-1);
            itemPath[sizeof(itemPath)-1] = '\0';
        }
        else
        {
            strncpy(itemPath, drawer, sizeof(itemPath) - 1);
            itemPath[sizeof(itemPath) - 1] = '\0';
            if (!IDOS->AddPart(itemPath, entries[i].name, sizeof(itemPath))) continue;
        }

        {
            /* Per un file .info mostrato in elenco carica l'icona dal file
               stesso: GetIconTags("x") apre "x.info". */
            if (!entries[i].isDir && PathIsInfo(itemPath))
                itemPath[strlen(itemPath) - 5] = '\0';
        }

        /* Carica l'icona; se il file .info e' appena stato riscritto (es. dopo
           aver modificato i tooltype nella finestra Informazioni) puo' essere
           temporaneamente non disponibile: riprova un paio di volte. */
        {
            int tries;
            gIcons[gEntryCount] = NULL;
            for (tries = 0; tries < 3; tries++)
            {
                gIcons[gEntryCount] = IIcon->GetIconTags(itemPath, ICONGETA_FailIfUnavailable, FALSE, TAG_DONE);
                if (gIcons[gEntryCount]) break;
                if (!entries[i].hasInfo) break;   /* nessuna icona prevista */
                IDOS->Delay(1);
            }
        }
        gIsDir[gEntryCount] = entries[i].isDir;
        gFileSize[gEntryCount] = entries[i].fileSize;
        gDateStamp[gEntryCount] = entries[i].dateStamp;
        gTypeRank[gEntryCount] = entries[i].typeRank;
        strncpy(gComment[gEntryCount], entries[i].comment, COMMENT_MAX_LEN - 1);
        gComment[gEntryCount][COMMENT_MAX_LEN - 1] = '\0';
        strncpy(gNames[gEntryCount], entries[i].name, NAME_MAX_LEN - 1);
        gNames[gEntryCount][NAME_MAX_LEN - 1] = '\0';

        node = IListBrowser->AllocListBrowserNode(NUM_COLS,
            LBNA_UserData, gEntryCount,
            LBNA_Column, COL_ICON, LBNCA_RenderHook, (ULONG)&gIconRenderHooks[gActivePane],
            LBNA_Column, COL_NAME, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)entries[i].name,
            LBNA_Column, COL_TYPE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)entries[i].typeStr,
            LBNA_Column, COL_SIZE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)entries[i].sizeStr, LBNCA_Justification, LCJ_RIGHT,
            LBNA_Column, COL_DATE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)entries[i].dateStr,
            LBNA_Column, COL_COMMENT, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)entries[i].comment,
            TAG_DONE);

        if (!node)
        {
            if (gIcons[gEntryCount]) { IIcon->FreeDiskObject(gIcons[gEntryCount]); gIcons[gEntryCount] = NULL; }
            gIsDir[gEntryCount] = FALSE; gNames[gEntryCount][0] = '\0';
            break;
        }
        IExec->AddTail(list, node);
        gEntryCount++;
    }

    return gEntryCount;
}

/* ===== Notifiche del filesystem ===== */

void SetupNotify(CONST_STRPTR path)
{
    if (gNotifyActive)
    {
        IDOS->EndNotify(&gNotify);
        gNotifyActive = FALSE;
    }
    memset(&gNotify, 0, sizeof(gNotify));
    if (!path || path[0] == '\0') return;   /* root view: nothing to watch */
    if (!gNotifyPort) return;
    /* DOS keeps the nr_Name pointer until EndNotify: copy into a persistent
       buffer, since callers often pass a stack-local path. */
    strncpy(gNotifyPath, path, sizeof(gNotifyPath) - 1);
    gNotifyPath[sizeof(gNotifyPath) - 1] = '\0';
    gNotify.nr_Name = gNotifyPath;
    gNotify.nr_Flags = NRF_SEND_MESSAGE;
    gNotify.nr_stuff.nr_Msg.nr_Port = gNotifyPort;
    if (IDOS->StartNotify(&gNotify) == DOSTRUE)
        gNotifyActive = TRUE;
}

void UpdateWindowTitle(struct Window *win, CONST_STRPTR path)
{
    CONST_STRPTR p, name;
    if (!win) return;
    if (!path || path[0] == '\0')
    {
        snprintf(gWinTitle, sizeof(gWinTitle), "%s", LS(MSG_APP_TITLE));
    }
    else
    {
        /* last component after the last '/' or ':' */
        name = path;
        for (p = path; *p; p++)
        {
            if (*p == '/' || *p == ':') name = p + 1;
        }
        if (name[0] == '\0') name = path;  /* volume root like "RAM Disk:" */
        snprintf(gWinTitle, sizeof(gWinTitle), "%s", name);
    }
    /* lascia invariato il titolo della schermata (barra titolo del Workbench) */
    IIntuition->SetWindowTitles(win, gWinTitle, (STRPTR)~0);
}

void SetPathGadget(Object *pathGad, struct Window *win, CONST_STRPTR path)
{
    CONST_STRPTR display;
    if (!pathGad) return;
    if (!path) path = "";
    strncpy(gPathBuffer, path, sizeof(gPathBuffer) - 1);
    gPathBuffer[sizeof(gPathBuffer) - 1] = '\0';
    display = (gPathBuffer[0] == '\0') ? LS(MSG_ROOT_ASSIGNS) : gPathBuffer;
    if (win)
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)pathGad, win, NULL,
            STRINGA_TextVal, (ULONG)display, STRINGA_BufferPos, strlen(display), TAG_DONE);
    else
        IIntuition->SetAttrs(pathGad, STRINGA_TextVal, (ULONG)display, STRINGA_BufferPos, strlen(display), TAG_DONE);
}

static void UpdateGadgetDisabled(Object *gad, struct Window *win, BOOL disabled)
{
    if (!gad) return;
    if (win)
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)gad, win, NULL,
            GA_Disabled, disabled ? TRUE : FALSE, TAG_DONE);
    else
        IIntuition->SetAttrs(gad, GA_Disabled, disabled ? TRUE : FALSE, TAG_DONE);
}

/* ===== Titolo, percorso e barra di stato ===== */

/* Aggiorna la barra di stato del pannello attivo (numero di voci,
   selezione e dimensione totale). */
void UpdateStatus(Object *listGad, Object *statusGad, struct Window *win)
{
    ULONG numSel = 0;
    char body[96];
    static char statusBuf[2][104];
    char *buf;
    int pane = 0;

    if (gPanes[1].listGad && listGad == gPanes[1].listGad) pane = 1;
    buf = statusBuf[pane];

    if (listGad) IIntuition->GetAttr(LISTBROWSER_NumSelected, listGad, &numSel);
    snprintf(body, sizeof(body), LS(MSG_STATUS_FORMAT),
             (long)gPanes[pane].entryCount, (unsigned long)numSel);
    snprintf(buf, sizeof(statusBuf[pane]), "%s", body);
    if (statusGad)
    {
        if (win) IIntuition->RefreshSetGadgetAttrs((struct Gadget *)statusGad, win, NULL, GA_Text, (ULONG)buf, TAG_DONE);
        else IIntuition->SetAttrs(statusGad, GA_Text, (ULONG)buf, TAG_DONE);
    }

    /* Ghost dei pulsanti che agiscono sui file, in base alla selezione
       e al contenuto degli appunti. */
    {
        struct Pane *pn = &gPanes[pane];
        BOOL hasSel = (numSel > 0);
        BOOL hasClip;
        BOOL hasDir = FALSE;
        struct Node *n;

        /* Il clipboard condiviso e' gia' in memoria: il file viene riletto
           dal loop principale (attivazione finestra e ogni ~1 s) e da
           DoPaste.  Evitiamo I/O DOS a ogni aggiornamento della barra di
           stato (era la chiamata piu' frequente e piu' costosa). */
        hasClip = (gClipboardCount > 0 && gClipboardMode != CLIP_NONE);

        for (n = pn->fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
        {
            ULONG sel = FALSE, idx = MAX_ENTRIES;
            IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
            if (!sel) continue;
            IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &idx, TAG_DONE);
            if (idx < (ULONG)pn->entryCount && pn->isDir[idx]) { hasDir = TRUE; break; }
        }

        UpdateGadgetDisabled(pn->btnRename,    win, !hasSel);
        UpdateGadgetDisabled(pn->btnDelete,    win, !hasSel);
        UpdateGadgetDisabled(pn->btnDuplicate, win, !hasSel);
        UpdateGadgetDisabled(pn->btnCopy,      win, !hasSel);
        UpdateGadgetDisabled(pn->btnCut,       win, !hasSel);
        UpdateGadgetDisabled(pn->btnInfo,      win, !hasSel);
        UpdateGadgetDisabled(pn->btnPaste,     win, !hasClip);
        UpdateGadgetDisabled(pn->sizeGad,      win, !hasDir);
    }
}

/* ===== Navigazione ===== */

/* Cambia la directory del pannello attivo: aggiorna percorso, titolo,
   contenuto, barra di stato e notifiche. */
void LoadPath(CONST_STRPTR path, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    char pathCopy[PATH_MAX_LEN];
    CONST_STRPTR usePath;
    if (!path || !listGad || !list) return;
    /* path may point to the gadget buffer, which SetPathGadget rewrites:
       work on a copy */
    strncpy(pathCopy, path, sizeof(pathCopy) - 1);
    pathCopy[sizeof(pathCopy) - 1] = '\0';
    usePath = IsRootPath(pathCopy) ? "" : pathCopy;

    /* Se stiamo navigando verso un altro percorso:
       - se ha impostazioni salvate, hanno la precedenza;
       - altrimenti si applicano i default (icone: solo file con icona se
         nella cartella esiste almeno un .info, altrimenti tutti; lista: tutti). */
    if (IUtility->Stricmp(usePath, gPathBuffer) != 0)
    {
        if (!ApplyPathPrefs(gActivePane, usePath))
        {
            if (gViewMode == FBVM_ICONS)
                gViewFilter = DirHasInfoFiles(usePath) ? VIEW_FILTER_ICONS_ONLY
                                                       : VIEW_FILTER_ALL_FILES;
            else
                gViewFilter = VIEW_FILTER_ALL_FILES;
        }
    }

    /* la modalità icone/lista è un attributo del gadget, non basta
       cambiare gViewMode */
    if (listGad)
        SetViewMode(gViewMode, listGad, gPanes[gActivePane].toggleGad, win);

    if (win) IIntuition->SetGadgetAttrs((struct Gadget *)listGad, win, NULL, LISTBROWSER_Labels, ~0, TAG_DONE);
    else IIntuition->SetAttrs(listGad, LISTBROWSER_Labels, ~0, TAG_DONE);
    ReadDirectory(usePath, list);
    SetPathGadget(pathGad, win, usePath);
    RememberPathPrefs(usePath, gViewMode, gViewFilter, gSortMode, gShowInfoFiles, gIconSize);
    UpdateWindowTitle(win, usePath);
    SetupNotify(usePath);
    if (win) IIntuition->RefreshSetGadgetAttrs((struct Gadget *)listGad, win, NULL, LISTBROWSER_Labels, (ULONG)list, TAG_DONE);
    else IIntuition->SetAttrs(listGad, LISTBROWSER_Labels, (ULONG)list, TAG_DONE);
    UpdateStatus(listGad, statusGad, win);
}

void NavigateTo(CONST_STRPTR path, Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    gForwardCount = 0;
    LoadPath(path, listGad, pathGad, statusGad, win, list);
}

void GoUp(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    char parent[PATH_MAX_LEN];
    if (GetParentPath(gPathBuffer, parent, sizeof(parent)))
    {
        if (gForwardCount < MAX_HISTORY)
        {
            strncpy(gForwardStack[gForwardCount], gPathBuffer, PATH_MAX_LEN - 1);
            gForwardStack[gForwardCount][PATH_MAX_LEN - 1] = '\0';
            gForwardCount++;
        }
        LoadPath(parent, listGad, pathGad, statusGad, win, list);
    }
}

void GoForward(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    if (gForwardCount > 0)
    {
        gForwardCount--;
        LoadPath(gForwardStack[gForwardCount], listGad, pathGad, statusGad, win, list);
    }
}

/* Apre la voce selezionata.  Con newInstance TRUE un cassetto (o un
   volume nella vista radice) viene aperto in una nuova istanza di VFM;
   con FALSE si naviga nella finestra corrente.  I file vengono sempre
   aperti tramite Workbench. */
BOOL TryOpenSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list, BOOL newInstance)
{
    struct Node *node = NULL;
    ULONG index = MAX_ENTRIES;
    STRPTR name = NULL;
    char newPath[PATH_MAX_LEN];

    /* In icon mode LISTBROWSER_SelectedNode puo' essere obsoleto dopo una
       selezione fatta via menu: usa il primo nodo selezionato. */
    node = GetFirstSelectedNode(list);
    if (!node) return FALSE;
    IListBrowser->GetListBrowserNodeAttrs(node, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
    if (!name || index >= (ULONG)gEntryCount) return FALSE;

    if (gPathBuffer[0] == '\0')
    {
        /* Root view: entries are device/assign names like SYS: */
        strncpy(newPath, name, sizeof(newPath)-1);
        newPath[sizeof(newPath)-1] = '\0';
    }
    else
    {
        if (!BuildChildPath(gPathBuffer, name, newPath, sizeof(newPath))) return FALSE;
    }

    if (!gIsDir[index])
    {
        /* file: launch it exactly like Workbench (via its default tool) */
        if (WorkbenchBase && IWorkbench)
        {
            DBG("launch '%s' via Workbench", newPath);
            IWorkbench->OpenWorkbenchObjectA(newPath, NULL);
        }
        else
        {
            DBG("cannot launch '%s': workbench.library not available", newPath);
        }
        return TRUE;
    }

    if (newInstance)
    {
        DBG("open drawer '%s' in a new instance", newPath);
        LaunchInstance(newPath, TRUE);
        return TRUE;
    }

    NavigateTo(newPath, listGad, pathGad, statusGad, win, list);
    return TRUE;
}

/* Oltre questo numero di file, Enter chiede conferma prima di lanciarli. */
#define OPEN_MANY_CONFIRM_LIMIT 10

/* Enter sulla lista: con una sola voce selezionata si comporta come il
   doppio click.  Con piu' voci lancia tutti i file selezionati via
   Workbench (i cassetti vengono ignorati: si entra in un cassetto solo
   se e' l'unica voce selezionata).  Oltre OPEN_MANY_CONFIRM_LIMIT file
   chiede conferma. */
BOOL OpenSelectedEntries(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    struct Node *n;
    LONG selCount, fileCount = 0;
    char newPath[PATH_MAX_LEN];

    selCount = CountSelectedEntries(list);
    if (selCount <= 0) return FALSE;
    if (selCount == 1)
        return TryOpenSelected(listGad, pathGad, statusGad, win, list, FALSE);

    /* prima passata: conta i file (non i cassetti) */
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, LBNA_UserData, &index, TAG_DONE);
        if (sel && index < (ULONG)gEntryCount && !gIsDir[index]) fileCount++;
    }
    if (fileCount == 0) return FALSE;
    if (!WorkbenchBase || !IWorkbench)
    {
        DBG("cannot launch files: workbench.library not available");
        return FALSE;
    }

    if (fileCount > OPEN_MANY_CONFIRM_LIMIT)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), LS(MSG_OPEN_MANY_CONFIRM), (long)fileCount);
        if (!ConfirmRequest(LS(MSG_BTN_OPEN), msg)) return FALSE;
    }

    /* seconda passata: lancia i file */
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;

        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, LBNA_UserData, &index,
                                              LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!sel || !name || index >= (ULONG)gEntryCount || gIsDir[index]) continue;
        if (!BuildChildPath(gPathBuffer, name, newPath, sizeof(newPath))) continue;
        DBG("launch '%s' via Workbench", newPath);
        IWorkbench->OpenWorkbenchObjectA(newPath, NULL);
    }
    return TRUE;
}

/* ===== Selezione ===== */

LONG CountSelectedEntries(struct List *list)
{
    struct Node *n; LONG count = 0;
    if (!list) return 0;
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (sel) count++;
    }
    return count;
}

void SelectAllEntries(struct List *list)
{
    struct Node *n;
    if (!list) return;
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
        IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, TRUE, TAG_DONE);
}

void DeselectAllEntries(struct List *list)
{
    struct Node *n;
    if (!list) return;
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
        IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, FALSE, TAG_DONE);
}

void InvertSelectionEntries(struct List *list)
{
    struct Node *n;
    if (!list) return;
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Selected, sel ? FALSE : TRUE, TAG_DONE);
    }
}

CONST_STRPTR FilePart(CONST_STRPTR path)
{
    CONST_STRPTR p = path;
    CONST_STRPTR last = path;
    if (!path) return path;
    while (*p)
    {
        if (*p == '/' || *p == ':') last = p + 1;
        p++;
    }
    return last;
}

struct Node *GetFirstSelectedNode(struct List *list)
{
    struct Node *n;
    if (!list) return NULL;
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (sel) return n;
    }
    return NULL;
}
