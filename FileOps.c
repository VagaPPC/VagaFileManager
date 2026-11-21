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
 * Modulo: FileOps.c
 * -----------------
 * Operazioni sul filesystem: copia e spostamento (con gestione dei
 * conflitti), cancellazione in stile DOpus4, appunti condivisi, rinomina,
 * creazione di file/cassetti, calcolo dimensioni e ricerca/grep.
 */

#include "VagaFileManager.h"
#include <ctype.h>

static struct ExamineData *ExamineRawObject(CONST_STRPTR path, ULONG fields);
static BOOL IsLinkPath(CONST_STRPTR path);
static void ClearWriteDelete(CONST_STRPTR path);
static BOOL PathExists(CONST_STRPTR path);

/* ===== Percorsi e sicurezza ===== */

BOOL GetResolvedPath(CONST_STRPTR path, STRPTR out, ULONG outSize)
{
    BPTR lock;
    BOOL ok = FALSE;
    if (!path || !out || outSize == 0) return FALSE;
    out[0] = '\0';
    lock = IDOS->Lock(path, SHARED_LOCK);
    if (lock)
    {
        ok = IDOS->NameFromLock(lock, out, outSize) ? TRUE : FALSE;
        IDOS->UnLock(lock);
    }
    return ok;
}

/* CreateDir() restituisce un lock che VA rilasciato con UnLock().
   Creare la directory senza rilasciarlo lascia l'oggetto "in uso" (202)
   per tutta la vita del processo: era questo a impedire poi la cancellazione
   (anche da Workbench) delle cartelle copiate. */
BOOL CreateDirUnlocked(CONST_STRPTR path)
{
    BPTR lock;
    if (!path || !path[0]) return FALSE;
    lock = IDOS->CreateDir(path);
    if (!lock) return FALSE;
    IDOS->UnLock(lock);
    return TRUE;
}

BOOL IsPathSameOrInside(CONST_STRPTR path, CONST_STRPTR container)
{
    char p[PATH_MAX_LEN], c[PATH_MAX_LEN];
    ULONG cl;
    if (!GetResolvedPath(path, p, sizeof(p))) return FALSE;
    if (!GetResolvedPath(container, c, sizeof(c))) return FALSE;
    cl = strlen(c);
    if (IUtility->Strnicmp(p, c, cl) != 0) return FALSE;
    if (p[cl] == '\0') return TRUE;
    if (cl > 0 && (c[cl-1] == ':' || c[cl-1] == '/')) return TRUE;
    if (p[cl] == '/' || p[cl] == ':') return TRUE;
    return FALSE;
}

BOOL IsDestInsideSrcForCopy(CONST_STRPTR src, CONST_STRPTR dst)
{
    char srcRes[PATH_MAX_LEN], dstParentRes[PATH_MAX_LEN], dstParent[PATH_MAX_LEN];
    CONST_STRPTR base;
    ULONG sl;

    /* Resolve source; if can't, be conservative only for obvious cases */
    if (!GetResolvedPath(src, srcRes, sizeof(srcRes))) return FALSE;

    /* Determine destination parent to resolve (dst may not exist yet) */
    strncpy(dstParent, dst, sizeof(dstParent)-1);
    dstParent[sizeof(dstParent)-1] = '\0';
    base = FilePart(dstParent);
    if (base && base != dstParent)
    {
        /* truncate to parent path */
        dstParent[base - dstParent] = '\0';
        if (dstParent[0] == '\0') strcpy(dstParent, gPathBuffer);
    }
    else
    {
        strncpy(dstParent, gPathBuffer, sizeof(dstParent)-1);
        dstParent[sizeof(dstParent)-1] = '\0';
    }

    if (!GetResolvedPath(dstParent, dstParentRes, sizeof(dstParentRes)))
    {
        strncpy(dstParentRes, dstParent, sizeof(dstParentRes)-1);
        dstParentRes[sizeof(dstParentRes)-1] = '\0';
    }

    sl = strlen(srcRes);
    if (IUtility->Strnicmp(dstParentRes, srcRes, sl) != 0) return FALSE;
    if (dstParentRes[sl] == '\0') return TRUE;
    if (sl > 0 && (srcRes[sl-1] == ':' || srcRes[sl-1] == '/')) return TRUE;
    if (dstParentRes[sl] == '/' || dstParentRes[sl] == ':') return TRUE;
    return FALSE;
}

void CopyAttributes(CONST_STRPTR src, CONST_STRPTR dst)
{
    struct ExamineData *ed = IDOS->ExamineObjectTags(EX_StringNameInput, (ULONG)src,
        EX_DataFields, EXF_PROTECTION | EXF_DATE | EXF_COMMENT, TAG_END);
    if (ed)
    {
        IDOS->SetProtection(dst, ed->Protection);
        IDOS->SetDate(dst, &ed->Date);
        if (ed->Comment && ed->Comment[0])
            IDOS->SetComment(dst, ed->Comment);
        IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
    }
}

/* ===== Conteggio e copia ===== */

ULONG CountFilesRecursive(CONST_STRPTR src)
{
    ULONG count = 0;
    if (IsLinkPath(src)) return 1;
    if (IsDirectoryPath(src))
    {
        APTR ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)src, EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
        if (ctx)
        {
            struct ExamineData *ed;
            while ((ed = IDOS->ExamineDir(ctx)) != NULL)
            {
                char s[PATH_MAX_LEN];
                ULONG nlen = strlen(ed->Name);
                /* gli .info si copiano insieme al file/cassetto: non contano */
                if (nlen > 5 && IUtility->Stricmp(ed->Name + nlen - 5, ".info") == 0)
                    continue;
                if (!BuildChildPath(src, ed->Name, s, sizeof(s))) continue;
                if (EXD_IS_LINK(ed)) count++;
                else if (EXD_IS_DIRECTORY(ed)) count += CountFilesRecursive(s);
                else count++;
            }
            IDOS->ReleaseDirContext(ctx);
        }
    }
    else count = 1;
    return count;
}

/* Somma la dimensione (in byte) di tutti i file sotto 'src'. */
uint64 CountBytesRecursive(CONST_STRPTR src)
{
    uint64 bytes = 0;
    if (!src || !src[0]) return 0;
    if (IsLinkPath(src)) return 0;
    if (IsDirectoryPath(src))
    {
        APTR ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)src,
                    EX_DataFields, EXF_NAME | EXF_TYPE | EXF_SIZE, TAG_END);
        if (ctx)
        {
            struct ExamineData *ed;
            while ((ed = IDOS->ExamineDir(ctx)) != NULL)
            {
                char s[PATH_MAX_LEN];
                ULONG nlen = strlen(ed->Name);
                /* gli .info si copiano insieme al file/cassetto: non contano */
                if (nlen > 5 && IUtility->Stricmp(ed->Name + nlen - 5, ".info") == 0)
                    continue;
                if (!BuildChildPath(src, ed->Name, s, sizeof(s))) continue;
                if (EXD_IS_LINK(ed)) continue;
                if (EXD_IS_DIRECTORY(ed)) bytes += CountBytesRecursive(s);
                else if (ed->FileSize > 0) bytes += (uint64)ed->FileSize;
            }
            IDOS->ReleaseDirContext(ctx);
        }
    }
    else
    {
        struct ExamineData *ed = IDOS->ExamineObjectTags(EX_StringNameInput, (ULONG)src,
                    EX_DataFields, EXF_SIZE, TAG_END);
        if (ed)
        {
            if (ed->FileSize > 0) bytes = (uint64)ed->FileSize;
            IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
        }
    }
    return bytes;
}

BOOL CopySingleFile(CONST_STRPTR src, CONST_STRPTR dst)
{
    BPTR inF, outF;
    UBYTE *buf;
    LONG n, chunks;
    BOOL ok = FALSE;

    buf = (UBYTE *)IExec->AllocVecTags(65536, AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!buf) return FALSE;

    inF = IDOS->Open(src, MODE_OLDFILE);
    if (inF)
    {
        outF = IDOS->Open(dst, MODE_NEWFILE);
        if (outF)
        {
            ok = TRUE;
            chunks = 0;
            while ((n = IDOS->Read(inF, buf, 65536)) > 0)
            {
                if (IDOS->Write(outF, buf, n) != n) { ok = FALSE; break; }
                if (gProgActive)
                {
                    gProgBytesDone += (uint64)n;
                    if (++chunks >= 8)
                    {
                        chunks = 0;
                        ProgressUpdate();
                        ProgressPoll();
                        if (gProgCancel) { ok = FALSE; break; }
                    }
                }
            }
            if (n < 0) ok = FALSE;
            IDOS->Close(outF);
            if (!ok) IDOS->Delete(dst);
            else CopyAttributes(src, dst);
        }
        IDOS->Close(inF);
    }
    IExec->FreeVec(buf);
    return ok;
}

void CopyInfoFileIfPresent(CONST_STRPTR src, CONST_STRPTR dst)
{
    char srcInfo[PATH_MAX_LEN + 8], dstInfo[PATH_MAX_LEN + 8];
    BPTR lock;
    snprintf(srcInfo, sizeof(srcInfo), "%s.info", src);
    snprintf(dstInfo, sizeof(dstInfo), "%s.info", dst);
    lock = IDOS->Lock(srcInfo, SHARED_LOCK);
    if (lock)
    {
        IDOS->UnLock(lock);
        CopySingleFile(srcInfo, dstInfo);
    }
}

/* Sposta l'icona <src>.info su <dst>.info, se esiste.  Prova prima il
   rename (stesso volume); se la destinazione esiste gia' o il rename non
   e' possibile, copia sopra e cancella l'originale. */
BOOL MoveInfoFile(CONST_STRPTR src, CONST_STRPTR dst)
{
    char srcInfo[PATH_MAX_LEN + 8], dstInfo[PATH_MAX_LEN + 8];
    BPTR lock;

    if (!src || !src[0] || !dst || !dst[0]) return FALSE;
    snprintf(srcInfo, sizeof(srcInfo), "%s.info", src);
    snprintf(dstInfo, sizeof(dstInfo), "%s.info", dst);

    lock = IDOS->Lock(srcInfo, SHARED_LOCK);
    if (!lock) return FALSE;          /* nessuna icona */
    IDOS->UnLock(lock);

    if (IDOS->Rename(srcInfo, dstInfo)) return TRUE;
    if (CopySingleFile(srcInfo, dstInfo))
    {
        IDOS->Delete(srcInfo);
        return TRUE;
    }
    return FALSE;
}

BOOL CopyDirectoryRecursive(CONST_STRPTR src, CONST_STRPTR dst)
{
    APTR ctx;
    struct ExamineData *ed;
    BOOL ok = TRUE;
    char srcRes[PATH_MAX_LEN], dstRes[PATH_MAX_LEN];

    if (GetResolvedPath(src, srcRes, sizeof(srcRes)) && GetResolvedPath(dst, dstRes, sizeof(dstRes)))
    {
        if (IsPathSameOrInside(dstRes, srcRes)) return FALSE;
    }

    if (!CreateDirUnlocked(dst))
    {
        BPTR l = IDOS->Lock(dst, SHARED_LOCK);
        if (!l) return FALSE;
        IDOS->UnLock(l);
    }
    /* resolve dst after creation for comparison */
    if (GetResolvedPath(dst, dstRes, sizeof(dstRes))) { /* ok */ } else { dstRes[0]='\0'; }

    ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)src, EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
    if (!ctx) return FALSE;

    while (!gProgCancel && (ed = IDOS->ExamineDir(ctx)) != NULL)
    {
        char s[PATH_MAX_LEN], d[PATH_MAX_LEN], sRes[PATH_MAX_LEN];
        ULONG nlen = strlen(ed->Name);

        /* i file .info non si copiano qui: vengono copiati come icona
           insieme al file/cassetto a cui appartengono (CopyInfoFileIfPresent),
           altrimenti verrebbero copiati due volte e sembrerebbero "esistere
           gia'" al secondo passaggio. */
        if (nlen > 5 && IUtility->Stricmp(ed->Name + nlen - 5, ".info") == 0)
            continue;

        if (!BuildChildPath(src, ed->Name, s, sizeof(s))) { ok = FALSE; continue; }
        if (!BuildChildPath(dst, ed->Name, d, sizeof(d))) { ok = FALSE; continue; }
        /* skip if this entry is the destination directory itself */
        if (dstRes[0] && GetResolvedPath(s, sRes, sizeof(sRes)))
        {
            if (strcmp(sRes, dstRes) == 0) continue;
        }
        if (!CopyEntryRecursive(s, d)) ok = FALSE;
    }
    IDOS->ReleaseDirContext(ctx);
    CopyAttributes(src, dst);
    return ok;
}

/* ===== Gestione dei conflitti copia/sposta ===== */

/* 0 = chiedi per ogni file; altrimenti la scelta valida per tutti i
   successivi ("sovrascrivi tutti"/"aggiorna tutti"). */
static int  gConflictMode = CONFLICT_ASK;
/* TRUE se almeno un elemento e' stato saltato: in tal caso uno spostamento
   non deve cancellare il sorgente. */
static BOOL gCopySkipped = FALSE;
static int  gCopyDepth = 0;
/* destinazione risolta dell'elemento di primo livello (per MoveEntry). */
static char gCopyResolvedDst[PATH_MAX_LEN];

void ResetCopyConflict(void)
{
    gConflictMode = CONFLICT_ASK;
    gCopySkipped = FALSE;
    gCopyDepth = 0;
    gCopyResolvedDst[0] = '\0';
}

static BOOL FileIsNewer(CONST_STRPTR src, CONST_STRPTR dst)
{
    struct ExamineData *a, *b;
    BOOL newer = FALSE;

    a = IDOS->ExamineObjectTags(EX_StringNameInput, (ULONG)src,
                                EX_DataFields, EXF_DATE, TAG_END);
    b = IDOS->ExamineObjectTags(EX_StringNameInput, (ULONG)dst,
                                EX_DataFields, EXF_DATE, TAG_END);
    if (a && b)
        newer = (IDOS->CompareDates(&a->Date, &b->Date) > 0) ? TRUE : FALSE;
    else if (a && !b)
        newer = TRUE;
    if (a) IDOS->FreeDosObject(DOS_EXAMINEDATA, a);
    if (b) IDOS->FreeDosObject(DOS_EXAMINEDATA, b);
    return newer;
}

/* Cambia 'dst' in un nome libero nella stessa cartella (es. "file_1"). */
static void MakeUniqueDstName(STRPTR dst, ULONG dstSize)
{
    char dir[PATH_MAX_LEN];
    char baseName[NAME_MAX_LEN];
    CONST_STRPTR base;

    if (!dst || !dst[0]) return;
    strncpy(dir, dst, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    base = FilePart(dir);
    if (!base || !base[0]) return;
    strncpy(baseName, base, sizeof(baseName) - 1);
    baseName[sizeof(baseName) - 1] = '\0';
    if (base > dir)
        ((char *)base)[-1] = '\0';   /* taglia via il separatore */
    else
        dir[0] = '\0';
    if (dir[0] == '\0')
    {
        strncpy(dir, gPathBuffer, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
    }
    MakeUniqueDestName(dir, baseName, dst, dstSize);
}

/* TRUE se il nome (ultimo componente) termina con ".info". */
static BOOL PathIsInfo(CONST_STRPTR path)
{
    CONST_STRPTR base;
    ULONG len;

    if (!path) return FALSE;
    base = FilePart(path);
    len = strlen(base);
    return (len > 5 && IUtility->Stricmp(base + len - 5, ".info") == 0) ? TRUE : FALSE;
}

/* Decide cosa fare per una destinazione gia' esistente.
   Ritorna: >0 procedi (dst puo' essere stato rinominato), 0 salta, -1 annulla. */
static int ResolveCopyConflict(CONST_STRPTR src, STRPTR dst, ULONG dstSize)
{
    int choice = gConflictMode;
    LONG r;

    if (choice == CONFLICT_ASK)
    {
        CONST_STRPTR base = FilePart(dst);
        r = AskConflict((base && base[0]) ? base : dst);
        if (r <= 0) return -1;      /* finestra chiusa = annulla */
        choice = (int)r;
    }

    switch (choice)
    {
        case CONFLICT_OVERWRITE_ALL:
            gConflictMode = CONFLICT_OVERWRITE_ALL;
            return 1;
        case CONFLICT_UPDATE_ALL:
            gConflictMode = CONFLICT_UPDATE_ALL;
            return FileIsNewer(src, dst) ? 1 : 0;
        case CONFLICT_UPDATE:
            return FileIsNewer(src, dst) ? 1 : 0;
        case CONFLICT_RENAME:
            MakeUniqueDstName(dst, dstSize);
            return 1;
        case CONFLICT_SKIP:
            return 0;
        case CONFLICT_OVERWRITE:
        default:
            return 1;
    }
}

/* Copia una voce (file o directory) gestendo i conflitti con la
   destinazione.  Ritorna FALSE in caso di errore o di annullamento. */
BOOL CopyEntryRecursive(CONST_STRPTR src, CONST_STRPTR dst)
{
    char srcRes[PATH_MAX_LEN], dstRes[PATH_MAX_LEN];
    char dstPath[PATH_MAX_LEN];
    BOOL isDir, ok = FALSE;
    int top;

    if (!src || !dst) return FALSE;
    strncpy(dstPath, dst, sizeof(dstPath) - 1);
    dstPath[sizeof(dstPath) - 1] = '\0';
    isDir = IsDirectoryPath(src);
    top = (gCopyDepth == 0);
    gCopyDepth++;

    /* Un link non va seguito: copiamo il contenuto referenziato come file
       per non ricorrere nella directory target (e per evitare cicli). */
    if (IsLinkPath(src))
    {
        if (PathExists(dstPath) && !PathIsInfo(src))
        {
            int act = ResolveCopyConflict(src, dstPath, sizeof(dstPath));
            if (act < 0) { gProgCancel = TRUE; goto done; }
            if (act == 0) { gCopySkipped = TRUE; ProgressAdvance(); ok = TRUE; goto done; }
        }
        if (top)
        {
            strncpy(gCopyResolvedDst, dstPath, sizeof(gCopyResolvedDst) - 1);
            gCopyResolvedDst[sizeof(gCopyResolvedDst) - 1] = '\0';
        }
        ok = CopySingleFile(src, dstPath);
        if (ok) { CopyInfoFileIfPresent(src, dstPath); ProgressAdvance(); }
        goto done;
    }

    /* Prevent copying a directory into itself/subdir; una cartella
       "duplicata" nella stessa posizione prende invece un nome nuovo. */
    if (isDir && GetResolvedPath(src, srcRes, sizeof(srcRes)))
    {
        if (GetResolvedPath(dstPath, dstRes, sizeof(dstRes)))
        {
            if (strcmp(srcRes, dstRes) == 0)
                MakeUniqueDstName(dstPath, sizeof(dstPath));
            else if (IsPathSameOrInside(dstRes, srcRes))
                goto done;
        }
        else if (IsDestInsideSrcForCopy(src, dstPath))
        {
            goto done;
        }
    }

    if (!isDir && GetResolvedPath(src, srcRes, sizeof(srcRes)) &&
        GetResolvedPath(dstPath, dstRes, sizeof(dstRes)) &&
        strcmp(srcRes, dstRes) == 0)
    {
        /* copia nella stessa cartella: nome nuovo, senza chiedere */
        MakeUniqueDstName(dstPath, sizeof(dstPath));
    }
    else if (PathExists(dstPath) && !PathIsInfo(src))
    {
        /* due cartelle: unisci i contenuti senza chiedere */
        if (!(isDir && IsDirectoryPath(dstPath)))
        {
            int act = ResolveCopyConflict(src, dstPath, sizeof(dstPath));
            if (act < 0) { gProgCancel = TRUE; goto done; }
            if (act == 0) { gCopySkipped = TRUE; ProgressAdvance(); ok = TRUE; goto done; }
            /* se i tipi sono diversi, evita di cancellare una cartella */
            if (PathExists(dstPath) && isDir && !IsDirectoryPath(dstPath))
                IDOS->Delete(dstPath);
            else if (PathExists(dstPath) && !isDir && IsDirectoryPath(dstPath))
                MakeUniqueDstName(dstPath, sizeof(dstPath));
        }
    }

    if (top)
    {
        strncpy(gCopyResolvedDst, dstPath, sizeof(gCopyResolvedDst) - 1);
        gCopyResolvedDst[sizeof(gCopyResolvedDst) - 1] = '\0';
    }

    if (isDir)
    {
        ok = CopyDirectoryRecursive(src, dstPath);
        /* copia anche l'icona del cassetto stesso (dir.info), che non sta
           dentro la directory */
        if (ok) CopyInfoFileIfPresent(src, dstPath);
    }
    else
    {
        ok = CopySingleFile(src, dstPath);
        if (ok) { CopyInfoFileIfPresent(src, dstPath); ProgressAdvance(); }
    }

done:
    gCopyDepth--;
    return ok;
}

/* ===== Cancellazione (stile DOpus4) ===== */

void DeleteSiblingInfo(CONST_STRPTR path)
{
    char info[PATH_MAX_LEN + 8];
    if (!path) return;
    snprintf(info, sizeof(info), "%s.info", path);
    IDOS->Delete(info);   /* best effort: rimuove l'icona se presente */
}

/* progresso per la cancellazione: DeleteEntryRecursive e' usata anche da
   move/copy, quindi va attivato solo dal comando di cancellazione. */
static BOOL gDeleteProgress = FALSE;

/* Abilita l'avanzamento della barra durante DeleteEntryRecursive (usato dal
   comando di cancellazione, anche da ARexx). */
void SetDeleteProgress(BOOL on) { gDeleteProgress = on; }

static BOOL PathExists(CONST_STRPTR path)
{
    BPTR l;
    if (!path || !path[0]) return FALSE;
    l = IDOS->Lock(path, SHARED_LOCK);
    if (l) { IDOS->UnLock(l); return TRUE; }
    return FALSE;
}

/* ExamineObjectTags con EX_StringNameInput risolve i link; usiamo
   EX_RawStringNameInput (V54.54) per sapere se l'oggetto stesso e' un link. */
static struct ExamineData *ExamineRawObject(CONST_STRPTR path, ULONG fields)
{
    struct ExamineData *ed;
    if (!path || !path[0]) return NULL;
    ed = IDOS->ExamineObjectTags(EX_RawStringNameInput, (ULONG)path,
                                 EX_DataFields, fields, TAG_END);
    if (!ed)
        ed = IDOS->ExamineObjectTags(EX_StringNameInput, (ULONG)path,
                                     EX_DataFields, fields, TAG_END);
    return ed;
}

static BOOL IsLinkPath(CONST_STRPTR path)
{
    struct ExamineData *ed = ExamineRawObject(path, EXF_TYPE);
    BOOL link = FALSE;
    if (ed)
    {
        link = EXD_IS_LINK(ed) ? TRUE : FALSE;
        IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
    }
    return link;
}

/* Toglie solo i bit write/delete, preservando hidden/script/archive. */
static void ClearWriteDelete(CONST_STRPTR path)
{
    struct ExamineData *ed = ExamineRawObject(path, EXF_PROTECTION);
    if (ed)
    {
        IDOS->SetProtection(path, ed->Protection &
                            ~(EXDF_NO_WRITE | EXDF_NO_DELETE));
        IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
    }
}

static void DeleteProgressStep(void)
{
    if (gDeleteProgress && gProgActive) ProgressAdvance();
}

static void MakeParentWritable(CONST_STRPTR path);

/* Cancella un singolo oggetto. Come fa DOpus4 (delfile), su qualunque errore
   azzera la protezione e riprova; su ERROR_DIRECTORY_NOT_EMPTY esce subito,
   perche' il cassetto va svuotato prima dal chiamante. */
static BOOL DeleteEntry(CONST_STRPTR path)
{
    BOOL removed = FALSE;
    int attempt;

    for (attempt = 0; attempt < 12 && !removed; attempt++)
    {
        if (IDOS->Delete(path))
            removed = TRUE;
        else
        {
            LONG err = IDOS->IoErr();
            if (!PathExists(path))
                removed = TRUE;              /* gia' sparito */
            else if (err == ERROR_DIRECTORY_NOT_EMPTY)
                break;                       /* non vuoto: serve svuotarlo */
            else
            {
                DBG("delete: '%s' errore %ld, azzero protezione e riprovo (%d)",
                    path, (long)err, attempt);
                IDOS->SetProtection(path, 0);  /* unprotect, come DOpus4 */
                if (IDOS->Delete(path))
                    removed = TRUE;
                else if (!PathExists(path))
                    removed = TRUE;
                else
                    IDOS->Delay(2);
            }
        }
    }
    if (removed) DeleteProgressStep();
    return removed;
}

/* DOpus4 recursive_delete: svuota 'directory' cancellando ogni voce al suo
   interno (file e sottocassetti, questi ultimi dopo averli svuotati),
   SENZA cancellare 'directory' stessa: quella viene rimossa dal chiamante.
   Usa EX_DoCurrentDir,TRUE come DOpus4: ReleaseDirContext() ripristina la
   current directory e rilascia i lock temporanei. */
static BOOL DeleteDirectoryContents(CONST_STRPTR directory, ULONG depth)
{
    APTR context;
    BOOL ok = TRUE;

    if (gDeleteProgress && gProgCancel) return FALSE;

    /* rendi scrivibile/deletabile il cassetto PRIMA di aprirlo/scansionarlo */
    ClearWriteDelete(directory);

    context = IDOS->ObtainDirContextTags(EX_StringName, (ULONG)directory,
                                         EX_DoCurrentDir, TRUE,
                                         EX_DataFields, EXF_NAME | EXF_TYPE,
                                         TAG_END);
    if (!context) return TRUE;   /* non e' una cartella accessibile */

    while (1)
    {
        struct ExamineData *ed = IDOS->ExamineDir(context);
        char child[PATH_MAX_LEN];

        if (ed == NULL) break;
        if (gDeleteProgress && gProgCancel) { ok = FALSE; break; }
        if (!BuildChildPath(directory, ed->Name, child, sizeof(child)))
        { ok = FALSE; continue; }

        if (EXD_IS_DIRECTORY(ed) && !EXD_IS_LINK(ed))
        {
            if (depth + 1 < MAX_DELETE_DEPTH)
                DeleteDirectoryContents(child, depth + 1);
        }

        if (!DeleteEntry(child))
            ok = FALSE;
    }
    IDOS->ReleaseDirContext(context);
    return ok;
}

/* Cancella un file o un intero albero di directory aggiornando la
   progress bar. */
BOOL DeleteEntryRecursive(CONST_STRPTR src, ULONG depth)
{
    if (!src || !src[0]) return FALSE;
    if (gDeleteProgress && gProgCancel) return FALSE;

    /* DOpus4: prova SUBITO a cancellare. Delete() agisce direttamente sul
       link (non segue il target), quindi un link viene rimosso qui senza
       toccare il contenuto del suo bersaglio. Un cassetto vuoto non viene
       nemmeno aperto/scansionato: sul RAM Disk la scansione (ExamineDir)
       puo' lasciare un lock e causare ERROR_OBJECT_IN_USE (202) sul
       Delete successivo. */
    if (DeleteEntry(src))
        return TRUE;
    if (!PathExists(src))
        return TRUE;   /* gia' sparito */

    /* Cassetto non vuoto: rendi scrivibile il parent, svuota e riprova. */
    MakeParentWritable(src);
    ClearWriteDelete(src);

    if (depth < MAX_DELETE_DEPTH)
        DeleteDirectoryContents(src, depth);

    if (gDeleteProgress && gProgCancel) return FALSE;

    return DeleteEntry(src);
}

/* Per rimuovere una voce da un cassetto serve il permesso di scrittura
   sul cassetto stesso, non solo sull'oggetto. Le directory estratte da
   archivi (es. WB3.1) sono spesso read-only: le rendiamo scrivibili. */
static void MakeParentWritable(CONST_STRPTR path)
{
    char buf[PATH_MAX_LEN];
    char *p, *start;

    if (!path) return;
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* parte dopo il separatore di volume (es. "RAM Disk:") */
    start = strchr(buf, ':');
    start = start ? start + 1 : buf;

    /* rende scrivibili tutti i cassetti antenati, dall'alto verso il basso */
    for (p = start; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (buf[0])
                ClearWriteDelete(buf);
            *p = '/';
        }
    }
}

ULONG CountDeleteObjects(CONST_STRPTR src)
{
    ULONG count = 1;
    if (IsLinkPath(src)) return 1;
    if (IsDirectoryPath(src))
    {
        /* EX_DoCurrentDir come DOpus4: evita di lasciare lock pendenti sul
           RAM Disk (che causerebbero ERROR_OBJECT_IN_USE in cancellazione). */
        APTR ctx = IDOS->ObtainDirContextTags(EX_StringName, (ULONG)src,
                                              EX_DoCurrentDir, TRUE,
                                              EX_DataFields, EXF_NAME | EXF_TYPE,
                                              TAG_END);
        if (ctx)
        {
            struct ExamineData *ed;
            while ((ed = IDOS->ExamineDir(ctx)) != NULL)
            {
                char child[PATH_MAX_LEN];
                if (!BuildChildPath(src, ed->Name, child, sizeof(child))) continue;
                if (EXD_IS_LINK(ed)) count++;
                else count += CountDeleteObjects(child);
            }
            IDOS->ReleaseDirContext(ctx);
        }
    }
    return count;
}

/* ===== Spostamento e rinomina ===== */

/* Sposta src in dst: rename diretto sullo stesso device, altrimenti
   copia + cancellazione.  Gestisce i conflitti come la copia. */
BOOL MoveEntry(CONST_STRPTR src, CONST_STRPTR dst)
{
    if (IDOS->Rename(src, dst))
    {
        /* Rename sposta solo l'oggetto: l'icona <src>.info e' un file
           separato e va spostata anche lei. */
        MoveInfoFile(src, dst);
        if (gProgActive) { gProgDone += CountFilesRecursive(dst); ProgressUpdate(); ProgressPoll(); }
        return TRUE;
    }

    gCopySkipped = FALSE;
    gCopyResolvedDst[0] = '\0';
    if (CopyEntryRecursive(src, dst))
    {
        CONST_STRPTR finalDst = gCopyResolvedDst[0] ? gCopyResolvedDst : dst;

        /* se qualcosa e' stato saltato (conflitto), non eliminare il
           sorgente: resterebbe incompleto */
        if (gCopySkipped) return TRUE;

        /* CopyEntryRecursive copia anche l'icona; dopo aver rimosso
           l'originale eliminiamo la .info rimasta nel percorso sorgente. */
        if (DeleteEntryRecursive(src, 0))
        {
            MoveInfoFile(src, finalDst);
            return TRUE;
        }
        return FALSE;   /* copia riuscita ma sorgente non rimosso */
    }
    return FALSE;
}

BOOL MakeUniqueDestName(CONST_STRPTR dir, CONST_STRPTR baseName, STRPTR out, ULONG outSize)
{
    char candidate[PATH_MAX_LEN];
    BPTR l;
    LONG i;

    if (!BuildChildPath(dir, baseName, candidate, sizeof(candidate))) return FALSE;
    l = IDOS->Lock(candidate, SHARED_LOCK);
    if (!l) { strncpy(out, candidate, outSize - 1); out[outSize - 1] = '\0'; return TRUE; }
    IDOS->UnLock(l);

    for (i = 1; i < 1000; i++)
    {
        char tmpName[NAME_MAX_LEN];
        snprintf(tmpName, sizeof(tmpName), LS(MSG_COPY_PREFIX), (long)i, baseName);
        if (!BuildChildPath(dir, tmpName, candidate, sizeof(candidate))) continue;
        l = IDOS->Lock(candidate, SHARED_LOCK);
        if (!l) { strncpy(out, candidate, outSize - 1); out[outSize - 1] = '\0'; return TRUE; }
        IDOS->UnLock(l);
    }
    return FALSE;
}

void DoRenameSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    LONG selCount = CountSelectedEntries(list);
    struct Node *node; ULONG index = MAX_ENTRIES; STRPTR name = NULL;
    char oldPath[PATH_MAX_LEN], newPath[PATH_MAX_LEN], newName[NAME_MAX_LEN];

    if (selCount != 1) { ShowMessage(LS(MSG_RENAME), LS(MSG_RENAME_SELECT_ONE)); return; }
    node = GetFirstSelectedNode(list); if (!node) return;
    IListBrowser->GetListBrowserNodeAttrs(node, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
    if (!name || index >= (ULONG)gEntryCount) return;
    if (!BuildChildPath(gPathBuffer, name, oldPath, sizeof(oldPath))) return;

    if (PromptRename(name, newName, sizeof(newName)))
    {
        if (newName[0] == '\0' || strcmp(newName, name) == 0) return;
        if (!BuildChildPath(gPathBuffer, newName, newPath, sizeof(newPath))) return;
        if (!IDOS->Rename(oldPath, newPath))
        {
            char msg[256]; snprintf(msg, sizeof(msg), LS(MSG_RENAME_FAILED), name);
            ShowMessage(LS(MSG_ERROR_TITLE), msg);
        }
        else
            MoveInfoFile(oldPath, newPath);
        LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
    }
}

static void TrimNewName(char *s)
{
    char *p = s;
    ULONG n;

    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    n = (ULONG)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

void DoNewDrawer(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    char name[NAME_MAX_LEN];
    char path[PATH_MAX_LEN];

    if (IsRootPath(gPathBuffer)) return;
    name[0] = '\0';
    if (!PromptString(LS(MSG_NEW_DRAWER_PROMPT), name, sizeof(name))) return;
    TrimNewName(name);
    if (name[0] == '\0') return;
    if (!BuildChildPath(gPathBuffer, name, path, sizeof(path)))
    {
        ShowMessage(LS(MSG_MENU_NEW), LS(MSG_NEW_DRAWER_FAILED));
        return;
    }
    if (!CreateDirUnlocked(path))
    {
        ShowMessage(LS(MSG_MENU_NEW), LS(MSG_NEW_DRAWER_FAILED));
        return;
    }
    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
}

void DoNewTextFile(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    char name[NAME_MAX_LEN];
    char path[PATH_MAX_LEN];
    BPTR lock, fh;

    if (IsRootPath(gPathBuffer)) return;
    name[0] = '\0';
    if (!PromptString(LS(MSG_NEW_FILE_PROMPT), name, sizeof(name))) return;
    TrimNewName(name);
    if (name[0] == '\0') return;
    if (!BuildChildPath(gPathBuffer, name, path, sizeof(path)))
    {
        ShowMessage(LS(MSG_MENU_NEW), LS(MSG_NEW_FILE_FAILED));
        return;
    }

    /* non sovrascrivere un file esistente */
    lock = IDOS->Lock(path, SHARED_LOCK);
    if (lock)
    {
        IDOS->UnLock(lock);
        ShowMessage(LS(MSG_MENU_NEW), LS(MSG_NEW_FILE_EXISTS));
        return;
    }

    fh = IDOS->Open(path, MODE_NEWFILE);
    if (!fh)
    {
        ShowMessage(LS(MSG_MENU_NEW), LS(MSG_NEW_FILE_FAILED));
        return;
    }
    IDOS->Close(fh);
    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
}

void DoDeleteSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    LONG selCount = CountSelectedEntries(list);
    struct Node *n;
    char msg[128];
    ULONG total = 0;
    BOOL useProgress = FALSE;

    if (selCount <= 0) { ShowMessage(LS(MSG_DELETE), LS(MSG_DELETE_NONE)); return; }
    if (IsRootPath(gPathBuffer)) return;  /* non cancellare volumi/assign dalla vista radice */
    if (selCount == 1) snprintf(msg, sizeof(msg), "%s", LS(MSG_DELETE_CONFIRM_ONE));
    else snprintf(msg, sizeof(msg), LS(MSG_DELETE_CONFIRM_MANY), (long)selCount);
    if (!ConfirmRequest(LS(MSG_DELETE_CONFIRM_TITLE), msg)) return;

    /* conta gli oggetti (file + cassetti) per la barra di progressione */
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL; char fullPath[PATH_MAX_LEN];
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (BuildChildPath(gPathBuffer, name, fullPath, sizeof(fullPath)))
            total += CountDeleteObjects(fullPath);
    }

    ProgressOpen(win, total > 1 ? total : 1);
    gDeleteProgress = TRUE;
    useProgress = TRUE;

    /* Sospendi i NotifyRequest: un pannello aperto sulla directory da
       cancellare tiene un lock che impedisce il Delete finale. */
    {
        int p;
        for (p = 0; p < 2; p++)
        {
            if (gPanes[p].notifyActive)
            {
                IDOS->EndNotify(&gPanes[p].notify);
                gPanes[p].notifyActive = FALSE;
            }
        }
    }

    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL; char fullPath[PATH_MAX_LEN];
        BOOL ok;

        if (useProgress && gProgCancel) break;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (!BuildChildPath(gPathBuffer, name, fullPath, sizeof(fullPath))) continue;

        /* Libera l'icona caricata: puo' tenere aperto il file .info */
        if (index < (ULONG)gEntryCount && gIcons[index])
        {
            IIcon->FreeDiskObject(gIcons[index]);
            gIcons[index] = NULL;
        }

        ok = DeleteEntryRecursive(fullPath, 0);
        if (!ok && !PathExists(fullPath))
            ok = TRUE;  /* era gia' sparito: nessun errore da mostrare */
        if (ok)
            DeleteSiblingInfo(fullPath);
        else if (!(useProgress && gProgCancel))
        {
            char em[300];
            DBG("delete: '%s' ancora presente dopo i tentativi (errore %ld)",
                fullPath, (long)IDOS->IoErr());
            snprintf(em, sizeof(em), LS(MSG_DELETE_FAILED), name);
            ShowMessage(LS(MSG_ERROR_TITLE), em);
        }
    }

    gDeleteProgress = FALSE;
    if (useProgress) ProgressClose();

    /* Riattiva i NotifyRequest dei pannelli ancora validi. */
    {
        int p, save = gActivePane;
        for (p = 0; p < 2; p++)
        {
            if (gPanes[p].listGad)
            {
                gActivePane = p;
                if (gPanes[p].pathBuffer[0] == '\0' ||
                    PathExists(gPanes[p].pathBuffer))
                    SetupNotify(gPanes[p].pathBuffer);
            }
        }
        gActivePane = save;
    }

    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
}

void DoDuplicateSelected(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    struct Node *n; LONG selCount = CountSelectedEntries(list); BOOL anyErr = FALSE;
    BOOL useProgress = FALSE;
    ULONG total = 0;
    uint64 bytes = 0;

    if (IsRootPath(gPathBuffer)) { ShowMessage(LS(MSG_DUPLICATE), LS(MSG_DUPLICATE_ROOT)); return; }
    if (selCount <= 0) { ShowMessage(LS(MSG_DUPLICATE), LS(MSG_DUPLICATE_NONE)); return; }

    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL; char src[PATH_MAX_LEN];
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (BuildChildPath(gPathBuffer, name, src, sizeof(src)))
        {
            total += CountFilesRecursive(src);
            bytes += CountBytesRecursive(src);
        }
    }

    ProgressOpen(win, total);
    ProgressSetBytes(bytes);
    useProgress = TRUE;

    ResetCopyConflict();
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL;
        char src[PATH_MAX_LEN], dst[PATH_MAX_LEN];

        if (useProgress && gProgCancel) break;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (!BuildChildPath(gPathBuffer, name, src, sizeof(src))) continue;
        if (!BuildChildPath(gPathBuffer, name, dst, sizeof(dst)) || !CopyEntryRecursive(src, dst))
            anyErr = TRUE;
    }

    {
        BOOL canceled = gProgCancel;
        if (useProgress) ProgressClose();
        if (anyErr && !(useProgress && canceled))
            ShowMessage(LS(MSG_DUPLICATE), LS(MSG_DUPLICATE_ERRORS));
    }
    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
}

/* ===== Appunti condivisi ===== */

void SaveSharedClipboard(void)
{
    BPTR fh;
    LONG i;
    fh = IDOS->Open(SHARED_CLIP_PATH, MODE_NEWFILE);
    if (!fh) return;
    IDOS->FPrintf(fh, "MODE=%s\n", (gClipboardMode == CLIP_CUT) ? "CUT" : "COPY");
    for (i = 0; i < gClipboardCount; i++)
    {
        IDOS->FPrintf(fh, "%s\n", gClipboard[i]);
    }
    IDOS->Close(fh);
}

BOOL LoadSharedClipboard(void)
{
    BPTR fh;
    char line[PATH_MAX_LEN + 8];
    LONG count = 0;

    fh = IDOS->Open(SHARED_CLIP_PATH, MODE_OLDFILE);
    if (!fh) return FALSE;

    gClipboardMode = CLIP_COPY;
    if (IDOS->FGets(fh, line, sizeof(line)))
    {
        /* strip newline */
        ULONG len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) { line[--len] = '\0'; }
        if (strncmp(line, "MODE=CUT", 8) == 0) gClipboardMode = CLIP_CUT;
        else gClipboardMode = CLIP_COPY;
    }

    while (count < MAX_CLIPBOARD && IDOS->FGets(fh, line, sizeof(line)))
    {
        ULONG len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) { line[--len] = '\0'; }
        if (len == 0) continue;
        strncpy(gClipboard[count], line, PATH_MAX_LEN - 1);
        gClipboard[count][PATH_MAX_LEN - 1] = '\0';
        count++;
    }
    IDOS->Close(fh);
    gClipboardCount = count;
    return (count > 0);
}

void DoCopySelected(struct List *list)
{
    struct Node *n;
    gClipboardCount = 0; gClipboardMode = CLIP_COPY;
    for (n = list->lh_Head; n->ln_Succ && gClipboardCount < MAX_CLIPBOARD; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL; char full[PATH_MAX_LEN];
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (IsRootPath(gPathBuffer))
        {
            strncpy(full, name, sizeof(full)-1);
            full[sizeof(full)-1] = '\0';
        }
        else
        {
            if (!BuildChildPath(gPathBuffer, name, full, sizeof(full))) continue;
        }
        strncpy(gClipboard[gClipboardCount], full, PATH_MAX_LEN - 1);
        gClipboard[gClipboardCount][PATH_MAX_LEN - 1] = '\0';
        gClipboardCount++;
    }
    if (gClipboardCount == 0)
        ShowMessage(LS(MSG_COPY), LS(MSG_COPY_NONE));
    else
        SaveSharedClipboard();
}

void DoPaste(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    LONG i; BOOL anyErr = FALSE; ULONG total = 0;
    uint64 bytes = 0;
    BOOL moved[MAX_CLIPBOARD];
    memset(moved, 0, sizeof(moved));

    if (IsRootPath(gPathBuffer)) { ShowMessage(LS(MSG_PASTE), LS(MSG_PASTE_ROOT)); return; }
    if (gClipboardCount <= 0 || gClipboardMode == CLIP_NONE)
    {
        LoadSharedClipboard();
    }
    if (gClipboardCount <= 0 || gClipboardMode == CLIP_NONE)
    { ShowMessage(LS(MSG_PASTE), LS(MSG_PASTE_EMPTY)); return; }

    for (i = 0; i < gClipboardCount; i++)
    {
        total += CountFilesRecursive(gClipboard[i]);
        bytes += CountBytesRecursive(gClipboard[i]);
    }
    ProgressOpen(win, total);
    ProgressSetBytes(bytes);

    ResetCopyConflict();
    for (i = 0; i < gClipboardCount && !gProgCancel; i++)
    {
        char dst[PATH_MAX_LEN]; CONST_STRPTR base = FilePart(gClipboard[i]);
        if (!base || !base[0] || !BuildChildPath(gPathBuffer, base, dst, sizeof(dst)))
        { anyErr = TRUE; continue; }
        if (gClipboardMode == CLIP_COPY)
        {
            if (!CopyEntryRecursive(gClipboard[i], dst)) anyErr = TRUE;
        }
        else if (gClipboardMode == CLIP_CUT)
        {
            if (MoveEntry(gClipboard[i], dst))
            {
                if (!gCopySkipped) moved[i] = TRUE;   /* saltato: resta negli appunti */
            }
            else anyErr = TRUE;
        }
    }
    {
        BOOL canceled = gProgCancel;
        ProgressClose();
        if (gClipboardMode == CLIP_CUT)
        {
            /* rimuovi dal clipboard solo gli elementi spostati davvero */
            LONG w = 0;
            for (i = 0; i < gClipboardCount; i++)
            {
                if (moved[i]) continue;
                if (w != i) memmove(gClipboard[w], gClipboard[i], PATH_MAX_LEN);
                w++;
            }
            gClipboardCount = w;
            if (gClipboardCount == 0) gClipboardMode = CLIP_NONE;
            SaveSharedClipboard();
        }
        if (anyErr && !canceled) ShowMessage(LS(MSG_PASTE), LS(MSG_PASTE_ERRORS));
    }
    LoadPath(gPathBuffer, listGad, pathGad, statusGad, win, list);
}

void DoCutSelected(struct List *list)
{
    DoCopySelected(list);
    if (gClipboardCount > 0)
    {
        gClipboardMode = CLIP_CUT;
        SaveSharedClipboard();
    }
}

/* Copia o sposta gli elementi selezionati in 'srcList' (percorso 'srcDir')
   dentro 'dstDir' (l'altro pannello). Usata dalla barra superiore condivisa. */
void DoTransferToPane(struct List *srcList, CONST_STRPTR srcDir, CONST_STRPTR dstDir,
                      BOOL move, struct Window *win)
{
    struct Node *n;
    ULONG total = 0;
    uint64 bytes = 0;
    BOOL anyErr = FALSE, useProgress = FALSE;

    if (!srcList || !srcDir || !dstDir || !srcDir[0] || !dstDir[0]) return;
    /* spostare nella stessa cartella non ha senso */
    if (move && IUtility->Stricmp(srcDir, dstDir) == 0) return;

    /* conta gli elementi selezionati per la barra di avanzamento */
    for (n = srcList->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL; char full[PATH_MAX_LEN];
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (!BuildChildPath(srcDir, name, full, sizeof(full))) continue;
        total += CountFilesRecursive(full);
        bytes += CountBytesRecursive(full);
    }
    if (total == 0)
    {
        ShowMessage(move ? LS(MSG_MOVE) : LS(MSG_COPY), LS(MSG_COPY_NONE));
        return;
    }
    ProgressOpen(win, total);
    ProgressSetBytes(bytes);
    useProgress = TRUE;

    ResetCopyConflict();
    for (n = srcList->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES; STRPTR name = NULL;
        char src[PATH_MAX_LEN], dst[PATH_MAX_LEN];

        if (useProgress && gProgCancel) break;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (!BuildChildPath(srcDir, name, src, sizeof(src))) continue;
        if (!BuildChildPath(dstDir, name, dst, sizeof(dst))) { anyErr = TRUE; continue; }
        if (move)
        {
            if (!MoveEntry(src, dst)) anyErr = TRUE;
        }
        else
        {
            if (!CopyEntryRecursive(src, dst)) anyErr = TRUE;
        }
    }
    {
        BOOL canceled = gProgCancel;
        if (useProgress) ProgressClose();
        if (anyErr && !canceled)
            ShowMessage(move ? LS(MSG_MOVE) : LS(MSG_COPY), LS(MSG_DROP_COPY_FAILED));
    }
}

/* ===== Calcolo dimensioni cartelle/volumi ===== */

/* La barra e' in modalita' indeterminata: avanza di un oggetto per
   volta e ridisegna ogni 8 oggetti. */
static BOOL  gSizeProgress = FALSE;
static ULONG gSizeStep = 0;

static BOOL CalcSizeRec(CONST_STRPTR path, ULONG depth, int64 *bytes, int64 *files, int64 *dirs)
{
    APTR ctx;
    struct ExamineData *ed;

    if (!path || depth > 128) return FALSE;

    ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)path,
                                     EX_DataFields, EXF_NAME | EXF_SIZE | EXF_TYPE,
                                     TAG_END);
    if (!ctx) return FALSE;

    while ((ed = IDOS->ExamineDir(ctx)) != NULL)
    {
        if (gSizeProgress)
        {
            if (gProgCancel) break;
            gProgDone++;
            if ((++gSizeStep & 0x07) == 0)
            {
                ProgressUpdate();
                ProgressPoll();
            }
        }
        if (EXD_IS_DIRECTORY(ed))
        {
            char sub[PATH_MAX_LEN];
            (*dirs)++;
            strncpy(sub, path, sizeof(sub) - 1);
            sub[sizeof(sub) - 1] = '\0';
            if (IDOS->AddPart(sub, ed->Name, sizeof(sub)))
                CalcSizeRec(sub, depth + 1, bytes, files, dirs);
        }
        else
        {
            (*files)++;
            *bytes += ed->FileSize;
            if (gSizeProgress) gProgBytesDone += (uint64)ed->FileSize;
        }
    }
    IDOS->ReleaseDirContext(ctx);
    return TRUE;
}

BOOL CalculatePathSize(CONST_STRPTR path, int64 *totalBytes, int64 *fileCount, int64 *dirCount)
{
    int64 b = 0, f = 0, d = 0;
    if (!path || !path[0]) return FALSE;
    if (!CalcSizeRec(path, 0, &b, &f, &d)) return FALSE;
    if (totalBytes) *totalBytes = b;
    if (fileCount)  *fileCount  = f;
    if (dirCount)   *dirCount   = d;
    return TRUE;
}

static void FormatSizeStr(int64 bytes, STRPTR out, ULONG outSize)
{
    if (bytes >= (int64)1024 * 1024 * 1024)
    {
        int64 v = bytes / ((int64)1024 * 1024 * 1024);
        int64 r = (bytes % ((int64)1024 * 1024 * 1024)) * 100 / ((int64)1024 * 1024 * 1024);
        snprintf(out, outSize, "%lld.%02lld GB", (long long)v, (long long)r);
    }
    else if (bytes >= (int64)1024 * 1024)
    {
        int64 v = bytes / ((int64)1024 * 1024);
        int64 r = (bytes % ((int64)1024 * 1024)) * 100 / ((int64)1024 * 1024);
        snprintf(out, outSize, "%lld.%02lld MB", (long long)v, (long long)r);
    }
    else if (bytes >= (int64)1024)
    {
        int64 v = bytes / (int64)1024;
        int64 r = (bytes % (int64)1024) * 10 / (int64)1024;
        snprintf(out, outSize, "%lld.%lld KB", (long long)v, (long long)r);
    }
    else
        snprintf(out, outSize, "%lld byte", (long long)bytes);
}

void DoCalculateSize(Object *listGad, Object *pathGad, Object *statusGad, struct Window *win, struct List *list)
{
    struct Node *n;
    int64 total = 0, files = 0, dirs = 0;
    BOOL any = FALSE, foundDir = FALSE, canceled = FALSE;
    char sizeStr[64], msg[512];
    CONST_STRPTR title = LS(MSG_SIZE_TITLE);

    (void)listGad; (void)pathGad; (void)statusGad;

    if (!list) return;

    /* Controlla subito se c'e' almeno una directory selezionata. */
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;

        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index, TAG_DONE);
        if (index < (ULONG)gEntryCount && gIsDir[index])
        {
            foundDir = TRUE;
            break;
        }
    }

    if (!foundDir)
    {
        ShowMessage(title, LS(MSG_SIZE_SELECT_DIR));
        return;
    }

    /* Un solo passaggio: niente conteggio preliminare.  La barra e'
       indeterminata (blocco scorrevole + contatori), cosi' si vede subito
       che il programma sta lavorando. */
    ProgressOpen(win, 1);
    ProgressSetIndeterminate(TRUE);
    gSizeProgress = TRUE;
    gSizeStep = 0;

    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;
        char full[PATH_MAX_LEN];

        if (gProgCancel) { canceled = TRUE; break; }
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index,
            LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount) continue;
        if (!gIsDir[index]) continue;

        if (IsRootPath(gPathBuffer))
        {
            strncpy(full, name, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
        }
        else if (!BuildChildPath(gPathBuffer, name, full, sizeof(full)))
            continue;

        {
            int64 b = 0, f = 0, d = 0;
            if (CalculatePathSize(full, &b, &f, &d))
            {
                total += b; files += f; dirs += d;
                any = TRUE;
            }
        }
        if (gProgCancel) canceled = TRUE;
    }

    gSizeProgress = FALSE;
    if (!canceled)
    {
        ProgressUpdate();
        ProgressPoll();
        if (gProgCancel) canceled = TRUE;
    }
    ProgressClose();

    if (canceled) return;
    if (!any)
    {
        ShowMessage(title, LS(MSG_SIZE_SELECT_DIR));
        return;
    }

    FormatSizeStr(total, sizeStr, sizeof(sizeStr));
    snprintf(msg, sizeof(msg), LS(MSG_SIZE_RESULT),
        sizeStr, (long long)total, (long long)files, (long long)dirs);
    ShowMessage(title, msg);
}


/* ===== Ricerca file e grep (usati anche dai plugin ARexx) ===== */

static BOOL NameContains(CONST_STRPTR hay, CONST_STRPTR needle)
{
    ULONG hl, nl, i, j;
    if (!hay || !needle) return FALSE;
    hl = strlen(hay);
    nl = strlen(needle);
    if (nl == 0) return TRUE;
    if (nl > hl) return FALSE;
    for (i = 0; i <= hl - nl; i++)
    {
        for (j = 0; j < nl; j++)
        {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nl) return TRUE;
    }
    return FALSE;
}

static void AddResultRow(struct List *list, LONG *count, CONST_STRPTR text)
{
    struct Node *n;
    if (!text) return;
    n = IListBrowser->AllocListBrowserNode(1,
        LBNA_UserData, 0,
        LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)text,
        TAG_DONE);
    if (n)
    {
        IExec->AddTail(list, n);
        (*count)++;
    }
}

static void FindRec(CONST_STRPTR dir, CONST_STRPTR pattern, struct List *list,
                    LONG *count, ULONG depth)
{
    APTR ctx;
    struct ExamineData *ed;

    if (!dir || depth > 32 || *count >= 200) return;

    ctx = IDOS->ObtainDirContextTags(EX_StringNameInput, (ULONG)dir,
                                     EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
    if (!ctx) return;

    while (*count < 200 && (ed = IDOS->ExamineDir(ctx)) != NULL)
    {
        char sub[PATH_MAX_LEN];

        strncpy(sub, dir, sizeof(sub) - 1);
        sub[sizeof(sub) - 1] = '\0';
        if (!IDOS->AddPart(sub, ed->Name, sizeof(sub))) continue;

        if (EXD_IS_DIRECTORY(ed))
        {
            if (NameContains(ed->Name, pattern))
                AddResultRow(list, count, sub);
            FindRec(sub, pattern, list, count, depth + 1);
        }
        else if (NameContains(ed->Name, pattern))
        {
            AddResultRow(list, count, sub);
        }
    }
    IDOS->ReleaseDirContext(ctx);
}

void DoFindFile(struct Window *win)
{
    char pattern[NAME_MAX_LEN];
    struct List list;
    LONG count = 0;

    (void)win;
    if (!PromptString(LS(MSG_FIND_TITLE), pattern, sizeof(pattern))) return;
    if (!pattern[0]) return;

    IExec->NewList(&list);
    FindRec(gPathBuffer, pattern, &list, &count, 0);

    if (count == 0)
    {
        ShowMessage(LS(MSG_FIND_TITLE), LS(MSG_FIND_NONE));
        IListBrowser->FreeListBrowserList(&list);
        return;
    }
    ShowResultsWindow(LS(MSG_FIND_RESULTS), &list, count);
}

static void GrepFile(CONST_STRPTR path, CONST_STRPTR word, struct List *list, LONG *count)
{
    BPTR fh;
    char line[1024];
    LONG ln = 0;

    if (!path || !word || !word[0]) return;
    fh = IDOS->Open(path, MODE_OLDFILE);
    if (!fh) return;

    while (*count < 200 && IDOS->FGets(fh, line, sizeof(line)))
    {
        ln++;
        if (NameContains(line, word))
        {
            char res[PATH_MAX_LEN + 1100];
            ULONG l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
                line[--l] = '\0';
            snprintf(res, sizeof(res), "%s:%ld: %s", path, (long)ln, line);
            AddResultRow(list, count, res);
        }
    }
    IDOS->Close(fh);
}

void DoGrep(struct Window *win)
{
    char word[128];
    struct List list;
    LONG count = 0;
    struct Node *n;

    (void)win;
    if (!PromptString(LS(MSG_GREP_TITLE), word, sizeof(word))) return;
    if (!word[0]) return;

    IExec->NewList(&list);

    for (n = gPanes[gActivePane].fileList.lh_Head; n->ln_Succ; n = n->ln_Succ)
    {
        ULONG sel = FALSE, index = MAX_ENTRIES;
        STRPTR name = NULL;
        char full[PATH_MAX_LEN];

        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Selected, &sel, TAG_DONE);
        if (!sel) continue;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &index,
            LBNA_Column, COL_NAME, LBNCA_Text, &name, TAG_DONE);
        if (!name || index >= (ULONG)gEntryCount || gIsDir[index]) continue;

        if (IsRootPath(gPathBuffer))
        {
            strncpy(full, name, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
        }
        else if (!BuildChildPath(gPathBuffer, name, full, sizeof(full)))
            continue;

        GrepFile(full, word, &list, &count);
    }

    if (count == 0)
    {
        ShowMessage(LS(MSG_GREP_TITLE), LS(MSG_GREP_NONE));
        IListBrowser->FreeListBrowserList(&list);
        return;
    }
    ShowResultsWindow(LS(MSG_GREP_RESULTS), &list, count);
}
