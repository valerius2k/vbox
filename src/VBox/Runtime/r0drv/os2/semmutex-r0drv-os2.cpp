/* $Id$ */
/** @file
 * IPRT - Mutex Semaphores, Ring-0 Driver, OS/2.
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



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define RTSEMMUTEX_WITHOUT_REMAPPING
#include "the-os2-kernel.h"

#include "internal/iprt.h"
#include <iprt/semaphore.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/lockvalidator.h>

#include "internal/magics.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * OS/2 mutex semaphore.
 */
typedef struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t volatile   u32Magic;
    /* Owner                     */
    uint32_t            fOwned;
    /* KEE Mutex Semaphore       */
    MutexLock_t		Mutex;
} RTSEMMUTEXINTERNAL, *PRTSEMMUTEXINTERNAL;



RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    return RTSemMutexCreateEx(phMutexSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
}


RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMMUTEX_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    AssertCompile(sizeof(RTSEMMUTEXINTERNAL) > sizeof(void *));
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)RTMemAlloc(sizeof(*pThis));

    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic = RTSEMMUTEX_MAGIC;
    KernAllocMutexLock(&pThis->Mutex);
    /* Owned by someone flag */
    pThis->fOwned = 0;

    *phMutexSem = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;
    if (pThis == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Invalidate it and signal the object just in case.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD, RTSEMMUTEX_MAGIC), VERR_INVALID_HANDLE);
    KernFreeMutexLock(&pThis->Mutex); // ???
    pThis->fOwned = 0;
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


/**
 * Internal worker for RTSemMutexRequest and RTSemMutexRequestNoResume
 *
 * @returns IPRT status code.
 * @param   hMutexSem           The mutex handle.
 * @param   cMillies            The timeout.
 * @param   fInterruptible      Whether it's interruptible
 *                              (RTSemMutexRequestNoResume) or not
 *                              (RTSemMutexRequest).
 */
static int rtR0SemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, BOOL fInterruptible)
{
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;

    /*
     * Validate and convert input.
     */
    if (!pThis)
        return VERR_INVALID_HANDLE;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Get the mutex.
     */
    int       rc;
    ULONG     ulTimeout = 0;
    ULONG     fBlock  = BLOCK_EXCLUSIVE_MUTEX;

    if (! fInterruptible)
        fBlock |= BLOCK_UNINTERRUPTABLE;

    if (cMillies == RT_INDEFINITE_WAIT)
        cMillies = SEM_INDEFINITE_WAIT;
    else
        ulTimeout = (ULONG)cMillies;

    ULONG ulData = (ULONG)VERR_INTERNAL_ERROR;

    rc = KernBlock((ULONG)pThis, ulTimeout, fBlock,
                    &pThis->Mutex,
                    &ulData);
    switch (rc)
    {
        case NO_ERROR:
            rc = (int)ulData;
            Assert(rc == VINF_SUCCESS || rc == VERR_SEM_DESTROYED);

	    if (pThis->u32Magic == RTSEMMUTEX_MAGIC)
	    {
	        pThis->fOwned = 1;
                rc = VINF_SUCCESS;
	    }

            rc = VERR_SEM_DESTROYED;
            break;

        case ERROR_TIMEOUT:
            Assert(Timeout != SEM_INDEFINITE_WAIT);
            rc = VERR_TIMEOUT;
            break;

        case ERROR_INTERRUPT:
            Assert(fInterruptible == RTSEMWAIT_FLAGS_INTERRUPTIBLE);
            rc = VERR_INTERRUPTED;
            break;

        default:
            AssertMsgFailed(("rc=%d\n", rc));
            rc = VERR_GENERAL_FAILURE;
            break;
    }
    return rc;
}


RTDECL(int) RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtR0SemMutexRequest(hMutexSem, cMillies, FALSE /*fInterruptible*/);
}


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return RTSemMutexRequest(hMutexSem, cMillies);
}


RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtR0SemMutexRequest(hMutexSem, cMillies, TRUE /*fInterruptible*/);
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return RTSemMutexRequestNoResume(hMutexSem, cMillies);
}


RTDECL(int) RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Release the mutex.
     */
    KernReleaseExclusiveMutex(&pThis->Mutex);
    /* Owned by someone flag */
    pThis->fOwned = 0;

    return VINF_SUCCESS;
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, false);

    return pThis && (pThis->fOwned != 0);
}
