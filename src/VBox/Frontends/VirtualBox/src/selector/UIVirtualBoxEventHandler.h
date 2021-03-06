/* $Id$ */
/** @file
 * VBox Qt GUI - UIVirtualBoxEventHandler class declaration.
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
 */

#ifndef ___UIVirtualBoxEventHandler_h___
#define ___UIVirtualBoxEventHandler_h___

/* Qt includes: */
#include <QObject>

/* COM includes: */
#include "COMEnums.h"
#include "CEventListener.h"


/** Active event handler singleton for the CVirtualBoxClient and CVirtualBox event-sources. */
class UIVirtualBoxEventHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about the VBoxSVC become @a fAvailable. */
    void sigVBoxSVCAvailabilityChange(bool fAvailable);

    /** Notifies about @a state change event for the machine with @a strId. */
    void sigMachineStateChange(QString strId, KMachineState state);
    /** Notifies about data change event for the machine with @a strId. */
    void sigMachineDataChange(QString strId);
    /** Notifies about machine with @a strId was @a fRegistered. */
    void sigMachineRegistered(QString strId, bool fRegistered);
    /** Notifies about @a state change event for the session of the machine with @a strId. */
    void sigSessionStateChange(QString strId, KSessionState state);
    /** Notifies about snapshot with @a strSnapshotId was taken for the machine with @a strId. */
    void sigSnapshotTake(QString strId, QString strSnapshotId);
    /** Notifies about snapshot with @a strSnapshotId was deleted for the machine with @a strId. */
    void sigSnapshotDelete(QString strId, QString strSnapshotId);
    /** Notifies about snapshot with @a strSnapshotId was changed for the machine with @a strId. */
    void sigSnapshotChange(QString strId, QString strSnapshotId);
    /** Notifies about snapshot with @a strSnapshotId was restored for the machine with @a strId. */
    void sigSnapshotRestore(QString strId, QString strSnapshotId);

public:

    /** Returns singleton instance created by the factory. */
    static UIVirtualBoxEventHandler* instance();
    /** Destroys singleton instance created by the factory. */
    static void destroy();

private:

    /** Constructor. */
    UIVirtualBoxEventHandler();
    /** Destructor. */
    ~UIVirtualBoxEventHandler();

    /** Holds the static singleton instance. */
    static UIVirtualBoxEventHandler *m_pInstance;
    /** Holds the COM event-listener instance. */
    CEventListener m_mainEventListener;
};

#define gVBoxEvents UIVirtualBoxEventHandler::instance()

#endif /* !___UIVirtualBoxEventHandler_h___ */
