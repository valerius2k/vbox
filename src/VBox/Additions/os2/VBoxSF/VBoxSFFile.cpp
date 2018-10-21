/** $Id$ */
/** @file
 * VBoxSF - OS/2 Shared Folders, the file level IFS EPs.
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

uint32_t VBoxToOS2Attr(uint32_t fMode);

/**
 * Resolve a file name to a share map handle and the path relative to this share
 *
 *  @param   Path
 *  @param   pcdfsi
 *
 */
APIRET APIENTRY parseFileName(const char *pszPath, PCDFSI pcdfsi,
                              char *pszParsedPath, int *pcbParsedPath, VBGLSFMAP *map)
{
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    char *p;
    int len;

    if (! pszPath || !pcdfsi || ! pszParsedPath || ! *pcbParsedPath)
        return ERROR_INVALID_PARAMETER;

    FSH32_GETVOLPARM(pcdfsi->cdi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    if ( (p = strstr(pszPath, ":"))  && p == pszPath + 1 )
    {
        /* absolute pathname starting from "d:\" */
        len = strlen(p + 2);

        if (len > *pcbParsedPath)
            return ERROR_FILENAME_EXCED_RANGE;

        strcpy(pszParsedPath, p + 2);
        *map = pvboxsfvp->map;
    }
    else if ( (p = strstr(pszPath, "\\\\")) && p == pszPath )
    {
        // UNC name
        return ERROR_NOT_SUPPORTED;
    }
    else if ( (p = strstr(pszPath, "\\")) && p == pszPath )
    {
        /* absolute pathname starting from "\" */
        len = strlen(p + 1);

        if (len > *pcbParsedPath)
            return ERROR_FILENAME_EXCED_RANGE;

        strcpy(pszParsedPath, p + 1);
        *map = pvboxsfvp->map;
    }
    else if (! (p = strstr(pszPath, "\\")) )
    {
        /* relative pathname */
        len = strlen(pcdfsi->cdi_curdir) + strlen(pszPath) + 1;

        if (len > *pcbParsedPath)
            return ERROR_FILENAME_EXCED_RANGE;

        strcpy(pszParsedPath, pcdfsi->cdi_curdir);
        strcat(pszParsedPath, "\\");
        strcat(pszParsedPath, pszPath);
        *map = pvboxsfvp->map;
    }

    return NO_ERROR;
}

DECLASM(int)
FS32_OPENCREATE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
                PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG ulOpenMode, USHORT usOpenFlags,
                PUSHORT pusAction, USHORT usAttr, PBYTE pcEABuf, PUSHORT pfgenflag)
{
    SHFLCREATEPARMS params = {0};
    APIRET hrc = NO_ERROR;
    char *pszFullName;
    int cbFullName;
    VBGLSFMAP map;
    char *pwsz;
    PSHFLSTRING path;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    RTTIME time;
    FDATE d;
    FTIME t;
    int rc;

    log("VBOXSF: FS32_OPENCREATE(%s, %lx, %x, %x)\n", pszName, ulOpenMode, usOpenFlags, usAttr);

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_OPENCREATEEXIT;
    }

    log("pszFullName=%s\n", pszFullName); 

    if (ulOpenMode & OPEN_FLAGS_DASD)
    {
        hrc = ERROR_NOT_SUPPORTED;
        goto FS32_OPENCREATEEXIT;
    }
    else if (ulOpenMode & OPEN_ACCESS_READWRITE)
        params.CreateFlags |= SHFL_CF_ACCESS_READWRITE;
    else if (ulOpenMode & OPEN_ACCESS_READONLY)
        params.CreateFlags |= SHFL_CF_ACCESS_READ;
    else if (ulOpenMode & OPEN_ACCESS_WRITEONLY)
        params.CreateFlags |= SHFL_CF_ACCESS_WRITE;

    if (usOpenFlags & OPEN_ACTION_CREATE_IF_NEW)
    {
        params.CreateFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
        if (usOpenFlags & OPEN_ACTION_FAIL_IF_EXISTS)
            params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS;
        else if (usOpenFlags & OPEN_ACTION_REPLACE_IF_EXISTS)
            params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        else if (usOpenFlags & OPEN_ACTION_OPEN_IF_EXISTS)
            params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;

        *pusAction = FILE_CREATED;
    }
    else if (usOpenFlags & OPEN_ACTION_FAIL_IF_NEW)
    {
        params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
        if (usOpenFlags & OPEN_ACTION_REPLACE_IF_EXISTS)
        {
            params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
            *pusAction = FILE_TRUNCATED;
        }
        else if (usOpenFlags & OPEN_ACTION_OPEN_IF_EXISTS)
        {
            params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
            *pusAction = FILE_EXISTED;
        }
    }
    else
        params.CreateFlags |= SHFL_CF_ACCESS_APPEND;

    if (ulOpenMode & OPEN_SHARE_DENYREADWRITE)
        params.CreateFlags |= SHFL_CF_ACCESS_DENYALL;
    else if (ulOpenMode & OPEN_SHARE_DENYWRITE)
        params.CreateFlags |= SHFL_CF_ACCESS_DENYWRITE;
    else if (ulOpenMode & OPEN_SHARE_DENYREAD)
        params.CreateFlags |= SHFL_CF_ACCESS_DENYREAD;
    else if (ulOpenMode & OPEN_SHARE_DENYNONE)
        params.CreateFlags |= SHFL_CF_ACCESS_DENYNONE;

    params.CreateFlags |= SHFL_CF_ACCESS_ATTR_READWRITE;

    if (usAttr & FILE_READONLY)
        params.CreateFlags &= ~SHFL_CF_ACCESS_ATTR_WRITE;

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxsfStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);
    rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);
    RTMemFree(pwsz);
    RTMemFree(pszFullName);

    if (!RT_SUCCESS(rc))
    {
        log("VbglR0SfCreate returned %d\n", rc);
        free_shflstring(path);
        hrc = vbox_err_to_os2_err(rc);
        goto FS32_OPENCREATEEXIT;
    }

    psffsd->filebuf = (PFILEBUF)RTMemAlloc(sizeof(FILEBUF));

    if (!psffsd->filebuf)
    {
        log("couldn't allocate file buf\n");
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    log("filebuf=%x\n", psffsd->filebuf);

    psffsd->filebuf->handle = params.Handle;
    psffsd->filebuf->path = path;

    psffsi->sfi_positionl = 0;
    psffsi->sfi_position = 0;

    psffsi->sfi_sizel = params.Info.cbObject;
    psffsi->sfi_size = (LONG)params.Info.cbObject;

    /* Creation time   */
    RTTimeExplode(&time, &params.Info.BirthTime);

    t.hours = time.u8Hour;
    t.minutes = time.u8Minute;
    t.twosecs = time.u8Second / 2;
    d.year = time.i32Year - 1980;
    d.month = time.u8Month;
    d.day = time.u8MonthDay;
    psffsi->sfi_ctime = *(PUSHORT)&t;
    psffsi->sfi_cdate = *(PUSHORT)&d;

    /* Last access time   */
    RTTimeExplode(&time, &params.Info.AccessTime);

    t.hours = time.u8Hour;
    t.minutes = time.u8Minute;
    t.twosecs = time.u8Second / 2;
    d.year = time.i32Year - 1980;
    d.month = time.u8Month;
    d.day = time.u8MonthDay;
    psffsi->sfi_atime = *(PUSHORT)&t;
    psffsi->sfi_adate = *(PUSHORT)&d;

    /* Last write time   */
    RTTimeExplode(&time, &params.Info.ModificationTime);

    t.hours = time.u8Hour;
    t.minutes = time.u8Minute;
    t.twosecs = time.u8Second / 2;
    d.year = time.i32Year - 1980;
    d.month = time.u8Month;
    d.day = time.u8MonthDay;
    psffsi->sfi_mtime = *(PUSHORT)&t;
    psffsi->sfi_mdate = *(PUSHORT)&d;

    // @todo omit ST_SCREAT | ST_PCREAT if not creating the file
    psffsi->sfi_tstamp = ST_SCREAT | ST_PCREAT | ST_SREAD | ST_PREAD | ST_SWRITE | ST_PWRITE;

    psffsi->sfi_DOSattr = VBoxToOS2Attr(params.Info.Attr.fMode);

FS32_OPENCREATEEXIT:
    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_CLOSE(ULONG type, ULONG IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    APIRET hrc = NO_ERROR;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    PFILEBUF filebuf = psffsd->filebuf;
    int rc;

    log("VBOXSF: FS32_CLOSE(%lx, %lx)\n", type, IOflag);

    if (type != 2)
    {
        hrc = NO_ERROR;
        goto FS32_CLOSEEXIT;
    }

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    rc = VbglR0SfClose(&g_clientHandle, &pvboxsfvp->map, filebuf->handle);
    hrc = vbox_err_to_os2_err(rc);
    free_shflstring(filebuf->path);
    //__asm__ __volatile__ (".byte 0xcc\n\t");
    log("filebuf=%x\n", filebuf);
    RTMemFree(filebuf);

FS32_CLOSEEXIT:
    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_COMMIT(ULONG type, ULONG IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    log("VBOXSF: FS32_COMMIT(%lx, %lx)\n", type, IOflag);
    return NO_ERROR;
}


extern "C" APIRET APIENTRY
FS32_CHGFILEPTRL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG off, ULONG ulMethod, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    LONGLONG llNewOffset = 0;

    log("VBOXSF: FS32_CHGFILEPTRL(%lld, %lx, %lx)\n", off, ulMethod, IOflag);

    switch (ulMethod)
    {
        case 0: /* relative to the beginning */
            llNewOffset = off;
            break;

        case 1: /* relative to the current position */
            llNewOffset = psffsi->sfi_positionl + off;
            break;

        case 2: /* relative to the end of file */
            llNewOffset = psffsi->sfi_sizel + off;
    }
    if (llNewOffset < 0)
    {
        hrc = ERROR_NEGATIVE_SEEK;
        goto FS32_CHGFILEPTRLEXIT;
    }
    if (llNewOffset != psffsi->sfi_positionl)
        psffsi->sfi_positionl = llNewOffset;

    psffsi->sfi_position = (LONG)psffsi->sfi_positionl;

FS32_CHGFILEPTRLEXIT:
    log(" => %d\n", hrc);
    return hrc;
}


/** Forwards the call to FS32_CHGFILEPTRL. */
extern "C" APIRET APIENTRY
FS32_CHGFILEPTR(PSFFSI psffsi, PVBOXSFFSD psffsd, LONG off, ULONG ulMethod, ULONG IOflag)
{
    log("VBOXSF: FS32_CHGFILEPTR(%ld, %lx, %lx)\n", off, ulMethod, IOflag);
    return FS32_CHGFILEPTRL(psffsi, psffsd, off, ulMethod, IOflag);
}


DECLASM(int)
FS32_FILEINFO(ULONG flag, PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG level,
              PBYTE pData, ULONG cbData, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params = {0};
    USHORT usNeededSize;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    PSHFLDIRINFO file = NULL;
    uint32_t len = sizeof(SHFLDIRINFO);
    int rc;

    log("VBOXSF: FS32_FILEINFO(%lx, %lx, %lx)\n", flag, level, IOflag);

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    switch (flag)
    {
        case 0: // retrieve
            {
                switch (level)
                {
                    case FIL_STANDARD:
                        usNeededSize = sizeof(FILESTATUS);
                        break;

                    case FIL_STANDARDL:
                        usNeededSize = sizeof(FILESTATUS3L);
                        break;

                    case FIL_QUERYEASIZE:
                        usNeededSize = sizeof(FILESTATUS2);
                        break;

                    case FIL_QUERYEASIZEL:
                        usNeededSize = sizeof(FILESTATUS4L);
                        break;

                    case FIL_QUERYEASFROMLIST:
                    case FIL_QUERYEASFROMLISTL:
                    case 4:
                        usNeededSize = sizeof(EAOP);
                        break;

                    default:
                        hrc = ERROR_INVALID_LEVEL;
                        goto FS32_FILEINFOEXIT;
                }

                if (cbData < usNeededSize)
                {
                    hrc = ERROR_BUFFER_OVERFLOW;
                    goto FS32_FILEINFOEXIT;
                }

                file = (PSHFLDIRINFO)RTMemAlloc(len);

                if (! file)
                {
                    log("Not enough memory 1\n");
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto FS32_FILEINFOEXIT;
                }

                log("pvboxsfvp->map=%x\n", pvboxsfvp->map);
                log("psffsd->filebuf->handle=%x\n", psffsd->filebuf->handle);

                rc = VbglR0SfFsInfo(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle,
                                    SHFL_INFO_GET | SHFL_INFO_FILE, &len, file);

                hrc = vbox_err_to_os2_err(rc);

                if (RT_FAILURE(rc))
                {
                    log("VbglR0SfFsInfo failed: %d\n", rc);
                    goto FS32_FILEINFOEXIT;
                }

                if (level == FIL_STANDARD    || level == FIL_STANDARDL ||
                    level == FIL_QUERYEASIZE || level == FIL_QUERYEASIZEL)
                {
                    RTTIME time;
                    FDATE Date;
                    FTIME Time;
                    /* Creation time   */
                    RTTimeExplode(&time, &file->Info.BirthTime);
                    Date.day = time.u8MonthDay;
                    Date.month = time.u8Month;
                    Date.year = time.i32Year - 1980;
                    Time.twosecs = time.u8Second / 2;
                    Time.minutes = time.u8Minute;
                    Time.hours = time.u8Hour;
                    memcpy(&psffsi->sfi_cdate, &Date, sizeof(USHORT));
                    memcpy(&psffsi->sfi_ctime, &Time, sizeof(USHORT));
                    /* Last access time   */
                    RTTimeExplode(&time, &file->Info.AccessTime);
                    Date.day = time.u8MonthDay;
                    Date.month = time.u8Month;
                    Date.year = time.i32Year - 1980;
                    Time.twosecs = time.u8Second / 2;
                    Time.minutes = time.u8Minute;
                    Time.hours = time.u8Hour;
                    memcpy(&psffsi->sfi_adate, &Date, sizeof(USHORT));
                    memcpy(&psffsi->sfi_atime, &Time, sizeof(USHORT));
                    /* Last write time   */
                    RTTimeExplode(&time, &file->Info.ModificationTime);
                    Date.day = time.u8MonthDay;
                    Date.month = time.u8Month;
                    Date.year = time.i32Year - 1980;
                    Time.twosecs = time.u8Second / 2;
                    Time.minutes = time.u8Minute;
                    Time.hours = time.u8Hour;
                    memcpy(&psffsi->sfi_mdate, &Date, sizeof(USHORT));
                    memcpy(&psffsi->sfi_mtime, &Time, sizeof(USHORT));
                    psffsi->sfi_DOSattr = VBoxToOS2Attr(file->Info.Attr.fMode);
                }

                switch (level)
                {
                    case FIL_STANDARD:
                        {
                            FILESTATUS filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;
                            /* Creation time   */
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus.cbFile = (ULONG)file->Info.cbObject;
                            filestatus.cbFileAlloc = (ULONG)file->Info.cbAllocated;
                            KernCopyOut(pData, &filestatus, sizeof(FILESTATUS));
                            break;
                        }
 
                    case FIL_STANDARDL:
                        {
                            FILESTATUS3L filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;
                            /* Creation time   */
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus.cbFile = file->Info.cbObject;
                            filestatus.cbFileAlloc = file->Info.cbAllocated;
                            KernCopyOut(pData, &filestatus, sizeof(FILESTATUS3L));
                            break;
                        }

                    case FIL_QUERYEASIZE:
                        {
                            FILESTATUS2 filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;
                            /* Creation time   */
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus.cbFile = (ULONG)file->Info.cbObject;
                            filestatus.cbFileAlloc = (ULONG)file->Info.cbAllocated;
                            filestatus.cbList = sizeof(filestatus.cbList);
                            KernCopyOut(pData, &filestatus, sizeof(FILESTATUS2));
                            break;
                        }

                    case FIL_QUERYEASIZEL:
                        {
                            FILESTATUS4L filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;
                            /* Creation time   */
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus.fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus.ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus.attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus.cbFile = file->Info.cbObject;
                            filestatus.cbFileAlloc = file->Info.cbAllocated;
                            filestatus.cbList = sizeof(filestatus.cbList);
                            KernCopyOut(pData, &filestatus, sizeof(FILESTATUS4L));
                            break;
                        }

                    case FIL_QUERYEASFROMLIST:
                    case FIL_QUERYEASFROMLISTL:
                        {
                            EAOP filestatus;
                            KernCopyIn(&filestatus, pData, sizeof(EAOP));
                            PFEALIST pFEA = (PFEALIST)KernSelToFlat((ULONG)filestatus.fpFEAList);
                            // @todo: get empty EAs
                            memset(pFEA, 0, (USHORT)pFEA->cbList);
                            pFEA->cbList = sizeof(pFEA->cbList);
                            KernCopyOut(pData, &filestatus, sizeof(EAOP));
                            break;
                        }

                    case 4: // FIL_QUERYALLEAS
                        {
                            EAOP filestatus;
                            KernCopyIn(&filestatus, pData, sizeof(EAOP));
                            PFEALIST pFEA = (PFEALIST)KernSelToFlat((ULONG)filestatus.fpFEAList);
                            memset(pFEA, 0, (USHORT)pFEA->cbList);
                            pFEA->cbList = sizeof(pFEA->cbList);
                            KernCopyOut(pData, &filestatus, sizeof(EAOP));
                            break;
                        }

                    default:
                        hrc = ERROR_INVALID_LEVEL;
                        goto FS32_FILEINFOEXIT;
                }

                hrc = NO_ERROR;
            }
            break;

        case 1: // set
            {
                if (!(psffsi->sfi_mode & OPEN_ACCESS_WRITEONLY) &&
                    !(psffsi->sfi_mode & OPEN_ACCESS_READWRITE))
                {
                    hrc = ERROR_ACCESS_DENIED;
                    goto FS32_FILEINFOEXIT;
                }

                file = (PSHFLDIRINFO)RTMemAlloc(len);

                if (! file)
                {
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto FS32_FILEINFOEXIT;
                }

                switch (level)
                {
                    case FIL_STANDARD:
                        {
                            USHORT usMask;
                            FILESTATUS filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            if (cbData < sizeof(filestatus))
                            {
                                hrc = ERROR_INSUFFICIENT_BUFFER;
                                goto FS32_FILEINFOEXIT;
                            }

                            KernCopyIn(&filestatus, pData, sizeof(filestatus));

                            /* Creation time   */
                            memcpy(&Date, &filestatus.fdateCreation, sizeof(USHORT));
                            memcpy(&Time, &filestatus.ftimeCreation, sizeof(USHORT));
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            RTTimeImplode(&file->Info.BirthTime, &time);
                            /* Last access time   */
                            memcpy(&Date, &filestatus.fdateLastAccess, sizeof(USHORT));
                            memcpy(&Time, &filestatus.ftimeLastAccess, sizeof(USHORT));
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            RTTimeImplode(&file->Info.AccessTime, &time);
                            /* Last write time   */
                            memcpy(&Date, &filestatus.fdateLastWrite, sizeof(USHORT));
                            memcpy(&Time, &filestatus.ftimeLastWrite, sizeof(USHORT));
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            RTTimeImplode(&file->Info.ModificationTime, &time);
                            
                            file->Info.cbObject = filestatus.cbFile;
                            file->Info.cbAllocated = filestatus.cbFileAlloc;
                            file->Info.Attr.fMode = OS2ToVBoxAttr(filestatus.attrFile);

                            usMask = ~(FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);

                            if (filestatus.attrFile & usMask)
                            {
                                hrc = ERROR_ACCESS_DENIED;
                                goto FS32_FILEINFOEXIT;
                            }

                            usMask = 0;
                            if (memcmp(&filestatus.fdateCreation, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus.ftimeCreation, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SCREAT;
                                psffsi->sfi_tstamp |= ST_PCREAT;
                                memcpy(&psffsi->sfi_ctime, &filestatus.ftimeCreation, sizeof(USHORT));
                                memcpy(&psffsi->sfi_cdate, &filestatus.fdateCreation, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus.fdateLastWrite, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus.ftimeLastWrite, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SWRITE;
                                psffsi->sfi_tstamp |= ST_PWRITE;
                                memcpy(&psffsi->sfi_mtime, &filestatus.ftimeLastWrite, sizeof(USHORT));
                                memcpy(&psffsi->sfi_mdate, &filestatus.fdateLastWrite, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus.fdateLastAccess, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus.ftimeLastAccess, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SREAD;
                                psffsi->sfi_tstamp |= ST_PREAD;
                                memcpy(&psffsi->sfi_atime, &filestatus.ftimeLastAccess, sizeof(USHORT));
                                memcpy(&psffsi->sfi_adate, &filestatus.fdateLastAccess, sizeof(USHORT));
                            }

                            if (psffsi->sfi_DOSattr != (BYTE)filestatus.attrFile)
                                psffsi->sfi_DOSattr = (BYTE)filestatus.attrFile;

                            break;
                        }

                    case FIL_STANDARDL:
                        {
                            USHORT usMask;
                            FILESTATUS3L filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            if (cbData < sizeof(filestatus))
                            {
                                hrc = ERROR_INSUFFICIENT_BUFFER;
                                goto FS32_FILEINFOEXIT;
                            }

                            KernCopyIn(&filestatus, pData, sizeof(filestatus));

                            /* Creation time   */
                            memcpy(&Date, &filestatus.fdateCreation, sizeof(USHORT));
                            memcpy(&Time, &filestatus.ftimeCreation, sizeof(USHORT));
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            RTTimeImplode(&file->Info.BirthTime, &time);
                            /* Last access time   */
                            memcpy(&Date, &filestatus.fdateLastAccess, sizeof(USHORT));
                            memcpy(&Time, &filestatus.ftimeLastAccess, sizeof(USHORT));
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            RTTimeImplode(&file->Info.AccessTime, &time);
                            /* Last write time   */
                            memcpy(&Date, &filestatus.fdateLastWrite, sizeof(USHORT));
                            memcpy(&Time, &filestatus.ftimeLastWrite, sizeof(USHORT));
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            RTTimeImplode(&file->Info.ModificationTime, &time);
                            
                            file->Info.cbObject = filestatus.cbFile;
                            file->Info.cbAllocated = filestatus.cbFileAlloc;
                            file->Info.Attr.fMode = OS2ToVBoxAttr(filestatus.attrFile);

                            usMask = ~(FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);

                            if (filestatus.attrFile & usMask)
                            {
                                hrc = ERROR_ACCESS_DENIED;
                                goto FS32_FILEINFOEXIT;
                            }

                            usMask = 0;
                            if (memcmp(&filestatus.fdateCreation, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus.ftimeCreation, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SCREAT;
                                psffsi->sfi_tstamp |= ST_PCREAT;
                                memcpy(&psffsi->sfi_ctime, &filestatus.ftimeCreation, sizeof(USHORT));
                                memcpy(&psffsi->sfi_cdate, &filestatus.fdateCreation, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus.fdateLastWrite, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus.ftimeLastWrite, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SWRITE;
                                psffsi->sfi_tstamp |= ST_PWRITE;
                                memcpy(&psffsi->sfi_mtime, &filestatus.ftimeLastWrite, sizeof(USHORT));
                                memcpy(&psffsi->sfi_mdate, &filestatus.fdateLastWrite, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus.fdateLastAccess, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus.ftimeLastAccess, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SREAD;
                                psffsi->sfi_tstamp |= ST_PREAD;
                                memcpy(&psffsi->sfi_atime, &filestatus.ftimeLastAccess, sizeof(USHORT));
                                memcpy(&psffsi->sfi_adate, &filestatus.fdateLastAccess, sizeof(USHORT));
                            }

                            if (psffsi->sfi_DOSattr != (BYTE)filestatus.attrFile)
                                psffsi->sfi_DOSattr = (BYTE)filestatus.attrFile;

                            break;
                        }

                    case FIL_QUERYEASIZE:
                    case FIL_QUERYEASIZEL:
                        break;

                    default:
                        hrc = ERROR_INVALID_LEVEL;
                }

                rc = VbglR0SfFsInfo(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle,
                                    SHFL_INFO_SET | SHFL_INFO_FILE, &len, file);

                if (RT_FAILURE(rc))
                {
                    log("VbglR0SfFsInfo failed: %d\n", rc);
                    hrc = vbox_err_to_os2_err(rc);
                    goto FS32_FILEINFOEXIT;
                }
            }
            break;

        default:
            hrc = ERROR_INVALID_FUNCTION;
    }

FS32_FILEINFOEXIT:
    if (file)
        RTMemFree(file);

    log(" => %d\n", hrc);
    return hrc;
}

int chsize(PVBOXSFVP pvboxsfvp, PVBOXSFFSD psffsd, ULONG size)
{
    PSHFLFSOBJINFO pObjInfo = NULL;
    uint32_t cbBuf = sizeof(SHFLFSOBJINFO);
    int rc;

    pObjInfo = (PSHFLFSOBJINFO)RTMemAlloc(cbBuf);

    if (! pObjInfo)
    {
        return VERR_NO_MEMORY;
    }

    memset(pObjInfo, 0, cbBuf);
    pObjInfo->cbObject = size;

    rc = VbglR0SfFsInfo(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle,
                        SHFL_INFO_SET | SHFL_INFO_SIZE, &cbBuf, (PSHFLDIRINFO)pObjInfo);

    if (pObjInfo)
        RTMemFree(pObjInfo);

    return rc;
}

DECLASM(int)
FS32_NEWSIZEL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG cbFile, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    PSHFLSTRING path;
    int rc;

    log("VBOXSF: FS32_NEWSIZEL(%lld, %lx)\n", cbFile, IOflag);

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    rc = chsize(pvboxsfvp, psffsd, cbFile);

    hrc = vbox_err_to_os2_err(rc);

FS32_NEWSIZELEXIT:
    log(" => %d\n", hrc);

    return hrc;
}


extern "C" APIRET APIENTRY
FS32_READ(PSFFSI psffsi, PVBOXSFFSD psffsd, PVOID pvData, PULONG pcb, ULONG IOflag)
{
    APIRET hrc;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    uint8_t *pBuf;
    ULONG cb = *pcb;
    int rc;

    log("VBOXSF: FS32_READ(%lx)\n", IOflag);

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    pBuf = (uint8_t *)RTMemAlloc(*pcb);

    if (psffsi->sfi_positionl > psffsi->sfi_sizel)
    {
        *pcb = 0;
    }
    else if (*pcb > psffsi->sfi_sizel - psffsi->sfi_positionl)
    {
        *pcb = psffsi->sfi_sizel - psffsi->sfi_positionl;
    }

    memset(pBuf, 0, cb);

    rc = VbglR0SfRead(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle, 
                      psffsi->sfi_positionl, (uint32_t *)pcb, pBuf, false);

    if (RT_SUCCESS(rc))
    {
        KernCopyOut((char *)pvData, pBuf, *pcb);
        psffsi->sfi_positionl += *pcb;
        psffsi->sfi_position  += *pcb;
    }

    psffsi->sfi_tstamp |= (ST_SREAD | ST_PREAD);

    hrc = vbox_err_to_os2_err(rc);

FS32_READEXIT:
    if (pBuf)
        RTMemFree(pBuf);

    log(" => %d\n", hrc);
    return hrc;
}


extern "C" APIRET APIENTRY
FS32_WRITE(PSFFSI psffsi, PVBOXSFFSD psffsd, PVOID pvData, PULONG pcb, ULONG IOflag)
{
    APIRET hrc;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    uint32_t cbNewPos;
    uint8_t *pBuf;
    int rc;

    log("VBOXSF: FS32_WRITE(%lx)\n", IOflag);

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    pBuf = (uint8_t *)RTMemAlloc(*pcb);

    if (! pBuf)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_WRITEEXIT;
    }

    KernCopyIn(pBuf, (char *)pvData, *pcb);

    cbNewPos = psffsi->sfi_positionl + *pcb;

    if (cbNewPos > psffsi->sfi_sizel)
    {
        rc = chsize(pvboxsfvp, psffsd, cbNewPos);

        if (rc)
        {
            hrc = vbox_err_to_os2_err(rc);
            goto FS32_WRITEEXIT;
        }

        psffsi->sfi_sizel = cbNewPos;
        psffsi->sfi_size  = cbNewPos;
    }

    rc = VbglR0SfWrite(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle, 
                       psffsi->sfi_positionl, (uint32_t *)pcb, pBuf, false);

    if (RT_SUCCESS(rc))
    {
        psffsi->sfi_positionl += *pcb;
        psffsi->sfi_position  += *pcb;
        psffsi->sfi_tstamp |= (ST_SWRITE | ST_PWRITE);
    }

    hrc = vbox_err_to_os2_err(rc);

FS32_WRITEEXIT:
    if (pBuf)
        RTMemFree(pBuf);

    log(" => %d\n", hrc);
    return hrc;
}


extern "C" APIRET APIENTRY
FS32_READFILEATCACHE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG IOflag, LONGLONG off, ULONG pcb, KernCacheList_t **ppCacheList)
{
    log("VBOXSF: FS32_READFILEATCACHE(%lx, %lld)\n", IOflag, off);
    return ERROR_NOT_SUPPORTED;
}


extern "C" APIRET APIENTRY
FS32_RETURNFILECACHE(KernCacheList_t *pCacheList)
{
    log("VBOXSF: FS32_RETURNFILECACHE\n");
    return ERROR_NOT_SUPPORTED;
}


/* oddments */

DECLASM(int)
FS32_CANCELLOCKREQUESTL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl *pLockRange)
{
    log("VBOXSF: FS32_CANCELLOCKREQUESTL\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_CANCELLOCKREQUEST(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock *pLockRange)
{
    log("VBOXSF: FS32_CANCELLOCKREQUEST\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILELOCKSL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl *pUnLockRange,
                struct filelockl *pLockRange, ULONG timeout, ULONG flags)
{
    log("VBOXSF: FS32_FILELOCKSL(%lu, %lx)\n", timeout, flags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILELOCKS(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock *pUnLockRange,
               struct filelock *pLockRange, ULONG timeout, ULONG flags)
{
    log("VBOXSF: FS32_FILELOCKS(%lu, %lx)\n", timeout, flags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_IOCTL(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT cat, USHORT func,
           PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
           PVOID pData, USHORT lenData, PUSHORT plenDataIO)
{
    log("VBOXSF: FS32_IOCTL(%x, %x)\n", cat, func);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pCmdList, USHORT cbCmdList,
            PUSHORT poError, USHORT IOflag)
{
    log("VBOXSF: FS32_FILEIO(%x)\n", IOflag);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_NMPIPE(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT OpType, union npoper *pOpRec,
            PBYTE pData, PCSZ pszName)
{
    log("VBOXSF: FS32_NPIPE(%x, %s)\n", OpType, pszName);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_VERIFYUNCNAME(ULONG flag, PCSZ pszName)
{
    log("VBOXSF: FS32_VERIFYUNCNAME(%x, %s)\n", flag, pszName);

    if (! stricmp((char *)pszName, "\\\\vboxsvr"))
        return NO_ERROR;
    else
        return ERROR_INVALID_NAME;
}

DECLASM(int)
FS32_OPENPAGEFILE(PULONG pFlag, PULONG pcMaxReq, PCSZ pszName, PSFFSI psffsi, PVBOXSFFSD psffsd,
                  USHORT ulOpenMode, USHORT usOpenFlag, USHORT usAttr, ULONG Reserved)
{
    log("VBOXSF: FS32_OPENPAGEFILE(%s, %x, %x, %x)\n", pszName, ulOpenMode, usOpenFlag, usAttr);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_SETSWAP(PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    log("VBOXSF: FS32_SETSWAP\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_ALLOCATEPAGESPACE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cb, USHORT cbWantContig)
{
    log("VBOXSF: FS32_ALLOCATEPAGESPACE(%lu, %u)\n", cb, cbWantContig);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_DOPAGEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, struct PageCmdHeader *pList)
{
    log("VBOXSF: FS32_DOPAGEIO\n");
    return ERROR_NOT_SUPPORTED;
}

