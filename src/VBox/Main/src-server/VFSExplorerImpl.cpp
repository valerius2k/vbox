/* $Id$ */
/** @file
 *
 * IVFSExplorer COM class implementations.
 */

/*
 * Copyright (C) 2009-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/file.h>
#include <iprt/s3.h>
#include <iprt/cpp/utils.h>

#include <VBox/com/array.h>

#include <VBox/param.h>
#include <VBox/version.h>

#include "VFSExplorerImpl.h"
#include "VirtualBoxImpl.h"
#include "ProgressImpl.h"

#include "AutoCaller.h"
#include "Logging.h"

#include <memory>

struct VFSExplorer::Data
{
    struct DirEntry
    {
        DirEntry(Utf8Str strName, FsObjType_T fileType, uint64_t cbSize, uint32_t fMode)
           : name(strName)
           , type(fileType)
           , size(cbSize)
           , mode(fMode) {}

        Utf8Str name;
        FsObjType_T type;
        uint64_t size;
        uint32_t mode;
    };

    VFSType_T storageType;
    Utf8Str strUsername;
    Utf8Str strPassword;
    Utf8Str strHostname;
    Utf8Str strPath;
    Utf8Str strBucket;
    std::list<DirEntry> entryList;
};


VFSExplorer::VFSExplorer()
    : mVirtualBox(NULL)
{
}

VFSExplorer::~VFSExplorer()
{
}


/**
 * VFSExplorer COM initializer.
 * @param
 * @return
 */
HRESULT VFSExplorer::init(VFSType_T aType, Utf8Str aFilePath, Utf8Str aHostname, Utf8Str aUsername,
                          Utf8Str aPassword, VirtualBox *aVirtualBox)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Weak reference to a VirtualBox object */
    unconst(mVirtualBox) = aVirtualBox;

    /* initialize data */
    m = new Data;

    m->storageType = aType;
    m->strPath = aFilePath;
    m->strHostname = aHostname;
    m->strUsername = aUsername;
    m->strPassword = aPassword;

    if (m->storageType == VFSType_S3)
    {
        size_t bpos = aFilePath.find("/", 1);
        if (bpos != Utf8Str::npos)
        {
            m->strBucket = aFilePath.substr(1, bpos - 1); /* The bucket without any slashes */
            aFilePath = aFilePath.substr(bpos); /* The rest of the file path */
        }
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * VFSExplorer COM uninitializer.
 * @return
 */
void VFSExplorer::uninit()
{
    delete m;
    m = NULL;
}

/**
 * Public method implementation.
 * @param
 * @return
 */
HRESULT VFSExplorer::getPath(com::Utf8Str &aPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aPath = m->strPath;

    return S_OK;
}


HRESULT VFSExplorer::getType(VFSType_T *aType)
{
    if (!aType)
        return E_POINTER;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aType = m->storageType;

    return S_OK;
}

struct VFSExplorer::TaskVFSExplorer
{
    enum TaskType
    {
        Update,
        Delete
    };

    TaskVFSExplorer(TaskType aTaskType, VFSExplorer *aThat, Progress *aProgress)
        : taskType(aTaskType),
          pVFSExplorer(aThat),
          progress(aProgress),
          rc(S_OK)
    {}
    ~TaskVFSExplorer() {}

    int startThread();
    static DECLCALLBACK(int) taskThread(RTTHREAD aThread, void *pvUser);
    static DECLCALLBACK(int) uploadProgress(unsigned uPercent, void *pvUser);

    TaskType taskType;
    VFSExplorer *pVFSExplorer;
    ComObjPtr<Progress> progress;
    HRESULT rc;

    /* task data */
    std::list<Utf8Str> filenames;
};

int VFSExplorer::TaskVFSExplorer::startThread()
{
    int vrc = RTThreadCreate(NULL, VFSExplorer::TaskVFSExplorer::taskThread, this,
                             0, RTTHREADTYPE_MAIN_HEAVY_WORKER, 0,
                             "Explorer::Task");

    if (RT_FAILURE(vrc))
        return VFSExplorer::setErrorStatic(E_FAIL, Utf8StrFmt("Could not create taskThreadVFS (%Rrc)\n", vrc));

    return vrc;
}

/* static */
DECLCALLBACK(int) VFSExplorer::TaskVFSExplorer::taskThread(RTTHREAD /* aThread */, void *pvUser)
{
    std::auto_ptr<TaskVFSExplorer> task(static_cast<TaskVFSExplorer*>(pvUser));
    AssertReturn(task.get(), VERR_GENERAL_FAILURE);

    VFSExplorer *pVFSExplorer = task->pVFSExplorer;

    LogFlowFuncEnter();
    LogFlowFunc(("VFSExplorer %p\n", pVFSExplorer));

    HRESULT rc = S_OK;

    switch(task->taskType)
    {
        case TaskVFSExplorer::Update:
        {
            if (pVFSExplorer->m->storageType == VFSType_File)
                rc = pVFSExplorer->i_updateFS(task.get());
            else if (pVFSExplorer->m->storageType == VFSType_S3)
#ifdef VBOX_WITH_S3
                rc = pVFSExplorer->i_updateS3(task.get());
#else
                rc = VERR_NOT_IMPLEMENTED;
#endif
            break;
        }
        case TaskVFSExplorer::Delete:
        {
            if (pVFSExplorer->m->storageType == VFSType_File)
                rc = pVFSExplorer->i_deleteFS(task.get());
            else if (pVFSExplorer->m->storageType == VFSType_S3)
#ifdef VBOX_WITH_S3
                rc = pVFSExplorer->i_deleteS3(task.get());
#else
                rc = VERR_NOT_IMPLEMENTED;
#endif
            break;
        }
        default:
            AssertMsgFailed(("Invalid task type %u specified!\n", task->taskType));
            break;
    }

    LogFlowFunc(("rc=%Rhrc\n", rc)); NOREF(rc);
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

/* static */
DECLCALLBACK(int) VFSExplorer::TaskVFSExplorer::uploadProgress(unsigned uPercent, void *pvUser)
{
    VFSExplorer::TaskVFSExplorer* pTask = *(VFSExplorer::TaskVFSExplorer**)pvUser;

    if (pTask &&
        !pTask->progress.isNull())
    {
        BOOL fCanceled;
        pTask->progress->COMGETTER(Canceled)(&fCanceled);
        if (fCanceled)
            return -1;
        pTask->progress->SetCurrentOperationProgress(uPercent);
    }
    return VINF_SUCCESS;
}

FsObjType_T VFSExplorer::i_iprtToVfsObjType(RTFMODE aType) const
{
    int a = aType & RTFS_TYPE_MASK;
    FsObjType_T t = FsObjType_Unknown;
    if ((a & RTFS_TYPE_DIRECTORY) == RTFS_TYPE_DIRECTORY)
        t = FsObjType_Directory;
    else if ((a & RTFS_TYPE_FILE) == RTFS_TYPE_FILE)
        t = FsObjType_File;
    else if ((a & RTFS_TYPE_SYMLINK) == RTFS_TYPE_SYMLINK)
        t = FsObjType_Symlink;
    else if ((a & RTFS_TYPE_FIFO) == RTFS_TYPE_FIFO)
        t = FsObjType_Fifo;
    else if ((a & RTFS_TYPE_DEV_CHAR) == RTFS_TYPE_DEV_CHAR)
        t = FsObjType_DevChar;
    else if ((a & RTFS_TYPE_DEV_BLOCK) == RTFS_TYPE_DEV_BLOCK)
        t = FsObjType_DevBlock;
    else if ((a & RTFS_TYPE_SOCKET) == RTFS_TYPE_SOCKET)
        t = FsObjType_Socket;
    else if ((a & RTFS_TYPE_WHITEOUT) == RTFS_TYPE_WHITEOUT)
        t = FsObjType_WhiteOut;

    return t;
}

HRESULT VFSExplorer::i_updateFS(TaskVFSExplorer *aTask)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    std::list<VFSExplorer::Data::DirEntry> fileList;
    char *pszPath = NULL;
    PRTDIR pDir = NULL;
    try
    {
        int vrc = RTDirOpen(&pDir, m->strPath.c_str());
        if (RT_FAILURE(vrc))
            throw setError(VBOX_E_FILE_ERROR, tr ("Can't open directory '%s' (%Rrc)"), pszPath, vrc);

        if (aTask->progress)
            aTask->progress->SetCurrentOperationProgress(33);
        RTDIRENTRYEX entry;
        while (RT_SUCCESS(vrc))
        {
            vrc = RTDirReadEx(pDir, &entry, NULL, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
            if (RT_SUCCESS(vrc))
            {
                Utf8Str name(entry.szName);
                if (   name != "."
                    && name != "..")
                    fileList.push_back(VFSExplorer::Data::DirEntry(name, i_iprtToVfsObjType(entry.Info.Attr.fMode),
                                       entry.Info.cbObject,
                                       entry.Info.Attr.fMode & (RTFS_UNIX_IRWXU | RTFS_UNIX_IRWXG | RTFS_UNIX_IRWXO)));
            }
        }
        if (aTask->progress)
            aTask->progress->SetCurrentOperationProgress(66);
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }

    /* Clean up */
    if (pszPath)
        RTStrFree(pszPath);
    if (pDir)
        RTDirClose(pDir);

    if (aTask->progress)
        aTask->progress->SetCurrentOperationProgress(99);

    /* Assign the result on success (this clears the old list) */
    if (rc == S_OK)
        m->entryList.assign(fileList.begin(), fileList.end());

    aTask->rc = rc;

    if (!aTask->progress.isNull())
        aTask->progress->i_notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

HRESULT VFSExplorer::i_deleteFS(TaskVFSExplorer *aTask)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    float fPercentStep = 100.0f / aTask->filenames.size();
    try
    {
        char szPath[RTPATH_MAX];
        std::list<Utf8Str>::const_iterator it;
        size_t i = 0;
        for (it = aTask->filenames.begin();
             it != aTask->filenames.end();
             ++it, ++i)
        {
            int vrc = RTPathJoin(szPath, sizeof(szPath), m->strPath.c_str(), (*it).c_str());
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL, tr("Internal Error (%Rrc)"), vrc);
            vrc = RTFileDelete(szPath);
            if (RT_FAILURE(vrc))
                throw setError(VBOX_E_FILE_ERROR, tr("Can't delete file '%s' (%Rrc)"), szPath, vrc);
            if (aTask->progress)
                aTask->progress->SetCurrentOperationProgress((ULONG)(fPercentStep * i));
        }
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }

    aTask->rc = rc;

    if (!aTask->progress.isNull())
        aTask->progress->i_notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_S3
HRESULT VFSExplorer::i_updateS3(TaskVFSExplorer *aTask)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    RTS3 hS3 = NULL;
    std::list<VFSExplorer::Data::DirEntry> fileList;
    try
    {
        int vrc = RTS3Create(&hS3, m->strUsername.c_str(), m->strPassword.c_str(),
                             m->strHostname.c_str(), "virtualbox-agent/" VBOX_VERSION_STRING);
        if (RT_FAILURE(vrc))
            throw setError(E_FAIL, tr ("Can't open S3 storage service (%Rrc)"), vrc);

        RTS3SetProgressCallback(hS3, VFSExplorer::TaskVFSExplorer::uploadProgress, &aTask);
        /* Do we need the list of buckets or keys? */
        if (m->strBucket.isEmpty())
        {
            PCRTS3BUCKETENTRY pBuckets = NULL;
            vrc = RTS3GetBuckets(hS3, &pBuckets);
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL, tr ("Can't get buckets (%Rrc)"), vrc);

            PCRTS3BUCKETENTRY pTmpBuckets = pBuckets;
            while (pBuckets)
            {
                /* Set always read/write permissions of the current logged in user. */
                fileList.push_back(VFSExplorer::Data::DirEntry(pBuckets->pszName, FsObjType_Directory,
                                   0, RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR));
                pBuckets = pBuckets->pNext;
            }
            RTS3BucketsDestroy(pTmpBuckets);
        }
        else
        {
            PCRTS3KEYENTRY pKeys = NULL;
            vrc = RTS3GetBucketKeys(hS3, m->strBucket.c_str(), &pKeys);
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL, tr ("Can't get keys for bucket (%Rrc)"), vrc);

            PCRTS3KEYENTRY pTmpKeys = pKeys;
            while (pKeys)
            {
                Utf8Str name(pKeys->pszName);
                /* Set always read/write permissions of the current logged in user. */
                fileList.push_back(VFSExplorer::Data::DirEntry(pKeys->pszName, FsObjType_File, pKeys->cbFile,
                                   RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR));
                pKeys = pKeys->pNext;
            }
            RTS3KeysDestroy(pTmpKeys);
        }
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }

    if (hS3 != NULL)
        RTS3Destroy(hS3);

    /* Assign the result on success (this clears the old list) */
    if (rc == S_OK)
        m->entryList.assign(fileList.begin(), fileList.end());

    aTask->rc = rc;

    if (!aTask->progress.isNull())
        aTask->progress->i_notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

HRESULT VFSExplorer::i_deleteS3(TaskVFSExplorer *aTask)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    RTS3 hS3 = NULL;
    float fPercentStep = 100.0f / aTask->filenames.size();
    try
    {
        int vrc = RTS3Create(&hS3, m->strUsername.c_str(), m->strPassword.c_str(),
                             m->strHostname.c_str(), "virtualbox-agent/" VBOX_VERSION_STRING);
        if (RT_FAILURE(vrc))
            throw setError(E_FAIL, tr ("Can't open S3 storage service (%Rrc)"), vrc);

        RTS3SetProgressCallback(hS3, VFSExplorer::TaskVFSExplorer::uploadProgress, &aTask);

        std::list<Utf8Str>::const_iterator it;
        size_t i = 0;
        for (it = aTask->filenames.begin();
             it != aTask->filenames.end();
             ++it, ++i)
        {
            vrc = RTS3DeleteKey(hS3, m->strBucket.c_str(), (*it).c_str());
            if (RT_FAILURE(vrc))
                throw setError(VBOX_E_FILE_ERROR, tr ("Can't delete file '%s' (%Rrc)"), (*it).c_str(), vrc);
            if (aTask->progress)
                aTask->progress->SetCurrentOperationProgress((ULONG)(fPercentStep * i));
        }
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }

    aTask->rc = rc;

    if (hS3 != NULL)
        RTS3Destroy(hS3);

    if (!aTask->progress.isNull())
        aTask->progress->i_notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_S3 */

HRESULT VFSExplorer::update(ComPtr<IProgress> &aProgress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    ComObjPtr<Progress> progress;
    try
    {
        Bstr progressDesc = BstrFmt(tr("Update directory info for '%s'"),
                                    m->strPath.c_str());
        /* Create the progress object */
        progress.createObject();

        rc = progress->init(mVirtualBox,
                            static_cast<IVFSExplorer*>(this),
                            progressDesc.raw(),
                            TRUE /* aCancelable */);
        if (FAILED(rc)) throw rc;

        /* Initialize our worker task */
        std::auto_ptr<TaskVFSExplorer> task(new TaskVFSExplorer(TaskVFSExplorer::Update, this, progress));

        rc = task->startThread();
        if (FAILED(rc)) throw rc;

        /* Don't destruct on success */
        task.release();
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

     if (SUCCEEDED(rc))
         /* Return progress to the caller */
         progress.queryInterfaceTo(aProgress.asOutParam());

    return rc;
}

HRESULT VFSExplorer::cd(const com::Utf8Str &aDir, ComPtr<IProgress> &aProgress)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->strPath = aDir;
    return update(aProgress);
}

HRESULT VFSExplorer::cdUp(ComPtr<IProgress> &aProgress)
{
    Utf8Str strUpPath;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        /* Remove lowest dir entry in a platform neutral way. */
        char *pszNewPath = RTStrDup(m->strPath.c_str());
        RTPathStripTrailingSlash(pszNewPath);
        RTPathStripFilename(pszNewPath);
        strUpPath = pszNewPath;
        RTStrFree(pszNewPath);
    }

    return cd(strUpPath, aProgress);
}

HRESULT VFSExplorer::entryList(std::vector<com::Utf8Str> &aNames,
                               std::vector<ULONG> &aTypes,
                               std::vector<LONG64> &aSizes,
                               std::vector<ULONG> &aModes)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aNames.resize(m->entryList.size());
    aTypes.resize(m->entryList.size());
    aSizes.resize(m->entryList.size());
    aModes.resize(m->entryList.size());

    std::list<VFSExplorer::Data::DirEntry>::const_iterator it;
    size_t i = 0;
    for (it = m->entryList.begin();
         it != m->entryList.end();
         ++it, ++i)
    {
        const VFSExplorer::Data::DirEntry &entry = (*it);
        aNames[i] = entry.name;
        aTypes[i] = entry.type;
        aSizes[i] = entry.size;
        aModes[i] = entry.mode;
    }

    return S_OK;
}

HRESULT VFSExplorer::exists(const std::vector<com::Utf8Str> &aNames,
                            std::vector<com::Utf8Str> &aExists)
{

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aExists.resize(0);
    for (size_t i=0; i < aNames.size(); ++i)
    {
        std::list<VFSExplorer::Data::DirEntry>::const_iterator it;
        for (it = m->entryList.begin();
             it != m->entryList.end();
             ++it)
        {
            const VFSExplorer::Data::DirEntry &entry = (*it);
            if (entry.name == RTPathFilename(aNames[i].c_str()))
                aExists.push_back(aNames[i]);
        }
    }

    return S_OK;
}

HRESULT VFSExplorer::remove(const std::vector<com::Utf8Str> &aNames,
                            ComPtr<IProgress> &aProgress)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    ComObjPtr<Progress> progress;
    try
    {
        /* Create the progress object */
        progress.createObject();

        rc = progress->init(mVirtualBox, static_cast<IVFSExplorer*>(this),
                            Bstr(tr("Delete files")).raw(),
                            TRUE /* aCancelable */);
        if (FAILED(rc)) throw rc;

        /* Initialize our worker task */
        std::auto_ptr<TaskVFSExplorer> task(new TaskVFSExplorer(TaskVFSExplorer::Delete, this, progress));

        /* Add all filenames to delete as task data */
        for (size_t i = 0; i < aNames.size(); ++i)
            task->filenames.push_back(aNames[i]);

        rc = task->startThread();
        if (FAILED(rc)) throw rc;

        /* Don't destruct on success */
        task.release();
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    if (SUCCEEDED(rc))
        /* Return progress to the caller */
        progress.queryInterfaceTo(aProgress.asOutParam());

    return rc;
}

