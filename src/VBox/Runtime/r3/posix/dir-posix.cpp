/* $Id$ */
/** @file
 * IPRT - Directory manipulation, POSIX.
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
#define LOG_GROUP RTLOGGROUP_DIR
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>

#include <iprt/dir.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include "internal/dir.h"
#include "internal/fs.h"
#include "internal/path.h"

#if !defined(RT_OS_SOLARIS) && !defined(RT_OS_HAIKU)
# define HAVE_DIRENT_D_TYPE 1
#endif


RTDECL(bool) RTDirExists(const char *pszPath)
{
    bool fRc = false;
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat s;
        fRc = !stat(pszNativePath, &s)
            && S_ISDIR(s.st_mode);

        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTDirExists(%p={%s}): returns %RTbool\n", pszPath, pszPath, fRc));
    return fRc;
}


RTDECL(int) RTDirCreate(const char *pszPath, RTFMODE fMode, uint32_t fCreate)
{
    int rc;
    fMode = rtFsModeNormalize(fMode, pszPath, 0);
    if (rtFsModeIsValidPermissions(fMode))
    {
        char const *pszNativePath;
        rc = rtPathToNative(&pszNativePath, pszPath, NULL);
        if (RT_SUCCESS(rc))
        {
            if (mkdir(pszNativePath, fMode & RTFS_UNIX_MASK))
            {
                rc = errno;
                bool fVerifyIsDir = true;
#ifdef RT_OS_SOLARIS
                /*
                 * mkdir on nfs mount points has been/is busted in various
                 * during the Nevada development cycle. We've observed:
                 *  - Build 111b (2009.06) returns EACCES.
                 *  - Build ca. 70-80 returns ENOSYS.
                 */
                if (    rc == ENOSYS
                    ||  rc == EACCES)
                {
                    rc = RTErrConvertFromErrno(rc);
                    fVerifyIsDir = false;  /* We'll check if it's a dir ourselves since we're going to stat() anyway. */
                    struct stat st;
                    if (!stat(pszNativePath, &st))
                    {
                        rc = VERR_ALREADY_EXISTS;
                        if (!S_ISDIR(st.st_mode))
                            rc = VERR_IS_A_FILE;
                    }
                }
                else
                    rc = RTErrConvertFromErrno(rc);
#else
                rc = RTErrConvertFromErrno(rc);
#endif
                if (   rc == VERR_ALREADY_EXISTS
                    && fVerifyIsDir == true)
                {
                    /*
                     * Verify that it really exists as a directory.
                     */
                    struct stat st;
                    if (!stat(pszNativePath, &st) && !S_ISDIR(st.st_mode))
                        rc = VERR_IS_A_FILE;
                }
            }
        }

        rtPathFreeNative(pszNativePath, pszPath);
    }
    else
    {
        AssertMsgFailed(("Invalid file mode! %RTfmode\n", fMode));
        rc = VERR_INVALID_FMODE;
    }
    LogFlow(("RTDirCreate(%p={%s}, %RTfmode): returns %Rrc\n", pszPath, pszPath, fMode, rc));
    return rc;
}


RTDECL(int) RTDirRemove(const char *pszPath)
{
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        if (rmdir(pszNativePath))
            rc = RTErrConvertFromErrno(errno);

        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTDirRemove(%p={%s}): returns %Rrc\n", pszPath, pszPath, rc));
    return rc;
}


RTDECL(int) RTDirFlush(const char *pszPath)
{
    /*
     * Linux: The fsync() man page hints at this being required for ensuring
     * consistency between directory and file in case of a crash.
     *
     * Solaris: No mentioned is made of directories on the fsync man page.
     * While rename+fsync will do what we want on ZFS, the code needs more
     * careful studying wrt whether the directory entry of a new file is
     * implicitly synced when the file is synced (it's very likely for ZFS).
     *
     * FreeBSD: The FFS fsync code seems to flush the directory entry as well
     * in some cases.  Don't know exactly what's up with rename, but from the
     * look of things fsync(dir) should work.
     */
    int rc;
#ifdef O_DIRECTORY
    int fd = open(pszPath, O_RDONLY | O_DIRECTORY, 0);
#else
    int fd = open(pszPath, O_RDONLY, 0);
#endif
    if (fd >= 0)
    {
        if (fsync(fd) == 0)
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromErrno(errno);
        close(fd);
    }
    else
        rc = RTErrConvertFromErrno(errno);
    return rc;
}


size_t rtDirNativeGetStructSize(const char *pszPath)
{
    long cbNameMax = pathconf(pszPath, _PC_NAME_MAX);
# ifdef NAME_MAX
    if (cbNameMax < NAME_MAX)           /* This is plain paranoia, but it doesn't hurt. */
        cbNameMax = NAME_MAX;
# endif
# ifdef _XOPEN_NAME_MAX
    if (cbNameMax < _XOPEN_NAME_MAX)    /* Ditto. */
        cbNameMax = _XOPEN_NAME_MAX;
# endif
    size_t cbDir = RT_OFFSETOF(RTDIR, Data.d_name[cbNameMax + 1]);
    if (cbDir < sizeof(RTDIR))          /* Ditto. */
        cbDir = sizeof(RTDIR);
    cbDir = RT_ALIGN_Z(cbDir, 8);

    return cbDir;
}


int rtDirNativeOpen(PRTDIR pDir, char *pszPathBuf)
{
    NOREF(pszPathBuf); /* only used on windows */

    /*
     * Convert to a native path and try opendir.
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pDir->pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        pDir->pDir = opendir(pszNativePath);
        if (pDir->pDir)
        {
            /*
             * Init data (allocated as all zeros).
             */
            pDir->fDataUnread = false; /* spelling it out */
        }
        else
            rc = RTErrConvertFromErrno(errno);

        rtPathFreeNative(pszNativePath, pDir->pszPath);
    }

    return rc;
}


RTDECL(int) RTDirClose(PRTDIR pDir)
{
    /*
     * Validate input.
     */
    if (!pDir)
        return VERR_INVALID_PARAMETER;
    if (pDir->u32Magic != RTDIR_MAGIC)
    {
        AssertMsgFailed(("Invalid pDir=%p\n", pDir));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Close the handle.
     */
    int rc = VINF_SUCCESS;
    pDir->u32Magic = RTDIR_MAGIC_DEAD;
    if (closedir(pDir->pDir))
    {
        rc = RTErrConvertFromErrno(errno);
        AssertMsgFailed(("closedir(%p) -> errno=%d (%Rrc)\n", pDir->pDir, errno, rc));
    }

    RTMemFree(pDir);
    return rc;
}


/**
 * Ensure that there is unread data in the buffer
 * and that there is a converted filename hanging around.
 *
 * @returns IPRT status code.
 * @param   pDir        the open directory. Fully validated.
 */
static int rtDirReadMore(PRTDIR pDir)
{
    /** @todo try avoid the rematching on buffer overflow errors. */
    for (;;)
    {
        /*
         * Fetch data?
         */
        if (!pDir->fDataUnread)
        {
            struct dirent *pResult = NULL;
            int rc = readdir_r(pDir->pDir, &pDir->Data, &pResult);
            if (rc)
            {
                rc = RTErrConvertFromErrno(rc);
                /** @todo Consider translating ENOENT (The current
                 *        position of the directory stream is invalid)
                 *        differently. */
                AssertMsg(rc == VERR_FILE_NOT_FOUND, ("%Rrc\n", rc));
                return rc;
            }
            if (!pResult)
                return VERR_NO_MORE_FILES;
        }

        /*
         * Convert the filename to UTF-8.
         */
        if (!pDir->pszName)
        {
            int rc = rtPathFromNative(&pDir->pszName, pDir->Data.d_name, pDir->pszPath);
            if (RT_FAILURE(rc))
            {
                pDir->pszName = NULL;
                return rc;
            }
            pDir->cchName = strlen(pDir->pszName);
        }
        if (    !pDir->pfnFilter
            ||  pDir->pfnFilter(pDir, pDir->pszName))
            break;
        rtPathFreeIprt(pDir->pszName, pDir->Data.d_name);
        pDir->pszName     = NULL;
        pDir->fDataUnread = false;
    }

    pDir->fDataUnread = true;
    return VINF_SUCCESS;
}


#ifdef HAVE_DIRENT_D_TYPE
/**
 * Converts the d_type field to IPRT directory entry type.
 *
 * @returns IPRT directory entry type.
 * @param    Unix
 */
static RTDIRENTRYTYPE rtDirType(int iType)
{
    switch (iType)
    {
        case DT_UNKNOWN:    return RTDIRENTRYTYPE_UNKNOWN;
        case DT_FIFO:       return RTDIRENTRYTYPE_FIFO;
        case DT_CHR:        return RTDIRENTRYTYPE_DEV_CHAR;
        case DT_DIR:        return RTDIRENTRYTYPE_DIRECTORY;
        case DT_BLK:        return RTDIRENTRYTYPE_DEV_BLOCK;
        case DT_REG:        return RTDIRENTRYTYPE_FILE;
        case DT_LNK:        return RTDIRENTRYTYPE_SYMLINK;
        case DT_SOCK:       return RTDIRENTRYTYPE_SOCKET;
        case DT_WHT:        return RTDIRENTRYTYPE_WHITEOUT;
        default:
            AssertMsgFailed(("iType=%d\n", iType));
            return RTDIRENTRYTYPE_UNKNOWN;
    }
}
#endif /*HAVE_DIRENT_D_TYPE */


RTDECL(int) RTDirRead(PRTDIR pDir, PRTDIRENTRY pDirEntry, size_t *pcbDirEntry)
{
    /*
     * Validate and digest input.
     */
    if (!rtDirValidHandle(pDir))
        return VERR_INVALID_PARAMETER;
    AssertMsgReturn(VALID_PTR(pDirEntry), ("%p\n", pDirEntry), VERR_INVALID_POINTER);

    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        AssertMsgReturn(VALID_PTR(pcbDirEntry), ("%p\n", pcbDirEntry), VERR_INVALID_POINTER);
        cbDirEntry = *pcbDirEntry;
        AssertMsgReturn(cbDirEntry >= RT_UOFFSETOF(RTDIRENTRY, szName[2]),
                        ("Invalid *pcbDirEntry=%d (min %d)\n", *pcbDirEntry, RT_OFFSETOF(RTDIRENTRYEX, szName[2])),
                        VERR_INVALID_PARAMETER);
    }

    /*
     * Fetch more data if necessary and/or convert the name.
     */
    int rc = rtDirReadMore(pDir);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check if we've got enough space to return the data.
         */
        const char  *pszName    = pDir->pszName;
        const size_t cchName    = pDir->cchName;
        const size_t cbRequired = RT_OFFSETOF(RTDIRENTRY, szName[1]) + cchName;
        if (pcbDirEntry)
            *pcbDirEntry = cbRequired;
        if (cbRequired <= cbDirEntry)
        {
            /*
             * Setup the returned data.
             */
            pDirEntry->INodeId = pDir->Data.d_ino; /* may need #ifdefing later */
#ifdef HAVE_DIRENT_D_TYPE
            pDirEntry->enmType = rtDirType(pDir->Data.d_type);
#else
            pDirEntry->enmType = RTDIRENTRYTYPE_UNKNOWN;
#endif
            pDirEntry->cbName  = (uint16_t)cchName;
            Assert(pDirEntry->cbName == cchName);
            memcpy(pDirEntry->szName, pszName, cchName + 1);

            /* free cached data */
            pDir->fDataUnread  = false;
            rtPathFreeIprt(pDir->pszName, pDir->Data.d_name);
            pDir->pszName = NULL;
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }

    LogFlow(("RTDirRead(%p:{%s}, %p:{%s}, %p:{%u}): returns %Rrc\n",
             pDir, pDir->pszPath, pDirEntry, RT_SUCCESS(rc) ? pDirEntry->szName : "<failed>",
             pcbDirEntry, pcbDirEntry ? *pcbDirEntry : 0, rc));
    return rc;
}


/**
 * Fills dummy info into the info structure.
 * This function is called if we cannot stat the file.
 *
 * @param   pInfo   The struct in question.
 * @param
 */
static void rtDirSetDummyInfo(PRTFSOBJINFO pInfo, RTDIRENTRYTYPE enmType)
{
    pInfo->cbObject = 0;
    pInfo->cbAllocated = 0;
    RTTimeSpecSetNano(&pInfo->AccessTime, 0);
    RTTimeSpecSetNano(&pInfo->ModificationTime, 0);
    RTTimeSpecSetNano(&pInfo->ChangeTime, 0);
    RTTimeSpecSetNano(&pInfo->BirthTime, 0);
    memset(&pInfo->Attr, 0, sizeof(pInfo->Attr));
    pInfo->Attr.enmAdditional = RTFSOBJATTRADD_NOTHING;
    switch (enmType)
    {
        default:
        case RTDIRENTRYTYPE_UNKNOWN:    pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL;                       break;
        case RTDIRENTRYTYPE_FIFO:       pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FIFO;      break;
        case RTDIRENTRYTYPE_DEV_CHAR:   pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_DEV_CHAR;  break;
        case RTDIRENTRYTYPE_DIRECTORY:  pInfo->Attr.fMode = RTFS_DOS_DIRECTORY | RTFS_TYPE_DIRECTORY; break;
        case RTDIRENTRYTYPE_DEV_BLOCK:  pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_DEV_BLOCK; break;
        case RTDIRENTRYTYPE_FILE:       pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_FILE;      break;
        case RTDIRENTRYTYPE_SYMLINK:    pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_SYMLINK;   break;
        case RTDIRENTRYTYPE_SOCKET:     pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_SOCKET;    break;
        case RTDIRENTRYTYPE_WHITEOUT:   pInfo->Attr.fMode = RTFS_DOS_NT_NORMAL | RTFS_TYPE_WHITEOUT;  break;
    }
}


RTDECL(int) RTDirReadEx(PRTDIR pDir, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry, RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    /*
     * Validate and digest input.
     */
    if (!rtDirValidHandle(pDir))
        return VERR_INVALID_PARAMETER;
    AssertMsgReturn(VALID_PTR(pDirEntry), ("%p\n", pDirEntry), VERR_INVALID_POINTER);
    AssertMsgReturn(    enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING
                    &&  enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                    ("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        AssertMsgReturn(VALID_PTR(pcbDirEntry), ("%p\n", pcbDirEntry), VERR_INVALID_POINTER);
        cbDirEntry = *pcbDirEntry;
        AssertMsgReturn(cbDirEntry >= (unsigned)RT_OFFSETOF(RTDIRENTRYEX, szName[2]),
                        ("Invalid *pcbDirEntry=%d (min %d)\n", *pcbDirEntry, RT_OFFSETOF(RTDIRENTRYEX, szName[2])),
                        VERR_INVALID_PARAMETER);
    }

    /*
     * Fetch more data if necessary and/or convert the name.
     */
    int rc = rtDirReadMore(pDir);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check if we've got enough space to return the data.
         */
        const char  *pszName    = pDir->pszName;
        const size_t cchName    = pDir->cchName;
        const size_t cbRequired = RT_OFFSETOF(RTDIRENTRYEX, szName[1]) + cchName;
        if (pcbDirEntry)
            *pcbDirEntry = cbRequired;
        if (cbRequired <= cbDirEntry)
        {
            /*
             * Setup the returned data.
             */
            pDirEntry->cwcShortName = 0;
            pDirEntry->wszShortName[0] = 0;
            pDirEntry->cbName  = (uint16_t)cchName;
            Assert(pDirEntry->cbName == cchName);
            memcpy(pDirEntry->szName, pszName, cchName + 1);

            /* get the info data */
            size_t cch = cchName + pDir->cchPath + 1;
            char *pszNamePath = (char *)alloca(cch);
            if (pszNamePath)
            {
                memcpy(pszNamePath, pDir->pszPath, pDir->cchPath);
                memcpy(pszNamePath + pDir->cchPath, pszName, cchName + 1);
                rc = RTPathQueryInfoEx(pszNamePath, &pDirEntry->Info, enmAdditionalAttribs, fFlags);
            }
            else
                rc = VERR_NO_MEMORY;
            if (RT_FAILURE(rc))
            {
#ifdef HAVE_DIRENT_D_TYPE
                rtDirSetDummyInfo(&pDirEntry->Info, rtDirType(pDir->Data.d_type));
#else
                rtDirSetDummyInfo(&pDirEntry->Info, RTDIRENTRYTYPE_UNKNOWN);
#endif
                rc = VWRN_NO_DIRENT_INFO;
            }

            /* free cached data */
            pDir->fDataUnread  = false;
            rtPathFreeIprt(pDir->pszName, pDir->Data.d_name);
            pDir->pszName = NULL;
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }

    return rc;
}


RTDECL(int) RTDirRename(const char *pszSrc, const char *pszDst, unsigned fRename)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pszSrc), ("%p\n", pszSrc), VERR_INVALID_POINTER);
    AssertMsgReturn(VALID_PTR(pszDst), ("%p\n", pszDst), VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fRename & ~RTPATHRENAME_FLAGS_REPLACE), ("%#x\n", fRename), VERR_INVALID_PARAMETER);

    /*
     * Take common cause with RTPathRename.
     */
    int rc = rtPathPosixRename(pszSrc, pszDst, fRename, RTFS_TYPE_DIRECTORY);

    LogFlow(("RTDirRename(%p:{%s}, %p:{%s}): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, rc));
    return rc;
}

