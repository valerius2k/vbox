/* $Id$ */
/** @file
 * IPRT - Semaphores, OS/2.
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
#define INCL_DOSSEMAPHORES
#define INCL_ERRORS
#include <os2.h>
#undef RT_MAX

#include <iprt/time.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/lockvalidator.h>
#include <iprt/assert.h>
#include <iprt/err.h>

#include "internal/magics.h"

/** Converts semaphore to OS/2 handle. */
#define SEM2HND(Sem) ((LHANDLE)(uintptr_t)Sem)


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value (RTSEMEVENTMULTI_MAGIC). */
    uint32_t            u32Magic;
    /** The event handle. */
    HEV                 hev;
#ifdef RTSEMEVENT_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
};

typedef R3R0PTRTYPE(struct RTSEMEVENTMULTIINTERNAL) RTSEMEVENTMULTIINTERNAL;
typedef R3R0PTRTYPE(struct RTSEMEVENTMULTIINTERNAL *) PRTSEMEVENTMULTIINTERNAL;

/*  */
struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t            u32Magic;
    /** The event handle. */
    HEV                 hev;
#ifdef RTSEMEVENT_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
    /** The creation flags. */
    uint32_t            fFlags;
};

typedef R3R0PTRTYPE(struct RTSEMEVENTINTERNAL) RTSEMEVENTINTERNAL;
typedef R3R0PTRTYPE(struct RTSEMEVENTINTERNAL *) PRTSEMEVENTINTERNAL;

/** Posix internal representation of a Mutex semaphore. */
struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t                u32Magic;
    /** Recursion count. */
    uint32_t volatile       cRecursions;
    /** The owner thread. */
    RTNATIVETHREAD volatile hNativeOwner;
    /** The mutex handle. */
    HMTX                    hMtx;
#ifdef RTSEMMUTEX_STRICT
    /** Lock validator record associated with this mutex. */
    RTLOCKVALRECEXCL        ValidatorRec;
#endif
};

typedef R3R0PTRTYPE(struct RTSEMMUTEXINTERNAL) RTSEMMUTEXINTERNAL;
typedef R3R0PTRTYPE(struct RTSEMMUTEXINTERNAL *) PRTSEMMUTEXINTERNAL;

RTDECL(int)  RTErrConvertFromOS2(unsigned uNativeCode);

RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));

    /*
     * Create the semaphore.
     * (Auto reset, not signaled, private event object.)
     */
    HEV hev = NULLHANDLE;
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)RTMemAlloc(sizeof(*pThis));

    if (! pThis)
        return VERR_NO_MEMORY;

    int rc = DosCreateEventSem(NULL, &hev, DCE_AUTORESET | DCE_POSTONE, 0);
    if (!rc)
    {
        *phEventSem = (RTSEMEVENT)pThis;
        pThis->hev = hev;
        pThis->u32Magic = RTSEMEVENT_MAGIC;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromOS2(rc);
}


RTDECL(int)   RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    if (hEventSem == NIL_RTSEMEVENT)
        return VINF_SUCCESS;

    /*
     * Close semaphore handle.
     */
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;

    int rc = DosCloseEventSem(pThis->hev);

    if (! rc)
    {
        RTMemFree(pThis);
        return VINF_SUCCESS;
    }

    AssertMsgFailed(("Destroy hEventSem %p failed, rc=%d\n", hEventSem, rc));
    return RTErrConvertFromOS2(rc);
}


RTDECL(int)   RTSemEventWaitNoResume(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies)
{
    /*
     * Wait for condition.
     */
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;
    int rc = DosWaitEventSem(pThis->hev, cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies);
    switch (rc)
    {
        case NO_ERROR:              return VINF_SUCCESS;
        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:         return VERR_TIMEOUT;
        case ERROR_INTERRUPT:       return VERR_INTERRUPTED;
        default:
        {
            AssertMsgFailed(("Wait on hEventSem %p failed, rc=%d\n", hEventSem, rc));
            return RTErrConvertFromOS2(rc);
        }
    }
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    /*
     * Signal the object.
     */
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;
    int rc = DosPostEventSem(pThis->hev);
    switch (rc)
    {
        case NO_ERROR:
        case ERROR_ALREADY_POSTED:
        case ERROR_TOO_MANY_POSTS:
            return VINF_SUCCESS;
        default:
            return RTErrConvertFromOS2(rc);
    }
}


RTDECL(void) RTSemEventSetSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
/** @todo implement RTSemEventSetSignaller and friends for OS/2 */
}


RTDECL(void) RTSemEventAddSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{

}


RTDECL(void) RTSemEventRemoveSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{

}


RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}

RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Create the semaphore.
     * (Manual reset, not signaled, private event object.)
     */
    HEV hev = NULLHANDLE;
    PRTSEMEVENTMULTIINTERNAL pThis = NULL;

    int rc = DosCreateEventSem(NULL, &hev, 0, FALSE);

    if (!rc)
    {
        pThis = (PRTSEMEVENTMULTIINTERNAL)RTMemAlloc(sizeof(*pThis));

        if (! pThis)
            return VERR_NO_MEMORY;

        pThis->hev = hev;
        pThis->u32Magic = RTSEMEVENTMULTI_MAGIC;
        *phEventMultiSem  = (RTSEMEVENTMULTI)pThis;
        return VINF_SUCCESS;
    }

    return RTErrConvertFromOS2(rc);
}


RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    if (hEventMultiSem == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;

    /*
     * Close semaphore handle.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;

    int rc = DosCloseEventSem(pThis->hev);

    if (!rc)
    {
        RTMemFree(pThis);
        return VINF_SUCCESS;
    }

    AssertMsgFailed(("Destroy hEventMultiSem %p failed, rc=%d\n", hEventMultiSem, rc));
    return RTErrConvertFromOS2(rc);
}


RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Signal the object.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;

    int rc = DosPostEventSem(pThis->hev);

    switch (rc)
    {
        case NO_ERROR:
        case ERROR_ALREADY_POSTED:
        case ERROR_TOO_MANY_POSTS:
            return VINF_SUCCESS;
        default:
            return RTErrConvertFromOS2(rc);
    }
}


RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Reset the object.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    ULONG ulIgnore = 0;
    int rc = DosResetEventSem(pThis->hev, &ulIgnore);
    switch (rc)
    {
        case NO_ERROR:
        case ERROR_ALREADY_RESET:
            return VINF_SUCCESS;
        default:
            return RTErrConvertFromOS2(rc);
    }
}


RTDECL(int)  RTSemEventMultiWaitNoResume(RTSEMEVENTMULTI hEventMultiSem, RTMSINTERVAL cMillies)
{
    /*
     * Wait for condition.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;

    int rc = DosWaitEventSem(pThis->hev, cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies);

    switch (rc)
    {
        case NO_ERROR:              return VINF_SUCCESS;
        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:         return VERR_TIMEOUT;
        case ERROR_INTERRUPT:       return VERR_INTERRUPTED;
        default:
        {
            AssertMsgFailed(("Wait on hEventMultiSem %p failed, rc=%d\n", hEventMultiSem, rc));
            rc = RTErrConvertFromOS2(rc);
            return rc;
        }
    }
}

RTDECL(int) rtSemEventMultiOs2Wait(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                       PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

    /*
     * Convert the timeout to a millisecond count.
     */
    uint64_t    uAbsDeadline;
    LONG    	dwMsTimeout;

    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
    {
        dwMsTimeout  = -1; // INFINITE;
        uAbsDeadline = UINT64_MAX;
    }
    else
    {
        if (fFlags & RTSEMWAIT_FLAGS_NANOSECS)
            uTimeout = uTimeout < UINT64_MAX - UINT32_C(1000000) / 2
                     ? (uTimeout + UINT32_C(1000000) / 2) / UINT32_C(1000000)
                     : UINT64_MAX / UINT32_C(1000000);
        if (fFlags & RTSEMWAIT_FLAGS_ABSOLUTE)
        {
            uAbsDeadline = uTimeout;
            uint64_t u64Now = RTTimeSystemMilliTS();
            if (u64Now < uTimeout)
                uTimeout -= u64Now;
            else
                uTimeout = 0;
        }
        else if (fFlags & RTSEMWAIT_FLAGS_RESUME)
            uAbsDeadline = RTTimeSystemMilliTS() + uTimeout;
        else
            uAbsDeadline = UINT64_MAX;

        dwMsTimeout = (uTimeout < UINT32_MAX)
                    ? (LONG)uTimeout
                    : (LONG)-1;
    }

    /*
     * Do the wait.
     */
    int rc;
#ifdef RTSEMEVENT_STRICT
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();

    if ((PRTSEMEVENTMULTIINTERNAL)pThis->fEverHadSignallers)
    {
        do
            // rc = WaitForSingleObjectEx(pThis->hev, 0 /*Timeout*/, TRUE /*fAlertable*/);
            rc = RTErrConvertFromOS2(DosWaitEventSem(pThis->hev, 0 /*Timeout*/));
        while (rc == VERR_INTERRUPTED && (fFlags & RTSEMWAIT_FLAGS_RESUME));

        if ((rc != VERR_INTERRUPTED && rc != VERR_TIMEOUT) || dwMsTimeout == 0)
        {
            //return rtSemEventWaitHandleStatus(pThis, fFlags, rc);
            rc = RTErrConvertFromOS2(rc);
            return rc;
        }

        int rc9 = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                        dwMsTimeout, RTTHREADSTATE_EVENT_MULTI, true);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
#endif
    RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT_MULTI, true);

    //rc = WaitForSingleObjectEx(pThis->hev, dwMsTimeout, TRUE /*fAlertable*/);
    rc = RTErrConvertFromOS2(DosWaitEventSem(pThis->hev, dwMsTimeout));

    if ((rc == VERR_INTERRUPTED || rc == VERR_TIMEOUT) && (fFlags & RTSEMWAIT_FLAGS_RESUME))
    {
        while ( (rc == VERR_INTERRUPTED || rc == VERR_TIMEOUT)
               && RTTimeSystemMilliTS() < uAbsDeadline)
            //rc = WaitForSingleObjectEx(pThis->hev, dwMsTimeout, TRUE /*fAlertable*/);
            rc = RTErrConvertFromOS2(DosWaitEventSem(pThis->hev, dwMsTimeout));
    }

    RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT_MULTI);

    // return rtSemEventWaitHandleStatus(pThis, fFlags, rc);
    return rc;
}


RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtSemEventMultiOs2Wait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemEventMultiOs2Wait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemEventMultiOs2Wait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(void) RTSemEventMultiSetSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
    /** @todo implement RTSemEventMultiSetSignaller on OS/2 */
}


RTDECL(void) RTSemEventMultiAddSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
}


RTDECL(void) RTSemEventMultiRemoveSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
}


#undef RTSemMutexCreate
RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    return RTSemMutexCreateEx(phMutexSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
}


RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMMUTEX_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Create the semaphore.
     */
    HMTX hmtx;
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)RTMemAlloc(sizeof(*pThis));

    if (! pThis)
        return VERR_NO_MEMORY;

    int rc = DosCreateMutexSem(NULL, &hmtx, 0, FALSE);

    if (! rc)
    {
        /** @todo implement lock validation of OS/2 mutex semaphores. */
        *phMutexSem = (RTSEMMUTEX)pThis;
        pThis->hMtx = hmtx;
        pThis->u32Magic = RTSEMMUTEX_MAGIC;
        return VINF_SUCCESS;
    }

    return RTErrConvertFromOS2(rc);
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    if (hMutexSem == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;

    /*
     * Close semaphore handle.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;

    int rc = DosCloseMutexSem(pThis->hMtx);

    if (! rc)
    {
        RTMemFree(pThis);
        return VINF_SUCCESS;
    }

    AssertMsgFailed(("Destroy hMutexSem %p failed, rc=%d\n", hMutexSem, rc));
    return RTErrConvertFromOS2(rc);
}



RTDECL(uint32_t) RTSemMutexSetSubClass(RTSEMMUTEX hMutexSem, uint32_t uSubClass)
{
#if 0 /** @todo def RTSEMMUTEX_STRICT */
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = (RTSEMMUTEXINTERNAL *)hMutexSem;
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);

    return RTLockValidatorRecExclSetSubClass(pThis->ValidatorRec, uSubClass);
#else
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}

#undef RTSemMutexRequestNoResume
RTDECL(int)  RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    /*
     * Lock mutex semaphore.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;

    int rc = DosRequestMutexSem(pThis->hMtx, cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies);

    switch (rc)
    {
        case NO_ERROR:              return VINF_SUCCESS;
        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:         return VERR_TIMEOUT;
        case ERROR_INTERRUPT:       return VERR_INTERRUPTED;
        case ERROR_SEM_OWNER_DIED:  return VERR_SEM_OWNER_DIED;
        default:
        {
            AssertMsgFailed(("Wait on hMutexSem %p failed, rc=%d\n", hMutexSem, rc));
            return RTErrConvertFromOS2(rc);
        }
    }
}

RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
//    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
//    return rtSemMutexRequestNoResume(hMutexSem, cMillies, &SrcPos);
    return RTSemMutexRequestNoResume(hMutexSem, cMillies);
}


RTDECL(int)  RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Unlock mutex semaphore.
     */
    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;

    int rc = DosReleaseMutexSem(pThis->hMtx);

    if (!rc)
        return VINF_SUCCESS;

    AssertMsgFailed(("Release hMutexSem %p failed, rc=%d\n", hMutexSem, rc));
    return RTErrConvertFromOS2(rc);
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    /*
     * Unlock mutex semaphore.
     */
    PID     pid;
    TID     tid;
    ULONG   cRecursions;

    PRTSEMMUTEXINTERNAL pThis = (PRTSEMMUTEXINTERNAL)hMutexSem;

    int rc = DosQueryMutexSem(pThis->hMtx, &pid, &tid, &cRecursions);

    if (!rc)
        return cRecursions != 0;

    AssertMsgFailed(("DosQueryMutexSem %p failed, rc=%d\n", hMutexSem, rc));
    return rc == ERROR_SEM_OWNER_DIED;
}

