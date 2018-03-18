/* $Id: RTSystemIsInsideVM-amd64-x86.cpp 57358 2015-08-14 15:16:38Z vboxsync $ */
/** @file
 * IPRT -
 */

/*
 * Copyright (C) 2013-2015 Oracle Corporation
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
#include "internal/iprt.h"
#include <iprt/system.h>

#include <iprt/asm-amd64-x86.h>
#include <iprt/x86.h>


RTDECL(bool) RTSystemIsInsideVM(void)
{
    if (ASMHasCpuId())
    {
        if (ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_HVP)
            return true;
    }
    return false;
}
RT_EXPORT_SYMBOL(RTSystemIsInsideVM);

