/* $Id$ */
/** @file
 * IPRT - A mini TAR Command.
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
#include <iprt/zip.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTZIPTARCMD_OPT_DELETE              1000
#define RTZIPTARCMD_OPT_OWNER               1001
#define RTZIPTARCMD_OPT_GROUP               1002
#define RTZIPTARCMD_OPT_UTC                 1003
#define RTZIPTARCMD_OPT_PREFIX              1004
#define RTZIPTARCMD_OPT_FILE_MODE_AND_MASK  1005
#define RTZIPTARCMD_OPT_FILE_MODE_OR_MASK   1006
#define RTZIPTARCMD_OPT_DIR_MODE_AND_MASK   1007
#define RTZIPTARCMD_OPT_DIR_MODE_OR_MASK    1008
#define RTZIPTARCMD_OPT_FORMAT              1009

/** File format. */
typedef enum RTZIPTARFORMAT
{
    RTZIPTARFORMAT_INVALID = 0,
    /** Autodetect if possible, defaulting to TAR. */
    RTZIPTARFORMAT_AUTO_DEFAULT,
    /** TAR.  */
    RTZIPTARFORMAT_TAR,
    /** XAR.  */
    RTZIPTARFORMAT_XAR
} RTZIPTARFORMAT;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * IPRT TAR option structure.
 */
typedef struct RTZIPTARCMDOPS
{
    /** The file format. */
    RTZIPTARFORMAT  enmFormat;

    /** The operation (Acdrtux or RTZIPTARCMD_OPT_DELETE). */
    int             iOperation;
    /** The long operation option name. */
    const char     *pszOperation;

    /** The directory to change into when packing and unpacking. */
    const char     *pszDirectory;
    /** The tar file name. */
    const char     *pszFile;
    /** Whether we're verbose or quiet. */
    bool            fVerbose;
    /** Whether to preserve the original file owner when restoring. */
    bool            fPreserveOwner;
    /** Whether to preserve the original file group when restoring. */
    bool            fPreserveGroup;
    /** Whether to skip restoring the modification time (only time stored by the
     * traditional TAR format). */
    bool            fNoModTime;
    /** The compressor/decompressor method to employ (0, z or j). */
    char            chZipper;

    /** The owner to set. NULL if not applicable.
     * Always resolved into uidOwner for extraction. */
    const char     *pszOwner;
    /** The owner ID to set. NIL_RTUID if not applicable. */
    RTUID           uidOwner;
    /** The group to set. NULL if not applicable.
     * Always resolved into gidGroup for extraction. */
    const char     *pszGroup;
    /** The group ID to set. NIL_RTGUID if not applicable. */
    RTGID           gidGroup;
    /** Display the modification times in UTC instead of local time. */
    bool            fDisplayUtc;
    /** File mode AND mask. */
    RTFMODE         fFileModeAndMask;
    /** File mode OR mask. */
    RTFMODE         fFileModeOrMask;
    /** Directory mode AND mask. */
    RTFMODE         fDirModeAndMask;
    /** Directory mode OR mask. */
    RTFMODE         fDirModeOrMask;

    /** What to prefix all names with when creating, adding, whatever. */
    const char     *pszPrefix;

    /** The number of files(, directories or whatever) specified. */
    uint32_t        cFiles;
    /** Array of files(, directories or whatever).
     * Terminated by a NULL entry. */
    const char * const *papszFiles;
} RTZIPTARCMDOPS;
/** Pointer to the IPRT tar options. */
typedef RTZIPTARCMDOPS *PRTZIPTARCMDOPS;

/**
 * Callback used by rtZipTarDoWithMembers
 *
 * @returns rcExit or RTEXITCODE_FAILURE.
 * @param   pOpts               The tar options.
 * @param   hVfsObj             The tar object to display
 * @param   pszName             The name.
 * @param   rcExit              The current exit code.
 */
typedef RTEXITCODE (*PFNDOWITHMEMBER)(PRTZIPTARCMDOPS pOpts, RTVFSOBJ hVfsObj, const char *pszName, RTEXITCODE rcExit);


/**
 * Checks if @a pszName is a member of @a papszNames, optionally returning the
 * index.
 *
 * @returns true if the name is in the list, otherwise false.
 * @param   pszName             The name to find.
 * @param   papszNames          The array of names.
 * @param   piName              Where to optionally return the array index.
 */
static bool rtZipTarCmdIsNameInArray(const char *pszName, const char * const *papszNames, uint32_t *piName)
{
    for (uint32_t iName = 0; papszNames[iName]; iName++)
        if (!strcmp(papszNames[iName], pszName))
        {
            if (piName)
                *piName = iName;
            return true;
        }
    return false;
}


/**
 * Opens the input archive specified by the options.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + printed message.
 * @param   pOpts           The options.
 * @param   phVfsFss        Where to return the TAR filesystem stream handle.
 */
static RTEXITCODE rtZipTarCmdOpenInputArchive(PRTZIPTARCMDOPS pOpts, PRTVFSFSSTREAM phVfsFss)
{
    int rc;

    /*
     * Open the input file.
     */
    RTVFSIOSTREAM   hVfsIos;
    if (   pOpts->pszFile
        && strcmp(pOpts->pszFile, "-") != 0)
    {
        const char *pszError;
        rc = RTVfsChainOpenIoStream(pOpts->pszFile,
                                    RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN,
                                    &hVfsIos,
                                    &pszError);
        if (RT_FAILURE(rc))
        {
            if (pszError && *pszError)
                return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                      "RTVfsChainOpenIoStream failed with rc=%Rrc:\n"
                                      "    '%s'\n"
                                      "     %*s^\n",
                                      rc, pOpts->pszFile, pszError - pOpts->pszFile, "");
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                  "Failed with %Rrc opening the input archive '%s'", rc, pOpts->pszFile);
        }
    }
    else
    {
        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_INPUT,
                                      RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN,
                                      true /*fLeaveOpen*/,
                                      &hVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to prepare standard in for reading: %Rrc", rc);
    }

    /*
     * Pass it thru a decompressor?
     */
    RTVFSIOSTREAM hVfsIosDecomp = NIL_RTVFSIOSTREAM;
    switch (pOpts->chZipper)
    {
        /* no */
        case '\0':
            rc = VINF_SUCCESS;
            break;

        /* gunzip */
        case 'z':
            rc = RTZipGzipDecompressIoStream(hVfsIos, 0 /*fFlags*/, &hVfsIosDecomp);
            if (RT_FAILURE(rc))
                RTMsgError("Failed to open gzip decompressor: %Rrc", rc);
            break;

        /* bunzip2 */
        case 'j':
            rc = VERR_NOT_SUPPORTED;
            RTMsgError("bzip2 is not supported by this build");
            break;

        /* bug */
        default:
            rc = VERR_INTERNAL_ERROR_2;
            RTMsgError("unknown decompression method '%c'",  pOpts->chZipper);
            break;
    }
    if (RT_FAILURE(rc))
    {
        RTVfsIoStrmRelease(hVfsIos);
        return RTEXITCODE_FAILURE;
    }

    if (hVfsIosDecomp != NIL_RTVFSIOSTREAM)
    {
        RTVfsIoStrmRelease(hVfsIos);
        hVfsIos = hVfsIosDecomp;
        hVfsIosDecomp = NIL_RTVFSIOSTREAM;
    }

    /*
     * Open the filesystem stream.
     */
    if (pOpts->enmFormat == RTZIPTARFORMAT_TAR)
        rc = RTZipTarFsStreamFromIoStream(hVfsIos, 0/*fFlags*/, phVfsFss);
    else if (pOpts->enmFormat == RTZIPTARFORMAT_XAR)
#ifdef IPRT_WITH_XAR /* Requires C++ and XML, so only in some configruation of IPRT. */
        rc = RTZipXarFsStreamFromIoStream(hVfsIos, 0/*fFlags*/, phVfsFss);
#else
        rc = VERR_NOT_SUPPORTED;
#endif
    else /** @todo make RTZipTarFsStreamFromIoStream fail if not tar file! */
        rc = RTZipTarFsStreamFromIoStream(hVfsIos, 0/*fFlags*/, phVfsFss);
    RTVfsIoStrmRelease(hVfsIos);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to open tar filesystem stream: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


/**
 * Worker for the --list and --extract commands.
 *
 * @returns The appropriate exit code.
 * @param   pOpts               The tar options.
 * @param   pfnCallback         The command specific callback.
 */
static RTEXITCODE rtZipTarDoWithMembers(PRTZIPTARCMDOPS pOpts, PFNDOWITHMEMBER pfnCallback)
{
    /*
     * Allocate a bitmap to go with the file list.  This will be used to
     * indicate which files we've processed and which not.
     */
    uint32_t *pbmFound = NULL;
    if (pOpts->cFiles)
    {
        pbmFound = (uint32_t *)RTMemAllocZ(((pOpts->cFiles + 31) / 32) * sizeof(uint32_t));
        if (!pbmFound)
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate the found-file-bitmap");
    }


    /*
     * Open the input archive.
     */
    RTVFSFSSTREAM hVfsFssIn;
    RTEXITCODE rcExit = rtZipTarCmdOpenInputArchive(pOpts, &hVfsFssIn);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Process the stream.
         */
        for (;;)
        {
            /*
             * Retrive the next object.
             */
            char       *pszName;
            RTVFSOBJ    hVfsObj;
            int rc = RTVfsFsStrmNext(hVfsFssIn, &pszName, NULL, &hVfsObj);
            if (RT_FAILURE(rc))
            {
                if (rc != VERR_EOF)
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsFsStrmNext returned %Rrc", rc);
                break;
            }

            /*
             * Should we process this entry?
             */
            uint32_t    iFile = UINT32_MAX;
            if (   !pOpts->cFiles
                || rtZipTarCmdIsNameInArray(pszName, pOpts->papszFiles, &iFile) )
            {
                if (pbmFound)
                    ASMBitSet(pbmFound, iFile);

                rcExit = pfnCallback(pOpts, hVfsObj, pszName, rcExit);
            }

            /*
             * Release the current object and string.
             */
            RTVfsObjRelease(hVfsObj);
            RTStrFree(pszName);
        }

        /*
         * Complain about any files we didn't find.
         */
        for (uint32_t iFile = 0; iFile < pOpts->cFiles; iFile++)
            if (!ASMBitTest(pbmFound, iFile))
            {
                RTMsgError("%s: Was not found in the archive", pOpts->papszFiles[iFile]);
                rcExit = RTEXITCODE_FAILURE;
            }

        RTVfsFsStrmRelease(hVfsFssIn);
    }
    RTMemFree(pbmFound);
    return rcExit;
}


/**
 * Checks if the name contains any escape sequences.
 *
 * An escape sequence would generally be one or more '..' references.  On DOS
 * like system, something that would make up a drive letter reference is also
 * considered an escape sequence.
 *
 * @returns true / false.
 * @param   pszName     The name to consider.
 */
static bool rtZipTarHasEscapeSequence(const char *pszName)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    if (pszName[0] == ':')
        return true;
#endif
    while (*pszName)
    {
        while (RTPATH_IS_SEP(*pszName))
            pszName++;
        if (   pszName[0] == '.'
            && pszName[1] == '.'
            && (pszName[2] == '\0' || RTPATH_IS_SLASH(pszName[2])) )
            return true;
        while (*pszName && !RTPATH_IS_SEP(*pszName))
            pszName++;
    }

    return false;
}


/**
 * Queries the user ID to use when extracting a member.
 *
 * @returns rcExit or RTEXITCODE_FAILURE.
 * @param   pOpts               The tar options.
 * @param   pUser               The user info.
 * @param   pszName             The file name to use when complaining.
 * @param   rcExit              The current exit code.
 * @param   pUid                Where to return the user ID.
 */
static RTEXITCODE rtZipTarQueryExtractOwner(PRTZIPTARCMDOPS pOpts, PCRTFSOBJINFO pOwner, const char *pszName, RTEXITCODE rcExit,
                                            PRTUID pUid)
{
    if (pOpts->uidOwner != NIL_RTUID)
        *pUid = pOpts->uidOwner;
    else if (pOpts->fPreserveGroup)
    {
        if (!pOwner->Attr.u.UnixGroup.szName[0])
             *pUid = pOwner->Attr.u.UnixOwner.uid;
        else
        {
            *pUid = NIL_RTUID;
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: User resolving is not implemented.", pszName);
        }
    }
    else
        *pUid = NIL_RTUID;
    return rcExit;
}


/**
 * Queries the group ID to use when extracting a member.
 *
 * @returns rcExit or RTEXITCODE_FAILURE.
 * @param   pOpts               The tar options.
 * @param   pGroup              The group info.
 * @param   pszName             The file name to use when complaining.
 * @param   rcExit              The current exit code.
 * @param   pGid                Where to return the group ID.
 */
static RTEXITCODE rtZipTarQueryExtractGroup(PRTZIPTARCMDOPS pOpts, PCRTFSOBJINFO pGroup, const char *pszName, RTEXITCODE rcExit,
                                            PRTGID pGid)
{
    if (pOpts->gidGroup != NIL_RTGID)
        *pGid = pOpts->gidGroup;
    else if (pOpts->fPreserveGroup)
    {
        if (!pGroup->Attr.u.UnixGroup.szName[0])
            *pGid = pGroup->Attr.u.UnixGroup.gid;
        else
        {
            *pGid = NIL_RTGID;
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Group resolving is not implemented.", pszName);
        }
    }
    else
        *pGid = NIL_RTGID;
    return rcExit;
}



/**
 * Extracts a file.
 *
 * Since we can restore permissions and attributes more efficiently by working
 * directly on the file handle, we have special code path for files.
 *
 * @returns rcExit or RTEXITCODE_FAILURE.
 * @param   pOpts               The tar options.
 * @param   hVfsObj             The tar object to display
 * @param   rcExit              The current exit code.
 * @param   pUnixInfo           The unix fs object info.
 * @param   pOwner              The owner info.
 * @param   pGroup              The group info.
 */
static RTEXITCODE rtZipTarCmdExtractFile(PRTZIPTARCMDOPS pOpts, RTVFSOBJ hVfsObj, RTEXITCODE rcExit,
                                         const char *pszDst, PCRTFSOBJINFO pUnixInfo, PCRTFSOBJINFO pOwner, PCRTFSOBJINFO pGroup)
{
    /*
     * Open the destination file and create a stream object for it.
     */
    uint32_t fOpen = RTFILE_O_READWRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_ACCESS_ATTR_DEFAULT
                   | ((RTFS_UNIX_IWUSR | RTFS_UNIX_IRUSR) << RTFILE_O_CREATE_MODE_SHIFT);
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszDst, fOpen);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error creating file: %Rrc", pszDst, rc);

    RTVFSIOSTREAM hVfsIosDst;
    rc = RTVfsIoStrmFromRTFile(hFile, fOpen, true /*fLeaveOpen*/, &hVfsIosDst);
    if (RT_SUCCESS(rc))
    {
        /*
         * Pump the data thru.
         */
        RTVFSIOSTREAM hVfsIosSrc = RTVfsObjToIoStream(hVfsObj);
        rc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, (uint32_t)RT_MIN(pUnixInfo->cbObject, _1M));
        if (RT_SUCCESS(rc))
        {
            /*
             * Correct the file mode and other attributes.
             */
            if (!pOpts->fNoModTime)
            {
                rc = RTFileSetTimes(hFile, NULL, &pUnixInfo->ModificationTime, NULL, NULL);
                if (RT_FAILURE(rc))
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error setting times: %Rrc", pszDst, rc);
            }

#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
            if (   pOpts->uidOwner != NIL_RTUID
                || pOpts->gidGroup != NIL_RTGID
                || pOpts->fPreserveOwner
                || pOpts->fPreserveGroup)
            {
                RTUID uidFile;
                rcExit = rtZipTarQueryExtractOwner(pOpts, pOwner, pszDst, rcExit, &uidFile);

                RTGID gidFile;
                rcExit = rtZipTarQueryExtractGroup(pOpts, pGroup, pszDst, rcExit, &gidFile);
                if (uidFile != NIL_RTUID || gidFile != NIL_RTGID)
                {
                    rc = RTFileSetOwner(hFile, uidFile, gidFile);
                    if (RT_FAILURE(rc))
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error owner/group: %Rrc", pszDst, rc);
                }
            }
#endif

            RTFMODE fMode = (pUnixInfo->Attr.fMode & pOpts->fFileModeAndMask) | pOpts->fFileModeOrMask;
            rc = RTFileSetMode(hFile, fMode | RTFS_TYPE_FILE);
            if (RT_FAILURE(rc))
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error changing mode: %Rrc", pszDst, rc);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error writing out file: %Rrc", pszDst, rc);
        RTVfsIoStrmRelease(hVfsIosSrc);
        RTVfsIoStrmRelease(hVfsIosDst);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error creating I/O stream for file: %Rrc", pszDst, rc);
    RTFileClose(hFile);
    return rcExit;
}


/**
 * @callback_method_impl{PFNDOWITHMEMBER, Implements --extract.}
 */
static RTEXITCODE rtZipTarCmdExtractCallback(PRTZIPTARCMDOPS pOpts, RTVFSOBJ hVfsObj, const char *pszName, RTEXITCODE rcExit)
{
    if (pOpts->fVerbose)
        RTPrintf("%s\n", pszName);

    /*
     * Query all the information.
     */
    RTFSOBJINFO UnixInfo;
    int rc = RTVfsObjQueryInfo(hVfsObj, &UnixInfo, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsObjQueryInfo returned %Rrc on '%s'", rc, pszName);

    RTFSOBJINFO Owner;
    rc = RTVfsObjQueryInfo(hVfsObj, &Owner, RTFSOBJATTRADD_UNIX_OWNER);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "RTVfsObjQueryInfo(,,UNIX_OWNER) returned %Rrc on '%s'",
                              rc, pszName);

    RTFSOBJINFO Group;
    rc = RTVfsObjQueryInfo(hVfsObj, &Group, RTFSOBJATTRADD_UNIX_GROUP);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "RTVfsObjQueryInfo(,,UNIX_OWNER) returned %Rrc on '%s'",
                              rc, pszName);

    const char *pszLinkType = NULL;
    char szTarget[RTPATH_MAX];
    szTarget[0] = '\0';
    RTVFSSYMLINK hVfsSymlink = RTVfsObjToSymlink(hVfsObj);
    if (hVfsSymlink != NIL_RTVFSSYMLINK)
    {
        rc = RTVfsSymlinkRead(hVfsSymlink, szTarget, sizeof(szTarget));
        RTVfsSymlinkRelease(hVfsSymlink);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: RTVfsSymlinkRead failed: %Rrc", pszName, rc);
        if (!RTFS_IS_SYMLINK(UnixInfo.Attr.fMode))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Hardlinks are not supported.", pszName);
        if (!szTarget[0])
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Link target is empty.", pszName);
    }
    else if (RTFS_IS_SYMLINK(UnixInfo.Attr.fMode))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to get symlink object for '%s'", pszName);

    if (rtZipTarHasEscapeSequence(pszName))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Name '%s' contains an escape sequence.", pszName);

    /*
     * Construct the path to the extracted member.
     */
    char szDst[RTPATH_MAX];
    rc = RTPathJoin(szDst, sizeof(szDst), pOpts->pszDirectory ? pOpts->pszDirectory : ".", pszName);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Failed to construct destination path for: %Rrc", pszName, rc);

    /*
     * Extract according to the type.
     */
    switch (UnixInfo.Attr.fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_FILE:
            return rtZipTarCmdExtractFile(pOpts, hVfsObj, rcExit, szDst, &UnixInfo, &Owner, &Group);

        case RTFS_TYPE_DIRECTORY:
            rc = RTDirCreateFullPath(szDst, UnixInfo.Attr.fMode & RTFS_UNIX_ALL_ACCESS_PERMS);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error creating directory: %Rrc", szDst, rc);
            break;

        case RTFS_TYPE_SYMLINK:
            rc = RTSymlinkCreate(szDst, szTarget, RTSYMLINKTYPE_UNKNOWN, 0);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error creating symbolic link: %Rrc", szDst, rc);
            break;

        case RTFS_TYPE_FIFO:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: FIFOs are not supported.", pszName);
        case RTFS_TYPE_DEV_CHAR:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: FIFOs are not supported.", pszName);
        case RTFS_TYPE_DEV_BLOCK:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Block devices are not supported.", pszName);
        case RTFS_TYPE_SOCKET:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Sockets are not supported.", pszName);
        case RTFS_TYPE_WHITEOUT:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Whiteouts are not support.", pszName);
        default:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Unknown file type.", pszName);
    }

    /*
     * Set other attributes as requested  .
     *                                    .
     * Note! File extraction does get here.
     */
    if (!pOpts->fNoModTime)
    {
        rc = RTPathSetTimesEx(szDst, NULL, &UnixInfo.ModificationTime, NULL, NULL, RTPATH_F_ON_LINK);
        if (RT_FAILURE(rc) && rc != VERR_NOT_SUPPORTED && rc != VERR_NS_SYMLINK_SET_TIME)
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error changing modification time: %Rrc.", pszName, rc);
    }

#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    if (   pOpts->uidOwner != NIL_RTUID
        || pOpts->gidGroup != NIL_RTGID
        || pOpts->fPreserveOwner
        || pOpts->fPreserveGroup)
    {
        RTUID uidFile;
        rcExit = rtZipTarQueryExtractOwner(pOpts, &Owner, szDst, rcExit, &uidFile);

        RTGID gidFile;
        rcExit = rtZipTarQueryExtractGroup(pOpts, &Group, szDst, rcExit, &gidFile);
        if (uidFile != NIL_RTUID || gidFile != NIL_RTGID)
        {
            rc = RTPathSetOwnerEx(szDst, uidFile, gidFile, RTPATH_F_ON_LINK);
            if (RT_FAILURE(rc))
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error owner/group: %Rrc", szDst, rc);
        }
    }
#endif

#if !defined(RT_OS_WINDOWS) /** @todo implement RTPathSetMode on windows... */
    if (!RTFS_IS_SYMLINK(UnixInfo.Attr.fMode)) /* RTPathSetMode follows symbolic links atm. */
    {
        RTFMODE fMode;
        if (RTFS_IS_DIRECTORY(UnixInfo.Attr.fMode))
            fMode = (UnixInfo.Attr.fMode & (pOpts->fDirModeAndMask  | RTFS_TYPE_MASK)) | pOpts->fDirModeOrMask;
        else
            fMode = (UnixInfo.Attr.fMode & (pOpts->fFileModeAndMask | RTFS_TYPE_MASK)) | pOpts->fFileModeOrMask;
        rc = RTPathSetMode(szDst, fMode);
        if (RT_FAILURE(rc))
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error changing mode: %Rrc", szDst, rc);
    }
#endif

    return rcExit;
}


/**
 * @callback_method_impl{PFNDOWITHMEMBER, Implements --list.}
 */
static RTEXITCODE rtZipTarCmdListCallback(PRTZIPTARCMDOPS pOpts, RTVFSOBJ hVfsObj, const char *pszName, RTEXITCODE rcExit)
{
    /*
     * This is very simple in non-verbose mode.
     */
    if (!pOpts->fVerbose)
    {
        RTPrintf("%s\n", pszName);
        return rcExit;
    }

    /*
     * Query all the information.
     */
    RTFSOBJINFO UnixInfo;
    int rc = RTVfsObjQueryInfo(hVfsObj, &UnixInfo, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(rc))
    {
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsObjQueryInfo returned %Rrc on '%s'", rc, pszName);
        RT_ZERO(UnixInfo);
    }

    RTFSOBJINFO Owner;
    rc = RTVfsObjQueryInfo(hVfsObj, &Owner, RTFSOBJATTRADD_UNIX_OWNER);
    if (RT_FAILURE(rc))
    {
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE,
                                "RTVfsObjQueryInfo(,,UNIX_OWNER) returned %Rrc on '%s'",
                                rc, pszName);
        RT_ZERO(Owner);
    }

    RTFSOBJINFO Group;
    rc = RTVfsObjQueryInfo(hVfsObj, &Group, RTFSOBJATTRADD_UNIX_GROUP);
    if (RT_FAILURE(rc))
    {
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE,
                                "RTVfsObjQueryInfo(,,UNIX_OWNER) returned %Rrc on '%s'",
                                rc, pszName);
        RT_ZERO(Group);
    }

    const char *pszLinkType = NULL;
    char szTarget[RTPATH_MAX];
    szTarget[0] = '\0';
    RTVFSSYMLINK hVfsSymlink = RTVfsObjToSymlink(hVfsObj);
    if (hVfsSymlink != NIL_RTVFSSYMLINK)
    {
        rc = RTVfsSymlinkRead(hVfsSymlink, szTarget, sizeof(szTarget));
        if (RT_FAILURE(rc))
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsSymlinkRead returned %Rrc on '%s'", rc, pszName);
        RTVfsSymlinkRelease(hVfsSymlink);
        pszLinkType = RTFS_IS_SYMLINK(UnixInfo.Attr.fMode) ? "->" : "link to";
    }
    else if (RTFS_IS_SYMLINK(UnixInfo.Attr.fMode))
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to get symlink object for '%s'", pszName);

    /*
     * Translate the mode mask.
     */
    char szMode[16];
    switch (UnixInfo.Attr.fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_FIFO:        szMode[0] = 'f'; break;
        case RTFS_TYPE_DEV_CHAR:    szMode[0] = 'c'; break;
        case RTFS_TYPE_DIRECTORY:   szMode[0] = 'd'; break;
        case RTFS_TYPE_DEV_BLOCK:   szMode[0] = 'b'; break;
        case RTFS_TYPE_FILE:        szMode[0] = '-'; break;
        case RTFS_TYPE_SYMLINK:     szMode[0] = 'l'; break;
        case RTFS_TYPE_SOCKET:      szMode[0] = 's'; break;
        case RTFS_TYPE_WHITEOUT:    szMode[0] = 'w'; break;
        default:                    szMode[0] = '?'; break;
    }
    if (pszLinkType && szMode[0] != 's')
        szMode[0] = 'h';

    szMode[1] = UnixInfo.Attr.fMode & RTFS_UNIX_IRUSR ? 'r' : '-';
    szMode[2] = UnixInfo.Attr.fMode & RTFS_UNIX_IWUSR ? 'w' : '-';
    szMode[3] = UnixInfo.Attr.fMode & RTFS_UNIX_IXUSR ? 'x' : '-';

    szMode[4] = UnixInfo.Attr.fMode & RTFS_UNIX_IRGRP ? 'r' : '-';
    szMode[5] = UnixInfo.Attr.fMode & RTFS_UNIX_IWGRP ? 'w' : '-';
    szMode[6] = UnixInfo.Attr.fMode & RTFS_UNIX_IXGRP ? 'x' : '-';

    szMode[7] = UnixInfo.Attr.fMode & RTFS_UNIX_IROTH ? 'r' : '-';
    szMode[8] = UnixInfo.Attr.fMode & RTFS_UNIX_IWOTH ? 'w' : '-';
    szMode[9] = UnixInfo.Attr.fMode & RTFS_UNIX_IXOTH ? 'x' : '-';
    szMode[10] = '\0';

    /** @todo sticky and set-uid/gid bits. */

    /*
     * Make sure we've got valid owner and group strings.
     */
    if (!Owner.Attr.u.UnixGroup.szName[0])
        RTStrPrintf(Owner.Attr.u.UnixOwner.szName, sizeof(Owner.Attr.u.UnixOwner.szName),
                    "%u", UnixInfo.Attr.u.Unix.uid);

    if (!Group.Attr.u.UnixOwner.szName[0])
        RTStrPrintf(Group.Attr.u.UnixGroup.szName, sizeof(Group.Attr.u.UnixGroup.szName),
                    "%u", UnixInfo.Attr.u.Unix.gid);

    /*
     * Format the modification time.
     */
    char       szModTime[32];
    RTTIME     ModTime;
    PRTTIME    pTime;
    if (!pOpts->fDisplayUtc)
        pTime = RTTimeLocalExplode(&ModTime, &UnixInfo.ModificationTime);
    else
        pTime = RTTimeExplode(&ModTime, &UnixInfo.ModificationTime);
    if (!pTime)
        RT_ZERO(ModTime);
    RTStrPrintf(szModTime, sizeof(szModTime), "%04d-%02u-%02u %02u:%02u",
                ModTime.i32Year, ModTime.u8Month, ModTime.u8MonthDay, ModTime.u8Hour, ModTime.u8Minute);

    /*
     * Format the size and figure how much space is needed between the
     * user/group and the size.
     */
    char   szSize[64];
    size_t cchSize;
    switch (UnixInfo.Attr.fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_DEV_CHAR:
        case RTFS_TYPE_DEV_BLOCK:
            cchSize = RTStrPrintf(szSize, sizeof(szSize), "%u,%u",
                                  RTDEV_MAJOR(UnixInfo.Attr.u.Unix.Device), RTDEV_MINOR(UnixInfo.Attr.u.Unix.Device));
            break;
        default:
            cchSize = RTStrPrintf(szSize, sizeof(szSize), "%RU64", UnixInfo.cbObject);
            break;
    }

    size_t cchUserGroup = strlen(Owner.Attr.u.UnixOwner.szName)
                        + 1
                        + strlen(Group.Attr.u.UnixGroup.szName);
    ssize_t cchPad = cchUserGroup + cchSize + 1 < 19
                   ? 19 - (cchUserGroup + cchSize + 1)
                   : 0;

    /*
     * Go to press.
     */
    if (pszLinkType)
        RTPrintf("%s %s/%s%*s %s %s %s %s %s\n",
                 szMode,
                 Owner.Attr.u.UnixOwner.szName, Group.Attr.u.UnixGroup.szName,
                 cchPad, "",
                 szSize,
                 szModTime,
                 pszName,
                 pszLinkType,
                 szTarget);
    else
        RTPrintf("%s %s/%s%*s %s %s %s\n",
                 szMode,
                 Owner.Attr.u.UnixOwner.szName, Group.Attr.u.UnixGroup.szName,
                 cchPad, "",
                 szSize,
                 szModTime,
                 pszName);

    return rcExit;
}


/**
 * Display usage.
 *
 * @param   pszProgName         The program name.
 */
static void rtZipTarUsage(const char *pszProgName)
{
    /*
     *        0         1         2         3         4         5         6         7         8
     *        012345678901234567890123456789012345678901234567890123456789012345678901234567890
     */
    RTPrintf("Usage: %s [options]\n"
             "\n",
             pszProgName);
    RTPrintf("Operations:\n"
             "    -A, --concatenate, --catenate\n"
             "        Append the content of one tar archive to another. (not impl)\n"
             "    -c, --create\n"
             "        Create a new tar archive. (not impl)\n"
             "    -d, --diff, --compare\n"
             "        Compare atar archive with the file system. (not impl)\n"
             "    -r, --append\n"
             "        Append more files to the tar archive. (not impl)\n"
             "    -t, --list\n"
             "        List the contents of the tar archive.\n"
             "    -u, --update\n"
             "        Update the archive, adding files that are newer than the\n"
             "        ones in the archive. (not impl)\n"
             "    -x, --extract, --get\n"
             "        Extract the files from the tar archive.\n"
             "    --delete\n"
             "        Delete files from the tar archive.\n"
             "\n"
             );
    RTPrintf("Basic Options:\n"
             "    -C <dir>, --directory <dir>           (-A, -C, -d, -r, -u, -x)\n"
             "        Sets the base directory for input and output file members.\n"
             "        This does not apply to --file, even if it preceeds it.\n"
             "    -f <archive>, --file <archive>        (all)\n"
             "        The tar file to create or process. '-' indicates stdout/stdin,\n"
             "        which is is the default.\n"
             "    -v, --verbose                         (all)\n"
             "        Verbose operation.\n"
             "    -p, --preserve-permissions            (-x)\n"
             "        Preserve all permissions when extracting.  Must be used\n"
             "        before the mode mask options as it will change some of these.\n"
             "    -j, --bzip2                           (all)\n"
             "        Compress/decompress the archive with bzip2.\n"
             "    -z, --gzip, --gunzip, --ungzip        (all)\n"
             "        Compress/decompress the archive with gzip.\n"
             "\n");
    RTPrintf("Misc Options:\n"
             "    --owner <uid/username>                (-A, -C, -d, -r, -u, -x)\n"
             "        Set the owner of extracted and archived files to the user specified.\n"
             "    --group <uid/username>                (-A, -C, -d, -r, -u, -x)\n"
             "        Set the group of extracted and archived files to the group specified.\n"
             "    --utc                                 (-t)\n"
             "        Display timestamps as UTC instead of local time.\n"
             "\n");
    RTPrintf("IPRT Options:\n"
             "    --prefix <dir-prefix>                 (-A, -C, -d, -r, -u)\n"
             "        Directory prefix to give the members added to the archive.\n"
             "    --file-mode-and-mask <octal-mode>     (-A, -C, -d, -r, -u, -x)\n"
             "        Restrict the access mode of regular and special files.\n"
             "    --file-mode-and-mask <octal-mode>     (-A, -C, -d, -r, -u, -x)\n"
             "        Include the given access mode for regular and special files.\n"
             "    --dir-mode-and-mask <octal-mode>      (-A, -C, -d, -r, -u, -x)\n"
             "        Restrict the access mode of directories.\n"
             "    --dir-mode-and-mask <octal-mode>      (-A, -C, -d, -r, -u, -x)\n"
             "        Include the given access mode for directories.\n"
             "\n");
    RTPrintf("Standard Options:\n"
             "    -h, -?, --help\n"
             "        Display this help text.\n"
             "    -V, --version\n"
             "        Display version number.\n");
}


RTDECL(RTEXITCODE) RTZipTarCmd(unsigned cArgs, char **papszArgs)
{
    /*
     * Parse the command line.
     *
     * N.B. This is less flexible that your regular tar program in that it
     *      requires the operation to be specified as an option.  On the other
     *      hand, you can specify it where ever you like in the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* operations */
        { "--concatenate",          'A', RTGETOPT_REQ_NOTHING },
        { "--catenate",             'A', RTGETOPT_REQ_NOTHING },
        { "--create",               'c', RTGETOPT_REQ_NOTHING },
        { "--diff",                 'd', RTGETOPT_REQ_NOTHING },
        { "--compare",              'd', RTGETOPT_REQ_NOTHING },
        { "--append",               'r', RTGETOPT_REQ_NOTHING },
        { "--list",                 't', RTGETOPT_REQ_NOTHING },
        { "--update",               'u', RTGETOPT_REQ_NOTHING },
        { "--extract",              'x', RTGETOPT_REQ_NOTHING },
        { "--get",                  'x', RTGETOPT_REQ_NOTHING },
        { "--delete",       RTZIPTARCMD_OPT_DELETE, RTGETOPT_REQ_NOTHING },

        /* basic options */
        { "--directory",            'C', RTGETOPT_REQ_STRING },
        { "--file",                 'f', RTGETOPT_REQ_STRING },
        { "--verbose",              'v', RTGETOPT_REQ_NOTHING },
        { "--preserve-permissions", 'p', RTGETOPT_REQ_NOTHING },
        { "--bzip2",                'j', RTGETOPT_REQ_NOTHING },
        { "--gzip",                 'z', RTGETOPT_REQ_NOTHING },
        { "--gunzip",               'z', RTGETOPT_REQ_NOTHING },
        { "--ungzip",               'z', RTGETOPT_REQ_NOTHING },

        /* other options. */
        { "--owner",                RTZIPTARCMD_OPT_OWNER, RTGETOPT_REQ_STRING },
        { "--group",                RTZIPTARCMD_OPT_GROUP, RTGETOPT_REQ_STRING },
        { "--utc",                  RTZIPTARCMD_OPT_UTC,   RTGETOPT_REQ_NOTHING },

        /* IPRT extensions */
        { "--prefix",               RTZIPTARCMD_OPT_PREFIX,             RTGETOPT_REQ_STRING },
        { "--file-mode-and-mask",   RTZIPTARCMD_OPT_FILE_MODE_AND_MASK, RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_OCT },
        { "--file-mode-or-mask",    RTZIPTARCMD_OPT_FILE_MODE_OR_MASK,  RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_OCT },
        { "--dir-mode-and-mask",    RTZIPTARCMD_OPT_DIR_MODE_AND_MASK,  RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_OCT },
        { "--dir-mode-or-mask",     RTZIPTARCMD_OPT_DIR_MODE_OR_MASK,   RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_OCT },
        { "--format",               RTZIPTARCMD_OPT_FORMAT,             RTGETOPT_REQ_STRING },
    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOpt failed: %Rrc", rc);

    RTZIPTARCMDOPS Opts;
    RT_ZERO(Opts);
    Opts.enmFormat = RTZIPTARFORMAT_AUTO_DEFAULT;
    Opts.uidOwner = NIL_RTUID;
    Opts.gidGroup = NIL_RTUID;
    Opts.fFileModeAndMask = RTFS_UNIX_ALL_ACCESS_PERMS;
    Opts.fDirModeAndMask  = RTFS_UNIX_ALL_ACCESS_PERMS;
#if 0
    if (RTPermIsSuperUser())
    {
        Opts.fFileModeAndMask = RTFS_UNIX_ALL_PERMS;
        Opts.fDirModeAndMask  = RTFS_UNIX_ALL_PERMS;
        Opts.fPreserveOwner   = true;
        Opts.fPreserveGroup   = true;
    }
#endif

    RTGETOPTUNION   ValueUnion;
    while (   (rc = RTGetOpt(&GetState, &ValueUnion)) != 0
           && rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            /* operations */
            case 'A':
            case 'c':
            case 'd':
            case 'r':
            case 't':
            case 'u':
            case 'x':
            case RTZIPTARCMD_OPT_DELETE:
                if (Opts.iOperation)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Conflicting tar operation (%s already set, now %s)",
                                          Opts.pszOperation, ValueUnion.pDef->pszLong);
                Opts.iOperation   = rc;
                Opts.pszOperation = ValueUnion.pDef->pszLong;
                break;

            /* basic options */
            case 'C':
                if (Opts.pszDirectory)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "You may only specify -C/--directory once");
                Opts.pszDirectory = ValueUnion.psz;
                break;

            case 'f':
                if (Opts.pszFile)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "You may only specify -f/--file once");
                Opts.pszFile = ValueUnion.psz;
                break;

            case 'v':
                Opts.fVerbose = true;
                break;

            case 'p':
                Opts.fFileModeAndMask   = RTFS_UNIX_ALL_PERMS;
                Opts.fDirModeAndMask    = RTFS_UNIX_ALL_PERMS;
                Opts.fPreserveOwner     = true;
                Opts.fPreserveGroup     = true;
                break;

            case 'j':
            case 'z':
                if (Opts.chZipper)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "You may only specify one compressor / decompressor");
                Opts.chZipper = rc;
                break;

            case RTZIPTARCMD_OPT_OWNER:
                if (Opts.pszOwner)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "You may only specify --owner once");
                Opts.pszOwner = ValueUnion.psz;

                rc = RTStrToUInt32Full(Opts.pszOwner, 0, &ValueUnion.u32);
                if (RT_SUCCESS(rc) && rc != VINF_SUCCESS)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                          "Error convering --owner '%s' into a number: %Rrc", Opts.pszOwner, rc);
                if (RT_SUCCESS(rc))
                {
                    Opts.uidOwner = ValueUnion.u32;
                    Opts.pszOwner = NULL;
                }
                break;

            case RTZIPTARCMD_OPT_GROUP:
                if (Opts.pszGroup)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "You may only specify --group once");
                Opts.pszGroup = ValueUnion.psz;

                rc = RTStrToUInt32Full(Opts.pszGroup, 0, &ValueUnion.u32);
                if (RT_SUCCESS(rc) && rc != VINF_SUCCESS)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                          "Error convering --group '%s' into a number: %Rrc", Opts.pszGroup, rc);
                if (RT_SUCCESS(rc))
                {
                    Opts.gidGroup = ValueUnion.u32;
                    Opts.pszGroup = NULL;
                }
                break;

            case RTZIPTARCMD_OPT_UTC:
                Opts.fDisplayUtc = true;
                break;

            /* iprt extensions */
            case RTZIPTARCMD_OPT_PREFIX:
                if (Opts.pszPrefix)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "You may only specify --prefix once");
                Opts.pszPrefix = ValueUnion.psz;
                break;

            case RTZIPTARCMD_OPT_FILE_MODE_AND_MASK:
                Opts.fFileModeAndMask = ValueUnion.u32 & RTFS_UNIX_ALL_PERMS;
                break;

            case RTZIPTARCMD_OPT_FILE_MODE_OR_MASK:
                Opts.fFileModeOrMask  = ValueUnion.u32 & RTFS_UNIX_ALL_PERMS;
                break;

            case RTZIPTARCMD_OPT_DIR_MODE_AND_MASK:
                Opts.fDirModeAndMask  = ValueUnion.u32 & RTFS_UNIX_ALL_PERMS;
                break;

            case RTZIPTARCMD_OPT_DIR_MODE_OR_MASK:
                Opts.fDirModeOrMask   = ValueUnion.u32 & RTFS_UNIX_ALL_PERMS;
                break;

            case RTZIPTARCMD_OPT_FORMAT:
                if (!strcmp(ValueUnion.psz, "auto") || !strcmp(ValueUnion.psz, "default"))
                    Opts.enmFormat = RTZIPTARFORMAT_AUTO_DEFAULT;
                else if (!strcmp(ValueUnion.psz, "tar"))
                    Opts.enmFormat = RTZIPTARFORMAT_TAR;
                else if (!strcmp(ValueUnion.psz, "xar"))
                    Opts.enmFormat = RTZIPTARFORMAT_XAR;
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown archive format: '%s'", ValueUnion.psz);
                break;

            /* Standard bits. */
            case 'h':
                rtZipTarUsage(RTPathFilename(papszArgs[0]));
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (rc == VINF_GETOPT_NOT_OPTION)
    {
        /* this is kind of ugly. */
        Assert((unsigned)GetState.iNext - 1 <= cArgs);
        Opts.papszFiles = (const char * const *)&papszArgs[GetState.iNext - 1];
        Opts.cFiles     = cArgs - GetState.iNext + 1;
    }

    /*
     * Post proceess the options.
     */
    if (Opts.iOperation == 0)
    {
        Opts.iOperation   = 't';
        Opts.pszOperation = "--list";
    }

    if (   Opts.iOperation == 'x'
        && Opts.pszOwner)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The use of --owner with %s has not implemented yet", Opts.pszOperation);

    if (   Opts.iOperation == 'x'
        && Opts.pszGroup)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The use of --group with %s has not implemented yet", Opts.pszOperation);

    /*
     * Do the job.
     */
    switch (Opts.iOperation)
    {
        case 't':
            return rtZipTarDoWithMembers(&Opts, rtZipTarCmdListCallback);

        case 'x':
            return rtZipTarDoWithMembers(&Opts, rtZipTarCmdExtractCallback);

        case 'A':
        case 'c':
        case 'd':
        case 'r':
        case 'u':
        case RTZIPTARCMD_OPT_DELETE:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "The operation %s is not implemented yet", Opts.pszOperation);

        default:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Internal error");
    }
}

