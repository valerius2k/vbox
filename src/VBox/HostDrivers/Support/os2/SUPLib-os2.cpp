/* $Id$ */
/** @file
 * VirtualBox Support Library - OS/2 specific parts.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define INCL_BASE
#define INCL_ERRORS
#define INCL_DOSMEMMGR
#include <os2.h>
#undef RT_MAX

#ifdef IN_SUP_HARDENED_R3
# undef DEBUG /* Warning: disables RT_STRICT */
# define LOG_DISABLED
# define RTLOG_REL_DISABLED
# include <iprt/log.h>
#endif

#include <VBox/types.h>
#include <VBox/sup.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include "../SUPLibInternal.h"
#include "../SUPDrvIOC.h"

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>

/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** OS/2 Device name. */
#define DEVICE_NAME     "/dev/vboxdrv$"



int suplibOsInit(PSUPLIBDATA pThis, bool fPreInited, bool fUnrestricted, SUPINITOP *penmWhat, PRTERRINFO pErrInfo)
{
    /*
     * Nothing to do if pre-inited.
     */
    if (fPreInited)
        return VINF_SUCCESS;

    /*
     * Try open the device.
     */
    ULONG ulAction = 0;
    HFILE hDevice = (HFILE)-1;
    APIRET rc = DosOpen((PCSZ)DEVICE_NAME,
                        &hDevice,
                        &ulAction,
                        0,
                        FILE_NORMAL,
                        OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                        OPEN_FLAGS_NOINHERIT | OPEN_SHARE_DENYNONE | OPEN_ACCESS_READWRITE,
                        NULL);
    if (rc)
    {
        int vrc;
        switch (rc)
        {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:  vrc = VERR_VM_DRIVER_NOT_INSTALLED; break;
            default:                    vrc = VERR_VM_DRIVER_OPEN_ERROR; break;
        }
        LogRel(("Failed to open \"%s\", rc=%d, vrc=%Rrc\n", DEVICE_NAME, rc, vrc));
        return vrc;
    }

    pThis->hDevice = hDevice;
    pThis->fUnrestricted = true;
    return VINF_SUCCESS;
}


#ifndef IN_SUP_HARDENED_R3

int suplibOsTerm(PSUPLIBDATA pThis)
{
    /*
     * Check if we're inited at all.
     */
    if (pThis->hDevice != (intptr_t)NIL_RTFILE)
    {
        APIRET rc = DosClose((HFILE)pThis->hDevice);
        AssertMsg(rc == NO_ERROR, ("%d\n", rc)); NOREF(rc);
        pThis->hDevice = (intptr_t)NIL_RTFILE;
    }

    return 0;
}


int suplibOsInstall(void)
{
    /** @remark OS/2: Not supported */
    return VERR_NOT_SUPPORTED;
}


int suplibOsUninstall(void)
{
    /** @remark OS/2: Not supported */
    return VERR_NOT_SUPPORTED;
}


int suplibOsIOCtl(PSUPLIBDATA pThis, uintptr_t uFunction, void *pvReq, size_t cbReq)
{
    ULONG cbReturned = sizeof(SUPREQHDR);
    ULONG cbSize, fFlags, rc;
    PSUPCALLSERVICE pReq = (PSUPCALLSERVICE)pvReq;
    //ULONG cbRetIn  = (ULONG)pHdr->cbIn;
    //ULONG cbRetOut = (ULONG)pHdr->cbOut; // hang!!!
    //Assert(cbReq == RT_MAX(pReq->Hdr->cbIn, pReq->Hdr->cbOut));

    /* Check if the buffer is committed first, to avoid returning ERROR_INVALID_PARAMETER */
    /* if ( (DosQueryMem((PVOID)pReq->u.In.u64Arg, &cbSize, &fFlags) != NO_ERROR) ||
         (cbSize < cbReturned) || (cbSize < cbReq) ||
         !(fFlags & PAG_READ) || !(fFlags & PAG_WRITE) || !(fFlags & PAG_COMMIT) )
        rc = ERROR_ACCESS_DENIED;
    else */
        //printf("DosDevIOCtl enter\n");
        rc = DosDevIOCtl((HFILE)pThis->hDevice, SUP_CTL_CATEGORY, uFunction,
                         pvReq, cbReturned, &cbReturned,
                         NULL, 0, NULL);
        //printf("DosDevIOCtl exit\n");

    //                     pvReq, cbRetIn,  &cbRetIn,
    //                     pvReq, cbRetOut, &cbRetOut);

    // rc = DosDevIOCtl((HFILE)pThis->hDevice, SUP_CTL_CATEGORY, uFunction,
    //                     pvReq, cbReturned, &cbReturned,
    //                     NULL, 0, NULL);

    if (RT_LIKELY(rc == NO_ERROR))
        return VINF_SUCCESS;

    //if (rc == ERROR_BUFFER_OVERFLOW)
    //    rc = ERROR_ACCESS_DENIED;

    // suplibOsIOCtl: rc=87, cbSize=5152, fFlags=13
    // cbReturned=24, pHdr->cbIn=328, pHdr->cbOut=328

    //printf("suplibOsIOCtl: rc=%lu, cbSize=%lu, fFlags=%lx\n", rc, cbSize, fFlags);
    //printf("cbReturned=%lu, pHdr->cbIn=%u, pHdr->cbOut=%u\n", cbReturned, pHdr->cbIn, pHdr->cbOut);
    return RTErrConvertFromOS2(rc);
}


int suplibOsIOCtlFast(PSUPLIBDATA pThis, uintptr_t uFunction, uintptr_t idCpu)
{
    NOREF(idCpu);
    int rc = DosDevIOCtl((HFILE)pThis->hDevice, SUP_CTL_CATEGORY_FAST, uFunction,
                         NULL, 0, NULL,
                         NULL, 0, NULL);
    return RTErrConvertFromOS2(rc);
}


int suplibOsPageAlloc(PSUPLIBDATA pThis, size_t cPages, void **ppvPages)
{
    NOREF(pThis);
    *ppvPages = NULL;
    int rc = DosAllocMem(ppvPages, cPages << PAGE_SHIFT, PAG_READ | PAG_WRITE | PAG_EXECUTE | PAG_COMMIT | OBJ_ANY);
    if (rc == ERROR_INVALID_PARAMETER)
        rc = DosAllocMem(ppvPages, cPages << PAGE_SHIFT, PAG_READ | PAG_WRITE | PAG_EXECUTE | PAG_COMMIT | OBJ_ANY);
    if (!rc)
        rc = VINF_SUCCESS;
    else
        rc = RTErrConvertFromOS2(rc);
    return rc;
}


int suplibOsPageFree(PSUPLIBDATA pThis, void *pvPages, size_t /* cPages */)
{
    NOREF(pThis);
    if (pvPages)
    {
        int rc = DosFreeMem(pvPages);
        Assert(!rc); NOREF(rc);
    }
    return VINF_SUCCESS;
}

#endif /* !IN_SUP_HARDENED_R3 */

