/* $Id$ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, OS/2.
 */

/*
 * Copyright (C) 2008-2015 Oracle Corporation
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-os2-kernel.h"

#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include "r0drv/mp-r0drv.h"

/**
 *  Returns the current processor set index
 */
RTDECL(int) RTMpCurSetIndex(void)
{
    return RTMpCpuId();
}


/**
 *  Returns the current processor set index
 *  and current processor id
 *
 *  @param   pidCpu   Pointer to a CPU Id
 */
RTDECL(int) RTMpCurSetIndexAndId(PRTCPUID pidCpu)
{
    return *pidCpu = RTMpCpuId();
}


/**
 *  Wrapper between the native OS/2 per-cpu callback and PFNRTWORKER
 *  for the RTMpOnAll API.
 *
 *  @param   pvArg   Pointer to the RTMPARGS package.
 */
RTDECL(void) rtmpOnAllOS2Wrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    pArgs->pfnWorker(0, pArgs->pvUser1, pArgs->pvUser2);
}


/**
 *  Run code on all CPU cores.
 *
 *  @param   pfnWorker  Pointer to a per-core callback
 *  @param   pvUser1    first arg
 *  @param   pvUser2    second arg
 */
RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RTMPARGS Args;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = NIL_RTCPUID;
    Args.cHits = 0;

    // @todo A real SMP implementation needs code execution on all CPU cores
    // made in sync (aka "CPU rendezvous"). We have no corresponding API
    // in kernel. So, we just trying to start the code on a current core
    // only. This is applicable for uniprocessor case only. The commented
    // line is a hint of how should a real SMP code look like.
    // See #32 for more info.

    // smp_rendezvous(NULL, rtmpOnAllOS2Wrapper, smp_no_rendevous_barrier, &Args);

    /* We don't support concurrent execution for now */
    Assert(RTMpGetOnlineCount() == 1);
    rtmpOnAllOS2Wrapper(&Args);
    return VINF_SUCCESS;
}


/**
 *  Run code on CPU cores other than the current one.
 *
 *  @param   pfnWorker  Pointer to a per-core callback
 *  @param   pvUser1    first arg
 *  @param   pvUser2    second arg
 */
RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    NOREF(pfnWorker);
    NOREF(pvUser1);
    NOREF(pvUser2);

    // @todo We support the uniprocessor case only, for now, and
    // we don't support concurrent execution on other processors,
    // as we have only one processor supported. See #32 for more info.
    return VERR_NOT_SUPPORTED;
}


/**
 * Wrapper between the native OS/2 per-cpu callback and PFNRTWORKER
 * for the RTMpOnSpecific API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
RTDECL(void) rtmpOnSpecificOS2Wrapper(void *pvArg)
{
    PRTMPARGS   pArgs = (PRTMPARGS)pvArg;
    RTCPUID     idCpu = RTMpCpuId(); // curcpu;
    if (pArgs->idCpu == idCpu)
    {
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
        ASMAtomicIncU32(&pArgs->cHits);
    }
}


/**
 *  Run code on specific CPU core.
 *
 *  @param   idCpu      Pointer to CPU id 
 *  @param   pfnWorker  Pointer to a per-core callback
 *  @param   pvUser1    first arg
 *  @param   pvUser2    second arg
 */
RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    RTMPARGS    Args;

    /* Will panic if no rendezvousing cpus, so make sure the cpu is online. */
    if (!RTMpIsCpuOnline(idCpu))
        return VERR_CPU_NOT_FOUND;

    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = idCpu;
    Args.cHits = 0;

    // @todo The real execution of code on specific CPU core
    // needs a CPU rendezvous API, or, at least, a ring0
    // DosSetThreadAffinity analogue. OS/2 kernel still
    // lacks them, so, we will need some kind of a hack
    // like those present in Panorama VESA or SNAP.
    // The following line commented out is just a 
    // hint of what should the real SMP aware
    // code be like. See #32 for details.

    //cpumask_t Mask = (cpumask_t)1 << idCpu;
    //smp_rendezvous_cpus(Mask, NULL, rtmpOnSpecificFreeBSDWrapper, smp_no_rendevous_barrier, &Args);

    /*  As we have no CPU affinity mask setting
     *  function for ring0 (and no CPU rendezvous
     *  implementation), we accept code execution
     *  on current CPU only, for now.
     */
    if (idCpu != RTMpCpuId())
        return VERR_CPU_NOT_FOUND;

    rtmpOnSpecificOS2Wrapper(&Args);

    return Args.cHits == 1
         ? VINF_SUCCESS
         : VERR_CPU_NOT_FOUND;
}


/**
 *  Concurrent execution on all CPU cores is not safe (yet)
 */
RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    // @todo Return false for now, see #32 for details.
    return false;
}