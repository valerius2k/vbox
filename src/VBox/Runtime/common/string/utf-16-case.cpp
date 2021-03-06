/* $Id$ */
/** @file
 * IPRT - UTF-16, Case Sensitivity.
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/uni.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include "internal/string.h"


RTDECL(int) RTUtf16ICmp(register PCRTUTF16 pwsz1, register PCRTUTF16 pwsz2)
{
    if (pwsz1 == pwsz2)
        return 0;
    if (!pwsz1)
        return -1;
    if (!pwsz2)
        return 1;

    PCRTUTF16 pwsz1Start = pwsz1; /* keep it around in case we have to backtrack on a surrogate pair */
    for (;;)
    {
        register RTUTF16  wc1 = *pwsz1;
        register RTUTF16  wc2 = *pwsz2;
        register int     iDiff = wc1 - wc2;
        if (iDiff)
        {
            /* unless they are *both* surrogate pairs, there is no chance they'll be identical. */
            if (    wc1 < 0xd800
                ||  wc2 < 0xd800
                ||  wc1 > 0xdfff
                ||  wc2 > 0xdfff)
            {
                /* simple UCS-2 char */
                iDiff = RTUniCpToUpper(wc1) - RTUniCpToUpper(wc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(wc1) - RTUniCpToLower(wc2);
            }
            else
            {
                /* a damned pair */
                RTUNICP uc1;
                RTUNICP uc2;
                if (wc1 >= 0xdc00)
                {
                    if (pwsz1Start == pwsz1)
                        return iDiff;
                    uc1 = pwsz1[-1];
                    if (uc1 < 0xd800 || uc1 >= 0xdc00)
                        return iDiff;
                    uc1 = 0x10000 + (((uc1       & 0x3ff) << 10) | (wc1 & 0x3ff));
                    uc2 = 0x10000 + (((pwsz2[-1] & 0x3ff) << 10) | (wc2 & 0x3ff));
                }
                else
                {
                    uc1 = *++pwsz1;
                    if (uc1 < 0xdc00 || uc1 >= 0xe000)
                        return iDiff;
                    uc1 = 0x10000 + (((wc1 & 0x3ff) << 10) | (uc1      & 0x3ff));
                    uc2 = 0x10000 + (((wc2 & 0x3ff) << 10) | (*++pwsz2 & 0x3ff));
                }
                iDiff = RTUniCpToUpper(uc1) - RTUniCpToUpper(uc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(uc1) - RTUniCpToLower(uc2); /* serious paranoia! */
            }
            if (iDiff)
                return iDiff;
        }
        if (!wc1)
            return 0;
        pwsz1++;
        pwsz2++;
    }
}
RT_EXPORT_SYMBOL(RTUtf16ICmp);


RTDECL(PRTUTF16) RTUtf16ToLower(PRTUTF16 pwsz)
{
    PRTUTF16 pwc = pwsz;
    for (;;)
    {
        RTUTF16 wc = *pwc;
        if (!wc)
            break;
        if (wc < 0xd800 || wc >= 0xdc00)
        {
            RTUNICP ucFolded = RTUniCpToLower(wc);
            if (ucFolded < 0x10000)
                *pwc++ = RTUniCpToLower(wc);
        }
        else
        {
            /* surrogate */
            RTUTF16 wc2 = pwc[1];
            if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
            {
                RTUNICP uc = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                RTUNICP ucFolded = RTUniCpToLower(uc);
                if (uc != ucFolded && ucFolded >= 0x10000) /* we don't support shrinking the string */
                {
                    uc -= 0x10000;
                    *pwc++ = 0xd800 | (uc >> 10);
                    *pwc++ = 0xdc00 | (uc & 0x3ff);
                }
            }
            else /* invalid encoding. */
                pwc++;
        }
    }
    return pwsz;
}
RT_EXPORT_SYMBOL(RTUtf16ToLower);


RTDECL(PRTUTF16) RTUtf16ToUpper(PRTUTF16 pwsz)
{
    PRTUTF16 pwc = pwsz;
    for (;;)
    {
        RTUTF16 wc = *pwc;
        if (!wc)
            break;
        if (wc < 0xd800 || wc >= 0xdc00)
            *pwc++ = RTUniCpToUpper(wc);
        else
        {
            /* surrogate */
            RTUTF16 wc2 = pwc[1];
            if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
            {
                RTUNICP uc = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                RTUNICP ucFolded = RTUniCpToUpper(uc);
                if (uc != ucFolded && ucFolded >= 0x10000) /* we don't support shrinking the string */
                {
                    uc -= 0x10000;
                    *pwc++ = 0xd800 | (uc >> 10);
                    *pwc++ = 0xdc00 | (uc & 0x3ff);
                }
            }
            else /* invalid encoding. */
                pwc++;
        }
    }
    return pwsz;
}
RT_EXPORT_SYMBOL(RTUtf16ToUpper);

