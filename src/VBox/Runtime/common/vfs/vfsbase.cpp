/* $Id$ */
/** @file
 * IPRT - Virtual File System, Base.
 */

/*
 * Copyright (C) 2010-2015 Oracle Corporation
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
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include "internal/file.h"
#include "internal/fs.h"
#include "internal/magics.h"
//#include "internal/vfs.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The instance data alignment. */
#define RTVFS_INST_ALIGNMENT        16U

/** The max number of symbolic links to resolve in a path. */
#define RTVFS_MAX_LINKS             20U


/** Asserts that the VFS base object vtable is valid. */
#define RTVFSOBJ_ASSERT_OPS(a_pObjOps, a_enmType) \
    do \
    { \
        Assert((a_pObjOps)->uVersion == RTVFSOBJOPS_VERSION); \
        Assert((a_pObjOps)->enmType == (a_enmType) || (a_enmType) == RTVFSOBJTYPE_INVALID); \
        AssertPtr((a_pObjOps)->pszName); \
        Assert(*(a_pObjOps)->pszName); \
        AssertPtr((a_pObjOps)->pfnClose); \
        AssertPtr((a_pObjOps)->pfnQueryInfo); \
        Assert((a_pObjOps)->uEndMarker == RTVFSOBJOPS_VERSION); \
    } while (0)

/** Asserts that the VFS set object vtable is valid. */
#define RTVFSOBJSET_ASSERT_OPS(a_pSetOps, a_offObjOps) \
    do \
    { \
        Assert((a_pSetOps)->uVersion == RTVFSOBJSETOPS_VERSION); \
        Assert((a_pSetOps)->offObjOps == (a_offObjOps)); \
        AssertPtr((a_pSetOps)->pfnSetMode); \
        AssertPtr((a_pSetOps)->pfnSetTimes); \
        AssertPtr((a_pSetOps)->pfnSetOwner); \
        Assert((a_pSetOps)->uEndMarker == RTVFSOBJSETOPS_VERSION); \
    } while (0)

/** Asserts that the VFS I/O stream vtable is valid. */
#define RTVFSIOSTREAM_ASSERT_OPS(pIoStreamOps, a_enmType) \
    do { \
        RTVFSOBJ_ASSERT_OPS(&(pIoStreamOps)->Obj, a_enmType); \
        Assert((pIoStreamOps)->uVersion == RTVFSIOSTREAMOPS_VERSION); \
        Assert(!((pIoStreamOps)->fFeatures & ~RTVFSIOSTREAMOPS_FEAT_VALID_MASK)); \
        AssertPtr((pIoStreamOps)->pfnRead); \
        AssertPtr((pIoStreamOps)->pfnWrite); \
        AssertPtr((pIoStreamOps)->pfnFlush); \
        AssertPtr((pIoStreamOps)->pfnPollOne); \
        AssertPtr((pIoStreamOps)->pfnTell); \
        AssertPtrNull((pIoStreamOps)->pfnSkip); \
        AssertPtrNull((pIoStreamOps)->pfnZeroFill); \
        Assert((pIoStreamOps)->uEndMarker == RTVFSIOSTREAMOPS_VERSION); \
    } while (0)

/** Asserts that the VFS symlink vtable is valid. */
#define RTVFSSYMLINK_ASSERT_OPS(pSymlinkOps, a_enmType) \
    do { \
        RTVFSOBJ_ASSERT_OPS(&(pSymlinkOps)->Obj, a_enmType); \
        RTVFSOBJSET_ASSERT_OPS(&(pSymlinkOps)->ObjSet, \
            RT_OFFSETOF(RTVFSSYMLINKOPS, Obj) - RT_OFFSETOF(RTVFSSYMLINKOPS, ObjSet)); \
        Assert((pSymlinkOps)->uVersion == RTVFSSYMLINKOPS_VERSION); \
        Assert(!(pSymlinkOps)->fReserved); \
        AssertPtr((pSymlinkOps)->pfnRead); \
        Assert((pSymlinkOps)->uEndMarker == RTVFSSYMLINKOPS_VERSION); \
    } while (0)


/** Validates a VFS handle and returns @a rcRet if it's invalid. */
#define RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, rcRet) \
    do { \
        if ((hVfs) != NIL_RTVFS) \
        { \
            AssertPtrReturn((hVfs), (rcRet)); \
            AssertReturn((hVfs)->uMagic == RTVFS_MAGIC, (rcRet)); \
        } \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @todo Move all this stuff to internal/vfs.h */


/**
 * The VFS internal lock data.
 */
typedef struct RTVFSLOCKINTERNAL
{
    /** The number of references to the this lock. */
    uint32_t volatile       cRefs;
    /** The lock type. */
    RTVFSLOCKTYPE           enmType;
    /** Type specific data. */
    union
    {
        /** Read/Write semaphore handle. */
        RTSEMRW             hSemRW;
        /** Fast mutex semaphore handle. */
        RTSEMFASTMUTEX      hFastMtx;
        /** Regular mutex semaphore handle. */
        RTSEMMUTEX          hMtx;
    } u;
} RTVFSLOCKINTERNAL;


/**
 * The VFS base object handle data.
 *
 * All other VFS handles are derived from this one.  The final handle type is
 * indicated by RTVFSOBJOPS::enmType via the RTVFSOBJINTERNAL::pOps member.
 */
typedef struct RTVFSOBJINTERNAL
{
    /** The VFS magic (RTVFSOBJ_MAGIC). */
    uint32_t                uMagic;
    /** The number of references to this VFS object. */
    uint32_t volatile       cRefs;
    /** Pointer to the instance data. */
    void                   *pvThis;
    /** The vtable. */
    PCRTVFSOBJOPS           pOps;
    /** The lock protecting all access to the VFS.
     * Only valid RTVFS_C_THREAD_SAFE is set, otherwise it is NIL_RTVFSLOCK. */
    RTVFSLOCK               hLock;
    /** Reference back to the VFS containing this object. */
    RTVFS                   hVfs;
} RTVFSOBJINTERNAL;


/**
 * The VFS filesystem stream handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSFSSTREAMINTERNAL
{
    /** The VFS magic (RTVFSFSTREAM_MAGIC). */
    uint32_t                uMagic;
    /** File open flags, at a minimum the access mask. */
    uint32_t                fFlags;
    /** The vtable. */
    PCRTVFSFSSTREAMOPS      pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSFSSTREAMINTERNAL;


/**
 * The VFS handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSINTERNAL
{
    /** The VFS magic (RTVFS_MAGIC). */
    uint32_t                uMagic;
    /** Creation flags (RTVFS_C_XXX). */
    uint32_t                fFlags;
    /** The vtable. */
    PCRTVFSOPS              pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSINTERNAL;


/**
 * The VFS directory handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSDIRINTERNAL
{
    /** The VFS magic (RTVFSDIR_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSDIROPS           pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSDIRINTERNAL;


/**
 * The VFS symbolic link handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSSYMLINKINTERNAL
{
    /** The VFS magic (RTVFSSYMLINK_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSSYMLINKOPS       pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSSYMLINKINTERNAL;


/**
 * The VFS I/O stream handle data.
 *
 * This is often part of a type specific handle, like a file or pipe.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSIOSTREAMINTERNAL
{
    /** The VFS magic (RTVFSIOSTREAM_MAGIC). */
    uint32_t                uMagic;
    /** File open flags, at a minimum the access mask. */
    uint32_t                fFlags;
    /** The vtable. */
    PCRTVFSIOSTREAMOPS      pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSIOSTREAMINTERNAL;


/**
 * The VFS file handle data.
 *
 * @extends RTVFSIOSTREAMINTERNAL
 */
typedef struct RTVFSFILEINTERNAL
{
    /** The VFS magic (RTVFSFILE_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSFILEOPS          pOps;
    /** The stream handle data. */
    RTVFSIOSTREAMINTERNAL   Stream;
} RTVFSFILEINTERNAL;

#if 0 /* later */

/**
 * The VFS pipe handle data.
 *
 * @extends RTVFSIOSTREAMINTERNAL
 */
typedef struct RTVFSPIPEINTERNAL
{
    /** The VFS magic (RTVFSPIPE_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSPIPEOPS          pOps;
    /** The stream handle data. */
    RTVFSIOSTREAMINTERNAL   Stream;
} RTVFSPIPEINTERNAL;


/**
 * The VFS socket handle data.
 *
 * @extends RTVFSIOSTREAMINTERNAL
 */
typedef struct RTVFSSOCKETINTERNAL
{
    /** The VFS magic (RTVFSSOCKET_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSSOCKETOPS        pOps;
    /** The stream handle data. */
    RTVFSIOSTREAMINTERNAL   Stream;
} RTVFSSOCKETINTERNAL;

#endif /* later */



/*
 *
 *  V F S   L o c k   A b s t r a c t i o n
 *  V F S   L o c k   A b s t r a c t i o n
 *  V F S   L o c k   A b s t r a c t i o n
 *
 *
 */


RTDECL(uint32_t) RTVfsLockRetain(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->enmType > RTVFSLOCKTYPE_INVALID && pThis->enmType < RTVFSLOCKTYPE_END, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p %d\n", cRefs, pThis, pThis->enmType));
    return cRefs;
}


/**
 * Destroys a VFS lock handle.
 *
 * @param   pThis               The lock to destroy.
 */
static void rtVfsLockDestroy(RTVFSLOCKINTERNAL *pThis)
{
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            RTSemRWDestroy(pThis->u.hSemRW);
            pThis->u.hSemRW = NIL_RTSEMRW;
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            RTSemFastMutexDestroy(pThis->u.hFastMtx);
            pThis->u.hFastMtx = NIL_RTSEMFASTMUTEX;
            break;

        case RTVFSLOCKTYPE_MUTEX:
            RTSemMutexDestroy(pThis->u.hMtx);
            pThis->u.hFastMtx = NIL_RTSEMMUTEX;
            break;

        default:
            AssertMsgFailedReturnVoid(("%p %d\n", pThis, pThis->enmType));
    }

    pThis->enmType = RTVFSLOCKTYPE_INVALID;
    RTMemFree(pThis);
}


RTDECL(uint32_t) RTVfsLockRelease(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    if (pThis == NIL_RTVFSLOCK)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->enmType > RTVFSLOCKTYPE_INVALID && pThis->enmType < RTVFSLOCKTYPE_END, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p %d\n", cRefs, pThis, pThis->enmType));
    if (cRefs == 0)
        rtVfsLockDestroy(pThis);
    return cRefs;
}


/**
 * Creates a read/write lock.
 *
 * @returns IPRT status code
 * @param   phLock              Where to return the lock handle.
 */
static int rtVfsLockCreateRW(PRTVFSLOCK phLock)
{
    RTVFSLOCKINTERNAL *pThis = (RTVFSLOCKINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->cRefs    = 1;
    pThis->enmType  = RTVFSLOCKTYPE_RW;

    int rc = RTSemRWCreate(&pThis->u.hSemRW);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phLock = pThis;
    return VINF_SUCCESS;
}


/**
 * Creates a fast mutex lock.
 *
 * @returns IPRT status code
 * @param   phLock              Where to return the lock handle.
 */
static int rtVfsLockCreateFastMutex(PRTVFSLOCK phLock)
{
    RTVFSLOCKINTERNAL *pThis = (RTVFSLOCKINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->cRefs    = 1;
    pThis->enmType  = RTVFSLOCKTYPE_FASTMUTEX;

    int rc = RTSemFastMutexCreate(&pThis->u.hFastMtx);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phLock = pThis;
    return VINF_SUCCESS;

}


/**
 * Creates a mutex lock.
 *
 * @returns IPRT status code
 * @param   phLock              Where to return the lock handle.
 */
static int rtVfsLockCreateMutex(PRTVFSLOCK phLock)
{
    RTVFSLOCKINTERNAL *pThis = (RTVFSLOCKINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->cRefs    = 1;
    pThis->enmType  = RTVFSLOCKTYPE_MUTEX;

    int rc = RTSemMutexCreate(&pThis->u.hMtx);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phLock = pThis;
    return VINF_SUCCESS;
}


/**
 * Acquires the lock for reading.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockAcquireReadSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWRequestRead(pThis->u.hSemRW, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRequest(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRequest(pThis->u.hMtx, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}


/**
 * Release a lock held for reading.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockReleaseReadSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWReleaseRead(pThis->u.hSemRW);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRelease(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRelease(pThis->u.hMtx);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}


/**
 * Acquires the lock for writing.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockAcquireWriteSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWRequestWrite(pThis->u.hSemRW, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRequest(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRequest(pThis->u.hMtx, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}


/**
 * Release a lock held for writing.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockReleaseWriteSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWReleaseWrite(pThis->u.hSemRW);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRelease(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRelease(pThis->u.hMtx);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}



/*
 *
 *  B A S E   O B J E C T
 *  B A S E   O B J E C T
 *  B A S E   O B J E C T
 *
 */

/**
 * Internal object retainer that asserts sanity in strict builds.
 *
 * @param   pThis               The base object handle data.
 */
DECLINLINE(void) rtVfsObjRetainVoid(RTVFSOBJINTERNAL *pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M,
              ("%#x %p ops=%p %s (%d)\n", cRefs, pThis, pThis->pOps, pThis->pOps->pszName, pThis->pOps->enmType));
    NOREF(cRefs);
}


/**
 * Initializes the base object part of a new object.
 *
 * @returns IPRT status code.
 * @param   pThis               Pointer to the base object part.
 * @param   pObjOps             The base object vtable.
 * @param   hVfs                The VFS handle to associate with.
 * @param   hLock               The lock handle, pseudo handle or nil.
 * @param   pvThis              Pointer to the private data.
 */
static int rtVfsObjInitNewObject(RTVFSOBJINTERNAL *pThis, PCRTVFSOBJOPS pObjOps, RTVFS hVfs, RTVFSLOCK hLock, void *pvThis)
{
    /*
     * Deal with the lock first as that's the most complicated matter.
     */
    if (hLock != NIL_RTVFSLOCK)
    {
        int rc;
        if (hLock == RTVFSLOCK_CREATE_RW)
        {
            rc = rtVfsLockCreateRW(&hLock);
            AssertRCReturn(rc, rc);
        }
        else if (hLock == RTVFSLOCK_CREATE_FASTMUTEX)
        {
            rc = rtVfsLockCreateFastMutex(&hLock);
            AssertRCReturn(rc, rc);
        }
        else if (hLock == RTVFSLOCK_CREATE_MUTEX)
        {
            rc = rtVfsLockCreateMutex(&hLock);
            AssertRCReturn(rc, rc);
        }
        else
        {
            /*
             * The caller specified a lock, we consume the this reference.
             */
            AssertPtrReturn(hLock, VERR_INVALID_HANDLE);
            AssertReturn(hLock->enmType > RTVFSLOCKTYPE_INVALID && hLock->enmType < RTVFSLOCKTYPE_END, VERR_INVALID_HANDLE);
            AssertReturn(hLock->cRefs > 0, VERR_INVALID_HANDLE);
        }
    }
    else if (hVfs != NIL_RTVFS)
    {
        /*
         * Retain a reference to the VFS lock, if there is one.
         */
        hLock = hVfs->Base.hLock;
        if (hLock != NIL_RTVFSLOCK)
        {
            uint32_t cRefs = RTVfsLockRetain(hLock);
            if (RT_UNLIKELY(cRefs == UINT32_MAX))
                return VERR_INVALID_HANDLE;
        }
    }


    /*
     * Do the actual initializing.
     */
    pThis->uMagic  = RTVFSOBJ_MAGIC;
    pThis->pvThis  = pvThis;
    pThis->pOps    = pObjOps;
    pThis->cRefs   = 1;
    pThis->hVfs    = hVfs;
    pThis->hLock   = hLock;
    if (hVfs != NIL_RTVFS)
        rtVfsObjRetainVoid(&hVfs->Base);

    return VINF_SUCCESS;
}


RTDECL(int) RTVfsNewBaseObj(PCRTVFSOBJOPS pObjOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock,
                            PRTVFSOBJ phVfsObj, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pObjOps);
    AssertReturn(pObjOps->uVersion   == RTVFSOBJOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pObjOps->uEndMarker == RTVFSOBJOPS_VERSION, VERR_VERSION_MISMATCH);
    RTVFSOBJ_ASSERT_OPS(pObjOps, RTVFSOBJTYPE_BASE);
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsObj);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSOBJINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSOBJINTERNAL *pThis = (RTVFSOBJINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(pThis, pObjOps, hVfs, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phVfsObj    = pThis;
    *ppvInstance = pThis->pvThis;
    return VINF_SUCCESS;
}


/**
 * Internal object retainer that asserts sanity in strict builds.
 *
 * @returns The new reference count.
 * @param   pThis               The base object handle data.
 */
DECLINLINE(uint32_t) rtVfsObjRetain(RTVFSOBJINTERNAL *pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M,
              ("%#x %p ops=%p %s (%d)\n", cRefs, pThis, pThis->pOps, pThis->pOps->pszName, pThis->pOps->enmType));
    return cRefs;
}


RTDECL(uint32_t) RTVfsObjRetain(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, UINT32_MAX);

    return rtVfsObjRetain(pThis);
}


/**
 * Does the actual object destruction for rtVfsObjRelease().
 *
 * @param   pThis               The object to destroy.
 */
static void rtVfsObjDestroy(RTVFSOBJINTERNAL *pThis)
{
    RTVFSOBJTYPE const enmType = pThis->pOps->enmType;

    /*
     * Invalidate the object.
     */
    RTVfsLockAcquireWrite(pThis->hLock);    /* paranoia */
    void *pvToFree = NULL;
    switch (enmType)
    {
        case RTVFSOBJTYPE_BASE:
            pvToFree = pThis;
            break;

        case RTVFSOBJTYPE_VFS:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSINTERNAL, Base)->uMagic, RTVFS_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_FS_STREAM:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSFSSTREAMINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSFSSTREAMINTERNAL, Base)->uMagic, RTVFSFSSTREAM_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_IO_STREAM:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base)->uMagic, RTVFSIOSTREAM_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_DIR:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSDIRINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSDIRINTERNAL, Base)->uMagic, RTVFSDIR_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_FILE:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream.Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream.Base)->uMagic, RTVFSFILE_MAGIC_DEAD);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base)->uMagic, RTVFSIOSTREAM_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_SYMLINK:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSSYMLINKINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSSYMLINKINTERNAL, Base)->uMagic, RTVFSSYMLINK_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_INVALID:
        case RTVFSOBJTYPE_END:
        case RTVFSOBJTYPE_32BIT_HACK:
            AssertMsgFailed(("enmType=%d ops=%p %s\n", enmType, pThis->pOps, pThis->pOps->pszName));
            break;
        /* no default as we want gcc warnings. */
    }
    ASMAtomicWriteU32(&pThis->uMagic, RTVFSOBJ_MAGIC_DEAD);
    RTVfsLockReleaseWrite(pThis->hLock);

    /*
     * Close the object and free the handle.
     */
    int rc = pThis->pOps->pfnClose(pThis->pvThis);
    AssertRC(rc);
    RTMemFree(pvToFree);
}


/**
 * Internal object releaser that asserts sanity in strict builds.
 *
 * @returns The new reference count.
 * @param   pcRefs              The reference counter.
 */
DECLINLINE(uint32_t) rtVfsObjRelease(RTVFSOBJINTERNAL *pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p ops=%p %s (%d)\n", cRefs, pThis, pThis->pOps, pThis->pOps->pszName, pThis->pOps->enmType));
    if (cRefs == 0)
        rtVfsObjDestroy(pThis);
    return cRefs;
}


RTDECL(uint32_t) RTVfsObjRelease(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis == NIL_RTVFSOBJ)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(pThis);
}


RTDECL(RTVFS)           RTVfsObjToVfs(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFS);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFS);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_VFS)
        {
            rtVfsObjRetainVoid(pThis);
            return RT_FROM_MEMBER(pThis, RTVFSINTERNAL, Base);
        }
    }
    return NIL_RTVFS;
}


RTDECL(RTVFSFSSTREAM)   RTVfsObjToFsStream(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSFSSTREAM);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSFSSTREAM);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_FS_STREAM)
        {
            rtVfsObjRetainVoid(pThis);
            return RT_FROM_MEMBER(pThis, RTVFSFSSTREAMINTERNAL, Base);
        }
    }
    return NIL_RTVFSFSSTREAM;
}


RTDECL(RTVFSDIR)        RTVfsObjToDir(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSDIR);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSDIR);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_DIR)
        {
            rtVfsObjRetainVoid(pThis);
            return RT_FROM_MEMBER(pThis, RTVFSDIRINTERNAL, Base);
        }
    }
    return NIL_RTVFSDIR;
}


RTDECL(RTVFSIOSTREAM)   RTVfsObjToIoStream(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSIOSTREAM);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSIOSTREAM);

        if (   pThis->pOps->enmType == RTVFSOBJTYPE_IO_STREAM
            || pThis->pOps->enmType == RTVFSOBJTYPE_FILE)
        {
            rtVfsObjRetainVoid(pThis);
            return RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base);
        }
    }
    return NIL_RTVFSIOSTREAM;
}


RTDECL(RTVFSFILE)       RTVfsObjToFile(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSFILE);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSFILE);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_FILE)
        {
            rtVfsObjRetainVoid(pThis);
            return RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream.Base);
        }
    }
    return NIL_RTVFSFILE;
}


RTDECL(RTVFSSYMLINK)    RTVfsObjToSymlink(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSSYMLINK);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSSYMLINK);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_SYMLINK)
        {
            rtVfsObjRetainVoid(pThis);
            return RT_FROM_MEMBER(pThis, RTVFSSYMLINKINTERNAL, Base);
        }
    }
    return NIL_RTVFSSYMLINK;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromVfs(RTVFS hVfs)
{
    if (hVfs != NIL_RTVFS)
    {
        RTVFSOBJINTERNAL *pThis = &hVfs->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis);
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromFsStream(RTVFSFSSTREAM hVfsFss)
{
    if (hVfsFss != NIL_RTVFSFSSTREAM)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsFss->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis);
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromDir(RTVFSDIR hVfsDir)
{
    if (hVfsDir != NIL_RTVFSDIR)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsDir->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis);
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromIoStream(RTVFSIOSTREAM hVfsIos)
{
    if (hVfsIos != NIL_RTVFSIOSTREAM)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsIos->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis);
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromFile(RTVFSFILE hVfsFile)
{
    if (hVfsFile != NIL_RTVFSFILE)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsFile->Stream.Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis);
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromSymlink(RTVFSSYMLINK hVfsSym)
{
    if (hVfsSym != NIL_RTVFSSYMLINK)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsSym->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis);
        return pThis;
    }
    return NIL_RTVFSOBJ;
}



RTDECL(int)         RTVfsObjQueryInfo(RTVFSOBJ hVfsObj, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireRead(pThis->hLock);
    int rc = pThis->pOps->pfnQueryInfo(pThis->pvThis, pObjInfo, enmAddAttr);
    RTVfsLockReleaseRead(pThis->hLock);
    return rc;
}



/*
 *
 *  U T I L   U T I L   U T I L
 *  U T I L   U T I L   U T I L
 *  U T I L   U T I L   U T I L
 *
 */



/**
 * Removes dots from the path.
 *
 * @returns The new @a pszDst value.
 * @param   pPath               The path parsing buffer.
 * @param   pszDst              The current szPath position.  This will be
 *                              updated and returned.
 * @param   fTheEnd             Indicates whether we're at the end of the path
 *                              or not.
 * @param   piRestartComp       The component to restart parsing at.
 */
static char *rtVfsParsePathHandleDots(PRTVFSPARSEDPATH pPath, char *pszDst, bool fTheEnd, uint16_t *piRestartComp)
{
    if (pszDst[-1] != '.')
        return pszDst;

    if (pszDst[-2] == '/')
    {
        pPath->cComponents--;
        pszDst = &pPath->szPath[pPath->aoffComponents[pPath->cComponents]];
    }
    else if (pszDst[-2] == '.' && pszDst[-3] == '/')
    {
        pPath->cComponents -= pPath->cComponents > 1 ? 2 : 1;
        pszDst = &pPath->szPath[pPath->aoffComponents[pPath->cComponents]];
        if (piRestartComp && *piRestartComp + 1 >= pPath->cComponents)
            *piRestartComp = pPath->cComponents > 0 ? pPath->cComponents - 1 : 0;
    }
    else
        return pszDst;

    /*
     * Drop the trailing slash if we're at the end of the source path.
     */
    if (fTheEnd && pPath->cComponents == 0)
        pszDst--;
    return pszDst;
}


RTDECL(int) RTVfsParsePathAppend(PRTVFSPARSEDPATH pPath, const char *pszPath, uint16_t *piRestartComp)
{
    AssertReturn(*pszPath != '/', VERR_INTERNAL_ERROR_4);

    /* In case *piRestartComp was set higher than the number of components
       before making the call to this function. */
    if (piRestartComp && *piRestartComp + 1 >= pPath->cComponents)
        *piRestartComp = pPath->cComponents > 0 ? pPath->cComponents - 1 : 0;

    /*
     * Append a slash to the destination path if necessary.
     */
    char *pszDst = &pPath->szPath[pPath->cch];
    if (pPath->cComponents > 0)
    {
        *pszDst++ = '/';
        if (pszDst - &pPath->szPath[0] >= RTVFSPARSEDPATH_MAX)
            return VERR_FILENAME_TOO_LONG;
    }
    Assert(pszDst[-1] == '/');

    /*
     * Parse and append the relative path.
     */
    const char *pszSrc = pszPath;
    pPath->fDirSlash   = false;
    while (pszSrc[0])
    {
        /* Skip unncessary slashes. */
        while (pszSrc[0] == '/')
            pszSrc++;

        /* Copy until we encounter the next slash. */
        pPath->aoffComponents[pPath->cComponents++] = pszDst - &pPath->szPath[0];
        while (pszSrc[0])
        {
            if (pszSrc[0] == '/')
            {
                pszSrc++;
                if (pszSrc[0])
                    *pszDst++ = '/';
                else
                    pPath->fDirSlash = true;
                pszDst = rtVfsParsePathHandleDots(pPath, pszDst, pszSrc[0] == '\0', piRestartComp);
                break;
            }

            *pszDst++ = *pszSrc++;
            if (pszDst - &pPath->szPath[0] >= RTVFSPARSEDPATH_MAX)
                return VERR_FILENAME_TOO_LONG;
        }
    }
    pszDst = rtVfsParsePathHandleDots(pPath, pszDst, true /*fTheEnd*/, piRestartComp);

    /* Terminate the string and enter its length. */
    pszDst[0] = '\0';
    pszDst[1] = '\0';                   /* for aoffComponents */
    pPath->cch = (uint16_t)(pszDst - &pPath->szPath[0]);
    pPath->aoffComponents[pPath->cComponents] = pPath->cch + 1;

    return VINF_SUCCESS;
}


RTDECL(int) RTVfsParsePath(PRTVFSPARSEDPATH pPath, const char *pszPath, const char *pszCwd)
{
    if (*pszPath != '/')
    {
        /*
         * Relative, recurse and parse pszCwd first.
         */
        int rc = RTVfsParsePath(pPath, pszCwd, NULL /*crash if pszCwd is not absolute*/);
        if (RT_FAILURE(rc))
            return rc;
    }
    else
    {
        /*
         * Make pszPath relative, i.e. set up pPath for the root and skip
         * leading slashes in pszPath before appending it.
         */
        pPath->cch               = 1;
        pPath->cComponents       = 0;
        pPath->fDirSlash         = false;
        pPath->aoffComponents[0] = 1;
        pPath->aoffComponents[1] = 2;
        pPath->szPath[0]         = '/';
        pPath->szPath[1]         = '\0';
        pPath->szPath[2]         = '\0';
        while (pszPath[0] == '/')
            pszPath++;
        if (!pszPath[0])
            return VINF_SUCCESS;
    }
    return RTVfsParsePathAppend(pPath, pszPath, NULL);
}



RTDECL(int) RTVfsParsePathA(const char *pszPath, const char *pszCwd, PRTVFSPARSEDPATH *ppPath)
{
    /*
     * Allocate the output buffer and hand the problem to rtVfsParsePath.
     */
    int rc;
    PRTVFSPARSEDPATH pPath = (PRTVFSPARSEDPATH)RTMemTmpAlloc(sizeof(RTVFSPARSEDPATH));
    if (pPath)
    {
        rc = RTVfsParsePath(pPath, pszPath, pszCwd);
        if (RT_FAILURE(rc))
        {
            RTMemTmpFree(pPath);
            pPath = NULL;
        }
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    *ppPath = pPath;                    /* always set it */
    return rc;
}


RTDECL(void) RTVfsParsePathFree(PRTVFSPARSEDPATH pPath)
{
    if (pPath)
    {
        pPath->cch               = UINT16_MAX;
        pPath->cComponents       = UINT16_MAX;
        pPath->aoffComponents[0] = UINT16_MAX;
        pPath->aoffComponents[1] = UINT16_MAX;
        RTMemTmpFree(pPath);
    }
}


/**
 * Handles a symbolic link, adding it to
 *
 * @returns IPRT status code.
 * @param   pPath               The parsed path to update.
 * @param   piComponent         The component iterator to update.
 * @param   hSymlink            The symbolic link to process.
 */
static int rtVfsTraverseHandleSymlink(PRTVFSPARSEDPATH pPath, uint16_t *piComponent, RTVFSSYMLINK hSymlink)
{
    /*
     * Read the link.
     */
    char szPath[RTPATH_MAX];
    int rc = RTVfsSymlinkRead(hSymlink, szPath, sizeof(szPath) - 1);
    if (RT_SUCCESS(rc))
    {
        szPath[sizeof(szPath) - 1] = '\0';
        if (szPath[0] == '/')
        {
            /*
             * Absolute symlink.
             */
            rc = RTVfsParsePath(pPath, szPath, NULL);
            if (RT_SUCCESS(rc))
            {
                *piComponent = 0;
                return VINF_SUCCESS;
            }
        }
        else
        {
            /*
             * Relative symlink, must replace the current component with the
             * link value.  We do that by using the remainder of the symlink
             * buffer as temporary storage.
             */
            uint16_t iComponent = *piComponent;
            if (iComponent + 1 < pPath->cComponents)
                rc = RTPathAppend(szPath, sizeof(szPath), &pPath->szPath[pPath->aoffComponents[iComponent + 1]]);
            if (RT_SUCCESS(rc))
            {
                pPath->cch = pPath->aoffComponents[iComponent] - (iComponent > 0);
                pPath->aoffComponents[iComponent + 1] = pPath->cch + 1;
                pPath->szPath[pPath->cch]     = '\0';
                pPath->szPath[pPath->cch + 1] = '\0';

                rc = RTVfsParsePathAppend(pPath, szPath, &iComponent);
                if (RT_SUCCESS(rc))
                {
                    *piComponent = iComponent;
                    return VINF_SUCCESS;
                }
            }
        }
    }
    return rc == VERR_BUFFER_OVERFLOW ? VERR_FILENAME_TOO_LONG : rc;
}


/**
 * Internal worker for various open functions as well as RTVfsTraverseToParent.
 *
 * @returns IPRT status code.
 * @param   pThis           The VFS.
 * @param   pPath           The parsed path.  This may be changed as symbolic
 *                          links are processed during the path traversal.
 * @param   fFollowSymlink  Whether to follow the final component if it is a
 *                          symbolic link.
 * @param   ppVfsParentDir  Where to return the parent directory handle
 *                          (referenced).
 */
static int rtVfsTraverseToParent(RTVFSINTERNAL *pThis, PRTVFSPARSEDPATH pPath, bool fFollowSymlink,
                                 RTVFSDIRINTERNAL **ppVfsParentDir)
{
    /*
     * Assert sanity.
     */
    AssertPtr(pThis);
    Assert(pThis->uMagic == RTVFS_MAGIC);
    Assert(pThis->Base.cRefs > 0);
    AssertPtr(pPath);
    AssertPtr(ppVfsParentDir);
    *ppVfsParentDir = NULL;
    AssertReturn(pPath->cComponents > 0, VERR_INTERNAL_ERROR_3);

    /*
     * Open the root directory.
     */
    /** @todo Union mounts, traversal optimization methods, races, ++ */
    RTVFSDIRINTERNAL *pCurDir;
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnOpenRoot(pThis->Base.pvThis, &pCurDir);
    RTVfsLockReleaseRead(pThis->Base.hLock);
    if (RT_FAILURE(rc))
        return rc;
    Assert(pCurDir->uMagic == RTVFSDIR_MAGIC);

    /*
     * The traversal loop.
     */
    unsigned cLinks     = 0;
    uint16_t iComponent = 0;
    for (;;)
    {
        /*
         * Are we done yet?
         */
        bool fFinal = iComponent + 1 >= pPath->cComponents;
        if (fFinal && !fFollowSymlink)
        {
            *ppVfsParentDir = pCurDir;
            return VINF_SUCCESS;
        }

        /*
         * Try open the next entry.
         */
        const char     *pszEntry    = &pPath->szPath[pPath->aoffComponents[iComponent]];
        char           *pszEntryEnd = &pPath->szPath[pPath->aoffComponents[iComponent + 1] - 1];
        *pszEntryEnd = '\0';
        RTVFSDIR        hDir     = NIL_RTVFSDIR;
        RTVFSSYMLINK    hSymlink = NIL_RTVFSSYMLINK;
        RTVFS           hVfsMnt  = NIL_RTVFS;
        if (fFinal)
        {
            RTVfsLockAcquireRead(pCurDir->Base.hLock);
            rc = pCurDir->pOps->pfnTraversalOpen(pCurDir->Base.pvThis, pszEntry, NULL, &hSymlink, NULL);
            RTVfsLockReleaseRead(pCurDir->Base.hLock);
            *pszEntryEnd = '\0';
            if (rc == VERR_PATH_NOT_FOUND)
                rc = VINF_SUCCESS;
            if (RT_FAILURE(rc))
                break;

            if (hSymlink == NIL_RTVFSSYMLINK)
            {
                *ppVfsParentDir = pCurDir;
                return VINF_SUCCESS;
            }
        }
        else
        {
            RTVfsLockAcquireRead(pCurDir->Base.hLock);
            rc = pCurDir->pOps->pfnTraversalOpen(pCurDir->Base.pvThis, pszEntry, &hDir, &hSymlink, &hVfsMnt);
            RTVfsLockReleaseRead(pCurDir->Base.hLock);
            *pszEntryEnd = '/';
            if (RT_FAILURE(rc))
                break;

            if (   hDir     == NIL_RTVFSDIR
                && hSymlink == NIL_RTVFSSYMLINK
                && hVfsMnt  == NIL_RTVFS)
            {
                rc = VERR_NOT_A_DIRECTORY;
                break;
            }
        }
        Assert(   (hDir != NIL_RTVFSDIR && hSymlink == NIL_RTVFSSYMLINK && hVfsMnt == NIL_RTVFS)
               || (hDir == NIL_RTVFSDIR && hSymlink != NIL_RTVFSSYMLINK && hVfsMnt == NIL_RTVFS)
               || (hDir == NIL_RTVFSDIR && hSymlink == NIL_RTVFSSYMLINK && hVfsMnt != NIL_RTVFS));

        if (hDir != NIL_RTVFSDIR)
        {
            /*
             * Directory - advance down the path.
             */
            AssertPtr(hDir);
            Assert(hDir->uMagic == RTVFSDIR_MAGIC);
            RTVfsDirRelease(pCurDir);
            pCurDir = hDir;
            iComponent++;
        }
        else if (hSymlink != NIL_RTVFSSYMLINK)
        {
            /*
             * Symbolic link - deal with it and retry the current component.
             */
            AssertPtr(hSymlink);
            Assert(hSymlink->uMagic == RTVFSSYMLINK_MAGIC);
            cLinks++;
            if (cLinks >= RTVFS_MAX_LINKS)
            {
                rc = VERR_TOO_MANY_SYMLINKS;
                break;
            }
            uint16_t iRestartComp = iComponent;
            rc = rtVfsTraverseHandleSymlink(pPath, &iRestartComp, hSymlink);
            if (RT_FAILURE(rc))
                break;
            if (iRestartComp != iComponent)
            {
                /* Must restart from the root (optimize this). */
                RTVfsDirRelease(pCurDir);
                RTVfsLockAcquireRead(pThis->Base.hLock);
                rc = pThis->pOps->pfnOpenRoot(pThis->Base.pvThis, &pCurDir);
                RTVfsLockReleaseRead(pThis->Base.hLock);
                if (RT_FAILURE(rc))
                {
                    pCurDir = NULL;
                    break;
                }
                iComponent = 0;
            }
        }
        else
        {
            /*
             * Mount point - deal with it and retry the current component.
             */
            RTVfsDirRelease(pCurDir);
            RTVfsLockAcquireRead(hVfsMnt->Base.hLock);
            rc = pThis->pOps->pfnOpenRoot(hVfsMnt->Base.pvThis, &pCurDir);
            RTVfsLockReleaseRead(hVfsMnt->Base.hLock);
            if (RT_FAILURE(rc))
            {
                pCurDir = NULL;
                break;
            }
            iComponent = 0;
            /** @todo union mounts. */
        }
    }

    if (pCurDir)
        RTVfsDirRelease(pCurDir);

    return rc;
}


RTDECL(int) RTVfsUtilDummyPollOne(uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr, uint32_t *pfRetEvents)
{
    NOREF(fEvents);
    int rc;
    if (fIntr)
        rc = RTThreadSleep(cMillies);
    else
    {
        uint64_t uMsStart = RTTimeMilliTS();
        do
            rc = RTThreadSleep(cMillies);
        while (   rc == VERR_INTERRUPTED
               && !fIntr
               && RTTimeMilliTS() - uMsStart < cMillies);
        if (rc == VERR_INTERRUPTED)
            rc = VERR_TIMEOUT;
    }

    *pfRetEvents = 0;
    return rc;
}


RTDECL(int) RTVfsUtilPumpIoStreams(RTVFSIOSTREAM hVfsIosSrc, RTVFSIOSTREAM hVfsIosDst, size_t cbBufHint)
{
    /*
     * Allocate a temporary buffer.
     */
    size_t cbBuf = cbBufHint;
    if (!cbBuf)
        cbBuf = _64K;
    else if (cbBuf < _4K)
        cbBuf = _4K;
    else if (cbBuf > _1M)
        cbBuf = _1M;

    void *pvBuf = RTMemTmpAlloc(cbBuf);
    if (!pvBuf)
    {
        cbBuf = _4K;
        pvBuf = RTMemTmpAlloc(cbBuf);
        if (!pvBuf)
            return VERR_NO_TMP_MEMORY;
    }

    /*
     * Pump loop.
     */
    int rc;
    for (;;)
    {
        size_t cbRead;
        rc = RTVfsIoStrmRead(hVfsIosSrc, pvBuf, cbBuf, true /*fBlocking*/, &cbRead);
        if (RT_FAILURE(rc))
            break;
        if (rc == VINF_EOF && cbRead == 0)
            break;

        rc = RTVfsIoStrmWrite(hVfsIosDst, pvBuf, cbRead, true /*fBlocking*/, NULL /*cbWritten*/);
        if (RT_FAILURE(rc))
            break;
    }

    RTMemTmpFree(pvBuf);

    /*
     * Flush the destination stream on success to make sure we've caught
     * errors caused by buffering delays.
     */
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmFlush(hVfsIosDst);

    return rc;
}





/*
 * F I L E S Y S T E M   R O O T
 * F I L E S Y S T E M   R O O T
 * F I L E S Y S T E M   R O O T
 */


RTDECL(int) RTVfsNew(PCRTVFSOPS pVfsOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock,
                     PRTVFS phVfs, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pVfsOps);
    AssertReturn(pVfsOps->uVersion   == RTVFSOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pVfsOps->uEndMarker == RTVFSOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfs);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSINTERNAL *pThis = (RTVFSINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, NULL, hVfs, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFS_MAGIC;
    pThis->pOps   = pVfsOps;

    *phVfs       = pThis;
    *ppvInstance = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(uint32_t)    RTVfsRetain(RTVFS hVfs)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}


RTDECL(uint32_t)    RTVfsRelease(RTVFS hVfs)
{
    RTVFSINTERNAL *pThis = hVfs;
    if (pThis == NIL_RTVFS)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}


RTDECL(int)         RTVfsIsRangeInUse(RTVFS hVfs, uint64_t off, size_t cb,
                                      bool *pfUsed)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnIsRangeInUse(pThis->Base.pvThis, off, cb, pfUsed);
    RTVfsLockReleaseWrite(pThis->Base.hLock);

    return rc;
}


/*
 *
 *  F I L E S Y S T E M   S T R E A M
 *  F I L E S Y S T E M   S T R E A M
 *  F I L E S Y S T E M   S T R E A M
 *
 */


RTDECL(int) RTVfsNewFsStream(PCRTVFSFSSTREAMOPS pFsStreamOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock,
                             PRTVFSFSSTREAM phVfsFss, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pFsStreamOps);
    AssertReturn(pFsStreamOps->uVersion   == RTVFSFSSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pFsStreamOps->uEndMarker == RTVFSFSSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!pFsStreamOps->fReserved);
    RTVFSOBJ_ASSERT_OPS(&pFsStreamOps->Obj, RTVFSOBJTYPE_FS_STREAM);
    AssertPtr(pFsStreamOps->pfnNext);
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsFss);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSFSSTREAMINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSFSSTREAMINTERNAL *pThis = (RTVFSFSSTREAMINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pFsStreamOps->Obj, hVfs, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));

    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFSFSSTREAM_MAGIC;
    pThis->fFlags = RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE;
    pThis->pOps   = pFsStreamOps;

    *phVfsFss     = pThis;
    *ppvInstance  = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(uint32_t)    RTVfsFsStrmRetain(RTVFSFSSTREAM hVfsFss)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}


RTDECL(uint32_t)    RTVfsFsStrmRelease(RTVFSFSSTREAM hVfsFss)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    if (pThis == NIL_RTVFSFSSTREAM)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}


RTDECL(int)         RTVfsFsStrmQueryInfo(RTVFSFSSTREAM hVfsFss, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Base, pObjInfo, enmAddAttr);
}


RTDECL(int)         RTVfsFsStrmNext(RTVFSFSSTREAM hVfsFss, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(ppszName, VERR_INVALID_POINTER);
    if (ppszName)
        *ppszName = NULL;
    AssertPtrNullReturn(penmType, VERR_INVALID_POINTER);
    if (penmType)
        *penmType = RTVFSOBJTYPE_INVALID;
    AssertPtrNullReturn(penmType, VERR_INVALID_POINTER);
    if (phVfsObj)
        *phVfsObj = NIL_RTVFSOBJ;

    return pThis->pOps->pfnNext(pThis->Base.pvThis, ppszName, penmType, phVfsObj);
}




/*
 *
 *  D I R   D I R   D I R
 *  D I R   D I R   D I R
 *  D I R   D I R   D I R
 *
 */

RTDECL(uint32_t)    RTVfsDirRetain(RTVFSDIR hVfsDir)
{
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}


RTDECL(uint32_t)    RTVfsDirRelease(RTVFSDIR hVfsDir)
{
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    if (pThis == NIL_RTVFSDIR)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}



/*
 *
 *  S Y M B O L I C   L I N K
 *  S Y M B O L I C   L I N K
 *  S Y M B O L I C   L I N K
 *
 */

RTDECL(int) RTVfsNewSymlink(PCRTVFSSYMLINKOPS pSymlinkOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock,
                            PRTVFSSYMLINK phVfsSym, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pSymlinkOps);
    AssertReturn(pSymlinkOps->uVersion   == RTVFSSYMLINKOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pSymlinkOps->uEndMarker == RTVFSSYMLINKOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!pSymlinkOps->fReserved);
    RTVFSSYMLINK_ASSERT_OPS(pSymlinkOps, RTVFSOBJTYPE_SYMLINK);
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsSym);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSSYMLINKINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSSYMLINKINTERNAL *pThis = (RTVFSSYMLINKINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pSymlinkOps->Obj, hVfs, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFSSYMLINK_MAGIC;
    pThis->pOps   = pSymlinkOps;

    *phVfsSym     = pThis;
    *ppvInstance  = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(uint32_t)    RTVfsSymlinkRetain(RTVFSSYMLINK hVfsSym)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}


RTDECL(uint32_t)    RTVfsSymlinkRelease(RTVFSSYMLINK hVfsSym)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    if (pThis == NIL_RTVFSSYMLINK)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}


RTDECL(int)         RTVfsSymlinkQueryInfo(RTVFSSYMLINK hVfsSym, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Base, pObjInfo, enmAddAttr);
}


RTDECL(int)  RTVfsSymlinkSetMode(RTVFSSYMLINK hVfsSym, RTFMODE fMode, RTFMODE fMask)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    fMode = rtFsModeNormalize(fMode, NULL, 0);
    if (!rtFsModeIsValid(fMode))
        return VERR_INVALID_PARAMETER;

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->ObjSet.pfnSetMode(pThis->Base.pvThis, fMode, fMask);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsSymlinkSetTimes(RTVFSSYMLINK hVfsSym, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    AssertPtrNullReturn(pAccessTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pChangeTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pBirthTime, VERR_INVALID_POINTER);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->ObjSet.pfnSetTimes(pThis->Base.pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsSymlinkSetOwner(RTVFSSYMLINK hVfsSym, RTUID uid, RTGID gid)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->ObjSet.pfnSetOwner(pThis->Base.pvThis, uid, gid);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsSymlinkRead(RTVFSSYMLINK hVfsSym, char *pszTarget, size_t cbTarget)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnRead(pThis->Base.pvThis, pszTarget, cbTarget);
    RTVfsLockReleaseWrite(pThis->Base.hLock);

    return rc;
}



/*
 *
 *  I / O   S T R E A M     I / O   S T R E A M     I / O   S T R E A M
 *  I / O   S T R E A M     I / O   S T R E A M     I / O   S T R E A M
 *  I / O   S T R E A M     I / O   S T R E A M     I / O   S T R E A M
 *
 */

RTDECL(int) RTVfsNewIoStream(PCRTVFSIOSTREAMOPS pIoStreamOps, size_t cbInstance, uint32_t fOpen, RTVFS hVfs, RTVFSLOCK hLock,
                             PRTVFSIOSTREAM phVfsIos, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pIoStreamOps);
    AssertReturn(pIoStreamOps->uVersion   == RTVFSIOSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pIoStreamOps->uEndMarker == RTVFSIOSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!(pIoStreamOps->fFeatures & ~RTVFSIOSTREAMOPS_FEAT_VALID_MASK));
    RTVFSIOSTREAM_ASSERT_OPS(pIoStreamOps, RTVFSOBJTYPE_IO_STREAM);
    Assert(cbInstance > 0);
    Assert(fOpen & RTFILE_O_ACCESS_MASK);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsIos);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSIOSTREAMINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSIOSTREAMINTERNAL *pThis = (RTVFSIOSTREAMINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pIoStreamOps->Obj, hVfs, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFSIOSTREAM_MAGIC;
    pThis->fFlags = fOpen;
    pThis->pOps   = pIoStreamOps;

    *phVfsIos     = pThis;
    *ppvInstance  = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(void *) RTVfsIoStreamToPrivate(RTVFSIOSTREAM hVfsIos, PCRTVFSIOSTREAMOPS pIoStreamOps)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, NULL);
    if (pThis->pOps != pIoStreamOps)
        return NULL;
    return pThis->Base.pvThis;
}


RTDECL(uint32_t)    RTVfsIoStrmRetain(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}


RTDECL(uint32_t)    RTVfsIoStrmRelease(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    if (pThis == NIL_RTVFSIOSTREAM)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}


RTDECL(RTVFSFILE)   RTVfsIoStrmToFile(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, NIL_RTVFSFILE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, NIL_RTVFSFILE);

    if (pThis->pOps->Obj.enmType == RTVFSOBJTYPE_FILE)
    {
        rtVfsObjRetainVoid(&pThis->Base);
        return RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream);
    }

    /* this is no crime, so don't assert. */
    return NIL_RTVFSFILE;
}


RTDECL(int) RTVfsIoStrmQueryInfo(RTVFSIOSTREAM hVfsIos, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Base, pObjInfo, enmAddAttr);
}


RTDECL(int) RTVfsIoStrmRead(RTVFSIOSTREAM hVfsIos, void *pvBuf, size_t cbToRead, bool fBlocking, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbRead, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_READ, VERR_ACCESS_DENIED);

    RTSGSEG Seg = { pvBuf, cbToRead };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnRead(pThis->Base.pvThis, -1 /*off*/, &SgBuf, fBlocking, pcbRead);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmReadAt(RTVFSIOSTREAM hVfsIos, RTFOFF off, void *pvBuf, size_t cbToRead,
                              bool fBlocking, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbRead, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_READ, VERR_ACCESS_DENIED);

    RTSGSEG Seg = { pvBuf, cbToRead };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnRead(pThis->Base.pvThis, off, &SgBuf, fBlocking, pcbRead);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmWrite(RTVFSIOSTREAM hVfsIos, const void *pvBuf, size_t cbToWrite, bool fBlocking, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbWritten, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    RTSGSEG Seg = { (void *)pvBuf, cbToWrite };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, -1 /*off*/, &SgBuf, fBlocking, pcbWritten);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmWriteAt(RTVFSIOSTREAM hVfsIos, RTFOFF off, const void *pvBuf, size_t cbToWrite,
                               bool fBlocking, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbWritten, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    RTSGSEG Seg = { (void *)pvBuf, cbToWrite };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, off, &SgBuf, fBlocking, pcbWritten);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmSgRead(RTVFSIOSTREAM hVfsIos, PCRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pSgBuf);
    AssertReturn(fBlocking || pcbRead, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_READ, VERR_ACCESS_DENIED);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc;
    if (!(pThis->pOps->fFeatures & RTVFSIOSTREAMOPS_FEAT_NO_SG))
        rc = pThis->pOps->pfnRead(pThis->Base.pvThis, -1 /*off*/, pSgBuf, fBlocking, pcbRead);
    else
    {
        size_t cbRead = 0;
        rc = VINF_SUCCESS;

        for (uint32_t iSeg = 0; iSeg < pSgBuf->cSegs; iSeg++)
        {
            RTSGBUF SgBuf;
            RTSgBufInit(&SgBuf, &pSgBuf->paSegs[iSeg], 1);

            size_t cbReadSeg = pcbRead ? 0 : pSgBuf->paSegs[iSeg].cbSeg;
            rc = pThis->pOps->pfnRead(pThis->Base.pvThis, -1 /*off*/, &SgBuf, fBlocking, pcbRead ? &cbReadSeg : NULL);
            if (RT_FAILURE(rc))
                break;
            cbRead += cbReadSeg;
            if ((pcbRead && cbReadSeg != SgBuf.paSegs[0].cbSeg) || rc != VINF_SUCCESS)
                break;
        }

        if (pcbRead)
            *pcbRead = cbRead;
    }
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmSgWrite(RTVFSIOSTREAM hVfsIos, PCRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pSgBuf);
    AssertReturn(fBlocking || pcbWritten, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc;
    if (!(pThis->pOps->fFeatures & RTVFSIOSTREAMOPS_FEAT_NO_SG))
        rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, -1 /*off*/, pSgBuf, fBlocking, pcbWritten);
    else
    {
        size_t cbWritten = 0;
        rc = VINF_SUCCESS;

        for (uint32_t iSeg = 0; iSeg < pSgBuf->cSegs; iSeg++)
        {
            RTSGBUF SgBuf;
            RTSgBufInit(&SgBuf, &pSgBuf->paSegs[iSeg], 1);

            size_t cbWrittenSeg = 0;
            rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, -1 /*off*/, &SgBuf, fBlocking, pcbWritten ? &cbWrittenSeg : NULL);
            if (RT_FAILURE(rc))
                break;
            if (pcbWritten)
            {
                cbWritten += cbWrittenSeg;
                if (cbWrittenSeg != SgBuf.paSegs[0].cbSeg)
                    break;
            }
        }

        if (pcbWritten)
            *pcbWritten = cbWritten;
    }
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmFlush(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnFlush(pThis->Base.pvThis);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmPoll(RTVFSIOSTREAM hVfsIos, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                            uint32_t *pfRetEvents)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnPollOne(pThis->Base.pvThis, fEvents, cMillies, fIntr, pfRetEvents);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(RTFOFF) RTVfsIoStrmTell(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, -1);

    RTFOFF off;
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnTell(pThis->Base.pvThis, &off);
    RTVfsLockReleaseRead(pThis->Base.hLock);
    if (RT_FAILURE(rc))
        off = rc;
    return off;
}


RTDECL(int) RTVfsIoStrmSkip(RTVFSIOSTREAM hVfsIos, RTFOFF cb)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, -1);
    AssertReturn(cb >= 0, VERR_INVALID_PARAMETER);

    int rc;
    if (pThis->pOps->pfnSkip)
    {
        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThis->pOps->pfnSkip(pThis->Base.pvThis, cb);
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    else if (pThis->pOps->Obj.enmType == RTVFSOBJTYPE_FILE)
    {
        RTVFSFILEINTERNAL *pThisFile = RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream);
        RTFOFF             offIgnored;

        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThisFile->pOps->pfnSeek(pThis->Base.pvThis, cb, RTFILE_SEEK_CURRENT, &offIgnored);
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    else
    {
        void *pvBuf = RTMemTmpAlloc(_64K);
        if (pvBuf)
        {
            rc = VINF_SUCCESS;
            while (cb > 0)
            {
                size_t cbToRead = (size_t)RT_MIN(cb, _64K);
                RTVfsLockAcquireWrite(pThis->Base.hLock);
                rc = RTVfsIoStrmRead(hVfsIos, pvBuf, cbToRead, true /*fBlocking*/, NULL);
                RTVfsLockReleaseWrite(pThis->Base.hLock);
                if (RT_FAILURE(rc))
                    break;
                cb -= cbToRead;
            }

            RTMemTmpFree(pvBuf);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    return rc;
}


RTDECL(int) RTVfsIoStrmZeroFill(RTVFSIOSTREAM hVfsIos, RTFOFF cb)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, -1);

    int rc;
    if (pThis->pOps->pfnSkip)
    {
        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThis->pOps->pfnZeroFill(pThis->Base.pvThis, cb);
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    else
    {
        void *pvBuf = RTMemTmpAllocZ(_64K);
        if (pvBuf)
        {
            rc = VINF_SUCCESS;
            while (cb > 0)
            {
                size_t cbToWrite = (size_t)RT_MIN(cb, _64K);
                RTVfsLockAcquireWrite(pThis->Base.hLock);
                rc = RTVfsIoStrmWrite(hVfsIos, pvBuf, cbToWrite, true /*fBlocking*/, NULL);
                RTVfsLockReleaseWrite(pThis->Base.hLock);
                if (RT_FAILURE(rc))
                    break;
                cb -= cbToWrite;
            }

            RTMemTmpFree(pvBuf);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    return rc;
}


RTDECL(bool) RTVfsIoStrmIsAtEnd(RTVFSIOSTREAM hVfsIos)
{
    /*
     * There is where the zero read behavior comes in handy.
     */
    char    bDummy;
    size_t  cbRead;
    int rc = RTVfsIoStrmRead(hVfsIos, &bDummy, 0 /*cbToRead*/, false /*fBlocking*/, &cbRead);
    return rc == VINF_EOF;
}






/*
 *
 *  F I L E   F I L E   F I L E
 *  F I L E   F I L E   F I L E
 *  F I L E   F I L E   F I L E
 *
 */

RTDECL(int) RTVfsNewFile(PCRTVFSFILEOPS pFileOps, size_t cbInstance, uint32_t fOpen, RTVFS hVfs, RTVFSLOCK hLock,
                         PRTVFSFILE phVfsFile, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pFileOps);
    AssertReturn(pFileOps->uVersion   == RTVFSFILEOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pFileOps->uEndMarker == RTVFSFILEOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!pFileOps->fReserved);
    RTVFSIOSTREAM_ASSERT_OPS(&pFileOps->Stream, RTVFSOBJTYPE_FILE);
    Assert(cbInstance > 0);
    Assert(fOpen & RTFILE_O_ACCESS_MASK);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsFile);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSFILEINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSFILEINTERNAL *pThis = (RTVFSFILEINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Stream.Base, &pFileOps->Stream.Obj, hVfs, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic        = RTVFSFILE_MAGIC;
    pThis->fReserved     = 0;
    pThis->pOps          = pFileOps;
    pThis->Stream.uMagic = RTVFSIOSTREAM_MAGIC;
    pThis->Stream.fFlags = fOpen;
    pThis->Stream.pOps   = &pFileOps->Stream;

    *phVfsFile   = pThis;
    *ppvInstance = pThis->Stream.Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(int)         RTVfsFileOpen(RTVFS hVfs, const char *pszFilename, uint64_t fOpen, PRTVFSFILE phVfsFile)
{
    /*
     * Validate input.
     */
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(phVfsFile, VERR_INVALID_POINTER);

    int rc = rtFileRecalcAndValidateFlags(&fOpen);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Parse the path, assume current directory is root since we've got no
     * caller context here.
     */
    PRTVFSPARSEDPATH pPath;
    rc = RTVfsParsePathA(pszFilename, "/", &pPath);
    if (RT_SUCCESS(rc))
    {
        if (!pPath->fDirSlash)
        {
            /*
             * Tranverse the path, resolving the parent node and any symlinks
             * in the final element, and ask the directory to open the file.
             */
            RTVFSDIRINTERNAL *pVfsParentDir;
            rc = rtVfsTraverseToParent(pThis, pPath, true /*fFollowSymlink*/, &pVfsParentDir);
            if (RT_SUCCESS(rc))
            {
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];

                /** @todo there is a symlink creation race here. */
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpenFile(pVfsParentDir->Base.pvThis, pszEntryName, fOpen, phVfsFile);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);

                RTVfsDirRelease(pVfsParentDir);

                if (RT_SUCCESS(rc))
                {
                    AssertPtr(*phVfsFile);
                    Assert((*phVfsFile)->uMagic == RTVFSFILE_MAGIC);
                }
            }
        }
        else
            rc = VERR_INVALID_PARAMETER;
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(uint32_t)    RTVfsFileRetain(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Stream.Base);
}


RTDECL(uint32_t)    RTVfsFileRelease(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    if (pThis == NIL_RTVFSFILE)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Stream.Base);
}


RTDECL(RTVFSIOSTREAM) RTVfsFileToIoStream(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, NIL_RTVFSIOSTREAM);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, NIL_RTVFSIOSTREAM);

    rtVfsObjRetainVoid(&pThis->Stream.Base);
    return &pThis->Stream;
}


RTDECL(int)         RTVfsFileQueryInfo(RTVFSFILE hVfsFile, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Stream.Base, pObjInfo, enmAddAttr);
}


RTDECL(int)         RTVfsFileRead(RTVFSFILE hVfsFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmRead(&pThis->Stream, pvBuf, cbToRead, true /*fBlocking*/, pcbRead);
}


RTDECL(int)         RTVfsFileWrite(RTVFSFILE hVfsFile, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmWrite(&pThis->Stream, pvBuf, cbToWrite, true /*fBlocking*/, pcbWritten);
}


RTDECL(int)         RTVfsFileWriteAt(RTVFSFILE hVfsFile, RTFOFF off, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    int rc = RTVfsFileSeek(hVfsFile, off, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmWriteAt(&pThis->Stream, off, pvBuf, cbToWrite, true /*fBlocking*/, pcbWritten);

    return rc;
}


RTDECL(int)         RTVfsFileReadAt(RTVFSFILE hVfsFile, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    int rc = RTVfsFileSeek(hVfsFile, off, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmReadAt(&pThis->Stream, off, pvBuf, cbToRead, true /*fBlocking*/, pcbRead);

    return rc;
}


RTDECL(int) RTVfsFileFlush(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmFlush(&pThis->Stream);
}


RTDECL(RTFOFF) RTVfsFilePoll(RTVFSFILE hVfsFile, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                  uint32_t *pfRetEvents)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmPoll(&pThis->Stream, fEvents, cMillies, fIntr, pfRetEvents);
}


RTDECL(RTFOFF) RTVfsFileTell(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmTell(&pThis->Stream);
}


RTDECL(int) RTVfsFileSeek(RTVFSFILE hVfsFile, RTFOFF offSeek, uint32_t uMethod, uint64_t *poffActual)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    AssertReturn(   uMethod == RTFILE_SEEK_BEGIN
                 || uMethod == RTFILE_SEEK_CURRENT
                 || uMethod == RTFILE_SEEK_END, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(poffActual, VERR_INVALID_POINTER);

    RTFOFF offActual = 0;
    RTVfsLockAcquireWrite(pThis->Stream.Base.hLock);
    int rc = pThis->pOps->pfnSeek(pThis->Stream.Base.pvThis, offSeek, uMethod, &offActual);
    RTVfsLockReleaseWrite(pThis->Stream.Base.hLock);
    if (RT_SUCCESS(rc) && poffActual)
    {
        Assert(offActual >= 0);
        *poffActual = offActual;
    }

    return rc;
}


RTDECL(int) RTVfsFileGetSize(RTVFSFILE hVfsFile, uint64_t *pcbSize)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    RTVfsLockAcquireWrite(pThis->Stream.Base.hLock);
    int rc = pThis->pOps->pfnQuerySize(pThis->Stream.Base.pvThis, pcbSize);
    RTVfsLockReleaseWrite(pThis->Stream.Base.hLock);

    return rc;
}
