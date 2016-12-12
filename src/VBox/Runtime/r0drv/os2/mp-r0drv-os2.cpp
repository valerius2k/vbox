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
#include <iprt/asm-amd64-x86.h>
#include <iprt/log.h>

#include <VBox/sup.h>

#include "internal/iprt.h"
#include "r0drv/mp-r0drv.h"


/**
 *  Returns the multiprocessing API variant to be used
 *  0 means no multiprocessing (default)
 *
 *  ISCS_DEFAULT  == 0: no multiprocessing
 *  ISCS_ACPI_PSD == 1: use acpi.psd
 *  ISCS_OS4_BASE == 2: use BASE OS/4 KEE (no MP)
 *  ISCS_OS4_MP   == 3: use OS/4 API with MP extensions
 *
 *  @param   none
 */
RTDECL(uint8_t) RTMpOs2GetApiExt(void)
{
    static uint8_t bIsConcurrentSafe = ISCS_DEFAULT;
    static uint8_t bInitted = 0;

    /* if we got called for the 1st time */
    if (! bInitted)
    {
        APIRET rc;

        union
        {
            RTFAR16  fp;
            uint32_t ulPsdFlags;
        } u;

        /* If new KEE is supported */
        if ( (uint32_t)&KernKEEVersion > 0x00010002 )
            bIsConcurrentSafe = ISCS_OS4_BASE;

        if ( (uint32_t)&KernKEEVersion > 0x00010005 )
            bIsConcurrentSafe = ISCS_OS4_MP;

        // if (...)
        //     bIsConcurrentSafe = ISCS_ACPI_PSD;

        /* get the DHGETDOSV_PSDFLAGS GetDosVar variable */
        memset(&u, 0, sizeof(u));
        rc = RTR0Os2DHQueryDOSVar(DHGETDOSV_PSDFLAGS, 0, &u.fp);
        AssertReturn(rc == 0, 0);

        /* If PSD is not installed, turn off safety flag */
        if (! (u.ulPsdFlags & PSD_INSTALLED) && 
              bIsConcurrentSafe != ISCS_OS4_BASE )
            bIsConcurrentSafe = ISCS_DEFAULT;

#ifndef IN_GUEST
        SUPR0Printf("VBoxDrv.sys: use ApiExt: %u\n", bIsConcurrentSafe);
#endif
        bInitted = 1;
    }

    return bIsConcurrentSafe;
}


/**
 *  Returns the current processor set index
 */
RTDECL(RTCPUID) RTMpCpuId(void)
{
    switch (RTMpOs2GetApiExt())
    {
        case ISCS_OS4_MP:
            return KernGetCurrentCpuId();

        default:
            return ASMGetApicId();
    }
}


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
 *  Returns the maximum possible Cpu Id 
 *
 *  @param   none
 */
RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    static uint32_t num_cpus = 0;

    if (! num_cpus)
    {
        /* APIRET */
        int rc;
        /* number of CPU's */
        union
        {
            RTFAR16  fp;
            uint32_t ulNumCpus;
        } u;

        /* get the DHGETDOSV_TOTALCPUS GetDosVar variable */
        memset(&u, 0, sizeof(u));
        rc = RTR0Os2DHQueryDOSVar(DHGETDOSV_TOTALCPUS, 0, &u.fp);
        AssertReturn(rc == 0, 0);
        u.ulNumCpus &= 0x3f;
        Assert(0 < u.ulNumCpus);
        num_cpus = u.ulNumCpus;
    }

    return num_cpus - 1;
}


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS && idCpu <= RTMpGetMaxCpuId() ? (int)idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu <= RTMpGetMaxCpuId() ? (RTCPUID)iCpu : NIL_RTCPUID;
}


/**
 *  Returns if the CPU Id is valid
 *
 *  @param   idCpu   Cpu Id
 */
RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return idCpu <= RTMpGetMaxCpuId();
}


/**
 *  Returns if the CPU is online
 *
 *  @param   idCpu   Cpu Id
 */
RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
    /* @todo Implement checking if CPU is online */
    return idCpu <= RTMpGetMaxCpuId();
           // && !CPU_ABSENT(idCpu);
}


/**
 *  Returns total number of CPU's
 *
 *  @param   none
 */
RTDECL(RTCPUID) RTMpGetCount(void)
{
    return RTMpGetMaxCpuId() + 1;
}


/**
 *  Returns number of online CPU's
 *
 *  @param   none
 */
RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    return RTMpGetCount();
}


/**
 *  Wrapper between the native OS/2 per-cpu callback and PFNRTWORKER
 *  for the RTMpOnAll API.
 *
 *  @param   pvArg   Pointer to the RTMPARGS package.
 */
void APIENTRY rtmpOnAllOS2Wrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    pArgs->pfnWorker(RTMpCpuId(), pArgs->pvUser1, pArgs->pvUser2);
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

    switch (RTMpOs2GetApiExt())
    {
        case ISCS_OS4_MP:
            KernInvokeAtEachCpu(rtmpOnAllOS2Wrapper, &Args, IAECPU_IN_BARRIER_ON | IAECPU_OUT_BARRIER_ON);
            break;

        default:
            // @todo A real SMP implementation needs code execution on all CPU cores
            // made in sync (aka "CPU rendezvous"). We have no corresponding API
            // in kernel. So, we just trying to start the code on a current core
            // only. This is applicable for uniprocessor case only. The commented
            // line is a hint of how should a real SMP code look like.
            // See #32 for more info.

            /* We don't support concurrent execution for now */
            rtmpOnAllOS2Wrapper(&Args);
    }

    return VINF_SUCCESS;
}


/**
 * Wrapper between the native FreeBSD per-cpu callback and PFNRTWORKER
 * for the RTMpOnOthers API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
void APIENTRY rtmpOnOthersOS2Wrapper(void *pvArg)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvArg;
    RTCPUID idCpu = RTMpCpuId();
    if (pArgs->idCpu != idCpu)
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
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
    /* Will panic if no rendezvousing cpus, so check up front. */
    switch (RTMpOs2GetApiExt())
    {
        case ISCS_OS4_MP:
            if (RTMpGetOnlineCount() > 1)
            {
                RTMPARGS    Args;

                Args.pfnWorker = pfnWorker;
                Args.pvUser1 = pvUser1;
                Args.pvUser2 = pvUser2;
                Args.idCpu = RTMpCpuId();
                Args.cHits = 0;

                KernInvokeAtEachCpu(rtmpOnOthersOS2Wrapper, &Args, 
                    IAECPU_IN_BARRIER_ON | IAECPU_OUT_BARRIER_ON | IAECPU_EXCLUDECURRENT);
            }
            return VINF_SUCCESS;

        default:
            return VERR_NOT_SUPPORTED;
    }
}


/**
 * Wrapper between the native OS/2 per-cpu callback and PFNRTWORKER
 * for the RTMpOnSpecific API.
 *
 * @param   pvArg   Pointer to the RTMPARGS package.
 */
void APIENTRY rtmpOnSpecificOS2Wrapper(void *pvArg)
{
    PRTMPARGS   pArgs = (PRTMPARGS)pvArg;
    RTCPUID     idCpu = RTMpCpuId();

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
    if (! RTMpIsCpuOnline(idCpu))
        return VERR_CPU_NOT_FOUND;

    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = idCpu;
    Args.cHits = 0;

    switch (RTMpOs2GetApiExt())
    {
        case ISCS_OS4_MP:
            KernInvokeAtSpecificCpu(rtmpOnSpecificOS2Wrapper, &Args, idCpu);
            break;

        default:
            // @todo The real execution of code on specific CPU core
            // needs a CPU rendezvous API, or, at least, a ring0
            // DosSetThreadAffinity analogue. OS/2 kernel still
            // lacks them, so, we will need some kind of a hack
            // like those present in Panorama VESA or SNAP.
            // The following line commented out is just a 
            // hint of what should the real SMP aware
            // code be like. See #32 for details.

            /*  As we have no CPU affinity mask setting
             *  function for ring0 (and no CPU rendezvous
             *  implementation), we accept code execution
             *  on current CPU only, for now.
             */
            if (idCpu != RTMpCpuId())
                return VERR_CPU_NOT_FOUND;

            rtmpOnSpecificOS2Wrapper(&Args);
    }

    return Args.cHits == 1
         ? VINF_SUCCESS
         : VERR_CPU_NOT_FOUND;
}


/**
 *  Concurrent execution on all CPU cores is not safe (yet)
 */
RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    return (RTMpOs2GetApiExt() == ISCS_OS4_MP) ||
           (RTMpOs2GetApiExt() == ISCS_ACPI_PSD);
}
