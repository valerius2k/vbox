/** $Id$ */
/** @file
 * VBoxSF - OS/2 Shared Folders, Find File IFS EPs.
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
#include "VBoxSFInternal.h"

#include <VBox/log.h>
#include <iprt/mem.h>
#include <iprt/assert.h>

extern VBGLSFCLIENT g_clientHandle;
extern PGINFOSEG g_pGIS;


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

    VbglR0SfClose(&g_clientHandle, &pFindBuf->map, pFindBuf->handle);
    free_shflstring(pFindBuf->path);
    RTMemFree(pFindBuf);
    return NO_ERROR;
}


APIRET APIENTRY FillFindBuf(PFINDBUF pFindBuf, PVBOXSFVP pvboxsfvp,
                            PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
                            ULONG level, ULONG flags)
{
    USHORT usEntriesWanted;
    APIRET hrc = NO_ERROR;
    char buf[260];
    char *p, *lastslash;
    USHORT usNeededLen;
    ULONG cbSize = 0;
    bool skip = false;
    int rc;

    log("g_pGIS=%lx\n", g_pGIS);
    log("timezone=%u minutes\n", g_pGIS->timezone);

    usEntriesWanted = *pcMatch;
    *pcMatch = 0;

    switch (level)
    {
        case FIL_STANDARD:
            usNeededLen = sizeof(FILEFINDBUF3) - CCHMAXPATHCOMP;
            break;

        case FIL_STANDARDL:
            usNeededLen = sizeof(FILEFINDBUF3L) - CCHMAXPATHCOMP;
            break;

        case FIL_QUERYEASIZE:
            usNeededLen = sizeof(FILEFINDBUF2) - CCHMAXPATHCOMP;
            break;

        case FIL_QUERYEASIZEL:
            usNeededLen = sizeof(FILEFINDBUF4L) - CCHMAXPATHCOMP;
            break;

        case FIL_QUERYEASFROMLIST:
            usNeededLen = sizeof(EAOP) + sizeof(FILEFINDBUF3) + sizeof(ULONG);
            break;

        case FIL_QUERYEASFROMLISTL:
            usNeededLen = sizeof(EAOP) + sizeof(FILEFINDBUF3L) + sizeof(ULONG);
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

    if (! pFindBuf->has_more_files)
        return ERROR_NO_MORE_FILES;

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
            hrc = vbox_err_to_os2_err(rc);
            goto FILLFINDBUFEXIT;
        }

        if (rc == VERR_NO_MORE_FILES)
        {
            RTMemFree(pFindBuf->buf);
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

            if (level == FIL_QUERYEASFROMLIST || level == FIL_QUERYEASFROMLISTL)
            {
                EAOP dummy  = {0};
                KernCopyOut(pbData, &dummy, sizeof(EAOP));
                pbData += sizeof(EAOP);
                cbData -= sizeof(EAOP);
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
                    /* File name */
                    vboxsfStrFromUtf8(findbuf.achName, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(findbuf.achName);
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
                    cbSize = sizeof(FILEFNDBUF) - CCHMAXPATHCOMP + findbuf.cchName;
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
                    break;
                }

            case FIL_STANDARDL:
                {
                    FILEFNDBUF3L findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File name */
                    vboxsfStrFromUtf8(findbuf.achName, (char *)pszFileName,
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(findbuf.achName);
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
                    cbSize = sizeof(FILEFNDBUF3L) - CCHMAXPATHCOMP + findbuf.cchName;
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
                    break;
                }

            case FIL_QUERYEASIZE:
                {
                    FILEFNDBUF2 findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File name */
                    vboxsfStrFromUtf8(findbuf.achName, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(findbuf.achName);
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
                    cbSize = sizeof(FILEFNDBUF2) - CCHMAXPATHCOMP + findbuf.cchName;
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
                    findbuf.cbList = file->Info.Attr.u.EASize.cb;
                    KernCopyOut(pbData, &findbuf, cbSize);
                    break;
                }

            case FIL_QUERYEASIZEL:
                {
                    FILEFNDBUF4L findbuf;
                    RTTIME time;
                    char *pszFileName = (char *)file->name.String.utf8;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File name */
                    vboxsfStrFromUtf8(findbuf.achName, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(findbuf.achName);
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
                    cbSize = sizeof(FILEFNDBUF4L) - CCHMAXPATHCOMP + findbuf.cchName;
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
                    findbuf.cbList = file->Info.Attr.u.EASize.cb;
                    KernCopyOut(pbData, &findbuf, cbSize);
                    break;
                }

            case FIL_QUERYEASFROMLIST:
                {
                    FILEFNDBUF3 findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File name */
                    vboxsfStrFromUtf8(findbuf.achName, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(findbuf.achName);
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
                    cbSize = sizeof(FILEFNDBUF3) - CCHMAXPATHCOMP + findbuf.cchName;
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
                    break;
                }

            case FIL_QUERYEASFROMLISTL:
                {
                    FILEFNDBUF3L findbuf;
                    char *pszFileName = (char *)file->name.String.utf8;
                    RTTIME time;
                    memset(&findbuf, 0, sizeof(findbuf));
                    /* File name */
                    vboxsfStrFromUtf8(findbuf.achName, (char *)pszFileName, 
                        CCHMAXPATHCOMP, file->name.u16Length);
                    findbuf.cchName = strlen(findbuf.achName);
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
                    cbSize = sizeof(FILEFNDBUF3L) - CCHMAXPATHCOMP + findbuf.cchName;
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
                    break;
                }

            default:
                log("incorrect level!\n");
                hrc = ERROR_INVALID_FUNCTION;
                goto FILLFINDBUFEXIT;
        }

        (*pcMatch)++;

        pbData += cbSize;
        cbData -= cbSize;

        pFindBuf->index++;

        // next file
        pFindBuf->bufpos = (PSHFLDIRINFO)((char *)pFindBuf->bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
    }

FILLFINDBUFEXIT:
    return hrc;
}

DECLASM(int)
FS32_FINDFIRST(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, ULONG iCurDirEnd, ULONG attr,
               PFSFSI pfsfsi, PVBOXFSFSD pfsfsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
               ULONG level, ULONG flags)
{
    SHFLCREATEPARMS params = {0};
    PFINDBUF pFindBuf;
    PSHFLSTRING path;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    char *pDir = NULL;
    char *p, *str, *lastslash;
    APIRET hrc = NO_ERROR;
    char *pwsz;
    int rc;

    log("FS32_FINDFIRST(%s, %lx, %lx, %lx)\n", pszName, attr, level, flags);

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;

    FSH32_GETVOLPARM(pfsfsi->fsi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    pFindBuf = (PFINDBUF)RTMemAllocZ(sizeof(FINDBUF));

    if (! pFindBuf)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_FINDFIRSTEXIT;
    }

    if (attr & 0x0040)
        attr &= ~0x0040;

    pFindBuf->bMustAttr = (BYTE)(attr >> 8);
    attr |= (FILE_READONLY | FILE_ARCHIVED);
    attr &= (FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY | FILE_ARCHIVED);
    pFindBuf->bAttr = (BYTE)~attr;

    pDir = (char *)RTMemAllocZ(strlen((char *)pszName) + 1);

    if (! pDir)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_FINDFIRSTEXIT;
    }

    // get the directory name
    strcpy(pDir, (char *)pszName);

    p = pDir;

    // skip drive letter and colon
    if (p[1] == ':') p += 2;
    lastslash = str = p;

    // change backslashes to slashes
    do {
        lastslash = p;
        p = strchr(p + 1, '\\');
    } while (p && p < pDir + strlen(pDir));

    // cut off file part from directory part
    str[lastslash - str] = '\0';
    if (*str == '\\') str++;

    log("str=%s\n", str);

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

    if (! pwsz)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_FINDFIRSTEXIT;
    }

    vboxsfStrToUtf8(pwsz, str);

    log("000\n");
    path = make_shflstring(pwsz);
    rc = VbglR0SfCreate(&g_clientHandle, &pvboxsfvp->map, path, &params);
    free_shflstring(path);
    RTMemFree(pwsz);

    if (RT_SUCCESS(rc))
    {
        log("001\n");
        if (params.Result == SHFL_FILE_EXISTS && params.Handle != SHFL_HANDLE_NIL)
        {
            log("002\n");
            pfsfsd->pFindBuf = pFindBuf;

            pFindBuf->len = 16384;
            pFindBuf->handle = params.Handle;
            pFindBuf->map = pvboxsfvp->map;
            pFindBuf->has_more_files = true;

            // get the directory name
            strcpy(pDir, (char *)pszName);

            p = pDir;

            // skip d:
            if (p[1] == ':') p += 3;
            str = p;

            log("wildcard: %s\n", str);

            pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

            if (! pwsz)
            {
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto FS32_FINDFIRSTEXIT;
            }

            vboxsfStrToUtf8(pwsz, str);

            pFindBuf->path = make_shflstring(pwsz);
            RTMemFree(pwsz);
            log("003\n");
            hrc = FillFindBuf(pFindBuf, pvboxsfvp, pbData, cbData, pcMatch, level, flags);
            log("004: hrc=%lu\n", hrc);
        }
        else
        {
            hrc = vbox_err_to_os2_err(rc);
            log("005: hrc=%lu\n", hrc);
            goto FS32_FINDFIRSTEXIT;
        }
    }
    else
    {
        hrc = vbox_err_to_os2_err(rc);
        log("006: hrc=%lu\n", hrc);
        goto FS32_FINDFIRSTEXIT;
    }

    if ( (hrc == ERROR_NO_MORE_FILES ||
          hrc == ERROR_BUFFER_OVERFLOW ||
          hrc == ERROR_EAS_DIDNT_FIT)
         && *pcMatch )
        hrc = 0;

FS32_FINDFIRSTEXIT:
    if (pDir)
        RTMemFree(pDir);

    if (hrc && hrc != ERROR_EAS_DIDNT_FIT)
    {
        FS32_FINDCLOSE(pfsfsi, pfsfsd);
    }

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_FINDNEXT(PFSFSI pfsfsi, PVBOXFSFSD pfsfsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatch,
              ULONG level, ULONG flags)
{
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    APIRET hrc = NO_ERROR;

    log("FS32_FINDNEXT(%lx, %lx)\n", level, flags);

    FSH32_GETVOLPARM(pfsfsi->fsi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    hrc = FillFindBuf(pfsfsd->pFindBuf, pvboxsfvp, pbData, cbData, pcMatch, level, flags);

FS32_FINDFIRSTEXIT:
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
