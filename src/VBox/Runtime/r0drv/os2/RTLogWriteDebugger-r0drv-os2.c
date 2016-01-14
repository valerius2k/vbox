/* $Id: RTLogWriteDebugger-r0drv-os2.c 14 2015-11-29 02:15:22Z dmik $ */
/** @file
 * IPRT - Log To Debugger, Ring-0 Driver, OS/2.
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
#include "the-os2-kernel.h"
#include "internal/iprt.h"
#include <iprt/string.h>
#include <iprt/log.h>

/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Function to print to the internal log buffer of a driver, initialized by it on init. */
int                         (*g_pfnR0Printf)(const char *pszFormat, ...) = NULL;

/* KEE */
extern uint32_t             KernKEEVersion;

/* New KEE printf */
void _System                KernPrintf(const char *fmt, ...);


RTDECL(void) RTLogWriteDebugger(const char *pch, size_t cb)
{
    char buf[512];
    int  cBuf;

    cBuf = RTStrPrintf(buf, sizeof(buf) - 1, "%.*s", cb, pch);
    buf[cBuf] = '\0';

    /*
     * Use KernPrintf when available as it has a large buffer (7 MB) and
     * fallback to a function proveided by the current drver, if any.
     */
    if ((uint32_t)&KernKEEVersion > 0x00010002)
        KernPrintf("%s", buf);
    else if (g_pfnR0Printf)
        g_pfnR0Printf("%.*s", cb, pch);
}
RT_EXPORT_SYMBOL(RTLogWriteDebugger);
