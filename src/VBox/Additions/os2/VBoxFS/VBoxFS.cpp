/** $Id$ */
/** @file
 * VBoxFS - OS/2 Shared Folders, the FS and FSD level IFS EPs
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 * Copyright (c) 2015-2018 Valery V. Sedletski <_valerius-no-spam@mail.ru>
 * Copyright (c) 2016 (?) Lars Erdmann
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
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/mem.h>

#include <stdarg.h>
#include <stdio.h>

VBGLSFCLIENT g_clientHandle = {0};

extern PGINFOSEG g_pGIS;

extern "C" unsigned long g_fLog_enable;

extern "C" UCHAR g_fLogPrint;

extern "C" void APIENTRY LogPrint(const char *fmt);

void log(const char *fmt, ...)
{
    char buf[256];
    PROCINFO Proc;
    ULONG ulmSecs = 0;
    USHORT usThreadID;
    va_list va;

    if (! g_fLog_enable)
    {
        return;
    }
    
    if (g_pGIS)
    {
        ulmSecs = g_pGIS->msecs;
    }

    FSH32_QSYSINFO(2, (char *)&Proc, sizeof(Proc));
    FSH32_QSYSINFO(3, (char *)&usThreadID, 2);

    memset(buf, 0, sizeof(buf));

    RTStrPrintf(buf, sizeof(buf), "VBOXFS: P:%X T:%X D:%X t=%u.%u ",
        Proc.usPid,
        usThreadID,
        Proc.usPdb,
        (USHORT)((ulmSecs / 1000) % 60),
        (USHORT)(ulmSecs % 1000));

    va_start(va, fmt);

    RTStrPrintfV(buf + strlen(buf), sizeof(buf) - strlen(buf) - 1, fmt, va);

    /*
     * Use LogPrintf from QSINIT / os4ldr / arcaldr
     * when available as it has a large log buffer
     * (some MB's), or do fallback to a function 
     * provided by the current driver, if any.
     */
    if (g_fLogPrint)
        /* output to qsinit / os4ldr / arcaldr log buffer */
        LogPrint(buf);
    else
        /* otherwise, print to a COM port directly */
        RTLogComPrintf(buf);

    /* Duplicate the same string to VBox log, 
     * via a backdoor I/O port
     */
    RTLogBackdoorPrintf(buf);

    va_end(va);
}


APIRET GetProcInfo(PPROCINFO pProcInfo, USHORT usSize)
{
APIRET rc;

   memset(pProcInfo, 0xFF, usSize);
   rc = FSH32_QSYSINFO(2, (char *)pProcInfo, 6);
   if (rc)
   {
      log("GetProcInfo failed, rc = %d", rc);
   }
   return rc;
}


bool IsDosSession(void)
{
    PROCINFO pr;

    GetProcInfo(&pr, sizeof pr);

    if (pr.usPdb)
    {
        return true;
    }

    return false;
}


APIRET GetEmptyEAS(PEAOP peaop)
{
   APIRET rc;

   PFEALIST pTarFeal = NULL;
   USHORT   usMaxSize;
   PFEA     pCurrFea;

   PGEALIST pGeaList;
   PGEA     pCurrGea;

   ULONG    ulGeaSize;
   ULONG    ulFeaSize;
   ULONG    ulCurrFeaLen;
   ULONG    ulCurrGeaLen;

   PFEALIST pFeal = NULL;
   ULONG    cbFeal = 0;

   PGEALIST pGeal = NULL;
   ULONG    cbGeal = 0;

   KernCopyIn(&cbFeal, &peaop->fpFEAList->cbList, sizeof(peaop->fpFEAList->cbList));

   pFeal = (PFEALIST)RTMemAlloc(cbFeal);

   if (! pFeal)
   {
       rc = ERROR_NOT_ENOUGH_MEMORY;
       goto GetEmptyEAS_exit;
   }

   KernCopyIn(pFeal, peaop->fpFEAList, cbFeal);

   KernCopyIn(&cbGeal, &peaop->fpGEAList->cbList, sizeof(peaop->fpGEAList->cbList));

   pGeal = (PGEALIST)RTMemAlloc(cbGeal);

   if (! pGeal)
   {
       rc = ERROR_NOT_ENOUGH_MEMORY;
       goto GetEmptyEAS_exit;
   }

   KernCopyIn(pGeal, peaop->fpGEAList, cbGeal);

   pTarFeal = pFeal;

   if (pTarFeal->cbList > MAX_EA_SIZE)
      usMaxSize = (USHORT)MAX_EA_SIZE;
   else
      usMaxSize = (USHORT)pTarFeal->cbList;

   if (usMaxSize < sizeof (ULONG))
      return ERROR_BUFFER_OVERFLOW;

   pGeaList = pGeal;

   if (pGeaList->cbList > MAX_EA_SIZE)
      return ERROR_EA_LIST_TOO_LONG;

   ulFeaSize = sizeof(pTarFeal->cbList);
   ulGeaSize = sizeof(pGeaList->cbList);

   pCurrGea = pGeaList->list;
   pCurrFea = pTarFeal->list;
   while(ulGeaSize < pGeaList->cbList)
      {
      ulFeaSize += sizeof(FEA) + pCurrGea->cbName + 1;
      ulCurrGeaLen = sizeof(GEA) + pCurrGea->cbName;
      pCurrGea = (PGEA)((PBYTE)pCurrGea + ulCurrGeaLen);
      ulGeaSize += ulCurrGeaLen;
      }

   if (ulFeaSize > usMaxSize)
      {
      /* this is what HPFS.IFS returns */
      /* when a file does not have any EAs */
      pTarFeal->cbList = 0xEF;
      rc = ERROR_EAS_DIDNT_FIT;
      }
   else
      {
       /* since we DO copy something to */
       /* FEALIST, we have to set the complete */
       /* size of the resulting FEALIST in the */
       /* length field */
       pTarFeal->cbList = ulFeaSize;
       ulGeaSize = sizeof(pGeaList->cbList);
       pCurrGea = pGeaList->list;
       pCurrFea = pTarFeal->list;
       /* copy the EA names requested to the FEA area */
       /* even if any values cannot be returned       */
       while (ulGeaSize < pGeaList->cbList)
          {
          pCurrFea->fEA     = 0;
          strcpy((char *)(pCurrFea+1), pCurrGea->szName);
          pCurrFea->cbName  = (BYTE)strlen(pCurrGea->szName);
          pCurrFea->cbValue = 0;

          ulCurrFeaLen = sizeof(FEA) + pCurrFea->cbName + 1;
          pCurrFea = (PFEA)((PBYTE)pCurrFea + ulCurrFeaLen);

          ulCurrGeaLen = sizeof(GEA) + pCurrGea->cbName;
          pCurrGea = (PGEA)((PBYTE)pCurrGea + ulCurrGeaLen);
          ulGeaSize += ulCurrGeaLen;
          }
       rc = 0;
       }

    memcpy(peaop->fpFEAList, pTarFeal, pTarFeal->cbList);

GetEmptyEAS_exit:
    if (pFeal)
        RTMemFree(pFeal);
    if (pGeal)
        RTMemFree(pGeal);

    return rc;
}


DECLASM(void)
FS32_EXIT(ULONG uid, ULONG pid, ULONG pdb)
{
    log("FS32_EXIT(%lx, %lx, %lx)\n", uid, pid, pdb);
}


DECLASM(int)
FS32_SHUTDOWN(ULONG type, ULONG reserved)
{
    log("FS32_SHUTDOWN(%lx)\n", type);
    return NO_ERROR;
}


DECLASM(int)
FS32_ATTACH(ULONG flag, PCSZ pszDev, PVBOXSFVP pvpfsd, PVBOXSFCD pcdfsd, PBYTE pszParm, PUSHORT pcbParm)
{
    APIRET hrc = NO_ERROR;
    int    len;
    int    rc;

    if (flag == FSA_ATTACH)
        log("FS32_ATTACH(%lx, %s, %s)\n", flag, pszDev, pszParm);
    else
        log("FS32_ATTACH(%lx, %s)\n", flag, pszDev);

    PSHFLSTRING sharename;
    PVBOXSFVP pvboxsfvp = (PVBOXSFVP)pvpfsd;

    if (! pszDev)
    {
        hrc = ERROR_INVALID_PARAMETER;
        goto FS32_ATTACHEXIT;
    }

    switch (flag)
    {
        case FSA_ATTACH: // Attach
            if (pszDev[1] != ':')
            {
                /* drives only */
                hrc = ERROR_NOT_SUPPORTED;
                goto FS32_ATTACHEXIT;
            }
            if (! pcbParm || ! *pcbParm || ! pszParm)
            {
                hrc = ERROR_BUFFER_OVERFLOW;
                goto FS32_ATTACHEXIT;
            }
            pvboxsfvp->pszShareName = (char *)RTMemAlloc(CCHMAXPATHCOMP);

            if (! pvboxsfvp->pszShareName)
            {
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto FS32_ATTACHEXIT;
            }

            strcpy(pvboxsfvp->pszShareName, "\\\\vboxfs\\");
            strcat(pvboxsfvp->pszShareName, (char *)pszParm);

            sharename = make_shflstring((char *)pszParm);
            rc = VbglR0SfMapFolder(&g_clientHandle, sharename, &pvboxsfvp->map);
            strncpy(pvboxsfvp->szLabel, (char *)pszParm, 12);
            pvboxsfvp->szLabel[11] = '\0';
            free_shflstring(sharename);

            if (RT_FAILURE(rc))
            {
                log("VbglR0SfMapFolder rc=%ld\n", rc);
                RTMemFree(pvboxsfvp->pszShareName);
                hrc = ERROR_VOLUME_NOT_MOUNTED;
                goto FS32_ATTACHEXIT;
            }

            hrc = NO_ERROR;
            break;

        case FSA_DETACH: // Detach
            if (pszDev[1] != ':')
            {
                /* drives only */
                hrc = ERROR_NOT_SUPPORTED;
                goto FS32_ATTACHEXIT;
            }
            VbglR0SfUnmapFolder(&g_clientHandle, &pvboxsfvp->map);
            RTMemFree(pvboxsfvp->pszShareName);
            break;

        case FSA_ATTACH_INFO: // Query
            len = strlen(pvboxsfvp->pszShareName) + 1;
            if (*pcbParm >= len && pszParm)
            {
                /* set cbFSAData to 0 => we return 0 bytes in rgFSAData area */
                *((USHORT *) pszParm) = len;
                memcpy((char *)&pszParm[2], pvboxsfvp->pszShareName, len);
                pszParm[len + 1] = '\0';
                hrc = NO_ERROR;
            }
            else
            {
                /* not enough room to tell that we wanted to return 0 bytes */
                hrc = ERROR_BUFFER_OVERFLOW;
            }
            *pcbParm = len;
            break;

        default:
            hrc = ERROR_INVALID_FUNCTION;
    }

FS32_ATTACHEXIT:
    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_FLUSHBUF(ULONG hVPB, ULONG flag)
{
    log("FS32_FLUSHBUF(%lx, %lx)\n", hVPB, flag);
    return NO_ERROR;
}


DECLASM(int)
FS32_FSINFO(ULONG flag, ULONG hVPB, PBYTE pbData, ULONG cbData, ULONG level)
{
    APIRET  rc = NO_ERROR;
    int32_t rv = 0;
    PVPFSI pvpfsi = NULL;
    PVPFSD pvpfsd = NULL;
    PVBOXSFVP pvboxsfvp;
    SHFLVOLINFO volume_info;
    uint32_t bytes = sizeof(SHFLVOLINFO);

    log("FS32_FSINFO(%lx, %lx, %lx)\n", hVPB, flag, level);

    if (hVPB == 0)
        return ERROR_INVALID_PARAMETER;

    FSH32_GETVOLPARM(hVPB, &pvpfsi, &pvpfsd);

    pvboxsfvp = (PVBOXSFVP)pvpfsd;
    log("pvboxsfvp->map=%lx\n", pvboxsfvp->map);

    rv = VbglR0SfFsInfo(&g_clientHandle, &pvboxsfvp->map, 0, 
        (SHFL_INFO_GET | SHFL_INFO_VOLUME), &bytes, (PSHFLDIRINFO)&volume_info);

    if (RT_FAILURE(rv))
    {
        log("VbglR0SfFsInfo failed (%d)\n", rv);
        return vbox_err_to_os2_err(rv);
    }

    switch (level)
    {
        case FSIL_ALLOC: // query/set sector and cluster information
            if (flag == INFO_RETRIEVE)
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

        case FSIL_VOLSER: // query/set volume label
            log("pbData=%lx, cbData=%lu\n", pbData, cbData);
            if (flag == INFO_RETRIEVE)
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
                strcpy((char *)&FsInfo.vol.szVolLabel, pvboxsfvp->szLabel);

                rc = KernCopyOut(pbData, &FsInfo, sizeof(FSINFO));
                log("rc=%lu\n", rc);
            }
            else if (flag == INFO_SET)
            {
                // set volume label
                VOLUMELABEL Label;

                rc = KernCopyIn(&Label, pbData, MIN(cbData, sizeof(VOLUMELABEL)));
                if (rc)
                {
                    break;
                }
                memcpy (pvboxsfvp->szLabel, &Label.szVolLabel, sizeof(pvboxsfvp->szLabel));
                pvboxsfvp->szLabel [sizeof(pvboxsfvp->szLabel) - 1] = '\0';

                rc = NO_ERROR;
            }
            break;
    }

    log(" => %d\n", rc);
    return rc;
}


DECLASM(int)
FS32_FSCTL(union argdat *pArgdat, ULONG iArgType, ULONG func,
           PVOID pParm, ULONG lenParm, PUSHORT plenParmIO,
           PVOID pData, ULONG lenData, PUSHORT plenDataIO)
{
    PEASIZEBUF pEA = (PEASIZEBUF)pData;
    APIRET rc;

    log("FS32_FSCTL(%lx, %lx)\n", iArgType, func);

    switch (func)
    {
        case FSCTL_FUNC_NEW_INFO:
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

        case FSCTL_FUNC_EASIZE:
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
    char *pszFilter = RTPathFilename((char const *)pszName);

    log("FS32_PROCESSNAME(%s)\n", pszName);

    /* got the idea from bird's version of
       vboxsf.ifs (but I made it a bit shorter) */
    if ( pszFilter && ( strchr(pszFilter, '?') || strchr(pszFilter, '*') ) )
    {
        /* convert DOS-style wildcard characters to WinNT style */
        for (; *pszFilter; pszFilter++)
        {
            switch (*pszFilter)
            {
                case '*':
                    /* DOS star, matches any number of chars (including none), except DOS dot */
                    if (pszFilter[1] == '.')
                        *pszFilter = '<';
                    break;

                case '?':
                    /* DOS query sign, matches one char, except a dot, and end of name eats it */
                    *pszFilter = '>';
                    break;

                case '.':
                    /* DOS dot, matches a dot or end of name */
                    if (pszFilter[1] == '?' || pszFilter[1] == '*')
                        *pszFilter = '"';
            }
        }
    }


    return NO_ERROR;
}


DECLASM(int)
FS32_CHDIR(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszDir, ULONG iCurDirEnd)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params = {0};
    PSHFLSTRING path = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    char *pwsz = NULL;
    bool tmp;
    int rc;

    log("FS32_CHDIR(%lx, %lu)\n", flag, iCurDirEnd);

    switch (flag)
    {
        case CD_EXPLICIT: /* allocate new working directory */
            log("chdir to: %s\n", pszDir);

            pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

            if (! pszFullName)
            {
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto FS32_CHDIREXIT;
            }

            cbFullName = CCHMAXPATHCOMP + 1;

            hrc = parseFileName((char *)pszDir, pcdfsi, pszFullName, &cbFullName, &map, &tmp);

            if (hrc)
            {
                log("Filename parse error!\n");
                goto FS32_CHDIREXIT;
            }

            log("pszFullName=%s\n", pszFullName);
            pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
            vboxfsStrToUtf8(pwsz, (char *)pszFullName);

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
    if (tmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &map);
    if (path)
        RTMemFree(path);
    if (pwsz)
        RTMemFree(pwsz);
    if (pszFullName)
        RTMemFree(pszFullName);
    if (tmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &map);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_MKDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, ULONG iCurDirEnd,
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
    bool tmp;
    int rc;

    log("FS32_MKDIR(%s, %lu)\n", pszName, flag);

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

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map, &tmp);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_MKDIREXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxfsStrToUtf8(pwsz, (char *)pszFullName);
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
    if (tmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &map);
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
FS32_RMDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, ULONG iCurDirEnd)
{
    APIRET hrc = NO_ERROR;
    PSHFLSTRING path = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    bool tmp;
    char *pwsz = NULL;
    int rc;

    log("FS32_RMDIR(%s)\n", pszName);

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_RMDIREXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map, &tmp);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_RMDIREXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxfsStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);
    rc = VbglR0SfRemove(&g_clientHandle, &map, path, SHFL_REMOVE_DIR);

    hrc = vbox_err_to_os2_err(rc);

FS32_RMDIREXIT:
    if (tmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &map);
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
FS32_COPY(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, ULONG iSrcCurDirEnd,
          PCSZ pszDst, ULONG iDstCurDirEnd, ULONG nameType)
{
    log("FS32_COPY(%lx, %s, %s, %lx)\n", flag, pszSrc, pszDst, nameType);
    return ERROR_CANNOT_COPY;
}


DECLASM(int)
FS32_MOVE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, ULONG iSrcCurDirEnd,
          PCSZ pszDst, ULONG iDstCurDirEnd, ULONG flag)
{
    APIRET hrc = NO_ERROR;
    SHFLCREATEPARMS params = {0};
    PSHFLSTRING oldpath = NULL, newpath = NULL;
    char *pszFullSrc = NULL;
    int cbFullSrc;
    char *pszFullDst = NULL;
    int cbFullDst;
    VBGLSFMAP srcmap, dstmap;
    bool srctmp, dsttmp;
    char *pwszSrc = NULL, *pwszDst = NULL;
    uint32_t flags;
    int rc;

    log("FS32_MOVE(%s, %s, %lx)\n", pszSrc, pszDst, flag);

    pszFullSrc = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullSrc)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_MOVEEXIT;
    }

    cbFullSrc = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszSrc, pcdfsi, pszFullSrc, &cbFullSrc, &srcmap, &srctmp);

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

    hrc = parseFileName((char *)pszDst, pcdfsi, pszFullDst, &cbFullDst, &dstmap, &dsttmp);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_MOVEEXIT;
    }

    pwszSrc = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    pwszDst = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

    vboxfsStrToUtf8(pwszSrc, (char *)pszFullSrc);
    vboxfsStrToUtf8(pwszDst, (char *)pszFullDst);

    oldpath = make_shflstring((char *)pwszSrc);
    newpath = make_shflstring((char *)pwszDst);

    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_ACT_FAIL_IF_NEW;

    rc = VbglR0SfCreate(&g_clientHandle, &srcmap, oldpath, &params);
    VbglR0SfClose(&g_clientHandle, &srcmap, params.Handle);

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

    rc = VbglR0SfRename(&g_clientHandle, &srcmap, oldpath, newpath, flags);
    log("rc=%d\n", rc);

    hrc = vbox_err_to_os2_err(rc);

FS32_MOVEEXIT:
    if (srctmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &srcmap);
    if (dsttmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &dstmap);
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
FS32_DELETE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszFile, ULONG iCurDirEnd)
{
    APIRET hrc = NO_ERROR;
    PSHFLSTRING path = NULL;
    char *pwsz = NULL;
    char *pszFullName = NULL;
    int cbFullName;
    VBGLSFMAP map;
    bool tmp;
    int rc;

    log("FS32_DELETE(%s)\n", pszFile);

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_DELETEEXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszFile, pcdfsi, pszFullName, &cbFullName, &map, &tmp);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_DELETEEXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxfsStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);
    rc = VbglR0SfRemove(&g_clientHandle, &map, path, SHFL_REMOVE_FILE);

    hrc = vbox_err_to_os2_err(rc);

FS32_DELETEEXIT:
    if (tmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &map);
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
FS32_FILEATTRIBUTE(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd,
                   PCSZ pszName, ULONG iCurDirEnd, PUSHORT pAttr)
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
    bool tmp;
    int rc;

    log("FS32_FILEATTRIBUTE(%lx, %s)\n", flag, pszName);

    switch (flag)
    {
        case FA_RETRIEVE: // retrieve
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

            hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map, &tmp);

            if (hrc)
            {
                log("Filename parse error!\n");
                goto FS32_FILEATTRIBUTEEXIT;
            }

            pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
            vboxfsStrToUtf8(pwsz, (char *)pszFullName);

            path = make_shflstring((char *)pwsz);

            params.Handle = SHFL_HANDLE_NIL;
            params.CreateFlags = SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;
            
            rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);

            if (RT_FAILURE(rc))
            {
                log("VbglR0SfCreate returned %d\n", rc);
                hrc = vbox_err_to_os2_err(rc);
                goto FS32_FILEATTRIBUTEEXIT;
            }

            switch (params.Result)
            {
                case SHFL_PATH_NOT_FOUND:
                    hrc = ERROR_PATH_NOT_FOUND;
                    goto FS32_FILEATTRIBUTEEXIT;

                case SHFL_FILE_EXISTS:
                    break;

                default:
                    hrc = ERROR_FILE_NOT_FOUND;
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

        case FA_SET: // set
            hrc = ERROR_NOT_SUPPORTED;
    }

FS32_FILEATTRIBUTEEXIT:
    if (params.Handle != SHFL_HANDLE_NIL)
        VbglR0SfClose(&g_clientHandle, &map, params.Handle);
    if (tmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &map);
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
FS32_PATHINFO(ULONG flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, ULONG iCurDirEnd,
              ULONG level, PBYTE pData, ULONG cbData)
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
    bool tmp;
    int rc;

    log("FS32_PATHINFO(%lx, %s, %lx)\n", flag, pszName, level);

    pszFullName = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

    if (! pszFullName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto FS32_PATHINFOEXIT;
    }

    cbFullName = CCHMAXPATHCOMP + 1;

    hrc = parseFileName((char *)pszName, pcdfsi, pszFullName, &cbFullName, &map, &tmp);

    if (hrc)
    {
        log("Filename parse error!\n");
        goto FS32_PATHINFOEXIT;
    }

    pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);
    vboxfsStrToUtf8(pwsz, (char *)pszFullName);

    path = make_shflstring((char *)pwsz);

    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = SHFL_CF_ACT_FAIL_IF_NEW;

    rc = VbglR0SfCreate(&g_clientHandle, &map, path, &params);

    if (params.Handle == SHFL_HANDLE_NIL)
    {
        log("fail\n");
        hrc = ERROR_PATH_NOT_FOUND;
        goto FS32_PATHINFOEXIT;
    }

    if (! RT_SUCCESS(rc))
    {
        log("VbglR0SfCreate returned %d\n", rc);
        hrc = vbox_err_to_os2_err(rc);
        goto FS32_PATHINFOEXIT;
    }

    log("path=%s\n", pszName);
    log("map=%x\n", map);
    log("params.Handle=%x\n", params.Handle);
    log("len=%x\n", len);

    switch (flag & 1)
    {
        case PI_RETRIEVE: // retrieve
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

                    case FIL_NAMEISVALID:
                        rc = 0;
                        goto FS32_PATHINFOEXIT;

                    case 7:
                        usNeededSize = strlen(pszFullName) + 1;
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
                            filestatus.fpFEAList = (PFEALIST)KernSelToFlat((ULONG)filestatus.fpFEAList);
                            filestatus.fpGEAList = (PGEALIST)KernSelToFlat((ULONG)filestatus.fpGEAList);
                            hrc = GetEmptyEAS(&filestatus);
                            KernCopyOut(pData, &filestatus, sizeof(EAOP));
                            break;
                        }

                    case FIL_QUERYALLEAS:
                        {
                            EAOP filestatus;
                            PFEALIST pFeal;
                            ULONG cbList;

                            KernCopyIn(&filestatus, pData, sizeof(EAOP));
                            filestatus.fpFEAList = (PFEALIST)KernSelToFlat((ULONG)filestatus.fpFEAList);
                            KernCopyIn(&cbList, &filestatus.fpFEAList->cbList, sizeof(filestatus.fpFEAList->cbList));
                            pFeal = (PFEALIST)RTMemAlloc(cbList);
                            if (! pFeal)
                            {
                                hrc = ERROR_NOT_ENOUGH_MEMORY;
                                break;
                            }
                            KernCopyIn(pFeal, filestatus.fpFEAList, cbList);
                            memset(pFeal, 0, cbList);
                            pFeal->cbList = sizeof(pFeal->cbList);
                            KernCopyOut(filestatus.fpFEAList, pFeal, cbList);
                            KernCopyOut(pData, &filestatus, sizeof(EAOP));
                            RTMemFree(pFeal);
                            break;
                        }

                    case FIL_NAMEISVALID:
                        hrc = 0;
                        break;

                    case 7:
                        strcpy((char *)pData, pszFullName);
                        hrc = 0;
                        break;

                    default:
                        hrc = ERROR_INVALID_LEVEL;
                        goto FS32_PATHINFOEXIT;
                }

                hrc = NO_ERROR;
            }
            break;

        case PI_SET: // set
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

                            if (filestatus.attrFile & FILE_DIRECTORY)
                            {
                                usMask &= ~FILE_DIRECTORY;
                            }

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

                            if (filestatus.attrFile & FILE_DIRECTORY)
                            {
                                usMask &= ~FILE_DIRECTORY;
                            }

                            if (filestatus.attrFile & usMask)
                            {
                                hrc = ERROR_ACCESS_DENIED;
                                goto FS32_PATHINFOEXIT;
                            }

                            break;
                        }

                    case FIL_QUERYEASIZE:
                    case FIL_QUERYEASIZEL:
                        hrc = NO_ERROR;
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
    if (params.Handle != SHFL_HANDLE_NIL)
        VbglR0SfClose(&g_clientHandle, &map, params.Handle);
    if (tmp)
        VbglR0SfUnmapFolder(&g_clientHandle, &map);
    if (pwsz)
        RTMemFree(pwsz);
    if (path)
        RTMemFree(path);
    if (file)
        RTMemFree(file);
    if (pszFullName)
        RTMemFree(pszFullName);

    log(" => %d\n", hrc);
    return hrc;
}


DECLASM(int)
FS32_MOUNT(ULONG flag, PVPFSI pvpfsi, PVBOXSFVP pvpfsd, ULONG hVPB, PCSZ pszBoot)
{
    log("FS32_MOUNT(%lx, %lx)\n", flag, hVPB);
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
