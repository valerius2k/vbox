/* $Id: UIDesktopServices_os2.cpp 3 2015-07-31 15:39:00Z dmik $ */
/** @file
 * VBox Qt GUI - Qt GUI - Utility Classes and Functions specific to OS/2...
 */

/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* VBox includes */
# include "UIDesktopServices.h"

/* Qt includes */
# include <QDir>
# include <QCoreApplication>

/* WPS includes */
# define OS2EMX_PLAIN_CHAR
# define INCL_WINWORKPLACE
# include <os2.h>

# define OPEN_DEFAULT       0
# define OPEN_DETAILS       102

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Create a VM shadow on the Desktop */
bool UIDesktopServices::createMachineShortcut(const QString &strSrcFile, const QString &strDstPath, const QString &strName, const QString &strUuid)
{
    QString   strVBox    = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QFileInfo fiVBox(strVBox);
    QFileInfo fiDst(strDstPath);
    QString   strVBoxDir = QDir::toNativeSeparators(fiVBox.absolutePath());
    QString   strDst     = QDir::toNativeSeparators(strDstPath);
    QString   strSetup   = QString("EXENAME=%1;STARTUPDIR=%2;PARAMETERS=--comment %3 "
        "--startvm %4;PROGTYPE=WINDOWABLEVIO;MINIMIZED=YES;ICONFILE=%5\\%6;")
        .arg(strVBox)
        .arg(strVBoxDir)
        .arg(strName)
        .arg(strUuid)
        .arg(strVBoxDir)
        .arg("VirtualBox.ico");

    if (WinCreateObject("WPProgram",
                        QFile::encodeName(strName).constData(),
                        QFile::encodeName(strSetup).constData(),
                        QFile::encodeName(strDst).constData(),
                        CO_UPDATEIFEXISTS))
        return true;

    return false;
}

/* Open a WPS folder with a given object */
bool UIDesktopServices::openInFileManager(const QString &strFile)
{
    QFileInfo fi(strFile);
    QString str  = QDir::toNativeSeparators(fi.absolutePath());

    HOBJECT hObj = WinQueryObject(QFile::encodeName(str).constData());

    if ((hObj != NULL) &&
        WinOpenObject(hObj, OPEN_DETAILS, FALSE))
        return true;

    return false;
}
