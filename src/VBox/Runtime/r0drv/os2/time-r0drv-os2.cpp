/* $Id$ */
/** @file
 * IPRT - Time, Ring-0 Driver, OS/2.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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
#include "the-os2-kernel.h"

#include <iprt/time.h>
#include <iprt/mp.h>


/* Note: define VBOX_OS2_USE_HIRES_TIMER in the Makefile.kmk for 
 * high resolution time to be used. If not defined, then
 * less precise time will be used (millisecond resolution),
 * but faster GIS-based routines will be used.
 */

RTDECL(uint64_t) RTTimeNanoTS(void)
{
    switch (RTMpOs2GetApiExt())
    {
        case ISCS_OS4_MP:
            struct timespec time;
            KernClockMonotonicGetTime(&time);
            return time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;

        default:
            /** @remark OS/2 Ring-0: will wrap after 48 days. */
            return g_pGIS->msecs * UINT64_C(1000000);
    }
}


RTDECL(uint64_t) RTTimeMilliTS(void)
{
#if VBOX_OS2_USE_HIRES_TIMER
    return RTTimeNanoTS() / UINT64_C(1000000);
#else
    /** @remark OS/2 Ring-0: will wrap after 48 days. */
    return g_pGIS->msecs;
#endif
}


RTDECL(uint64_t) RTTimeSystemNanoTS(void)
{
#if VBOX_OS2_USE_HIRES_TIMER
    return RTTimeNanoTS();
#else
    /** @remark OS/2 Ring-0: will wrap after 48 days. */
    return g_pGIS->msecs * UINT64_C(1000000);
#endif
}


RTDECL(uint64_t) RTTimeSystemMilliTS(void)
{
#if VBOX_OS2_USE_HIRES_TIMER
    return RTTimeMilliTS();
#else
    /** @remark OS/2 Ring-0: will wrap after 48 days. */
    return g_pGIS->msecs;
#endif
}


RTDECL(PRTTIMESPEC) RTTimeNow(PRTTIMESPEC pTime)
{
    /*
     * Get the seconds since the unix epoch (local time) and current hundredths.
     */
    GINFOSEG volatile *pGIS = (GINFOSEG volatile *)g_pGIS;
    UCHAR uchHundredths;
    ULONG ulSeconds;
    do
    {
        uchHundredths = pGIS->hundredths;
        ulSeconds = pGIS->time;
    } while (   uchHundredths  == pGIS->hundredths
             && ulSeconds      == pGIS->time);

    /*
     * Combine the two and convert to UCT (later).
     */
    uint64_t u64 = ulSeconds * UINT64_C(1000000000) + (uint32_t)uchHundredths * 10000000;
    /** @todo convert from local to UCT. */

    /** @remark OS/2 Ring-0: Currently returns local time instead of UCT. */
    return RTTimeSpecSetNano(pTime, u64);
}
