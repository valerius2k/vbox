/* $Id$ */
/** @file
 * VBox Qt GUI - UIKeyboardHandlerNormal class implementation.
 */

/*
 * Copyright (C) 2010-2014 Oracle Corporation
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
# ifndef Q_WS_MAC
#  include <QMainWindow>
#  include <QMenuBar>
#  include <QKeyEvent>
#  include <QTimer>
# endif /* !Q_WS_MAC */

/* GUI includes: */
# include "UIKeyboardHandlerNormal.h"
# ifndef Q_WS_MAC
#  include "UIMachineLogic.h"
#  include "UIMachineWindow.h"
#  include "UIShortcutPool.h"
# endif /* !Q_WS_MAC */

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */


/* Namespaces: */
#ifndef Q_WS_MAC
using namespace UIExtraDataDefs;
#endif /* !Q_WS_MAC */

UIKeyboardHandlerNormal::UIKeyboardHandlerNormal(UIMachineLogic* pMachineLogic)
    : UIKeyboardHandler(pMachineLogic)
{
}

UIKeyboardHandlerNormal::~UIKeyboardHandlerNormal()
{
}

#ifndef Q_WS_MAC
bool UIKeyboardHandlerNormal::eventFilter(QObject *pWatchedObject, QEvent *pEvent)
{
    /* Check if pWatchedObject object is view: */
    if (UIMachineView *pWatchedView = isItListenedView(pWatchedObject))
    {
        /* Get corresponding screen index: */
        ulong uScreenId = m_views.key(pWatchedView);
        NOREF(uScreenId);
        /* Handle view events: */
        switch (pEvent->type())
        {
            /* We don't want this on the Mac, cause there the menu-bar isn't within the
             * window and popping up a menu there looks really ugly. */
            case QEvent::KeyPress:
            {
                /* Get key-event: */
                QKeyEvent *pKeyEvent = static_cast<QKeyEvent*>(pEvent);
                /* Process Host+Home as menu-bar activator: */
                if (isHostKeyPressed() && QKeySequence(pKeyEvent->key()) == gShortcutPool->shortcut(GUI_Input_MachineShortcuts, QString("PopupMenu")).sequence())
                {
                    /* Trying to get menu-bar: */
                    QMenuBar *pMenuBar = qobject_cast<QMainWindow*>(m_windows[uScreenId])->menuBar();
                    /* If menu-bar is present and have actions: */
                    if (pMenuBar && !pMenuBar->actions().isEmpty())
                    {
                        /* Is menu-bar visible? */
                        if (pMenuBar->isVisible())
                        {
                            /* If 'active' action is NOT chosen: */
                            if (!pMenuBar->activeAction())
                                /* Set first menu-bar action as 'active': */
                                pMenuBar->setActiveAction(pMenuBar->actions()[0]);
                            /* If 'active' action is chosen: */
                            if (pMenuBar->activeAction())
                            {
                                /* Activate 'active' menu-bar action: */
                                pMenuBar->activeAction()->activate(QAction::Trigger);
#ifdef Q_WS_WIN
                                /* Windows host needs separate 'focus set'
                                 * to let menubar operate while popped up: */
                                pMenuBar->setFocus();
#endif /* Q_WS_WIN */
                            }
                        }
                        else
                        {
                            /* Post request to show popup-menu: */
                            QTimer::singleShot(0, m_pMachineLogic, SLOT(sltInvokePopupMenu()));
                        }
                        /* Filter-out this event: */
                        return true;
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    /* Else just propagate to base-class: */
    return UIKeyboardHandler::eventFilter(pWatchedObject, pEvent);
}
#endif /* !Q_WS_MAC */

