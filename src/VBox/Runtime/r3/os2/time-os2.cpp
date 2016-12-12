/* $Id$ */
/** @file
 * IPRT - Time, POSIX.
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
#define LOG_GROUP RTLOGGROUP_TIME
#include <InnoTekLIBC/FastInfoBlocks.h>

#define  INCL_LONGLONG
#define  INCL_BASE

#include <os2.h>

#include <math.h>

#include <iprt/time.h>
#include "internal/time.h"

/** @todo mscount will roll over after ~48 days. */
/* Note: define VBOX_OS2_USE_HIRES_TIMER in the Makefile.kmk for 
 * high resolution time to be used. If not defined, then
 * less precise time will be used (millisecond resolution),
 * but faster GIS-based routines will be used.
 */

RTDECL(uint64_t) RTTimeSystemNanoTS(void)
{
#if VBOX_OS2_USE_HIRES_TIMER
    ULONGLONG time = 0;
    static ULONG freq = 0;
    static bool bInitted = 0;
    double x;

    if (! bInitted)
    {
        DosTmrQueryFreq(&freq);
        bInitted = 1;
    }

    DosTmrQueryTime((PQWORD)&time);

    x = time;
    x /= freq;
    x *= 1.0e9;

    return trunc(x);
#else
    return fibGetMsCount() * UINT64_C(1000000); // 10000000
#endif
}


RTDECL(uint64_t) RTTimeSystemMilliTS(void)
{
#if VBOX_OS2_USE_HIRES_TIMER
    return RTTimeSystemNanoTS() / 1000000UL;
#else
    return fibGetMsCount();
#endif
}
