/** $Id: VBoxServiceMouse-os2.cpp 172 2018-10-20 12:27:05Z valerius $ */
/** @file
 * VBoxService - Guest Additions Mouse Service, OS/2.
 * Based on Haiku input server addon
 */

/*
 * Copyright (C) 2015-2018 Valery V. Sedletski
 * Copyright (C) 2007-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define INCL_BASE
#define INCL_PM
#define INCL_ERRORS
#include <os2.h>

#include <fcntl.h>
#include <io.h>

#include <iprt/string.h>
#include <VBox/VBoxGuestLib.h>
#include "VBoxServiceInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#pragma pack(1)
/* \DEV\XSMOUSE$ driver common event buffer */
typedef struct _evbuf
{
    unsigned short flags;  /* event flags        */
    unsigned short yPos;   /* current y-position */
    unsigned short xPos;   /* current x-position */
    unsigned short yMax;   /* max y-position     */
    unsigned short xMax;   /* max x-position     */
} evbuf_t;
#pragma pack()

#define BUTTONS_ALL_UP  0001
#define BUTTON1_DOWN    0002
#define BUTTON2_DOWN    0008
#define BUTTON3_DOWN    0020


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/* XSMOUSE$ driver handle */
static int fd = -1;
/* Exit flag              */
static bool fExiting = false;



static inline int vboxMouseAcquire()
{
    uint32_t fFeatures = 0;
    int rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);

    if (RT_SUCCESS(rc))
    {
        rc = VbglR3SetMouseStatus(fFeatures |
                                  VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE |
                                  VMMDEV_MOUSE_NEW_PROTOCOL);

        if (RT_FAILURE(rc))
        {
            VGSvcVerbose(2, "VbglR3SetMouseStatus failed. rc=%d\n", rc);
        }
    }
    else
    {
        VGSvcVerbose(2, "VbglR3GetMouseStatus failed. rc=%d\n", rc);
    }

    return rc;
}


static inline int vboxMouseRelease()
{
    uint32_t fFeatures = 0;
    int rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);

    if (RT_SUCCESS(rc))
    {
        rc = VbglR3SetMouseStatus(fFeatures & ~VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE & ~VMMDEV_MOUSE_NEW_PROTOCOL);

        if (RT_FAILURE(rc))
        {
            VGSvcVerbose(2, "VbglR3SetMouseStatus failed. rc=%d\n", rc);
        }
    }
    else
    {
        VGSvcVerbose(2, "VbglR3GetMouseStatus failed. rc=%d\n", rc);
    }

    return rc;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnPreInit}
 */
static DECLCALLBACK(int) vgsvcMouseOs2PreInit(void)
{
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnOption}
 */
static DECLCALLBACK(int) vgsvcMouseOs2Option(const char **ppszShort, int argc, char **argv, int *pi)
{
    NOREF(ppszShort);
    NOREF(argc);
    NOREF(argv);
    NOREF(pi);

    return -1;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vgsvcMouseOs2Init(void)
{
    return vboxMouseAcquire();
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vgsvcMouseOs2Worker(bool volatile *pfShutdown)
{
    int rc = VERR_GENERAL_FAILURE;
    int len;
    evbuf_t event;

    Log(("VBoxServiceMouse::%s()\n", __FUNCTION__));
    fd = open("\\dev\\xsmouse$", O_RDWR);

    if (fd < 0)
    {
        return rc;
    }

    /* The thread waits for incoming messages from the host. */
    while (! fExiting)
    {
        uint32_t cx, cy, fFeatures;
        rc = VbglR3GetMouseStatus(&fFeatures, &cx, &cy);

        if (   RT_SUCCESS(rc)
            && (fFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE))
        {
            VGSvcVerbose(2, "VBoxMouse: at %d, %d\n", cx, cy);

            event.flags = BUTTONS_ALL_UP; // ???
            event.xPos  = cx;
            event.yPos  = cy;
            event.xMax  = 65535;
            event.yMax  = 65535;

            len = sizeof(evbuf_t);

            len = write(fd, &event, len);
        }
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vgsvcMouseOs2Stop(void)
{
    Log(("VBoxMouse::%s()\n", __FUNCTION__));
    fExiting = true;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnTerm}
 */
static DECLCALLBACK(void) vgsvcMouseOs2Term(void)
{
    vboxMouseRelease();
    close(fd);
    fd = -1;
}


VBOXSERVICE g_Mouse =
{
    /* pszName. */
    "mouse",
    /* pszDescription. */
    "Mouse Input Injector",
    /* pszUsage. */
    ""
    ,
    /* pszOptions. */
    ""
    ,
    /* methods */
    vgsvcMouseOs2PreInit,
    vgsvcMouseOs2Option,
    vgsvcMouseOs2Init,
    vgsvcMouseOs2Worker,
    vgsvcMouseOs2Stop,
    vgsvcMouseOs2Term
};
