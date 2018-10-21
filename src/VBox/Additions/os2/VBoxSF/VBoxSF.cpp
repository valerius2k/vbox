/** $Id$ */
/** @file
 * VBoxSF - OS/2 Shared Folders, the FS and FSD level IFS EPs
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
#include <iprt/assert.h>
#include <iprt/mem.h>

#include <stdarg.h>

APIRET APIENTRY parseFileName(const char *pszPath, PCDFSI pcdfsi,
                              char *pszParsedPath, int *pcbParsedPath, VBGLSFMAP *map);

VBGLSFCLIENT g_clientHandle = {0};

extern "C" unsigned long g_fLog_enable;

extern uint32_t             KernKEEVersion;

void _System                KernPrintf(const char *fmt, ...);

void log(const char *fmt, ...)
{
    char buf[1024];
    va_list va;

    if (! g_fLog_enable)
    {
        return;
    }
    
    va_start(va, fmt);

    RTStrPrintfV(buf, sizeof(buf) - 1, fmt, va);

    /*
     * Use KernPrintf when available as it has a large buffer (7 MB) and
     * fallback to a function provided by the current drver, if any.
     */
    if ((uint32_t)&KernKEEVersion > 0x00010002)
        KernPrintf("%s", buf);
    else
        RTLogComPrintf("%s", buf);
   
    RTLogBackdoorPrintf("%s", buf);

    va_end(va);
}

DECLASM(void)
FS32_EXIT(ULONG uid, ULONG pid, ULONG pdb)
{
    log("VBOXSF: FS32_EXIT(%lx, %lx, %lx)\n", uid, pid, pdb);
}


DECLASM(int)
FS32_SHUTDOWN(ULONG type, ULONG reserved)
{
    log("VBOXSF: FS32_SHUTDOWN(%lx)\n", type);
    return NO_ERROR;
}


DECLASM(int)
FS32_ATTACH(ULONG flag, PCSZ pszDev, PVBOXSFVP pvpfsd, PVBOXSFCD pcdfsd, PBYTE pszParm, PUSHORT pcbParm)
{
    APIRET rc = NO_ERROR;
    int    len;

    log("VBOXSF: FS32_ATTACH(%lx, %s, %s)\n", flag, pszDev, pszParm);
    PSHFLSTRING sharename;
    PVBOXSFVP pvboxsfvp = (PVBOXSFVP)pvpfsd;

    if (! pszDev)
    {
        rc = ERROR_INVALID_PARAMETER;
        goto FS32_ATTACHEXIT;
    }

    switch (flag)
    {
        case 0: // Attach
            if (pszDev[1] != ':')
            {
                /* drives only */
                rc = ERROR_NOT_SUPPORTED;
                goto FS32_ATTACHEXIT;
            }
            if (! pcbParm || ! *pcbParm || ! pszParm)
            {
                rc = ERROR_BUFFER_OVERFLOW;
                goto FS32_ATTACHEXIT;
            }
            sharename = make_shflstring((char *)pszParm);
            rc = VbglR0SfMapFolder(&g_clientHandle, sharename, &pvboxsfvp->map);
            strncpy(pvboxsfvp->szLabel, (char *)pszParm, 12);
            pvboxsfvp->szLabel[11] = '\0';
            free_shflstring(sharename);
            if (RT_FAILURE(rc))
            {
                log("VbglR0SfMapFolder rc=%d", rc);
                rc = ERROR_VOLUME_NOT_MOUNTED;
                goto FS32_ATTACHEXIT;
            }
            rc = NO_ERROR;
            break;

        case 1: // Detach
            if (pszDev[1] != ':')
            {
                /* drives only */
                rc = ERROR_NOT_SUPPORTED;
                goto FS32_ATTACHEXIT;
            }
            break;

        case 2: // Query
            len = MIN(strlen(pvboxsfvp->szLabel) + 1, 12);
	    if (*pcbParm >= sizeof(pvboxsfvp->szLabel) && pszParm)
	    {
	        /* set cbFSAData to 0 => we return 0 bytes in rgFSAData area */
	        *((USHORT *) pszParm) = len;
	        memcpy((char *)&pszParm[2], pvboxsfvp->szLabel, len);
                pszParm[len + 1] = '\0';
	        rc = NO_ERROR;
	    }
	    else
	    {
	        /* not enough room to tell that we wanted to return 0 bytes */
	        rc = ERROR_BUFFER_OVERFLOW;
	    }
	    *pcbParm = len;
            break;
    }

FS32_ATTACHEXIT:
    log(" => %d\n", rc);
    return rc;
}


DECLASM(int)
FS32_FLUSHBUF(USHORT hVPB, ULONG flag)
{
    log("VBOXSF: FS32_FLUSHBUF(%lx, %lx)\n", hVPB, flag);
    return NO_ERROR;
}


DECLASM(int)
FS32_FSINFO(ULONG flag, ULONG hVPB, PBYTE pbData, ULONG cbData, ULONG level)
{
    APIRET  rc = NO_ERROR;
    int32_t rv = 0;
    VBGLSFMAP map;
    PVPFSI pvpfsi = NULL;
    PVPFSD pvpfsd = NULL;
    PVBOXSFVP pvboxsfvp;
    SHFLVOLINFO volume_info;
    uint32_t bytes = sizeof(SHFLVOLINFO);

    log("VBOXSF: FS32_FSINFO(%x, %lx, %lx)\n", hVPB, flag, level);

    if (hVPB == 0)
        return ERROR_INVALID_PARAMETER;

    FSH32_GETVOLPARM(hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;
    map = pvboxsfvp->map;
    log("map=%lx\n", map);

    rv = VbglR0SfFsInfo(&g_clientHandle, &map, 0, 
        (SHFL_INFO_GET | SHFL_INFO_VOLUME), &bytes, (PSHFLDIRINFO)&volume_info);

    if (RT_FAILURE(rv))
    {
        log("VBOXSF: VbglR0SfFsInfo failed (%d)\n", rv);
        return vbox_err_to_os2_err(rv);
    }

    switch (level)
    {
        case 1: // query/set sector and cluster information
            if (flag == 0)
            {
                // query sector and cluster information
                if (cbData < sizeof(FSALLOCATE))
                {
                    rc = ERROR_BUFFER_OVERFLOW;
                    break;
                }

                FSALLOCATE fsallocate;

                fsallocate.idFileSystem = 0;
                fsallocate.cSectorUnit  = volume_info.ulBytesPerAllocationUnit / volume_info.ulBytesPerSector;
                fsallocate.cUnit        = volume_info.ullTotalAllocationBytes / volume_info.ulBytesPerAllocationUnit;
                fsallocate.cUnitAvail   = volume_info.ullAvailableAllocationBytes / volume_info.ulBytesPerAllocationUnit;
                fsallocate.cbSector     = volume_info.ulBytesPerSector;

                rc = KernCopyOut(pbData, &fsallocate, sizeof(FSALLOCATE));
            }
            else
            {
                rc = ERROR_NOT_SUPPORTED;
            }
            break;

        case 2: // query/set volume label
            log("pbData=%lx, cbData=%lu\n", pbData, cbData);
            if (flag == 0)
            {
                // query volume label
                if (cbData < sizeof(FSINFO))
                {
                    rc = ERROR_BUFFER_OVERFLOW;
                    break;
                }
                FSINFO FsInfo;

                memset (&FsInfo, 0, sizeof(FSINFO));
                FsInfo.vol.cch = strlen(pvboxsfvp->szLabel);
                strcpy((char *)&FsInfo.vol.szVolLabel, (char *)pvboxsfvp->szLabel);

                rc = KernCopyOut(pbData, &FsInfo, sizeof(FSINFO));
                log("rc=%lu\n", rc);
            }
            else if (flag == 1)
            {
                // set volume label
                VOLUMELABEL Label;

                rc = KernCopyIn(&Label, pbData, MIN(cbData, sizeof(VOLUMELABEL)));
                if (rc)
                {
                    break;
                }
                memcpy (pvboxsfvp->szLabel, &Label.szVolLabel, sizeof(pvboxsfvp->szLabel));
                pvboxsfvp->szLabel [sizeof(pvboxsfvp->szLabel)-1] = '\0';

                rc = NO_ERROR;
            }
            break;
    }

    log(" => %d\n", rc);
    return rc;
}


DECLASM(int)
FS32_FSCTL(union argdat *pArgdat, ULONG iArgType, ULONG func,
           PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
           PVOID pData, USHORT lenData, PUSHORT plenDataIO)
{
    PEASIZEBUF pEA = (PEASIZEBUF)pData;
    APIRET rc;
    log("VBOXSF: FS32_FSCTL(%lx, %lx)\n", iArgType, func);

    switch (func)
    {
        case 0:
            if (lenData > 15)
            {
                strcpy((char *)pData, "Unknown error");
                if (plenDataIO)
                    *plenDataIO = strlen((char *)pData) + 1;
                rc = NO_ERROR;
            }
            else
            {
                if (plenDataIO)
                    *plenDataIO = 15;
                rc = ERROR_BUFFER_OVERFLOW;
            }
            break;

        case 1:
            if (plenDataIO)
                *plenDataIO = sizeof(EASIZEBUF);
            if (lenData < sizeof(EASIZEBUF))
            {
                rc = ERROR_BUFFER_OVERFLOW;
                goto FS32_FSCTLEXIT;
            }
            pEA->cbMaxEASize = 0;
            pEA->cbMaxEAListSize = 0;
            rc = NO_ERROR;
            break;

        default:
            rc = ERROR_NOT_SUPPORTED;
    }

FS32_FSCTLEXIT:
    log(" => %d\n", rc);
    return NO_ERROR;
}


DECLASM(int)
FS32_PROCESSNAME(PSZ pszName)
{
    log("VBOXSF: FS32_PROCESSNAME(%s)\n", pszName);
    return NO_ERROR;
}


DECLASM(int)
FS32_CHDIR(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszDir, USHORT iCurDirEnd)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params = {0};
    PSHFLSTRING path = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    char *pwsz = NULL;
    int rc;

    log("VBOXSF: FS32_CHDIR(%lx, %u)\n", flag, iCurDirEnd);

    switch (flag)
    {
        case 0: /* allocate new working directory */
            log("chdir to: %s\n", pszDir);

            pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

            if (! pszFullName)
            {
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto FS32_CHDIREXIT;
            }

            cbFullName = CCHMAXPATHCOMP + 1;

            hrc = parseFileName((char *)pszDir, pcdfsi, pszFullName, &cbFullName, &map);

            if (hrc)
            {
                log("Filename parse error!\n");
                goto FS32_CHDIREXIT;
            }

            log("pszFullName=%s\n", pszFullName);
            pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
            vboxsfStrToUtf8(pwsz, (char *)pszFullName);

            path = make_shflstring((char *)pwsz);

            params.Handle = SHFL_HANDLE_NIL;
            params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;
            
            rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);

            if (params.Handle == SHFL_HANDLE_NIL)
            {
                log("fail\n");
                hrc = ERROR_PATH_NOT_FOUND;
                goto FS32_CHDIREXIT;
            }

            if (RT_SUCCESS(rc))
            {
                pcdfsd->cwd = (PCWD)RTMemAllocZ(sizeof(CWD));

                if (! pcdfsd->cwd)
                    return ERROR_NOT_ENOUGH_MEMORY;

                pcdfsd->cwd->handle = params.Handle;
                pcdfsd->cwd->map = map;
                log("success\n");
            }
            else
                hrc = vbox_err_to_os2_err(rc);
            break;

        case CD_VERIFY: /* verify working directory - only for removable media? */
            hrc = ERROR_NOT_SUPPORTED;
            break;

        case CD_FREE: /* deallocate working directory */
            VbglR0SfClose(&g_clientHandle, &pcdfsd->cwd->map, pcdfsd->cwd->handle);
            RTMemFree(pcdfsd->cwd);
            hrc = NO_ERROR;
            break;

        default:
            hrc = ERROR_INVALID_FUNCTION;
    }

FS32_CHDIREXIT:
    if (path)
        RTMemFree(path);
    if (pwsz)
        RTMemFree(pwsz);
    if (pszFullName)
        RTMemFree(pszFullName);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_MKDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
           PBYTE pEABuf, ULONG flag)
{
    SHFLCREATEPARMS params = {0};
    APIRET hrc = NO_ERROR;
    char *pszFullName = NULL;
    int cbFullName;
    PVBOXSFVP pvboxsfvp;
    PSHFLSTRING path = NULL;
    char *pwsz = NULL;
    VBGLSFMAP map;
    int rc;

    log("VBOXSF: FS32_MKDIR(%s, %lu)\n", pszName, flag);

    params.Handle = 0;
    params.Info.cbObject = 0;
    params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_CREATE_IF_NEW |
        SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACCESS_READWRITE; //SHFL_CF_ACCESS_READ;

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_MKDIREXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_MKDIREXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxsfStrToUtf8(pwsz, (char *)pszFullName);
    log("path=%s\n", pwsz);

    path = make_shflstring((char *)pwsz);
    rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);

    /** @todo r=ramshankar: we should perhaps also check rc here and change
     *        Handle initialization from 0 to SHFL_HANDLE_NIL. */
    if (params.Handle == SHFL_HANDLE_NIL)
    {
        log("fail\n");
        hrc = ERROR_PATH_NOT_FOUND;
        goto FS32_MKDIREXIT;
    }

    VbglR0SfClose(&g_clientHandle, &map, params.Handle);
    hrc = NO_ERROR;

FS32_MKDIREXIT:
    if (pszFullName)
        RTMemFree(pszFullName);
    if (path)
        RTMemFree(path);
    if (pwsz)
        RTMemFree(pwsz);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_RMDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd)
{
    APIRET hrc = NO_ERROR;
    PSHFLSTRING path = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    char *pwsz = NULL;
    int rc;

    log("VBOXSF: FS32_RMDIR(%s)\n", pszName);

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_RMDIREXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_RMDIREXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxsfStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);
    rc = VbglR0SfRemove(&g_clientHandle, &map, path, SHFL_REMOVE_DIR);

    hrc = vbox_err_to_os2_err(rc);

FS32_RMDIREXIT:
    if (pszFullName)
        RTMemFree(pszFullName);
    if (path)
        RTMemFree(path);
    if (pwsz)
        RTMemFree(pwsz);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_COPY(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd,
          PCSZ pszDst, USHORT iDstCurDirEnd, USHORT nameType)
{
    log("VBOXSF: FS32_COPY(%lx, %s, %s, %lx)\n", flag, pszSrc, pszDst, nameType);
    return ERROR_CANNOT_COPY;
}


DECLASM(int)
FS32_MOVE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd,
          PCSZ pszDst, USHORT iDstCurDirEnd, USHORT flag)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params = {0};
    PSHFLSTRING oldpath = NULL, newpath = NULL;
    char *pszFullSrc = NULL;
    int cbFullSrc;
    char *pszFullDst = NULL;
    int cbFullDst;
    VBGLSFMAP map;
    char *pwszSrc = NULL, *pwszDst = NULL;
    uint32_t flags;
    int rc;

    log("VBOXSF: FS32_MOVE(%s, %s, %lx)\n", pszSrc, pszDst, flag);

    pszFullSrc = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullSrc)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_MOVEEXIT;
    }

    cbFullSrc = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszSrc, pcdfsi, pszFullSrc, &cbFullSrc, &map);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_MOVEEXIT;
    }

    pszFullDst = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullDst)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_MOVEEXIT;
    }

    cbFullDst = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszDst, pcdfsi, pszFullDst, &cbFullDst, &map);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_MOVEEXIT;
    }

    pwszSrc = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    pwszDst = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

    vboxsfStrToUtf8(pwszSrc, (char *)pszFullSrc);
    vboxsfStrToUtf8(pwszDst, (char *)pszFullDst);

    oldpath = make_shflstring((char *)pwszSrc);
    newpath = make_shflstring((char *)pwszDst);

    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_ACT_FAIL_IF_NEW;

    rc = VbglR0SfCreate(&g_clientHandle, &map, oldpath, &params);
    VbglR0SfClose(&g_clientHandle, &map, params.Handle);

    if (! RT_SUCCESS(rc))
    {
        log("VbglR0SfCreate returned %d\n", rc);
        hrc = ERROR_PATH_NOT_FOUND;
        goto FS32_MOVEEXIT;
    }

    if (params.Handle == SHFL_HANDLE_NIL)
    {
        log("fail\n");
        hrc = ERROR_PATH_NOT_FOUND;
        goto FS32_MOVEEXIT;
    }

    flags = SHFL_RENAME_REPLACE_IF_EXISTS;

    if (params.Info.Attr.fMode & RTFS_TYPE_DIRECTORY)
        flags |= SHFL_RENAME_DIR;
    else
        flags |= SHFL_RENAME_FILE;

    rc = VbglR0SfRename(&g_clientHandle, &map, oldpath, newpath, flags);
    log("rc=%d\n", rc);

    hrc = vbox_err_to_os2_err(rc);

FS32_MOVEEXIT:
    if (oldpath)
        RTMemFree(oldpath);
    if (newpath)
        RTMemFree(newpath);
    if (pwszSrc)
        RTMemFree(pwszSrc);
    if (pwszDst)
        RTMemFree(pwszDst);
    if (pszFullSrc)
        RTMemFree(pszFullSrc);
    if (pszFullDst)
        RTMemFree(pszFullDst);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_DELETE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszFile, USHORT iCurDirEnd)
{
    APIRET hrc = NO_ERROR;
    PSHFLSTRING path = NULL;
    char *pwsz = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    int rc;

    log("VBOXSF: FS32_DELETE(%s)\n", pszFile);

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_DELETEEXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszFile, pcdfsi, pszFullName, &cbFullName, &map);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_DELETEEXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxsfStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);
    rc = VbglR0SfRemove(&g_clientHandle, &map, path, SHFL_REMOVE_FILE);

    hrc = vbox_err_to_os2_err(rc);

FS32_DELETEEXIT:
    if (pszFullName)
        RTMemFree(pszFullName);
    if (path)
        RTMemFree(path);
    if (pwsz)
        RTMemFree(pwsz);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_FILEATTRIBUTE(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, PUSHORT pAttr)
{
    SHFLCREATEPARMS params = {0};
    APIRET hrc = NO_ERROR;
    PSHFLDIRINFO file = NULL;
    uint32_t len = sizeof(SHFLDIRINFO);
    PSHFLSTRING path = NULL;
    char *pwsz = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    int rc;

    log("VBOXSF: FS32_FILEATTRIBUTE(%lx, %s)\n", flag, pszName);

    switch (flag)
    {
        case 0: // retrieve
            file = (PSHFLDIRINFO)RTMemAlloc(len);

            if (! file)
            {
                log("Not enough memory 1\n");
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto FS32_FILEATTRIBUTEEXIT;
            }

            pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

            if (! pszFullName)
            {
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto FS32_FILEATTRIBUTEEXIT;
            }

            cbFullName = CCHMAXPATHCOMP + 1;

            hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map);

            if (hrc)
            {
                log("Filename parse error!\n");
                goto FS32_FILEATTRIBUTEEXIT;
            }

            pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
            vboxsfStrToUtf8(pwsz, (char *)pszFullName);

            path = make_shflstring((char *)pwsz);

            params.Handle = SHFL_HANDLE_NIL;
            params.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;

            rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);

            if (params.Handle == SHFL_HANDLE_NIL)
            {
                log("fail\n");
                hrc = ERROR_PATH_NOT_FOUND;
                goto FS32_FILEATTRIBUTEEXIT;
            }

            if (! RT_SUCCESS(rc))
            {
                log("VbglR0SfCreate returned %d\n", rc);
                hrc = vbox_err_to_os2_err(rc);
                goto FS32_FILEATTRIBUTEEXIT;
            }

            log("path=%s\n", pszName);
            log("map=%x\n", map);
            log("params.Handle=%x\n", params.Handle);
            log("len=%x\n", len);

            rc = VbglR0SfFsInfo(&g_clientHandle, &map, params.Handle,
                                SHFL_INFO_GET | SHFL_INFO_FILE, &len, file);

            hrc = vbox_err_to_os2_err(rc);

            if (RT_FAILURE(rc))
            {
                log("VbglR0SfFsInfo failed: %d\n", rc);
                goto FS32_FILEATTRIBUTEEXIT;
            }

            VbglR0SfClose(&g_clientHandle, &map, params.Handle);

            *pAttr = VBoxToOS2Attr(file->Info.Attr.fMode);
            break;

        case 1: // set
            hrc = ERROR_NOT_SUPPORTED;
    }

FS32_FILEATTRIBUTEEXIT:
    if (file)
        RTMemFree(file);
    if (path)
        RTMemFree(path);
    if (pwsz)
        RTMemFree(pwsz);
    if (pszFullName)
        RTMemFree(pszFullName);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_PATHINFO(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
              USHORT level, PBYTE pData, USHORT cbData)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params = {0};
    USHORT usNeededSize;
    PSHFLDIRINFO file = NULL;
    uint32_t len = sizeof(SHFLDIRINFO);
    PSHFLSTRING path = NULL;
    char *pwsz = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    int rc;

    log("VBOXSF: FS32_PATHINFO(%x, %s, %x)\n", flag, pszName, level);

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_PATHINFOEXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_PATHINFOEXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxsfStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);

    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_ACT_FAIL_IF_NEW; // SHFL_CF_LOOKUP

    rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);

    if (! RT_SUCCESS(rc))
    {
        log("VbglR0SfCreate returned %d\n", rc);
        hrc = ERROR_PATH_NOT_FOUND;
        goto FS32_PATHINFOEXIT;
    }

    log("path=%s\n", pszName);
    log("map=%x\n", map);
    log("params.Handle=%x\n", params.Handle);
    log("len=%x\n", len);

    if (params.Handle == SHFL_HANDLE_NIL)
    {
        log("fail\n");
        hrc = ERROR_PATH_NOT_FOUND;
        goto FS32_PATHINFOEXIT;
    }

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
                        goto FS32_PATHINFOEXIT;
                }

                if (cbData < usNeededSize)
                {
                    hrc = ERROR_BUFFER_OVERFLOW;
                    goto FS32_PATHINFOEXIT;
                }

                file = (PSHFLDIRINFO)RTMemAlloc(len);

                if (! file)
                {
                    log("Not enough memory 1\n");
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto FS32_PATHINFOEXIT;
                }

                log("file=%x\n", file);

                rc = VbglR0SfFsInfo(&g_clientHandle, &map, params.Handle,
                                    SHFL_INFO_GET | SHFL_INFO_FILE, &len, file);

                hrc = vbox_err_to_os2_err(rc);

                if (RT_FAILURE(rc))
                {
                    log("VbglR0SfFsInfo failed: %d\n", rc);
                    goto FS32_PATHINFOEXIT;
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
                        goto FS32_PATHINFOEXIT;
                }

                hrc = NO_ERROR;
            }
            break;

        case 1: // set
            {
                file = (PSHFLDIRINFO)RTMemAlloc(len);

                if (! file)
                {
                    hrc = ERROR_NOT_ENOUGH_MEMORY;
                    goto FS32_PATHINFOEXIT;
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
                                goto FS32_PATHINFOEXIT;
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
                                goto FS32_PATHINFOEXIT;
                            }

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
                                goto FS32_PATHINFOEXIT;
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
                                goto FS32_PATHINFOEXIT;
                            }

                            break;
                        }

                    case FIL_QUERYEASIZE:
                    case FIL_QUERYEASIZEL:
                        break;

                    default:
                        hrc = ERROR_INVALID_LEVEL;
                }

                rc = VbglR0SfFsInfo(&g_clientHandle, &map, params.Handle,
                                    SHFL_INFO_SET | SHFL_INFO_FILE, &len, file);

                if (RT_FAILURE(rc))
                {
                    log("VbglR0SfFsInfo failed: %d\n", rc);
                    hrc = vbox_err_to_os2_err(rc);
                    goto FS32_PATHINFOEXIT;
                }
            }
            break;

        default:
            hrc = ERROR_INVALID_FUNCTION;
    }

FS32_PATHINFOEXIT:
    if (pwsz)
        RTMemFree(pwsz);
    if (path)
        RTMemFree(path);
    if (file)
        RTMemFree(file);
    if (pszFullName)
        RTMemFree(pszFullName);

    if (params.Handle)
        VbglR0SfClose(&g_clientHandle, &map, params.Handle);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_MOUNT(USHORT flag, PVPFSI pvpfsi, PVBOXSFVP pvpfsd, USHORT hVPB, PCSZ pszBoot)
{
    return ERROR_NOT_SUPPORTED;
}

/* @todo move this into the runtime */
USHORT vbox_err_to_os2_err(int rc)
{
    switch (rc)
    {
        case VINF_SUCCESS:              return NO_ERROR;
        case VERR_INVALID_POINTER:      return ERROR_INVALID_ADDRESS;
        case VERR_INVALID_PARAMETER:    return ERROR_INVALID_PARAMETER;
        case VERR_PERMISSION_DENIED:    return ERROR_ACCESS_DENIED;
        case VERR_NOT_IMPLEMENTED:      return ERROR_NOT_SUPPORTED;
        case VERR_FILE_NOT_FOUND:       return ERROR_FILE_NOT_FOUND;

        case SHFL_PATH_NOT_FOUND:
        case SHFL_FILE_NOT_FOUND:       return ERROR_FILE_NOT_FOUND;
        case SHFL_FILE_EXISTS:          return ERROR_FILE_EXISTS;

        default:
            return ERROR_GEN_FAILURE;
    }
}
