/** $Id$ */
/** @file
 * VBoxFS - OS/2 Shared Folders, Find File IFS EPs.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 * Copyright (c) 2015-2018 Valery V. Sedletski <_valerius-no-spam@mail.ru>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT
#include "VBoxFSInternal.h"

#include <VBox/log.h>
#include <iprt/mem.h>
#include <iprt/assert.h>

extern VBGLSFCLIENT g_clientHandle;
extern PGINFOSEG    g_pGIS;


uint32_t VBoxToOS2Attr(uint32_t fMode)
{
    uint32_t attr = 0;

    if (fMode & RTFS_DOS_READONLY)
        attr |= FILE_READONLY;
    if (fMode & RTFS_DOS_HIDDEN)
        attr |= FILE_HIDDEN;
    if (fMode & RTFS_DOS_SYSTEM)
        attr |= FILE_SYSTEM;
    if (fMode & RTFS_DOS_DIRECTORY)
        attr |= FILE_DIRECTORY;
    if (fMode & RTFS_DOS_ARCHIVED)
        attr |= FILE_ARCHIVED;

    return attr;
}


uint32_t OS2ToVBoxAttr(uint32_t attr)
{
    uint32_t fMode = 0;

    if (attr & FILE_READONLY)
        fMode |= RTFS_DOS_READONLY;
    if (attr & FILE_HIDDEN)
        fMode |= RTFS_DOS_HIDDEN;
    if (attr & FILE_SYSTEM)
        fMode |= RTFS_DOS_SYSTEM;
    if (attr & FILE_DIRECTORY)
        fMode |= RTFS_DOS_DIRECTORY;
    if (attr & FILE_ARCHIVED)
        fMode |= RTFS_DOS_ARCHIVED;

    return fMode;
}


DECLASM(int)
FS32_FINDCLOSE(PFSFSI pfsfsi, PVBOXFSFSD pfsfsd)
{
    PFINDBUF pFindBuf = pfsfsd->pFindBuf;

    log("FS32_FINDCLOSE\n");

    if (! pFindBuf)
    {
        return NO_ERROR;
    }

    if (pFindBuf->buf)
    {
        RTMemFree(pFindBuf->buf);
    }

    if (pFindBuf->handle != SHFL_HANDLE_NIL)
    {
        VbglR0SfClose(&g_clientHandle, &pFindBuf->map, pFindBuf->handle);
    }

    if (pFindBuf->path)
    {
        free_shflstring(pFindBuf->path);
    }

    if (pFindBuf->tmp)
    {
        VbglR0SfUnmapFolder(&g_clientHandle, &pFindBuf->map);
    }

    if (pFindBuf)
    {
        RTMemFree(pFindBuf);
    }


    return NO_ERROR;
}


APIRET APIENTRY FillFindBuf(PFINDBUF pFindBuf,
                            PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
                            ULONG level, ULONG flags)
{
    USHORT usEntriesWanted;
    APIRET hrc = NO_ERROR;
    char buf[260];
    char *p, *lastslash;
    USHORT usNeededLen;
    ULONG cbSize = 0;
    PSZ pszTzValue;
    char *pszFn = NULL;
    EAOP eaop = {0};
    PFEALIST pFeal = NULL;
    bool skip = false;
    int rc;

    //log("g_pGIS=%lx\n", g_pGIS);
    //log("timezone=%d minutes\n", (SHORT)g_pGIS->timezone);

    usEntriesWanted = *pcMatch;
    *pcMatch = 0;

    switch (level)
    {
        case FIL_STANDARD:
            usNeededLen = sizeof(FILEFNDBUF3);
            break;

        case FIL_STANDARDL:
            usNeededLen = sizeof(FILEFNDBUF3L);
            break;

        case FIL_QUERYEASIZE:
            usNeededLen = sizeof(FILEFNDBUF2);
            break;

        case FIL_QUERYEASIZEL:
            usNeededLen = sizeof(FILEFNDBUF4L);
            break;

        case FIL_QUERYEASFROMLIST:
            usNeededLen = sizeof(EAOP) + sizeof(FILEFNDBUF3) + MIN_EA_SIZE;
            break;

        case FIL_QUERYEASFROMLISTL:
            usNeededLen = sizeof(EAOP) + sizeof(FILEFNDBUF3L) + MIN_EA_SIZE;
            break;

        default:
            hrc = ERROR_INVALID_FUNCTION;
            goto FILLFINDBUFEXIT;
    }

    if (flags == FF_GETPOS)
        usNeededLen += sizeof(ULONG);

    if (cbData < usNeededLen)
    {
        hrc = ERROR_BUFFER_OVERFLOW;
        goto FILLFINDBUFEXIT;
    }

    if (level == FIL_QUERYEASFROMLIST || level == FIL_QUERYEASFROMLISTL)
    {
        memcpy(&eaop, pbData, sizeof (EAOP));
        pFeal = (PFEALIST)RTMemAlloc(MIN_EA_SIZE);
        
        if (! pFeal)
        {
            hrc = ERROR_NOT_ENOUGH_MEMORY;
            goto FILLFINDBUFEXIT;
        }
    }

    memset(pbData, 0, cbData);

    if (level == FIL_QUERYEASFROMLIST || level == FIL_QUERYEASFROMLISTL)
    {
        KernCopyOut(pbData, &eaop, sizeof(EAOP));
        pbData += sizeof(EAOP);
        cbData -= sizeof(EAOP);

        eaop.fpFEAList = pFeal;
        eaop.fpGEAList = (PGEALIST)KernSelToFlat((ULONG)eaop.fpGEAList);
    }

    if (! pFindBuf->has_more_files)
    {
        hrc = ERROR_NO_MORE_FILES;
        goto FILLFINDBUFEXIT;
    }

    pszFn = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFn)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FILLFINDBUFEXIT;
    }

    if (! pFindBuf->buf)
    {
        pFindBuf->buf = (PSHFLDIRINFO)RTMemAlloc(pFindBuf->len);

        if (! pFindBuf->buf)
        {
            log("RTMemAlloc failed!\n");
            hrc = ERROR_NOT_ENOUGH_MEMORY;
            goto FILLFINDBUFEXIT;
        }

        pFindBuf->bufpos = pFindBuf->buf;

        if (pFindBuf->index >= pFindBuf->num_files)
        {
            pFindBuf->num_files = 0;
            pFindBuf->index = 0;
        }

        rc = VbglR0SfDirInfo(&g_clientHandle, &pFindBuf->map, pFindBuf->handle, pFindBuf->path, 0, pFindBuf->index,
                             &pFindBuf->len, pFindBuf->buf, &pFindBuf->num_files);

        if (rc != 0 && rc != VERR_NO_MORE_FILES)
        {
            log("VbglR0SfDirInfo failed: %d\n", rc);
            RTMemFree(pFindBuf->buf);
            pFindBuf->buf = NULL;
            hrc = vbox_err_to_os2_err(rc);
            goto FILLFINDBUFEXIT;
        }

        if (rc == VERR_NO_MORE_FILES)
        {
            RTMemFree(pFindBuf->buf);
            pFindBuf->buf = NULL;
            hrc = ERROR_NO_MORE_FILES;
            pFindBuf->has_more_files = false;
            goto FILLFINDBUFEXIT;
        }
    }

    while (pFindBuf->bufpos < pFindBuf->buf + pFindBuf->len && *pcMatch < usEntriesWanted)
    {
        PSHFLDIRINFO file = pFindBuf->bufpos;

        if (pFindBuf->index >= pFindBuf->num_files)
        {
            ULONG oNextEntryOffset = 0;
            KernCopyOut(pbData - cbSize - sizeof(ULONG), &oNextEntryOffset, sizeof(ULONG));
            RTMemFree(pFindBuf->buf);
            pFindBuf->buf = file = NULL;
            break;
        }

        if (! skip)
        {
            if (flags == FF_GETPOS)
            {
                ULONG oNextEntryOffset = 0;
                KernCopyOut(pbData, &oNextEntryOffset, sizeof(ULONG));
                if (cbSize)
                {
                    oNextEntryOffset = cbSize + sizeof(ULONG);
                    KernCopyOut(pbData - cbSize - sizeof(ULONG), &oNextEntryOffset, sizeof(ULONG));
                }
                pbData += sizeof(ULONG);
                cbData -= sizeof(ULONG);
            }
        }
        skip = false;

        switch (level)
        {
            case FIL_STANDARD:
                {
                    FILEFNDBUF findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File attributes */
                    findbuf.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                    /* Creation time   */
                    RTTimeExplode(&time, &file->Info.BirthTime);
                    findbuf.fdateCreation.day = time.u8MonthDay;
                    findbuf.fdateCreation.month = time.u8Month;
                    findbuf.fdateCreation.year = time.i32Year - 1980;
                    findbuf.ftimeCreation.twosecs = time.u8Second / 2;
                    findbuf.ftimeCreation.minutes = time.u8Minute;
                    findbuf.ftimeCreation.hours = time.u8Hour;
                    /* Last access time   */
                    RTTimeExplode(&time, &file->Info.AccessTime);
                    findbuf.fdateLastAccess.day = time.u8MonthDay;
                    findbuf.fdateLastAccess.month = time.u8Month;
                    findbuf.fdateLastAccess.year = time.i32Year - 1980;
                    findbuf.ftimeLastAccess.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastAccess.minutes = time.u8Minute;
                    findbuf.ftimeLastAccess.hours = time.u8Hour;
                    /* Last write time   */
                    RTTimeExplode(&time, &file->Info.ModificationTime);
                    findbuf.fdateLastWrite.day = time.u8MonthDay;
                    findbuf.fdateLastWrite.month = time.u8Month;
                    findbuf.fdateLastWrite.year = time.i32Year - 1980;
                    findbuf.ftimeLastWrite.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastWrite.minutes = time.u8Minute;
                    findbuf.ftimeLastWrite.hours = time.u8Hour;
                    findbuf.cbFile = (ULONG)file->Info.cbObject;
                    findbuf.cbFileAlloc = (ULONG)file->Info.cbAllocated;
                    cbSize = sizeof(FILEFNDBUF) - 1;
                    /* Check for file attributes */
                    if (pFindBuf->bMustAttr)
                    {
                        if ((findbuf.attrFile & pFindBuf->bMustAttr) != pFindBuf->bMustAttr)
                        {
                            skip = true;
                            pFindBuf->index++;
                            pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
                            continue;
                        }
                    }
                    /* File name */
                    vboxfsStrFromUtf8(pszFn, (char *)pszFileName,
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(pszFn);
                    KernCopyOut(pbData, &findbuf, cbSize);
                    KernCopyOut(pbData + cbSize, pszFn, findbuf.cchName + 1);
                    cbSize += findbuf.cchName + 1;
                    break;
                }

            case FIL_STANDARDL:
                {
                    FILEFNDBUF3L findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File attributes */
                    findbuf.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                    /* Creation time   */
                    RTTimeExplode(&time, &file->Info.BirthTime);
                    findbuf.fdateCreation.day = time.u8MonthDay;
                    findbuf.fdateCreation.month = time.u8Month;
                    findbuf.fdateCreation.year = time.i32Year - 1980;
                    findbuf.ftimeCreation.twosecs = time.u8Second / 2;
                    findbuf.ftimeCreation.minutes = time.u8Minute;
                    findbuf.ftimeCreation.hours = time.u8Hour;
                    /* Last access time   */
                    RTTimeExplode(&time, &file->Info.AccessTime);
                    findbuf.fdateLastAccess.day = time.u8MonthDay;
                    findbuf.fdateLastAccess.month = time.u8Month;
                    findbuf.fdateLastAccess.year = time.i32Year - 1980;
                    findbuf.ftimeLastAccess.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastAccess.minutes = time.u8Minute;
                    findbuf.ftimeLastAccess.hours = time.u8Hour;
                    /* Last write time   */
                    RTTimeExplode(&time, &file->Info.ModificationTime);
                    findbuf.fdateLastWrite.day = time.u8MonthDay;
                    findbuf.fdateLastWrite.month = time.u8Month;
                    findbuf.fdateLastWrite.year = time.i32Year - 1980;
                    findbuf.ftimeLastWrite.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastWrite.minutes = time.u8Minute;
                    findbuf.ftimeLastWrite.hours = time.u8Hour;
                    findbuf.cbFile = file->Info.cbObject;
                    findbuf.cbFileAlloc = file->Info.cbAllocated;
                    cbSize = sizeof(FILEFNDBUF3L) - 1;
                    /* Check for file attributes */
                    if (pFindBuf->bMustAttr)
                    {
                        if ((findbuf.attrFile & pFindBuf->bMustAttr) != pFindBuf->bMustAttr)
                        {
                            skip = true;
                            pFindBuf->index++;
                            pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
                            continue;
                        }
                    }
                    /* File name */
                    vboxfsStrFromUtf8(pszFn, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(pszFn);
                    KernCopyOut(pbData, &findbuf, cbSize);
                    KernCopyOut(pbData + cbSize, pszFn, findbuf.cchName + 1);
                    cbSize += findbuf.cchName + 1;
                    break;
                }

            case FIL_QUERYEASIZE:
                {
                    FILEFNDBUF2 findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File attributes */
                    findbuf.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                    /* Creation time   */
                    RTTimeExplode(&time, &file->Info.BirthTime);
                    findbuf.fdateCreation.day = time.u8MonthDay;
                    findbuf.fdateCreation.month = time.u8Month;
                    findbuf.fdateCreation.year = time.i32Year - 1980;
                    findbuf.ftimeCreation.twosecs = time.u8Second / 2;
                    findbuf.ftimeCreation.minutes = time.u8Minute;
                    findbuf.ftimeCreation.hours = time.u8Hour;
                    /* Last access time   */
                    RTTimeExplode(&time, &file->Info.AccessTime);
                    findbuf.fdateLastAccess.day = time.u8MonthDay;
                    findbuf.fdateLastAccess.month = time.u8Month;
                    findbuf.fdateLastAccess.year = time.i32Year - 1980;
                    findbuf.ftimeLastAccess.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastAccess.minutes = time.u8Minute;
                    findbuf.ftimeLastAccess.hours = time.u8Hour;
                    /* Last write time   */
                    RTTimeExplode(&time, &file->Info.ModificationTime);
                    findbuf.fdateLastWrite.day = time.u8MonthDay;
                    findbuf.fdateLastWrite.month = time.u8Month;
                    findbuf.fdateLastWrite.year = time.i32Year - 1980;
                    findbuf.ftimeLastWrite.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastWrite.minutes = time.u8Minute;
                    findbuf.ftimeLastWrite.hours = time.u8Hour;
                    findbuf.cbFile = (ULONG)file->Info.cbObject;
                    findbuf.cbFileAlloc = (ULONG)file->Info.cbAllocated;
                    cbSize = sizeof(FILEFNDBUF2) - 1;
                    /* Check for file attributes */
                    if (pFindBuf->bMustAttr)
                    {
                        if ((findbuf.attrFile & pFindBuf->bMustAttr) != pFindBuf->bMustAttr)
                        {
                            skip = true;
                            pFindBuf->index++;
                            pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
                            continue;
                        }
                    }
                    findbuf.cbList = 0; //file->Info.Attr.u.EASize.cb;
                    /* File name */
                    vboxfsStrFromUtf8(pszFn, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(pszFn);
                    KernCopyOut(pbData, &findbuf, cbSize);
                    KernCopyOut(pbData + cbSize, pszFn, findbuf.cchName + 1);
                    cbSize += findbuf.cchName + 1;
                    break;
                }

            case FIL_QUERYEASIZEL:
                {
                    FILEFNDBUF4L findbuf;
                    RTTIME time;
                    char *pszFileName = (char *)file->name.String.utf8;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File attributes */
                    findbuf.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                    /* Creation time   */
                    RTTimeExplode(&time, &file->Info.BirthTime);
                    findbuf.fdateCreation.day = time.u8MonthDay;
                    findbuf.fdateCreation.month = time.u8Month;
                    findbuf.fdateCreation.year = time.i32Year - 1980;
                    findbuf.ftimeCreation.twosecs = time.u8Second / 2;
                    findbuf.ftimeCreation.minutes = time.u8Minute;
                    findbuf.ftimeCreation.hours = time.u8Hour;
                    /* Last access time   */
                    RTTimeExplode(&time, &file->Info.AccessTime);
                    findbuf.fdateLastAccess.day = time.u8MonthDay;
                    findbuf.fdateLastAccess.month = time.u8Month;
                    findbuf.fdateLastAccess.year = time.i32Year - 1980;
                    findbuf.ftimeLastAccess.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastAccess.minutes = time.u8Minute;
                    findbuf.ftimeLastAccess.hours = time.u8Hour;
                    /* Last write time   */
                    RTTimeExplode(&time, &file->Info.ModificationTime);
                    findbuf.fdateLastWrite.day = time.u8MonthDay;
                    findbuf.fdateLastWrite.month = time.u8Month;
                    findbuf.fdateLastWrite.year = time.i32Year - 1980;
                    findbuf.ftimeLastWrite.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastWrite.minutes = time.u8Minute;
                    findbuf.ftimeLastWrite.hours = time.u8Hour;
                    findbuf.cbFile = file->Info.cbObject;
                    findbuf.cbFileAlloc = file->Info.cbAllocated;
                    cbSize = sizeof(FILEFNDBUF4L) - 1;
                    /* Check for file attributes */
                    if (pFindBuf->bMustAttr)
                    {
                        if ((findbuf.attrFile & pFindBuf->bMustAttr) != pFindBuf->bMustAttr)
                        {
                            skip = true;
                            pFindBuf->index++;
                            pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
                            continue;
                        }
                    }
                    findbuf.cbList = 0; //file->Info.Attr.u.EASize.cb;
                    /* File name */
                    vboxfsStrFromUtf8(pszFn, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(pszFn);
                    KernCopyOut(pbData, &findbuf, cbSize);
                    KernCopyOut(pbData + cbSize, pszFn, findbuf.cchName + 1);
                    cbSize += findbuf.cchName + 1;
                    break;
                }

            case FIL_QUERYEASFROMLIST:
                {
                    FILEFNDBUF3 findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    ULONG ulFeaSize;
                    BYTE len;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File attributes */
                    findbuf.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                    /* Creation time   */
                    RTTimeExplode(&time, &file->Info.BirthTime);
                    findbuf.fdateCreation.day = time.u8MonthDay;
                    findbuf.fdateCreation.month = time.u8Month;
                    findbuf.fdateCreation.year = time.i32Year - 1980;
                    findbuf.ftimeCreation.twosecs = time.u8Second / 2;
                    findbuf.ftimeCreation.minutes = time.u8Minute;
                    findbuf.ftimeCreation.hours = time.u8Hour;
                    /* Last access time   */
                    RTTimeExplode(&time, &file->Info.AccessTime);
                    findbuf.fdateLastAccess.day = time.u8MonthDay;
                    findbuf.fdateLastAccess.month = time.u8Month;
                    findbuf.fdateLastAccess.year = time.i32Year - 1980;
                    findbuf.ftimeLastAccess.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastAccess.minutes = time.u8Minute;
                    findbuf.ftimeLastAccess.hours = time.u8Hour;
                    /* Last write time   */
                    RTTimeExplode(&time, &file->Info.ModificationTime);
                    findbuf.fdateLastWrite.day = time.u8MonthDay;
                    findbuf.fdateLastWrite.month = time.u8Month;
                    findbuf.fdateLastWrite.year = time.i32Year - 1980;
                    findbuf.ftimeLastWrite.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastWrite.minutes = time.u8Minute;
                    findbuf.ftimeLastWrite.hours = time.u8Hour;
                    findbuf.cbFile = (ULONG)file->Info.cbObject;
                    findbuf.cbFileAlloc = (ULONG)file->Info.cbAllocated;
                    cbSize = sizeof(FILEFNDBUF3) - 2;
                    /* Check for file attributes */
                    if (pFindBuf->bMustAttr)
                    {
                        if ((findbuf.attrFile & pFindBuf->bMustAttr) != pFindBuf->bMustAttr)
                        {
                            skip = true;
                            pFindBuf->index++;
                            pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
                            continue;
                        }
                    }
                    KernCopyOut(pbData, &findbuf, cbSize);
                    cbData -= cbSize;
                    pbData += cbSize;
                    /* File name */
                    vboxfsStrFromUtf8(pszFn, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    len = strlen(pszFn);
                    eaop.fpFEAList->cbList = cbData - (len + 2);
                    hrc = GetEmptyEAS(&eaop);
                    if (hrc && (hrc != ERROR_EAS_DIDNT_FIT))
                        goto FILLFINDBUFEXIT;
                    else if (hrc == ERROR_EAS_DIDNT_FIT)
                        ulFeaSize = sizeof(eaop.fpFEAList->cbList);
                    else
                        ulFeaSize = eaop.fpFEAList->cbList;
                    KernCopyOut(pbData, eaop.fpFEAList, ulFeaSize);
                    cbData -= ulFeaSize;
                    pbData += ulFeaSize;
                    KernCopyOut(pbData, &len, sizeof(len));
                    pbData += sizeof(len);
                    cbData -= sizeof(len);
                    KernCopyOut(pbData, pszFn, len + 1);
                    cbSize += len + 1;
                    pbData += len + 1;
                    cbData -= len + 1;
                    break;
                }

            case FIL_QUERYEASFROMLISTL:
                {
                    FILEFNDBUF3L findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    ULONG ulFeaSize;
                    BYTE len;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File attributes */
                    findbuf.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                    /* Creation time   */
                    RTTimeExplode(&time, &file->Info.BirthTime);
                    findbuf.fdateCreation.day = time.u8MonthDay;
                    findbuf.fdateCreation.month = time.u8Month;
                    findbuf.fdateCreation.year = time.i32Year - 1980;
                    findbuf.ftimeCreation.twosecs = time.u8Second / 2;
                    findbuf.ftimeCreation.minutes = time.u8Minute;
                    findbuf.ftimeCreation.hours = time.u8Hour;
                    /* Last access time   */
                    RTTimeExplode(&time, &file->Info.AccessTime);
                    findbuf.fdateLastAccess.day = time.u8MonthDay;
                    findbuf.fdateLastAccess.month = time.u8Month;
                    findbuf.fdateLastAccess.year = time.i32Year - 1980;
                    findbuf.ftimeLastAccess.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastAccess.minutes = time.u8Minute;
                    findbuf.ftimeLastAccess.hours = time.u8Hour;
                    /* Last write time   */
                    RTTimeExplode(&time, &file->Info.ModificationTime);
                    findbuf.fdateLastWrite.day = time.u8MonthDay;
                    findbuf.fdateLastWrite.month = time.u8Month;
                    findbuf.fdateLastWrite.year = time.i32Year - 1980;
                    findbuf.ftimeLastWrite.twosecs = time.u8Second / 2;
                    findbuf.ftimeLastWrite.minutes = time.u8Minute;
                    findbuf.ftimeLastWrite.hours = time.u8Hour;
                    findbuf.cbFile = file->Info.cbObject;
                    findbuf.cbFileAlloc = file->Info.cbAllocated;
                    cbSize = sizeof(FILEFNDBUF3L) - 2;
                    /* Check for file attributes */
                    if (pFindBuf->bMustAttr)
                    {
                        if ((findbuf.attrFile & pFindBuf->bMustAttr) != pFindBuf->bMustAttr)
                        {
                            skip = true;
                            pFindBuf->index++;
                            pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
                            continue;
                        }
                    }
                    KernCopyOut(pbData, &findbuf, cbSize);
                    cbData -= cbSize;
                    pbData += cbSize;
                    /* File name */
                    vboxfsStrFromUtf8(pszFn, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    len = strlen(pszFn);
                    eaop.fpFEAList->cbList = cbData - (len + 2);
                    hrc = GetEmptyEAS(&eaop);
                    if (hrc && (hrc != ERROR_EAS_DIDNT_FIT))
                        goto FILLFINDBUFEXIT;
                    else if (hrc == ERROR_EAS_DIDNT_FIT)
                        ulFeaSize = sizeof(eaop.fpFEAList->cbList);
                    else
                        ulFeaSize = eaop.fpFEAList->cbList;
                    KernCopyOut(pbData, eaop.fpFEAList, ulFeaSize);
                    cbData -= ulFeaSize;
                    pbData += ulFeaSize;
                    KernCopyOut(pbData, &len, sizeof(len));
                    pbData += sizeof(len);
                    cbData -= sizeof(len);
                    KernCopyOut(pbData, pszFn, len + 1);
                    cbSize += len + 1;
                    pbData += len + 1;
                    cbData -= len + 1;
                    break;
                }

            default:
                log("incorrect level!\n");
                hrc = ERROR_INVALID_FUNCTION;
                goto FILLFINDBUFEXIT;
        }

        (*pcMatch)++;

        if (level != FIL_QUERYEASFROMLIST && level != FIL_QUERYEASFROMLISTL)
        {
            pbData += cbSize;
            cbData -= cbSize;
        }

        pFindBuf->index++;

        // next file
        pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
    }

FILLFINDBUFEXIT:
    if (pszFn)
        RTMemFree(pszFn);
    if (pFeal)
        RTMemFree(pFeal);

    return hrc;
}

DECLASM(int)
FS32_FINDFIRST(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, ULONG iCurDirEnd, ULONG attr,
               PFSFSI pfsfsi, PVBOXFSFSD pfsfsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
               ULONG level, ULONG flags)
{
    SHFLCREATEPARMS params = {0};
    PFINDBUF pFindBuf = NULL;
    PSHFLSTRING path;
    VBGLSFMAP map;
    bool tmp;
    char *pFile = NULL;
    char *pDir = NULL;
    int cbDir;
    char *p, *str, *lastslash;
    APIRET hrc = NO_ERROR;
    char *pwsz;
    int rc;

    log("FS32_FINDFIRST(%s, %lx, %lx, %lx)\n", pszName, attr, level, flags);

    RT_ZERO(params);
    //pfsfsd->pFindBuf = NULL;

    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;

    pFindBuf = (PFINDBUF)RTMemAllocZ(sizeof(FINDBUF));

    if (! pFindBuf)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_FINDFIRSTEXIT;
    }

    pFindBuf->buf = NULL;
    pfsfsd->pFindBuf = pFindBuf;

    if (attr & 0x0040)
        attr &= ~0x0040;

    pFindBuf->bMustAttr = (BYTE)(attr >> 8);
    attr |= (FILE_READONLY | FILE_ARCHIVED);
    attr &= (FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY | FILE_ARCHIVED);
    pFindBuf->bAttr = (BYTE)~attr;
    pFindBuf->handle = SHFL_HANDLE_NIL;

    cbDir = strlen((char *)pszName) + 1;

    pFile = (char *)RTMemAllocZ(cbDir);

    if (! pFile)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_FINDFIRSTEXIT;
    }

    pDir = (char *)RTMemAllocZ(cbDir);

    if (! pDir)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_FINDFIRSTEXIT;
    }

    // get the directory name
    hrc = parseFileName((char *)pszName, pcdfsi, pFile, &cbDir, &map, &tmp);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_FINDFIRSTEXIT;
    }

    log("pFile=%s\n", pFile); 

    strcpy(pDir, pFile);
    p = pDir;

    str = p;
    lastslash = strrchr(p, '\\');
    
    if (lastslash)
    {
        str[lastslash - str] = '\0';
    }
    else
    {
        str[0] = '\0';
    }

    log("str=%s\n", str);

    str = pDir;

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

    if (! pwsz)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_FINDFIRSTEXIT;
    }

    vboxfsStrToUtf8(pwsz, str);

    path = make_shflstring(pwsz);
    rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);
    free_shflstring(path);
    RTMemFree(pwsz);

    if (RT_SUCCESS(rc))
    {
        if (params.Result == SHFL_PATH_NOT_FOUND)
        {
            hrc = ERROR_PATH_NOT_FOUND;
            goto FS32_FINDFIRSTEXIT;
        }

        if (params.Result == SHFL_FILE_EXISTS)
        {
            if (params.Handle != SHFL_HANDLE_NIL)
            {
                pFindBuf->len = 16384;
                pFindBuf->handle = params.Handle;
                pFindBuf->map = map;
                pFindBuf->tmp = tmp;
                pFindBuf->has_more_files = true;

                cbDir = strlen((char *)pszName) + 1;

                str = pFile;

                log("wildcard: %s\n", str);

                pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

                if (! pwsz)
                {
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto FS32_FINDFIRSTEXIT;
                }

                vboxfsStrToUtf8(pwsz, str);

                pFindBuf->path = make_shflstring(pwsz);
                RTMemFree(pwsz);
                hrc = FillFindBuf(pFindBuf, pbData, cbData, pcMatch, level, flags);
            }
            else
            {
                log("VbglR0SfCreate: handle is NULL!\n");
                hrc = ERROR_PATH_NOT_FOUND;
                goto FS32_FINDFIRSTEXIT;
            }
        }
        else
        {
            hrc = ERROR_FILE_NOT_FOUND;
            goto FS32_FINDFIRSTEXIT;
        }
    }
    else
    {
        hrc = vbox_err_to_os2_err(rc);
        goto FS32_FINDFIRSTEXIT;
    }

    if ( (hrc == ERROR_NO_MORE_FILES ||
          hrc == ERROR_BUFFER_OVERFLOW ||
          hrc == ERROR_EAS_DIDNT_FIT)
         && *pcMatch )
        hrc = 0;

   if (hrc == ERROR_EAS_DIDNT_FIT && ! *pcMatch)
      *pcMatch = 1;

FS32_FINDFIRSTEXIT:
    if (hrc && hrc != ERROR_EAS_DIDNT_FIT)
    {
        FS32_FINDCLOSE(pfsfsi, pfsfsd);
    }

    if (pDir)
        RTMemFree(pDir);
    if (pFile)
        RTMemFree(pFile);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_FINDNEXT(PFSFSI pfsfsi, PVBOXFSFSD pfsfsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
              ULONG level, ULONG flags)
{
    APIRET hrc = NO_ERROR;

    log("FS32_FINDNEXT(%lx, %lx)\n", level, flags);

    hrc = FillFindBuf(pfsfsd->pFindBuf, pbData, cbData, pcMatch, level, flags);

    if ( (hrc == ERROR_NO_MORE_FILES ||
          hrc == ERROR_BUFFER_OVERFLOW ||
          hrc == ERROR_EAS_DIDNT_FIT)
         && *pcMatch )
        hrc = 0;

   if (hrc == ERROR_EAS_DIDNT_FIT && ! *pcMatch)
      *pcMatch = 1;

FS32_FINDNEXTEXIT:
    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_FINDFROMNAME(PFSFSI pfsfsi, PVBOXFSFSD pfsfsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
                  ULONG level, ULONG position, PCSZ pszName, ULONG flag)
{
    APIRET hrc;
    log("FS32_FINDFFROMNAME(%s)\n", pszName);

    hrc = FS32_FINDNEXT(pfsfsi, pfsfsd, pbData, cbData, pcMatch, level, flag);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_FINDNOTIFYFIRST(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, ULONG iCurDirEnd, ULONG attr,
                     PUSHORT pHandle, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
                     ULONG level, ULONG flags)
{
    log("FS32_FINDNOTIFYFIRST(%s, %lx, %lx, %lx)\n", pszName, attr, level, flags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FINDNOTIFYNEXT(ULONG handle, PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
                    ULONG level, ULONG timeout)
{
    log("FS32_FINDNOTIFYNEXT(%lx, %lx, %lx)\n", handle, level, timeout);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FINDNOTIFYCLOSE(ULONG handle)
{
    log("FS32_FINDNOTIFYCLOSE(%lx)\n", handle);
    return ERROR_NOT_SUPPORTED;
}
