/** $Id$ */
/** @file
 * VBoxSF - OS/2 Shared Folders, the file level IFS EPs.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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

extern VBSFCLIENT g_clientHandle;

/**
 * Resolve a file name to a share map handle and the path relative to this share
 *
 *  @param   Path
 *  @param   pcdfsi
 *
 */
APIRET APIENTRY parseFileName(const char *pszPath, PCDFSI pcdfsi,
                              char *pszParsedPath, int *pcbParsedPath, VBSFMAP *map)
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
    SHFLCREATEPARMS params;
    APIRET hrc = NO_ERROR;
    char *pszFullName;
    int cbFullName;
    VBSFMAP map;
    char *pwsz;
    PSHFLSTRING path;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    int rc;

    dprintf("VBOXSF: FS32_OPENCREATE(%s, %lx, %x, %x)\n", pszName, ulOpenMode, usOpenFlags, usAttr);

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP);
    cbFullName = CCHMAXPATHCOMP;

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map);

    if (hrc)
    {
        dprintf("Filename parse error!\n");
        goto FS32_OPENCREATEEXIT;
    }

    dprintf("pszFullName=%s\n", pszFullName); 

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

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP);
    vboxsfStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);
    rc = vboxCallCreate(&g_clientHandle, &map, path, &params);
    RTMemFree(pwsz);
    RTMemFree(pszFullName);

    if (!RT_SUCCESS(rc))
    {
        dprintf("vboxCallCreate returned %d\n", rc);
        free_shflstring(path);
        hrc = vbox_err_to_os2_err(rc);
        goto FS32_OPENCREATEEXIT;
    }

    psffsd->filebuf = (PFILEBUF)RTMemAlloc(sizeof(FILEBUF));

    if (!psffsd->filebuf)
    {
        dprintf("couldn't allocate file buf\n");
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    dprintf("filebuf=%x\n", psffsd->filebuf);

    psffsd->filebuf->handle = params.Handle;
    psffsd->filebuf->path = path;

FS32_OPENCREATEEXIT:
    dprintf(" => %d\n", hrc);
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

    dprintf("VBOXSF: FS32_CLOSE(%lx, %lx)\n", type, IOflag);

    if (type != 2)
    {
        hrc = NO_ERROR;
        goto FS32_CLOSEEXIT;
    }

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    rc = vboxCallClose(&g_clientHandle, &pvboxsfvp->map, filebuf->handle);
    hrc = vbox_err_to_os2_err(rc);
    free_shflstring(filebuf->path);
    //__asm__ __volatile__ (".byte 0xcc\n\t");
    dprintf("filebuf=%x\n", filebuf);
    RTMemFree(filebuf);

FS32_CLOSEEXIT:
    dprintf(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_COMMIT(ULONG type, ULONG IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    dprintf("VBOXSF: FS32_COMMIT(%lx, %lx)\n", type, IOflag);
    return NO_ERROR;
}


extern "C" APIRET APIENTRY
FS32_CHGFILEPTRL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG off, ULONG ulMethod, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    LONGLONG llNewOffset = 0;

    dprintf("VBOXSF: FS32_CHGFILEPTRL(%lld, %lx, %lx)\n", off, ulMethod, IOflag);

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
    dprintf(" => %d\n", hrc);
    return hrc;
}


/** Forwards the call to FS32_CHGFILEPTRL. */
extern "C" APIRET APIENTRY
FS32_CHGFILEPTR(PSFFSI psffsi, PVBOXSFFSD psffsd, LONG off, ULONG ulMethod, ULONG IOflag)
{
    dprintf("VBOXSF: FS32_CHGFILEPTR(%ld, %lx, %lx)\n", off, ulMethod, IOflag);
    return FS32_CHGFILEPTRL(psffsi, psffsd, off, ulMethod, IOflag);
}


DECLASM(int)
FS32_FILEINFO(ULONG flag, PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG level,
              PBYTE pData, ULONG cbData, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params;
    USHORT usNeededSize;
    PSHFLSTRING path;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    PSHFLDIRINFO file = NULL;
    uint32_t len = sizeof(SHFLDIRINFO);
    uint32_t num_files = 0;
    uint32_t index = 0;
    char *str, *p, *lastslash;
    char *pszDirName;
    char *pwsz;
    int rc;

    dprintf("VBOXSF: FS32_FILEINFO(%lx, %lx, %lx)\n", flag, level, IOflag);

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
                    dprintf("Not enough memory 1\n");
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto FS32_FILEINFOEXIT;
                }

                dprintf("file=%x\n", file);

                str = (char *)RTMemAlloc(psffsd->filebuf->path->u16Length);

                if (! str)
                {
                    dprintf("Not enough memory 2\n");
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto FS32_FILEINFOEXIT;
                }

                strcpy(str, "\\");
                strcat(str, (char *)psffsd->filebuf->path->String.utf8);

                lastslash = p = str;

                // get last backslash
                do {
                    lastslash = p;
                    p = strchr(p + 1, '\\');
                } while (p && p < str + strlen(str));

                // cut off file part from directory part
                str[lastslash - str + 1] = '\0';
                pszDirName = str + 1;

                dprintf("pszDirName=%s\n", pszDirName);

                pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP);
                vboxsfStrToUtf8(pwsz, (char *)pszDirName);

                path = make_shflstring((char *)pwsz);
                rc = vboxCallCreate(&g_clientHandle, &pvboxsfvp->map, path, &params);
                RTMemFree(pwsz);
                RTMemFree(str);

                if (!RT_SUCCESS(rc))
                {
                    dprintf("vboxCallCreate returned %d\n", rc);
                    free_shflstring(path);
                    hrc = vbox_err_to_os2_err(rc);
                    goto FS32_FILEINFOEXIT;
                }

                dprintf("path=%s\n", psffsd->filebuf->path->String.utf8);
                dprintf("pvboxsfvp->map=%x\n", pvboxsfvp->map);
                dprintf("params.Handle=%x\n", params.Handle);
                dprintf("len=%x\n", len);
                dprintf("num_files=%x\n", num_files);

                rc = vboxCallFSInfo(&g_clientHandle, &pvboxsfvp->map, params.Handle,
                                    SHFL_INFO_GET | SHFL_INFO_FILE, &len, file);

                hrc = vbox_err_to_os2_err(rc);

                if (RT_FAILURE(rc))
                {
                    dprintf("vboxCallFSInfo failed: %d\n", rc);
                    goto FS32_FILEINFOEXIT;
                }

                vboxCallClose(&g_clientHandle, &pvboxsfvp->map, params.Handle);

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
                            PFEALIST pFEA = filestatus.fpFEAList;
                            // @todo: get empty EAs
                            memset(pFEA, 0, (USHORT)pFEA->cbList);
                            pFEA->cbList = sizeof(pFEA->cbList);
                            KernCopyOut(pData, &filestatus, sizeof(FILESTATUS4L));
                            break;
                        }

                    case 4:
                        {
                            EAOP filestatus;
                            KernCopyIn(&filestatus, pData, sizeof(EAOP));
                            PFEALIST pFEA = filestatus.fpFEAList;
                            memset(pFEA, 0, (USHORT)pFEA->cbList);
                            pFEA->cbList = sizeof(pFEA->cbList);
                            KernCopyOut(pData, &filestatus, sizeof(FILESTATUS4L));
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

                rc = vboxCallFSInfo(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle,
                                    SHFL_INFO_SET | SHFL_INFO_FILE, &len, file);

                if (RT_FAILURE(rc))
                {
                    dprintf("vboxCallFSInfo failed: %d\n", rc);
                    hrc = vbox_err_to_os2_err(rc);
                    goto FS32_FILEINFOEXIT;
                }
            }
            break;

        default:
            hrc = ERROR_INVALID_FUNCTION;
    }

FS32_FILEINFOEXIT:
    dprintf("file=%x\n", file);
    //__asm__ __volatile__ (".byte 0xcc\n\t");
    if (file)
        RTMemFree(file);
    dprintf(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_NEWSIZEL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG cbFile, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    PSHFLFSOBJINFO pObjInfo = NULL;
    PSHFLSTRING path;
    uint32_t cbBuf = sizeof(SHFLFSOBJINFO);
    int rc;

    dprintf("VBOXSF: FS32_NEWSIZEL(%lld, %lx)\n", cbFile, IOflag);

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    pObjInfo = (PSHFLFSOBJINFO)RTMemAlloc(cbBuf);
    if (!pObjInfo)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_NEWSIZELEXIT;
    }

    memset(pObjInfo, 0, cbBuf);
    pObjInfo->cbObject = cbFile;

    rc = vboxCallFSInfo(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle,
                        SHFL_INFO_SET | SHFL_INFO_SIZE, &cbBuf, (PSHFLDIRINFO)pObjInfo);

    hrc = vbox_err_to_os2_err(rc);

FS32_NEWSIZELEXIT:
    dprintf(" => %d\n", hrc);

    if (pObjInfo)
        RTMemFree(pObjInfo);

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
    int rc;

    dprintf("VBOXSF: FS32_READ(%lx)\n", IOflag);

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    pBuf = (uint8_t *)RTMemAlloc(*pcb);

    rc = vboxCallRead(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle, 
                      psffsi->sfi_positionl, (uint32_t *)pcb, pBuf, false);

    if (RT_SUCCESS(rc))
    {
        KernCopyOut((char *)pvData, pBuf, *pcb);
        psffsi->sfi_positionl += *pcb;
        psffsi->sfi_position  += *pcb;
    }

    hrc = vbox_err_to_os2_err(rc);
    RTMemFree(pBuf);

FS32_READEXIT:
    dprintf(" => %d\n", hrc);
    return hrc;
}


extern "C" APIRET APIENTRY
FS32_WRITE(PSFFSI psffsi, PVBOXSFFSD psffsd, PVOID pvData, PULONG pcb, ULONG IOflag)
{
    APIRET hrc;
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    uint8_t *pBuf;
    int rc;

    dprintf("VBOXSF: FS32_WRITE(%lx)\n", IOflag);

    FSH32_GETVOLPARM(psffsi->sfi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    pBuf = (uint8_t *)RTMemAlloc(*pcb);
    KernCopyIn(pBuf, (char *)pvData, *pcb);

    rc = vboxCallWrite(&g_clientHandle, &pvboxsfvp->map, psffsd->filebuf->handle, 
                       psffsi->sfi_positionl, (uint32_t *)pcb, pBuf, false);

    if (RT_SUCCESS(rc))
    {
        psffsi->sfi_positionl += *pcb;
        psffsi->sfi_position  += *pcb;
    }

    hrc = vbox_err_to_os2_err(rc);
    RTMemFree(pBuf);

FS32_WRITEEXIT:
    dprintf(" => %d\n", hrc);
    return hrc;
}


extern "C" APIRET APIENTRY
FS32_READFILEATCACHE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG IOflag, LONGLONG off, ULONG pcb, KernCacheList_t **ppCacheList)
{
    dprintf("VBOXSF: FS32_READFILEATCACHE(%lx, %lld)\n", IOflag, off);
    return ERROR_NOT_SUPPORTED;
}


extern "C" APIRET APIENTRY
FS32_RETURNFILECACHE(KernCacheList_t *pCacheList)
{
    dprintf("VBOXSF: FS32_RETURNFILECACHE\n");
    return ERROR_NOT_SUPPORTED;
}


/* oddments */

DECLASM(int)
FS32_CANCELLOCKREQUESTL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl *pLockRange)
{
    dprintf("VBOXSF: FS32_CANCELLOCKREQUESTL\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_CANCELLOCKREQUEST(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock *pLockRange)
{
    dprintf("VBOXSF: FS32_CANCELLOCKREQUEST\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILELOCKSL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl *pUnLockRange,
                struct filelockl *pLockRange, ULONG timeout, ULONG flags)
{
    dprintf("VBOXSF: FS32_FILELOCKSL(%lu, %lx)\n", timeout, flags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILELOCKS(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock *pUnLockRange,
               struct filelock *pLockRange, ULONG timeout, ULONG flags)
{
    dprintf("VBOXSF: FS32_FILELOCKS(%lu, %lx)\n", timeout, flags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_IOCTL(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT cat, USHORT func,
           PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
           PVOID pData, USHORT lenData, PUSHORT plenDataIO)
{
    dprintf("VBOXSF: FS32_IOCTL(%x, %x)\n", cat, func);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pCmdList, USHORT cbCmdList,
            PUSHORT poError, USHORT IOflag)
{
    dprintf("VBOXSF: FS32_FILEIO(%x)\n", IOflag);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_NMPIPE(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT OpType, union npoper *pOpRec,
            PBYTE pData, PCSZ pszName)
{
    dprintf("VBOXSF: FS32_NPIPE(%x, %s)\n", OpType, pszName);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_VERIFYUNCNAME(ULONG flag, PCSZ pszName)
{
    dprintf("VBOXSF: FS32_VERIFYUNCNAME(%x, %s)\n", flag, pszName);

    if (! stricmp((char *)pszName, "\\\\vboxsvr"))
        return NO_ERROR;
    else
        return ERROR_INVALID_NAME;
}

DECLASM(int)
FS32_OPENPAGEFILE(PULONG pFlag, PULONG pcMaxReq, PCSZ pszName, PSFFSI psffsi, PVBOXSFFSD psffsd,
                  USHORT ulOpenMode, USHORT usOpenFlag, USHORT usAttr, ULONG Reserved)
{
    dprintf("VBOXSF: FS32_OPENPAGEFILE(%s, %x, %x, %x)\n", pszName, ulOpenMode, usOpenFlag, usAttr);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_SETSWAP(PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    dprintf("VBOXSF: FS32_SETSWAP\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_ALLOCATEPAGESPACE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cb, USHORT cbWantContig)
{
    dprintf("VBOXSF: FS32_ALLOCATEPAGESPACE(%lu, %u)\n", cb, cbWantContig);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_DOPAGEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, struct PageCmdHeader *pList)
{
    dprintf("VBOXSF: FS32_DOPAGEIO\n");
    return ERROR_NOT_SUPPORTED;
}

