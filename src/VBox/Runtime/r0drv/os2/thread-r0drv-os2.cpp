/* $Id$ */
/** @file
 * IPRT - Threads (Part 1), Ring-0 Driver, OS/2.
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
#include "internal/iprt.h"
#include <iprt/thread.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mp.h>
#include "internal/thread.h"

#include <VBox/sup.h>

/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Per-cpu preemption counters. */
static int32_t volatile g_acPreemptDisabled[256];



RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    PLINFOSEG pLIS = (PLINFOSEG)RTR0Os2Virt2Flat(g_fpLIS);
    AssertReturn(pLIS, NIL_RTNATIVETHREAD);
    Assert(! RTThreadIsInInterrupt(NIL_RTTHREAD));
    return pLIS->tidCurrent | (pLIS->pidCurrent << 16);
}


static int rtR0ThreadOs2SleepCommon(RTMSINTERVAL cMillies)
{
    int rc = KernBlock((ULONG)RTThreadSleep,
                       cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies,
                       0, NULL, NULL);
    switch (rc)
    {
        case NO_ERROR:
            return VINF_SUCCESS;
        case ERROR_TIMEOUT:
            return VERR_TIMEOUT;
        case ERROR_INTERRUPT:
            return VERR_INTERRUPTED;
        default:
            AssertMsgFailed(("%d\n", rc));
            return VERR_NO_TRANSLATION;
    }
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    return rtR0ThreadOs2SleepCommon(cMillies);
}


RTDECL(int) RTThreadSleepNoBlock(RTMSINTERVAL cMillies)
{
    return rtR0ThreadOs2SleepCommon(cMillies);
}


RTDECL(bool) RTThreadYield(void)
{
    switch (RTMpOs2GetApiExt())
    {
        //case ISCS_OS4_MP:
        //    return KernYield();

        default:
            return RTR0Os2DHYield();
    }
}


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);
    int32_t c = g_acPreemptDisabled[RTMpCpuId()];
    AssertMsg(c >= 0 && c < 32, ("%d\n", c));
    return RTThreadPreemptIsPossible() && (c == 0)
        && ASMIntAreEnabled();
}


#define MAX_VCPUS 32

RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    /* array of msec counters per EMT */
    static uint32_t msecs[MAX_VCPUS] = {0};
    /* array of known EMT thread ID's */
    static RTTHREAD vcpus[MAX_VCPUS] = {0};
    uint32_t ms_prev, ms = 0;
    RTTHREAD thread = RTThreadSelf();
    bool found = false;
    int i;

    Assert(hThread == NIL_RTTHREAD);
    Assert(thread);

    for (i = 0; i < MAX_VCPUS && vcpus[i]; i++)
    {
        if (vcpus[i] == thread)
        {
            found = true;
            break;
        }
    }

    Assert(i <= MAX_VCPUS);

    /* if EMT is not found, then store its thread ID */
    if (! found)
        vcpus[i] = thread;

    ms_prev = msecs[i];
    ms = g_pGIS->msecs;
    msecs[i] = ms;

    switch (RTMpOs2GetApiExt())
    {
        //case ISCS_OS4_MP:
        //    if (KernGetReschedStatus())
        //        return true;
        //
        //    if (KernGetTCReschedStatus())
        //        return true;

        default:
            union
            {
                RTFAR16 fp;
                uint8_t fResched;
            } u;

            int rc = RTR0Os2DHQueryDOSVar(DHGETDOSV_YIELDFLAG, 0, &u.fp);
            AssertReturn(rc == 0, false);

            if (u.fResched)
                return true;

            /** @todo Check if DHGETDOSV_YIELDFLAG includes TCYIELDFLAG. */
            rc = RTR0Os2DHQueryDOSVar(DHGETDOSV_TCYIELDFLAG, 0, &u.fp);
            AssertReturn(rc == 0, false);

            if (u.fResched)
                return true;
    }

    // if milliseconds are incremented since the last call
    if (ms != ms_prev)
        return true;

    return false;
}


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* yes, RTThreadPreemptIsPending is reliable. */
    return true;
}


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    /* no kernel preemption on OS/2. */
    return false;
}


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->u32Reserved == 0);

    /* No preemption on OS/2, so do our own accounting. */
    int32_t c = ASMAtomicIncS32(&g_acPreemptDisabled[RTMpCpuId()]);
    AssertMsg(c > 0 && c < 32, ("%d\n", c));
    pState->u32Reserved = c;
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);
}


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    AssertMsg(pState->u32Reserved > 0 && pState->u32Reserved < 32, ("%d\n", pState->u32Reserved));
    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);

    /* No preemption on OS/2, so do our own accounting. */
    int32_t volatile *pc = &g_acPreemptDisabled[RTMpCpuId()];
    AssertMsg(pState->u32Reserved == (uint32_t)*pc, ("uchDummy=%d *pc=%d \n", pState->u32Reserved, *pc));
    ASMAtomicUoWriteS32(pc, pState->u32Reserved - 1);
    pState->u32Reserved = 0;
}


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);

    if (KernInterruptLevel == -1)
        return false;

    return true;
}
