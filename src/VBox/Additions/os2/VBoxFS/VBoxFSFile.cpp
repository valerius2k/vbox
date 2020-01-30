/** $Id$ */
/** @file
 * VBoxFS - OS/2 Shared Folders, the file level IFS EPs.
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

uint32_t VBoxToOS2Attr(uint32_t fMode);

/**
 * Resolve a file name to a share map handle and the path relative to this share
 *
 *  @param   Path
 *  @param   pcdfsi
 *
 */
APIRET APIENTRY parseFileName(const char *pszPath, PCDFSI pcdfsi,
                              char *pszParsedPath, int *pcbParsedPath,
                              VBGLSFMAP *map, bool *tmp)
{
    PVPFSI pvpfsi;
    PVPFSD pvpfsd;
    PVBOXSFVP pvboxsfvp;
    char *p, *pszShareName, *pszSrvName, *path = NULL;
    int len;
    APIRET hrc = 0;

    if (! map || ! tmp || 
        ! pszPath || ! pcdfsi ||
        ! pszParsedPath || 
        ! pcbParsedPath ||
        ! *pcbParsedPath)
    {
        hrc = ERROR_INVALID_PARAMETER;
        goto parse_exit;
    }

    *tmp = false;

    FSH32_GETVOLPARM(pcdfsi->cdi_hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;

    path = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! path)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto parse_exit;
    }

    if ( (p = strstr(pszPath, ":"))  && p == pszPath + 1 )
    {
        /* absolute pathname starting from "d:\" */
        len = strlen(p + 2);

        if (len > *pcbParsedPath)
        {
            hrc = ERROR_FILENAME_EXCED_RANGE;
            goto parse_exit;
        }

        strcpy(path, p + 2);
        *map = pvboxsfvp->map;
        hrc = NO_ERROR;
    }
    else if ( (p = strstr(pszPath, "\\\\")) && p == pszPath )
    {
        // UNC name
        char szShareName[CCHMAXPATHCOMP];
        char szSrvName[12];
        SHFLMAPPING mappings[26];
        uint32_t cMappings;
        uint32_t i = 0;
        PSHFLSTRING str;
        int rc = 0;
        char *r;

        p += 2;

        pszSrvName = p;
        p = strchr(pszSrvName, '\\');

        if (! p || p - pszSrvName > 11)
        {
            hrc = ERROR_INVALID_NAME;
            goto parse_exit;
        }

        strncpy(szSrvName, pszSrvName, p - pszSrvName);
        szSrvName[p - pszSrvName] = '\0';
        log("szSrvName=%s\n", szSrvName);

        r = szSrvName;

        while (*r)
        {
            *r = tolower(*r);
            r++;
        }

        if ( strstr(szSrvName, "vboxsrv") != szSrvName &&
             strstr(szSrvName, "vboxsvr") != szSrvName &&
             strstr(szSrvName, "vboxfs") != szSrvName &&
             strstr(szSrvName, "vboxsf") != szSrvName )
        {
            hrc = ERROR_BAD_NET_NAME;
            goto parse_exit;
        }

        cMappings = sizeof(mappings);
        rc = VbglR0SfQueryMappings(&g_clientHandle, mappings, &cMappings);

        if (RT_FAILURE(rc))
        {
            hrc = vbox_err_to_os2_err(rc);
            goto parse_exit;
        }

        if (p)
        {
            p++;

            pszShareName = p;
            p = strchr(pszShareName, '\\');

            if (! p)
            {
                p = pszShareName + strlen(pszShareName);
            }

            if (! p || p - pszShareName > CCHMAXPATHCOMP - 1)
            {
                hrc = ERROR_INVALID_NAME;
                goto parse_exit;
            }

            strncpy(szShareName, pszShareName, p - pszShareName);
            szShareName[p - pszShareName] = '\0';
            log("szShareName=%s\n", szShareName);

            if (p)
            {
                if (*p)
                {
                    p++;
                }
            }
            else
            {
                hrc = ERROR_INVALID_NAME;
                goto parse_exit;
            }

            len = strlen(p);

            if (len > *pcbParsedPath)
            {
                hrc = ERROR_FILENAME_EXCED_RANGE;
                goto parse_exit;
            }

            strcpy(path, p);

            len = sizeof(SHFLSTRING) + 2 * CCHMAXPATHCOMP + 2;
            str = (PSHFLSTRING)RTMemAlloc(len);

            if (! str)
            {
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto parse_exit;
            }

            for (i = 0; i < cMappings; i++)
            {
                rc = VbglR0SfQueryMapName(&g_clientHandle, mappings[i].root, str, len);

                if (RT_SUCCESS(rc))
                {
                    char szFileName[CCHMAXPATHCOMP];

                    vboxfsStrFromUtf8(szFileName, (char *)str->String.utf8, CCHMAXPATHCOMP);

                    if (! stricmp(szShareName, szFileName) )
                    {
                        break;
                    }
                }
            }

            RTMemFree(str);

            if (i == cMappings)
            {
                // not found, create a temporary mapping
                char *pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

                if (! pwsz)
                {
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto parse_exit;
                }

                vboxfsStrToUtf8(pwsz, szShareName);
                str = make_shflstring(pwsz);
                RTMemFree(pwsz);

                rc = VbglR0SfMapFolder(&g_clientHandle, str, map);
                RTMemFree(str);
            
                if (RT_SUCCESS(rc))
                {
                    *tmp = true;
                }
            }
            else
            {
                // found, return an existing mapping
                *map = *(PVBGLSFMAP)&mappings[i].root;
            }
        }
        else
        {
            // root namespace, contains all share names
            //*map = 0;
            hrc = ERROR_INVALID_NAME;
            goto parse_exit;
        }

        hrc = vbox_err_to_os2_err(rc);
    }
    else if ( (p = strstr(pszPath, "\\")) && p == pszPath )
    {
        /* absolute pathname starting from "\" */
        len = strlen(p + 1);

        if (len > *pcbParsedPath)
        {
            hrc = ERROR_FILENAME_EXCED_RANGE;
            goto parse_exit;
        }

        strcpy(path, p + 1);

        *map = pvboxsfvp->map;
        hrc = NO_ERROR;
    }
    else if ( (p = strstr(pszPath, "\\")) && p - pszPath )
    {
        /* relative pathname */
        len = strlen(pcdfsi->cdi_curdir) + strlen(pszPath) + 1;

        if (len > *pcbParsedPath)
        {
            hrc = ERROR_FILENAME_EXCED_RANGE;
            goto parse_exit;
        }

        strcpy(path, pcdfsi->cdi_curdir);
        strcat(path, "\\");
        strcat(path, pszPath);

        *map = pvboxsfvp->map;
        hrc = NO_ERROR;
    }

    strcpy(pszParsedPath, path);
    //*pcbParsedPath = strlen(pszParsedPath) + 1;

parse_exit:
    if (path)
        RTMemFree(path);

    return hrc;
}

DECLASM(int)
FS32_OPENCREATE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, ULONG iCurDirEnd,
                PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG ulOpenMode, ULONG usOpenFlags,
                PUSHORT pusAction, ULONG usAttr, PBYTE pcEABuf, PUSHORT pfgenflag)
{
    PSHFLCREATEPARMS params = NULL;
    APIRET hrc = NO_ERROR;
    char *pszFullName = NULL;
    char *pszParsedPath = NULL;
    int cbParsedPath;
    VBGLSFMAP *map = NULL;
    bool tmp;
    char *pwsz = NULL;
    PSHFLSTRING path;
    RTTIME time;
    FDATE Date;
    FTIME Time;
    int rc;

    log("FS32_OPENCREATE(%s, %lx, %lx, %lx)\n", pszName, ulOpenMode, usOpenFlags, usAttr);

    params = (PSHFLCREATEPARMS)RTMemAlloc(sizeof(SHFLCREATEPARMS));

    if (! params)
    {
        rc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    map = (VBGLSFMAP *)RTMemAlloc(sizeof(VBGLSFMAP));

    if (! map)
    {
        rc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    RT_ZERO(*params);
    params->Handle = SHFL_HANDLE_NIL;

    pszParsedPath = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszParsedPath)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    cbParsedPath = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszName, pcdfsi, pszParsedPath, &cbParsedPath, map, &tmp);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_OPENCREATEEXIT;
    }

    memset(pszFullName, 0, cbParsedPath);

    if ( TranslateName(map, pszParsedPath, pszFullName, TRANSLATE_SHORT_TO_LONG) )
        strcpy( pszFullName, pszParsedPath );

    log("pszFullName=%s\n", pszFullName); 

    if (ulOpenMode & OPEN_FLAGS_DASD)
    {
        hrc = ERROR_NOT_SUPPORTED;
        goto FS32_OPENCREATEEXIT;
    }

    if (ulOpenMode & OPEN_ACCESS_READWRITE)
        params->CreateFlags |= SHFL_CF_ACCESS_READWRITE | SHFL_CF_ACCESS_ATTR_READWRITE;
    else
    {
        if (ulOpenMode & OPEN_ACCESS_WRITEONLY)
            params->CreateFlags |= SHFL_CF_ACCESS_WRITE | SHFL_CF_ACCESS_ATTR_WRITE;
        else
            params->CreateFlags |= SHFL_CF_ACCESS_READ | SHFL_CF_ACCESS_ATTR_READ;
    }

    if (ulOpenMode & OPEN_SHARE_DENYREAD)
        params->CreateFlags |= SHFL_CF_ACCESS_DENYREAD;
    else
    {
        if (ulOpenMode & OPEN_SHARE_DENYREADWRITE)
            params->CreateFlags |= SHFL_CF_ACCESS_DENYALL;
        else if ( (ulOpenMode & OPEN_SHARE_DENYWRITE) ||
                  (ulOpenMode & (OPEN_SHARE_DENYNONE | OPEN_SHARE_DENYREADWRITE |
                                 OPEN_SHARE_DENYREAD | OPEN_SHARE_DENYWRITE)) )
            params->CreateFlags |= SHFL_CF_ACCESS_DENYWRITE;
    }

    if (ulOpenMode & OPEN_SHARE_DENYNONE)
            params->CreateFlags &= ~SHFL_CF_ACCESS_MASK_DENY;

    switch (usOpenFlags & 0x13)
    {
        case OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW:
            params->CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            break;

        case OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:
            params->CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            break;

        case OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW:
            params->CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            break;

        case OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:
            params->CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            break;

        case OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW:
            params->CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            break;

        case OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:
            params->CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            break;

        default:
            log("Invalid open flags\n");
            hrc = ERROR_INVALID_PARAMETER;
            goto FS32_OPENCREATEEXIT;
    }

    params->Info.Attr.fMode = ((uint32_t)usAttr << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_OS2;

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

    if (! pwsz)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    vboxfsStrToUtf8(pwsz, (char *)pszFullName);

    if (! pwsz)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    path = make_shflstring((char *)pwsz);

    if (! path)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    rc = VbglR0SfCreate(&g_clientHandle, map, path, params);

    if (params->Handle == SHFL_HANDLE_NIL)
    {
        log("fail\n");
        log("rc=%d\n", rc);

        if (! (usOpenFlags & OPEN_ACTION_CREATE_IF_NEW) )
        {
            hrc = ERROR_OPEN_FAILED;
        }
        else
        {
            switch (params->Result)
            {
                case SHFL_FILE_EXISTS:
                    hrc = ERROR_FILE_EXISTS;
                    break;

                case SHFL_PATH_NOT_FOUND:
                    hrc = ERROR_PATH_NOT_FOUND;
                    break;

                default:
                    hrc = ERROR_FILE_NOT_FOUND;
            }
        }

        if (rc == VERR_ACCESS_DENIED)
        {
            hrc = ERROR_ACCESS_DENIED;
        }

        free_shflstring(path);
        goto FS32_OPENCREATEEXIT;
    }

    if (! RT_SUCCESS(rc))
    {
        log("VbglR0SfCreate returned %d\n", rc);
        free_shflstring(path);
        hrc = vbox_err_to_os2_err(rc);
        goto FS32_OPENCREATEEXIT;
    }

    *pusAction = 0;

    switch (params->Result)
    {
        case SHFL_FILE_EXISTS:
            *pusAction = FILE_EXISTED;
            break;

        case SHFL_FILE_CREATED:
            *pusAction = FILE_CREATED;
            break;

        case SHFL_FILE_REPLACED:
            *pusAction = FILE_TRUNCATED;
            break;

        default:
            ;
    }

    psffsd->filebuf = (PFILEBUF)RTMemAlloc(sizeof(FILEBUF));

    if (! psffsd->filebuf)
    {
        log("couldn't allocate file buf\n");
        free_shflstring(path);
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_OPENCREATEEXIT;
    }

    psffsd->filebuf->handle = params->Handle;
    psffsd->filebuf->path = path;
    psffsd->filebuf->map = *map;
    psffsd->filebuf->tmp = tmp;

    psffsi->sfi_positionl = 0;
    psffsi->sfi_position = 0;

    psffsi->sfi_sizel = params->Info.cbObject;
    psffsi->sfi_size = (LONG)params->Info.cbObject;

    /* Creation time   */
    RTTimeSpecAddSeconds(&params->Info.BirthTime, -60 * VBoxTimezoneGetOffsetMin());
    RTTimeExplode(&time, &params->Info.BirthTime);

    Time.hours = time.u8Hour;
    Time.minutes = time.u8Minute;
    Time.twosecs = time.u8Second / 2;
    Date.year = time.i32Year - 1980;
    Date.month = time.u8Month;
    Date.day = time.u8MonthDay;
    psffsi->sfi_ctime = Time;
    psffsi->sfi_cdate = Date;

    /* Last access time   */
    RTTimeSpecAddSeconds(&params->Info.AccessTime, -60 * VBoxTimezoneGetOffsetMin());
    RTTimeExplode(&time, &params->Info.AccessTime);

    Time.hours = time.u8Hour;
    Time.minutes = time.u8Minute;
    Time.twosecs = time.u8Second / 2;
    Date.year = time.i32Year - 1980;
    Date.month = time.u8Month;
    Date.day = time.u8MonthDay;
    psffsi->sfi_atime = Time;
    psffsi->sfi_adate = Date;

    /* Last write time   */
    RTTimeSpecAddSeconds(&params->Info.ModificationTime, -60 * VBoxTimezoneGetOffsetMin());
    RTTimeExplode(&time, &params->Info.ModificationTime);

    Time.hours = time.u8Hour;
    Time.minutes = time.u8Minute;
    Time.twosecs = time.u8Second / 2;
    Date.year = time.i32Year - 1980;
    Date.month = time.u8Month;
    Date.day = time.u8MonthDay;
    psffsi->sfi_mtime = Time;
    psffsi->sfi_mdate = Date;

    psffsi->sfi_tstamp = ST_SREAD | ST_PREAD | ST_SWRITE | ST_PWRITE;

    if (*pusAction == FILE_CREATED)
    {
        psffsi->sfi_tstamp |= ST_SCREAT | ST_PCREAT;
    }

    psffsi->sfi_DOSattr = VBoxToOS2Attr(params->Info.Attr.fMode);

FS32_OPENCREATEEXIT:
    if (pwsz)
        RTMemFree(pwsz);
    if (pszParsedPath)
        RTMemFree(pszParsedPath);
    if (pszFullName)
        RTMemFree(pszFullName);
    if (params)
        RTMemFree(params);
    if (map)
        RTMemFree(map);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_CLOSE(ULONG type, ULONG IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    APIRET hrc = NO_ERROR;
    int rc;

    log("FS32_CLOSE(%lx, %lx)\n", type, IOflag);

    if (type != FS_CL_FORSYS)
    {
        hrc = NO_ERROR;
        goto FS32_CLOSEEXIT;
    }

    rc = VbglR0SfClose(&g_clientHandle, &psffsd->filebuf->map, psffsd->filebuf->handle);

    hrc = vbox_err_to_os2_err(rc);

    free_shflstring(psffsd->filebuf->path);
    
    if (psffsd->filebuf->tmp)
    {
        VbglR0SfUnmapFolder(&g_clientHandle, &psffsd->filebuf->map);
    }

    RTMemFree(psffsd->filebuf);

FS32_CLOSEEXIT:
    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_COMMIT(ULONG type, ULONG IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    APIRET hrc = NO_ERROR;
    int rc;

    log("FS32_COMMIT(%lx, %lx)\n", type, IOflag);

    rc = VbglR0SfFlush(&g_clientHandle, &psffsd->filebuf->map, psffsd->filebuf->handle);

    hrc = vbox_err_to_os2_err(rc);

    log(" => %d\n", hrc);
    return hrc;
}


extern "C" APIRET APIENTRY
FS32_CHGFILEPTRL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG off, ULONG ulMethod, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    LONGLONG llNewOffset = 0;

    log("FS32_CHGFILEPTRL(%lld, %lx, %lx)\n", off, ulMethod, IOflag);

    switch (ulMethod)
    {
        case CFP_RELBEGIN: /* relative to the beginning */
            llNewOffset = off;
            break;

        case CFP_RELCUR: /* relative to the current position */
            llNewOffset = psffsi->sfi_positionl + off;
            break;

        case CFP_RELEND: /* relative to the end of file */
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
    log("FS32_CHGFILEPTR(%ld, %lx, %lx)\n", off, ulMethod, IOflag);
    return FS32_CHGFILEPTRL(psffsi, psffsd, off, ulMethod, IOflag);
}


DECLASM(int)
FS32_FILEINFO(ULONG flag, PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG level,
              PBYTE pData, ULONG cbData, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    USHORT usNeededSize;
    PSHFLDIRINFO file = NULL;
    uint32_t len = sizeof(SHFLDIRINFO);
    int rc;

    log("FS32_FILEINFO(%lx, %lx, %lx)\n", flag, level, IOflag);

    switch (flag)
    {
        case FI_RETRIEVE: // retrieve
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
                    case FIL_QUERYALLEAS:
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

                log("psffsd->filebuf->map=%x\n", psffsd->filebuf->map);
                log("psffsd->filebuf->handle=%x\n", psffsd->filebuf->handle);

                rc = VbglR0SfFsInfo(&g_clientHandle, &psffsd->filebuf->map, psffsd->filebuf->handle,
                                    SHFL_INFO_GET | SHFL_INFO_FILE, &len, file);

                hrc = vbox_err_to_os2_err(rc);

                if (RT_FAILURE(rc))
                {
                    log("VbglR0SfFsInfo failed: %d\n", rc);
                    goto FS32_FILEINFOEXIT;
                }

                switch (level)
                {
                    case FIL_STANDARD:
                        {
                            PFILESTATUS filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            filestatus = (PFILESTATUS)RTMemAlloc(sizeof(FILESTATUS));

                            if (! filestatus)
                            {
                                hrc = ERROR_NOT_ENOUGH_MEMORY;
                                goto FS32_FILEINFOEXIT;
                            }

                            /* Creation time   */
                            RTTimeSpecAddSeconds(&file->Info.BirthTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeSpecAddSeconds(&file->Info.AccessTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeSpecAddSeconds(&file->Info.ModificationTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus->attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus->cbFile = (ULONG)file->Info.cbObject;
                            filestatus->cbFileAlloc = (ULONG)file->Info.cbAllocated;
                            KernCopyOut(pData, filestatus, sizeof(FILESTATUS));
                            RTMemFree(filestatus);
                            hrc = NO_ERROR;
                            break;
                        }
 
                    case FIL_STANDARDL:
                        {
                            PFILESTATUS3L filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            filestatus = (PFILESTATUS3L)RTMemAlloc(sizeof(FILESTATUS3L));

                            if (! filestatus)
                            {
                                hrc = ERROR_NOT_ENOUGH_MEMORY;
                                goto FS32_FILEINFOEXIT;
                            }

                            /* Creation time   */
                            RTTimeSpecAddSeconds(&file->Info.BirthTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeSpecAddSeconds(&file->Info.AccessTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeSpecAddSeconds(&file->Info.ModificationTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus->attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus->cbFile = file->Info.cbObject;
                            filestatus->cbFileAlloc = file->Info.cbAllocated;
                            KernCopyOut(pData, filestatus, sizeof(FILESTATUS3L));
                            RTMemFree(filestatus);
                            hrc = NO_ERROR;
                            break;
                        }

                    case FIL_QUERYEASIZE:
                        {
                            PFILESTATUS2 filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            filestatus = (PFILESTATUS2)RTMemAlloc(sizeof(FILESTATUS2));

                            if (! filestatus)
                            {
                                hrc = ERROR_NOT_ENOUGH_MEMORY;
                                goto FS32_FILEINFOEXIT;
                            }

                            /* Creation time   */
                            RTTimeSpecAddSeconds(&file->Info.BirthTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeSpecAddSeconds(&file->Info.AccessTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeSpecAddSeconds(&file->Info.ModificationTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus->attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus->cbFile = (ULONG)file->Info.cbObject;
                            filestatus->cbFileAlloc = (ULONG)file->Info.cbAllocated;
                            filestatus->cbList = sizeof(filestatus->cbList);
                            KernCopyOut(pData, filestatus, sizeof(FILESTATUS2));
                            RTMemFree(filestatus);
                            hrc = NO_ERROR;
                            break;
                        }

                    case FIL_QUERYEASIZEL:
                        {
                            PFILESTATUS4L filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            filestatus = (PFILESTATUS4L)RTMemAlloc(sizeof(FILESTATUS4L));

                            if (! filestatus)
                            {
                                hrc = ERROR_NOT_ENOUGH_MEMORY;
                                goto FS32_FILEINFOEXIT;
                            }

                            /* Creation time   */
                            RTTimeSpecAddSeconds(&file->Info.BirthTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.BirthTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateCreation, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeCreation, &Time, sizeof(USHORT));
                            /* Last access time   */
                            RTTimeSpecAddSeconds(&file->Info.AccessTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.AccessTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastAccess, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastAccess, &Time, sizeof(USHORT));
                            /* Last write time   */
                            RTTimeSpecAddSeconds(&file->Info.ModificationTime, -60 * VBoxTimezoneGetOffsetMin());
                            RTTimeExplode(&time, &file->Info.ModificationTime);
                            Date.day = time.u8MonthDay;
                            Date.month = time.u8Month;
                            Date.year = time.i32Year - 1980;
                            Time.twosecs = time.u8Second / 2;
                            Time.minutes = time.u8Minute;
                            Time.hours = time.u8Hour;
                            memcpy(&filestatus->fdateLastWrite, &Date, sizeof(USHORT));
                            memcpy(&filestatus->ftimeLastWrite, &Time, sizeof(USHORT));
                            filestatus->attrFile = VBoxToOS2Attr(file->Info.Attr.fMode);
                            filestatus->cbFile = file->Info.cbObject;
                            filestatus->cbFileAlloc = file->Info.cbAllocated;
                            filestatus->cbList = sizeof(filestatus->cbList);
                            KernCopyOut(pData, filestatus, sizeof(FILESTATUS4L));
                            RTMemFree(filestatus);
                            hrc = NO_ERROR;
                            break;
                        }

                    case FIL_QUERYEASFROMLIST:
                    case FIL_QUERYEASFROMLISTL:
                        {
                            EAOP filestatus;

                            KernCopyIn(&filestatus, pData, sizeof(EAOP));
                            filestatus.fpFEAList = (PFEALIST)KernSelToFlat((ULONG)filestatus.fpFEAList);
                            filestatus.fpGEAList = (PGEALIST)KernSelToFlat((ULONG)filestatus.fpGEAList);
                            filestatus.fpFEAList->cbList = MIN_EA_SIZE;
                            hrc = GetEmptyEAS(&filestatus);
                            break;
                        }

                    case FIL_QUERYALLEAS:
                        {
                            EAOP filestatus;
                            FEALIST feal;
                            ULONG cbList;

                            KernCopyIn(&filestatus, pData, sizeof(EAOP));
                            filestatus.fpFEAList = (PFEALIST)KernSelToFlat((ULONG)filestatus.fpFEAList);
                            KernCopyIn(&feal, filestatus.fpFEAList, sizeof(feal));
                            cbList = feal.cbList;
                            feal.cbList = sizeof(feal.cbList);
                            KernCopyOut(filestatus.fpFEAList, &feal, cbList);
                            hrc = NO_ERROR;
                            break;
                        }

                    default:
                        hrc = ERROR_INVALID_LEVEL;
                        goto FS32_FILEINFOEXIT;
                }
            }
            break;

        case FI_SET: // set
            {
                RTTIMESPEC delta;

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
                            PFILESTATUS filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            if (cbData < sizeof(FILESTATUS))
                            {
                                hrc = ERROR_INSUFFICIENT_BUFFER;
                                goto FS32_FILEINFOEXIT;
                            }

                            filestatus = (PFILESTATUS)RTMemAlloc(sizeof(FILESTATUS));

                            if (! filestatus)
                            {
                                hrc = ERROR_NOT_ENOUGH_MEMORY;
                                goto FS32_FILEINFOEXIT;
                            }

                            KernCopyIn(filestatus, pData, sizeof(FILESTATUS));

                            /* Creation time   */
                            memset(&time, 0, sizeof(RTTIME));
                            Date = filestatus->fdateCreation;
                            Time = filestatus->ftimeCreation;
                            time.u8WeekDay = UINT8_MAX;
                            time.u16YearDay = 0;
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            time.u32Nanosecond = 0;
                            time.offUTC = 0;
                            time.fFlags |= RTTIME_FLAGS_TYPE_UTC;
                            RTTimeNormalize(&time);
                            RTTimeImplode(&file->Info.BirthTime, &time);
                            RTTimeSpecAddSeconds(&file->Info.BirthTime, 60 * VBoxTimezoneGetOffsetMin());
                            /* Last access time   */
                            memset(&time, 0, sizeof(RTTIME));
                            Date = filestatus->fdateLastAccess;
                            Time = filestatus->ftimeLastAccess;
                            time.u8WeekDay = UINT8_MAX;
                            time.u16YearDay = 0;
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            time.u32Nanosecond = 0;
                            time.offUTC = 0;
                            time.fFlags |= RTTIME_FLAGS_TYPE_UTC;
                            RTTimeNormalize(&time);
                            RTTimeImplode(&file->Info.AccessTime, &time);
                            RTTimeSpecAddSeconds(&file->Info.AccessTime, 60 * VBoxTimezoneGetOffsetMin());
                            /* Last write time   */
                            memset(&time, 0, sizeof(RTTIME));
                            Date = filestatus->fdateLastWrite;
                            Time = filestatus->ftimeLastWrite;
                            time.u8WeekDay = UINT8_MAX;
                            time.u16YearDay = 0;
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            time.u32Nanosecond = 0;
                            time.offUTC = 0;
                            time.fFlags = RTTIME_FLAGS_TYPE_UTC;
                            RTTimeNormalize(&time);
                            RTTimeImplode(&file->Info.ModificationTime, &time);
                            RTTimeSpecAddSeconds(&file->Info.ModificationTime, 60 * VBoxTimezoneGetOffsetMin());
                            
                            file->Info.cbObject = filestatus->cbFile;
                            file->Info.cbAllocated = filestatus->cbFileAlloc;
                            file->Info.Attr.fMode = OS2ToVBoxAttr(filestatus->attrFile);

                            usMask = ~(FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);

                            if (filestatus->attrFile & FILE_DIRECTORY)
                            {
                                usMask &= ~FILE_DIRECTORY;
                            }

                            if (filestatus->attrFile & usMask)
                            {
                                hrc = ERROR_ACCESS_DENIED;
                                RTMemFree(filestatus);
                                goto FS32_FILEINFOEXIT;
                            }

                            usMask = 0;
                            if (memcmp(&filestatus->fdateCreation, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus->ftimeCreation, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SCREAT;
                                psffsi->sfi_tstamp |= ST_PCREAT;
                                memcpy(&psffsi->sfi_ctime, &filestatus->ftimeCreation, sizeof(USHORT));
                                memcpy(&psffsi->sfi_cdate, &filestatus->fdateCreation, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus->fdateLastWrite, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus->ftimeLastWrite, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SWRITE;
                                psffsi->sfi_tstamp |= ST_PWRITE;
                                memcpy(&psffsi->sfi_mtime, &filestatus->ftimeLastWrite, sizeof(USHORT));
                                memcpy(&psffsi->sfi_mdate, &filestatus->fdateLastWrite, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus->fdateLastAccess, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus->ftimeLastAccess, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SREAD;
                                psffsi->sfi_tstamp |= ST_PREAD;
                                memcpy(&psffsi->sfi_atime, &filestatus->ftimeLastAccess, sizeof(USHORT));
                                memcpy(&psffsi->sfi_adate, &filestatus->fdateLastAccess, sizeof(USHORT));
                            }

                            if (psffsi->sfi_DOSattr != (BYTE)filestatus->attrFile)
                                psffsi->sfi_DOSattr = (BYTE)filestatus->attrFile;

                            hrc = NO_ERROR;
                            RTMemFree(filestatus);
                            break;
                        }

                    case FIL_STANDARDL:
                        {
                            USHORT usMask;
                            PFILESTATUS3L filestatus;
                            RTTIME time;
                            FDATE Date;
                            FTIME Time;

                            if (cbData < sizeof(FILESTATUS3L))
                            {
                                hrc = ERROR_INSUFFICIENT_BUFFER;
                                goto FS32_FILEINFOEXIT;
                            }

                            filestatus = (PFILESTATUS3L)RTMemAlloc(sizeof(FILESTATUS3L));

                            if (! filestatus)
                            {
                                hrc = ERROR_NOT_ENOUGH_MEMORY;
                                goto FS32_FILEINFOEXIT;
                            }

                            KernCopyIn(filestatus, pData, sizeof(FILESTATUS3L));

                            /* Creation time   */
                            memset(&time, 0, sizeof(RTTIME));
                            Date = filestatus->fdateCreation;
                            Time = filestatus->ftimeCreation;
                            time.u8WeekDay = UINT8_MAX;
                            time.u16YearDay = 0;
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            time.u32Nanosecond = 0;
                            time.offUTC = 0;
                            time.fFlags = RTTIME_FLAGS_TYPE_UTC;
                            RTTimeNormalize(&time);
                            RTTimeImplode(&file->Info.BirthTime, &time);
                            RTTimeSpecAddSeconds(&file->Info.BirthTime, 60 * VBoxTimezoneGetOffsetMin());
                            /* Last access time   */
                            memset(&time, 0, sizeof(RTTIME));
                            Date = filestatus->fdateLastAccess;
                            Time = filestatus->ftimeLastAccess;
                            time.u8WeekDay = UINT8_MAX;
                            time.u16YearDay = 0;
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            time.u32Nanosecond = 0;
                            time.offUTC = 0;
                            time.fFlags = RTTIME_FLAGS_TYPE_UTC;
                            RTTimeNormalize(&time);
                            RTTimeImplode(&file->Info.AccessTime, &time);
                            RTTimeSpecAddSeconds(&file->Info.AccessTime, 60 * VBoxTimezoneGetOffsetMin());
                            /* Last write time   */
                            memset(&time, 0, sizeof(RTTIME));
                            Date = filestatus->fdateLastWrite;
                            Time = filestatus->ftimeLastWrite;
                            time.u8WeekDay = UINT8_MAX;
                            time.u16YearDay = 0;
                            time.u8MonthDay = Date.day;
                            time.u8Month = Date.month;
                            time.i32Year = Date.year + 1980;
                            time.u8Second = Time.twosecs * 2;
                            time.u8Minute = Time.minutes;
                            time.u8Hour = Time.hours;
                            time.u32Nanosecond = 0;
                            time.offUTC = 0;
                            time.fFlags = RTTIME_FLAGS_TYPE_UTC;
                            RTTimeNormalize(&time);
                            RTTimeImplode(&file->Info.ModificationTime, &time);
                            RTTimeSpecAddSeconds(&file->Info.ModificationTime, 60 * VBoxTimezoneGetOffsetMin());
                            
                            file->Info.cbObject = filestatus->cbFile;
                            file->Info.cbAllocated = filestatus->cbFileAlloc;
                            file->Info.Attr.fMode = OS2ToVBoxAttr(filestatus->attrFile);

                            usMask = ~(FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);

                            if (filestatus->attrFile & FILE_DIRECTORY)
                            {
                                usMask &= ~FILE_DIRECTORY;
                            }

                            if (filestatus->attrFile & usMask)
                            {
                                hrc = ERROR_ACCESS_DENIED;
                                RTMemFree(filestatus);
                                goto FS32_FILEINFOEXIT;
                            }

                            usMask = 0;
                            if (memcmp(&filestatus->fdateCreation, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus->ftimeCreation, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SCREAT;
                                psffsi->sfi_tstamp |= ST_PCREAT;
                                memcpy(&psffsi->sfi_ctime, &filestatus->ftimeCreation, sizeof(USHORT));
                                memcpy(&psffsi->sfi_cdate, &filestatus->fdateCreation, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus->fdateLastWrite, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus->ftimeLastWrite, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SWRITE;
                                psffsi->sfi_tstamp |= ST_PWRITE;
                                memcpy(&psffsi->sfi_mtime, &filestatus->ftimeLastWrite, sizeof(USHORT));
                                memcpy(&psffsi->sfi_mdate, &filestatus->fdateLastWrite, sizeof(USHORT));
                            }

                            if (memcmp(&filestatus->fdateLastAccess, &usMask, sizeof(usMask)) ||
                                memcmp(&filestatus->ftimeLastAccess, &usMask, sizeof(usMask)))
                            {
                                psffsi->sfi_tstamp &= ~ST_SREAD;
                                psffsi->sfi_tstamp |= ST_PREAD;
                                memcpy(&psffsi->sfi_atime, &filestatus->ftimeLastAccess, sizeof(USHORT));
                                memcpy(&psffsi->sfi_adate, &filestatus->fdateLastAccess, sizeof(USHORT));
                            }

                            if (psffsi->sfi_DOSattr != (BYTE)filestatus->attrFile)
                                psffsi->sfi_DOSattr = (BYTE)filestatus->attrFile;

                            hrc = NO_ERROR;

                            RTMemFree(filestatus);
                            break;
                        }

                    case FIL_QUERYEASIZE:
                    case FIL_QUERYEASIZEL:
                        /* {
                            EAOP filestatus;
                            FEALIST feal;
                            ULONG cbList;

                            KernCopyIn(&filestatus, pData, sizeof(EAOP));
                            filestatus.fpFEAList = (PFEALIST)KernSelToFlat((ULONG)filestatus.fpFEAList);
                            filestatus.fpGEAList = (PGEALIST)KernSelToFlat((ULONG)filestatus.fpGEAList);
                            KernCopyIn(&feal, filestatus.fpFEAList, sizeof(feal));
                            cbList = feal.cbList;
                            hrc = GetEmptyEAS(&filestatus);
                            KernCopyOut(filestatus.fpFEAList, &feal, cbList);
                            break;
                        } */
                        hrc = 0;
                        break;

                    default:
                        hrc = ERROR_INVALID_LEVEL;
                }

                rc = VbglR0SfFsInfo(&g_clientHandle, &psffsd->filebuf->map, psffsd->filebuf->handle,
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

int chsize(PVBOXSFFSD psffsd, ULONG size)
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

    rc = VbglR0SfFsInfo(&g_clientHandle, &psffsd->filebuf->map, psffsd->filebuf->handle,
                        SHFL_INFO_SET | SHFL_INFO_SIZE, &cbBuf, (PSHFLDIRINFO)pObjInfo);

    if (pObjInfo)
        RTMemFree(pObjInfo);

    return rc;
}

DECLASM(int)
FS32_NEWSIZEL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG cbFile, ULONG IOflag)
{
    APIRET hrc = NO_ERROR;
    int rc;

    log("FS32_NEWSIZEL(%lld, %lx)\n", cbFile, IOflag);
    rc = chsize(psffsd, cbFile);

    hrc = vbox_err_to_os2_err(rc);

FS32_NEWSIZELEXIT:
    log(" => %d\n", hrc);

    return hrc;
}


extern "C" APIRET APIENTRY
FS32_READ(PSFFSI psffsi, PVBOXSFFSD psffsd, PVOID pvData, PULONG pcb, ULONG IOflag)
{
    APIRET hrc;
    uint8_t *pBuf = NULL, *p;
    ULONG cb, cbBuf = 16 * 1024 * 1024;
    int rc;

    log("FS32_READ(%lx)\n", IOflag);

    if (! pcb || ! pvData)
    {
        hrc = ERROR_INVALID_PARAMETER;
        goto FS32_READEXIT;
    }

    cb = *pcb;
    *pcb = 0;
    p = (uint8_t *)pvData;

    if (! cb)
    {
        hrc = NO_ERROR;
        goto FS32_READEXIT;
    }

    if (psffsi->sfi_positionl > psffsi->sfi_sizel)
    {
        cb = 0;
    }
    else if (cb > psffsi->sfi_sizel - psffsi->sfi_positionl)
    {
        cb = psffsi->sfi_sizel - psffsi->sfi_positionl;
    }

    if (! cb)
    {
        hrc = NO_ERROR;
        goto FS32_READEXIT;
    }

    if (cbBuf > cb)
    {
        cbBuf = cb;
    }

    pBuf = (uint8_t *)RTMemAllocZ(cbBuf);

    if (! pBuf)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_READEXIT;
    }

    while (cb)
    {
        rc = VbglR0SfRead(&g_clientHandle, &psffsd->filebuf->map, psffsd->filebuf->handle, 
                          psffsi->sfi_positionl, (uint32_t *)&cbBuf, pBuf, true); // false

        if (RT_SUCCESS(rc))
        {
            KernCopyOut((char *)p, pBuf, cbBuf);
            psffsi->sfi_positionl += cbBuf;
            psffsi->sfi_position  += (LONG)psffsi->sfi_positionl;
            *pcb += cbBuf;
            p += cbBuf;

            if (cb > cbBuf)
                cb -= cbBuf;
            else
            {
                cbBuf = cb;
                cb = 0;
            }
        }
        else
            break;
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
    uint32_t cbNewPos;
    uint8_t *pBuf = NULL, *p;
    ULONG cb, cbBuf = 16 * 1024 * 1024;
    int rc;

    log("FS32_WRITE(%lx)\n", IOflag);

    if (! pcb || ! pvData)
    {
        hrc = ERROR_INVALID_PARAMETER;
        goto FS32_WRITEEXIT;
    }

    cb = *pcb;
    *pcb = 0;
    p = (uint8_t *)pvData;

    if (! cb)
    {
        hrc = NO_ERROR;
        goto FS32_WRITEEXIT;
    }

    if (cbBuf > cb)
    {
        cbBuf = cb;
    }

    pBuf = (uint8_t *)RTMemAllocZ(cb);

    if (! pBuf)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_WRITEEXIT;
    }

    while (cb)
    {
        KernCopyIn(pBuf, (char *)p, cbBuf);

        rc = VbglR0SfWrite(&g_clientHandle, &psffsd->filebuf->map, psffsd->filebuf->handle, 
                           psffsi->sfi_positionl, (uint32_t *)&cbBuf, pBuf, true); // false

        if (RT_SUCCESS(rc))
        {
            cbNewPos = psffsi->sfi_positionl + cbBuf;

            if (cbNewPos > psffsi->sfi_sizel)
            {
                psffsi->sfi_sizel = cbNewPos;
                psffsi->sfi_size  = (LONG)cbNewPos;
            }

            psffsi->sfi_positionl = cbNewPos;
            psffsi->sfi_position  = (LONG)cbNewPos;

            *pcb += cbBuf;
            p += cbBuf;

            if (cb > cbBuf)
                cb -= cbBuf;
            else
            {
                cbBuf = cb;
                cb = 0;
            }
        }
        else
            break;
    }

    psffsi->sfi_tstamp |= (ST_SWRITE | ST_PWRITE);

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
    log("FS32_READFILEATCACHE(%lx, %lld)\n", IOflag, off);
    return ERROR_NOT_SUPPORTED;
}


extern "C" APIRET APIENTRY
FS32_RETURNFILECACHE(KernCacheList_t *pCacheList)
{
    log("FS32_RETURNFILECACHE\n");
    return ERROR_NOT_SUPPORTED;
}


/* oddments */

DECLASM(int)
FS32_CANCELLOCKREQUESTL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl *pLockRange)
{
    log("FS32_CANCELLOCKREQUESTL\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_CANCELLOCKREQUEST(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock *pLockRange)
{
    log("FS32_CANCELLOCKREQUEST\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILELOCKSL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl *pUnLockRange,
                struct filelockl *pLockRange, ULONG timeout, ULONG flags)
{
    log("FS32_FILELOCKSL(%lu, %lx)\n", timeout, flags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILELOCKS(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock *pUnLockRange,
               struct filelock *pLockRange, ULONG timeout, ULONG flags)
{
    log("FS32_FILELOCKS(%lu, %lx)\n", timeout, flags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_IOCTL(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cat, ULONG func,
           PVOID pParm, ULONG lenParm, PUSHORT plenParmIO,
           PVOID pData, ULONG lenData, PUSHORT plenDataIO)
{
    log("FS32_IOCTL(%lx, %lx)\n", cat, func);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pCmdList, ULONG cbCmdList,
            PUSHORT poError, ULONG IOflag)
{
    log("FS32_FILEIO(%lx)\n", IOflag);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_NMPIPE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG OpType, union npoper *pOpRec,
            PBYTE pData, PCSZ pszName)
{
    log("FS32_NPIPE(%lx, %s)\n", OpType, pszName);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_VERIFYUNCNAME(ULONG flag, PCSZ pszName)
{
    char *pszServer = NULL, *p;
    APIRET hrc = NO_ERROR;

    log("FS32_VERIFYUNCNAME(%lx, %s)\n", flag, pszName);

    pszServer = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszServer)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_VERIFYUNCNAMEEXIT;
    }

    if (pszName[0] != '\\' ||
        pszName[1] != '\\')
    {
        hrc = ERROR_INVALID_NAME;
        goto FS32_VERIFYUNCNAMEEXIT;
    }

    pszName += 2;

    p = strchr((char *)pszName, '\\');

    if (p)
    {
        strncpy(pszServer, (char *)pszName, p - (char *)pszName);
        pszServer[p - (char *)pszName] = '\0';
    }

    if (! stricmp(pszServer, "vboxsvr") ||
        ! stricmp(pszServer, "vboxsrv") ||
        ! stricmp(pszServer, "vboxfs") ||
        ! stricmp(pszServer, "vboxsf") )
        hrc = NO_ERROR;
    else
        hrc = ERROR_INVALID_NAME;

FS32_VERIFYUNCNAMEEXIT:
    if (pszServer)
        RTMemFree(pszServer);

    log(" => %d\n", hrc);
    return hrc;
}

DECLASM(int)
FS32_OPENPAGEFILE(PULONG pFlag, PULONG pcMaxReq, PCSZ pszName, PSFFSI psffsi, PVBOXSFFSD psffsd,
                  ULONG ulOpenMode, ULONG ulOpenFlag, ULONG ulAttr, ULONG Reserved)
{
    log("FS32_OPENPAGEFILE(%s, %lx, %lx, %lx)\n", pszName, ulOpenMode, ulOpenFlag, ulAttr);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_SETSWAP(PSFFSI psffsi, PVBOXSFFSD psffsd)
{
    log("FS32_SETSWAP\n");
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_ALLOCATEPAGESPACE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cb, ULONG cbWantContig)
{
    log("FS32_ALLOCATEPAGESPACE(%lu, %lu)\n", cb, cbWantContig);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_DOPAGEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, struct PageCmdHeader *pList)
{
    log("FS32_DOPAGEIO\n");
    return ERROR_NOT_SUPPORTED;
}
