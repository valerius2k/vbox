/** $Id$ */
/** @file
 * VBoxSF - OS/2 Shared Folders, the FS and FSD level IFS EPs
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
#include <iprt/assert.h>
#include <iprt/mem.h>

VBSFCLIENT g_clientHandle = {0};


DECLASM(void)
FS32_EXIT(ULONG uid, ULONG pid, ULONG pdb)
{
    dprintf("VBOXSF: FS32_EXIT(%lx, %lx, %lx)\n", uid, pid, pdb);
}


DECLASM(int)
FS32_SHUTDOWN(ULONG type, ULONG reserved)
{
    dprintf("VBOXSF: FS32_SHUTDOWN(%lx)\n", type);
    return NO_ERROR;
}


DECLASM(int)
FS32_ATTACH(ULONG flag, PCSZ pszDev, PVBOXSFVP pvpfsd, PVBOXSFCD pcdfsd, PBYTE pszParm, PUSHORT pcbParm)
{
    APIRET rc = NO_ERROR;
    int    len;

    dprintf("VBOXSF: FS32_ATTACH(%lx, %s, %s)\n", flag, pszDev, pszParm);
    PSHFLSTRING sharename;
    PVBOXSFVP pvboxsfvp = (PVBOXSFVP)pvpfsd;

    //__asm__ __volatile__ (".byte 0xcc\n\t");

    if (! pszDev)
    {
        rc = ERROR_INVALID_PARAMETER;
        goto FS32_ATTACHEXIT;
    }

    switch (flag)
    {
        case 0: // Attach
            dprintf("*pcbParm=%u, pszParm=%s\n", *pcbParm, pszParm);
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
            dprintf("g_clientHandle=%lx\n", g_clientHandle);
            sharename = make_shflstring((char *)pszParm);
            rc = vboxCallMapFolder(&g_clientHandle, sharename, &pvboxsfvp->map);
            strncpy(pvboxsfvp->szLabel, (char *)pszParm, 12);
            pvboxsfvp->szLabel[11] = '\0';
            dprintf("pvboxsfvp->map=%lx\n", pvboxsfvp->map);
            free_shflstring(sharename);
            if (RT_FAILURE(rc))
            {
                dprintf("vboxCallMapFolder rc=%d", rc);
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
            dprintf("pvboxsfvp->szLabel=%s\n", pvboxsfvp->szLabel);
            dprintf("pvboxsfvp->map=%lx\n", pvboxsfvp->map);
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
    dprintf(" => %d\n", rc);
    return rc;
}


DECLASM(int)
FS32_FLUSHBUF(USHORT hVPB, ULONG flag)
{
    dprintf("VBOXSF: FS32_FLUSHBUF(%lx, %lx)\n", hVPB, flag);
    return NO_ERROR;
}


DECLASM(int)
FS32_FSINFO(ULONG flag, ULONG hVPB, PBYTE pbData, ULONG cbData, ULONG level)
{
    APIRET  rc = NO_ERROR;
    int32_t rv = 0;
    VBSFMAP map;
    PVPFSI pvpfsi = NULL;
    PVPFSD pvpfsd = NULL;
    PVBOXSFVP pvboxsfvp;
    SHFLVOLINFO volume_info;
    uint32_t bytes = sizeof(SHFLVOLINFO);

    dprintf("VBOXSF: FS32_FSINFO(%x, %lx, %lx)\n", hVPB, flag, level);

    if (hVPB == 0)
        return ERROR_INVALID_PARAMETER;

    FSH32_GETVOLPARM(hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;
    map = pvboxsfvp->map;
    dprintf("map=%lx\n", map);

    rv = vboxCallFSInfo(&g_clientHandle, &map, 0, 
        (SHFL_INFO_GET | SHFL_INFO_VOLUME), &bytes, (PSHFLDIRINFO)&volume_info);

    if (RT_FAILURE(rv))
    {
        dprintf("VBOXSF: vboxCallFSInfo failed (%d)\n", rv);
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
                fsallocate.cSectorUnit  = volume_info.ulBytesPerAllocationUnit / SECTORSIZE;
                fsallocate.cUnit        = volume_info.ullTotalAllocationBytes / volume_info.ulBytesPerAllocationUnit;
                fsallocate.cUnitAvail   = volume_info.ullAvailableAllocationBytes / volume_info.ulBytesPerAllocationUnit;
                fsallocate.cbSector     = SECTORSIZE;

                rc = KernCopyOut(pbData, &fsallocate, sizeof(FSALLOCATE));
            }
            else
            {
                rc = ERROR_NOT_SUPPORTED;
            }
            break;

        case 2: // query/set volume label
            dprintf("pbData=%lx, cbData=%lu\n", pbData, cbData);
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
                dprintf("rc=%lu\n", rc);
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

    dprintf(" => %d\n", rc);
    return rc;
}


DECLASM(int)
FS32_FSCTL(union argdat *pArgdat, ULONG iArgType, ULONG func,
           PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
           PVOID pData, USHORT lenData, PUSHORT plenDataIO)
{
    PEASIZEBUF pEA = (PEASIZEBUF)pData;
    APIRET rc;
    dprintf("VBOXSF: FS32_FSCTL(%lx, %lx)\n", iArgType, func);

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
    dprintf(" => %d\n", rc);
    return NO_ERROR;
}


DECLASM(int)
FS32_PROCESSNAME(PSZ pszName)
{
    dprintf("VBOXSF: FS32_PROCESSNAME(%s)\n", pszName);
    return NO_ERROR;
}


DECLASM(int)
FS32_CHDIR(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszDir, USHORT iCurDirEnd)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params;
    PVPFSI pvpfsi = NULL;
    PVPFSD pvpfsd = NULL;
    PVBOXSFVP pvboxsfvp;
    PSHFLSTRING path;
    char *pwsz;
    char *pszFullDir;
    int rc;

    dprintf("VBOXSF: FS32_CHDIR(%lx, %u)\n", flag, iCurDirEnd);

    switch (flag)
    {
        case 0: /* allocate new working directory */
            dprintf("chdir to: %s\n", pszDir);

            FSH32_GETVOLPARM(pcdfsi->cdi_hVPB, &pvpfsi, &pvpfsd);

            pvboxsfvp = (PVBOXSFVP)pvpfsd;

            pszFullDir = (char *)RTMemAlloc(CCHMAXPATHCOMP);

            if ( (pszDir[0] == '\\' || pszDir[1] == ':') )
            {
                // absolute path
                strcpy(pszFullDir, (char *)pszDir);
                dprintf("1\n");
            }
            else
            {
                // relative path
                strcpy(pszFullDir, pcdfsi->cdi_curdir);

                if (pszFullDir[strlen(pszFullDir) - 1] != '\\')
                    strcat(pszFullDir, "\\");

                strcat(pszFullDir, (char *)pszDir);
                dprintf("2\n");
            }

            pszFullDir += 3;
            //if (iCurDirEnd != 0xffff)
            //{
            //    pszFullDir += iCurDirEnd - 3;
            //}

            dprintf("pszFullDir=%s\n", pszFullDir);
            pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP);
            vboxsfStrToUtf8(pwsz, (char *)pszFullDir);

            path = make_shflstring((char *)pwsz);
            rc = vboxCallCreate(&g_clientHandle, &pvboxsfvp->map, path, &params);
            free_shflstring(path);
            RTMemFree(pwsz);
            RTMemFree(pszFullDir);

            if (RT_SUCCESS(rc))
            {
                pcdfsd->cwd = (PCWD)RTMemAllocZ(sizeof(CWD));

                if (! pcdfsd->cwd)
                    return ERROR_NOT_ENOUGH_MEMORY;

                pcdfsd->cwd->handle = params.Handle;
                pcdfsd->cwd->map = pvboxsfvp->map;
                dprintf("success\n");
            }
            else
                hrc = vbox_err_to_os2_err(rc);
            break;

        case CD_VERIFY: /* verify working directory - only for removable media? */
            hrc = ERROR_NOT_SUPPORTED;
            break;

        case CD_FREE: /* deallocate working directory */
            vboxCallClose(&g_clientHandle, &pcdfsd->cwd->map, pcdfsd->cwd->handle);
            RTMemFree(pcdfsd->cwd);
            hrc = NO_ERROR;
            break;

        default:
            hrc = ERROR_INVALID_FUNCTION;
    }

FS32_CHDIREXIT:
    dprintf(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_MKDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
           PBYTE pEABuf, ULONG flag)
{
    dprintf("VBOXSF: FS32_MKDIR(%s, flag)\n", pszName, flag);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_RMDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd)
{
    dprintf("VBOXSF: FS32_RMDIR(%s)\n", pszName);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_COPY(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd,
          PCSZ pszDst, USHORT iDstCurDirEnd, USHORT nameType)
{
    dprintf("VBOXSF: FS32_COPY(%lx, %s, %s, %lx)\n", flag, pszSrc, pszDst, nameType);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_MOVE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd,
          PCSZ pszDst, USHORT iDstCurDirEnd, USHORT type)
{
    dprintf("VBOXSF: FS32_MOVE(%s, %s, %lx)\n", pszSrc, pszDst, type);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_DELETE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszFile, USHORT iCurDirEnd)
{
    dprintf("VBOXSF: FS32_DELETE(%s)\n", pszFile);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_FILEATTRIBUTE(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, PUSHORT pAttr)
{
    dprintf("VBOXSF: FS32_FILEATTRIBUTE(%lx, %s)\n", flag, pszName);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(int)
FS32_PATHINFO(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnt,
              USHORT level, PBYTE pData, USHORT cbData)
{
    dprintf("VBOXSF: FS32_PATHINFO(%x, %s, %x)\n", flag, pszName, level);
    return ERROR_NOT_SUPPORTED;
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
