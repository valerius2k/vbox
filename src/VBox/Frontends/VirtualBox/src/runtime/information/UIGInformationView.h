/* $Id$ */
/** @file
 * VBox Qt GUI - UIGInformationView class declaration.
 */

/*
 * Copyright (C) 2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __UIGInformationView_h__
#define __UIGInformationView_h__

/* GUI includes: */
#include "QIGraphicsView.h"

/* Graphics details-view: */
class UIGInformationView : public QIGraphicsView
{
    Q_OBJECT;

signals:

    /* Notifier: Resize stuff: */
    void sigResized();

public:

    /* Constructor: */
    UIGInformationView(QWidget *pParent);

private slots:

    /* Handlers: Size-hint stuff: */
    void sltMinimumWidthHintChanged(int iMinimumWidthHint);
    void sltMinimumHeightHintChanged(int iMinimumHeightHint);

private:

    /* Helper: Prepare stuff: */
    void preparePalette();

    /* Handler: Resize-event stuff: */
    void resizeEvent(QResizeEvent *pEvent);

    /* Helper: Update stuff: */
    void updateSceneRect();

    /* Variables: */
    int m_iMinimumWidthHint;
    int m_iMinimumHeightHint;
};

#endif /* __UIGInformationView_h__ */

