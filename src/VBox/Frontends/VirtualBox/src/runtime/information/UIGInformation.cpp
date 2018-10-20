/* $Id$ */
/** @file
 * VBox Qt GUI - UIGInformation class implementation.
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

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
# include <QApplication>
# include <QVBoxLayout>

/* GUI includes: */
# include "UIExtraDataManager.h"
# include "UIGInformation.h"
# include "UIGInformationModel.h"
# include "UIGInformationView.h"

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */

UIGInformation::UIGInformation(QWidget *pParent)
    : QWidget(pParent)
    , m_pMainLayout(0)
    , m_pDetailsModel(0)
    , m_pDetailsView(0)
{
    /* Prepare palette: */
    preparePalette();

    /* Prepare layout: */
    prepareLayout();

    /* Prepare model: */
    prepareModel();

    /* Prepare view: */
    prepareView();

    /* Prepare connections: */
    prepareConnections();
}

void UIGInformation::setItems(const QList<UIVMItem*> &items)
{
    /* Propagate to details-model: */
    m_pDetailsModel->setItems(items);
}

void UIGInformation::preparePalette()
{
    /* Setup palette: */
    setAutoFillBackground(true);
    QPalette pal = qApp->palette();
    pal.setColor(QPalette::Window, pal.color(QPalette::Active, QPalette::Window));
    setPalette(pal);
}

void UIGInformation::prepareLayout()
{
    /* Setup main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    m_pMainLayout->setContentsMargins(2, 0, 0, 0);
    m_pMainLayout->setSpacing(0);
}

void UIGInformation::prepareModel()
{
    /* Setup details-model: */
    m_pDetailsModel = new UIGInformationModel(this);
    m_pDetailsModel->setInformationWindowElements(gEDataManager->informationWindowElements());
}

void UIGInformation::prepareView()
{
    /* Setup details-view: */
    m_pDetailsView = new UIGInformationView(this);
    m_pDetailsView->setScene(m_pDetailsModel->scene());
    m_pDetailsView->show();
    setFocusProxy(m_pDetailsView);
    m_pMainLayout->addWidget(m_pDetailsView);
}

void UIGInformation::prepareConnections()
{
    /* Setup details-model connections: */
    connect(m_pDetailsModel, SIGNAL(sigRootItemMinimumWidthHintChanged(int)),
            m_pDetailsView, SLOT(sltMinimumWidthHintChanged(int)));
    connect(m_pDetailsModel, SIGNAL(sigRootItemMinimumHeightHintChanged(int)),
            m_pDetailsView, SLOT(sltMinimumHeightHintChanged(int)));

    /* Setup details-view connections: */
    connect(m_pDetailsView, SIGNAL(sigResized()),
            m_pDetailsModel, SLOT(sltHandleViewResize()));
}

